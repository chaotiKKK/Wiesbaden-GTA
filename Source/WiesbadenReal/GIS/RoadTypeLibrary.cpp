// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/RoadTypeLibrary.h"

#include "WiesbadenReal.h"

#include "GIS/RoadNetworkGenerator.h"   // ParseTurnIndication (static, reiner Tag-Parser)
#include "GIS/WiesbadenConfigPaths.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FString URoadTypeLibrary::GetDefaultConfigPath()
{
	return WiesbadenConfigPaths::ConfigFile(TEXT("WiesbadenRoadTypes.json"));
}

void URoadTypeLibrary::ApplyBuiltInDefaults()
{
	Definitions.Reset();

	// Hilfslambda, damit die Tabelle unten lesbar bleibt.
	auto Add = [this](EOSMHighwayType Type, double LaneWidth, int32 LanesPerDir, double Speed,
		double SidewalkWidth, bool bSidewalk, bool bCenterLine, int32 Priority,
		double Density, bool bTraffic)
	{
		FRoadTypeDefinition Def;
		Def.LaneWidthMeters = LaneWidth;
		Def.DefaultLanesPerDirection = LanesPerDir;
		Def.DefaultMaxSpeedKmh = Speed;
		Def.SidewalkWidthMeters = SidewalkWidth;
		Def.bSidewalkByDefault = bSidewalk;
		Def.bHasCenterLineMarking = bCenterLine;
		Def.Priority = Priority;
		Def.TrafficDensityFactor = Density;
		Def.bTrafficEnabled = bTraffic;
		Definitions.Add(Type, Def);
	};

	//   Typ                              Spur  Spuren Tempo Gehweg Gehw. Mittell. Prio Dichte Verkehr
	Add(EOSMHighwayType::Motorway,        3.75,  2,   130.0,  0.0, false, true,     0,  1.00, true);
	Add(EOSMHighwayType::MotorwayLink,    3.75,  1,    80.0,  0.0, false, false,    1,  0.70, true);
	Add(EOSMHighwayType::Trunk,           3.50,  2,   100.0,  0.0, false, true,     1,  0.90, true);
	Add(EOSMHighwayType::TrunkLink,       3.50,  1,    60.0,  0.0, false, false,    2,  0.60, true);
	// Rheinstrasse (B263) und Mainzer Strasse fallen unter primary: breite
	// Fahrspuren, beidseitige Gehwege, 50 km/h innerorts.
	Add(EOSMHighwayType::Primary,         3.50,  2,    50.0,  3.0, true,  true,     2,  0.95, true);
	Add(EOSMHighwayType::PrimaryLink,     3.50,  1,    50.0,  2.5, true,  false,    3,  0.50, true);
	Add(EOSMHighwayType::Secondary,       3.25,  1,    50.0,  2.5, true,  true,     3,  0.75, true);
	Add(EOSMHighwayType::SecondaryLink,   3.25,  1,    50.0,  2.5, true,  false,    4,  0.40, true);
	Add(EOSMHighwayType::Tertiary,        3.25,  1,    50.0,  2.5, true,  true,     4,  0.60, true);
	Add(EOSMHighwayType::TertiaryLink,    3.25,  1,    50.0,  2.5, true,  false,    5,  0.35, true);
	Add(EOSMHighwayType::Unclassified,    3.00,  1,    50.0,  2.0, true,  false,    6,  0.35, true);
	Add(EOSMHighwayType::Residential,     2.75,  1,    30.0,  2.0, true,  false,    7,  0.30, true);
	Add(EOSMHighwayType::LivingStreet,    2.50,  1,     7.0,  0.0, false, false,    8,  0.15, true);
	Add(EOSMHighwayType::Service,         2.50,  1,    20.0,  0.0, false, false,    9,  0.10, true);
	Add(EOSMHighwayType::Pedestrian,      3.00,  1,     7.0,  0.0, false, false,   10,  0.00, false);
	Add(EOSMHighwayType::Footway,         1.80,  1,     0.0,  0.0, false, false,   11,  0.00, false);
	Add(EOSMHighwayType::Cycleway,        1.60,  1,    20.0,  0.0, false, false,   11,  0.00, false);
	Add(EOSMHighwayType::Path,            1.50,  1,     0.0,  0.0, false, false,   12,  0.00, false);
	Add(EOSMHighwayType::Steps,           1.50,  1,     0.0,  0.0, false, false,   12,  0.00, false);
	Add(EOSMHighwayType::Track,           2.50,  1,    20.0,  0.0, false, false,   12,  0.05, false);

	// Fussgaengerzonen sind gepflastert - die Wiesbadener Innenstadt
	// (Langgasse, Kirchgasse, Mauritiusplatz) ist durchgaengig
	// Pflasterstein, nicht Asphalt.
	if (FRoadTypeDefinition* Def = Definitions.Find(EOSMHighwayType::Pedestrian))
	{
		Def->DefaultSurface = EOSMSurfaceType::PavingStones;
	}
	if (FRoadTypeDefinition* Def = Definitions.Find(EOSMHighwayType::Footway))
	{
		Def->DefaultSurface = EOSMSurfaceType::PavingStones;
	}
	if (FRoadTypeDefinition* Def = Definitions.Find(EOSMHighwayType::Track))
	{
		Def->DefaultSurface = EOSMSurfaceType::Compacted;
	}
	if (FRoadTypeDefinition* Def = Definitions.Find(EOSMHighwayType::Path))
	{
		Def->DefaultSurface = EOSMSurfaceType::Ground;
	}

	// Randlinie (Fahrbahnbegrenzung) nur auf klassifizierten Durchgangsstrassen -
	// Wohn-/Erschliessungsstrassen tragen in Deutschland keine durchgezogene
	// Randmarkierung.
	for (const EOSMHighwayType EdgeType : {
		EOSMHighwayType::Motorway, EOSMHighwayType::MotorwayLink,
		EOSMHighwayType::Trunk, EOSMHighwayType::TrunkLink,
		EOSMHighwayType::Primary, EOSMHighwayType::Secondary, EOSMHighwayType::Tertiary })
	{
		if (FRoadTypeDefinition* Def = Definitions.Find(EdgeType))
		{
			Def->bHasEdgeLineMarking = true;
		}
	}

	FallbackDefinition = Definitions.FindRef(EOSMHighwayType::Residential);

	UE_LOG(LogWbRoads, Log, TEXT("Strassentyp-Defaults gesetzt (%d Klassen)."), Definitions.Num());
}

