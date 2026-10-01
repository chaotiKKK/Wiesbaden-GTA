// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "UI/WiesbadenMenuFlow.h"
#include "Core/WiesbadenInputMap.h"

#include "Misc/App.h"
#include "Misc/EngineVersion.h"

namespace
{
	/** Ein Eintrag anlegen - die Kurzform, mit der alle Listen gebaut werden. */
	FWbMenuEntry MakeEntry(EWbMenuEntryKind Kind, EWbMenuAction Action,
		const TCHAR* Label, const TCHAR* Hinweis = TEXT(""),
		EWbOptionId Option = EWbOptionId::MAX,
		EWbOptionGroup Group = EWbOptionGroup::MAX)
	{
		FWbMenuEntry Entry;
		Entry.Kind = Kind;
		Entry.Action = Action;
		Entry.Option = Option;
		Entry.Group = Group;
		Entry.Label = Label;
		Entry.Hinweis = Hinweis;
		return Entry;
	}

	/** Eine Bindungszeile anlegen. */
	FWbControlBinding MakeBinding(EWbControlContext Context, const TCHAR* Aktion,
		const TCHAR* Taste, const TCHAR* Pad)
	{
		FWbControlBinding B;
		B.Context = Context;
		B.Aktion = Aktion;
		B.Taste = Taste;
		B.Pad = Pad;
		return B;
	}

	// --- Der Intro-Ablauf --------------------------------------------------
	//
	// Drei Phasen, jede mit gleich langem Ein- und Ausblenden. Die Zeiten
	// stehen als Konstanten, damit die Rechnung UND die Anzeige daraus
	// dieselben Zahlen nehmen.
	constexpr double Einblenden = 0.40;
	constexpr double Ausblenden = 0.40;
	const double PhasenDauer[3] = { 2.40, 2.00, 2.40 };
	const TCHAR* PhasenText[3] = {
		TEXT("SEBBO'S CHAOTIKKK GAMESTUDIO"),
		TEXT("praesentiert"),
		TEXT("WIESBADEN REAL"),
	};

	/** Deckkraft einer Phase: hoch in der Mitte, an den Raendern fast null. */
	float PhaseDeckkraft(double InPhase, double Dauer)
	{
		if (InPhase < 0.0 || InPhase > Dauer)
		{
			return 0.0f;
		}
		if (InPhase < Einblenden)
		{
			return static_cast<float>(InPhase / Einblenden);
		}
		if (InPhase > Dauer - Ausblenden)
		{
			return static_cast<float>((Dauer - InPhase) / Ausblenden);
		}
		return 1.0f;
	}
}

FString FormatBindingLine(const FWbControlBinding& Binding)
{
	// Feste Spalten: eine Belegung ist eine Tabelle, und eine Tabelle, deren
	// Beschriftungen springen, laesst sich nicht nebeninander lesen.
	const int32 SpalteAktion = 30;
	const int32 SpalteTaste = 26;
	FString Zeile = Binding.Aktion;
	while (Zeile.Len() < SpalteAktion)
	{
		Zeile += TEXT(" ");
	}
	Zeile += Binding.Taste;
	while (Zeile.Len() < SpalteAktion + SpalteTaste)
	{
		Zeile += TEXT(" ");
	}
	Zeile += Binding.Pad;
	return Zeile;
}

FString WiesbadenMenu::ScreenTitle(EWbMenuScreen Screen)
{
	switch (Screen)
	{
	case EWbMenuScreen::Intro:      return TEXT("SEBBO'S CHAOTIKKK GAMESTUDIO");
	case EWbMenuScreen::Titel:      return TEXT("WIESBADEN REAL");
	case EWbMenuScreen::Optionen:   return TEXT("OPTIONEN");
	case EWbMenuScreen::Gruppe:     return TEXT("OPTIONEN");
	case EWbMenuScreen::Belegung:   return TEXT("STEUERUNG");
	default:                        return FString();
	}
}

