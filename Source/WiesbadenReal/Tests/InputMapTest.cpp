// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/WiesbadenInputMap.h"

/**
 * Eingabebelegung (Tastatur/Maus + XBox) nach RDR2-Vorbild.
 *
 * Die Belegung ist Daten in Core/WiesbadenInputMap - hier wird geprueft,
 * dass die Tabelle vollstaendig und widerspruchsfrei ist und dass die
 * beiden Ableitungen (Rad-Aufteilung, Zoom-Stufen) die im Spiel gebrauchten
 * Grenzen halten. Anlass (26.09.2026): ADS hing ausschliesslich an der
 * rechten Maustaste, fuer das Gamepad gab es kein Zielen und keinen
 * Waffenwechsel; ohne Tabelle gab es nichts, gegen das ein Test pruefen
 * koennte.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInputBelegungTest,
	"WiesbadenReal.Input.Belegung",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FInputBelegungTest::RunTest(const FString& Parameters)
{
	const TArray<FWiesbadenBelegung>& Tabelle = WiesbadenInputMap::Belegungen();

	// Jede Aktion kommt GENAU einmal vor - eine doppelte Zeile wuerde eine
	// Aktion still verdoppeln, eine fehlende sie unbedienbar machen.
	for (EWiesbadenInputAction Action : {
		EWiesbadenInputAction::Feuern, EWiesbadenInputAction::Zielen,
		EWiesbadenInputAction::Springen, EWiesbadenInputAction::RennenHalten,
		EWiesbadenInputAction::DuckenHalten, EWiesbadenInputAction::WaffeVor,
		EWiesbadenInputAction::WaffeZurueck, EWiesbadenInputAction::AnsichtWechseln,
		EWiesbadenInputAction::Einsteigen })
	{
		int32 Treffer = 0;
		for (const FWiesbadenBelegung& Zeile : Tabelle)
		{
			if (Zeile.Action == Action)
			{
				++Treffer;
			}
		}
		TestEqual(FString::Printf(TEXT("Aktion %d genau einmal belegt"),
			static_cast<int32>(Action)), Treffer, 1);
	}

	// Jede Zeile braucht mindestens EINE Taste (Tastatur oder Gamepad) und
	// einen Anzeigetext fuer die Legende.
	for (const FWiesbadenBelegung& Zeile : Tabelle)
	{
		TestTrue(FString::Printf(TEXT("Zeile '%s' hat eine Taste"), Zeile.Beschreibung),
			Zeile.Tasten.Num() > 0 || Zeile.Gamepad.Num() > 0);
		TestTrue(FString::Printf(TEXT("Zeile '%s' hat einen Text"), Zeile.Beschreibung),
			FCString::Strlen(Zeile.Beschreibung) > 0);
	}

	// Das RDR2-Kernlayout am Gamepad: RT feuern, LT zielen, A springen,
	// L3 rennen, R3 ducken. Ohne diese Zuordnung waere das Pad unvollstaendig.
	struct FPadProbe { EWiesbadenInputAction Action; FKey Key; bool bAnalog; };
	const FPadProbe Probes[] = {
		{ EWiesbadenInputAction::Feuern, EKeys::Gamepad_RightTriggerAxis, true },
		{ EWiesbadenInputAction::Zielen, EKeys::Gamepad_LeftTriggerAxis, true },
		{ EWiesbadenInputAction::Springen, EKeys::Gamepad_FaceButton_Bottom, false },
		{ EWiesbadenInputAction::RennenHalten, EKeys::Gamepad_LeftThumbstick, false },
		{ EWiesbadenInputAction::DuckenHalten, EKeys::Gamepad_RightThumbstick, false },
		{ EWiesbadenInputAction::WaffeVor, EKeys::Gamepad_RightShoulder, false },
		{ EWiesbadenInputAction::WaffeZurueck, EKeys::Gamepad_LeftShoulder, false },
		{ EWiesbadenInputAction::AnsichtWechseln, EKeys::Gamepad_FaceButton_Top, false },
		{ EWiesbadenInputAction::Einsteigen, EKeys::Gamepad_FaceButton_Left, false },
	};
	for (const FPadProbe& Probe : Probes)
	{
		bool bGefunden = false;
		for (const FWiesbadenBelegung& Zeile : Tabelle)
		{
			if (Zeile.Action != Probe.Action)
			{
				continue;
			}
			bGefunden = Zeile.Gamepad.Contains(Probe.Key) && Zeile.bAnalog == Probe.bAnalog;
			break;
		}
		TestTrue(FString::Printf(TEXT("Gamepad-Belegung fuer Aktion %d"),
			static_cast<int32>(Probe.Action)), bGefunden);
	}

	// Maus bleibt voll bestehen: Feuern links, Zielen rechts.
	for (const FWiesbadenBelegung& Zeile : Tabelle)
	{
		if (Zeile.Action == EWiesbadenInputAction::Feuern)
		{
			TestTrue(TEXT("Feuern auf linker Maustaste"),
				Zeile.Tasten.Contains(EKeys::LeftMouseButton));
		}
		if (Zeile.Action == EWiesbadenInputAction::Zielen)
		{
			TestTrue(TEXT("Zielen auf rechter Maustaste"),
				Zeile.Tasten.Contains(EKeys::RightMouseButton));
		}
	}

	// Ohne Controller keine Aktion - die Tabelle darf nie crashen.
	TestFalse(TEXT("Ohne Controller nichts gedrueckt"),
		WiesbadenInputMap::IsActionDown(nullptr, EWiesbadenInputAction::Feuern));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInputTriggerTest,
	"WiesbadenReal.Input.TriggerSchwelle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FInputTriggerTest::RunTest(const FString& Parameters)
{
	// Trigger sind Achsen: die Schwelle entscheidet. Exakt auf der Schwelle
	// gilt als gedrueckt (leichte Anschlaege landeten genau dort).
	TestFalse(TEXT("0.00 nicht gedrueckt"), WiesbadenInputMap::TriggerGedrueckt(0.0f));
	TestFalse(TEXT("0.34 nicht gedrueckt"), WiesbadenInputMap::TriggerGedrueckt(0.34f));
	TestTrue(TEXT("0.35 gedrueckt (Schwelle)"), WiesbadenInputMap::TriggerGedrueckt(0.35f));
	TestTrue(TEXT("1.00 gedrueckt"), WiesbadenInputMap::TriggerGedrueckt(1.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInputMausradRouteTest,
	"WiesbadenReal.Input.MausradRoute",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FInputMausradRouteTest::RunTest(const FString& Parameters)
{
	using ERoute = WiesbadenInputMap::EMausradRoute;

	// Die schneidende Waffe hat VORRANG: das Rad dreht die Schnittebene,
	// auch waehrend gezielt wird (Dead-Space-Prinzip).
	TestTrue(TEXT("Schneidende Waffe: Schnittebene, auch ohne Zielen"),
		WiesbadenInputMap::RouteMausrad(true, false) == ERoute::Schnittebene);
	TestTrue(TEXT("Schneidende Waffe: Schnittebene, auch beim Zielen"),
		WiesbadenInputMap::RouteMausrad(true, true) == ERoute::Schnittebene);

	// Ohne schneidende Waffe: zielen zoomt, sonst wird gewechselt.
	TestTrue(TEXT("Zielen zoomt"),
		WiesbadenInputMap::RouteMausrad(false, true) == ERoute::Zoom);
	TestTrue(TEXT("Ohne Zielen wird gewechselt"),
		WiesbadenInputMap::RouteMausrad(false, false) == ERoute::Waffenwechsel);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInputZoomStufeTest,
	"WiesbadenReal.Input.ZoomStufen",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FInputZoomStufeTest::RunTest(const FString& Parameters)
{
	// Aufzoomen in Rasten, Grenze der Waffe (z. B. Scharfschuetze 3.5).
	TestEqual(TEXT("Ein Klick zoomt eine Stufe"),
		WiesbadenInputMap::ZoomStufe(1.0f, 1, 0.25f, 3.5f), 1.25f);
	TestEqual(TEXT("Obergrenze wird gehalten"),
		WiesbadenInputMap::ZoomStufe(3.5f, 1, 0.25f, 3.5f), 3.5f);
	TestEqual(TEXT("Sprung ueber die Grenze landet auf der Grenze"),
		WiesbadenInputMap::ZoomStufe(3.25f, 5, 0.25f, 3.5f), 3.5f);

	// Rueckwaertszoom faellt nicht unter 1 (kein Auszoomen ins Weitwinkel).
	TestEqual(TEXT("Untergrenze 1.0"),
		WiesbadenInputMap::ZoomStufe(1.25f, -5, 0.25f, 3.5f), 1.0f);

	// Eine Waffe ohne Zoom (Grenze <= 1) bleibt bei 1.0 - frueher hebelte
	// eine MaxZoom-Angabe unter 1 die Clamp-Grenze durcheinander.
	TestEqual(TEXT("Grenze unter 1 wird auf 1 gehoben"),
		WiesbadenInputMap::ZoomStufe(1.0f, 3, 0.25f, 0.8f), 1.0f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
