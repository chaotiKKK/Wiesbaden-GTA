// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/BuildingGenerator.h"

#include "WiesbadenReal.h"

#include <initializer_list>

#include "Algo/Reverse.h"
#include "GIS/CityPrompt.h"
#include "GIS/GeoCoordinateConverter.h"
#include "GIS/PolygonUtils.h"

namespace
{
	constexpr double MetersToCm = 100.0;

	/** Quadratzentimeter -> Quadratmeter. */
	constexpr double SqCmToSqM = 1.0 / (100.0 * 100.0);

	/** Materialvarianten der Fassaden. Index entspricht dem Materialslot. */
	enum EFacadeVariant : int32
	{
		Facade_Plaster = 0,		// Putz - Gruenderzeit und Nachkriegsbau
		Facade_Brick = 1,		// Backstein
		Facade_Sandstone = 2,	// Sandstein - historische Bauten, Kurhaus, Landtag
		Facade_Glass = 3,		// Glas/Stahl - Buerobauten
		Facade_Concrete = 4,	// Sichtbeton - Industrie, Parkhaeuser
		Facade_Timber = 5,		// Fachwerk - Altstadt Bierstadt, Sonnenberg
		Facade_MAX
	};

	/**
	 * Deterministischer 32-Bit-Hash (FNV-1a) ueber die ASCII-Zeichen des
	 * Seeds (OSM-Id als String). Reicht fuer die gewichtete Stil-Verteilung;
	 * kryptographische Eigenschaften sind nicht noetig.
	 */
	uint32 PromptSeedHash(const FString& Seed)
	{
		uint32 Hash = 2166136261u;
		for (const TCHAR Char : Seed)
		{
			Hash ^= static_cast<uint32>(Char);
			Hash *= 16777619u;
		}
		return Hash;
	}
}

FString FBuildingGenerationReport::ToString() const
{
	if (!bSuccess)
	{
		return FString::Printf(TEXT("Gebaeude FEHLGESCHLAGEN: %s"), *ErrorMessage);
	}

	return FString::Printf(
		TEXT("Gebaeude erzeugt in %.2f s: %d Gebaeude (%d aus Relationen, %d Landmarken), ")
		TEXT("verworfen: %d zu klein, %d degeneriert, %d unvollstaendig, %d im Wasser. %d Vertices, %d Dreiecke"),
		DurationSeconds, BuildingCount, RelationBuildingCount, LandmarkCount,
		TooSmallCount, DegenerateCount, IncompleteCount, WaterRemovedCount, VertexCount, TriangleCount);
}

double UBuildingGenerator::VaryDefaultHeightMeters(
	double BaseHeightMeters,
	double FootprintAreaSqm,
	double DistanceToCentreCm,
	int64 SourceId,
	double MetersPerLevel)
{
	if (BaseHeightMeters <= 0.0 || MetersPerLevel <= 0.0)
	{
		return BaseHeightMeters;
	}

	double Levels = BaseHeightMeters / MetersPerLevel;

	// -- Lage zum Zentrum ---------------------------------------------------
	//
	// Die Georeferenz liegt auf dem Wiesbadener Zentrum (8.24, 50.0824, nahe
	// Luisenplatz); der Weltursprung ist damit die Innenstadt. Gruenderzeit-
	// Bloecke dort haben 4-5 Geschosse, die Vororte 2-3. Der Bonus faellt
	// linear bis 4 km ab.
	constexpr double CentreRadiusCm = 400000.0;   // 4 km
	const double CentreFactor = FMath::Clamp(1.0 - DistanceToCentreCm / CentreRadiusCm, 0.0, 1.0);
	Levels += 1.6 * CentreFactor;

	// -- Grundrissgroesse ---------------------------------------------------
	//
	// Ein Reihenhaus mit 70 m2 hat nicht so viele Geschosse wie ein
	// Mehrfamilienblock mit 400 m2. Die Schwellen sind an der tatsaechlichen
	// Wiesbadener Bebauung orientiert.
	if (FootprintAreaSqm > 0.0)
	{
		if (FootprintAreaSqm < 80.0)        { Levels -= 0.6; }
		else if (FootprintAreaSqm > 600.0)  { Levels += 1.0; }
		else if (FootprintAreaSqm > 250.0)  { Levels += 0.5; }
	}

	// -- Streuung -----------------------------------------------------------
	//
	// Ohne sie bekommt eine ganze Strassenzeile dieselbe Hoehe - genau das
	// liess die Stadt wie eine Mauer aussehen. Aus der SourceId abgeleitet und
	// damit reproduzierbar: derselbe Build ergibt dieselbe Stadt.
	const uint32 Hash = GetTypeHash(SourceId) * 2654435761u;
	const double Jitter = (static_cast<double>(Hash % 1000u) / 1000.0) * 2.0 - 1.0;   // -1 .. +1
	Levels += Jitter * 0.8;

	// Mindestens ein Geschoss; nach oben begrenzt, damit ein Ausreisser in den
	// Daten keinen Turm mitten im Wohngebiet erzeugt.
	Levels = FMath::Clamp(Levels, 1.0, 8.0);

	return Levels * MetersPerLevel;
}

float UBuildingGenerator::GetRegionalHeightScale(ECityRegionType Type)
{
	switch (Type)
	{
	case ECityRegionType::Industrial:
		// Grosszuegige Hallen und Produktionsbauten.
		return 1.1f;
	case ECityRegionType::Green:
		// Im Park stehen keine Hochhaeuser.
		return 0.85f;
	default:
		return 1.0f;
	}
}

FString UBuildingGenerator::GetRegionalFacadeKey(ECityRegionType Type)
{
	switch (Type)
	{
	case ECityRegionType::Industrial:
		return TEXT("Region:Industrie");
	case ECityRegionType::Commercial:
		return TEXT("Region:Gewerbe");
	default:
		return FString();
	}
}

const TArray<FString>& UBuildingGenerator::GetLandmarkNames()
{
	// Namen exakt so, wie sie in OSM eingetragen sind. Der Abgleich erfolgt
	// case-insensitiv und als Teilstring, damit Varianten wie
	// "Kurhaus" / "Kurhaus Wiesbaden" beide greifen.
	static const TArray<FString> Names = {
		TEXT("Kurhaus"),
		TEXT("Hessischer Landtag"),
		TEXT("Stadtschloss"),
		TEXT("Rathaus"),
		TEXT("Marktkirche"),
		TEXT("Bergkirche"),
		TEXT("St. Bonifatius"),
		TEXT("Russisch-Orthodoxe Kirche"),
		TEXT("Nerobergbahn"),
		TEXT("Opelbad"),
		TEXT("Schloss Biebrich"),
		TEXT("BRITA-Arena"),
		TEXT("Hauptbahnhof"),
		TEXT("Wiesbaden Hauptbahnhof"),
		TEXT("Mauritius-Therme"),
		TEXT("Kaiser-Friedrich-Therme"),
		TEXT("LuisenForum"),
		TEXT("Lilien-Carr"),			// "Lilien-Carré" - ohne Akzent fuer robusten Vergleich
		TEXT("Hessisches Staatstheater"),
		TEXT("Museum Wiesbaden"),
		TEXT("Schlachthof"),
		TEXT("Landeshaus"),
	};
	return Names;
}

