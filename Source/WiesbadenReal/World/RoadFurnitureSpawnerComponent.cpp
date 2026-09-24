// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/RoadFurnitureSpawnerComponent.h"

#include "WiesbadenReal.h"

#include "GIS/WiesbadenSignAssets.h"
#include "World/StreetFurnitureShapes.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/UObjectGlobals.h"
#include "World/WiesbadenCitySubsystem.h"
#include "Engine/World.h"

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

	Zone30Instances = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("Zone30Symbols"));
	Zone30Instances->SetupAttachment(this);
	Zone30Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);

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
	if (Zone30Instances) { Zone30Instances->ClearInstances(); }

	for (UHierarchicalInstancedStaticMeshComponent* Panel : SignPanelInstances)
	{
		if (Panel)
		{
			Panel->DestroyComponent();
		}
	}
	SignPanelInstances.Reset();

	if (LampPostInstances) { LampPostInstances->ClearInstances(); }

	for (UHierarchicalInstancedStaticMeshComponent* Group : FurnitureInstances)
	{
		if (Group)
		{
			Group->DestroyComponent();
		}
	}
	FurnitureInstances.Reset();

	for (UHierarchicalInstancedStaticMeshComponent* Group : FurnitureMeshInstances)
	{
		if (Group)
		{
			Group->DestroyComponent();
		}
	}
	FurnitureMeshInstances.Reset();
	FurnitureMeshCache.Reset();

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
	LastSpawnedFurnitureCount = 0;
	LastSpawnedFurniturePartCount = 0;
	LastFurnitureWithMeshCount = 0;
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
	SpawnStreetFurniture(Layout.Furniture, CubeMesh, CylinderMesh);

	const ECollisionEnabled::Type Collision = bCreateCollision
		? ECollisionEnabled::QueryAndPhysics
		: ECollisionEnabled::NoCollision;
	SignPoleInstances->SetCollisionEnabled(Collision);
	DelineatorPostInstances->SetCollisionEnabled(Collision);
	DelineatorReflectorInstances->SetCollisionEnabled(Collision);
	MarkingInstances->SetCollisionEnabled(Collision);
	// Aufgemalte "30" -> immer kollisionsfrei, unabhaengig von bCreateCollision.
	if (Zone30Instances) { Zone30Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision); }
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

	// Zeichen ohne Grafik: kein Panel (siehe unten), je Id nur einmal gemeckert.
	TSet<FString> SkippedSignIds;

	const float PlaneScale = SignSizeCm / 100.0f;    // Engine-Plane = 100x100.
	const float PoleScaleXy = SignPoleRadiusCm / 50.0f;  // Zylinder-Radius 50.
	const float PoleScaleZ = SignPoleHeightCm / 100.0f;  // Zylinder-Hoehe 100.

	for (const FSignInstance& Sign : Signs)
	{
		// Id aus den Daten auf die Form der Grafik bringen (die gebackenen
		// Kacheln tragen noch " 274.1" bzw. "1042-31[Mo-Sa 08:00-19:00]").
		const FString SignId = WiesbadenSignAssets::NormalizeSignId(Sign.SignId);
		if (SignId.IsEmpty() || SkippedSignIds.Contains(SignId))
		{
			continue;
		}

		UHierarchicalInstancedStaticMeshComponent* Panel = PanelBySignId.FindRef(SignId);
		if (!Panel)
		{
			// Tafel nur mit eigener Grafik: ein Panel ohne Materialinstanz traegt
			// im ISM das Vorgabebild des Basismaterials (M_WbSign -> Sign_206) und
			// zeigt damit ein sichtbar FALSCHES Schild. Lieber keine Tafel als die
			// falsche; die Id wird einmal gemeldet und dann uebersprungen.
			UMaterialInstanceDynamic* MID = WiesbadenSignAssets::CreateMaterial(
				SignId, SignTextureFolder, SignMaterial, SignTextureParameterName, GetOwner());
			if (!MID)
			{
				SkippedSignIds.Add(SignId);
				continue;
			}

			Panel = NewObject<UHierarchicalInstancedStaticMeshComponent>(GetOwner());
			Panel->SetupAttachment(this);
			Panel->SetStaticMesh(PanelMesh);
			Panel->SetCollisionEnabled(
				bCreateCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
			Panel->SetMaterial(0, MID);
			Panel->RegisterComponent();

			PanelBySignId.Add(SignId, Panel);
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
			TEXT("Ausstattungs-Spawner: kein SignMaterial zugewiesen - Tafeln werden uebersprungen."));
	}

	// Eine Zeile statt einer Warnung je Zeichen: was fehlt, ist die importierte
	// Grafik unter <SignTextureFolder> (PNG allein genuegt nicht - die Engine
	// laedt nur das Asset).
	if (SkippedSignIds.Num() > 0)
	{
		UE_LOG(LogWbCore, Warning,
			TEXT("Ausstattungs-Spawner: %d Zeichen ohne Tafel uebersprungen (kein Grafik-Asset in %s): %s"),
			SkippedSignIds.Num(), *SignTextureFolder,
			*FString::Join(SkippedSignIds.Array(), TEXT(", ")));
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

	// "30"-Zonensymbole tragen ein eigenes, maskiertes Material (weisse Ziffern
	// auf transparentem Grund) -> eigenes ISM. Fehlt das Material, bleibt der
	// Default (grau) - besser als gar keine Markierung.
	if (Zone30Instances)
	{
		Zone30Instances->SetStaticMesh(PlaneMesh);
		if (UMaterialInterface* Zone30Mat = LoadObject<UMaterialInterface>(
				nullptr, TEXT("/Game/Materials/City/M_WbZone30.M_WbZone30")))
		{
			Zone30Instances->SetMaterial(0, Zone30Mat);
		}
	}

	for (const FMarkingInstance& Marking : Markings)
	{
		const FVector Dir = Marking.Direction.GetSafeNormal2D();
		const float Yaw = Dir.Rotation().Yaw;
		const float WidthScale = static_cast<float>(Marking.WidthCm) / 100.0f;

		if (Marking.Kind == ERoadMarkingKind::SpeedZone30)
		{
			// Aufgemalte "30": eine flache Quad-Instanz mittig auf der Fahrbahn.
			// Die Textur laeuft hochkant (Ziffern entlang V = lokal +Y). Plane um
			// Yaw-90 drehen, damit die Ziffern LAENGS zur Fahrtrichtung stehen;
			// LengthCm laeuft dann laengs, WidthCm quer.
			if (Zone30Instances)
			{
				const FVector Center = Marking.Center + FVector(0.0, 0.0, MarkingZOffsetCm);
				Zone30Instances->AddInstance(
					FTransform(
						FRotator(0.0f, Yaw - 90.0f, 0.0f).Quaternion(),
						Center,
						FVector(WidthScale, static_cast<float>(Marking.LengthCm) / 100.0f, 1.0f)),
					/*bWorldSpace=*/true);
				++LastSpawnedMarkingCount;
			}
		}
		else if (Marking.Kind == ERoadMarkingKind::StopLine)
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
	EnsureLampHeads();
}

float URoadFurnitureSpawnerComponent::ComputeStreetLampNightFactor(float SunElevationFactor)
{
	// Einblenden zwischen 0,18 (Scheinwerfer-Automatik der Fahrzeuge) und
	// -0,05 (Sonne knapp unter dem Horizont) - Stadt- und Fahrzeuglicht
	// gehen damit zusammen an, nicht erst in tiefer Nacht.
	constexpr float On = -0.05f;
	constexpr float Off = 0.18f;
	const float T = FMath::Clamp((Off - SunElevationFactor) / (Off - On), 0.0f, 1.0f);
	return T * T * (3.0f - 2.0f * T);
}

namespace
{
	/** Ecken je Zylinder - bei 9 cm Mastradius und 32 cm Kappe glatt genug. */
	constexpr int32 LampSides = 24;

	/**
	 * Zylinder (Mantel + beide Deckel) in Welt-Zentimetern um die Mastmitte
	 * anlegen und in den vorskalierten Raum teilen. Jedes Dreieck wird so
	 * gewickelt, dass Cross(B - A, C - A) nach aussen zeigt - die Konvention des
	 * Projekts (siehe GIS.PolygonUtils.RibbonWinding), sonst entfernt das
	 * Backface-Culling die Aussenseite. Eine positive Skalierung aendert das
	 * Vorzeichen nicht; Mantel- und Deckelnormalen bleiben unter ihr gleich.
	 */
	void AddLampCylinder(FStreetLampGeometry& G, int32 Section, double CentreZ, double Radius,
		double HalfHeight, const FVector& Scale)
	{
		auto Tri = [&G, Section, &Scale](FVector A, FVector B, FVector C, const FVector& Normal,
			FVector2D UA, FVector2D UB, FVector2D UC)
		{
			if (FVector::DotProduct(FVector::CrossProduct(B - A, C - A), Normal) < 0.0)
			{
				Swap(B, C);
				Swap(UB, UC);
			}
			for (const TPair<FVector, FVector2D>& Corner : { TPair<FVector, FVector2D>(A, UA),
				TPair<FVector, FVector2D>(B, UB), TPair<FVector, FVector2D>(C, UC) })
			{
				const FVector& P = Corner.Key;
				G.Positions.Add(FVector3f(P.X / Scale.X, P.Y / Scale.Y, P.Z / Scale.Z));
				G.Normals.Add(FVector3f(Normal));
				G.UVs.Add(FVector2f(Corner.Value));
			}
			G.Section.Add(Section);
		};
		const double Bottom = CentreZ - HalfHeight;
		const double Top = CentreZ + HalfHeight;
		for (int32 I = 0; I < LampSides; ++I)
		{
			const double A0 = UE_DOUBLE_TWO_PI * I / LampSides;
			const double A1 = UE_DOUBLE_TWO_PI * (I + 1) / LampSides;
			const FVector D0(FMath::Cos(A0), FMath::Sin(A0), 0.0);
			const FVector D1(FMath::Cos(A1), FMath::Sin(A1), 0.0);
			const FVector Mid = (D0 + D1).GetSafeNormal();
			const double U0 = double(I) / LampSides;
			const double U1 = double(I + 1) / LampSides;
			// Mantel: zwei Dreiecke je Segment.
			Tri(D0 * Radius + FVector(0, 0, Bottom), D1 * Radius + FVector(0, 0, Bottom),
				D1 * Radius + FVector(0, 0, Top), Mid, FVector2D(U0, 1), FVector2D(U1, 1), FVector2D(U1, 0));
			Tri(D0 * Radius + FVector(0, 0, Bottom), D1 * Radius + FVector(0, 0, Top),
				D0 * Radius + FVector(0, 0, Top), Mid, FVector2D(U0, 1), FVector2D(U1, 0), FVector2D(U0, 0));
			// Deckel oben und unten als Faecher.
			auto Cap = [](const FVector& D) { return FVector2D(0.5 + 0.5 * D.X, 0.5 + 0.5 * D.Y); };
			Tri(FVector(0, 0, Top), D0 * Radius + FVector(0, 0, Top), D1 * Radius + FVector(0, 0, Top),
				FVector::UpVector, FVector2D(0.5, 0.5), Cap(D0), Cap(D1));
			Tri(FVector(0, 0, Bottom), D1 * Radius + FVector(0, 0, Bottom), D0 * Radius + FVector(0, 0, Bottom),
				-FVector::UpVector, FVector2D(0.5, 0.5), Cap(D1), Cap(D0));
		}
	}

	/** Laufzeit-Mesh aus der Geometrie (zwei Slots: Mast, Glas). */
	UStaticMesh* BuildLampMesh(const FStreetLampGeometry& G, UObject* Outer,
		UMaterialInterface* PostMaterial, UMaterialInterface* GlassMaterial)
	{
		FMeshDescription Desc;
		FStaticMeshAttributes Attr(Desc);
		Attr.Register();
		Attr.GetVertexInstanceUVs().SetNumChannels(1);
		TVertexAttributesRef<FVector3f> Positions = Attr.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> Normals = Attr.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector3f> Tangents = Attr.GetVertexInstanceTangents();
		TVertexInstanceAttributesRef<float> Signs = Attr.GetVertexInstanceBinormalSigns();
		TVertexInstanceAttributesRef<FVector2f> UVs = Attr.GetVertexInstanceUVs();
		TPolygonGroupAttributesRef<FName> SlotNames = Attr.GetPolygonGroupMaterialSlotNames();
		const FName Slots[2] = { TEXT("Mast"), TEXT("Glas") };
		FPolygonGroupID Groups[2];
		for (int32 S = 0; S < 2; ++S)
		{
			Groups[S] = Desc.CreatePolygonGroup();
			SlotNames[Groups[S]] = Slots[S];
		}
		for (int32 T = 0; T < G.Section.Num(); ++T)
		{
			FVertexInstanceID Vi[3];
			for (int32 K = 0; K < 3; ++K)
			{
				const int32 Index = T * 3 + K;
				const FVertexID V = Desc.CreateVertex();
				Positions[V] = G.Positions[Index];
				Vi[K] = Desc.CreateVertexInstance(V);
				const FVector3f N = G.Normals[Index];
				Normals[Vi[K]] = N;
				Tangents[Vi[K]] = FMath::Abs(N.Z) > 0.9f
					? FVector3f(1.0f, 0.0f, 0.0f) : FVector3f::CrossProduct(FVector3f::UpVector, N).GetSafeNormal();
				Signs[Vi[K]] = 1.0f;
				UVs.Set(Vi[K], 0, G.UVs[Index]);
			}
			Desc.CreatePolygon(Groups[G.Section[T]], { Vi[0], Vi[1], Vi[2] });
		}
		UStaticMesh* Mesh = NewObject<UStaticMesh>(Outer, TEXT("SM_WbStreetLamp"), RF_Transient);
		Mesh->GetStaticMaterials().Add(FStaticMaterial(PostMaterial, Slots[0], Slots[0]));
		Mesh->GetStaticMaterials().Add(FStaticMaterial(GlassMaterial, Slots[1], Slots[1]));
		UStaticMesh::FBuildMeshDescriptionsParams Params;
		Params.bBuildSimpleCollision = false;
		Params.bFastBuild = true;
		return Mesh->BuildFromMeshDescriptions({ &Desc }, Params) ? Mesh : nullptr;
	}
}

FStreetLampGeometry URoadFurnitureSpawnerComponent::BuildStreetLampGeometry(const FVector& InstanceScale)
{
	FStreetLampGeometry G;
	const FVector Scale(FMath::Max(InstanceScale.X, 1e-3), FMath::Max(InstanceScale.Y, 1e-3),
		FMath::Max(InstanceScale.Z, 1e-3));
	// Mast: genau der Engine-Zylinder (100 cm hoch, Radius 50, Ursprung mittig)
	// - nach der Instanz-Skalierung also derselbe Mast wie bisher.
	const double HalfPost = 50.0 * Scale.Z;
	AddLampCylinder(G, 0, 0.0, 50.0 * Scale.X, HalfPost, Scale);
	// Pilzleuchte wie an Wohnstrassen: Kappe 64 cm breit und 10 cm hoch, 22 cm
	// ueber der Mastspitze; das Glas 50 cm breit und 16 cm hoch darunter.
	AddLampCylinder(G, 0, HalfPost + 22.0, 32.0, 5.0, Scale);
	AddLampCylinder(G, 1, HalfPost + 9.0, 25.0, 8.0, Scale);
	return G;
}

void URoadFurnitureSpawnerComponent::EnsureLampHeads()
{
	if (!LampPostInstances || LampPostInstances->GetInstanceCount() == 0
		|| (LampMesh && LampPostInstances->GetStaticMesh() == LampMesh))
	{
		return;
	}
	// Alle Masten tragen dieselbe Skalierung (SpawnStreetLamps bzw. die
	// gebackenen Instanzen) - die erste gilt fuer alle.
	FTransform First;
	LampPostInstances->GetInstanceTransform(0, First, /*bWorldSpace=*/false);
	FTransform Last;
	LampPostInstances->GetInstanceTransform(LampPostInstances->GetInstanceCount() - 1, Last, false);
	if (!First.GetScale3D().Equals(Last.GetScale3D(), 0.01))
	{
		UE_LOG(LogWbCore, Warning, TEXT("Laternen: Masten verschieden skaliert (%s / %s) - Koepfe sitzen nur auf den ersten richtig."),
			*First.GetScale3D().ToString(), *Last.GetScale3D().ToString());
	}

	// Eigenes ISM-taugliches Material (Tools/create_street_lamp_material.py):
	// ohne used_with_instanced_static_meshes ersetzt UE es im Spiel durch
	// das Default-Material.
	if (!LampGlassMID)
	{
		if (UMaterialInterface* Glass = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/Materials/City/M_WbStreetLampGlass.M_WbStreetLampGlass")))
		{
			LampGlassMID = UMaterialInstanceDynamic::Create(Glass, this);
			LampGlassMID->SetVectorParameterValue(TEXT("LensColor"), LampLightColor);
			LampGlassMID->SetScalarParameterValue(TEXT("Glow"), 0.0f);
		}
	}
	// Mast und Kappe: das Pfostenmaterial der Strassenausstattung (M_WbPole, wie
	// die Schilderpfosten). Der nackte Engine-Zylinder trug nur das Platzhalter-
	// Raster DefaultMaterial; dass die Masten damit fast schwarz wirkten, lag an
	// den gestauchten Zylinder-UVs, nicht an einer gewollten Farbe.
	UMaterialInterface* PostMaterial = LampPostMaterial ? LampPostMaterial
		: PoleMaterial ? PoleMaterial : LampPostInstances->GetMaterial(0);
	LampMesh = BuildLampMesh(BuildStreetLampGeometry(First.GetScale3D()), this, PostMaterial, LampGlassMID);
	if (!LampMesh)
	{
		UE_LOG(LogWbCore, Warning, TEXT("Laternen: Laternen-Netz liess sich nicht bauen - Masten bleiben ohne Kopf."));
		return;
	}
	LampPostInstances->SetStaticMesh(LampMesh);
	LampPostInstances->SetMaterial(0, PostMaterial);
	if (LampGlassMID)
	{
		LampPostInstances->SetMaterial(1, LampGlassMID);
	}
	LastLampNightFactor = -1.0f;
	UE_LOG(LogWbCore, Log, TEXT("Leuchtenkoepfe: %d Masten mit Kappe und Glas im Mast-Netz, keine Zusatz-Instanzen (Glasmaterial %s)."),
		LampPostInstances->GetInstanceCount(),
		LampGlassMID ? TEXT("geladen") : TEXT("FEHLT - Tools/create_street_lamp_material.py"));
}

void URoadFurnitureSpawnerComponent::UpdateLampNightState()
{
	const UWorld* World = GetWorld();
	const UWiesbadenCitySubsystem* City = World ? World->GetSubsystem<UWiesbadenCitySubsystem>() : nullptr;
	if (!City)
	{
		return;
	}
	// A/B-Schalter im Stil von -WbLumen: -WbNoStreetLamps lasst Glas-Glow und
	// Punktlichter aus, damit der Laternen-Beitrag im Nachtbild messbar ist
	// (andere Threads aendern die Szene laufend - ein Vergleich gegen alte
	// Screenshots misst sonst alles moegliche ausser den Laternen).
	static const bool bNoStreetLamps = FParse::Param(FCommandLine::Get(), TEXT("WbNoStreetLamps"));
	const float Night = bNoStreetLamps
		? 0.0f
		: ComputeStreetLampNightFactor(City->GetWeatherState().SunElevationFactor());
	if (FMath::Abs(Night - LastLampNightFactor) < 0.01f)
	{
		return;
	}
	const bool bFirst = LastLampNightFactor < 0.0f;
	LastLampNightFactor = Night;
	if (LampGlassMID)
	{
		LampGlassMID->SetScalarParameterValue(TEXT("Glow"), LampGlassGlowAtNight * Night);
	}
	// Punktlichter tagsueber aus: 48 unsichtbare 40.000-cd-Lichter kosteten
	// bisher auch in der Mittagssonne Bildzeit.
	for (UPointLightComponent* Light : LampLights)
	{
		if (Light)
		{
			Light->SetIntensity(LampLightIntensity * Night);
			Light->SetVisibility(Night > 0.01f);
		}
	}
	if (bFirst)
	{
		UE_LOG(LogWbCore, Log, TEXT("Laternen: Nachtanteil %.2f (Sonne %.2f) - Glas-Glow %.1f, Punktlichter %s."),
			Night, City->GetWeatherState().SunElevationFactor(), LampGlassGlowAtNight * Night,
			Night > 0.01f ? TEXT("an") : TEXT("aus"));
	}
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
	EnsureLampHeads();
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
		// Sichtbarkeit/Staerke folgen der Tageszeit (UpdateLampNightState).
		Light->SetVisibility(LastLampNightFactor > 0.01f);
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

	// Tageszeit zuerst - sie laeuft auch, wenn der Spieler steht.
	UpdateLampNightState();

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

UHierarchicalInstancedStaticMeshComponent* URoadFurnitureSpawnerComponent::GetFurnitureInstances(
	EFurnitureMeshKind Mesh,
	EFurnitureMaterialKind Material,
	UStaticMesh* CubeMesh,
	UStaticMesh* CylinderMesh)
{
	const int32 MaterialCount = static_cast<int32>(EFurnitureMaterialKind::MAX);
	const int32 Index = static_cast<int32>(Mesh) * MaterialCount + static_cast<int32>(Material);

	if (FurnitureInstances.Num() <= Index)
	{
		FurnitureInstances.SetNumZeroed(static_cast<int32>(EFurnitureMeshKind::MAX) * MaterialCount);
	}
	if (FurnitureInstances[Index])
	{
		return FurnitureInstances[Index];
	}

	// Erst beim ersten Teil dieser Paarung anlegen - eine Stadt ohne
	// Picknick-Tische soll keinen leeren Holz-ISM mitschleppen. Dasselbe
	// Muster wie bei den Schild-Tafeln (ein ISM je Zeichen).
	UHierarchicalInstancedStaticMeshComponent* Group =
		NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
	Group->SetupAttachment(this);
	Group->RegisterComponent();
	Group->SetStaticMesh(Mesh == EFurnitureMeshKind::Cylinder ? CylinderMesh : CubeMesh);
	Group->SetCollisionEnabled(bCreateCollision
		? ECollisionEnabled::QueryAndPhysics
		: ECollisionEnabled::NoCollision);
	Group->SetCullDistances(0, FurnitureCullDistanceCm);

	UMaterialInterface* WerkStoff = nullptr;
	switch (Material)
	{
	case EFurnitureMaterialKind::Wood:   WerkStoff = FurnitureWoodMaterial;   break;
	case EFurnitureMaterialKind::Signal: WerkStoff = FurnitureSignalMaterial; break;
	default:                             WerkStoff = FurnitureMetalMaterial;  break;
	}
	if (WerkStoff)
	{
		Group->SetMaterial(0, WerkStoff);
	}

	FurnitureInstances[Index] = Group;
	return Group;
}

void URoadFurnitureSpawnerComponent::SpawnStreetFurniture(
	const TArray<FFurnitureInstance>& Furniture,
	UStaticMesh* CubeMesh,
	UStaticMesh* CylinderMesh)
{
	LastSpawnedFurnitureCount = 0;
	LastSpawnedFurniturePartCount = 0;
	LastFurnitureWithMeshCount = 0;
	if (Furniture.Num() == 0)
	{
		return;
	}

	// Gebaute Meshes bevorzugen: EINE Instanz je Moebel statt zwei bis sieben
	// Primitivteilen, und das Holz sieht aus wie Holz. Was fehlt, faellt
	// unten auf die Primitive zurueck - ein frischer Klon hat die .uassets
	// nicht, und eine leere Stadt waere die schlechtere Antwort.
	const int32 MeshSlots = static_cast<int32>(EStreetFurnitureKind::MAX) * 2;
	TArray<TArray<FTransform>> ProMesh;
	ProMesh.SetNum(MeshSlots);

	TArray<FFurnitureInstance> OhneMesh;
	for (const FFurnitureInstance& Instance : Furniture)
	{
		const int32 Slot = static_cast<int32>(Instance.Kind) * 2 + (Instance.Variant > 0 ? 1 : 0);
		if (ResolveFurnitureMesh(Instance.Kind, Instance.Variant) != nullptr)
		{
			ProMesh[Slot].Add(FTransform(Instance.Rotation, Instance.Location));
		}
		else
		{
			OhneMesh.Add(Instance);
		}
	}

	for (int32 Slot = 0; Slot < MeshSlots; ++Slot)
	{
		if (ProMesh[Slot].Num() == 0)
		{
			continue;
		}
		const EStreetFurnitureKind Kind = static_cast<EStreetFurnitureKind>(Slot / 2);
		UStaticMesh* Mesh = ResolveFurnitureMesh(Kind, Slot % 2);
		if (FurnitureMeshInstances.Num() <= Slot)
		{
			FurnitureMeshInstances.SetNumZeroed(MeshSlots);
		}
		UHierarchicalInstancedStaticMeshComponent* Group = FurnitureMeshInstances[Slot];
		if (!Group)
		{
			Group = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
			Group->SetupAttachment(this);
			Group->RegisterComponent();
			Group->SetStaticMesh(Mesh);
			Group->SetCollisionEnabled(bCreateCollision
				? ECollisionEnabled::QueryAndPhysics
				: ECollisionEnabled::NoCollision);
			Group->SetCullDistances(0, FurnitureCullDistanceCm);
			FurnitureMeshInstances[Slot] = Group;
		}
		Group->AddInstances(ProMesh[Slot], /*bShouldReturnIndices=*/false, /*bWorldSpace=*/true);
		LastFurnitureWithMeshCount += ProMesh[Slot].Num();
	}

	if (OhneMesh.Num() == 0)
	{
		LastSpawnedFurnitureCount = Furniture.Num();
		ProtokolliereMoebel(Furniture, 0, 0);
		return;
	}

	// Alle Teile EINMAL sammeln und dann je Paarung am Stueck eintragen.
	//
	// AddInstance je Teil einzeln aufzurufen kostet bei rund zehntausend
	// Teilen jedes Mal eine Aktualisierung des Instanzpuffers; AddInstances
	// nimmt den ganzen Schwung auf einmal. Die Reserve verhindert zusaetzlich
	// das Umkopieren waehrend des Sammelns.
	int32 ErwarteteTeile = 0;
	for (const FFurnitureInstance& Instance : OhneMesh)
	{
		ErwarteteTeile += WiesbadenStreetFurniture::GetPartCount(Instance.Kind, Instance.Variant);
	}

	TArray<FFurniturePart> Teile;
	Teile.Reserve(ErwarteteTeile);
	for (const FFurnitureInstance& Instance : OhneMesh)
	{
		WiesbadenStreetFurniture::BuildParts(Instance, FurnitureDimensions, Teile);
	}

	const int32 MaterialCount = static_cast<int32>(EFurnitureMaterialKind::MAX);
	const int32 GroupCount = static_cast<int32>(EFurnitureMeshKind::MAX) * MaterialCount;
	TArray<TArray<FTransform>> ProGruppe;
	ProGruppe.SetNum(GroupCount);
	for (const FFurniturePart& Teil : Teile)
	{
		const int32 Index = static_cast<int32>(Teil.Mesh) * MaterialCount
			+ static_cast<int32>(Teil.Material);
		ProGruppe[Index].Add(Teil.Transform);
	}

	for (int32 Index = 0; Index < GroupCount; ++Index)
	{
		if (ProGruppe[Index].Num() == 0)
		{
			continue;
		}
		const EFurnitureMeshKind Mesh = static_cast<EFurnitureMeshKind>(Index / MaterialCount);
		const EFurnitureMaterialKind Material =
			static_cast<EFurnitureMaterialKind>(Index % MaterialCount);
		UHierarchicalInstancedStaticMeshComponent* Group =
			GetFurnitureInstances(Mesh, Material, CubeMesh, CylinderMesh);
		Group->AddInstances(ProGruppe[Index], /*bShouldReturnIndices=*/false, /*bWorldSpace=*/true);
	}

	LastSpawnedFurnitureCount = Furniture.Num();
	LastSpawnedFurniturePartCount = Teile.Num();

	int32 BelegteGruppen = 0;
	for (const TArray<FTransform>& Gruppe : ProGruppe)
	{
		BelegteGruppen += Gruppe.Num() > 0 ? 1 : 0;
	}
	ProtokolliereMoebel(Furniture, Teile.Num(), BelegteGruppen);
}

UStaticMesh* URoadFurnitureSpawnerComponent::ResolveFurnitureMesh(
	EStreetFurnitureKind Kind, int32 Variant)
{
	const int32 Slot = static_cast<int32>(Kind) * 2 + (Variant > 0 ? 1 : 0);
	const int32 SlotCount = static_cast<int32>(EStreetFurnitureKind::MAX) * 2;
	if (FurnitureMeshCache.Num() != SlotCount)
	{
		FurnitureMeshCache.SetNumZeroed(SlotCount);
		FurnitureMeshSearched.Init(false, SlotCount);
	}
	if (!FurnitureMeshSearched[Slot])
	{
		// EINMAL je Art suchen. Ohne das Merken liefe bei 2.000 Moebeln ohne
		// Assets zweitausendmal ein LoadObject auf denselben fehlenden Pfad.
		FurnitureMeshSearched[Slot] = true;
		const FString Pfad = WiesbadenStreetFurniture::GetMeshPath(Kind, Variant);
		FurnitureMeshCache[Slot] = Pfad.IsEmpty()
			? nullptr
			: LoadObject<UStaticMesh>(nullptr, *Pfad);
	}
	return FurnitureMeshCache[Slot];
}

void URoadFurnitureSpawnerComponent::ProtokolliereMoebel(
	const TArray<FFurnitureInstance>& Furniture, int32 TeilInstanzen, int32 Zeichengruppen)
{
	// Je Art zaehlen: "3.412 Moebel" sagt nichts darueber, ob die Baenke
	// fehlen. Die Zeile ist die einzige Stelle, an der ohne Bild auffaellt,
	// dass eine ganze Kategorie leer geblieben ist.
	int32 ProArt[static_cast<int32>(EStreetFurnitureKind::MAX)] = {};
	for (const FFurnitureInstance& Instance : Furniture)
	{
		++ProArt[static_cast<int32>(Instance.Kind)];
	}

	UE_LOG(LogWbCore, Log,
		TEXT("Strassenmoebel gestellt: %d Moebel (%d mit gebautem Mesh, %d Teil-Instanzen ")
		TEXT("in %d Zeichengruppen als Rueckfall) - Baenke %d, Poller %d, Koerbe %d, ")
		TEXT("Automaten %d, Recycling %d, Hydranten %d, Briefkaesten %d, Picknick %d; ")
		TEXT("Sichtweite %.0f m."),
		Furniture.Num(), LastFurnitureWithMeshCount, TeilInstanzen, Zeichengruppen,
		ProArt[static_cast<int32>(EStreetFurnitureKind::Bench)],
		ProArt[static_cast<int32>(EStreetFurnitureKind::Bollard)],
		ProArt[static_cast<int32>(EStreetFurnitureKind::WasteBasket)],
		ProArt[static_cast<int32>(EStreetFurnitureKind::VendingMachine)],
		ProArt[static_cast<int32>(EStreetFurnitureKind::Recycling)],
		ProArt[static_cast<int32>(EStreetFurnitureKind::FireHydrant)],
		ProArt[static_cast<int32>(EStreetFurnitureKind::PostBox)],
		ProArt[static_cast<int32>(EStreetFurnitureKind::PicnicTable)],
		FurnitureCullDistanceCm / 100.0f);
}