void WiesbadenMenu::BuildHauptmenu(TArray<FWbMenuEntry>& Out)
{
	Out.Reset();
	Out.Add(MakeEntry(EWbMenuEntryKind::Aktion, EWbMenuAction::SpielStarten,
		TEXT("Spiel starten"), TEXT("Die Stadt betreten.")));
	Out.Add(MakeEntry(EWbMenuEntryKind::Aktion, EWbMenuAction::OptionenOeffnen,
		TEXT("Optionen"), TEXT("Sound, Grafik, Steuerung, Bild, Debug und Config.")));
	Out.Add(MakeEntry(EWbMenuEntryKind::Aktion, EWbMenuAction::BelegungOeffnen,
		TEXT("Steuerung"), TEXT("Alle Tasten und alle Tasten am Controller.")));
	Out.Add(MakeEntry(EWbMenuEntryKind::Aktion, EWbMenuAction::Beenden,
		TEXT("Beenden"), TEXT("Das Spiel schliessen.")));
}

void WiesbadenMenu::BuildGruppenliste(TArray<FWbMenuEntry>& Out)
{
	Out.Reset();
	Out.Add(MakeEntry(EWbMenuEntryKind::Aktion, EWbMenuAction::GruppeOeffnen,
		TEXT("Sound"), TEXT("Lautstaerken des Mischpults."), EWbOptionId::MAX, EWbOptionGroup::Ton));
	Out.Add(MakeEntry(EWbMenuEntryKind::Aktion, EWbMenuAction::GruppeOeffnen,
		TEXT("Grafik"), TEXT("Sichtweite, Schatten, Effekte, Texturen."), EWbOptionId::MAX, EWbOptionGroup::Grafik));
	Out.Add(MakeEntry(EWbMenuEntryKind::Aktion, EWbMenuAction::GruppeOeffnen,
		TEXT("Steuerung"), TEXT("Maus und Steuerungshilfe."), EWbOptionId::MAX, EWbOptionGroup::Steuerung));
	Out.Add(MakeEntry(EWbMenuEntryKind::Aktion, EWbMenuAction::GruppeOeffnen,
		TEXT("Bild"), TEXT("Bildrate, Vollbild, V-Sync, Aufloesung."), EWbOptionId::MAX, EWbOptionGroup::Bild));
	Out.Add(MakeEntry(EWbMenuEntryKind::Aktion, EWbMenuAction::GruppeOeffnen,
		TEXT("Debug"), TEXT("Bilder je Sekunde, Statistik, Kollisionsboxen."), EWbOptionId::MAX, EWbOptionGroup::Debug));
	Out.Add(MakeEntry(EWbMenuEntryKind::Aktion, EWbMenuAction::GruppeOeffnen,
		TEXT("Config"), TEXT("Verkehr, Tageszeit, Einstellungen speichern."), EWbOptionId::MAX, EWbOptionGroup::Spielwelt));
	Out.Add(MakeEntry(EWbMenuEntryKind::Zurueck, EWbMenuAction::Zurueck,
		TEXT("Zurueck"), TEXT("Zurueck zum Hauptmenue.")));
}

void WiesbadenMenu::BuildGruppe(EWbOptionGroup Gruppe,
	const TArray<FWbOptionRow>& Rows, TArray<FWbMenuEntry>& Out)
{
	Out.Reset();
	for (const FWbOptionRow& Row : Rows)
	{
		if (Row.Group != Gruppe)
		{
			continue;
		}
		Out.Add(MakeEntry(EWbMenuEntryKind::Wert, EWbMenuAction::WertAendern,
			*Row.Label, *Row.Hinweis, Row.Id));
	}

	// Die Config-Seite traegt zusaetzlich die Handgriffe, die keine Einstellung
	// sind, sondern eine Folge daraus: speichern, zuruecksetzen, und die
	// Angabe, worum es sich handelt. Sie stehen unten, nicht zwischen den
	// Reglern - wer eine Einstellung aendert, soll nicht darauf treten.
	if (Gruppe == EWbOptionGroup::Spielwelt)
	{
		Out.Add(MakeEntry(EWbMenuEntryKind::Aktion, EWbMenuAction::OptionenSpeichern,
			TEXT("Einstellungen speichern"),
			TEXT("Schreibt alle Regler in die Datei neben dem Spiel.")));
		Out.Add(MakeEntry(EWbMenuEntryKind::Aktion, EWbMenuAction::OptionenZuruecksetzen,
			TEXT("Einstellungen zuruecksetzen"),
			TEXT("Alle Regler auf den Auslieferungszustand.")));
		Out.Add(MakeEntry(EWbMenuEntryKind::Aktion, EWbMenuAction::Ueber,
			TEXT("Ueber das Spiel"), TEXT("Name, Engine, Build.")));
	}

	Out.Add(MakeEntry(EWbMenuEntryKind::Zurueck, EWbMenuAction::Zurueck,
		TEXT("Zurueck"), TEXT("Zurueck zur Gruppenliste.")));
}

