// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/WiesbadenOptions.h"

// Bewusst OHNE "WiesbadenMenuFlow.generated.h": dieser Header enthaelt keine
// reflektierten Typen (kein UCLASS/USTRUCT/UENUM/UFUNCTION) und ist damit ein
// reiner Daten- und Ablaufkern wie WiesbadenOptions.h und WiesbadenMinimap.h.
// UHT erzeugt fuer solche Header keine .generated.h - der Include waere ein
// "C1083: Datei kann nicht geoeffnet werden" schon beim ersten Bau.

/**
 * Der Menue-Zustand: Intro, Titel, Hauptmenue, Optionen und ihre Unterseiten.
 *
 * Bewusst EINE Groesse fuer "was liegt ueber dem Spiel" - genau wie es das
 * Pausemenue schon haelt (EWbPauseView dort). Zwei Schalter, von denen einer
 * das Bild und der andere die Eingabe verantwortet, koennen auseinanderlaufen;
 * dann ist etwas zu sehen, das sich nicht bedienen laesst.
 */
enum class EWbMenuScreen : uint8
{
	Intro,        // Studio-Bildschirm, laeuft von selbst durch
	Titel,        // Titelbildschirm MIT Hauptmenue
	Optionen,     // Gruppenliste (Sound, Grafik, ...)
	Gruppe,       // eine Gruppe als Unterseite
	Belegung,     // Tastatur- und Xbox-360-Belegung
	MAX
};

/** Was ein Eintrag tut, wenn man ihn bestaetigt. */
enum class EWbMenuAction : uint8
{
	Keine,
	SpielStarten,
	OptionenOeffnen,
	GruppeOeffnen,
	BelegungOeffnen,
	Zurueck,
	Beenden,
	WertAendern,          // eine Optionszeile verstellen
	OptionenSpeichern,
	OptionenZuruecksetzen,
	Ueber,
	MAX
};

/** Art eines Menue-Eintrags - bestimmt, wie er gezeichnet wird. */
enum class EWbMenuEntryKind : uint8
{
	Aktion,   // waehlbar, tut etwas
	Wert,     // waehlbar, verstellt eine Einstellung
	Zurueck,  // waehlbar, eine Ebene zurueck
	Hinweis   // nicht waehlbar (Zusammenfassung, Fussnote)
};

/** Ein Eintrag des Menues. Traegt keinen Wert - nur, was er ist. */
struct WIESBADENREAL_API FWbMenuEntry
{
	EWbMenuEntryKind Kind = EWbMenuEntryKind::Aktion;
	EWbMenuAction Action = EWbMenuAction::Keine;

	/** Nur bei Kind Wert: WORAN die Zeile ihren Wert findet. */
	EWbOptionId Option = EWbOptionId::MAX;

	/** Nur bei Art Gruppe: welche Gruppe geoeffnet wird. */
	EWbOptionGroup Group = EWbOptionGroup::MAX;

	FString Label;
	/** Unter dem Eintrag - was er bewirkt. */
	FString Hinweis;
};

/** Der Kontext, fuer den eine Belegung gilt. */
enum class EWbControlContext : uint8
{
	Fahrzeug,
	ZuFuss,
	Helikopter,
	Karte,
	Menue,
	MAX
};

/**
 * Eine Zeile der Belegungs-Uebersicht.
 *
 * Bewusst als TEXT und nicht als FKey: die Anzeige soll sagen, was der Knopf
 * HEISST ("Rechter Ausloeser", "Y"), nicht wie er in der Engine heisst. Wer
 * den Knopf umlegt, aendert hier einen Text - und der Test prueft weiter, dass
 * keine Aktion zwei Tasten im selben Konument belegt.
 */
struct WIESBADENREAL_API FWbControlBinding
{
	EWbControlContext Context = EWbControlContext::Fahrzeug;
	/** Was die Taste tut, in Spielersprache. */
	FString Aktion;
	/** Tastatur - mit ", " getrennt, wenn es mehrere sind. */
	FString Taste;
	/** Xbox-360-Controller - mehrere mit ", ". */
	FString Pad;
};

/** Zusammenfassung einer Zeile: "Springen    Leertaste    A". */
WIESBADENREAL_API FString FormatBindingLine(const FWbControlBinding& Binding);

namespace WiesbadenMenu
{
	/** Ueberschrift eines Bildschirms. */
	WIESBADENREAL_API FString ScreenTitle(EWbMenuScreen Screen);

