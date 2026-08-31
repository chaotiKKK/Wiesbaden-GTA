// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/WiesbadenTrafficSignLibrary.h"

#include "WiesbadenReal.h"

TArray<FWiesbadenTrafficSign> UWiesbadenTrafficSignLibrary::GetTrafficSignCatalog()
{
	return FWiesbadenTrafficSignCatalog::GetCatalog();
}

bool UWiesbadenTrafficSignLibrary::FindTrafficSignById(const FString& Id, FWiesbadenTrafficSign& OutSign)
{
	return FWiesbadenTrafficSignCatalog::FindById(Id, OutSign);
}

void UWiesbadenTrafficSignLibrary::AddTrafficSign(
	const FString& Id,
	const FString& Name,
	EWiesbadenSignCategory Category,
	const TArray<FString>& Aliases)
{
	FWiesbadenTrafficSign Sign;
	Sign.Id = Id;
	Sign.Name = Name;
	Sign.Category = Category;
	Sign.Aliases = Aliases;
	Sign.OsmValue = FString(TEXT("DE:")) + Id;
	FWiesbadenTrafficSignCatalog::AddSign(Sign);
}

bool UWiesbadenTrafficSignLibrary::RemoveTrafficSign(const FString& Id)
{
	return FWiesbadenTrafficSignCatalog::RemoveSign(Id);
}

bool UWiesbadenTrafficSignLibrary::ReloadTrafficSignCatalogFromJsonString(const FString& Json, FString& OutError)
{
	return FWiesbadenTrafficSignCatalog::ReloadFromJsonString(Json, OutError);
}

bool UWiesbadenTrafficSignLibrary::ReloadTrafficSignCatalog()
{
	FString Error;
	if (FWiesbadenTrafficSignCatalog::ReloadFromJsonFile(
			FWiesbadenTrafficSignCatalog::GetDefaultCatalogPath(), Error))
	{
		return true;
	}

	UE_LOG(LogWbGIS, Warning,
		TEXT("Verkehrszeichen-Katalog-Reload fehlgeschlagen: %s"), *Error);
	return false;
}
