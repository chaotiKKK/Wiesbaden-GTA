// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/OSMTypes.h"

#include "WiesbadenReal.h"

namespace
{
	/** Statische Zuordnung highway=* -> Enum. Einmalig aufgebaut, dann read-only. */
	const TMap<FString, EOSMHighwayType>& GetHighwayTypeMap()
	{
		static const TMap<FString, EOSMHighwayType> Map = {
			{ TEXT("motorway"),			EOSMHighwayType::Motorway },
			{ TEXT("motorway_link"),	EOSMHighwayType::MotorwayLink },
			{ TEXT("trunk"),			EOSMHighwayType::Trunk },
			{ TEXT("trunk_link"),		EOSMHighwayType::TrunkLink },
			{ TEXT("primary"),			EOSMHighwayType::Primary },
			{ TEXT("primary_link"),		EOSMHighwayType::PrimaryLink },
			{ TEXT("secondary"),		EOSMHighwayType::Secondary },
			{ TEXT("secondary_link"),	EOSMHighwayType::SecondaryLink },
			{ TEXT("tertiary"),			EOSMHighwayType::Tertiary },
			{ TEXT("tertiary_link"),	EOSMHighwayType::TertiaryLink },
			{ TEXT("unclassified"),		EOSMHighwayType::Unclassified },
			{ TEXT("residential"),		EOSMHighwayType::Residential },
			{ TEXT("living_street"),	EOSMHighwayType::LivingStreet },
			{ TEXT("service"),			EOSMHighwayType::Service },
			{ TEXT("pedestrian"),		EOSMHighwayType::Pedestrian },
			{ TEXT("footway"),			EOSMHighwayType::Footway },
			{ TEXT("cycleway"),			EOSMHighwayType::Cycleway },
			{ TEXT("path"),				EOSMHighwayType::Path },
			{ TEXT("steps"),			EOSMHighwayType::Steps },
			{ TEXT("track"),			EOSMHighwayType::Track },
			// Haeufige Synonyme und Altbestand in deutschen OSM-Daten:
			{ TEXT("bridleway"),		EOSMHighwayType::Path },
			{ TEXT("corridor"),			EOSMHighwayType::Footway },
			{ TEXT("busway"),			EOSMHighwayType::Service },
		};
		return Map;
	}

	const TMap<FString, EOSMSurfaceType>& GetSurfaceTypeMap()
	{
		static const TMap<FString, EOSMSurfaceType> Map = {
			{ TEXT("asphalt"),			EOSMSurfaceType::Asphalt },
			{ TEXT("paved"),			EOSMSurfaceType::Asphalt },
			{ TEXT("concrete"),			EOSMSurfaceType::Concrete },
			{ TEXT("concrete:plates"),	EOSMSurfaceType::Concrete },
			{ TEXT("concrete:lanes"),	EOSMSurfaceType::Concrete },
			{ TEXT("paving_stones"),	EOSMSurfaceType::PavingStones },
			{ TEXT("sett"),				EOSMSurfaceType::Sett },
			{ TEXT("cobblestone"),		EOSMSurfaceType::Cobblestone },
			{ TEXT("unhewn_cobblestone"), EOSMSurfaceType::Cobblestone },
			{ TEXT("gravel"),			EOSMSurfaceType::Gravel },
			{ TEXT("fine_gravel"),		EOSMSurfaceType::Gravel },
			{ TEXT("compacted"),		EOSMSurfaceType::Compacted },
			{ TEXT("unpaved"),			EOSMSurfaceType::Compacted },
			{ TEXT("ground"),			EOSMSurfaceType::Ground },
			{ TEXT("dirt"),				EOSMSurfaceType::Ground },
			{ TEXT("earth"),			EOSMSurfaceType::Ground },
			{ TEXT("sand"),				EOSMSurfaceType::Ground },
			{ TEXT("grass"),			EOSMSurfaceType::Grass },
			{ TEXT("grass_paver"),		EOSMSurfaceType::Grass },
			{ TEXT("wood"),				EOSMSurfaceType::Wood },
			{ TEXT("metal"),			EOSMSurfaceType::Metal },
		};
		return Map;
	}

