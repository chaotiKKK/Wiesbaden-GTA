// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/RegionAssetSpawnerComponent.h"

#include "World/WiesbadenStreamingCost.h"

#include "WiesbadenReal.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInterface.h"

URegionAssetSpawnerComponent::URegionAssetSpawnerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	TreeInstances = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("Trees"));
	TreeInstances->SetupAttachment(this);
	TreeInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	WaterfrontInstances = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("Waterfront"));
	WaterfrontInstances->SetupAttachment(this);
	WaterfrontInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	IndustrialInstances = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("Industrial"));
	IndustrialInstances->SetupAttachment(this);
	IndustrialInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void URegionAssetSpawnerComponent::EnsureDefaultAssets()
{
	// Engine-Grundformen als Platzhalter. Ein Kegel liest sich aus der
	// Entfernung als Baumkrone; das ist sichtbar ein Platzhalter, aber einer
	// leeren Wiese deutlich vorzuziehen.
	// Die zwoelf Pflanzen aus dem Blendswap-Paket (CC BY 3.0, siehe
	// CREDITS.md). Fehlt der Ordner, bleibt der Engine-Kegel als Rueckfall -
	// dann sieht man Platzhalter statt gar nichts.
	if (TreeMeshes.Num() == 0)
	{
		// SM_WbTree_01..06: Blendswap-Paket. SM_WbTree_07: importierte Birke
		// (echtes Modell, andere Art -> Abwechslung Richtung Referenz). Fehlt eine
		// Nummer, liefert LoadObject nullptr und sie wird uebersprungen.
		for (int32 Index = 1; Index <= 7; ++Index)
		{
			const FString Path = FString::Printf(
				TEXT("/Game/Vegetation/Meshes/SM_WbTree_%02d.SM_WbTree_%02d"), Index, Index);
			if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path))
			{
				TreeMeshes.Add(Mesh);
			}
		}
	}

	if (BushMeshes.Num() == 0)
	{
		for (int32 Index = 1; Index <= 6; ++Index)
		{
			const FString Path = FString::Printf(
				TEXT("/Game/Vegetation/Meshes/SM_WbBush_%02d.SM_WbBush_%02d"), Index, Index);
			if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path))
			{
				BushMeshes.Add(Mesh);
			}
		}
	}

	if (!TreeMesh)
	{
		TreeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	}
	if (!WaterfrontMesh)
	{
		WaterfrontMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	}
	if (!IndustrialMesh)
	{
		IndustrialMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	}
	if (!TreeMaterial)
	{
		TreeMaterial = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Game/Materials/City/M_WbTree.M_WbTree"));
	}
}

void URegionAssetSpawnerComponent::ClearRegionAssets()
{
	for (UHierarchicalInstancedStaticMeshComponent* Component : VariedInstances)
	{
		if (Component)
		{
			Component->ClearInstances();
		}
	}

	if (TreeInstances) { TreeInstances->ClearInstances(); }
	if (WaterfrontInstances) { WaterfrontInstances->ClearInstances(); }
	if (IndustrialInstances) { IndustrialInstances->ClearInstances(); }
	LastSpawnedTreeCount = 0;
	LastSpawnedWaterfrontCount = 0;
	LastSpawnedIndustrialCount = 0;
}

