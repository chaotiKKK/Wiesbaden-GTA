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
 * Render-Ziel-Ansicht der Vollbild-Weltkarte.
 *
 * Zieht die STATISCHE Ebene (Strassen + Gebaeude) EINMAL in ein
 * CanvasRenderTarget statt sie je Bild aus ~16.000 Linien neu zu zeichnen. Die
 * HUD blittet danach nur das fertige Texture-Quad und zeichnet die DYNAMISCHEN
 * Overlays (Spielerpfeil, Text) je Bild live darueber.
 *
 * Die datenreine Geometrie/Projektion bleibt in FWiesbadenMinimap (dort getestet);
 * dieses UObject ist die nicht-unit-testbare Render-Orchestrierung. Der eine
 * datenreine Teil hier - "muss neu gerendert werden?" - ist als statische
 * Funktion herausgezogen und getestet (Test World.WorldMapView.NeedsRerender).
 */
UCLASS()
class WIESBADENREAL_API UWiesbadenWorldMapView : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Stellt sicher, dass Strassen+Gebaeude fuer die aktuelle Sicht im
	 * RenderTarget stehen und gibt das Texture zum Blitten zurueck - oder nullptr,
	 * wenn nichts gerendert werden konnte.
	 *
	 * Neu gerendert wird bei Netz-/Groessenwechsel ODER bei Sichtaenderung (Zoom
	 * bzw. verschobenes Blickzentrum). Solange die Sicht steht, wird nur das
	 * fertige Texture weiterverwendet.
	 *
	 * @param ZoomFactor        1 = ganzes Netz eingepasst; groesser = naeher heran.
	 * @param DesiredCentreWorld Blick-Mittelpunkt in Welt-cm (wird auf die
	 *                           Netzgrenzen geklemmt).
	 * @param bCentreValid      false auf dem ersten Bild nach dem Oeffnen: dann
	 *                           wird auf die Netzmitte zentriert (GetProjection()
	 *                           liefert das geklemmte Zentrum zum Zuruecklesen).
	 */
	UTextureRenderTarget2D* EnsureRendered(
		UWorld* World, const FRoadNetwork& Network,
		const TArray<FGeneratedBuilding>* Buildings, const FVector2D& ScreenSize,
		float ZoomFactor, const FVector2D& DesiredCentreWorld, bool bCentreValid);

	/** Projektion der zuletzt gerenderten Sicht (fuer die Overlays der HUD). */
	const FWorldMapProjection& GetProjection() const { return Projection; }

	/**
	 * Datenrein + testbar: muss das RenderTarget neu bespielt werden? True, wenn
	 * noch kein Ziel existiert, das Netz gewechselt hat oder sich die Bildgroesse
	 * geaendert hat.
	 */
	static bool NeedsRerender(
		bool bHasTarget,
		const FRoadNetwork* CachedNetwork, const FVector2D& CachedSize,
		const FRoadNetwork* Network, const FVector2D& Size);

private:
	UPROPERTY(Transient)
	TObjectPtr<UCanvasRenderTarget2D> RenderTarget = nullptr;

	/** Projektion der zuletzt gerenderten Sicht. */
	FWorldMapProjection Projection;

	/** Voll-Einpassung (Netzmitte + Fit-Massstab); nur bei Netz-/Groessenwechsel neu
	 *  berechnet - ComputeNetworkBoundsXY laeuft ueber alle Mittellinienpunkte. */
	FWorldMapProjection BaseFit;

	/** Netz + Bildgroesse, fuer die zuletzt gerendert wurde (Neu-Render-Erkennung). */
	const FRoadNetwork* CachedNetwork = nullptr;
	FVector2D CachedSize = FVector2D::ZeroVector;

	/** Zoom + Blickzentrum der zuletzt gerenderten Sicht (Sichtwechsel-Erkennung). */
	float CachedZoom = -1.0f;
	FVector2D CachedCentre = FVector2D(FLT_MAX, FLT_MAX);
};
