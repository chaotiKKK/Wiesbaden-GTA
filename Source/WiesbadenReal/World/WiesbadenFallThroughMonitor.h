// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UWorld;
class AActor;

/**
 * Bodenabfrage-Naht des Durchfall-Waechters.
 *
 * Der Monitor fragt "liegt hier Boden?" NUR hierueber - im Spiel ueber einen
 * echten Abwaerts-Trace (FWbWorldGroundProbe), im Test ueber eine gescriptete
 * Attrappe. Zwei Adapter machen die Naht echt und die Verdikte ohne Welt pruefbar.
 */
struct IWbGroundProbe
{
	virtual ~IWbGroundProbe() = default;

	/**
	 * Liegt in [UpCm ueber .. DownCm unter] WorldPos geladener WorldStatic-Boden?
	 * OutDropCm = WorldPos.Z - BodenZ (kann negativ sein: Boden ueber der Position).
	 */
	virtual bool ProbeGround(const FVector& WorldPos, double UpCm, double DownCm, double& OutDropCm) const = 0;
};

/** Welt-Adapter: echter Abwaerts-Trace gegen geladene WorldStatic-Kollision. */
struct WIESBADENREAL_API FWbWorldGroundProbe : public IWbGroundProbe
{
	FWbWorldGroundProbe(const UWorld* InWorld, const AActor* InIgnore)
		: World(InWorld), Ignore(InIgnore) {}

	virtual bool ProbeGround(const FVector& WorldPos, double UpCm, double DownCm, double& OutDropCm) const override;

	const UWorld* World = nullptr;
	const AActor* Ignore = nullptr;
};

/** Ergebnis des Durchfall-Tests (Rohzahlen + Verdikt; die Prosa formatiert der Aufrufer). */
struct FWbFallReport
{
	enum class EMode : uint8 { Trace, Car };
	enum class EVerdict : uint8 { Passed, FellThrough, Invalid };

	EMode Mode = EMode::Trace;
	EVerdict Verdict = EVerdict::Invalid;
	FVector2D OriginXY = FVector2D::ZeroVector;
	double DistanceM = 0.0;
	int32 MovingTicks = 0;

	// -- Trace-Modus (Teleport-Pawn, Bodenluecken) --
	int32 VoidTicks = 0;
	int32 LeadVoidTicks = 0;
	double WorstGapM = 0.0;
	double WorstGapX = 0.0;

	// -- Wagen-Modus (echter Chaos-Wagen, Karosserie-Hoehensturz) --
	int32 AirborneTicks = 0;
	double MaxSturzM = 0.0;
	double MaxSturzX = 0.0;
	double PeakSinkMs = 0.0;
};

/**
 * Durchfall-Waechter: misst waehrend der -WbAutoDrive-Fahrt, ob unter dem Pawn
 * jederzeit geladener Boden liegt.
 *
 * Zwei Modi hinter einer Naht: TRACE (deterministisch +X teleportierter Pawn,
 * Bodenluecken + Leading-Edge per Abwaerts-Trace) und CAR (echter Chaos-Wagen,
 * realer Karosserie-Hoehensturz). Die Weltabfragen laufen ueber IWbGroundProbe,
 * damit "40-m-Luecke -> DURCHGEFALLEN" und der UNGUELTIG-Wachhund ohne den
 * 6-Minuten-Laufzeitlauf pruefbar sind (Test World.FallThroughMonitor).
 */
struct WIESBADENREAL_API FWbFallThroughMonitor
{
	/** Fahrt beginnen: Modus, Startort, Sollgeschwindigkeit (fuer die Leading-Edge). */
	void Begin(FWbFallReport::EMode InMode, const FVector& Origin, double DriveKmh);

	/** Ein Messschritt aus Pawn-Ort/-Geschwindigkeit und der Bodenabfrage. */
	void Observe(const FVector& PawnPos, const FVector& PawnVel, double DeltaSeconds, const IWbGroundProbe& Probe);

	/** Rohzahlen + Verdikt des bisherigen Laufs (offene Endluecke wird mitgezaehlt). */
	FWbFallReport Summary() const;

	bool IsCarMode() const { return Mode == FWbFallReport::EMode::Car; }

	// Fuer die Sekundentakt-Diagnose der HUD/des Subsystems.
	double CurrentDistanceM() const { return DistanceCm * 0.01; }
	double CurrentMaxSturzM() const { return MaxSturzCm * 0.01; }
	double CurrentPeakSinkMs() const { return PeakSinkCmS * 0.01; }

private:
	FWbFallReport::EMode Mode = FWbFallReport::EMode::Trace;
	FVector Origin = FVector::ZeroVector;
	double DriveKmh = 0.0;
	double DistanceCm = 0.0;

	// Trace-Modus.
	int32 MovingTicks = 0;
	int32 VoidTicks = 0;
	int32 LeadVoidTicks = 0;
	bool bInVoid = false;
	double VoidStartX = 0.0;
	double WorstGapLenCm = 0.0;
	double WorstGapX = 0.0;

	// Wagen-Modus.
	int32 CarDriveTicks = 0;
	int32 AirborneTicks = 0;
	bool bAirborne = false;
	double FallStartZ = 0.0;
	double MaxSturzCm = 0.0;
	double MaxSturzX = 0.0;
	double PeakSinkCmS = 0.0;
};
