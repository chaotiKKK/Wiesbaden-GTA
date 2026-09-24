// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WiesbadenDennoShop.generated.h"

class UGeoCoordinateConverter;
class UStaticMeshComponent;
class UPointLightComponent;
class UMeshComponent;

/** Was der Laden mit dem Ergebnis der Wandsuche tut. */
enum class EDennoShopBuild : uint8
{
	Build,   // Wand gefunden - Laden bauen
	Wait,    // noch nicht gestreamt - naechster Tick
	GiveUp   // nach der Wartezeit keine Wand: KEIN Laden (statt eines schwebenden)
};

/**
 * Dennos Laden im Erdgeschoss von Sedanplatz 5 (OSM 175418681): Cafe in der
 * Nordhaelfte, Friseur in der Suedhaelfte, Denno selbst im Cafe.
 *
 * Das Haus ist GEBACKEN - eine geschlossene Chunk-Fassade mit aufgemalten
 * Fenstern. Ohne Re-Bake wird es so geoeffnet: die Fassadenmaterialien der
 * EINEN Chunk-Komponente, die das Haus traegt, bekommen eine Masked-Variante
 * (Tools/create_facade_cut_materials.py), die einen orientierten Kasten um die
 * Erdgeschossfront verwirft. Dahinter steht der Laden aus eigenen Blender-
 * Assets (Tools/Blender/build_denno_shop.py, build_denno_figure.py). Die
 * uebrige Stadt behaelt ihre opaken Fassaden.
 *
 * Lage: die Front zeigt nach Westen auf den Sedanplatz. Der Actor misst die
 * gebackene Wand per Strahl und setzt den Laden genau davor/dahinter.
 * Lokales System der Laden-Meshes: X nach Norden entlang der Front, +Y ins
 * Haus, -Y zur Strasse, Z ab Ladenboden.
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenDennoShop : public AActor
{
	GENERATED_BODY()

public:
	AWiesbadenDennoShop();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// -- Masse ---------------------------------------------------------------
	// Einzige Quelle ist Tools/denno_shop.json (liest auch build_denno_shop.py);
	// der Test WiesbadenReal.World.DennoShop.SharedDims prueft diese Konstanten
	// dagegen. Hier stehen sie nur, weil C++ sie zur Laufzeit ohne Dateizugriff
	// braucht.
	/** Halbe Breite des Ausschnitts = halbe Ladenbreite + Front-Ueberstand. */
	static constexpr double ShopHalfWidthCm = 705.0;
	/** Oberkante des Ausschnitts: knapp unter der Oberkante des Schildbands. */
	static constexpr double CutTopCm = 338.0;
	static constexpr double CutBottomCm = -10.0;
	/** Quer zur Wand: die gebackene Fassade ist eine Flaeche; +-60 cm fangen
	 *  kleine Abweichungen zwischen OSM-Linie und gebackener Wand. */
	static constexpr double CutHalfDepthCm = 60.0;
	/** Denno im Cafe, Blick zur Strasse (ihr Mesh blickt nach lokal +Y). */
	static constexpr double DennoXCm = 300.0;
	static constexpr double DennoYCm = 340.0;
	static constexpr float DennoYawDeg = 180.0f;
	/** So lange wird auf das gestreamte Haus gewartet, dann aufgegeben. */
	static constexpr double WallWaitSeconds = 30.0;
	/** Ein Wandtreffer zaehlt nur so nah an der OSM-Frontlinie (cm). */
	static constexpr double WallToleranceCm = 150.0;

	/** Entscheidung nach der Wandsuche (datenrein, Test). */
	static EDennoShopBuild DecideBuild(bool bWallFound, double WaitedSeconds);
	/** Liegt ein Treffer quer zur Front nah genug an der OSM-Linie? Ein
	 *  Schildmast oder Baum davor ist KEINE Hauswand. */
	static bool IsPlausibleWall(const FVector& Hit, const FVector& OsmFrontMid, const FVector& Outward);

	/** Halbmasse des Ausschnitts (cm): laengs der Front, quer zur Wand, Hoehe. */
	static FVector CutHalfExtentCm();
	/** Liegt ein Weltpunkt im orientierten Ausschnitt? (datenrein, Test) */
	static bool IsInsideCut(const FVector& Point, const FVector& Centre,
		const FVector2D& AxisU, const FVector& HalfExtent);

	bool IsBuilt() const { return bBuilt; }

private:
	bool TryBuild();
	/** Chunk-Fassaden um den Laden auf die Ausschnitt-Varianten umstellen. */
	int32 PatchFacades();
	void PatchComponent(UMeshComponent* Mesh);
	FVector WorldXY(const FVector2D& EastNorthM) const;
	UStaticMeshComponent* AddPart(const TCHAR* Name, const TCHAR* MeshPath,
		const FVector& LocalCm, float LocalYaw);

	UPROPERTY(Transient) USceneComponent* Root = nullptr;
	UPROPERTY(Transient) UGeoCoordinateConverter* Converter = nullptr;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> Parts;
	UPROPERTY(Transient) TArray<UPointLightComponent*> Lights;
	/** Bereits umgestellte Chunk-Komponenten (Streaming kann sie ersetzen). */
	TSet<TWeakObjectPtr<UMeshComponent>> PatchedFacades;

	bool bBuilt = false;
	double FirstAttemptSeconds = -1.0;
	double NextPatchSeconds = 0.0;
	FVector CutCentre = FVector::ZeroVector;
	FVector2D CutAxisU = FVector2D(1.0, 0.0);
};
