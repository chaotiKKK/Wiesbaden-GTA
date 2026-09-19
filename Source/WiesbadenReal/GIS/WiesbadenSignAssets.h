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

	/**
	 * Bringt eine Zeichen-Id aus den Daten auf die Form, unter der die Grafik
	 * abgelegt ist: trimmt, entfernt den "DE:"-Prefix und eine OSM-Bedingung in
	 * eckigen Klammern ("1042-31[Mo-Sa 08:00-19:00]" -> "1042-31").
	 *
	 * Noetig, weil in den GEBACKENEN Kacheln noch Ids aus aelteren
	 * Parser-Staenden stecken (u. a. " 274.1" mit Leerzeichen hinter "DE:");
	 * die Tafel muss trotzdem ihre Grafik finden, ohne dass neu gebacken wird.
	 */
	inline FString NormalizeSignId(const FString& SignId)
	{
		FString Result = SignId;
		Result.TrimStartAndEndInline();
		if (Result.StartsWith(TEXT("DE:"), ESearchCase::IgnoreCase))
		{
			Result = Result.Mid(3);
			Result.TrimStartAndEndInline();
		}
		else if (Result.Len() > 2 && Result.StartsWith(TEXT("DE"), ESearchCase::IgnoreCase)
			&& FChar::IsDigit(Result[2]))
		{
			Result = Result.Mid(2);
			Result.TrimStartAndEndInline();
		}
		int32 Bracket = INDEX_NONE;
		if (Result.FindChar('[', Bracket))
		{
			Result = Result.Left(Bracket).TrimEnd();
		}

		// Gleiche kanonische Aufloesung wie der OSM-Katalog: die Zahl hinter
		// 1001-30 ist ein variabler Aufdruck, die beiden anderen Formen sind
		// alte/parametrisierte Schreibweisen vorhandener Grafiken.
		if (Result.StartsWith(TEXT("1001-30-")))
		{
			const FString Value = Result.Mid(8);
			if (Value.IsNumeric())
			{
				Result = TEXT("1001-30");
			}
		}
		else if (Result.Equals(TEXT("1036-37"), ESearchCase::IgnoreCase))
		{
			Result = TEXT("1026-37");
		}
		else if (Result.Equals(TEXT("260-30"), ESearchCase::IgnoreCase))
		{
			Result = TEXT("260");
		}

		// Aeltere gebackene Daten koennen den OSM-Werttrenner fuer ein
		// Tempolimit bis in den Asset-Lookup tragen.
		int32 Colon = INDEX_NONE;
		if (Result.FindLastChar(TEXT(':'), Colon))
		{
			const FString Value = Result.Mid(Colon + 1);
			const FString Base = Result.Left(Colon);
			if (Value.IsNumeric() && (Base == TEXT("274") || Base == TEXT("274.1")
				|| Base == TEXT("278") || Base == TEXT("278.1")))
			{
				Result = FString::Printf(TEXT("%s-%s"), *Base.Left(3), *Value);
			}
		}
		return Result;
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
	 * Laedt die Schild-Textur zu einer VzKat-Id (Asset Sign_<Id>.png). Die Id
	 * wird vorher normalisiert (siehe NormalizeSignId).
	 * @return nullptr bei leerer Id oder fehlendem Asset (Warn-Log je Id).
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
