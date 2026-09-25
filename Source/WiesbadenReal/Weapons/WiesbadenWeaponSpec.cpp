// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "Weapons/WiesbadenWeaponSpec.h"

namespace
{
	/**
	 * Die Tabelle in EWiesbadenWeaponId-Reihenfolge.
	 *
	 * Werte im Diskurs 2026-09 festgelegt: Arcade-Kampf mit physischen
	 * Flugbahnen - Flugzeit und Fall sichtbar, Magazin begrenzt, Munition
	 * unbegrenzt in Reserve. Lichtschwert und Kettensaege sind Nahkampf.
	 */
	TArray<FWiesbadenWeaponSpec> MakeTable()
	{
		TArray<FWiesbadenWeaponSpec> T;
		T.SetNum(static_cast<int32>(EWiesbadenWeaponId::Count));

		{
			FWiesbadenWeaponSpec S;                 // 0 Pistole
			S.DisplayName = TEXT("Pistole");
			S.Damage = 25.0f;
			S.RoundsPerSecond = 4.0f;
			S.SpreadDegrees = 0.6f;
			S.ProjectileMassKg = 0.008f;
			S.MuzzleVelocityCmPerS = 35000.0f;
			S.RangeCm = 15000.0f;
			S.MagazineSize = 15;
			S.ReloadSeconds = 1.4f;
			T[0] = S;
		}
		{
			FWiesbadenWeaponSpec S;                 // 1 Maschinenpistole (bestehende Waffe)
			S.DisplayName = TEXT("Maschinenpistole");
			S.Damage = 18.0f;
			S.RoundsPerSecond = 9.0f;
			S.SpreadDegrees = 1.2f;
			S.ProjectileMassKg = 0.008f;
			S.MuzzleVelocityCmPerS = 38000.0f;
			S.RangeCm = 20000.0f;
			S.MagazineSize = 30;
			S.ReloadSeconds = 1.8f;
			T[1] = S;
		}
		{
			FWiesbadenWeaponSpec S;                 // 2 Gewehr (G36)
			S.DisplayName = TEXT("Gewehr");
			S.Damage = 34.0f;
			S.RoundsPerSecond = 7.0f;
			S.SpreadDegrees = 0.8f;
			S.ProjectileMassKg = 0.004f;
			S.MuzzleVelocityCmPerS = 88000.0f;
			S.RangeCm = 30000.0f;
			S.MagazineSize = 30;
			S.ReloadSeconds = 2.2f;
			T[2] = S;
		}
		{
			FWiesbadenWeaponSpec S;                 // 3 Schrotflinte
			S.DisplayName = TEXT("Schrotflinte");
			S.Damage = 12.0f;                       // je Korn
			S.RoundsPerSecond = 1.1f;
			S.SpreadDegrees = 3.5f;
			S.PelletsPerShot = 8;
			S.ProjectileMassKg = 0.002f;
			S.MuzzleVelocityCmPerS = 38000.0f;
			S.RangeCm = 4500.0f;
			S.MagazineSize = 6;
			S.ReloadSeconds = 2.8f;
			T[3] = S;
		}
		{
			FWiesbadenWeaponSpec S;                 // 4 Scharfschuetze
			S.DisplayName = TEXT("Scharfschuetze");
			S.Damage = 90.0f;
			S.RoundsPerSecond = 0.7f;
			S.SpreadDegrees = 0.05f;
			S.ProjectileMassKg = 0.011f;
			S.MuzzleVelocityCmPerS = 82000.0f;
			S.RangeCm = 60000.0f;
			S.MagazineSize = 5;
			S.ReloadSeconds = 2.9f;
			T[4] = S;
		}
		{
			FWiesbadenWeaponSpec S;                 // 5 Maschinengewehr (MG3)
			S.DisplayName = TEXT("Maschinengewehr");
			S.Damage = 26.0f;
			S.RoundsPerSecond = 17.0f;
			S.SpreadDegrees = 1.8f;
			S.ProjectileMassKg = 0.009f;
			S.MuzzleVelocityCmPerS = 82000.0f;
			S.RangeCm = 30000.0f;
			S.MagazineSize = 100;
			S.ReloadSeconds = 4.5f;
			T[5] = S;
		}
		{
			FWiesbadenWeaponSpec S;                 // 6 Granatwerfer
			S.DisplayName = TEXT("Granatwerfer");
			S.bMelee = false;
			S.Damage = 0.0f;                        // Schaden kommt aus der Explosion
			S.RoundsPerSecond = 0.5f;
			S.SpreadDegrees = 0.4f;
			S.ProjectileMassKg = 0.25f;
			S.MuzzleVelocityCmPerS = 7600.0f;       // langsames Geschoss: Bogen sichtbar
			S.RangeCm = 25000.0f;
			S.GravityCmPerS2 = 980.665f;
			S.bExplosive = true;
			S.BlastRadiusCm = 350.0f;
			S.BlastDamage = 120.0f;
			S.SelfDamage = 120.0f;                  // voller Eigenschaden (Entwurf)
			S.MagazineSize = 4;
			S.ReloadSeconds = 3.0f;
			T[6] = S;
		}
		{
			FWiesbadenWeaponSpec S;                 // 7 Lichtschwert
			S.DisplayName = TEXT("Lichtschwert");
			S.bMelee = true;
			S.Damage = 45.0f;
			S.RoundsPerSecond = 2.2f;               // Schwaenge je Sekunde
			S.MeleeReachCm = 220.0f;
			S.SwingSeconds = 0.45f;
			S.MagazineSize = 0;                     // kein Magazin
			T[7] = S;
		}
		{
			FWiesbadenWeaponSpec S;                 // 8 Kettensaege
			S.DisplayName = TEXT("Kettensaege");
			S.bMelee = true;
			S.Damage = 60.0f;
			S.RoundsPerSecond = 1.4f;
			S.MeleeReachCm = 180.0f;
			S.SwingSeconds = 0.7f;
			S.MagazineSize = 0;
			T[8] = S;
		}
		return T;
	}
}

namespace WiesbadenWeapons
{
	const TArray<FWiesbadenWeaponSpec>& Table()
	{
		static const TArray<FWiesbadenWeaponSpec> Table = MakeTable();
		return Table;
	}

	const FWiesbadenWeaponSpec& Spec(int32 WeaponIndex)
	{
		const TArray<FWiesbadenWeaponSpec>& T = Table();
		const int32 Clamped = FMath::Clamp(WeaponIndex, 0, T.Num() - 1);
		return T[Clamped];
	}

	bool InMeleeReach(const FVector& From, const FVector& Target,
		const FWiesbadenWeaponSpec& Spec)
	{
		if (!Spec.bMelee)
		{
			return false;
		}
		return FVector::Dist(From, Target) <= Spec.MeleeReachCm;
	}
}
