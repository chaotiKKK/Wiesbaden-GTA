// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/RoadNetworkGenerator.h"

#include "WiesbadenReal.h"

#include "Algo/Reverse.h"
#include "GIS/GeoCoordinateConverter.h"
#include "GIS/PolygonUtils.h"
#include "GIS/RoadTypeLibrary.h"

namespace
{
	// Kreuzungs-Bilanz. Ohne Zahlen laesst sich nicht sagen, wie viele
	// Kreuzungen ohne Fahrbahnflaeche blieben - der Abbruch bei gescheiterter
	// Triangulierung war eine Verbose-Meldung, also unsichtbar.
	int32 GIntersectionFanFallbackCount = 0;

	/**
	 * Dreiecke der Kreuzungsplatten, deren Umlaufrichtung gedreht werden musste.
	 *
	 * Ohne Zahl bliebe offen, ob das ein Randfall war oder der Regelfall - und
	 * genau daran haengt, ob die Kreuzungen ueberhaupt sichtbar sind.
	 */
	int32 GIntersectionFlippedTriangles = 0;

	/** Bilanz der Flaechen (Plaetze, Fussgaengerzonen). */
	int32 GAreaBuilt = 0;
	int32 GAreaTriangles = 0;
	int32 GAreaFanFallback = 0;
	int32 GAreaTooFewPoints = 0;

	/**
	 * Bandenden, die an ein Kreuzungstor angeschlossen wurden, und wie viele
	 * es sein koennten.
	 *
	 * Ohne diese Zahl bliebe offen, ob der Anschluss ueberhaupt greift. Ein
	 * Segment ohne Kreuzung an einem Ende (Sackgasse, Kartenrand) hat dort
	 * kein Tor - der Anteil kann deshalb nie 100 Prozent erreichen.
	 */
	int32 GSegmentGatesApplied = 0;
	int32 GSegmentGatesPossible = 0;

	/**
	 * Wie weit ein Bandende beim Anschluss an das Tor verschoben wurde.
	 *
	 * Das ist die eigentliche Aussage: Ein kleiner Wert heisst, dass Band und
	 * Kreuzung ohnehin fast zusammenpassten und der Anschluss nur die letzten
	 * Zentimeter schliesst. Ein Wert in der Groessenordnung der Fahrbahnbreite
	 * hiesse, dass links und rechts vertauscht sind - das Band waere verdreht.
	 */
	double GGateSnapSumCm = 0.0;
	double GGateSnapWorstCm = 0.0;
	int32 GGateSnapCount = 0;
	int32 GIntersectionTooFewPointsCount = 0;

	/** Farbcodierung im Vertex-Alpha: Nasswerte fuer die Regen-Shader. */
	constexpr uint8 DefaultWetnessMask = 255;

	/** Umrechnungsfaktor Meter -> Unreal Units. */
	constexpr double MetersToCm = 100.0;

	/**
	 * Winkel eines 2D-Vektors gegen +X in Grad, [0, 360).
	 * Wird zur Sortierung der Kreuzungsarme gebraucht.
	 */
	double BearingDegrees(const FVector2D& Direction)
	{
		double Degrees = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
		if (Degrees < 0.0)
		{
			Degrees += 360.0;
		}
		return Degrees;
	}

	/** Quadratische Bezierkurve, gleichmaessig abgetastet. */
	void SampleQuadraticBezier(
		const FVector& Start,
		const FVector& Control,
		const FVector& End,
		int32 SampleCount,
		TArray<FVector>& OutPoints)
	{
		OutPoints.Reset();
		OutPoints.Reserve(SampleCount + 1);

		for (int32 Index = 0; Index <= SampleCount; ++Index)
		{
			const double T = static_cast<double>(Index) / static_cast<double>(SampleCount);
			const double OneMinusT = 1.0 - T;

			OutPoints.Add(
				Start * (OneMinusT * OneMinusT)
				+ Control * (2.0 * OneMinusT * T)
				+ End * (T * T));
		}
	}
}

void FRoadMeshSection::Append(const FRoadMeshSection& Other)
{
	const int32 IndexOffset = Vertices.Num();

	Vertices.Append(Other.Vertices);
	Normals.Append(Other.Normals);
	UVs.Append(Other.UVs);
	VertexColors.Append(Other.VertexColors);
	Tangents.Append(Other.Tangents);

	Triangles.Reserve(Triangles.Num() + Other.Triangles.Num());
	for (const int32 Index : Other.Triangles)
	{
		Triangles.Add(Index + IndexOffset);
	}
}

FString FRoadGenerationReport::ToString() const
{
	if (!bSuccess)
	{
		return FString::Printf(TEXT("Strassennetz FEHLGESCHLAGEN: %s"), *ErrorMessage);
	}

	return FString::Printf(
		TEXT("Strassennetz erzeugt in %.2f s: %d/%d Ways verarbeitet (%d unvollstaendig), ")
		TEXT("%d Segmente, %d Spuren, %d Kreuzungen, %d Verbindungen (%d gesperrt), ")
		TEXT("%d Vertices, %d Dreiecke"),
		DurationSeconds, ProcessedWayCount, ProcessedWayCount + SkippedWayCount, IncompleteWayCount,
		SegmentCount, LaneCount, IntersectionCount, ConnectionCount, RestrictedConnectionCount,
		VertexCount, TriangleCount);
}

ETurnType URoadNetworkGenerator::ClassifyTurn(const FVector& IncomingDirection, const FVector& OutgoingDirection)
{
	const FVector2D In(IncomingDirection.X, IncomingDirection.Y);
	const FVector2D Out(OutgoingDirection.X, OutgoingDirection.Y);

	if (In.IsNearlyZero() || Out.IsNearlyZero())
	{
		return ETurnType::Through;
	}

	const FVector2D InNorm = In.GetSafeNormal();
	const FVector2D OutNorm = Out.GetSafeNormal();

	// Kreuzprodukt-Z bestimmt die Drehrichtung. Unreal ist linkshaendig mit
	// +Y nach Sueden: ein positives Kreuzprodukt entspricht damit einer
	// Rechtsdrehung in der Draufsicht.
	const double CrossZ = InNorm.X * OutNorm.Y - InNorm.Y * OutNorm.X;
	const double Dot = FVector2D::DotProduct(InNorm, OutNorm);

	const double AngleDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.0, 1.0)));

	// Schwellen orientieren sich an dem, was ein Fahrer als Abbiegevorgang
	// wahrnimmt: bis 20 Grad ist es eine Kurve, kein Abbiegen.
	if (AngleDegrees < 20.0)
	{
		return ETurnType::Through;
	}
	if (AngleDegrees > 150.0)
	{
		return ETurnType::UTurn;
	}

	const bool bIsRight = CrossZ > 0.0;

	if (AngleDegrees < 50.0)
	{
		return bIsRight ? ETurnType::SlightRight : ETurnType::SlightLeft;
	}
	if (AngleDegrees < 115.0)
	{
		return bIsRight ? ETurnType::Right : ETurnType::Left;
	}

	return bIsRight ? ETurnType::SharpRight : ETurnType::SharpLeft;
}

uint8 URoadNetworkGenerator::ParseTurnIndication(const FString& Value)
{
	if (Value.IsEmpty())
	{
		return static_cast<uint8>(ETurnIndication::None);
	}

	// Ein Spurwert kann mehrere durch ";" getrennte Angaben tragen,
	// z. B. "through;right" fuer eine kombinierte Geradeaus-Rechts-Spur.
	TArray<FString> Parts;
	Value.ParseIntoArray(Parts, TEXT(";"), /*InCullEmpty=*/true);

	uint8 Flags = 0;

	for (const FString& RawPart : Parts)
	{
		const FString Part = RawPart.TrimStartAndEnd().ToLower();

		if (Part == TEXT("through")) { Flags |= static_cast<uint8>(ETurnIndication::Through); }
		else if (Part == TEXT("left")) { Flags |= static_cast<uint8>(ETurnIndication::Left); }
		else if (Part == TEXT("right")) { Flags |= static_cast<uint8>(ETurnIndication::Right); }
		else if (Part == TEXT("slight_left")) { Flags |= static_cast<uint8>(ETurnIndication::SlightLeft); }
		else if (Part == TEXT("slight_right")) { Flags |= static_cast<uint8>(ETurnIndication::SlightRight); }
		else if (Part == TEXT("sharp_left")) { Flags |= static_cast<uint8>(ETurnIndication::SharpLeft); }
		else if (Part == TEXT("sharp_right")) { Flags |= static_cast<uint8>(ETurnIndication::SharpRight); }
		else if (Part == TEXT("reverse") || Part == TEXT("merge_to_left") || Part == TEXT("merge_to_right"))
		{
			// "reverse" ist Wenden; die Merge-Werte beschreiben Spurendungen
			// und geben keine Abbiegerichtung vor.
			if (Part == TEXT("reverse"))
			{
				Flags |= static_cast<uint8>(ETurnIndication::UTurn);
			}
			else
			{
				Flags |= static_cast<uint8>(ETurnIndication::Through);
			}
		}
		else if (Part == TEXT("none"))
		{
			Flags |= static_cast<uint8>(ETurnIndication::Through);
		}
	}

	// Eine Spur ohne jede erkannte Angabe bleibt geradeaus befahrbar - sonst
	// waere sie fuer die KI eine Sackgasse.
	if (Flags == 0)
	{
		Flags = static_cast<uint8>(ETurnIndication::Through);
	}

	return Flags;
}

void URoadNetworkGenerator::CountNodeReferences(const FOSMDataSet& DataSet, TMap<FOSMId, int32>& OutCounts) const
{
	OutCounts.Reset();
	OutCounts.Reserve(DataSet.Ways.Num() * 4);

	for (const TPair<FOSMId, FOSMWay>& WayPair : DataSet.Ways)
	{
		const FOSMWay& Way = WayPair.Value;
		if (!Way.IsHighway())
		{
			continue;
		}

		const EOSMHighwayType Type = FOSMTagParser::ParseHighwayType(Way.GetTag(TEXT("highway")));
		if (Type == EOSMHighwayType::None)
		{
			continue;
		}

		for (const FOSMId NodeId : Way.NodeIds)
		{
			++OutCounts.FindOrAdd(NodeId, 0);
		}
	}
}

