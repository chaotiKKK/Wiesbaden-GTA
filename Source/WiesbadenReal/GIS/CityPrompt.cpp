// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/CityPrompt.h"

namespace
{
	/** Normalisiert einen Text fuer den Regel-Abgleich (lowercase, sz/ae/oe/ue). */
	FString NormalizePrompt(const FString& Text)
	{
		FString Normalized = Text.ToLower();
		// Umlaute als \\uXXXX-Escapes, damit die Quelle ASCII bleibt.
		Normalized = Normalized.Replace(TEXT("\u00DF"), TEXT("ss"), ESearchCase::CaseSensitive);
		Normalized = Normalized.Replace(TEXT("\u00E4"), TEXT("ae"), ESearchCase::CaseSensitive);
		Normalized = Normalized.Replace(TEXT("\u00F6"), TEXT("oe"), ESearchCase::CaseSensitive);
		Normalized = Normalized.Replace(TEXT("\u00FC"), TEXT("ue"), ESearchCase::CaseSensitive);
		return Normalized;
	}

	/** Enthalten mit Wortgrenzen (einfache Tokenisierung um Nicht-Buchstaben). */
	bool ContainsWord(const FString& Normalized, const FString& Keyword)
	{
		if (Keyword.IsEmpty())
		{
			return false;
		}
		int32 StartIndex = 0;
		while (StartIndex < Normalized.Len())
		{
			const int32 Found = Normalized.Find(Keyword, ESearchCase::CaseSensitive, ESearchDir::FromStart, StartIndex);
			if (Found == INDEX_NONE)
			{
				return false;
			}
			// Vor und nach dem Treffer muss eine Wortgrenze liegen.
			const bool bBoundaryBefore = (Found == 0) || !FChar::IsAlnum(Normalized[Found - 1]);
			const int32 EndIndex = Found + Keyword.Len();
			const bool bBoundaryAfter = (EndIndex >= Normalized.Len()) || !FChar::IsAlnum(Normalized[EndIndex]);
			if (bBoundaryBefore && bBoundaryAfter)
			{
				return true;
			}
			StartIndex = Found + 1;
		}
		return false;
	}

	/**
	 * Liest die erste Zahl direkt hinter einem Schluesselwort (dezimale
	 * Trenner Punkt ODER Komma). -1.0, wenn keine Zahl folgt.
	 */
	double ParseNumberAfter(const FString& Normalized, const TCHAR* Keyword)
	{
		const int32 Found = Normalized.Find(Keyword, ESearchCase::CaseSensitive);
		if (Found == INDEX_NONE)
		{
			return -1.0;
		}
		int32 P = Found + FCString::Strlen(Keyword);
		while (P < Normalized.Len()
			&& (Normalized[P] == TEXT(' ') || Normalized[P] == TEXT(':') || Normalized[P] == TEXT('=')))
		{
			++P;
		}
		const int32 Start = P;
		if (P < Normalized.Len() && (Normalized[P] == TEXT('-') || Normalized[P] == TEXT('+')))
		{
			++P;
		}
		// Ganzzahliger Teil, dann optional ein Dezimaltrenner (Punkt/Komma),
		// aber NUR wenn eine Ziffer folgt - sonst waere "5..2000" der
		// Span-Trenner und duerfte nicht mitgefressen werden.
		while (P < Normalized.Len() && FChar::IsDigit(Normalized[P]))
		{
			++P;
		}
		if (P + 1 < Normalized.Len()
			&& (Normalized[P] == TEXT('.') || Normalized[P] == TEXT(','))
			&& FChar::IsDigit(Normalized[P + 1]))
		{
			++P;
			while (P < Normalized.Len() && FChar::IsDigit(Normalized[P]))
			{
				++P;
			}
		}
		if (P == Start)
		{
			return -1.0;
		}
		FString Number = Normalized.Mid(Start, P - Start);
		Number = Number.Replace(TEXT(","), TEXT("."), ESearchCase::CaseSensitive);
		return FCString::Atod(*Number);
	}

