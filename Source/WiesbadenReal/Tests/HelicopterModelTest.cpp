// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_EDITOR

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Vehicles/WiesbadenHelicopter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHelicopterModelTest,
	"WiesbadenReal.Vehicles.HelicopterModell",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Die Modell-Bindung des Helikopters pruefen - genau die vier Fehler, die hier
 * tatsaechlich aufgetreten sind (Ka-52-Neubau, Import 2026-09-17).
 *
 * Alle vier waren im Spiel NICHT als Fehler zu erkennen: Der Actor faellt
 * still auf Wuerfel zurueck, ein fehlendes Material rendert grau, und eine
 * Nabe ausserhalb der Mastachse sieht nur beim Drehen falsch aus.
 *
 *   1. MESH-PFAD. Die ConstructorHelpers-Pfade muessen exakt
 *      "<Paket>.<Objekt>" lauten. Ein Tippfehler kostet keinen Build, keinen
 *      Log-Eintrag - nur den Wuerfel-Rueckfall.
 *
 *   2. MASTACHSE. Der Blatt-Component traegt -Hubhoehe und die Korrektur des
 *      gemessenen Scheiben-Drehpunkts (ComputeRotorMountOffset). Ein
 *      XY-Versatz von "~0" ist gerade NICHT richtig: der Drehpunkt der
 *      unteren Scheibe liegt 5,4 cm neben dem Modellursprung (gemessen an der
 *      Importquelle, Saved/Diagnose/ka52/rotorachse_fbx.txt). Ohne Korrektur
 *      kreist der untere Rotor sichtbar neben der Nabe - der Fehler
 *      "Rotor schlenkert".
 *
 *   3. MATERIAL. `AssetTools.create_asset` legt ein Material nur IM SPEICHER
 *      an; ohne `save_loaded_asset` zeigen die gespeicherten Meshes auf ein
 *      Asset, das es auf der Platte nicht gibt (grauer Rumpf, Texturen
 *      ungenutzt) - der Commandlet-Log meldete trotzdem Erfolg.
 *
 *   4. NAVIDEE. Der Rumpf liegt mit der Sohle auf z = 0; der obere Rotor
 *      erreicht damit die Bauhoehe von ~5 m. Rutscht der Nullpunkt, steht der
 *      Heli im Boden oder schwebt.
 */
