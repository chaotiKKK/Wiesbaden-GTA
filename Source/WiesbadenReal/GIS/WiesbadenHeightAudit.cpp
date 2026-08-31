// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/WiesbadenHeightAudit.h"

#include "WiesbadenReal.h"

#include "GIS/RoadNetworkTypes.h"

#include "Landscape.h"
#include "LandscapeProxy.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FString FHeightAuditReport::ToString() const
{
	if (!bSuccess)
	{
		return FString::Printf(TEXT("Hoehenpruefung FEHLGESCHLAGEN: %s"), *ErrorMessage);
	}

	return FString::Printf(
		TEXT("Hoehenpruefung: %d Kreuzungen geprueft (%d ohne Gelaendedaten). ")
		TEXT("%d schweben, %d liegen begraben. Mittlere Abweichung %.0f cm, ")
		TEXT("schlimmster Fall %.0f cm in der Luft / %.0f cm im Boden."),
		CheckedCount, WithoutTerrainCount, FloatingCount, BuriedCount,
		MeanAbsDeltaCm, WorstFloatingCm, WorstBuriedCm);
}

FHeightAuditReport FWiesbadenHeightAudit::Run(
	const FRoadNetwork& Network,
	const TArray<ALandscapeProxy*>& Proxies,
	double ToleranceCm,
	int32 MaxWorst)
{
	FHeightAuditReport Report;

	if (Network.Intersections.Num() == 0)
	{
		Report.ErrorMessage = TEXT("Strassennetz enthaelt keine Kreuzungen.");
		return Report;
	}
	if (Proxies.Num() == 0)
	{
		Report.ErrorMessage = TEXT("Kein Landscape in der Welt - ohne Gelaende keine Aussage.");
		return Report;
	}

	// Strassennamen je Kreuzung ueber die KNOTEN-IDS, die jedes Segment
	// mitfuehrt (StartNodeId / EndNodeId).
	//
	// Der erste Entwurf verglich stattdessen Koordinaten - jedes Segment
	// gegen jede Kreuzung. Das waeren 125.024 x 22.227 Vergleiche gewesen,
	// also 2,8 Milliarden, fuer eine reine Beschriftung. Ueber die Ids ist es
	// ein Durchlauf.
	TMap<int64, FString> NameByNode;
	for (const FRoadSegment& Segment : Network.Segments)
	{
		if (Segment.StreetName.IsEmpty())
		{
			continue;
		}
		NameByNode.FindOrAdd(Segment.StartNodeId, Segment.StreetName);
		NameByNode.FindOrAdd(Segment.EndNodeId, Segment.StreetName);
	}

	// Umschliessende Flaeche des Gelaendes bestimmen.
	//
	// Noetig, weil GetHeightAtLocation AUCH ausserhalb noch antwortet - mit
	// dem geklemmten Randwert. Der erste Durchlauf hat genau das gezeigt:
	// die vierzig schlimmsten Faelle meldeten alle exakt 25.599 cm
	// Gelaendehoehe, an Stellen elf Kilometer auseinander. Das war kein
	// Gelaende, das war ein Anschlag.
	// Zahl der Gelaendeflaechen MELDEN.
	//
	// Liegt mehr als eine in der Welt, misst die Schleife unten gegen die
	// erste, die antwortet - und das kann die falsche sein. Genau so hat
	// ein Durchlauf zweimal denselben alten Bestand gemessen, weil das
	// Schmierlevel nicht angelegt worden war und noch das Gelaende der
	// Spielkarte in der Welt lag.
	int32 ValidProxyCount = 0;
	for (const ALandscapeProxy* Proxy : Proxies)
	{
		if (Proxy)
		{
			++ValidProxyCount;
		}
	}
	if (ValidProxyCount != 1)
	{
		UE_LOG(LogWbRoads, Warning,
			TEXT("Hoehenpruefung: %d Gelaendeflaechen in der Welt. Gemessen wird ")
			TEXT("gegen die erste, die antwortet - bei mehr als einer ist das ")
			TEXT("Ergebnis nicht eindeutig."),
			ValidProxyCount);
	}

	FBox TerrainBounds(ForceInit);
	for (const ALandscapeProxy* Proxy : Proxies)
	{
		if (Proxy)
		{
			TerrainBounds += Proxy->GetComponentsBoundingBox(true);
		}
	}
	Report.TerrainBounds = TerrainBounds;

	// Verteilungsstufen in cm; die letzte Zelle sammelt alles darueber.
	static const double HistogramEdgesCm[] = { 25.0, 50.0, 100.0, 200.0, 500.0, 1000.0 };
	constexpr int32 HistogramBuckets = UE_ARRAY_COUNT(HistogramEdgesCm) + 1;
	Report.HistogramInside.Init(0, HistogramBuckets);

	double SumAbs = 0.0;
	double SumAbsInside = 0.0;
	TArray<FHeightMismatch> All;
	All.Reserve(Network.Intersections.Num());

	for (const FRoadIntersection& Node : Network.Intersections)
	{
		// Gelaendehoehe aus den Landscape-Daten, KEIN Trace: ein Trace traefe
		// die Fahrbahn selbst und meldete immer Abweichung null.
		TOptional<float> Height;
		for (ALandscapeProxy* Proxy : Proxies)
		{
			if (!Proxy)
			{
				continue;
			}
			Height = Proxy->GetHeightAtLocation(Node.Location, EHeightfieldSource::Complex);
			if (Height.IsSet())
			{
				break;
			}
		}

		if (!Height.IsSet())
		{
			++Report.WithoutTerrainCount;
			continue;
		}

		++Report.CheckedCount;

		FHeightMismatch Entry;
		Entry.Location = Node.Location;
		Entry.RoadZ = Node.Location.Z;
		Entry.TerrainZ = *Height;
		Entry.DeltaCm = Entry.RoadZ - Entry.TerrainZ;
		Entry.StreetName = NameByNode.FindRef(Node.NodeId);

		SumAbs += FMath::Abs(Entry.DeltaCm);

		if (Entry.DeltaCm > ToleranceCm)
		{
			++Report.FloatingCount;
			Report.WorstFloatingCm = FMath::Max(Report.WorstFloatingCm, Entry.DeltaCm);
		}
		else if (Entry.DeltaCm < -ToleranceCm)
		{
			++Report.BuriedCount;
			Report.WorstBuriedCm = FMath::Max(Report.WorstBuriedCm, -Entry.DeltaCm);
		}

		// Liegt die Kreuzung ueberhaupt auf Gelaende? Nur in der Ebene
		// pruefen - die Hoehe ist ja genau das Gesuchte.
		const bool bInside = TerrainBounds.IsValid != 0
			&& Node.Location.X >= TerrainBounds.Min.X && Node.Location.X <= TerrainBounds.Max.X
			&& Node.Location.Y >= TerrainBounds.Min.Y && Node.Location.Y <= TerrainBounds.Max.Y;

		if (!bInside)
		{
			++Report.OutsideTerrainCount;
		}
		else
		{
			++Report.CheckedInsideCount;
			const double AbsDelta = FMath::Abs(Entry.DeltaCm);
			SumAbsInside += AbsDelta;

			if (Entry.DeltaCm > ToleranceCm)
			{
				++Report.FloatingInsideCount;
			}
			else if (Entry.DeltaCm < -ToleranceCm)
			{
				++Report.BuriedInsideCount;
			}

			int32 Bucket = HistogramBuckets - 1;
			for (int32 Edge = 0; Edge < UE_ARRAY_COUNT(HistogramEdgesCm); ++Edge)
			{
				if (AbsDelta < HistogramEdgesCm[Edge])
				{
					Bucket = Edge;
					break;
				}
			}
			++Report.HistogramInside[Bucket];
		}

		All.Add(Entry);
	}

	if (Report.CheckedCount > 0)
	{
		Report.MeanAbsDeltaCm = SumAbs / Report.CheckedCount;
	}
	if (Report.CheckedInsideCount > 0)
	{
		Report.MeanAbsDeltaInsideCm = SumAbsInside / Report.CheckedInsideCount;
	}

	All.Sort([](const FHeightMismatch& A, const FHeightMismatch& B)
	{
		return FMath::Abs(A.DeltaCm) > FMath::Abs(B.DeltaCm);
	});

	const int32 Count = FMath::Min(FMath::Max(MaxWorst, 0), All.Num());
	Report.Worst.Append(All.GetData(), Count);

	Report.bSuccess = true;
	return Report;
}

