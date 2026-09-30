// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "WiesbadenPowertrainSpec.generated.h"

/**
 * Antriebsstrang-Spec: die EINE Quelle der Wahrheit fuer den Antrieb des
 * Kaefer 1302 (1969). Motor-Drehmomentkurve, Getriebe, Achsantrieb, Masse.
 *
 * Zwei Verbraucher (Adapter) teilen sie: das kinematische Modell
 * FWiesbadenVehiclePhysics (Beetle) und der Konstruktor von AWiesbadenChaosCar
 * (Chaos-Fahrzeugkomponente). Genau das macht sie zu einer echten Naht statt
 * einer hypothetischen. BEWUSST nur der Antriebsstrang - Luftwiderstand,
 * Radradius, Bremsen, Lenkung und Tank bleiben fahrzeugspezifisch, weil beide
 * Fahrzeuge sie unterschiedlich darstellen.
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenPowertrainSpec
{
    GENERATED_BODY()

    // -- Motor ------------------------------------------------------------
    /** Maximales Drehmoment (Nm); die Kurve gilt als Vielfaches davon. */
    UPROPERTY(EditAnywhere, Category = "Powertrain")
    float MaxTorqueNm = 102.0f;

    /** Normierte Drehmomentkurve: X = Drehzahl (U/min), Y = Anteil 0..1. */
    UPROPERTY(EditAnywhere, Category = "Powertrain")
    TArray<FVector2D> TorqueCurveNormalized;

    /** Drehzahlgrenze (U/min). */
    UPROPERTY(EditAnywhere, Category = "Powertrain")
    float MaxRpm = 4600.0f;

    /** Leerlaufdrehzahl (U/min). */
    UPROPERTY(EditAnywhere, Category = "Powertrain")
    float IdleRpm = 800.0f;

    // -- Getriebe ---------------------------------------------------------
    /** Vorwaerts-Gangverhaeltnisse (Gang 1 = Index 0). */
    UPROPERTY(EditAnywhere, Category = "Powertrain")
    TArray<float> ForwardGearRatios;

    /** Rueckwaerts-Gangverhaeltnis. */
    UPROPERTY(EditAnywhere, Category = "Powertrain")
    float ReverseGearRatio = 3.61f;

    /** Achsantriebs-Uebersetzung. */
    UPROPERTY(EditAnywhere, Category = "Powertrain")
    float FinalDriveRatio = 4.375f;

    // -- Fahrgestell ------------------------------------------------------
    /** Fahrfertige Masse mit Fahrer (kg). */
    UPROPERTY(EditAnywhere, Category = "Powertrain")
    float MassKg = 970.0f;

    /**
     * Absolutes Motordrehmoment (Nm) bei einer Drehzahl - lineare Interpolation
     * der normierten Kurve, ausserhalb geklemmt (kein Extrapolieren).
     */
    float TorqueNmAt(float Rpm) const;

    /** Der kanonische Antriebsstrang des VW Kaefer 1302 von 1969. */
    static FWiesbadenPowertrainSpec Kaefer1302();
};
