// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenCityChunk.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

#include "World/WiesbadenStreamingCost.h"

#include "WiesbadenReal.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GIS/WiesbadenChunkStaticMeshBaker.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
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

EWbBuildingUse AWiesbadenCityChunk::BuildingUseFromMaterialName(const FString& MaterialName)
{
	// Die Fassadennamen kommen aus der Pipeline: MI_WbFacade_<Art>.
	if (MaterialName.Contains(TEXT("Buerohaus"))
		|| MaterialName.Contains(TEXT("Glasturm"))
		|| MaterialName.Contains(TEXT("Hochhaus")))
	{
		return EWbBuildingUse::Office;
	}

	if (MaterialName.Contains(TEXT("Wohnhaus"))
		|| MaterialName.Contains(TEXT("Altbau"))
		|| MaterialName.Contains(TEXT("Nachkrieg"))
		|| MaterialName.Contains(TEXT("Ziegel"))
		|| MaterialName.Contains(TEXT("Klinker"))
		|| MaterialName.Contains(TEXT("Backstein"))
		|| MaterialName.Contains(TEXT("Sandstein")))
	{
		return EWbBuildingUse::Residential;
	}

	return EWbBuildingUse::Other;
}

float AWiesbadenCityChunk::WindowLightStrength(EWbBuildingUse Use, float TimeOfDayHours)
{
	// Stunde auf [0, 24) bringen - die Uhr kann ueber Mitternacht laufen.
	float H = FMath::Fmod(TimeOfDayHours, 24.0f);
	if (H < 0.0f)
	{
		H += 24.0f;
	}

	// Stuetzstellen (Stunde, Staerke), dazwischen linear. Eine Tabelle statt
	// verschachtelter Bedingungen: so ist die Kurve an einem Blick ablesbar
	// und im Test Stuetzstelle fuer Stuetzstelle pruefbar.
	struct FKey { float Hour; float Value; };

	// Wohnen: morgens kurz hell, tagsueber aus, abends lange hell, nachts
	// einzelne Fenster.
	static const FKey Residential[] = {
		{ 0.0f, 0.10f }, { 5.0f, 0.10f }, { 6.5f, 0.55f }, { 8.0f, 0.30f },
		{ 9.0f, 0.00f }, { 16.0f, 0.00f }, { 18.5f, 0.85f }, { 21.0f, 1.00f },
		{ 22.5f, 0.85f }, { 24.0f, 0.10f }
	};

	// Buero: Feierabend statt Abendprogramm - um 23 Uhr ist Schluss.
	static const FKey Office[] = {
		{ 0.0f, 0.04f }, { 6.0f, 0.04f }, { 7.5f, 0.70f }, { 9.0f, 0.25f },
		{ 16.0f, 0.25f }, { 17.5f, 0.80f }, { 20.0f, 0.55f }, { 22.0f, 0.15f },
		{ 24.0f, 0.04f }
	};

	const FKey* Keys = nullptr;
	int32 Count = 0;
	float Scale = 1.0f;

	switch (Use)
	{
	case EWbBuildingUse::Office:
		Keys = Office;
		Count = UE_ARRAY_COUNT(Office);
		break;
	case EWbBuildingUse::Residential:
		Keys = Residential;
		Count = UE_ARRAY_COUNT(Residential);
		break;
	default:
		// Unbekannte Nutzung: wie Wohnen, aber gedaempft - lieber zu wenig
		// Licht als ein Schuppen, der aussieht wie ein Wohnzimmer.
		Keys = Residential;
		Count = UE_ARRAY_COUNT(Residential);
		Scale = 0.45f;
		break;
	}

	for (int32 i = 0; i + 1 < Count; ++i)
	{
		if (H <= Keys[i + 1].Hour)
		{
			const float Span = Keys[i + 1].Hour - Keys[i].Hour;
			const float Alpha = Span > KINDA_SMALL_NUMBER
				? (H - Keys[i].Hour) / Span : 0.0f;
			return FMath::Lerp(Keys[i].Value, Keys[i + 1].Value, Alpha) * Scale;
		}
	}

	return Keys[Count - 1].Value * Scale;
}

