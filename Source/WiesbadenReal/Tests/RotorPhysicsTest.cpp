// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Vehicles/WiesbadenRotorPhysics.h"

namespace
{
	constexpr float RotorDt = 0.01f;
	const FVector ZeroVelocity(0.0f, 0.0f, 0.0f);

	/** Spult den Rotor mit laufendem Triebwerk und halbem Collective hoch. */
	void SpoolUp(FWiesbadenRotorPhysics& Rotor, float Seconds)
	{
		FWiesbadenRotorPhysicsInput In;
		In.Collective = 0.5f;
		In.bEngineRunning = true;

		FWiesbadenRotorPhysicsOutput Out;
		for (float T = 0.0f; T < Seconds; T += RotorDt)
		{
			Rotor.Tick(In, RotorDt, ZeroVelocity, ZeroVelocity, Out);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRotorPhysicsHoverTest,
	"WiesbadenReal.Vehicles.RotorPhysics.Hover",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRotorPhysicsHoverTest::RunTest(const FString& Parameters)
{
	FWiesbadenRotorPhysics Rotor;
	SpoolUp(Rotor, 8.0f);

	// Ein Tick bei Schwebeflug-Eingaben liefert Auftrieb ~ Gewichtskraft.
	FWiesbadenRotorPhysicsInput In;
	In.Collective = 0.5f;
	In.bEngineRunning = true;

	FWiesbadenRotorPhysicsOutput Out;
	Rotor.Tick(In, RotorDt, ZeroVelocity, ZeroVelocity, Out);

	const float Weight = Rotor.MassKg * Rotor.GravityMetersPerS2;
	// Drehzahlband AUS DEN EINSTELLUNGEN ableiten, nicht als Zahl wiederholen.
	//
	// Hier stand 350..430 - das Band der frueheren Solldrehzahl von 420 U/min.
	// Als die Maschine auf Ka-52-Werte umgestellt wurde (350 U/min), schlug der
	// Test fehl, obwohl der Regler sauber arbeitete: Er prueft seither den
	// ALTEN Auslegungspunkt.
	const float TargetRpm = Rotor.TargetMainRotorRpm;
	TestTrue(
		FString::Printf(TEXT("Rotor dreht nahe Soll-Drehzahl (%.0f von %.0f U/min)"),
			Rotor.MainRotorRpm, TargetRpm),
		Rotor.MainRotorRpm > TargetRpm * 0.85f && Rotor.MainRotorRpm < TargetRpm * 1.05f);
	TestTrue(TEXT("Auftrieb ~ Gewicht (Schwebeflug)"),
		FMath::IsNearlyEqual(Out.Force.Z, Weight, Weight * 0.05f));
	TestTrue(TEXT("Keine seitliche Kraft ohne Zyklik"), FMath::IsNearlyZero(Out.Force.X, 1.0f));
	TestTrue(TEXT("Kein Yaw-Moment bei neutralem Pedal"), FMath::IsNearlyZero(Out.Torque.Z, 1.0f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRotorPhysicsAutorotationTest,
	"WiesbadenReal.Vehicles.RotorPhysics.Autorotation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRotorPhysicsAutorotationTest::RunTest(const FString& Parameters)
{
	// Variante A: Triebwerk aus + Sinkflug -> der aufsteigende Luftstrom haelt
	// die Drehzahl (Autorotation).
	{
		FWiesbadenRotorPhysics Rotor;
		SpoolUp(Rotor, 5.0f);

		FWiesbadenRotorPhysicsInput In;
		In.Collective = 0.2f;
		In.bEngineRunning = false;

		const FVector Descent(0.0f, 0.0f, -800.0f); // -8 m/s
		FWiesbadenRotorPhysicsOutput Out;
		for (float T = 0.0f; T < 10.0f; T += RotorDt)
		{
			Rotor.Tick(In, RotorDt, Descent, ZeroVelocity, Out);
		}

		// Ebenfalls aus der Solldrehzahl abgeleitet: Autorotation haelt die
		// Drehzahl in einem breiten Band um den Auslegungspunkt.
		const float TargetRpm = Rotor.TargetMainRotorRpm;
		TestTrue(
			FString::Printf(TEXT("Autorotation haelt die Drehzahl (%.0f von %.0f U/min)"),
				Rotor.MainRotorRpm, TargetRpm),
			Rotor.MainRotorRpm > TargetRpm * 0.55f && Rotor.MainRotorRpm < TargetRpm * 1.15f);
	}

	// Variante B: Triebwerk aus ohne Sinkflug -> die Drehzahl klingt ab.
	{
		FWiesbadenRotorPhysics Rotor;
		SpoolUp(Rotor, 5.0f);

		FWiesbadenRotorPhysicsInput In;
		In.Collective = 0.2f;
		In.bEngineRunning = false;

		FWiesbadenRotorPhysicsOutput Out;
		for (float T = 0.0f; T < 10.0f; T += RotorDt)
		{
			Rotor.Tick(In, RotorDt, ZeroVelocity, ZeroVelocity, Out);
		}

		TestTrue(TEXT("Ohne Sinkflug klingt die Drehzahl ab"), Rotor.MainRotorRpm < 150.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRotorPhysicsControlAuthorityTest,
	"WiesbadenReal.Vehicles.RotorPhysics.ControlAuthority",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRotorPhysicsControlAuthorityTest::RunTest(const FString& Parameters)
{
	FWiesbadenRotorPhysics Spooled;
	SpoolUp(Spooled, 5.0f);

	FWiesbadenRotorPhysicsOutput Out;

	// Zyklik Pitch +1 -> Vorwaertskraft und Nase-runter-Moment (Pitch < 0).
	{
		FWiesbadenRotorPhysics Rotor = Spooled;
		FWiesbadenRotorPhysicsInput In;
		In.Collective = 0.5f;
		In.CyclicPitch = 1.0f;
		Rotor.Tick(In, RotorDt, ZeroVelocity, ZeroVelocity, Out);
		TestTrue(TEXT("Zyklik vor -> Vorwaertskraft"), Out.Force.X > 0.0f);
		TestTrue(TEXT("Zyklik vor -> Nase runter (Pitch < 0)"), Out.Torque.Y < 0.0f);
	}

	// Zyklik Roll +1 -> Seitwaertskraft rechts und Roll-rechts-Moment.
	{
		FWiesbadenRotorPhysics Rotor = Spooled;
		FWiesbadenRotorPhysicsInput In;
		In.Collective = 0.5f;
		In.CyclicRoll = 1.0f;
		Rotor.Tick(In, RotorDt, ZeroVelocity, ZeroVelocity, Out);
		TestTrue(TEXT("Zyklik rechts -> Kraft rechts"), Out.Force.Y > 0.0f);
		TestTrue(TEXT("Zyklik rechts -> Roll rechts (Roll > 0)"), Out.Torque.X > 0.0f);
	}

	// Heckrotor-Pedal +1/-1 -> Yaw-Moment rechts/links.
	{
		FWiesbadenRotorPhysics Rotor = Spooled;
		FWiesbadenRotorPhysicsInput In;
		In.Collective = 0.5f;
		In.YawPedal = 1.0f;
		Rotor.Tick(In, RotorDt, ZeroVelocity, ZeroVelocity, Out);
		TestTrue(TEXT("Pedal rechts -> Yaw rechts (Z > 0)"), Out.Torque.Z > 0.0f);

		Rotor = Spooled;
		In.YawPedal = -1.0f;
		Rotor.Tick(In, RotorDt, ZeroVelocity, ZeroVelocity, Out);
		TestTrue(TEXT("Pedal links -> Yaw links (Z < 0)"), Out.Torque.Z < 0.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRotorPhysicsCoaxialTest,
	"WiesbadenReal.Vehicles.RotorPhysics.CoaxialRotors",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRotorPhysicsCoaxialTest::RunTest(const FString& Parameters)
{
	// Gleiche Parameter wie der Schwebeflug-Test, aber mit gegenlaeufigem
	// Doppelrotor: Auftrieb ~ doppeltes Gewicht, kein Reaktionsmoment, kein
	// Heckrotor-Seitenschub, Pedal erzeugt direktes Yaw-Moment.
	FWiesbadenRotorPhysics Rotor;
	Rotor.bCoaxialRotors = true;
	SpoolUp(Rotor, 8.0f);

	FWiesbadenRotorPhysicsInput In;
	In.Collective = 0.5f;
	In.bEngineRunning = true;

	FWiesbadenRotorPhysicsOutput Out;
	Rotor.Tick(In, RotorDt, ZeroVelocity, ZeroVelocity, Out);

	const float Weight = Rotor.MassKg * Rotor.GravityMetersPerS2;

	// Auch der Koaxialrotor HAELT bei neutralem Hebel die Hoehe.
	//
	// Hier stand zuvor "Auftrieb ~ doppeltes Gewicht" - der Test hielt damit
	// genau den Fehler als Sollverhalten fest, ueber den der Helikopter im
	// Spiel ohne Zutun davonstieg: Die Kollektiv-Kennlinie war fuer EINEN Rotor
	// abgestimmt, das gegenlaeufige Paar verdoppelt den Auftrieb.
	//
	// Die Verdopplung zeigt sich jetzt dort, wo sie hingehoert: im
	// Anstellwinkel. Ein Koaxialrotor braucht zum Schweben nur etwa den halben
	// Winkel eines Einzelrotors.
	TestTrue(
		FString::Printf(TEXT("Koaxial haelt die Hoehe (%.0f N gegen %.0f N)"), Out.Force.Z, Weight),
		FMath::IsNearlyEqual(Out.Force.Z, Weight, Weight * 0.1f));

	{
		FWiesbadenRotorPhysics Single;
		Single.bCoaxialRotors = false;
		Single.MainRotorRpm = Rotor.MainRotorRpm;

		const float CoaxialPitch = Rotor.ComputeHoverPitchDeg();
		const float SinglePitch = Single.ComputeHoverPitchDeg();

		TestTrue(
			FString::Printf(TEXT("Koaxial braucht rund den halben Anstellwinkel (%.2f gegen %.2f Grad)"),
				CoaxialPitch, SinglePitch),
			FMath::IsNearlyEqual(CoaxialPitch * 2.0f, SinglePitch, SinglePitch * 0.05f));
	}
	TestTrue(TEXT("Koaxial: kein Reaktionsmoment (neutrales Pedal)"),
		FMath::IsNearlyZero(Out.Torque.Z, 1.0f));
	TestTrue(TEXT("Koaxial: kein Heckrotor-Seitenschub"), FMath::IsNearlyZero(Out.Force.Y, 1.0f));

	// Pedal erzeugt direktes Yaw-Moment (differentielle Blattverstellung).
	In.YawPedal = 1.0f;
	Rotor.Tick(In, RotorDt, ZeroVelocity, ZeroVelocity, Out);
	TestTrue(TEXT("Koaxial: Pedal rechts -> Yaw rechts (Z > 0)"), Out.Torque.Z > 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRotorPhysicsBladeStallTest,
	"WiesbadenReal.Vehicles.RotorPhysics.RetreatingBladeStall",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRotorPhysicsBladeStallTest::RunTest(const FString& Parameters)
{
	// Blattspitzenverlust: Im Vorwaertsflug nahe der Hoechstgeschwindigkeit
	// bricht der Auftrieb der ruecklaufenden Blaetter ein (LiftScale sinkt).
	FWiesbadenRotorPhysics Rotor;
	SpoolUp(Rotor, 8.0f);

	FWiesbadenRotorPhysicsInput In;
	In.Collective = 0.5f;
	In.bEngineRunning = true;

	FWiesbadenRotorPhysicsOutput Hover;
	Rotor.Tick(In, RotorDt, ZeroVelocity, ZeroVelocity, Hover);

	// Pruefgeschwindigkeiten AUS DEN EINSTELLUNGEN ableiten.
	//
	// Hier standen 60 und 75 m/s als feste Zahlen, gueltig fuer die frueheren
	// vmax = 75 m/s. Mit der Ka-52-Hoechstgeschwindigkeit von 83 m/s lag die
	// erste Probe unterhalb der Stall-Schwelle, und der Test schlug fehl,
	// obwohl das Modell korrekt rechnete.
	const float MaxSpeed = Rotor.MaxForwardSpeedMetersPerS;
	const float StallStart = MaxSpeed * Rotor.RetreatingBladeStallStartFrac;

	// Deutlich im Einbruch, aber noch nicht am Limit: auf halbem Weg dorthin.
	const float ProbeSpeed = StallStart + (MaxSpeed - StallStart) * 0.5f;

	const FVector FastForward(ProbeSpeed * 100.0f, 0.0f, 0.0f);
	FWiesbadenRotorPhysicsOutput Forward;
	Rotor.Tick(In, RotorDt, FastForward, ZeroVelocity, Forward);
	TestTrue(
		FString::Printf(TEXT("Blattspitzenverlust senkt den Auftrieb (bei %.0f m/s)"), ProbeSpeed),
		Forward.Force.Z < Hover.Force.Z * 0.95f);

	// Am Limit bleibt nur ~45 % des Schwebeflug-Auftriebs.
	const FVector AtLimit(MaxSpeed * 100.0f, 0.0f, 0.0f);
	FWiesbadenRotorPhysicsOutput Limited;
	Rotor.Tick(In, RotorDt, AtLimit, ZeroVelocity, Limited);
	TestTrue(TEXT("Am Limit bleibt ~45 Prozent Auftrieb"),
		FMath::IsNearlyEqual(Limited.Force.Z, Hover.Force.Z * 0.45f, Hover.Force.Z * 0.05f));

	// Ohne Vorwaertsgeschwindigkeit bleibt der Auftrieb unveraendert.
	TestTrue(TEXT("Schwebe ohne Geschwindigkeitsverlust"),
		FMath::IsNearlyEqual(Hover.Force.Z, Hover.Force.Z, 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRotorNeutralCollectiveTest,
	"WiesbadenReal.Vehicles.Rotor.NeutralCollectiveHolds",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Ohne Eingabe muss der Helikopter die Hoehe HALTEN.
 *
 * Die Kollektiv-Kennlinie interpolierte gerade von Min nach Max. Die Ruhelage
 * ohne Eingabe liegt bei 0,5 und ergab damit 8 Grad Anstellwinkel, waehrend zum
 * Schweben rund 3,2 Grad genuegen - der Rotor erzeugte das Zweieinhalbfache des
 * Gewichts und der Helikopter stieg von selbst davon.
 *
 * Geprueft wird die ganze Kennlinie: Mitte haelt, oben steigt, unten sinkt.
 * Ohne die beiden Randproben waere auch ein Rotor "richtig", der ueberhaupt
 * keinen Schub mehr regelt.
 */
bool FRotorNeutralCollectiveTest::RunTest(const FString& Parameters)
{
	FWiesbadenRotorPhysics Rotor;
	Rotor.bCoaxialRotors = true;

	// Auf Solldrehzahl bringen.
	FWiesbadenRotorPhysicsInput In;
	In.bEngineRunning = true;
	In.Collective = 0.5f;

	FWiesbadenRotorPhysicsOutput Out;
	for (int32 i = 0; i < 600; ++i)
	{
		Rotor.Tick(In, 0.05f, FVector::ZeroVector, FVector::ZeroVector, Out);
	}

	const float Weight = Rotor.MassKg * Rotor.GravityMetersPerS2;

	TestTrue(TEXT("Rotor auf Solldrehzahl"),
		Rotor.MainRotorRpm > Rotor.TargetMainRotorRpm * 0.85f);

	// Mittelstellung: Auftrieb traegt das Gewicht (10 Prozent Toleranz).
	TestTrue(
		FString::Printf(TEXT("Neutral haelt die Hoehe (Auftrieb %.0f N, Gewicht %.0f N)"),
			Out.Force.Z, Weight),
		FMath::Abs(Out.Force.Z - Weight) < Weight * 0.10f);

	// Voll oben: deutlich mehr als das Gewicht.
	In.Collective = 1.0f;
	Rotor.Tick(In, 0.05f, FVector::ZeroVector, FVector::ZeroVector, Out);
	TestTrue(
		FString::Printf(TEXT("Voll oben steigt (%.0f N gegen %.0f N)"), Out.Force.Z, Weight),
		Out.Force.Z > Weight * 1.3f);

	// Voll unten: deutlich weniger als das Gewicht.
	In.Collective = 0.0f;
	Rotor.Tick(In, 0.05f, FVector::ZeroVector, FVector::ZeroVector, Out);
	TestTrue(
		FString::Printf(TEXT("Voll unten sinkt (%.0f N gegen %.0f N)"), Out.Force.Z, Weight),
		Out.Force.Z < Weight * 0.8f);

	return true;
}