	const TMap<FString, EOSMBuildingType>& GetBuildingTypeMap()
	{
		static const TMap<FString, EOSMBuildingType> Map = {
			{ TEXT("residential"),		EOSMBuildingType::Residential },
			{ TEXT("apartments"),		EOSMBuildingType::Apartments },
			{ TEXT("house"),			EOSMBuildingType::House },
			{ TEXT("detached"),			EOSMBuildingType::Detached },
			{ TEXT("semidetached_house"), EOSMBuildingType::House },
			{ TEXT("terrace"),			EOSMBuildingType::Residential },
			{ TEXT("dormitory"),		EOSMBuildingType::Apartments },
			{ TEXT("office"),			EOSMBuildingType::Office },
			{ TEXT("commercial"),		EOSMBuildingType::Commercial },
			{ TEXT("retail"),			EOSMBuildingType::Retail },
			{ TEXT("supermarket"),		EOSMBuildingType::Retail },
			{ TEXT("kiosk"),			EOSMBuildingType::Retail },
			{ TEXT("industrial"),		EOSMBuildingType::Industrial },
			{ TEXT("manufacture"),		EOSMBuildingType::Industrial },
			{ TEXT("factory"),			EOSMBuildingType::Industrial },
			{ TEXT("warehouse"),		EOSMBuildingType::Warehouse },
			{ TEXT("church"),			EOSMBuildingType::Church },
			{ TEXT("chapel"),			EOSMBuildingType::Church },
			{ TEXT("cathedral"),		EOSMBuildingType::Church },
			{ TEXT("synagogue"),		EOSMBuildingType::Church },
			{ TEXT("mosque"),			EOSMBuildingType::Church },
			{ TEXT("civic"),			EOSMBuildingType::Civic },
			{ TEXT("public"),			EOSMBuildingType::Civic },
			{ TEXT("government"),		EOSMBuildingType::Civic },
			{ TEXT("townhall"),			EOSMBuildingType::Civic },
			{ TEXT("school"),			EOSMBuildingType::School },
			{ TEXT("kindergarten"),		EOSMBuildingType::School },
			{ TEXT("college"),			EOSMBuildingType::School },
			{ TEXT("university"),		EOSMBuildingType::University },
			{ TEXT("hospital"),			EOSMBuildingType::Hospital },
			{ TEXT("clinic"),			EOSMBuildingType::Hospital },
			{ TEXT("train_station"),	EOSMBuildingType::TrainStation },
			{ TEXT("transportation"),	EOSMBuildingType::TrainStation },
			{ TEXT("hotel"),			EOSMBuildingType::Hotel },
			{ TEXT("garage"),			EOSMBuildingType::Garage },
			{ TEXT("garages"),			EOSMBuildingType::Garage },
			{ TEXT("carport"),			EOSMBuildingType::Garage },
			{ TEXT("roof"),				EOSMBuildingType::Roof },
			{ TEXT("yes"),				EOSMBuildingType::Generic },
		};
		return Map;
	}
}

EOSMHighwayType FOSMTagParser::ParseHighwayType(const FString& Value)
{
	const FString Normalized = Value.TrimStartAndEnd().ToLower();
	if (const EOSMHighwayType* Found = GetHighwayTypeMap().Find(Normalized))
	{
		return *Found;
	}
	return EOSMHighwayType::None;
}

FString FOSMTagParser::HighwayTypeToString(EOSMHighwayType Type)
{
	for (const TPair<FString, EOSMHighwayType>& Pair : GetHighwayTypeMap())
	{
		if (Pair.Value == Type)
		{
			return Pair.Key;
		}
	}
	return TEXT("none");
}

EOSMSurfaceType FOSMTagParser::ParseSurfaceType(const FString& Value)
{
	const FString Normalized = Value.TrimStartAndEnd().ToLower();
	if (const EOSMSurfaceType* Found = GetSurfaceTypeMap().Find(Normalized))
	{
		return *Found;
	}
	return EOSMSurfaceType::Unknown;
}