void URegionAssetSpawnerComponent::SpawnCategory(const TArray<FPlacedRegionAsset>& Assets,
	UStaticMesh* Mesh, UMaterialInterface* Material, UHierarchicalInstancedStaticMeshComponent* ISM,
	const FVector& BaseSizeCm)
{
	if (!Mesh || !ISM)
	{
		return;
	}

	ISM->SetStaticMesh(Mesh);
	if (Material)
	{
		ISM->SetMaterial(0, Material);
	}

	// Batch-Add: PreAllocate + AddInstances (kein einzelnes AddInstance - das
	// wuerde bei tausenden Instanzen mehrfach re-alloc und Render-State-Update
	// bedeuten; siehe ue-procedural-generation: PreAllocate + Batch).
	TArray<FTransform> Transforms;
	Transforms.Reserve(Assets.Num());
	for (const FPlacedRegionAsset& Asset : Assets)
	{
		// Asset.Scale ist RELATIVE Streuung (0,85..1,15), keine absolute
		// Groesse. Die Grundgroesse kommt aus BaseSizeCm - bei den
		// Engine-Platzhaltern (Kegel/Wuerfel, je 100 cm) waeren die Baeume
		// sonst EINEN Meter hoch und aus der Ferne unsichtbar.
		FTransform Transform(
			FRotator(0.0f, Asset.YawDegrees, 0.0f),
			Asset.Location + FVector(0.0f, 0.0f, ZOffsetCm),
			BaseSizeCm / 100.0 * Asset.Scale);
		Transforms.Add(Transform);
	}
	ISM->PreAllocateInstancesMemory(Assets.Num());
	ISM->AddInstances(Transforms, /*bShouldReturnIndices=*/false, /*bWorldSpace=*/true);
	ISM->MarkRenderStateDirty();
}

UHierarchicalInstancedStaticMeshComponent* URegionAssetSpawnerComponent::MakeInstanceComponent(
	const FName& Name, UStaticMesh* Mesh)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Mesh)
	{
		return nullptr;
	}

	// Bereits vorhandene, gleichnamige Komponente WIEDERVERWENDEN statt sie mit
	// NewObject zu ueberschreiben.
	//
	// Grund: Die Objekte werden ueber NewObject (ohne RF_Transient) angelegt und
	// deshalb in die gebackene Karte serialisiert. Beim Oeffnen laedt jede Zelle
	// ihre Komponenten mit, und BeginPlay baut sie hier erneut auf. NewObject mit
	// schon belegtem Namen zwingt den Spiel-Thread, auf die Render-Aufraeumung
	// des alten Objekts zu warten ("Gamethread hitch waiting for resource
	// cleanup") - bei 1.393 Zellen x 12 Komponenten rund 15.000 Mal, fast alles
	// beim Laden. Die bestehende Komponente wiederzuverwenden vermeidet das ganz
	// und funktioniert mit der bereits gebackenen Karte ohne Neubau.
	UHierarchicalInstancedStaticMeshComponent* Component =
		Cast<UHierarchicalInstancedStaticMeshComponent>(StaticFindObjectFast(
			UHierarchicalInstancedStaticMeshComponent::StaticClass(), Owner, Name));

	if (!Component)
	{
		Component = NewObject<UHierarchicalInstancedStaticMeshComponent>(Owner, Name);
		if (!Component)
		{
			return nullptr;
		}
		Component->SetupAttachment(this);
		Component->RegisterComponent();
		Owner->AddInstanceComponent(Component);
	}

	Component->ClearInstances();
	Component->SetStaticMesh(Mesh);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	return Component;
}

