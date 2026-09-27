// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Weapons/WiesbadenCutMath.h"

namespace WiesbadenCutMath
{
	FVector CutPlaneNormal(const FVector& AimDirection, float AngleDeg)
	{
		const FVector Aim = AimDirection.GetSafeNormal();
		if (Aim.IsNearlyZero())
		{
			return FVector::UpVector;
		}

		// Bezugsnormale: die Waagerechte senkrecht zum Blick (die Ebene
		// enthaelt bei 0 Grad die Blickachse). Fast senkrechter Blick kippt
		// den Bezug auf die Querachse, sonst degeneriert das Kreuzprodukt.
		FVector Up = FVector::UpVector;
		if (FMath::Abs(FVector::DotProduct(Up, Aim)) > 0.99f)
		{
			Up = FVector::RightVector;
		}
		const FVector BaseNormal = FVector::CrossProduct(Up, Aim).GetSafeNormal();

		// Rodrigues-Drehung um die Blickachse. Der dritte Term entfaellt,
		// weil BaseNormal senkrecht auf Aim steht.
		const float Rad = FMath::DegreesToRadians(AngleDeg);
		const FVector Rotated = BaseNormal * FMath::Cos(Rad)
			+ FVector::CrossProduct(Aim, BaseNormal) * FMath::Sin(Rad);
		return Rotated.GetSafeNormal();
	}

	float SignedDistanceToPlane(
		const FVector& Point, const FVector& PlanePoint, const FVector& PlaneNormal)
	{
		return FVector::DotProduct(Point - PlanePoint, PlaneNormal.GetSafeNormal());
	}

	bool ShouldDetach(
		const FVector& PieceCentroid, const FVector& PlanePoint, const FVector& PlaneNormal)
	{
		return SignedDistanceToPlane(PieceCentroid, PlanePoint, PlaneNormal) < 0.0f;
	}

	float RotateCutPlane(float AngleDeg, float StepDeg)
	{
		const float Wrapped = FMath::Fmod(AngleDeg + StepDeg, 360.0f);
		return Wrapped < 0.0f ? Wrapped + 360.0f : Wrapped;
	}
}
