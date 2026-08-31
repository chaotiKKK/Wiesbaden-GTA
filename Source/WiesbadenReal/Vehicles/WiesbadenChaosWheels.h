// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChaosVehicleWheel.h"

#include "WiesbadenChaosWheels.generated.h"

/**
 * Gemeinsame Radwerte des VW Kaefer 1969.
 *
 * Alle Zahlen sind am Modell vermessen oder aus den Fahrzeugdaten des Kaefers
 * uebernommen, nicht geschaetzt:
 *
 *   Reifen 5.60-15, Durchmesser 68,6 cm  -> Halbmesser 34,3 cm
 *   Laufflaechenbreite                      17,2 cm
 *   Leergewicht                            820 kg
 *
 * Der Kaefer ist HECKGETRIEBEN mit Pendelachse hinten und Kurbellenkerachse
 * vorn. Das ist kein Detail fuer Liebhaber: Die Gewichtsverteilung von rund
 * 40:60 zugunsten des Hecks und die weiche Federung bestimmen sein
 * Fahrverhalten - Untersteuern beim Anbremsen, Uebersteuern beim Lastwechsel.
 * Genau das kann ein Reifenmodell abbilden und ein handgeschriebenes
 * Giermodell nicht.
 */
UCLASS()
class WIESBADENREAL_API UWiesbadenWheelBase : public UChaosVehicleWheel
{
	GENERATED_BODY()

public:
	UWiesbadenWheelBase();
};

/**
 * Vorderrad: lenkt, bremst, hat keinen Antrieb.
 *
 * Der maximale Lenkeinschlag von 36 Grad entspricht dem Wendekreis des
 * Kaefers von 11 m bei 2,40 m Radstand.
 */
UCLASS()
class WIESBADENREAL_API UWiesbadenWheelFront : public UWiesbadenWheelBase
{
	GENERATED_BODY()

public:
	UWiesbadenWheelFront();
};

/**
 * Hinterrad: angetrieben, bremst, lenkt nicht, haelt die Handbremse.
 */
UCLASS()
class WIESBADENREAL_API UWiesbadenWheelRear : public UWiesbadenWheelBase
{
	GENERATED_BODY()

public:
	UWiesbadenWheelRear();
};
