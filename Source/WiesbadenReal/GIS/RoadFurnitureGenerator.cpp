// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/RoadFurnitureGenerator.h"

#include "WiesbadenReal.h"

#include "GIS/BuildingGenerator.h"
#include "GIS/GeoCoordinateConverter.h"
#include "GIS/PolygonUtils.h"
#include "GIS/WiesbadenRoadClearance.h"
#include "HAL/PlatformTime.h"

namespace
{
	/** Rechter Normalenvektor einer horizontalen Richtung (UE: +X vor, +Y rechts). */
	FVector RightPerpendicular(const FVector& Direction)
	{
		const FVector N = Direction.GetSafeNormal2D();
		return FVector(-N.Y, N.X, 0.0);
	}

	/**
	 * Tastet eine Polylinie in festem Bogenabstand ab. Startpunkt (Abstand 0)
	 * und danach jeder Spacing-Schritt werden aufgenommen.
	 */
	void WalkCenterline(
		const TArray<FVector>& Line,
		double SpacingCm,
		TArray<FVector>& OutPositions,
		TArray<FVector>& OutDirections)
	{
		OutPositions.Reset();
		OutDirections.Reset();

		if (Line.Num() < 2)
		{
			return;
		}

		OutPositions.Add(Line[0]);
		OutDirections.Add((Line[1] - Line[0]).GetSafeNormal());

		double Accumulated = 0.0;
		double NextSample = SpacingCm;

		for (int32 i = 1; i < Line.Num(); ++i)
		{
			const FVector A = Line[i - 1];
			const FVector B = Line[i];
			const double SegmentLength = FVector::Dist(B, A);
			const FVector Dir = (B - A).GetSafeNormal();

			while (NextSample <= Accumulated + SegmentLength)
			{
				const double T = (SegmentLength > KINDA_SMALL_NUMBER)
					? FMath::Clamp((NextSample - Accumulated) / SegmentLength, 0.0, 1.0)
					: 0.0;
				OutPositions.Add(FMath::Lerp(A, B, T));
				OutDirections.Add(Dir);
				NextSample += SpacingCm;
			}

			Accumulated += SegmentLength;
		}
	}

	/**
	 * Hoehe der OBERFLAECHE, auf der die Ausstattung steht, an einer XY-Position.
	 *
	 * = Terrainhoehe + SurfaceOffsetCm. Der Offset ist entscheidend: die Strasse
	 * und der Gehweg liegen um RoadSurfaceOffsetCm (Fahrbahn) bzw.
	 * RoadSurfaceOffsetCm + KerbHeightCm (Gehweg) UEBER dem Terrain. Ohne den
	 * Offset (frueherer Zustand) sass jede Basis um genau diesen Betrag im Boden.
	 * 0, wenn kein Sampler vorhanden.
	 */
	double SampleZ(const IHeightSampler* Sampler, const FVector& P, double SurfaceOffsetCm)
	{
		return Sampler ? Sampler->SampleHeightCm(FVector2D(P.X, P.Y)) + SurfaceOffsetCm : 0.0;
	}

	/** Das Tag, das der Nachzug (Tools/fetch_street_furniture.py) schreibt. */
	const FName FurnitureKindTagName(TEXT("wb:furniture"));

	/** Tagwert -> Art. Dieselben acht Namen wie im Python-Nachzug. */
	struct FFurnitureKindName
	{
		const TCHAR* Value;
		EStreetFurnitureKind Kind;
	};

	const FFurnitureKindName FurnitureKindNames[] = {
		{ TEXT("bench"),           EStreetFurnitureKind::Bench },
		{ TEXT("bollard"),         EStreetFurnitureKind::Bollard },
		{ TEXT("waste_basket"),    EStreetFurnitureKind::WasteBasket },
		{ TEXT("vending_machine"), EStreetFurnitureKind::VendingMachine },
		{ TEXT("recycling"),       EStreetFurnitureKind::Recycling },
		{ TEXT("fire_hydrant"),    EStreetFurnitureKind::FireHydrant },
		{ TEXT("post_box"),        EStreetFurnitureKind::PostBox },
		{ TEXT("picnic_table"),    EStreetFurnitureKind::PicnicTable },
	};

	/**
	 * Zellgitter ueber die Stadtflaeche (50-m-Zellen).
	 *
	 * Der Moebel-Pass stellt zwei Umkreisfragen - "welcher Weg liegt am
	 * naechsten" und "steckt der Punkt in einem Gebaeude" -, und beide
	 * brauchen dieselbe Buchhaltung: Zelle zu einem Punkt, Eintrag in alle
	 * Zellen eines Kastens, Deckel gegen entartete Kaesten. Die steht deshalb
	 * EINMAL hier, statt in jedem der beiden Indizes noch einmal.
	 *
	 * Warum nicht FWiesbadenRoadClearance: das beantwortet ausschliesslich
	 * "blockiert ja/nein" (Build/BuildAround/IsBlocked) und kennt weder
	 * Abstand noch Breite noch Laengsrichtung - und es kennt ueberhaupt nur
	 * Strassen, keine Gebaeude. Es wird ausserdem von Regionen-Streuung,
	 * WorldBuilder und GameMode benutzt; es fuer diesen Pass umzubauen hiesse,
	 * den Baum-Streuer mit anzufassen.
	 */
	struct FCellGrid
	{
		static constexpr double CellSizeCm = 5000.0;

		/**
		 * Deckel gegen entartete Kaesten: 400 Zellen sind 20 x 20 bei 50 m
		 * Kantenlaenge, also ein Quadratkilometer. Ein einzelnes Segment oder
		 * Gebaeude mit unsinnigen Koordinaten - und die kommen in OSM-Daten
		 * vor - trueg sich sonst in Millionen Zellen ein.
		 */
		static constexpr int64 MaxCellsPerEntry = 400;

		TMap<FIntPoint, TArray<int32>> Cells;

		static FIntPoint CellOf(const FVector2D& Point)
		{
			return FIntPoint(
				FMath::FloorToInt(Point.X / CellSizeCm),
				FMath::FloorToInt(Point.Y / CellSizeCm));
		}

		/** Traegt Index in alle Zellen des Kastens ein. False = verworfen. */
		bool Insert(const FVector2D& Min, const FVector2D& Max, int32 Index)
		{
			const FIntPoint MinCell = CellOf(Min);
			const FIntPoint MaxCell = CellOf(Max);
			const int64 Wide = static_cast<int64>(MaxCell.X - MinCell.X) + 1;
			const int64 High = static_cast<int64>(MaxCell.Y - MinCell.Y) + 1;
			if (Wide * High > MaxCellsPerEntry)
			{
				return false;
			}

			for (int32 Cx = MinCell.X; Cx <= MaxCell.X; ++Cx)
			{
				for (int32 Cy = MinCell.Y; Cy <= MaxCell.Y; ++Cy)
				{
					Cells.FindOrAdd(FIntPoint(Cx, Cy)).Add(Index);
				}
			}
			return true;
		}

		const TArray<int32>* Find(const FIntPoint& Cell) const
		{
			return Cells.Find(Cell);
		}
	};

	/** Was der Moebel-Pass ueber den naechsten Weg wissen muss. */
	struct FNearestWay
	{
		/** Waagerechter Abstand zur Wegachse (cm). */
		double DistanceCm = 0.0;
		/** Halbe Breite des befahrenen/begangenen Wegs (cm). */
		double HalfWidthCm = 0.0;
		/** Breite des Gehwegstreifens daneben (cm). */
		double SidewalkWidthCm = 0.0;
		bool bBankettErlaubt = false;
		/** Einheitsvektor vom Weg WEG, zum Moebel hin. */
		FVector2D AwayDirection = FVector2D(0.0, 1.0);
		/** Einheitsvektor laengs der Wegachse. */
		FVector2D AxisDirection = FVector2D(1.0, 0.0);
	};

	/**
	 * Naechster Weg zu einem Punkt - ueber ein Gitter, nicht ueber alle Segmente.
	 *
	 * WARUM NICHT DER FAHRBAHN-INDEX: FWiesbadenRoadClearance beantwortet nur
	 * "blockiert ja/nein". Aus einer Ja/Nein-Antwort laesst sich die Richtung
	 * zur Strasse nur abtasten, und ein Abtasten in 16 Richtungen liegt bis zu
	 * 11 Grad daneben - gemessen kam eine Bank, die der Strasse den Ruecken
	 * kehren soll, 27 Grad schief heraus. Hier wird stattdessen der Lotfusspunkt
	 * auf den naechsten Abschnitt gerechnet: exakt, und dieselbe Antwort traegt
	 * zugleich Abstand, Breite und Laengsrichtung.
	 */
	struct FNearestWayIndex
	{
		/** Ueber 200 m von jedem Weg entfernt ist kein Strassenrand mehr. */
		static constexpr int32 MaxRings = 4;

