// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenZufahrtProbe.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "TimerManager.h"

#include "GIS/GeoCoordinateConverter.h"
#include "GIS/RoadNetworkGenerator.h"
#include "GIS/WiesbadenWorldBuilder.h"
#include "World/SebboHqShape.h"
#include "World/SebboHqSite.h"

DEFINE_LOG_CATEGORY_STATIC(LogWbZufahrt, Log, All);

namespace
{
	/** JSON-Text maskieren - Strassennamen tragen Anfuehrungszeichen und Backslashes. */
	FString Maskiert(const FString& Text)
	{
		return Text.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
	}

	/**
	 * Der Filter des Pad-Passes, wortgleich zu FlattenSitePads.
	 *
	 * Bewusst hier NACHGEBILDET statt aufgerufen: FlattenSitePads nimmt ein
	 * FTerrainTile und veraendert es. Diese Sonde darf nichts veraendern, und
	 * der Filter ist eine Zeile - eine Kopie ist hier ehrlicher als ein
	 * Umbau am Pad-Pass, den dieser Durchgang ausdruecklich nicht will.
	 */
	bool PadFilterLaesstZu(const FRoadSegment& Segment)
	{
		return !Segment.bIsArea && !Segment.bIsBridge && !Segment.bIsTunnel && Segment.Layer == 0;
	}

	/** Naechster Punkt auf der Achse eines Segments zu Ziel; liefert Abstand und Z. */
	bool NaechsterPunkt(const FRoadSegment& Segment, const FVector2D& Ziel,
		double& OutAbstandCm, double& OutZCm)
	{
		const TArray<FVector>& Linie = Segment.TrimmedCenterline.Num() >= 2
			? Segment.TrimmedCenterline : Segment.Centerline;
		bool bGefunden = false;
		double BestSq = TNumericLimits<double>::Max();
		for (int32 i = 0; i + 1 < Linie.Num(); ++i)
		{
			const FVector2D A(Linie[i].X, Linie[i].Y);
			const FVector2D B(Linie[i + 1].X, Linie[i + 1].Y);
			const FVector2D D = B - A;
			const double LenSq = D.SizeSquared();
			if (LenSq <= KINDA_SMALL_NUMBER)
			{
				continue;
			}
			const double T = FMath::Clamp(FVector2D::DotProduct(Ziel - A, D) / LenSq, 0.0, 1.0);
			const double DistSq = FVector2D::DistSquared(Ziel, A + D * T);
			if (DistSq < BestSq)
			{
				BestSq = DistSq;
				OutZCm = FMath::Lerp(Linie[i].Z, Linie[i + 1].Z, T);
				bGefunden = true;
			}
		}
		OutAbstandCm = bGefunden ? FMath::Sqrt(BestSq) : 0.0;
		return bGefunden;
	}

	struct FNachbarstrasse
	{
		int32 SegmentId = INDEX_NONE;
		FString Name;
		FString Typ;
		double AbstandCm = 0.0;
		double ZCm = 0.0;
		bool bZulaessig = false;
	};
}

bool UWiesbadenZufahrtProbe::ShouldCreateSubsystem(UObject* Outer) const
{
	// Ohne den Schalter existiert dieses Subsystem gar nicht erst.
	return FParse::Param(FCommandLine::Get(), TEXT("WbZufahrtProbe"));
}

void UWiesbadenZufahrtProbe::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// Die Ringtaster brauchen gestreamte Zellen; das Strassennetz selbst liegt
	// serialisiert im WorldBuilder und waere sofort da.
	InWorld.GetTimerManager().SetTimer(Verzoegerung,
		FTimerDelegate::CreateUObject(this, &UWiesbadenZufahrtProbe::Messen),
		30.0f, false);
}

