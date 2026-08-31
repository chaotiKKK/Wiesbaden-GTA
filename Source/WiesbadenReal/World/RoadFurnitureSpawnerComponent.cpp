// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/RoadFurnitureSpawnerComponent.h"

#include "WiesbadenReal.h"

#include "GIS/WiesbadenSignAssets.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "UObject/UObjectGlobals.h"

URoadFurnitureSpawnerComponent::URoadFurnitureSpawnerComponent()
{
	// Die Laternen-Leuchten wandern mit dem Spieler mit (siehe TickComponent).
	PrimaryComponentTick.bCanEverTick = true;

	SignPoleInstances = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("SignPoles"));
	SignPoleInstances->SetupAttachment(this);
	SignPoleInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	DelineatorPostInstances = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("DelineatorPosts"));
	DelineatorPostInstances->SetupAttachment(this);
	DelineatorPostInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	DelineatorReflectorInstances = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("DelineatorReflectors"));
	DelineatorReflectorInstances->SetupAttachment(this);
	DelineatorReflectorInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	MarkingInstances = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("Markings"));
	MarkingInstances->SetupAttachment(this);
	MarkingInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	LampPostInstances = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("LampPosts"));
	LampPostInstances->SetupAttachment(this);
	LampPostInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void URoadFurnitureSpawnerComponent::ClearFurniture()
{
	if (SignPoleInstances) { SignPoleInstances->ClearInstances(); }
	if (DelineatorPostInstances) { DelineatorPostInstances->ClearInstances(); }
	if (DelineatorReflectorInstances) { DelineatorReflectorInstances->ClearInstances(); }
	if (MarkingInstances) { MarkingInstances->ClearInstances(); }

	for (UHierarchicalInstancedStaticMeshComponent* Panel : SignPanelInstances)
	{
		if (Panel)
		{
			Panel->DestroyComponent();
		}
	}
	SignPanelInstances.Reset();

	if (LampPostInstances) { LampPostInstances->ClearInstances(); }

	for (UPointLightComponent* Light : LampLights)
	{
		if (Light)
		{
			Light->DestroyComponent();
		}
	}
	LampLights.Reset();
	LampLocations.Reset();
	LastLampReference = FVector(TNumericLimits<double>::Max());

	LastSpawnedSignCount = 0;
	LastSpawnedDelineatorCount = 0;
	LastSpawnedMarkingCount = 0;
	LastSpawnedLampCount = 0;
}

void URoadFurnitureSpawnerComponent::SpawnFurniture(const FRoadFurnitureLayout& Layout)
{
	ClearFurniture();

	// Engine-Basis-Meshes (werden von LoadObject gecacht).
	UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane"));
	UStaticMesh* CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder"));
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube"));
	if (!PlaneMesh || !CylinderMesh || !CubeMesh)
	{
		UE_LOG(LogWbCore, Warning,
			TEXT("Ausstattungs-Spawner: Engine-Basis-Meshes (Plane/Cylinder/Cube) nicht gefunden."));
		return;
	}

	SpawnSigns(Layout.Signs, PlaneMesh, CylinderMesh);
	SpawnDelineators(Layout.Delineators, CylinderMesh, CubeMesh);
	SpawnMarkings(Layout.Markings, PlaneMesh);
	SpawnStreetLamps(Layout.StreetLamps, CylinderMesh);

	const ECollisionEnabled::Type Collision = bCreateCollision
		? ECollisionEnabled::QueryAndPhysics
		: ECollisionEnabled::NoCollision;
	SignPoleInstances->SetCollisionEnabled(Collision);
	DelineatorPostInstances->SetCollisionEnabled(Collision);
	DelineatorReflectorInstances->SetCollisionEnabled(Collision);
	MarkingInstances->SetCollisionEnabled(Collision);
	LampPostInstances->SetCollisionEnabled(Collision);

	// Sichtweiten setzen: Ohne sie zeichnet die Engine auch Leitpfosten, die
	// einen Kilometer entfernt sind und dort ein Pixel gross waeren.
	SignPoleInstances->SetCullDistances(0, SignCullDistanceCm);
	DelineatorPostInstances->SetCullDistances(0, DelineatorCullDistanceCm);
	DelineatorReflectorInstances->SetCullDistances(0, DelineatorCullDistanceCm);
	LampPostInstances->SetCullDistances(0, LampPostCullDistanceCm);

	for (UHierarchicalInstancedStaticMeshComponent* Panel : SignPanelInstances)
	{
		if (Panel)
		{
			Panel->SetCullDistances(0, SignCullDistanceCm);
		}
	}

	UE_LOG(LogWbCore, Log,
		TEXT("Ausstattungs-Spawner: %d Schilder, %d Leitpfosten, %d Markierungen, ")
		TEXT("%d Laternen (%d davon mit Licht)."),
		LastSpawnedSignCount, LastSpawnedDelineatorCount, LastSpawnedMarkingCount,
		LastSpawnedLampCount, LampLights.Num());
}

