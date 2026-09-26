// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Weapons/WiesbadenBallistics.h"
#include "Weapons/WiesbadenWeaponSpec.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponSpecTest,
	"WiesbadenReal.Weapons.WeaponSpec",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWeaponSpecTest::RunTest(const FString& Parameters)
{
	const TArray<FWiesbadenWeaponSpec>& Table = WiesbadenWeapons::Table();

	// Zwoelf Eintraege: die neun aus dem Entwurf 2026-09 plus Laserpistole,
	// Raketenwerfer und Plasmacutter (Auftrag 26.09.2026). Die Sollzahl kommt
	// aus dem Enum - wer eine Waffe anhaengt, vergisst hier nichts.
	TestEqual(TEXT("12 Waffen"), Table.Num(),
		static_cast<int32>(EWiesbadenWeaponId::Count));

	// Nahkampf-Flag: nur Lichtschwert und Kettensaege.
	for (int32 Index = 0; Index < Table.Num(); ++Index)
	{
		const bool bMelee = Table[Index].bMelee;
		const bool bExpected = (Index == static_cast<int32>(EWiesbadenWeaponId::Lichtschwert))
			|| (Index == static_cast<int32>(EWiesbadenWeaponId::Kettensaege));
		TestEqual(FString::Printf(TEXT("Nahkampf-Flag %d"), Index), bMelee, bExpected);
	}

	// Explosiv: nur der Granatwerfer, und der traegt volle Eigenschaden-Regel.
	const FWiesbadenWeaponSpec& Grenade = WiesbadenWeapons::Spec(
		static_cast<int32>(EWiesbadenWeaponId::Granatwerfer));
	TestTrue(TEXT("Granatwerfer explosiv"), Grenade.bExplosive);
	TestEqual(TEXT("Eigenschaden = Blast"), Grenade.SelfDamage, Grenade.BlastDamage);

	// Explosiv sind zwei: der Granatwerfer und der Raketenwerfer.
	int32 ExplosiveCount = 0;
	for (const FWiesbadenWeaponSpec& S : Table)
	{
		ExplosiveCount += S.bExplosive ? 1 : 0;
	}
	TestEqual(TEXT("Zwei explosive Waffen"), ExplosiveCount, 2);
	TestTrue(TEXT("Raketenwerfer explosiv"),
		WiesbadenWeapons::Spec(
			static_cast<int32>(EWiesbadenWeaponId::Raketenwerfer)).bExplosive);
	TestTrue(TEXT("Rakete fliegt sichtbar"),
		WiesbadenWeapons::Spec(
			static_cast<int32>(EWiesbadenWeaponId::Raketenwerfer)).bVisibleProjectile);

	// Physik-Sinn: positive Geschwindigkeit, Schaden >= 0, Magazin >= 0.
	for (int32 Index = 0; Index < Table.Num(); ++Index)
	{
		const FWiesbadenWeaponSpec& S = Table[Index];
		TestTrue(FString::Printf(TEXT("V > 0 (%d)"), Index), S.MuzzleVelocityCmPerS > 0.0f);
		TestTrue(FString::Printf(TEXT("Damage >= 0 (%d)"), Index), S.Damage >= 0.0f);
		TestTrue(FString::Printf(TEXT("Magazin >= 0 (%d)"), Index), S.MagazineSize >= 0);
		TestTrue(FString::Printf(TEXT("Schussintervall > 0 (%d)"), Index),
			S.ShotIntervalSeconds() > 0.0f);
	}

	// Spec() clampt ausserhalb des Bereichs, statt zu crashen.
	TestTrue(TEXT("Spec(-1) gueltig"),
		&WiesbadenWeapons::Spec(-1) == &Table[0]);
	TestTrue(TEXT("Spec(99) gueltig"),
		&WiesbadenWeapons::Spec(99) == &Table[Table.Num() - 1]);

	// Nahkampf-Reichweite datenrein.
	FWiesbadenWeaponSpec Schwert = WiesbadenWeapons::Spec(
		static_cast<int32>(EWiesbadenWeaponId::Lichtschwert));
	TestTrue(TEXT("Nah dran trifft"),
		WiesbadenWeapons::InMeleeReach(FVector::ZeroVector, FVector(100.0, 0.0, 0.0), Schwert));
	TestFalse(TEXT("Weit weg trifft nicht"),
		WiesbadenWeapons::InMeleeReach(FVector::ZeroVector, FVector(1000.0, 0.0, 0.0), Schwert));

	// Die acht Kernwaffen des Auftrags (Ziffern 1-8) existieren, haben einen
	// Kurznamen fuer die Anzeige beim Wechsel und eine sinnvolle Zoom-Grenze.
	static const EWiesbadenWeaponId CoreOrder[8] = {
		EWiesbadenWeaponId::Pistole, EWiesbadenWeaponId::Gewehr,
		EWiesbadenWeaponId::Maschinengewehr, EWiesbadenWeaponId::Laserpistole,
		EWiesbadenWeaponId::Lichtschwert, EWiesbadenWeaponId::Raketenwerfer,
		EWiesbadenWeaponId::Granatwerfer, EWiesbadenWeaponId::Plasmacutter };
	for (int32 Slot = 0; Slot < 8; ++Slot)
	{
		const FWiesbadenWeaponSpec& S = WiesbadenWeapons::Spec(
			static_cast<int32>(CoreOrder[Slot]));
		TestTrue(FString::Printf(TEXT("Kernwaffe %d hat Namen"), Slot + 1),
			S.DisplayName != nullptr && S.DisplayName[0] != 0);
		TestTrue(FString::Printf(TEXT("Kernwaffe %d hat Kurzname"), Slot + 1),
			S.ShortName != nullptr && S.ShortName[0] != 0);
		TestTrue(FString::Printf(TEXT("Kernwaffe %d zoomt mindestens 1.0"), Slot + 1),
			S.AdsZoomMax >= 1.0f);
	}

	// Der Plasmacutter schneidet: Flag, Geschwindigkeit und Rastwinkel der
	// Schnittebene (Dead-Space-Prinzip: das Mausrad dreht die Ebene).
	const FWiesbadenWeaponSpec& Cutter = WiesbadenWeapons::Spec(
		static_cast<int32>(EWiesbadenWeaponId::Plasmacutter));
	TestTrue(TEXT("Plasmacutter schneidet"), Cutter.bCuts);
	TestTrue(TEXT("Schnittgeschwindigkeit > 0"), Cutter.CutSpeedCmPerS > 0.0f);
	TestTrue(TEXT("Rastwinkel der Schnittebene > 0"), Cutter.CutAngleStepDeg > 0.0f);

	// Mausrad-Blaettern: ueber beide Raender zurueck, Spruenge bleiben im Ring.
	const int32 Num = Table.Num();
	TestEqual(TEXT("Von hinten nach vorn"),
		WiesbadenWeapons::NextWeaponIndex(Num - 1, 1), 0);
	TestEqual(TEXT("Von vorn nach hinten"),
		WiesbadenWeapons::NextWeaponIndex(0, -1), Num - 1);
	TestEqual(TEXT("Zwei Rasten vor"),
		WiesbadenWeapons::NextWeaponIndex(0, 2), 2);
	TestEqual(TEXT("Sprung um den ganzen Ring bleibt"),
		WiesbadenWeapons::NextWeaponIndex(3, Num), 3);
	TestEqual(TEXT("Nullschritt wechselt nicht"),
		WiesbadenWeapons::NextWeaponIndex(5, 0), 5);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBallisticsTest,
	"WiesbadenReal.Weapons.Ballistics",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBallisticsTest::RunTest(const FString& Parameters)
{
	// Gerader Schuss ohne Gravitation: Position schreitet linear fort.
	{
		FWiesbadenProjectile P;
		P.Position = FVector::ZeroVector;
		P.Velocity = FVector(10000.0, 0.0, 0.0);   // 100 m/s nach +X
		P.RemainingRangeCm = 50000.0f;
		P.GravityCmPerS2 = 0.0f;

		const double Dt = 0.1;
		FWiesbadenFlightStep S = WiesbadenBallistics::Step(P, Dt);
		TestTrue(TEXT("X fortgeschritten"),
			FMath::IsNearlyEqual(S.SegmentEnd.X, 1000.0, 0.01));
		TestTrue(TEXT("Z ohne g unverandert"), FMath::IsNearlyZero(S.SegmentEnd.Z, 0.001));
		TestFalse(TEXT("Reichweite nicht am Ende"), S.bRangeEnd);
	}

	// Gravitation: nach 1 s exakt g/2 Tiefe bei 1 m/s (Anschauung aus der Schule).
	{
		FWiesbadenProjectile P;
		P.Position = FVector::ZeroVector;
		P.Velocity = FVector(100.0, 0.0, 0.0);     // 1 m/s, sehr langsam
		P.RemainingRangeCm = 100000.0f;
		P.GravityCmPerS2 = 980.665f;

		const double Dt = 0.1;
		double ZAfter1s = 0.0;
		for (int32 i = 0; i < 10; ++i)
		{
			const FWiesbadenFlightStep S = WiesbadenBallistics::Step(P, Dt);
			P = S.Projectile;
		}
		ZAfter1s = P.Position.Z;
		// Analytisch: -0.5 * g * 1^2 = -490.3 cm; symplektisch leicht daneben.
		TestTrue(FString::Printf(TEXT("Fall nach 1 s nahe analytisch (%.1f)"), ZAfter1s),
			FMath::IsNearlyEqual(ZAfter1s, -490.33, 60.0));
	}

	// Reichweitenende: kurze Restreichweite stoppt das Projektil dort.
	{
		FWiesbadenProjectile P;
		P.Position = FVector::ZeroVector;
		P.Velocity = FVector(2000.0, 0.0, 0.0);
		P.RemainingRangeCm = 500.0f;
		P.GravityCmPerS2 = 0.0f;

		const FWiesbadenFlightStep S = WiesbadenBallistics::Step(P, 0.5);
		TestTrue(TEXT("Reichweite zu Ende"), S.bRangeEnd);
		TestTrue(TEXT("Stopp an der Kante"),
			FMath::IsNearlyEqual(S.SegmentEnd.X, 500.0, 0.01));
	}

	// Segment-Kugel-Test: Treffer mittig, knapp daneben daneben.
	{
		TestTrue(TEXT("Segment durch Kugel"),
			WiesbadenBallistics::SegmentHitsSphere(FVector(0, 0, 100), FVector(1000, 0, 100),
				FVector(500, 0, 100), 50.0));
		TestFalse(TEXT("Segment an Kugel vorbei"),
			WiesbadenBallistics::SegmentHitsSphere(FVector(0, 0, 100), FVector(1000, 0, 100),
				FVector(500, 300, 100), 50.0));
		// Anfang/Ende exakt auf der Kugel zählen (Clamp auf [0,1]).
		TestTrue(TEXT("Start in Kugel"),
			WiesbadenBallistics::SegmentHitsSphere(FVector(500, 0, 100), FVector(2000, 0, 100),
				FVector(500, 0, 100), 50.0));
	}

	// Explosions-Schaden: voll im Zentrum, null am Rand, halb bei halbem Radius.
	{
		const FVector Centre(100.0, 200.0, 300.0);
		TestEqual(TEXT("Zentrum voll"),
			WiesbadenBallistics::BlastDamageAt(Centre, 350.0f, 120.0f, Centre), 120.0f);
		TestEqual(TEXT("Aussen null"),
			WiesbadenBallistics::BlastDamageAt(Centre, 350.0f, 120.0f, Centre + FVector(400.0, 0, 0)), 0.0f);
		TestTrue(TEXT("Halber Radius halber Schaden"),
			FMath::IsNearlyEqual(
				WiesbadenBallistics::BlastDamageAt(Centre, 350.0f, 120.0f, Centre + FVector(175.0, 0, 0)),
				60.0f, 0.5f));
	}

	// Fallhoehe-Formel: 100 m bei 400 m/s fallen ~30 cm (g/2 * (0.25)^2).
	{
		const double Drop = WiesbadenBallistics::DropAtDistance(10000.0, 40000.0, 980.665);
		TestTrue(FString::Printf(TEXT("Fallhoehe plausibel (%.2f cm)"), Drop),
			FMath::IsNearlyEqual(Drop, 30.6, 1.0));
	}

	return true;
}