bool FWiesbadenHeightAudit::WriteJson(const FHeightAuditReport& Report, const FString& Path)
{
	FString Json;
	Json += TEXT("{\n");
	Json += FString::Printf(TEXT("  \"geprueft\": %d,\n"), Report.CheckedCount);
	Json += FString::Printf(TEXT("  \"ohneGelaende\": %d,\n"), Report.WithoutTerrainCount);
	Json += FString::Printf(TEXT("  \"schwebend\": %d,\n"), Report.FloatingCount);
	Json += FString::Printf(TEXT("  \"begraben\": %d,\n"), Report.BuriedCount);
	Json += FString::Printf(TEXT("  \"mittlereAbweichungCm\": %.1f,\n"), Report.MeanAbsDeltaCm);
	Json += FString::Printf(TEXT("  \"schlimmsteLuftCm\": %.1f,\n"), Report.WorstFloatingCm);
	Json += FString::Printf(TEXT("  \"schlimmsteBodenCm\": %.1f,\n"), Report.WorstBuriedCm);
	Json += FString::Printf(TEXT("  \"ausserhalbGelaende\": %d,\n"), Report.OutsideTerrainCount);
	Json += FString::Printf(TEXT("  \"innenGeprueft\": %d,\n"), Report.CheckedInsideCount);
	Json += FString::Printf(TEXT("  \"innenSchwebend\": %d,\n"), Report.FloatingInsideCount);
	Json += FString::Printf(TEXT("  \"innenBegraben\": %d,\n"), Report.BuriedInsideCount);
	Json += FString::Printf(TEXT("  \"innenMittlereAbweichungCm\": %.1f,\n"),
		Report.MeanAbsDeltaInsideCm);
	if (Report.TerrainBounds.IsValid != 0)
	{
		Json += FString::Printf(
			TEXT("  \"gelaendeFlaeche\": { \"minX\": %.0f, \"minY\": %.0f, ")
			TEXT("\"maxX\": %.0f, \"maxY\": %.0f },\n"),
			Report.TerrainBounds.Min.X, Report.TerrainBounds.Min.Y,
			Report.TerrainBounds.Max.X, Report.TerrainBounds.Max.Y);
	}
	{
		// Stufen: <25, <50, <100, <200, <500, <1000, >=1000 cm.
		static const TCHAR* Labels[] = {
			TEXT("bis25"), TEXT("bis50"), TEXT("bis100"), TEXT("bis200"),
			TEXT("bis500"), TEXT("bis1000"), TEXT("ueber1000") };
		Json += TEXT("  \"innenVerteilungCm\": { ");
		for (int32 i = 0; i < Report.HistogramInside.Num()
			&& i < UE_ARRAY_COUNT(Labels); ++i)
		{
			Json += FString::Printf(TEXT("%s\"%s\": %d"),
				i > 0 ? TEXT(", ") : TEXT(""), Labels[i], Report.HistogramInside[i]);
		}
		Json += TEXT(" },\n");
	}
	Json += TEXT("  \"schlimmsteFaelle\": [\n");

	for (int32 i = 0; i < Report.Worst.Num(); ++i)
	{
		const FHeightMismatch& W = Report.Worst[i];
		Json += FString::Printf(
			TEXT("    {\"strasse\": \"%s\", \"x\": %.0f, \"y\": %.0f, ")
			TEXT("\"fahrbahnZ\": %.0f, \"gelaendeZ\": %.0f, \"deltaCm\": %.0f}%s\n"),
			*W.StreetName.ReplaceCharWithEscapedChar(),
			W.Location.X, W.Location.Y, W.RoadZ, W.TerrainZ, W.DeltaCm,
			(i + 1 < Report.Worst.Num()) ? TEXT(",") : TEXT(""));
	}

	Json += TEXT("  ]\n}\n");

	const FString Full = FPaths::ConvertRelativePathToFull(Path);
	if (!FFileHelper::SaveStringToFile(Json, *Full))
	{
		UE_LOG(LogWbRoads, Warning, TEXT("Hoehenbericht konnte nicht geschrieben werden: %s"), *Full);
		return false;
	}

	UE_LOG(LogWbRoads, Log, TEXT("Hoehenbericht geschrieben: %s"), *Full);
	return true;
}