bool URoadTypeLibrary::LoadFromJsonFile(const FString& FilePath)
{
	// Immer zuerst die Defaults setzen. Die JSON-Datei ueberschreibt danach nur
	// die Felder, die sie tatsaechlich enthaelt - dadurch bleibt eine
	// unvollstaendige Konfiguration benutzbar.
	ApplyBuiltInDefaults();

	if (!FPaths::FileExists(FilePath))
	{
		UE_LOG(LogWbRoads, Warning,
			TEXT("Strassentyp-Konfiguration nicht gefunden: %s. Es gelten die Code-Defaults."),
			*FilePath);
		return false;
	}

	FString JsonContent;
	if (!FFileHelper::LoadFileToString(JsonContent, *FilePath))
	{
		UE_LOG(LogWbRoads, Warning, TEXT("Strassentyp-Konfiguration nicht lesbar: %s."), *FilePath);
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(JsonContent);

	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogWbRoads, Error,
			TEXT("Strassentyp-Konfiguration ist kein gueltiges JSON: %s. Es gelten die Code-Defaults."),
			*FilePath);
		return false;
	}

	const TSharedPtr<FJsonObject>* TypesObject = nullptr;
	if (!Root->TryGetObjectField(TEXT("roadTypes"), TypesObject) || !TypesObject || !TypesObject->IsValid())
	{
		UE_LOG(LogWbRoads, Error, TEXT("Strassentyp-Konfiguration enthaelt kein 'roadTypes'-Objekt."));
		return false;
	}

	int32 OverriddenCount = 0;

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*TypesObject)->Values)
	{
		const EOSMHighwayType Type = FOSMTagParser::ParseHighwayType(Pair.Key);
		if (Type == EOSMHighwayType::None)
		{
			UE_LOG(LogWbRoads, Warning,
				TEXT("Unbekannter Strassentyp '%s' in der Konfiguration - uebersprungen."), *Pair.Key);
			continue;
		}

		const TSharedPtr<FJsonObject>* EntryPtr = nullptr;
		if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(EntryPtr) || !EntryPtr || !EntryPtr->IsValid())
		{
			continue;
		}

		const TSharedPtr<FJsonObject>& Entry = *EntryPtr;

		FRoadTypeDefinition& Def = Definitions.FindOrAdd(Type);

		double NumberValue = 0.0;
		bool BoolValue = false;
		FString StringValue;

		// Jedes Feld einzeln pruefen: TryGetNumberField laesst den Zielwert
		// unangetastet, wenn das Feld fehlt. So bleiben Defaults erhalten.
		if (Entry->TryGetNumberField(TEXT("laneWidthMeters"), NumberValue) && NumberValue > 0.0)
		{
			Def.LaneWidthMeters = NumberValue;
		}
		if (Entry->TryGetNumberField(TEXT("defaultLanesPerDirection"), NumberValue) && NumberValue >= 1.0)
		{
			Def.DefaultLanesPerDirection = FMath::Clamp(FMath::RoundToInt32(NumberValue), 1, 6);
		}
		if (Entry->TryGetNumberField(TEXT("defaultMaxSpeedKmh"), NumberValue) && NumberValue >= 0.0)
		{
			Def.DefaultMaxSpeedKmh = NumberValue;
		}
		if (Entry->TryGetNumberField(TEXT("sidewalkWidthMeters"), NumberValue) && NumberValue >= 0.0)
		{
			Def.SidewalkWidthMeters = NumberValue;
		}
		if (Entry->TryGetNumberField(TEXT("kerbHeightMeters"), NumberValue) && NumberValue >= 0.0)
		{
			Def.KerbHeightMeters = NumberValue;
		}
		if (Entry->TryGetNumberField(TEXT("cyclewayWidthMeters"), NumberValue) && NumberValue >= 0.0)
		{
			Def.CyclewayWidthMeters = NumberValue;
		}
		if (Entry->TryGetNumberField(TEXT("priority"), NumberValue))
		{
			Def.Priority = FMath::RoundToInt32(NumberValue);
		}
		if (Entry->TryGetNumberField(TEXT("trafficDensityFactor"), NumberValue))
		{
			Def.TrafficDensityFactor = FMath::Clamp(NumberValue, 0.0, 1.0);
		}
		if (Entry->TryGetBoolField(TEXT("sidewalkByDefault"), BoolValue))
		{
			Def.bSidewalkByDefault = BoolValue;
		}
		if (Entry->TryGetBoolField(TEXT("hasCenterLineMarking"), BoolValue))
		{
			Def.bHasCenterLineMarking = BoolValue;
		}
		if (Entry->TryGetBoolField(TEXT("hasEdgeLineMarking"), BoolValue))
		{
			Def.bHasEdgeLineMarking = BoolValue;
		}
		if (Entry->TryGetBoolField(TEXT("trafficEnabled"), BoolValue))
		{
			Def.bTrafficEnabled = BoolValue;
		}
		if (Entry->TryGetStringField(TEXT("roadMaterialPath"), StringValue))
		{
			Def.RoadMaterialPath = StringValue;
		}
		if (Entry->TryGetStringField(TEXT("sidewalkMaterialPath"), StringValue))
		{
			Def.SidewalkMaterialPath = StringValue;
		}
		if (Entry->TryGetStringField(TEXT("defaultSurface"), StringValue))
		{
			Def.DefaultSurface = FOSMTagParser::ParseSurfaceType(StringValue);
		}

		++OverriddenCount;
	}

	FallbackDefinition = Definitions.FindRef(EOSMHighwayType::Residential);

	UE_LOG(LogWbRoads, Log,
		TEXT("Strassentyp-Konfiguration geladen: %d Klassen aus %s ueberschrieben."),
		OverriddenCount, *FilePath);

	return true;
}

