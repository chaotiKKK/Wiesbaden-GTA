// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "Missions/WiesbadenMissionDeadline.h"

double FWiesbadenMissionDeadline::ComputeSeconds(
	const FVector& Start,
	const TArray<FMissionObjective>& Objectives,
	const FMissionDeadlineParams& Params)
{
	// Planare Routenlaenge: Start -> Ziel0 -> ... -> ZielN. Hoehe (Z) bleibt aussen
	// vor, damit Haenge/Etagen die Frist nicht verzerren (wie ReachLocation misst).
	auto Dist2D = [](const FVector& A, const FVector& B) -> double
	{
		const double Dx = A.X - B.X;
		const double Dy = A.Y - B.Y;
		return FMath::Sqrt(Dx * Dx + Dy * Dy);
	};

	double RouteCm = 0.0;
	double HoldSeconds = 0.0; // Verweilzeit (Dwell) ist reine ZEIT, nicht Strecke
	FVector Prev = Start;
	for (int32 i = 0; i < Objectives.Num(); ++i)
	{
		const FMissionObjective& O = Objectives[i];
		if (O.Type == EObjectiveType::LeaveArea)
		{
			// Fluchtpunkt: der Spieler muss RadiusCm vom Zentrum weg und dann zum
			// naechsten Ziel. Minimal (und fair) faehrt er in dessen Richtung aus dem
			// Gebiet - bei fernem Ziel ist der Ausstieg "gratis" (unterwegs), bei
			// nahem Ziel kostet der Umweg. Das Bein endet am Ausstiegspunkt.
			const FVector Center = O.Location;
			const FVector Dir = (i + 1 < Objectives.Num())
				? (Objectives[i + 1].Location - Center)
				: FVector::ZeroVector;
			const double DirLen = FMath::Sqrt(Dir.X * Dir.X + Dir.Y * Dir.Y);
			const FVector Exit = (DirLen > KINDA_SMALL_NUMBER)
				? FVector(Center.X + Dir.X / DirLen * O.RadiusCm,
					Center.Y + Dir.Y / DirLen * O.RadiusCm, 0.0)
				: FVector(Center.X + O.RadiusCm, Center.Y, 0.0); // letztes Ziel: Richtung egal
			RouteCm += Dist2D(Prev, Exit);
			Prev = Exit;
		}
		else
		{
			RouteCm += Dist2D(Prev, O.Location);
			Prev = O.Location;
		}

		// Verweil-Ziele kosten zusaetzlich ihre Haltezeit - reine Zeit, damit die
		// Frist eines Beobachtungsauftrags das Warten fair einbudgetiert.
		if (O.Type == EObjectiveType::Dwell)
		{
			HoldSeconds += O.HoldSeconds;
		}
	}

	const double Pace = FMath::Max(1.0, Params.PaceCmPerSecond); // Division absichern
	const double Window = RouteCm / Pace + HoldSeconds + Params.BufferSeconds;
	return FMath::Max(Window, Params.MinSeconds);
}