FRoadGenerationReport URoadNetworkGenerator::Generate(
	const FOSMDataSet& DataSet,
	const UGeoCoordinateConverter* Converter,
	const URoadTypeLibrary* TypeLibrary,
	const IHeightSampler* HeightSampler,
	const FRoadGenerationSettings& Settings,
	FRoadNetwork& OutNetwork,
	FRoadMeshData* OutMeshData)
{
	FRoadGenerationReport Report;
	const double StartTime = FPlatformTime::Seconds();

	// -- Eingangspruefungen -------------------------------------------------

	if (bGenerationInProgress)
	{
		Report.ErrorMessage = TEXT("Generate() ist nicht wiedereintrittsfaehig und laeuft bereits.");
		UE_LOG(LogWbRoads, Error, TEXT("%s"), *Report.ErrorMessage);
		return Report;
	}

	if (!Converter || !Converter->IsInitialized())
	{
		Report.ErrorMessage = TEXT("Georeferenzierung fehlt oder ist nicht initialisiert.");
		UE_LOG(LogWbRoads, Error, TEXT("%s"), *Report.ErrorMessage);
		return Report;
	}

	if (!TypeLibrary)
	{
		Report.ErrorMessage = TEXT("Strassentyp-Bibliothek fehlt.");
		UE_LOG(LogWbRoads, Error, TEXT("%s"), *Report.ErrorMessage);
		return Report;
	}

	if (DataSet.Ways.Num() == 0)
	{
		Report.ErrorMessage = TEXT("OSM-Datensatz enthaelt keine Ways.");
		UE_LOG(LogWbRoads, Error, TEXT("%s"), *Report.ErrorMessage);
		return Report;
	}

	TGuardValue<bool> ReentrancyGuard(bGenerationInProgress, true);

	OutNetwork.Reset();
	WorkingCenterlines2D.Reset();
	WorkingTrimmed2D.Reset();

	if (OutMeshData)
	{
		OutMeshData->Reset();
	}

	UE_LOG(LogWbRoads, Log, TEXT("Starte Strassennetz-Erzeugung aus %s."), *DataSet.GetStatisticsString());

	// -- Schritt 1+2: Ways filtern und an Verzweigungen zerlegen ------------

	TMap<FOSMId, int32> NodeReferenceCounts;
	CountNodeReferences(DataSet, NodeReferenceCounts);

	// Deterministische Reihenfolge: TMap-Iteration ist es nicht, und ein
	// nichtdeterministisches Netz waere weder reproduzierbar debugbar noch
	// im Multiplayer zwischen Clients identisch.
	TArray<FOSMId> SortedWayIds;
	SortedWayIds.Reserve(DataSet.Ways.Num());
	for (const TPair<FOSMId, FOSMWay>& Pair : DataSet.Ways)
	{
		SortedWayIds.Add(Pair.Key);
	}
	SortedWayIds.Sort();

	TArray<FGeoCoordinate> WayCoords;

	for (const FOSMId WayId : SortedWayIds)
	{
		const FOSMWay& Way = DataSet.Ways[WayId];

		if (!Way.IsHighway())
		{
			continue;
		}

		const EOSMHighwayType HighwayType = FOSMTagParser::ParseHighwayType(Way.GetTag(TEXT("highway")));
		if (HighwayType == EOSMHighwayType::None)
		{
			++Report.SkippedWayCount;
			++Report.SkippedUnknownTypeCount;
			continue;
		}

		const bool bDrivable = FOSMTagParser::IsDrivable(HighwayType);
		if (!bDrivable && !Settings.bGenerateFootways)
		{
			++Report.SkippedWayCount;
			++Report.SkippedNotDrivableCount;
			continue;
		}

		// FLAECHE oder Band?
		//
		// Ein Platz ist in OSM ein geschlossener Weg mit area=yes, oder ein
		// geschlossener Ring mit highway=pedestrian/footway. Als Band gebaut
		// ergibt er einen Pfad um sich selbst herum - im Spiel ein Ring mit
		// Wiese in der Mitte.
		//
		// Bewusst NICHT dabei: geschlossene service-Wege. Das sind meist
		// Ringstrassen auf Parkplaetzen, also echte Fahrwege, keine Flaechen.
		const bool bAreaTag = Way.GetTag(TEXT("area")).Equals(TEXT("yes"), ESearchCase::IgnoreCase);
		const bool bClosedRing = Way.NodeIds.Num() > 3 && Way.NodeIds[0] == Way.NodeIds.Last();
		const bool bPedestrianType =
			HighwayType == EOSMHighwayType::Pedestrian || HighwayType == EOSMHighwayType::Footway;

		const bool bIsAreaWay = bAreaTag || (bClosedRing && bPedestrianType);

		// Bauliche Sperrungen: ein Way mit access=no ist zwar vorhanden, aber
		// fuer die Verkehrs-KI nicht nutzbar. Die Geometrie wird trotzdem
		// erzeugt, damit die Strasse sichtbar bleibt.
		int32 MissingNodes = 0;
		if (!DataSet.ResolveWayCoordinates(Way, WayCoords, MissingNodes))
		{
			++Report.SkippedWayCount;
			++Report.SkippedUnresolvedCount;
			continue;
		}

		if (MissingNodes > 0)
		{
			++Report.IncompleteWayCount;
		}

		// Nur die tatsaechlich aufgeloesten Nodes weiterverwenden - die
		// Node-ID-Liste muss dazu passend gefiltert werden, sonst stimmt die
		// Zuordnung Punkt <-> Knoten nicht mehr und die Kreuzungen landen an
		// falschen Stellen.
		TArray<FOSMId> ResolvedNodeIds;
		TArray<FVector2D> Points2D;
		ResolvedNodeIds.Reserve(Way.NodeIds.Num());
		Points2D.Reserve(Way.NodeIds.Num());

		for (const FOSMId NodeId : Way.NodeIds)
		{
			const FOSMNode* Node = DataSet.Nodes.Find(NodeId);
			if (!Node)
			{
				continue;
			}

			const FVector World = Converter->GeoToUnrealGround(Node->Location);
			ResolvedNodeIds.Add(NodeId);
			Points2D.Add(FVector2D(World.X, World.Y));
		}

		if (Points2D.Num() < 2)
		{
			++Report.SkippedWayCount;
			++Report.SkippedTooFewPointsCount;
			continue;
		}

		++Report.ProcessedWayCount;
		const int32 SegmentsBeforeWay = OutNetwork.Segments.Num();

		// Attribute einmal je Way aufloesen und auf alle Segmente uebertragen.
		const EOSMOnewayType Oneway = FOSMTagParser::ParseOneway(Way);

		int32 ForwardLanes = 1;
		int32 BackwardLanes = 1;
		TypeLibrary->ResolveLaneCounts(Way, HighwayType, Oneway, ForwardLanes, BackwardLanes);

		const double CarriagewayWidthM =
			TypeLibrary->ResolveCarriagewayWidthMeters(Way, HighwayType, ForwardLanes, BackwardLanes);
		const double MaxSpeed = TypeLibrary->ResolveMaxSpeedKmh(Way, HighwayType);
		const EOSMSidewalkType Sidewalk = TypeLibrary->ResolveSidewalk(Way, HighwayType);
		const EOSMSurfaceType Surface = TypeLibrary->ResolveSurface(Way, HighwayType);
		const FRoadTypeDefinition& TypeDef = TypeLibrary->GetDefinition(HighwayType);

		// Zerlegungspunkte bestimmen: Anfang, Ende und jeder Innenknoten, der
		// von mindestens zwei Ways benutzt wird.
		TArray<int32> SplitIndices;
		SplitIndices.Add(0);

		// FLAECHEN werden NICHT zerlegt.
		//
		// Ein Platz ist ein geschlossener Ring. Wird er an jedem beruehrenden
		// Knoten zerschnitten, entstehen Teilstuecke mit OFFENEM Umriss - und
		// jedes davon wuerde als eigene Flaeche trianguliert. Gemessen ergab
		// das 731 "Flaechen" statt der 184, die es in den Daten gibt, davon
		// 137 zu klein fuer einen Umriss. Was dabei entsteht, ist Unsinn.
		//
		// Der Ring bleibt deshalb ganz. Er traegt ohnehin keinen Verkehr: Die
		// Zerlegung dient dem Spurgraphen, und eine Fussgaengerzone ist kein
		// Fahrweg.
		if (!bIsAreaWay)
		{
			for (int32 Index = 1; Index < ResolvedNodeIds.Num() - 1; ++Index)
			{
				const int32* RefCount = NodeReferenceCounts.Find(ResolvedNodeIds[Index]);
				if (RefCount && *RefCount >= 2)
				{
					SplitIndices.Add(Index);
				}
			}
		}

		SplitIndices.Add(ResolvedNodeIds.Num() - 1);

		for (int32 SplitIndex = 0; SplitIndex + 1 < SplitIndices.Num(); ++SplitIndex)
		{
			const int32 FirstPoint = SplitIndices[SplitIndex];
			const int32 LastPoint = SplitIndices[SplitIndex + 1];

			if (LastPoint <= FirstPoint)
			{
				++Report.DroppedSubSegmentCount;
				continue;
			}

			TArray<FVector2D> RawSegment;
			RawSegment.Reserve(LastPoint - FirstPoint + 1);
			for (int32 Index = FirstPoint; Index <= LastPoint; ++Index)
			{
				RawSegment.Add(Points2D[Index]);
			}

			// Digitalisierungsrauschen entfernen, dann glaetten, dann fuer die
			// Terrainfolge unterteilen. Diese Reihenfolge ist wesentlich:
			// glaettet man vor dem Vereinfachen, arbeitet Chaikin auf dem
			// Rauschen; unterteilt man vor dem Glaetten, hat Chaikin keinen
			// Effekt mehr, weil die Segmente schon kurz sind.
			TArray<FVector2D> Simplified;
			FPolygonUtils::SimplifyPolyline(RawSegment, Settings.SimplificationToleranceCm, Simplified);

			TArray<FVector2D> Smoothed;
			if (Settings.SmoothingIterations > 0 && Simplified.Num() >= 3)
			{
				FPolygonUtils::SmoothPolylineChaikin(Simplified, Settings.SmoothingIterations, Smoothed, false);
			}
			else
			{
				Smoothed = MoveTemp(Simplified);
			}

			TArray<FVector2D> Resampled;
			FPolygonUtils::ResamplePolyline(Smoothed, Settings.MaxSegmentLengthCm, Resampled);

			if (Resampled.Num() < 2)
			{
				++Report.DroppedSubSegmentCount;
				continue;
			}

			FRoadSegment Segment;
			Segment.SegmentId = OutNetwork.Segments.Num();
			Segment.SourceWayId = Way.Id;
			Segment.StreetName = Way.GetTag(TEXT("name"));
			Segment.RoadReference = Way.GetTag(TEXT("ref"));
			Segment.HighwayType = HighwayType;
			Segment.Surface = Surface;
			Segment.Oneway = Oneway;
			Segment.SidewalkType = Settings.bGenerateSidewalks ? Sidewalk : EOSMSidewalkType::None;
			Segment.ForwardLaneCount = ForwardLanes;
			Segment.BackwardLaneCount = BackwardLanes;
			Segment.CarriagewayWidthCm = CarriagewayWidthM * MetersToCm;
			Segment.SidewalkWidthCm = TypeDef.SidewalkWidthMeters * MetersToCm;
			Segment.KerbHeightCm = TypeDef.KerbHeightMeters * MetersToCm;
			Segment.MaxSpeedKmh = MaxSpeed;
			Segment.Layer = Way.GetLayer();
			Segment.bIsBridge = Way.IsBridge();

			// Flaechen behalten ihren Umriss.
			//
			// Die uebrige Pipeline (Spuren, Kreuzungen, Verkehr) arbeitet auf
			// Centerline. Fuer eine Flaeche ist die der UMRISS - daraus wuerde
			// ein Rundkurs, an dem sich Fahrzeuge ausrichten. Der Umriss liegt
			// deshalb in einem eigenen Feld.
			Segment.bIsArea = bIsAreaWay;
			if (bIsAreaWay)
			{
				Segment.AreaOutline.Reserve(Resampled.Num());
				for (const FVector2D& Point : Resampled)
				{
					Segment.AreaOutline.Add(FVector(Point.X, Point.Y, 0.0));
				}
			}
			Segment.bIsTunnel = Way.IsTunnel();
			Segment.bIsRoundabout = Way.IsRoundabout();
			Segment.StartNodeId = ResolvedNodeIds[FirstPoint];
			Segment.EndNodeId = ResolvedNodeIds[LastPoint];
			Segment.LengthCm = FPolygonUtils::ComputePolylineLength(Resampled);

			if (!Segment.StreetName.IsEmpty())
			{
				OutNetwork.SegmentsByName.FindOrAdd(Segment.StreetName.ToLower()).Add(Segment.SegmentId);
			}

			OutNetwork.Segments.Add(MoveTemp(Segment));
			WorkingCenterlines2D.Add(MoveTemp(Resampled));
		}

		// Der stille Fall: Way verarbeitet, aber kein einziges Teilstueck hat
		// es bis zum Segment geschafft. Ohne diesen Zaehler faellt das nirgends
		// auf - der Way steht in ProcessedWayCount, in keinem Skipped-Zaehler,
		// und im Spiel fehlt einfach ein Stueck Strasse.
		if (OutNetwork.Segments.Num() == SegmentsBeforeWay)
		{
			++Report.WaysWithoutSegmentCount;
		}
	}

	if (OutNetwork.Segments.Num() == 0)
	{
		Report.ErrorMessage = TEXT("Keine verwertbaren Strassensegmente erzeugt. ")
			TEXT("Enthaelt der Datensatz highway=*-Ways?");
		UE_LOG(LogWbRoads, Error, TEXT("%s"), *Report.ErrorMessage);
		return Report;
	}

	UE_LOG(LogWbRoads, Log,
		TEXT("Schritt 1-2: %d Segmente aus %d Ways. Verworfen: %d unbekannte Art, ")
		TEXT("%d nicht befahrbar, %d ohne Koordinaten, %d mit unter zwei Punkten. ")
		TEXT("Ohne Wirkung: %d Ways ergaben kein Segment, %d Teilstuecke fielen durch."),
		OutNetwork.Segments.Num(), Report.ProcessedWayCount,
		Report.SkippedUnknownTypeCount, Report.SkippedNotDrivableCount,
		Report.SkippedUnresolvedCount, Report.SkippedTooFewPointsCount,
		Report.WaysWithoutSegmentCount, Report.DroppedSubSegmentCount);

	// -- Schritt 3: Lose Enden verbinden -----------------------------------
	//
	// MUSS vor BuildIntersections laufen: Erst wenn zwei Enden denselben Knoten
	// tragen, entsteht dort ueberhaupt eine Kreuzung.
	if (Settings.bSnapLooseRoadEnds)
	{
		const int32 Snapped = SnapLooseRoadEnds(Settings, OutNetwork);
		if (Snapped > 0)
		{
			UE_LOG(LogWbRoads, Log,
				TEXT("Schritt 3: %d lose Strassenenden-Paare verbunden (Radius %.0f m)."),
				Snapped, Settings.LooseEndSnapRadiusCm / 100.0);
		}
	}

	// -- Schritt 4: Kreuzungen ---------------------------------------------

	BuildIntersections(DataSet, *Converter, Settings, OutNetwork);
	TrimSegmentsAtIntersections(Settings, OutNetwork);

	UE_LOG(LogWbRoads, Log, TEXT("Schritt 4: %d Kreuzungen erfasst."), OutNetwork.Intersections.Num());

	// -- Projektion auf Terrainhoehe ---------------------------------------

	for (int32 SegmentIndex = 0; SegmentIndex < OutNetwork.Segments.Num(); ++SegmentIndex)
	{
		FRoadSegment& Segment = OutNetwork.Segments[SegmentIndex];

		ProjectToTerrain(WorkingCenterlines2D[SegmentIndex], HeightSampler,
			Settings.RoadSurfaceOffsetCm, Segment.Layer, Settings, Segment.Centerline);

		ProjectToTerrain(WorkingTrimmed2D[SegmentIndex], HeightSampler,
			Settings.RoadSurfaceOffsetCm, Segment.Layer, Settings, Segment.TrimmedCenterline);

		// Flaechenumriss ebenfalls auf das Gelaende legen.
		//
		// Ohne das laege der Platz auf Z = 0, also rund hundert Meter unter
		// Wiesbaden. Genau dieser Fehler hat schon die Kreuzungsplatten
		// betroffen.
		if (Segment.bIsArea && Segment.AreaOutline.Num() >= 3)
		{
			TArray<FVector2D> Outline2D;
			Outline2D.Reserve(Segment.AreaOutline.Num());
			for (const FVector& Point : Segment.AreaOutline)
			{
				Outline2D.Add(FVector2D(Point.X, Point.Y));
			}

			TArray<FVector> Projected;
			ProjectToTerrain(Outline2D, HeightSampler,
				Settings.RoadSurfaceOffsetCm, Segment.Layer, Settings, Projected);
			Segment.AreaOutline = MoveTemp(Projected);
		}
	}

	for (FRoadIntersection& Intersection : OutNetwork.Intersections)
	{
		if (HeightSampler && HeightSampler->HasValidData())
		{
			const FVector2D XY(Intersection.Location.X, Intersection.Location.Y);
			Intersection.Location.Z = HeightSampler->SampleHeightCm(XY) + Settings.RoadSurfaceOffsetCm;
		}

		// Kreuzungsflaeche aus den TATSAECHLICHEN Bandenden aufbauen.
		//
		// Zuvor entstand sie aus "Mitte + Auswaertsrichtung * Kuerzungslaenge",
		// also unter der Annahme, jede Strasse verlasse die Kreuzung
		// GERADLINIG entlang ihrer Anfangstangente. Das trifft auf gekruemmte
		// Zufahrten nicht zu - und davon hat Wiesbaden reichlich. Das
		// Fahrbahnband endet dann seitlich versetzt, und zwischen Band und
		// Kreuzungsflaeche klafft Wiese.
		//
		// Gemessen lagen 1.274 von 4.754 Bandenden (27 %) AUSSERHALB der
		// Flaeche, im Extremfall 5,2 m daneben. Genau das war im Spiel als
		// nicht angeschlossene Strasse zu sehen.
		//
		// Die Enden stehen an dieser Stelle fest (Kuerzung und Projektion sind
		// gelaufen), also wird der Umriss hier neu gebildet - aus den Ecken der
		// Baender selbst. Damit KANN keine Luecke mehr entstehen.
		const FVector2D Center2D(Intersection.Location.X, Intersection.Location.Y);

		TArray<FVector2D> CarriagewayCorners;
		TArray<FVector2D> SidewalkCorners;
		TArray<double> CornerHeights;
		TArray<double> SidewalkCornerHeights;

		CarriagewayCorners.Reserve(Intersection.Arms.Num() * 2);
		SidewalkCorners.Reserve(Intersection.Arms.Num() * 2);

		for (FIntersectionArm& Arm : Intersection.Arms)
		{
			// ACHTUNG: FIntersectionArm::SegmentId ist trotz seines Namens der
			// INDEX in Network.Segments, nicht die Segment-Id. So wird er im
			// gesamten uebrigen Code benutzt (TrimSegmentsAtIntersections
			// indiziert damit direkt).
			//
			// Hier stand zuvor eine Suche in einer Id->Index-Tabelle. Wo Index
			// und Id zufaellig uebereinstimmten, ging es gut; sonst schlug sie
			// fehl, es entstanden keine Eckpunkte, und der Kreuzungsumriss
			// blieb LEER - die Kreuzung fehlte im Spiel vollstaendig.
			if (!OutNetwork.Segments.IsValidIndex(Arm.SegmentId))
			{
				continue;
			}

			const TArray<FVector>& Trimmed = OutNetwork.Segments[Arm.SegmentId].TrimmedCenterline;
			if (Trimmed.Num() < 2)
			{
				continue;
			}

			// Endpunkt am Knoten und die dortige Auswaertsrichtung.
			const FVector& EndPoint = Arm.bIsSegmentStart ? Trimmed[0] : Trimmed.Last();
			const FVector& Neighbour = Arm.bIsSegmentStart ? Trimmed[1] : Trimmed[Trimmed.Num() - 2];

			FVector2D Outward(Neighbour.X - EndPoint.X, Neighbour.Y - EndPoint.Y);
			if (!Outward.Normalize())
			{
				continue;
			}

			const FVector2D End2D(EndPoint.X, EndPoint.Y);
			const FVector2D Normal = FPolygonUtils::GetLeftNormal(Outward);

			// Die Tore dieses Arms - einmal berechnet, von allen benutzt.
			//
			// "Outward" zeigt hier von der Kreuzung WEG, weil es aus dem
			// Nachbarpunkt gebildet wird. GetLeftNormal davon ist damit die
			// linke Seite in Auswaertsrichtung.
			const FVector2D LeftGate2D = End2D + Normal * Arm.HalfWidthCm;
			const FVector2D RightGate2D = End2D - Normal * Arm.HalfWidthCm;

			Arm.GateLeft = FVector(LeftGate2D.X, LeftGate2D.Y, EndPoint.Z);
			Arm.GateRight = FVector(RightGate2D.X, RightGate2D.Y, EndPoint.Z);

			const double OuterHalf = Arm.HalfWidthCm + FMath::Max(Arm.SidewalkWidthCm, 0.0);
			const FVector2D LeftOuter2D = End2D + Normal * OuterHalf;
			const FVector2D RightOuter2D = End2D - Normal * OuterHalf;

			Arm.SidewalkGateLeft = FVector(LeftOuter2D.X, LeftOuter2D.Y, EndPoint.Z);
			Arm.SidewalkGateRight = FVector(RightOuter2D.X, RightOuter2D.Y, EndPoint.Z);
			Arm.bGateValid = true;

			CarriagewayCorners.Add(LeftGate2D);
			CarriagewayCorners.Add(RightGate2D);
			CornerHeights.Add(EndPoint.Z);
			CornerHeights.Add(EndPoint.Z);

			if (Arm.SidewalkWidthCm > 0.0)
			{
				SidewalkCorners.Add(LeftOuter2D);
				SidewalkCorners.Add(RightOuter2D);
				SidewalkCornerHeights.Add(EndPoint.Z);
				SidewalkCornerHeights.Add(EndPoint.Z);
			}
		}

		// Huelle bilden und die Hoehe des jeweils naechsten Bandendes uebernehmen.
		auto BuildPolygon = [](const TArray<FVector2D>& Corners, const TArray<double>& Heights,
			double FallbackZ, TArray<FVector>& OutPolygon)
		{
			OutPolygon.Reset();
			if (Corners.Num() < 3)
			{
				return;
			}

			TArray<FVector2D> Hull;
			if (!FPolygonUtils::ComputeConvexHull(Corners, Hull))
			{
				return;
			}

			OutPolygon.Reserve(Hull.Num());
			for (const FVector2D& Point : Hull)
			{
				double BestDistSq = TNumericLimits<double>::Max();
				double BestZ = FallbackZ;
				for (int32 Index = 0; Index < Corners.Num(); ++Index)
				{
					const double DistSq = FVector2D::DistSquared(Point, Corners[Index]);
					if (DistSq < BestDistSq)
					{
						BestDistSq = DistSq;
						BestZ = Heights[Index];
					}
				}
				OutPolygon.Add(FVector(Point.X, Point.Y, BestZ));
			}
		};

		// Umriss aus den TOREN, in Richtungsreihenfolge - keine konvexe Huelle.
		//
		// Die Huelle war zweifach schaedlich:
		//
		//   1. Sie kann Ecken VERSCHLUCKEN. Ein schmaler Arm zwischen zwei
		//      breiten liegt innerhalb der Huelle; sein Tor lag dann nicht auf
		//      dem Rand der Platte, und das Band dieses Arms hing an nichts.
		//   2. Ihr Umlaufsinn ist eine Eigenschaft des Algorithmus. Genau daran
		//      sind saemtliche Kreuzungsplatten der Stadt gescheitert: 78.158
		//      Dreiecke zeigten nach unten und waren unsichtbar.
		//
		// Die Arme strahlen von der Kreuzungsmitte nach aussen. Nach ihrer
		// Auswaertsrichtung sortiert ergeben ihre Tore unmittelbar einen
		// sternfoermigen Umriss: Fuer jeden Arm rechte Torecke, dann linke,
		// und der naechste Arm schliesst an. Jede Torkante ist damit eine
		// Randkante - konstruktiv, nicht durch Nachbessern.
		auto BuildOutlineFromGates = [](
			const TArray<FIntersectionArm>& Arms,
			bool bWithSidewalk,
			const FVector& Fallback,
			TArray<FVector>& OutPolygon)
		{
			OutPolygon.Reset();

			TArray<const FIntersectionArm*> Sorted;
			Sorted.Reserve(Arms.Num());
			for (const FIntersectionArm& Arm : Arms)
			{
				if (Arm.bGateValid)
				{
					Sorted.Add(&Arm);
				}
			}

			if (Sorted.Num() < 2)
			{
				return;
			}

			// Winkel aus der GEOMETRIE, nicht aus BearingDegrees.
			//
			// BearingDegrees ist zwar derselbe Winkel, wird aber an anderer
			// Stelle und aus anderen Daten gebildet. Zwei Wege zum selben Wert
			// sind eine Fehlerquelle, und genau hier hat sie zugeschlagen: Der
			// Umriss verschraenkte sich, 30 Prozent der Faecher-Dreiecke
			// zeigten nach unten - bei einem sauber umlaufenden Umriss muessten
			// es null oder hundert sein.
			//
			// Massgeblich ist der Winkel der Tormitte um den Kreuzungspunkt.
			// Der ist aus denselben Punkten gerechnet, die auch den Umriss
			// bilden.
			// Sortiert wird um den SCHWERPUNKT der Tore, nicht um den Knoten.
			// Derselbe Grund wie beim Faecher: Der Knoten liegt nicht immer
			// innerhalb der Tore, und dann ergibt der Winkel um ihn herum keine
			// brauchbare Reihenfolge.
			FVector2D Centre2D(0.0, 0.0);
			{
				int32 Count = 0;
				for (const FIntersectionArm* Arm : Sorted)
				{
					const FVector L = bWithSidewalk ? Arm->SidewalkGateLeft : Arm->GateLeft;
					const FVector R = bWithSidewalk ? Arm->SidewalkGateRight : Arm->GateRight;
					Centre2D += FVector2D((L.X + R.X) * 0.5, (L.Y + R.Y) * 0.5);
					++Count;
				}
				Centre2D = Count > 0 ? Centre2D / Count : FVector2D(Fallback.X, Fallback.Y);
			}

			Sorted.Sort([&Centre2D, bWithSidewalk](const FIntersectionArm& A, const FIntersectionArm& B)
			{
				auto AngleOf = [&Centre2D, bWithSidewalk](const FIntersectionArm& Arm)
				{
					const FVector L = bWithSidewalk ? Arm.SidewalkGateLeft : Arm.GateLeft;
					const FVector R = bWithSidewalk ? Arm.SidewalkGateRight : Arm.GateRight;
					const FVector2D Mid((L.X + R.X) * 0.5, (L.Y + R.Y) * 0.5);
					return FMath::Atan2(Mid.Y - Centre2D.Y, Mid.X - Centre2D.X);
				};
				return AngleOf(A) < AngleOf(B);
			});

			OutPolygon.Reserve(Sorted.Num() * 2);
			for (const FIntersectionArm* Arm : Sorted)
			{
				// ERST die linke Torecke, dann die rechte.
				//
				// Das sieht falsch herum aus und ist es nicht:
				// FPolygonUtils::GetLeftNormal liefert (Y, -X), also eine
				// Drehung um MINUS 90 Grad. Der Vektor heisst "links", zeigt
				// aber auf die mathematisch rechte Seite - eine Konvention aus
				// dem linkshaendigen Koordinatensystem von Unreal.
				//
				// GateLeft liegt damit im Uhrzeigersinn VOR der Armachse. Beim
				// Umlauf gegen den Uhrzeigersinn kommt es zuerst.
				//
				// Umgekehrt gereiht entstand ein STERN: Die Kanten kreuzten
				// sich zwischen den Armen, im Bild als Zacken statt gefuellter
				// Kreuzung zu sehen, und die Flaechenformel meldete durch die
				// Vorzeichen-Aufhebung 31 statt 45 Quadratmeter.
				OutPolygon.Add(bWithSidewalk ? Arm->SidewalkGateLeft : Arm->GateLeft);
				OutPolygon.Add(bWithSidewalk ? Arm->SidewalkGateRight : Arm->GateRight);
			}

			// Umlaufsinn am fertigen Umriss erzwingen.
			//
			// Die Sortierung liefert ihn zwar schon richtig, aber das haengt an
			// der Annahme, welche Torecke bei einem Umlauf zuerst kommt. Die
			// Flaechenformel braucht keine Annahme: Ist die vorzeichenbehaftete
			// Flaeche negativ, laeuft der Umriss im Uhrzeigersinn und wird
			// umgedreht.
			double SignedArea2 = 0.0;
			for (int32 Index = 0; Index < OutPolygon.Num(); ++Index)
			{
				const FVector& P = OutPolygon[Index];
				const FVector& Q = OutPolygon[(Index + 1) % OutPolygon.Num()];
				SignedArea2 += P.X * Q.Y - Q.X * P.Y;
			}

			if (SignedArea2 < 0.0)
			{
				Algo::Reverse(OutPolygon);
			}
		};

		BuildOutlineFromGates(Intersection.Arms, false,
			Intersection.Location, Intersection.Polygon);
		BuildOutlineFromGates(Intersection.Arms, true,
			Intersection.Location, Intersection.SidewalkPolygon);
	}

	// -- Schritt 5+6: Spuren und Graph -------------------------------------

	BuildLanes(Settings, OutNetwork);

	int32 RestrictedCount = 0;
	ConnectLanes(DataSet, Settings, OutNetwork, RestrictedCount);

	UE_LOG(LogWbRoads, Log, TEXT("Schritt 5-6: %d Spuren, %d Verbindungen (%d gesperrt)."),
		OutNetwork.Lanes.Num(), OutNetwork.Connections.Num(), RestrictedCount);

	// -- Schritt 7: Geometrie ----------------------------------------------

	if (OutMeshData)
	{
		// Segmente OHNE Fahrbahnflaeche zaehlen.
		//
		// "Strassensegmente fehlen" laesst sich nur beantworten, wenn man
		// weiss, WIE VIELE und WARUM. Zwei Ausfallpfade: eine gekuerzte
		// Mittellinie mit weniger als zwei Punkten, oder eine Bandbildung, die
		// an entarteter Geometrie scheitert.
		int32 NoCenterlineCount = 0;
		int32 NoRibbonCount = 0;

		// Flaechen-Zaehler ebenfalls VOR der Schleife.
		//
		// Sie standen zunaechst beim Zuruecksetzen der Kreuzungs-Zaehler, und
		// das geschieht NACH der Segment-Schleife - also nach dem Zaehlen.
		// Gemeldet wurden "0 Flaechen gebaut", obwohl sie gebaut wurden.
		// Derselbe Fehler war zuvor schon beim Tor-Anschluss aufgetreten.
		GAreaBuilt = 0;
		GAreaTriangles = 0;
		GAreaFanFallback = 0;
		GAreaTooFewPoints = 0;

		// VOR der Schleife zuruecksetzen, nicht danach.
		//
		// Diese beiden Zaehler standen zunaechst weiter unten, zusammen mit
		// denen der Kreuzungen - also genau NACH der Schleife, die sie
		// hochzaehlt. Gemeldet wurde "0 von 0 Bandenden", und das hiess nicht
		// "nichts angeschlossen", sondern "nicht gemessen".
		GSegmentGatesApplied = 0;
		GSegmentGatesPossible = 0;
		GGateSnapSumCm = 0.0;
		GGateSnapWorstCm = 0.0;
		GGateSnapCount = 0;

		for (const FRoadSegment& Segment : OutNetwork.Segments)
		{
			if (Segment.TrimmedCenterline.Num() < 2)
			{
				++NoCenterlineCount;
			}
			else
			{
				TArray<FVector2D> Probe2D;
				Probe2D.Reserve(Segment.TrimmedCenterline.Num());
				for (const FVector& Point : Segment.TrimmedCenterline)
				{
					Probe2D.Add(FVector2D(Point.X, Point.Y));
				}

				TArray<FVector2D> ProbeVertices;
				TArray<int32> ProbeIndices;
				TArray<FVector2D> ProbeUVs;
				if (!FPolygonUtils::BuildRibbonMesh(
					Probe2D, Segment.CarriagewayWidthCm, ProbeVertices, ProbeIndices, ProbeUVs))
				{
					++NoRibbonCount;
				}
			}

			BuildSegmentMesh(OutNetwork, Segment, *TypeLibrary, HeightSampler, Settings, *OutMeshData);

			if (Settings.bGenerateLaneMarkings)
			{
				BuildLaneMarkings(Segment, *TypeLibrary, Settings, *OutMeshData);
			}
			if (Settings.bGenerateEmbankments)
			{
				BuildEmbankmentMesh(Segment, *TypeLibrary, HeightSampler, Settings, *OutMeshData);
			}
		}

		GIntersectionFanFallbackCount = 0;
		GIntersectionTooFewPointsCount = 0;
		GIntersectionFlippedTriangles = 0;

		for (const FRoadIntersection& Intersection : OutNetwork.Intersections)
		{
			BuildIntersectionMesh(Intersection, Settings, *OutMeshData);
		}

		// Hoehenlage je Kanal.
		//
		// Die Kreuzungsplatten sind im Spiel nicht zu sehen, obwohl sie erzeugt
		// werden (0 Fehlschlaege bei der Triangulierung). Damit bleibt die
		// Hoehe als Erklaerung: Eine Platte auf Z = 0 laege rund 100 m unter
		// Wiesbaden und waere nie sichtbar. BuildIntersections legt den Umriss
		// zunaechst mit Z = 0 an - ob die spaetere Projektion ihn wirklich
		// erreicht, war nie geprueft.
		{
			TMap<int32, TTuple<int32, double, double>> ChannelStats;   // Kanal -> (Dreiecke, ZMin, ZMax)
			for (const FRoadMeshSection& Section : OutMeshData->Sections)
			{
				const int32 Key = static_cast<int32>(Section.Channel);
				TTuple<int32, double, double>& Stat = ChannelStats.FindOrAdd(
					Key, TTuple<int32, double, double>(0, TNumericLimits<double>::Max(),
						TNumericLimits<double>::Lowest()));

				Stat.Get<0>() += Section.Triangles.Num() / 3;
				for (const FVector& Vertex : Section.Vertices)
				{
					Stat.Get<1>() = FMath::Min(Stat.Get<1>(), Vertex.Z);
					Stat.Get<2>() = FMath::Max(Stat.Get<2>(), Vertex.Z);
				}
			}

			for (const TPair<int32, TTuple<int32, double, double>>& Pair : ChannelStats)
			{
				UE_LOG(LogWbRoads, Log,
					TEXT("Kanal %d: %d Dreiecke, Hoehe %.0f bis %.0f cm."),
					Pair.Key, Pair.Value.Get<0>(), Pair.Value.Get<1>(), Pair.Value.Get<2>());
			}
		}

		// Groesse der Kreuzungsflaechen.
		//
		// Im Diagnosebild mit ausgeblendetem Gelaende ist die Kreuzungsmitte
		// LEER - die Fahrbahnbaender enden am Rand, dazwischen nichts. Die
		// Platten werden aber alle erzeugt (0 Fehlschlaege) und liegen auf
		// plausibler Hoehe. Bleibt: Sie sind zu klein fuer die Luecke.
		//
		// Verglichen wird die tatsaechliche Flaeche des Umrisses mit der
		// Flaeche, die zu fuellen waere - grob der Kreis mit dem Radius der
		// Trimmweite. Ein Verhaeltnis weit unter 1 beweist die Luecke.
		{
			double AreaSum = 0.0;
			double TrimSum = 0.0;
			double RadiusSum = 0.0;
			int32 CornerSum = 0;
			int32 ArmSum = 0;
			int32 Counted = 0;

			for (const FRoadIntersection& Intersection : OutNetwork.Intersections)
			{
				if (Intersection.Polygon.Num() < 3)
				{
					continue;
				}

				// Flaeche des Umrisses ueber die Schnuersenkelformel.
				double Area2 = 0.0;
				const int32 N = Intersection.Polygon.Num();
				for (int32 i = 0; i < N; ++i)
				{
					const FVector& A = Intersection.Polygon[i];
					const FVector& B = Intersection.Polygon[(i + 1) % N];
					Area2 += A.X * B.Y - B.X * A.Y;
				}

				double TrimMax = 0.0;
				for (const FIntersectionArm& Arm : Intersection.Arms)
				{
					TrimMax = FMath::Max(TrimMax, Arm.TrimDistanceCm);
				}

				AreaSum += FMath::Abs(Area2) * 0.5;
				TrimSum += TrimMax;
				RadiusSum += Intersection.RadiusCm;
				CornerSum += N;
				ArmSum += Intersection.Arms.Num();
				++Counted;
			}

			if (Counted > 0)
			{
				const double MeanArea = AreaSum / Counted;
				const double MeanTrim = TrimSum / Counted;
				const double NeededArea = PI * MeanTrim * MeanTrim;

				UE_LOG(LogWbRoads, Log,
					TEXT("Kreuzungsgroesse: %d Kreuzungen, im Mittel %.1f Arme, %.1f Ecken, ")
					TEXT("Trimmweite %.0f cm, Radius %.0f cm. Plattenflaeche %.0f m2, ")
					TEXT("zu fuellen waeren rund %.0f m2 (%.0f %%)."),
					Counted, static_cast<double>(ArmSum) / Counted,
					static_cast<double>(CornerSum) / Counted,
					MeanTrim, RadiusSum / Counted,
					MeanArea / 10000.0, NeededArea / 10000.0,
					NeededArea > 0.0 ? 100.0 * MeanArea / NeededArea : 0.0);
			}
		}

		// Kreuzungs-Bilanz.
		//
		// Der Abbruch bei gescheiterter Triangulierung war eine
		// Verbose-Meldung - also unsichtbar. Betroffene Kreuzungen bekamen den
		// Gehwegring, aber keine Fahrbahnflaeche: graue Baender aussen, in der
		// Mitte sah das Gelaende durch. Im Spiel als gruene Kreuzung sichtbar.
		UE_LOG(LogWbRoads, Log,
			TEXT("Kreuzungsflaechen: %d von %d Kreuzungen ueber den Schwerpunkt-Faecher ")
			TEXT("geschlossen, %d ohne verwertbaren Umriss (unter 3 Punkte)."),
			GIntersectionFanFallbackCount, OutNetwork.Intersections.Num(),
			GIntersectionTooFewPointsCount);

		UE_LOG(LogWbRoads, Log,
			TEXT("Kreuzungsplatten: %d Dreiecke mussten umgedreht werden - sie waren ")
			TEXT("rueckseitig zugewandt und damit von oben unsichtbar."),
			GIntersectionFlippedTriangles);

		UE_LOG(LogWbRoads, Log,
			TEXT("Flaechen (Plaetze, Fussgaengerzonen): %d gebaut, %d Dreiecke, ")
			TEXT("%d ueber den Faecher, %d ohne verwertbaren Umriss."),
			GAreaBuilt, GAreaTriangles, GAreaFanFallback, GAreaTooFewPoints);

		UE_LOG(LogWbRoads, Log,
			TEXT("Anschluss an Kreuzungstore: %d von %d Bandenden (%.1f %%)."),
			GSegmentGatesApplied, GSegmentGatesPossible,
			GSegmentGatesPossible > 0
				? 100.0 * GSegmentGatesApplied / GSegmentGatesPossible : 0.0);

		if (GGateSnapCount > 0)
		{
			UE_LOG(LogWbRoads, Log,
				TEXT("Versatz beim Anschluss: im Mittel %.2f cm, schlimmstenfalls %.1f cm ")
				TEXT("(%d Randpunkte). Werte in Fahrbahnbreite hiessen vertauschte Seiten."),
				GGateSnapSumCm / GGateSnapCount, GGateSnapWorstCm, GGateSnapCount);
		}

		// Fuge zwischen Band und Platte messen.
		//
		// Der Anschluss ueber gemeinsame Punkte soll die Fuge auf exakt null
		// bringen. Nur eine Messung beweist das - "es benutzt dieselben Punkte"
		// ist eine Behauptung ueber den Code, keine ueber das Ergebnis.
		{
			double WorstGapCm = 0.0;
			double GapSum = 0.0;
			int32 GapCount = 0;

			for (const FRoadIntersection& Intersection : OutNetwork.Intersections)
			{
				for (const FIntersectionArm& Arm : Intersection.Arms)
				{
					if (!Arm.bGateValid || !OutNetwork.Segments.IsValidIndex(Arm.SegmentId))
					{
						continue;
					}

					const FRoadSegment& ArmSegment = OutNetwork.Segments[Arm.SegmentId];
					const TArray<FVector>& Line = ArmSegment.TrimmedCenterline.Num() >= 2
						? ArmSegment.TrimmedCenterline
						: ArmSegment.Centerline;

					if (Line.Num() < 2)
					{
						continue;
					}

					// Der Bandanfang liegt am Achspunkt des Arms; die Tore
					// muessen die halbe Fahrbahnbreite davon entfernt sein.
					const FVector& End = Arm.bIsSegmentStart ? Line[0] : Line.Last();
					const double ToLeft = FVector::Dist2D(End, Arm.GateLeft);
					const double ToRight = FVector::Dist2D(End, Arm.GateRight);
					const double Expected = ArmSegment.CarriagewayWidthCm * 0.5;

					const double Gap = FMath::Max(
						FMath::Abs(ToLeft - Expected), FMath::Abs(ToRight - Expected));

					WorstGapCm = FMath::Max(WorstGapCm, Gap);
					GapSum += Gap;
					++GapCount;
				}
			}

			if (GapCount > 0)
			{
				UE_LOG(LogWbRoads, Log,
					TEXT("Fuge Band/Platte an %d Armen: im Mittel %.2f cm, schlimmstenfalls %.2f cm."),
					GapCount, GapSum / GapCount, WorstGapCm);
			}
		}

		// Fugen an den Knick-Knoten schliessen (siehe BuildBendFillers).
		BuildBendFillers(OutNetwork, *OutMeshData);

		if (NoCenterlineCount > 0 || NoRibbonCount > 0)
		{
			UE_LOG(LogWbRoads, Warning,
				TEXT("Segmente OHNE Fahrbahnflaeche: %d ohne Mittellinie, %d ohne Band ")
				TEXT("(von %d Segmenten)."),
				NoCenterlineCount, NoRibbonCount, OutNetwork.Segments.Num());
		}
		else
		{
			UE_LOG(LogWbRoads, Log,
				TEXT("Alle %d Segmente haben eine Fahrbahnflaeche."), OutNetwork.Segments.Num());
		}

		Report.VertexCount = OutMeshData->GetTotalVertexCount();
		Report.TriangleCount = OutMeshData->GetTotalTriangleCount();
	}

	// Arbeitsdaten freigeben - bei einer Stadt wie Wiesbaden sind das rund
	// 100 MB, die nach der Generierung niemand mehr braucht.
	WorkingCenterlines2D.Empty();
	WorkingTrimmed2D.Empty();

	Report.bSuccess = true;
	Report.SegmentCount = OutNetwork.Segments.Num();
	Report.LaneCount = OutNetwork.Lanes.Num();
	Report.IntersectionCount = OutNetwork.Intersections.Num();
	Report.ConnectionCount = OutNetwork.Connections.Num();
	Report.RestrictedConnectionCount = RestrictedCount;
	Report.DurationSeconds = FPlatformTime::Seconds() - StartTime;

	UE_LOG(LogWbRoads, Log, TEXT("%s"), *Report.ToString());
	UE_LOG(LogWbRoads, Log, TEXT("Netzstatistik: %s"), *OutNetwork.GetStatisticsString());

	return Report;
}