void URoadFurnitureSpawnerComponent::SpawnSigns(
	const TArray<FSignInstance>& Signs,
	UStaticMesh* PanelMesh,
	UStaticMesh* PoleMesh)
{
	LastSpawnedSignCount = 0;
	if (Signs.Num() == 0)
	{
		return;
	}

	SignPoleInstances->SetStaticMesh(PoleMesh);
	if (PoleMaterial)
	{
		SignPoleInstances->SetMaterial(0, PoleMaterial);
	}

	// Ein ISM je eindeutiger SignId (gemeinsame MID): Tafeln mit gleichem
	// Zeichen bleiben instanziert, nur unterschiedliche Zeichen erzeugen je
	// einen Draw-Call.
	TMap<FString, UHierarchicalInstancedStaticMeshComponent*> PanelBySignId;
	PanelBySignId.Reserve(Signs.Num());

	const float PlaneScale = SignSizeCm / 100.0f;    // Engine-Plane = 100x100.
	const float PoleScaleXy = SignPoleRadiusCm / 50.0f;  // Zylinder-Radius 50.
	const float PoleScaleZ = SignPoleHeightCm / 100.0f;  // Zylinder-Hoehe 100.

	for (const FSignInstance& Sign : Signs)
	{
		UHierarchicalInstancedStaticMeshComponent*& Panel = PanelBySignId.FindOrAdd(Sign.SignId);
		if (!Panel)
		{
			Panel = NewObject<UHierarchicalInstancedStaticMeshComponent>(GetOwner());
			Panel->SetupAttachment(this);
			Panel->SetStaticMesh(PanelMesh);
			Panel->SetCollisionEnabled(
				bCreateCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);

			if (UMaterialInstanceDynamic* MID = WiesbadenSignAssets::CreateMaterial(
				Sign.SignId, SignTextureFolder, SignMaterial, SignTextureParameterName, GetOwner()))
			{
				Panel->SetMaterial(0, MID);
			}

			Panel->RegisterComponent();
			SignPanelInstances.Add(Panel);
		}

		// Tafel: vertikale Flaeche, deren Normale in die Blickrichtung zeigt.
		// MakeFromZY bildet lokales +Z (Plane-Normale) auf die Blickrichtung und
		// lokales +Y auf Welt-Oben ab. Die negative Y-Skalierung spiegelt die
		// Tafel vertikal (die Engine-Plane zeigt V=0 unten); UE gleicht das
		// invertierte Winding beim Rendern automatisch aus.
		const FVector Facing = Sign.Rotation.Vector();
		const FVector PanelCenter = Sign.Location + FVector(0.0, 0.0, SignSizeCm * 0.5f);
		const FMatrix Basis = FRotationMatrix::MakeFromZY(Facing, FVector::UpVector);
		const FTransform PanelTransform(
			FQuat(Basis),
			PanelCenter,
			FVector(PlaneScale, -PlaneScale, 1.0f));

		Panel->AddInstance(PanelTransform, /*bWorldSpace=*/true);

		// Mast: senkrechter Zylinder vom Boden bis zur Schildunterkante.
		const FVector PoleCenter = FVector(
			Sign.Location.X,
			Sign.Location.Y,
			Sign.Location.Z - SignPoleHeightCm * 0.5f);
		SignPoleInstances->AddInstance(
			FTransform(FQuat::Identity, PoleCenter, FVector(PoleScaleXy, PoleScaleXy, PoleScaleZ)),
			/*bWorldSpace=*/true);

		++LastSpawnedSignCount;
	}

	if (!SignMaterial)
	{
		UE_LOG(LogWbCore, Warning,
			TEXT("Ausstattungs-Spawner: kein SignMaterial zugewiesen - Tafeln rendern mit Default-Material."));
	}
}

