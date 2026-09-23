// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Das Optionsmenue - datenreiner Teil.
 *
 * WOFUER EIN EIGENES MODUL: Das HUD ist gross genug. Hier liegt nur, was OHNE
 * Welt, Canvas und Audiogeraet entschieden werden kann - welche Zeilen es gibt,
 * wie ein Wert aussieht und wie er sich beim Tastendruck bewegt. Damit ist
 * genau der Teil pruefbar, der sonst nur im Bild zu sehen waere.
 *
 * DIE ENTSCHEIDENDE REGEL: Das Menue haelt KEINEN eigenen Wert. Jede Zeile
 * liest ihre Zahl bei dem System, dem sie gehoert (GameUserSettings,
 * Mischpult, Verkehrs-Sim, ...), und schreibt sie dorthin zurueck. Eine Option,
 * die nichts bewirkt, kann es damit nicht geben: kommt die Schreibung nicht an,
 * aendert sich die ANGEZEIGTE Zahl nicht mit.
 */

/** Gruppe, unter der eine Zeile im Menue steht. */
enum class EWbOptionGroup : uint8
{
	Grafik,
	Ton,
	Steuerung,
	Spielwelt,
	MAX
};

/** Art des Wertes - bestimmt Darstellung, Schrittweite und Grenzen. */
enum class EWbOptionKind : uint8
{
	Qualitaet,      // 0..4, benannt von "Sehr niedrig" bis "Episch"
	Bildrate,       // 0 (ohne Grenze), 30, 60, 90, 120, 144
	Lautstaerke,    // 0..1, Schritt 5 %
	Faktor,         // 0,25..3,00, Schritt 0,25 (Maus-Empfindlichkeit)
	Schalter,       // 0 = aus, 1 = an
	Anteil,         // 0..1, Schritt 10 % (Verkehrsdichte)
	Tageszeit,      // -1 = Systemzeit, sonst 0..23 Uhr
	MAX
};

/** Eine Zeile des Menues. Traegt KEINEN Wert - nur, was sie ist. */
struct WIESBADENREAL_API FWbOptionRow
{
	EWbOptionGroup Group = EWbOptionGroup::Grafik;
	EWbOptionKind Kind = EWbOptionKind::Qualitaet;

	/** Beschriftung links. */
	FString Label;

	/** Eine Zeile darueber, was die Einstellung bewirkt - fuer die Fusszeile. */
	FString Hinweis;

	/**
	 * Nur fuer die Gruppe Ton: welcher Bus des Mischpults.
	 * -1 bei allen anderen Zeilen.
	 */
	int32 BusIndex = -1;
};

/** Grenzen und Schrittweite einer Wertart. */
struct WIESBADENREAL_API FWbOptionRange
{
	double Min = 0.0;
	double Max = 1.0;
	double Step = 0.1;
};

namespace WiesbadenOptions
{
	/** Name der Gruppe, wie er als Zwischenueberschrift erscheint. */
	WIESBADENREAL_API FString GroupLabel(EWbOptionGroup Group);

	/** Grenzen und Schrittweite je Wertart. */
	WIESBADENREAL_API FWbOptionRange RangeOf(EWbOptionKind Kind);

	/**
	 * Alle Zeilen in Anzeigereihenfolge, nach Gruppen sortiert.
	 *
	 * @param AudioBusCount Zahl der Regler des Mischpults (eine Zeile je Bus).
	 *                      0 blendet die Gruppe Ton aus - ohne Mischpult gibt
	 *                      es dort nichts zu regeln, und ein toter Regler waere
	 *                      genau die Option, die nichts tut.
	 * @param BusLabels     Beschriftungen der Busse; fehlen sie, heissen die
	 *                      Zeilen "Bus 1", "Bus 2", ...
	 */
	WIESBADENREAL_API void BuildRows(int32 AudioBusCount,
		const TArray<FString>& BusLabels, TArray<FWbOptionRow>& OutRows);