void URoadNetworkGenerator::BuildIntersections(
	const FOSMDataSet& DataSet,
	const UGeoCoordinateConverter& Converter,
	const FRoadGenerationSettings& Settings,
	FRoadNetwork& Network) const
{
	// Alle Segmentenden je Knoten sammeln.
	TMap<int64, TArray<FIntersectionArm>> ArmsByNode;
	ArmsByNode.Reserve(Network.Segments.Num());

	for (int32 SegmentIndex = 0; SegmentIndex < Network.Segments.Num(); ++SegmentIndex)
	{
		const FRoadSegment& Segment = Network.Segments[SegmentIndex];
		const TArray<FVector2D>& Line = WorkingCenterlines2D[SegmentIndex];

		if (Line.Num() < 2)
		{
			continue;
		}

		// Fusswege bilden keine Fahrbahnkreuzungen. Sie kreuzen die Fahrbahn
		// zwar, aber eine Kreuzungsflaeche aus einem Gehweg und einer Strasse
		// waere baulich falsch.
		if (!FOSMTagParser::IsDrivable(Segment.HighwayType))
		{
			continue;
		}

		const double HalfWidth = Segment.CarriagewayWidthCm * 0.5;

		FIntersectionArm StartArm;
		StartArm.SegmentId = SegmentIndex;
		StartArm.bIsSegmentStart = true;
		const FVector2D StartDir = FPolygonUtils::GetStartTangent(Line);
		StartArm.OutwardDirection = FVector(StartDir.X, StartDir.Y, 0.0);
		StartArm.HalfWidthCm = HalfWidth;
		StartArm.SidewalkWidthCm = Segment.SidewalkType != EOSMSidewalkType::None ? Segment.SidewalkWidthCm : 0.0;
		StartArm.BearingDegrees = BearingDegrees(StartDir);
		ArmsByNode.FindOrAdd(Segment.StartNodeId).Add(StartArm);

		FIntersectionArm EndArm;
		EndArm.SegmentId = SegmentIndex;
		EndArm.bIsSegmentStart = false;
		// Auswaerts vom Knoten gesehen ist das die Gegenrichtung der Endtangente.
		const FVector2D EndDir = -FPolygonUtils::GetEndTangent(Line);
		EndArm.OutwardDirection = FVector(EndDir.X, EndDir.Y, 0.0);
		EndArm.HalfWidthCm = HalfWidth;
		EndArm.SidewalkWidthCm = Segment.SidewalkType != EOSMSidewalkType::None ? Segment.SidewalkWidthCm : 0.0;
		EndArm.BearingDegrees = BearingDegrees(EndDir);
		ArmsByNode.FindOrAdd(Segment.EndNodeId).Add(EndArm);
	}

	// Deterministische Reihenfolge der Kreuzungen.
	TArray<int64> SortedNodeIds;
	SortedNodeIds.Reserve(ArmsByNode.Num());
	for (const TPair<int64, TArray<FIntersectionArm>>& Pair : ArmsByNode)
	{
		SortedNodeIds.Add(Pair.Key);
	}
	SortedNodeIds.Sort();

	// Ampeln an Kreuzungen erkennen, die den Knoten NICHT selbst taggen.
	//
	// In OSM sitzt highway=traffic_signals fast immer auf einem Knoten der
	// zufuehrenden Strasse KURZ VOR der Kreuzung - dort, wo der Mast steht -
	// und nicht auf dem Kreuzungsknoten. Wer nur den Kreuzungsknoten prueft,
	// findet fast nichts: von rund 2300 Signalknoten in Wiesbaden ergaben sich
	// so 27 Ampeln, und im Spiel hielt kein Fahrzeug jemals an Rot.
	//
	// Ein Signalknoten zaehlt zu einer Kreuzung, wenn er auf DEMSELBEN Way
	// liegt und hoechstens SignalSearchRadiusCm entfernt ist. Die Bindung an
	// den Way verhindert, dass eine Ampel auf eine parallele Nachbarstrasse
	// ueberspringt.
	TSet<int64> SignalizedJunctionNodes;
	{
		constexpr double SignalSearchRadiusCm = 4000.0;   // 40 m
		const double SignalSearchRadiusSq = SignalSearchRadiusCm * SignalSearchRadiusCm;

		TSet<int64> JunctionNodeIds;
		for (const int64 NodeId : SortedNodeIds)
		{
			if (ArmsByNode[NodeId].Num() >= 3)
			{
				JunctionNodeIds.Add(NodeId);
			}
		}

		TArray<FOSMId> SignalNodes;
		TArray<FOSMId> JunctionNodes;

		for (const TPair<FOSMId, FOSMWay>& WayPair : DataSet.Ways)
		{
			SignalNodes.Reset();
			JunctionNodes.Reset();

			for (const FOSMId NodeRef : WayPair.Value.NodeIds)
			{
				if (JunctionNodeIds.Contains(NodeRef))
				{
					JunctionNodes.Add(NodeRef);
				}

				const FOSMNode* WayNode = DataSet.Nodes.Find(NodeRef);
				if (WayNode && WayNode->IsTrafficSignal())
				{
					SignalNodes.Add(NodeRef);
				}
			}

			if (SignalNodes.Num() == 0 || JunctionNodes.Num() == 0)
			{
				continue;
			}

			for (const FOSMId JunctionId : JunctionNodes)
			{
				if (SignalizedJunctionNodes.Contains(JunctionId))
				{
					continue;
				}

				const FOSMNode* JunctionNode = DataSet.Nodes.Find(JunctionId);
				if (!JunctionNode)
				{
					continue;
				}

				const FVector JunctionWorld = Converter.GeoToUnrealGround(JunctionNode->Location);

				for (const FOSMId SignalId : SignalNodes)
				{
					const FOSMNode* SignalNode = DataSet.Nodes.Find(SignalId);
					if (!SignalNode)
					{
						continue;
					}

					const FVector SignalWorld = Converter.GeoToUnrealGround(SignalNode->Location);
					const double Dx = SignalWorld.X - JunctionWorld.X;
					const double Dy = SignalWorld.Y - JunctionWorld.Y;

					if (Dx * Dx + Dy * Dy <= SignalSearchRadiusSq)
					{
						SignalizedJunctionNodes.Add(JunctionId);
						break;
					}
				}
			}
		}
	}

	for (const int64 NodeId : SortedNodeIds)
	{
		TArray<FIntersectionArm>& Arms = ArmsByNode[NodeId];

		// Knoten mit weniger als drei Armen sind Fortsetzungen oder
		// Sackgassen - dort entsteht keine Kreuzungsflaeche.
		if (Arms.Num() < 3)
		{
			continue;
		}

		const FOSMNode* Node = DataSet.Nodes.Find(NodeId);
		if (!Node)
		{
			continue;
		}

		FRoadIntersection Intersection;
		Intersection.NodeId = NodeId;
		Intersection.Location = Converter.GeoToUnrealGround(Node->Location);

		// Arme im Uhrzeigersinn sortieren. Die Verkehrs-KI braucht diese
		// Ordnung fuer "rechts vor links": der rechte Nachbararm ist der mit
		// dem naechstkleineren Winkel.
		Arms.Sort([](const FIntersectionArm& A, const FIntersectionArm& B)
		{
			return A.BearingDegrees < B.BearingDegrees;
		});

		Intersection.Arms = Arms;

		// Verkehrsregelung bestimmen.
		bool bAnyRoundabout = false;
		int32 BestPriority = TNumericLimits<int32>::Max();
		int32 WorstPriority = TNumericLimits<int32>::Min();

		for (const FIntersectionArm& Arm : Arms)
		{
			const FRoadSegment& Segment = Network.Segments[Arm.SegmentId];
			if (Segment.bIsRoundabout)
			{
				bAnyRoundabout = true;
			}
			const int32 Priority = static_cast<int32>(Segment.HighwayType);
			BestPriority = FMath::Min(BestPriority, Priority);
			WorstPriority = FMath::Max(WorstPriority, Priority);
		}

		if (bAnyRoundabout)
		{
			Intersection.Control = EIntersectionControl::Roundabout;
		}
		else if (Node->IsTrafficSignal() || SignalizedJunctionNodes.Contains(NodeId))
		{
			Intersection.Control = EIntersectionControl::TrafficSignals;
		}
		else if (Node->IsStopSign())
		{
			Intersection.Control = EIntersectionControl::Stop;
		}
		else if (Node->IsGiveWay())
		{
			Intersection.Control = EIntersectionControl::Yield;
		}
		else if (WorstPriority - BestPriority >= 2)
		{
			// Treffen deutlich unterschiedliche Strassenklassen aufeinander,
			// ist die hoehere in Deutschland praktisch immer Vorfahrtstrasse -
			// auch wenn OSM das Schild nicht erfasst hat.
			Intersection.Control = EIntersectionControl::PriorityRoad;
		}
		else
		{
			Intersection.Control = EIntersectionControl::Uncontrolled;
		}

		Intersection.bHasPedestrianCrossing = Node->IsCrossing();
		Intersection.bHasZebraCrossing = Node->HasTagValue(TEXT("crossing"), TEXT("zebra"))
			|| Node->HasTagValue(TEXT("crossing:markings"), TEXT("zebra"))
			|| Node->HasTagValue(TEXT("crossing_ref"), TEXT("zebra"));

		// Kuerzungslaenge je Arm: so weit, dass die Fahrbahn des breitesten
		// anderen Arms vollstaendig Platz hat. Der Sinus-Term beruecksichtigt
		// schraege Einmuendungen - bei einem 30-Grad-Winkel reicht die halbe
		// Breite nicht aus, weil die Fahrbahn dann weiter in den Arm hineinragt.
		double MaxRadius = 0.0;

		for (FIntersectionArm& Arm : Intersection.Arms)
		{
			double RequiredTrim = 0.0;

			for (const FIntersectionArm& Other : Intersection.Arms)
			{
				if (Other.SegmentId == Arm.SegmentId && Other.bIsSegmentStart == Arm.bIsSegmentStart)
				{
					continue;
				}

				const double AngleDeg = FMath::Abs(FMath::UnwindDegrees(Other.BearingDegrees - Arm.BearingDegrees));

				// Geradeaus durchlaufende Strasse: KEIN Rueckschnitt.
				//
				// Zeigt der andere Arm in die Gegenrichtung, ist er die
				// Fortsetzung derselben Strasse. Die beiden Baender liegen auf
				// einer Linie und setzen einander fort - sie kreuzen sich nie
				// und muessen einander nicht ausweichen.
				//
				// Hier wurde genau dieser Fall wie der UNGUENSTIGSTE behandelt:
				// Die Formel teilt durch |sin(Winkel)|, und bei 180 Grad ist der
				// Sinus null. Das Divisor-Minimum von 0,35 machte daraus das
				// 2,86-fache der halben Fahrbahnbreite. Gemessen ergab das eine
				// mittlere Trimmweite von 884 cm - fast NEUN METER Rueckschnitt
				// an jedem Arm jeder Kreuzung. Die konvexe Huelle der Armenden
				// deckte davon nur 42 % ab; der Rest blieb blankes Gelaende.
				// Im Spiel war das die Luecke in der Kreuzungsmitte.
				constexpr double StraightThroughDeg = 150.0;
				if (AngleDeg > StraightThroughDeg)
				{
					continue;
				}

				const double SinAngle = FMath::Abs(FMath::Sin(FMath::DegreesToRadians(AngleDeg)));

				// Spitz zusammenlaufende Arme wuerden die Division weiterhin
				// aufblaehen. Das Minimum entspricht 30 Grad und begrenzt den
				// Rueckschnitt damit auf das Doppelte der halben Breite.
				const double Divisor = FMath::Max(SinAngle, 0.5);
				RequiredTrim = FMath::Max(RequiredTrim, Other.HalfWidthCm / Divisor);
			}

			Arm.TrimDistanceCm = RequiredTrim + Settings.IntersectionMarginCm;
			MaxRadius = FMath::Max(MaxRadius, Arm.TrimDistanceCm);
		}

		Intersection.RadiusCm = MaxRadius;

		// Kreuzungsflaeche: die Eckpunkte der gekuerzten Fahrbahnenden bilden
		// eine Punktwolke, deren konvexe Huelle die Decke ergibt.
		TArray<FVector2D> HullInput;
		HullInput.Reserve(Intersection.Arms.Num() * 2);

		const FVector2D Center2D(Intersection.Location.X, Intersection.Location.Y);

		for (const FIntersectionArm& Arm : Intersection.Arms)
		{
			const FVector2D Outward(Arm.OutwardDirection.X, Arm.OutwardDirection.Y);
			const FVector2D Normal = FPolygonUtils::GetLeftNormal(Outward);
			const FVector2D ArmEnd = Center2D + Outward * Arm.TrimDistanceCm;

			HullInput.Add(ArmEnd + Normal * Arm.HalfWidthCm);
			HullInput.Add(ArmEnd - Normal * Arm.HalfWidthCm);
		}

		TArray<FVector2D> Hull;
		if (FPolygonUtils::ComputeConvexHull(HullInput, Hull))
		{
			Intersection.Polygon.Reserve(Hull.Num());
			for (const FVector2D& Point : Hull)
			{
				Intersection.Polygon.Add(FVector(Point.X, Point.Y, 0.0));
			}
		}

		// Zweiter Umriss einschliesslich der Gehwege: dieselben Armenden, nur
		// um die Gehwegbreite weiter aussen.
		//
		// Ohne ihn endeten die Gehwege an JEDER Kreuzung. Die Fahrbahnen trafen
		// sich sauber, aber zwischen den Armen blieben gruene Keile stehen, wo
		// der Buergersteig um die Ecke haette laufen muessen - aus dem Fahrzeug
		// sah das aus, als sei die Strasse nicht angeschlossen.
		TArray<FVector2D> SidewalkHullInput;
		SidewalkHullInput.Reserve(Intersection.Arms.Num() * 2);

		bool bAnySidewalk = false;
		for (const FIntersectionArm& Arm : Intersection.Arms)
		{
			if (Arm.SidewalkWidthCm <= 0.0)
			{
				continue;
			}
			bAnySidewalk = true;

			const FVector2D Outward(Arm.OutwardDirection.X, Arm.OutwardDirection.Y);
			const FVector2D Normal = FPolygonUtils::GetLeftNormal(Outward);
			const FVector2D ArmEnd = Center2D + Outward * Arm.TrimDistanceCm;
			const double OuterHalf = Arm.HalfWidthCm + Arm.SidewalkWidthCm;

			SidewalkHullInput.Add(ArmEnd + Normal * OuterHalf);
			SidewalkHullInput.Add(ArmEnd - Normal * OuterHalf);
		}

		TArray<FVector2D> SidewalkHull;
		if (bAnySidewalk && FPolygonUtils::ComputeConvexHull(SidewalkHullInput, SidewalkHull))
		{
			Intersection.SidewalkPolygon.Reserve(SidewalkHull.Num());
			for (const FVector2D& Point : SidewalkHull)
			{
				Intersection.SidewalkPolygon.Add(FVector(Point.X, Point.Y, 0.0));
			}
		}

		Network.IntersectionByNode.Add(NodeId, Network.Intersections.Num());
		Network.Intersections.Add(MoveTemp(Intersection));
	}
}