void AWiesbadenCityChunk::ApplyWindowLight(float TimeOfDayHours)
{
	// Idempotent: erst ab einer merklichen Aenderung neu setzen. Ohne das
	// entstuenden je Bild neue dynamische Materialinstanzen fuer jede geladene
	// Zelle.
	if (AppliedWindowLightHours >= 0.0f
		&& FMath::Abs(AppliedWindowLightHours - TimeOfDayHours) < 0.05f)
	{
		return;
	}
	AppliedWindowLightHours = TimeOfDayHours;

	// Gebackener Pfad: BuildingStaticMesh. Laufzeit-Pfad: BuildingMesh.
	UMeshComponent* Targets[] = {
		Cast<UMeshComponent>(BuildingStaticMesh), Cast<UMeshComponent>(BuildingMesh) };

	for (UMeshComponent* Mesh : Targets)
	{
		if (!Mesh)
		{
			continue;
		}

		const int32 SlotCount = Mesh->GetNumMaterials();
		for (int32 Slot = 0; Slot < SlotCount; ++Slot)
		{
			UMaterialInterface* Material = Mesh->GetMaterial(Slot);
			if (!Material)
			{
				continue;
			}

			// Die Nutzungsart steckt im Namen des ZUGEWIESENEN Materials. Bei
			// einer dynamischen Instanz traegt der Name den der Vorlage, also
			// bleibt die Zuordnung ueber die Zeit stabil.
			const EWbBuildingUse Use = BuildingUseFromMaterialName(Material->GetName());
			const float Strength = WindowLightStrength(Use, TimeOfDayHours);

			UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Material);
			if (!Dynamic)
			{
				Dynamic = Mesh->CreateDynamicMaterialInstance(Slot, Material);
			}
			if (Dynamic)
			{
				// Sichtprüfung statt Rechnung: die Kurve liefert 0..1 als
				// "wie viel Licht", das Material macht daraus ZWEIERLEI -
				// den Anteil der erleuchteten Fenster UND ihre Helligkeit.
				// Ungedaempft leuchteten bei 0,9 rund neun von zehn Fenstern
				// gleisshell; im Nachtbild war die Fassade ein Leuchtkasten.
				// 0,38 ergibt gut ein Drittel erleuchtete Fenster in ruhigem
				// Warmton - nachgemessen am Bild, nicht geschaetzt.
				constexpr float VisualCalibration = 0.38f;
				Dynamic->SetScalarParameterValue(
					TEXT("FensterlichtStaerke"), Strength * VisualCalibration);
			}
		}
	}
}