void URoadFurnitureSpawnerComponent::SpawnDelineators(
	const TArray<FDelineatorInstance>& Delineators,
	UStaticMesh* CylinderMesh,
	UStaticMesh* CubeMesh)
{
	LastSpawnedDelineatorCount = 0;
	if (Delineators.Num() == 0)
	{
		return;
	}

	DelineatorPostInstances->SetStaticMesh(CylinderMesh);
	if (DelineatorMaterial)
	{
		DelineatorPostInstances->SetMaterial(0, DelineatorMaterial);
	}
	else if (PoleMaterial)
	{
		DelineatorPostInstances->SetMaterial(0, PoleMaterial);
	}

	DelineatorReflectorInstances->SetStaticMesh(CubeMesh);
	if (ReflectorMaterial)
	{
		DelineatorReflectorInstances->SetMaterial(0, ReflectorMaterial);
	}

	const float PostScaleXy = DelineatorRadiusCm / 50.0f;
	const float PostScaleZ = DelineatorHeightCm / 100.0f;

	// Einmal die tatsaechlich erzeugten Masse melden. Aus einem Bild laesst
	// sich Groesse nur schaetzen; hier steht sie in Zentimetern.
	UE_LOG(LogWbCore, Log,
		TEXT("Leitpfosten: %.0f cm Durchmesser, %.0f cm hoch (Skalierung %.3f / %.3f)."),
		DelineatorRadiusCm * 2.0f, DelineatorHeightCm, PostScaleXy, PostScaleZ);
	const FVector ReflectorScale = ReflectorSizeCm / 100.0f; // Engine-Cube = 100cm.

	for (const FDelineatorInstance& Delineator : Delineators)
	{
		// Pfosten vom Boden bis zur vollen Hoehe.
		const FVector PostCenter = Delineator.Location + FVector(0.0, 0.0, DelineatorHeightCm * 0.5f);
		DelineatorPostInstances->AddInstance(
			FTransform(FQuat::Identity, PostCenter, FVector(PostScaleXy, PostScaleXy, PostScaleZ)),
			/*bWorldSpace=*/true);

		// Reflektor-Platte an der Blickrichtung (quer zur Fahrbahn).
		const FVector Facing = Delineator.Rotation.Vector();
		const FVector ReflectorPos = Delineator.Location
			+ FVector(0.0, 0.0, ReflectorHeightCm)
			+ Facing * (DelineatorRadiusCm + ReflectorSizeCm.X * 0.5f + 1.0f);
		const FMatrix Basis = FRotationMatrix::MakeFromX(Facing);
		DelineatorReflectorInstances->AddInstance(
			FTransform(FQuat(Basis), ReflectorPos, ReflectorScale),
			/*bWorldSpace=*/true);

		++LastSpawnedDelineatorCount;
	}
}

void URoadFurnitureSpawnerComponent::SpawnMarkings(
	const TArray<FMarkingInstance>& Markings,
	UStaticMesh* PlaneMesh)
{
	LastSpawnedMarkingCount = 0;
	if (Markings.Num() == 0)
	{
		return;
	}

	MarkingInstances->SetStaticMesh(PlaneMesh);
	if (MarkingMaterial)
	{
		MarkingInstances->SetMaterial(0, MarkingMaterial);
	}
	else
	{
		UE_LOG(LogWbCore, Warning,
			TEXT("Ausstattungs-Spawner: kein MarkingMaterial zugewiesen - Markierungen rendern mit Default-Material."));
	}

	for (const FMarkingInstance& Marking : Markings)
	{
		const FVector Dir = Marking.Direction.GetSafeNormal2D();
		const float Yaw = Dir.Rotation().Yaw;
		const float WidthScale = static_cast<float>(Marking.WidthCm) / 100.0f;

		if (Marking.Kind == ERoadMarkingKind::StopLine)
		{
			// Durchgehende Haltlinie: eine Quad-Instanz quer zur Fahrbahn.
			const FVector Center = Marking.Center + FVector(0.0, 0.0, MarkingZOffsetCm);
			MarkingInstances->AddInstance(
				FTransform(
					FRotator(0.0f, Yaw, 0.0f).Quaternion(),
					Center,
					FVector(static_cast<float>(Marking.LengthCm) / 100.0f, WidthScale, 1.0f)),
				/*bWorldSpace=*/true);
			++LastSpawnedMarkingCount;
		}
		else
		{
			// Unterbrochene Wartelinie: Striche entlang der Linienachse.
			const double Half = Marking.LengthCm * 0.5;
			const double Step = static_cast<double>(GiveWayDashLengthCm) + static_cast<double>(GiveWayGapLengthCm);

			for (double D = -Half; D < Half; D += Step)
			{
				const double DashLength = FMath::Min(static_cast<double>(GiveWayDashLengthCm), Half - D);
				if (DashLength <= 0.0)
				{
					break;
				}

				const FVector DashCenter = Marking.Center
					+ Dir * static_cast<float>(D + DashLength * 0.5)
					+ FVector(0.0, 0.0, MarkingZOffsetCm);
				MarkingInstances->AddInstance(
					FTransform(
						FRotator(0.0f, Yaw, 0.0f).Quaternion(),
						DashCenter,
						FVector(static_cast<float>(DashLength) / 100.0f, WidthScale, 1.0f)),
					/*bWorldSpace=*/true);
				++LastSpawnedMarkingCount;
			}
		}
	}
}

