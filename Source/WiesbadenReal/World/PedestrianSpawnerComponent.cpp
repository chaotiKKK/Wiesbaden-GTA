// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/PedestrianSpawnerComponent.h"

#include "WiesbadenReal.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/**
	 * Figur der Fussgaenger.
	 *
	 * Auf dem Rechner lag kein einziges Personenmodell - gesucht wurde das
	 * gesamte Benutzerprofil. Diese Figur ist deshalb in Blender erzeugt
	 * worden: 900 Dreiecke, 175 cm, Ursprung zwischen den Fuessen. Bewusst
	 * niedrig aufgeloest, weil Dutzende gleichzeitig als Instanzen laufen.
	 */
	// Bessere Figur (Blender, fuenf Materialzonen: Haut/Hemd/Hose/Haare/Schuhe).
	// Die Kleidungsfarben kommen PRO INSTANZ aus Custom-Data (siehe Spawner) -
	// darum werden die eigenen Mesh-Materialien NICHT ueberschrieben.
	const TCHAR* PersonMeshPath =
		TEXT("/Game/Assets/People/Varied/SM_WbPed2_1/StaticMeshes/SM_WbPed2_1.SM_WbPed2_1");

	/** Die vier Gangphasen. Reihenfolge = Schrittzyklus. */
	const TCHAR* PosePaths[] = {
		TEXT("/Game/Assets/People/Varied/SM_WbPed2_0/StaticMeshes/SM_WbPed2_0.SM_WbPed2_0"),
		TEXT("/Game/Assets/People/Varied/SM_WbPed2_1/StaticMeshes/SM_WbPed2_1.SM_WbPed2_1"),
		TEXT("/Game/Assets/People/Varied/SM_WbPed2_2/StaticMeshes/SM_WbPed2_2.SM_WbPed2_2"),
		TEXT("/Game/Assets/People/Varied/SM_WbPed2_3/StaticMeshes/SM_WbPed2_3.SM_WbPed2_3"),
	};

	/** Zahl der Custom-Data-Floats je Instanz: Hemd RGB (0..2), Hose RGB (3..5),
	 *  Hautton (6). Das Material liest genau diese Indizes. */
	constexpr int32 PedCustomDataFloats = 7;

	/**
	 * Rueckfall: der Engine-Zylinder.
	 *
	 * Bis hierher war das der NORMALFALL - PedestrianMesh war nicht zugewiesen,
	 * und die Fussgaenger liefen als Zylinder durch die Stadt.
	 */
	const TCHAR* FallbackMeshPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");

	/** Kanonisches Fussgaenger-Material (Tools/build_materials.py). */
	const TCHAR* FallbackMaterialPath = TEXT("/Game/Materials/City/M_WbPedestrian.M_WbPedestrian");

	/** Kantenlaenge des Engine-Grundkoerpers in cm. */
	constexpr double EngineShapeSizeCm = 100.0;
}

UPedestrianSpawnerComponent::UPedestrianSpawnerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	Instances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("PedestrianInstances"));
	Instances->SetupAttachment(this);

	// Fussgaenger brauchen keine Kollision: die Simulation fuehrt sie auf dem
	// Gehweg, und ein Kollisionskoerper je Figur waere bei mehreren hundert
	// Instanzen teuer, ohne dass er etwas beitraegt.
	Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Instances->SetCastShadow(true);
}

void UPedestrianSpawnerComponent::OnRegister()
{
	Super::OnRegister();
	EnsureMeshAndMaterial();
}

