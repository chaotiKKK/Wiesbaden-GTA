// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/WiesbadenTrafficSignCatalog.h"

#include "WiesbadenReal.h"

#include "GIS/WiesbadenConfigPaths.h"
#include "GIS/WiesbadenSignAssets.h"
#include "Dom/JsonObject.h"
#include "HAL/CriticalSection.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	/** Teilt einen Token in Basis-Id ("274") und Zahlenwert (50) auf. */
	void SplitBaseValue(const FString& Token, FString& OutBase, int32& OutValue)
	{
		OutBase = Token;
		OutValue = 0;

		// Form "274[50]".
		int32 Bracket = INDEX_NONE;
		if (OutBase.FindChar('[', Bracket))
		{
			int32 End = INDEX_NONE;
			OutBase.FindChar(']', End);
			const FString ValueStr = (End > Bracket)
				? OutBase.Mid(Bracket + 1, End - Bracket - 1)
				: OutBase.Mid(Bracket + 1);
			OutValue = FCString::Atoi(*ValueStr);
			OutBase = OutBase.Left(Bracket);
			return;
		}

		// Form "274-50".
		int32 Dash = INDEX_NONE;
		if (OutBase.FindLastChar('-', Dash))
		{
			const FString Suffix = OutBase.Mid(Dash + 1);
			if (Suffix.IsNumeric())
			{
				OutValue = FCString::Atoi(*Suffix);
				OutBase = OutBase.Left(Dash);
			}
		}
	}

	/** Kategorie aus dem JSON-String; unbekannt/leer -> Unbekannt. */
	EWiesbadenSignCategory ParseCategory(const FString& Value)
	{
		if (Value == TEXT("Gefahrzeichen")) { return EWiesbadenSignCategory::Gefahrzeichen; }
		if (Value == TEXT("Vorschriftzeichen")) { return EWiesbadenSignCategory::Vorschriftzeichen; }
		if (Value == TEXT("Richtzeichen")) { return EWiesbadenSignCategory::Richtzeichen; }
		if (Value == TEXT("Zusatzzeichen")) { return EWiesbadenSignCategory::Zusatzzeichen; }
		if (Value == TEXT("Verkehrseinrichtung")) { return EWiesbadenSignCategory::Verkehrseinrichtung; }
		return EWiesbadenSignCategory::Unbekannt;
	}

	/** Baut den Katalog aus dem "signs"-Array eines FJsonObject (Datei/String). */
	bool BuildFromJsonArray(
		const TArray<TSharedPtr<FJsonValue>>& Entries,
		TArray<FWiesbadenTrafficSign>& Out,
		FString& OutError)
	{
		Out.Reset();
		TSet<FString> SeenIds;
		SeenIds.Reserve(Entries.Num());

		for (const TSharedPtr<FJsonValue>& Value : Entries)
		{
			const TSharedPtr<FJsonObject>* EntryPtr = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(EntryPtr) || !EntryPtr || !EntryPtr->IsValid())
			{
				OutError = TEXT("Katalog-Eintrag ist kein Objekt.");
				return false;
			}
			const TSharedPtr<FJsonObject>& Entry = *EntryPtr;

			FString Id;
			if (!Entry->TryGetStringField(TEXT("id"), Id) || Id.IsEmpty())
			{
				OutError = TEXT("Katalog-Eintrag ohne 'id'.");
				return false;
			}
			if (SeenIds.Contains(Id))
			{
				OutError = FString::Printf(TEXT("Doppelte Katalog-Id '%s'."), *Id);
				return false;
			}
			SeenIds.Add(Id);

			FWiesbadenTrafficSign Sign;
			Sign.Id = Id;
			Entry->TryGetStringField(TEXT("name"), Sign.Name);
			Sign.Category = ParseCategory(Entry->GetStringField(TEXT("category")));
			Sign.OsmValue = FString(TEXT("DE:")) + Id;

			bool bSpeedLimit = false;
			if (Entry->TryGetBoolField(TEXT("speedLimit"), bSpeedLimit))
			{
				Sign.bSpeedLimit = bSpeedLimit;
			}

			bool bNoTexture = false;
			if (Entry->TryGetBoolField(TEXT("noTexture"), bNoTexture))
			{
				Sign.bNoTexture = bNoTexture;
			}

			const TArray<TSharedPtr<FJsonValue>>* Aliases = nullptr;
			if (Entry->TryGetArrayField(TEXT("aliases"), Aliases))
			{
				for (const TSharedPtr<FJsonValue>& AliasValue : *Aliases)
				{
					FString Alias;
					if (AliasValue.IsValid() && AliasValue->TryGetString(Alias) && !Alias.IsEmpty())
					{
						Sign.Aliases.Add(Alias);
					}
				}
			}

			Out.Add(Sign);
		}

		if (Out.Num() == 0)
		{
			OutError = TEXT("Katalog ist leer.");
			return false;
		}
		return true;
	}

	// -- Geteiltes, mutables Katalog-Registry ---------------------------------
	// Der Katalog wird beim ersten Zugriff geladen, kann aber zur Laufzeit
	// durch Reload/AddSign/RemoveSign geaendert werden. Alle Zugriffe laufen
	// ueber den kritischen Abschnitt; GetCatalog() liefert eine Kopie.
	FCriticalSection CatalogLock;
	TArray<FWiesbadenTrafficSign> GSignCatalog;
	bool bSignCatalogInitialized = false;

	/** Laedt die Standard-JSON bzw. den 274/278-Fallback. */
	TArray<FWiesbadenTrafficSign> LoadDefaultCatalog()
	{
		TArray<FWiesbadenTrafficSign> Signs;
		FString Error;
		if (!FWiesbadenTrafficSignCatalog::LoadFromJsonFile(
				FWiesbadenTrafficSignCatalog::GetDefaultCatalogPath(), Signs, Error))
		{
			UE_LOG(LogWbGIS, Warning,
				TEXT("Verkehrszeichen-Katalog: %s - Fallback nur 274/278."), *Error);

			FWiesbadenTrafficSign Limit;
			Limit.Id = TEXT("274");
			Limit.Name = TEXT("Zulaessige Hoechstgeschwindigkeit");
			Limit.Category = EWiesbadenSignCategory::Vorschriftzeichen;
			Limit.OsmValue = TEXT("DE:274");
			Limit.bSpeedLimit = true;
			Signs.Add(Limit);

			FWiesbadenTrafficSign EndLimit;
			EndLimit.Id = TEXT("278");
			EndLimit.Name = TEXT("Ende der zulaessigen Hoechstgeschwindigkeit");
			EndLimit.Category = EWiesbadenSignCategory::Vorschriftzeichen;
			EndLimit.OsmValue = TEXT("DE:278");
			EndLimit.bSpeedLimit = true;
			Signs.Add(EndLimit);
		}
		return Signs;
	}

	/** Lazy-Init; nur bei gehaltenem CatalogLock aufrufen. */
	void EnsureCatalogInitializedLocked()
	{
		if (!bSignCatalogInitialized)
		{
			GSignCatalog = LoadDefaultCatalog();
			bSignCatalogInitialized = true;
		}
	}

	/** Exakte Id-Suche in einer Katalog-Momentaufnahme. */
	bool FindByIdInCatalog(
		const TArray<FWiesbadenTrafficSign>& Catalog,
		const FString& Id,
		FWiesbadenTrafficSign& Out)
	{
		for (const FWiesbadenTrafficSign& Sign : Catalog)
		{
			if (Sign.Id.Equals(Id, ESearchCase::IgnoreCase))
			{
				Out = Sign;
				return true;
			}
		}
		return false;
	}

	/** Alias-Suche in einer Katalog-Momentaufnahme. */
	bool FindByAliasInCatalog(
		const TArray<FWiesbadenTrafficSign>& Catalog,
		const FString& Id,
		FWiesbadenTrafficSign& Out)
	{
		for (const FWiesbadenTrafficSign& Sign : Catalog)
		{
			for (const FString& Alias : Sign.Aliases)
			{
				if (Alias.Equals(Id, ESearchCase::IgnoreCase))
				{
					Out = Sign;
					return true;
				}
			}
		}
		return false;
	}
}