void AWiesbadenCityChunk::BeginPlay()
{
	// Zuordnung der Nachlade-Aussetzer: dieser Aufruf laeuft im Spiel-Strang,
	// wenn World Partition die Zelle hereinstreamt.
	const FWbStreamingCostScope CostScope(FWbStreamingCost::BeginPlayMs);
	++FWbStreamingCost::Cells;

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
	const FWbStreamingCostScope CostScope(FWbStreamingCost::AnchorMs);

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
			const FVector O = Mesh->CalcBounds(FTransform::Identity).Origin;
			if (!O.ContainsNaN())
			{
				Anchor = O;
				bHasAnchor = true;
				break;
			}
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
				// CalcBounds eines gerade erst geladenen/gebackenen StaticMesh kann
				// NaN liefern (Render-Daten noch nicht bereit / degenerierte Bake-
				// Bounds). Ein NaN-Anker wuerde ueber SetWorldLocation eine ungueltige
				// Transform setzen (Ensure NewTransform.IsValid()), die Komponenten-
				// Bounds NaN machen und den Renderer beim ersten Bild abstuerzen lassen
				// (ContainsNaN -> IntFitsIn-narrowing). Solche Werte NICHT uebernehmen.
				const FVector O = SM->CalcBounds(FTransform::Identity).Origin;
				if (!O.ContainsNaN())
				{
					Anchor = O;
					bHasAnchor = true;
					break;
				}
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
	if (!bHasAnchor || Anchor.ContainsNaN())
	{
		// Kein gueltiger Anker -> Komponenten unveraendert lassen. NIE eine NaN-
		// Transform setzen (siehe oben): der Absturz lag hier, nicht im
		// Welt-Koordinaten-Design.
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
	// Diagnose-Schalter -WbProcMeshChunks: StaticMesh-Bake KOMPLETT ueberspringen,
	// die Chunks bleiben ProcMesh (mit synchron gekochter Kollision). Nur so laesst
	// sich eine gebackene ProcMesh-Karte erzeugen und im kontrollierten A/B gegen
	// die StaticMesh-Bake stellen (gleiche Quelle/Code, nur der Kollisionspfad
	// unterscheidet sich). ApplyChunk hat die Bounds bereits verankert.
	if (FParse::Param(FCommandLine::Get(), TEXT("WbProcMeshChunks")))
	{
		static bool bLoggedOnce = false;
		if (!bLoggedOnce)
		{
			bLoggedOnce = true;
			UE_LOG(LogWbCore, Log,
				TEXT("WbProcMeshChunks: StaticMesh-Bake uebersprungen - Chunks bleiben ProcMesh (Diagnose-A/B)."));
		}
		return;
	}

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
		// WICHTIG (Commit-OOM beim Stadt-Bake): das Mesh wieder FREIGEBEN, sobald
		// es gesichert ist. Dst->SetStaticMesh(Baked) haelt hier nur einen starken
		// Verweis - bei ~4600 Chunks x 3 Meshes blieben sonst ALLE gebackenen
		// Render-/Kollisionsdaten (mehrere 100 MB Raw-Daten je bake, zusammen
		// viele GiB) bis zum ENDE des BuildCity im Speicher, obwohl das Asset
		// laengst auf der Platte liegt. Nachfolgend setzen wir den Verweis
		// wegweisend NICHT auf das frische Objekt, sondern laden das Mesh beim
		// SPATEREN Gebrauch lazy - dazu gehoert der Komponenten-Verweis nach dem
		// Bake nur noch als Pfad, nicht als Objekt.
		//
		// Kurzfristig muss der Slot jedoch etwas anzeigen: Wir geben das Mesh
		// frei, indem die Komponente den Verweis verwirft, sobald die Welt
		// gespeichert wurde. Das loest der WorldBuilder nach SpawnCityChunks mit
		// UnloadBakedChunkMeshes (Purge + GC), nicht hier.
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

	// Fahrbahn RENDER: alle Sections (inkl. Boeschungen), OHNE gekochte Kollision.
	//
	// Nanite HIER (noch) AUS: Die Chunk-Geometrie steckt in absoluten WELT-
	// Koordinaten (Wiesbaden-Georeferenz, tausende Meter vom Ursprung). Nanite
	// kodiert Positionen/Instanz-Transforms fixpunktbasiert und laeuft bei diesen
	// Groessen ueber - der Renderer stuerzt reproduzierbar ab ("Ensure OriginX <=
	// OriginMax ... precision loss converting matrix to GPU format", danach
	// "IntFitsIn narrowing conversion"). Der Baker KANN Nanite (bEnableNanite,
	// per Test belegt an lokaler Geometrie); nutzbar wird es erst, wenn die
	// Chunk-Vertices LOKAL (relativ zum Zellmittelpunkt) gebacken und die
	// Komponente per Transform in die Welt gesetzt wird. Bis dahin: false.
	BakeInto(RoadMesh, RoadStaticMesh, TEXT("Road"), /*bCollision=*/false, /*bHidden=*/false, /*bNanite=*/false);

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

	// Gebaeude: wie bisher (Box-Koerper anderswo -> i.d.R. keine Trimesh-Kollision).
	// Nanite AUS aus demselben Grund wie bei der Fahrbahn (Welt-Koordinaten -> Nanite-
	// Praezisions-Absturz); erst mit lokal gebackener Geometrie aktivierbar.
	BakeInto(BuildingMesh, BuildingStaticMesh, TEXT("Building"), bBuildingCollision, /*bHidden=*/false, /*bNanite=*/false);

	// ProcMesh-Puffer erst JETZT leeren (die Kollisions-Extraktion brauchte RoadMesh).
	RoadMesh->ClearAllMeshSections();
	BuildingMesh->ClearAllMeshSections();

	bBakedToStaticMesh = true;

	// Bounds neu auf den Zell-Inhalt ankern - jetzt tragen die StaticMesh-
	// Komponenten die Geometrie, die (geleerten) ProcMeshes nur Punkt-Bounds.
	AnchorStreamingBounds();
#endif
}

void AWiesbadenCityChunk::UnloadBakedChunkMeshes()
{
#if WITH_EDITOR
	// Verweise auf die frisch gebackenen Meshes verwerfen, damit die Pakete
	// (und mit ihnen RenderData + gekochte Kollision) beim naechsten Purge/GC
	// freigegeben werden koennen. Die Komponenten bleiben bestehen (sie werden
	// von der gebackenen Karte beim Stream-in neu verdrahtet).
	for (UStaticMeshComponent* SM : { RoadStaticMesh, BuildingStaticMesh, RoadCollisionStaticMesh })
	{
		if (SM)
		{
			SM->SetStaticMesh(nullptr);
		}
	}
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

bool AWiesbadenCityChunk::HasRenderGeometry() const
{
	// Waehrend des Bakes tragen die ProcMeshes die Geometrie; nach
	// BakeToStaticMeshes sind sie leer und die StaticMesh-Komponenten tragen sie.
	// Beide Wege zaehlen, sonst gilt die fertige (StaticMesh-)Stadt als leer.
	const bool bRoad = (RoadMesh && RoadMesh->GetNumSections() > 0)
		|| (RoadStaticMesh && RoadStaticMesh->GetStaticMesh() != nullptr);
	const bool bBuilding = (BuildingMesh && BuildingMesh->GetNumSections() > 0)
		|| (BuildingStaticMesh && BuildingStaticMesh->GetStaticMesh() != nullptr);
	return bRoad || bBuilding;
}