void UPedestrianSpawnerComponent::EnsureMeshAndMaterial()
{
	if (bMeshReady || !Instances)
	{
		return;
	}

	UStaticMesh* Mesh = PedestrianMesh;
	if (!Mesh)
	{
		Mesh = LoadObject<UStaticMesh>(nullptr, PersonMeshPath);
	}
	if (!Mesh)
	{
		Mesh = LoadObject<UStaticMesh>(nullptr, FallbackMeshPath);
		UE_LOG(LogWbCore, Warning,
			TEXT("Fussgaenger-Spawner: %s nicht ladbar - es laufen wieder Zylinder."),
			PersonMeshPath);
		if (!Mesh)
		{
			UE_LOG(LogWbCore, Warning,
				TEXT("Fussgaenger-Spawner: weder eigenes Mesh noch %s ladbar - es wird nichts gezeichnet."),
				FallbackMeshPath);
			return;
		}
	}

	Instances->SetStaticMesh(Mesh);

	// Zusaetzliche Pools fuer die Gangphasen.
	//
	// Der Grundpool bleibt bestehen und traegt die STEHENDEN Figuren; die
	// gehenden verteilen sich auf die vier Phasen. Fehlt eine Phase, laeuft
	// alles wie bisher ueber den Grundpool - ohne Animation, aber sichtbar.
	if (PoseInstances.Num() == 0 && GetOwner())
	{
		for (int32 Phase = 0; Phase < WalkPoseCount; ++Phase)
		{
			UStaticMesh* PoseMesh = LoadObject<UStaticMesh>(nullptr, PosePaths[Phase]);
			if (!PoseMesh)
			{
				UE_LOG(LogWbCore, Warning,
					TEXT("Fussgaenger: Gangphase %d (%s) fehlt - es wird nicht animiert."),
					Phase, PosePaths[Phase]);
				PoseInstances.Reset();
				break;
			}

			UInstancedStaticMeshComponent* Pool = NewObject<UInstancedStaticMeshComponent>(
				GetOwner(), *FString::Printf(TEXT("PedestrianPose%d"), Phase));
			if (!Pool)
			{
				PoseInstances.Reset();
				break;
			}

			Pool->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
			Pool->SetStaticMesh(PoseMesh);
			Pool->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Pool->SetCastShadow(true);
			Pool->RegisterComponent();
			PoseInstances.Add(Pool);
		}

		if (PoseInstances.Num() == WalkPoseCount)
		{
			UE_LOG(LogWbCore, Log,
				TEXT("Fussgaenger: %d Gangphasen geladen - Figuren werden animiert."),
				WalkPoseCount);
		}
	}

	// Custom-Data-Floats fuer die pro-Instanz-Kleidungsfarben (Hemd/Hose/Hautton).
	// Auf ALLEN Pools setzen, bevor Instanzen entstehen.
	Instances->NumCustomDataFloats = PedCustomDataFloats;
	for (UInstancedStaticMeshComponent* Pool : PoseInstances)
	{
		if (Pool)
		{
			Pool->NumCustomDataFloats = PedCustomDataFloats;
		}
	}

	// Zonierte Figur (mehr als ein Materialslot: Haut/Hemd/Hose/Haare/Schuhe)
	// behaelt ihre EIGENEN Materialien - sie tragen die pro-Instanz-Farben. Nur
	// ein ausdruecklich gesetztes PedestrianMaterial ODER der einslotige
	// Rueckfall (Zylinder/altes Mesh) bekommt das kanonische Flach-Material.
	const bool bZoned = Mesh && Mesh->GetStaticMaterials().Num() > 1;
	UMaterialInterface* Override = PedestrianMaterial;
	if (!Override && !bZoned)
	{
		Override = LoadObject<UMaterialInterface>(nullptr, FallbackMaterialPath);
	}
	if (Override)
	{
		Instances->SetMaterial(0, Override);
		for (UInstancedStaticMeshComponent* Pool : PoseInstances)
		{
			if (Pool)
			{
				Pool->SetMaterial(0, Override);
			}
		}
	}
	else if (!bZoned)
	{
		UE_LOG(LogWbCore, Warning,
			TEXT("Fussgaenger-Spawner: Material %s nicht ladbar - die Figuren rendern mit dem Default-Material."),
			FallbackMaterialPath);
	}

	bMeshReady = true;
}