		/** Wegtypen, neben denen wirklich ein begehbarer Streifen liegt. */
		static bool WbBankettErlaubt(EOSMHighwayType Typ)
		{
			switch (Typ)
			{
			case EOSMHighwayType::Footway:
			case EOSMHighwayType::Path:
			case EOSMHighwayType::Track:
			case EOSMHighwayType::Pedestrian:
			case EOSMHighwayType::Steps:
			case EOSMHighwayType::Cycleway:
			case EOSMHighwayType::LivingStreet:
				return true;
			default:
				return false;   // Autobahn, Trunk, Service, Zubringer ...
			}
		}

		struct FSpan
		{
			FVector2D Start = FVector2D::ZeroVector;
			FVector2D End = FVector2D::ZeroVector;
			double HalfWidthCm = 0.0;
			double SidewalkWidthCm = 0.0;

			/**
			 * Darf hier ein BANKETT als Ersatz-Gehweg gelten?
			 *
			 * Gehwegbreite 0 heisst nicht "da ist ein begehbarer Streifen".
			 * Sie steht auch an Autobahn, Kraftfahrstrasse und Zubringer.
			 * Das Bankett wurde mit dem Fussweg begruendet - dort stehen
			 * Baenke neben dem Weg. Neben einer 130er-Fahrbahn steht die
			 * Standspur, und eine Bank 0,5 m daneben waere kein gerettetes
			 * Moebel, sondern ein Hindernis.
			 */
			bool bBankettErlaubt = false;
		};

		TArray<FSpan> Spans;
		FCellGrid Grid;

		void Build(const FRoadNetwork& Network)
		{
			for (const FRoadSegment& Segment : Network.Segments)
			{
				const TArray<FVector>& Line = Segment.TrimmedCenterline.Num() >= 2
					? Segment.TrimmedCenterline
					: Segment.Centerline;
				if (Line.Num() < 2)
				{
					continue;
				}

				for (int32 i = 0; i + 1 < Line.Num(); ++i)
				{
					FSpan Span;
					Span.Start = FVector2D(Line[i].X, Line[i].Y);
					Span.End = FVector2D(Line[i + 1].X, Line[i + 1].Y);
					Span.HalfWidthCm = Segment.CarriagewayWidthCm * 0.5;
					Span.SidewalkWidthCm = FMath::Max(0.0, Segment.SidewalkWidthCm);
					Span.bBankettErlaubt = WbBankettErlaubt(Segment.HighwayType);

					const int32 Index = Spans.Add(Span);
					const FVector2D Min(
						FMath::Min(Span.Start.X, Span.End.X), FMath::Min(Span.Start.Y, Span.End.Y));
					const FVector2D Max(
						FMath::Max(Span.Start.X, Span.End.X), FMath::Max(Span.Start.Y, Span.End.Y));
					if (!Grid.Insert(Min, Max, Index))
					{
						Spans.Pop();
					}
				}
			}
		}

		bool Find(const FVector2D& Point, FNearestWay& OutWay) const
		{
			const FIntPoint Center = FCellGrid::CellOf(Point);
			int32 BestIndex = INDEX_NONE;
			double BestDistSq = TNumericLimits<double>::Max();
			FVector2D BestFoot = FVector2D::ZeroVector;

			for (int32 Ring = 0; Ring <= MaxRings; ++Ring)
			{
				// Gefunden UND der naechste Ring kann nichts Naeheres mehr
				// liefern? Dann ist die Antwort sicher.
				if (BestIndex != INDEX_NONE)
				{
					const double RingReach = (Ring - 1) * FCellGrid::CellSizeCm;
					if (RingReach > 0.0 && RingReach * RingReach >= BestDistSq)
					{
						break;
					}
				}

				for (int32 Cx = Center.X - Ring; Cx <= Center.X + Ring; ++Cx)
				{
					for (int32 Cy = Center.Y - Ring; Cy <= Center.Y + Ring; ++Cy)
					{
						// Nur der Rand des Rings ist neu.
						const bool bOnRingBorder = Ring == 0
							|| FMath::Abs(Cx - Center.X) == Ring
							|| FMath::Abs(Cy - Center.Y) == Ring;
						if (!bOnRingBorder)
						{
							continue;
						}

						const TArray<int32>* Indices = Grid.Find(FIntPoint(Cx, Cy));
						if (!Indices)
						{
							continue;
						}

						for (const int32 Index : *Indices)
						{
							const FSpan& Span = Spans[Index];
							const FVector2D Axis = Span.End - Span.Start;
							const double LengthSq = Axis.SizeSquared();
							const double T = LengthSq > KINDA_SMALL_NUMBER
								? FMath::Clamp(FVector2D::DotProduct(Point - Span.Start, Axis) / LengthSq, 0.0, 1.0)
								: 0.0;
							const FVector2D Foot = Span.Start + Axis * T;
							const double DistSq = FVector2D::DistSquared(Point, Foot);
							if (DistSq < BestDistSq)
							{
								BestDistSq = DistSq;
								BestIndex = Index;
								BestFoot = Foot;
							}
						}
					}
				}
			}

			if (BestIndex == INDEX_NONE)
			{
				return false;
			}

			const FSpan& Span = Spans[BestIndex];
			OutWay.DistanceCm = FMath::Sqrt(BestDistSq);
			OutWay.HalfWidthCm = Span.HalfWidthCm;
			OutWay.SidewalkWidthCm = Span.SidewalkWidthCm;
			OutWay.bBankettErlaubt = Span.bBankettErlaubt;
			OutWay.AxisDirection = (Span.End - Span.Start).GetSafeNormal();
			OutWay.AwayDirection = (Point - BestFoot).GetSafeNormal();
			if (OutWay.AwayDirection.IsNearlyZero())
			{
				// Genau auf der Achse: quer zur Fahrtrichtung ausweichen.
				OutWay.AwayDirection = FVector2D(-OutWay.AxisDirection.Y, OutWay.AxisDirection.X);
			}
			return true;
		}
	};

	/**
	 * Punkt im gedrehten Grundriss eines Gebaeudes?
	 *
	 * FGeneratedBuilding traegt die flaechenminimale GEDREHTE Box - die
	 * achsparallele `Bounds` deckt bei schraegen Haeusern im Mittel das
	 * 2,1-fache ab und wuerde die halbe Strasse als "im Gebaeude" melden.
	 */
	bool IsInsideFootprint(const FGeneratedBuilding& Building, const FVector2D& Point)
	{
		if (Building.FootprintExtentCm.IsNearlyZero())
		{
			return false;
		}

		// Eine Pruefung fuer alle: der Platzierungs-Audit rechnet dieselbe
		// Frage ueber FPolygonUtils - dort wie hier, Bit fuer Bit gleich.
		return FPolygonUtils::IsInsideRotatedBox2D(
			Point, Building.FootprintCenterCm,
			Building.FootprintExtentCm, Building.FootprintYawDegrees);
	}

	/**
	 * Gitter ueber die Gebaeude-Grundrisse (50-m-Zellen).
	 *
	 * Ohne das waeren es 3.600 Moebel x 104.458 Gebaeude = 376 Millionen
	 * Boxentests je Bake - fuer eine Frage, die je Moebel nur die Haeuser im
	 * eigenen Block angeht.
	 */
	struct FBuildingFootprintGrid
	{
		FCellGrid Grid;
		const TArray<FGeneratedBuilding>* Buildings = nullptr;

		void Build(const TArray<FGeneratedBuilding>& InBuildings)
		{
			Buildings = &InBuildings;
			for (int32 Index = 0; Index < InBuildings.Num(); ++Index)
			{
				const FGeneratedBuilding& Building = InBuildings[Index];
				const FVector2D Extent = Building.FootprintExtentCm;
				if (Extent.IsNearlyZero())
				{
					continue;
				}

				// Umkreis der gedrehten Box - drehungsunabhaengig und billig.
				const FVector2D Radius(Extent.Size(), Extent.Size());
				Grid.Insert(Building.FootprintCenterCm - Radius,
					Building.FootprintCenterCm + Radius, Index);
			}
		}

