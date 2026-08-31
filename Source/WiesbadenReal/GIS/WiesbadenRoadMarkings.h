// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Strassenmarkierung & Strassenausstattung nach StVO/RMS (Richtlinien fuer die
 * Markierung von Strassen) - Naeherungswerte in Zentimetern.
 *
 * Reine Datenkonstanten fuer den spaeteren Strassenausstattungs-Pass
 * (Markierungen auf die Fahrbahn, Leitpfosten, Ampelmasten, Schilder).
 * Die zugehoerigen Texturen/Modelle werden separat bereitgestellt (ASSETS.md).
 */
namespace WiesbadenRoadMarkings
{
	// -- Querlinien --------------------------------------------------------
	/** Haltlinie (Breitstrich) an Ampeln/Stopp-Schildern. */
	inline constexpr float StopLineWidthCm = 50.0f;

	/** Zebrastreifen: Strichbreite und Luecke (50/50 cm). */
	inline constexpr float ZebraStripeWidthCm = 50.0f;
	inline constexpr float ZebraStripeGapCm = 50.0f;

	/** Mindestbreite eines Fussgaengerueberwegs. */
	inline constexpr float ZebraMinWidthCm = 300.0f;

	// -- Laengslinien ------------------------------------------------------
	/** Schmalstrich (Leit-/Randlinie, 0,12 m). */
	inline constexpr float NarrowLineWidthCm = 12.0f;

	/** Breitstrich (Fahrstreifenbegrenzung, 0,25 m). */
	inline constexpr float WideLineWidthCm = 25.0f;

	/** Leitlinie: Strich-/Lueckenlaenge (1:1, hier 6 m). */
	inline constexpr float LaneLineDashLengthCm = 600.0f;
	inline constexpr float LaneLineGapLengthCm = 600.0f;

	// -- Pfeile ------------------------------------------------------------
	/** Richtungspfeil auf der Fahrbahn (Stadt, ca. 5 m). */
	inline constexpr float DirectionArrowLengthCm = 500.0f;

	// -- Leitpfosten (RMS) -------------------------------------------------
	/** Hoehe des Leitpfostens ueber der Fahrbahn. */
	inline constexpr float DelineatorHeightCm = 100.0f;

	/** Abstand der Leitpfosten auf Geraden (50 m). */
	inline constexpr float DelineatorSpacingStraightCm = 5000.0f;

	/** Abstand der Leitpfosten in Kurven (verkuerzt). */
	inline constexpr float DelineatorSpacingCurveCm = 3000.0f;

	/** Seitlicher Abstand des Leitpfostens vom Fahrbahnrand. */
	inline constexpr float DelineatorOffsetFromEdgeCm = 50.0f;

	// -- Schilder & Ampeln -------------------------------------------------
	/** Schildunterkante ueber dem Gehweg. */
	inline constexpr float SignHeightAboveWalkwayCm = 220.0f;

	/** Schildunterkante ueber der Fahrbahn (ohne Gehweg). */
	inline constexpr float SignHeightAboveRoadCm = 200.0f;

	/** Seitlicher Abstand der Zeichenkante vom Fahrbahnrand. */
	inline constexpr float SignLateralOffsetCm = 50.0f;

	/** Ampelmast-/Signalgeber-Hoehe. */
	inline constexpr float TrafficLightHeightCm = 300.0f;
}
