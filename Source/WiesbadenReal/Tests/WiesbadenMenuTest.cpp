// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "UI/WiesbadenMenuFlow.h"
#include "UI/WiesbadenMinimap.h"

namespace
{
	/** Segment mit eigener Z-Hoehe - die Strasse traegt sie mit. */
	FRoadSegment MakeSegment(
		int32 Id, const FString& Name,
		const TArray<FVector>& Centerline)
	{
		FRoadSegment Segment;
		Segment.SegmentId = Id;
		Segment.StreetName = Name;
		Segment.HighwayType = EOSMHighwayType::Secondary;
		Segment.CarriagewayWidthCm = 650.0;
		Segment.Centerline = Centerline;
		double Laenge = 0.0;
		for (int32 i = 1; i < Centerline.Num(); ++i)
		{
			Laenge += FVector::Dist2D(Centerline[i - 1], Centerline[i]);
		}
		Segment.LengthCm = Laenge;
		return Segment;
	}

	/** Die Gruppen, die das Hauptmenue anbietet - in Lesefolge. */
	const EWbOptionGroup AlleGruppen[] = {
		EWbOptionGroup::Ton,
		EWbOptionGroup::Grafik,
		EWbOptionGroup::Steuerung,
		EWbOptionGroup::Bild,
		EWbOptionGroup::Debug,
		EWbOptionGroup::Spielwelt,
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWiesbadenMenuAblaufTest,
	"WiesbadenReal.UI.Menu.Ablauf",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Der Weg durch die Menues, ohne Bildschirm: von "Spiel starten" bis zur
 * Gruppe, und wieder zurueck.
 *
 * Geprueft wird die EIGENTLICHE Gefahr dieses Ablaufs - dass eine Taste auf
 * einer Ebene etwas tut und auf der naechsten nicht. Deshalb wird jeder Weg
 * ZWEIMAL gegangen: hinwaerts ueber Advance (Enter druecken) und
 * rueckwaerts ueber Back (Escape druecken). Beide muessen am selben Anfang
 * und Ende landen.
 */
bool FWiesbadenMenuAblaufTest::RunTest(const FString& Parameters)
{
	// -- Das Hauptmenue -----------------------------------------------------
	{
		TArray<FWbMenuEntry> Menue;
		WiesbadenMenu::BuildHauptmenu(Menue);
		TestEqual(TEXT("das Hauptmenue hat vier Eintraege"), Menue.Num(), 4);

		bool bStarten = false;
		bool bOptionen = false;
		bool bBelegung = false;
		bool bBeenden = false;
		for (const FWbMenuEntry& Eintrag : Menue)
		{
			bStarten |= (Eintrag.Action == EWbMenuAction::SpielStarten);
			bOptionen |= (Eintrag.Action == EWbMenuAction::OptionenOeffnen);
			bBelegung |= (Eintrag.Action == EWbMenuAction::BelegungOeffnen);
			bBeenden |= (Eintrag.Action == EWbMenuAction::Beenden);
		}
		TestTrue(TEXT("Spiel starten ist da"), bStarten);
		TestTrue(TEXT("Optionen sind da"), bOptionen);
		TestTrue(TEXT("die Steuerungsseite ist da"), bBelegung);
		TestTrue(TEXT("Beenden ist da"), bBeenden);

		// Jeder Eintrag traegt eine Beschriftung, die man auch im Spiel
		// wiederfindet - ein leeres Feld waere eine Auswahl ohne Namen.
		for (const FWbMenuEntry& Eintrag : Menue)
		{
			TestFalse(TEXT("jeder Eintrag heisst etwas"),
				Eintrag.Label.IsEmpty());
		}
	}

	// -- Die beiden Wege aus dem Spiel heraus --------------------------------
	{
		TArray<FWbMenuEntry> Menue;
		WiesbadenMenu::BuildHauptmenu(Menue);

		bool bGleich = false;
		// "Spiel starten" heisst: es liegt nichts mehr ueber dem Spiel.
		TestEqual(TEXT("Spiel starten schliesst das Menue"),
			WiesbadenMenu::Advance(EWbMenuScreen::Titel, &Menue[0], bGleich),
			EWbMenuScreen::MAX);
		TestFalse(TEXT("Spiel starten wechselt nicht die Seite"), bGleich);
		// "Beenden" heisst dasselbe - das Spiel ist danach zu.
		TestEqual(TEXT("Beenden schliesst das Menue"),
			WiesbadenMenu::Advance(EWbMenuScreen::Titel, &Menue[3], bGleich),
			EWbMenuScreen::MAX);
	}

	// -- Hinab in die Optionen, wieder hinauf -------------------------------
	{
		TArray<FString> Busse = { TEXT("Gesamt"), TEXT("Musik"), TEXT("Effekte") };
		TArray<FWbOptionRow> Rows;
		WiesbadenOptions::BuildRows(Busse.Num(), Busse, Rows);

		TArray<FWbMenuEntry> Gruppen;
		WiesbadenMenu::BuildGruppenliste(Gruppen);
		// Achtung: UE_ARRAY_COUNT liefert size_t, und int32/size_t macht die
		// TestEqual-Ueberladungen mehrdeutig - deshalb der explizite int32.
		TestEqual(TEXT("sechs Gruppen plus Zurueck"), Gruppen.Num(),
			static_cast<int32>(UE_ARRAY_COUNT(AlleGruppen)) + 1);

		// Reihenfolge und Vollstaendigkeit: die Gruppen stehen in der
		// Reihenfolge, in der der Spieler sie liest.
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(AlleGruppen); ++Index)
		{
			const FWbMenuEntry& Eintrag = Gruppen[Index];
			TestEqual(TEXT("die Gruppe steht an ihrer Stelle"), Eintrag.Group,
				AlleGruppen[Index]);
			TestEqual(TEXT("und oeffnet sich auch"), Eintrag.Action,
				EWbMenuAction::GruppeOeffnen);
		}
		TestEqual(TEXT("der letzte Eintrag ist Zurueck"), Gruppen.Last().Action,
			EWbMenuAction::Zurueck);

		// Jede angebotene Gruppe hat auch etwas zu zeigen. Eine leere
		// Unterseite waere ein Bildschirm, in dem kein Knopf zu druecken ist.
		for (const EWbOptionGroup Gruppe : AlleGruppen)
		{
			TArray<FWbMenuEntry> Seite;
			WiesbadenMenu::BuildGruppe(Gruppe, Rows, Seite);
			int32 Werte = 0;
			for (const FWbMenuEntry& Eintrag : Seite)
			{
				Werte += (Eintrag.Kind == EWbMenuEntryKind::Wert) ? 1 : 0;
			}
			TestTrue(TEXT("jede Gruppe hat mindestens einen Regler"), Werte > 0);
			// Und sie endet auf Zurueck - nicht auf einem Absender.
			TestEqual(TEXT("jede Gruppe endet mit Zurueck"), Seite.Last().Action,
				EWbMenuAction::Zurueck);
		}

		// Der Abstieg als Kette: Titel -> Optionen -> Gruppe -> Optionen.
		bool bGleich = false;
		const FWbMenuEntry* Ton = &Gruppen[0];
		EWbMenuScreen Bildschirm = WiesbadenMenu::Advance(
			EWbMenuScreen::Titel, Ton, bGleich);
		TestEqual(TEXT("eine Gruppe oeffnet die Unterseite"), Bildschirm,
			EWbMenuScreen::Gruppe);

		TArray<FWbMenuEntry> Seite;
		WiesbadenMenu::BuildGruppe(AlleGruppen[0], Rows, Seite);

		// Ein Regler verschiebt den Wert, nicht die Seite.
		int32 ErsterWert = INDEX_NONE;
		for (int32 Index = 0; Index < Seite.Num(); ++Index)
		{
			if (Seite[Index].Kind == EWbMenuEntryKind::Wert)
			{
				ErsterWert = Index;
				break;
			}
		}
		TestTrue(TEXT("die Gruppe hat einen Regler"), ErsterWert != INDEX_NONE);
		bGleich = false;
		TestEqual(TEXT("ein Regler laesst die Seite stehen"),
			WiesbadenMenu::Advance(EWbMenuScreen::Gruppe, &Seite[ErsterWert], bGleich),
			EWbMenuScreen::Gruppe);
		TestTrue(TEXT("und meldet, dass sich nur der Wert aendert"), bGleich);
	}

	// -- Zurueck, Bildschirm fuer Bildschirm ---------------------------------
	{
		EWbMenuScreen Bildschirm = EWbMenuScreen::Intro;
		TestTrue(TEXT("aus dem Intro fuehrt zurueck zum Titel"),
			WiesbadenMenu::Back(Bildschirm));
		TestEqual(TEXT("und landet auf dem Titel"), Bildschirm, EWbMenuScreen::Titel);

		Bildschirm = EWbMenuScreen::Optionen;
		WiesbadenMenu::Back(Bildschirm);
		TestEqual(TEXT("aus den Optionen fuehrt zurueck zum Hauptmenue"),
			Bildschirm, EWbMenuScreen::Titel);

		Bildschirm = EWbMenuScreen::Gruppe;
		WiesbadenMenu::Back(Bildschirm);
		TestEqual(TEXT("aus einer Gruppe fuehrt zurueck zur Gruppenliste"),
			Bildschirm, EWbMenuScreen::Optionen);

		Bildschirm = EWbMenuScreen::Belegung;
		WiesbadenMenu::Back(Bildschirm);
		TestEqual(TEXT("aus der Belegung fuehrt zurueck zum Hauptmenue"),
			Bildschirm, EWbMenuScreen::Titel);

		// Und aus dem Hauptmenue fuehrt zurueck hinaus - nicht in sich
		// selbst, wo Escape nichts mehr bewirken wuerde.
		Bildschirm = EWbMenuScreen::Titel;
		WiesbadenMenu::Back(Bildschirm);
		TestEqual(TEXT("aus dem Hauptmenue fuehrt zurueck aus dem Menue"),
			Bildschirm, EWbMenuScreen::MAX);

		// "MAX" ist schon das Ende: noch ein Escape darf nichts aendern.
		Bildschirm = EWbMenuScreen::MAX;
		TestFalse(TEXT("am Ende aendert zurueck nichts"),
			WiesbadenMenu::Back(Bildschirm));
		TestEqual(TEXT("und es bleibt am Ende"), Bildschirm, EWbMenuScreen::MAX);
	}

	// -- Die Auswahl wandert und bleibt haengen --------------------------------
	{
		TArray<FWbMenuEntry> Menue;
		WiesbadenMenu::BuildHauptmenu(Menue);
		// Eine Hinweiszeile dazwischen: sie ist nicht anwaehlbar, die
		// Auswahl muss darueber springen statt auf ihr stehen zu bleiben.
		FWbMenuEntry Hinweis;
		Hinweis.Kind = EWbMenuEntryKind::Hinweis;
		Hinweis.Label = TEXT("Speichert automatisch");
		Menue.Insert(Hinweis, 1);

		const int32 Index2 = WiesbadenMenu::MoveSelection(0, Menue, 1);
		TestTrue(TEXT("die Auswahl ueberspringt die Hinweiszeile"),
			Menue[Index2].Kind != EWbMenuEntryKind::Hinweis);

		// Einmal um die ganze Liste: jeder anwaehlbare Eintrag genau einmal.
		TSet<int32> Besucht;
		int32 Pos = 0;
		for (int32 Schritt = 0; Schritt < Menue.Num() + 2; ++Schritt)
		{
			Pos = WiesbadenMenu::MoveSelection(Pos, Menue, 1);
			Besucht.Add(Pos);
		}
		TestEqual(TEXT("ein Umlauf besucht jeden anwaehlbaren Eintrag einmal"),
			Besucht.Num(), Menue.Num() - 1);

		// Und rueckwaerts kommt man wieder dort hin, wo man war.
		int32 Zurueck = Pos;
		for (int32 Schritt = 0; Schritt < Besucht.Num(); ++Schritt)
		{
			Zurueck = WiesbadenMenu::MoveSelection(Zurueck, Menue, -1);
		}
		TestEqual(TEXT("rueckwaerts laeuft derselbe Ring"), Zurueck, Pos);
	}

	// -- Die Config-Seite traegt die Handgriffe, die keine Regler sind -------
	{
		TArray<FString> Busse = { TEXT("Gesamt") };
		TArray<FWbOptionRow> Rows;
		WiesbadenOptions::BuildRows(Busse.Num(), Busse, Rows);
		TArray<FWbMenuEntry> Seite;
		WiesbadenMenu::BuildGruppe(EWbOptionGroup::Spielwelt, Rows, Seite);

		bool bSpeichern = false;
		bool bZuruecksetzen = false;
		bool bUeber = false;
		for (const FWbMenuEntry& Eintrag : Seite)
		{
			bSpeichern |= (Eintrag.Action == EWbMenuAction::OptionenSpeichern);
			bZuruecksetzen |= (Eintrag.Action == EWbMenuAction::OptionenZuruecksetzen);
			bUeber |= (Eintrag.Action == EWbMenuAction::Ueber);
		}
		TestTrue(TEXT("die Config-Seite kann speichern"), bSpeichern);
		TestTrue(TEXT("die Config-Seite kann zuruecksetzen"), bZuruecksetzen);
		TestTrue(TEXT("die Config-Seite nennt das Spiel"), bUeber);
	}

	// -- Jede Bildschirm-Ueberschrift ist ein Text ----------------------------
	{
		for (int32 Index = 0; Index < static_cast<int32>(EWbMenuScreen::MAX); ++Index)
		{
			const FString Titel = WiesbadenMenu::ScreenTitle(
				static_cast<EWbMenuScreen>(Index));
			TestFalse(TEXT("jeder Bildschirm traegt eine Ueberschrift"),
				Titel.IsEmpty());
		}
		TestFalse(TEXT("und die nennt das Studio"),
			WiesbadenMenu::ScreenTitle(EWbMenuScreen::Intro).IsEmpty());
	}

	// Die Versionszeile nennt Build UND Engine - eine Angabe, die beim
	// Fehlersuchen mehr wert ist als das Wort "Version".
	TestTrue(TEXT("die Versionszeile nennt die Engine"),
		WiesbadenMenu::VersionLine().Contains(TEXT("Wiesbaden Real")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWiesbadenMenuIntroTest,
	"WiesbadenReal.UI.Menu.Intro",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Der Takt des Intros - und vor allem seine Sperrregel.
 *
 * Die Sperrregel ist das Wichtigste hier: ein wartender Titelbildschirm
 * haengt den Rauchtest, der auf Belege aus dem laufenden Spiel wartet. Der
 * Test prueft deshalb JEDE Verbot-Ueberschrift einzeln - eine Regel, die nur
 * bei einer davon greift, ist eine, die man beim Naechsten Erweitern verliert.
 */
bool FWiesbadenMenuIntroTest::RunTest(const FString& Parameters)
{
	// Drei Phasen hintereinander, danach ist Schluss.
	TestEqual(TEXT("das Intro dauert 6,8 Sekunden"),
		WiesbadenMenu::IntroSeconds(), 6.8);

	FString Zeile;
	float Deckkraft = 0.0f;

	// Am Anfang steht der erste Satz, noch eingeblendet.
	TestTrue(TEXT("am Anfang laeuft das Intro"),
		WiesbadenMenu::IntroPhase(0.0, Zeile, Deckkraft));
	TestEqual(TEXT("mit dem Studio-Namen"), Zeile,
		FString(TEXT("SEBBO'S CHAOTIKKK GAMESTUDIO")));
	TestEqual(TEXT("am Anfang noch schwarz"), Deckkraft, 0.0f);

	// Nach der Einblendung steht er voll da.
	TestTrue(TEXT("nach der Einblendung laeuft es weiter"),
		WiesbadenMenu::IntroPhase(0.4, Zeile, Deckkraft));
	TestEqual(TEXT("und ist deckend"), Deckkraft, 1.0f);

	// In der Mitte der Phase steht er voll da - die Deckkraft ist in der
	// Mitte HOCH, an den Raendern fast null (Blenden je 0,40 s bei 2,40 s
	// Phasendauer). "Halb eingeblendet" gibt es deshalb nur an den Raendern,
	// nicht in der Mitte: hier stand vorher 0,5 erwartet und 1,0 gemeldet.
	TestTrue(TEXT("in der Mitte laeuft es"),
		WiesbadenMenu::IntroPhase(1.2, Zeile, Deckkraft));
	TestEqual(TEXT("und ist in der Mitte deckend"), Deckkraft, 1.0f);

	// Die Haelfte der Blende: mitten im Ein- und im Ausblenden. Ohne diese
	// beiden Proben koennte die Deckkraft in der Mitte Spruenge machen und
	// trotzdem oben durchfallen.
	TestTrue(TEXT("die Einblendung laeuft noch"),
		WiesbadenMenu::IntroPhase(0.2, Zeile, Deckkraft));
	TestEqual(TEXT("halb eingeblendet"), Deckkraft, 0.5f);
	TestTrue(TEXT("die Ausblendung laeuft schon"),
		WiesbadenMenu::IntroPhase(2.2, Zeile, Deckkraft));
	TestEqual(TEXT("und wieder halb"), Deckkraft, 0.5f);

	// Die zweite Phase laeuft, die erste ist vorbei.
	TestTrue(TEXT("die zweite Phase laeuft"),
		WiesbadenMenu::IntroPhase(2.5, Zeile, Deckkraft));
	TestEqual(TEXT("mit dem Verbindungswort"), Zeile, FString(TEXT("praesentiert")));

	// Die dritte Phase nennt das Spiel.
	TestTrue(TEXT("die dritte Phase laeuft"),
		WiesbadenMenu::IntroPhase(5.0, Zeile, Deckkraft));
	TestEqual(TEXT("mit dem Spielnamen"), Zeile, FString(TEXT("WIESBADEN REAL")));

	// Danach ist Schluss - das Intro darf nicht ewig laufen.
	TestFalse(TEXT("am Ende ist Schluss"),
		WiesbadenMenu::IntroPhase(WiesbadenMenu::IntroSeconds(), Zeile, Deckkraft));
	TestTrue(TEXT("und der Bildschirm leert sich"), Zeile.IsEmpty());
	TestEqual(TEXT("ohne Restdeckkraft"), Deckkraft, 0.0f);

	// Und eine Zeit vor dem Start ist auch nichts.
	TestFalse(TEXT("vor dem Start ist nichts"),
		WiesbadenMenu::IntroPhase(-1.0, Zeile, Deckkraft));

	// -- Die Sperrregel ------------------------------------------------------
	TestTrue(TEXT("ohne Verbote laeuft das Intro"),
		WiesbadenMenu::ShouldShowIntro(true, TEXT("")));
	TestTrue(TEXT("und auch neben anderen Schaltern"),
		WiesbadenMenu::ShouldShowIntro(true, TEXT("-log -windowed")));

	// Jedes Verbot einzeln - sie duerfen sich nicht gegenseitig ersetzen.
	TestFalse(TEXT("-unattended unterdrueckt das Intro"),
		WiesbadenMenu::ShouldShowIntro(true, TEXT("-unattended")));
	TestFalse(TEXT("-nullrhi unterdrueckt das Intro"),
		WiesbadenMenu::ShouldShowIntro(true, TEXT("-nullrhi")));
	TestFalse(TEXT("-ExecCmds unterdrueckt das Intro"),
		WiesbadenMenu::ShouldShowIntro(true, TEXT("-ExecCmds=\"WbHealth\"")));
	TestFalse(TEXT("-WbKeinIntro unterdrueckt das Intro"),
		WiesbadenMenu::ShouldShowIntro(true, TEXT("-WbKeinIntro")));
	// Auch mitten im Text, nicht nur am Anfang.
	TestFalse(TEXT("auch als Teil eines anderen Schalters"),
		WiesbadenMenu::ShouldShowIntro(true, TEXT("-Game -nullrhi -nosplash")));

	// Und unabhaengig davon, was der Spieler eingestellt hat.
	TestFalse(TEXT("ausgeschaltet laeuft es ohnehin nicht"),
		WiesbadenMenu::ShouldShowIntro(false, TEXT("")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWiesbadenMenuBelegungTest,
	"WiesbadenReal.UI.Menu.Belegung",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Die Belegungs-Seite ist eine Tabelle, und eine Tabelle muss in sich
 * stimmen: kein Kontext darf eine Pad-Taste zweimal vergeben haben, und
 * keine Zeile darf fuer den Spieler leer bleiben.
 *
 * Der Doppelpunkt-Fall ist keine Theorie: am Hubschrauber lag Y gleichzeitig
 * auf "Motor an/aus" und auf "Ein-/Aussteigen" - zwei Knoepfe, eine Wirkung,
 * und der Spieler haette nie herausgefunden, welche. Genau darum wird es
 * hier geprueft und nicht nur hoffentlich richtig gebaut.
 */
bool FWiesbadenMenuBelegungTest::RunTest(const FString& Parameters)
{
	TArray<FString> Labels;
	WiesbadenMenu::GetContextLabels(Labels);
	TestEqual(TEXT("es gibt einen Reiter je Kontext"), Labels.Num(),
		static_cast<int32>(EWbControlContext::MAX));
	for (const FString& Label : Labels)
	{
		TestFalse(TEXT("jeder Reiter heisst etwas"), Label.IsEmpty());
	}

	for (int32 Index = 0; Index < static_cast<int32>(EWbControlContext::MAX); ++Index)
	{
		const EWbControlContext Kontext = static_cast<EWbControlContext>(Index);
		TArray<FWbControlBinding> Zeilen;
		WiesbadenMenu::ControlBindings(Kontext, Zeilen);

		TestTrue(TEXT("jeder Kontext hat Zeilen"), Zeilen.Num() > 0);

		// Die vier Punkte, an denen eine Belegungsseite im Spiel entgleist:
		// ohne Namen, ohne Pad-Taste, doppelte Taste, doppelter Knopf.
		TSet<FString> GeseheneTasten;
		TSet<FString> GesehenePad;
		for (const FWbControlBinding& Zeile : Zeilen)
		{
			TestFalse(TEXT("jede Aktion heisst etwas"), Zeile.Aktion.IsEmpty());
			TestFalse(TEXT("jede Aktion hat eine Tastaturbelegung"), Zeile.Taste.IsEmpty());
			// "-" ist die ehrliche Antwort: es gibt dafuer keine Taste am Pad.
			TestFalse(TEXT("jede Aktion nennt ihre Pad-Belegung"), Zeile.Pad.IsEmpty());

			// TSet::Add gibt ein Element-Handle zurueck, kein bool - die
			// Frage lautet also erst "stand er schon da?".
			const bool bTasteFrei = !GeseheneTasten.Contains(Zeile.Taste);
			TestTrue(TEXT("keine Tastaturtaste doppelt im selben Kontext"), bTasteFrei);
			GeseheneTasten.Add(Zeile.Taste);
			if (!bTasteFrei)
			{
				AddError(FString::Printf(TEXT("Taste '%s' steht zweimal da (Kontext %d, Aktion '%s')."),
					*Zeile.Taste, Index, *Zeile.Aktion));
			}

			// Der Knopfvergleich gilt nur fuer echte Knopfangaben; die Mehrfach-
			// angaben wie "LB / RB" sind je eine Zeile fuer zwei Knoepfe.
			TArray<FString> Knoepfe;
			Zeile.Pad.ParseIntoArray(Knoepfe, TEXT(", "), /*bCullEmpty=*/true);
			for (const FString& Knoepf : Knoepfe)
			{
				if (Knoepf == TEXT("-"))
				{
					continue;
				}
				const bool bKnoepfFrei = !GesehenePad.Contains(Knoepf);
				TestTrue(TEXT("kein Knopf doppelt im selben Kontext"), bKnoepfFrei);
				GesehenePad.Add(Knoepf);
				if (!bKnoepfFrei)
				{
					AddError(FString::Printf(TEXT("Knopf '%s' steht zweimal da (Kontext %d, Aktion '%s')."),
						*Knoepf, Index, *Zeile.Aktion));
				}
			}
		}

		// Die Zeile als Ganzes: die Spalten muessen stehen, sonst springt die
		// Tabelle beim Lesen.
		for (const FWbControlBinding& Zeile : Zeilen)
		{
			const FString Text = FormatBindingLine(Zeile);
			TestTrue(TEXT("die Zeile traegt die Aktion"), Text.Contains(Zeile.Aktion));
			TestTrue(TEXT("die Zeile traegt die Taste"), Text.Contains(Zeile.Taste));
			// Die Knopfangabe steht GANZ RECHTS - so, wie man in einer
			// Tabelle nach ihr sucht. Ein "enthaelt" waere hier zu schwach:
			// der Bindestrich der Zeilen ohne Pad-Belegung steckt in
			// fast jeder Beschriftung.
			TestTrue(TEXT("die Zeile endet auf der Knopfangabe"), Text.EndsWith(Zeile.Pad));
			TestTrue(TEXT("die Knopfspalte beginnt erst nach der Tastaturspalte"),
				Text.Len() >= 56);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWiesbadenMenuWarpZielTest,
	"WiesbadenReal.UI.Menu.WarpZiel",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Wohin der Sprung auf eine Strasse geht - und welche Namen das Suchfeld
 * vorschlagen darf.
 *
 * Der Zielpunkt ist bewusst NICHT der Schwerpunkt aller Mittellinien: bei
 * einer Strasse aus fuenf Segmenten laege der dann mitten in der Wiese
 * nebenan, und man stuende im Feld statt auf der Fahrbahn. Gesucht ist der
 * Mittelpunkt des laengsten Segments.
 */
bool FWiesbadenMenuWarpZielTest::RunTest(const FString& Parameters)
{
	// -- Der Zielpunkt liegt auf dem LAENGSTEN Segment -----------------------
	{
		FRoadNetwork Netz;
		// Derselbe Name, zweimal: einmal kurz, einmal lang und woanders.
		Netz.Segments.Add(MakeSegment(0, TEXT("Wilhelmstrasse"), {
			FVector(0.0, 0.0, 100.0), FVector(1000.0, 0.0, 100.0) }));
		Netz.Segments.Add(MakeSegment(1, TEXT("Wilhelmstrasse"), {
			FVector(90000.0, 5000.0, 140.0), FVector(100000.0, 5000.0, 140.0) }));

		FVector2D Ziel = FVector2D::ZeroVector;
		float Yaw = 0.0f;
		double Z = 0.0;
		TestTrue(TEXT("die Strasse wird gefunden"),
			FWiesbadenMinimap::FindStreetWarpTarget(Netz, TEXT("Wilhelmstrasse"), Ziel, Yaw, Z));
		// Das LANGE Segment, nicht das kurze: 95 000 m, nicht 1 000 m.
		TestEqual(TEXT("der Zielpunkt liegt auf dem laengsten Segment"),
			Ziel, FVector2D(95000.0, 5000.0));
		// Die Strasse traegt ihre eigene Hoehe mit.
		TestEqual(TEXT("die Hoehe kommt aus der Mittellinie"), Z, 140.0);
		// Und die Richtung: nach Osten, 0 Grad.
		TestEqual(TEXT("die Richtung stimmt"), Yaw, 0.0f);
	}

	// -- Die Richtung folgt der Fahrbahn, nicht dem Zufall --------------------
	{
		FRoadNetwork Netz;
		// Von West nach Ost: 0 Grad.
		Netz.Segments.Add(MakeSegment(0, TEXT("Oststrasse"), {
			FVector(0.0, 0.0, 0.0), FVector(4000.0, 0.0, 0.0) }));
		FVector2D Ziel = FVector2D::ZeroVector;
		float Yaw = 0.0f;
		double Z = 0.0;
		FWiesbadenMinimap::FindStreetWarpTarget(Netz, TEXT("Oststrasse"), Ziel, Yaw, Z);
		TestEqual(TEXT("nach Osten ist 0 Grad"), Yaw, 0.0f);

		// Nach Norden: +90 Grad im Uhrzeigersinn.
		FRoadNetwork Norden;
		Norden.Segments.Add(MakeSegment(0, TEXT("Nordstrasse"), {
			FVector(0.0, 0.0, 0.0), FVector(0.0, 4000.0, 0.0) }));
		FWiesbadenMinimap::FindStreetWarpTarget(Norden, TEXT("Nordstrasse"), Ziel, Yaw, Z);
		TestEqual(TEXT("nach Norden ist 90 Grad"), Yaw, 90.0f);

		// Von Osten nach Westen: 180 Grad. Ohne diese Probe koennte die
		// Richtung spiegelverkehrt sein und trotzdem "irgendwie stimmen".
		FRoadNetwork Westen;
		Westen.Segments.Add(MakeSegment(0, TEXT("Weststrasse"), {
			FVector(4000.0, 0.0, 0.0), FVector(0.0, 0.0, 0.0) }));
		FWiesbadenMinimap::FindStreetWarpTarget(Westen, TEXT("Weststrasse"), Ziel, Yaw, Z);
		TestEqual(TEXT("nach Westen ist 180 Grad"), Yaw, 180.0f);
	}

	// -- Ein S-Bogen: die Richtung darf nicht aus allen Punkten gemittelt sein -
	{
		// Erst nach Norden, dann nach Osten - begane und ende sich fast auf.
		FRoadNetwork Netz;
		Netz.Segments.Add(MakeSegment(0, TEXT("Bogenstrasse"), {
			FVector(0.0, 0.0, 0.0),
			FVector(0.0, 2000.0, 0.0),
			FVector(0.0, 2000.0, 0.0),
			FVector(2000.0, 2000.0, 0.0) }));
		FVector2D Ziel = FVector2D::ZeroVector;
		float Yaw = 0.0f;
		double Z = 0.0;
		TestTrue(TEXT("der Bogen wird gefunden"),
			FWiesbadenMinimap::FindStreetWarpTarget(Netz, TEXT("Bogenstrasse"), Ziel, Yaw, Z));
		// Der Mittelpunkt der Strecke liegt auf dem Knick: 2 000 m nach
		// Norden und 2 000 m nach Osten, also bei der halben Laenge genau
		// am Wendepunkt (0, 2000). Stand hier vorher (1000, 1000) - das ist
		// die Mitte der Diagonalen und liegt in der Wiese, also weder auf
		// der Strasse noch das, was der Kommentar darunter beschreibt.
		TestEqual(TEXT("der Knick ist die Mitte"), Ziel, FVector2D(0.0, 2000.0));
		// Und die Richtung kommt aus dem letzten Drittel: nach Osten.
		TestTrue(TEXT("die Richtung zeigt entlang des Bogens"), Yaw > -1.0f && Yaw < 1.0f);
	}

	// -- Ungleichmaessige Punktdichte -----------------------------------------
	{
		// Drei Stuetzpunkte liegen dicht beieinander, der vierte weit weg.
		// Der Mittelwert der Stuetzpunkte waere 2 060 m - die Mitte der
		// Strecke ist aber 5 000 m. Genau dieser Unterschied ist der Grund,
		// warum nach Bogenlaenge abgelaufen und nicht ueber die Punkte
		// gemittelt wird. (Stand hier vorher 1 325,75 m: der Mittelwert der
		// Abschnittsmitten rechnet im Index-Raum, nicht in der Laenge, und
		// rutscht bei dichter Punktfolge nach vorn - auf einem S-Bogen dann
		// aus der Strasse heraus.)
		FRoadNetwork Netz;
		Netz.Segments.Add(MakeSegment(0, TEXT("Dichtstrasse"), {
			FVector(0.0, 0.0, 0.0),
			FVector(100.0, 0.0, 0.0),
			FVector(101.0, 0.0, 0.0),
			FVector(102.0, 0.0, 0.0),
			FVector(10000.0, 0.0, 0.0) }));
		FVector2D Ziel = FVector2D::ZeroVector;
		float Yaw = 0.0f;
		double Z = 0.0;
		FWiesbadenMinimap::FindStreetWarpTarget(Netz, TEXT("Dichtstrasse"), Ziel, Yaw, Z);
		TestTrue(TEXT("die Mitte der Strecke, nicht der Mittel der Stuetzpunkte"),
			FMath::IsNearlyEqual(Ziel.X, 5000.0, 1.0));
	}

	// -- Namen, Grossschreibung und Trefferarten -----------------------------
	{
		FRoadNetwork Netz;
		Netz.Segments.Add(MakeSegment(0, TEXT("Am Ring"), {
			FVector(0.0, 0.0, 0.0), FVector(2000.0, 0.0, 0.0) }));
		// Langer, damit er beim naechsten Probe den exakten Namen nicht
		// verdraengt - er darf es nur als Teiltreffer erreichen.
		Netz.Segments.Add(MakeSegment(1, TEXT("Am Ringweg"), {
			FVector(50000.0, 0.0, 0.0), FVector(90000.0, 0.0, 0.0) }));

		FVector2D Ziel = FVector2D::ZeroVector;
		float Yaw = 0.0f;
		double Z = 0.0;
		TestTrue(TEXT("der exakte Name gewinnt gegen den laengeren Teiltreffer"),
			FWiesbadenMinimap::FindStreetWarpTarget(Netz, TEXT("Am Ring"), Ziel, Yaw, Z));
		TestEqual(TEXT("und zwar wirklich der exakte"), Ziel, FVector2D(1000.0, 0.0));

		// Gross-/Kleinschreibung ist egal - getippt wird schnell.
		TestTrue(TEXT("die Schreibung ist egal"),
			FWiesbadenMinimap::FindStreetWarpTarget(Netz, TEXT("am ringweg"), Ziel, Yaw, Z));

		// Ein Stueck des Namens genuegt.
		TestTrue(TEXT("auch ein Namensstueck findet die Strasse"),
			FWiesbadenMinimap::FindStreetWarpTarget(Netz, TEXT("Ringweg"), Ziel, Yaw, Z));

		// Was es nicht gibt, gibt es auch nicht - und der Zielpunkt bleibt,
		// was er war. Ein Treffer auf Nichts wuerde den Spieler ins Leere
		// setzen.
		const FVector2D Vorher = Ziel;
		TestFalse(TEXT("eine unbekannte Strasse liefert kein Ziel"),
			FWiesbadenMinimap::FindStreetWarpTarget(Netz, TEXT("Gibtsnicht"), Ziel, Yaw, Z));
		TestEqual(TEXT("und der Zielpunkt bleibt unberuehrt"), Ziel, Vorher);

		// Ein leerer Name ebenso wenig.
		TestFalse(TEXT("ein leerer Name liefert kein Ziel"),
			FWiesbadenMinimap::FindStreetWarpTarget(Netz, TEXT("  "), Ziel, Yaw, Z));

		// Ein namenloser Weg (Feldweg, Zufahrt) ist keine Strasse zum Springen.
		FRoadNetwork OhneNamen;
		OhneNamen.Segments.Add(MakeSegment(0, FString(), {
			FVector(0.0, 0.0, 0.0), FVector(2000.0, 0.0, 0.0) }));
		TestFalse(TEXT("ein namenloser Weg traegt kein Ziel"),
			FWiesbadenMinimap::FindStreetWarpTarget(OhneNamen, TEXT("Feldweg"), Ziel, Yaw, Z));
	}

	// -- Die Vorschlagsliste des Suchfelds ------------------------------------
	{
		FRoadNetwork Netz;
		Netz.Segments.Add(MakeSegment(0, TEXT("Am Ring"), {
			FVector(0.0, 0.0, 0.0), FVector(2000.0, 0.0, 0.0) }));
		// Derselbe Name noch einmal: er darf in der Liste nur einmal stehen,
		// sonst faellt die Liste bei jedem Strassenpaar in sich zusammen.
		Netz.Segments.Add(MakeSegment(1, TEXT("Am Ring"), {
			FVector(0.0, 3000.0, 0.0), FVector(2000.0, 3000.0, 0.0) }));
		Netz.Segments.Add(MakeSegment(2, TEXT("Am Ringweg"), {
			FVector(0.0, 6000.0, 0.0), FVector(2000.0, 6000.0, 0.0) }));
		Netz.Segments.Add(MakeSegment(3, TEXT("Berliner Ring"), {
			FVector(0.0, 9000.0, 0.0), FVector(2000.0, 9000.0, 0.0) }));
		Netz.Segments.Add(MakeSegment(4, TEXT("Wilhelmstrasse"), {
			FVector(0.0, 12000.0, 0.0), FVector(2000.0, 12000.0, 0.0) }));

		TArray<FString> Vorschlaege;

		// Wer "Am" tippt, will die Strassen, die damit BEGINNEN. "Berliner
		// Ring" enthaelt "Am" nicht - "Ring" schon, und das ist eine andere
		// Frage als "Am Ring".
		FWiesbadenMinimap::FindStreetSuggestions(Netz, TEXT("Am"), Vorschlaege);
		TestEqual(TEXT("zwei Namen, die mit 'Am' beginnen"), Vorschlaege.Num(), 2);
		if (Vorschlaege.Num() == 2)
		{
			TestEqual(TEXT("alphabetisch zuerst"), Vorschlaege[0], FString(TEXT("Am Ring")));
			TestEqual(TEXT("dann der zweite"), Vorschlaege[1], FString(TEXT("Am Ringweg")));
		}

		// Derselbe Name steht trotz zweier Segmente nur einmal.
		int32 RingCount = 0;
		for (const FString& Name : Vorschlaege)
		{
			RingCount += (Name.Equals(TEXT("Am Ring"))) ? 1 : 0;
		}
		TestEqual(TEXT("jeder Name steht genau einmal"), RingCount, 1);

		// "Ring" steckt in allen dreien - diesmal in der Reihenfolge, in der
		// sie dastehen, und ohne Luecken.
		FWiesbadenMinimap::FindStreetSuggestions(Netz, TEXT("Ring"), Vorschlaege);
		TestEqual(TEXT("alle drei Ringe"), Vorschlaege.Num(), 3);

		// Die Obergrenze wird eingehalten - sonst laeuft die Liste aus dem
		// Bild.
		FWiesbadenMinimap::FindStreetSuggestions(Netz, TEXT(""), Vorschlaege, 2);
		TestEqual(TEXT("die Liste endet bei der Grenze"), Vorschlaege.Num(), 2);

		// Und es wird nichts erfunden.
		FWiesbadenMinimap::FindStreetSuggestions(Netz, TEXT("Gibtsnicht"), Vorschlaege);
		TestEqual(TEXT("ohne Treffer bleibt die Liste leer"), Vorschlaege.Num(), 0);
	}

	return true;
}