void UPedestrianSpawnerComponent::ComputePedestrianColors(
	int32 Seed, FLinearColor& OutShirt, FLinearColor& OutTrouser, float& OutSkinT)
{
	// Feste Kleiderpaletten - gaengige Alltagsfarben, keine Neonwerte.
	static const FLinearColor Shirts[] = {
		FLinearColor(0.24f, 0.34f, 0.62f),  // Blau
		FLinearColor(0.70f, 0.20f, 0.18f),  // Rot
		FLinearColor(0.24f, 0.45f, 0.28f),  // Gruen
		FLinearColor(0.85f, 0.85f, 0.86f),  // Weiss
		FLinearColor(0.30f, 0.30f, 0.32f),  // Grau
		FLinearColor(0.80f, 0.62f, 0.20f),  // Senf
		FLinearColor(0.20f, 0.48f, 0.52f),  // Petrol
		FLinearColor(0.50f, 0.24f, 0.42f),  // Beere
	};
	static const FLinearColor Trousers[] = {
		FLinearColor(0.16f, 0.20f, 0.30f),  // Jeansblau
		FLinearColor(0.10f, 0.10f, 0.11f),  // Schwarz
		FLinearColor(0.32f, 0.32f, 0.34f),  // Grau
		FLinearColor(0.55f, 0.47f, 0.36f),  // Beige
		FLinearColor(0.30f, 0.22f, 0.16f),  // Braun
	};
	constexpr int32 NumShirts = UE_ARRAY_COUNT(Shirts);
	constexpr int32 NumTrousers = UE_ARRAY_COUNT(Trousers);

	// Drei entkoppelte Hashes aus dem Seed, damit Hemd/Hose/Haut unabhaengig
	// streuen (sonst korrelieren gleiche Reste).
	const uint32 S = static_cast<uint32>(Seed);
	const uint32 H1 = (S * 2654435761u) ^ 0x9E3779B9u;
	const uint32 H2 = (S * 2246822519u) ^ 0x85EBCA6Bu;
	const uint32 H3 = (S * 3266489917u) ^ 0xC2B2AE35u;

	OutShirt = Shirts[H1 % NumShirts];
	OutTrouser = Trousers[H2 % NumTrousers];
	OutSkinT = static_cast<float>(H3 % 1000u) / 999.0f;
}

