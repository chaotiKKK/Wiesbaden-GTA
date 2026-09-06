// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "World/RegionAssetSpawnerComponent.h"

/**
 * WP-Streaming-Fix: Leere Instanz-Komponenten duerfen ihre Punkt-Bounds nicht
 * am Karten-Ursprung lassen (3x4-km-Bounds-Falle, Diagnose "jenseits 2 km").
 *
 * Reproduziert den Produktionsmechanismus der gebackenen Karte: Eine geladene,
 * LEERE Zelle traegt serialisierte Varianten-Komponenten (Trees_01..06,
 * Bushes_01..06), die KEIN Laufzeit-Array mehr kennt - VariedInstances wird
 * nur beim Bau gefuellt, BeginPlay ruft bei 0 Regionsobjekten SpawnRegionAssets
 * gar nicht erst. AnchorEmptyInstanceComponents muss sie deshalb ueber den
 * Besitzer (Klassen-Sweep) finden, nicht ueber Arrays.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegionAssetAnchorTest,
	"WiesbadenReal.World.RegionAssetAnchor",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRegionAssetAnchorTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld*/ false);
	if (!TestNotNull(TEXT("Test-Welt erstellt"), World))
	{
		return false;
	}

	AActor* Owner = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Besitzer-Actor gespawnt"), Owner))
	{
		World->DestroyWorld(false);
		return false;
	}

	URegionAssetSpawnerComponent* Spawner =
		NewObject<URegionAssetSpawnerComponent>(Owner);
	Spawner->RegisterComponent();

	// Serialisierte Varianten-Komponente NACH dem Spawn nachbilden: existiert
	// in der gebackenen Karte, ist aber in keinem Laufzeit-Array (VariedInstances
	// ist leer, denn SpawnVaried lief nie in dieser Sitzung).
	UHierarchicalInstancedStaticMeshComponent* SerializedTree =
		NewObject<UHierarchicalInstancedStaticMeshComponent>(Owner, FName("Trees_01"));
	SerializedTree->SetupAttachment(Spawner);
	SerializedTree->RegisterComponent();
	Owner->AddInstanceComponent(SerializedTree);

	UHierarchicalInstancedStaticMeshComponent* SerializedBush =
		NewObject<UHierarchicalInstancedStaticMeshComponent>(Owner, FName("Bushes_01"));
	SerializedBush->SetupAttachment(Spawner);
	SerializedBush->RegisterComponent();
	Owner->AddInstanceComponent(SerializedBush);

	const FVector Anchor(12345.0, 67890.0, 0.0);
	Spawner->AnchorEmptyInstanceComponents(Anchor);

	TestTrue(TEXT("Serialisierte Variante (Trees_01) am Zell-Inhalt verankert"),
		SerializedTree->GetComponentLocation().Equals(Anchor, 1.0));
	TestTrue(TEXT("Serialisierte Variante (Bushes_01) am Zell-Inhalt verankert"),
		SerializedBush->GetComponentLocation().Equals(Anchor, 1.0));

	World->DestroyWorld(false);
	return true;
}