FString FWiesbadenTrafficSignCatalog::GetDefaultCatalogPath()
{
	return WiesbadenConfigPaths::ConfigFile(TEXT("TrafficSignCatalog.json"));
}

bool FWiesbadenTrafficSignCatalog::LoadFromJsonString(
	const FString& Json,
	TArray<FWiesbadenTrafficSign>& Out,
	FString& OutError)
{
	Out.Reset();
	OutError.Reset();

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("Katalog ist kein gueltiges JSON.");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	if (!Root->TryGetArrayField(TEXT("signs"), Entries))
	{
		OutError = TEXT("Katalog-JSON enthaelt kein 'signs'-Array.");
		return false;
	}

	return BuildFromJsonArray(*Entries, Out, OutError);
}

bool FWiesbadenTrafficSignCatalog::LoadFromJsonFile(
	const FString& Path,
	TArray<FWiesbadenTrafficSign>& Out,
	FString& OutError)
{
	Out.Reset();
	OutError.Reset();

	FString JsonContent;
	if (!FPaths::FileExists(Path) || !FFileHelper::LoadFileToString(JsonContent, *Path))
	{
		OutError = FString::Printf(TEXT("Katalog-Datei nicht gefunden/lesbar: %s"), *Path);
		return false;
	}

	return LoadFromJsonString(JsonContent, Out, OutError);
}

