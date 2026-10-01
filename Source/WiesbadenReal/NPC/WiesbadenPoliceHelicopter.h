// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NPC/WiesbadenPoliceHeli.h"
#include "WiesbadenPoliceHelicopter.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class USpotLightComponent;
class UPointLightComponent;

/**
 * Polizei-Helikopter als Luft-Verfolger (Eskalation ab Fahndungsstufe 5).
 *
 * Die Bewegung/Sicht-Hysterese liegt datenrein in FWiesbadenPoliceHeli;
 * dieser Actor ergaenzt die Welt-Anbindung: echte Strahlen-Sichtpruefung
 * (WiesbadenPolice::CanSee) gegen Gebaeude, Bodenabstand (der Heli fliegt nie
 * ins Gelaeuse), rotierende Rotoren und ein Suchscheinwerfer auf den Spieler.
 * Modell: Ka-52-Rumpf (dieselben Meshes wie der fliegbare Heli, ohne Cockpit
 * und Waffe - das ist ein NPC, kein Fahrzeug).
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenPoliceHelicopter : public AActor
{
	GENERATED_BODY()

public:
	AWiesbadenPoliceHelicopter();
	virtual void Tick(float DeltaSeconds) override;

	/** Spieler aus der Luft im Blick (Modell-Hysterese UND echte Sicht). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Polizei")
	bool IsPlayerSpotted() const { return bPlayerSpotted; }

	/** Entfernung zum Spieler in Metern (fuer Log/HUD). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Polizei")
	float GetDistanceToPlayerMeters() const;

private:
	UPROPERTY(VisibleAnywhere, Category = "Polizei")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Polizei")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "Polizei")
	TObjectPtr<UStaticMeshComponent> RotorUpper;

	UPROPERTY(VisibleAnywhere, Category = "Polizei")
	TObjectPtr<UStaticMeshComponent> RotorLower;

	/** Suchscheinwerfer auf den Spieler (Sichtbarkeit der Verfolgung). */
	UPROPERTY(VisibleAnywhere, Category = "Polizei")
	TObjectPtr<USpotLightComponent> Searchlight;

	/** Blaulicht-Blinker (Polizei-Identitaet). */
	UPROPERTY(VisibleAnywhere, Category = "Polizei")
	TObjectPtr<UPointLightComponent> BlueLeft;

	UPROPERTY(VisibleAnywhere, Category = "Polizei")
	TObjectPtr<UPointLightComponent> BlueRight;

	FWiesbadenPoliceHeliState State;
	FWiesbadenPoliceHeliParams Params;
	bool bPlayerSpotted = false;
	float FlashTime = 0.0f;
};
