// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/WiesbadenRegionAssets.h"

#include "WiesbadenReal.h"

#include "GIS/PolygonUtils.h"

namespace
{
	// Jitter-Amplitude je Kategorie (Baeume streuen mehr als Ufer-Objekte).
	constexpr double TreeJitterCm = 300.0;
	constexpr double WaterfrontJitterCm = 80.0;
	constexpr double IndustrialJitterCm = 400.0;

	// FNV-1a-Basis.
	constexpr uint32 FNVOffsetBasis = 2166136261u;
	constexpr uint32 FNVPrime = 16777619u;
}

FString FRegionAssetReport::ToString() const
{
	if (!bSuccess)
	{
		return FString::Printf(TEXT("Regionen-Assets FEHLGESCHLAGEN: %s"), *ErrorMessage);
	}
	return FString::Printf(
		TEXT("Regionen-Assets: %d Regionen, %d Assets (%d Baeume, %d Ufer, %d Industrie), "
			"%d auf Fahrbahnen weggelassen"),
		RegionCount, AssetCount, TreeCount, WaterfrontCount, IndustrialCount,
		SkippedOnRoadCount);
}

uint32 UWiesbadenRegionAssetGenerator::HashString(const FString& Text)
{
	uint32 Hash = FNVOffsetBasis;
	const TArray<TCHAR>& Chars = Text.GetCharArray();
	for (const TCHAR C : Chars)
	{
		if (C == 0)
		{
			continue;
		}
		Hash ^= static_cast<uint32>(C);
		Hash *= FNVPrime;
	}
	return Hash;
}

void UWiesbadenRegionAssetGenerator::GetCategoriesForRegion(
	ECityRegionType Type, TArray<ERegionAssetCategory>& OutCategories)
{
	OutCategories.Reset();
	switch (Type)
	{
	case ECityRegionType::Green:
		OutCategories.Add(ERegionAssetCategory::Tree);
		break;
	case ECityRegionType::Water:
		OutCategories.Add(ERegionAssetCategory::Waterfront);
		break;
	case ECityRegionType::Industrial:
	case ECityRegionType::Commercial:
		OutCategories.Add(ERegionAssetCategory::Industrial);
		break;
	default:
		break;
	}
}

bool UWiesbadenRegionAssetGenerator::IsCategoryEnabled(
	ERegionAssetCategory Category, const FRegionAssetSettings& Settings)
{
	switch (Category)
	{
	case ERegionAssetCategory::Tree: return Settings.bPlaceTrees;
	case ERegionAssetCategory::Waterfront: return Settings.bPlaceWaterfront;
	case ERegionAssetCategory::Industrial: return Settings.bPlaceIndustrial;
	default: return false;
	}
}

double UWiesbadenRegionAssetGenerator::GetSpacingCm(
	ERegionAssetCategory Category, const FRegionAssetSettings& Settings)
{
	switch (Category)
	{
	case ERegionAssetCategory::Tree: return Settings.TreeSpacingCm;
	case ERegionAssetCategory::Waterfront: return Settings.WaterfrontSpacingCm;
	case ERegionAssetCategory::Industrial: return Settings.IndustrialSpacingCm;
	default: return 1000.0;
	}
}

double UWiesbadenRegionAssetGenerator::GetJitterCm(ERegionAssetCategory Category)
{
	switch (Category)
	{
	case ERegionAssetCategory::Tree: return TreeJitterCm;
	case ERegionAssetCategory::Waterfront: return WaterfrontJitterCm;
	case ERegionAssetCategory::Industrial: return IndustrialJitterCm;
	default: return 200.0;
	}
}

void UWiesbadenRegionAssetGenerator::ScatterRegion(
	const FWiesbadenRegion& Region, ERegionAssetCategory Category,
	double SpacingCm, double JitterCm, const IHeightSampler* HeightSampler,
	const FWiesbadenRoadClearance* Clearance, int32& OutSkipped,
	FRegionAssetLayout& OutLayout)
{
	if (Region.Polygon.Num() < 3)
	{
		return;
	}

	// Deterministischer Seed aus Region-Name + Kategorie (stabil ueber Laeufe).
	FRandomStream Stream(HashString(Region.Name + TEXT(":") + FString::FromInt(static_cast<int32>(Category))));

	// Bounds der Region als Gitter-Raster (halber Abstand vom Rand starten).
	const FBox2D Bounds = FPolygonUtils::ComputeBounds2D(Region.Polygon);
	const double Half = SpacingCm * 0.5;

	for (double Gx = Bounds.Min.X + Half; Gx < Bounds.Max.X; Gx += SpacingCm)
	{
		for (double Gy = Bounds.Min.Y + Half; Gy < Bounds.Max.Y; Gy += SpacingCm)
		{
			const FVector2D Point(
				Gx + (Stream.FRand() - 0.5) * JitterCm,
				Gy + (Stream.FRand() - 0.5) * JitterCm);

			if (!FPolygonUtils::IsPointInPolygon(Point, Region.Polygon))
			{
				continue;
			}

			// Fahrbahn freihalten. Ohne diese Pruefung stehen Baeume mitten
			// auf der Strasse - Landnutzungsflaechen ueberlappen Strassen
			// regelmaessig, und das Regionspolygon allein weiss nichts davon.
			if (Clearance && Clearance->IsBlocked(Point))
			{
				++OutSkipped;
				continue;
			}

			FPlacedRegionAsset Asset;
			Asset.Category = Category;
			Asset.RegionName = Region.Name;
			Asset.Location = FVector(Point.X, Point.Y, 0.0);
			if (HeightSampler && HeightSampler->HasValidData())
			{
				Asset.Location.Z = HeightSampler->SampleHeightCm(Point);
			}
			Asset.YawDegrees = Stream.FRandRange(0.0f, 360.0f);
			Asset.Scale = Stream.FRandRange(0.85f, 1.15f);
			OutLayout.Assets.Add(Asset);
		}
	}
}