const FWbMenuEntry* WiesbadenMenu::EntryAt(
	const TArray<FWbMenuEntry>& Entries, int32 Index)
{
	return Entries.IsValidIndex(Index) ? &Entries[Index] : nullptr;
}

int32 WiesbadenMenu::MoveSelection(
	int32 Current, const TArray<FWbMenuEntry>& Entries, int32 Richtung)
{
	if (Entries.Num() == 0)
	{
		return 0;
	}
	const int32 Schritt = (Richtung >= 0) ? 1 : -1;
	int32 Index = FMath::Clamp(Current, 0, Entries.Num() - 1);
	// Hinweiszeilen sind nicht anwaehlbar: die Auswahl springt darueber.
	// Hoechstens so oft wie Zeilen da sind, damit eine Schleife sicher endet.
	for (int32 Versuch = 0; Versuch <= Entries.Num(); ++Versuch)
	{
		Index = ((Index + Schritt) % Entries.Num() + Entries.Num()) % Entries.Num();
		if (Entries[Index].Kind != EWbMenuEntryKind::Hinweis)
		{
			return Index;
		}
	}
	return FMath::Clamp(Current, 0, Entries.Num() - 1);
}

EWbMenuScreen WiesbadenMenu::Advance(
	EWbMenuScreen Screen, const FWbMenuEntry* Entry, bool& bSameScreen)
{
	bSameScreen = false;
	if (!Entry)
	{
		return Screen;
	}
	switch (Entry->Action)
	{
	case EWbMenuAction::WertAendern:
		// Die Seite bleibt, aber der Wert aendert sich: das ist der einzige
		// Fall, in dem Enter nichts vom Bildschirm weiterschiebt.
		bSameScreen = true;
		return Screen;

	case EWbMenuAction::SpielStarten:
	case EWbMenuAction::Beenden:
		// Beides heisst: nichts liegt mehr ueber dem Spiel. MAX ist der
		// Bildschirm "keiner" - derselbe Wert, mit dem Optionen ihr Ende
		// markieren.
		return EWbMenuScreen::MAX;

	case EWbMenuAction::OptionenOeffnen:  return EWbMenuScreen::Optionen;
	case EWbMenuAction::BelegungOeffnen:  return EWbMenuScreen::Belegung;
	case EWbMenuAction::GruppeOeffnen:    return EWbMenuScreen::Gruppe;
	case EWbMenuAction::Zurueck:
		{
			EWbMenuScreen Zurueck = Screen;
			Back(Zurueck);
			return Zurueck;
		}
	default:
		return Screen;
	}
}

bool WiesbadenMenu::Back(EWbMenuScreen& Screen)
{
	const EWbMenuScreen Vorher = Screen;
	switch (Screen)
	{
	case EWbMenuScreen::Intro:     Screen = EWbMenuScreen::Titel;     break;
	case EWbMenuScreen::Titel:     Screen = EWbMenuScreen::MAX;       break;
	case EWbMenuScreen::Optionen:  Screen = EWbMenuScreen::Titel;     break;
	case EWbMenuScreen::Gruppe:    Screen = EWbMenuScreen::Optionen;  break;
	case EWbMenuScreen::Belegung:  Screen = EWbMenuScreen::Titel;     break;
	default: break;
	}
	return Screen != Vorher;
}

