// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "WiesbadenGameStateSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnGuthabenChanged);

/**
 * Datenreine Kontostand-Arithmetik - ohne Engine/Save testbar. Das Subsystem
 * ist nur die persistente Huelle drumherum.
 */
namespace WiesbadenEconomy
{
	/** Guthaben + Delta, aber nie unter 0. */
	int32 ApplyDelta(int32 Guthaben, int32 Delta);

	/**
	 * Bucht Kosten nur ab, wenn gedeckt. true + neuer Stand in OutGuthaben,
	 * sonst false und OutGuthaben unveraendert (= altes Guthaben).
	 */
	bool TrySpend(int32 Guthaben, int32 Kosten, int32& OutGuthaben);
}

/**
 * Persistenter Spielzustand (Guthaben) - lebt auf GameInstance-Ebene, ueberlebt
 * also Level-Wechsel, und persistiert per SaveGame ueber Sitzungen. Zentraler
 * Besitzer der Oekonomie; Missionen schreiben hier gut, das HUD liest hier.
 */
UCLASS()
class WIESBADENREAL_API UWiesbadenGameStateSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	int32 GetGuthaben() const { return Guthaben; }

	/** Schreibt Delta gut (nie unter 0), speichert, feuert OnGuthabenChanged. */
	void AddGuthaben(int32 Delta);

	/** Bucht Kosten ab, wenn gedeckt; sonst false und unveraendert. */
	bool SpendGuthaben(int32 Kosten);

	FOnGuthabenChanged OnGuthabenChanged;

private:
	void Save() const;
	void Load();

	int32 Guthaben = 0;
};
