// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Core/WiesbadenInputMap.h"
#include "UI/WiesbadenVehicleHUD.h"
#include "Vehicles/WiesbadenHelicopter.h"
#include "Vehicles/WiesbadenHeliGunComponent.h"
#include "Vehicles/WiesbadenVehicleCameraComponent.h"
#include "World/WiesbadenSebboHq.h"

#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "InputKeyEventArgs.h"
#include "Misc/ScopeExit.h"

namespace
{
	constexpr int32 Abschluss = WiesbadenHelicopterLesson::PracticeStepCount;
	constexpr int32 Umsehschritt = 5;
}

/** Reine Lektions-/Anzeigeregeln. Controller- und Geschuetzpfade folgen unten. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHelicopterLessonSafetyTest,
	"WiesbadenReal.Vehicles.LessonSafety",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FHelicopterLessonSafetyTest::RunTest(const FString& Parameters)
{
	// -- 1) Einladung und laufende Lektion werden beide abgebaut -----------
	{
		TestTrue(TEXT("laufende Lektion wird beim Pawn-Wechsel beendet"),
			AWiesbadenVehicleHUD::ShouldLessonBreakOnPawnChange(/*Active*/ true, /*Offer*/ false));
		TestTrue(TEXT("offene Einladung wird beim Pawn-Wechsel beendet"),
			AWiesbadenVehicleHUD::ShouldLessonBreakOnPawnChange(/*Active*/ false, /*Offer*/ true));
		TestFalse(TEXT("ohne Lektion und ohne Einladung passiert nichts"),
			AWiesbadenVehicleHUD::ShouldLessonBreakOnPawnChange(false, false));
	}

	// -- 2) Abschlussanzeige: Enter / A, kein Skip-Versprechen --------------
	{
		TestEqual(TEXT("Abschlussschritt nennt Enter / A"),
			AWiesbadenVehicleHUD::GetHelicopterLessonConfirmKeys(Abschluss), FString(TEXT("Enter / A")));
		TestTrue(TEXT("Uebungsschritt bestaetigt nicht per Enter/A"),
			AWiesbadenVehicleHUD::GetHelicopterLessonConfirmKeys(Umsehschritt).IsEmpty());
		TestTrue(TEXT("Uebungen nennen Tab / B"),
			AWiesbadenVehicleHUD::GetHelicopterLessonSkipHint(0).Contains(TEXT("Tab")));
		TestTrue(TEXT("Abschluss nennt kein Skip"),
			AWiesbadenVehicleHUD::GetHelicopterLessonSkipHint(Abschluss).IsEmpty());
		// Der Abschlussschitt traegt weiterhin den Ausstiegs-Hinweis fuer F/Y.
		TestTrue(TEXT("Abschluss-Anleitung nennt F / Y"),
			AWiesbadenVehicleHUD::GetHelicopterLessonInstructions(Abschluss).Contains(TEXT("F / Y")));
		// Und die Bestaetigung wird tatsaechlich angenommen, nicht uebersprungen.
		TestEqual(TEXT("Abschluss braucht Bestaetigung"),
			AWiesbadenVehicleHUD::AdvanceHelicopterLessonStep(Abschluss, false, true), Abschluss);
		TestEqual(TEXT("Bestaetigung schliesst ab"),
			AWiesbadenVehicleHUD::AdvanceHelicopterLessonStep(Abschluss, true, false),
			WiesbadenHelicopterLesson::StepCount);
	}

	// -- 3) Umsehschritt braucht eine Aussenansicht ------------------------
	{
		TestTrue(TEXT("Umsehen in Follow erfuellt den Schritt"),
			AWiesbadenVehicleHUD::IsLessonLookStepSatisfied(
				1.0f, EWiesbadenVehicleCameraMode::Follow));
		TestTrue(TEXT("Umsehen in Orbit erfuellt den Schritt"),
			AWiesbadenVehicleHUD::IsLessonLookStepSatisfied(
				1.0f, EWiesbadenVehicleCameraMode::Orbit));
		TestFalse(TEXT("Cockpit ohne Umsehen erfuellt den Schritt nicht"),
			AWiesbadenVehicleHUD::IsLessonLookStepSatisfied(
				1.0f, EWiesbadenVehicleCameraMode::Cockpit));
		TestFalse(TEXT("Aussenansicht ohne Eingabe erfuellt den Schritt nicht"),
			AWiesbadenVehicleHUD::IsLessonLookStepSatisfied(
				0.0f, EWiesbadenVehicleCameraMode::Follow));
		TestTrue(TEXT("Anleitung nennt den Weg aus dem Cockpit"),
			AWiesbadenVehicleHUD::GetHelicopterLessonInstructions(Umsehschritt).Contains(TEXT("Cockpit")));
	}

	// -- 4) Feuerfreiheit: zwei Sperren, beide muessen greifen -------------
	{
		TestTrue(TEXT("ohne Sperren ist scharfes Feuern erlaubt"),
			AWiesbadenHelicopter::AllowsLiveFire(false, false));
		TestFalse(TEXT("Trockenmodus sperrt"),
			AWiesbadenHelicopter::AllowsLiveFire(true, false));
		TestFalse(TEXT("unterbrochener Schuss sperrt"),
			AWiesbadenHelicopter::AllowsLiveFire(false, true));
		TestFalse(TEXT("Trockenmodus mit unterbrochenem Schuss sperrt"),
			AWiesbadenHelicopter::AllowsLiveFire(true, true));
	}

	// -- 5) Geschuetzanzeige: reine Textprioritaet, keine Bildabnahme -------
	{
		TestEqual(TEXT("Trockenmodus wird angezeigt"),
			AWiesbadenVehicleHUD::GetHelicopterGunStatus(true, false, false, 300),
			FString(TEXT("TROCKEN")));
		TestEqual(TEXT("wartendes Loslassen wird angezeigt"),
			AWiesbadenVehicleHUD::GetHelicopterGunStatus(false, true, false, 300),
			FString(TEXT("ABZUG LOS")));
		TestEqual(TEXT("Ueberhitzung wird angezeigt"),
			AWiesbadenVehicleHUD::GetHelicopterGunStatus(false, false, true, 300),
			FString(TEXT("UEBERHITZT")));
		TestEqual(TEXT("leere Kette wird angezeigt"),
			AWiesbadenVehicleHUD::GetHelicopterGunStatus(false, false, false, 0),
			FString(TEXT("LEER")));
		TestEqual(TEXT("bereite Kette nennt die Munition"),
			AWiesbadenVehicleHUD::GetHelicopterGunStatus(false, false, false, 137),
			FString(TEXT("BEREIT 137")));
		// Die sperrenden Zustaende kommen VOR der Munition: solange die Lektion
		// laeuft, ist die Zahl Nebensache, der Grund nicht.
		TestEqual(TEXT("Trockenmodus schlaegt Ueberhitzung und Munition"),
			AWiesbadenVehicleHUD::GetHelicopterGunStatus(true, true, true, 0),
			FString(TEXT("TROCKEN")));
	}

	// -- 6) Verlorener Hubschrauber beendet die Lektion --------------------
	// GEMESSEN am 01.10.2026: nach dem Absturz blieb die Lektion stehen. Der
	// Pawnwechsel greift nicht - der Spieler sitzt im selben Actor weiter
	// (bDestroyed, nicht UnPossessed) -, also wartete die Lektion auf Schritte,
	// die nicht mehr erfuellbar sind, und die Feuersperre blieb bis zum
	// Aussteigen stehen.
	{
		TestTrue(TEXT("abgesturzter Heli beendet die laufende Lektion"),
			AWiesbadenVehicleHUD::ShouldLessonBreakOnHeliLoss(
				/*bLessonActive*/ true, /*bOfferVisible*/ false, /*bHeliUsable*/ false));
		TestTrue(TEXT("abgesturzter Heli schliesst auch die offene Einladung"),
			AWiesbadenVehicleHUD::ShouldLessonBreakOnHeliLoss(false, true, false));
		TestFalse(TEXT("fliegender Heli beendet nichts"),
			AWiesbadenVehicleHUD::ShouldLessonBreakOnHeliLoss(true, true, true));
		TestFalse(TEXT("ohne Lektion und ohne Heli passiert nichts"),
			AWiesbadenVehicleHUD::ShouldLessonBreakOnHeliLoss(false, false, false));
	}

	// -- 7) Legende nennt die Pad-Tasten der Heli-Belegung ---------------
	{
		TArray<FString> Zeilen;
		AWiesbadenVehicleHUD::GetControlLegendLines(/*bInVehicle=*/true, Zeilen);
		const FString Text = FString::Join(Zeilen, TEXT("\n"));
		TestTrue(TEXT("Legende nennt LB/RB fuer die Gier"), Text.Contains(TEXT("LB/RB")));
		TestTrue(TEXT("Legende nennt RT/LT fuer das Kollektiv"), Text.Contains(TEXT("RT/LT")));
	}

	return true;
}