const FRoadTypeDefinition& URoadTypeLibrary::GetDefinition(EOSMHighwayType Type) const
{
	if (const FRoadTypeDefinition* Found = Definitions.Find(Type))
	{
		return *Found;
	}
	return FallbackDefinition;
}

void URoadTypeLibrary::ResolveLaneCounts(
	const FOSMWay& Way,
	EOSMHighwayType Type,
	EOSMOnewayType Oneway,
	int32& OutForwardLanes,
	int32& OutBackwardLanes) const
{
	// Wege ohne Fahrtrichtungen: Bei Fuss-, Rad- und Wirtschaftswegen ist die
	// Typbreite die GESAMTE Wegbreite, nicht die einer Spur. Ohne diese
	// Sonderbehandlung wurden sie mit 1+1 Spuren doppelt so breit gebaut - ein
	// Fussweg kam auf 3,60 m statt 1,80 m und sah aus wie eine Fahrbahn.
	// Betroffen waren 14.318 Fusswege, 10.089 Pfade und 9.267 Wirtschaftswege.
	switch (Type)
	{
	case EOSMHighwayType::Footway:
	case EOSMHighwayType::Cycleway:
	case EOSMHighwayType::Path:
	case EOSMHighwayType::Steps:
	case EOSMHighwayType::Track:
		OutForwardLanes = 1;
		OutBackwardLanes = 0;
		return;
	default:
		break;
	}

	const FRoadTypeDefinition& Def = GetDefinition(Type);

	const bool bIsOneway = (Oneway == EOSMOnewayType::Forward)
		|| (Oneway == EOSMOnewayType::Backward)
		|| (Oneway == EOSMOnewayType::Reversible);

	// Prioritaet 1: explizite Richtungsspuren.
	int32 ExplicitForward = 0;
	int32 ExplicitBackward = 0;
	const bool bHasForward = FOSMTagParser::ParseLaneCount(Way.GetTag(TEXT("lanes:forward")), ExplicitForward);
	const bool bHasBackward = FOSMTagParser::ParseLaneCount(Way.GetTag(TEXT("lanes:backward")), ExplicitBackward);

	if (bHasForward || bHasBackward)
	{
		OutForwardLanes = bHasForward ? ExplicitForward : 0;
		OutBackwardLanes = bHasBackward ? ExplicitBackward : 0;

		// Ist nur eine Richtung getaggt, wird die andere aus lanes abgeleitet.
		int32 TotalLanes = 0;
		if (FOSMTagParser::ParseLaneCount(Way.GetTag(TEXT("lanes")), TotalLanes))
		{
			if (!bHasForward)
			{
				OutForwardLanes = FMath::Max(0, TotalLanes - OutBackwardLanes);
			}
			if (!bHasBackward)
			{
				OutBackwardLanes = FMath::Max(0, TotalLanes - OutForwardLanes);
			}
		}
		else
		{
			if (!bHasForward) { OutForwardLanes = bIsOneway ? 0 : 1; }
			if (!bHasBackward) { OutBackwardLanes = bIsOneway ? 0 : 1; }
		}
	}
	else
	{
		// Prioritaet 2: Gesamtspurzahl.
		int32 TotalLanes = 0;
		if (FOSMTagParser::ParseLaneCount(Way.GetTag(TEXT("lanes")), TotalLanes))
		{
			if (bIsOneway)
			{
				OutForwardLanes = TotalLanes;
				OutBackwardLanes = 0;
			}
			else
			{
				// Bei ungerader Spurzahl erhaelt die Vorwaertsrichtung die
				// zusaetzliche Spur (typischerweise eine Linksabbiegespur).
				OutBackwardLanes = TotalLanes / 2;
				OutForwardLanes = TotalLanes - OutBackwardLanes;
			}
		}
		else
		{
			// Prioritaet 3: Klassendefault.
			if (bIsOneway)
			{
				OutForwardLanes = Def.DefaultLanesPerDirection;
				OutBackwardLanes = 0;
			}
			else
			{
				OutForwardLanes = Def.DefaultLanesPerDirection;
				OutBackwardLanes = Def.DefaultLanesPerDirection;
			}
		}
	}

	// Bei oneway=-1 verlaeuft der Verkehr gegen die Way-Richtung. Die Spuren
	// werden getauscht, damit "forward" in der Geometrie immer die
	// Fahrtrichtung ist und die Verkehrs-KI keine Sonderfaelle braucht.
	if (Oneway == EOSMOnewayType::Backward)
	{
		Swap(OutForwardLanes, OutBackwardLanes);
	}

	// Eine Strasse ohne jede Spur ist nicht befahrbar - das waere ein
	// Datenfehler (z. B. lanes:forward=0 ohne lanes:backward).
	if (OutForwardLanes + OutBackwardLanes < 1)
	{
		UE_LOG(LogWbRoads, Verbose,
			TEXT("Way %lld: Spurzahl 0 aus Tags ermittelt, auf 1 korrigiert."), Way.Id);
		OutForwardLanes = 1;
		OutBackwardLanes = 0;
	}

	OutForwardLanes = FMath::Clamp(OutForwardLanes, 0, 8);
	OutBackwardLanes = FMath::Clamp(OutBackwardLanes, 0, 8);
}