bool UWiesbadenRegionAssetGenerator::IsTreeLinedStreet(EOSMHighwayType Type)
{
	switch (Type)
	{
	case EOSMHighwayType::Primary:
	case EOSMHighwayType::PrimaryLink:
	case EOSMHighwayType::Secondary:
	case EOSMHighwayType::SecondaryLink:
	case EOSMHighwayType::Tertiary:
	case EOSMHighwayType::TertiaryLink:
	case EOSMHighwayType::Unclassified:
	case EOSMHighwayType::Residential:
	case EOSMHighwayType::LivingStreet:
		return true;
	default:
		// Autobahn/Kraftfahrstrasse (+Auffahrten), Erschliessungswege,
		// Fuss-/Radwege, Pfade, Treppen, Wirtschaftswege, None -> keine Baeume.
		return false;
	}
}

void UWiesbadenRegionAssetGenerator::ScatterStreetTrees(
	const FRoadNetwork& Network, const IHeightSampler* HeightSampler,
	const FWiesbadenRoadClearance* Clearance, const FRegionAssetSettings& Settings,
	int32& OutSkipped, FRegionAssetLayout& OutLayout)
{
	const double Spacing = FMath::Max(300.0, Settings.StreetTreeSpacingCm);

	int32 SegIndex = 0;
	for (const FRoadSegment& Seg : Network.Segments)
	{
		++SegIndex;
		if (!IsTreeLinedStreet(Seg.HighwayType))
		{
			continue;
		}
		// An Kreuzungen gekuerzte Mittellinie -> Baeume blockieren keine Knoten.
		const TArray<FVector>& Line = Seg.TrimmedCenterline.Num() >= 2
			? Seg.TrimmedCenterline : Seg.Centerline;
		if (Line.Num() < 2)
		{
			continue;
		}
		const double LateralCm = Seg.CarriagewayWidthCm * 0.5 + Settings.StreetTreeVergeOffsetCm;

		// Deterministischer Seed je Segment (stabil ueber Laeufe).
		FRandomStream Stream(static_cast<int32>(GetTypeHash(SegIndex) ^ 0x57B0357Eu));

		// Kontinuierlicher Weg entlang der Polylinie: NextAt ist die Bogenlaenge
		// des naechsten Baums, Accum die Bogenlaenge am aktuellen Stuetzpunkt.
		double NextAt = Spacing * 0.5;
		double Accum = 0.0;
		for (int32 i = 0; i + 1 < Line.Num(); ++i)
		{
			FVector Dir = Line[i + 1] - Line[i];
			Dir.Z = 0.0;
			const double SegLen = Dir.Size();
			if (SegLen < 1.0)
			{
				Accum += SegLen;
				continue;
			}
			Dir /= SegLen;
			const FVector Right = FVector::CrossProduct(Dir, FVector::UpVector).GetSafeNormal();

			while (NextAt <= Accum + SegLen)
			{
				const double LocalS = NextAt - Accum;
				const FVector Base = Line[i] + Dir * LocalS;
				for (int32 Side = 0; Side < 2; ++Side)
				{
					const FVector Off = (Side == 0 ? Right : -Right) * LateralCm;
					const FVector2D Pt(Base.X + Off.X, Base.Y + Off.Y);
					// Fahrbahn-Freihaltung als Sicherheitsnetz (Versatz sitzt ohnehin
					// auf dem Gehweg/der Verge hinter dem Bordstein).
					if (Clearance && Clearance->IsBlocked(Pt))
					{
						++OutSkipped;
						continue;
					}
					FPlacedRegionAsset Asset;
					Asset.Category = ERegionAssetCategory::Tree;
					Asset.RegionName = TEXT("StreetTree");
					Asset.Location = FVector(Pt.X, Pt.Y, 0.0);
					if (HeightSampler && HeightSampler->HasValidData())
					{
						Asset.Location.Z = HeightSampler->SampleHeightCm(Pt);
					}
					Asset.YawDegrees = Stream.FRandRange(0.0f, 360.0f);
					Asset.Scale = Stream.FRandRange(0.85f, 1.15f);
					OutLayout.Assets.Add(Asset);
				}
				NextAt += Spacing;
			}
			Accum += SegLen;
		}
	}
}