double WiesbadenMenu::IntroSeconds()
{
	double Summe = 0.0;
	for (double Dauer : PhasenDauer)
	{
		Summe += Dauer;
	}
	return Summe;
}

bool WiesbadenMenu::IntroPhase(
	double Sekunden, FString& OutZeile, float& OutDeckkraft)
{
	OutZeile.Empty();
	OutDeckkraft = 0.0f;

	if (Sekunden < 0.0 || Sekunden >= IntroSeconds())
	{
		return false;
	}
	double Rest = Sekunden;
	for (int32 Phase = 0; Phase < 3; ++Phase)
	{
		if (Rest < PhasenDauer[Phase])
		{
			OutZeile = PhasenText[Phase];
			OutDeckkraft = PhaseDeckkraft(Rest, PhasenDauer[Phase]);
			return true;
		}
		Rest -= PhasenDauer[Phase];
	}
	return false;
}

bool WiesbadenMenu::ShouldShowIntro(bool bEinstellungAn, const FString& Kommandozeile)
{
	if (!bEinstellungAn)
	{
		return false;
	}
	// In einem Automationslauf oder einem expliziten BugTank-Start waere ein
	// wartender Titelbildschirm ein Hindernis fuer den angeforderten Lauf.
	static const TCHAR* Verbote[] = {
		TEXT("-unattended"), TEXT("-nullrhi"), TEXT("-ExecCmds"),
		TEXT("-WbKeinIntro"), TEXT("-WbBugTank") };
	for (const TCHAR* Verbot : Verbote)
	{
		if (Kommandozeile.Contains(Verbot, ESearchCase::IgnoreCase))
		{
			return false;
		}
	}
	return true;
}

FString WiesbadenMenu::VersionLine()
{
	return FString::Printf(TEXT("Wiesbaden Real - %s"), *FEngineVersion::Current().ToString());
}

void WiesbadenMenu::GetContextLabels(TArray<FString>& OutLabels)
{
	OutLabels.Reset();
	OutLabels.Add(TEXT("Fahrzeug"));
	OutLabels.Add(TEXT("Zu Fuss"));
	OutLabels.Add(TEXT("Helikopter"));
	OutLabels.Add(TEXT("Karte"));
	OutLabels.Add(TEXT("Menue"));
}