TArray<FLaneAttributes> URoadTypeLibrary::ResolveLaneAttributes(
	const FOSMWay& Way,
	EOSMHighwayType Type,
	EOSMOnewayType Oneway,
	int32 ForwardLanes,
	int32 BackwardLanes) const
{
	const int32 F = FMath::Max(0, ForwardLanes);
	const int32 B = FMath::Max(0, BackwardLanes);
	const int32 Total = F + B;

	TArray<FLaneAttributes> Attrs;
	if (Total < 1)
	{
		return Attrs;
	}
	Attrs.SetNum(Total);   // Default: Through, keine Sonderspur, keine Grenze
	(void)Oneway;          // Frame ist bereits in ForwardLanes/BackwardLanes aufgeloest

	const bool bTwoWay = (F > 0 && B > 0);

	auto GetTag = [&Way](const FString& Key) { return Way.GetTag(FName(*Key)); };

	// Token-Position (links->rechts in Fahrtrichtung) -> Geometrie-Spurindex.
	// Vorwaertsgruppe liegt rechts (Index B..Total-1), von links aufsteigend.
	// Rueckwaertsgruppe liegt links (Index 0..B-1); in IHRER Fahrtrichtung ist die
	// linkeste Spur der Way-rechte Rand, daher umgekehrt.
	const TFunction<int32(int32)> FwdIndex = [B](int32 i) { return B + i; };
	const TFunction<int32(int32)> BwdIndex = [B](int32 i) { return (B - 1) - i; };

	// Liest eine Pro-Spur-Tagliste (z. B. turn:lanes:forward) und ruft je Spur
	// Fn(GeoIndex, Token). false, wenn Tag fehlt oder die Tokenzahl nicht zur
	// Spurzahl passt (dann bleibt es beim Default - lieber nichts als falsch).
	auto ForEachLaneToken = [&](const FString& Key, int32 Count,
		const TFunction<int32(int32)>& Map, const TFunction<void(int32, const FString&)>& Fn) -> bool
	{
		const FString Tag = GetTag(Key);
		if (Tag.IsEmpty()) { return false; }
		const TArray<FString> Toks = FOSMTagParser::SplitLaneValues(Tag);
		if (Toks.Num() != Count) { return false; }
		for (int32 i = 0; i < Count; ++i) { Fn(Map(i), Toks[i]); }
		return true;
	};

	// ---- turn:lanes -> TurnFlags ----
	const TFunction<void(int32, const FString&)> TurnFn = [&Attrs](int32 Idx, const FString& Tok)
	{
		const uint8 Flags = URoadNetworkGenerator::ParseTurnIndication(Tok);
		if (Flags != static_cast<uint8>(ETurnIndication::None)) { Attrs[Idx].TurnFlags = Flags; }
	};
	if (bTwoWay)
	{
		ForEachLaneToken(TEXT("turn:lanes:forward"), F, FwdIndex, TurnFn);
		ForEachLaneToken(TEXT("turn:lanes:backward"), B, BwdIndex, TurnFn);
	}
	else if (F > 0)
	{
		if (!ForEachLaneToken(TEXT("turn:lanes"), F, FwdIndex, TurnFn))
		{
			ForEachLaneToken(TEXT("turn:lanes:forward"), F, FwdIndex, TurnFn);
		}
	}
	else // Einbahn gegen die Way-Richtung: alle Spuren in der Rueckwaertsgruppe
	{
		if (!ForEachLaneToken(TEXT("turn:lanes"), B, BwdIndex, TurnFn))
		{
			ForEachLaneToken(TEXT("turn:lanes:backward"), B, BwdIndex, TurnFn);
		}
	}

	// ---- Busspuren (nur echte OSM-Fakten) ----
	// (a) ganze Strasse dediziert
	if (GetTag(TEXT("bus")).Equals(TEXT("designated"), ESearchCase::IgnoreCase)
		|| GetTag(TEXT("psv")).Equals(TEXT("designated"), ESearchCase::IgnoreCase))
	{
		for (FLaneAttributes& A : Attrs) { A.bIsBusLane = true; }
	}
	// (b) lanes:psv[:richtung] = Anzahl -> die N rechten Spuren der Richtung
	auto MarkRightmostBus = [&Attrs](int32 GroupStart, int32 GroupCount, bool bForwardGroup, int32 N)
	{
		N = FMath::Clamp(N, 0, GroupCount);
		for (int32 k = 0; k < N; ++k)
		{
			const int32 Idx = bForwardGroup ? (GroupStart + GroupCount - 1 - k) : (GroupStart + k);
			Attrs[Idx].bIsBusLane = true;
		}
	};
	int32 PsvCount = 0;
	if (bTwoWay)
	{
		if (FOSMTagParser::ParseLaneCount(GetTag(TEXT("lanes:psv:forward")), PsvCount)) { MarkRightmostBus(B, F, true, PsvCount); }
		if (FOSMTagParser::ParseLaneCount(GetTag(TEXT("lanes:psv:backward")), PsvCount)) { MarkRightmostBus(0, B, false, PsvCount); }
	}
	else if (FOSMTagParser::ParseLaneCount(GetTag(TEXT("lanes:psv")), PsvCount))
	{
		MarkRightmostBus(0, Total, /*bForwardGroup=*/(F > 0), PsvCount);
	}
	// (c) busway[:seite] = lane
	auto IsLaneValue = [](const FString& V)
	{
		return V.Equals(TEXT("lane"), ESearchCase::IgnoreCase) || V.Equals(TEXT("opposite_lane"), ESearchCase::IgnoreCase);
	};
	if (IsLaneValue(GetTag(TEXT("busway"))) || IsLaneValue(GetTag(TEXT("busway:right")))) { Attrs[Total - 1].bIsBusLane = true; }
	if (IsLaneValue(GetTag(TEXT("busway:left")))) { Attrs[0].bIsBusLane = true; }
	// (d) bus:lanes / psv:lanes je Spur "designated"
	const TFunction<void(int32, const FString&)> BusTokenFn = [&Attrs](int32 Idx, const FString& Tok)
	{
		if (Tok.Equals(TEXT("designated"), ESearchCase::IgnoreCase)) { Attrs[Idx].bIsBusLane = true; }
	};
	if (bTwoWay)
	{
		if (!ForEachLaneToken(TEXT("bus:lanes:forward"), F, FwdIndex, BusTokenFn)) { ForEachLaneToken(TEXT("psv:lanes:forward"), F, FwdIndex, BusTokenFn); }
		if (!ForEachLaneToken(TEXT("bus:lanes:backward"), B, BwdIndex, BusTokenFn)) { ForEachLaneToken(TEXT("psv:lanes:backward"), B, BwdIndex, BusTokenFn); }
	}
	else
	{
		const TFunction<int32(int32)> Map = (F > 0) ? FwdIndex : BwdIndex;
		if (!ForEachLaneToken(TEXT("bus:lanes"), Total, Map, BusTokenFn)) { ForEachLaneToken(TEXT("psv:lanes"), Total, Map, BusTokenFn); }
	}

	// ---- Radfahrstreifen (cycleway=lane/track am Weg) ----
	auto IsBikeValue = [](const FString& V)
	{
		return V.Equals(TEXT("lane"), ESearchCase::IgnoreCase) || V.Equals(TEXT("track"), ESearchCase::IgnoreCase)
			|| V.Equals(TEXT("opposite_lane"), ESearchCase::IgnoreCase) || V.Equals(TEXT("opposite_track"), ESearchCase::IgnoreCase);
	};
	if (IsBikeValue(GetTag(TEXT("cycleway"))) || IsBikeValue(GetTag(TEXT("cycleway:right"))) || IsBikeValue(GetTag(TEXT("cycleway:both")))) { Attrs[Total - 1].bIsBikeLane = true; }
	if (IsBikeValue(GetTag(TEXT("cycleway:left"))) || IsBikeValue(GetTag(TEXT("cycleway:both")))) { Attrs[0].bIsBikeLane = true; }
	const TFunction<void(int32, const FString&)> BikeTokenFn = [&Attrs, &IsBikeValue](int32 Idx, const FString& Tok)
	{
		if (IsBikeValue(Tok)) { Attrs[Idx].bIsBikeLane = true; }
	};
	if (bTwoWay)
	{
		ForEachLaneToken(TEXT("cycleway:lanes:forward"), F, FwdIndex, BikeTokenFn);
		ForEachLaneToken(TEXT("cycleway:lanes:backward"), B, BwdIndex, BikeTokenFn);
	}
	else
	{
		const TFunction<int32(int32)> Map = (F > 0) ? FwdIndex : BwdIndex;
		ForEachLaneToken(TEXT("cycleway:lanes"), Total, Map, BikeTokenFn);
	}

	// ---- Grenzstile (Konvention aus Klasse/Anordnung) ----
	const FRoadTypeDefinition& Def = GetDefinition(Type);
	for (int32 i = 0; i + 1 < Total; ++i)
	{
		const bool bSpecialBoundary = (Attrs[i].bIsBusLane != Attrs[i + 1].bIsBusLane)
			|| (Attrs[i].bIsBikeLane != Attrs[i + 1].bIsBikeLane);
		ELaneBoundaryStyle Style;
		if (bSpecialBoundary)
		{
			Style = ELaneBoundaryStyle::Solid;       // Sonderspur wird immer abgetrennt (Breitstrich)
		}
		else if (!Def.bHasCenterLineMarking)
		{
			Style = ELaneBoundaryStyle::None;        // z. B. Tempo-30-Wohnstrasse ohne Fahrbahnmarkierung
		}
		else if (bTwoWay && (i + 1 == B))
		{
			Style = ELaneBoundaryStyle::DirSplit;    // Richtungstrennung (Mittellinie)
		}
		else
		{
			Style = ELaneBoundaryStyle::Dashed;      // Leitlinie
		}
		Attrs[i].RightBoundary = Style;
		Attrs[i + 1].LeftBoundary = Style;
	}
	const ELaneBoundaryStyle EdgeStyle = Def.bHasEdgeLineMarking ? ELaneBoundaryStyle::Edge : ELaneBoundaryStyle::None;
	Attrs[0].LeftBoundary = EdgeStyle;
	Attrs[Total - 1].RightBoundary = EdgeStyle;

	return Attrs;
}