void URoadNetworkGenerator::TrimSegmentsAtIntersections(
	const FRoadGenerationSettings& Settings,
	FRoadNetwork& Network) const
{
	WorkingTrimmed2D.SetNum(Network.Segments.Num());

	// Kuerzungslaengen je Segmentende einsammeln.
	TArray<double> TrimStart;
	TArray<double> TrimEnd;
	TrimStart.Init(0.0, Network.Segments.Num());
	TrimEnd.Init(0.0, Network.Segments.Num());

	for (const FRoadIntersection& Intersection : Network.Intersections)
	{
		for (const FIntersectionArm& Arm : Intersection.Arms)
		{
			if (!Network.Segments.IsValidIndex(Arm.SegmentId))
			{
				continue;
			}

			if (Arm.bIsSegmentStart)
			{
				TrimStart[Arm.SegmentId] = FMath::Max(TrimStart[Arm.SegmentId], Arm.TrimDistanceCm);
			}
			else
			{
				TrimEnd[Arm.SegmentId] = FMath::Max(TrimEnd[Arm.SegmentId], Arm.TrimDistanceCm);
			}
		}
	}

	// Kuerzung begrenzen, damit KEIN Segment ganz verschwindet.
	//
	// Hier stand zuvor: laesst sich ein Segment nicht kuerzen, weil die
	// Kuerzungen an seinen beiden Enden zusammen laenger sind als es selbst,
	// dann wird es verworfen - "die Kreuzungsflaechen schliessen die Luecke".
	//
	// Diese Annahme wurde nie geprueft, und sie stimmt nicht. Jede
	// Kreuzungsflaeche reicht nur so weit wie IHRE eigene Kuerzung. Liegen zwei
	// Kreuzungen 30 m auseinander und kuerzt jede 12 m, decken die beiden
	// Flaechen zusammen 24 m ab - dazwischen bleiben 6 m blanke Wiese. Genau
	// diese Loecher waren im Spiel als abgerissene Fahrbahn zu sehen: Fahrbahn
	// und beide Gehwege enden in einer sauberen Querkante, dahinter nichts.
	//
	// Gemeldet wurde der Fall nur auf Verbose und damit praktisch nie.
	//
	// Statt zu verwerfen werden beide Kuerzungen anteilig so weit
	// zurueckgenommen, dass ein Reststueck stehen bleibt. Die Fahrbahn ragt
	// dann etwas in die Kreuzungsflaeche hinein - dasselbe Material auf
	// derselben Hoehe, im Bild nicht zu unterscheiden. Eine Ueberlappung ist
	// harmlos, eine Luecke nicht.
	constexpr double MinRemainingLengthCm = 200.0;

	int32 ClampedTrimCount = 0;
	int32 UntrimmedShortCount = 0;

	for (int32 SegmentIndex = 0; SegmentIndex < Network.Segments.Num(); ++SegmentIndex)
	{
		const TArray<FVector2D>& Source = WorkingCenterlines2D[SegmentIndex];

		double LengthCm = 0.0;
		for (int32 Index = 1; Index < Source.Num(); ++Index)
		{
			LengthCm += FVector2D::Distance(Source[Index - 1], Source[Index]);
		}

		double& Start = TrimStart[SegmentIndex];
		double& End = TrimEnd[SegmentIndex];
		const double Requested = Start + End;
		const double Allowed = LengthCm - MinRemainingLengthCm;

		if (Allowed <= 0.0)
		{
			// Das Segment ist kuerzer als das geforderte Reststueck. Gar nicht
			// kuerzen - lieber eine Ueberlappung als ein fehlendes Stueck.
			if (Requested > 0.0)
			{
				Start = 0.0;
				End = 0.0;
				++UntrimmedShortCount;
			}
		}
		else if (Requested > Allowed)
		{
			const double Factor = Allowed / Requested;
			Start *= Factor;
			End *= Factor;
			++ClampedTrimCount;
		}

		TArray<FVector2D> Trimmed;
		if (FPolygonUtils::TrimPolyline(Source, Start, End, Trimmed) && Trimmed.Num() >= 2)
		{
			WorkingTrimmed2D[SegmentIndex] = MoveTemp(Trimmed);
		}
		else
		{
			// Letzte Rueckfallebene: ungekuerzt uebernehmen. Ein Segment ohne
			// Fahrbahnflaeche darf es nicht geben.
			WorkingTrimmed2D[SegmentIndex] = Source;
		}
	}

	if (ClampedTrimCount > 0 || UntrimmedShortCount > 0)
	{
		UE_LOG(LogWbRoads, Log,
			TEXT("Kreuzungs-Kuerzung begrenzt: %d Segmente anteilig zurueckgenommen, ")
			TEXT("%d zu kurze gar nicht gekuerzt (Mindestrest %.0f cm)."),
			ClampedTrimCount, UntrimmedShortCount, MinRemainingLengthCm);
	}
}