EOSMSidewalkType FOSMTagParser::ParseSidewalkType(const FOSMWay& Way)
{
	// Variante 1: sidewalk=both|left|right|no|separate
	const FString Direct = Way.GetTag(TEXT("sidewalk")).TrimStartAndEnd().ToLower();
	if (!Direct.IsEmpty())
	{
		if (Direct == TEXT("both")) { return EOSMSidewalkType::Both; }
		if (Direct == TEXT("left")) { return EOSMSidewalkType::Left; }
		if (Direct == TEXT("right")) { return EOSMSidewalkType::Right; }
		if (Direct == TEXT("separate")) { return EOSMSidewalkType::Separate; }
		if (Direct == TEXT("no") || Direct == TEXT("none")) { return EOSMSidewalkType::None; }
		// "yes" ist streng genommen deprecated, kommt aber vor und bedeutet beidseitig.
		if (Direct == TEXT("yes")) { return EOSMSidewalkType::Both; }
	}

	// Variante 2: sidewalk:left=yes / sidewalk:right=yes (das aktuelle Schema)
	auto SideIsPresent = [&Way](const TCHAR* Key) -> bool
	{
		const FString V = Way.GetTag(Key).TrimStartAndEnd().ToLower();
		return V == TEXT("yes") || V == TEXT("separate") || V == TEXT("sidewalk");
	};

	const bool bLeft = SideIsPresent(TEXT("sidewalk:left")) || SideIsPresent(TEXT("sidewalk:both"));
	const bool bRight = SideIsPresent(TEXT("sidewalk:right")) || SideIsPresent(TEXT("sidewalk:both"));

	if (bLeft && bRight) { return EOSMSidewalkType::Both; }
	if (bLeft) { return EOSMSidewalkType::Left; }
	if (bRight) { return EOSMSidewalkType::Right; }

	return EOSMSidewalkType::None;
}

EOSMOnewayType FOSMTagParser::ParseOneway(const FOSMWay& Way)
{
	// Kreisverkehre in Deutschland sind implizit Einbahnstrassen, auch ohne
	// oneway-Tag. Wird das ignoriert, fahren KI-Fahrzeuge im Kreisverkehr
	// gegeneinander.
	const FString Value = Way.GetTag(TEXT("oneway")).TrimStartAndEnd().ToLower();

	if (Value.IsEmpty())
	{
		return Way.IsRoundabout() ? EOSMOnewayType::Forward : EOSMOnewayType::No;
	}

	if (Value == TEXT("yes") || Value == TEXT("true") || Value == TEXT("1"))
	{
		return EOSMOnewayType::Forward;
	}
	if (Value == TEXT("-1") || Value == TEXT("reverse") || Value == TEXT("backward"))
	{
		return EOSMOnewayType::Backward;
	}
	if (Value == TEXT("reversible") || Value == TEXT("alternating"))
	{
		return EOSMOnewayType::Reversible;
	}
	if (Value == TEXT("no") || Value == TEXT("false") || Value == TEXT("0"))
	{
		return Way.IsRoundabout() ? EOSMOnewayType::Forward : EOSMOnewayType::No;
	}

	UE_LOG(LogWbGIS, Verbose,
		TEXT("Way %lld: unbekannter oneway-Wert '%s', als zweispurig behandelt."),
		Way.Id, *Value);
	return EOSMOnewayType::No;
}

EOSMBuildingType FOSMTagParser::ParseBuildingType(const FString& Value)
{
	const FString Normalized = Value.TrimStartAndEnd().ToLower();
	if (const EOSMBuildingType* Found = GetBuildingTypeMap().Find(Normalized))
	{
		return *Found;
	}
	return EOSMBuildingType::Generic;
}

EOSMRoofShape FOSMTagParser::ParseRoofShape(const FString& Value)
{
	const FString Normalized = Value.TrimStartAndEnd().ToLower();
	if (Normalized == TEXT("gabled")) { return EOSMRoofShape::Gabled; }
	if (Normalized == TEXT("hipped") || Normalized == TEXT("half-hipped")) { return EOSMRoofShape::Hipped; }
	if (Normalized == TEXT("pyramidal")) { return EOSMRoofShape::Pyramidal; }
	if (Normalized == TEXT("skillion") || Normalized == TEXT("shed")) { return EOSMRoofShape::Skillion; }
	if (Normalized == TEXT("dome") || Normalized == TEXT("onion")) { return EOSMRoofShape::Dome; }
	return EOSMRoofShape::Flat;
}