void UPedestrianSpawnerComponent::UpdateInstances(const TArray<FPlacedPedestrian>& Placed)
{
	if (!Instances)
	{
		return;
	}

	EnsureMeshAndMaterial();
	if (!bMeshReady)
	{
		return;
	}

	// Massstab aus den ECHTEN Massen des Meshes, nicht aus einer Annahme.
	//
	// Hier stand fest die Kantenlaenge des Engine-Grundkoerpers (100 cm). Das
	// stimmt fuer den Rueckfall-Zylinder und ist fuer jede andere Figur falsch:
	// Die 175 cm hohe Menschfigur waere damit auf 306 cm gestreckt worden.
	//
	// Skaliert wird GLEICHMAESSIG ueber die Hoehe. Die Breite getrennt auf
	// BodySizeCm.X zu zwingen wuerde die Figur stauchen - 45 cm ist die
	// Schulterbreite, das Modell misst mit den Armen 66 cm.
	FVector Scale = FVector::OneVector;
	{
		const FBoxSphereBounds MeshBounds = Instances->GetStaticMesh()->GetBounds();
		const double MeshHeightCm = FMath::Max(MeshBounds.BoxExtent.Z * 2.0, 1.0);
		const double Uniform = BodySizeCm.Z / MeshHeightCm;

		// Der Zylinder ist rund und muesste in der Breite eigens gestaucht
		// werden - eine 175 cm breite Saeule waere sonst das Ergebnis.
		const double MeshWidthCm = FMath::Max(MeshBounds.BoxExtent.X * 2.0, 1.0);
		const bool bLooksLikeEngineShape =
			FMath::IsNearlyEqual(MeshWidthCm, MeshHeightCm, 1.0)
			&& FMath::IsNearlyEqual(MeshHeightCm, EngineShapeSizeCm, 1.0);

		Scale = bLooksLikeEngineShape
			? FVector(BodySizeCm.X / EngineShapeSizeCm,
				BodySizeCm.Y / EngineShapeSizeCm,
				BodySizeCm.Z / EngineShapeSizeCm)
			: FVector(Uniform, Uniform, Uniform);
	}

	// Das Mesh auf seinen eigenen Ursprung heben.
	//
	// Die Simulation liefert den Punkt, auf dem die Figur STEHT. Wo der
	// Ursprung eines Meshes relativ zu seiner Unterkante sitzt, ist von Mesh
	// zu Mesh verschieden:
	//
	//   Engine-Zylinder   Ursprung in der MITTE  -> Unterkante bei -50 cm
	//   SM_WbPerson       Ursprung an den FUESSEN -> Unterkante bei 0
	//
	// Gerechnet wird deshalb aus den tatsaechlichen Grenzen des Meshes, statt
	// einen festen Wert anzunehmen. Genau diese Annahme (fest 88 cm, die halbe
	// Koerperhoehe) liess die Menschfigur ueber dem Boden schweben.
	double FootLiftCm = 0.0;
	{
		const FBoxSphereBounds MeshBounds = Instances->GetStaticMesh()->GetBounds();
		const double LocalBottom = MeshBounds.Origin.Z - MeshBounds.BoxExtent.Z;
		FootLiftCm = -LocalBottom * Scale.Z;
	}

	// Boden-Snap gegen "im Boden steckende" Figuren.
	//
	// SampleSidewalk liefert die Fusshoehe aus der GEBACKENEN Gehweghoehe
	// (Mittellinie + Bordstein). Zur Laufzeit weicht das gestreamte Gelaende
	// davon ab - dann stecken die Figuren im Boden oder schweben. Darum die
	// Figur per kurzem Down-Trace auf die tatsaechliche Oberflaeche direkt unter
	// ihr setzen (Gehweg-/Strassenmesh, sonst Landscape). Traces gegen einfache
	// Kollision; die ISM-Instanzen selbst haben keine, der Owner wird ignoriert.
	//
	// DISTANZ-CULL: ein Trace je Figur je Frame ist bei vielen NPCs teuer. Nur
	// Figuren nahe der Kamera snappen (dort faellt der Versatz auf); ferne
	// behalten ihre gebackene Hoehe (auf Distanz nicht sichtbar). Die Blickpunkt-
	// Position wird EINMAL geholt, nicht je Figur.
	UWorld* const PedWorld = GetWorld();
	AActor* const PedOwner = GetOwner();
	constexpr double SnapCullDistCm = 7000.0;   // ~70 m
	const double SnapCullDistSq = SnapCullDistCm * SnapCullDistCm;
	FVector ViewLoc = FVector::ZeroVector;
	bool bHaveView = false;
	if (PedWorld)
	{
		if (APlayerController* PC = PedWorld->GetFirstPlayerController())
		{
			FVector CamLoc; FRotator CamRot;
			PC->GetPlayerViewPoint(CamLoc, CamRot);
			ViewLoc = CamLoc;
			bHaveView = true;
		}
	}
	auto GroundSnap = [PedWorld, PedOwner, ViewLoc, bHaveView, SnapCullDistSq](const FVector& Foot) -> FVector
	{
		if (!PedWorld)
		{
			return Foot;
		}
		// Ferne Figuren nicht tracen (nur die nahe Kamera; spart die Raycasts).
		if (bHaveView && FVector::DistSquared(Foot, ViewLoc) > SnapCullDistSq)
		{
			return Foot;
		}
		FHitResult Hit;
		const FVector Start(Foot.X, Foot.Y, Foot.Z + 500.0);
		const FVector End(Foot.X, Foot.Y, Foot.Z - 1500.0);
		FCollisionQueryParams Params(FName(TEXT("WbPedGround")), /*bTraceComplex=*/false, PedOwner);
		if (PedWorld->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params)
			&& !Hit.bStartPenetrating)
		{
			return FVector(Foot.X, Foot.Y, Hit.Location.Z);
		}
		return Foot;
	};

	const int32 Needed = Placed.Num();
	const bool bAnimated = (PoseInstances.Num() == WalkPoseCount);

	if (bAnimated)
	{
		// Jede Figur in den Pool ihrer Schrittphase.
		//
		// StridePhase laeuft von 0 bis 1 ueber einen Schritt. Vier Pools
		// bedeuten: Wer bei 0,0 bis 0,25 ist, steht im Pool 0, und so weiter.
		// Beim Weitergehen wandert die Figur von Pool zu Pool - das ergibt den
		// Gang.
		//
		// Gezaehlt wird zuerst, damit jeder Pool genau einmal auf seine Groesse
		// gebracht wird. Instanzen einzeln anzulegen und zu entfernen waere bei
		// mehreren Dutzend Figuren je Bild spuerbar.
		TArray<TArray<int32>> ByPose;
		ByPose.SetNum(WalkPoseCount);

		for (int32 Index = 0; Index < Needed; ++Index)
		{
			const float Phase = FMath::Frac(FMath::Max(Placed[Index].StridePhase, 0.0f));
			const int32 Pose = FMath::Clamp(
				FMath::FloorToInt(Phase * WalkPoseCount), 0, WalkPoseCount - 1);
			ByPose[Pose].Add(Index);
		}

		// Der Grundpool bleibt leer, solange animiert wird.
		if (Instances->GetInstanceCount() > 0)
		{
			Instances->ClearInstances();
		}

		for (int32 Pose = 0; Pose < WalkPoseCount; ++Pose)
		{
			UInstancedStaticMeshComponent* Pool = PoseInstances[Pose];
			if (!Pool)
			{
				continue;
			}

			const int32 Want = ByPose[Pose].Num();
			const int32 Have = Pool->GetInstanceCount();

			for (int32 i = Have; i < Want; ++i)
			{
				Pool->AddInstance(FTransform::Identity, /*bWorldSpace=*/true);
			}
			for (int32 i = Have - 1; i >= Want; --i)
			{
				Pool->RemoveInstance(i);
			}

			for (int32 i = 0; i < Want; ++i)
			{
				const FPlacedPedestrian& Walker = Placed[ByPose[Pose][i]];
				Pool->UpdateInstanceTransform(
					i,
					FTransform(Walker.Rotation,
						GroundSnap(Walker.Location) + FVector(0.0, 0.0, FootLiftCm),
						Scale * Walker.ScaleFactor),
					/*bWorldSpace=*/true,
					/*bMarkRenderStateDirty=*/false);

				// Kleidungsfarben pro Instanz (Hemd/Hose/Hautton) aus dem Seed.
				if (Pool->NumCustomDataFloats == PedCustomDataFloats)
				{
					FLinearColor Shirt, Trouser; float SkinT;
					ComputePedestrianColors(Walker.Seed, Shirt, Trouser, SkinT);
					const TArray<float> CD = {
						Shirt.R, Shirt.G, Shirt.B, Trouser.R, Trouser.G, Trouser.B, SkinT };
					Pool->SetCustomData(i, CD, /*bMarkRenderStateDirty=*/false);
				}
			}

			Pool->MarkRenderStateDirty();
		}
	}
	else
	{
		// Instanzenzahl angleichen, statt alles zu loeschen und neu anzulegen:
		// ClearInstances je Bild wuerde den Pool jedes Mal neu aufbauen.
		const int32 Existing = Instances->GetInstanceCount();

		for (int32 Index = Existing; Index < Needed; ++Index)
		{
			Instances->AddInstance(FTransform::Identity, /*bWorldSpace=*/true);
		}
		for (int32 Index = Existing - 1; Index >= Needed; --Index)
		{
			Instances->RemoveInstance(Index);
		}

		for (int32 Index = 0; Index < Needed; ++Index)
		{
			const FPlacedPedestrian& Walker = Placed[Index];
			Instances->UpdateInstanceTransform(
				Index,
				FTransform(Walker.Rotation, GroundSnap(Walker.Location) + FVector(0.0, 0.0, FootLiftCm), Scale * Walker.ScaleFactor),
				/*bWorldSpace=*/true,
				/*bMarkRenderStateDirty=*/false);

			if (Instances->NumCustomDataFloats == PedCustomDataFloats)
			{
				FLinearColor Shirt, Trouser; float SkinT;
				ComputePedestrianColors(Walker.Seed, Shirt, Trouser, SkinT);
				const TArray<float> CD = {
					Shirt.R, Shirt.G, Shirt.B, Trouser.R, Trouser.G, Trouser.B, SkinT };
				Instances->SetCustomData(Index, CD, /*bMarkRenderStateDirty=*/false);
			}
		}
	}

	// Einmal am Ende markieren statt je Instanz - sonst wird der Renderzustand
	// mehrere hundert Mal je Bild neu aufgebaut.
	Instances->MarkRenderStateDirty();
}

void UPedestrianSpawnerComponent::ClearInstances()
{
	if (Instances)
	{
		Instances->ClearInstances();
	}
}

int32 UPedestrianSpawnerComponent::GetVisibleCount() const
{
	// Alle Pools summieren, nicht nur den Grundpool. Sobald die vier Gangphasen
	// geladen sind, wird animiert: der Grundpool wird GELEERT und jede Figur lebt
	// in ihrem Pose-Pool (siehe UpdateInstances). Nur Instances->GetInstanceCount()
	// zu lesen meldete dann dauerhaft 0, obwohl die Figuren gezeichnet werden - das
	// loeste eine Fehlwarnung "KEINER gezeichnet" in Health-Report und Diagnose aus.
	int32 Count = Instances ? Instances->GetInstanceCount() : 0;
	for (const UInstancedStaticMeshComponent* Pool : PoseInstances)
	{
		if (Pool)
		{
			Count += Pool->GetInstanceCount();
		}
	}
	return Count;
}