void UWiesbadenZufahrtProbe::Messen()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// --- Das gebackene Strassennetz finden ------------------------------------
	AWiesbadenWorldBuilder* Builder = nullptr;
	for (TActorIterator<AWiesbadenWorldBuilder> It(World); It; ++It)
	{
		Builder = *It;
		break;
	}
	if (!Builder)
	{
		UE_LOG(LogWbZufahrt, Error, TEXT("Kein WorldBuilder im Level - kein Strassennetz."));
		return;
	}
	const FRoadNetwork& Netz = Builder->RoadNetwork;

	// --- Standort und Zufahrtsanker, aus denselben Quellen wie der Bake -------
	UGeoCoordinateConverter* Konverter = NewObject<UGeoCoordinateConverter>(this);
	const bool bKonverter = Builder->bUseWiesbadenOrigin
		? Konverter->InitializeWithWiesbadenOrigin()
		: Konverter->Initialize(Builder->CustomOrigin);
	if (!bKonverter)
	{
		UE_LOG(LogWbZufahrt, Error, TEXT("Georeferenzierung fehlgeschlagen."));
		return;
	}

	const FVector Fuss = Konverter->GeoToUnrealGround(SebboHqSite::Coordinate());
	const FVector2D Mitte(Fuss.X, Fuss.Y);
	const FSebboHqDimensions Masse;
	const FSebboHqArrivalLayout Layout = SebboHq::BuildArrivalFacilities(Masse);
	const FRotator Drehung = SebboHqSite::Heading();
	const auto NachWelt = [&Fuss, &Drehung](const FVector& Oertlich)
	{
		return Fuss + Drehung.RotateVector(Oertlich);
	};
	const FVector GarageWelt = NachWelt(Layout.GarageTarget.CenterCm);
	const FVector PortalWelt = NachWelt(Layout.PedestrianTarget.CenterCm);

	// --- 1) Alle Segmente im Umkreis, nach Abstand sortiert -------------------
	constexpr double UmkreisCm = 15000.0;      // 150 m
	TArray<FNachbarstrasse> Nachbarn;
	for (const FRoadSegment& Segment : Netz.Segments)
	{
		double AbstandCm = 0.0;
		double ZCm = 0.0;
		if (!NaechsterPunkt(Segment, FVector2D(GarageWelt.X, GarageWelt.Y), AbstandCm, ZCm))
		{
			continue;
		}
		if (AbstandCm > UmkreisCm)
		{
			continue;
		}
		FNachbarstrasse N;
		N.SegmentId = Segment.SegmentId;
		N.Name = Segment.StreetName.IsEmpty() ? TEXT("(ohne Namen)") : Segment.StreetName;
		N.Typ = UEnum::GetValueAsString(Segment.HighwayType);
		N.AbstandCm = AbstandCm;
		N.ZCm = ZCm;
		N.bZulaessig = PadFilterLaesstZu(Segment);
		Nachbarn.Add(N);
	}
	Nachbarn.Sort([](const FNachbarstrasse& A, const FNachbarstrasse& B)
	{
		return A.AbstandCm < B.AbstandCm;
	});

	// Was der Pad-Pass waehlt: das naechste ZULAESSIGE Segment.
	const FNachbarstrasse* PadWahl = nullptr;
	for (const FNachbarstrasse& N : Nachbarn)
	{
		if (N.bZulaessig)
		{
			PadWahl = &N;
			break;
		}
	}

	// --- 2) Was die Road-Pipeline waehlt --------------------------------------
	FRoadAccessOverride Zugang;
	Zugang.bEnabled = true;
	Zugang.GarageEntranceWorldCm = GarageWelt;
	Zugang.PedestrianEntranceWorldCm = PortalWelt;
	Zugang.SearchRadiusCm = 5000.0;
	const FResolvedRoadAccess Aufgeloest =
		URoadNetworkGenerator::ResolveRoadAccess(Netz, Zugang);

	// --- 2b) DIE GEWAEHLTE FAHRBAHN IN TURMKOORDINATEN -----------------------
	//
	// "0,9 m vom Garagenzugang" sagt noch nicht, WIE die Wolkenbruch dort
	// liegt: quer vor der Tuer, laengs an der Fassade oder schraeg durch das
	// Erdgeschoss. Von der Antwort haengt ab, ob eine Zufahrt ueberhaupt
	// irgendwohin fuehren kann - darum die Achse selbst, Punkt fuer Punkt, in
	// derselben Rechnung, in der auch der Turm gebaut wird (X aus der
	// Garagenoeffnung heraus, Y quer).
	FString AchseJson;
	double UnterbauCm = 0.0;
	double FahrbahnBreiteCm = 0.0;
	{
		const FRotator Zurueck(0.0, -Drehung.Yaw, 0.0);
		int32 Geschrieben = 0;
		for (const FRoadSegment& Segment : Netz.Segments)
		{
			if (Segment.SegmentId != Aufgeloest.Garage.SegmentId)
			{
				continue;
			}
			FahrbahnBreiteCm = Segment.CarriagewayWidthCm;
			const TArray<FVector>& Linie = Segment.TrimmedCenterline.Num() >= 2
				? Segment.TrimmedCenterline : Segment.Centerline;
			// WIE WEIT REICHT DIE FAHRBAHN UNTER DAS HAUS?
			//
			// Diese eine Zahl fehlte. "0,9 m vom Garagenzugang" klang nach
			// bester Anbindung und war in Wahrheit der Befund, dass der Turm
			// in der Strasse steht: die Achse laeuft HINTER der Fassade
			// durch, nicht davor. Darum fuehrte die Garagenschuerze ins
			// Nichts; der Wagen kam trotzdem an, er fuhr auf genau diesem
			// verdeckten Stueck Fahrbahn.
			//
			// Gemessen wird die turmseitige Fahrbahnkante gegen die
			// Fassadenlinie - und zwar ueber die echte Normale der Strecke,
			// nicht ueber X allein: die Wolkenbruch laeuft hier schraeg, eine
			// Abschaetzung entlang der Achse waere um rund 10 cm daneben.
			{
				const double Halb = Masse.FootprintCm * 0.5
					+ FMath::Max(0.0, Masse.PodiumOversizeCm);
				for (int32 i = 0; i + 1 < Linie.Num(); ++i)
				{
					const FVector A = Zurueck.RotateVector(Linie[i] - Fuss);
					const FVector B = Zurueck.RotateVector(Linie[i + 1] - Fuss);
					const FVector2D Richtung = FVector2D(B.X - A.X, B.Y - A.Y);
					if (Richtung.IsNearlyZero())
					{
						continue;
					}
					FVector2D Normale(-Richtung.Y, Richtung.X);
					Normale.Normalize();
					if (Normale.X < 0.0)
					{
						Normale = -Normale;   // zur Garagenseite zeigend
					}
					// Beide Enden und die Mitte reichen: die Stuecke sind kurz.
					for (const double T : { 0.0, 0.5, 1.0 })
					{
						const FVector2D M = FVector2D(A.X, A.Y)
							+ (FVector2D(B.X, B.Y) - FVector2D(A.X, A.Y)) * T;
						if (FMath::Abs(M.Y) > Halb)
						{
							continue;
						}
						const double InnereKante =
							M.X - Normale.X * Segment.CarriagewayWidthCm * 0.5;
						if (InnereKante < Halb)
						{
							UnterbauCm = FMath::Max(UnterbauCm, Halb - InnereKante);
						}
					}
				}
			}

			for (const FVector& Punkt : Linie)
			{
				const FVector Lokal = Zurueck.RotateVector(Punkt - Fuss);
				if (FVector2D(Lokal.X, Lokal.Y).Size() > 6000.0)
				{
					continue;   // nur die Umgebung des Grundstuecks
				}
				AchseJson += FString::Printf(
					TEXT("%s[%.0f, %.0f, %.0f]"),
					Geschrieben > 0 ? TEXT(", ") : TEXT(""),
					Lokal.X, Lokal.Y, Punkt.Z);
				++Geschrieben;
			}
			break;
		}
	}


	// --- 3) Ringtaster MIT Komponentennamen -----------------------------------
	//
	// Der entscheidende Zusatz: ein WiesbadenCityChunk traegt Fahrbahn und
	// Gebaeude in getrennten Komponenten. Erst der Komponentenname sagt, ob
	// unter dem Taster Asphalt liegt oder eine Hauswand.
	FString RingJson;
	int32 RingPunkte = 0;
	{
		const double Half = Masse.FootprintCm * 0.5 + FMath::Max(0.0, Masse.PodiumOversizeCm);
		const double Radien[] = { Half + 300.0, Half + 800.0, Half + 1800.0, Half + 3500.0 };
		FCollisionQueryParams P(SCENE_QUERY_STAT(WbZufahrtProbe), true);
		for (const double Radius : Radien)
		{
			for (int32 i = 0; i < 24; ++i)
			{
				const double Winkel = 2.0 * PI * i / 24.0;
				const FVector Lokal(Radius * FMath::Cos(Winkel), Radius * FMath::Sin(Winkel), 0.0);
				// ABSOLUT von 300 m ueber Null nach unten. GeoToUnrealGround
				// setzt Z auf 0; ein Taster relativ zu diesem "Fusspunkt"
				// startete rund 100 m UNTER dem Gelaende und traf nie etwas -
				// der erste Lauf lieferte null Ringpunkte.
				//
				// Und MULTI statt Single: an derselben Stelle liegen
				// uebereinander Dachflaeche, Fahrbahn und Gelaende. Erst die
				// ganze Saeule sagt, was dort wirklich der Belag ist.
				TArray<FHitResult> Treffer;
				const FVector Oben = NachWelt(FVector(Lokal.X, Lokal.Y, 0.0))
					+ FVector(0.0, 0.0, 30000.0);
				if (!World->LineTraceMultiByChannel(Treffer, Oben,
					Oben - FVector(0.0, 0.0, 30000.0), ECC_WorldStatic, P))
				{
					continue;
				}
				FString SaeuleJson;
				for (int32 h = 0; h < FMath::Min(Treffer.Num(), 4); ++h)
				{
					const FString Komponente = GetNameSafe(Treffer[h].GetComponent());
					const bool bFahrbahn = Komponente.Contains(TEXT("Road"));
					const bool bGebaeude = Komponente.Contains(TEXT("Building"));
					SaeuleJson += FString::Printf(
						TEXT("%s{\"z_cm\": %.0f, \"art\": \"%s\", \"komponente\": \"%s\"}"),
						h > 0 ? TEXT(", ") : TEXT(""),
						Treffer[h].Location.Z,
						bFahrbahn ? TEXT("Fahrbahn") : (bGebaeude ? TEXT("Gebaeude") : TEXT("Gelaende")),
						*Maskiert(Komponente));
				}
				RingJson += FString::Printf(
					TEXT("%s{\"radius_m\": %.0f, \"grad\": %.0f, \"saeule\": [%s]}"),
					RingPunkte > 0 ? TEXT(",\n  ") : TEXT(""),
					Radius / 100.0, FMath::RadiansToDegrees(Winkel), *SaeuleJson);
				++RingPunkte;
			}
		}
	}

	// --- Bericht ---------------------------------------------------------------
	FString NachbarJson;
	for (int32 i = 0; i < FMath::Min(Nachbarn.Num(), 12); ++i)
	{
		const FNachbarstrasse& N = Nachbarn[i];
		NachbarJson += FString::Printf(
			TEXT("%s{\"segment\": %d, \"name\": \"%s\", \"typ\": \"%s\", ")
			TEXT("\"abstand_m\": %.1f, \"z_cm\": %.0f, \"ueber_fuss_cm\": %.0f, \"pad_zulaessig\": %s}"),
			i > 0 ? TEXT(",\n  ") : TEXT(""),
			N.SegmentId, *Maskiert(N.Name), *Maskiert(N.Typ),
			N.AbstandCm / 100.0, N.ZCm, N.ZCm - Fuss.Z,
			N.bZulaessig ? TEXT("true") : TEXT("false"));
	}

	UE_LOG(LogWbZufahrt, Log,
		TEXT("Zufahrtsprobe: Turmfuss %.0f cm, %d Segmente im Umkreis. ")
		TEXT("Pad waehlt Segment %d (%s) auf %.0f cm. ")
		TEXT("ResolveRoadAccess: Garage %d, Portal %d, gueltig %s."),
		Fuss.Z, Nachbarn.Num(),
		PadWahl ? PadWahl->SegmentId : -1,
		PadWahl ? *PadWahl->Name : TEXT("-"),
		PadWahl ? PadWahl->ZCm : 0.0,
		Aufgeloest.Garage.SegmentId, Aufgeloest.Pedestrian.SegmentId,
		Aufgeloest.IsValid() ? TEXT("ja") : TEXT("nein"));

	const FString Inhalt = FString::Printf(
		TEXT("{\n")
		TEXT(" \"turmfuss_z_cm\": %.0f,\n")
		TEXT(" \"garage_anker\": [%.0f, %.0f],\n")
		TEXT(" \"portal_anker\": [%.0f, %.0f],\n")
		TEXT(" \"pad_wahl\": {\"segment\": %d, \"name\": \"%s\", \"z_cm\": %.0f, \"abstand_m\": %.1f},\n")
		TEXT(" \"road_access\": {\"garage_segment\": %d, \"portal_segment\": %d, \"gueltig\": %s},\n")
		TEXT(" \"fahrbahn_lokal\": {\"breite_cm\": %.0f, \"unter_gebaeude_cm\": %.0f, \"achse\": [%s]},\n")
		TEXT(" \"nachbarstrassen\": [\n  %s\n ],\n")
		TEXT(" \"ring\": [\n  %s\n ]\n}\n"),
		Fuss.Z,
		GarageWelt.X, GarageWelt.Y, PortalWelt.X, PortalWelt.Y,
		PadWahl ? PadWahl->SegmentId : -1, PadWahl ? *Maskiert(PadWahl->Name) : TEXT("-"),
		PadWahl ? PadWahl->ZCm : 0.0, PadWahl ? PadWahl->AbstandCm / 100.0 : 0.0,
		Aufgeloest.Garage.SegmentId, Aufgeloest.Pedestrian.SegmentId,
		Aufgeloest.IsValid() ? TEXT("true") : TEXT("false"),
		FahrbahnBreiteCm, UnterbauCm, *AchseJson,
		*NachbarJson, *RingJson);

	const FString Pfad = FPaths::ProjectSavedDir() / TEXT("Diagnose") / TEXT("zufahrtsprobe.json");
	FFileHelper::SaveStringToFile(Inhalt, *Pfad);
	UE_LOG(LogWbZufahrt, Log, TEXT("Zufahrtsprobe geschrieben: %s"), *Pfad);
}
