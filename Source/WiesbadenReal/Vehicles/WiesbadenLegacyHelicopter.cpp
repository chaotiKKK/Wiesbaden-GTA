// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenLegacyHelicopter.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AWiesbadenLegacyHelicopter::AWiesbadenLegacyHelicopter()
{
	// NICHT selbst uebernehmen. Die Basisklasse steht auf Player0 - der zweite
	// Hubschrauber risse den Spieler sonst beim Aufstellen aus dem Auto, und
	// man faende sich ohne Zutun in der Luft wieder. Eingestiegen wird mit F.
	AutoPossessPlayer = EAutoReceiveInput::Disabled;

	// Dieselben Netze wie der Spielerheli VOR dem Ka-52-Neubau.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BodyAsset(
		TEXT("/Game/Assets/Landmarks/HeliBody/StaticMeshes/SM_HeliBody.SM_HeliBody"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> UpperAsset(
		TEXT("/Game/Assets/Landmarks/HeliRotorUpper/StaticMeshes/SM_HeliRotorUpper.SM_HeliRotorUpper"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> LowerAsset(
		TEXT("/Game/Assets/Landmarks/HeliRotorLower/StaticMeshes/SM_HeliRotorLower.SM_HeliRotorLower"));

	// Lackierung: eigenes Material, damit die beiden Maschinen im Bild
	// auseinanderzuhalten sind (siehe M_WbHeliCivil).
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PaintAsset(
		TEXT("/Game/Materials/City/M_WbHeliCivil.M_WbHeliCivil"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> RotorAsset(
		TEXT("/Game/Materials/City/M_WbHeliRotor.M_WbHeliRotor"));

	// Massstab des ALTEN Modells: es ist im Original nur 100,7 cm lang
	// (Rumpflaenge 14,6 m -> Faktor 14,5).
	constexpr float ModelBodyLengthCm = 100.7f;
	constexpr float TargetBodyLengthCm = 1460.0f;
	const float ModelScale = TargetBodyLengthCm / ModelBodyLengthCm;

	// Gierdrehung, die Modell-+Y auf Welt-+X legt (Nase nach vorn).
	const FRotator ModelYaw(0.0f, -90.0f, 0.0f);
	constexpr float UpperRotorHeightCm = 345.0f;
	constexpr float LowerRotorHeightCm = 300.0f;

	// Wo sitzt der Mast am Rumpf bzw. die Nabe im Rotormesh? Beides sind
	// MESSUNGEN am alten Modell (Vertex-Median der Rotoren, Schwerpunkt der
	// hohen Aufbauten am Rumpf) - ohne sie kreisen die Blaetter neben dem Mast.
	const FVector2D MastInBodyModelCm(1.0f, -5.0f);
	const FVector2D RotorHubInModelCm(0.0f, 33.0f);
	const FVector MastOffset =
		ModelYaw.RotateVector(FVector(MastInBodyModelCm.X, MastInBodyModelCm.Y, 0.0f)) * ModelScale;
	const FVector RotorSelfHub =
		ModelYaw.RotateVector(FVector(RotorHubInModelCm.X, RotorHubInModelCm.Y, 0.0f)) * ModelScale;

	// -- Rumpf ---------------------------------------------------------------
	// Der Modell-Ursprung liegt an der Rumpfunterseite (gemessene Bounds
	// z 0..17,1 cm) - dieselbe Konvention wie beim Ka-52 (Ursprung auf der
	// Kufenebene). Deshalb bleibt der Hoehenversatz null, und das Aufstellen
	// rechnet fuer beide Maschinen gleich.
	if (FuselageMesh && BodyAsset.Succeeded())
	{
		FuselageMesh->SetStaticMesh(BodyAsset.Object);
		FuselageMesh->SetRelativeRotation(ModelYaw);
		FuselageMesh->SetRelativeScale3D(FVector(ModelScale));
		FuselageMesh->SetRelativeLocation(FVector::ZeroVector);
		if (PaintAsset.Succeeded())
		{
			FuselageMesh->SetMaterial(0, PaintAsset.Object);
		}
	}

	// Heckausleger, Flosse und Heckrotor sind Bauteile der WUERFEL-Notloesung
	// der Basisklasse. Das alte Modell traegt sein Heck im Rumpfnetz - und ein
	// Koaxialheli hat ohnehin keinen Heckrotor.
	if (TailBoomMesh) { TailBoomMesh->SetVisibility(false); }
	if (TailFinMesh) { TailFinMesh->SetVisibility(false); }
	if (TailRotorBlade) { TailRotorBlade->SetVisibility(false); }

	// Keine Ka-52-Kabine: sie haengt an FuselageMesh und erbte dessen Faktor
	// 14,5 - ein 38-m-Kasten ueber dem Garagenhof (27.09.2026).
	if (CockpitMesh) { CockpitMesh->SetStaticMesh(nullptr); }

	// -- Koaxiales Rotorpaar --------------------------------------------------
	// Die Naben sitzen auf den gemessenen Masthoehen des alten Modells, nicht
	// auf denen des Ka-52 (495 / 376,5 cm) - sonst schwebten die Scheiben
	// anderthalb Meter ueber dem Rumpf.
	if (MainRotorHub)
	{
		MainRotorHub->SetRelativeLocation(
			FVector(MastOffset.X, MastOffset.Y, UpperRotorHeightCm));
	}
	if (MainRotorBlade && UpperAsset.Succeeded())
	{
		MainRotorBlade->SetStaticMesh(UpperAsset.Object);
		MainRotorBlade->SetRelativeRotation(ModelYaw);
		MainRotorBlade->SetRelativeScale3D(FVector(ModelScale));
		// Der Rotor traegt seinen eigenen Ursprung: um den Nabenversatz
		// zurueckgeschoben (GEDREHT abgezogen, in denselben Achsen wie die
		// Geometrie) kreist die Scheibe genau ueber dem Mast. KEIN z-Ausgleich
		// wie beim Ka-52 - dessen Rotormesh liegt in Gebaeudehoehe, dieses hier
		// hat seinen Ursprung schon in der Rotorebene.
		MainRotorBlade->SetRelativeLocation(-RotorSelfHub);
		if (RotorAsset.Succeeded())
		{
			MainRotorBlade->SetMaterial(0, RotorAsset.Object);
		}
	}

	if (LowerRotorHub)
	{
		LowerRotorHub->SetRelativeLocation(
			FVector(MastOffset.X, MastOffset.Y, LowerRotorHeightCm));
	}
	if (LowerRotorBlade && LowerAsset.Succeeded())
	{
		LowerRotorBlade->SetStaticMesh(LowerAsset.Object);
		LowerRotorBlade->SetRelativeRotation(ModelYaw);
		LowerRotorBlade->SetRelativeScale3D(FVector(ModelScale));
		LowerRotorBlade->SetRelativeLocation(-RotorSelfHub);
		if (RotorAsset.Succeeded())
		{
			LowerRotorBlade->SetMaterial(0, RotorAsset.Object);
		}
	}

	// -- Rotor-Blur-Scheiben auf DIESE Rotorkreise ---------------------------
	// Die Basisklasse skaliert sie auf die Ka-52-Kreise (15,6 / 16,0 m). Das
	// alte Modell hat kleinere Rotoren; stehen bliebe eine Scheibe, die weit
	// ueber die Blattspitzen hinausragt. Der Massstab kommt aus der Geometrie
	// (Zylinder-Durchmesser 100 cm), nicht aus einer zweiten Zahl.
	auto ScheibeAnpassen = [ModelScale](UStaticMeshComponent* Blur, const UStaticMesh* Rotor)
	{
		if (!Blur || !Rotor)
		{
			return;
		}
		const FVector Size = Rotor->GetBoundingBox().GetSize();
		const float DurchmesserM =
			FMath::Max(Size.X, Size.Y) * ModelScale * 0.01f;
		Blur->SetRelativeScale3D(FVector(DurchmesserM, DurchmesserM, 0.02f));
	};
	ScheibeAnpassen(UpperRotorBlur, UpperAsset.Succeeded() ? UpperAsset.Object : nullptr);
	ScheibeAnpassen(LowerRotorBlur, LowerAsset.Succeeded() ? LowerAsset.Object : nullptr);
}
