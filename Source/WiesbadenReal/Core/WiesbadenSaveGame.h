// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Missions/WiesbadenCourierStats.h"
#include "WiesbadenSaveGame.generated.h"

/**
 * Persistenter Spielzustand auf Platte (Slot "WiesbadenReal", Index 0).
 * v1: nur Guthaben. v2: zusaetzlich gekaufte Freischaltungen. v3: Kurier-Bilanz
 * (Dennos Lieferungen). SaveVersion erlaubt vorwaertskompatible Felder - ein
 * aelterer Stand laedt ohne die neuen (Freischaltungen leer, Bilanz bei null).
 */
UCLASS()
class WIESBADENREAL_API UWiesbadenSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 SaveVersion = 3;

	UPROPERTY()
	int32 Guthaben = 0;

	/** Ids der gekauften Freischaltungen (Ausgabe-Senke, TP2 Stueck 3). */
	UPROPERTY()
	TArray<FName> OwnedUnlocks;

	/** Kurier-Bilanz der Denno-Lieferungen (v3). */
	UPROPERTY()
	FWbCourierStats CourierStats;
};