void WiesbadenMenu::ControlBindings(
	EWbControlContext Context, TArray<FWbControlBinding>& Out)
{
	Out.Reset();
	auto Add = [&Out, Context](const TCHAR* Aktion, const TCHAR* Taste, const TCHAR* Pad)
	{
		Out.Add(MakeBinding(Context, Aktion, Taste, Pad));
	};

	switch (Context)
	{
	case EWbControlContext::Fahrzeug:
		// Die Buchstaben der Tastatur sind absichtlich die Buchstaben der
		// vier Gesichtsknoepfe: A hupt wie am Pad, B bremst wie am Pad. Wer
		// zwischen Tastatur und Controller wechselt, trifft dieselbe Taste.
		Add(TEXT("Beschleunigen"),      TEXT("W / Pfeil hoch"),   TEXT("Rechter Ausloeser"));
		Add(TEXT("Bremsen"),            TEXT("S / Pfeil runter"), TEXT("Linker Ausloeser"));
		Add(TEXT("Lenken"),             TEXT("A D / Pfeil links/rechts"), TEXT("Linker Stick"));
		Add(TEXT("Rueckwaertsgang"),    TEXT("R"),                TEXT("X"));
		Add(TEXT("Handbremse"),         TEXT("Leertaste"),        TEXT("B"));
		Add(TEXT("Hupe"),               TEXT("B"),                TEXT("A"));
		Add(TEXT("Licht durchschalten"), TEXT("L"),               TEXT("Steuerkreuz hoch"));
		Add(TEXT("Warnblinkanlage"),    TEXT("H"),                TEXT("Steuerkreuz runter"));
		Add(TEXT("Blinker links"),      TEXT("Q"),                TEXT("LB"));
		Add(TEXT("Blinker rechts"),     TEXT("E"),                TEXT("RB"));
		Add(TEXT("Licht auf und ab"),   TEXT("X"),                TEXT("Rechter Stick (Klick)"));
		Add(TEXT("Ein-/Aussteigen"),    TEXT("F"),                TEXT("Y"));
		break;

	case EWbControlContext::ZuFuss:
		Add(TEXT("Gehen"),              TEXT("W A S D"),          TEXT("Linker Stick"));
		Add(TEXT("Umsehen"),            TEXT("Maus"),             TEXT("Rechter Stick"));
		Add(TEXT("Sprinten"),           TEXT("Linke Umschalt"),   TEXT("Linker Stick (Klick)"));
		Add(TEXT("Springen"),           TEXT("Leertaste"),        TEXT("A"));
		Add(TEXT("Ducken (halten)"),    TEXT("X"),                TEXT("Rechter Stick (Klick)"));
		Add(TEXT("Schiessen"),          TEXT("Linke Maustaste"),  TEXT("Rechter Ausloeser"));
		// Die Ich-Perspektive haengt an C und an nichts anderem. Sie hier
		// zu erfinden waere die schlimmere Sorte Tabelle: eine, die etwas
		// verspricht, was nicht geht.
		Add(TEXT("Ich-Perspektive"),    TEXT("C"),                TEXT("-"));
		// Die Waffenwahl geht zu Fuss ueber die Ziffern - "vor" und
		// "zurueck" gibt es nicht, und am Pad ebenfalls nichts. Zwei Zeilen
		// mit derselben Taste waeren in der Tabelle eine Luege in
		// Zeitlupe; eine Zeile mit "-" sagt, was wirklich gilt.
		Add(TEXT("Waffe waehlen"),      TEXT("1 bis 9"),          TEXT("-"));
		Add(TEXT("Ein-/Aussteigen"),    TEXT("F"),                TEXT("Y"));
		break;

	case EWbControlContext::Helikopter:
		for (const FWiesbadenHeliBinding& Binding : WiesbadenInputMap::HelicopterBindings())
		{
			Add(Binding.Beschreibung, Binding.Tastatur, Binding.Gamepad);
		}
		break;

	case EWbControlContext::Karte:
		Add(TEXT("Karte oeffnen"),      TEXT("M"),                TEXT("Select"));
		Add(TEXT("Karte schliessen"),   TEXT("M / Escape"),       TEXT("B"));
		Add(TEXT("Strasse suchen"),     TEXT("Tab"),              TEXT("A"));
		Add(TEXT("Zoomen"),             TEXT("Mausrad / + -"),    TEXT("RB / LB"));
		Add(TEXT("Karte schieben"),     TEXT("Pfeiltasten"),      TEXT("Rechter Stick"));
		Add(TEXT("Wegpunkt setzen"),    TEXT("Enter / Linksklick"), TEXT("Y"));
		Add(TEXT("Warp zur Strasse"),   TEXT("W"),                TEXT("X"));
		Add(TEXT("Auf den Spieler"),    TEXT("C"),                TEXT("Steuerkreuz runter"));
		Add(TEXT("Menue"),              TEXT("Escape"),           TEXT("Start"));
		break;

	case EWbControlContext::Menue:
		Add(TEXT("Navigieren"),         TEXT("Pfeiltasten / W S"), TEXT("Steuerkreuz / Linker Stick"));
		Add(TEXT("Bestaetigen"),        TEXT("Enter"),            TEXT("A"));
		Add(TEXT("Zurueck"),            TEXT("Escape"),           TEXT("B"));
		Add(TEXT("Wert aendern"),       TEXT("Links / rechts, A D"), TEXT("LB / RB"));
		Add(TEXT("Karte"),              TEXT("M"),                TEXT("Select"));
		break;

	default:
		break;
	}
}
