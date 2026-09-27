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
			S.MeshAssetPath = TEXT("/Game/Waffen/Meshes/SM_Waffe_Pistole.SM_Waffe_Pistole");
			S.ShortName = TEXT("P8");
			S.ShotSoundPath = TEXT("/Game/Audio/Samples/A_ShotBerettaM12.A_ShotBerettaM12");
			S.AdsZoomMax = 1.6f;
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
			S.ShortName = TEXT("MP");
			S.ShotSoundPath = TEXT("/Game/Audio/Samples/A_ShotBerettaM12.A_ShotBerettaM12");
			S.AdsZoomMax = 1.6f;
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
			S.MeshAssetPath = TEXT("/Game/Waffen/Meshes/SM_Waffe_Gewehr.SM_Waffe_Gewehr");
			S.ShortName = TEXT("G36");
			S.ShotSoundPath = TEXT("/Game/Audio/Samples/A_ShotRifle.A_ShotRifle");
			S.AdsZoomMax = 2.2f;
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
			S.ShortName = TEXT("Schrot");
			S.ShotSoundPath = TEXT("/Game/Audio/Samples/A_ShotRifle.A_ShotRifle");
			S.ShotPitchJitter = 0.02f;          // tiefer, schwerer Schuss
			S.AdsZoomMax = 1.3f;
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
			S.ShortName = TEXT("G3");
			S.ShotSoundPath = TEXT("/Game/Audio/Samples/A_ShotRifle.A_ShotRifle");
			S.ShotPitchJitter = 0.01f;          // ruhiger Lauf, kein Wackeln
			S.AdsZoomMax = 3.5f;
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
			S.MeshAssetPath = TEXT("/Game/Waffen/Meshes/SM_Waffe_MG.SM_Waffe_MG");
			S.ShortName = TEXT("MG3");
			S.ShotSoundPath = TEXT("/Game/Audio/Samples/A_ShotRifle.A_ShotRifle");
			S.ShotPitchJitter = 0.09f;          // grosse Streuung im Klang bei 17/s
			S.AdsZoomMax = 1.8f;
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
			S.MeshAssetPath = TEXT("/Game/Waffen/Meshes/SM_Waffe_Granatwerfer.SM_Waffe_Granatwerfer");
			S.ShortName = TEXT("GW");
			S.ShotSoundPath = TEXT("/Game/Audio/Samples/A_Explosion.A_Explosion");
			S.AdsZoomMax = 2.0f;
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
			S.MeshAssetPath = TEXT("/Game/Waffen/Meshes/SM_Waffe_Lichtschwert.SM_Waffe_Lichtschwert");
			S.ShortName = TEXT("Lichtschwert");   // Nahkampf: kein Schussklang
			S.AdsZoomMax = 1.0f;
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
			S.ShortName = TEXT("Kettensaege");
			S.AdsZoomMax = 1.0f;
			S.bMelee = true;
			S.Damage = 60.0f;
			S.RoundsPerSecond = 1.4f;
			S.MeleeReachCm = 180.0f;
			S.SwingSeconds = 0.7f;
			S.MagazineSize = 0;
			T[8] = S;
		}
		{
			FWiesbadenWeaponSpec S;                 // 9 Laserpistole (Energie)
			S.DisplayName = TEXT("Laserpistole");
			S.MeshAssetPath = TEXT("/Game/Waffen/Meshes/SM_Waffe_Laserpistole.SM_Waffe_Laserpistole");
			S.ShortName = TEXT("Laser");
			// Keine Aufnahme fuer einen Laser vorhanden - hier bleibt der
			// prozedurale Klang aktiv (ShotSoundPath leer) und wird auf einen
			// Energie-Ton abgestimmt. Erfinden darf man hier nichts.
			S.ShotSoundPath = TEXT("");
			S.Damage = 22.0f;
			S.RoundsPerSecond = 3.0f;
			S.SpreadDegrees = 0.15f;
			S.ProjectileMassKg = 0.001f;
			S.MuzzleVelocityCmPerS = 120000.0f;     // Lichttempo: kein sichtbarer Flug
			S.RangeCm = 40000.0f;
			S.GravityCmPerS2 = 0.0f;                // gerade Bahn
			S.MagazineSize = 20;
			S.ReloadSeconds = 1.2f;
			S.AdsZoomMax = 1.8f;
			T[9] = S;
		}
		{
			FWiesbadenWeaponSpec S;                 // 10 Raketenwerfer
			S.DisplayName = TEXT("Raketenwerfer");
			S.MeshAssetPath = TEXT("/Game/Waffen/Meshes/SM_Waffe_Raketenwerfer.SM_Waffe_Raketenwerfer");
			S.ShortName = TEXT("Rakete");
			S.ShotSoundPath = TEXT("/Game/Audio/Samples/A_Explosion.A_Explosion");
			S.Damage = 0.0f;                        // Schaden aus der Explosion
			S.RoundsPerSecond = 0.4f;
			S.SpreadDegrees = 0.1f;
			S.ProjectileMassKg = 2.0f;
			S.MuzzleVelocityCmPerS = 6000.0f;       // langsam genug, um sie zu sehen
			S.RangeCm = 60000.0f;
			S.GravityCmPerS2 = 250.0f;              // leichter Bogen
			S.bExplosive = true;
			S.BlastRadiusCm = 700.0f;
			S.BlastDamage = 220.0f;
			S.SelfDamage = 120.0f;
			S.MagazineSize = 1;
			S.ReloadSeconds = 3.5f;
			S.AdsZoomMax = 2.0f;
			S.bVisibleProjectile = true;
			T[10] = S;
		}
		{
			FWiesbadenWeaponSpec S;                 // 11 Plasmacutter
			S.DisplayName = TEXT("Plasmacutter");
			S.MeshAssetPath = TEXT("/Game/Waffen/Meshes/SM_Waffe_Plasmacutter.SM_Waffe_Plasmacutter");
			S.ShortName = TEXT("Cutter");
			S.ShotSoundPath = TEXT("");             // Energie-Klang, prozedural
			S.Damage = 8.0f;
			S.RoundsPerSecond = 12.0f;              // gehalten schneiden, nicht pulsen
			S.SpreadDegrees = 0.1f;
			S.ProjectileMassKg = 0.0f;
			S.MuzzleVelocityCmPerS = 20000.0f;
			S.RangeCm = 900.0f;                     // Werkzeug-Reichweite, keine Waffe
			S.GravityCmPerS2 = 0.0f;
			S.MagazineSize = 100;
			S.ReloadSeconds = 2.0f;
			S.AdsZoomMax = 1.4f;
			S.bCuts = true;
			S.CutSpeedCmPerS = 70.0f;
			S.CutAngleStepDeg = 15.0f;
			T[11] = S;
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

	int32 NextWeaponIndex(int32 CurrentIndex, int32 Steps)
	{
		const int32 Num = Table().Num();
		if (Num <= 0 || Steps == 0)
		{
			return FMath::Clamp(CurrentIndex, 0, FMath::Max(Num - 1, 0));
		}
		// Einmal um den ganzen Ring drehen, dann das Ergebnis in den
		// Bereich legen - so verhaelt sich auch ein Sprung um mehrere
		// Rasten (starkes Scrollen) wie ein Blaettern.
		int32 Next = (CurrentIndex + Steps) % Num;
		if (Next < 0)
		{
			Next += Num;
		}
		return Next;
	}
}