void URoadFurnitureSpawnerComponent::SpawnStreetLamps(
	const TArray<FStreetLampInstance>& Lamps,
	UStaticMesh* CylinderMesh)
{
	LastSpawnedLampCount = 0;

	if (Lamps.Num() == 0 || !LampPostInstances || !CylinderMesh)
	{
		return;
	}

	LampPostInstances->SetStaticMesh(CylinderMesh);
	if (LampPostMaterial)
	{
		LampPostInstances->SetMaterial(0, LampPostMaterial);
	}

	// Der Engine-Zylinder ist 100 cm hoch und hat 50 cm Radius, sein Ursprung
	// liegt in der Mitte. Der Mast wird deshalb auf halbe Hoehe gehoben.
	const float RadiusScale = FMath::Max(LampPostRadiusCm, 1.0f) / 50.0f;
	const float HeightScale = FMath::Max(LampPostHeightCm, 1.0f) / 100.0f;

	UE_LOG(LogWbCore, Log,
		TEXT("Laternenmasten: %.0f cm Durchmesser, %.0f cm hoch (Skalierung %.3f / %.3f)."),
		LampPostRadiusCm * 2.0f, LampPostHeightCm, RadiusScale, HeightScale);
	const FVector PostScale(RadiusScale, RadiusScale, HeightScale);

	for (const FStreetLampInstance& Lamp : Lamps)
	{
		const FVector PostCentre = Lamp.Location + FVector(0.0f, 0.0f, LampPostHeightCm * 0.5f);
		LampPostInstances->AddInstance(
			FTransform(FRotator::ZeroRotator, PostCentre, PostScale), /*bWorldSpace=*/true);
		++LastSpawnedLampCount;
	}

	// Standorte merken - die Auswahl der Leuchten folgt spaeter dem Spieler.
	LampLocations.Reset(Lamps.Num());
	for (const FStreetLampInstance& Lamp : Lamps)
	{
		LampLocations.Add(Lamp.Location);
	}

	CreateLampLightPool();
}

void URoadFurnitureSpawnerComponent::CreateLampLightPool()
{
	// Eine feste Zahl Leuchten anlegen, die anschliessend nur noch umgesetzt
	// wird. Die Masten kosten als ISM einen einzigen Draw-Call und stehen
	// deshalb alle; Punktlichter kosten je Stueck, bei 3.332 Laternen in
	// Wiesbaden waere das nicht darstellbar.
	if (LampLights.Num() > 0 || LampLocations.Num() == 0)
	{
		return;
	}

	const int32 LightBudget = FMath::Clamp(MaxActiveLampLights, 0, LampLocations.Num());
	for (int32 Slot = 0; Slot < LightBudget; ++Slot)
	{
		UPointLightComponent* Light = NewObject<UPointLightComponent>(this);
		if (!Light)
		{
			continue;
		}

		Light->SetupAttachment(this);
		Light->RegisterComponent();
		Light->SetAttenuationRadius(LampLightRadiusCm);
		Light->SetIntensity(LampLightIntensity);
		Light->SetLightColor(LampLightColor);

		// Ohne Schattenwurf: schattenwerfende Punktlichter in einer
		// Strassenschlucht kosten mehr, als die Beleuchtung optisch einbringt.
		Light->SetCastShadows(false);
		Light->SetVisibility(false);

		LampLights.Add(Light);
	}
}

void URoadFurnitureSpawnerComponent::BeginPlay()
{
	Super::BeginPlay();

	// In der gebackenen Karte laeuft SpawnFurniture nicht mehr - die Masten
	// kommen als gespeicherte ISM-Instanzen, die Leuchten muessen hier neu
	// entstehen. LampLocations ueberlebt als UPROPERTY.
	//
	// Die Zahl wird protokolliert, weil "es ist nachts dunkel" sonst drei
	// verschiedene Ursachen haben kann, die sich im Bild gleich anfuehlen:
	// keine Laternen in den Daten, Standorte beim Backen verloren, oder
	// Leuchten am falschen Ort.
	UE_LOG(LogWbCore, Log,
		TEXT("Laternen beim Start: %d Standorte geladen, %d Mast-Instanzen, Budget %d."),
		LampLocations.Num(),
		LampPostInstances ? LampPostInstances->GetInstanceCount() : 0,
		MaxActiveLampLights);

	CreateLampLightPool();
}

