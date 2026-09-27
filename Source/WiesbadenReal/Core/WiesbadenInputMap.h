// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class APlayerController;

/**
 * Die Tastenbelegung als DATEN, nicht als verstreute IsInputKeyDown-Aufrufe.
 *
 * Anlass (26.09.2026): die Belegung steckte verteilt im FootPawn - ADS hing
 * ausschliesslich an der rechten Maustaste, fuer das Gamepad gab es kein
 * Zielen, keinen Waffenwechsel und kein Mausrad-Aequivalent. Ein Test konnte
 * das nicht pruefen, weil es keine Tabelle gab, gegen die zu pruefen waere.
 *
 * Das Layout folgt der ueblichen Shooter-Belegung der Xbox (Red Dead
 * Redemption 2 als Vorbild):
 *   RT feuern   LT zielen (ADS)   A springen   R3 ducken   L3 rennen
 *   RB/LB naechste/vorherige Waffe   D-Pad hoch/runter = Mausrad
 *   X einsteigen/interagieren   Y Ansicht (Ego/Schulter)
 * Tastatur/Maus bleibt voll bestehen (Ziffern 1-8, Rad, Maus-Look).
 *
 * Alles hier ist ohne Welt pruefbar (Test WiesbadenReal.Input.*); der Pawn
 * liest die Tabelle ueber IsActionDown und wendet nur noch an.
 */
enum class EWiesbadenInputAction : uint8
{
	Feuern,
	Zielen,
	Springen,
	RennenHalten,
	DuckenHalten,
	WaffeVor,
	WaffeZurueck,
	AnsichtWechseln,
	Einsteigen,
};

/** Eine Belegungszeile: eine Aktion, alle Tasten, die sie ausloesen. */
struct FWiesbadenBelegung
{
	EWiesbadenInputAction Action = EWiesbadenInputAction::Feuern;
	/** Anzeigetext fuer HUD-Legende und Fehlermeldungen. */
	const TCHAR* Beschreibung = TEXT("");
	/** Tastatur/Maus-Tasten (digital). */
	TArray<FKey> Tasten;
	/** XBox-Tasten; bei bAnalog sind es Trigger-Achsen (Schwelle statt Taste). */
	TArray<FKey> Gamepad;
	/** Trigger-Achsen: ueber der Schwelle = gedrueckt (sonst IsInputKeyDown). */
	bool bAnalog = false;
};

namespace WiesbadenInputMap
{
	/** Wohin ein "Mausrad"-Klick geht - dieselbe Entscheidung fuer Rad und D-Pad. */
	enum class EMausradRoute : uint8
	{
		Schnittebene, // schneidende Waffe in der Hand (Dead-Space-Prinzip)
		Zoom,         // Zielen aktiv
		Waffenwechsel,
	};
	/** Die Belegungstabelle (eine Zeile je Aktion). */
	WIESBADENREAL_API const TArray<FWiesbadenBelegung>& Belegungen();

	/**
	 * Ist die Aktion gerade gedrueckt? Prueft alle Tasten ihrer Zeile,
	 * Trigger ueber die Schwelle. Ohne Controller false.
	 */
	WIESBADENREAL_API bool IsActionDown(
		const APlayerController* PC, EWiesbadenInputAction Action);

	/** Trigger-Achse ueber der Schwelle? (Toleranz: >= Schwelle ist gedrueckt.) */
	WIESBADENREAL_API bool TriggerGedrueckt(float Achse, float Schwelle = 0.35f);

	/**
	 * Aufteilung eines Radklicks. Die schneidende Waffe hat Vorrang - das
	 * Mausrad dreht dann die Schnittebene, weder Zoom noch Wechsel.
	 */
	WIESBADENREAL_API EMausradRoute RouteMausrad(bool bSchneidendeWaffe, bool bZielt);

	/**
	 * Zoom-Stufe nach Klicks weiterstellen: immer im Bereich 1..MaxZoom,
	 * auch bei rueckwaerts drehen, Spruengen um mehrere Klicks und einer
	 * Obergrenze unter 1 (wird dann auf 1 gehoben).
	 */
	WIESBADENREAL_API float ZoomStufe(
		float Aktuell, int32 Klicks, float Schritt, float MaxZoom);
}