bool FHelicopterModelTest::RunTest(const FString& Parameters)
{
	// GetMutableDefault statt GetDefault: FindObject<T>() nimmt in UE 5.8 einen
	// NICHT-const UObject* als Outer (sonst C2672 "keine passende Ueberladung").
	// Der Zeiger wird hier nur gelesen, nie veraendert.
	AWiesbadenHelicopter* CDO = GetMutableDefault<AWiesbadenHelicopter>();
	if (!CDO)
	{
		AddError(TEXT("Kein CDO von AWiesbadenHelicopter"));
		return false;
	}

	struct FExpect
	{
		const TCHAR* Component;
		const TCHAR* Mesh;
	};
	const FExpect Expected[] = {
		{ TEXT("FuselageMesh"),    TEXT("Fuselage") },
		{ TEXT("MainRotorBlade"),  TEXT("Rotor_Upper") },
		{ TEXT("LowerRotorBlade"), TEXT("Rotor_Lower") },
	};

	// Sammelt die GEMESSENEN Ist-Werte (nicht die erwarteten) fuer die
	// Zusammenfassungszeile am Ende: ein gruener Lauf soll ohne Log-
	// Quervergleich belegen, WAS tatsaechlich an den Komponenten haengt.
	TArray<FString> Ist;

	bool bAnyMesh = false;
	for (const FExpect& E : Expected)
	{
		UStaticMeshComponent* Comp = FindObject<UStaticMeshComponent>(CDO, E.Component);
		if (!Comp)
		{
			AddError(FString::Printf(TEXT("Komponente %s fehlt am CDO"), E.Component));
			continue;
		}

		UStaticMesh* Mesh = Comp->GetStaticMesh();
		if (!Mesh)
		{
			// Frischer Checkout ohne importiertes Modell: Hinweis statt Fehler
			// (das Mesh entsteht erst durch Tools/import_ka52.cmd).
			AddInfo(FString::Printf(TEXT("%s ohne Mesh - Tools/import_ka52.cmd laufen lassen"),
				E.Component));
			Ist.Add(FString::Printf(TEXT("%s=OHNE_MESH"), E.Component));
			continue;
		}
		bAnyMesh = true;
		Ist.Add(FString::Printf(TEXT("%s=%s"), E.Component, *Mesh->GetName()));

		TestEqual(FString::Printf(TEXT("%s traegt %s"), E.Component, E.Mesh),
			Mesh->GetName(), FString(E.Mesh));

		// Material: jeder Slot muss das PBR tragen (leere Slots bleiben grau).
		// Die Ist-Zeile zaehlt die Namen aus, damit ein leerer Slot auch bei
		// gruenem Lauf sichtbar bleibt (z.B. "Slots=10 [M_Ka52PBR x9]").
		TMap<FString, int32> MatCounts;
		for (int32 Slot = 0; Slot < Mesh->GetStaticMaterials().Num(); ++Slot)
		{
			UMaterialInterface* Mat = Mesh->GetStaticMaterials()[Slot].MaterialInterface;
			TestNotNull(FString::Printf(TEXT("%s Slot %d hat ein Material"), E.Mesh, Slot), Mat);
			if (Mat)
			{
				MatCounts.FindOrAdd(Mat->GetName())++;
				TestEqual(FString::Printf(TEXT("%s Slot %d = M_Ka52PBR"), E.Mesh, Slot),
					Mat->GetName(), FString(TEXT("M_Ka52PBR")));
			}
		}

		TArray<FString> MatTexte;
		for (const TPair<FString, int32>& P : MatCounts)
		{
			MatTexte.Add(FString::Printf(TEXT("%s x%d"), *P.Key, P.Value));
		}
		Ist.Add(FString::Printf(TEXT("%s Slots=%d [%s]"), E.Mesh,
			Mesh->GetStaticMaterials().Num(), *FString::Join(MatTexte, TEXT(", "))));
	}

	// Die Ist-Zeile geht per UE_LOG statt AddInfo ins Log: AddInfo-Events
	// erscheinen im headless-Lauf nur als leere BeginEvents/EndEvents-Zeile
	// ("Automation RunTests"), die Zeile hier steht in JEDEM Lauf im Log.
	const auto LogIst = [&Ist](const TCHAR* Suffix)
	{
		UE_LOG(LogTemp, Display, TEXT("HelicopterModell-Ist: %s%s"),
			*FString::Join(Ist, TEXT(" | ")), Suffix);
	};

	if (!bAnyMesh)
	{
		// Ohne Modell sind die Geometrie-Pruefungen sinnlos - der Hinweis oben
		// sagt, wie das Mesh entsteht. Die Ist-Zeile steht trotzdem im Log,
		// damit ein gruener Lauf hier nicht als "geprueft" missverstanden wird.
		LogIst(TEXT(" (ohne importiertes Mesh - nur Hinweise)"));
		return true;
	}

	// -- Rumpfmasse ----------------------------------------------------------
	if (UStaticMeshComponent* Fuselage = FindObject<UStaticMeshComponent>(CDO, TEXT("FuselageMesh")))
	{
		if (UStaticMesh* Mesh = Fuselage->GetStaticMesh())
		{
			const FVector Size = Mesh->GetBoundingBox().GetSize();
			// Ka-52: 14,1 m lang (Y im Modell), 8,7 m ueber die Stummelfluegel,
			// 2,95 m hoch. Spannen fangen Massstabsfehler (Faktor 15!) sicher ab.
			TestTrue(FString::Printf(TEXT("Rumpflaenge %.0f cm (erwartet 1200..1600)"), Size.Y),
				Size.Y > 1200.0 && Size.Y < 1600.0);
			TestTrue(FString::Printf(TEXT("Spannweite %.0f cm (erwartet 700..1000)"), Size.X),
				Size.X > 700.0 && Size.X < 1000.0);
			TestTrue(FString::Printf(TEXT("Rumpfhoehe %.0f cm (erwartet 200..400)"), Size.Z),
				Size.Z > 200.0 && Size.Z < 400.0);
			// Sohle auf 0: sonst steht der Heli im Boden.
			TestTrue(FString::Printf(TEXT("Rumpf-Unterkante z=%.1f cm (erwartet ~0)"),
				Mesh->GetBoundingBox().Min.Z),
				FMath::Abs(Mesh->GetBoundingBox().Min.Z) < 10.0);

			Ist.Add(FString::Printf(TEXT("Rumpf %.0fx%.0fx%.0f cm (X/Y/Z), Sohle z=%.1f"),
				Size.X, Size.Y, Size.Z, Mesh->GetBoundingBox().Min.Z));
		}
	}

	// -- Mastachse -----------------------------------------------------------
	// bUnten gehoert zur Zuordnung: die gemessenen Drehpunkte der beiden
	// Scheiben sind verschiedene Zahlen, und ein Tausch wuerde an der
	// Assertion unten auffallen.
	const struct
	{
		const TCHAR* HubName;
		const TCHAR* BladeName;
		bool bUnten;
	} Rotors[] = {
		{ TEXT("MainRotorHub"),  TEXT("MainRotorBlade"),  false },
		{ TEXT("LowerRotorHub"), TEXT("LowerRotorBlade"), true },
	};

	for (const auto& R : Rotors)
	{
		USceneComponent* Hub = FindObject<USceneComponent>(CDO, R.HubName);
		USceneComponent* Blade = FindObject<USceneComponent>(CDO, R.BladeName);
		if (!Hub || !Blade)
		{
			AddError(FString::Printf(TEXT("%s/%s fehlt am CDO"),
				R.HubName, R.BladeName));
			continue;
		}

		const FVector HubLoc = Hub->GetRelativeLocation();
		const FVector BladeLoc = Blade->GetRelativeLocation();

		// Der Blatt-Component hebt die eingebackene Modellage wieder auf: sein
		// z-Versatz muss exakt die negative Nabenhoehe sein.
		TestTrue(FString::Printf(TEXT("%s: Blatt-z %.1f hebt Nabe %.1f auf"),
			R.HubName, BladeLoc.Z, HubLoc.Z),
			FMath::IsNearlyEqual(BladeLoc.Z, -HubLoc.Z, 0.5f));

		// Der XY-Versatz ist die KORREKTUR, nicht null: der Drehpunkt der
		// Scheibe liegt nachweislich nicht auf (0, 0) (unten 5,4 cm in X,
		// gemessen an der Importquelle). "Versatz 0" war die alte Annahme,
		// und sie liess den unteren Rotor 5 cm neben der Nabe kreisen. Der
		// Versatz wird deshalb gegen dieselbe Funktion geprueft, die ihn
		// erzeugt - ein fest verdrahteter Wert an zweiter Stelle faellt auf.
		const FVector Soll = AWiesbadenHelicopter::ComputeRotorMountOffset(
			AWiesbadenHelicopter::GetRotorDrehpunktCm(R.bUnten),
			Blade->GetRelativeRotation(), HubLoc.Z);
		const FVector Fehler = BladeLoc - Soll;
		TestTrue(FString::Printf(
			TEXT("%s: Blatt-Versatz (%.2f, %.2f) cm entspricht der Achsenkorrektur "
				"(%.2f, %.2f) cm, Abweichung %.4f cm"),
			R.HubName, BladeLoc.X, BladeLoc.Y, Soll.X, Soll.Y,
			FMath::Sqrt(static_cast<double>(Fehler.X) * Fehler.X
				+ static_cast<double>(Fehler.Y) * Fehler.Y)),
			FMath::Abs(Fehler.X) < 0.01f && FMath::Abs(Fehler.Y) < 0.01f);

		// Und das Ergebnis muss auf der Mastachse liegen: Nabe + Versatz +
		// Modelldrehung hebt den gemessenen Drehpunkt auf.
		const FVector AufDerAchse = HubLoc + BladeLoc
			+ Blade->GetRelativeRotation().RotateVector(
				AWiesbadenHelicopter::GetRotorDrehpunktCm(R.bUnten));
		TestTrue(FString::Printf(
			TEXT("%s: gemessener Drehpunkt liegt nach der Kette bei (%.4f, %.4f) cm"),
			R.HubName, AufDerAchse.X, AufDerAchse.Y),
			FMath::Abs(AufDerAchse.X) < 0.01f && FMath::Abs(AufDerAchse.Y) < 0.01f);

		// Nabenhoehen des Neubaus (z 495 / 376,5 cm), Abstand ~118 cm.
		TestTrue(FString::Printf(TEXT("%s: Nabenhoehe %.1f cm (erwartet 370..500)"),
			R.HubName, HubLoc.Z), HubLoc.Z > 370.0 && HubLoc.Z < 500.0);

		Ist.Add(FString::Printf(TEXT("%s Nabe z=%.1f, Blatt (%.2f, %.2f, %.1f) cm"),
			R.HubName, HubLoc.Z, BladeLoc.X, BladeLoc.Y, BladeLoc.Z));
	}

	// Eine Zeile, alles drin: Mesh-Namen, Material je Slot, Rumpfmasse,
	// Nabenhoehen und Achsabstand - der Beleg fuer den gruenen Lauf.
	LogIst(TEXT(""));
	return true;
}

#endif // WITH_EDITOR
