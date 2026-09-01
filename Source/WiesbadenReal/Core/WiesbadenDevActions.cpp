// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Core/WiesbadenDevActions.h"

FVector FWiesbadenDevActions::TeleportTargetCm(EWiesbadenDevTeleport Target)
{
	switch (Target)
	{
	case EWiesbadenDevTeleport::PlatterStrasse:  return FVector(-121474.0, -119312.0, 11347.0);
	case EWiesbadenDevTeleport::Nerobergbahn:    return FVector(-104083.0, -137317.0, 8800.0);
	case EWiesbadenDevTeleport::GartenNerotal48: return FVector(-71366.0, -124226.0, 8530.0);
	default:                                     return FVector::ZeroVector;
	}
}

FVector FWiesbadenDevActions::TeleportSpawnCm(EWiesbadenDevTeleport Target)
{
	// 300 cm hoch ansetzen und fallen lassen: ein Punkt IM Boden liesse den
	// Wagen steckenbleiben.
	return TeleportTargetCm(Target) + FVector(0.0, 0.0, 300.0);
}

FTransform FWiesbadenDevActions::UprightTransform(const FTransform& Current)
{
	// Nick/Roll auf 0 (aufrichten), Yaw + Ort behalten, 150 cm anheben und auf
	// die Raeder fallen lassen.
	const FRotator Rot = Current.Rotator();
	return FTransform(
		FRotator(0.0, Rot.Yaw, 0.0),
		Current.GetLocation() + FVector(0.0, 0.0, 150.0),
		FVector::OneVector);
}
