// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenCityChunk.h"

#include "ProceduralMeshComponent.h"
#include "World/RegionAssetSpawnerComponent.h"

AWiesbadenCityChunk::AWiesbadenCityChunk()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	RoadMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("RoadMesh"));
	RoadMesh->SetupAttachment(Root);

	BuildingMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BuildingMesh"));
	BuildingMesh->SetupAttachment(Root);

	// Kollision NEBENHER kochen, nicht im Spiel-Strang.
	//
	// UProceduralMeshComponent kocht seine Kollisionsdaten vorgabegemaess
	// SYNCHRON - der Spiel-Strang steht so lange still. Bei 1.394 Chunk-Actors,
	// die World Partition beim Fahren laufend nachlaedt, bedeutet das einen
	// Aussetzer je Chunk.
	//
	// Das Ladeprotokoll ist voll mit "LogChaos: Input trimesh contains N bad
	// triangles" - jede dieser Zeilen ist ein Kochvorgang. Und die Messung
	// zeigt genau das Bild dazu: Spiel-Strang 137 ms, Renderer nur 33 ms.
	// Die Last liegt nicht beim Zeichnen.
	RoadMesh->bUseAsyncCooking = true;
	BuildingMesh->bUseAsyncCooking = true;

	// Regionsobjekte JE ZELLE.
	//
	// Vorher hingen alle 1.532.254 Baeume in einer Komponente am
	// WorldBuilder - einem Actor, den World Partition nie streamt. Sie waren
	// damit permanent vollstaendig geladen. Gemessen an einer Stelle, an der
	// nur eine Handvoll winziger Kegel am Horizont im Bild stand:
	//
	//     1.532.254 Instanzen   82 ms Bildzeit
	//       153.226 Instanzen   64 ms
	//             0 Instanzen   54 ms
	//
	// Die Kosten haengen also an der VERWALTETEN, nicht an der sichtbaren
	// Menge - Sichtweitenbegrenzung haette daran nichts geaendert.
	RegionAssetSpawner = CreateDefaultSubobject<URegionAssetSpawnerComponent>(
		TEXT("RegionAssets"));
	RegionAssetSpawner->SetupAttachment(Root);
}

void AWiesbadenCityChunk::SetRegionAssets(const TArray<FPlacedRegionAsset>& InAssets)
{
	RegionAssets = InAssets;

	if (!RegionAssetSpawner)
	{
		return;
	}

	RegionAssetSpawner->EnsureDefaultAssets();

	FRegionAssetLayout Layout;
	Layout.Assets = RegionAssets;
	RegionAssetSpawner->SpawnRegionAssets(Layout);
}

void AWiesbadenCityChunk::BeginPlay()
{
	Super::BeginPlay();

	if (RegionAssetSpawner && RegionAssets.Num() > 0)
	{
		RegionAssetSpawner->EnsureDefaultAssets();

		FRegionAssetLayout Layout;
		Layout.Assets = RegionAssets;
		RegionAssetSpawner->SpawnRegionAssets(Layout);
	}
}

void AWiesbadenCityChunk::ApplyChunk(const FCityChunkMesh& Chunk, bool bRoadCollision, bool bBuildingCollision)
{
	if (!RoadMesh || !BuildingMesh)
	{
		return;
	}

	RoadMesh->ClearAllMeshSections();
	BuildingMesh->ClearAllMeshSections();

	// Regionsobjekte merken und sofort aufbauen. Das Merken ist der Teil, der
	// die Karte ueberlebt; der Aufbau ist nur fuer die Editor-Sitzung, in der
	// gebaut wird.
	SetRegionAssets(Chunk.RegionAssets);

	// Road-Sections: Indizes 0..N-1 in der Reihenfolge des Chunk-Arrays.
	RoadSectionChannels.Reset();
	int32 RoadSectionIndex = 0;
	for (const FRoadMeshSection& Section : Chunk.RoadSections)
	{
		if (Section.IsEmpty())
		{
			continue;
		}

		// Boeschungen bekommen KEINE Kollision.
		//
		// Sie sind senkrechte Erdwaende an der Fahrbahnkante - dort faehrt und
		// geht niemand. Mit Kollision waeren es allein in Wiesbaden rund
		// 3,5 Millionen Dreiecke in der Physikszene, mehr als die Fahrbahn
		// selbst (949.000), ohne dass sie irgendetwas tragen.
		// Boeschungen haben seit der Kanal-Trennung einen eigenen Kanal. Die
		// alte Erkennung ueber Kanal "Fahrbahn" PLUS Oberflaeche "Ground" hat
		// echte unbefestigte Strassen mit erfasst und ihnen die Kollision
		// genommen - ein Feldweg ohne Kollision laesst das Fahrzeug
		// durchfallen.
		const bool bIsEmbankment = Section.Channel == ERoadMeshChannel::Embankment;

		RoadMesh->CreateMeshSection(
			RoadSectionIndex,
			Section.Vertices,
			Section.Triangles,
			Section.Normals,
			Section.UVs,
			Section.VertexColors,
			Section.Tangents,
			bRoadCollision && !bIsEmbankment);

		RoadSectionChannels.Add(static_cast<uint8>(Section.Channel));
		++RoadSectionIndex;
	}

	RoadMesh->SetCollisionEnabled(
		bRoadCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);

	// Die Fahrbahn blockiert Fahrzeuge und Fussgaenger, aber NICHT die Kamera.
	//
	// Der Verfolgerarm haengt hinter und ueber dem Fahrzeug und tastet mit
	// einer Kugel nach Hindernissen. Seit die Fahrbahn eigene Kollision hat,
	// streift dieser Tastkoerper bei jeder Kuppe und in jeder Senke den
	// Asphalt - der Arm zieht die Kamera dann schlagartig ans Fahrzeug heran
	// und wieder weg. Im Spiel sah das aus, als wechsle die Ansicht staendig
	// zwischen innen und aussen.
	//
	// Eine Strasse kann die Sicht auf das Fahrzeug ohnehin nicht verdecken;
	// Gebaeude sollen es weiterhin koennen und behalten ihre Kamerakollision.
	RoadMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	// Building-Sections analog.
	int32 BuildingSectionIndex = 0;
	for (const FBuildingMeshSection& Section : Chunk.BuildingSections)
	{
		if (Section.IsEmpty())
		{
			continue;
		}

		BuildingMesh->CreateMeshSection(
			BuildingSectionIndex,
			Section.Vertices,
			Section.Triangles,
			Section.Normals,
			Section.UVs,
			Section.VertexColors,
			Section.Tangents,
			bBuildingCollision);

		++BuildingSectionIndex;
	}

	BuildingMesh->SetCollisionEnabled(
		bBuildingCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
}

void AWiesbadenCityChunk::SetRoadSectionMaterial(int32 SectionIndex, UMaterialInterface* Material)
{
	if (RoadMesh && Material)
	{
		RoadMesh->SetMaterial(SectionIndex, Material);
	}
}

void AWiesbadenCityChunk::SetBuildingSectionMaterial(int32 SectionIndex, UMaterialInterface* Material)
{
	if (BuildingMesh && Material)
	{
		BuildingMesh->SetMaterial(SectionIndex, Material);
	}
}
