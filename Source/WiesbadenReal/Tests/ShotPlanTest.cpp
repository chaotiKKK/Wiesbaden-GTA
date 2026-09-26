// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
//
// Test des Aufnahme-Werkzeugs, nicht des Spiels: -WbShotSteps=<plan> sagt
// einer Aufnahmesitzung, in welcher Reihenfolge sie knipsen soll. Genau
// dieser Parser hat am 26.09.2026 eine ganze Sitzung verschluckt - die
// 19 geplanten Schritte liefen als EINE, das Bild war ein Follow-Bild mit
// Cockpit-Titel. Beide Faelle stehen hier als Bedingung.

#include "Misc/AutomationTest.h"

#include "World/WiesbadenCitySubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWbShotPlanTest,
	"WiesbadenReal.World.Ablaufplan",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWbShotPlanTest::RunTest(const FString& Parameters)
{
	TArray<FString> Steps;

	// -- 1. Der Normalfall: mehrere Schritte je Sitzung ---------------------
	{
		Steps.Reset();
		UWiesbadenCitySubsystem::ParseShotPlan(TEXT("modus=2+hold+hold"), Steps);

		TestEqual(TEXT("Drei Schritte ergeben drei Schritte"), Steps.Num(), 3);
		if (Steps.Num() == 3)
		{
			TestEqual(TEXT("Schritt 1 ist der Kameramodus"), Steps[0], FString(TEXT("modus=2")));
			TestEqual(TEXT("Schritt 2 ist ein Bild aus der aktuellen Sicht"),
				Steps[1], FString(TEXT("hold")));
			TestEqual(TEXT("Schritt 3 dito"), Steps[2], FString(TEXT("hold")));
		}
	}

	// -- 2. Der Rueckfall, der die Sitzung verschluckt hat -------------------
	{
		// Die Kuerzung passiert NICHT im Plan-Parser, sondern eine Ebene
		// weiter oben: FParse::Value liest die Befehlszeile und haelt am
		// ersten Komma an. Ein Plan mit Komma kommt also bereits auf einem
		// Schritt beim Parser an - die uebrigen gelten als unbekannte
		// Schalter. Genau das war der Befund vom 26.09.2026: 19 Schritte
		// geplant, einer gefahren. Deshalb wird HIER geprueft, und das Plus-
		// zeichen ist genau der Umweg darueber herum.
		FString Gelesen;
		const bool bGefunden = FParse::Value(
			TEXT("-WbShotSteps=modus=2,hold,hold -WbShotGap=1.2"),
			TEXT("WbShotSteps="), Gelesen);

		TestTrue(TEXT("Das Pluszeichen ist Pflicht: die Kommazeichen-Form findet "
			"der Parameter nicht als ganzen Plan"), bGefunden);
		TestEqual(TEXT("FParse haelt am ersten Komma an"),
			Gelesen, FString(TEXT("modus=2")));

		Steps.Reset();
		UWiesbadenCitySubsystem::ParseShotPlan(Gelesen, Steps);
		TestEqual(TEXT("Es bleibt genau ein Schritt uebrig"), Steps.Num(), 1);
	}

	{
		// Derselbe Aufruf mit Pluszeichen: hier kommt der GANZE Plan an.
		FString Gelesen;
		const bool bGefunden = FParse::Value(
			TEXT("-WbShotSteps=modus=2+hold+hold -WbShotGap=1.2"),
			TEXT("WbShotSteps="), Gelesen);

		TestTrue(TEXT("Die Pluszeichen-Form wird vollstaendig gelesen"), bGefunden);
		TestEqual(TEXT("und endet am Schalter, nicht am Komma"),
			Gelesen, FString(TEXT("modus=2+hold+hold")));

		Steps.Reset();
		UWiesbadenCitySubsystem::ParseShotPlan(Gelesen, Steps);
		TestEqual(TEXT("Daraus werden drei Schritte"), Steps.Num(), 3);
	}

	// -- 3. Leerzeichen sind beim Abtippen der Normalfall --------------------
	{
		Steps.Reset();
		UWiesbadenCitySubsystem::ParseShotPlan(TEXT("  modus=1 +  turm  "), Steps);

		TestEqual(TEXT("Leerzeichen stoeren den Plan nicht"), Steps.Num(), 2);
		if (Steps.Num() == 2)
		{
			TestEqual(TEXT("Schritt 1 ohne Randausschuss"), Steps[0], FString(TEXT("modus=1")));
			TestEqual(TEXT("Schritt 2 ohne Randausschuss"), Steps[1], FString(TEXT("turm")));
		}
	}

	// -- 4. Leere Eintraege sind Tippfehler, keine Schritte ----------------
	{
		Steps.Reset();
		UWiesbadenCitySubsystem::ParseShotPlan(TEXT("hold++turm+"), Steps);

		TestEqual(TEXT("Leere Eintraege fallen weg"), Steps.Num(), 2);
		if (Steps.Num() == 2)
		{
			TestEqual(TEXT("und stoeren die Reihenfolge nicht"),
				Steps[1], FString(TEXT("turm")));
		}
	}

	// -- 5. Leerer Plan = kein Bild ----------------------------------------
	{
		Steps.Reset();
		UWiesbadenCitySubsystem::ParseShotPlan(TEXT("   "), Steps);
		TestEqual(TEXT("Ein leerer Plan liefert keine Schritte"), Steps.Num(), 0);
	}

	// -- 6. Die Grenze, die daraus folgt: ein Schritt ohne Komma -----------
	{
		// Eine Posenzeile enthaelt Kommas ("Hoehe, AtX, AtY, ..."). Als
		// Schritt eines Plans wuerde sie zerschnitten - Posen kommen
		// darum ueber -WbShotPoseFile, nicht ueber den Plan.
		Steps.Reset();
		UWiesbadenCitySubsystem::ParseShotPlan(TEXT("40,-121964,-119658,90,-70,10,90,-10"),
			Steps);

		// Acht Felder, also acht Teile. Genau das ist der Grund, warum Posen
		// ueber -WbShotPoseFile laufen und nicht ueber den Plan.
		TestEqual(TEXT("Eine Posenzeile im Plan wird zerschnitten - sie gehoert "
			"in die Posendatei"), Steps.Num(), 8);
	}

	// -- 7. Die andere Seite: ein Schritt, der eine Zahl IST ----------------
	{
		// "modus=2" darf nicht als Zahl gelesen und verworfen werden - der
		// Schritt traegt seinen Namen im Text.
		Steps.Reset();
		UWiesbadenCitySubsystem::ParseShotPlan(TEXT("modus=2"), Steps);
		TestEqual(TEXT("Ein einzelner Schritt bleibt ganz"), Steps.Num(), 1);
		if (Steps.Num() == 1)
		{
			TestEqual(TEXT("und unveraendert"), Steps[0], FString(TEXT("modus=2")));
		}
	}


	// -- 8. Die Abzug-Schritte kommen unveraendert an -----------------------
	{
		// "feuer" und "feuer-aus" werden NICHT vom Parser ausgewertet, sondern
		// erst beim Abspielen des Schritts. Der Parser darf sie deshalb weder
	// verschlucken noch verwechseln - sonst haelt das Geschaeft nie.
		Steps.Reset();
		UWiesbadenCitySubsystem::ParseShotPlan(TEXT("feuer+hold+hold+feuer-aus"), Steps);

		TestEqual(TEXT("Der Abzug-Plan ergibt vier Schritte"), Steps.Num(), 4);
		if (Steps.Num() == 4)
		{
			TestEqual(TEXT("Schritt 1 haelt den Abzug"), Steps[0], FString(TEXT("feuer")));
			TestEqual(TEXT("Schritt 4 loest ihn wieder"),
				Steps[3], FString(TEXT("feuer-aus")));
		}
	}

	return true;
}
