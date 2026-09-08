// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "Store/WiesbadenStoreLoader.h"

#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

FStoreLoadResult FWiesbadenStoreLoader::ParseItems(const FString& Json)
{
	FStoreLoadResult Result;

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		Result.Errors.Add(TEXT("Store-JSON konnte nicht geparst werden."));
		return Result;
	}

	const TArray<TSharedPtr<FJsonValue>>* ItemsArr = nullptr;
	if (!Root->TryGetArrayField(TEXT("unlocks"), ItemsArr) || !ItemsArr)
	{
		Result.Errors.Add(TEXT("Kein 'unlocks'-Array im JSON."));
		return Result;
	}

	Result.bParsed = true;

	for (const TSharedPtr<FJsonValue>& ItemVal : *ItemsArr)
	{
		const TSharedPtr<FJsonObject>* ItemObj = nullptr;
		if (!ItemVal.IsValid() || !ItemVal->TryGetObject(ItemObj) || !ItemObj)
		{
			Result.Errors.Add(TEXT("Store-Eintrag ist kein Objekt - uebersprungen."));
			continue;
		}

		FString IdStr;
		if (!(*ItemObj)->TryGetStringField(TEXT("id"), IdStr) || IdStr.IsEmpty())
		{
			Result.Errors.Add(TEXT("Store-Eintrag ohne 'id' - uebersprungen."));
			continue;
		}

		FStoreItem Item;
		Item.Id = FName(*IdStr);
		(*ItemObj)->TryGetStringField(TEXT("title"), Item.Title);
		(*ItemObj)->TryGetStringField(TEXT("beschreibung"), Item.Beschreibung);

		double Kosten = 0.0;
		(*ItemObj)->TryGetNumberField(TEXT("kosten"), Kosten);
		Item.Kosten = static_cast<int32>(Kosten);

		Result.Items.Add(Item);
	}

	return Result;
}
