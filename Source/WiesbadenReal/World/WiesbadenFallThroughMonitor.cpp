// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenFallThroughMonitor.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
	// Trace-Modus: 500 m ueber bis 2 km unter der Fahrhoehe - ein Totalausfall
	// ueber die ganze Spalte heisst "Zelle nicht gestreamt".
	constexpr double TraceUpCm = 50000.0;
	constexpr double TraceDownCm = 200000.0;

	// Wagen-Modus: knapp ueber der Karosserie bis 300 m darunter.
	constexpr double CarUpCm = 200.0;
	constexpr double CarDownCm = 30000.0;

	// Boden < 5 m unter der Karosserie = auf/nahe der Strasse; sonst faellt der Wagen.
	constexpr double CarGroundNearCm = 500.0;

	// Echter Durchfall = Karosserie sinkt merklich unter die Strasse (> 1 m).
	constexpr double CarFellThroughCm = 100.0;

	// Ohne echte Strecke (> 50 m) ist ein "Bestanden" wertlos -> UNGUELTIG.
	constexpr double MinValidDistanceCm = 5000.0;
}

bool FWbWorldGroundProbe::ProbeGround(const FVector& WorldPos, double UpCm, double DownCm, double& OutDropCm) const
{
	OutDropCm = 0.0;
	if (!World)
	{
		return false;
	}
	FCollisionQueryParams Params(FName(TEXT("WbGroundProbe")), /*bTraceComplex=*/false);
	if (Ignore)
	{
		Params.AddIgnoredActor(Ignore);
	}
	FHitResult Hit;
	const FVector Start = WorldPos + FVector(0.0, 0.0, UpCm);
	const FVector End = WorldPos - FVector(0.0, 0.0, DownCm);
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
	{
		OutDropCm = WorldPos.Z - Hit.ImpactPoint.Z;
		return true;
	}
	return false;
}

void FWbFallThroughMonitor::Begin(FWbFallReport::EMode InMode, const FVector& InOrigin, double InDriveKmh)
{
	Mode = InMode;
	Origin = InOrigin;
	DriveKmh = InDriveKmh;
}

void FWbFallThroughMonitor::Observe(const FVector& PawnPos, const FVector& PawnVel, double DeltaSeconds, const IWbGroundProbe& Probe)
{
	if (Mode == FWbFallReport::EMode::Car)
	{
		++CarDriveTicks;
		DistanceCm = FVector::Dist2D(PawnPos, Origin);
		PeakSinkCmS = FMath::Max(PeakSinkCmS, -PawnVel.Z);

		double Drop = 0.0;
		const bool bHit = Probe.ProbeGround(PawnPos, CarUpCm, CarDownCm, Drop);
		const bool bGroundNear = bHit && Drop < CarGroundNearCm;
		if (!bGroundNear)
		{
			++AirborneTicks;
			if (!bAirborne)
			{
				bAirborne = true;
				FallStartZ = PawnPos.Z;
			}
			const double Sturz = FallStartZ - PawnPos.Z;
			if (Sturz > MaxSturzCm)
			{
				MaxSturzCm = Sturz;
				MaxSturzX = PawnPos.X;
			}
		}
		else
		{
			bAirborne = false;
		}
		return;
	}

	// Trace-Modus: deterministisch +X gefahren; Strecke = X ab Start.
	++MovingTicks;
	DistanceCm = PawnPos.X - Origin.X;

	double Drop = 0.0;
	const bool bGround = Probe.ProbeGround(PawnPos, TraceUpCm, TraceDownCm, Drop);
	if (!bGround)
	{
		++VoidTicks;
		if (!bInVoid)
		{
			bInVoid = true;
			VoidStartX = PawnPos.X;
		}
	}
	else if (bInVoid)
	{
		bInVoid = false;
		const double LenCm = PawnPos.X - VoidStartX;
		if (LenCm > WorstGapLenCm)
		{
			WorstGapLenCm = LenCm;
			WorstGapX = VoidStartX;
		}
	}

	// Leading-Edge: eine Sekunde Fahrweg voraus (die Quelle laedt nicht vor).
	const FVector Lead(PawnPos.X + DriveKmh / 3.6 * 100.0, PawnPos.Y, PawnPos.Z);
	double LeadDrop = 0.0;
	if (!Probe.ProbeGround(Lead, TraceUpCm, TraceDownCm, LeadDrop))
	{
		++LeadVoidTicks;
	}
}

FWbFallReport FWbFallThroughMonitor::Summary() const
{
	FWbFallReport R;
	R.Mode = Mode;
	R.OriginXY = FVector2D(Origin.X, Origin.Y);
	R.DistanceM = DistanceCm * 0.01;

	if (Mode == FWbFallReport::EMode::Car)
	{
		R.MovingTicks = CarDriveTicks;
		R.AirborneTicks = AirborneTicks;
		R.MaxSturzM = MaxSturzCm * 0.01;
		R.MaxSturzX = MaxSturzX;
		R.PeakSinkMs = PeakSinkCmS * 0.01;

		const bool bMoved = DistanceCm > MinValidDistanceCm;
		R.Verdict = !bMoved
			? FWbFallReport::EVerdict::Invalid
			: (MaxSturzCm > CarFellThroughCm ? FWbFallReport::EVerdict::FellThrough : FWbFallReport::EVerdict::Passed);
		return R;
	}

	R.MovingTicks = MovingTicks;

	// Eine am Streckenende noch offene Luecke mitzaehlen.
	double WorstLenCm = WorstGapLenCm;
	double WorstX = WorstGapX;
	if (bInVoid)
	{
		const double OpenLenCm = (Origin.X + DistanceCm) - VoidStartX;
		if (OpenLenCm > WorstLenCm)
		{
			WorstLenCm = OpenLenCm;
			WorstX = VoidStartX;
		}
	}
	R.VoidTicks = VoidTicks;
	R.LeadVoidTicks = LeadVoidTicks;
	R.WorstGapM = WorstLenCm * 0.01;
	R.WorstGapX = WorstX;
	R.Verdict = (MovingTicks <= 0)
		? FWbFallReport::EVerdict::Invalid
		: (VoidTicks > 0 ? FWbFallReport::EVerdict::FellThrough : FWbFallReport::EVerdict::Passed);
	return R;
}
