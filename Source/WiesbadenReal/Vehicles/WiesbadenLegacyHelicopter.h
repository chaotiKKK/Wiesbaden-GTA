// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vehicles/WiesbadenHelicopter.h"
#include "WiesbadenLegacyHelicopter.generated.h"

/**
 * Der ZWEITE fliegbare Hubschrauber - dasselbe Fluggeraet, anderes Modell.
 *
 * WAS SICH GEAENDERT HAT UND WARUM:
 *
 * Bis hierher war das ein `AActor` - eine Standfigur aus drei Netzen, "ohne
 * Fluglogik, damit es nicht faellt, nicht abstuerzt und nicht besessen wird".
 * Genau daran lag es: ein Actor ist kein Pawn, also konnte ihn nie jemand
 * uebernehmen. Er liess sich nicht betreten und nicht fliegen, und
 * `FindNearbyVehicle` sah ihn nicht einmal, weil es Pawns sucht.
 *
 * Die alte Begruendung - ein zweiter Spielerheli muesste Schwerkraft,
 * Drehzahl und Eingabe "einzeln abschalten" - traegt nicht mehr, seit er
 * genau das NICHT soll. Er soll fliegen. Damit ist die Ableitung die kleinere
 * Loesung: eine Flugmechanik, zwei Modelle. Was am Flugverhalten verbessert
 * wird, gilt sofort fuer beide.
 *
 * WAS DIESE KLASSE NOCH TUT:
 *
 * Nur die Geometrie umhaengen. Das alte Landmarken-Modell ist 100,7 cm lang
 * und wird auf 14,6 m skaliert (Faktor 14,5), seine Laengsachse liegt auf Y,
 * der Rotormast sitzt bei (1 | -5) im Modell und die Nabe im Rotormesh bei
 * (0 | 33). Alles Messungen am Asset - ohne sie kreisen die Blaetter neben
 * dem Mast. Das Ka-52-Mesh der Basisklasse braucht nichts davon (schon
 * eingebacken), genau deshalb muss es hier stehen.
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenLegacyHelicopter : public AWiesbadenHelicopter
{
	GENERATED_BODY()

public:
	AWiesbadenLegacyHelicopter();
	virtual void Tick(float DeltaSeconds) override;

	/** Rumpfnetz (fuer Kamera-/Sichtpruefungen und den Test). */
	UStaticMeshComponent* GetFuselageMesh() const { return FuselageMesh; }
	UStaticMeshComponent* GetUpperRotorMesh() const { return MainRotorBlade; }
	UStaticMeshComponent* GetLowerRotorMesh() const { return LowerRotorBlade; }
};