bool FOSMTagParser::ParseLengthMeters(const FString& Value, double& OutMeters)
{
	OutMeters = 0.0;

	FString Work = Value.TrimStartAndEnd();
	if (Work.IsEmpty())
	{
		return false;
	}

	// Deutsches Dezimalkomma normalisieren. Ein Komma mit Ziffern auf beiden
	// Seiten ist in DE-Daten immer ein Dezimaltrenner, kein Listentrenner.
	Work.ReplaceInline(TEXT(","), TEXT("."), ESearchCase::CaseSensitive);

	// Fuss-/Zoll-Notation: 40' oder 40'6"
	if (Work.Contains(TEXT("'")))
	{
		FString FeetPart;
		FString InchPart;
		Work.Split(TEXT("'"), &FeetPart, &InchPart);

		const double Feet = FCString::Atod(*FeetPart.TrimStartAndEnd());
		double Inches = 0.0;

		InchPart.ReplaceInline(TEXT("\""), TEXT(""), ESearchCase::CaseSensitive);
		InchPart = InchPart.TrimStartAndEnd();
		if (!InchPart.IsEmpty())
		{
			Inches = FCString::Atod(*InchPart);
		}

		if (Feet <= 0.0 && Inches <= 0.0)
		{
			return false;
		}

		OutMeters = Feet * 0.3048 + Inches * 0.0254;
		return true;
	}

	const bool bIsFeetSuffix = Work.EndsWith(TEXT("ft"), ESearchCase::IgnoreCase);
	const bool bIsMileSuffix = Work.EndsWith(TEXT("mi"), ESearchCase::IgnoreCase);

	// Numerisches Praefix extrahieren. Alles ab dem ersten Nicht-Zahl-Zeichen
	// wird als Einheitensuffix betrachtet.
	int32 NumEnd = 0;
	while (NumEnd < Work.Len())
	{
		const TCHAR C = Work[NumEnd];
		const bool bIsNumeric = FChar::IsDigit(C) || C == TEXT('.')
			|| (NumEnd == 0 && (C == TEXT('-') || C == TEXT('+')));
		if (!bIsNumeric)
		{
			break;
		}
		++NumEnd;
	}

	if (NumEnd == 0)
	{
		return false;
	}

	const FString NumberPart = Work.Left(NumEnd);
	const double Number = FCString::Atod(*NumberPart);

	if (!FMath::IsFinite(Number) || Number <= 0.0)
	{
		return false;
	}

	if (bIsFeetSuffix)
	{
		OutMeters = Number * 0.3048;
	}
	else if (bIsMileSuffix)
	{
		OutMeters = Number * 1609.344;
	}
	else
	{
		// Ohne Suffix oder mit "m"/"meter": OSM-Default ist Meter.
		OutMeters = Number;
	}

	return true;
}

bool FOSMTagParser::ParseMaxSpeedKmh(const FString& Value, double& OutKmh)
{
	OutKmh = 0.0;

	const FString Work = Value.TrimStartAndEnd().ToLower();
	if (Work.IsEmpty())
	{
		return false;
	}

	// Implizite Limits nach deutscher StVO. Diese Werte sind rechtlich
	// definiert, nicht geschaetzt.
	if (Work == TEXT("de:urban") || Work == TEXT("urban")) { OutKmh = 50.0; return true; }
	if (Work == TEXT("de:rural") || Work == TEXT("rural")) { OutKmh = 100.0; return true; }
	if (Work == TEXT("de:motorway")) { OutKmh = 250.0; return true; }
	if (Work == TEXT("de:living_street") || Work == TEXT("walk")) { OutKmh = 7.0; return true; }
	if (Work == TEXT("de:bicycle_road")) { OutKmh = 30.0; return true; }
	if (Work == TEXT("de:zone30") || Work == TEXT("de:zone:30")) { OutKmh = 30.0; return true; }

	// "none" = unbegrenzt. Fuer die KI wird auf einen fahrbaren Wert gekappt,
	// sonst beschleunigen Verkehrsfahrzeuge unbegrenzt.
	if (Work == TEXT("none") || Work == TEXT("unlimited")) { OutKmh = 250.0; return true; }

	// "signals" und "variable" bedeuten Wechselverkehrszeichen. Ohne
	// Live-Daten wird das Regellimit angenommen.
	if (Work == TEXT("signals") || Work == TEXT("variable")) { OutKmh = 100.0; return true; }

	const bool bIsMph = Work.Contains(TEXT("mph"));
	const bool bIsKnots = Work.Contains(TEXT("knots"));

	int32 NumEnd = 0;
	while (NumEnd < Work.Len() && (FChar::IsDigit(Work[NumEnd]) || Work[NumEnd] == TEXT('.')))
	{
		++NumEnd;
	}

	if (NumEnd == 0)
	{
		UE_LOG(LogWbGIS, Verbose, TEXT("maxspeed '%s' nicht interpretierbar."), *Value);
		return false;
	}

	const double Number = FCString::Atod(*Work.Left(NumEnd));
	if (!FMath::IsFinite(Number) || Number <= 0.0)
	{
		return false;
	}

	if (bIsMph)
	{
		OutKmh = Number * 1.609344;
	}
	else if (bIsKnots)
	{
		OutKmh = Number * 1.852;
	}
	else
	{
		OutKmh = Number;
	}

	// Plausibilitaetsgrenze: Werte oberhalb 300 km/h sind Tippfehler
	// (z. B. "500" statt "50").
	if (OutKmh > 300.0)
	{
		UE_LOG(LogWbGIS, Verbose,
			TEXT("maxspeed '%s' (%.0f km/h) unplausibel, auf 250 km/h begrenzt."), *Value, OutKmh);
		OutKmh = 250.0;
	}

	return true;
}