void URoadNetworkGenerator::BuildLanes(
	const FRoadGenerationSettings& Settings,
	FRoadNetwork& Network) const
{
	for (int32 SegmentIndex = 0; SegmentIndex < Network.Segments.Num(); ++SegmentIndex)
	{
		FRoadSegment& Segment = Network.Segments[SegmentIndex];

		if (!FOSMTagParser::IsDrivable(Segment.HighwayType))
		{
			continue;
		}

		const TArray<FVector>& Centerline = Segment.Centerline;
		if (Centerline.Num() < 2)
		{
			continue;
		}

		const int32 TotalLanes = Segment.GetTotalLaneCount();
		if (TotalLanes < 1)
		{
			continue;
		}

		const double LaneWidth = Segment.CarriagewayWidthCm / static_cast<double>(TotalLanes);
		const double HalfCarriageway = Segment.CarriagewayWidthCm * 0.5;

		// Anmerkung zu turn:lanes: die Abbiegepfeile stehen am Quell-Way und
		// werden beim Zerlegen nicht auf die Segmente uebertragen. Die
		// tatsaechlich erlaubten Abbiegebeziehungen ermittelt ConnectLanes
		// ohnehin aus der Kreuzungsgeometrie; das Tag steuert daher nur die
		// aufgemalten Pfeile und wird beim Erzeugen der Fahrbahnmarkierungen
		// ausgewertet, nicht hier.

		for (int32 LaneIndexFromLeft = 0; LaneIndexFromLeft < TotalLanes; ++LaneIndexFromLeft)
		{
			// Rechtsverkehr: die Vorwaertsspuren liegen rechts der Achse, die
			// Gegenspuren links. Von links gezaehlt kommen daher zuerst die
			// Backward-Spuren.
			const bool bIsBackward = LaneIndexFromLeft < Segment.BackwardLaneCount;

			// Versatz vom linken Fahrbahnrand zur Spurmitte, dann relativ zur
			// Achse. Positiver Offset = links.
			const double OffsetFromLeftEdge = (LaneIndexFromLeft + 0.5) * LaneWidth;
			const double OffsetFromCenter = HalfCarriageway - OffsetFromLeftEdge;

			TArray<FVector2D> Centerline2D;
			Centerline2D.Reserve(Centerline.Num());
			for (const FVector& Point : Centerline)
			{
				Centerline2D.Add(FVector2D(Point.X, Point.Y));
			}

			TArray<FVector2D> LaneLine2D;
			if (!FPolygonUtils::OffsetPolyline(Centerline2D, OffsetFromCenter, LaneLine2D))
			{
				continue;
			}

			FRoadLane Lane;
			Lane.LaneId = Network.Lanes.Num();
			Lane.SegmentId = SegmentIndex;
			Lane.LaneIndexFromLeft = LaneIndexFromLeft;
			Lane.Direction = bIsBackward ? ELaneDirection::Backward : ELaneDirection::Forward;
			Lane.WidthCm = LaneWidth;
			Lane.SpeedLimitKmh = Segment.MaxSpeedKmh;
			Lane.TurnFlags = static_cast<uint8>(ETurnIndication::Through);

			// Hoehe von der Segmentachse uebernehmen: die Spur liegt hoechstens
			// wenige Meter daneben, dort ist die Terrainhoehe praktisch gleich,
			// und ein erneutes Sampling wuerde die Spur gegenueber der
			// Fahrbahndecke verschieben.
			Lane.Centerline.Reserve(LaneLine2D.Num());
			for (int32 PointIndex = 0; PointIndex < LaneLine2D.Num(); ++PointIndex)
			{
				// Die Offsetlinie kann durch Bevel-Joins mehr Punkte haben als
				// die Achse; die Hoehe wird daher anteilig zugeordnet.
				const int32 SourceIndex = FMath::Clamp(
					FMath::RoundToInt32(
						static_cast<double>(PointIndex) * (Centerline.Num() - 1)
						/ FMath::Max(1, LaneLine2D.Num() - 1)),
					0, Centerline.Num() - 1);

				Lane.Centerline.Add(FVector(
					LaneLine2D[PointIndex].X,
					LaneLine2D[PointIndex].Y,
					Centerline[SourceIndex].Z));
			}

			// Backward-Spuren werden gegen die Way-Richtung befahren; die
			// Sollbahn muss daher umgekehrt werden, damit
			// Centerline[0] -> Centerline.Last() immer die Fahrtrichtung ist.
			if (bIsBackward)
			{
				Algo::Reverse(Lane.Centerline);
			}

			Lane.LengthCm = 0.0;
			for (int32 PointIndex = 1; PointIndex < Lane.Centerline.Num(); ++PointIndex)
			{
				Lane.LengthCm += FVector::Dist(Lane.Centerline[PointIndex - 1], Lane.Centerline[PointIndex]);
			}

			Segment.LaneIds.Add(Lane.LaneId);
			Network.Lanes.Add(MoveTemp(Lane));
		}
	}
}

void URoadNetworkGenerator::CollectTurnRestrictions(
	const FOSMDataSet& DataSet,
	TSet<TPair<int64, int64>>& OutForbiddenWayPairs) const
{
	OutForbiddenWayPairs.Reset();

	for (const TPair<FOSMId, FOSMRelation>& Pair : DataSet.Relations)
	{
		const FOSMRelation& Relation = Pair.Value;
		if (!Relation.IsTurnRestriction())
		{
			continue;
		}

		const FString RestrictionValue = Relation.GetTag(TEXT("restriction")).ToLower();

		// "only_*"-Vorschriften verbieten alle nicht genannten Beziehungen.
		// Sie korrekt umzusetzen erfordert die Kenntnis aller Arme und wird
		// beim Verknuepfen behandelt; hier werden die expliziten Verbote
		// gesammelt.
		if (!RestrictionValue.StartsWith(TEXT("no_")))
		{
			continue;
		}

		int64 FromWay = 0;
		int64 ToWay = 0;

		for (const FOSMRelationMember& Member : Relation.Members)
		{
			if (Member.Type != EOSMMemberType::Way)
			{
				continue;
			}

			if (Member.Role == TEXT("from"))
			{
				FromWay = Member.Ref;
			}
			else if (Member.Role == TEXT("to"))
			{
				ToWay = Member.Ref;
			}
		}

		if (FromWay != 0 && ToWay != 0)
		{
			OutForbiddenWayPairs.Add(TPair<int64, int64>(FromWay, ToWay));
		}
	}

	if (OutForbiddenWayPairs.Num() > 0)
	{
		UE_LOG(LogWbRoads, Log, TEXT("%d Abbiegeverbote aus OSM-Relationen uebernommen."),
			OutForbiddenWayPairs.Num());
	}
}

void URoadNetworkGenerator::ConnectLanes(
	const FOSMDataSet& DataSet,
	const FRoadGenerationSettings& Settings,
	FRoadNetwork& Network,
	int32& OutRestrictedCount) const
{
	OutRestrictedCount = 0;

	TSet<TPair<int64, int64>> ForbiddenWayPairs;
	CollectTurnRestrictions(DataSet, ForbiddenWayPairs);

	// Spuren nach dem Knoten indizieren, an dem sie beginnen bzw. enden.
	// Eine Spur endet an StartNodeId, wenn sie ruecklaeufig ist, sonst an
	// EndNodeId - die Sollbahn wurde fuer Backward-Spuren bereits gedreht.
	TMap<int64, TArray<int32>> LanesEndingAtNode;
	TMap<int64, TArray<int32>> LanesStartingAtNode;

	for (const FRoadLane& Lane : Network.Lanes)
	{
		const FRoadSegment* Segment = Network.GetSegment(Lane.SegmentId);
		if (!Segment || !Lane.IsValid())
		{
			continue;
		}

		const bool bForward = Lane.Direction == ELaneDirection::Forward;

		const int64 EntryNode = bForward ? Segment->StartNodeId : Segment->EndNodeId;
		const int64 ExitNode = bForward ? Segment->EndNodeId : Segment->StartNodeId;

		LanesStartingAtNode.FindOrAdd(EntryNode).Add(Lane.LaneId);
		LanesEndingAtNode.FindOrAdd(ExitNode).Add(Lane.LaneId);
	}

	// Deterministische Reihenfolge.
	TArray<int64> SortedNodes;
	SortedNodes.Reserve(LanesEndingAtNode.Num());
	for (const TPair<int64, TArray<int32>>& Pair : LanesEndingAtNode)
	{
		SortedNodes.Add(Pair.Key);
	}
	SortedNodes.Sort();

	for (const int64 NodeId : SortedNodes)
	{
		const TArray<int32>& IncomingLanes = LanesEndingAtNode[NodeId];
		const TArray<int32>* OutgoingLanesPtr = LanesStartingAtNode.Find(NodeId);

		if (!OutgoingLanesPtr || OutgoingLanesPtr->Num() == 0)
		{
			// Sackgasse. Keine Nachfolger - die KI muss dort wenden oder
			// despawnen; das entscheidet das Verkehrssystem, nicht der Graph.
			continue;
		}

		const FRoadIntersection* Intersection = Network.GetIntersectionByNode(NodeId);

		for (const int32 FromLaneId : IncomingLanes)
		{
			const FRoadLane& FromLane = Network.Lanes[FromLaneId];
			const FRoadSegment* FromSegment = Network.GetSegment(FromLane.SegmentId);
			if (!FromSegment)
			{
				continue;
			}

			const FVector IncomingDirection = FromLane.GetExitDirection();

			for (const int32 ToLaneId : *OutgoingLanesPtr)
			{
				const FRoadLane& ToLane = Network.Lanes[ToLaneId];
				const FRoadSegment* ToSegment = Network.GetSegment(ToLane.SegmentId);
				if (!ToSegment)
				{
					continue;
				}

				// Kein Zurueckfahren in dasselbe Segment. Ausnahme: das
				// Segment ist eine Sackgasse mit nur diesem einen Arm - dort
				// ist Wenden die einzige Moeglichkeit.
				if (ToLane.SegmentId == FromLane.SegmentId)
				{
					const bool bIsDeadEnd = IncomingLanes.Num() <= 1 || OutgoingLanesPtr->Num() <= 1;
					if (!bIsDeadEnd)
					{
						continue;
					}
				}

				const FVector OutgoingDirection = ToLane.GetEntryDirection();
				const ETurnType TurnType = ClassifyTurn(IncomingDirection, OutgoingDirection);

				// Wenden an Kreuzungen mit mehr als zwei Armen ist in
				// Deutschland nicht generell verboten, fuer die Verkehrs-KI
				// aber unerwuenscht - es fuehrt zu blockierten Kreuzungen.
				if (TurnType == ETurnType::UTurn && Intersection && Intersection->GetArmCount() > 2)
				{
					continue;
				}

				FLaneConnection Connection;
				Connection.FromLaneId = FromLaneId;
				Connection.ToLaneId = ToLaneId;
				Connection.IntersectionNodeId = NodeId;
				Connection.TurnType = TurnType;

				// Abbiegeverbot aus OSM.
				if (ForbiddenWayPairs.Contains(
					TPair<int64, int64>(FromSegment->SourceWayId, ToSegment->SourceWayId)))
				{
					Connection.bRestricted = true;
					++OutRestrictedCount;
				}

				// Verbindungskurve: quadratische Bezier mit einem Kontrollpunkt
				// im Schnittpunkt der beiden Fahrtrichtungsgeraden. Bei
				// Geradeausfahrt faellt der Kontrollpunkt praktisch mit der
				// Mitte zusammen und die Kurve wird zur Geraden.
				const FVector Start = FromLane.GetEndPoint();
				const FVector End = ToLane.GetStartPoint();

				FVector2D ControlPoint2D;
				const bool bHasControl = FPolygonUtils::SegmentIntersection(
					FVector2D(Start.X, Start.Y),
					FVector2D(Start.X, Start.Y) + FVector2D(IncomingDirection.X, IncomingDirection.Y) * 100000.0,
					FVector2D(End.X, End.Y),
					FVector2D(End.X, End.Y) - FVector2D(OutgoingDirection.X, OutgoingDirection.Y) * 100000.0,
					ControlPoint2D);

				const FVector Control = bHasControl
					? FVector(ControlPoint2D.X, ControlPoint2D.Y, (Start.Z + End.Z) * 0.5)
					: (Start + End) * 0.5;

				// Abtastdichte nach Kurvigkeit: eine Geradeausverbindung
				// braucht keine Zwischenpunkte, eine enge Rechtskurve schon.
				const int32 SampleCount = (TurnType == ETurnType::Through) ? 2 : 8;
				SampleQuadraticBezier(Start, Control, End, SampleCount, Connection.ConnectionPath);

				const int32 ConnectionIndex = Network.Connections.Num();
				Network.Connections.Add(MoveTemp(Connection));
				Network.LaneSuccessors.FindOrAdd(FromLaneId).Add(ConnectionIndex);
			}
		}
	}
}

void URoadNetworkGenerator::ProjectToTerrain(
	const TArray<FVector2D>& Points2D,
	const IHeightSampler* HeightSampler,
	double AdditionalOffsetCm,
	int32 Layer,
	const FRoadGenerationSettings& Settings,
	TArray<FVector>& OutPoints) const
{
	OutPoints.Reset();
	OutPoints.Reserve(Points2D.Num());

	// Bruecken und Tunnel liegen ueber bzw. unter dem Terrain. Ohne diesen
	// Versatz wuerde die Salzbachtalbruecke im Boden liegen und die
	// Unterfuehrung am Hauptbahnhof durch die Fahrbahn darueber stossen.
	const double LayerOffset = Layer * Settings.LayerHeightCm;

	const bool bHasTerrain = HeightSampler && HeightSampler->HasValidData();

	for (const FVector2D& Point : Points2D)
	{
		const double TerrainZ = bHasTerrain ? HeightSampler->SampleHeightCm(Point) : 0.0;
		OutPoints.Add(FVector(Point.X, Point.Y, TerrainZ + AdditionalOffsetCm + LayerOffset));
	}
}

FRoadMeshSection& URoadNetworkGenerator::FindOrAddSection(
	FRoadMeshData& MeshData,
	ERoadMeshChannel Channel,
	EOSMSurfaceType Surface)
{
	for (FRoadMeshSection& Section : MeshData.Sections)
	{
		if (Section.Channel == Channel && Section.Surface == Surface)
		{
			return Section;
		}
	}

	FRoadMeshSection NewSection;
	NewSection.Channel = Channel;
	NewSection.Surface = Surface;
	const int32 Index = MeshData.Sections.Add(MoveTemp(NewSection));
	return MeshData.Sections[Index];
}

void URoadNetworkGenerator::BuildAreaMesh(
	const FRoadSegment& Segment,
	const FRoadGenerationSettings& Settings,
	FRoadMeshData& OutMeshData) const
{
	if (Segment.AreaOutline.Num() < 3)
	{
		++GAreaTooFewPoints;
		return;
	}

	// Geschlossene Umrisse enden mit ihrem Anfangspunkt. Fuer die Zerlegung
	// stoert der doppelte Punkt.
	TArray<FVector> Outline = Segment.AreaOutline;
	if (Outline.Num() > 3 && FVector::Dist2D(Outline[0], Outline.Last()) < 1.0)
	{
		Outline.Pop();
	}

	if (Outline.Num() < 3)
	{
		++GAreaTooFewPoints;
		return;
	}

	TArray<FVector2D> Outline2D;
	Outline2D.Reserve(Outline.Num());
	for (const FVector& Point : Outline)
	{
		Outline2D.Add(FVector2D(Point.X, Point.Y));
	}

	// Ohr-Abschneiden fuer beliebige einfache Umrisse; ein Platz ist selten
	// konvex. Scheitert es an entarteten Stellen, springt der Faecher vom
	// Schwerpunkt ein - der ist bei sternfoermigen Umrissen immer gueltig.
	TArray<int32> Indices;
	bool bUsedFan = false;

	if (!FPolygonUtils::TriangulatePolygon(Outline2D, Indices) || Indices.Num() < 3)
	{
		bUsedFan = true;
		Indices.Reset();
		const int32 CentroidIndex = Outline.Num();
		for (int32 Corner = 0; Corner < Outline.Num(); ++Corner)
		{
			Indices.Add(CentroidIndex);
			Indices.Add(Corner);
			Indices.Add((Corner + 1) % Outline.Num());
		}
	}

	// Pflasterstein fuer Fussgaengerbereiche, sonst der Belag des Weges.
	const EOSMSurfaceType Surface =
		(Segment.HighwayType == EOSMHighwayType::Pedestrian
			|| Segment.HighwayType == EOSMHighwayType::Footway)
		? EOSMSurfaceType::PavingStones
		: Segment.Surface;

	FRoadMeshSection& Section = FindOrAddSection(
		OutMeshData, ERoadMeshChannel::Intersection, Surface);

	const int32 BaseIndex = Section.Vertices.Num();

	FVector Centroid = FVector::ZeroVector;
	for (const FVector& Point : Outline)
	{
		Centroid += Point;
	}
	Centroid /= static_cast<double>(Outline.Num());

	auto AddVertex = [&Section, &Centroid](const FVector& Point)
	{
		Section.Vertices.Add(Point);
		Section.Normals.Add(FVector::UpVector);
		Section.UVs.Add(FVector2D(
			(Point.X - Centroid.X) / MetersToCm,
			(Point.Y - Centroid.Y) / MetersToCm));
		Section.VertexColors.Add(FColor(200, 230, 255, DefaultWetnessMask));
		Section.Tangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));
	};

	for (const FVector& Point : Outline)
	{
		AddVertex(Point);
	}
	if (bUsedFan)
	{
		AddVertex(Centroid);
	}

	// Umlaufrichtung je Dreieck aus der Geometrie.
	//
	// Der Umlaufsinn eines OSM-Rings ist nicht festgelegt - manche Plaetze
	// sind im Uhrzeigersinn erfasst, manche dagegen. Fest angenommen waere die
	// Haelfte aller Plaetze rueckseitig zugewandt und damit unsichtbar. Genau
	// so sind zuvor saemtliche 20.213 Kreuzungsplatten verschwunden.
	Section.Triangles.Reserve(Section.Triangles.Num() + Indices.Num());
	for (int32 Tri = 0; Tri + 2 < Indices.Num(); Tri += 3)
	{
		const int32 IA = Indices[Tri + 0] + BaseIndex;
		const int32 IB = Indices[Tri + 1] + BaseIndex;
		const int32 IC = Indices[Tri + 2] + BaseIndex;

		if (!Section.Vertices.IsValidIndex(IA) || !Section.Vertices.IsValidIndex(IB)
			|| !Section.Vertices.IsValidIndex(IC))
		{
			continue;
		}

		const FVector& A = Section.Vertices[IA];
		const FVector& B = Section.Vertices[IB];
		const FVector& C = Section.Vertices[IC];

		if (FVector::CrossProduct(B - A, C - A).Z > 0.0)
		{
			Section.Triangles.Add(IA);
			Section.Triangles.Add(IC);
			Section.Triangles.Add(IB);
		}
		else
		{
			Section.Triangles.Add(IA);
			Section.Triangles.Add(IB);
			Section.Triangles.Add(IC);
		}
	}

	++GAreaBuilt;
	GAreaTriangles += Indices.Num() / 3;
	if (bUsedFan)
	{
		++GAreaFanFallback;
	}
}

