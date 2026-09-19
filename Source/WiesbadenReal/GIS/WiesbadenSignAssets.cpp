// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/WiesbadenSignAssets.h"

#include "WiesbadenReal.h"

#include "Engine/Texture2D.h"
#include "HAL/CriticalSection.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ScopeLock.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	// Fehlende Schild-Grafiken werden EINMAL je Id gemeldet: dieselbe Id steht
	// hundertfach in der Stadt, ein Log je Instanz waere unbrauchbar.
	FCriticalSection MissingSignTextureLock;
	TSet<FString> MissingSignTextures;
}

UTexture2D* WiesbadenSignAssets::ResolveTexture(const FString& SignId, const FString& Folder)
{
	const FString Key = NormalizeSignId(SignId);
	if (Key.IsEmpty())
	{
		return nullptr;
	}

	const FString AssetPath = NormalizeFolder(Folder) + BuildTextureName(Key);
	UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *AssetPath);

	if (!Texture)
	{
		bool bFirstReport = false;
		{
			FScopeLock Lock(&MissingSignTextureLock);
			bFirstReport = !MissingSignTextures.Contains(AssetPath);
			MissingSignTextures.Add(AssetPath);
		}
		if (bFirstReport)
		{
			// Das Asset fehlt - die PNG-Quelle daneben genuegt nicht, die Engine
			// laedt nur importierte Texturen. Import: Tools/import_sign_textures.py.
			UE_LOG(LogWbCore, Warning, TEXT("Schild-Textur nicht gefunden: %s"), *AssetPath);
		}
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