double URoadTypeLibrary::ResolveCarriagewayWidthMeters(
	const FOSMWay& Way,
	EOSMHighwayType Type,
	int32 ForwardLanes,
	int32 BackwardLanes) const
{
	const FRoadTypeDefinition& Def = GetDefinition(Type);

	// Prioritaet 1: width=* ist eine Vermessung und schlaegt jede Rechnung.
	double ExplicitWidth = 0.0;
	if (FOSMTagParser::ParseLengthMeters(Way.GetTag(TEXT("width")), ExplicitWidth))
	{
		// Plausibilitaetsgrenzen: unter 1,5 m ist keine Fahrbahn, ueber 60 m
		// ist ein Tippfehler (in Wiesbaden ist keine Fahrbahn breiter als
		// die Mainzer Strasse mit ~22 m).
		if (ExplicitWidth >= 1.5 && ExplicitWidth <= 60.0)
		{
			return ExplicitWidth;
		}

		UE_LOG(LogWbRoads, Verbose,
			TEXT("Way %lld: width=%.1f m unplausibel, wird aus der Spurzahl berechnet."),
			Way.Id, ExplicitWidth);
	}

	// carriageway_width ist ein selteneres, aber praeziseres Synonym.
	if (FOSMTagParser::ParseLengthMeters(Way.GetTag(TEXT("carriageway_width")), ExplicitWidth)
		&& ExplicitWidth >= 1.5 && ExplicitWidth <= 60.0)
	{
		return ExplicitWidth;
	}

	// Prioritaet 2: Spurzahl * Spurbreite.
	const int32 TotalLanes = FMath::Max(1, ForwardLanes + BackwardLanes);
	double Width = TotalLanes * Def.LaneWidthMeters;

	// Parkstreifen erweitern die befestigte Flaeche merklich. In Wiesbadener
	// Wohnstrassen ist Laengsparken der Regelfall und macht den Unterschied
	// zwischen einer glaubwuerdig engen und einer zu breiten Strasse.
	auto HasParkingLane = [&Way](const TCHAR* Key) -> bool
	{
		const FString Value = Way.GetTag(Key).ToLower();
		return Value == TEXT("parallel") || Value == TEXT("diagonal") || Value == TEXT("perpendicular");
	};

	if (HasParkingLane(TEXT("parking:lane:left")) || HasParkingLane(TEXT("parking:left")))
	{
		Width += 2.0;
	}
	if (HasParkingLane(TEXT("parking:lane:right")) || HasParkingLane(TEXT("parking:right")))
	{
		Width += 2.0;
	}
	if (HasParkingLane(TEXT("parking:lane:both")) || HasParkingLane(TEXT("parking:both")))
	{
		Width += 4.0;
	}

	return Width;
}

