// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "WiesbadenSaveGame.generated.h"

/**
 * Persistenter Spielzustand auf Platte (Slot "WiesbadenReal", Index 0).
 * v1 nur das Guthaben; SaveVersion erlaubt spaetere, vorwaertskompatible Felder.
 */
UCLASS()
class WIESBADENREAL_API UWiesbadenSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 SaveVersion = 1;

	UPROPERTY()
	int32 Guthaben = 0;
};
