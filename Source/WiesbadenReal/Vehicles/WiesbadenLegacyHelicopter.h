// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "WiesbadenLegacyHelicopter.generated.h"

class UStaticMeshComponent;

/**
 * Das ALTE Helikopter-Modell als Standstueck - neben dem neuen Spielerheli.
 *
 * Bis zum Ka-52-Neubau (2026-09-17) war der Spielerheli der alte Landmarken-
 * Heli aus /Game/Assets/Landmarks. Seit der Neubau das Fluggeraet ist, steht das
 * alte Modell hier als reine Anschauung daneben: gleiche Bauart, andere
 * Herkunft - und ohne Fluglogik, damit es nicht faellt, nicht abstuerzt und
 * nicht besessen wird.
 *
 * Warum ein eigener Actor und nicht AWiesbadenHelicopter mit Schalter: Der
 * Spielerheli ist ein Pawn mit Schwerkraft, Rotordrehzahl und Eingabe. Ein
 * zweiter davon in "geparkt" muesste all das einzeln abschalten - eine
 * Standfigur mit drei Netzen ist dagegen in sich stimmig.
 *
 * Masse und Lage sind die des ALTEN Modells (Stand vor dem Neubau, aus
 * WiesbadenHelicopter.cpp): Modellmasstab 1460/100,7 = 14,5, Mast bei
 * (1 | -5) im Modell, Rotornabe im Rotormesh bei (0 | 33), Nabenhoehen 345 und
 * 300 cm. Das neue Ka-52-Mesh braucht nichts davon (schon gebacken) - genau
 * deshalb muss der Aufbau hier stehen, sonst stuenden die Rotoren quer.
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenLegacyHelicopter : public AActor
{
	GENERATED_BODY()

public:
	AWiesbadenLegacyHelicopter();

	/**
	 * Laenge des aufgestellten Modells entlang seiner Blickrichtung, cm.
	 * Fuer die Abstandspruefung beim Aufstellen (Rotorkreise sollen sich nicht
	 * durchdringen) - kommt aus der Geometrie, nicht aus einer zweiten Zahl.
	 */
	double GetNoseToTailCm() const;

	/** Durchmesser des oberen Rotorkreises, cm (Abstand zweier Standstuecke). */
	double GetUpperRotorDiameterCm() const;

	/** Rumpfnetz (fuer Kamera-/Sichtpruefungen und den Test). */
	UStaticMeshComponent* GetFuselageMesh() const { return FuselageMesh; }
	UStaticMeshComponent* GetUpperRotorMesh() const { return UpperRotorMesh; }
	UStaticMeshComponent* GetLowerRotorMesh() const { return LowerRotorMesh; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Helikopter")
	USceneComponent* Root = nullptr;

	/** Rumpf (alter Landmarken-Heli, Ka-52-artige Silhouette). */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Helikopter")
	UStaticMeshComponent* FuselageMesh = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Helikopter")
	USceneComponent* UpperRotorHub = nullptr;
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Helikopter")
	USceneComponent* LowerRotorHub = nullptr;
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Helikopter")
	UStaticMeshComponent* UpperRotorMesh = nullptr;
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Helikopter")
	UStaticMeshComponent* LowerRotorMesh = nullptr;
};
