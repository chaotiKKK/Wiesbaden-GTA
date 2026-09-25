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
}
