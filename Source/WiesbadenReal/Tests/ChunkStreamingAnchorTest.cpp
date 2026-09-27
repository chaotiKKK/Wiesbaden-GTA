// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GIS/WiesbadenRegionAssets.h"
#include "ProceduralMeshComponent.h"
#include "World/WiesbadenCityChunk.h"
#include "World/RegionAssetSpawnerComponent.h"

/**
 * Regressionstest der Streaming-Verankerung einer Stadt-Zelle.
 *
 * WOZU: Der Anker einer Zelle (Mittelpunkt ihres Inhalts) decides, welche
 * World-Partition-Zelle die leeren Komponenten belegen. Er wird als
 * UPROPERTY im Actor-Paket gespeichert und bei jedem Laden (PostRegister-
 * AllComponents) neu angewendet - er darf NICHT davon abhaengen, dass die
 * Komponenten-Transforms das Speichern ueberlebt. Genau daran ist es
 * gescheitert: SetWorldLocation auf einem Actor am (0,0,0) schreibt den
 * Weltanker als relatives Delta in die Karte, beim naechsten Laden
 * addiert der Actor das Delta erneut, und der gemessene Zustand nach
 * jedem Re-Bake lautete wieder "alle 2010 Zellen ueber dem Ursprung"
 * (Messung: Tools/verify_anchor_state.py auf Alkis24 und Alkis25).
 *
 * Der Test nagelt die drei Punkte fest, an denen es wieder kippen kann:
 *   1) Der Anker ist eine serialisierte Property (kein Transient, kein
 *      reines Komponenten-Transform).
 *   2) Ein Chunk OHNE Inhalt speichert keinen Anker (sonst wuerde der
 *      Kartenzentrumspunkt (0,0,0) als "Anker" in die Karte wandern).
 *   3) Aus dem Zustand "Komponenten frisch aufgebaut" (am Actor-Ort, wie
 *      ein geladener Actor sie ohne gespeicherte Transforms behaelt) holt
 *      ApplyStreamingAnchor() die leeren Komponenten auf den Zellinhalt
 *      zurueck - und zwar ohne zu driften (zweimal anwenden aendert
 *      nichts).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChunkStreamingAnchorTest,
	"WiesbadenReal.World.ChunkStreamingAnchor",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	/** Alle Komponenten eines Chunks auf den Actor-Ort zuruecksetzen - der
	 *  Zustand, den ein frisch geladener Actor ohne gespeicherte
	 *  Komponenten-Transforms hat. */
	void ResetToFreshState(AWiesbadenCityChunk& Chunk)
	{
		TArray<USceneComponent*> Components;
		Chunk.GetComponents<USceneComponent>(Components);
		for (USceneComponent* Component : Components)
		{
			if (Component)
			{
				Component->SetWorldLocation(Chunk.GetActorLocation());
			}
		}
	}
}