void URoadNetworkGenerator::BuildSegmentMesh(
	const FRoadNetwork& Network,
	const FRoadSegment& Segment,
	const URoadTypeLibrary& TypeLibrary,
	const IHeightSampler* HeightSampler,
	const FRoadGenerationSettings& Settings,
	FRoadMeshData& OutMeshData) const
{
	// FLAECHEN statt Baender: Plaetze und Fussgaengerzonen.
	//
	// Ein Platz ist in OSM ein geschlossener Weg. Als Band gebaut ergibt er
	// einen Pfad um sich selbst herum, mit Wiese in der Mitte - genau so sah
	// der Bischofsplatz im Spiel aus.
	if (Segment.bIsArea)
	{
		BuildAreaMesh(Segment, Settings, OutMeshData);
		return;
	}

	const TArray<FVector>& Line = Segment.TrimmedCenterline;
	if (Line.Num() < 2)
	{
		return;
	}

	TArray<FVector2D> Line2D;
	Line2D.Reserve(Line.Num());
	for (const FVector& Point : Line)
	{
		Line2D.Add(FVector2D(Point.X, Point.Y));
	}

	// -- Anschluss an die Kreuzungstore -------------------------------------
	//
	// Fahrbahn, Gehweg und Bordstein enden alle drei an derselben Kreuzung und
	// muessen dort dieselben Punkte benutzen. Bisher rechnete jeder von ihnen
	// seine Randpunkte selbst aus Mittellinie, halber Breite und Gehwegbreite -
	// drei Wege zum selben Ergebnis, und damit drei Gelegenheiten fuer
	// Abweichungen. Genau daraus entstanden die Gehwege, die in die Kreuzung
	// ragen.
	//
	// Der Arm der Kreuzung hat die Punkte bereits festgelegt. Von hier aus
	// werden sie nur noch uebernommen.

	auto FindArm = [&Network, &Segment](int64 NodeId, bool bSegmentStart) -> const FIntersectionArm*
	{
		const FRoadIntersection* Junction = Network.GetIntersectionByNode(NodeId);
		if (!Junction)
		{
			return nullptr;
		}
		for (const FIntersectionArm& Arm : Junction->Arms)
		{
			if (Arm.bGateValid
				&& Arm.SegmentId == Segment.SegmentId
				&& Arm.bIsSegmentStart == bSegmentStart)
			{
				return &Arm;
			}
		}
		return nullptr;
	};

	// Zwei Randpunkte auf die jeweils naechstgelegene Torecke setzen.
	//
	// Zugeordnet wird ueber den ABSTAND, nicht ueber links und rechts. Welche
	// Ecke zu welchem Randpunkt gehoert, haengt an zwei Konventionen: der
	// Armrichtung (am Segmentende zeigt sie rueckwaerts) und daran, welche
	// Seite FPolygonUtils::GetLeftNormal liefert - das ist (Y, -X), also eine
	// Drehung um MINUS 90 Grad. Der Vektor heisst "links" und zeigt nach
	// rechts. An genau dieser Konvention ist der Kreuzungsumriss schon einmal
	// gescheitert; ein zweites Mal von Hand herleiten will ich sie nicht.
	auto SnapPair = [](FRoadMeshSection& Section, int32 BaseIndex,
		int32 VertexA, int32 VertexB, const FVector& CandidateA, const FVector& CandidateB)
	{
		if (!Section.Vertices.IsValidIndex(BaseIndex + VertexA)
			|| !Section.Vertices.IsValidIndex(BaseIndex + VertexB))
		{
			return false;
		}

		FVector& A = Section.Vertices[BaseIndex + VertexA];
		FVector& B = Section.Vertices[BaseIndex + VertexB];

		// Die Zuordnung, die insgesamt den kleineren Versatz ergibt.
		const double Straight =
			FVector::DistSquared2D(A, CandidateA) + FVector::DistSquared2D(B, CandidateB);
		const double Crossed =
			FVector::DistSquared2D(A, CandidateB) + FVector::DistSquared2D(B, CandidateA);

		const FVector& ForA = (Straight <= Crossed) ? CandidateA : CandidateB;
		const FVector& ForB = (Straight <= Crossed) ? CandidateB : CandidateA;

		GGateSnapSumCm += FVector::Dist2D(A, ForA) + FVector::Dist2D(B, ForB);
		GGateSnapWorstCm = FMath::Max(GGateSnapWorstCm,
			FMath::Max(FVector::Dist2D(A, ForA), FVector::Dist2D(B, ForB)));
		GGateSnapCount += 2;

		A = ForA;
		B = ForB;
		return true;
	};

	const FIntersectionArm* StartArm = FindArm(Segment.StartNodeId, /*bSegmentStart=*/true);
	const FIntersectionArm* EndArm = FindArm(Segment.EndNodeId, /*bSegmentStart=*/false);

	// -- Fahrbahndecke ------------------------------------------------------

	TArray<FVector2D> RibbonVertices2D;
	TArray<int32> RibbonIndices;
	TArray<FVector2D> RibbonUVs;

	if (FPolygonUtils::BuildRibbonMesh(
		Line2D, Segment.CarriagewayWidthCm, RibbonVertices2D, RibbonIndices, RibbonUVs))
	{
		FRoadMeshSection& Section = FindOrAddSection(
			OutMeshData, ERoadMeshChannel::Carriageway, Segment.Surface);

		const int32 BaseIndex = Section.Vertices.Num();

		for (int32 Index = 0; Index < RibbonVertices2D.Num(); ++Index)
		{
			// Hoehe vom naechstgelegenen Achspunkt uebernehmen: je Achspunkt
			// entstehen genau zwei Randvertices.
			const int32 CenterIndex = FMath::Clamp(Index / 2, 0, Line.Num() - 1);

			Section.Vertices.Add(FVector(
				RibbonVertices2D[Index].X,
				RibbonVertices2D[Index].Y,
				Line[CenterIndex].Z));

			Section.Normals.Add(FVector::UpVector);
			Section.UVs.Add(RibbonUVs[Index]);

			// Vertex-Farbe transportiert Shader-Parameter:
			//  R = Abnutzung (aussen staerker als in der Fahrbahnmitte)
			//  G = Reibungskoeffizient der Oberflaeche
			//  B = Fahrbahnrand-Maske fuer Pfuetzenbildung
			const bool bIsLeftEdge = (Index % 2) == 0;
			const uint8 Wear = bIsLeftEdge ? 180 : 180;
			const uint8 Friction = static_cast<uint8>(
				FMath::Clamp(FOSMTagParser::GetSurfaceFriction(Segment.Surface) * 255.0, 0.0, 255.0));

			Section.VertexColors.Add(FColor(Wear, Friction, 255, DefaultWetnessMask));
			Section.Tangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));
		}

		// Bandenden an die Tore.
		{
			const int32 LastPair = (RibbonVertices2D.Num() / 2 - 1) * 2;

			if (StartArm && SnapPair(Section, BaseIndex, 0, 1,
				StartArm->GateLeft, StartArm->GateRight))
			{
				++GSegmentGatesApplied;
			}
			if (EndArm && SnapPair(Section, BaseIndex, LastPair, LastPair + 1,
				EndArm->GateLeft, EndArm->GateRight))
			{
				++GSegmentGatesApplied;
			}
			GSegmentGatesPossible += 2;
		}

		Section.Triangles.Reserve(Section.Triangles.Num() + RibbonIndices.Num());
		for (const int32 Index : RibbonIndices)
		{
			Section.Triangles.Add(Index + BaseIndex);
		}
	}

	// -- Gehwege ------------------------------------------------------------

	if (!Settings.bGenerateSidewalks || Segment.SidewalkType == EOSMSidewalkType::None
		|| Segment.SidewalkType == EOSMSidewalkType::Separate)
	{
		return;
	}

	const bool bLeftSidewalk = Segment.SidewalkType == EOSMSidewalkType::Both
		|| Segment.SidewalkType == EOSMSidewalkType::Left;
	const bool bRightSidewalk = Segment.SidewalkType == EOSMSidewalkType::Both
		|| Segment.SidewalkType == EOSMSidewalkType::Right;

	const double HalfCarriageway = Segment.CarriagewayWidthCm * 0.5;

	// Untergrenze aus der Strassenklasse, mindestens aber 1,0 m: ein schmalerer
	// Gehweg waere baulich unzulaessig und im Spiel nicht begehbar.
	const FRoadTypeDefinition& SegmentTypeDef = TypeLibrary.GetDefinition(Segment.HighwayType);
	const double SidewalkWidth = FMath::Max3(
		Segment.SidewalkWidthCm,
		SegmentTypeDef.SidewalkWidthMeters * MetersToCm,
		100.0);

	// Hoehe an der FAHRBAHN-Mittellinie sampeln, nicht an der (versetzten)
	// Gehweg-/Bordsteinposition. Sonst tastet der Gehweg das Gelaende mehrere
	// Meter neben der Strasse ab, weicht dort in der Hoehe ab und "schwebt"
	// bzw. verspringt gegenueber der Fahrbahn. So bleibt der Gehweg buendig auf
	// Strassenniveau + Bordstein und folgt der Strasse.
	auto NearestOnLine = [&](const FVector2D& P) -> FVector2D
	{
		FVector2D Best = Line2D.Num() > 0 ? Line2D[0] : P;
		double BestD = TNumericLimits<double>::Max();
		for (int32 i = 0; i + 1 < Line2D.Num(); ++i)
		{
			const FVector2D A = Line2D[i];
			const FVector2D AB = Line2D[i + 1] - A;
			const double L2 = AB.SizeSquared();
			const double T = (L2 > KINDA_SMALL_NUMBER)
				? FMath::Clamp(FVector2D::DotProduct(P - A, AB) / L2, 0.0, 1.0) : 0.0;
			const FVector2D Proj = A + AB * T;
			const double D = FVector2D::DistSquared(P, Proj);
			if (D < BestD) { BestD = D; Best = Proj; }
		}
		return Best;
	};
	auto SampleRoadZ = [&](const FVector2D& P) -> double
	{
		return (HeightSampler && HeightSampler->HasValidData())
			? HeightSampler->SampleHeightCm(NearestOnLine(P)) : 0.0;
	};

	auto BuildSidewalk = [&](double SideSign)
	{
		// Achse des Gehwegs: halbe Fahrbahnbreite plus halbe Gehwegbreite.
		const double SidewalkOffset = SideSign * (HalfCarriageway + SidewalkWidth * 0.5);

		TArray<FVector2D> SidewalkAxis;
		if (!FPolygonUtils::OffsetPolyline(Line2D, SidewalkOffset, SidewalkAxis))
		{
			return;
		}

		TArray<FVector2D> Vertices2D;
		TArray<int32> Indices;
		TArray<FVector2D> UVs;

		if (!FPolygonUtils::BuildRibbonMesh(SidewalkAxis, SidewalkWidth, Vertices2D, Indices, UVs))
		{
			return;
		}

		// Gehwegoberflaeche: Pflasterstein ist in Wiesbaden der Regelbelag,
		// unabhaengig vom Fahrbahnmaterial.
		FRoadMeshSection& Section = FindOrAddSection(
			OutMeshData, ERoadMeshChannel::Sidewalk, EOSMSurfaceType::PavingStones);

		const int32 BaseIndex = Section.Vertices.Num();

		for (int32 Index = 0; Index < Vertices2D.Num(); ++Index)
		{
			const FVector2D& Point = Vertices2D[Index];

			// Hoehe an der Fahrbahn-Mittellinie (nicht an der versetzten Gehweg-
			// position) -> Gehweg bleibt buendig auf Strassenniveau + Bordstein,
			// schwebt nicht ueberm Gras und verspringt nicht gegen die Fahrbahn.
			const double TerrainZ = SampleRoadZ(Point);

			Section.Vertices.Add(FVector(
				Point.X, Point.Y,
				TerrainZ + Settings.RoadSurfaceOffsetCm + Segment.KerbHeightCm
				+ Segment.Layer * Settings.LayerHeightCm));

			Section.Normals.Add(FVector::UpVector);
			Section.UVs.Add(UVs[Index]);
			Section.VertexColors.Add(FColor(200, 220, 255, DefaultWetnessMask));
			Section.Tangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));
		}

		// Gehwegenden an die Tore der Kreuzung.
		//
		// Der Gehweg spannt zwischen Fahrbahnkante und Aussenkante. Genau diese
		// beiden Punkte hat der Kreuzungsarm bereits festgelegt: GateLeft bzw.
		// GateRight fuer innen, SidewalkGateLeft/Right fuer aussen. Bisher hat
		// der Gehweg sie eigenstaendig aus der versetzten Achse gebildet - und
		// lief dadurch ueber die Kreuzung hinweg, statt an ihr zu enden.
		//
		// Welcher der beiden Randpunkte innen liegt, entscheidet der Abstand.
		// Die Gegenseite ist eine ganze Fahrbahnbreite entfernt und kommt
		// deshalb nie versehentlich in Frage.
		{
			const int32 LastPair = (Vertices2D.Num() / 2 - 1) * 2;

			auto SnapSidewalkEnd = [&](const FIntersectionArm* Arm, int32 VertexA, int32 VertexB)
			{
				if (!Arm)
				{
					return;
				}

				// Die naeher liegende Seite des Arms bestimmen: Der Gehweg
				// dieser Segmentseite gehoert zu genau einer der beiden.
				const FVector& EndVertex = Section.Vertices[BaseIndex + VertexA];
				const double ToLeft = FVector::DistSquared2D(EndVertex, Arm->SidewalkGateLeft);
				const double ToRight = FVector::DistSquared2D(EndVertex, Arm->SidewalkGateRight);

				const bool bLeftSide = ToLeft <= ToRight;
				const FVector Inner = bLeftSide ? Arm->GateLeft : Arm->GateRight;
				const FVector Outer = bLeftSide ? Arm->SidewalkGateLeft : Arm->SidewalkGateRight;

				SnapPair(Section, BaseIndex, VertexA, VertexB, Inner, Outer);
			};

			SnapSidewalkEnd(StartArm, 0, 1);
			SnapSidewalkEnd(EndArm, LastPair, LastPair + 1);
		}

		Section.Triangles.Reserve(Section.Triangles.Num() + Indices.Num());
		for (const int32 Index : Indices)
		{
			Section.Triangles.Add(Index + BaseIndex);
		}

		// -- Bordstein: senkrechtes Band zwischen Fahrbahn- und Gehwegniveau --

		TArray<FVector2D> KerbLine;
		if (!FPolygonUtils::OffsetPolyline(Line2D, SideSign * HalfCarriageway, KerbLine)
			|| KerbLine.Num() < 2)
		{
			return;
		}

		FRoadMeshSection& KerbSection = FindOrAddSection(
			OutMeshData, ERoadMeshChannel::Kerb, EOSMSurfaceType::Concrete);

		const int32 KerbBase = KerbSection.Vertices.Num();
		double AccumulatedLength = 0.0;

		for (int32 Index = 0; Index < KerbLine.Num(); ++Index)
		{
			if (Index > 0)
			{
				AccumulatedLength += FVector2D::Distance(KerbLine[Index - 1], KerbLine[Index]);
			}

			// Wie der Gehweg: Hoehe an der Fahrbahn-Mittellinie, damit Bordstein-
			// Oberkante und Gehwegniveau exakt zusammenpassen (keine Stufe).
			const double TerrainZ = SampleRoadZ(KerbLine[Index]);

			const double BaseZ = TerrainZ + Settings.RoadSurfaceOffsetCm + Segment.Layer * Settings.LayerHeightCm;

			KerbSection.Vertices.Add(FVector(KerbLine[Index].X, KerbLine[Index].Y, BaseZ));
			KerbSection.Vertices.Add(FVector(KerbLine[Index].X, KerbLine[Index].Y, BaseZ + Segment.KerbHeightCm));

			// Normale zeigt zur Fahrbahn hin.
			FVector2D Tangent2D = (Index > 0)
				? (KerbLine[Index] - KerbLine[Index - 1]).GetSafeNormal()
				: (KerbLine[1] - KerbLine[0]).GetSafeNormal();

			const FVector2D Normal2D = FPolygonUtils::GetLeftNormal(Tangent2D) * -SideSign;
			const FVector Normal(Normal2D.X, Normal2D.Y, 0.0);

			KerbSection.Normals.Add(Normal);
			KerbSection.Normals.Add(Normal);

			const double VMeters = AccumulatedLength / MetersToCm;
			KerbSection.UVs.Add(FVector2D(VMeters, 0.0));
			KerbSection.UVs.Add(FVector2D(VMeters, 1.0));

			KerbSection.VertexColors.Add(FColor::White);
			KerbSection.VertexColors.Add(FColor::White);

			KerbSection.Tangents.Add(FProcMeshTangent(Tangent2D.X, Tangent2D.Y, 0.0f));
			KerbSection.Tangents.Add(FProcMeshTangent(Tangent2D.X, Tangent2D.Y, 0.0f));

			if (Index > 0)
			{
				const int32 Base = KerbBase + (Index - 1) * 2;

				// Wicklungsrichtung haengt von der Strassenseite ab, sonst
				// zeigt der linke Bordstein sein Backface zur Fahrbahn.
				if (SideSign > 0.0)
				{
					KerbSection.Triangles.Add(Base + 0);
					KerbSection.Triangles.Add(Base + 1);
					KerbSection.Triangles.Add(Base + 2);
					KerbSection.Triangles.Add(Base + 1);
					KerbSection.Triangles.Add(Base + 3);
					KerbSection.Triangles.Add(Base + 2);
				}
				else
				{
					KerbSection.Triangles.Add(Base + 0);
					KerbSection.Triangles.Add(Base + 2);
					KerbSection.Triangles.Add(Base + 1);
					KerbSection.Triangles.Add(Base + 1);
					KerbSection.Triangles.Add(Base + 2);
					KerbSection.Triangles.Add(Base + 3);
				}
			}
		}
		// Bordsteinenden an die Fahrbahnkante der Kreuzung.
		//
		// Der Bordstein ist ein senkrechtes Band; je Punkt entstehen ZWEI
		// Vertices auf derselben XY - unten auf Fahrbahnhoehe, oben um die
		// Bordsteinhoehe versetzt. Beide muessen deshalb auf dieselbe Torecke,
		// nicht auf zwei verschiedene.
		//
		// Ohne diesen Anschluss lief der Bordstein ueber die Kreuzung hinaus
		// und schnitt als Betonkante durch den Asphalt.
		{
			auto SnapKerbEnd = [&](const FIntersectionArm* Arm, int32 PointIndex)
			{
				if (!Arm)
				{
					return;
				}

				const int32 Bottom = KerbBase + PointIndex * 2;
				const int32 Top = Bottom + 1;
				if (!KerbSection.Vertices.IsValidIndex(Top))
				{
					return;
				}

				const FVector Current = KerbSection.Vertices[Bottom];
				const bool bLeft =
					FVector::DistSquared2D(Current, Arm->GateLeft)
					<= FVector::DistSquared2D(Current, Arm->GateRight);

				const FVector Gate = bLeft ? Arm->GateLeft : Arm->GateRight;
				const double Height =
					KerbSection.Vertices[Top].Z - KerbSection.Vertices[Bottom].Z;

				GGateSnapSumCm += FVector::Dist2D(Current, Gate);
				GGateSnapWorstCm = FMath::Max(GGateSnapWorstCm, FVector::Dist2D(Current, Gate));
				++GGateSnapCount;

				KerbSection.Vertices[Bottom] = Gate;
				KerbSection.Vertices[Top] = Gate + FVector(0.0, 0.0, Height);
			};

			SnapKerbEnd(StartArm, 0);
			SnapKerbEnd(EndArm, KerbLine.Num() - 1);
		}

	};

	if (bLeftSidewalk)
	{
		BuildSidewalk(1.0);
	}
	if (bRightSidewalk)
	{
		BuildSidewalk(-1.0);
	}
}

