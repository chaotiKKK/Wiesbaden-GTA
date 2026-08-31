// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTexture2D;

/**
 * Gemeinsame Hilfsfunktionen fuer Schild-Assets (Textur-Namen, Ordner,
 * Textur-/Material-Aufloesung).
 *
 * Eine einzige Quelle fuer die Namenskonvention "Sign_<VzKat>.png": der
 * WorldBuilder-Lookup (ResolveSignTexture/ResolveSignMaterial) und der
 * Ausstattungs-Spawner muessen exakt dieselben Asset-Pfade aufloesen.
 */
namespace WiesbadenSignAssets
{
	/**
	 * Baut den Textur-Asset-Namen zu einer Schild-Id.
	 *
	 * Punkte in VzKat-Nummern (z. B. "325.1") sind in UE-Asset-Namen
	 * unzulaessig (der Punkt trennt Package von Objekt) und werden daher zu
	 * Bindestrichen: "325.1" -> "Sign_325-1", "274-50" -> "Sign_274-50".
	 */
	inline FString BuildTextureName(const FString& SignId)
	{
		FString Name = FString(TEXT("Sign_")) + SignId;
		Name.ReplaceInline(TEXT("."), TEXT("-"));
		return Name;
	}

	/** Normalisiert einen Content-Ordner auf einen abschliessenden Slash. */
	inline FString NormalizeFolder(const FString& Folder)
	{
		FString Result = Folder.IsEmpty() ? FString(TEXT("/Game/Textures/TrafficSigns/")) : Folder;
		if (!Result.EndsWith(TEXT("/")))
		{
			Result += TEXT("/");
		}
		return Result;
	}

	/**
	 * Laedt die Schild-Textur zu einer VzKat-Id (Asset Sign_<Id>.png).
	 * @return nullptr bei leerer Id oder fehlendem Asset (Warn-Log).
	 */
	WIESBADENREAL_API UTexture2D* ResolveTexture(const FString& SignId, const FString& Folder);

	/**
	 * Erzeugt ein Material-Instanz-Dynamic fuer eine Schild-Id: Basismaterial
	 * + Textur unter dem angegebenen Parameter.
	 * @return nullptr, wenn Textur oder Basismaterial fehlt.
	 */
	WIESBADENREAL_API UMaterialInstanceDynamic* CreateMaterial(
		const FString& SignId,
		const FString& Folder,
		UMaterialInterface* BaseMaterial,
		FName TextureParameterName,
		UObject* Outer);
}