	/** Die Hauptmenue-Eintraege: starten, Optionen, Steuerung, beenden. */
	WIESBADENREAL_API void BuildHauptmenu(TArray<FWbMenuEntry>& Out);

	/**
	 * Die Gruppenliste der Optionen - in der Reihenfolge, in der sie der
	 * Spieler liest: Sound, Grafik, Steuerung, Bild, Debug, Config, zurueck.
	 */
	WIESBADENREAL_API void BuildGruppenliste(TArray<FWbMenuEntry>& Out);

	/**
	 * Eine Gruppe als Unterseite.
	 *
	 * @param Rows   Alle Zeilen des Optionsfensters (WiesbadenOptions::BuildRows);
	 *               genommen wird die Gruppe, die hier verlangt wird.
	 * @param Out    die Eintraege der Seite, in Ausgabe-Reihenfolge
	 */
	WIESBADENREAL_API void BuildGruppe(EWbOptionGroup Gruppe,
		const TArray<FWbOptionRow>& Rows, TArray<FWbMenuEntry>& Out);

	/** Naechster waehlbarer Index (kippt um, ueberspringt Hinweiszeilen). */
	WIESBADENREAL_API int32 MoveSelection(
		int32 Current, const TArray<FWbMenuEntry>& Entries, int32 Richtung);

	/** Der Eintrag an dieser Stelle - nullptr, wenn dort keiner ist. */
	WIESBADENREAL_API const FWbMenuEntry* EntryAt(
		const TArray<FWbMenuEntry>& Entries, int32 Index);

	/**
	 * Der naechste Bildschirm, wenn man auf einem Eintrag Enter drueckt.
	 *
	 * Getrennt vom Zeichnen, damit sich der Ablauf pruefen laesst, ohne
	 * einen Bildschirm zu brauchen (Test Menu.Ablauf).
	 *
	 * @param SameScreen true, wenn die Seite bleiben soll (Wert verstellen)
	 */
	WIESBADENREAL_API EWbMenuScreen Advance(
		EWbMenuScreen Screen, const FWbMenuEntry* Entry, bool& bSameScreen);

	/** Eine Ebene zurueck; true, wenn sich der Bildschirm dadurch aendert. */
	WIESBADENREAL_API bool Back(EWbMenuScreen& Screen);

	/**
	 * Das Intro in Worten: Phase, Zeile und Deckkraft fuer die gegebene Zeit.
	 *
	 * Drei Phasen hintereinander, jede mit Ein- und Ausblenden; danach ist
	 * Schluss, damit das Intro nicht ewig laeuft. Wer es ueberspringt, soll
	 * dieselbe Kuerze sehen - deshalb die reine Rechnung ohne Welt und Uhr.
	 *
	 * @param Sekunden   Zeit seit dem Start (Weltzeit, Sekunden)
	 * @param OutZeile    der Text, der gerade stehen soll (leer = fertig)
	 * @param OutDeckkraft 0..1 fuer die Ein-/Ausblendung
	 * @return true, solange das Intro laeuft
	 */
	WIESBADENREAL_API bool IntroPhase(
		double Sekunden, FString& OutZeile, float& OutDeckkraft);

	/** Dauer des ganzen Intros in Sekunden (Ende der letzten Phase). */
	WIESBADENREAL_API double IntroSeconds();

	/**
	 * Muss das Intro beim Start gezeigt werden?
	 *
	 * Nein, wenn der Spieler es abgewaehlt hat - und NEIN in jedem
	 * Automationslauf: dort wartet der Rauchtest auf Belege aus dem Spiel
	 * (Fahrprofil, WbHealth), und ein wartender Titelbildschirm wuerde ihn
	 * genau da aufhalten. Erkannt an -unattended, -nullrhi, -ExecCmds und an
	 * -WbKeinIntro. Datenrein, damit sich die Regel ohne Spiel pruefen laesst.
	 */
	WIESBADENREAL_API bool ShouldShowIntro(bool bEinstellungAn, const FString& Kommandozeile);

	/** Die Belegung eines Kontexts, in Anzeigereihenfolge. */
	WIESBADENREAL_API void ControlBindings(
		EWbControlContext Context, TArray<FWbControlBinding>& Out);

	/** Namen der Kontexte fuer die Reiter der Belegungs-Seite. */
	WIESBADENREAL_API void GetContextLabels(TArray<FString>& OutLabels);

	/** GroupLabel der Seite "Belegung" (Zwischenueberschrift). */
	WIESBADENREAL_API FString VersionLine();
}
