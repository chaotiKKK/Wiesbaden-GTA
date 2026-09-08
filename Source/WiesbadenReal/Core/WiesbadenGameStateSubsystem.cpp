// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "Core/WiesbadenGameStateSubsystem.h"

#include "WiesbadenReal.h"

namespace WiesbadenEconomy
{
	int32 ApplyDelta(int32 Guthaben, int32 Delta)
	{
		return FMath::Max(0, Guthaben + Delta);
	}

	bool TrySpend(int32 Guthaben, int32 Kosten, int32& OutGuthaben)
	{
		if (Kosten >= 0 && Kosten <= Guthaben)
		{
			OutGuthaben = Guthaben - Kosten;
			return true;
		}
		OutGuthaben = Guthaben;
		return false;
	}
}

void UWiesbadenGameStateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Load();
}

void UWiesbadenGameStateSubsystem::AddGuthaben(int32 Delta)
{
	Guthaben = WiesbadenEconomy::ApplyDelta(Guthaben, Delta);
	Save();
	OnGuthabenChanged.Broadcast();
}

bool UWiesbadenGameStateSubsystem::SpendGuthaben(int32 Kosten)
{
	int32 NewGuthaben = Guthaben;
	if (WiesbadenEconomy::TrySpend(Guthaben, Kosten, NewGuthaben))
	{
		Guthaben = NewGuthaben;
		Save();
		OnGuthabenChanged.Broadcast();
		return true;
	}
	return false;
}

void UWiesbadenGameStateSubsystem::Save() const
{
	// Persistenz folgt in Phase 2.
}

void UWiesbadenGameStateSubsystem::Load()
{
	// Persistenz folgt in Phase 2.
}