void URegionAssetSpawnerComponent::SpawnVaried(const TArray<FPlacedRegionAsset>& Assets)
{
	// Erst Baeume, dann Buesche - die Reihenfolge legt die Nummerierung der
	// Komponenten fest und muss stabil sein, damit -WbHideTrees und
	// -WbThinTrees dieselben Komponenten treffen wie beim letzten Lauf.
	TArray<UStaticMesh*> Meshes;
	Meshes.Append(TreeMeshes);
	const int32 TreeCount = TreeMeshes.Num();
	Meshes.Append(BushMeshes);

	if (Meshes.Num() == 0)
	{
		return;
	}

	// Je Modell eine Komponente.
	VariedInstances.Reset();
	VariedInstances.Reserve(Meshes.Num());
	for (int32 Index = 0; Index < Meshes.Num(); ++Index)
	{
		const bool bIsTree = Index < TreeCount;
		const FName Name(*FString::Printf(TEXT("%s_%02d"),
			bIsTree ? TEXT("Trees") : TEXT("Bushes"),
			bIsTree ? Index + 1 : Index - TreeCount + 1));

		UHierarchicalInstancedStaticMeshComponent* Component =
			MakeInstanceComponent(Name, Meshes[Index]);

		// Sichtweite je Instanz.
		//
		// Die Detailstufen allein reichen nicht: Auch die groebste hat noch
		// 900 bis 10.600 Dreiecke, und ohne Begrenzung zeichnet die Karte
		// jeden Waldhang bis zum Horizont. Der Anfangswert liegt bewusst
		// dicht unter dem Endwert - HISM blendet dazwischen aus, und ein zu
		// grosser Abstand haelt die Baeume unnoetig lange in der Szene.
		const float CullEnd = bIsTree ? TreeCullDistanceCm : BushCullDistanceCm;
		if (Component && CullEnd > 0.0f)
		{
			Component->SetCullDistances(
				FMath::Max(CullEnd * 0.8f, 0.0f), CullEnd);
		}

		VariedInstances.Add(Component);
	}

	TArray<TArray<FTransform>> PerMesh;
	PerMesh.SetNum(Meshes.Num());
	for (TArray<FTransform>& List : PerMesh)
	{
		List.Reserve(Assets.Num() / FMath::Max(Meshes.Num(), 1) + 8);
	}

	const int32 BushCount = BushMeshes.Num();
	const int32 BushThreshold = FMath::Clamp(
		FMath::RoundToInt(BushShare * 1000.0f), 0, 1000);

	for (const FPlacedRegionAsset& Asset : Assets)
	{
		// Art aus dem STANDORT ableiten, nicht aus der Laufnummer.
		//
		// Die Objekte liegen inzwischen je Streaming-Zelle vor. Eine Auswahl
		// nach Index haenge damit davon ab, in welcher Zelle ein Baum landet
		// und an welcher Stelle ihrer Liste - nach jedem Neubau der Stadt
		// waere derselbe Baum eine andere Sorte. Der Standort aendert sich
		// nicht.
		const uint32 Hash = GetTypeHash(FIntVector(
			FMath::RoundToInt(Asset.Location.X),
			FMath::RoundToInt(Asset.Location.Y),
			0));

		const bool bBush = BushCount > 0 && static_cast<int32>(Hash % 1000u) < BushThreshold;

		int32 MeshIndex = 0;
		if (bBush)
		{
			MeshIndex = TreeCount + static_cast<int32>((Hash / 1000u) % static_cast<uint32>(BushCount));
		}
		else if (TreeCount > 0)
		{
			MeshIndex = static_cast<int32>((Hash / 1000u) % static_cast<uint32>(TreeCount));
		}
		else
		{
			continue;
		}

		// Modelle in echten Massen brauchen nur noch die Streuung; TreeScaleBoost
		// hebt die Baeume Richtung der ueppigen Referenz-Strassenbaeume.
		const FVector Scale = (bMeshesAreRealScale
			? FVector(Asset.Scale)
			: TreeBaseSizeCm / 100.0 * Asset.Scale) * TreeScaleBoost;

		PerMesh[MeshIndex].Emplace(
			FRotator(0.0f, Asset.YawDegrees, 0.0f),
			Asset.Location + FVector(0.0f, 0.0f, ZOffsetCm),
			Scale);
	}

	for (int32 Index = 0; Index < VariedInstances.Num(); ++Index)
	{
		UHierarchicalInstancedStaticMeshComponent* Component = VariedInstances[Index];
		if (!Component || PerMesh[Index].Num() == 0)
		{
			continue;
		}
		Component->PreAllocateInstancesMemory(PerMesh[Index].Num());
		Component->AddInstances(PerMesh[Index], /*bShouldReturnIndices=*/false, /*bWorldSpace=*/true);
		Component->MarkRenderStateDirty();
	}
}

