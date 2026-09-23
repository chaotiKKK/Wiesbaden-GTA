// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "UI/WiesbadenOptions.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWiesbadenOptionsTest,
	"WiesbadenReal.UI.Optionen",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Der datenreine Teil des Optionsmenues: welche Zeilen es gibt, wie ein Wert
 * aussieht und wie er sich beim Tastendruck bewegt. Ohne Welt, ohne Canvas.
 *
 * Was hier NICHT geprueft werden kann, ist die Wirkung selbst - die haengt an
 * GameUserSettings, Mischpult und Verkehrs-Sim und wird im Spiel belegt.
 */
bool FWiesbadenOptionsTest::RunTest(const FString& Parameters)
{
	using namespace WiesbadenOptions;

	// -- Zeilen: Gruppen bleiben zusammen und in fester Reihenfolge ----------
	{
		TArray<FString> BusLabels = { TEXT("Gesamt"), TEXT("Musik"), TEXT("Effekte") };
		TArray<FWbOptionRow> Rows;
		BuildRows(BusLabels.Num(), BusLabels, Rows);

		TestTrue(TEXT("es gibt Zeilen"), Rows.Num() > 0);

		// Eine Gruppe darf nicht zweimal anfangen - sonst stuenden im Menue
		// zwei Ueberschriften "TON" mit Fremdzeilen dazwischen.
		TSet<EWbOptionGroup> Gesehen;
		EWbOptionGroup Letzte = EWbOptionGroup::MAX;
		for (const FWbOptionRow& Row : Rows)
		{
			if (Row.Group != Letzte)
			{
				TestFalse(TEXT("jede Gruppe beginnt genau einmal"), Gesehen.Contains(Row.Group));
				Gesehen.Add(Row.Group);
				Letzte = Row.Group;
			}
		}

		// Je Bus genau eine Tonzeile, und jede kennt ihren Bus.
		int32 TonZeilen = 0;
		for (const FWbOptionRow& Row : Rows)
		{
			if (Row.Group == EWbOptionGroup::Ton)
			{
				TestEqual(TEXT("Tonzeile kennt ihren Bus"), Row.BusIndex, TonZeilen);
				TestEqual(TEXT("Tonzeile traegt die Bus-Beschriftung"),
					Row.Label, BusLabels[TonZeilen]);
				++TonZeilen;
			}
		}
		TestEqual(TEXT("eine Tonzeile je Bus"), TonZeilen, BusLabels.Num());

		// Keine leere Beschriftung, kein leerer Hinweis - eine Zeile ohne
		// Erklaerung ist im Menue eine Zeile, die man nicht anfasst.
		for (const FWbOptionRow& Row : Rows)
		{
			TestFalse(TEXT("Beschriftung nicht leer"), Row.Label.IsEmpty());
			TestFalse(TEXT("Hinweis nicht leer"), Row.Hinweis.IsEmpty());
		}
	}

	// -- OHNE MISCHPULT GIBT ES KEINE TONZEILE -------------------------------
	//
	// Das ist der Kern der Zusage "keine Option, die nichts tut": fehlt das
	// System, fehlt die Zeile. Ein Regler ohne Mischpult liesse sich zeichnen
	// und bewegte nichts.
	{
		TArray<FWbOptionRow> Rows;
		BuildRows(0, TArray<FString>(), Rows);
		for (const FWbOptionRow& Row : Rows)
		{
			TestTrue(TEXT("ohne Mischpult keine Tonzeile"),
				Row.Group != EWbOptionGroup::Ton);
		}
		TestTrue(TEXT("die uebrigen Gruppen bleiben"), Rows.Num() > 0);
	}

	// -- Fehlende Bus-Beschriftungen ergeben trotzdem lesbare Zeilen ---------
	{
		TArray<FWbOptionRow> Rows;
		BuildRows(2, TArray<FString>(), Rows);
		int32 Ton = 0;
		for (const FWbOptionRow& Row : Rows)
		{
			if (Row.Group == EWbOptionGroup::Ton)
			{
				TestFalse(TEXT("Ersatzname statt leer"), Row.Label.IsEmpty());
				++Ton;
			}
		}
		TestEqual(TEXT("zwei Tonzeilen"), Ton, 2);
	}

	// -- Darstellung der Werte ----------------------------------------------
	{
		TestEqual(TEXT("Qualitaet 0"), FormatValue(EWbOptionKind::Qualitaet, 0.0), FString(TEXT("Sehr niedrig")));
		TestEqual(TEXT("Qualitaet 4"), FormatValue(EWbOptionKind::Qualitaet, 4.0), FString(TEXT("Episch")));
		TestEqual(TEXT("Bildrate 0 = ohne Grenze"),
			FormatValue(EWbOptionKind::Bildrate, 0.0), FString(TEXT("ohne Grenze")));
		TestEqual(TEXT("Bildrate 60"), FormatValue(EWbOptionKind::Bildrate, 60.0), FString(TEXT("60 /s")));
		TestEqual(TEXT("Lautstaerke 0,5"), FormatValue(EWbOptionKind::Lautstaerke, 0.5), FString(TEXT("50 %")));
		TestEqual(TEXT("Faktor"), FormatValue(EWbOptionKind::Faktor, 1.25), FString(TEXT("1.25x")));
		TestEqual(TEXT("Schalter an"), FormatValue(EWbOptionKind::Schalter, 1.0), FString(TEXT("an")));
		TestEqual(TEXT("Schalter aus"), FormatValue(EWbOptionKind::Schalter, 0.0), FString(TEXT("aus")));
		TestEqual(TEXT("Tageszeit -1 = Systemzeit"),
			FormatValue(EWbOptionKind::Tageszeit, -1.0), FString(TEXT("Systemzeit")));
		TestEqual(TEXT("Tageszeit 22 Uhr"),
			FormatValue(EWbOptionKind::Tageszeit, 22.0), FString(TEXT("22:00 Uhr")));
	}

	// -- Schritte klemmen, statt umzulaufen ---------------------------------
	//
	// Ein umlaufender Lautstaerkeregler waere eine Falle: einmal zu weit
	// rechts, und es ist still.
	{
		TestEqual(TEXT("Lautstaerke bleibt bei 1"),
			Step(EWbOptionKind::Lautstaerke, 1.0, +1), 1.0);
		TestEqual(TEXT("Lautstaerke bleibt bei 0"),
			Step(EWbOptionKind::Lautstaerke, 0.0, -1), 0.0);
		TestEqual(TEXT("Qualitaet bleibt bei 4"),
			Step(EWbOptionKind::Qualitaet, 4.0, +1), 4.0);
		TestEqual(TEXT("Qualitaet bleibt bei 0"),
			Step(EWbOptionKind::Qualitaet, 0.0, -1), 0.0);
		TestEqual(TEXT("ohne Richtung keine Aenderung"),
			Step(EWbOptionKind::Qualitaet, 2.0, 0), 2.0);
	}

	// -- Die Bildrate laeuft ueber eine Leiter, nicht ueber eine Schrittweite -
	{
		TestEqual(TEXT("von ohne Grenze auf 30"), Step(EWbOptionKind::Bildrate, 0.0, +1), 30.0);
		TestEqual(TEXT("von 30 auf 60"), Step(EWbOptionKind::Bildrate, 30.0, +1), 60.0);
		TestEqual(TEXT("oben bleibt 144"), Step(EWbOptionKind::Bildrate, 144.0, +1), 144.0);
		TestEqual(TEXT("unten bleibt ohne Grenze"), Step(EWbOptionKind::Bildrate, 0.0, -1), 0.0);
		// Ein Fremdwert aus der ini rastet auf die naechste Stufe ein, statt
		// die Leiter zu verlassen.
		TestEqual(TEXT("Fremdwert rastet ein"), Step(EWbOptionKind::Bildrate, 58.0, +1), 90.0);
	}

	// -- NUR die Tageszeit laeuft um ----------------------------------------
	//
	// Sonst muesste man 24 Mal nach links, um die Uhr des Rechners
	// zurueckzubekommen.
	{
		TestEqual(TEXT("Systemzeit -> 0 Uhr"), Step(EWbOptionKind::Tageszeit, -1.0, +1), 0.0);
		TestEqual(TEXT("23 Uhr -> Systemzeit"), Step(EWbOptionKind::Tageszeit, 23.0, +1), -1.0);
		TestEqual(TEXT("Systemzeit rueckwaerts -> 23 Uhr"),
			Step(EWbOptionKind::Tageszeit, -1.0, -1), 23.0);
	}

	// -- Balken: nur wo ein Fuellstand etwas bedeutet ------------------------
	{
		TestTrue(TEXT("Schalter hat keinen Balken"),
			BarFraction(EWbOptionKind::Schalter, 1.0) < 0.0);
		TestTrue(TEXT("Tageszeit hat keinen Balken"),
			BarFraction(EWbOptionKind::Tageszeit, 12.0) < 0.0);
		TestEqual(TEXT("Lautstaerke halb"),
			BarFraction(EWbOptionKind::Lautstaerke, 0.5), 0.5, 0.001);
		TestEqual(TEXT("Qualitaet Episch ist voll"),
			BarFraction(EWbOptionKind::Qualitaet, 4.0), 1.0, 0.001);
		TestEqual(TEXT("Faktor 1,0 liegt bei ihrem Anteil"),
			BarFraction(EWbOptionKind::Faktor, 0.25), 0.0, 0.001);
	}

	// -- Auswahl laeuft um, und zwar in beide Richtungen ---------------------
	{
		TestEqual(TEXT("vorwaerts"), NextRow(0, 3, +1), 1);
		TestEqual(TEXT("ueber das Ende"), NextRow(2, 3, +1), 0);
		TestEqual(TEXT("rueckwaerts unter null"), NextRow(0, 3, -1), 2);
		TestEqual(TEXT("leere Liste bleibt bei 0"), NextRow(0, 0, +1), 0);
	}

	// -- Jede Gruppe hat eine Ueberschrift ----------------------------------
	{
		for (int32 i = 0; i < static_cast<int32>(EWbOptionGroup::MAX); ++i)
		{
			TestFalse(TEXT("Gruppe hat eine Ueberschrift"),
				GroupLabel(static_cast<EWbOptionGroup>(i)).IsEmpty());
		}
	}

	return true;
}