bool UBuildingGenerator::IsLandmark(const FString& BuildingName, EOSMBuildingType Type, double FootprintAreaSqm)
{
	if (!BuildingName.IsEmpty())
	{
		for (const FString& Landmark : GetLandmarkNames())
		{
			if (BuildingName.Contains(Landmark, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
	}

	// Sehr grosse oeffentliche Bauten sind auch ohne Namenstreffer
	// Landmarken - sie praegen die Silhouette und rechtfertigen ein
	// handmodelliertes Asset.
	if (FootprintAreaSqm > 5000.0)
	{
		switch (Type)
		{
		case EOSMBuildingType::Civic:
		case EOSMBuildingType::Church:
		case EOSMBuildingType::TrainStation:
		case EOSMBuildingType::University:
		case EOSMBuildingType::Hospital:
			return true;
		default:
			break;
		}
	}

	return false;
}

int32 UBuildingGenerator::SelectMaterialVariant(const TMap<FName, FString>& Tags,
	EOSMBuildingType Type, int64 SeedId)
{
	// Prioritaet 1: explizites Material-Tag - authoritativ, KEINE Streuung.
	// Wo OSM die Bauweise kennt, wird sie befolgt.
	if (const FString* Material = Tags.Find(TEXT("building:material")))
	{
		const FString Value = Material->ToLower();
		if (Value.Contains(TEXT("brick"))) { return Facade_Brick; }
		if (Value.Contains(TEXT("sandstone")) || Value.Contains(TEXT("stone"))) { return Facade_Sandstone; }
		if (Value.Contains(TEXT("glass"))) { return Facade_Glass; }
		if (Value.Contains(TEXT("concrete"))) { return Facade_Concrete; }
		if (Value.Contains(TEXT("wood")) || Value.Contains(TEXT("timber"))) { return Facade_Timber; }
		if (Value.Contains(TEXT("plaster")) || Value.Contains(TEXT("render"))) { return Facade_Plaster; }
	}

	// Deterministische Streuung je Gebaeude aus der Quell-Id. Benachbarte
	// Gebaeude tragen verschiedene OSM-Ids -> verschiedene Fassaden, aber
	// stabil ueber Neubauten hinweg (gleiche Id -> gleiche Wahl). Ohne das
	// fiel frueher ein ganzer Wohnblock auf DIESELBE Variante (meist Putz) und
	// wirkte uniform - genau das soll die Palette aufbrechen.
	struct FWeighted { int32 Variant; int32 Weight; };
	auto Pick = [SeedId](std::initializer_list<FWeighted> Items) -> int32
	{
		int32 Total = 0;
		for (const FWeighted& It : Items) { Total += It.Weight; }
		if (Total <= 0) { return Facade_Plaster; }
		uint32 H = static_cast<uint32>(SeedId);
		H *= 2654435761u; H ^= H >> 15; H *= 2246822519u; H ^= H >> 13;
		int32 Roll = static_cast<int32>(H % static_cast<uint32>(Total));
		for (const FWeighted& It : Items)
		{
			if (Roll < It.Weight) { return It.Variant; }
			Roll -= It.Weight;
		}
		return Items.begin()->Variant;
	};

	// Baujahr bestimmen (start_date: "1895", "1895-04", "C19", "~1900" -> erste
	// vier Ziffern). 0 = unbekannt.
	int32 Year = 0;
	if (const FString* StartDate = Tags.Find(TEXT("start_date")))
	{
		FString YearString;
		for (const TCHAR Char : *StartDate)
		{
			if (FChar::IsDigit(Char))
			{
				YearString.AppendChar(Char);
				if (YearString.Len() == 4) { break; }
			}
			else if (YearString.Len() > 0) { break; }
		}
		if (YearString.Len() == 4) { Year = FCString::Atoi(*YearString); }
	}

	// Prioritaet 2: eindeutig nicht-wohnliche Typen bleiben EINHEITLICH - ein
	// Bueroturm soll Glas sein, ein Parkhaus Beton, eine Kirche Sandstein.
	switch (Type)
	{
	case EOSMBuildingType::Office:
	case EOSMBuildingType::Commercial:
	case EOSMBuildingType::Retail:			return Facade_Glass;
	case EOSMBuildingType::Industrial:
	case EOSMBuildingType::Warehouse:
	case EOSMBuildingType::Garage:			return Facade_Concrete;
	case EOSMBuildingType::Church:
	case EOSMBuildingType::Civic:
	case EOSMBuildingType::University:		return Facade_Sandstone;
	default:								break;   // Wohn-/Generisch: streuen
	}

	// Prioritaet 3: Wohnbauten - je Epoche eine gemischte Palette statt EINER
	// Variante. Die Gewichte spiegeln die Wiesbadener Bausubstanz wider.
	if (Year > 1000 && Year < 1850)
	{
		return Pick({ { Facade_Timber, 60 }, { Facade_Plaster, 30 }, { Facade_Sandstone, 10 } });
	}
	if (Year >= 1850 && Year < 1920)   // Gruenderzeit
	{
		return Pick({ { Facade_Sandstone, 40 }, { Facade_Plaster, 40 }, { Facade_Brick, 20 } });
	}
	if (Year >= 1920 && Year < 1960)   // Zwischenkrieg / Wiederaufbau
	{
		return Pick({ { Facade_Plaster, 55 }, { Facade_Brick, 30 }, { Facade_Sandstone, 15 } });
	}
	if (Year >= 1960 && Year < 1990)
	{
		return Pick({ { Facade_Concrete, 55 }, { Facade_Plaster, 45 } });
	}
	if (Year >= 1990)
	{
		return Pick({ { Facade_Glass, 50 }, { Facade_Concrete, 50 } });
	}

	// Kein Baujahr: Wiesbadener Gruenderzeit-Mischung als Standard.
	return Pick({ { Facade_Plaster, 45 }, { Facade_Sandstone, 35 }, { Facade_Brick, 20 } });
}

int32 UBuildingGenerator::RoofCoveringIndex(int32 MaterialVariant, EOSMRoofShape Shape)
{
	// Flachdaecher sind nie gedeckt - Bitumen/Kies/Blech. Als Zink-Grau (2)
	// dargestellt, unabhaengig von der Fassade: ein flaches Buero- wie ein
	// flaches Wohnhaus-Dach traegt keine Pfannen.
	if (Shape == EOSMRoofShape::Flat)
	{
		return 2;
	}

	// Geneigte Daecher folgen der Bauweise (dieselbe Variante wie die Fassade):
	//   Sandstein  = Gruenderzeit, Kirchen, Civic  -> Schiefer (historisch)
	//   Glas/Beton = Buero, Industrie, Parkhaus    -> Zink/Blech
	//   Rest (Putz/Backstein/Fachwerk = Wohnbau)   -> Terrakotta-Pfanne
	switch (MaterialVariant)
	{
	case Facade_Sandstone:
		return 1;
	case Facade_Glass:
	case Facade_Concrete:
		return 2;
	default:
		return 0;
	}
}

uint8 UBuildingGenerator::RoofToneByte(int64 SourceId)
{
	// Deterministische, gleichverteilte Tonstufe je Gebaeude aus der OSM-Id.
	// Eigene Mischung (nicht die der Fassaden-/Deckungswahl in Zeile ~105 und
	// SelectMaterialVariant), damit der Farbton NICHT mit der gewuerfelten
	// Fassaden- oder Deckungsart korreliert: zwei gleichtypige Nachbarhaeuser
	// bekommen unabhaengige Toene. Ganzzahl-Finalizer im Stil von Murmur3 -
	// benachbarte Ids (typisch fortlaufend im OSM) landen weit auseinander,
	// die oberen 8 Bit sind am staerksten durchmischt.
	uint32 H = static_cast<uint32>(SourceId) ^ static_cast<uint32>(static_cast<uint64>(SourceId) >> 32);
	H *= 0x9E3779B1u;
	H ^= H >> 15;
	H *= 0x85EBCA77u;
	H ^= H >> 13;
	return static_cast<uint8>(H >> 24);
}

FString UBuildingGenerator::NormalizeAddressForMatch(const FString& Address)
{
	// ToLower ist in UE ASCII-only: Grossbuchstaben-Umlaute (Ae/Oe/Ue) und das
	// grosse sz wuerden NICHT umgewandelt. Deshalb werden beide Schreibweisen
	// vorab auf ihre ASCII-Form transliteriert (Gross- und Kleinschreibung),
	// damit "GRUENBERGER Strasse" und "Gruenberger Strasse" dieselbe
	// Vergleichsform ergeben. Quelle bleibt ASCII (Umlaute als \uXXXX-Escapes).
	FString Normalized = Address;
	Normalized = Normalized.Replace(TEXT("\u00C4"), TEXT("ae"), ESearchCase::CaseSensitive); // A
	Normalized = Normalized.Replace(TEXT("\u00E4"), TEXT("ae"), ESearchCase::CaseSensitive); // a
	Normalized = Normalized.Replace(TEXT("\u00D6"), TEXT("oe"), ESearchCase::CaseSensitive); // O
	Normalized = Normalized.Replace(TEXT("\u00F6"), TEXT("oe"), ESearchCase::CaseSensitive); // o
	Normalized = Normalized.Replace(TEXT("\u00DC"), TEXT("ue"), ESearchCase::CaseSensitive); // U
	Normalized = Normalized.Replace(TEXT("\u00FC"), TEXT("ue"), ESearchCase::CaseSensitive); // u
	Normalized = Normalized.Replace(TEXT("\u00DF"), TEXT("ss"), ESearchCase::CaseSensitive); // sz
	Normalized = Normalized.Replace(TEXT("\u1E9E"), TEXT("ss"), ESearchCase::CaseSensitive); // grosses sz
	return Normalized.ToLower();
}

bool UBuildingGenerator::ApplyPlatterAddressOverride(FGeneratedBuilding& OutBuilding, double MetersPerLevel,
	bool bHasReliableHeight)
{
	// Amtliche Hoehe (Hessen-LoD2, als height-Tag injiziert) schlaegt das
	// hartcodierte Raten: der Override weicht dann komplett zurueck. Er bleibt
	// nur Fallback fuer Footprints ohne verlaessliche Hoehe.
	if (bHasReliableHeight)
	{
		return false;
	}

	if (OutBuilding.Address.IsEmpty())
	{
		return false;
	}

	const FString NormAddr = NormalizeAddressForMatch(OutBuilding.Address);
	if (NormAddr == NormalizeAddressForMatch(TEXT("Platter Strasse 140")))
	{
		OutBuilding.LevelCount = 12;
		OutBuilding.HeightCm = 12.0 * MetersPerLevel * MetersToCm;
		return true;
	}
	if (NormAddr == NormalizeAddressForMatch(TEXT("Platter Strasse 142")))
	{
		OutBuilding.LevelCount = 1;
		OutBuilding.HeightCm = 3.2 * MetersToCm;   // flache, eingeschossige Garage
		OutBuilding.BuildingType = EOSMBuildingType::Garage;
		OutBuilding.RoofShape = EOSMRoofShape::Flat;
		return true;
	}
	if (NormAddr == NormalizeAddressForMatch(TEXT("Platter Strasse 144"))
		|| NormAddr == NormalizeAddressForMatch(TEXT("Platter Strasse 146")))
	{
		OutBuilding.LevelCount = 6;
		OutBuilding.HeightCm = 6.0 * MetersPerLevel * MetersToCm;
		return true;
	}
	return false;
}

FString UBuildingGenerator::ResolveFacadeOverrideKey(
	const FString& Address,
	const TArray<FString>& OverrideAddresses)
{
	if (Address.IsEmpty())
	{
		return FString();
	}

	const FString NormalizedAddress = NormalizeAddressForMatch(Address);
	for (const FString& Override : OverrideAddresses)
	{
		if (NormalizeAddressForMatch(Override) == NormalizedAddress)
		{
			// Kanonische Schreibweise der Override-Liste zurueckgeben, damit die
			// Render-Seite exakt in AddressFacadeMaterials nachschlagen kann.
			return Override;
		}
	}

	return FString();
}

int32 UBuildingGenerator::SelectPromptVariant(const TMap<int32, float>& VariantWeights, const FString& SeedString)
{
	// Sortierte Varianten: die gewichtete Auswahl muss ueber Laeufe hinweg
	// deterministisch sein (TMap-Iteration ist nicht sortiert).
	TArray<int32> SortedVariants;
	VariantWeights.GetKeys(SortedVariants);
	SortedVariants.Sort();

	float Total = 0.0f;
	for (const int32 Variant : SortedVariants)
	{
		Total += FMath::Max(0.0f, VariantWeights[Variant]);
	}
	if (Total <= 0.0f)
	{
		return -1;
	}

	// Deterministische gewichtete Auswahl: Roll aus dem Seed im Wertebereich
	// 0..Total (exklusive Total).
	const float Roll = FMath::Fmod(static_cast<float>(PromptSeedHash(SeedString)), Total);
	float Accumulated = 0.0f;
	for (const int32 Variant : SortedVariants)
	{
		Accumulated += FMath::Max(0.0f, VariantWeights[Variant]);
		if (Roll < Accumulated)
		{
			return Variant;
		}
	}
	return SortedVariants.Last();
}

FString UBuildingGenerator::ResolvePromptStyleKey(
	const TMap<int32, float>& VariantWeights,
	const FString& SeedString)
{
	const int32 Variant = SelectPromptVariant(VariantWeights, SeedString);
	if (Variant < 0)
	{
		return FString();
	}

	const TArray<FString> StyleNames = CityPromptParser::GetFacadeStyleNames();
	if (Variant >= StyleNames.Num())
	{
		return FString();
	}
	return FString::Printf(TEXT("PromptStyle:%s"), *StyleNames[Variant]);
}

FString UBuildingGenerator::ResolvePromptLandmarkKey(
	const FString& BuildingName,
	const TArray<FString>& PromptLandmarkNames)
{
	if (BuildingName.IsEmpty() || PromptLandmarkNames.Num() == 0)
	{
		return FString();
	}

	// Gleiche Normalisierung wie bei Adressen (lowercase + sz/ae/oe/ue):
	// OSM-Name "Kurhaus Wiesbaden" und Prompt-Keyword "Kurhaus" ergeben
	// dieselbe Vergleichsform. Wortgrenzen verhindern Teilwort-Treffer
	// ("Kurhausstrasse" soll nicht "Kurhaus" matchen).
	const FString NormalizedName = NormalizeAddressForMatch(BuildingName);
	for (const FString& Landmark : PromptLandmarkNames)
	{
		const FString Keyword = NormalizeAddressForMatch(Landmark);
		if (Keyword.IsEmpty())
		{
			continue;
		}

		const int32 Found = NormalizedName.Find(Keyword, ESearchCase::CaseSensitive);
		if (Found == INDEX_NONE)
		{
			continue;
		}
		const bool bBoundaryBefore = (Found == 0) || !FChar::IsAlnum(NormalizedName[Found - 1]);
		const int32 EndIndex = Found + Keyword.Len();
		const bool bBoundaryAfter = (EndIndex >= NormalizedName.Len()) || !FChar::IsAlnum(NormalizedName[EndIndex]);
		if (bBoundaryBefore && bBoundaryAfter)
		{
			// Kanonischer Name der Prompt-Liste: die Render-Seite schlaegt
			// exakt in PromptFacadeMaterials nach.
			return FString::Printf(TEXT("PromptLandmark:%s"), *Landmark);
		}
	}
	return FString();
}

int32 UBuildingGenerator::FindOrAddSection(
	FBuildingMeshData& MeshData,
	EBuildingMeshChannel Channel,
	int32 MaterialVariant,
	const FString& FacadeOverrideKey)
{
	for (int32 Index = 0; Index < MeshData.Sections.Num(); ++Index)
	{
		const FBuildingMeshSection& Section = MeshData.Sections[Index];
		if (Section.Channel == Channel
			&& Section.MaterialVariant == MaterialVariant
			&& Section.FacadeOverrideKey == FacadeOverrideKey)
		{
			return Index;
		}
	}

	FBuildingMeshSection NewSection;
	NewSection.Channel = Channel;
	NewSection.MaterialVariant = MaterialVariant;
	NewSection.FacadeOverrideKey = FacadeOverrideKey;
	return MeshData.Sections.Add(MoveTemp(NewSection));
}

FBuildingGenerationReport UBuildingGenerator::Generate(
	const FOSMDataSet& DataSet,
	const UGeoCoordinateConverter* Converter,
	const IHeightSampler* HeightSampler,
	const FBuildingGenerationSettings& Settings,
	TArray<FGeneratedBuilding>& OutBuildings,
	FBuildingMeshData* OutMeshData,
	TFunction<bool()> Cancel)
{
	FBuildingGenerationReport Report;
	const double StartTime = FPlatformTime::Seconds();

	// Abbruch-Callback: wird zwischen den Gebaeuden gepollt (siehe Header).
	// Default-parametrisiert mit nullptr, damit bestehende Aufrufer unveraendert
	// kompilieren.
	const auto Cancelled = [&Cancel]()
	{
		return Cancel && Cancel();
	};

	if (!Converter || !Converter->IsInitialized())
	{
		Report.ErrorMessage = TEXT("Georeferenzierung fehlt oder ist nicht initialisiert.");
		UE_LOG(LogWbBuildings, Error, TEXT("%s"), *Report.ErrorMessage);
		return Report;
	}

	OutBuildings.Reset();
	if (OutMeshData)
	{
		OutMeshData->Reset();
	}

	// Ways, die als Member einer Gebaeude-Multipolygon-Relation auftreten,
	// duerfen nicht zusaetzlich einzeln erzeugt werden - sonst steht in jedem
	// Innenhof ein zweites Gebaeude.
	TSet<FOSMId> WaysUsedByRelations;

	TArray<FOSMId> SortedRelationIds;
	SortedRelationIds.Reserve(DataSet.Relations.Num());
	for (const TPair<FOSMId, FOSMRelation>& Pair : DataSet.Relations)
	{
		SortedRelationIds.Add(Pair.Key);
	}
	SortedRelationIds.Sort();

	// -- Multipolygon-Gebaeude ---------------------------------------------

	for (const FOSMId RelationId : SortedRelationIds)
	{
		if (Cancelled())
		{
			Report.bCancelled = true;
			Report.BuildingCount = OutBuildings.Num();
			Report.DurationSeconds = FPlatformTime::Seconds() - StartTime;
			UE_LOG(LogWbBuildings, Log,
				TEXT("Gebaeude-Generierung abgebrochen (Multipolygon-Pass, %d erzeugt)."),
				OutBuildings.Num());
			return Report;
		}

		const FOSMRelation& Relation = DataSet.Relations[RelationId];

		if (!Relation.IsMultipolygon())
		{
			continue;
		}

		const bool bIsBuilding = Relation.Tags.Contains(TEXT("building"))
			|| Relation.Tags.Contains(TEXT("building:part"));
		if (!bIsBuilding)
		{
			continue;
		}

		TArray<TArray<FVector2D>> OuterRings;
		TArray<TArray<FVector2D>> InnerRings;

		if (!AssembleMultipolygon(Relation, DataSet, *Converter, OuterRings, InnerRings))
		{
			++Report.IncompleteCount;
			continue;
		}

		for (const FOSMRelationMember& Member : Relation.Members)
		{
			if (Member.Type == EOSMMemberType::Way)
			{
				WaysUsedByRelations.Add(Member.Ref);
			}
		}

		// Jeder Aussenring wird ein eigenes Volumen. Die Innenringe werden
		// demjenigen Aussenring zugeordnet, der sie enthaelt - eine Relation
		// kann mehrere getrennte Baukoerper mit je eigenem Hof beschreiben.
		for (const TArray<FVector2D>& OuterRing : OuterRings)
		{
			TArray<TArray<FVector2D>> MatchingHoles;
			for (const TArray<FVector2D>& InnerRing : InnerRings)
			{
				if (InnerRing.Num() > 0 && FPolygonUtils::IsPointInPolygon(InnerRing[0], OuterRing))
				{
					MatchingHoles.Add(InnerRing);
				}
			}

			FGeneratedBuilding Building;
			if (BuildSingleBuilding(
				Relation.Id, /*bFromRelation=*/true, Relation.Tags,
				OuterRing, MatchingHoles, HeightSampler, Settings, Building, OutMeshData))
			{
				if (Building.bIsLandmark)
				{
					++Report.LandmarkCount;
				}
				++Report.RelationBuildingCount;
				OutBuildings.Add(MoveTemp(Building));
			}
			else if (Building.RegionType == ECityRegionType::Water)
			{
				// Im Wasser verworfen (Regionen-Regel) - kein Degenerationsfehler.
				++Report.WaterRemovedCount;
			}
			else
			{
				++Report.DegenerateCount;
			}
		}
	}

	// -- Gebaeude aus einzelnen Ways ---------------------------------------

	TArray<FOSMId> SortedWayIds;
	SortedWayIds.Reserve(DataSet.Ways.Num());
	for (const TPair<FOSMId, FOSMWay>& Pair : DataSet.Ways)
	{
		SortedWayIds.Add(Pair.Key);
	}
	SortedWayIds.Sort();

	TArray<FGeoCoordinate> Coords;

	for (const FOSMId WayId : SortedWayIds)
	{
		if (Cancelled())
		{
			Report.bCancelled = true;
			Report.BuildingCount = OutBuildings.Num();
			Report.DurationSeconds = FPlatformTime::Seconds() - StartTime;
			UE_LOG(LogWbBuildings, Log,
				TEXT("Gebaeude-Generierung abgebrochen (Way-Pass, %d erzeugt)."),
				OutBuildings.Num());
			return Report;
		}

		const FOSMWay& Way = DataSet.Ways[WayId];

		if (!Way.IsBuilding())
		{
			continue;
		}

		if (WaysUsedByRelations.Contains(WayId))
		{
			continue;
		}

		if (!Settings.bGenerateBuildingParts && Way.HasTag(TEXT("building:part"))
			&& !Way.HasTag(TEXT("building")))
		{
			continue;
		}

		// Ein Gebaeudeumriss muss ein geschlossener Ring sein.
		if (!Way.IsClosed())
		{
			++Report.IncompleteCount;
			continue;
		}

		int32 MissingNodes = 0;
		if (!DataSet.ResolveWayCoordinates(Way, Coords, MissingNodes) || MissingNodes > 0)
		{
			++Report.IncompleteCount;
			continue;
		}

		TArray<FVector2D> Ring;
		Ring.Reserve(Coords.Num());
		for (const FGeoCoordinate& Coord : Coords)
		{
			const FVector World = Converter->GeoToUnrealGround(Coord);
			Ring.Add(FVector2D(World.X, World.Y));
		}

		FGeneratedBuilding Building;
		if (BuildSingleBuilding(
			Way.Id, /*bFromRelation=*/false, Way.Tags,
			Ring, TArray<TArray<FVector2D>>(), HeightSampler, Settings, Building, OutMeshData))
		{
			if (Building.bIsLandmark)
			{
				++Report.LandmarkCount;
			}
			OutBuildings.Add(MoveTemp(Building));
		}
		else
		{
			// Unterscheiden, ob zu klein oder degeneriert - das ist fuer die
			// Beurteilung der Datenqualitaet relevant.
			TArray<FVector2D> Cleaned = Ring;
			FPolygonUtils::RemoveDuplicatePoints(Cleaned);
			const double AreaSqm = FPolygonUtils::ComputeArea(Cleaned) * SqCmToSqM;

			if (Building.RegionType == ECityRegionType::Water)
			{
				// Im Wasser verworfen (Regionen-Regel) - kein Datenqualitaetsfehler.
				++Report.WaterRemovedCount;
			}
			else if (AreaSqm < Settings.MinFootprintAreaSqm)
			{
				++Report.TooSmallCount;
			}
			else
			{
				++Report.DegenerateCount;
			}
		}
	}

	Report.bSuccess = true;
	Report.BuildingCount = OutBuildings.Num();
	Report.DurationSeconds = FPlatformTime::Seconds() - StartTime;

	if (OutMeshData)
	{
		Report.VertexCount = OutMeshData->GetTotalVertexCount();
		Report.TriangleCount = OutMeshData->GetTotalTriangleCount();
	}

	if (Report.BuildingCount == 0)
	{
		Report.bSuccess = false;
		Report.ErrorMessage = TEXT("Keine Gebaeude erzeugt. Enthaelt der Datensatz building=*-Ways?");
		UE_LOG(LogWbBuildings, Error, TEXT("%s"), *Report.ErrorMessage);
	}
	else
	{
		UE_LOG(LogWbBuildings, Log, TEXT("%s"), *Report.ToString());
	}

	return Report;
}

bool UBuildingGenerator::BuildSingleBuilding(
	int64 SourceId,
	bool bFromRelation,
	const TMap<FName, FString>& Tags,
	const TArray<FVector2D>& OuterRing,
	const TArray<TArray<FVector2D>>& Holes,
	const IHeightSampler* HeightSampler,
	const FBuildingGenerationSettings& Settings,
	FGeneratedBuilding& OutBuilding,
	FBuildingMeshData* OutMeshData) const
{
	TArray<FVector2D> Ring = OuterRing;
	FPolygonUtils::RemoveDuplicatePoints(Ring);

	if (Ring.Num() < 3)
	{
		return false;
	}

	// Kollineare Stuetzpunkte entfernen: OSM-Gebaeudeumrisse haben oft Punkte
	// mitten auf einer geraden Wand (Ueberbleibsel der Digitalisierung). Sie
	// erzeugen zusaetzliche Wandsegmente ohne sichtbaren Nutzen.
	FPolygonUtils::RemoveCollinearPoints(Ring, 0.5);

	if (Ring.Num() < 3)
	{
		return false;
	}

	const double AreaSqCm = FPolygonUtils::ComputeArea(Ring);
	const double AreaSqm = AreaSqCm * SqCmToSqM;

	if (AreaSqm < Settings.MinFootprintAreaSqm)
	{
		return false;
	}

	// Aussenring immer CCW: die Wandnormalen werden aus der Umlaufrichtung
	// abgeleitet, und bei CW-Ring zeigten sie ins Gebaeudeinnere.
	FPolygonUtils::EnsureWinding(Ring, /*bCounterClockwise=*/true);

	// -- Attribute ----------------------------------------------------------

	// Die Tag-Auswertung braucht ein FOSMWay; fuer Relationen wird ein
	// temporaeres Objekt mit denselben Tags gebaut, damit beide Pfade exakt
	// dieselbe Logik durchlaufen.
	FOSMWay TagCarrier;
	TagCarrier.Id = SourceId;
	TagCarrier.Tags = Tags;

	FString BuildingTypeValue = TagCarrier.GetTag(TEXT("building"));
	if (BuildingTypeValue.IsEmpty())
	{
		BuildingTypeValue = TagCarrier.GetTag(TEXT("building:part"));
	}

	OutBuilding.SourceId = SourceId;
	OutBuilding.bFromRelation = bFromRelation;
	OutBuilding.BuildingType = FOSMTagParser::ParseBuildingType(BuildingTypeValue);
	OutBuilding.RoofShape = FOSMTagParser::ParseRoofShape(TagCarrier.GetTag(TEXT("roof:shape")));
	OutBuilding.BuildingName = TagCarrier.GetTag(TEXT("name"));
	OutBuilding.FootprintAreaSqm = AreaSqm;

	const FString Street = TagCarrier.GetTag(TEXT("addr:street"));
	const FString HouseNumber = TagCarrier.GetTag(TEXT("addr:housenumber"));
	if (!Street.IsEmpty())
	{
		OutBuilding.Address = HouseNumber.IsEmpty()
			? Street
			: FString::Printf(TEXT("%s %s"), *Street, *HouseNumber);
	}

	// Gebaeude-Schwerpunkt: einmal berechnet, fuer die Region-Zuordnung unten
	// und als Metadatum (OutBuilding.Centroid) weiter unten.
	const FVector2D Centroid2D = FPolygonUtils::ComputeCentroid(Ring);

	// -- Region (WorldClaw-Schritt 3: regionale Planung VOR der Objekt-
	// Erzeugung). Der BuildingGenerator erhaelt die Regionen-Karte aus der
	// Pipeline; jedes Gebaeude wird beim Bauen seiner Region zugeordnet.
	// Dadurch kann regional entschieden werden, BEVOR Geometrie entsteht:
	// kein Haus mitten im See (auch nicht im Mesh), Hoehen-/Fassaden-
	// Feintuning je Region.
	ECityRegionType RegionType = ECityRegionType::Other;
	FString RegionName;
	if (Settings.bApplyRegionRules && Settings.RegionMap.Num() > 0)
	{
		if (const FWiesbadenRegion* Region = UWiesbadenRegionGenerator::FindRegionAt(Settings.RegionMap, Centroid2D))
		{
			RegionType = Region->Type;
			RegionName = Region->Name;
			OutBuilding.RegionType = RegionType;
			OutBuilding.RegionName = RegionName;

			if (RegionType == ECityRegionType::Water && Settings.bRemoveWaterBuildings)
			{
				// Kein Haus im See: verwerfen, bevor Mesh oder Metadaten entstehen.
				return false;
			}
		}
	}

	// Hoehe aus OSM-Tags/Defaults, skaliert um den City-Prompt-Faktor
	// (BuildingHeightScale: "niedrig" 0.6, "hoch" 1.4, "Hochhaus" 2.0) und
	// den Regional-Faktor (Industrie hoeher, Gruen niedriger). Die
	// Geschosszahl unten leitet sich daraus ab, bleibt also konsistent.
	double HeightMeters = FOSMTagParser::ResolveBuildingHeightMeters(
		TagCarrier, OutBuilding.BuildingType, Settings.MetersPerLevel)
		* Settings.HeightScale * GetRegionalHeightScale(RegionType);

	// Ohne getaggte Hoehe streuen. 85 % der ALKIS-Grundrisse sind
	// building=residential ohne Geschosszahl und bekamen bisher alle exakt
	// denselben Typ-Default - die Stadt sah dadurch aus wie eine Mauer.
	// Getaggte Hoehen bleiben unberuehrt, die sind belastbar.
	const bool bHasTaggedHeight =
		TagCarrier.HasTag(TEXT("height"))
		|| TagCarrier.HasTag(TEXT("building:height"))
		|| TagCarrier.HasTag(TEXT("building:levels"));

	if (!bHasTaggedHeight)
	{
		HeightMeters = VaryDefaultHeightMeters(
			HeightMeters, AreaSqm, Centroid2D.Size(), SourceId, Settings.MetersPerLevel);
	}

	OutBuilding.HeightCm = HeightMeters * MetersToCm;

	int32 Levels = 0;
	if (!FOSMTagParser::ParseBuildingLevels(TagCarrier, Levels) || Levels < 1)
	{
		Levels = FMath::Max(1, FMath::RoundToInt32(HeightMeters / Settings.MetersPerLevel));
	}
	OutBuilding.LevelCount = Levels;

	// Adress-Override fuer die realen Gebaeude an der Platter Strasse (Fallback):
	// 140 -> 12 Geschosse, 142 -> Garage/Flachdach/1, 144/146 -> 6. Greift NUR,
	// wenn keine amtliche Hoehe getaggt ist. Liegt eine explizite height/
	// building:height vor (der LoD2-Injektor setzt sie aus den Hessen-Daten),
	// gewinnen die echten Gebaeudehoehen und ersetzen das hartcodierte Raten -
	// das loest zugleich Suffix-/Duplikat-Footprints (140a, 142a/b/c) per alkis:id.
	const bool bHasReliableHeight =
		TagCarrier.HasTag(TEXT("height")) || TagCarrier.HasTag(TEXT("building:height"));
	ApplyPlatterAddressOverride(OutBuilding, Settings.MetersPerLevel, bHasReliableHeight);

	OutBuilding.bIsLandmark = IsLandmark(OutBuilding.BuildingName, OutBuilding.BuildingType, AreaSqm);

	// City-Prompt-Override: prompt-erkannte Landmarken markieren zusaetzlich
	// zur OSM-basierten Erkennung (IsLandmark oben). Die Markierung traegt
	// auch den FacadeOverrideKey weiter unten (Landmarke vor Stil).
	if (Settings.bApplyPromptOverrides
		&& !Settings.PromptLandmarkNames.IsEmpty()
		&& !ResolvePromptLandmarkKey(OutBuilding.BuildingName, Settings.PromptLandmarkNames).IsEmpty())
	{
		OutBuilding.bIsLandmark = true;
	}

	// -- Hoehenlage ---------------------------------------------------------

	// Das Gebaeude wird auf die NIEDRIGSTE Terrainhoehe seines Umrisses
	// gesetzt, nicht auf die mittlere: am Hang (Neroberg, Sonnenberg) wuerde
	// ein Gebaeude auf mittlerer Hoehe mit der bergseitigen Ecke im Boden
	// stecken und talseitig schweben. Der Sockel verschwindet stattdessen im
	// Hang - genau wie beim echten Bauwerk.
	double MinTerrainZ = TNumericLimits<double>::Max();
	double MaxTerrainZ = -TNumericLimits<double>::Max();

	if (HeightSampler && HeightSampler->HasValidData())
	{
		for (const FVector2D& Point : Ring)
		{
			const double Z = HeightSampler->SampleHeightCm(Point);
			MinTerrainZ = FMath::Min(MinTerrainZ, Z);
			MaxTerrainZ = FMath::Max(MaxTerrainZ, Z);
		}
	}
	else
	{
		MinTerrainZ = 0.0;
		MaxTerrainZ = 0.0;
	}

	const double BaseZ = MinTerrainZ - Settings.FoundationDepthCm;

	// min_height beschreibt schwebende Gebaeudeteile (Durchfahrten, Arkaden -
	// in der Wilhelmstrasse mehrfach vorhanden).
	double MinHeightMeters = 0.0;
	FOSMTagParser::ParseLengthMeters(TagCarrier.GetTag(TEXT("min_height")), MinHeightMeters);

	const double WallBaseZ = BaseZ + MinHeightMeters * MetersToCm;
	const double TopZ = MinTerrainZ + OutBuilding.HeightCm;

	if (TopZ <= WallBaseZ + 1.0)
	{
		return false;
	}

	OutBuilding.Centroid = FVector(Centroid2D.X, Centroid2D.Y, MinTerrainZ);

	OutBuilding.Bounds = FBox(ForceInit);
	for (const FVector2D& Point : Ring)
	{
		OutBuilding.Bounds += FVector(Point.X, Point.Y, BaseZ);
		OutBuilding.Bounds += FVector(Point.X, Point.Y, TopZ + OutBuilding.HeightCm * 0.5);
	}

	// Gedrehte Grundriss-Box fuer die Kollision. Die achsparallele Bounds
	// oben bleibt fuer Streaming und Auswahl erhalten.
	{
		FVector2D BoxCenter = FVector2D::ZeroVector;
		FVector2D BoxExtent = FVector2D::ZeroVector;
		double BoxYaw = 0.0;
		if (FPolygonUtils::ComputeMinimumAreaBox2D(Ring, BoxCenter, BoxExtent, BoxYaw))
		{
			OutBuilding.FootprintCenterCm = BoxCenter;
			OutBuilding.FootprintExtentCm = BoxExtent;
			OutBuilding.FootprintYawDegrees = static_cast<float>(FMath::RadiansToDegrees(BoxYaw));
		}
	}

	if (!OutMeshData)
	{
		// Nur Metadaten gefragt (dedizierter Server): Geometrie ueberspringen.
		return true;
	}

	// -- Geometrie ----------------------------------------------------------

	// Fassaden-Variante: unveraendert ueber SelectMaterialVariant (0-5). Ein
	// Per-Adress-Override (z. B. Mainzer Strasse 129) greift NICHT in die
	// Varianten-Pipeline ein, sondern gibt dem Gebaeude nur einen eigenen
	// Mesh-Abschnitt (FacadeOverrideKey), dem die Render-Seite ein eigenes
	// Material zuweist.
	const int32 MaterialVariant = SelectMaterialVariant(Tags, OutBuilding.BuildingType, SourceId);
	FString FacadeOverrideKey = ResolveFacadeOverrideKey(
		OutBuilding.Address, Settings.FacadeOverrideAddresses);

	// City-Prompt-Overrides (Adresse hat Vorrang): Landmarke (OSM-Name matcht
	// eine prompt-erkannte Landmarke) > Stil (deterministische gewichtete
	// Auswahl je Gebaeude, Seed = OSM-Id). Die Varianten-Pipeline bleibt
	// unberuehrt - MaterialVariant kommt weiter aus SelectMaterialVariant,
	// der Key trennt nur den Abschnitt fuer die Render-Seite
	// (PromptFacadeMaterials).
	if (FacadeOverrideKey.IsEmpty() && Settings.bApplyPromptOverrides)
	{
		FacadeOverrideKey = ResolvePromptLandmarkKey(
			OutBuilding.BuildingName, Settings.PromptLandmarkNames);
		if (FacadeOverrideKey.IsEmpty())
		{
			FacadeOverrideKey = ResolvePromptStyleKey(
				Settings.PromptFacadeVariantWeights,
				FString::Printf(TEXT("%lld"), static_cast<long long>(SourceId)));
		}
	}

	// Regionales Fassaden-Feintuning (WorldClaw C_object: Stil je Region):
	// Industrie- und Gewerbe-Regionen bekommen einen eigenen Abschnitt, dessen
	// Material die Render-Seite ueber die Key->Material-Maps zuweisen kann.
	if (FacadeOverrideKey.IsEmpty() && RegionType != ECityRegionType::Other)
	{
		FacadeOverrideKey = GetRegionalFacadeKey(RegionType);
	}

	// Dachhoehe: bei geneigten Daechern zusaetzlich zur Traufhoehe. Die
	// getaggte height ist in OSM die Gesamthoehe inklusive Dach, daher wird
	// die Traufe entsprechend abgesenkt.
	double RoofHeightCm = 0.0;

	if (Settings.bGenerateRoofs && OutBuilding.RoofShape != EOSMRoofShape::Flat)
	{
		double TaggedRoofHeight = 0.0;
		if (FOSMTagParser::ParseLengthMeters(TagCarrier.GetTag(TEXT("roof:height")), TaggedRoofHeight))
		{
			RoofHeightCm = TaggedRoofHeight * MetersToCm;
		}
		else
		{
			// Aus der Dachneigung und der halben Gebaeudetiefe schaetzen.
			const FBox2D Bounds2D = FPolygonUtils::ComputeBounds2D(Ring);
			const double ShorterSide = FMath::Min(
				Bounds2D.Max.X - Bounds2D.Min.X,
				Bounds2D.Max.Y - Bounds2D.Min.Y);

			RoofHeightCm = (ShorterSide * 0.5)
				* FMath::Tan(FMath::DegreesToRadians(Settings.GabledRoofAngleDegrees));
		}

		// Das Dach darf nicht mehr als 45 % der Gesamthoehe einnehmen, sonst
		// entstehen bei langgestreckten Baukoerpern absurde Spitzdaecher.
		RoofHeightCm = FMath::Clamp(RoofHeightCm, 50.0, (TopZ - WallBaseZ) * 0.45);
	}

	const double EavesZ = TopZ - RoofHeightCm;

	BuildWalls(Ring, WallBaseZ, EavesZ, OutBuilding.LevelCount, MaterialVariant, FacadeOverrideKey, Settings, *OutMeshData);

	// Innenhofwaende: ein Hof hat ebenfalls Fassaden. Die Umlaufrichtung ist
	// gegenlaeufig, wodurch die Normalen automatisch in den Hof zeigen.
	for (const TArray<FVector2D>& RawHole : Holes)
	{
		TArray<FVector2D> Hole = RawHole;
		FPolygonUtils::RemoveDuplicatePoints(Hole);
		if (Hole.Num() < 3)
		{
			continue;
		}
		FPolygonUtils::EnsureWinding(Hole, /*bCounterClockwise=*/false);
		BuildWalls(Hole, WallBaseZ, EavesZ, OutBuilding.LevelCount, MaterialVariant, FacadeOverrideKey, Settings, *OutMeshData);
	}

	if (Settings.bGenerateRoofs)
	{
		BuildRoof(Ring, Holes, EavesZ, OutBuilding.RoofShape, RoofHeightCm, MaterialVariant, SourceId, FacadeOverrideKey, Settings.RoofOverhangMeters, *OutMeshData);
	}

	return true;
}

void UBuildingGenerator::BuildWalls(
	const TArray<FVector2D>& Ring,
	double BaseZ,
	double TopZ,
	int32 LevelCount,
	int32 MaterialVariant,
	const FString& FacadeOverrideKey,
	const FBuildingGenerationSettings& Settings,
	FBuildingMeshData& OutMeshData) const
{
	if (Ring.Num() < 3 || TopZ <= BaseZ)
	{
		return;
	}

	const double TotalHeight = TopZ - BaseZ;
	const double LevelHeight = TotalHeight / FMath::Max(1, LevelCount);

	// Erdgeschoss separat: Schaufenster und Ladeneingaenge brauchen ein
	// anderes Material als die Obergeschosse. Ohne diese Trennung haben
	// Geschaeftshaeuser in der Kirchgasse Wohnungsfenster im Erdgeschoss.
	const bool bSplitGroundFloor = Settings.bSeparateGroundFloor && LevelCount > 1;
	const double GroundFloorTopZ = bSplitGroundFloor ? (BaseZ + LevelHeight) : BaseZ;

	// Erst BEIDE Sections anfordern (jede kann OutMeshData.Sections neu allozieren),
	// DANN die Referenzen binden. Frueher hielt WallSection eine Referenz, die das
	// GroundFloor-FindOrAddSection beim Realloc entwertete; das anschliessende
	// Schreiben durch die haengende Referenz (AddQuad -> Section.Vertices.Add)
	// korrumpierte den Heap (per Stomp-Allocator dort lokalisiert; der Absturz trat
	// erst spaeter in RemoveDuplicatePoints auf). Im Rest von BuildWalls waechst
	// OutMeshData.Sections nicht mehr, daher bleiben die Referenzen ab hier gueltig.
	const int32 WallSectionIndex = FindOrAddSection(
		OutMeshData, EBuildingMeshChannel::Wall, MaterialVariant, FacadeOverrideKey);
	const int32 GroundSectionIndex = bSplitGroundFloor
		? FindOrAddSection(OutMeshData, EBuildingMeshChannel::GroundFloor, MaterialVariant, FacadeOverrideKey)
		: INDEX_NONE;

	FBuildingMeshSection& WallSection = OutMeshData.Sections[WallSectionIndex];
	FBuildingMeshSection* GroundSection = (GroundSectionIndex != INDEX_NONE)
		? &OutMeshData.Sections[GroundSectionIndex]
		: nullptr;

	const int32 PointCount = Ring.Num();

	for (int32 Index = 0; Index < PointCount; ++Index)
	{
		const FVector2D& A = Ring[Index];
		const FVector2D& B = Ring[(Index + 1) % PointCount];

		const FVector2D Edge = B - A;
		const double EdgeLength = Edge.Size();

		if (EdgeLength < FPolygonUtils::GeometryEpsilon)
		{
			continue;
		}

		const FVector2D EdgeDirection = Edge / EdgeLength;

		// Aussennormale: bei CCW-Umlauf zeigt die Rechtsnormale nach aussen.
		const FVector2D Normal2D = -FPolygonUtils::GetLeftNormal(EdgeDirection);
		const FVector Normal(Normal2D.X, Normal2D.Y, 0.0);
		const FProcMeshTangent Tangent(EdgeDirection.X, EdgeDirection.Y, 0.0f);

		// U in Metern entlang der Wand, V in Geschossen. Das Fassadenmaterial
		// kachelt damit exakt geschossweise.
		const double UMeters = EdgeLength / MetersToCm;

		auto AddQuad = [&](FBuildingMeshSection& Section, double LowerZ, double UpperZ, double VStart, double VEnd)
		{
			if (UpperZ <= LowerZ + 0.1)
			{
				return;
			}

			const int32 Base = Section.Vertices.Num();

			Section.Vertices.Add(FVector(A.X, A.Y, LowerZ));
			Section.Vertices.Add(FVector(B.X, B.Y, LowerZ));
			Section.Vertices.Add(FVector(A.X, A.Y, UpperZ));
			Section.Vertices.Add(FVector(B.X, B.Y, UpperZ));

			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				Section.Normals.Add(Normal);
				Section.Tangents.Add(Tangent);
				// Vertexfarbe: R = Hoehe ueber Grund (fuer Schmutzverlauf am
				// Sockel), G = Geschosszahl normiert, B = frei.
				const bool bUpper = Corner >= 2;
				const uint8 HeightMask = bUpper ? 255 : 0;
				Section.VertexColors.Add(FColor(HeightMask, static_cast<uint8>(FMath::Min(LevelCount * 25, 255)), 128, 255));
			}

			Section.UVs.Add(FVector2D(0.0, VStart));
			Section.UVs.Add(FVector2D(UMeters, VStart));
			Section.UVs.Add(FVector2D(0.0, VEnd));
			Section.UVs.Add(FVector2D(UMeters, VEnd));

			// Wicklung gegen den Uhrzeigersinn von aussen betrachtet.
			Section.Triangles.Add(Base + 0);
			Section.Triangles.Add(Base + 2);
			Section.Triangles.Add(Base + 1);
			Section.Triangles.Add(Base + 1);
			Section.Triangles.Add(Base + 2);
			Section.Triangles.Add(Base + 3);
		};

		if (bSplitGroundFloor && GroundSection)
		{
			AddQuad(*GroundSection, BaseZ, GroundFloorTopZ, 0.0, 1.0);
			AddQuad(WallSection, GroundFloorTopZ, TopZ, 0.0, static_cast<double>(LevelCount - 1));
		}
		else
		{
			AddQuad(WallSection, BaseZ, TopZ, 0.0, static_cast<double>(LevelCount));
		}
	}
}

void UBuildingGenerator::BuildRoof(
	const TArray<FVector2D>& OuterRing,
	const TArray<TArray<FVector2D>>& Holes,
	double EavesZ,
	EOSMRoofShape Shape,
	double RoofHeightCm,
	int32 MaterialVariant,
	int64 SourceId,
	const FString& FacadeOverrideKey,
	double RoofOverhangMeters,
	FBuildingMeshData& OutMeshData) const
{
	if (OuterRing.Num() < 3)
	{
		return;
	}

	const int32 SectionIndex = FindOrAddSection(
		OutMeshData, EBuildingMeshChannel::Roof, MaterialVariant, FacadeOverrideKey);
	FBuildingMeshSection& Section = OutMeshData.Sections[SectionIndex];

	// Dachdeckung als Vertexfarbe: R traegt die Deckung (0/85/170 =
	// Terrakotta/Schiefer/Zink), damit das Dachmaterial EINE typgerechte
	// Deckung je Gebaeude liest statt einer Weltregion zu wuerfeln. Der Wert
	// R=255 (Weiss) bleibt bewusst UNGENUTZT und bedeutet im Material "Legacy" -
	// so bleiben aeltere Bakes (Dach-Verts = FColor::White) auf der alten
	// Regionswahl und regredieren nicht.
	//
	// G traegt eine deterministische Tonstufe je Gebaeude-Id: das Material
	// verschiebt damit die Deckungsfarbe leicht (+-~9 %), sodass eine Reihe
	// gleichtypiger Haeuser nicht identisch wirkt - die DeckungsART (R) bleibt
	// unberuehrt. Legacy-Bakes tragen G=255; das Material wertet den Ton nur im
	// Nicht-Legacy-Pfad aus, ein Ton-Byte von 255 in einem neuen Bake bleibt
	// also folgenlos.
	const int32 CoveringIndex = RoofCoveringIndex(MaterialVariant, Shape);
	const FColor RoofVertexColor(
		static_cast<uint8>(CoveringIndex * 85), RoofToneByte(SourceId), 255, 255);

	// Dach-UVs GEBAeUDE-LOKAL statt weltbezogen. Die georeferenzierten
	// Weltkoordinaten sind in Wiesbaden riesig (Tausende Meter). Als per-Vertex-
	// float32 gespeichert und ueber das Dreieck interpoliert verlieren so grosse
	// UVs die Praezision -> die Dachtextur kollabiert zu Grau (das frueher als
	// "fehlende Dach-UVs" beobachtete Symptom, das nur die material-seitige
	// Weltraum-Projektion umging). Relativ zum Gebaeude-Schwerpunkt bleiben die
	// Werte klein und praezise; die Kachelgroesse (1 Kachel/m via /MetersToCm)
	// bleibt ueber alle Gebaeude gleich - nur die Kachel-PHASE startet je Dach
	// neu, was fuer getrennte Daecher unerheblich ist.
	const FVector2D UVOrigin = FPolygonUtils::ComputeCentroid(OuterRing);
	auto RoofUV = [&](double WorldX, double WorldY) -> FVector2D
	{
		return FVector2D((WorldX - UVOrigin.X) / MetersToCm, (WorldY - UVOrigin.Y) / MetersToCm);
	};

	// -- Flachdach und Fallback --------------------------------------------

	auto BuildFlatCap = [&](double CapZ)
	{
		TArray<FVector2D> Vertices2D;
		TArray<int32> Indices;

		if (Holes.Num() > 0)
		{
			if (!FPolygonUtils::TriangulatePolygonWithHoles(OuterRing, Holes, Vertices2D, Indices))
			{
				return false;
			}
		}
		else
		{
			Vertices2D = OuterRing;
			if (!FPolygonUtils::TriangulatePolygon(Vertices2D, Indices))
			{
				return false;
			}
		}

		const int32 Base = Section.Vertices.Num();

		for (const FVector2D& Point : Vertices2D)
		{
			Section.Vertices.Add(FVector(Point.X, Point.Y, CapZ));
			Section.Normals.Add(FVector::UpVector);
			Section.UVs.Add(RoofUV(Point.X, Point.Y));
			Section.VertexColors.Add(RoofVertexColor);
			Section.Tangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));
		}

		// Wicklung umkehren: die Triangulierung liefert die Dachflaeche mit der
		// Vorderseite nach UNTEN, sie waere von oben weggecullt. Belegt durch den
		// Test mit beidseitigem Dachmaterial - dabei wurden aus hohlen Schalen
		// schlagartig geschlossene Baukoerper.
		Section.Triangles.Reserve(Section.Triangles.Num() + Indices.Num());
		for (int32 Tri = 0; Tri + 2 < Indices.Num(); Tri += 3)
		{
			Section.Triangles.Add(Indices[Tri + 0] + Base);
			Section.Triangles.Add(Indices[Tri + 2] + Base);
			Section.Triangles.Add(Indices[Tri + 1] + Base);
		}

		return true;
	};

	if (Shape == EOSMRoofShape::Flat || RoofHeightCm <= 1.0)
	{
		BuildFlatCap(EavesZ);
		return;
	}

	const double RidgeZ = EavesZ + RoofHeightCm;
	const FVector2D Centroid = FPolygonUtils::ComputeCentroid(OuterRing);

	// -- Zeltdach und Kuppel: Faecher zum Scheitelpunkt ---------------------

	if (Shape == EOSMRoofShape::Pyramidal || Shape == EOSMRoofShape::Dome)
	{
		const int32 Base = Section.Vertices.Num();
		const int32 PointCount = OuterRing.Num();

		for (int32 Index = 0; Index < PointCount; ++Index)
		{
			const FVector2D& A = OuterRing[Index];
			const FVector2D& B = OuterRing[(Index + 1) % PointCount];

			const FVector VA(A.X, A.Y, EavesZ);
			const FVector VB(B.X, B.Y, EavesZ);
			const FVector VApex(Centroid.X, Centroid.Y, RidgeZ);

			const FVector FaceNormal = FVector::CrossProduct(VB - VA, VApex - VA).GetSafeNormal();

			const int32 TriangleBase = Section.Vertices.Num();

			Section.Vertices.Add(VA);
			Section.Vertices.Add(VB);
			Section.Vertices.Add(VApex);

			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				Section.Normals.Add(FaceNormal);
				Section.VertexColors.Add(RoofVertexColor);
				Section.Tangents.Add(FProcMeshTangent((VB - VA).GetSafeNormal(), false));
			}

			Section.UVs.Add(RoofUV(A.X, A.Y));
			Section.UVs.Add(RoofUV(B.X, B.Y));
			Section.UVs.Add(RoofUV(Centroid.X, Centroid.Y));

			// Vorderseite nach oben (siehe Dach-Wicklung oben).
			Section.Triangles.Add(TriangleBase + 0);
			Section.Triangles.Add(TriangleBase + 1);
			Section.Triangles.Add(TriangleBase + 2);
		}

		(void)Base;
		return;
	}

	// -- Sattel-, Walm- und Pultdach ---------------------------------------
	//
	// Alle drei werden ueber eine Firstlinie entlang der laengeren Achse der
	// umschliessenden Box gebildet. Das ist eine Naeherung: ein exaktes
	// Straight-Skeleton wuerde auch L-foermige Grundrisse korrekt bedachen,
	// kostet aber ein Vielfaches an Rechenzeit und ist bei einem Dach, das
	// aus Strassenperspektive kaum sichtbar ist, nicht gerechtfertigt.
	// Fuer die Landmarken wird ohnehin ein handmodelliertes Asset gesetzt.

	// First im LOKALEN Rahmen der flaechenminimalen (gedrehten) Box bilden -
	// NICHT der welt-achsenparallelen AABB.
	//
	// ALKIS-Grundrisse sind fast nie zur UTM-Nord/Ost-Achse ausgerichtet. Die
	// AABB-Mittellinie lief dann quer zum Gebaeude: der First stand schief, die
	// Dachflaechen kippten durch die Waende. Die orientierte Box (schon fuer die
	// Kollision berechnet) legt den First entlang der ECHTEN Laengsachse.
	FVector2D BoxCenter = Centroid;
	FVector2D BoxExtent(1.0, 1.0);
	double BoxYaw = 0.0;
	FPolygonUtils::ComputeMinimumAreaBox2D(OuterRing, BoxCenter, BoxExtent, BoxYaw);

	const double CosY = FMath::Cos(BoxYaw);
	const double SinY = FMath::Sin(BoxYaw);
	auto ToLocal = [&](const FVector2D& W) -> FVector2D
	{
		const double dx = W.X - BoxCenter.X;
		const double dy = W.Y - BoxCenter.Y;
		return FVector2D(dx * CosY + dy * SinY, -dx * SinY + dy * CosY);
	};
	auto ToWorld = [&](const FVector2D& L) -> FVector2D
	{
		return FVector2D(
			BoxCenter.X + L.X * CosY - L.Y * SinY,
			BoxCenter.Y + L.X * SinY + L.Y * CosY);
	};

	// First laeuft entlang der laengeren LOKALEN Achse.
	const bool bRidgeAlongLocalX = BoxExtent.X >= BoxExtent.Y;
	const bool bIsSkillion = (Shape == EOSMRoofShape::Skillion);

	// Walmdach: First auf beiden Enden um die halbe Gebaeudebreite eingezogen.
	const double HipInset = (Shape == EOSMRoofShape::Hipped)
		? FMath::Min(BoxExtent.X, BoxExtent.Y)
		: 0.0;

	const int32 PointCount = OuterRing.Num();

	// Dachueberstand: RoofOverhangMeters war bisher toter Code - Daecher sassen
	// buendig auf den Waenden ("aufgesetzte Kartons"). Jeder Traufpunkt wandert
	// entlang seiner gemittelten Aussennormale nach aussen; der First bleibt am
	// Wand-Umriss. (Aeussere Ringe sind CCW erzwungen -> rechte Senkrechte aussen.)
	const double OverhangCm = FMath::Max(0.0, RoofOverhangMeters) * MetersToCm;
	TArray<FVector2D> EaveRing;
	EaveRing.SetNum(PointCount);
	{
		auto EdgeOutN = [](const FVector2D& From, const FVector2D& To) -> FVector2D
		{
			const FVector2D E = (To - From).GetSafeNormal();
			return FVector2D(E.Y, -E.X);
		};
		for (int32 i = 0; i < PointCount; ++i)
		{
			if (OverhangCm <= 0.0)
			{
				EaveRing[i] = OuterRing[i];
				continue;
			}
			const FVector2D& P = OuterRing[i];
			const FVector2D& Prev = OuterRing[(i + PointCount - 1) % PointCount];
			const FVector2D& Nxt = OuterRing[(i + 1) % PointCount];
			const FVector2D N = (EdgeOutN(Prev, P) + EdgeOutN(P, Nxt)).GetSafeNormal();
			EaveRing[i] = P + N * OverhangCm;
		}
	}

	auto ProjectToRidge = [&](const FVector2D& Wpt) -> FVector
	{
		const FVector2D L = ToLocal(Wpt);
		if (bIsSkillion)
		{
			// Pultdach: Hoehe steigt linear ueber die kurze LOKALE Achse.
			const double Alpha = bRidgeAlongLocalX
				? (L.Y + BoxExtent.Y) / FMath::Max(2.0 * BoxExtent.Y, 1.0)
				: (L.X + BoxExtent.X) / FMath::Max(2.0 * BoxExtent.X, 1.0);
			return FVector(Wpt.X, Wpt.Y, FMath::Lerp(EavesZ, RidgeZ, Alpha));
		}

		FVector2D RidgeLocal;
		if (bRidgeAlongLocalX)
		{
			RidgeLocal.X = FMath::Clamp(L.X, -BoxExtent.X + HipInset, BoxExtent.X - HipInset);
			RidgeLocal.Y = 0.0;
		}
		else
		{
			RidgeLocal.X = 0.0;
			RidgeLocal.Y = FMath::Clamp(L.Y, -BoxExtent.Y + HipInset, BoxExtent.Y - HipInset);
		}
		const FVector2D RidgeWorld = ToWorld(RidgeLocal);
		return FVector(RidgeWorld.X, RidgeWorld.Y, RidgeZ);
	};

	for (int32 Index = 0; Index < PointCount; ++Index)
	{
		const int32 NextIdx = (Index + 1) % PointCount;
		const FVector EaveA(EaveRing[Index].X, EaveRing[Index].Y, EavesZ);
		const FVector EaveB(EaveRing[NextIdx].X, EaveRing[NextIdx].Y, EavesZ);
		const FVector RidgeA = ProjectToRidge(OuterRing[Index]);
		const FVector RidgeB = ProjectToRidge(OuterRing[NextIdx]);

		// Giebelende: die First-Endpunkte fallen zusammen -> aus dem Viereck wird
		// EIN senkrechtes Giebeldreieck; das zweite Dreieck waere entartet.
		const bool bGable = FVector::DistSquared(RidgeA, RidgeB) < FMath::Square(5.0);

		// Normale aus der ECHTEN Wicklung (EaveA, EaveB, RidgeA) - NICHT nach oben
		// erzwingen. Passt die Wicklung nicht zur gewuenschten Aussenseite, wird
		// SIE gedreht (nicht die Normale gekippt), sonst cullt der Renderer die
		// Flaeche von der Sichtseite weg = schwarze/unsichtbare Daecher.
		const FVector GeoNormal = FVector::CrossProduct(EaveB - EaveA, RidgeA - EaveA);
		if (GeoNormal.IsNearlyZero())
		{
			continue;   // entartete Kante (z. B. Ueberstand-Kollaps) auslassen
		}

		bool bReverse;
		if (bGable)
		{
			// Giebel: Aussenseite zeigt WAAGERECHT vom Gebaeude-Schwerpunkt weg.
			const FVector2D FaceCtr(
				(EaveA.X + EaveB.X + RidgeA.X) / 3.0,
				(EaveA.Y + EaveB.Y + RidgeA.Y) / 3.0);
			const FVector2D Outward = FaceCtr - Centroid;
			bReverse = (GeoNormal.X * Outward.X + GeoNormal.Y * Outward.Y) < 0.0;
		}
		else
		{
			// Dachflaeche (Schraege): Aussenseite zeigt nach OBEN.
			bReverse = GeoNormal.Z < 0.0;
		}

		FVector FaceNormal = GeoNormal.GetSafeNormal();
		if (bReverse)
		{
			FaceNormal = -FaceNormal;
		}

		const int32 Base = Section.Vertices.Num();
		Section.Vertices.Add(EaveA);
		Section.Vertices.Add(EaveB);
		Section.Vertices.Add(RidgeA);
		Section.Vertices.Add(RidgeB);

		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			Section.Normals.Add(FaceNormal);
			Section.VertexColors.Add(RoofVertexColor);
			Section.Tangents.Add(FProcMeshTangent((EaveB - EaveA).GetSafeNormal(), false));
		}

		// UVs entlang der Dachflaeche: U laeuft an der Traufe, V die echte Schraege
		// hinauf (3D-Laenge). So behaelt der Ziegel auf jeder Neigung seine Groesse.
		const FVector EaveDir = (EaveB - EaveA).GetSafeNormal();
		auto SlopeUV = [&](const FVector& P) -> FVector2D
		{
			const FVector Local = P - EaveA;
			const double U = FVector::DotProduct(Local, EaveDir);
			const double V = (Local - EaveDir * U).Size();
			return FVector2D(U / MetersToCm, V / MetersToCm);
		};
		Section.UVs.Add(SlopeUV(EaveA));
		Section.UVs.Add(SlopeUV(EaveB));
		Section.UVs.Add(SlopeUV(RidgeA));
		Section.UVs.Add(SlopeUV(RidgeB));

		// Dreiecke mit zur Normale passender Wicklung; Giebel nur eines.
		auto AddTri = [&](int32 I0, int32 I1, int32 I2)
		{
			Section.Triangles.Add(Base + I0);
			Section.Triangles.Add(Base + (bReverse ? I2 : I1));
			Section.Triangles.Add(Base + (bReverse ? I1 : I2));
		};
		AddTri(0, 1, 2);
		if (!bGable)
		{
			AddTri(1, 3, 2);
		}
	}
}