		bool Contains(const FVector2D& Point) const
		{
			if (!Buildings)
			{
				return false;
			}
			if (const TArray<int32>* Indices = Grid.Find(FCellGrid::CellOf(Point)))
			{
				for (const int32 Index : *Indices)
				{
					if (IsInsideFootprint((*Buildings)[Index], Point))
					{
						return true;
					}
				}
			}
			return false;
		}
	};
}

FRoadFurnitureReport URoadFurnitureGenerator::Generate(
	const FRoadNetwork& Network,
	const FOSMDataSet* DataSet,
	const UGeoCoordinateConverter* Converter,
	const IHeightSampler* HeightSampler,
	const FRoadFurnitureSettings& Settings,
	FRoadFurnitureLayout& OutLayout,
	const TArray<FGeneratedBuilding>* Buildings)
{
	FRoadFurnitureReport Report;
	const double StartSeconds = FPlatformTime::Seconds();

	OutLayout.Reset();

	if (Network.IsEmpty())
	{
		Report.bSuccess = false;
		Report.ErrorMessage = TEXT("Strassennetz ist leer - Ausstattungs-Pass uebersprungen.");
		Report.DurationSeconds = FPlatformTime::Seconds() - StartSeconds;
		return Report;
	}

	if (Settings.bPlaceSigns)
	{
		PlaceSigns(Network, DataSet, Converter, HeightSampler, Settings, OutLayout);
	}
	if (Settings.bPlaceDelineators)
	{
		PlaceDelineators(Network, HeightSampler, Settings, OutLayout);
	}
	if (Settings.bPlaceMarkings)
	{
		PlaceMarkings(Network, HeightSampler, Settings, OutLayout);
	}
	if (Settings.bPlaceStreetLamps)
	{
		PlaceStreetLamps(DataSet, Converter, HeightSampler, Settings, OutLayout);

		const int32 MappedLamps = OutLayout.StreetLamps.Num();
		if (Settings.bSynthesiseMissingStreetLamps)
		{
			SynthesiseStreetLamps(Network, HeightSampler, Settings, OutLayout);
			Report.SynthesisedStreetLampCount = OutLayout.StreetLamps.Num() - MappedLamps;
		}
	}
	if (Settings.bPlaceStreetFurniture)
	{
		PlaceStreetFurniture(Network, DataSet, Converter, HeightSampler, Buildings,
			Settings, OutLayout, Report);
	}

	// Fahrbahn freiraeumen - EIN Durchgang fuer alles.
	//
	// Jeder Platzierer oben setzt sein Objekt seitlich neben die eigene
	// Mittellinie, ueblicherweise "halbe Fahrbahnbreite + 50 cm". Das ist
	// richtig gerechnet und trotzdem falsch, aus zwei Gruenden:
	//
	//  - Gerechnet wird auf der UNGETRIMMTEN Mittellinie, die in die
	//    Kreuzung hineinlaeuft. Schilder sitzen sogar auf Centerline[0],
	//    also exakt am Kreuzungsknoten, wo die gepflasterte Flaeche viel
	//    breiter ist als das einzelne Segment.
	//  - Geprueft wird nur die EIGENE Segmentbreite. Neben einer Querstrasse
	//    heisst "50 cm ausserhalb meiner Fahrbahn" nicht "neben der Strasse".
	//
	// Deshalb hier zum Schluss der Abgleich gegen ALLE Segmente. Der Index
	// laesst den Gehweg ausdruecklich zu - dorthin gehoeren Schilder und
	// Laternen ja - und sperrt nur die Fahrbahn selbst.
	Report.RemovedOnCarriagewayCount = RemoveFurnitureOnCarriageway(Network, OutLayout);

	Report.bSuccess = true;
	Report.SignCount = OutLayout.Signs.Num();
	Report.DelineatorCount = OutLayout.Delineators.Num();
	Report.MarkingCount = OutLayout.Markings.Num();
	Report.StreetLampCount = OutLayout.StreetLamps.Num();
	Report.DurationSeconds = FPlatformTime::Seconds() - StartSeconds;
	return Report;
}

