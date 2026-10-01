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

/** Aktionen und Belegung des fliegbaren Helikopters. */
enum class EWiesbadenHeliAction : uint8
{
	Pitch,
	Roll,
	Yaw,
	Collective,
	CollectiveUp,
	CollectiveDown,
	Engine,
	Fire,
	CameraMode,
	Look,
	Searchlight,
	LandingLight,
	Exit,
	MAX
};

/** Eine tabellarische Zeile fuer die Flugsteuerung und ihre Anzeige. */
struct FWiesbadenHeliBinding
{
	EWiesbadenHeliAction Action = EWiesbadenHeliAction::Pitch;
	EWiesbadenHeliAction RelatedAction = EWiesbadenHeliAction::MAX;
	const TCHAR* Beschreibung = TEXT("");
	const TCHAR* Tastatur = TEXT("");
	const TCHAR* Gamepad = TEXT("");
};

struct FWbHelicopterLessonStep
{
	const TCHAR* Titel = TEXT("");
	const TCHAR* Anleitung = TEXT("");
	EWiesbadenHeliAction Action = EWiesbadenHeliAction::MAX;
	EWiesbadenHeliAction RelatedAction = EWiesbadenHeliAction::MAX;
	bool bManualConfirm = false;
};

namespace WiesbadenHelicopterLesson
{
	constexpr int32 PracticeStepCount = 10;
	constexpr int32 StepCount = PracticeStepCount + 1;
	WIESBADENREAL_API const TArray<FWbHelicopterLessonStep>& Steps();
	WIESBADENREAL_API int32 AdvanceStep(int32 CurrentStep, bool bSatisfied, bool bSkip);
	WIESBADENREAL_API int32 ClampStep(int32 Step);
}

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

	/** Belegungen des fliegbaren Helikopters, auch fuer HUD und Menue. */
	WIESBADENREAL_API const TArray<FWiesbadenHeliBinding>& HelicopterBindings();
	/** Signierte Tastatur/Gamepad-Achse; die jeweils groessere Eingabe gewinnt. */
	WIESBADENREAL_API float HelicopterAxis(
		const APlayerController* PC, EWiesbadenHeliAction Action);
	/** Digitale Eingabe einer Helikopter-Aktion. */
	WIESBADENREAL_API bool IsHelicopterActionDown(
		const APlayerController* PC, EWiesbadenHeliAction Action);
}
