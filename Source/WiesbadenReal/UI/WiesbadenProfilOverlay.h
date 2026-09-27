// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "World/WiesbadenFrameProfiler.h"

/**
 * Die Profil-Tafel im HUD: halb so gross und halbtransparent.
 *
 * Anlass (26.09.2026): Die "Engine-Statistik" (stat fps/unit/game) der Option
 * StatEinblendung legte undurchsichtige Tabellen ueber das Bild - in
 * Filmaufnahmen war die Szene darunter kaum noch zu erkennen. Die Engine
 * zeichnet diese Tabellen mit festen Fonts und ohne Skalierungs-/Alpha-Haken
 * (StatsRender2.cpp: GStatFonts, DrawTile) - aus Projektcode nicht verkleinerbar.
 * Darum zeichnet das HUD eine EIGENE Tafel an ihrer Stelle: dieselben
 * Bildzeiten, aber halbe Schriftgroesse (= halbe Tafel) und 50 % Deckkraft.
 *
 * Alles hier ist datenrein und ohne Welt pruefbar (Test
 * WiesbadenReal.UI.ProfilOverlay): die Stilwerte, die Zeilen aus dem
 * FWbFrameReport und die Tafelgroesse. Das Zeichnen selbst steht im HUD.
 */

/** Aussehen der Profil-Tafel. */
struct FWbProfilOverlayStyle
{
	/** Schriftgroesse gegenueber der HUD-Standardgroesse. 0.5 = HALB so gross. */
	float TextSkala = 0.5f;

	/** Deckkraft des Hintergrunds. 0.5 = halbtransparent - das Bild darunter
	 *  muss erkennbar bleiben. */
	float HintergrundAlpha = 0.5f;

	/** Innenabstand (bei Skala 1 gemessen; laeuft mit TextSkala). */
	float RandPx = 8.0f;

	/** Zusatzabstand je Zeile (bei Skala 1 gemessen; laeuft mit TextSkala). */
	float ZeilenAbstandPx = 4.0f;
};

/** Die Zeilen der Tafel aus dem laufenden Messfenster (null-sicher). */
TArray<FString> WbProfilZeilen(const FWbFrameReport& Report);

/**
 * Tafelgroesse in Pixeln. RohTextBreite/RohZeilenHoehe sind die bei Skala 1
 * gemessenen Schriftmasse (UFont::GetStringSize / GetMaxCharHeight); alle
 * Masse laufen mit Stil.TextSkala, die Tafel wird also bei Skala 0.5 exakt
 * halb so breit und halb so hoch.
 */
FVector2D WbProfilTafelGroesse(int32 ZeilenAnzahl, float RohTextBreite,
	float RohZeilenHoehe, const FWbProfilOverlayStyle& Stil);
