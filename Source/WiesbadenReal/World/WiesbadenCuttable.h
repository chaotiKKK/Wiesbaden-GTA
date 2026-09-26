// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "WiesbadenCuttable.generated.h"

class UPointLightComponent;
class USceneComponent;
class UStaticMeshComponent;

/**
 * Ein vorbereitetes Trenn-Objekt: zwei Stuecke (oben/unten), die der
 * Plasmacutter voneinander trennen kann.
 *
 * BEWUSST vorbereitet statt beliebigem Laufzeit-Slicing: das Zerschneiden
 * beliebiger Meshes kostet Laufzeit und liefert eine schlecht aussehende
 * Kante. Hier sind die Stuecke echte Mesh-Teile, und die Schnittkante
 * bekommt eine Glutkante (leuchtende Flaeche + Licht, ueber EmberSeconds
 * abklingend).
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenCuttable : public AActor
{
	GENERATED_BODY()

public:
	AWiesbadenCuttable();

	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Trennen entlang der Ebene durch CutPoint mit der uebergebenen Normale.
	 *
	 * Das Stueck auf der negativen Seite faellt ab (Physik) und glimmt an der
	 * Kante. Liefert false, wenn die Kante nur streift oder schon getrennt
	 * wurde - dann bleibt alles stehen.
	 */
	bool ApplyCut(const FVector& CutPoint, const FVector& PlaneNormal);

	/** Schon getrennt? (Ein zweiter Schnitt aendert nichts mehr.) */
	bool IsCut() const { return bCutDone; }

	/** Das abgefallene Stueck (Pruefung); null vor dem ersten Schnitt. */
	USceneComponent* GetFallenPiece() const { return FallenPiece; }

	/** Die beiden vorbereiteten Stuecke (Pruefung). */
	UStaticMeshComponent* GetPieceBelow() const { return PieceBelow; }
	UStaticMeshComponent* GetPieceAbove() const { return PieceAbove; }

	/** Glutkante (Pruefung): die Flaeche, die am abgefallenen Stueck klebt. */
	UStaticMeshComponent* GetEmberFace() const { return EmberFace; }

	/** Glutlicht (Pruefung): klingt mit der Flaeche zusammen ab. */
	UPointLightComponent* GetEmberLight() const { return EmberLight; }

	/** Glimmt die Schnittkante noch? */
	bool IsEmberGlowing() const { return EmberRemaining > 0.0f; }

	/** Restzeit des Glimmens (Pruefung). */
	float GetEmberRemaining() const { return EmberRemaining; }

	/** Wie lange die Schnittkante nach dem Trennen glimmt, in Sekunden. */
	UPROPERTY(EditAnywhere, Category = "Trennen", meta = (ClampMin = "0.5"))
	float EmberSeconds = 6.0f;

	/** Helligkeit der Glutkante beim Trennen (Candela). */
	UPROPERTY(EditAnywhere, Category = "Trennen", meta = (ClampMin = "0.0"))
	float EmberIntensity = 3000.0f;

private:
	/** Glutkante an das fallende Stueck heften (Flaeche + Licht). */
	void AttachEmber(USceneComponent* Piece, const FVector& CutPoint);

	/** Stueck unter der Mitte. */
	UPROPERTY(VisibleAnywhere, Category = "Trennen")
	UStaticMeshComponent* PieceBelow = nullptr;

	/** Stueck ueber der Mitte. */
	UPROPERTY(VisibleAnywhere, Category = "Trennen")
	UStaticMeshComponent* PieceAbove = nullptr;

	/** Glutkante: die leuchtende Schnittflaeche am abgefallenen Stueck. */
	UPROPERTY(VisibleAnywhere, Category = "Trennen")
	UStaticMeshComponent* EmberFace = nullptr;

	/** Glutkante: das Licht, das mit der Flaeche abklingt. */
	UPROPERTY(VisibleAnywhere, Category = "Trennen")
	UPointLightComponent* EmberLight = nullptr;

	/** Restzeit des Glimmens in Sekunden; 0 = aus. */
	float EmberRemaining = 0.0f;

	bool bCutDone = false;

	USceneComponent* FallenPiece = nullptr;
};