void URoadFurnitureSpawnerComponent::UpdateLampLights(const FVector& Reference)
{
	if (LampLights.Num() == 0 || LampLocations.Num() == 0)
	{
		return;
	}

	// Die naechstgelegenen Laternen bestimmen.
	//
	// WICHTIG: Bezugspunkt ist der SPIELER, nicht die Komponente. Der
	// Ausstattungs-Spawner haengt am WorldBuilder, und der steht - wie alle
	// Stadt-Actors - im Weltursprung. Waehlt man nach GetComponentLocation(),
	// leuchten die 48 Laternen rund um den Nullpunkt, waehrend der Spieler
	// einen Kilometer entfernt im Dunkeln steht. Dieselbe Falle ist in
	// AGENTS.md bereits fuer die Chunk-Actors beschrieben.
	TArray<int32> Order;
	Order.Reserve(LampLocations.Num());
	for (int32 Index = 0; Index < LampLocations.Num(); ++Index)
	{
		Order.Add(Index);
	}

	const FVector Ref = Reference;
	Order.Sort([this, &Ref](int32 A, int32 B)
	{
		return FVector::DistSquared(LampLocations[A], Ref)
			< FVector::DistSquared(LampLocations[B], Ref);
	});

	const int32 Count = FMath::Min(LampLights.Num(), Order.Num());
	for (int32 Slot = 0; Slot < Count; ++Slot)
	{
		UPointLightComponent* Light = LampLights[Slot];
		if (!Light)
		{
			continue;
		}

		const FVector& LampBase = LampLocations[Order[Slot]];
		Light->SetWorldLocation(LampBase + FVector(0.0f, 0.0f, LampPostHeightCm));
		Light->SetVisibility(true);
	}

	// Einmalig melden, wie weit die naechste Laterne ueberhaupt entfernt ist.
	// OSM hat fuer Wiesbaden nur 3.332 Laternen erfasst - real sind es ein
	// Vielfaches. Ohne diese Zahl liesse sich "es ist dunkel" nicht von
	// "hier ist keine Laterne kartiert" unterscheiden.
	static bool bReportedNearest = false;
	if (!bReportedNearest && Order.Num() > 0)
	{
		bReportedNearest = true;
		// Zusaetzlich der Zustand der ERSTEN Leuchte. "48 aktiv" sagt nur, dass
		// die Schleife lief - nicht, dass dort Licht ankommt.
		FString FirstState = TEXT("keine");
		if (LampLights.Num() > 0 && LampLights[0])
		{
			const FVector L = LampLights[0]->GetComponentLocation();
			FirstState = FString::Printf(
				TEXT("(%.0f, %.0f, %.0f), sichtbar %s, %.0f cd, Radius %.0f cm, angehaengt an %s"),
				L.X, L.Y, L.Z,
				LampLights[0]->IsVisible() ? TEXT("ja") : TEXT("NEIN"),
				LampLights[0]->Intensity,
				LampLights[0]->AttenuationRadius,
				LampLights[0]->GetAttachParent() ? TEXT("ja") : TEXT("NEIN"));
		}

		UE_LOG(LogWbCore, Log,
			TEXT("Laternen: naechste %.0f m entfernt, %d Leuchten aktiv von %d Standorten. ")
			TEXT("Erste Leuchte: %s. Bezugspunkt (%.0f, %.0f, %.0f)."),
			FVector::Dist(LampLocations[Order[0]], Reference) / 100.0,
			LampLights.Num(), LampLocations.Num(), *FirstState,
			Reference.X, Reference.Y, Reference.Z);
	}

	LastLampReference = Reference;
}

void URoadFurnitureSpawnerComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (LampLights.Num() == 0)
	{
		return;
	}

	// Nicht jeden Frame sortieren: 3.332 Standorte zu ordnen ist zu teuer fuer
	// 60 Hz, und die Auswahl aendert sich beim Fahren nur langsam.
	LampUpdateCountdown -= DeltaTime;
	if (LampUpdateCountdown > 0.0f)
	{
		return;
	}
	LampUpdateCountdown = 0.5f;

	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);

	// Erst umsetzen, wenn der Spieler ein Stueck weit gefahren ist.
	if (FVector::DistSquared(ViewLocation, LastLampReference) < 500.0 * 500.0)
	{
		return;
	}

	UpdateLampLights(ViewLocation);
}
