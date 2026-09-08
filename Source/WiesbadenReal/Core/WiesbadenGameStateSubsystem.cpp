// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "Core/WiesbadenGameStateSubsystem.h"

#include "Core/WiesbadenSaveGame.h"
#include "WiesbadenReal.h"
#include "Kismet/GameplayStatics.h"

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
	UE_LOG(LogWbCore, Log, TEXT("Guthaben gutgeschrieben: %+d (gesamt %d)."), Delta, Guthaben);
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

namespace
{
	const TCHAR* const GSaveSlot = TEXT("WiesbadenReal");
	constexpr int32 GSaveUserIndex = 0;
}

void UWiesbadenGameStateSubsystem::Save() const
{
	UWiesbadenSaveGame* SaveObj = Cast<UWiesbadenSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UWiesbadenSaveGame::StaticClass()));
	if (!SaveObj)
	{
		return;
	}
	SaveObj->Guthaben = Guthaben;
	if (UGameplayStatics::SaveGameToSlot(SaveObj, GSaveSlot, GSaveUserIndex))
	{
		UE_LOG(LogWbCore, Log, TEXT("Guthaben gespeichert: %d."), Guthaben);
	}
	else
	{
		UE_LOG(LogWbCore, Warning, TEXT("Guthaben konnte nicht gespeichert werden."));
	}
}

void UWiesbadenGameStateSubsystem::Load()
{
	if (UGameplayStatics::DoesSaveGameExist(GSaveSlot, GSaveUserIndex))
	{
		if (UWiesbadenSaveGame* SaveObj = Cast<UWiesbadenSaveGame>(
			UGameplayStatics::LoadGameFromSlot(GSaveSlot, GSaveUserIndex)))
		{
			Guthaben = SaveObj->Guthaben;
			UE_LOG(LogWbCore, Log, TEXT("Guthaben geladen: %d."), Guthaben);
			return;
		}
	}
	Guthaben = 0;
	UE_LOG(LogWbCore, Log, TEXT("Kein Guthaben-Speicherstand - starte mit 0."));
}