void URegionAssetSpawnerComponent::AnchorEmptyInstanceComponents(const FVector& AnchorLocation)
{
	// Klassen-Sweep ueber den Besitzer statt Arrays: Auf einer GELADENEN,
	// leeren Zelle existieren die serialisierten Varianten-Komponenten
	// (Trees_01..06, Bushes_01..06), ohne dass ein Laufzeit-Array sie kennt -
	// VariedInstances wird nur beim Bau gefuellt, und BeginPlay ruft bei 0
	// Regionsobjekten SpawnRegionAssets gar nicht erst (Genau das liess den
	// ersten Re-Bake 502 Zellen unheilen: Basis-HISMs wanderten, Varianten
	// blieben am Ursprung). Der Sweep deckt Basis- und Varianten-Komponenten
	// auf jedem Pfad ab.
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	TArray<UHierarchicalInstancedStaticMeshComponent*> Components;
	Owner->GetComponents<UHierarchicalInstancedStaticMeshComponent>(Components);

	for (UHierarchicalInstancedStaticMeshComponent* Component : Components)
	{
		if (!Component)
		{
			continue;
		}
		if (Component->GetInstanceCount() > 0)
		{
			// Mit Instanzen gilt wieder die echte Geometrie; die
			// Rueck-Ankerung stellt den Ausgangszustand her.
			Component->SetRelativeLocation(FVector::ZeroVector);
		}
		else
		{
			// Ankerung am Zell-Inhalt statt am Actor-Ursprung: Der Punkt
			// waehlt die Streaming-Zelle, in der diese (leere) Komponente
			// landet - nicht den Ursprung der Karte.
			Component->SetWorldLocation(AnchorLocation);
		}
		Component->MarkRenderStateDirty();
	}
}

void URegionAssetSpawnerComponent::SpawnRegionAssets(const FRegionAssetLayout& Layout)
{
	// Zuordnung der Nachlade-Aussetzer: dieser Aufruf laeuft im Spiel-Strang,
	// wenn World Partition eine Zelle hereinstreamt. Ohne die Messung bleibt
	// "Aussetzer 90 ms" eine Beobachtung ohne Ursache.
	const FWbStreamingCostScope CostScope(FWbStreamingCost::SpawnMs);
	FWbStreamingCost::Instances += Layout.Assets.Num();

	ClearRegionAssets();

	// Nach Kategorie gruppieren (ein ISM/Draw-Call je Kategorie).
	TArray<FPlacedRegionAsset> Trees;
	TArray<FPlacedRegionAsset> Waterfront;
	TArray<FPlacedRegionAsset> Industrial;
	Trees.Reserve(Layout.Assets.Num());
	for (const FPlacedRegionAsset& Asset : Layout.Assets)
	{
		switch (Asset.Category)
		{
		case ERegionAssetCategory::Tree: Trees.Add(Asset); break;
		case ERegionAssetCategory::Waterfront: Waterfront.Add(Asset); break;
		case ERegionAssetCategory::Industrial: Industrial.Add(Asset); break;
		default: break;
		}
	}

	if (TreeMeshes.Num() > 0 || BushMeshes.Num() > 0)
	{
		SpawnVaried(Trees);
	}
	else
	{
		SpawnCategory(Trees, TreeMesh, TreeMaterial, TreeInstances, TreeBaseSizeCm);
	}
	SpawnCategory(Waterfront, WaterfrontMesh, WaterfrontMaterial, WaterfrontInstances, WaterfrontBaseSizeCm);
	SpawnCategory(Industrial, IndustrialMesh, IndustrialMaterial, IndustrialInstances, IndustrialBaseSizeCm);

	LastSpawnedTreeCount = Trees.Num();
	LastSpawnedWaterfrontCount = Waterfront.Num();
	LastSpawnedIndustrialCount = Industrial.Num();

	// Verbose, nicht Log: Seit die Objekte je Zelle liegen, gibt es 1.393
	// Aufrufer. Die Gesamtzahl meldet der Aufrufer.
	UE_LOG(LogWbCore, Verbose, TEXT("Regionen-Assets gespawnt: %s"), *Layout.GetStatisticsString());
}
