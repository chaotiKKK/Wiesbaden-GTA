// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_EDITOR

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/WiesbadenGameMode.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Vehicles/WiesbadenHelicopter.h"
#include "Vehicles/WiesbadenLegacyHelicopter.h"

/**
 * Das ALTE Heli-Modell als Standstueck neben dem Spielerheli.
 *
 * Geprueft werden die vier Dinge, die am alten Modell tatsaechlich gemessen
 * werden mussten und die man im Spiel nicht als Fehler sieht: die Mesh-Pfade
 * (ein Tippfehler laesst nur ein leeres Standstueck stehen), die Mastachse
 * (ohne die gemessenen Nabenversaetze kreisen die Rotoren neben dem Mast), die
 * Hoehen (345 / 300 cm) und die Materialien (Rumpf-Tarnung, dunkle Rotoren).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLegacyHelicopterModelTest,
	"WiesbadenReal.Vehicles.LegacyHelicopterModell",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FLegacyHelicopterModelTest::RunTest(const FString& Parameters)
{
	AWiesbadenLegacyHelicopter* CDO = GetMutableDefault<AWiesbadenLegacyHelicopter>();
	if (!CDO)
	{
		AddError(TEXT("Kein CDO von AWiesbadenLegacyHelicopter"));
		return false;
	}

	// Ein Standstueck darf nicht ticken - sonst laufen Rotordrehzahl, Schwerkraft
	// oder Audio darauf an, und es bewegt sich irgendwann von selbst.
	TestFalse(TEXT("Standstueck tickt nicht"), CDO->PrimaryActorTick.bCanEverTick);

	// -- Netze ---------------------------------------------------------------
	UStaticMeshComponent* Fuselage = CDO->GetFuselageMesh();
	TestNotNull(TEXT("Rumpfkomponente vorhanden"), Fuselage);
	if (!Fuselage)
	{
		return false;
	}

	UStaticMesh* Body = Fuselage->GetStaticMesh();
	if (!Body)
	{
		// Frischer Checkout ohne die Landmarken-Netze: Hinweis statt Fehler.
		AddInfo(TEXT("Altes Heli-Netz fehlt (Assets nicht im Projekt)"));
		return true;
	}
	TestEqual(TEXT("Rumpfnetz"), Body->GetName(), FString(TEXT("SM_HeliBody")));

	UStaticMeshComponent* Upper = CDO->GetUpperRotorMesh();
	UStaticMeshComponent* Lower = CDO->GetLowerRotorMesh();
	TestNotNull(TEXT("oberer Rotor vorhanden"), Upper);
	TestNotNull(TEXT("unterer Rotor vorhanden"), Lower);
	if (Upper && Upper->GetStaticMesh())
	{
		TestEqual(TEXT("oberes Rotornetz"), Upper->GetStaticMesh()->GetName(),
			FString(TEXT("SM_HeliRotorUpper")));
	}
	if (Lower && Lower->GetStaticMesh())
	{
		TestEqual(TEXT("unteres Rotornetz"), Lower->GetStaticMesh()->GetName(),
			FString(TEXT("SM_HeliRotorLower")));
	}

	// -- Materialien ---------------------------------------------------------
	TestNotNull(TEXT("Rumpf-Material"), Fuselage->GetMaterial(0));
	if (Fuselage->GetMaterial(0))
	{
		TestEqual(TEXT("Rumpf traegt die alte Zell-Tarnung"),
			Fuselage->GetMaterial(0)->GetName(), FString(TEXT("M_WbHelicopter")));
	}
	if (Upper && Upper->GetMaterial(0))
	{
		TestEqual(TEXT("Rotor traegt das Rotor-Material"),
			Upper->GetMaterial(0)->GetName(), FString(TEXT("M_HeliRotorBase")));
	}

	// -- Mastachse und Nabenhoehen -------------------------------------------
	// Das alte Modell ist 1,007 m lang (Faktor 14,5); Mast bei (1|-5) im Rumpf,
	// Rotornabe bei (0|33) im Rotormesh. Beide Versaetze sind MESSUNGEN.
	constexpr double ModelScale = 1460.0 / 100.7;
	const FVector ExpectedMast = FRotator(0.0, -90.0, 0.0)
		.RotateVector(FVector(1.0, -5.0, 0.0)) * ModelScale;
	const FVector ExpectedSelfHub = FRotator(0.0, -90.0, 0.0)
		.RotateVector(FVector(0.0, 33.0, 0.0)) * ModelScale;

	const TPair<UStaticMeshComponent*, double> Rotors[] = {
		{ Upper, 345.0 },
		{ Lower, 300.0 },
	};
	for (const TPair<UStaticMeshComponent*, double>& R : Rotors)
	{
		if (!R.Key)
		{
			continue;
		}
		USceneComponent* Hub = R.Key->GetAttachParent();
		TestNotNull(FString::Printf(TEXT("Nabe bei %.0f cm vorhanden"), R.Value), Hub);
		if (!Hub)
		{
			continue;
		}

		const FVector HubLoc = Hub->GetRelativeLocation();
		const FVector BladeLoc = R.Key->GetRelativeLocation();

		TestTrue(FString::Printf(TEXT("Nabe auf %.0f cm (ist %.1f)"), R.Value, HubLoc.Z),
			FMath::IsNearlyEqual(HubLoc.Z, R.Value, 0.5));
		TestTrue(FString::Printf(TEXT("Nabe auf dem Mast: (%.1f|%.1f) erwartet (%.1f|%.1f)"),
			HubLoc.X, HubLoc.Y, ExpectedMast.X, ExpectedMast.Y),
			FMath::IsNearlyEqual(HubLoc.X, ExpectedMast.X, 1.0)
			&& FMath::IsNearlyEqual(HubLoc.Y, ExpectedMast.Y, 1.0));
		// Der Rotor traegt seinen eigenen Ursprung: nur mit dem GEDREHTEN
		// Nabenversatz bleibt die Scheibe ueber dem Mast.
		TestTrue(FString::Printf(TEXT("Blattversatz hebt den Nabenursprung auf: (%.1f|%.1f) erwartet (%.1f|%.1f)"),
			BladeLoc.X, BladeLoc.Y, -ExpectedSelfHub.X, -ExpectedSelfHub.Y),
			FMath::IsNearlyEqual(BladeLoc.X, -ExpectedSelfHub.X, 1.0)
			&& FMath::IsNearlyEqual(BladeLoc.Y, -ExpectedSelfHub.Y, 1.0));
	}

	// -- Standmasse ----------------------------------------------------------
	// Das Mesh ist 100,7 cm lang und wird auf 14,6 m skaliert (Faktor 14,5);
	// der obere Rotor misst im Modell 87,0 cm ueber die laengste Achse ->
	// 12,6 m. Beide Zahlen als SPANNE: ein falscher Massstab (Faktor 1,0
	// statt 14,5) faellt damit sofort auf, ein kleiner Messfehler nicht.
	TestTrue(FString::Printf(TEXT("Rumpflaenge %.0f cm (erwartet 1400..1520)"), CDO->GetNoseToTailCm()),
		CDO->GetNoseToTailCm() > 1400.0 && CDO->GetNoseToTailCm() < 1520.0);
	TestTrue(FString::Printf(TEXT("oberer Rotorkreis %.0f cm (erwartet 1200..1320)"),
		CDO->GetUpperRotorDiameterCm()),
		CDO->GetUpperRotorDiameterCm() > 1200.0 && CDO->GetUpperRotorDiameterCm() < 1320.0);

	// Der Actor-Ursprung muss auf der RUMPFUNTERSEITE liegen: nur dann setzt die
	// Bodensuche beim Aufstellen den Rumpf auf die Strasse. Gemessene Bounds
	// z 0..17,1 cm - der Ursprung ist NICHT der Mittelpunkt.
	if (Body)
	{
		const double MinZ = Body->GetBoundingBox().Min.Z;
		TestTrue(FString::Printf(TEXT("Rumpfunterkante z=%.1f cm (erwartet ~0)"), MinZ),
			FMath::Abs(MinZ) < 5.0);
		TestTrue(FString::Printf(TEXT("Rumpf senkrecht oberhalb des Ursprungs: MaxZ=%.1f"),
			Body->GetBoundingBox().Max.Z), Body->GetBoundingBox().Max.Z > 0.0);
	}

	return true;
}

/**
 * Standabstand der beiden Helikopter.
 *
 * Der Fehler, den der Test ausschliesst: die Rotorkreise beider Maschinen
 * durchdringen sich, oder die Rumpfspitzen stehen ineinander. Beim Ka-52 liegen
 * die Scheiben nur ~30 cm uebereinander (unten 3,77 m gegen 3,45 m am alten
 * Modell) - der Abstand in der Reihe ist die einzige Sicherung.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliStandDistanceTest,
	"WiesbadenReal.Vehicles.HeliStandAbstand",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FHeliStandDistanceTest::RunTest(const FString& Parameters)
{
	const AWiesbadenHelicopter* HeliCDO = GetDefault<AWiesbadenHelicopter>();
	const AWiesbadenLegacyHelicopter* LegacyCDO = GetDefault<AWiesbadenLegacyHelicopter>();
	if (!HeliCDO || !LegacyCDO)
	{
		AddError(TEXT("CDO fehlt"));
		return false;
	}

	const double OwnDisc = HeliCDO->GetUpperRotorDiameterCm();
	const double OwnLength = HeliCDO->GetNoseToTailCm();
	const double LegacyDisc = LegacyCDO->GetUpperRotorDiameterCm();
	const double LegacyLength = LegacyCDO->GetNoseToTailCm();

	if (OwnDisc <= 0.0 || LegacyDisc <= 0.0)
	{
		AddInfo(TEXT("Ohne importierte Netze sind die Rotorkreise 0 - Abstandsformel separat geprueft"));
	}
	else
	{
		const double Stand = AWiesbadenGameMode::ComputeHelicopterStandDistanceCm(
			OwnDisc, LegacyDisc, OwnLength, LegacyLength);

		// Der Abstand muss ZWISCHEN den Scheiben 5 m Luft lassen - die Scheiben
		// sind die haertere Bedingung, weil sie sich nur 30 cm hoch ausweichen.
		const double DiscAirCm = Stand - OwnDisc * 0.5 - LegacyDisc * 0.5;
		TestTrue(FString::Printf(
			TEXT("Abstand %.1f m laesst %.1f m Luft zwischen den Rotorkreisen (%.1f / %.1f m)"),
			Stand / 100.0, DiscAirCm / 100.0, OwnDisc / 100.0, LegacyDisc / 100.0),
			DiscAirCm > 400.0);

		// Und die Rumpfspitzen duerfen sich nicht beruehren.
		TestTrue(FString::Printf(TEXT("Rumpfspitzen: %.1f m Luft"),
			(Stand - (OwnLength + LegacyLength) * 0.5) / 100.0),
			Stand > (OwnLength + LegacyLength) * 0.5);

		// Nebeneinander, nicht "irgendwo in der Stadt": die beiden Helikopter
		// sollen zusammen zu sehen sein. Mehr als 30 m waere kein Paar mehr.
		TestTrue(FString::Printf(TEXT("Abstand %.1f m bleibt ein Paar (<= 30 m)"), Stand / 100.0),
			Stand <= 3000.0);
	}

	// Datenreine Formel: halbe Durchmesser + 5 m Luft, mindestens halbe
	// Rumpflaengen + 2 m Luft, Untergrenze 8 m.
	TestTrue(TEXT("Rotorkreise gewinnen: 14 + 12 m Durchmesser -> 18 m"),
		FMath::IsNearlyEqual(
			AWiesbadenGameMode::ComputeHelicopterStandDistanceCm(1400.0, 1200.0, 100.0, 100.0),
			1800.0, 0.01));
	TestTrue(TEXT("lange Rumpfe gewinnen: 14,6 + 14,1 m -> 16,35 m"),
		FMath::IsNearlyEqual(
			AWiesbadenGameMode::ComputeHelicopterStandDistanceCm(100.0, 100.0, 1460.0, 1406.0),
			1633.0, 0.01));
	TestTrue(TEXT("ohne Masse bleibt der Mindestabstand"),
		FMath::IsNearlyEqual(
			AWiesbadenGameMode::ComputeHelicopterStandDistanceCm(0.0, 0.0, 0.0, 0.0),
			800.0, 0.01));
	TestTrue(TEXT("negative Masse wird geklemmt"),
		FMath::IsNearlyEqual(
			AWiesbadenGameMode::ComputeHelicopterStandDistanceCm(-500.0, -500.0, -100.0, -100.0),
			800.0, 0.01));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLegacyHelicopterStandSpotsTest,
	"WiesbadenReal.Vehicles.HeliStandplaetze",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FLegacyHelicopterStandSpotsTest::RunTest(const FString& Parameters)
{
	const FVector Anchor(1000.0, 2000.0, 300.0);
	constexpr double Yaw = 30.0;
	constexpr double StandCm = 1800.0;

	const TArray<FVector> Spots =
		AWiesbadenGameMode::BuildHelicopterStandCandidates(Anchor, Yaw, StandCm);

	TestTrue(TEXT("Es gibt Ausweichplaetze"), Spots.Num() > 1);

	// 1. Der GEWOHNTE Platz bleibt die erste Wahl: geradeaus, im gerechneten
	//    Abstand. Sonst haette die Fahrbahnpruefung die Aufstellung veraendert,
	//    auch wo gar keine Strasse liegt.
	const FVector Expected = Anchor + FRotator(0.0, Yaw, 0.0).Vector() * StandCm;
	TestTrue(TEXT("Erster Platz ist der bisherige (geradeaus, gerechneter Abstand)"),
		Spots[0].Equals(Expected, 0.01));

	// 2. Alle Plaetze liegen auf der Hoehe des Ankers - die Hoehe kommt erst
	//    aus dem Bodenlot, nicht aus der Faecherung.
	bool bSameHeight = true;
	for (const FVector& Spot : Spots)
	{
		bSameHeight = bSameHeight && FMath::IsNearlyEqual(Spot.Z, Anchor.Z, 0.01);
	}
	TestTrue(TEXT("Die Faecherung aendert die Hoehe nicht"), bSameHeight);

	// 3. Kein Platz liegt naeher als der gerechnete Standabstand: sonst
	//    koennten sich beim Ausweichen die Rotorkreise durchdringen - genau das,
	//    wogegen der Abstand gerechnet wurde.
	double MinDistance = TNumericLimits<double>::Max();
	for (const FVector& Spot : Spots)
	{
		MinDistance = FMath::Min(MinDistance, FVector::Dist2D(Spot, Anchor));
	}
	TestTrue(FString::Printf(TEXT("Naechster Platz %.2f m >= Standabstand %.2f m"),
		MinDistance / 100.0, StandCm / 100.0),
		MinDistance >= StandCm - 0.01);

	// 4. Erst seitlich ausweichen, dann weiter weg: unter den ersten Plaetzen
	//    muss der Abstand noch der gerechnete sein. Ein Sprung auf 2,4-fache
	//    Entfernung waere ein ganz anderes Bild.
	int32 SameDistanceCount = 0;
	for (const FVector& Spot : Spots)
	{
		if (FMath::IsNearlyEqual(FVector::Dist2D(Spot, Anchor), StandCm, 1.0))
		{
			++SameDistanceCount;
		}
	}
	TestTrue(FString::Printf(TEXT("%d Plaetze im gewohnten Abstand, nur die Richtung wechselt"),
		SameDistanceCount), SameDistanceCount >= 8);

	// 5. Keine zwei gleichen Plaetze - jede Probe soll eine neue Stelle testen.
	bool bAllDistinct = true;
	for (int32 i = 0; i < Spots.Num() && bAllDistinct; ++i)
	{
		for (int32 k = i + 1; k < Spots.Num(); ++k)
		{
			if (Spots[i].Equals(Spots[k], 1.0))
			{
				bAllDistinct = false;
				break;
			}
		}
	}
	TestTrue(TEXT("Alle Plaetze sind verschieden"), bAllDistinct);

	// 6. Die Faecherung deckt alle Richtungen ab - sonst bliebe eine Strasse,
	//    die genau quer liegt, unausweichlich.
	bool bSawOpposite = false;
	const FVector Backwards = Anchor + FRotator(0.0, Yaw + 180.0, 0.0).Vector() * StandCm;
	for (const FVector& Spot : Spots)
	{
		bSawOpposite = bSawOpposite || Spot.Equals(Backwards, 1.0);
	}
	TestTrue(TEXT("Auch die Gegenrichtung wird geprueft"), bSawOpposite);

	return true;
}

#endif // WITH_EDITOR
