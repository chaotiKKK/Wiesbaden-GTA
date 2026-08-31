// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/WiesbadenSignAssets.h"

#include "WiesbadenReal.h"

#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UObjectGlobals.h"

UTexture2D* WiesbadenSignAssets::ResolveTexture(const FString& SignId, const FString& Folder)
{
	if (SignId.IsEmpty())
	{
		return nullptr;
	}

	const FString AssetPath = NormalizeFolder(Folder) + BuildTextureName(SignId);
	UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *AssetPath);

	if (!Texture)
	{
		UE_LOG(LogWbCore, Warning, TEXT("Schild-Textur nicht gefunden: %s"), *AssetPath);
	}

	return Texture;
}

UMaterialInstanceDynamic* WiesbadenSignAssets::CreateMaterial(
	const FString& SignId,
	const FString& Folder,
	UMaterialInterface* BaseMaterial,
	FName TextureParameterName,
	UObject* Outer)
{
	UTexture2D* Texture = ResolveTexture(SignId, Folder);
	if (!Texture || !BaseMaterial)
	{
		return nullptr;
	}

	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseMaterial, Outer);
	if (Material)
	{
		Material->SetTextureParameterValue(TextureParameterName, Texture);
	}

	return Material;
}
