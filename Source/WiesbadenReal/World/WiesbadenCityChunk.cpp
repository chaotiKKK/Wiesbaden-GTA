// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenCityChunk.h"

#include "WiesbadenReal.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GIS/WiesbadenChunkStaticMeshBaker.h"
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

	// Ziel der Bake-Umstellung: vorgekochte StaticMeshes, die beim Stream-in nur
	// GELADEN werden (kein ProcMesh-Proxy-Neuaufbau, kein Kollisions-Cook). Auf der
	// gebackenen Karte tragen sie die Geometrie; die ProcMeshes bleiben leer.
	RoadStaticMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RoadStaticMesh"));
	RoadStaticMesh->SetupAttachment(Root);

	BuildingStaticMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BuildingStaticMesh"));
	BuildingStaticMesh->SetupAttachment(Root);

	// Getrenntes, unsichtbares Kollisions-Mesh der Fahrbahn (nur kollisionsfaehige
	// Sections) - so bleiben Boeschungen kollisionsfrei, obwohl SM-Kollision sonst
	// die ganze Asset-Geometrie kocht.
	RoadCollisionStaticMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RoadCollisionStaticMesh"));
	RoadCollisionStaticMesh->SetupAttachment(Root);

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

	// Nachtraegliche Verteilung kann Zellen geleert oder gefuellt haben -
	// die Ankerung darf dem nicht hinterherhaengen.
	AnchorStreamingBounds();
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

	// Nach dem Instanz-Aufbau neu verankern: SpawnRegionAssets legt die
	// Varianten-Komponenten (Trees_01..) NEU an - im Konstruktor sitzen sie
	// am Actor-Ursprung, und genau dieser Ursprungspunkt war die 3x4-km-
	// Bounds-Falle. Auch leere Zellen (0 Regionsobjekte) brauchen den
	// Aufruf, weil ihre Basis-HISMs aus der Karte leer und am Ursprung sind.
	AnchorStreamingBounds();
}