void URoadNetworkGenerator::BuildLaneMarkings(
	const FRoadSegment& Segment,
	const URoadTypeLibrary& TypeLibrary,
	const FRoadGenerationSettings& Settings,
	FRoadMeshData& OutMeshData) const
{
	const TArray<FVector>& Line = Segment.TrimmedCenterline;
	if (Line.Num() < 2)
	{
		return;
	}

	const FRoadTypeDefinition& TypeDef = TypeLibrary.GetDefinition(Segment.HighwayType);
	if (!TypeDef.bHasCenterLineMarking)
	{
		return;
	}

	const int32 TotalLanes = Segment.GetTotalLaneCount();
	if (TotalLanes < 2)
	{
		return;
	}

	TArray<FVector2D> Line2D;
	Line2D.Reserve(Line.Num());
	for (const FVector& Point : Line)
	{
		Line2D.Add(FVector2D(Point.X, Point.Y));
	}

	const double LaneWidth = Segment.CarriagewayWidthCm / static_cast<double>(TotalLanes);
	const double HalfCarriageway = Segment.CarriagewayWidthCm * 0.5;

	FRoadMeshSection& Section = FindOrAddSection(
		OutMeshData, ERoadMeshChannel::LaneMarking, EOSMSurfaceType::Asphalt);

	// Markierung auf jeder Spurgrenze. Die Trennlinie zwischen den
	// Fahrtrichtungen wird ueber die Vertexfarbe als durchgezogene Mittellinie
	// gekennzeichnet, alle uebrigen als unterbrochene Leitlinie - das
	// unterscheidet der Shader ueber den Rotkanal.
	for (int32 Boundary = 1; Boundary < TotalLanes; ++Boundary)
	{
		const double OffsetFromCenter = HalfCarriageway - Boundary * LaneWidth;

		const bool bIsDirectionSplit = (Boundary == Segment.BackwardLaneCount)
			&& Segment.BackwardLaneCount > 0
			&& Segment.ForwardLaneCount > 0;

		TArray<FVector2D> MarkingAxis;
		if (!FPolygonUtils::OffsetPolyline(Line2D, OffsetFromCenter, MarkingAxis))
		{
			continue;
		}

		TArray<FVector2D> Vertices2D;
		TArray<int32> Indices;
		TArray<FVector2D> UVs;

		if (!FPolygonUtils::BuildRibbonMesh(
			MarkingAxis, Settings.MarkingWidthCm, Vertices2D, Indices, UVs))
		{
			continue;
		}

		const int32 BaseIndex = Section.Vertices.Num();

		for (int32 Index = 0; Index < Vertices2D.Num(); ++Index)
		{
			const int32 CenterIndex = FMath::Clamp(Index / 2, 0, Line.Num() - 1);

			Section.Vertices.Add(FVector(
				Vertices2D[Index].X,
				Vertices2D[Index].Y,
				Line[CenterIndex].Z + Settings.MarkingOffsetCm));

			Section.Normals.Add(FVector::UpVector);
			Section.UVs.Add(UVs[Index]);

			// R = 255: durchgezogen, R = 0: unterbrochen.
			Section.VertexColors.Add(FColor(bIsDirectionSplit ? 255 : 0, 255, 255, 255));
			Section.Tangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));
		}

		Section.Triangles.Reserve(Section.Triangles.Num() + Indices.Num());
		for (const int32 Index : Indices)
		{
			Section.Triangles.Add(Index + BaseIndex);
		}
	}
}

void URoadNetworkGenerator::BuildIntersectionMesh(
	const FRoadIntersection& Intersection,
	const FRoadGenerationSettings& Settings,
	FRoadMeshData& OutMeshData) const
{
	if (Intersection.Polygon.Num() < 3)
	{
		++GIntersectionTooFewPointsCount;
		return;
	}

	// Gehweg der Kreuzung als RING, nicht als gefuellte Flaeche.
	//
	// Hier stand eine gefuellte Huelle mit der Begruendung, die Fahrbahnplatte
	// werde davon nicht verdeckt, "weil sie kleiner ist". Das ist genau
	// verkehrt: Die Gehwegflaeche ist GROESSER und liegt seit der
	// Bordstein-Korrektur 12 cm HOEHER - sie deckt die Fahrbahn damit
	// vollstaendig zu. An allen Kreuzungen mit Gehweg lag Beton ueber dem
	// Asphalt.
	//
	// Der Ring zwischen aeusserem und innerem Umriss loest beides: Er fuellt
	// die Ecken zwischen den Armen und laesst die Fahrbahnmitte frei. Gebaut
	// wird er als Band - zu jedem Punkt des aeusseren Umrisses der
	// naechstgelegene Punkt des inneren. Beide Umrisse stammen aus denselben
	// Armenden und sind konvex, deshalb passt die Zuordnung.
	if (Intersection.SidewalkPolygon.Num() >= 3 && Intersection.Polygon.Num() >= 3)
	{
		// Bordsteinhoehe: dieselbe, mit der BuildSegmentMesh die Gehwegbaender
		// anlegt (FRoadSegment::KerbHeightCm, Standard 12 cm).
		constexpr double JunctionKerbHeightCm = 12.0;

		FRoadMeshSection& SidewalkSection = FindOrAddSection(
			OutMeshData, ERoadMeshChannel::Sidewalk, EOSMSurfaceType::Concrete);

		const FVector2D SidewalkCentre(Intersection.Location.X, Intersection.Location.Y);
		const int32 OuterCount = Intersection.SidewalkPolygon.Num();

		auto AddRingVertex = [&SidewalkSection, &SidewalkCentre](const FVector& Point, double ZOffset)
		{
			const int32 Index = SidewalkSection.Vertices.Num();
			SidewalkSection.Vertices.Add(FVector(Point.X, Point.Y, Point.Z + ZOffset));
			SidewalkSection.Normals.Add(FVector::UpVector);
			SidewalkSection.UVs.Add(FVector2D(
				(Point.X - SidewalkCentre.X) / MetersToCm,
				(Point.Y - SidewalkCentre.Y) / MetersToCm));
			SidewalkSection.VertexColors.Add(FColor(200, 230, 255, DefaultWetnessMask));
			SidewalkSection.Tangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));
			return Index;
		};

		// Zuordnung ueber den INDEX, solange beide Umrisse gleich lang sind.
		//
		// Seit beide aus denselben, nach Richtung sortierten Armen entstehen,
		// haben sie dieselbe Punktzahl UND dieselbe Reihenfolge: Punkt i des
		// Gehwegumrisses gehoert zu Punkt i des Fahrbahnumrisses. Das ist
		// exakt.
		//
		// Die Suche nach dem naechstgelegenen Punkt war eine Notloesung aus der
		// Zeit der konvexen Huelle, als beide Umrisse verschieden viele Ecken
		// haben konnten. Sie greift bei zwei dicht beieinander liegenden Armen
		// daneben - der Ring legt sich dann quer ueber die Kreuzung, im Bild
		// als helles Band ueber dem Asphalt.
		const bool bIndexAligned =
			Intersection.SidewalkPolygon.Num() == Intersection.Polygon.Num();

		// Zu jedem aeusseren Punkt den naechstgelegenen inneren suchen.
		auto NearestInner = [&Intersection](const FVector& Outer)
		{
			int32 Best = 0;
			double BestDistSq = TNumericLimits<double>::Max();
			for (int32 Index = 0; Index < Intersection.Polygon.Num(); ++Index)
			{
				const double DistSq = FVector2D::DistSquared(
					FVector2D(Outer.X, Outer.Y),
					FVector2D(Intersection.Polygon[Index].X, Intersection.Polygon[Index].Y));
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					Best = Index;
				}
			}
			return Best;
		};

		for (int32 Step = 0; Step < OuterCount; ++Step)
		{
			const FVector& OuterA = Intersection.SidewalkPolygon[Step];
			const FVector& OuterB = Intersection.SidewalkPolygon[(Step + 1) % OuterCount];
			const int32 IndexA = bIndexAligned ? Step : NearestInner(OuterA);
			const int32 IndexB = bIndexAligned
				? (Step + 1) % OuterCount : NearestInner(OuterB);

			const FVector& InnerA = Intersection.Polygon[IndexA];
			const FVector& InnerB = Intersection.Polygon[IndexB];

			// Entartete Streifen ueberspringen: Faellt der innere Punkt mit dem
			// aeusseren zusammen, gaebe es dort keinen Gehweg.
			if (FVector::Dist2D(OuterA, InnerA) < 1.0 && FVector::Dist2D(OuterB, InnerB) < 1.0)
			{
				continue;
			}

			const int32 IOuterA = AddRingVertex(OuterA, JunctionKerbHeightCm);
			const int32 IOuterB = AddRingVertex(OuterB, JunctionKerbHeightCm);
			const int32 IInnerA = AddRingVertex(InnerA, JunctionKerbHeightCm);
			const int32 IInnerB = AddRingVertex(InnerB, JunctionKerbHeightCm);

			SidewalkSection.Triangles.Add(IOuterA);
			SidewalkSection.Triangles.Add(IInnerA);
			SidewalkSection.Triangles.Add(IOuterB);

			SidewalkSection.Triangles.Add(IInnerA);
			SidewalkSection.Triangles.Add(IInnerB);
			SidewalkSection.Triangles.Add(IOuterB);
		}
	}

	// Faecher vom Mittelpunkt - keine Triangulierung.
	//
	// Der Umriss entsteht aus den nach Richtung sortierten Toren der Arme und
	// ist damit STERNFOERMIG um die Kreuzungsmitte: Jeder Randpunkt ist von
	// der Mitte aus geradlinig sichtbar. Fuer solche Umrisse ist der Faecher
	// die richtige Zerlegung - er ist immer gueltig, unabhaengig davon, ob der
	// Umriss konvex ist.
	//
	// Hier lief zuvor ein Ohr-Abschneiden ueber die konvexe Huelle. Das hat
	// zwei Fehler gleichzeitig erzeugt: Ecken schmaler Arme fielen aus der
	// Huelle heraus, und der Umlaufsinn kam aus dem Algorithmus statt aus der
	// Geometrie - 78.158 Dreiecke zeigten nach unten und waren unsichtbar.
	FRoadMeshSection& Section = FindOrAddSection(
		OutMeshData, ERoadMeshChannel::Intersection, EOSMSurfaceType::Asphalt);

	const int32 BaseIndex = Section.Vertices.Num();

	// UV-Ursprung im Kreuzungsmittelpunkt, Skalierung in Metern - so ist die
	// Asphalttextur ueber Kreuzungen und Fahrbahnen hinweg gleich gross.
	const FVector2D Center(Intersection.Location.X, Intersection.Location.Y);

	// Faechermitte ist der SCHWERPUNKT der Torecken, nicht der Knotenpunkt.
	//
	// Ein Faecher ist nur dann durchgaengig gleich orientiert, wenn sein
	// Mittelpunkt INNERHALB des Umrisses liegt. Der OSM-Knoten liegt das nicht
	// immer: Bei einer Gabelung, deren Arme in aehnliche Richtung laufen, ist
	// er von den Toren gar nicht umschlossen. Gemessen zeigten dadurch 70 %
	// der Dreiecke in die falsche Richtung - bei sauberem Umlauf muessten es
	// null oder alle sein.
	//
	// Der Schwerpunkt der Randpunkte liegt bei sternfoermigen Umrissen immer
	// innen.
	FVector Centroid = FVector::ZeroVector;
	for (const FVector& Point : Intersection.Polygon)
	{
		Centroid += Point;
	}
	Centroid /= static_cast<double>(Intersection.Polygon.Num());

	const double CentreZ = Centroid.Z;

	auto AddVertex = [&Section, &Center](const FVector& Point)
	{
		const int32 Index = Section.Vertices.Num();
		Section.Vertices.Add(Point);
		Section.Normals.Add(FVector::UpVector);
		Section.UVs.Add(FVector2D(
			(Point.X - Center.X) / MetersToCm,
			(Point.Y - Center.Y) / MetersToCm));
		Section.VertexColors.Add(FColor(200, 230, 255, DefaultWetnessMask));
		Section.Tangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));
		return Index;
	};

	const int32 CentreIndex = AddVertex(FVector(Centroid.X, Centroid.Y, CentreZ));

	TArray<int32> RimIndices;
	RimIndices.Reserve(Intersection.Polygon.Num());
	for (const FVector& Point : Intersection.Polygon)
	{
		RimIndices.Add(AddVertex(Point));
	}

	// Umlaufrichtung aus der GEOMETRIE, nicht aus einer Annahme.
	//
	// Unreal zeichnet die Vorderseite im Uhrzeigersinn aus Blickrichtung. Von
	// oben gesehen heisst das: Das Kreuzprodukt muss nach unten zeigen.
	for (int32 Step = 0; Step < RimIndices.Num(); ++Step)
	{
		const int32 IA = CentreIndex;
		const int32 IB = RimIndices[Step];
		const int32 IC = RimIndices[(Step + 1) % RimIndices.Num()];

		const FVector& A = Section.Vertices[IA];
		const FVector& B = Section.Vertices[IB];
		const FVector& C = Section.Vertices[IC];

		if (FVector::CrossProduct(B - A, C - A).Z > 0.0)
		{
			Section.Triangles.Add(IA);
			Section.Triangles.Add(IC);
			Section.Triangles.Add(IB);
			++GIntersectionFlippedTriangles;
		}
		else
		{
			Section.Triangles.Add(IA);
			Section.Triangles.Add(IB);
			Section.Triangles.Add(IC);
		}
	}
}