	/**
	 * Parst ein "min..max"-Paar direkt hinter einem Schluesselwort (Trenner:
	 * "..", ".", "-", "bis", "to", "max[imal]" oder reiner Abstand).
	 */
	bool ParseSpanAfter(const FString& Normalized, const TCHAR* Keyword, double& OutMin, double& OutMax)
	{
		const int32 Found = Normalized.Find(Keyword, ESearchCase::CaseSensitive);
		if (Found == INDEX_NONE)
		{
			return false;
		}
		int32 P = Found + FCString::Strlen(Keyword);

		auto SkipSpace = [&]()
		{
			// Konsistent zu ParseNumberAfter auch ':'/'=' ueberspringen, damit
			// "hoehenspanne: 5..2000" nicht stillschweigend ignoriert wird.
			while (P < Normalized.Len()
				&& (Normalized[P] == TEXT(' ') || Normalized[P] == TEXT(':') || Normalized[P] == TEXT('=')))
			{
				++P;
			}
		};
		auto ReadNumber = [&](double& OutValue)
		{
			SkipSpace();
			const int32 Start = P;
			if (P < Normalized.Len() && (Normalized[P] == TEXT('-') || Normalized[P] == TEXT('+')))
			{
				++P;
			}
			// Ganzzahliger Teil + optionaler Dezimaltrenner nur bei folgender
			// Ziffer (sonst wuerde "5..2000" als eine Zahl verschluckt).
			while (P < Normalized.Len() && FChar::IsDigit(Normalized[P]))
			{
				++P;
			}
			if (P + 1 < Normalized.Len()
				&& (Normalized[P] == TEXT('.') || Normalized[P] == TEXT(','))
				&& FChar::IsDigit(Normalized[P + 1]))
			{
				++P;
				while (P < Normalized.Len() && FChar::IsDigit(Normalized[P]))
				{
					++P;
				}
			}
			if (P == Start)
			{
				return false;
			}
			FString Number = Normalized.Mid(Start, P - Start);
			Number = Number.Replace(TEXT(","), TEXT("."), ESearchCase::CaseSensitive);
			OutValue = FCString::Atod(*Number);
			return true;
		};

		if (!ReadNumber(OutMin))
		{
			return false;
		}

		// Trenner zwischen min und max ueberspringen (falls vorhanden).
		SkipSpace();
		const FString Rest = Normalized.Mid(P);
		if (Rest.StartsWith(TEXT(".."))) { P += 2; }
		else if (Rest.StartsWith(TEXT(".")) || Rest.StartsWith(TEXT("-"))) { ++P; }
		else if (Rest.StartsWith(TEXT("maximal"))) { P += 7; }
		else if (Rest.StartsWith(TEXT("max"))) { P += 3; }
		else if (Rest.StartsWith(TEXT("bis"))) { P += 3; }
		else if (Rest.StartsWith(TEXT("to"))) { P += 2; }

		if (!ReadNumber(OutMax))
		{
			return false;
		}

		// Umgekehrte Bereiche ("hoehenspanne 2000..5") normalisieren: min/max
		// tauschen, damit die Pipeline keine unbrauchbare Schwelle (min > max)
		// bekommt. Nur bei tatsaechlich vertauschten Grenzen - korrekte
		// Reihenfolge bleibt unveraendert.
		if (OutMin > OutMax)
		{
			const double Tmp = OutMin;
			OutMin = OutMax;
			OutMax = Tmp;
		}
		return true;
	}
}

TArray<FString> CityPromptParser::GetFacadeStyleNames()
{
	// Reihenfolge = Materialvarianten 0-5. Zentral gehalten, damit der
	// BuildingGenerator (PromptStyle:<Name>-Keys) dieselben Namen nutzt.
	return {
		TEXT("Gruenderzeit"), TEXT("Backstein"), TEXT("Sandstein"),
		TEXT("Moderne"), TEXT("Industrie"), TEXT("Fachwerk")
	};
}

