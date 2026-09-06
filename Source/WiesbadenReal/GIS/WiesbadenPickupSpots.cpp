// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/WiesbadenPickupSpots.h"

#include "WiesbadenReal.h"

// -----------------------------------------------------------------------------
// FWiesbadenPickupSpotLayout
// -----------------------------------------------------------------------------

FString FWiesbadenPickupSpotLayout::GetStatisticsString() const
{
	int32 Fuel = 0;
	int32 Health = 0;
	for (const FWiesbadenPickupSpot& Spot : Spots)
	{
		switch (Spot.Kind)
		{
		case EWiesbadenPickupSpotKind::Fuel: ++Fuel; break;
		case EWiesbadenPickupSpotKind::Health: ++Health; break;
		default: break;
		}
	}
	return FString::Printf(TEXT("%d Treibstoff-Pickups, %d Gesundheits-Pickups"), Fuel, Health);
}

// -----------------------------------------------------------------------------
// Klassifikation
// -----------------------------------------------------------------------------

bool UWiesbadenPickupSpotGenerator::ClassifyNode(
	const FOSMNode& Node, EWiesbadenPickupSpotKind& OutKind)
{
	if (Node.HasTagValue(TEXT("amenity"), TEXT("fuel")))
	{
		OutKind = EWiesbadenPickupSpotKind::Fuel;
		return true;
	}
	if (Node.HasTagValue(TEXT("amenity"), TEXT("pharmacy"))
		|| Node.HasTagValue(TEXT("amenity"), TEXT("hospital")))
	{
		OutKind = EWiesbadenPickupSpotKind::Health;
		return true;
	}
	return false;
}

// -----------------------------------------------------------------------------
// Spur-Rastung
// -----------------------------------------------------------------------------

double UWiesbadenPickupSpotGenerator::FindNearestLanePoint(
	const FRoadNetwork& Network, const FVector& WorldXY,
	FVector& OutPoint, double& OutLaneWidthCm, double& OutSidewalkWidthCm)
{
	OutPoint = FVector::ZeroVector;
	OutLaneWidthCm = 325.0;
	OutSidewalkWidthCm = 0.0;

	if (Network.IsEmpty())
	{
		return TNumericLimits<double>::Max();
	}

	// 2D-Abstand: Z vergleichen macht keinen Sinn - die Lane liegt auf
	// Terrainhoehe, der Node wird erst danach gehoben.
	const FVector2D Query(WorldXY.X, WorldXY.Y);

	double BestDistSq = TNumericLimits<double>::Max();

	for (const FRoadLane& Lane : Network.Lanes)
	{
		if (Lane.Centerline.Num() < 2)
		{
			continue;
		}

		const int32 PointCount = Lane.Centerline.Num();
		for (int32 i = 0; i < PointCount - 1; ++i)
		{
			const FVector& A = Lane.Centerline[i];
			const FVector& B = Lane.Centerline[i + 1];

			const FVector2D AB(B.X - A.X, B.Y - A.Y);
			const double LenSq = AB.X * AB.X + AB.Y * AB.Y;
			if (LenSq < 1.0)
			{
				continue;
			}

			const FVector2D AP(Query.X - A.X, Query.Y - A.Y);
			const double T = FMath::Clamp((AP.X * AB.X + AP.Y * AB.Y) / LenSq, 0.0, 1.0);

			const FVector2D Closest(A.X + AB.X * T, A.Y + AB.Y * T);
			const double Dx = Query.X - Closest.X;
			const double Dy = Query.Y - Closest.Y;
			const double DistSq = Dx * Dx + Dy * Dy;

			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;

				// Hoehe an der Projektionsstelle interpolieren.
				OutPoint = FVector(Closest.X, Closest.Y, FMath::Lerp(A.Z, B.Z, T));

				const FRoadSegment* Segment2 = Network.GetSegment(Lane.SegmentId);
				OutLaneWidthCm = Lane.WidthCm;
				OutSidewalkWidthCm = Segment2 ? Segment2->SidewalkWidthCm : 0.0;
			}
		}
	}

	return FMath::Sqrt(BestDistSq);
}

FVector UWiesbadenPickupSpotGenerator::ComputeRoadsidePosition(
	const FVector& LanePoint, const FVector& NodeWorldXY,
	double LaneWidthCm, double SidewalkWidthCm)
{
	// Von der Spurmitte raus zum Fahrbahnrand und - bei vorhandenem Gehweg -
	// dessen halbe Breite hinein: Das Pickup steht dann auf dem Gehweg neben
	// der Fahrbahn, nicht auf der Fahrbahn selbst und nicht im Innenhof.
	const double Outset = LaneWidthCm * 0.5 + (SidewalkWidthCm > 0.0 ? SidewalkWidthCm * 0.5 : 50.0);

	const FVector2D ToNode(NodeWorldXY.X - LanePoint.X, NodeWorldXY.Y - LanePoint.Y);
	if (ToNode.SizeSquared() < 1.0)
	{
		// Degeneriert: Richtung egal, einfach nach "Sueden" rausschieben.
		return FVector(LanePoint.X, LanePoint.Y - Outset, LanePoint.Z);
	}

	const FVector2D Dir = ToNode.GetSafeNormal();
	return FVector(LanePoint.X + Dir.X * Outset, LanePoint.Y + Dir.Y * Outset, LanePoint.Z);
}

