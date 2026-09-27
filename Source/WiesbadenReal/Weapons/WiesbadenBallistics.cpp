// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "Weapons/WiesbadenBallistics.h"

namespace WiesbadenBallistics
{
	FWiesbadenFlightStep Step(const FWiesbadenProjectile& Current, double DeltaSeconds)
	{
		FWiesbadenFlightStep Out;
		const double Dt = FMath::Max(DeltaSeconds, 0.0);

		// Halbschritt-Integration (Symplectic Euler): erst Geschwindigkeit,
		// dann Position mit der NEUEN Geschwindigkeit - stabiler als
		// explizites Euler und deterministisch, ohne RK4-Aufwand.
		FWiesbadenProjectile Next = Current;
		Next.Velocity.Z -= Current.GravityCmPerS2 * Dt;

		const FVector Delta = Next.Velocity * Dt;
		const double MoveCm = Delta.Size();

		Out.SegmentStart = Current.Position;
		Out.SegmentEnd = Current.Position + Delta;

		// Reichweite: Reststaerke kuerzen; ist sie vor dem Schritt aufgebraucht,
		// endet das Projektil an der Reichweitenkante.
		const double Remaining = FMath::Max(static_cast<double>(Current.RemainingRangeCm) - MoveCm, 0.0);
		Next.RemainingRangeCm = static_cast<float>(Remaining);
		Out.bRangeEnd = Remaining <= KINDA_SMALL_NUMBER
			|| static_cast<double>(Current.RemainingRangeCm) - MoveCm <= KINDA_SMALL_NUMBER;

		if (Out.bRangeEnd && static_cast<double>(Current.RemainingRangeCm) < MoveCm)
		{
			// An der Reichweitenkante anhalten statt darueber hinaus.
			const double T = static_cast<double>(Current.RemainingRangeCm) / FMath::Max(MoveCm, KINDA_SMALL_NUMBER);
			Out.SegmentEnd = Current.Position + Delta * T;
			Next.Position = Out.SegmentEnd;
		}
		else
		{
			Next.Position = Out.SegmentEnd;
		}

		Out.Projectile = Next;
		return Out;
	}

	bool SegmentHitsSphere(const FVector& Start, const FVector& End,
		const FVector& Centre, double RadiusCm)
	{
		// Kuerzester Abstand des Punkts Centre zum Segment Start..End.
		const FVector D = End - Start;
		const double LenSq = static_cast<double>(D.SizeSquared());
		if (LenSq <= KINDA_SMALL_NUMBER)
		{
			return static_cast<double>(FVector::Dist(Start, Centre)) <= RadiusCm;
		}
		double T = static_cast<double>(FVector::DotProduct(Centre - Start, D)) / LenSq;
		T = FMath::Clamp(T, 0.0, 1.0);
		const FVector Closest = Start + D * T;
		return static_cast<double>(FVector::Dist(Closest, Centre)) <= RadiusCm;
	}

	double ImpactSpeed(const FWiesbadenProjectile& P)
	{
		return static_cast<double>(P.Velocity.Size());
	}

	float BlastDamageAt(const FVector& BlastCentre, float BlastRadiusCm,
		float BlastDamage, const FVector& TargetPoint)
	{
		if (BlastRadiusCm <= KINDA_SMALL_NUMBER)
		{
			return 0.0f;
		}
		const double Dist = static_cast<double>(FVector::Dist(BlastCentre, TargetPoint));
		if (Dist >= static_cast<double>(BlastRadiusCm))
		{
			return 0.0f;
		}
		const double T = 1.0 - Dist / static_cast<double>(BlastRadiusCm);
		return static_cast<float>(BlastDamage * T);
	}

	double DropAtDistance(double HorizontalDistanceCm, double VelocityCmPerS,
		double GravityCmPerS2)
	{
		if (VelocityCmPerS <= KINDA_SMALL_NUMBER)
		{
			return 0.0;
		}
		const double T = HorizontalDistanceCm / VelocityCmPerS;
		return 0.5 * GravityCmPerS2 * T * T;
	}
}