bool UBuildingGenerator::AssembleMultipolygon(
	const FOSMRelation& Relation,
	const FOSMDataSet& DataSet,
	const UGeoCoordinateConverter& Converter,
	TArray<TArray<FVector2D>>& OutOuterRings,
	TArray<TArray<FVector2D>>& OutInnerRings) const
{
	OutOuterRings.Reset();
	OutInnerRings.Reset();

	// Way-Fragmente je Rolle sammeln. Ein Multipolygon-Member ohne Rolle gilt
	// nach OSM-Konvention als "outer".
	TArray<TArray<FOSMId>> OuterFragments;
	TArray<TArray<FOSMId>> InnerFragments;

	for (const FOSMRelationMember& Member : Relation.Members)
	{
		if (Member.Type != EOSMMemberType::Way)
		{
			continue;
		}

		const FOSMWay* Way = DataSet.Ways.Find(Member.Ref);
		if (!Way || Way->NodeIds.Num() < 2)
		{
			continue;
		}

		const bool bIsInner = (Member.Role == TEXT("inner"));

		if (bIsInner)
		{
			InnerFragments.Add(Way->NodeIds);
		}
		else
		{
			OuterFragments.Add(Way->NodeIds);
		}
	}

	if (OuterFragments.Num() == 0)
	{
		return false;
	}

	/**
	 * Setzt Way-Fragmente zu geschlossenen Ringen zusammen.
	 * Ein Fragment kann in beliebiger Richtung vorliegen; es wird jeweils das
	 * Fragment angehaengt, dessen Anfang oder Ende auf den aktuellen Ringende-
	 * Knoten passt.
	 */
	auto AssembleRings = [&DataSet, &Converter](
		TArray<TArray<FOSMId>>& Fragments,
		TArray<TArray<FVector2D>>& OutRings)
	{
		TArray<bool> Used;
		Used.Init(false, Fragments.Num());

		for (int32 StartIndex = 0; StartIndex < Fragments.Num(); ++StartIndex)
		{
			if (Used[StartIndex])
			{
				continue;
			}

			Used[StartIndex] = true;
			TArray<FOSMId> RingNodes = Fragments[StartIndex];

			// Solange anfuegen, bis der Ring geschlossen ist oder nichts mehr passt.
			bool bExtended = true;
			while (bExtended && RingNodes.Num() >= 2 && RingNodes[0] != RingNodes.Last())
			{
				bExtended = false;

				for (int32 Index = 0; Index < Fragments.Num(); ++Index)
				{
					if (Used[Index])
					{
						continue;
					}

					TArray<FOSMId>& Candidate = Fragments[Index];
					if (Candidate.Num() < 2)
					{
						continue;
					}

					if (Candidate[0] == RingNodes.Last())
					{
						RingNodes.Append(Candidate.GetData() + 1, Candidate.Num() - 1);
					}
					else if (Candidate.Last() == RingNodes.Last())
					{
						for (int32 NodeIndex = Candidate.Num() - 2; NodeIndex >= 0; --NodeIndex)
						{
							RingNodes.Add(Candidate[NodeIndex]);
						}
					}
					else
					{
						continue;
					}

					Used[Index] = true;
					bExtended = true;
					break;
				}
			}

			// Nur geschlossene Ringe sind verwertbar. Ein offener Ring bedeutet
			// eine fehlerhafte Relation oder ein am BBox-Rand abgeschnittenes
			// Fragment - beides kommt in Extrakten vor.
			if (RingNodes.Num() < 4 || RingNodes[0] != RingNodes.Last())
			{
				continue;
			}

			TArray<FVector2D> Ring;
			Ring.Reserve(RingNodes.Num());
			bool bComplete = true;

			for (const FOSMId NodeId : RingNodes)
			{
				const FOSMNode* Node = DataSet.Nodes.Find(NodeId);
				if (!Node)
				{
					bComplete = false;
					break;
				}
				const FVector World = Converter.GeoToUnrealGround(Node->Location);
				Ring.Add(FVector2D(World.X, World.Y));
			}

			if (bComplete && Ring.Num() >= 4)
			{
				OutRings.Add(MoveTemp(Ring));
			}
		}
	};

	AssembleRings(OuterFragments, OutOuterRings);
	AssembleRings(InnerFragments, OutInnerRings);

	if (OutOuterRings.Num() == 0)
	{
		UE_LOG(LogWbBuildings, Verbose,
			TEXT("Relation %lld: kein geschlossener Aussenring aus %d Fragmenten bildbar."),
			Relation.Id, OuterFragments.Num());
		return false;
	}

	return true;
}