double URoadTypeLibrary::ResolveMaxSpeedKmh(const FOSMWay& Way, EOSMHighwayType Type) const
{
	double Speed = 0.0;
	if (FOSMTagParser::ParseMaxSpeedKmh(Way.GetTag(TEXT("maxspeed")), Speed) && Speed > 0.0)
	{
		return Speed;
	}

	// maxspeed:type traegt in Deutschland haeufig das implizite Limit.
	if (FOSMTagParser::ParseMaxSpeedKmh(Way.GetTag(TEXT("maxspeed:type")), Speed) && Speed > 0.0)
	{
		return Speed;
	}
	if (FOSMTagParser::ParseMaxSpeedKmh(Way.GetTag(TEXT("zone:maxspeed")), Speed) && Speed > 0.0)
	{
		return Speed;
	}

	return GetDefinition(Type).DefaultMaxSpeedKmh;
}

EOSMSidewalkType URoadTypeLibrary::ResolveSidewalk(const FOSMWay& Way, EOSMHighwayType Type) const
{
	const EOSMSidewalkType Tagged = FOSMTagParser::ParseSidewalkType(Way);

	// Ein explizites Tag ist immer verbindlich - auch sidewalk=no.
	if (Way.HasTag(TEXT("sidewalk"))
		|| Way.HasTag(TEXT("sidewalk:left"))
		|| Way.HasTag(TEXT("sidewalk:right"))
		|| Way.HasTag(TEXT("sidewalk:both")))
	{
		return Tagged;
	}

	// Ohne Tag entscheidet die Strassenklasse. In Wiesbaden hat praktisch jede
	// innerstaedtische Strasse Gehwege, auch wenn OSM sie nicht erfasst;
	// sie weglassen wuerde die Stadt sichtbar falsch darstellen.
	const FRoadTypeDefinition& Def = GetDefinition(Type);
	if (!Def.bSidewalkByDefault)
	{
		return EOSMSidewalkType::None;
	}

	// Bruecken und Tunnel bekommen nur Gehwege, wenn sie getaggt sind -
	// Autobahnbruecken haben keine.
	if (Way.IsBridge() || Way.IsTunnel())
	{
		return EOSMSidewalkType::None;
	}

	return EOSMSidewalkType::Both;
}

EOSMSurfaceType URoadTypeLibrary::ResolveSurface(const FOSMWay& Way, EOSMHighwayType Type) const
{
	const FString SurfaceTag = Way.GetTag(TEXT("surface"));
	if (!SurfaceTag.IsEmpty())
	{
		const EOSMSurfaceType Parsed = FOSMTagParser::ParseSurfaceType(SurfaceTag);
		if (Parsed != EOSMSurfaceType::Unknown)
		{
			return Parsed;
		}
	}

	return GetDefinition(Type).DefaultSurface;
}