bool FOSMTagParser::ParseLaneCount(const FString& Value, int32& OutLanes)
{
	OutLanes = 0;

	FString Work = Value.TrimStartAndEnd();
	if (Work.IsEmpty())
	{
		return false;
	}

	// Fehlerhafte Eintraege wie "2;3" oder "1.5" kommen in OSM vor. Bei
	// Semikolon wird der erste Wert genommen, bei Dezimalzahlen abgerundet.
	if (Work.Contains(TEXT(";")))
	{
		FString First;
		Work.Split(TEXT(";"), &First, nullptr);
		Work = First.TrimStartAndEnd();
	}

	const double AsDouble = FCString::Atod(*Work);
	const int32 Lanes = FMath::FloorToInt32(AsDouble);

	// Wiesbaden hat keine Strasse mit mehr als 8 Spuren; 12 ist eine
	// grosszuegige obere Schranke gegen Datenfehler.
	if (Lanes < 1 || Lanes > 12)
	{
		return false;
	}

	OutLanes = Lanes;
	return true;
}

TArray<FString> FOSMTagParser::SplitLaneValues(const FString& Value)
{
	TArray<FString> Result;
	// bCullEmpty = false ist zwingend: bei "left||right" muss die mittlere
	// Spur als leerer Eintrag erhalten bleiben, sonst verschiebt sich die
	// Zuordnung von Spurindex zu Abbiegepfeil.
	Value.ParseIntoArray(Result, TEXT("|"), /*InCullEmpty=*/false);

	for (FString& Entry : Result)
	{
		Entry = Entry.TrimStartAndEnd();
	}

	return Result;
}

bool FOSMTagParser::ParseBuildingLevels(const FOSMWay& Way, int32& OutLevels)
{
	OutLevels = 0;

	FString Value = Way.GetTag(TEXT("building:levels")).TrimStartAndEnd();
	if (Value.IsEmpty())
	{
		Value = Way.GetTag(TEXT("levels")).TrimStartAndEnd();
	}
	if (Value.IsEmpty())
	{
		return false;
	}

	Value.ReplaceInline(TEXT(","), TEXT("."), ESearchCase::CaseSensitive);

	const double AsDouble = FCString::Atod(*Value);
	// Halbgeschosse (2.5) werden aufgerundet, weil das Dachgeschoss visuell
	// als volles Geschoss wirkt.
	const int32 Levels = FMath::CeilToInt32(AsDouble);

	if (Levels < 1 || Levels > 100)
	{
		return false;
	}

	OutLevels = Levels;
	return true;
}