FCityPromptSpec CityPromptParser::Parse(const FString& Text)
{
	FCityPromptSpec Spec;
	Spec.RawPrompt = Text;

	const FString Normalized = NormalizePrompt(Text);
	if (Normalized.IsEmpty())
	{
		Spec.DisplayName = TEXT("Standard-Stadt");
		return Spec;
	}

	// -- Bebauungsdichte ------------------------------------------------------
	float Density = 0.5f;
	// Flektierte Formen ("dichte", "gruener") matchen wegen der Wortgrenzen
	// nicht den Stamm - daher je Stamm die haeufigsten Formen auflisten.
	if (ContainsWord(Normalized, TEXT("dicht")) || ContainsWord(Normalized, TEXT("dichte"))
		|| ContainsWord(Normalized, TEXT("dichter")) || ContainsWord(Normalized, TEXT("dichtes"))
		|| ContainsWord(Normalized, TEXT("innenstadt")) || ContainsWord(Normalized, TEXT("urban"))
		|| ContainsWord(Normalized, TEXT("zentrum")) || ContainsWord(Normalized, TEXT("dense"))
		|| ContainsWord(Normalized, TEXT("city center")))
	{
		Density = FMath::Max(Density, 0.85f);
	}
	if (ContainsWord(Normalized, TEXT("locker")) || ContainsWord(Normalized, TEXT("lockere"))
		|| ContainsWord(Normalized, TEXT("lockeren")) || ContainsWord(Normalized, TEXT("lockerer"))
		|| ContainsWord(Normalized, TEXT("lockeres")) || ContainsWord(Normalized, TEXT("vorort"))
		|| ContainsWord(Normalized, TEXT("vororte")) || ContainsWord(Normalized, TEXT("gruenflaeche"))
		|| ContainsWord(Normalized, TEXT("sparse")) || ContainsWord(Normalized, TEXT("suburb"))
		|| ContainsWord(Normalized, TEXT("gruen")) || ContainsWord(Normalized, TEXT("gruene"))
		|| ContainsWord(Normalized, TEXT("gruener")) || ContainsWord(Normalized, TEXT("gruenen")))
	{
		Density = FMath::Min(Density, 0.3f);
	}
	Spec.BuildingDensity = Density;

	// -- Fassadenstile (Gewichte je Materialvariante 0-5) --------------------
	// Flektions-/Kompositum-Formen (moderner, Backsteinfassaden, Betonbauten)
	// muessen eigens gefuehrt werden: ContainsWord matcht nur an Wortgrenzen,
	// ein Stamm wie "backstein" matcht "backsteinfassaden" nicht (alnum-
	// Folgezeichen 'f'), und der umlaut-Plural "buerohaeuser" enthaelt
	// "buerohaus" gar nicht. Singular UND Plural sind je eigene Keywords
	// ("fassaden" endet auf alnum 'n').
	struct FFacadeRule
	{
		const TCHAR* Keyword;
		int32 Variant;
	};
	static const FFacadeRule FacadeRules[] = {
		{ TEXT("gruenderzeit"), 0 },  { TEXT("putz"), 0 },        { TEXT("plaster"), 0 },
		{ TEXT("putzfassade"), 0 },   { TEXT("putzfassaden"), 0 },
		{ TEXT("backstein"), 1 },     { TEXT("ziegel"), 1 },      { TEXT("brick"), 1 },
		{ TEXT("backsteinfassade"), 1 }, { TEXT("backsteinfassaden"), 1 },
		{ TEXT("ziegelfassade"), 1 }, { TEXT("ziegelfassaden"), 1 },
		{ TEXT("sandstein"), 2 },     { TEXT("sandstone"), 2 },
		{ TEXT("sandsteinfassade"), 2 }, { TEXT("sandsteinfassaden"), 2 },
		{ TEXT("glas"), 3 },          { TEXT("moderne"), 3 },     { TEXT("buerohaus"), 3 },
		{ TEXT("glass"), 3 },         { TEXT("modern"), 3 },      { TEXT("office"), 3 },
		{ TEXT("moderner"), 3 },      { TEXT("modernen"), 3 },    { TEXT("modernes"), 3 },
		{ TEXT("glasfassade"), 3 },   { TEXT("glasfassaden"), 3 },
		{ TEXT("buerohaeuser"), 3 },
		{ TEXT("beton"), 4 },         { TEXT("industrie"), 4 },   { TEXT("concrete"), 4 },
		{ TEXT("betonbau"), 4 },      { TEXT("betonbauten"), 4 },
		{ TEXT("fachwerk"), 5 },      { TEXT("timber"), 5 },
		{ TEXT("fachwerkfassade"), 5 }, { TEXT("fachwerkfassaden"), 5 },
	};
	for (const FFacadeRule& Rule : FacadeRules)
	{
		if (ContainsWord(Normalized, Rule.Keyword))
		{
			Spec.FacadeVariantWeights.FindOrAdd(Rule.Variant) += 1.0f;
			++Spec.MatchedKeywordCount;
		}
	}

	// -- Landmarken (kanonische Namen) ---------------------------------------
	struct FLandmarkRule
	{
		const TCHAR* Keyword;
		const TCHAR* CanonicalName;
	};
	static const FLandmarkRule LandmarkRules[] = {
		{ TEXT("marktkirche"), TEXT("Marktkirche") },
		{ TEXT("kurhaus"), TEXT("Kurhaus") },
		{ TEXT("neroberg"), TEXT("Neroberg") },
		{ TEXT("stadtschloss"), TEXT("Stadtschloss") },
		{ TEXT("hauptbahnhof"), TEXT("Hauptbahnhof") },
		{ TEXT("rathaus"), TEXT("Rathaus") },
		{ TEXT("schloss biebrich"), TEXT("Schloss Biebrich") },
		{ TEXT("landtag"), TEXT("Hessischer Landtag") },
		{ TEXT("staatstheater"), TEXT("Hessisches Staatstheater") },
		{ TEXT("museum"), TEXT("Museum Wiesbaden") },
	};
	for (const FLandmarkRule& Rule : LandmarkRules)
	{
		if (ContainsWord(Normalized, Rule.Keyword))
		{
			Spec.DetectedLandmarks.AddUnique(Rule.CanonicalName);
			++Spec.MatchedKeywordCount;
		}
	}

	// -- Wetterlage -----------------------------------------------------------
	// Flektionsformen (neblig, schneit, regnerisch, wolkige ...) muessen eigens
	// gefuehrt werden: ContainsWord matcht nur an Wortgrenzen, ein Stamm wie
	// "nebel" matcht "neblig" nicht (alnum-Folgezeichen nach dem Stamm).
	// "Sturm"/"stuermisch" ist Wind+Regen -> Rain (Gewitter bleibt Thunderstorm).
	if (ContainsWord(Normalized, TEXT("gewitter")) || ContainsWord(Normalized, TEXT("thunderstorm")))
	{
		Spec.Weather = ECityWeatherPreset::Thunderstorm;
	}
	else if (ContainsWord(Normalized, TEXT("nebel")) || ContainsWord(Normalized, TEXT("neblig"))
		|| ContainsWord(Normalized, TEXT("neblige")) || ContainsWord(Normalized, TEXT("nebliger"))
		|| ContainsWord(Normalized, TEXT("nebliges")) || ContainsWord(Normalized, TEXT("nebligen"))
		|| ContainsWord(Normalized, TEXT("fog")) || ContainsWord(Normalized, TEXT("foggy")))
	{
		Spec.Weather = ECityWeatherPreset::Fog;
	}
	else if (ContainsWord(Normalized, TEXT("schnee")) || ContainsWord(Normalized, TEXT("schneit"))
		|| ContainsWord(Normalized, TEXT("schneien")) || ContainsWord(Normalized, TEXT("schneefall"))
		|| ContainsWord(Normalized, TEXT("verschneit")) || ContainsWord(Normalized, TEXT("verschneite"))
		|| ContainsWord(Normalized, TEXT("verschneiter")) || ContainsWord(Normalized, TEXT("verschneites"))
		|| ContainsWord(Normalized, TEXT("snow")) || ContainsWord(Normalized, TEXT("snowy"))
		|| ContainsWord(Normalized, TEXT("snowing")))
	{
		Spec.Weather = ECityWeatherPreset::Snow;
	}
	else if (ContainsWord(Normalized, TEXT("regen")) || ContainsWord(Normalized, TEXT("regnerisch"))
		|| ContainsWord(Normalized, TEXT("regnerische")) || ContainsWord(Normalized, TEXT("regnerischer"))
		|| ContainsWord(Normalized, TEXT("regnerisches")) || ContainsWord(Normalized, TEXT("regnet"))
		|| ContainsWord(Normalized, TEXT("regnen")) || ContainsWord(Normalized, TEXT("regenwetter"))
		|| ContainsWord(Normalized, TEXT("rain")) || ContainsWord(Normalized, TEXT("rainy"))
		|| ContainsWord(Normalized, TEXT("raining")) || ContainsWord(Normalized, TEXT("sturm"))
		|| ContainsWord(Normalized, TEXT("stuermisch")) || ContainsWord(Normalized, TEXT("stuermische"))
		|| ContainsWord(Normalized, TEXT("stuermischer")) || ContainsWord(Normalized, TEXT("storm"))
		|| ContainsWord(Normalized, TEXT("stormy")))
	{
		Spec.Weather = ECityWeatherPreset::Rain;
	}
	else if (ContainsWord(Normalized, TEXT("wolkig")) || ContainsWord(Normalized, TEXT("wolkige"))
		|| ContainsWord(Normalized, TEXT("wolkiger")) || ContainsWord(Normalized, TEXT("wolkiges"))
		|| ContainsWord(Normalized, TEXT("bewoelkt")) || ContainsWord(Normalized, TEXT("bewoelkte"))
		|| ContainsWord(Normalized, TEXT("bewoelkter")) || ContainsWord(Normalized, TEXT("bewoelktes"))
		|| ContainsWord(Normalized, TEXT("trueb")) || ContainsWord(Normalized, TEXT("truebe"))
		|| ContainsWord(Normalized, TEXT("trueber")) || ContainsWord(Normalized, TEXT("truebes"))
		|| ContainsWord(Normalized, TEXT("cloudy")))
	{
		Spec.Weather = ECityWeatherPreset::Cloudy;
	}
	else if (ContainsWord(Normalized, TEXT("klar")) || ContainsWord(Normalized, TEXT("sonne"))
		|| ContainsWord(Normalized, TEXT("sonnig")) || ContainsWord(Normalized, TEXT("clear"))
		|| ContainsWord(Normalized, TEXT("sunny")))
	{
		Spec.Weather = ECityWeatherPreset::Clear;
	}

	// -- Tageszeit (spezifischste Lage zuerst; flektierte Formen eigens) -----
	if (ContainsWord(Normalized, TEXT("nacht")) || ContainsWord(Normalized, TEXT("nachts"))
		|| ContainsWord(Normalized, TEXT("night")))
	{
		Spec.TimeOfDayHours = 0.0f;
	}
	// "spaet(er)" vor "abend": sonst wuerde "spaeter Abend" nur den
	// generischen abend-Teil (19) treffen statt die spaetere Stunde (21).
	else if (ContainsWord(Normalized, TEXT("spaet")) || ContainsWord(Normalized, TEXT("spaete"))
		|| ContainsWord(Normalized, TEXT("spaeter")) || ContainsWord(Normalized, TEXT("late")))
	{
		Spec.TimeOfDayHours = 21.0f;
	}
	else if (ContainsWord(Normalized, TEXT("abend")) || ContainsWord(Normalized, TEXT("abends"))
		|| ContainsWord(Normalized, TEXT("evening")))
	{
		Spec.TimeOfDayHours = 19.0f;
	}
	else if (ContainsWord(Normalized, TEXT("nachmittag")) || ContainsWord(Normalized, TEXT("nachmittags"))
		|| ContainsWord(Normalized, TEXT("afternoon")))
	{
		Spec.TimeOfDayHours = 16.0f;
	}
	else if (ContainsWord(Normalized, TEXT("vormittag")) || ContainsWord(Normalized, TEXT("vormittags"))
		|| ContainsWord(Normalized, TEXT("forenoon")))
	{
		Spec.TimeOfDayHours = 10.0f;
	}
	else if (ContainsWord(Normalized, TEXT("mittag")) || ContainsWord(Normalized, TEXT("mittags"))
		|| ContainsWord(Normalized, TEXT("noon")))
	{
		Spec.TimeOfDayHours = 12.0f;
	}
	else if (ContainsWord(Normalized, TEXT("morgen")) || ContainsWord(Normalized, TEXT("morgens"))
		|| ContainsWord(Normalized, TEXT("frueh")) || ContainsWord(Normalized, TEXT("early"))
		|| ContainsWord(Normalized, TEXT("morning")))
	{
		Spec.TimeOfDayHours = 8.0f;
	}

	// -- Verkehrsdichte (spezifischste Formulierung zuerst) -------------------
	if (ContainsWord(Normalized, TEXT("wenig verkehr")) || ContainsWord(Normalized, TEXT("little traffic")))
	{
		Spec.TrafficDensity = 0.25f;
	}
	else if (ContainsWord(Normalized, TEXT("viel verkehr")) || ContainsWord(Normalized, TEXT("heavy traffic")))
	{
		Spec.TrafficDensity = 0.85f;
	}
	else if (ContainsWord(Normalized, TEXT("stau")) || ContainsWord(Normalized, TEXT("staus"))
		|| ContainsWord(Normalized, TEXT("staue")) || ContainsWord(Normalized, TEXT("traffic jam")))
	{
		Spec.TrafficDensity = 0.95f;
	}
	else if (ContainsWord(Normalized, TEXT("belebt")) || ContainsWord(Normalized, TEXT("belebte"))
		|| ContainsWord(Normalized, TEXT("belebten")) || ContainsWord(Normalized, TEXT("belebter"))
		|| ContainsWord(Normalized, TEXT("belebtes")) || ContainsWord(Normalized, TEXT("busy")))
	{
		Spec.TrafficDensity = 0.8f;
	}
	else if (ContainsWord(Normalized, TEXT("ruhig")) || ContainsWord(Normalized, TEXT("ruhige"))
		|| ContainsWord(Normalized, TEXT("ruhigen")) || ContainsWord(Normalized, TEXT("ruhiger"))
		|| ContainsWord(Normalized, TEXT("ruhiges")) || ContainsWord(Normalized, TEXT("quiet")))
	{
		Spec.TrafficDensity = 0.3f;
	}
	else if (ContainsWord(Normalized, TEXT("leer")) || ContainsWord(Normalized, TEXT("leere"))
		|| ContainsWord(Normalized, TEXT("leeren")) || ContainsWord(Normalized, TEXT("leerer"))
		|| ContainsWord(Normalized, TEXT("leeres")) || ContainsWord(Normalized, TEXT("empty")))
	{
		Spec.TrafficDensity = 0.2f;
	}
	else if (ContainsWord(Normalized, TEXT("verkehr")) || ContainsWord(Normalized, TEXT("traffic")))
	{
		Spec.TrafficDensity = 0.7f;
	}

	// -- Gebaeudehoehen (spezifischste Formulierung zuerst) -------------------
	if (ContainsWord(Normalized, TEXT("hochhaus")) || ContainsWord(Normalized, TEXT("hochhaeuser"))
		|| ContainsWord(Normalized, TEXT("hochhaeusern")) || ContainsWord(Normalized, TEXT("wolkenkratzer"))
		|| ContainsWord(Normalized, TEXT("skyscraper")))
	{
		Spec.BuildingHeightScale = 2.0f;
	}
	else if (ContainsWord(Normalized, TEXT("niedrig")) || ContainsWord(Normalized, TEXT("niedrige"))
		|| ContainsWord(Normalized, TEXT("niedrigen")) || ContainsWord(Normalized, TEXT("niedriger"))
		|| ContainsWord(Normalized, TEXT("niedriges")) || ContainsWord(Normalized, TEXT("low")))
	{
		Spec.BuildingHeightScale = 0.6f;
	}
	else if (ContainsWord(Normalized, TEXT("hoch")) || ContainsWord(Normalized, TEXT("hohe"))
		|| ContainsWord(Normalized, TEXT("hohen")) || ContainsWord(Normalized, TEXT("hoher"))
		|| ContainsWord(Normalized, TEXT("hohes")) || ContainsWord(Normalized, TEXT("high")))
	{
		Spec.BuildingHeightScale = 1.4f;
	}
	else if (ContainsWord(Normalized, TEXT("mittel")) || ContainsWord(Normalized, TEXT("medium")))
	{
		Spec.BuildingHeightScale = 1.0f;
	}

	// -- Terrain-Qualitaetsschwellen (numerisch, deutsch + englisch) ----------
	// "tile faktor 3.5" / "tile ratio 2.5" -> Tile darf hoechstens das
	// MaxTileToOsmRatio-fache der OSM-Ausdehnung sein. Ohne Angabe bleibt der
	// Default 2.0 (CheckTerrainQuality-Default).
	{
		const TCHAR* RatioKeywords[] = {
			TEXT("tile faktor"), TEXT("tile-faktor"), TEXT("crop faktor"), TEXT("crop-faktor"),
			TEXT("vergroesserungsfaktor"), TEXT("tile ratio"), TEXT("tile factor")
		};
		for (const TCHAR* Keyword : RatioKeywords)
		{
			const double Ratio = ParseNumberAfter(Normalized, Keyword);
			if (Ratio > 0.0)
			{
				Spec.TerrainMaxTileToOsmRatio = static_cast<float>(Ratio);
				break;
			}
		}
	}

	// "hoehenspanne 5..2000" / "height span 2 to 1500" / "hoehen min 10 max
	// 4000" -> plausible Hoehenspanne in Metern. Ohne Angabe bleiben die
	// CheckTerrainQuality-Defaults (1..3000 m).
	{
		const TCHAR* SpanKeywords[] = {
			TEXT("hoehenspanne"), TEXT("hoehen min"), TEXT("hoehenbereich"),
			TEXT("height span"), TEXT("height range")
		};
		for (const TCHAR* Keyword : SpanKeywords)
		{
			double Min = 0.0;
			double Max = 0.0;
			if (ParseSpanAfter(Normalized, Keyword, Min, Max))
			{
				Spec.TerrainMinHeightSpanMeters = static_cast<float>(Min);
				Spec.TerrainMaxHeightSpanMeters = static_cast<float>(Max);
				break;
			}
		}
	}

	// -- Anzeigename ----------------------------------------------------------
	// Stil: haeufigste Variante benennen.
	const TArray<FString> StyleNames = GetFacadeStyleNames();
	FString StyleName = TEXT("Standard");
	int32 BestVariant = -1;
	float BestWeight = 0.0f;
	for (const TPair<int32, float>& Pair : Spec.FacadeVariantWeights)
	{
		// Bei Gleichstand gewinnt die kleinere Variantennummer - deterministisch,
		// da die TMap-Iterationsreihenfolge (Hash) nicht garantiert ist.
		if (Pair.Value > BestWeight || (Pair.Value == BestWeight && Pair.Key < BestVariant))
		{
			BestWeight = Pair.Value;
			BestVariant = Pair.Key;
		}
	}
	if (BestVariant >= 0 && BestVariant < StyleNames.Num())
	{
		StyleName = StyleNames[BestVariant];
	}

	const TCHAR* DensityName = (Density >= 0.7f) ? TEXT("Innenstadt") : ((Density <= 0.3f) ? TEXT("Vorort") : TEXT("Stadt"));
	Spec.DisplayName = FString::Printf(TEXT("%s-%s"), *StyleName, DensityName);

	return Spec;
}