TArray<FWiesbadenTrafficSign> FWiesbadenTrafficSignCatalog::GetCatalog()
{
	// Momentaufnahme (Kopie) unter dem kritischen Abschnitt. Der erste Aufruf
	// kann vom Worker-Thread des Builds kommen (Ausstattungs-Pass); Datei-IO
	// und FJsonSerializer sind thread-sicher.
	FScopeLock Lock(&CatalogLock);
	EnsureCatalogInitializedLocked();
	return GSignCatalog;
}

bool FWiesbadenTrafficSignCatalog::ReloadFromJsonString(const FString& Json, FString& OutError)
{
	TArray<FWiesbadenTrafficSign> Parsed;
	if (!LoadFromJsonString(Json, Parsed, OutError))
	{
		// Bisheriger Katalog bleibt bei fehlerhaftem Input unveraendert.
		return false;
	}

	FScopeLock Lock(&CatalogLock);
	GSignCatalog = MoveTemp(Parsed);
	bSignCatalogInitialized = true;
	return true;
}

bool FWiesbadenTrafficSignCatalog::ReloadFromJsonFile(const FString& Path, FString& OutError)
{
	TArray<FWiesbadenTrafficSign> Parsed;
	if (!LoadFromJsonFile(Path, Parsed, OutError))
	{
		return false;
	}

	FScopeLock Lock(&CatalogLock);
	GSignCatalog = MoveTemp(Parsed);
	bSignCatalogInitialized = true;
	return true;
}

void FWiesbadenTrafficSignCatalog::AddSign(const FWiesbadenTrafficSign& Sign)
{
	if (Sign.Id.IsEmpty())
	{
		return;
	}

	FScopeLock Lock(&CatalogLock);
	EnsureCatalogInitializedLocked();

	for (FWiesbadenTrafficSign& Existing : GSignCatalog)
	{
		if (Existing.Id.Equals(Sign.Id, ESearchCase::IgnoreCase))
		{
			Existing = Sign;
			return;
		}
	}
	GSignCatalog.Add(Sign);
}

bool FWiesbadenTrafficSignCatalog::RemoveSign(const FString& Id)
{
	FScopeLock Lock(&CatalogLock);
	EnsureCatalogInitializedLocked();

	const int32 Removed = GSignCatalog.RemoveAll(
		[&Id](const FWiesbadenTrafficSign& Sign)
		{
			return Sign.Id.Equals(Id, ESearchCase::IgnoreCase);
		});
	return Removed > 0;
}

void FWiesbadenTrafficSignCatalog::ResetCatalog()
{
	FScopeLock Lock(&CatalogLock);
	GSignCatalog = LoadDefaultCatalog();
	bSignCatalogInitialized = true;
}

bool FWiesbadenTrafficSignCatalog::FindById(const FString& Id, FWiesbadenTrafficSign& Out)
{
	const TArray<FWiesbadenTrafficSign> Catalog = GetCatalog();
	return FindByIdInCatalog(Catalog, Id, Out);
}

void FWiesbadenTrafficSignCatalog::ParseOsmTag(const FString& OsmTag, TArray<FWiesbadenTrafficSign>& Out)
{
	Out.Reset();

	// Eine Momentaufnahme fuer den ganzen Parse-Vorgang (konsistent, eine Kopie).
	const TArray<FWiesbadenTrafficSign> Catalog = GetCatalog();

	// Mehrere Zeichen, getrennt durch ";" oder ",".
	TArray<FString> Tokens;
	FString Working = OsmTag;
	Working.ReplaceInline(TEXT(","), TEXT(";"));
	Working.ParseIntoArray(Tokens, TEXT(";"), /*InCullEmpty=*/true);

	for (FString Token : Tokens)
	{
		Token.TrimStartAndEndInline();
		if (Token.IsEmpty())
		{
			continue;
		}

		// "DE:"-Prefix abstreifen (case-insensitive).
		if (Token.StartsWith(TEXT("DE:"), ESearchCase::IgnoreCase))
		{
			Token = Token.Mid(3);
		}

		FWiesbadenTrafficSign Sign;

		// 1) Exakte Katalog-Id (z. B. "103-10", "350-10", "1000-32", "325.1").
		if (FindByIdInCatalog(Catalog, Token, Sign))
		{
			Out.Add(Sign);
			continue;
		}

		// 2) Alias-Formen aus der Katalog-JSON ("325" -> "325.1", "605" -> "620-40").
		if (FindByAliasInCatalog(Catalog, Token, Sign))
		{
			Out.Add(Sign);
			continue;
		}

		// 3) Tempolimit-Syntax "274-50" / "274[30]": Basis + Wert.
		FString Base;
		int32 Value = 0;
		SplitBaseValue(Token, Base, Value);
		if (FindByIdInCatalog(Catalog, Base, Sign) && Sign.bSpeedLimit)
		{
			if (Value > 0)
			{
				Sign.SpeedLimitKmh = Value;
				// Kanonische Form fuer den Textur-Lookup ("274[30]" -> "274-30").
				Sign.Id = FString::Printf(TEXT("%s-%d"), *Base, Value);
			}
			Out.Add(Sign);
			continue;
		}

		// 4) Unbekanntes Zeichen: nicht verlieren, als Unbekannt weiterreichen.
		FWiesbadenTrafficSign Unknown;
		Unknown.Id = Token;
		Unknown.Name = TEXT("Unbekanntes Verkehrszeichen");
		Unknown.Category = EWiesbadenSignCategory::Unbekannt;
		Unknown.OsmValue = FString(TEXT("DE:")) + Token;
		Out.Add(Unknown);
	}
}

