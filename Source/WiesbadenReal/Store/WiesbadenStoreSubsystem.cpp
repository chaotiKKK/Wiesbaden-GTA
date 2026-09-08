// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "Store/WiesbadenStoreSubsystem.h"

#include "Store/WiesbadenStore.h"
#include "Store/WiesbadenStoreLoader.h"
#include "Core/WiesbadenGameStateSubsystem.h"
#include "WiesbadenReal.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/IConsoleManager.h"
#include "Engine/GameInstance.h"

void UWiesbadenStoreSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadCatalog();
	RegisterConsoleCommands();
}

void UWiesbadenStoreSubsystem::Deinitialize()
{
	for (IConsoleObject* Cmd : ConsoleCommands)
	{
		if (Cmd)
		{
			IConsoleManager::Get().UnregisterConsoleObject(Cmd);
		}
	}
	ConsoleCommands.Reset();
	Super::Deinitialize();
}

void UWiesbadenStoreSubsystem::LoadCatalog()
{
	const FString Path = FPaths::ProjectDir() / TEXT("Data/Store/unlocks.json");
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		UE_LOG(LogWbCore, Warning,
			TEXT("Store: '%s' nicht gefunden - kein Freischaltungs-Katalog."), *Path);
		return;
	}

	FStoreLoadResult Loaded = FWiesbadenStoreLoader::ParseItems(Json);
	for (const FString& Err : Loaded.Errors)
	{
		UE_LOG(LogWbCore, Warning, TEXT("Store-Ladefehler: %s"), *Err);
	}
	Catalog = MoveTemp(Loaded.Items);
	UE_LOG(LogWbCore, Log, TEXT("Freischaltungen geladen: %d."), Catalog.Num());
}

UWiesbadenGameStateSubsystem* UWiesbadenStoreSubsystem::GameState() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UWiesbadenGameStateSubsystem>() : nullptr;
}

const FStoreItem* UWiesbadenStoreSubsystem::FindItem(FName ItemId) const
{
	return Catalog.FindByPredicate(
		[ItemId](const FStoreItem& Item) { return Item.Id == ItemId; });
}

EPurchaseOutcome UWiesbadenStoreSubsystem::TryPurchase(FName ItemId)
{
	const FStoreItem* Item = FindItem(ItemId);
	if (!Item)
	{
		UE_LOG(LogWbCore, Warning, TEXT("Kauf: unbekannte Freischaltung '%s'."), *ItemId.ToString());
		return EPurchaseOutcome::UnknownItem;
	}

	UWiesbadenGameStateSubsystem* GS = GameState();
	if (!GS)
	{
		UE_LOG(LogWbCore, Warning, TEXT("Kauf: kein GameState-Subsystem - abgebrochen."));
		return EPurchaseOutcome::UnknownItem;
	}

	const bool bOwned = GS->HasUnlock(ItemId);
	const FPurchaseEvaluation Eval = FWiesbadenStore::Evaluate(*Item, GS->GetGuthaben(), bOwned);

	switch (Eval.Outcome)
	{
	case EPurchaseOutcome::Success:
		// Reihenfolge: erst abbuchen (bewertet als gedeckt), dann freischalten.
		GS->SpendGuthaben(Item->Kosten);
		GS->GrantUnlock(ItemId);
		UE_LOG(LogWbCore, Log, TEXT("Gekauft: %s fuer %d (Rest %d)."),
			*Item->Title, Item->Kosten, Eval.NewGuthaben);
		OnStoreChanged.Broadcast();
		break;

	case EPurchaseOutcome::AlreadyOwned:
		UE_LOG(LogWbCore, Log, TEXT("Bereits freigeschaltet: %s."), *Item->Title);
		break;

	case EPurchaseOutcome::NotEnoughGuthaben:
		UE_LOG(LogWbCore, Log, TEXT("Guthaben reicht nicht fuer %s (kostet %d, vorhanden %d)."),
			*Item->Title, Item->Kosten, GS->GetGuthaben());
		break;

	default:
		break;
	}

	return Eval.Outcome;
}

void UWiesbadenStoreSubsystem::RegisterConsoleCommands()
{
	IConsoleManager& Console = IConsoleManager::Get();

	// Wb.Store - Katalog auflisten und das HUD-Panel umschalten.
	ConsoleCommands.Add(Console.RegisterConsoleCommand(
		TEXT("Wb.Store"),
		TEXT("Zeigt/versteckt den Freischaltungs-Katalog (Ausgabe-Senke)."),
		FConsoleCommandDelegate::CreateLambda([this]()
		{
			UWiesbadenGameStateSubsystem* GS = GameState();
			const int32 Guthaben = GS ? GS->GetGuthaben() : 0;
			UE_LOG(LogWbCore, Log, TEXT("--- Freischaltungen (Guthaben: %d) ---"), Guthaben);
			for (const FStoreItem& Item : Catalog)
			{
				const bool bOwned = GS && GS->HasUnlock(Item.Id);
				UE_LOG(LogWbCore, Log, TEXT("  %s  [%d EUR]  %s%s"),
					*Item.Id.ToString(), Item.Kosten,
					bOwned ? TEXT("(im Besitz) ") : TEXT(""), *Item.Title);
			}
			TogglePanel();
		}),
		ECVF_Default));

	// Wb.Buy <id> - eine Freischaltung kaufen.
	ConsoleCommands.Add(Console.RegisterConsoleCommand(
		TEXT("Wb.Buy"),
		TEXT("Kauft eine Freischaltung: Wb.Buy <id>"),
		FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogWbCore, Log, TEXT("Verwendung: Wb.Buy <id>  (Ids via Wb.Store)."));
				return;
			}
			TryPurchase(FName(*Args[0]));
		}),
		ECVF_Default));

	// Wb.Guthaben <N> - Entwicklerhilfe: Guthaben gutschreiben, um die Senke zu testen.
	ConsoleCommands.Add(Console.RegisterConsoleCommand(
		TEXT("Wb.Guthaben"),
		TEXT("Entwicklerhilfe: schreibt Guthaben gut. Wb.Guthaben <N>"),
		FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogWbCore, Log, TEXT("Verwendung: Wb.Guthaben <N>."));
				return;
			}
			if (UWiesbadenGameStateSubsystem* GS = GameState())
			{
				GS->AddGuthaben(FCString::Atoi(*Args[0]));
			}
		}),
		ECVF_Default));
}
