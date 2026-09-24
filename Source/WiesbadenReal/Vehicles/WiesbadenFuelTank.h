// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "WiesbadenFuelTank.generated.h"

/**
 * Treibstofftank eines Fahrzeugs - eine Gameplay-RESSOURCE, getrennt von der
 * Fahrdynamik.
 *
 * Warum ein eigener Typ: Fuellstand, Verbrauch und Nachtanken haben mit der
 * Laengs-/Querdynamik nichts zu tun - sie sind der Stoff der Tankstellen-Pickups.
 * Als eigener Besitzer des Fuellstands bleibt die Fahrphysik frei davon, und die
 * Pickup-Seite spricht GENAU diesen Tank an (statt in den Physik-Zustand zu
 * greifen). `FWiesbadenVehiclePhysics` haelt einen Tank und ruft je Tick
 * `Consume(...)`; die Ressource selbst lebt hier.
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenFuelTank
{
	GENERATED_BODY()

	/** Tankgroesse in Litern. */
	UPROPERTY(EditAnywhere, Category = "Vehicle|Treibstoff", meta = (ClampMin = "1.0"))
	float TankCapacityLiters = 42.0f;

	/**
	 * Aktueller Tankinhalt in Litern (Zustand).
	 *
	 * 42 l entsprechen dem Tank eines Kaefer 1300. Bei Verbrauch im
	 * zweistelligen Literbereich auf 100 km reicht der Tank fuer die
	 * halbe Karte - die Tankstellen-Pickups machen ihn zur Ressource.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Treibstoff")
	float FuelLiters = 42.0f;

	/** Grundverbrauch laufender Motor im Leerlauf (Liter je Stunde). */
	UPROPERTY(EditAnywhere, Category = "Vehicle|Treibstoff", meta = (ClampMin = "0.0"))
	float IdleConsumptionLitersPerHour = 1.5f;

	/** Verbrauch je mechanischer Arbeit (Liter je Kilowattstunde). */
	UPROPERTY(EditAnywhere, Category = "Vehicle|Treibstoff", meta = (ClampMin = "0.0"))
	float ConsumptionLitersPerKWh = 0.35f;

	/** True, solange Treibstoff da ist; ein leerer Motor liefert keine Kraft. */
	bool HasFuel() const { return FuelLiters > 0.0f; }

	/** Tankfuellstand 0..1 (fuer HUD). */
	float GetFuelFraction() const
	{
		return TankCapacityLiters > 0.0f ? FMath::Clamp(FuelLiters / TankCapacityLiters, 0.0f, 1.0f) : 0.0f;
	}

	/** Setzt den Tank voll (Ruhezustand). */
	void Reset() { FuelLiters = TankCapacityLiters; }

	/**
	 * Tankt nach.
	 * @return false, wenn der Tank bereits voll war (das Pickup bleibt dann liegen).
	 */
	bool Refuel(float Liters)
	{
		if (FuelLiters >= TankCapacityLiters - 0.01f)
		{
			return false;
		}
		FuelLiters = FMath::Min(FuelLiters + FMath::Max(Liters, 0.0f), TankCapacityLiters);
		return true;
	}

	/**
	 * Verbraucht einen Tick lang: Grundverbrauch des laufenden Motors plus Arbeit
	 * aus der mechanischen Radleistung (P = F * v). Energiegehalt Benzin ~8,9 kWh/l.
	 */
	void Consume(float WheelPowerKw, float DeltaSeconds)
	{
		if (!HasFuel())
		{
			return;
		}
		const float IdleLiters = IdleConsumptionLitersPerHour * (DeltaSeconds / 3600.0f);
		const float DriveLiters = (FMath::Max(WheelPowerKw, 0.0f) * (DeltaSeconds / 3600.0f)) * ConsumptionLitersPerKWh;
		FuelLiters = FMath::Max(0.0f, FuelLiters - IdleLiters - DriveLiters);
	}
};