int32 FWiesbadenTrafficSignCatalog::ParseSpeedLimitKmh(const FString& OsmTag)
{
	TArray<FWiesbadenTrafficSign> Signs;
	ParseOsmTag(OsmTag, Signs);

	for (const FWiesbadenTrafficSign& Sign : Signs)
	{
		if (Sign.bSpeedLimit && Sign.Id.StartsWith(TEXT("274")))
		{
			return Sign.SpeedLimitKmh;
		}
	}
	return 0;
}

FWiesbadenSignPlacement FWiesbadenTrafficSignCatalog::GetDefaultPlacement()
{
	return FWiesbadenSignPlacement();
}

FString FWiesbadenTrafficSignCatalog::GetDefaultTextureFolder()
{
	return FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Textures"), TEXT("TrafficSigns"));
}

TArray<FString> FWiesbadenTrafficSignCatalog::GetTexturedIds(const TArray<FWiesbadenTrafficSign>& Catalog)
{
	TArray<FString> Ids;
	Ids.Reserve(Catalog.Num());

	for (const FWiesbadenTrafficSign& Sign : Catalog)
	{
		// Tempolimit-Basen (274/278) haben nur die -x-Serie, kein Einzelbild;
		// noTexture-Eintraege (z. B. 600) haben bewusst keine Grafik.
		if (Sign.bSpeedLimit || Sign.bNoTexture)
		{
			continue;
		}
		Ids.Add(Sign.Id);
	}

	return Ids;
}

int32 FWiesbadenTrafficSignCatalog::ValidateTextures(
	const TArray<FWiesbadenTrafficSign>& Catalog,
	const FString& Folder,
	TArray<FString>& OutMissing)
{
	OutMissing.Reset();

	const TArray<FString> Ids = GetTexturedIds(Catalog);
	for (const FString& Id : Ids)
	{
		// UE-Asset-Namen ohne Punkt: 325.1 -> Sign_325-1. Nach dem
		// Editor-Import liegt Sign_<Id>.uasset neben der PNG-Quelle; beides
		// genuegt.
		const FString BaseName = WiesbadenSignAssets::BuildTextureName(Id);
		const FString PngPath = FPaths::Combine(Folder, BaseName + TEXT(".png"));
		const FString UAssetPath = FPaths::Combine(Folder, BaseName + TEXT(".uasset"));

		if (!FPaths::FileExists(PngPath) && !FPaths::FileExists(UAssetPath))
		{
			OutMissing.Add(Id);
		}
	}

	return Ids.Num();
}

void FWiesbadenTrafficSignCatalog::ValidateTexturesAtStartup()
{
	const TArray<FWiesbadenTrafficSign> Catalog = GetCatalog();
	const FString Folder = GetDefaultTextureFolder();

	TArray<FString> Missing;
	const int32 Checked = ValidateTextures(Catalog, Folder, Missing);

	if (Missing.Num() == 0)
	{
		UE_LOG(LogWbGIS, Log,
			TEXT("Verkehrszeichen-Katalog: %d Texturen geprueft, alle vorhanden (%s)."),
			Checked, *Folder);
		return;
	}

	UE_LOG(LogWbGIS, Warning,
		TEXT("Verkehrszeichen-Katalog: %d/%d Texturen fehlen in %s - fehlende Zeichen: %s"),
		Missing.Num(), Checked, *Folder, *FString::Join(Missing, TEXT(", ")));
}