	/** Der angezeigte Wert, z. B. "Hoch", "60 /s", "45 %", "22:00 Uhr". */
	WIESBADENREAL_API FString FormatValue(EWbOptionKind Kind, double Value);

	/**
	 * Ein Schritt nach links (-1) oder rechts (+1).
	 *
	 * Geklemmt, nicht umlaufend - ausser bei der Tageszeit, wo "Systemzeit"
	 * hinter 23 Uhr wieder auftaucht. Ein umlaufender Lautstaerkeregler waere
	 * eine Falle: einmal zu weit rechts, und es ist still.
	 */
	WIESBADENREAL_API double Step(EWbOptionKind Kind, double Value, int32 Direction);

	/**
	 * Fuellstand des Balkens, 0..1 - oder -1, wenn die Zeile keinen Balken hat.
	 * Schalter und Tageszeit haben keinen: ein halb gefuellter Balken fuer "an"
	 * waere Zierrat ohne Aussage.
	 */
	WIESBADENREAL_API double BarFraction(EWbOptionKind Kind, double Value);

	/** Nachster/voriger auswaehlbarer Index - Ueberschriften gibt es hier nicht. */
	WIESBADENREAL_API int32 NextRow(int32 Current, int32 Count, int32 Direction);

	/**
	 * Auswahl in die Liste klemmen.
	 *
	 * Die Zeilenzahl haengt davon ab, welche Systeme es gerade gibt - ohne
	 * Mischpult fallen sieben Zeilen weg, waehrend das Fenster offen ist. Eine
	 * Auswahl, die von vorher stehengeblieben ist, zeigt dann auf nichts.
	 */
	WIESBADENREAL_API int32 ClampRow(int32 Current, int32 Count);

	/**
	 * Flankenerkennung: hat die Taste GERADE angeschlagen?
	 *
	 * Dieselbe Mechanik, die das Pausemenue seit jeher benutzt - und seit
	 * dieser Fassung buchstaeblich dieselbe FUNKTION, nicht nur dieselben vier
	 * Zeilen zweimal geschrieben. Wer sie aendert, aendert beide Menues.
	 *
	 * Ohne sie wuerde eine gehaltene Taste in jedem Bild einen Schritt machen:
	 * ein Tastendruck haette die Lautstaerke in einem Wimpernschlag von 0 auf
	 * 100 gezogen.
	 *
	 * @param bIsDown  Ist die Taste JETZT unten?
	 * @param bHeld    Der Merker des Aufrufers; wird fortgeschrieben.
	 */
	WIESBADENREAL_API bool EdgePressed(bool bIsDown, bool& bHeld);

	/**
	 * Ist der geschriebene Wert beim Besitzer wirklich angekommen?
	 *
	 * Die Toleranz muss zur GENAUIGKEIT DES BESITZERS passen, nicht zu der des
	 * Menues. Hier stand FMath::IsNearlyEqual mit der Vorgabe fuer double
	 * (1e-8). `FWiesbadenTrafficSettings::TrafficDensity` ist aber ein float:
	 * die geschriebene 0,6 kommt als 0,60000002384 zurueck, 2,4e-8 daneben -
	 * und der Waechter meldete "NICHT ANGEKOMMEN" bei einer Einstellung, die
	 * angekommen war. Ein Waechter, der bei Gesunden Alarm schlaegt, bringt
	 * einem das Wegsehen bei.
	 *
	 * 1e-6 relativ liegt bequem ueber dem Rundungsrauschen eines float (rund
	 * 1,2e-7 relativ) und weit unter dem kleinsten echten Schritt des Menues
	 * (0,05 bei der Lautstaerke). Ein wirklich verschluckter oder geklemmter
	 * Wert faellt damit weiterhin auf.
	 */
	WIESBADENREAL_API bool ValueArrived(double Written, double ReadBack);
}