bool FChunkStreamingAnchorTest::RunTest(const FString& Parameters)
{
	// -- 1) Der Anker ist serialisiert -------------------------------------
	const FProperty* AnchorProperty = AWiesbadenCityChunk::StaticClass()->
		FindPropertyByName(TEXT("StreamingAnchorCm"));
	if (TestNotNull(TEXT("Property StreamingAnchorCm existiert"), AnchorProperty))
	{
		TestFalse(TEXT("StreamingAnchorCm ist NICHT Transient - sonst ueberlebt "
			"der Anker das Speichern nicht"),
			AnchorProperty->HasAnyPropertyFlags(CPF_Transient));
	}

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld*/ false);
	if (!TestNotNull(TEXT("Test-Welt erstellt"), World))
	{
		return false;
	}

	AWiesbadenCityChunk* Chunk = World->SpawnActor<AWiesbadenCityChunk>();
	if (!TestNotNull(TEXT("Chunk gespawnt"), Chunk))
	{
		World->DestroyWorld(false);
		return false;
	}

	// Der Chunk-Actor steht per Bauplan auf (0,0,0) - alles Weitere haengt
	// daran, dass das auch im Test so ist.
	TestTrue(TEXT("Chunk-Actor steht am Ursprung (Bauplan)"),
		Chunk->GetActorLocation().IsNearlyZero());

	// -- 2) Ohne Inhalt kein Anker ------------------------------------------
	Chunk->AnchorStreamingBounds();
	TestFalse(TEXT("Leerer Chunk speichert keinen Anker (sonst wandert der "
		"Kartenzentrumspunkt in die Karte)"), Chunk->HasStreamingAnchor());

	// -- Inhalt geben: eine Region weitab vom Ursprung ---------------------
	TArray<FPlacedRegionAsset> Assets;
	FPlacedRegionAsset Tree;
	Tree.Category = ERegionAssetCategory::Tree;
	Tree.Location = FVector(250000.0, -175000.0, 120.0);
	Tree.Scale = 1.0f;
	Assets.Add(Tree);

	// SetRegionAssets ruft am Ende selbst AnchorStreamingBounds auf.
	Chunk->SetRegionAssets(Assets);

	if (!TestTrue(TEXT("Chunk mit Inhalt speichert einen Anker"),
			Chunk->HasStreamingAnchor()))
	{
		World->DestroyWorld(false);
		return false;
	}

	const FVector Anchor = Chunk->GetStreamingAnchor();
	TestTrue(TEXT("Anker liegt auf dem Inhalt der Zelle (RegionAsset)"),
		FVector::Dist(Anchor, Tree.Location) < 1.0);
	TestFalse(TEXT("Anker ist kein NaN"), Anchor.ContainsNaN());

	// -- 3) Frisch aufgebaut -> wieder ankeren -----------------------------
	ResetToFreshState(*Chunk);

	// Vor dem Anwenden steht alles am Actor-Ort: das ist der Zustand, den
	// die alte Loesung allein liess (genau der untersuchte Fehler).
	UProceduralMeshComponent* RoadMesh =
		Chunk->FindComponentByClass<UProceduralMeshComponent>();
	if (TestNotNull(TEXT("RoadMesh vorhanden"), RoadMesh))
	{
		TestTrue(TEXT("Nach dem Zuruecksetzen liegt RoadMesh am Actor-Ort"),
			FVector::Dist(RoadMesh->GetRelativeLocation(), FVector::ZeroVector) < 1.0);
	}

	Chunk->ApplyStreamingAnchor();

	if (RoadMesh)
	{
		TestTrue(TEXT("Nach ApplyStreamingAnchor liegt die leere RoadMesh am "
			"gespeicherten Anker (relativ, Actor steht auf 0)"),
			FVector::Dist(RoadMesh->GetRelativeLocation(), Anchor) < 1.0);
	}

	// Jede Komponente ohne Inhalt gehoert an den Anker, jede MIT Inhalt an
	// den Actor (die Geometrie traegt Weltkoordinaten).
	//
	// Geprueft wird die RELATIVE Lage - das ist der Wert, der serialisiert
	// wird und an dem der Fehler sichtbar war. Die Weltlage haengt im Test
	// an der Transform-Propagation, die ohne Tick ausbleibt; relativ ist
	// zugleich die Aussage, die zaehlt.
	TArray<USceneComponent*> Components;
	Chunk->GetComponents<USceneComponent>(Components);
	int32 Checked = 0;
	for (USceneComponent* Component : Components)
	{
		if (!Component || Component == Chunk->GetRootComponent())
		{
			continue;
		}
		// Der Spawner ist der Anker-Halter, kein Instanz-Container: er
		// bleibt am Actor wie der Root.
		if (Component == Chunk->FindComponentByClass<URegionAssetSpawnerComponent>())
		{
			continue;
		}

		// Reihenfolge ist bedeutsam: UHierarchicalInstancedStaticMeshComponent
		// ERBT von UStaticMeshComponent. Mit dem StaticMesh-Zweig zuerst
		// galte jedes HISM mit gesetztem Mesh als "hat Inhalt" - auch ohne
		// eine einzige Instanz, und genau daran scheiterte der Test.
		bool bHasContent = false;
		if (const UHierarchicalInstancedStaticMeshComponent* HISM =
			Cast<UHierarchicalInstancedStaticMeshComponent>(Component))
		{
			bHasContent = HISM->GetInstanceCount() > 0;
		}
		else if (const UProceduralMeshComponent* Mesh = Cast<UProceduralMeshComponent>(Component))
		{
			bHasContent = Mesh->GetNumSections() > 0;
		}
		else if (const UStaticMeshComponent* SM = Cast<UStaticMeshComponent>(Component))
		{
			bHasContent = SM->GetStaticMesh() != nullptr;
		}

		// Actor steht auf (0,0,0): Welt == relativ. Erwartet wird die
		// Komponentenlage, die das Speichern festschreibt.
		const FVector Expected = bHasContent ? FVector::ZeroVector : Anchor;
		const FVector Actual = Component->GetRelativeLocation();
		TestTrue(*FString::Printf(TEXT("%s liegt %s: relativ (%.0f, %.0f, %.0f), "
			"erwartet (%.0f, %.0f, %.0f)"),
			*Component->GetName(),
			bHasContent ? TEXT("am Actor") : TEXT("am Anker"),
			Actual.X, Actual.Y, Actual.Z,
			Expected.X, Expected.Y, Expected.Z),
			FVector::Dist(Actual, Expected) < 1.0);
		++Checked;
	}
	TestTrue(TEXT("Mindestens fuenf Komponenten geprueft (leere Meshes, "
		"Statics, Instanzen)"), Checked >= 5);

	// -- Idempotenz: zweimal anwenden aendert nichts ------------------------
	TArray<FVector> Before;
	for (USceneComponent* Component : Components)
	{
		if (Component)
		{
			Before.Add(Component->GetRelativeLocation());
		}
	}
	Chunk->ApplyStreamingAnchor();
	int32 Drift = 0;
	for (int32 i = 0; i < Components.Num(); ++i)
	{
		if (Components[i] && FVector::Dist(
			Components[i]->GetRelativeLocation(), Before[i]) > 0.01)
		{
			++Drift;
		}
	}
	TestEqual(TEXT("Zweites Anwenden bewegt nichts (kein Aufsummieren des "
		"Ankers)"), Drift, 0);

	World->DestroyWorld(false);
	return true;
}