FRegionAssetReport UWiesbadenRegionAssetGenerator::Generate(
	const TArray<FWiesbadenRegion>& Regions,
	const IHeightSampler* HeightSampler,
	const FRegionAssetSettings& Settings,
	FRegionAssetLayout& OutLayout)
{
	return GenerateInternal(Regions, HeightSampler, Settings, nullptr, OutLayout);
}

FRegionAssetReport UWiesbadenRegionAssetGenerator::GenerateClearOfRoads(
	const TArray<FWiesbadenRegion>& Regions,
	const IHeightSampler* HeightSampler,
	const FRegionAssetSettings& Settings,
	const FRoadNetwork& Network,
	FRegionAssetLayout& OutLayout)
{
	// Zuschlag von 1,5 m auf halbe Fahrbahn plus Gehweg: haelt auch die
	// Kronen aus dem Lichtraum ueber der Fahrbahn.
	FWiesbadenRoadClearance Clearance;
	Clearance.Build(Network, 150.0);

	UE_LOG(LogWbCore, Log,
		TEXT("Fahrbahn-Freihaltung: %d Abschnitte aus %d Segmenten."),
		Clearance.GetSpanCount(), Network.Segments.Num());

	FRegionAssetReport Report = GenerateInternal(Regions, HeightSampler, Settings, &Clearance, OutLayout);

	// Strassenbaeume entlang der Fahrbahnraender ANHAENGEN: macht die bebauten
	// Strassen baumgesaeumt (Referenz: echtes Wiesbaden). Gleiche Baum-Kategorie
	// -> gleicher Bake-/Spawn-/Stream-Pfad; nur die Positionen kommen aus dem
	// Netz. Nach GenerateInternal (das OutLayout zuruecksetzt), damit sie an die
	// Regions-Baeume anschliessen statt sie zu loeschen.
	if (Settings.bPlaceStreetTrees)
	{
		// EIGENES, enges Freihaltenetz NUR fuer die Fahrbahn (ohne Gehweg): die
		// Strassenbaeume gehoeren auf die Verge/den Gehweg hinter dem Bordstein.
		// Das breite Region-Netz oben schliesst den Gehweg mit ein und wuerde die
		// Verge-Baeume alle wegwerfen; dieses Netz blockt nur die Fahrbahn selbst
		// (Sicherheitsnetz gegen Baeume, die an Kreuzungen ueber eine Querstrasse
		// fielen).
		FWiesbadenRoadClearance StreetClearance;
		StreetClearance.Build(Network, 40.0, /*bIncludeSidewalk=*/false);

		const int32 BeforeCount = OutLayout.Assets.Num();
		int32 StreetSkipped = 0;
		ScatterStreetTrees(Network, HeightSampler, &StreetClearance, Settings, StreetSkipped, OutLayout);
		const int32 Added = OutLayout.Assets.Num() - BeforeCount;
		Report.TreeCount += Added;
		Report.AssetCount = OutLayout.Assets.Num();
		Report.SkippedOnRoadCount += StreetSkipped;
		UE_LOG(LogWbCore, Log,
			TEXT("Strassenbaeume: %d entlang der Fahrbahnraender gesetzt (%d auf Fahrbahn ausgelassen)."),
			Added, StreetSkipped);
	}

	return Report;
}

FRegionAssetReport UWiesbadenRegionAssetGenerator::GenerateInternal(
	const TArray<FWiesbadenRegion>& Regions,
	const IHeightSampler* HeightSampler,
	const FRegionAssetSettings& Settings,
	const FWiesbadenRoadClearance* Clearance,
	FRegionAssetLayout& OutLayout)
{
	FRegionAssetReport Report;
	OutLayout.Reset();

	for (const FWiesbadenRegion& Region : Regions)
	{
		TArray<ERegionAssetCategory> Categories;
		GetCategoriesForRegion(Region.Type, Categories);
		if (Categories.Num() == 0)
		{
			continue;
		}

		++Report.RegionCount;
		for (const ERegionAssetCategory Category : Categories)
		{
			if (!IsCategoryEnabled(Category, Settings))
			{
				continue;
			}
			ScatterRegion(Region, Category, GetSpacingCm(Category, Settings),
				GetJitterCm(Category), HeightSampler, Clearance,
				Report.SkippedOnRoadCount, OutLayout);
		}
	}

	for (const FPlacedRegionAsset& Asset : OutLayout.Assets)
	{
		switch (Asset.Category)
		{
		case ERegionAssetCategory::Tree: ++Report.TreeCount; break;
		case ERegionAssetCategory::Waterfront: ++Report.WaterfrontCount; break;
		case ERegionAssetCategory::Industrial: ++Report.IndustrialCount; break;
		default: break;
		}
	}
	Report.AssetCount = OutLayout.Assets.Num();
	Report.bSuccess = true;

	UE_LOG(LogWbCore, Log, TEXT("%s"), *Report.ToString());
	return Report;
}
