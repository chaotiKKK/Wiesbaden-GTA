// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/WiesbadenRoadClearance.h"

#include "GIS/RoadNetworkTypes.h"

double FWiesbadenRoadClearance::DistanceToSegmentSquared2D(
	const FVector2D& Point, const FVector2D& Start, const FVector2D& End)
{
	const FVector2D Axis = End - Start;
	const double LengthSq = Axis.SizeSquared();
	if (LengthSq < KINDA_SMALL_NUMBER)
	{
		return FVector2D::DistSquared(Point, Start);
	}

	// Lotfusspunkt auf die Strecke, auf [0,1] geklemmt: ausserhalb der
	// Strecke zaehlt der Abstand zum naeheren Endpunkt.
	const double T = FMath::Clamp(FVector2D::DotProduct(Point - Start, Axis) / LengthSq, 0.0, 1.0);
	const FVector2D Foot = Start + Axis * T;
	return FVector2D::DistSquared(Point, Foot);
}

FIntPoint FWiesbadenRoadClearance::CellOf(const FVector2D& Point)
{
	return FIntPoint(
		FMath::FloorToInt(Point.X / CellSizeCm),
		FMath::FloorToInt(Point.Y / CellSizeCm));
}

void FWiesbadenRoadClearance::Build(const FRoadNetwork& Network, double ExtraMarginCm,
	bool bIncludeSidewalk)
{
	BuildInternal(Network, ExtraMarginCm, bIncludeSidewalk,
		/*bLimitToArea=*/false, FVector2D::ZeroVector, 0.0);
}

void FWiesbadenRoadClearance::BuildAround(const FRoadNetwork& Network,
	const FVector2D& Center, double AreaRadiusCm, double ExtraMarginCm,
	bool bIncludeSidewalk)
{
	BuildInternal(Network, ExtraMarginCm, bIncludeSidewalk,
		/*bLimitToArea=*/true, Center, FMath::Max(0.0, AreaRadiusCm));
}

void FWiesbadenRoadClearance::BuildInternal(const FRoadNetwork& Network,
	double ExtraMarginCm, bool bIncludeSidewalk, bool bLimitToArea,
	const FVector2D& Center, double AreaRadiusCm)
{
	Spans.Reset();
	Cells.Reset();

	const double Margin = FMath::Max(0.0, ExtraMarginCm);
	int32 OversizedSpans = 0;

	for (const FRoadSegment& Segment : Network.Segments)
	{
		// Die getrimmte Mittellinie ist die, auf der wirklich gefahren wird;
		// die ungetrimmte laeuft in Kreuzungen hinein, die eigene Flaechen
		// haben. Fehlt sie, bleibt die urspruengliche.
		const TArray<FVector>& Line = Segment.TrimmedCenterline.Num() >= 2
			? Segment.TrimmedCenterline
			: Segment.Centerline;
		if (Line.Num() < 2)
		{
			continue;
		}

		// Ausserhalb des gefragten Umkreises gar nicht erst eintragen. Grob
		// ueber die Stuetzpunkte - ein Abschnitt, der den Kreis nur streift,
		// darf ruhig mitkommen, einer der ihn verfehlt kostet sonst Speicher
		// und Zeit fuer nichts.
		if (bLimitToArea)
		{
			bool bNear = false;
			for (const FVector& Point : Line)
			{
				if (FVector2D::DistSquared(FVector2D(Point.X, Point.Y), Center)
					<= AreaRadiusCm * AreaRadiusCm)
				{
					bNear = true;
					break;
				}
			}
			if (!bNear)
			{
				continue;
			}
		}

		// Halbe Fahrbahn, wahlweise plus Gehweg, plus Zuschlag.
		const double Radius = Segment.CarriagewayWidthCm * 0.5
			+ (bIncludeSidewalk ? FMath::Max(0.0, Segment.SidewalkWidthCm) : 0.0)
			+ Margin;

		for (int32 i = 0; i + 1 < Line.Num(); ++i)
		{
			FSpan Span;
			Span.Start = FVector2D(Line[i].X, Line[i].Y);
			Span.End = FVector2D(Line[i + 1].X, Line[i + 1].Y);
			Span.RadiusCm = Radius;

			const int32 Index = Spans.Add(Span);

			// In alle Zellen eintragen, die der Abschnitt samt Radius
			// beruehrt. Nur die Endpunkte einzutragen genuegt nicht: ein
			// langer Abschnitt uebersprAenge sonst ganze Zellen, und die
			// Baeume mitten darauf blieben stehen.
			const FIntPoint MinCell = CellOf(
				FVector2D(FMath::Min(Span.Start.X, Span.End.X) - Radius,
					FMath::Min(Span.Start.Y, Span.End.Y) - Radius));
			const FIntPoint MaxCell = CellOf(
				FVector2D(FMath::Max(Span.Start.X, Span.End.X) + Radius,
					FMath::Max(Span.Start.Y, Span.End.Y) + Radius));

			// Deckel gegen entartete Segmente.
			//
			// Ein einziges Segment mit unsinnigen Koordinaten - und die
			// kommen in OSM-Daten vor - spannt einen Kasten ueber die halbe
			// Welt auf. Die Schleife darunter trueg es dann in Millionen
			// Zellen ein und fraesse den Speicher auf, waehrend im Protokoll
			// nichts steht ausser einem Bau, der nicht zurueckkommt.
			//
			// 400 Zellen sind 20 x 20 bei 50 m Kantenlaenge, also ein
			// Quadratkilometer: mehr kann ein Fahrbahnabschnitt zwischen zwei
			// Stuetzpunkten nicht ueberdecken.
			const int64 CellsWide = static_cast<int64>(MaxCell.X - MinCell.X) + 1;
			const int64 CellsHigh = static_cast<int64>(MaxCell.Y - MinCell.Y) + 1;
			if (CellsWide * CellsHigh > 400)
			{
				Spans.Pop();
				++OversizedSpans;
				continue;
			}

			for (int32 Cx = MinCell.X; Cx <= MaxCell.X; ++Cx)
			{
				for (int32 Cy = MinCell.Y; Cy <= MaxCell.Y; ++Cy)
				{
					Cells.FindOrAdd(FIntPoint(Cx, Cy)).Add(Index);
				}
			}
		}
	}

	if (OversizedSpans > 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Fahrbahn-Freihaltung: %d entartete Abschnitte uebersprungen "
				"(Kasten groesser als ein Quadratkilometer)."),
			OversizedSpans);
	}
}

bool FWiesbadenRoadClearance::IsBlocked(const FVector2D& Point) const
{
	if (Spans.Num() == 0)
	{
		return false;
	}

	const TArray<int32>* Candidates = Cells.Find(CellOf(Point));
	if (!Candidates)
	{
		return false;
	}

	for (const int32 Index : *Candidates)
	{
		const FSpan& Span = Spans[Index];
		if (DistanceToSegmentSquared2D(Point, Span.Start, Span.End)
			<= Span.RadiusCm * Span.RadiusCm)
		{
			return true;
		}
	}

	return false;
}
