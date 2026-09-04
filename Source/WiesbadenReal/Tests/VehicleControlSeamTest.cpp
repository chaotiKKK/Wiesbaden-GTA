// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Engine/World.h"

#include "Vehicles/WiesbadenCar.h"
#include "Vehicles/WiesbadenChaosCar.h"
#include "Vehicles/WiesbadenVehicleControl.h"

/**
 * Zwei-Fahrzeug-Verdrahtungsnachweis der Steuernaht (Spec-Schritt 6).
 *
 * Steuert BEIDE Fahrzeuge - den kinematischen AWiesbadenCar und den Chaos-
 * physikalischen AWiesbadenChaosCar - ausschliesslich ueber IWiesbadenVehicle-
 * Control an und prueft, dass die Naht Steuerung UND Readouts korrekt durch-
 * reicht: aktivieren/abschalten (Lebenszyklus) sowie die HUD-/Diagnose-Readouts
 * (Tempo, Gang, Drehzahl + Skala, Licht, Kamera).
 *
 * Bewusst OHNE echte Fahrt: die tatsaechliche Laengsdynamik (Tempo>20,
 * Kursaenderung>15) weist der Rauchtest ueber WbDrive an der echten Fahrphysik
 * nach - fuer den Kaefer heute, fuer den ChaosCar nach dem Bauch-Kollisions-Fix.
 * Dieser Unit-Test sichert die NAHT selbst ab (kein Editor, kein Physik-Schritt):
 * dass beide Klassen das Interface erfuellen und ihre Werte durchreichen.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleControlSeamTest,
	"WiesbadenReal.Vehicles.ControlSeam",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVehicleControlSeamTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld*/ false);
	if (!TestNotNull(TEXT("Test-Welt erstellt"), World))
	{
		return false;
	}

	// Prueft den kompletten Naht-Vertrag an EINEM Fahrzeug ueber das Interface -
	// identisch fuer Kaefer und ChaosCar, denn genau das ist der Sinn der Naht.
	auto CheckSeam = [this](IWiesbadenVehicleControl* Ctrl, const TCHAR* Name)
	{
		if (!TestNotNull(*FString::Printf(TEXT("%s: implementiert IWiesbadenVehicleControl"), Name), Ctrl))
		{
			return;
		}

		// -- Steuerungs-Lebenszyklus: aktivieren / abschalten -----------------
		TestFalse(*FString::Printf(TEXT("%s: anfangs keine externe Steuerung"), Name),
			Ctrl->IsExternalControlActive());

		FWiesbadenCarControl Cmd;
		Cmd.Throttle = 1.0f;
		Cmd.Steering = 0.5f;
		Cmd.bHandbrake = false;
		Ctrl->SetExternalControl(Cmd);
		TestTrue(*FString::Printf(TEXT("%s: SetExternalControl aktiviert die Naht"), Name),
			Ctrl->IsExternalControlActive());

		// -- Readouts durchgereicht (plausibel, kein Absturz) -----------------
		const float Idle = Ctrl->GetEngineIdleRpm();
		const float MaxRpm = Ctrl->GetEngineMaxRpm();
		TestTrue(*FString::Printf(TEXT("%s: Leerlaufdrehzahl > 0 (Skala verdrahtet)"), Name), Idle > 0.0f);
		TestTrue(*FString::Printf(TEXT("%s: Hoechstdrehzahl > Leerlauf"), Name), MaxRpm > Idle);
		TestTrue(*FString::Printf(TEXT("%s: Motordrehzahl endlich und >= 0"), Name),
			FMath::IsFinite(Ctrl->GetEngineRpm()) && Ctrl->GetEngineRpm() >= 0.0f);
		TestTrue(*FString::Printf(TEXT("%s: Tempo endlich"), Name),
			FMath::IsFinite(Ctrl->GetSpeedKmh()));
		TestNotNull(*FString::Printf(TEXT("%s: Lichtanlage fuer Kontrollleuchten vorhanden"), Name),
			Ctrl->GetLights());

		const int32 Gear = Ctrl->GetGear();
		TestTrue(*FString::Printf(TEXT("%s: Gang im plausiblen Bereich (-1..10)"), Name),
			Gear >= -1 && Gear <= 10);

		// GetCameraMode nur aufrufen - der Wert ist der Follow-Default, es geht um
		// die Durchreichung (kein Absturz, gueltiger Enum).
		const EWiesbadenVehicleCameraMode Cam = Ctrl->GetCameraMode();
		TestTrue(*FString::Printf(TEXT("%s: Kameramodus ist ein gueltiger Wert"), Name),
			Cam == EWiesbadenVehicleCameraMode::Follow
			|| Cam == EWiesbadenVehicleCameraMode::Orbit
			|| Cam == EWiesbadenVehicleCameraMode::Cockpit);

		// -- Abschalten gibt die Steuerung frei -------------------------------
		Ctrl->ClearExternalControl();
		TestFalse(*FString::Printf(TEXT("%s: ClearExternalControl gibt die Steuerung frei"), Name),
			Ctrl->IsExternalControlActive());
	};

	// -- Kaefer (kinematisch) ------------------------------------------------
	AWiesbadenCar* Car = World->SpawnActor<AWiesbadenCar>(
		FVector(0.0, 0.0, 100.0), FRotator::ZeroRotator);
	TestNotNull(TEXT("Kaefer gespawnt"), Car);
	CheckSeam(Cast<IWiesbadenVehicleControl>(Car), TEXT("Kaefer"));
	// Familien-Wurzel: derselbe Pawn ist auch IWiesbadenExternalControl (EIN
	// Zugriffspfad fuer Autopilot/Harness ueber alle Fahrzeuge).
	TestNotNull(TEXT("Kaefer castet auf die Familien-Wurzel IWiesbadenExternalControl"),
		Cast<IWiesbadenExternalControl>(Car));

	// -- ChaosCar (echte Fahrzeugphysik) - reiner Verdrahtungsnachweis -------
	AWiesbadenChaosCar* Chaos = World->SpawnActor<AWiesbadenChaosCar>(
		FVector(5000.0, 0.0, 100.0), FRotator::ZeroRotator);
	TestNotNull(TEXT("ChaosCar gespawnt"), Chaos);
	CheckSeam(Cast<IWiesbadenVehicleControl>(Chaos), TEXT("ChaosCar"));
	TestNotNull(TEXT("ChaosCar castet auf die Familien-Wurzel IWiesbadenExternalControl"),
		Cast<IWiesbadenExternalControl>(Chaos));

	World->DestroyWorld(false);
	return true;
}