// -----------------------------------------------------------------------------
// Der Pass
// -----------------------------------------------------------------------------

FPickupSpotReport UWiesbadenPickupSpotGenerator::Generate(
	const FOSMDataSet& OsmData,
	const UGeoCoordinateConverter& Converter,
	const FRoadNetwork* Network,
	const IHeightSampler* HeightSampler,
	const FWiesbadenPickupSpotSettings& Settings,
	FWiesbadenPickupSpotLayout& OutLayout)
{
	FPickupSpotReport Report;
	Report.bSuccess = true;

	OutLayout.Reset();

	if (!OsmData.Nodes.Num())
	{
		Report.ErrorMessage = TEXT("Keine OSM-Nodes - keine Pickups.");
		return Report;
	}

	// Empfangslisten pro Art (fuer Mindestabstand und Capping getrennt).
	TArray<FWiesbadenPickupSpot> FuelSpots;
	TArray<FWiesbadenPickupSpot> HealthSpots;

	for (const TPair<FOSMId, FOSMNode>& Pair : OsmData.Nodes)
	{
		const FOSMNode& Node = Pair.Value;

		EWiesbadenPickupSpotKind Kind;
		if (!ClassifyNode(Node, Kind))
		{
			continue;
		}

		// Geo -> Welt (XY); die Hoehe kommt vom Sampler.
		const FVector WorldXY = Converter.GeoToUnreal(Node.Location);

		FVector Candidate(WorldXY.X, WorldXY.Y, 0.0);
		bool bSnapped = false;

		if (Network && !Network->IsEmpty())
		{
			FVector LanePoint = FVector::ZeroVector;
			double LaneWidthCm = 0.0;
			double SidewalkWidthCm = 0.0;
			const double DistCm = FindNearestLanePoint(*Network, Candidate, LanePoint, LaneWidthCm, SidewalkWidthCm);

			if (DistCm <= Settings.SnapRadiusCm)
			{
				Candidate = ComputeRoadsidePosition(LanePoint, Candidate, LaneWidthCm, SidewalkWidthCm);
				bSnapped = true;
			}
		}

		if (HeightSampler && HeightSampler->HasValidData())
		{
			Candidate.Z = HeightSampler->SampleHeightCm(FVector2D(Candidate.X, Candidate.Y));
		}

		FWiesbadenPickupSpot Spot;
		Spot.Kind = Kind;
		Spot.Location = Candidate;
		Spot.SourceName = Node.GetTag(TEXT("name"));
		Spot.SourceNodeId = Node.Id;
		Spot.bSnappedToRoad = bSnapped;

		if (Kind == EWiesbadenPickupSpotKind::Fuel)
		{
			FuelSpots.Add(Spot);
		}
		else
		{
			HealthSpots.Add(Spot);
		}
	}

	// Mindestabstand + Obergrenze je Art durchziehen.
	ApplySpacingAndCap(FuelSpots, Settings.MinSpacingCm, Settings.MaxFuelPickups,
		Report.SkippedTooCloseCount, Report.SkippedOverCapCount);
	ApplySpacingAndCap(HealthSpots, Settings.MinSpacingCm, Settings.MaxHealthPickups,
		Report.SkippedTooCloseCount, Report.SkippedOverCapCount);

	OutLayout.Spots.Reserve(FuelSpots.Num() + HealthSpots.Num());
	OutLayout.Spots.Append(FuelSpots);
	OutLayout.Spots.Append(HealthSpots);

	for (const FWiesbadenPickupSpot& Spot : OutLayout.Spots)
	{
		if (Spot.bSnappedToRoad)
		{
			++Report.SnappedToRoadCount;
		}
	}

	Report.FuelCount = FuelSpots.Num();
	Report.HealthCount = HealthSpots.Num();

	UE_LOG(LogWbGIS, Log, TEXT("Pickup-Spots: %s (gerastet: %d, zu nah: %d, ueber Obergrenze: %d)"),
		*OutLayout.GetStatisticsString(),
		Report.SnappedToRoadCount, Report.SkippedTooCloseCount, Report.SkippedOverCapCount);

	return Report;
}

// -----------------------------------------------------------------------------
// Ausduennen
// -----------------------------------------------------------------------------

void UWiesbadenPickupSpotGenerator::ApplySpacingAndCap(
	TArray<FWiesbadenPickupSpot>& Spots, double MinSpacingCm, int32 MaxCount,
	int32& InOutSkippedTooClose, int32& InOutSkippedOverCap)
{
	TArray<FWiesbadenPickupSpot> Kept;
	Kept.Reserve(Spots.Num());

	for (FWiesbadenPickupSpot& Spot : Spots)
	{
		if (Kept.Num() >= MaxCount)
		{
			++InOutSkippedOverCap;
			continue;
		}

		bool bTooClose = false;
		for (const FWiesbadenPickupSpot& Existing : Kept)
		{
			const double Dx = Spot.Location.X - Existing.Location.X;
			const double Dy = Spot.Location.Y - Existing.Location.Y;
			const double DistSq = Dx * Dx + Dy * Dy;
			if (DistSq < MinSpacingCm * MinSpacingCm)
			{
				bTooClose = true;
				break;
			}
		}

		if (bTooClose)
		{
			++InOutSkippedTooClose;
		}
		else
		{
			Kept.Add(Spot);
		}
	}

	Spots = MoveTemp(Kept);
}
