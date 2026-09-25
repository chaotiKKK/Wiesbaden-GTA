// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * EINE Quelle fuer die Bild- und Lichtabstimmung (Befund 24.09.2026: zwei
 * Systeme legten Himmelslicht und Belichtung mit verschiedenen Werten an -
 * EnsureLightingActors 2.2 gegen EnsureCinematicLighting 3.2, je nach
 * Aufrufreihenfolge gewann einer).
 *
 * Ziel-Look: heller, sonniger Referenz-Look (echtes Wiesbaden, Mittagslicht).
 * Die Werte sind gegen das Bild abgestimmt, nicht geraten - die Kommentar-
 * ketten in WiesbadenWorldBuilder.cpp (Himmelslicht) und
 * WiesbadenCitySubsystem.cpp (Belichtung/Grade) dokumentieren die Reihe.
 */
namespace WiesbadenVisualTuning
{
	// Himmelslicht-Fuellung gegen die Sonne (10.0). 2.2 hebt die Schattenseiten
	// an und bleibt unter dem milchigen 3.5.
	constexpr float SkyLightIntensity = 2.2f;

	// Enges Belichtungsfenster: Tag/Nacht bleibt moeglich, das Ausbleichen
	// am Tag nicht. Bias -0.2 nimmt die aktive Abdunkelung fast zurueck
	// (gegen verschattete Fassaden, die ins Schwarz abtauchten).
	constexpr float AutoExposureMinBrightness = 0.15f;
	constexpr float AutoExposureMaxBrightness = 1.5f;
	constexpr float AutoExposureBias = -0.2f;

	// Dezent mehr Kontrast/Saettigung (gegen "flach").
	constexpr float ColorContrast = 1.08f;
	constexpr float ColorSaturation = 1.08f;

	// Cinematic-Feinschliff: kuehle Schatten, warme Lichter (Split-Toning),
	// Vignette dezenter als der Engine-Default 0.4, Bloom nur fuer echte
	// Glanzstellen (hohe Schwelle) - Glanz ohne Milchschleier.
	constexpr float ShadowTintR = 0.96f;
	constexpr float ShadowTintG = 0.99f;
	constexpr float ShadowTintB = 1.06f;
	constexpr float HighlightTintR = 1.03f;
	constexpr float HighlightTintG = 1.01f;
	constexpr float HighlightTintB = 0.97f;
	constexpr float VignetteIntensity = 0.25f;
	constexpr float BloomIntensity = 0.7f;
	constexpr float BloomThreshold = 1.0f;

	// Kamerabild: der Engine-Default (90 Grad) ist ein Fischauge-Weitwinkel
	// und einer der sichtbarsten "nicht AAA"-Zuege. Fahrzeug 72 fuer die
	// Verfolger-Kamera, enger 65 im Cockpit (weniger Verzerrung am Rand),
	// Fussgaenger wie Verfolger.
	constexpr float FollowFieldOfView = 72.0f;
	constexpr float CockpitFieldOfView = 65.0f;
	constexpr float FootFieldOfView = 72.0f;
}