double FOSMTagParser::ResolveBuildingHeightMeters(const FOSMWay& Way, EOSMBuildingType Type, double MetersPerLevel)
{
	// Prioritaet 1: explizites height=*. In Wiesbaden nur bei Landmarken
	// gesetzt, dort aber verlaesslich.
	double ExplicitHeight = 0.0;
	if (ParseLengthMeters(Way.GetTag(TEXT("height")), ExplicitHeight) && ExplicitHeight > 1.0)
	{
		return FMath::Min(ExplicitHeight, 300.0);
	}

	// building:height ist ein verbreitetes Synonym.
	if (ParseLengthMeters(Way.GetTag(TEXT("building:height")), ExplicitHeight) && ExplicitHeight > 1.0)
	{
		return FMath::Min(ExplicitHeight, 300.0);
	}

	// Prioritaet 2: Geschosszahl. Zusaetzlich min_height beruecksichtigen,
	// damit Gebaeudeteile auf Sockeln nicht in den Boden versinken.
	int32 Levels = 0;
	if (ParseBuildingLevels(Way, Levels))
	{
		double MinHeight = 0.0;
		ParseLengthMeters(Way.GetTag(TEXT("min_height")), MinHeight);
		return Levels * MetersPerLevel + MinHeight;
	}

	// Prioritaet 3: typabhaengiger Default. Die Werte entsprechen der
	// tatsaechlichen Bebauung in Wiesbaden: Gruenderzeit-Bloecke in der
	// Innenstadt haben 4-5 Geschosse, Vororte 2.
	switch (Type)
	{
	case EOSMBuildingType::Apartments:		return 5 * MetersPerLevel;
	case EOSMBuildingType::Residential:		return 3 * MetersPerLevel;
	case EOSMBuildingType::House:
	case EOSMBuildingType::Detached:		return 2 * MetersPerLevel;
	case EOSMBuildingType::Office:			return 6 * MetersPerLevel;
	case EOSMBuildingType::Commercial:
	case EOSMBuildingType::Retail:			return 3 * MetersPerLevel;
	case EOSMBuildingType::Industrial:
	case EOSMBuildingType::Warehouse:		return 9.0;
	case EOSMBuildingType::Church:			return 22.0;
	case EOSMBuildingType::Civic:			return 5 * MetersPerLevel;
	case EOSMBuildingType::School:			return 3 * MetersPerLevel;
	case EOSMBuildingType::University:		return 4 * MetersPerLevel;
	case EOSMBuildingType::Hospital:		return 7 * MetersPerLevel;
	case EOSMBuildingType::TrainStation:	return 15.0;
	case EOSMBuildingType::Hotel:			return 5 * MetersPerLevel;
	case EOSMBuildingType::Garage:			return 2.6;
	case EOSMBuildingType::Roof:			return 3.5;
	case EOSMBuildingType::Generic:
	default:								return 3 * MetersPerLevel;
	}
}

double FOSMTagParser::GetSurfaceFriction(EOSMSurfaceType Surface)
{
	// Reibungsmultiplikatoren relativ zu trockenem Asphalt (= 1.0).
	switch (Surface)
	{
	case EOSMSurfaceType::Asphalt:		return 1.00;
	case EOSMSurfaceType::Concrete:		return 0.95;
	case EOSMSurfaceType::PavingStones:	return 0.85;
	case EOSMSurfaceType::Sett:			return 0.78;
	case EOSMSurfaceType::Cobblestone:	return 0.70;
	case EOSMSurfaceType::Compacted:	return 0.72;
	case EOSMSurfaceType::Gravel:		return 0.55;
	case EOSMSurfaceType::Ground:		return 0.60;
	case EOSMSurfaceType::Grass:		return 0.45;
	case EOSMSurfaceType::Wood:			return 0.65;
	case EOSMSurfaceType::Metal:		return 0.60;
	case EOSMSurfaceType::Unknown:
	default:							return 0.90;
	}
}

bool FOSMTagParser::IsDrivable(EOSMHighwayType Type)
{
	switch (Type)
	{
	case EOSMHighwayType::Motorway:
	case EOSMHighwayType::MotorwayLink:
	case EOSMHighwayType::Trunk:
	case EOSMHighwayType::TrunkLink:
	case EOSMHighwayType::Primary:
	case EOSMHighwayType::PrimaryLink:
	case EOSMHighwayType::Secondary:
	case EOSMHighwayType::SecondaryLink:
	case EOSMHighwayType::Tertiary:
	case EOSMHighwayType::TertiaryLink:
	case EOSMHighwayType::Unclassified:
	case EOSMHighwayType::Residential:
	case EOSMHighwayType::LivingStreet:
	case EOSMHighwayType::Service:
		return true;
	default:
		return false;
	}
}

bool FOSMTagParser::IsWalkable(EOSMHighwayType Type)
{
	switch (Type)
	{
	case EOSMHighwayType::Motorway:
	case EOSMHighwayType::MotorwayLink:
	case EOSMHighwayType::Trunk:
	case EOSMHighwayType::TrunkLink:
	case EOSMHighwayType::None:
		return false;
	default:
		// Alle uebrigen Klassen sind in Deutschland fuer Fussgaenger frei
		// (Bundesstrassen haben Gehwege, Wohnstrassen ebenfalls).
		return true;
	}
}
