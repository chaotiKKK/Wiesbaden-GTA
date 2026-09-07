// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UI/WiesbadenMinimap.h"

#include "WiesbadenWorldMapView.generated.h"

class UCanvasRenderTarget2D;
class UTextureRenderTarget2D;
class UWorld;
struct FRoadNetwork;
struct FGeneratedBuilding;

/**
 * Basis-Karten-Textur der Vollbild-Weltkarte.
 *
 * Backt das GANZE Strassennetz + die Gebaeude EINMAL hochaufloesend (ueber-
 * abgetastet) in ein CanvasRenderTarget. Zoom und Pan macht danach die HUD
 * allein durch ein TEXTUR-TRANSFORM (sie blittet ein UV-Teilrechteck dieser
 * Textur auf den Bildschirm) - es wird NICHTS mehr je Bild neu gerastert. Das
 * beseitigt das Ruckeln, das entstand, weil frueher bei jedem Zoom/Pan bis zu
 * 16.000 Linien neu gezeichnet wurden.
 *
 * Strassen werden als gefuellte, an den Knoten verrundete Baender gebacken, damit
 * die Zuege nahtlos statt zerstueckelt wirken. Beschriftungen zeichnet die HUD
 * LIVE im Bildschirmraum darueber (immer scharf, unabhaengig vom Zoom).
 */
UCLASS()
class WIESBADENREAL_API UWiesbadenWorldMapView : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Stellt sicher, dass das GANZE Netz einmal in die Basis-Textur gebacken ist,
	 * und gibt sie zum Blitten zurueck (oder nullptr). Neu gebacken wird NUR bei
	 * Netz-Wechsel oder geaenderter Basisgroesse - nicht bei Zoom/Pan.
	 */
	UTextureRenderTarget2D* EnsureBaseMap(
		UWorld* World, const FRoadNetwork& Network,
		const TArray<FGeneratedBuilding>* Buildings, const FVector2D& ScreenSize);

	/** Einpass-Projektion, mit der die Basis-Textur gebacken wurde (Basis-Pixel). */
	const FWorldMapProjection& GetBaseFit() const { return BaseFit; }

	/** Pixelgroesse der Basis-Textur. */
	FVector2D GetBaseSize() const { return FVector2D(BaseSizeX, BaseSizeY); }

	/** True, wenn eine gueltige Basiskarte vorliegt. */
	bool HasBase() const { return RenderTarget != nullptr && BaseFit.IsValid(); }

	/**
	 * Datenrein + testbar: muss die Basiskarte neu gebacken werden? True, wenn
	 * noch keine existiert, das Netz gewechselt hat oder sich die Bildgroesse
	 * geaendert hat.
	 */
	static bool NeedsRerender(
		bool bHasTarget,
		const FRoadNetwork* CachedNetwork, const FVector2D& CachedSize,
		const FRoadNetwork* Network, const FVector2D& Size);

private:
	UPROPERTY(Transient)
	TObjectPtr<UCanvasRenderTarget2D> RenderTarget = nullptr;

	/** Einpassung, mit der gebacken wurde (Netzmitte + Fit-Massstab in Basis-Pixeln). */
	FWorldMapProjection BaseFit;

	/** Pixelgroesse der Basis-Textur. */
	int32 BaseSizeX = 0;
	int32 BaseSizeY = 0;

	/** Netz + Bildgroesse, fuer die zuletzt gebacken wurde (Neu-Back-Erkennung). */
	const FRoadNetwork* CachedNetwork = nullptr;
	FVector2D CachedSize = FVector2D::ZeroVector;
};
