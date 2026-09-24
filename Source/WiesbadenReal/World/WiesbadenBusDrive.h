// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vehicles/WiesbadenVehiclePhysics.h"
#include "World/WiesbadenBusLine.h"

/** Fahrplan-Folger fuer ESWE-Busse. Nutzt dieselbe Antriebs-/Reifenphysik wie der
 * Spielerwagen, mit einem Bus-Antriebsstrang. Die Bogenlaenge wird aus der
 * physikalischen Geschwindigkeit integriert; der Fahrplan bleibt das Ziel. */
namespace WiesbadenBusDrive
{
	struct FState
	{
		FWiesbadenVehiclePhysics Physics;
		double ArcCm = 0.0;
		double LastTimetableArcCm = 0.0;
		float WheelDegrees = 0.0f;
		int64 VehicleId = -1;
		bool bForward = true;
		bool bInitialized = false;
	};

	struct FStep
	{
		WiesbadenBusLine::FBusState Position;
		FWiesbadenVehiclePhysicsOutput Physics;
		double TravelledCm = 0.0;
		float WheelDegrees = 0.0f;
	};

	WIESBADENREAL_API FWiesbadenVehiclePhysics MakeBusPhysics();
	WIESBADENREAL_API FStep Advance(FState& State,
		const WiesbadenBusLine::FBusState& Timetable,
		const WiesbadenBusLine::FBusRoute& Route,
		float CruiseKmh, float DeltaSeconds, int64 VehicleId,
		float SteeringNorm = 0.0f);
	/** Linkes Spenderrad hat seine Aussenseite in -X. Der Rueckseiten-Umbau
	 * dreht die Radrichtung um, damit beide Reifen vorwaerts abrollen. */
	WIESBADENREAL_API FRotator WheelVisualRotation(float ForwardRollDegrees,
		bool bRightSide, float SteeringDegrees = 0.0f);
}
