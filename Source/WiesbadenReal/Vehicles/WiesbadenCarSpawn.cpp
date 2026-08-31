// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenCarSpawn.h"

#include "WiesbadenReal.h"

double FWiesbadenCarSpawn::DistanceToPolylineSquared(
	const TArray<FVector>& Polyline,
	const FVector& Point,
	FVector& OutClosest,
	FVector& OutDirection)
{
	OutClosest = FVector::ZeroVector;
	OutDirection = FVector::ForwardVector;

	if (Polyline.Num() == 0)
	{
		return TNumericLimits<double>::Max();
	}

	if (Polyline.Num() == 1)
	{
		OutClosest = Polyline[0];
		return FVector2D::DistSquared(
			FVector2D(Point.X, Point.Y),
			FVector2D(Polyline[0].X, Polyline[0].Y));
	}

	double BestDistSq = TNumericLimits<double>::Max();

	// Der Abstand wird HORIZONTAL gemessen, der zurueckgegebene Punkt behaelt
	// aber die Hoehe der Fahrbahn.
	//
	// Grund: der Zielpunkt kommt aus einer Adresse und hat keine sinnvolle
	// Hoehe (GeoToUnrealGround liefert Z = 0), waehrend die Spuren auf
	// Terrainhoehe liegen. In Wiesbaden sind das am Hang ueber 100 m
	// Unterschied - eine 3D-Messung wuerde davon voellig dominiert und koennte
	// eine weiter entfernte Strasse waehlen, die zufaellig auf aehnlicher
	// Hoehe liegt. Gesucht ist die naechste Strasse in der Draufsicht.
	const FVector2D Point2D(Point.X, Point.Y);

	for (int32 Index = 1; Index < Polyline.Num(); ++Index)
	{
		const FVector& A = Polyline[Index - 1];
		const FVector& B = Polyline[Index];

		const FVector2D A2D(A.X, A.Y);
		const FVector2D AB2D(B.X - A.X, B.Y - A.Y);
		const double LengthSq2D = AB2D.SizeSquared();

		FVector Candidate;
		if (LengthSq2D < UE_DOUBLE_SMALL_NUMBER)
		{
			Candidate = A;
		}
		else
		{
			// Projektion auf das Segment, begrenzt auf [0,1]. Ohne die
			// Begrenzung laege der Punkt auf der Geraden durch das Segment
			// und damit moeglicherweise weit ausserhalb der Strasse.
			const double T = FMath::Clamp(FVector2D::DotProduct(Point2D - A2D, AB2D) / LengthSq2D, 0.0, 1.0);
			Candidate = A + (B - A) * T;
		}

		const double DistSq = FVector2D::DistSquared(Point2D, FVector2D(Candidate.X, Candidate.Y));
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			OutClosest = Candidate;
			OutDirection = (B - A).GetSafeNormal();
			if (OutDirection.IsNearlyZero())
			{
				OutDirection = FVector::ForwardVector;
			}
		}
	}

	return BestDistSq;
}

bool FWiesbadenCarSpawn::FindNearestDrivableLanePoint(
	const FRoadNetwork& Network,
	const FVector& TargetWorld,
	double MaxRadiusCm,
	FVector& OutLocation,
	FRotator& OutRotation,
	int32& OutLaneId)
{
	OutLocation = TargetWorld;
	OutRotation = FRotator::ZeroRotator;
	OutLaneId = INDEX_NONE;

	if (Network.Lanes.Num() == 0)
	{
		UE_LOG(LogWbVehicles, Warning,
			TEXT("Spielerstart: Strassennetz enthaelt keine Fahrspuren."));
		return false;
	}

	const double MaxRadiusSq = MaxRadiusCm * MaxRadiusCm;

	double BestDistSq = TNumericLimits<double>::Max();
	FVector BestPoint = FVector::ZeroVector;
	FVector BestDirection = FVector::ForwardVector;
	int32 BestLane = INDEX_NONE;

	for (const FRoadLane& Lane : Network.Lanes)
	{
		if (!Lane.IsValid())
		{
			continue;
		}

		const FRoadSegment* Segment = Network.GetSegment(Lane.SegmentId);
		if (!Segment || !FOSMTagParser::IsDrivable(Segment->HighwayType))
		{
			continue;
		}

		FVector Closest;
		FVector Direction;
		const double DistSq = DistanceToPolylineSquared(Lane.Centerline, TargetWorld, Closest, Direction);

		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestPoint = Closest;
			BestDirection = Direction;
			BestLane = Lane.LaneId;
		}
	}

	if (BestLane == INDEX_NONE || BestDistSq > MaxRadiusSq)
	{
		UE_LOG(LogWbVehicles, Warning,
			TEXT("Spielerstart: keine befahrbare Spur im Umkreis von %.0f m gefunden (naechste: %.0f m)."),
			MaxRadiusCm / 100.0,
			BestLane == INDEX_NONE ? -1.0 : FMath::Sqrt(BestDistSq) / 100.0);
		return false;
	}

	OutLocation = BestPoint + FVector(0.0, 0.0, SpawnHeightOffsetCm);
	OutRotation = BestDirection.Rotation();
	OutLaneId = BestLane;

	// Nur die Gierachse uebernehmen: das Fahrzeug soll der Strasse folgen,
	// aber nicht deren Laengsneigung erben - es steht sonst schief in der
	// Luft, weil die Physik die Lage ohnehin je Tick neu bestimmt.
	OutRotation.Pitch = 0.0;
	OutRotation.Roll = 0.0;

	UE_LOG(LogWbVehicles, Log,
		TEXT("Spielerstart auf Spur %d, %.1f m vom Zielpunkt, Ausrichtung %.1f Grad."),
		BestLane, FMath::Sqrt(BestDistSq) / 100.0, OutRotation.Yaw);

	return true;
}
