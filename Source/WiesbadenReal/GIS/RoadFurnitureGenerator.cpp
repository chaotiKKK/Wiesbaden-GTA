// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/RoadFurnitureGenerator.h"

#include "WiesbadenReal.h"

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
}

FRoadFurnitureReport URoadFurnitureGenerator::Generate(
	const FRoadNetwork& Network,
	const FOSMDataSet* DataSet,
	const UGeoCoordinateConverter* Converter,
	const IHeightSampler* HeightSampler,
	const FRoadFurnitureSettings& Settings,
	FRoadFurnitureLayout& OutLayout)
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

void URoadFurnitureGenerator::PlaceDelineators(
	const FRoadNetwork& Network,
	const IHeightSampler* HeightSampler,
	const FRoadFurnitureSettings& Settings,
	FRoadFurnitureLayout& Layout)
{
	for (const FRoadSegment& Segment : Network.Segments)
	{
		if (!FOSMTagParser::IsDrivable(Segment.HighwayType) || Segment.Centerline.Num() < 2)
		{
			continue;
		}

		const double Half = Segment.CarriagewayWidthCm * 0.5 + Settings.DelineatorOffsetCm;

		TArray<FVector> Positions;
		TArray<FVector> Directions;
		WalkCenterline(Segment.Centerline, Settings.DelineatorSpacingCm, Positions, Directions);

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