void AWiesbadenCityChunk::AnchorStreamingBounds()
{
	// Packetschmutz VOR den Aenderungen: Programmatische Transforms rufen
	// (anders als Gizmo-Zuege) kein PostEditMove auf - ohne Modify(true)
	// bliebe das External-Actor-Package sauber, save_dirty_packages haette
	// nichts zu speichern und der Re-Bake wuerde laut "erfolgreich" laufen,
	// aber nichts schreiben. bAlwaysMarkDirty=true greift auch im
	// Commandlet (dort existiert keine Undo-Transaktion mehr).
	// Anker: Mittelpunkt des Zell-Inhalts. Zuerst die Mesh-Geometrie (die
	// Sections tragen Weltkoordinaten, CalcBounds liefert deren Welt-Box),
	// sonst die Regions-Assets. Voellig leere Zellen behalten den Ursprung -
	// sie tragen nur Punkt-Bounds und druecken die Actor-Bounds nicht auf.
	FVector Anchor = FVector::ZeroVector;
	bool bHasAnchor = false;
	for (const UProceduralMeshComponent* Mesh : { RoadMesh, BuildingMesh })
	{
		if (Mesh && Mesh->GetNumSections() > 0)
		{
			// IDENTITY, NICHT GetComponentTransform(): die Section-Vertices tragen
			// bereits WELT-Koordinaten, ihr Identity-Schwerpunkt IST der Welt-
			// mittelpunkt der Zelle. Im Build-Pfad hat CALL 1 (SetRegionAssets, vor
			// dem Section-Aufbau) das noch leere Mesh an den Zellmittelpunkt C
			// gezogen; mit GetComponentTransform() wuerde CALL 2 hier C + C = 2C
			// sampeln und alle leeren Komponenten faelschlich nach 2C ankern ->
			// Bounds wieder ueber km aufgeblaeht. Identity zaehlt den transienten
			// Komponentenversatz nicht doppelt (Re-Bake-Pfad: Mesh ohnehin am
			// Ursprung -> gleiches Ergebnis C).
			Anchor = Mesh->CalcBounds(FTransform::Identity).Origin;
			bHasAnchor = true;
			break;
		}
	}
	// Nach dem Bake tragen die StaticMesh-Komponenten die (welt-koordinierte)
	// Geometrie; ihre Bounds liefern denselben Zellmittelpunkt wie die ProcMeshes.
	if (!bHasAnchor)
	{
		for (const UStaticMeshComponent* SM : { RoadStaticMesh, BuildingStaticMesh, RoadCollisionStaticMesh })
		{
			if (SM && SM->GetStaticMesh())
			{
				Anchor = SM->CalcBounds(FTransform::Identity).Origin;
				bHasAnchor = true;
				break;
			}
		}
	}
	if (!bHasAnchor && RegionAssets.Num() > 0)
	{
		FBox AssetBounds(ForceInit);
		for (const FPlacedRegionAsset& Asset : RegionAssets)
		{
			AssetBounds += Asset.Location;
		}
		Anchor = AssetBounds.GetCenter();
		bHasAnchor = true;
	}
	if (!bHasAnchor)
	{
		return;
	}

	Modify(true);

	// Mesh-Komponenten: volle Geometrie steckt in WELT-Koordinaten, ihre
	// Komponente gehoert daher an die Actor-Position (Identitaet). Das Pin
	// ist noetig, weil SetRegionAssets VOR dem Section-Aufbau laeuft und
	// dort noch leere Meshes an den Asset-Schwerpunkt gezogen haette - die
	// danach erzeugten Sections wuerden verschoben rendern. Leere Meshes
	// dagegen an den Inhalt; nur sie tragen Punkt-Bounds.
	for (UProceduralMeshComponent* Mesh : { RoadMesh, BuildingMesh })
	{
		if (!Mesh)
		{
			continue;
		}
		if (Mesh->GetNumSections() > 0)
		{
			Mesh->SetWorldLocation(GetActorLocation());
		}
		else
		{
			Mesh->SetWorldLocation(Anchor);
		}
		Mesh->MarkRenderStateDirty();
	}
	// StaticMesh-Komponenten ebenso: mit Geometrie an die Actor-Position (Welt-
	// koordinaten), leere an den Zell-Inhalt (nur sie tragen Punkt-Bounds).
	for (UStaticMeshComponent* SM : { RoadStaticMesh, BuildingStaticMesh, RoadCollisionStaticMesh })
	{
		if (!SM)
		{
			continue;
		}
		if (SM->GetStaticMesh())
		{
			SM->SetWorldLocation(GetActorLocation());
		}
		else
		{
			SM->SetWorldLocation(Anchor);
		}
		SM->MarkRenderStateDirty();
	}
	if (RegionAssetSpawner)
	{
		RegionAssetSpawner->AnchorEmptyInstanceComponents(Anchor);
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

	// Der Chunk-Actor spawnt am Ursprung; ohne diese Ankerung wuerden die
	// leeren Komponenten (BuildingMesh ohne Gebaeude, ungenutzte Basis-HISMs)
	// Punkt-Bounds bei (0,0,0) beitragen und die Actor-Bounds bis zum Ursprung
	// spannen - World Partition koennte die Zelle nicht raeumlich trennen.
	AnchorStreamingBounds();
}

void AWiesbadenCityChunk::BakeToStaticMeshes(int32 CellX, int32 CellY, bool bRoadCollision, bool bBuildingCollision)
{
#if WITH_EDITOR
	// Ein ProcMesh -> ein vorgekochtes StaticMesh (Render + optional gekochte
	// Kollision), an die Komponente gehaengt. Leert den ProcMesh NICHT (die
	// Kollisions-Extraktion braucht RoadMesh noch); das Leeren macht der Aufrufer.
	auto BakeInto = [&](UProceduralMeshComponent* Src, UStaticMeshComponent* Dst,
		const TCHAR* Kind, bool bCollision, bool bHideRender, bool bNanite) -> bool
	{
		if (!Src || !Dst || Src->GetNumSections() == 0)
		{
			return false;
		}
		const FString Path = FString::Printf(TEXT("/Game/Generated/Chunks/SM_%s_%d_%d"), Kind, CellX, CellY);
		FString Err;
		UStaticMesh* Baked = WiesbadenChunkStaticMeshBaker::BakeFromProcMesh(Src, Path, bCollision, bNanite, Err);
		if (!Baked)
		{
			UE_LOG(LogWbCore, Warning, TEXT("BakeToStaticMeshes %s (%d,%d): %s"), Kind, CellX, CellY, *Err);
			return false;
		}
		Dst->SetStaticMesh(Baked);
		Dst->SetWorldLocation(GetActorLocation());   // Geometrie steckt in Weltkoordinaten
		Dst->SetCollisionEnabled(bCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		if (bCollision)
		{
			// Wie im ProcMesh-Pfad: die Fahrbahn blockiert Fahrzeuge, aber NICHT die
			// Verfolgerkamera (sonst zuckt der Arm an jeder Kuppe).
			Dst->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		}
		Dst->SetHiddenInGame(bHideRender);   // reines Kollisions-Mesh: unsichtbar, kollidiert aber
		return true;
	};

	// Fahrbahn RENDER: alle Sections (inkl. Boeschungen), OHNE gekochte Kollision,
	// mit Nanite (sichtbares Mesh).
	BakeInto(RoadMesh, RoadStaticMesh, TEXT("Road"), /*bCollision=*/false, /*bHidden=*/false, /*bNanite=*/true);

	// Fahrbahn KOLLISION: nur die kollisionsfaehigen Sections (ApplyChunk setzt
	// bEnableCollision = bRoadCollision && !Boeschung) in ein separates, unsichtbares
	// StaticMesh mit vorgekochter Trimesh-Kollision. So bekommen Boeschungen KEINE
	// Trimesh-Kollision (allein in Wiesbaden ~3,5 Mio Dreiecke) - wie im ProcMesh-Pfad.
	if (bRoadCollision && RoadMesh && RoadMesh->GetNumSections() > 0)
	{
		UProceduralMeshComponent* ColProc = NewObject<UProceduralMeshComponent>(this);
		ColProc->RegisterComponent();
		int32 DstSection = 0;
		for (int32 S = 0; S < RoadMesh->GetNumSections(); ++S)
		{
			const FProcMeshSection* Sec = RoadMesh->GetProcMeshSection(S);
			if (!Sec || !Sec->bEnableCollision || Sec->ProcIndexBuffer.Num() < 3)
			{
				continue;
			}
			TArray<FVector> Verts;
			TArray<FVector> Normals;
			TArray<FVector2D> UVs;
			TArray<int32> Tris;
			Verts.Reserve(Sec->ProcVertexBuffer.Num());
			Normals.Reserve(Sec->ProcVertexBuffer.Num());
			UVs.Reserve(Sec->ProcVertexBuffer.Num());
			for (const FProcMeshVertex& V : Sec->ProcVertexBuffer)
			{
				Verts.Add(V.Position);
				Normals.Add(V.Normal);
				UVs.Add(V.UV0);
			}
			Tris.Reserve(Sec->ProcIndexBuffer.Num());
			for (uint32 I : Sec->ProcIndexBuffer)
			{
				Tris.Add(static_cast<int32>(I));
			}
			ColProc->CreateMeshSection(DstSection++, Verts, Tris, Normals, UVs,
				TArray<FColor>(), TArray<FProcMeshTangent>(), /*bCreateCollision=*/true);
		}
		if (DstSection > 0)
		{
			// Unsichtbares Kollisions-Mesh: rendert nie -> KEIN Nanite (spart Bake-Zeit/Speicher).
			BakeInto(ColProc, RoadCollisionStaticMesh, TEXT("RoadCol"), /*bCollision=*/true, /*bHidden=*/true, /*bNanite=*/false);
		}
		ColProc->DestroyComponent();
	}

	// Gebaeude: wie bisher (Box-Koerper anderswo -> i.d.R. keine Trimesh-Kollision),
	// mit Nanite (sichtbares Mesh).
	BakeInto(BuildingMesh, BuildingStaticMesh, TEXT("Building"), bBuildingCollision, /*bHidden=*/false, /*bNanite=*/true);

	// ProcMesh-Puffer erst JETZT leeren (die Kollisions-Extraktion brauchte RoadMesh).
	RoadMesh->ClearAllMeshSections();
	BuildingMesh->ClearAllMeshSections();

	bBakedToStaticMesh = true;

	// Bounds neu auf den Zell-Inhalt ankern - jetzt tragen die StaticMesh-
	// Komponenten die Geometrie, die (geleerten) ProcMeshes nur Punkt-Bounds.
	AnchorStreamingBounds();
#endif
}

void AWiesbadenCityChunk::SetRoadSectionMaterial(int32 SectionIndex, UMaterialInterface* Material)
{
	if (!Material)
	{
		return;
	}
	// Nach dem Bake zielen die echten Materialien auf die StaticMesh-Slots (ein Slot
	// je ProcMesh-Section, Reihenfolge erhalten); davor auf das ProcMesh.
	if (bBakedToStaticMesh)
	{
		if (RoadStaticMesh) { RoadStaticMesh->SetMaterial(SectionIndex, Material); }
	}
	else if (RoadMesh)
	{
		RoadMesh->SetMaterial(SectionIndex, Material);
	}
}

void AWiesbadenCityChunk::SetBuildingSectionMaterial(int32 SectionIndex, UMaterialInterface* Material)
{
	if (!Material)
	{
		return;
	}
	if (bBakedToStaticMesh)
	{
		if (BuildingStaticMesh) { BuildingStaticMesh->SetMaterial(SectionIndex, Material); }
	}
	else if (BuildingMesh)
	{
		BuildingMesh->SetMaterial(SectionIndex, Material);
	}
}