int32 URoadFurnitureGenerator::RemoveFurnitureOnCarriageway(
	const FRoadNetwork& Network, FRoadFurnitureLayout& Layout)
{
	// Nur die Fahrbahn sperren, nicht den Gehweg (bIncludeSidewalk = false).
	// Zuschlag 20 cm: ein Mast direkt an der Bordsteinkante ist richtig, ein
	// Mast einen halben Meter DAVOR steht im Weg.
	FWiesbadenRoadClearance Carriageway;
	Carriageway.Build(Network, 20.0, /*bIncludeSidewalk=*/false);

	if (Carriageway.IsEmpty())
	{
		return 0;
	}

	int32 Removed = 0;
	int32 Moved = 0;

	// Erst VERSETZEN, erst dann entfernen.
	//
	// Der erste Entwurf hat blockierte Objekte schlicht geloescht. Das nahm
	// auch ausdruecklich in OSM erfasste Schilder mit, die an einer Kreuzung
	// stehen - und ein Stoppschild, das verschwindet, ist schlimmer als eines,
	// das einen Meter zu weit rechts steht. Die Tests haben es gemeldet:
	// "3 Stopp-Schilder erwartet, 1 vorhanden".
	//
	// Gesucht wird in acht Richtungen und in wachsendem Abstand der naechste
	// freie Platz. Erst wenn ringsum alles Fahrbahn ist - etwa mitten auf
	// einem grossen Kreisverkehr - faellt das Objekt weg.
	auto FreeSpotNear = [&Carriageway](const FVector& P, FVector& OutFree) -> bool
	{
		static const FVector2D Directions[8] = {
			{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
			{ 0.707, 0.707 }, { -0.707, 0.707 }, { 0.707, -0.707 }, { -0.707, -0.707 }
		};

		for (double Step = 100.0; Step <= 900.0; Step += 100.0)
		{
			for (const FVector2D& Dir : Directions)
			{
				const FVector2D Candidate(P.X + Dir.X * Step, P.Y + Dir.Y * Step);
				if (!Carriageway.IsBlocked(Candidate))
				{
					OutFree = FVector(Candidate.X, Candidate.Y, P.Z);
					return true;
				}
			}
		}
		return false;
	};

	// Markierungen bleiben unangetastet: sie GEHOEREN auf die Fahrbahn.
	auto Sweep = [&Carriageway, &Removed, &Moved, &FreeSpotNear](auto& Items)
	{
		for (int32 i = Items.Num() - 1; i >= 0; --i)
		{
			const FVector& P = Items[i].Location;
			if (!Carriageway.IsBlocked(FVector2D(P.X, P.Y)))
			{
				continue;
			}

			FVector Free;
			if (FreeSpotNear(P, Free))
			{
				Items[i].Location = Free;
				++Moved;
			}
			else
			{
				Items.RemoveAtSwap(i);
				++Removed;
			}
		}
	};

	Sweep(Layout.Signs);
	Sweep(Layout.Delineators);
	Sweep(Layout.StreetLamps);
	// Die Strassenmoebel bleiben ausdruecklich DRAUSSEN: ihr eigener Pass hat
	// sie bereits exakt an den Gehweg gerechnet, und der Poller steht mit
	// Absicht auf der Fahrbahnkante - dieser Sweep wuerde ihn wegschieben.

	UE_LOG(LogWbRoads, Log,
		TEXT("Ausstattung: %d Objekte von der Fahrbahn an den Rand versetzt, "
			"%d entfernt (ringsum kein freier Platz). Bestand: %d Schilder, "
			"%d Leitpfosten, %d Laternen."),
		Moved, Removed, Layout.Signs.Num(), Layout.Delineators.Num(),
		Layout.StreetLamps.Num());

	return Removed + Moved;
}

void URoadFurnitureGenerator::PlaceStreetLamps(
	const FOSMDataSet* DataSet,
	const UGeoCoordinateConverter* Converter,
	const IHeightSampler* HeightSampler,
	const FRoadFurnitureSettings& Settings,
	FRoadFurnitureLayout& OutLayout) const
{
	if (!DataSet || !Converter || !Converter->IsInitialized())
	{
		return;
	}

	// highway=street_lamp sind eigenstaendige Knoten, keine Way-Mitglieder.
	// Die Overpass-Abfrage holt sie seit jeher mit (siehe OSMDataParser), nur
	// ausgewertet wurden sie nie - die Stadt blieb dadurch nachts voellig
	// unbeleuchtet.
	//
	// Deterministische Reihenfolge: ueber die sortierten Knoten-Ids, damit zwei
	// Builds derselben Daten dieselbe Liste ergeben.
	TArray<FOSMId> LampNodeIds;
	for (const TPair<FOSMId, FOSMNode>& Pair : DataSet->Nodes)
	{
		if (Pair.Value.IsStreetLamp())
		{
			LampNodeIds.Add(Pair.Key);
		}
	}

	if (LampNodeIds.Num() == 0)
	{
		return;
	}

	LampNodeIds.Sort();
	OutLayout.StreetLamps.Reserve(OutLayout.StreetLamps.Num() + LampNodeIds.Num());

	for (const FOSMId NodeId : LampNodeIds)
	{
		const FOSMNode* Node = DataSet->Nodes.Find(NodeId);
		if (!Node)
		{
			continue;
		}

		FStreetLampInstance Lamp;
		Lamp.NodeId = NodeId;
		Lamp.Location = Converter->GeoToUnrealGround(Node->Location);

		if (HeightSampler && HeightSampler->HasValidData())
		{
			// Laternen stehen auf dem Gehweg: Terrain + Fahrbahn-Offset + Bordstein.
			Lamp.Location.Z = HeightSampler->SampleHeightCm(
				FVector2D(Lamp.Location.X, Lamp.Location.Y))
				+ Settings.RoadSurfaceOffsetCm + Settings.KerbHeightCm;
		}

		OutLayout.StreetLamps.Add(Lamp);
	}

	UE_LOG(LogWbRoads, Log,
		TEXT("Strassenlaternen: %d Standorte aus highway=street_lamp uebernommen."),
		OutLayout.StreetLamps.Num());
}

void URoadFurnitureGenerator::PlaceSigns(
	const FRoadNetwork& Network,
	const FOSMDataSet* DataSet,
	const UGeoCoordinateConverter* Converter,
	const IHeightSampler* HeightSampler,
	const FRoadFurnitureSettings& Settings,
	FRoadFurnitureLayout& Layout)
{
	// Node-IDs, an denen bereits ein explizites traffic_sign existiert -
	// dort kein abgeleitetes Schild mehr setzen (verhindert Doppel-Schilder).
	TSet<int64> NodesWithExplicitSigns;

	// Ausrichtung eines Node-Schilds aus der NAECHSTEN Strasse ableiten (wie die
	// abgeleiteten Schilder). Ohne das bekamen OSM-Node-Schilder ZeroRotator und
	// standen verdreht, waehrend die Nachbarn korrekt zur Strasse zeigten.
	auto NearestRoadYaw = [&Network](const FVector& At, bool& bOk) -> double
	{
		double BestD = TNumericLimits<double>::Max();
		FVector BestDir = FVector::ForwardVector;
		bOk = false;
		for (const FRoadSegment& Seg : Network.Segments)
		{
			for (int32 i = 0; i + 1 < Seg.Centerline.Num(); ++i)
			{
				const FVector A = Seg.Centerline[i];
				const FVector AB = Seg.Centerline[i + 1] - A;
				const double L2 = AB.SizeSquared2D();
				const double T = (L2 > 1.0)
					? FMath::Clamp(FVector::DotProduct(At - A, AB) / L2, 0.0, 1.0) : 0.0;
				const FVector Proj = A + AB * T;
				const double D = FVector::DistSquared2D(At, Proj);
				if (D < BestD)
				{
					BestD = D;
					BestDir = AB.GetSafeNormal();
					bOk = true;
				}
			}
		}
		return BestDir.Rotation().Yaw;
	};

	// 1) Explizite OSM-Node-Schilder (authoritative Quelle).
	if (DataSet && Converter && Converter->IsInitialized())
	{
		for (const TPair<FOSMId, FOSMNode>& Pair : DataSet->Nodes)
		{
			const FOSMNode& Node = Pair.Value;
			const FString Tag = Node.GetTag(TEXT("traffic_sign"));
			if (Tag.IsEmpty())
			{
				continue;
			}

			TArray<FWiesbadenTrafficSign> Signs;
			FWiesbadenTrafficSignCatalog::ParseOsmTag(Tag, Signs);

			FVector Ground = Converter->GeoToUnrealGround(Node.Location);
			// Schilder stehen auf dem GEHWEG: Terrain + Fahrbahn-Offset + Bordstein.
			Ground.Z = SampleZ(HeightSampler, Ground,
				Settings.RoadSurfaceOffsetCm + Settings.KerbHeightCm);

			for (const FWiesbadenTrafficSign& Sign : Signs)
			{
				FSignInstance Instance;
				Instance.SignId = Sign.Id;
				Instance.Sign = Sign;
				Instance.Location = Ground + FVector(0.0, 0.0, Settings.SignHeightAboveGroundCm);
				// Orientierung aus der naechsten Strasse (sonst stand das Schild
				// verdreht, waehrend die Nachbarschilder korrekt zeigten).
				bool bYawOk = false;
				const double NodeYaw = NearestRoadYaw(Ground, bYawOk);
				Instance.Rotation = bYawOk ? FRotator(0.0, NodeYaw, 0.0) : FRotator::ZeroRotator;
				Instance.SourceNodeId = Node.Id;
				Layout.Signs.Add(Instance);
			}

			// Platzhalter wie "none"/"no" ergeben bewusst keine Zeichen und
			// duerfen daher die abgeleitete Kreuzungskontrolle nicht unterdruecken.
			if (Signs.Num() > 0)
			{
				NodesWithExplicitSigns.Add(Node.Id);
			}
		}
	}

	// 2) Abgeleitete Schilder je Kreuzung (Kontrolle).
	for (const FRoadIntersection& Intersection : Network.Intersections)
	{
		FString ControlSignId;
		switch (Intersection.Control)
		{
		case EIntersectionControl::Stop:
			ControlSignId = TEXT("206");
			break;
		case EIntersectionControl::Yield:
			ControlSignId = TEXT("205");
			break;
		case EIntersectionControl::Roundabout:
			ControlSignId = TEXT("215");
			break;
		default:
			break;
		}

		if (ControlSignId.IsEmpty() || NodesWithExplicitSigns.Contains(Intersection.NodeId))
		{
			continue;
		}

		FWiesbadenTrafficSign Sign;
		if (!FWiesbadenTrafficSignCatalog::FindById(ControlSignId, Sign))
		{
			continue;
		}

		for (const FIntersectionArm& Arm : Intersection.Arms)
		{
			// Standort am TATSAECHLICHEN Fahrbahnende, nicht auf einem geraden
			// Strahl aus der Kreuzungsmitte.
			//
			// Hier stand:
			//   Base = Mitte + Auswaertsrichtung * (RadiusCm + Backset)
			//        + Rechts * (HalbeBreite + SeitenversatzCm)
			//
			// Zwei Fehler auf einmal:
			// 1. Es unterstellt, die Strasse verlasse die Kreuzung GERADLINIG
			//    entlang ihrer Anfangstangente. Bei gekruemmten Zufahrten
			//    marschieren die Schilder in gerader Linie neben der Strasse
			//    her - quer durch Vorgaerten und Wiesen.
			// 2. RadiusCm ist das MAXIMUM ueber alle Arme. An einer Kreuzung
			//    mit einer breiten Strasse werden auch die Schilder der
			//    schmalen Arme entsprechend weit hinausgeschoben, teils 20 m.
			//
			// Beides zusammen ergibt Schilder, die sichtbar nichts mit der
			// Strasse zu tun haben, an der sie stehen sollen.
			FVector ArmEnd = Intersection.Location;
			FVector Outward = Arm.OutwardDirection.GetSafeNormal2D();

			if (Network.Segments.IsValidIndex(Arm.SegmentId))
			{
				const TArray<FVector>& Trimmed = Network.Segments[Arm.SegmentId].TrimmedCenterline;
				if (Trimmed.Num() >= 2)
				{
					const FVector& End = Arm.bIsSegmentStart ? Trimmed[0] : Trimmed.Last();
					const FVector& Neighbour = Arm.bIsSegmentStart
						? Trimmed[1]
						: Trimmed[Trimmed.Num() - 2];

					const FVector LocalOutward = (End - Neighbour).GetSafeNormal2D();
					if (!LocalOutward.IsNearlyZero())
					{
						ArmEnd = End;
						Outward = LocalOutward;
					}
				}
			}

			const FVector Right = RightPerpendicular(Outward);

			// Vom Fahrbahnende noch ein Stueck auswaerts und seitlich neben die
			// eigene Fahrbahn - nicht neben die breiteste der Kreuzung.
			FVector Base = ArmEnd
				+ Outward * Settings.SignBacksetCm
				+ Right * (Arm.HalfWidthCm + Settings.SignLateralOffsetCm);
			// Schild auf dem Gehweg: Terrain + Fahrbahn-Offset + Bordstein.
		Base.Z = SampleZ(HeightSampler, Base,
			Settings.RoadSurfaceOffsetCm + Settings.KerbHeightCm);

			FSignInstance Instance;
			Instance.SignId = Sign.Id;
			Instance.Sign = Sign;
			Instance.Location = Base + FVector(0.0, 0.0, Settings.SignHeightAboveGroundCm);
			Instance.Rotation = FRotator(0.0, Outward.Rotation().Yaw, 0.0);
			Instance.SourceNodeId = Intersection.NodeId;
			Layout.Signs.Add(Instance);
		}
	}

	// 3) Abgeleitete Tempolimit-/Sonderzeichen je Segment.
	for (const FRoadSegment& Segment : Network.Segments)
	{
		if (!FOSMTagParser::IsDrivable(Segment.HighwayType) || Segment.Centerline.Num() < 2)
		{
			continue;
		}
		if (NodesWithExplicitSigns.Contains(Segment.StartNodeId))
		{
			continue;
		}

		// Signatur des abgeleiteten Schilds.
		FString SignId;
		int32 LimitKmh = 0;
		if (Segment.HighwayType == EOSMHighwayType::LivingStreet)
		{
			SignId = TEXT("325.1");
		}
		else if (!FMath::IsNearlyEqual(Segment.MaxSpeedKmh, 50.0, 0.5))
		{
			SignId = TEXT("274");
			LimitKmh = FMath::Clamp(FMath::RoundToInt(Segment.MaxSpeedKmh), 0, 300);
		}

		if (SignId.IsEmpty())
		{
			continue;
		}

		FWiesbadenTrafficSign Sign;
		if (!FWiesbadenTrafficSignCatalog::FindById(SignId, Sign))
		{
			continue;
		}

		if (LimitKmh > 0)
		{
			Sign.Id = FString::Printf(TEXT("274-%d"), LimitKmh);
			Sign.bSpeedLimit = true;
			Sign.SpeedLimitKmh = LimitKmh;
		}
		else
		{
			Sign.Id = SignId;
		}

		const FVector Dir = (Segment.Centerline[1] - Segment.Centerline[0]).GetSafeNormal();
		const FVector Right = RightPerpendicular(Dir);

		FVector Base = Segment.Centerline[0]
			+ Right * (Segment.CarriagewayWidthCm * 0.5 + Settings.SignLateralOffsetCm);
		// Schild auf dem Gehweg: Terrain + Fahrbahn-Offset + Bordstein.
		Base.Z = SampleZ(HeightSampler, Base,
			Settings.RoadSurfaceOffsetCm + Settings.KerbHeightCm);

		FSignInstance Instance;
		Instance.SignId = Sign.Id;
		Instance.Sign = Sign;
		Instance.Location = Base + FVector(0.0, 0.0, Settings.SignHeightAboveGroundCm);
		Instance.Rotation = FRotator(0.0, Dir.Rotation().Yaw, 0.0);
		Instance.SourceSegmentId = Segment.SegmentId;
		Layout.Signs.Add(Instance);
	}
}

bool URoadFurnitureGenerator::WantsDelineators(const FRoadSegment& Segment)
{
	// Nur Klassen, die es ausserorts gibt - Wohn-, Spiel- und Erschliessungs-
	// strassen liegen im Ort, auch wenn sie ungewoehnlich schnell getaggt sind.
	switch (Segment.HighwayType)
	{
	case EOSMHighwayType::Motorway:     case EOSMHighwayType::MotorwayLink:
	case EOSMHighwayType::Trunk:        case EOSMHighwayType::TrunkLink:
	case EOSMHighwayType::Primary:      case EOSMHighwayType::PrimaryLink:
	case EOSMHighwayType::Secondary:    case EOSMHighwayType::SecondaryLink:
	case EOSMHighwayType::Tertiary:     case EOSMHighwayType::TertiaryLink:
	case EOSMHighwayType::Unclassified:
		break;
	default:
		return false;
	}

	// Ausserorts-Signal: Tempo ueber 50 oder ein rural-Tag. Das Tempo ist bei
	// primary bis unclassified nur dann ueber 50, wenn OSM es sagt (maxspeed,
	// maxspeed:type, zone:maxspeed - Vorgabe dieser Klassen ist 50); Autobahn
	// und Kraftfahrstrasse sind schon per Vorgabe schnell.
	if (Segment.MaxSpeedKmh <= 50.0 && !Segment.bRuralTagged)
	{
		return false;
	}

	// Nur ein GETAGGTER Gehweg an der Fahrbahn spricht dagegen. Die bloss
	// angenommene Vorgabe "beidseitig" (RoadTypeLibrary) haette 2110 von 2194
	// secondary-Wegen ausgeschlossen - fast jede Landstrasse. "separate" liegt
	// abseits der Fahrbahn und sperrt nicht. Links/rechts entsteht nur aus
	// einem Tag und gilt darum auch in aelteren Bakes (ohne bSidewalkTagged)
	// als getaggt.
	const bool bGehwegAnDerFahrbahn = Segment.SidewalkType == EOSMSidewalkType::Left
		|| Segment.SidewalkType == EOSMSidewalkType::Right
		|| (Segment.SidewalkType == EOSMSidewalkType::Both && Segment.bSidewalkTagged);
	return !bGehwegAnDerFahrbahn;
}

namespace
{
	/** Waagrechter Abstand eines Punkts zu einer Linie (cm). */
	double AbstandZurLinie(const FVector& P, const TArray<FVector>& Linie)
	{
		double Best = TNumericLimits<double>::Max();
		for (int32 i = 1; i < Linie.Num(); ++i)
		{
			const FVector2D A(Linie[i - 1]), B(Linie[i]), Q(P);
			const FVector2D AB = B - A;
			const double L2 = AB.SizeSquared();
			const double T = L2 > 0.0 ? FMath::Clamp(FVector2D::DotProduct(Q - A, AB) / L2, 0.0, 1.0) : 0.0;
			Best = FMath::Min(Best, FVector2D::Distance(Q, A + AB * T));
		}
		return Best;
	}
}

int32 URoadFurnitureGenerator::RemoveDelineatorsAgainstRule(
	const FRoadNetwork& Network, FRoadFurnitureLayout& Layout)
{
	TMap<int32, const FRoadSegment*> NachId;
	NachId.Reserve(Network.Segments.Num());
	for (const FRoadSegment& Segment : Network.Segments)
	{
		NachId.Add(Segment.SegmentId, &Segment);
	}
	const int32 Vorher = Layout.Delineators.Num();
	Layout.Delineators.RemoveAllSwap([&NachId](const FDelineatorInstance& Pfosten)
	{
		const FRoadSegment* const* Gefunden = NachId.Find(Pfosten.SegmentId);
		if (!Gefunden)
		{
			return false;   // ohne Segment laesst sich nichts pruefen
		}
		const FRoadSegment& Segment = **Gefunden;
		if (!WantsDelineators(Segment))
		{
			return true;
		}
		// Aeltere Bakes setzten die Reihe entlang der UNGEKUERZTEN Linie bis
		// in den Knoten. Wer weiter als eine Pfostenreihe (halbe Fahrbahn +
		// Randabstand, 1 m Luft) von der gekuerzten Linie steht, stand dort.
		if (Segment.TrimmedCenterline.Num() >= 2)
		{
			const double Reihe = Segment.CarriagewayWidthCm * 0.5
				+ WiesbadenRoadMarkings::DelineatorOffsetFromEdgeCm + 100.0;
			return AbstandZurLinie(Pfosten.Location, Segment.TrimmedCenterline) > Reihe;
		}
		return false;
	});
	return Vorher - Layout.Delineators.Num();
}

void URoadFurnitureGenerator::PlaceDelineators(
	const FRoadNetwork& Network,
	const IHeightSampler* HeightSampler,
	const FRoadFurnitureSettings& Settings,
	FRoadFurnitureLayout& Layout)
{
	for (const FRoadSegment& Segment : Network.Segments)
	{
		if (!WantsDelineators(Segment))
		{
			continue;
		}
		// Die an den Kreuzungen GEKUERZTE Linie: die volle Mittellinie laeuft
		// bis in den Knoten, und die Pfostenreihe stand dann in der Fahrbahn
		// der einmuendenden Strasse.
		const TArray<FVector>& Linie = Segment.TrimmedCenterline.Num() >= 2
			? Segment.TrimmedCenterline : Segment.Centerline;
		if (Linie.Num() < 2)
		{
			continue;
		}

		const double Half = Segment.CarriagewayWidthCm * 0.5 + Settings.DelineatorOffsetCm;

		TArray<FVector> Positions;
		TArray<FVector> Directions;
		WalkCenterline(Linie, Settings.DelineatorSpacingCm, Positions, Directions);

		for (int32 i = 0; i < Positions.Num(); ++i)
		{
			const FVector Right = RightPerpendicular(Directions[i]);
			// Leitpfosten stehen am Fahrbahnrand auf dem Gehweg/der Verge.
			const double Z = SampleZ(HeightSampler, Positions[i],
				Settings.RoadSurfaceOffsetCm + Settings.KerbHeightCm);
			const float Yaw = Directions[i].Rotation().Yaw;

			// Rechte Fahrbahnkante.
			FDelineatorInstance RightPost;
			RightPost.Location = FVector(Positions[i].X, Positions[i].Y, Z) + Right * Half;
			RightPost.Rotation = FRotator(0.0f, Yaw + 90.0f, 0.0f);
			RightPost.SegmentId = Segment.SegmentId;
			Layout.Delineators.Add(RightPost);

			// Linke Fahrbahnkante.
			FDelineatorInstance LeftPost;
			LeftPost.Location = FVector(Positions[i].X, Positions[i].Y, Z) - Right * Half;
			LeftPost.Rotation = FRotator(0.0f, Yaw - 90.0f, 0.0f);
			LeftPost.SegmentId = Segment.SegmentId;
			Layout.Delineators.Add(LeftPost);
		}
	}
}

void URoadFurnitureGenerator::PlaceMarkings(
	const FRoadNetwork& Network,
	const IHeightSampler* HeightSampler,
	const FRoadFurnitureSettings& Settings,
	FRoadFurnitureLayout& Layout)
{
	for (const FRoadIntersection& Intersection : Network.Intersections)
	{
		ERoadMarkingKind Kind;
		switch (Intersection.Control)
		{
		case EIntersectionControl::Stop:
			Kind = ERoadMarkingKind::StopLine;
			break;
		case EIntersectionControl::Yield:
			Kind = ERoadMarkingKind::GiveWayLine;
			break;
		default:
			continue;
		}

		for (const FIntersectionArm& Arm : Intersection.Arms)
		{
			const FVector Outward = Arm.OutwardDirection.GetSafeNormal2D();
			// Die Linie verlaueft quer zur Fahrbahn (entlang der rechten Normalen).
			const FVector LineDirection = RightPerpendicular(Outward);

			FVector Center = Intersection.Location
				+ Outward * (Intersection.RadiusCm + Settings.StopLineDistanceCm);
			// Markierungen liegen AUF der Fahrbahn: nur der Fahrbahn-Offset, kein
			// Bordstein (sonst schwebten sie ueber dem Asphalt).
			Center.Z = SampleZ(HeightSampler, Center, Settings.RoadSurfaceOffsetCm);

			FMarkingInstance Marking;
			Marking.Kind = Kind;
			Marking.Center = Center;
			Marking.Direction = LineDirection;
			Marking.LengthCm = Arm.HalfWidthCm * 2.0;
			Marking.WidthCm = Settings.StopLineWidthCm;
			Marking.IntersectionNodeId = Intersection.NodeId;
			Layout.Markings.Add(Marking);
		}
	}

	// -- Tempo-30-Zonen: grosse weisse "30" auf die Fahrbahn ----------------
	//
	// Deutsche 30-Zonen tragen ein aufgemaltes "30" auf dem Asphalt. Als
	// 30-Zone gelten Wohnstrassen und verkehrsberuhigte Bereiche sowie alles
	// mit maxspeed <= 30; Durchgangsstrassen (Primary..Tertiary, i.d.R. 50)
	// bleiben unmarkiert. Die "30" wird entlang der Fahrbahnachse in festem
	// Abstand gesetzt (dieselbe getrimmte Mittellinie, die auch die
	// Fahrbahndecke traegt), mittig auf der Fahrbahn und laengs zur
	// Fahrtrichtung ausgerichtet.
	if (Settings.bPlaceMarkings)
	{
		constexpr double Zone30SpacingCm = 5000.0;     // ~alle 50 m eine "30"
		constexpr double Zone30LengthCm = 420.0;       // Ziffernhoehe (laengs)
		constexpr double Zone30WidthCm = 190.0;        // Ziffernbreite (quer)
		constexpr double Zone30MinSegmentCm = 3000.0;  // Kreuzungsstummel ueberspringen

		for (const FRoadSegment& Segment : Network.Segments)
		{
			if (Segment.bIsArea)
			{
				continue;
			}
			const bool bIsThirtyZone =
				Segment.HighwayType == EOSMHighwayType::LivingStreet ||
				Segment.HighwayType == EOSMHighwayType::Residential ||
				Segment.MaxSpeedKmh <= 30.5;
			if (!bIsThirtyZone)
			{
				continue;
			}

			const TArray<FVector>& Line = (Segment.TrimmedCenterline.Num() >= 2)
				? Segment.TrimmedCenterline : Segment.Centerline;
			if (Line.Num() < 2)
			{
				continue;
			}

			double TotalLen = 0.0;
			for (int32 i = 1; i < Line.Num(); ++i)
			{
				TotalLen += FVector::Dist2D(Line[i - 1], Line[i]);
			}
			if (TotalLen < Zone30MinSegmentCm)
			{
				continue;
			}

			// Ab halbem Abstand vom Segmentanfang, dann alle Zone30SpacingCm.
			double NextAt = Zone30SpacingCm * 0.5;
			double Travelled = 0.0;
			for (int32 i = 1; i < Line.Num(); ++i)
			{
				const FVector A = Line[i - 1];
				const FVector B = Line[i];
				const double SegLen = FVector::Dist2D(A, B);
				if (SegLen <= KINDA_SMALL_NUMBER)
				{
					continue;
				}
				const FVector Tangent = (B - A).GetSafeNormal2D();
				while (NextAt <= Travelled + SegLen)
				{
					const double T = (NextAt - Travelled) / SegLen;
					FVector Center = FMath::Lerp(A, B, T);
					Center.Z = SampleZ(HeightSampler, Center, Settings.RoadSurfaceOffsetCm);

					FMarkingInstance Marking;
					Marking.Kind = ERoadMarkingKind::SpeedZone30;
					Marking.Center = Center;
					Marking.Direction = Tangent;    // laengs zur Fahrtrichtung
					Marking.LengthCm = Zone30LengthCm;
					Marking.WidthCm = Zone30WidthCm;
					Marking.IntersectionNodeId = 0;
					Layout.Markings.Add(Marking);

					NextAt += Zone30SpacingCm;
				}
				Travelled += SegLen;
			}
		}
	}
}

FString FRoadFurnitureReport::ToString() const
{
	if (!bSuccess)
	{
		return ErrorMessage;
	}
	return FString::Printf(
		TEXT("%d Schilder, %d Leitpfosten, %d Markierungen, %d Laternen in %.3f s"),
		SignCount, DelineatorCount, MarkingCount, StreetLampCount, DurationSeconds);
}

void URoadFurnitureGenerator::SynthesiseStreetLamps(
	const FRoadNetwork& Network,
	const IHeightSampler* HeightSampler,
	const FRoadFurnitureSettings& Settings,
	FRoadFurnitureLayout& OutLayout) const
{
	const double Spacing = FMath::Max(Settings.StreetLampSpacingCm, 500.0);

	// Mindestabstand zu einer bereits vorhandenen Leuchte. Etwas kleiner als
	// der Sollabstand, damit kartierte und ergaenzte Leuchten nicht paarweise
	// nebeneinander stehen, aber auch keine Luecke von zwei Feldern entsteht.
	const double MinDistance = Spacing * 0.7;
	const double MinDistanceSq = MinDistance * MinDistance;

	// Raster-Index ueber die vorhandenen Standorte. Ein linearer Vergleich
	// waere hier quadratisch: bei 3.332 kartierten und rund 90.000 ergaenzten
	// Leuchten sind das Milliarden Vergleiche.
	const double CellSize = FMath::Max(MinDistance, 100.0);
	TMultiMap<FIntPoint, FVector2D> Grid;

	auto CellOf = [CellSize](const FVector2D& P)
	{
		return FIntPoint(
			FMath::FloorToInt(P.X / CellSize),
			FMath::FloorToInt(P.Y / CellSize));
	};

	auto AddToGrid = [&Grid, &CellOf](const FVector2D& P)
	{
		Grid.Add(CellOf(P), P);
	};

	auto HasNeighbour = [&Grid, &CellOf, MinDistanceSq](const FVector2D& P)
	{
		const FIntPoint Base = CellOf(P);
		TArray<FVector2D> Found;
		for (int32 Dy = -1; Dy <= 1; ++Dy)
		{
			for (int32 Dx = -1; Dx <= 1; ++Dx)
			{
				Found.Reset();
				Grid.MultiFind(FIntPoint(Base.X + Dx, Base.Y + Dy), Found);
				for (const FVector2D& Other : Found)
				{
					if (FVector2D::DistSquared(P, Other) < MinDistanceSq)
					{
						return true;
					}
				}
			}
		}
		return false;
	};

	for (const FStreetLampInstance& Lamp : OutLayout.StreetLamps)
	{
		AddToGrid(FVector2D(Lamp.Location.X, Lamp.Location.Y));
	}

	int32 Added = 0;

	for (const FRoadSegment& Segment : Network.Segments)
	{
		// Nur befahrbare Strassen. Fuss- und Radwege werden in Deutschland
		// nicht durchgaengig beleuchtet, und eine Laterne je 30 m an jedem
		// Trampelpfad waere weder realistisch noch bezahlbar.
		if (!FOSMTagParser::IsDrivable(Segment.HighwayType))
		{
			continue;
		}

		const TArray<FVector>& Line = Segment.Centerline;
		if (Line.Num() < 2)
		{
			continue;
		}

		// Seitlicher Versatz bis hinter den Bordstein.
		const double SideOffset = Segment.CarriagewayWidthCm * 0.5
			+ Segment.SidewalkWidthCm * 0.5
			+ Settings.StreetLampKerbOffsetCm;

		double DistanceSinceLast = Spacing;   // erste Leuchte gleich am Anfang
		bool bLeftSide = ((Segment.SegmentId & 1) == 0);

		for (int32 Index = 1; Index < Line.Num(); ++Index)
		{
			const FVector& A = Line[Index - 1];
			const FVector& B = Line[Index];

			const FVector2D A2(A.X, A.Y);
			const FVector2D B2(B.X, B.Y);
			const double SegLength = FVector2D::Distance(A2, B2);
			if (SegLength <= UE_DOUBLE_SMALL_NUMBER)
			{
				continue;
			}

			const FVector2D Dir = (B2 - A2) / SegLength;
			const FVector2D Normal = FPolygonUtils::GetLeftNormal(Dir);

			double Travelled = 0.0;
			while (DistanceSinceLast + (SegLength - Travelled) >= Spacing)
			{
				const double Step = Spacing - DistanceSinceLast;
				Travelled += Step;
				DistanceSinceLast = 0.0;

				const FVector2D OnAxis = A2 + Dir * Travelled;
				const FVector2D Side = OnAxis + Normal * (bLeftSide ? SideOffset : -SideOffset);

				// Seiten abwechseln - so wie es an einer realen Strasse
				// ueblich ist und mit halbem Materialaufwand dieselbe
				// Beleuchtungsstaerke erreicht.
				bLeftSide = !bLeftSide;

				if (HasNeighbour(Side))
				{
					continue;
				}

				FStreetLampInstance Lamp;
				Lamp.NodeId = 0;   // 0 = ergaenzt, nicht aus OSM
				Lamp.Location = FVector(Side.X, Side.Y, 0.0);

				if (HeightSampler && HeightSampler->HasValidData())
				{
					// Ergaenzte Laterne auf dem Gehweg: Terrain + Fahrbahn-Offset +
					// Bordstein (wie die kartierten Laternen und Schilder).
					Lamp.Location.Z = HeightSampler->SampleHeightCm(Side)
						+ Settings.RoadSurfaceOffsetCm + Settings.KerbHeightCm;
				}
				else
				{
					// Ohne Hoehenmodell die Hoehe der Mittellinie uebernehmen (die
					// enthaelt den Fahrbahn-Offset bereits) plus den Bordstein.
					Lamp.Location.Z = FMath::Lerp(A.Z, B.Z, Travelled / SegLength)
						+ Settings.KerbHeightCm;
				}

				OutLayout.StreetLamps.Add(Lamp);
				AddToGrid(Side);
				++Added;
			}

			DistanceSinceLast += SegLength - Travelled;
		}
	}

	if (Added > 0)
	{
		UE_LOG(LogWbRoads, Log,
			TEXT("Strassenlaternen ergaenzt: %d zusaetzlich (Sollabstand %.0f m), ")
			TEXT("insgesamt %d."),
			Added, Spacing / 100.0, OutLayout.StreetLamps.Num());
	}
}

int32 URoadFurnitureGenerator::GetFurnitureVariantCount(EStreetFurnitureKind Kind)
{
	// Genau die Varianten, die der Renderer auch BAUT (StreetFurnitureShapes):
	// die Bank mit und ohne Lehne, der Poller mit und ohne Reflektorring. Das
	// Spec nennt mehr - Glas/Papier/Textil beim Recycling etwa -, aber solange
	// die Formen sie nicht unterscheiden, waere eine groessere Zahl hier ein
	// Versprechen auf Vielfalt, das niemand einloest: die Instanzen truegen
	// eine Variantennummer, und jede saehe gleich aus.
	switch (Kind)
	{
	case EStreetFurnitureKind::Bench:   return 2;   // mit/ohne Lehne
	case EStreetFurnitureKind::Bollard: return 2;   // mit/ohne Reflektorring
	default:                            return 1;
	}
}

bool URoadFurnitureGenerator::TryParseFurnitureKind(const FString& Tag, EStreetFurnitureKind& OutKind)
{
	for (const FFurnitureKindName& Entry : FurnitureKindNames)
	{
		if (Tag.Equals(Entry.Value, ESearchCase::IgnoreCase))
		{
			OutKind = Entry.Kind;
			return true;
		}
	}
	return false;
}

void URoadFurnitureGenerator::PlaceStreetFurniture(
	const FRoadNetwork& Network,
	const FOSMDataSet* DataSet,
	const UGeoCoordinateConverter* Converter,
	const IHeightSampler* HeightSampler,
	const TArray<FGeneratedBuilding>* Buildings,
	const FRoadFurnitureSettings& Settings,
	FRoadFurnitureLayout& OutLayout,
	FRoadFurnitureReport& OutReport) const
{
	if (!DataSet || !Converter || !Converter->IsInitialized())
	{
		return;
	}

	// Deterministische Reihenfolge ueber die sortierten Knoten-Ids: zwei Bakes
	// derselben Daten muessen dieselbe Liste ergeben (TMap-Reihenfolge ist es
	// nicht).
	TArray<FOSMId> NodeIds;
	TMap<FOSMId, EStreetFurnitureKind> KindByNode;
	for (const TPair<FOSMId, FOSMNode>& Pair : DataSet->Nodes)
	{
		// Nur das Tag des Nachzugs. Die rohen OSM-Tags (amenity=bench und so
		// fort) werden bewusst NICHT mehr ausgewertet: die Quelldatei ohne
		// Nachzug traegt sie fast nirgends (kein einziger Briefkasten, je ein
		// Korb/Recycling/Automat/Picknicktisch), und ein Zehntel Bestand aus
		// der falschen Datei waere schlimmer als gar keiner - letzterer faellt
		// wenigstens auf.
		EStreetFurnitureKind Kind = EStreetFurnitureKind::Bench;
		const FString KindTag = Pair.Value.GetTag(FurnitureKindTagName);
		if (!KindTag.IsEmpty() && TryParseFurnitureKind(KindTag, Kind))
		{
			NodeIds.Add(Pair.Key);
			KindByNode.Add(Pair.Key, Kind);
		}
	}

	if (NodeIds.Num() == 0)
	{
		// Ohne diese Zeile bliebe ein Bake auf der Datei OHNE Nachzug still:
		// keine Moebel, kein Hinweis - und ein fehlender Abfallkorb faellt im
		// Spiel niemandem auf.
		UE_LOG(LogWbRoads, Warning,
			TEXT("Strassenmoebel: kein Knoten traegt wb:furniture - wurde ohne den ")
			TEXT("Nachzug gebacken? (Tools/fetch_street_furniture.py)"));
		return;
	}
	NodeIds.Sort();

	FNearestWayIndex Ways;
	Ways.Build(Network);

	FBuildingFootprintGrid BuildingGrid;
	if (Buildings && Buildings->Num() > 0)
	{
		BuildingGrid.Build(*Buildings);
	}

	const double DockingRange = FMath::Max(0.0, Settings.FurnitureDockingRangeCm);
	int32 InBuilding = 0;
	int32 Docked = 0;
	int32 WithoutEdge = 0;

	OutLayout.Furniture.Reserve(OutLayout.Furniture.Num() + NodeIds.Num());

	for (const FOSMId NodeId : NodeIds)
	{
		const FOSMNode* Node = DataSet->Nodes.Find(NodeId);
		if (!Node)
		{
			continue;
		}

		const EStreetFurnitureKind Kind = KindByNode[NodeId];
		const FVector World = Converter->GeoToUnrealGround(Node->Location);
		FVector2D Position(World.X, World.Y);

		// Regel 2: im Gebaeude = Verortungsfehler.
		if (BuildingGrid.Contains(Position))
		{
			++InBuilding;
			continue;
		}

		FNearestWay Way;
		if (!Ways.Find(Position, Way))
		{
			// Weit und breit kein Weg - das ist kein Strassenrand mehr.
			++WithoutEdge;
			continue;
		}

		// Regel 3 + 4: vom Fahrweg herunter und an den befestigten Rand.
		// Der Poller ist die Ausnahme - er steht dort mit Absicht.
		//
		// Zielband ist die MITTE des Gehwegs; hat der Weg keinen Gehweg
		// (Feldweg, Fussweg), ein halber Meter neben der Kante.
		const bool bMayStandOnWay = (Kind == EStreetFurnitureKind::Bollard);
		const double EdgeCm = Way.HalfWidthCm;

		// Wege OHNE Gehweg bekommen ein BANKETT als Ersatzbreite.
		//
		// RoadTypeLibrary gibt footway, path und track die Gehwegbreite 0,0 -
		// dort richtig, hier folgenschwer: der "befestigte Streifen" ist dann
		// nur so breit wie der Weg, bei 1,80 m Fussweg also 0,90 m ab Achse.
		// Eine Bank einen Meter daneben lag damit ausserhalb. 78 Prozent der
		// 1464 verworfenen Knoten hingen genau daran.
		// Das Bankett gilt NUR neben Wegen, neben denen wirklich einer liegt.
		// Gehwegbreite 0 steht auch an der Autobahn; dort waere eine
		// herangezogene Bank kein gerettetes Moebel, sondern ein Hindernis
		// einen halben Meter neben 130 km/h.
		const double BelagCm = Way.SidewalkWidthCm > KINDA_SMALL_NUMBER
			? Way.SidewalkWidthCm
			: (Way.bBankettErlaubt ? FMath::Max(0.0, Settings.FurnitureVergeCm) : 0.0);
		const double PavedCm = Way.HalfWidthCm + BelagCm;

		// Das ZIELBAND bleibt schmal: wer herangezogen werden MUSS, landet auf
		// dem Gehweg bzw. einen halben Meter neben der Wegkante - nicht
		// mitten im Bankett. Das Bankett erweitert nur, was noch als "am Weg"
		// gilt; wer darin liegt, bleibt ohnehin stehen, wo er kartiert ist.
		const double TargetCm = Way.SidewalkWidthCm > KINDA_SMALL_NUMBER
			? Way.HalfWidthCm + Way.SidewalkWidthCm * 0.5
			: Way.HalfWidthCm + 50.0;

		if (Way.DistanceCm < EdgeCm && !bMayStandOnWay)
		{
			// Auf dem Fahrweg: hinausschieben. Die Strecke ist hoechstens eine
			// halbe Fahrbahnbreite und damit sicher erlaubt - ein Moebel, das
			// auf der Fahrbahn STEHT, ist immer ein Fehler, und Versetzen ist
			// besser als Loeschen (dieselbe Entscheidung wie bei den Schildern).
			Position = Position + Way.AwayDirection * (TargetCm - Way.DistanceCm);
			++Docked;
		}
		else if (Way.DistanceCm > PavedCm)
		{
			// Neben dem befestigten Streifen: heranziehen, wenn der RAND in
			// Reichweite liegt - sonst verwerfen (die "in der Wiese
			// schwebende Bank").
			//
			// Gemessen wird bis zur KANTE des befestigten Streifens, nicht bis
			// zum Zielband in seiner Mitte: sonst fraesse die halbe Gehwegbreite
			// die 1,5 m Reichweite auf, und auf einem breiten Gehweg bliebe
			// von der Regel nichts uebrig.
			if (Way.DistanceCm - PavedCm > DockingRange)
			{
				++WithoutEdge;
				continue;
			}
			Position = Position - Way.AwayDirection * (Way.DistanceCm - TargetCm);
			++Docked;
		}

		FFurnitureInstance Instance;
		Instance.Kind = Kind;
		Instance.NodeId = NodeId;

		// Moebel stehen auf dem Gehweg: Terrain + Fahrbahn-Offset + Bordstein.
		const double GroundZ = HeightSampler && HeightSampler->HasValidData()
			? HeightSampler->SampleHeightCm(Position) + Settings.RoadSurfaceOffsetCm + Settings.KerbHeightCm
			: World.Z;
		Instance.Location = FVector(Position.X, Position.Y, GroundZ);

		// Variante und der kleine Ausrichtungs-Versatz kommen aus der Knoten-Id:
		// gestreut, aber bei jedem Bake dieselbe Streuung.
		const uint32 Hash = GetTypeHash(NodeId);
		Instance.Variant = static_cast<int32>(Hash % static_cast<uint32>(
			FMath::Max(1, GetFurnitureVariantCount(Kind))));

		{
			// Blickrichtung ZUR Fahrbahn = Gegenrichtung von "weg vom Weg".
			const FVector2D ToWay = -Way.AwayDirection;
			const double YawToWay = FMath::RadiansToDegrees(FMath::Atan2(ToWay.Y, ToWay.X));

			// Regel 5, die Ausrichtungen:
			//  - Bank und Picknick-Tisch: Ruecken zur Strasse, Blick ins Gruene.
			//  - Korb, Automat, Briefkasten, Recycling: werden vom Gehweg aus
			//    benutzt, zeigen also ebenfalls von der Fahrbahn weg.
			//  - Hydrant: die Feuerwehr kuppelt von der Strasse her an - er
			//    zeigt als einziger ZUR Fahrbahn.
			//  - Poller: steht laengs der Kante, quer zur Blickrichtung.
			double Yaw = YawToWay + 180.0;
			if (Kind == EStreetFurnitureKind::FireHydrant)
			{
				Yaw = YawToWay;
			}
			else if (Kind == EStreetFurnitureKind::Bollard)
			{
				// Laengs der Kante heisst: die Achse des Wegs selbst - nicht
				// "quer zur Blickrichtung", denn bei einem Poller AUF dem Weg
				// zeigt die Blickrichtung irgendwohin.
				Yaw = FMath::RadiansToDegrees(
					FMath::Atan2(Way.AxisDirection.Y, Way.AxisDirection.X));
			}

			// +-5 Grad Streuung: eine Reihe exakt gleich gedrehter Baenke sieht
			// gestempelt aus, und im Vorbild steht keine genau parallel.
			const double Jitter = (static_cast<double>(Hash % 1001u) / 1000.0 - 0.5) * 10.0;
			Instance.Rotation = FRotator(0.0, Yaw + Jitter, 0.0);
		}

		OutLayout.Furniture.Add(Instance);
	}

	OutReport.FurnitureCount = OutLayout.Furniture.Num();
	OutReport.FurnitureInBuildingCount = InBuilding;
	OutReport.FurnitureDockedCount = Docked;
	OutReport.FurnitureWithoutEdgeCount = WithoutEdge;

	UE_LOG(LogWbRoads, Log,
		TEXT("Strassenmoebel: %d von %d OSM-Knoten uebernommen (%d angedockt, ")
		TEXT("%d im Gebaeude verworfen, %d ohne befestigten Rand verworfen)."),
		OutLayout.Furniture.Num(), NodeIds.Num(), Docked, InBuilding, WithoutEdge);
}