/**
 * Dasselbe am echten Hubschrauber: Aussteigen ist auch ein Abbruch, und der
 * unterbrochene Schuss muss auf das Loslassen warten.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHelicopterLessonFireTest,
	"WiesbadenReal.Vehicles.LessonFire",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FHelicopterLessonFireTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld*/ false);
	if (!TestNotNull(TEXT("Test-Welt erstellt"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
	};

	AWiesbadenHelicopter* Heli = World->SpawnActor<AWiesbadenHelicopter>();
	if (!TestNotNull(TEXT("Helikopter gespawnt"), Heli))
	{
		return false;
	}
	UWiesbadenHeliGunComponent* Gun = Heli->GetGunComponent();
	if (!TestNotNull(TEXT("Bordgeschuetz vorhanden"), Gun))
	{
		return false;
	}

	const int32 SchussBasis = Gun->GetShotsFired();

	// Trockenmodus wie beim Angebot: der Abzug wird freigegeben, feuert aber
	// nicht, und zaehlt keinen Schuss.
	Heli->SetLessonDryFireActive(true);
	TestTrue(TEXT("Trockenmodus aktiv"), Heli->IsLessonDryFireActive());
	Gun->SetTriggerHeld(AWiesbadenHelicopter::AllowsLiveFire(
		Heli->IsLessonDryFireActive(), Heli->IsDryFireReleasePending()));
	TestEqual(TEXT("Trockenmodus schiesst nicht"), Gun->GetShotsFired(), SchussBasis);

	// Beim Beenden muss der Trockenmodus den Abzug loslassen.
	Heli->SetLessonDryFireActive(false);
	TestFalse(TEXT("Trockenmodus beendet"), Heli->IsLessonDryFireActive());
	TestTrue(TEXT("nach dem Ende muss der physische Abzug erst los"), Heli->IsDryFireReleasePending());
	TestFalse(TEXT("Abzug nach dem Ende losgelassen"), Gun->IsTriggerHeld());

	// Ein unterbrochener Schuss sperrt, bis der Abzug losgelassen wurde.
	TestFalse(TEXT("unterbrochener Schuss sperrt scharfes Feuern"),
		AWiesbadenHelicopter::AllowsLiveFire(false, /*bReleasePending*/ true));

	// Aussteigen (UnPossessed) raeumt den Feuerzustand restlos auf - sonst
	// schoesse der Heli beim naechsten Aufsteigen weiter.
	Heli->SetLessonDryFireActive(true);
	Gun->SetTriggerHeld(false);
	Heli->UnPossessed();
	TestFalse(TEXT("nach Aussteigen kein Trockenmodus"), Heli->IsLessonDryFireActive());
	TestTrue(TEXT("nach Aussteigen bleibt die Loslass-Sperre"), Heli->IsDryFireReleasePending());
	TestFalse(TEXT("nach Aussteigen Abzug los"), Gun->IsTriggerHeld());
	TestEqual(TEXT("nach Aussteigen kein Schuss"), Gun->GetShotsFired(), SchussBasis);
	TestFalse(TEXT("nach Aussteigen ist scharfes Feuern noch gesperrt"),
		AWiesbadenHelicopter::AllowsLiveFire(Heli->IsLessonDryFireActive(), Heli->IsDryFireReleasePending()));

	// -- Absturz und Wiederaufsetzen --------------------------------------
	// GEMESSEN am 01.10.2026: RespawnOnTowerHelipad setzte den Flugzustand
	// zurueck, den Feuerzustand aber nicht. Nach einem Absturz waehrend der
	// Lektion stand der Heli mit gedruecktem Abzug und wartendem Schuss
	// wieder auf dem Landeplatz.
	// Der Wiederaufsetzpfad rechnet den Landeplatz aus dem Sebbotower
	// (PlaceOnTowerHelipad) und gibt ohne ihn auf: im nackten Testfeld fehlte
	// der Turm, der Lauf meldete "Wiederaufsetzen gelingt" nicht. Getestet wird
	// deshalb gegen einen echten Turm, nicht gegen eine Sonderlage.
	AWiesbadenSebboHq* Turm = World->SpawnActor<AWiesbadenSebboHq>();
	TestNotNull(TEXT("Sebbotower als Landeplatzbezug gespawnt"), Turm);

	AWiesbadenHelicopter* Abgestuerzt = World->SpawnActor<AWiesbadenHelicopter>();
	if (TestNotNull(TEXT("zweiter Heli fuer den Absturz gespawnt"), Abgestuerzt))
	{
		Abgestuerzt->SetActorLocation(FVector(2000.0f, 0.0f, 300.0f));
		UWiesbadenHeliGunComponent* AbsturzGun = Abgestuerzt->GetGunComponent();
		TestNotNull(TEXT("Geschuetz des abgestuerzten Heli vorhanden"), AbsturzGun);
		if (AbsturzGun)
		{
			Abgestuerzt->SetLessonDryFireActive(true);
			AbsturzGun->SetTriggerHeld(true);
			// Vierter Parameter DamageCauser: die Basis-Signatur von UE 5.8
			// hat ihn (Actor.h:3660), ohne ihn findet der Aufruf nichts.
			// FDamageEvent hat in UE 5.8 nur einen Default-Konstruktor und
			// einen expliziten fuer die Schadensklasse; die Menge steckt in
			// DamageAmount, das TakeDamage des Helis auch auswertet.
			FDamageEvent Absturzschaden;
			Abgestuerzt->TakeDamage(100000.0f, Absturzschaden, nullptr, Abgestuerzt);
			TestTrue(TEXT("Heli gilt als abgestuerzt"), Abgestuerzt->IsDestroyed());
			// TakeDamage laesst den Abzug bereits los.
			TestFalse(TEXT("nach dem Absturz ist der Abzug los"), AbsturzGun->IsTriggerHeld());

			// Wiederaufsetzen: der Abzug muss frei und der Schusszaehler
			// zurueck, die Feuersperre der Lektion aber ZU bleiben - wer sie
			// loest, ist der HUD (ShouldLessonBreakOnHeliLoss, Abschnitt 6).
			AbsturzGun->SetTriggerHeld(true);
			TestTrue(TEXT("Wiederaufsetzen gelingt"), Abgestuerzt->RespawnOnTowerHelipad());
			TestFalse(TEXT("nach dem Wiederaufsetzen ist der Abzug los"),
				AbsturzGun->IsTriggerHeld());
			TestTrue(TEXT("nach dem Wiederaufsetzen muss der physische Abzug erst los"),
				Abgestuerzt->IsDryFireReleasePending());
			TestTrue(TEXT("nach dem Wiederaufsetzen haelt die Lektion die Feuersperre"),
				Abgestuerzt->IsLessonDryFireActive());
			TestFalse(TEXT("während der Lektion kein scharfes Feuern nach dem Absturz"),
				AWiesbadenHelicopter::AllowsLiveFire(
					Abgestuerzt->IsLessonDryFireActive(), Abgestuerzt->IsDryFireReleasePending()));
			// Und die Anzeige sagt genau das dem Spieler.
			TestEqual(TEXT("Tafel meldet nach dem Absturz den Trockenmodus"),
				AWiesbadenVehicleHUD::GetHelicopterGunStatus(
					Abgestuerzt->IsLessonDryFireActive(), Abgestuerzt->IsDryFireReleasePending(),
					AbsturzGun->IsOverheated(), AbsturzGun->GetRemainingRounds()),
				FString(TEXT("TROCKEN")));
		}
	}

	// -- EndPlay raeumt den Rest -----------------------------------------
	// GEMESSEN am 01.10.2026: weder der Helikopter noch der HUD hatten ein
	// EndPlay. Ein zustaendiges Geschuetz, das den Actor mitnimmt, ist
	// harmlos - ein zustaendiger Lektionsfehler nicht.
	AWiesbadenHelicopter* EndHeli = World->SpawnActor<AWiesbadenHelicopter>();
	if (TestNotNull(TEXT("Heli fuer den EndPlay-Nachweis gespawnt"), EndHeli))
	{
		EndHeli->SetLessonDryFireActive(true);
		EndHeli->EndPlay(EEndPlayReason::Destroyed);
		TestFalse(TEXT("EndPlay loest den Trockenmodus"), EndHeli->IsLessonDryFireActive());
		TestFalse(TEXT("EndPlay raeumt den unterbrochenen Schuss"),
			EndHeli->IsDryFireReleasePending());
		TestTrue(TEXT("EndPlay raeumt beide lokalen Sperren"),
			AWiesbadenHelicopter::AllowsLiveFire(
				EndHeli->IsLessonDryFireActive(), EndHeli->IsDryFireReleasePending()));
	}

	return true;
}

// Echte Controller-Eingabe und Geschuetzticks statt einer Wahrheitstabelle.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHelicopterHeldFireTest,
	"WiesbadenReal.Vehicles.LessonHeldFire",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FHelicopterHeldFireTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Test-Welt"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AWiesbadenHelicopter* Heli = World->SpawnActor<AWiesbadenHelicopter>();
	APlayerController* PC = World->SpawnActor<APlayerController>();
	AWiesbadenVehicleHUD* HUD = World->SpawnActor<AWiesbadenVehicleHUD>();
	if (!TestNotNull(TEXT("Heli"), Heli) || !TestNotNull(TEXT("Controller"), PC)
		|| !TestNotNull(TEXT("HUD"), HUD)) { return false; }
	PC->InitInputSystem();
	PC->Possess(Heli);
	HUD->SetOwner(PC);
	HUD->PlayerOwner = PC;
	UWiesbadenHeliGunComponent* Gun = Heli->GetGunComponent();
	Gun->Reload();

	auto Eingabe = [&](const FKey& Taste, bool bHalten)
	{
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(Taste,
			bHalten ? IE_Pressed : IE_Released, bHalten ? 1.0f : 0.0f));
		PC->PlayerInput->ProcessInputStack(TArray<UInputComponent*>(), 1.0f / 30.0f, false);
	};
	auto Ticken = [&]()
	{
		Heli->Tick(1.0f / 30.0f);
		Gun->TickComponent(1.0f / 30.0f, LEVELTICK_All, nullptr);
	};
	for (const FKey& Taste : { EKeys::LeftMouseButton, EKeys::Gamepad_FaceButton_Bottom })
	{
		Gun->Reload();
		const int32 Schuesse = Gun->GetShotsFired();
		const int32 Munition = Gun->GetRemainingRounds();
		HUD->StartHelicopterLesson();
		TestTrue(TEXT("HUD startet den Trockenmodus"), Heli->IsLessonDryFireActive());
		Eingabe(Taste, true);
		TestTrue(TEXT("physischer Abzug liegt am Controller an"), PC->IsInputKeyDown(Taste));
		for (int32 I = 0; I < 15; ++I) { Ticken(); }
		TestEqual(TEXT("Trockenmodus verbraucht keine Munition"), Gun->GetRemainingRounds(), Munition);
		HUD->EndHelicopterLesson(FString());
		for (int32 I = 0; I < 15; ++I) { Ticken(); }
		TestEqual(TEXT("gehaltener Abzug feuert nach Abbruch nicht"), Gun->GetShotsFired(), Schuesse);
		TestEqual(TEXT("gehaltener Abzug verbraucht nach Abbruch keine Munition"), Gun->GetRemainingRounds(), Munition);
		TestTrue(TEXT("Loslass-Sperre bleibt aktiv"), Heli->IsDryFireReleasePending());
		Eingabe(Taste, false);
		Ticken();
		TestFalse(TEXT("Loslassen entsperrt"), Heli->IsDryFireReleasePending());
		Eingabe(Taste, true);
		for (int32 I = 0; I < 6; ++I) { Ticken(); }
		TestTrue(TEXT("neuer Druck feuert scharf"), Gun->GetShotsFired() > Schuesse);
		Eingabe(Taste, false);
		Ticken();
	}

	HUD->StartHelicopterLesson();
	AWiesbadenHelicopter* AndererHeli = World->SpawnActor<AWiesbadenHelicopter>();
	AndererHeli->SetLessonDryFireActive(true);
	PC->Possess(AndererHeli);
	HUD->bShowHUD = false;
	HUD->Tick(1.0f / 30.0f);
	TestFalse(TEXT("verborgenes HUD baut alte Lektion beim Pawnwechsel ab"), Heli->IsLessonDryFireActive());
	TestTrue(TEXT("Abbau fasst fremde Heli-Sperre nicht an"), AndererHeli->IsLessonDryFireActive());
	PC->Possess(Heli);
	HUD->StartHelicopterLesson();
	FDamageEvent TickSchaden;
	Heli->TakeDamage(100000.0f, TickSchaden, PC, nullptr);
	HUD->Tick(1.0f / 30.0f);
	TestFalse(TEXT("verborgenes HUD beendet Lektion beim Absturz"), Heli->IsLessonDryFireActive());
	World->SpawnActor<AWiesbadenSebboHq>();
	TestTrue(TEXT("Heli fuer HUD-Ende wiederaufgesetzt"), Heli->RespawnOnTowerHelipad());
	Eingabe(EKeys::Gamepad_FaceButton_Bottom, false);
	Ticken();
	HUD->StartHelicopterLesson();
	Eingabe(EKeys::Gamepad_FaceButton_Bottom, true);
	Ticken();
	const int32 VorHUD = Gun->GetShotsFired();
	HUD->EndPlay(EEndPlayReason::Destroyed);
	for (int32 I = 0; I < 15; ++I) { Ticken(); }
	TestEqual(TEXT("HUD-Ende mit gehaltenem A feuert nicht"), Gun->GetShotsFired(), VorHUD);

	FDamageEvent Schaden;
	Heli->TakeDamage(100000.0f, Schaden, PC, nullptr);
	for (int32 I = 0; I < 15; ++I) { Ticken(); }
	TestEqual(TEXT("abgestuerzter Heli kann nicht feuern"), Gun->GetShotsFired(), VorHUD);
	TestFalse(TEXT("Absturz stoppt den Geschuetzabzug"), Gun->IsTriggerHeld());
	World->SpawnActor<AWiesbadenSebboHq>();
	TestTrue(TEXT("Respawn gelingt"), Heli->RespawnOnTowerHelipad());
	for (int32 I = 0; I < 15; ++I) { Ticken(); }
	TestEqual(TEXT("gehaltener Abzug feuert nach Respawn nicht"), Gun->GetShotsFired(), VorHUD);
	Eingabe(EKeys::Gamepad_FaceButton_Bottom, false);
	Ticken();
	TestFalse(TEXT("Loslassen nach Respawn entsperrt"), Heli->IsDryFireReleasePending());
	return true;
}
