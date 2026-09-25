// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vehicles/WiesbadenVehiclePhysics.h"

/**
 * Ein Fahrzeugtyp des Stadtverkehrs: Tripo-Modell (Karosserie + vier Raeder
 * unter /Game/Vehicles/Traffic/<Name>) und die Physik seines Vorbilds.
 *
 * Die Masse kommen aus der Vermessung beim Bau (Tools/Blender/build_traffic_cars.py
 * -> Data/Raw/Verkehr/<Name>/masse.json); Test Vehicles.TrafficCars.MeshDims
 * prueft sie gegen die importierten Meshes. Der Ursprung liegt in der
 * Radstandmitte am Boden, +X vorn, +Y rechts (Unreal).
 */
struct WIESBADENREAL_API FWbTrafficCarType
{
	/** Ordner- und Asset-Name (/Game/Vehicles/Traffic/<Name>/Meshes/SM_<Name>_Body). */
	const TCHAR* Name = TEXT("");
	/** Vorbild (Anzeige, Diagnose). */
	const TCHAR* Model = TEXT("");
	/** Anteil am Verkehr (relativ). */
	float Weight = 1.0f;

	// -- Masse (cm) -------------------------------------------------------
	double FrontCm = 200.0;        // Stossstange vorn ab Ursprung (+X)
	double RearCm = 200.0;         // Stossstange hinten ab Ursprung (-X), positiv
	double BodyWidthCm = 170.0;    // Karosserie ohne Spiegel (Vorbild)
	double WheelbaseCm = 250.0;
	double TrackCm = 145.0;        // Spurweite vorn
	double WheelRadiusCm = 30.0;
	/** Linke Lampen (Unreal: links = -Y); die rechten spiegeln sich an Y. */
	FVector HeadLampCm = FVector::ZeroVector;
	FVector TailLampCm = FVector::ZeroVector;

	// -- Physik des Vorbilds ---------------------------------------------
	FWiesbadenPowertrainSpec Powertrain;
	bool bFrontWheelDrive = true;
	float DragCoeffAreaM2 = 0.65f;
	float FrontWeightFraction = 0.6f;
	float CgHeightM = 0.5f;
	float YawInertiaKgM2 = 1600.0f;
	float CorneringStiffnessNPerRad = 70000.0f;
	float MaxSteerAngleDeg = 33.0f;
	/** Gelassenes Stadtfahren: frueh hoch-, spaet runterschalten. */
	float ShiftUpRpm = 3000.0f;
	float ShiftDownRpm = 1400.0f;

	double HalfLengthCm() const { return 0.5 * (FrontCm + RearCm); }

	/** Einsatzbereite Physik (Player-Modell FWiesbadenVehiclePhysics) mit den
	 *  Werten dieses Typs, voller Tank, Stillstand im ersten Gang. */
	FWiesbadenVehiclePhysics MakePhysics() const;
};

/** Was der Fahrer eines Verkehrsautos sieht - aus Simulation (Soll) und Physik (Ist). */
struct WIESBADENREAL_API FWbTrafficDriverView
{
	/** Karosserie (Ist): Ort, Gierwinkel (rad, Unreal: +Y = rechts herum), Tempo. */
	FVector2D BodyXY = FVector2D::ZeroVector;
	double BodyYawRad = 0.0;
	double BodySpeedCmS = 0.0;
	/** Zielpunkt der reinen Verfolgung weiter vorn auf der Bahn. */
	FVector2D PursuitTargetXY = FVector2D::ZeroVector;
	/** Sollposition auf der Bahn und Solltempo der Simulation (jetzt und im Vortick). */
	FVector2D SollXY = FVector2D::ZeroVector;
	double SollSpeedCmS = 0.0;
	double PrevSollSpeedCmS = 0.0;
	double Dt = 1.0 / 60.0;
	/** Groesster Radeinschlag bei diesem Tempo (rad) und Radstand (cm). */
	double UsableSteerRad = 0.5;
	double WheelbaseCm = 250.0;
	/** Verzoegerung bei voller Bremse, Beschleunigung bei Vollgas (cm/s^2, Schaetzung). */
	double FullBrakeCmS2 = 700.0;
	double FullThrottleCmS2 = 250.0;
};

/**
 * Der Stadtverkehr faehrt mit DERSELBEN Fahrphysik wie das Spielerauto
 * (FWiesbadenVehiclePhysics: Antriebsstrang mit Gaengen, Reifen-Seitenkraefte,
 * Radlastverlagerung, Reibungskreis) - nur sitzt statt des Spielers ein
 * Fahrer am Steuer: Er folgt der Sollbahn der Verkehrs-Simulation (Abstand,
 * Ampeln, Vorfahrt entscheidet weiter der Spur-Graph) mit Lenkrad, Gas und
 * Bremse.
 */
namespace WiesbadenTrafficCars
{
	/** Die Verkehrsfahrzeuge: 0 Golf, 1 Peugeot, 2 Transporter. */
	WIESBADENREAL_API const TArray<FWbTrafficCarType>& Types();

	/** Typ eines Fahrzeugs aus seiner Id - deterministisch, nach Weight gewichtet. */
	WIESBADENREAL_API int32 SelectType(int32 VehicleId);

	/** Radnamen in Mesh-Reihenfolge: FL, FR, RL, RR. */
	WIESBADENREAL_API const TCHAR* WheelName(int32 Wheel);
	WIESBADENREAL_API bool IsFrontWheel(int32 Wheel);

	/** Objektpfade fuer LoadObject. */
	WIESBADENREAL_API FString BodyMeshPath(const FWbTrafficCarType& Type);
	WIESBADENREAL_API FString WheelMeshPath(const FWbTrafficCarType& Type, int32 Wheel);

	/**
	 * Der Fahrer: Lenkrad aus reiner Verfolgung des Zielpunkts, Gas/Bremse aus
	 * Solltempo plus Abstand zur Sollposition (hinkt er hinterher, gibt er Gas;
	 * ist er voraus, bremst er). Datenrein (Test Vehicles.TrafficCars.Driver).
	 */
	WIESBADENREAL_API FWiesbadenVehiclePhysicsInput ComputeDriverInput(const FWbTrafficDriverView& View);

	/**
	 * Transform eines Rades in Fahrzeugkoordinaten: um die Radmitte (Mitte der
	 * Rad-Bounds) gedreht (Rollen um Y) und - vorn - eingeschlagen (um Z).
	 * Der Rad-Mesh-Ursprung ist der Fahrzeugursprung. Datenrein (Test
	 * Vehicles.TrafficCars.WheelTransform).
	 */
	WIESBADENREAL_API FTransform ComputeWheelTransform(const FVector& WheelCenterCm, double SpinRad, double SteerRad);
}