void URoadNetworkGenerator::BuildBendFillers(
	const FRoadNetwork& Network,
	FRoadMeshData& OutMeshData) const
{
	// Ein Ende eines Fahrbahnbands an einem Knoten.
	struct FRibbonEnd
	{
		FVector Point = FVector::ZeroVector;    // Endpunkt der Mittellinie
		FVector2D Outward = FVector2D::ZeroVector;   // Richtung AUS dem Knoten heraus
		double HalfWidthCm = 0.0;
		double SidewalkWidthCm = 0.0;
	};

	TMap<int64, TArray<FRibbonEnd>> EndsByNode;
	EndsByNode.Reserve(Network.Segments.Num());

	for (const FRoadSegment& Segment : Network.Segments)
	{
		const TArray<FVector>& Line = Segment.TrimmedCenterline.Num() >= 2
			? Segment.TrimmedCenterline
			: Segment.Centerline;

		if (Line.Num() < 2)
		{
			continue;
		}

		const double HalfWidth = Segment.CarriagewayWidthCm * 0.5;
		const double SidewalkWidth = Segment.SidewalkType != EOSMSidewalkType::None
			? Segment.SidewalkWidthCm
			: 0.0;

		auto AddEnd = [&](int64 NodeId, const FVector& End, const FVector& Neighbour)
		{
			FVector2D Outward(Neighbour.X - End.X, Neighbour.Y - End.Y);
			if (!Outward.Normalize())
			{
				return;
			}

			FRibbonEnd Entry;
			Entry.Point = End;
			Entry.Outward = Outward;
			Entry.HalfWidthCm = HalfWidth;
			Entry.SidewalkWidthCm = SidewalkWidth;
			EndsByNode.FindOrAdd(NodeId).Add(Entry);
		};

		AddEnd(Segment.StartNodeId, Line[0], Line[1]);
		AddEnd(Segment.EndNodeId, Line.Last(), Line[Line.Num() - 2]);
	}

	int32 FillerCount = 0;

	// Deterministische Reihenfolge.
	TArray<int64> NodeIds;
	EndsByNode.GetKeys(NodeIds);
	NodeIds.Sort();

	for (const int64 NodeId : NodeIds)
	{
		// Kreuzungen haben ihre eigene Flaeche; Sackgassen brauchen keine.
		if (Network.IntersectionByNode.Contains(NodeId))
		{
			continue;
		}

		const TArray<FRibbonEnd>& Ends = EndsByNode[NodeId];
		if (Ends.Num() != 2)
		{
			continue;
		}

		// Eckpunkte beider Endquerschnitte einsammeln. Ihre konvexe Huelle
		// ueberdeckt sowohl den Keil am Knick als auch einen Breitensprung.
		TArray<FVector2D> Corners;
		TArray<double> Heights;
		TArray<FVector2D> OuterCorners;
		TArray<double> OuterHeights;

		bool bAnySidewalk = false;

		for (const FRibbonEnd& End : Ends)
		{
			const FVector2D Normal = FPolygonUtils::GetLeftNormal(End.Outward);
			const FVector2D At(End.Point.X, End.Point.Y);

			Corners.Add(At + Normal * End.HalfWidthCm);
			Corners.Add(At - Normal * End.HalfWidthCm);
			Heights.Add(End.Point.Z);
			Heights.Add(End.Point.Z);

			const double Outer = End.HalfWidthCm
				+ (End.SidewalkWidthCm > 0.0 ? End.SidewalkWidthCm : 0.0);
			OuterCorners.Add(At + Normal * Outer);
			OuterCorners.Add(At - Normal * Outer);
			OuterHeights.Add(End.Point.Z);
			OuterHeights.Add(End.Point.Z);

			bAnySidewalk = bAnySidewalk || End.SidewalkWidthCm > 0.0;
		}

		auto EmitPlate = [&OutMeshData](const TArray<FVector2D>& Points, const TArray<double>& PointHeights,
			ERoadMeshChannel Channel, EOSMSurfaceType Surface, double ZOffset) -> bool
		{
			TArray<FVector2D> Hull;
			if (!FPolygonUtils::ComputeConvexHull(Points, Hull) || Hull.Num() < 3)
			{
				return false;
			}

			TArray<int32> Indices;
			if (!FPolygonUtils::TriangulatePolygon(Hull, Indices))
			{
				return false;
			}

			FRoadMeshSection& Section = FindOrAddSection(OutMeshData, Channel, Surface);
			const int32 BaseIndex = Section.Vertices.Num();

			FVector2D Centre = FVector2D::ZeroVector;
			for (const FVector2D& Point : Hull)
			{
				Centre += Point;
			}
			Centre /= static_cast<double>(Hull.Num());

			for (const FVector2D& Point : Hull)
			{
				// Hoehe des naechstgelegenen Eckpunkts uebernehmen, damit die
				// Fuellflaeche mit beiden Baendern buendig liegt.
				double BestDistSq = TNumericLimits<double>::Max();
				double BestZ = 0.0;
				for (int32 Index = 0; Index < Points.Num(); ++Index)
				{
					const double DistSq = FVector2D::DistSquared(Point, Points[Index]);
					if (DistSq < BestDistSq)
					{
						BestDistSq = DistSq;
						BestZ = PointHeights[Index];
					}
				}

				Section.Vertices.Add(FVector(Point.X, Point.Y, BestZ + ZOffset));
				Section.Normals.Add(FVector::UpVector);
				Section.UVs.Add(FVector2D(
					(Point.X - Centre.X) / MetersToCm,
					(Point.Y - Centre.Y) / MetersToCm));
				Section.VertexColors.Add(FColor(200, 230, 255, DefaultWetnessMask));
				Section.Tangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));
			}

			Section.Triangles.Reserve(Section.Triangles.Num() + Indices.Num());
			for (const int32 Index : Indices)
			{
				Section.Triangles.Add(Index + BaseIndex);
			}
			return true;
		};

		// Gehwegflaeche zuerst und zwei Zentimeter tiefer, damit die Fahrbahn
		// oben liegt - dieselbe Reihenfolge wie an den Kreuzungen.
		if (bAnySidewalk)
		{
			EmitPlate(OuterCorners, OuterHeights,
				ERoadMeshChannel::Sidewalk, EOSMSurfaceType::Concrete, -2.0);
		}

		if (EmitPlate(Corners, Heights,
			ERoadMeshChannel::Intersection, EOSMSurfaceType::Asphalt, 0.0))
		{
			++FillerCount;
		}
	}

	if (FillerCount > 0)
	{
		UE_LOG(LogWbRoads, Log,
			TEXT("Knick-Fugen geschlossen: %d Fuellflaechen an Knoten mit zwei Armen."),
			FillerCount);
	}
}

void URoadNetworkGenerator::BuildEmbankmentMesh(
	const FRoadSegment& Segment,
	const URoadTypeLibrary& TypeLibrary,
	const IHeightSampler* HeightSampler,
	const FRoadGenerationSettings& Settings,
	FRoadMeshData& OutMeshData) const
{
	if (!HeightSampler || !HeightSampler->HasValidData())
	{
		return;
	}

	const TArray<FVector>& Line = Segment.TrimmedCenterline.Num() >= 2
		? Segment.TrimmedCenterline
		: Segment.Centerline;

	if (Line.Num() < 2)
	{
		return;
	}

	TArray<FVector2D> Line2D;
	Line2D.Reserve(Line.Num());
	for (const FVector& Point : Line)
	{
		Line2D.Add(FVector2D(Point.X, Point.Y));
	}

	// Aeussere Kante der befestigten Flaeche: Fahrbahn plus Gehweg, wenn einer
	// vorhanden ist. Dort setzt die Boeschung an.
	const bool bHasSidewalk = Segment.SidewalkType != EOSMSidewalkType::None
		&& Segment.SidewalkType != EOSMSidewalkType::Separate;

	const double OuterHalfWidth = Segment.CarriagewayWidthCm * 0.5
		+ (bHasSidewalk ? Segment.SidewalkWidthCm : 0.0);

	for (int32 Side = 0; Side < 2; ++Side)
	{
		const double SideSign = (Side == 0) ? 1.0 : -1.0;

		TArray<FVector2D> EdgeLine;
		if (!FPolygonUtils::OffsetPolyline(Line2D, SideSign * OuterHalfWidth, EdgeLine)
			|| EdgeLine.Num() != Line.Num())
		{
			continue;
		}

		// Erst pruefen, ob ueberhaupt irgendwo eine nennenswerte Kante frei
		// steht - sonst entstuende fuer jede ebene Strasse eine leere Sektion.
		bool bAnyGap = false;
		for (int32 Index = 0; Index < EdgeLine.Num(); ++Index)
		{
			const double TerrainZ = HeightSampler->SampleHeightCm(EdgeLine[Index]);
			if (Line[Index].Z - TerrainZ > Settings.EmbankmentMinHeightCm)
			{
				bAnyGap = true;
				break;
			}
		}

		if (!bAnyGap)
		{
			continue;
		}

		// Erdfarbene Boeschung: Kanal Fahrbahn mit unbefestigter Oberflaeche,
		// damit ResolveRoadMaterial das Erd-Material waehlt.
		FRoadMeshSection& Section = FindOrAddSection(
			OutMeshData, ERoadMeshChannel::Embankment, EOSMSurfaceType::Ground);

		const int32 BaseIndex = Section.Vertices.Num();
		double AccumulatedLength = 0.0;

		for (int32 Index = 0; Index < EdgeLine.Num(); ++Index)
		{
			if (Index > 0)
			{
				AccumulatedLength += FVector2D::Distance(EdgeLine[Index - 1], EdgeLine[Index]);
			}

			const double TerrainZ = HeightSampler->SampleHeightCm(EdgeLine[Index]);
			const double TopZ = Line[Index].Z;

			// Nach unten begrenzen: Bruecken stehen weit ueber Grund, dort
			// waere eine Erdwand falsch (siehe EmbankmentMaxHeightCm).
			double BottomZ = FMath::Max(TerrainZ - 10.0,
				TopZ - Settings.EmbankmentMaxHeightCm);

			// UND NIEMALS UEBER DIE FAHRBAHN.
			//
			// Das Gelaende liegt nicht ueberall unter der Strasse: gemessen
			// sind 10 % der Fahrbahn-Vertices bis zu 2,3 m davon ueberdeckt.
			// Ohne diese Klemmung kippt das Band an solchen Stellen - die
			// Boeschung stuende als Erdwand UEBER der Fahrbahn statt unter ihr.
			// An Kreuzungen, wo Arme verschiedener Hoehe zusammenlaufen, trifft
			// das besonders oft zu.
			BottomZ = FMath::Min(BottomZ, TopZ);

			Section.Vertices.Add(FVector(EdgeLine[Index].X, EdgeLine[Index].Y, TopZ));
			Section.Vertices.Add(FVector(EdgeLine[Index].X, EdgeLine[Index].Y, BottomZ));

			FVector2D Tangent2D = (Index > 0)
				? (EdgeLine[Index] - EdgeLine[Index - 1]).GetSafeNormal()
				: (EdgeLine[1] - EdgeLine[0]).GetSafeNormal();

			// Normale zeigt von der Fahrbahn WEG - die Boeschung wird von
			// aussen gesehen.
			const FVector2D Normal2D = FPolygonUtils::GetLeftNormal(Tangent2D) * SideSign;
			const FVector Normal(Normal2D.X, Normal2D.Y, 0.0);

			Section.Normals.Add(Normal);
			Section.Normals.Add(Normal);

			const double VMeters = AccumulatedLength / MetersToCm;
			Section.UVs.Add(FVector2D(VMeters, 0.0));
			Section.UVs.Add(FVector2D(VMeters, 1.0));

			Section.VertexColors.Add(FColor::White);
			Section.VertexColors.Add(FColor::White);

			Section.Tangents.Add(FProcMeshTangent(Tangent2D.X, Tangent2D.Y, 0.0f));
			Section.Tangents.Add(FProcMeshTangent(Tangent2D.X, Tangent2D.Y, 0.0f));

			if (Index > 0)
			{
				const int32 Base = BaseIndex + (Index - 1) * 2;

				// Wicklung wie beim Bordstein: die Seite mit SideSign spiegeln,
				// sonst zeigt eine der beiden Boeschungen nach innen und wird
				// vom Backface-Culling weggeschnitten.
				if (SideSign > 0.0)
				{
					Section.Triangles.Add(Base + 0);
					Section.Triangles.Add(Base + 1);
					Section.Triangles.Add(Base + 2);
					Section.Triangles.Add(Base + 1);
					Section.Triangles.Add(Base + 3);
					Section.Triangles.Add(Base + 2);
				}
				else
				{
					Section.Triangles.Add(Base + 0);
					Section.Triangles.Add(Base + 2);
					Section.Triangles.Add(Base + 1);
					Section.Triangles.Add(Base + 1);
					Section.Triangles.Add(Base + 2);
					Section.Triangles.Add(Base + 3);
				}
			}
		}
	}
}

int32 URoadNetworkGenerator::SnapLooseRoadEnds(
	const FRoadGenerationSettings& Settings,
	FRoadNetwork& Network) const
{
	const double Radius = FMath::Max(Settings.LooseEndSnapRadiusCm, 0.0);
	if (Radius <= 0.0 || Network.Segments.Num() == 0)
	{
		return 0;
	}

	// Ein "loses Ende" ist ein Knoten, den genau EIN Segmentende beruehrt.
	// Knoten mit zwei oder mehr Enden sind bereits verbunden.
	// WICHTIG: Zu diesem Zeitpunkt ist Segment.Centerline noch LEER - sie wird
	// erst bei der Geländeprojektion gefuellt. Die Geometrie liegt in den
	// 2D-Arbeitsdaten. Der erste Versuch griff auf Centerline zu und
	// verband deshalb gar nichts.
	TMap<int64, int32> EndsPerNode;
	for (int32 Index = 0; Index < Network.Segments.Num(); ++Index)
	{
		if (!WorkingCenterlines2D.IsValidIndex(Index) || WorkingCenterlines2D[Index].Num() < 2)
		{
			continue;
		}
		++EndsPerNode.FindOrAdd(Network.Segments[Index].StartNodeId);
		++EndsPerNode.FindOrAdd(Network.Segments[Index].EndNodeId);
	}

	struct FLooseEnd
	{
		int32 SegmentIndex = INDEX_NONE;
		bool bIsStart = false;
		int64 NodeId = 0;
		FVector2D Position = FVector2D::ZeroVector;
		FVector2D Outward = FVector2D::ZeroVector;   // vom Segment weg
	};

	TArray<FLooseEnd> Loose;
	for (int32 Index = 0; Index < Network.Segments.Num(); ++Index)
	{
		const FRoadSegment& Segment = Network.Segments[Index];
		if (!WorkingCenterlines2D.IsValidIndex(Index) || WorkingCenterlines2D[Index].Num() < 2)
		{
			continue;
		}

		const TArray<FVector2D>& Line = WorkingCenterlines2D[Index];

		const int32* StartCount = EndsPerNode.Find(Segment.StartNodeId);
		if (StartCount && *StartCount == 1)
		{
			FLooseEnd End;
			End.SegmentIndex = Index;
			End.bIsStart = true;
			End.NodeId = Segment.StartNodeId;
			End.Position = Line[0];
			End.Outward = (Line[0] - Line[1]).GetSafeNormal();
			Loose.Add(End);
		}

		const int32* EndCount = EndsPerNode.Find(Segment.EndNodeId);
		if (EndCount && *EndCount == 1)
		{
			const int32 Last = Line.Num() - 1;
			FLooseEnd End;
			End.SegmentIndex = Index;
			End.bIsStart = false;
			End.NodeId = Segment.EndNodeId;
			End.Position = Line[Last];
			End.Outward = (Line[Last] - Line[Last - 1]).GetSafeNormal();
			Loose.Add(End);
		}
	}

	// Immer melden, wie viele freie Enden ueberhaupt gefunden wurden. Ohne
	// diese Zahl laesst sich "nichts verbunden" nicht von "nichts gefunden"
	// unterscheiden.
	UE_LOG(LogWbRoads, Log,
		TEXT("Lose Enden: %d gefunden (von %d Segmenten), Suchradius %.0f cm."),
		Loose.Num(), Network.Segments.Num(), Radius);

	if (Loose.Num() < 2)
	{
		return 0;
	}

	// Rasterindex, sonst waere der Paarvergleich quadratisch.
	const double CellSize = FMath::Max(Radius, 100.0);
	TMultiMap<FIntPoint, int32> Grid;
	auto CellOf = [CellSize](const FVector2D& P)
	{
		return FIntPoint(
			FMath::FloorToInt(P.X / CellSize),
			FMath::FloorToInt(P.Y / CellSize));
	};

	for (int32 Index = 0; Index < Loose.Num(); ++Index)
	{
		Grid.Add(CellOf(Loose[Index].Position), Index);
	}

	const double RadiusSq = Radius * Radius;
	TArray<bool> bUsed;
	bUsed.Init(false, Loose.Num());

	int32 SnappedPairs = 0;
	int32 RejectedCount = 0;
	int32 NearButNotFacing = 0;

	for (int32 A = 0; A < Loose.Num(); ++A)
	{
		if (bUsed[A])
		{
			continue;
		}

		int32 BestB = INDEX_NONE;
		double BestDistSq = RadiusSq;

		const FIntPoint Base = CellOf(Loose[A].Position);
		TArray<int32> Candidates;

		for (int32 Dy = -1; Dy <= 1; ++Dy)
		{
			for (int32 Dx = -1; Dx <= 1; ++Dx)
			{
				Candidates.Reset();
				Grid.MultiFind(FIntPoint(Base.X + Dx, Base.Y + Dy), Candidates);

				for (const int32 B : Candidates)
				{
					if (B == A || bUsed[B] || Loose[B].SegmentIndex == Loose[A].SegmentIndex)
					{
						continue;
					}

					const double DistSq = FVector2D::DistSquared(
						Loose[A].Position, Loose[B].Position);
					if (DistSq >= BestDistSq)
					{
						continue;
					}

					// Jedes Ende muss AUF DAS ANDERE ZU zeigen.
					//
					// Hier wurden zuvor die beiden Auswaertsrichtungen
					// miteinander verglichen und nur fast entgegengesetzte
					// Paare zugelassen. Das lehnt Ecken ab: Zwei Strassen, die
					// im rechten Winkel aufeinandertreffen, zeigen um 90 Grad
					// versetzt - gemessen fielen dadurch 987 Paare durch,
					// verbunden wurden nur 186.
					//
					// Die Richtung ZUM anderen Ende trifft die Absicht besser:
					// Sie laesst Ecken zu und weist parallele Sackgassen ab,
					// bei denen das andere Ende seitlich liegt statt voraus.
					const FVector2D ToOther = (Loose[B].Position - Loose[A].Position).GetSafeNormal();
					const double AlignA = FVector2D::DotProduct(Loose[A].Outward, ToOther);
					const double AlignB = FVector2D::DotProduct(Loose[B].Outward, -ToOther);

					if (AlignA < 0.1 || AlignB < 0.1)
					{
						++NearButNotFacing;
						continue;
					}

					BestDistSq = DistSq;
					BestB = B;
				}
			}
		}

		if (BestB == INDEX_NONE)
		{
			++RejectedCount;
			continue;
		}

		// Beide Enden auf die Mitte ziehen und denselben Knoten tragen lassen.
		const FVector2D Meeting = (Loose[A].Position + Loose[BestB].Position) * 0.5;
		const int64 SharedNode = FMath::Min(Loose[A].NodeId, Loose[BestB].NodeId);

		for (const int32 Which : { A, BestB })
		{
			FRoadSegment& Segment = Network.Segments[Loose[Which].SegmentIndex];
			TArray<FVector2D>& Line = WorkingCenterlines2D[Loose[Which].SegmentIndex];
			const int32 PointIndex = Loose[Which].bIsStart ? 0 : Line.Num() - 1;

			Line[PointIndex] = Meeting;

			if (Loose[Which].bIsStart)
			{
				Segment.StartNodeId = SharedNode;
			}
			else
			{
				Segment.EndNodeId = SharedNode;
			}
		}

		bUsed[A] = true;
		bUsed[BestB] = true;
		++SnappedPairs;
	}

	UE_LOG(LogWbRoads, Log,
		TEXT("Lose Enden: %d Paare verbunden, %d ohne Partner, %d nah aber nicht zueinander gerichtet."),
		SnappedPairs, RejectedCount, NearButNotFacing);

	return SnappedPairs;
}
