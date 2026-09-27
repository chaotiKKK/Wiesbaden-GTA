// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/**
 * Die Waffen des Shooters als reine Daten.
 *
 * Acht Waffen plus die Kettensaege (Entwurfsgespraech 2026-09): Pistole, MP,
 * Gewehr, Schrotflinte, Scharfschuetze, MG, Granatwerfer, Lichtschwert -
 * Lichtschwert und Kettensaege sind Nahkampf (bMelee), alle anderen feuern
 * Projektile mit physischer Flugbahn (Gravitation, Flugzeit).
 *
 * Kein Verhalten, kein Weltzugriff: die Tabelle wird von der Waffen-Komponente
 * gelesen (Feuerrate, Streuung, Magazin) und von der Ballistik (Masse,
 * Muendungsgeschwindigkeit). Alle Werte in cm bzw. cm/s, wie im Rest des
 * Projekts; Gravitation in cm/s^2 (Erde ~ 980.665).
 */
enum class EWiesbadenWeaponId : uint8
{
	Pistole = 0,
	Maschinenpistole,
	Gewehr,
	Schrotflinte,
	Scharfschuetze,
	Maschinengewehr,
	Granatwerfer,
	Lichtschwert,
	Kettensaege,
	// Neu am 26.09.2026. Angehaengt, NICHT einsortiert: die Nummern stehen
	// in Konfigurationen und Dev-Befehlen (WbFussWaffe <n>), und ein Ruecken
	// der Werte wuerde dort stillschweigend eine andere Waffe treffen.
	Laserpistole,
	Raketenwerfer,
	Plasmacutter,
	Count
};

struct FWiesbadenWeaponSpec
{
	/** Anzeigename (HUD, Waffenrad). */
	const TCHAR* DisplayName = TEXT("");

	bool bMelee = false;

	// -- Schuss ------------------------------------------------------------
	/** Schaden je Treffer (Punkte; Passant 100, Spieler 100). */
	float Damage = 25.0f;

	/** Schuesse je Sekunde. */
	float RoundsPerSecond = 3.0f;

	/** Streukreis-Radius in Grad. */
	float SpreadDegrees = 0.7f;

	/** Projektile je Schuss (Schrotflinte: Kugelpackung). */
	int32 PelletsPerShot = 1;

	/** Projektilmasse in kg (Impuls auf Ziel = Masse * Geschwindigkeit). */
	float ProjectileMassKg = 0.008f;

	/** Muendungsgeschwindigkeit in cm/s. */
	float MuzzleVelocityCmPerS = 38000.0f;

	/** Maximale Reichweite in cm (Reichweitenende = Streukreis-Endpunkt). */
	float RangeCm = 20000.0f;

	/** Wirksame Gravitation in cm/s^2 (0 = gerade Bahn). */
	float GravityCmPerS2 = 980.665f;

	/** Explosiv? (Granatwerfer: Aufschlag Explosion + Eigenschaden.) */
	bool bExplosive = false;

	/** Explosionsradius in cm (nur bei bExplosive). */
	float BlastRadiusCm = 350.0f;

	/** Explosions-Schaden im Zentrum (faellt linear zum Rand). */
	float BlastDamage = 120.0f;

	/** Eigenschaden der Explosion (voll, Entwurfsentscheidung). */
	float SelfDamage = BlastDamage;

	// -- Magazin -----------------------------------------------------------
	int32 MagazineSize = 15;
	float ReloadSeconds = 1.6f;

	// -- Klang ------------------------------------------------------------
	/**
	 * Pfad eines realistischen Schuss-Samples (leer = prozeduraler Rueckfall).
	 *
	 * Stand frueher hart im Code (if/else ueber den Waffenschlitz). In der
	 * Tabelle gehoert er hin, weil der Klang zur Waffe gehoert und nicht zur
	 * Komponente - sonst vergisst jede neue Waffe ihren Klang.
	 */
	const TCHAR* ShotSoundPath = TEXT("");

	/** Lautstaerke des Schussklangs. */
	float ShotVolume = 1.0f;

	/** Pitch-Variation je Schuss (0.06 = +-6 %), damit Serien nicht monoton klingen. */
	float ShotPitchJitter = 0.06f;

	// -- Sicht ------------------------------------------------------------
	/** Kurzname fuer die Anzeige beim Waffenwechsel. */
	const TCHAR* ShortName = TEXT("");

	/** Blender-Mesh der Waffe (Soft-Pfad; leer = prozedurale Huelle aus AddPart). */
	const TCHAR* MeshAssetPath = TEXT("");

	// -- Zielen ------------------------------------------------------------
	/** Staerkster Zoom im Zielmodus (1.0 = kein Zoom). */
	float AdsZoomMax = 2.0f;

	// -- Schneiden (Plasmacutter) ------------------------------------------
	/** true = die Waffe trennt Teile aus schneidbaren Objekten. */
	bool bCuts = false;

	/** Schnittgeschwindigkeit beim Halten in cm/s. */
	float CutSpeedCmPerS = 70.0f;

	/** Drehung der Schnittebene je Mausradrast in Grad (Dead-Space-Prinzip). */
	float CutAngleStepDeg = 15.0f;

	// -- Sichtbares Geschoss -----------------------------------------------
	/** true = die Rakete fliegt sichtbar, nicht nur als Leuchtspur. */
	bool bVisibleProjectile = false;

	// -- Nahkampf ----------------------------------------------------------
	/** Reichweite Nahkampf in cm (ab Kamera/Pawn). */
	float MeleeReachCm = 220.0f;

	/** Dauer eines Nahkampf-Schwungs in Sekunden (Treffer zur Mitte). */
	float SwingSeconds = 0.45f;

	/** Sekunden zwischen zwei Schuss-/Schwungausloesungen (abgeleitet, kein Feld). */
	float ShotIntervalSeconds() const
	{
		return RoundsPerSecond > KINDA_SMALL_NUMBER ? 1.0f / RoundsPerSecond : KINDA_SMALL_NUMBER;
	}
};

/**
 * Die feste Tabelle (Reihenfolge = EWiesbadenWeaponId). WiesbadenReal-Daten:
 * Pistole P8, MP wie die bestehende Waffe, G36-Gewehr, Schrot 12/70,
 * G3-Scharfschuetze (besser: Scharfschuetzengewehr auf G3-Basis), MG3,
 * Granatwerfer (Aufschlagszuender), Lichtschwert, Kettensaege.
 */
namespace WiesbadenWeapons
{
	WIESBADENREAL_API const TArray<FWiesbadenWeaponSpec>& Table();

	/** Spekulationssicher: Index clamped auf die Tabelle. */
	WIESBADENREAL_API const FWiesbadenWeaponSpec& Spec(int32 WeaponIndex);

	/** Nahkampf-Hilfspruefung datenrein: Ist ein Ziel in Schwungreichweite? */
	WIESBADENREAL_API bool InMeleeReach(const FVector& From, const FVector& Target,
		const FWiesbadenWeaponSpec& Spec);

	/**
	 * Blaettern ueber die Tabelle (Mausrad): Steps nach vorn oder hinten,
	 * ueber beide Raender zurueck. Datapure Klammer, damit der Wechsel ohne
	 * laufende Welt pruefbar ist - die Regel gehoert zur Tabelle, nicht zum
	 * Pawn.
	 */
	WIESBADENREAL_API int32 NextWeaponIndex(int32 CurrentIndex, int32 Steps);
}
