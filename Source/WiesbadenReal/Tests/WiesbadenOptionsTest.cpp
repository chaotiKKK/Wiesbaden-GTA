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

	// -- KENNUNGEN: woran Lesen und Schreiben eine Zeile erkennen ------------
	//
	// Die Bindung hing frueher an der deutschen Beschriftung. Wer eine
	// umbenannt haette, haette die Zeile lautlos vom System getrennt: sie
	// stuende weiter im Menue, liesse sich verstellen und bewirkte nichts.
	{
		TArray<FString> BusLabels = { TEXT("Gesamt"), TEXT("Musik"), TEXT("Effekte") };
		TArray<FWbOptionRow> Rows;
		BuildRows(BusLabels.Num(), BusLabels, Rows);

		// (a) Keine Zeile ohne Kennung. MAX heisst "nicht vergeben" - eine
		// solche Zeile faende im HUD keinen Zweig.
		TSet<EWbOptionId> Gesehen;
		for (const FWbOptionRow& Row : Rows)
		{
			TestTrue(*FString::Printf(TEXT("Zeile '%s' hat eine Kennung"), *Row.Label),
				Row.Id != EWbOptionId::MAX);
			Gesehen.Add(Row.Id);
		}

		// (b) Umgekehrt: jede erklaerte Kennung kommt auch wirklich vor. Eine
		// Kennung ohne Zeile waere ein Zweig im HUD, den nie jemand erreicht.
		for (int32 i = 0; i < static_cast<int32>(EWbOptionId::MAX); ++i)
		{
			const EWbOptionId Id = static_cast<EWbOptionId>(i);
			TestTrue(*FString::Printf(TEXT("Kennung %d hat eine Zeile"), i),
				Gesehen.Contains(Id));
		}

		// (c) Eindeutig - ausser TonBus, der absichtlich mehrfach vorkommt und
		// sich ueber BusIndex unterscheidet.
		TMap<EWbOptionId, int32> Zahl;
		for (const FWbOptionRow& Row : Rows)
		{
			Zahl.FindOrAdd(Row.Id)++;
		}
		for (const TPair<EWbOptionId, int32>& Paar : Zahl)
		{
			if (Paar.Key == EWbOptionId::TonBus)
			{
				continue;
			}
			TestEqual(*FString::Printf(TEXT("Kennung %d genau einmal"),
				static_cast<int32>(Paar.Key)), Paar.Value, 1);
		}

		// (d) Ton-Zeilen tragen TonBus und einen gueltigen Bus - daran und nur
		// daran erkennt das Mischpult sie.
		TSet<int32> Busse;
		for (const FWbOptionRow& Row : Rows)
		{
			if (Row.Group != EWbOptionGroup::Ton)
			{
				continue;
			}
			TestEqual(TEXT("Ton-Zeile traegt TonBus"),
				static_cast<int32>(Row.Id), static_cast<int32>(EWbOptionId::TonBus));
			TestTrue(TEXT("Ton-Zeile hat einen Bus"),
				Row.BusIndex >= 0 && Row.BusIndex < BusLabels.Num());
			TestFalse(TEXT("jeder Bus nur einmal"), Busse.Contains(Row.BusIndex));
			Busse.Add(Row.BusIndex);
		}

		// (e) Der eigentliche Punkt: die Beschriftung ist jetzt FREI. "Effekte"
		// heisst schon heute zweierlei - die Grafikstufe und ein Ton-Bus. Zwei
		// Zeilen mit demselben Namen muessen verschiedene Kennungen haben,
		// sonst haette der alte Vergleich die falsche erwischt.
		for (const FWbOptionRow& A : Rows)
		{
			for (const FWbOptionRow& B : Rows)
			{
				if (&A == &B || A.Label != B.Label)
				{
					continue;
				}
				const bool bUnterscheidbar =
					(A.Id != B.Id) || (A.BusIndex != B.BusIndex);
				TestTrue(*FString::Printf(
					TEXT("gleichnamige Zeilen '%s' bleiben unterscheidbar"), *A.Label),
					bUnterscheidbar);
			}
		}
	}

	// -- FLANKENERKENNUNG: ein Druck ist ein Schritt, nicht dreissig ---------
	//
	// Dieselbe Funktion bedient Pausemenue UND Optionsfenster. Ohne sie wuerde
	// eine gehaltene Taste in jedem Bild einen Schritt machen - die
	// Lautstaerke ginge in einem Wimpernschlag von 0 auf 100.
	{
		bool bHeld = false;

		TestTrue(TEXT("der erste Druck zaehlt"), EdgePressed(true, bHeld));
		TestTrue(TEXT("und merkt sich, dass sie unten ist"), bHeld);

		TestFalse(TEXT("gehalten zaehlt NICHT nochmal"), EdgePressed(true, bHeld));
		TestFalse(TEXT("auch beim dritten Bild nicht"), EdgePressed(true, bHeld));

		TestFalse(TEXT("das Loslassen selbst ist kein Schritt"), EdgePressed(false, bHeld));
		TestFalse(TEXT("und merkt sich, dass sie oben ist"), bHeld);

		TestTrue(TEXT("nach dem Loslassen zaehlt der naechste Druck wieder"),
			EdgePressed(true, bHeld));

		// Aus der Ruhe heraus loslassen aendert nichts - sonst haette ein
		// Fenster, das mit gedrueckter Taste aufgeht, einen Geisterschritt.
		bool bRuhe = false;
		TestFalse(TEXT("losgelassen aus der Ruhe zaehlt nicht"), EdgePressed(false, bRuhe));
		TestFalse(TEXT("und bleibt oben"), bRuhe);
	}

	// -- AUSWAHLKLEMMUNG: die Zeilenzahl kann sich unter der Auswahl aendern --
	//
	// Faellt das Mischpult weg, waehrend das Fenster offen ist, verschwinden
	// sieben Zeilen. Eine Auswahl von vorher zeigte dann ins Leere.
	{
		TestEqual(TEXT("mitten drin bleibt sie"), ClampRow(3, 16), 3);
		TestEqual(TEXT("ueber dem Ende faellt sie auf die letzte"), ClampRow(15, 9), 8);
		TestEqual(TEXT("unter null faellt sie auf die erste"), ClampRow(-4, 9), 0);
		TestEqual(TEXT("die letzte Zeile bleibt gueltig"), ClampRow(8, 9), 8);
		TestEqual(TEXT("ohne Zeilen bleibt 0"), ClampRow(5, 0), 0);
		TestEqual(TEXT("auch bei negativer Zahl"), ClampRow(5, -1), 0);
	}

	// -- SCHRITTWEITE je Wertart, ausdruecklich --------------------------------
	//
	// Die Schrittweite ist das, was man beim Tippen spuert. Zu gross, und man
	// trifft den gewuenschten Wert nie; zu klein, und man haelt die Taste.
	{
		TestEqual(TEXT("Qualitaet eine Stufe"),
			Step(EWbOptionKind::Qualitaet, 1.0, +1), 2.0);
		TestEqual(TEXT("Lautstaerke 5 Prozent"),
			Step(EWbOptionKind::Lautstaerke, 0.50, +1), 0.55, 1e-9);
		TestEqual(TEXT("Anteil 10 Prozent"),
			Step(EWbOptionKind::Anteil, 0.50, +1), 0.60, 1e-9);
		TestEqual(TEXT("Faktor ein Viertel"),
			Step(EWbOptionKind::Faktor, 1.00, +1), 1.25, 1e-9);
		TestEqual(TEXT("Tageszeit eine Stunde"),
			Step(EWbOptionKind::Tageszeit, 12.0, +1), 13.0);
		TestEqual(TEXT("Schalter kippt"),
			Step(EWbOptionKind::Schalter, 0.0, +1), 1.0);

		// Und in die Gegenrichtung genauso gross.
		TestEqual(TEXT("Lautstaerke rueckwaerts 5 Prozent"),
			Step(EWbOptionKind::Lautstaerke, 0.50, -1), 0.45, 1e-9);
		TestEqual(TEXT("Anteil rueckwaerts 10 Prozent"),
			Step(EWbOptionKind::Anteil, 0.50, -1), 0.40, 1e-9);
	}

	// -- Der Ruecklese-Waechter: Rundungsrauschen ja, echte Fehlschlaege nein -
	//
	// GEMESSEN: der Waechter lief mit FMath::IsNearlyEqual und der Vorgabe fuer
	// double (1e-8). TrafficDensity ist ein float - die geschriebene 0,6 kam
	// als 0,60000002384 zurueck und wurde als "NICHT ANGEKOMMEN" gemeldet,
	// obwohl sie angekommen war.
	{
		// Genau der Fall aus dem Spiel: float-Rundung auf die 0,6.
		const double AlsFloat = static_cast<double>(static_cast<float>(0.6));
		TestTrue(TEXT("float-Rundung gilt als angekommen"),
			ValueArrived(0.6, AlsFloat));
		TestTrue(TEXT("auch bei grossen Werten"),
			ValueArrived(144.0, static_cast<double>(static_cast<float>(144.0))));
		TestTrue(TEXT("und bei null"), ValueArrived(0.0, 0.0));

		// GEGENPROBE: ein wirklich verschluckter Wert faellt weiter auf. Der
		// kleinste echte Schritt des Menues ist 0,05 (Lautstaerke) - alles
		// darunter waere kein Schritt, alles darueber ein Fehlschlag.
		TestFalse(TEXT("ein geklemmter Wert faellt auf"),
			ValueArrived(0.6, 0.5));
		TestFalse(TEXT("ein kleinster Schritt faellt auf"),
			ValueArrived(0.55, 0.50));
		TestFalse(TEXT("eine verschluckte Qualitaetsstufe faellt auf"),
			ValueArrived(4.0, 3.0));
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
