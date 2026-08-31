// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/TerrainGenerator.h"

#include "WiesbadenReal.h"

#include "GIS/GeoCoordinateConverter.h"
#include "GIS/PolygonUtils.h"

namespace
{
	/** Zentimeter pro Meter. */
	constexpr double CmPerMeter = 100.0;

	/**
	 * Setzt eine Vertexhoehe und zaehlt nur tatsaechliche Aenderungen.
	 * Die Einebnung laeuft ueber viele ueberlappende Discs (ein Way-Punkt alle
	 * 5 m); ohne diese Pruefung wuerde der Zaehler jede Zelle mehrfach melden.
	 */
	bool SetHeightIfChanged(FTerrainTile& Tile, int32 X, int32 Y, float HeightCm)
	{
		const float Old = Tile.GetHeightCm(X, Y);
		if (FMath::IsNearlyEqual(Old, HeightCm, 0.5f))
		{
			return false;
		}
		Tile.SetHeightCm(X, Y, HeightCm);
		return true;
	}

	/**
	 * Ebnet alle Raster-Vertices innerhalb eines Kreises auf eine Hoehe ein.
	 * Wird fuer Fahrbahnen verwendet: entlang der Mittellinie wird je Stuetzpunkt
	 * eine Kreisscheibe mit Radius = halbe Fahrbahnbreite gesetzt, sodass die
	 * Landscape die Querneigung der Strasse nicht mehr abbildet (und damit die
	 * separat erzeugte Fahrbahndecke nirgends durchsticht).
	 */
	/**
	 * Ruft Func fuer jede Rasterzelle auf, deren Mittelpunkt in der Scheibe
	 * liegt. Uebergeben wird der lineare Zellindex (Y * GridSize + X).
	 */
	template <typename FuncType>
	void ForEachDiscCell(const FTerrainTile& Tile, const FVector2D& Center, double Radius, FuncType&& Func)
	{
		if (!Tile.IsValid() || Radius <= 0.0)
		{
			return;
		}

		// Vertex-Indexbereich der umschreibenden Box, auf das Raster geklemmt.
		const double MinX = (Center.X - Radius - Tile.WorldMinXY.X) / Tile.CellSizeCm;
		const double MinY = (Center.Y - Radius - Tile.WorldMinXY.Y) / Tile.CellSizeCm;
		const double MaxX = (Center.X + Radius - Tile.WorldMinXY.X) / Tile.CellSizeCm;
		const double MaxY = (Center.Y + Radius - Tile.WorldMinXY.Y) / Tile.CellSizeCm;

		const int32 X0 = FMath::Max(FMath::FloorToInt(MinX), 0);
		const int32 Y0 = FMath::Max(FMath::FloorToInt(MinY), 0);
		const int32 X1 = FMath::Min(FMath::CeilToInt(MaxX), Tile.GridSize - 1);
		const int32 Y1 = FMath::Min(FMath::CeilToInt(MaxY), Tile.GridSize - 1);

		if (X0 > X1 || Y0 > Y1)
		{
			return;
		}

		const double RadiusSq = Radius * Radius;

		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				const FVector2D P = Tile.CellToWorld(X, Y);
				const double Dx = P.X - Center.X;
				const double Dy = P.Y - Center.Y;
				if (Dx * Dx + Dy * Dy <= RadiusSq)
				{
					Func(Y * Tile.GridSize + X);
				}
			}
		}
	}

	/**
	 * Ruft Func fuer jede Rasterzelle auf, die hoechstens Radius von der
	 * STRECKE A-B entfernt liegt. Uebergeben werden Zellindex, der quadrierte
	 * Abstand und der Laufparameter 0..1 entlang der Strecke.
	 *
	 * Der Unterschied zur Kreisscheibe je Stuetzpunkt ist entscheidend: Die
	 * Fahrbahnhoehe an der Stelle der Zelle laesst sich nur so bestimmen. Wer
	 * je Zelle den naechstgelegenen STUETZPUNKT nimmt, baut eine Treppe -
	 * benachbarte Zellen greifen dann auf Punkte zu, die entlang der Strasse
	 * weit auseinander liegen.
	 */
	template <typename FuncType>
	void ForEachSegmentCell(
		const FTerrainTile& Tile, const FVector2D& A, const FVector2D& B,
		double Radius, FuncType&& Func)
	{
		if (!Tile.IsValid() || Radius <= 0.0)
		{
			return;
		}

		const double MinX = (FMath::Min(A.X, B.X) - Radius - Tile.WorldMinXY.X) / Tile.CellSizeCm;
		const double MinY = (FMath::Min(A.Y, B.Y) - Radius - Tile.WorldMinXY.Y) / Tile.CellSizeCm;
		const double MaxX = (FMath::Max(A.X, B.X) + Radius - Tile.WorldMinXY.X) / Tile.CellSizeCm;
		const double MaxY = (FMath::Max(A.Y, B.Y) + Radius - Tile.WorldMinXY.Y) / Tile.CellSizeCm;

		const int32 X0 = FMath::Max(FMath::FloorToInt(MinX), 0);
		const int32 Y0 = FMath::Max(FMath::FloorToInt(MinY), 0);
		const int32 X1 = FMath::Min(FMath::CeilToInt(MaxX), Tile.GridSize - 1);
		const int32 Y1 = FMath::Min(FMath::CeilToInt(MaxY), Tile.GridSize - 1);

		if (X0 > X1 || Y0 > Y1)
		{
			return;
		}

		const FVector2D AB = B - A;
		const double LengthSq = AB.SizeSquared();
		const double RadiusSq = Radius * Radius;

		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				const FVector2D P = Tile.CellToWorld(X, Y);

				const double T = (LengthSq > UE_DOUBLE_SMALL_NUMBER)
					? FMath::Clamp(FVector2D::DotProduct(P - A, AB) / LengthSq, 0.0, 1.0)
					: 0.0;

				const FVector2D Closest = A + AB * T;
				const double DistSq = FVector2D::DistSquared(P, Closest);

				if (DistSq <= RadiusSq)
				{
					Func(Y * Tile.GridSize + X, DistSq, T);
				}
			}
		}
	}

	/**
	 * Deckelt die VIER Rasterzellen, aus denen die Gelaendehoehe an der Stelle
	 * P bilinear gebildet wird, auf hoechstens HeightCm.
	 *
	 * Das ist die exakte Bedingung dafuer, dass das Gelaende an dieser Stelle
	 * nicht ueber HeightCm liegt: Der bilineare Wert ist eine gewichtete
	 * Mittelung der vier Ecken, also hoechstens so gross wie die groesste.
	 *
	 * Zuvor wurde stattdessen der Deckel-RADIUS auf die halbe Zelldiagonale
	 * (5,53 m) aufgeweitet, um dieselben Ecken zu treffen. Das erfasst aber
	 * auch Zellen unter NACHBARSTRASSEN: Liegt dort eine tiefere Fahrbahn,
	 * zieht ihr Deckel das Gelaende unter dieser hier mit nach unten. Gemessen
	 * schwebten die frei liegenden Fahrbahn-Vertices dadurch im Mittel 65 cm
	 * ueber Grund, bei einem Sollwert von 14 cm.
	 */
	template <typename MapType>
	void CapInterpolationCorners(
		const FTerrainTile& Tile, MapType& Ceiling, const FVector2D& P, float HeightCm)
	{
		if (!Tile.IsValid() || Tile.CellSizeCm <= 0.0)
		{
			return;
		}

		const double Fx = (P.X - Tile.WorldMinXY.X) / Tile.CellSizeCm;
		const double Fy = (P.Y - Tile.WorldMinXY.Y) / Tile.CellSizeCm;

		const int32 X0 = FMath::FloorToInt(Fx);
		const int32 Y0 = FMath::FloorToInt(Fy);

		for (int32 Dy = 0; Dy <= 1; ++Dy)
		{
			for (int32 Dx = 0; Dx <= 1; ++Dx)
			{
				const int32 X = FMath::Clamp(X0 + Dx, 0, Tile.GridSize - 1);
				const int32 Y = FMath::Clamp(Y0 + Dy, 0, Tile.GridSize - 1);
				const int32 Index = Y * Tile.GridSize + X;

				if (float* Existing = Ceiling.Find(Index))
				{
					*Existing = FMath::Min(*Existing, HeightCm);
				}
				else
				{
					Ceiling.Add(Index, HeightCm);
				}
			}
		}
	}

	int32 RasterizeDisc(FTerrainTile& Tile, const FVector2D& Center, double Radius, float HeightCm)
	{
		int32 ModifiedCount = 0;
		ForEachDiscCell(Tile, Center, Radius,
			[&Tile, &ModifiedCount, HeightCm](int32 Index)
			{
				if (SetHeightIfChanged(Tile, Index % Tile.GridSize, Index / Tile.GridSize, HeightCm))
				{
					++ModifiedCount;
				}
			});
		return ModifiedCount;
	}

	/** Quadrierter Abstand Punkt zu Segment (2D). */
	double PointToSegmentDistanceSquared(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D AB = B - A;
		const double LengthSq = AB.SizeSquared();
		if (LengthSq <= UE_DOUBLE_SMALL_NUMBER)
		{
			return FVector2D::DistSquared(P, A);
		}

		const double T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / LengthSq, 0.0, 1.0);
		const FVector2D Closest = A + AB * T;
		return FVector2D::DistSquared(P, Closest);
	}

	/**
	 * True, wenn P innerhalb des Polygons liegt oder hoechstens Margin von
	 * einer Kante entfernt ist. Der Randband-Term sorgt dafuer, dass ein
	 * Gebaeude mit einer Grundflaeche von z. B. 10 m die Landscape nicht nur im
	 * strengen Umriss, sondern mit etwas Vorlauf einebnet - sonst wuerde an der
	 * Hauskante ein Erdschwall sichtbar.
	 */
	bool IsWithinMarginOfPolygon(const FVector2D& P, const TArray<FVector2D>& Polygon, double Margin)
	{
		if (FPolygonUtils::IsPointInPolygon(P, Polygon))
		{
			return true;
		}

		const double MarginSq = Margin * Margin;
		const int32 N = Polygon.Num();
		for (int32 i = 0; i < N; ++i)
		{
			const FVector2D& A = Polygon[i];
			const FVector2D& B = Polygon[(i + 1) % N];
			if (PointToSegmentDistanceSquared(P, A, B) <= MarginSq)
			{
				return true;
			}
		}
		return false;
	}
}

void FTerrainTile::GetHeightRange(float& OutMinCm, float& OutMaxCm) const
{
	OutMinCm = TNumericLimits<float>::Max();
	OutMaxCm = TNumericLimits<float>::Lowest();

	for (const float Height : HeightsCm)
	{
		OutMinCm = FMath::Min(OutMinCm, Height);
		OutMaxCm = FMath::Max(OutMaxCm, Height);
	}

	if (OutMinCm > OutMaxCm)
	{
		OutMinCm = 0.0f;
		OutMaxCm = 0.0f;
	}
}

TArray<uint16> FTerrainTile::ToUEHeightmap(float MinCm, float MaxCm) const
{
	TArray<uint16> Result;
	if (!IsValid())
	{
		return Result;
	}

	const float Range = MaxCm - MinCm;
	if (Range <= 0.0f)
	{
		return Result;
	}

	Result.SetNumUninitialized(HeightsCm.Num());
	for (int32 Index = 0; Index < HeightsCm.Num(); ++Index)
	{
		const float Normalized = FMath::Clamp((HeightsCm[Index] - MinCm) / Range, 0.0f, 1.0f);
		Result[Index] = static_cast<uint16>(FMath::RoundToInt(Normalized * 65535.0f));
	}
	return Result;
}

uint16 FTerrainTile::EncodeLandscapeHeightCm(double HeightCm, double ZScale)
{
	if (ZScale <= 0.0)
	{
		// Degenerierte ZScale: neutraler Wert (= 0 cm Hoehe).
		return 32768;
	}

	// UE interpretiert value als (value - 32768) / 128 * ZScale in cm.
	// Fuer worldZ == HeightCm ist StepsPerCm = 128 / ZScale.
	const double StepsPerCm = 128.0 / ZScale;
	const double Value = 32768.0 + HeightCm * StepsPerCm;
	return static_cast<uint16>(FMath::Clamp(FMath::RoundToInt(Value), 0, 65535));
}

FTerrainGenerationReport UTerrainGenerator::Generate(
	const FHeightmapRaster& Dem,
	const UGeoCoordinateConverter& Converter,
	const FTerrainGenerationSettings& Settings,
	FTerrainTile& OutTile,
	const FGeoBounds* CropBounds)
{
	FTerrainGenerationReport Report;
	const double StartTime = FPlatformTime::Seconds();

	OutTile = FTerrainTile();

	if (!Dem.IsValid())
	{
		Report.ErrorMessage = TEXT("Ungueltiges DEM-Raster (Dimensionen oder Sample-Anzahl).");
		UE_LOG(LogWbTerrain, Error, TEXT("%s"), *Report.ErrorMessage);
		return Report;
	}

	if (!Converter.IsInitialized())
	{
		Report.ErrorMessage = TEXT("Geokoordinaten-Konverter ist nicht initialisiert.");
		UE_LOG(LogWbTerrain, Error, TEXT("%s"), *Report.ErrorMessage);
		return Report;
	}

	const int32 GridSize = FMath::Max(2, Settings.GridSize);

	// Welt-Ausdehnung des DEMs ueber seine vier Ecken. GeoToUnrealGround
	// rechnet exakt ueber ECEF - die Konvergenz der Meridiane ist damit bereits
	// korrekt enthalten, und die resultierende Box ist die minimale
	// achsenparallele Umhuellung in Unreal-Koordinaten.
	const FGeoCoordinate Corners[4] = {
		FGeoCoordinate(Dem.MinLongitude, Dem.MinLatitude, 0.0),
		FGeoCoordinate(Dem.GetMaxLongitude(), Dem.MinLatitude, 0.0),
		FGeoCoordinate(Dem.MinLongitude, Dem.GetMaxLatitude(), 0.0),
		FGeoCoordinate(Dem.GetMaxLongitude(), Dem.GetMaxLatitude(), 0.0),
	};

	double DemMinX = TNumericLimits<double>::Max();
	double DemMinY = TNumericLimits<double>::Max();
	double DemMaxX = TNumericLimits<double>::Lowest();
	double DemMaxY = TNumericLimits<double>::Lowest();

	for (const FGeoCoordinate& Corner : Corners)
	{
		const FVector P = Converter.GeoToUnrealGround(Corner);
		DemMinX = FMath::Min(DemMinX, P.X);
		DemMinY = FMath::Min(DemMinY, P.Y);
		DemMaxX = FMath::Max(DemMaxX, P.X);
		DemMaxY = FMath::Max(DemMaxY, P.Y);
	}

	// Optionaler Zuschnitt auf den Analyse-Bereich (z. B. die OSM-Ausdehnung +
	// 200-m-Rand), beschnitten auf die DEM-Box. Ohne gueltigen Schnitt wird die
	// volle DEM-Ausdehnung verwendet - sonst waere die Stadt nur ein winziger
	// Fleck in einem 1-Grad-SRTM-Tile (~111 km) und die Aufloesung verschwendet.
	double MinX = DemMinX;
	double MinY = DemMinY;
	double MaxX = DemMaxX;
	double MaxY = DemMaxY;

	if (CropBounds && CropBounds->IsValid())
	{
		const FGeoCoordinate CropCorners[4] = {
			FGeoCoordinate(CropBounds->MinLongitude, CropBounds->MinLatitude, 0.0),
			FGeoCoordinate(CropBounds->MaxLongitude, CropBounds->MinLatitude, 0.0),
			FGeoCoordinate(CropBounds->MinLongitude, CropBounds->MaxLatitude, 0.0),
			FGeoCoordinate(CropBounds->MaxLongitude, CropBounds->MaxLatitude, 0.0),
		};

		double CropMinX = TNumericLimits<double>::Max();
		double CropMinY = TNumericLimits<double>::Max();
		double CropMaxX = TNumericLimits<double>::Lowest();
		double CropMaxY = TNumericLimits<double>::Lowest();

		for (const FGeoCoordinate& Corner : CropCorners)
		{
			const FVector P = Converter.GeoToUnrealGround(Corner);
			CropMinX = FMath::Min(CropMinX, P.X);
			CropMinY = FMath::Min(CropMinY, P.Y);
			CropMaxX = FMath::Max(CropMaxX, P.X);
			CropMaxY = FMath::Max(CropMaxY, P.Y);
		}

		// Konfigurierbarer Rand, damit das Terrain ein Stueck ueber die Stadt
		// hinausragt (Settings.CropMarginMeters, Default 200 m).
		const double CropMarginCm = FMath::Max(0.0, Settings.CropMarginMeters) * 100.0;
		CropMinX -= CropMarginCm;
		CropMinY -= CropMarginCm;
		CropMaxX += CropMarginCm;
		CropMaxY += CropMarginCm;

		// Schnitt von Crop- und DEM-Box; nur uebernehmen, wenn er gueltig ist
		// (sonst Fallback auf die volle Ausdehnung).
		const double IMinX = FMath::Max(DemMinX, CropMinX);
		const double IMaxX = FMath::Min(DemMaxX, CropMaxX);
		const double IMinY = FMath::Max(DemMinY, CropMinY);
		const double IMaxY = FMath::Min(DemMaxY, CropMaxY);

		if (IMaxX - IMinX > UE_DOUBLE_KINDA_SMALL_NUMBER
			&& IMaxY - IMinY > UE_DOUBLE_KINDA_SMALL_NUMBER)
		{
			MinX = IMinX;
			MaxX = IMaxX;
			MinY = IMinY;
			MaxY = IMaxY;
		}
	}

	const double ExtentX = MaxX - MinX;
	const double ExtentY = MaxY - MinY;

	if (ExtentX <= UE_DOUBLE_KINDA_SMALL_NUMBER || ExtentY <= UE_DOUBLE_KINDA_SMALL_NUMBER)
	{
		Report.ErrorMessage = TEXT("Weltbox des DEMs ist degeneriert (Breite oder Hoehe = 0).");
		UE_LOG(LogWbTerrain, Error, TEXT("%s"), *Report.ErrorMessage);
		return Report;
	}

	// Quadratisches Tile, das die DEM-Box vollstaendig ueberdeckt. UE-
	// Landscapes sind quadratisch; die Zellgroesse wird aus der groesseren
	// Ausdehnung bestimmt, damit in beide Richtungen keine Abdeckungsluecke
	// entsteht. Der ueberstehende Bereich wird durch das Rand-Clamping des
	// Samplers gefuellt (flacher Rand, keine Klippe).
	const double CellSizeCm = FMath::Max(ExtentX, ExtentY) / static_cast<double>(GridSize - 1);

	OutTile.GridSize = GridSize;
	OutTile.WorldMinXY = FVector2D(MinX, MinY);
	OutTile.CellSizeCm = CellSizeCm;
	OutTile.HeightsCm.SetNumUninitialized(GridSize * GridSize);

	// Resampling: jede Vertex-Position zurueck nach WGS84 und dort bilinear
	// sampeln. Der Sampler liefert die Hoehe bereits relativ zur gemeinsamen
	// Bezugshoehe in cm - exakt die gleiche Bezugsgroesse, die Strassen- und
	// Gebaeudegenerator verwenden.
	const FRasterHeightSampler Sampler(Dem, Converter);

	for (int32 Y = 0; Y < GridSize; ++Y)
	{
		for (int32 X = 0; X < GridSize; ++X)
		{
			const FVector2D World = OutTile.CellToWorld(X, Y);
			OutTile.SetHeightCm(X, Y, static_cast<float>(Sampler.SampleHeightCm(World)));
		}
	}

	OutTile.GetHeightRange(Report.MinHeightCm, Report.MaxHeightCm);

	Report.bSuccess = true;
	Report.GridSize = GridSize;
	Report.WorldWidthMeters = CellSizeCm * static_cast<double>(GridSize - 1) / CmPerMeter;
	Report.WorldHeightMeters = Report.WorldWidthMeters;
	Report.DurationSeconds = FPlatformTime::Seconds() - StartTime;

	UE_LOG(LogWbTerrain, Log, TEXT("Landscape-Heightmap erzeugt: %s"), *Report.ToString());

	return Report;
}

namespace
{
	/**
	 * Weltraum-Ausdehnung (lange Seite) eines geo-Bereichs in Metern ueber den
	 * Konverter. Ungueltige Bounds liefern 0 (kein Vergleich moeglich).
	 */
	double GeoBoundsExtentMeters(const FGeoBounds& Bounds, const UGeoCoordinateConverter& Converter)
	{
		if (!Bounds.IsValid() || !Converter.IsInitialized())
		{
			return 0.0;
		}

		const FGeoCoordinate Corners[4] = {
			FGeoCoordinate(Bounds.MinLongitude, Bounds.MinLatitude, 0.0),
			FGeoCoordinate(Bounds.MaxLongitude, Bounds.MinLatitude, 0.0),
			FGeoCoordinate(Bounds.MinLongitude, Bounds.MaxLatitude, 0.0),
			FGeoCoordinate(Bounds.MaxLongitude, Bounds.MaxLatitude, 0.0),
		};

		double MinX = TNumericLimits<double>::Max();
		double MinY = TNumericLimits<double>::Max();
		double MaxX = TNumericLimits<double>::Lowest();
		double MaxY = TNumericLimits<double>::Lowest();

		for (const FGeoCoordinate& Corner : Corners)
		{
			const FVector P = Converter.GeoToUnrealGround(Corner);
			MinX = FMath::Min(MinX, P.X);
			MinY = FMath::Min(MinY, P.Y);
			MaxX = FMath::Max(MaxX, P.X);
			MaxY = FMath::Max(MaxY, P.Y);
		}

		return FMath::Max(MaxX - MinX, MaxY - MinY) / 100.0; // cm -> m
	}
}

FTerrainQualityReport UTerrainGenerator::CheckTerrainQuality(
	const FTerrainGenerationReport& TerrainReport,
	const FGeoBounds& OsmBounds,
	const UGeoCoordinateConverter& Converter,
	double MaxTileToOsmRatio,
	double MinHeightSpanMeters,
	double MaxHeightSpanMeters)
{
	FTerrainQualityReport Report;
	if (!TerrainReport.bSuccess || TerrainReport.WorldWidthMeters <= 0.0)
	{
		return Report;
	}

	// 1) Tile deutlich groesser als die OSM-Ausdehnung -> Crop fehlt?
	//    Das gecroppte Tile entspricht OSM + 2*CropMargin (Default ~12 km
	//    statt der vollen 1-Grad-SRTM-Ausdehnung ~111 km); ein Verhaeltnis
	//    ueber MaxTileToOsmRatio deutet auf einen fehlenden/leeren Crop.
	const double OsmExtentMeters = GeoBoundsExtentMeters(OsmBounds, Converter);
	if (OsmExtentMeters > 0.0
		&& TerrainReport.WorldWidthMeters > OsmExtentMeters * MaxTileToOsmRatio)
	{
		Report.bWarnTileTooLarge = true;
		Report.WarningMessage += FString::Printf(
			TEXT("Terrain-Tile (%.0f m) deutlich groesser als die OSM-Ausdehnung (%.0f m) - fehlender Crop? "),
			TerrainReport.WorldWidthMeters, OsmExtentMeters);
	}

	// 2) Hoehenspanne unplausibel gross/klein: zu klein = flaches oder
	//    fehlendes DEM (NoData-Kennung 0), zu gross = NoData-Spikes im Raster.
	const double SpanMeters =
		(static_cast<double>(TerrainReport.MaxHeightCm) - static_cast<double>(TerrainReport.MinHeightCm)) / 100.0;
	if (SpanMeters < MinHeightSpanMeters || SpanMeters > MaxHeightSpanMeters)
	{
		Report.bWarnHeightRangeSuspicious = true;
		Report.WarningMessage += FString::Printf(
			TEXT("Hoehenspanne %.1f m unplausibel (erwartet %.0f..%.0f m) - DEM-Daten pruefen."),
			SpanMeters, MinHeightSpanMeters, MaxHeightSpanMeters);
	}

	return Report;
}

int32 UTerrainGenerator::FlattenUnderRoads(
	const FRoadNetwork& Network,
	const FTerrainGenerationSettings& Settings,
	FTerrainTile& Tile) const
{
	if (!Tile.IsValid())
	{
		UE_LOG(LogWbTerrain, Warning, TEXT("FlattenUnderRoads: ungueltiges TerrainTile."));
		return 0;
	}

	// Zielhoehen erst sammeln, dann schreiben. Zwei Fehler werden damit
	// vermieden, die beide dazu fuehren, dass die Fahrbahn im Boden verschwindet:
	//
	// 1. Die Mittellinie liegt NICHT auf Terrainhoehe. Der RoadNetworkGenerator
	//    setzt Point.Z = SampleHeightCm(XY) + RoadSurfaceOffsetCm. Wer diesen
	//    Wert direkt als Gelaendehoehe schreibt, macht Gelaende und Fahrbahn
	//    koplanar. Deshalb RoadFlattenSinkCm abziehen.
	//
	// 2. Beim direkten Ueberschreiben gewinnt der zuletzt gemalte Punkt. Am
	//    Hang kann das ein Punkt einen ganzen Einebnungsradius weiter oben
	//    sein, der das Gelaende ueber die Fahrbahn hebt. Deshalb entscheidet je
	//    Zelle der NAECHSTGELEGENE Strassenpunkt.
	//
	// Hier stand zuvor das MINIMUM aller ueberdeckenden Strassenpunkte. Das war
	// ein Kurieren an einer Fehlmessung: Die damalige Verdeckungsstatistik
	// verglich einen Fahrbahn-Vertex mit dem obersten Trace-Treffer - und seit
	// die Fahrbahnen eigene Kollision haben, ist das die Fahrbahn selbst. Sie
	// mass also Strasse gegen Strasse und meldete konstant rund 30 % Verdeckung,
	// die es nie gab. Gegen die Landscape-Daten gemessen lagen 0 von 134
	// Vertices unter dem Gelaende - dafuer schwebte die Fahrbahn im Mittel
	// 91,6 cm darueber, weil das Minimum ueber einen 9-m-Radius am Hang das
	// Gelaende weit unter die Strasse abtraegt. Genau diese Luecke war im Spiel
	// als aufgerissener Strassenrand zu sehen.
	struct FCellTarget
	{
		float HeightCm = 0.0f;
		double NearestDistSq = TNumericLimits<double>::Max();
	};

	TMap<int32, FCellTarget> TargetHeightCm;

	// Obergrenze fuer Zellen, unter denen tatsaechlich Fahrbahn liegt.
	// Siehe FTerrainGenerationSettings::PavedCeilingMarginCm.
	TMap<int32, float> PavedCeilingCm;

	for (const FRoadSegment& Segment : Network.Segments)
	{
		// Bruecken und Tunnel duerfen das Gelaende NICHT mitziehen.
		//
		// Hier lief die Schleife ueber alle Segmente ohne Ebenen-Filter. Eine
		// Bruecke 13 m ueber Grund hat damit das Gelaende auf 13 m angehoben -
		// und die ebenerdige Strasse, die DARUNTER durchfuehrt, im Erdreich
		// begraben. Ein Tunnel hat umgekehrt einen Graben ausgehoben.
		if (Segment.bIsBridge || Segment.bIsTunnel || Segment.Layer != 0)
		{
			continue;
		}

		const TArray<FVector>& Centerline = Segment.TrimmedCenterline.Num() >= 2
			? Segment.TrimmedCenterline
			: Segment.Centerline;

		if (Centerline.Num() < 2)
		{
			continue;
		}

		const double Radius = FMath::Min(
			Segment.CarriagewayWidthCm * 0.5 + Settings.RoadFlattenMarginCm,
			Settings.MaxRoadFlattenRadiusCm);


		// STRECKENWEISE, nicht je Stuetzpunkt.
		//
		// Zuvor wurde je Stuetzpunkt eine Kreisscheibe gesetzt und je Zelle der
		// naechstgelegene PUNKT gewaehlt. Das ergibt eine Treppe: Benachbarte
		// Zellen greifen auf Stuetzpunkte zu, die entlang der Strasse weit
		// auseinander liegen.
		//
		// Sichtbar wurde das erst in der Messung gegen die echte Landscape:
		// 8 % der Fahrbahn-Vertices lagen DARUNTER, waehrend die frei
		// liegenden im Mittel 65 cm SCHWEBTEN (Sollwert 20 cm). Beides
		// zugleich ist die Handschrift einer zackigen Flaeche, nicht einer
		// gleichmaessigen Verschiebung. Die Messung gegen die Gelaendekachel
		// im Speicher zeigte davon nichts - sie tastet die Mittellinie ab, und
		// genau dort ist die Treppe am flachsten.
		//
		// Jetzt wird die Fahrbahnhoehe AN DER STELLE DER ZELLE bestimmt:
		// Projektion auf den Streckenabschnitt, lineare Interpolation zwischen
		// seinen Endpunkten.
		for (int32 Step = 1; Step < Centerline.Num(); ++Step)
		{
			const FVector& PointA = Centerline[Step - 1];
			const FVector& PointB = Centerline[Step];

			const FVector2D A2D(PointA.X, PointA.Y);
			const FVector2D B2D(PointB.X, PointB.Y);

			// Deckel: NUR fuer echte Ueberlappungen zweier Fahrbahnen.
			//
			// Der Deckel nimmt je Zelle die TIEFSTE Fahrbahn. Das ist noetig,
			// wo zwei Fahrbahnen dieselbe Zelle bedecken - eine Rampe neben
			// der ebenerdigen Strasse -, denn sonst begraebt die hoehere die
			// tiefere.
			//
			// Er darf aber NICHT weiter reichen als der Asphalt. Zwei
			// Aufweitungen sind hier nacheinander gescheitert:
			//
			//   - Radius auf die halbe Zelldiagonale (5,53 m): erfasst Zellen
			//     unter Nachbarstrassen; deren tiefere Fahrbahn zieht das
			//     Gelaende unter dieser hier mit nach unten. Gemessen schwebten
			//     die frei liegenden Fahrbahn-Vertices im Mittel 65 cm.
			//
			//   - Alle vier Interpolationsecken je Tastestelle deckeln: Am Hang
			//     bekommt die bergseitige Ecke den talseitigen Wert. Das kostet
			//     Rasterweite mal Gefaelle an Bodenfreiheit - bei 7,81 m und
			//     10 % sind das 78 cm. Der Freiraum-Test hat 64 cm gemessen und
			//     die Aenderung zurueckgewiesen.
			//
			// Fuer die Glaettung ist der Deckel ohnehin nicht mehr zustaendig:
			// Die Zielhoehe wird seit der Projektion an der Stelle der Zelle
			// bestimmt und ist damit von sich aus stetig.
			ForEachSegmentCell(Tile, A2D, B2D, Segment.CarriagewayWidthCm * 0.5,
				[&PavedCeilingCm, &PointA, &PointB, &Settings](int32 Index, double, double T)
				{
					const float TargetCm = static_cast<float>(
						FMath::Lerp(PointA.Z, PointB.Z, T) - Settings.RoadFlattenSinkCm);

					if (float* Existing = PavedCeilingCm.Find(Index))
					{
						*Existing = FMath::Min(*Existing, TargetCm);
					}
					else
					{
						PavedCeilingCm.Add(Index, TargetCm);
					}
				});

			// Boeschung: naechstgelegene STELLE bestimmt die Hoehe, damit das
			// Gelaende der Strasse folgt statt am Hang unter ihr wegzusacken.
			ForEachSegmentCell(Tile, A2D, B2D, Radius,
				[&TargetHeightCm, &PointA, &PointB, &Settings](int32 Index, double DistSq, double T)
				{
					const float TargetCm = static_cast<float>(
						FMath::Lerp(PointA.Z, PointB.Z, T) - Settings.RoadFlattenSinkCm);

					FCellTarget& Target = TargetHeightCm.FindOrAdd(Index);
					if (DistSq < Target.NearestDistSq)
					{
						Target.NearestDistSq = DistSq;
						Target.HeightCm = TargetCm;
					}
				});
		}
	}


	// Kreuzungsflaechen einebnen.
	//
	// Die Schleife oben laeuft ueber TrimmedCenterline - und getrimmt heisst
	// genau: das Kreuzungsinnere ist ausgespart. Die Fahrbahnbaender enden am
	// Rand der Kreuzung, die Flaeche dazwischen deckt die Kreuzungsplatte ab.
	// Unter dieser Platte wurde das Gelaende nie abgesenkt, und deshalb stand
	// dort das Gras durch die Kreuzung.
	//
	// Massgeblich ist die TIEFSTE Ecke des Kreuzungsumrisses: Die Platte ist
	// eine zusammenhaengende Flaeche, sie verschwindet schon dann teilweise im
	// Boden, wenn nur ihr niedrigster Punkt unterschritten wird.
	int32 JunctionCellsLowered = 0;
	int32 JunctionsSkippedAtStructures = 0;
	for (const FRoadIntersection& Intersection : Network.Intersections)
	{
		if (Intersection.Polygon.Num() < 1)
		{
			continue;
		}

		// Kreuzungen an Bruecken und Tunneln auslassen.
		//
		// Ihre Umrissecken liegen auf voellig verschiedenen Ebenen - eine
		// Rampenecke 15 m ueber der ebenerdigen Ecke. Wer die Flaeche dazwischen
		// einebnet, reisst entweder das Gelaende unter der Rampe weg (gemessen:
		// 1499 cm Freiraum, eine Strasse schwebte 15 m ueber dem Boden) oder
		// begraebt die tiefe Ecke. Diese Kreuzungen bleiben den Fahrbahnbaendern
		// ueberlassen, die ebenenweise arbeiten.
		bool bTouchesStructure = false;
		for (const FIntersectionArm& Arm : Intersection.Arms)
		{
			if (Network.Segments.IsValidIndex(Arm.SegmentId))
			{
				const FRoadSegment& ArmSegment = Network.Segments[Arm.SegmentId];
				if (ArmSegment.bIsBridge || ArmSegment.bIsTunnel || ArmSegment.Layer != 0)
				{
					bTouchesStructure = true;
					break;
				}
			}
		}
		if (bTouchesStructure)
		{
			++JunctionsSkippedAtStructures;
			continue;
		}

		// Je Zelle das MINIMUM der Ecken in der Umgebung, nicht die
		// naechstgelegene.
		//
		// Die Platte ist eine Flaeche zwischen den Ecken. "Naechstgelegene Ecke"
		// erzeugt eine Treppe, deren hoehere Stufe die bilinear interpolierte
		// Gelaendeflaeche ueber die benachbarte, tiefere Ecke hebt - derselbe
		// Interpolationsfehler wie bei den Fahrbahnbaendern, nur eine Ebene
		// hoeher. Gemessen stieg die Zahl der verdeckten Umrisspunkte damit von
		// 188 auf 1583.
		//
		// Das Minimum ueber die Umgebung ist bei kleinen Kreuzungen das Minimum
		// ueber alle Ecken (flach, kein Interpolationsfehler moeglich) und
		// bleibt bei grossen oertlich, folgt also dem Gefaelle.
		const double InterpolationReachCm = Tile.CellSizeCm * 0.7072 + 1.0;
		const double LocalRadiusCm = InterpolationReachCm * 2.0;
		const bool bHasArea = Intersection.Polygon.Num() >= 3;

		TArray<FVector2D> Outline2D;
		Outline2D.Reserve(Intersection.Polygon.Num());
		for (const FVector& Point : Intersection.Polygon)
		{
			Outline2D.Add(FVector2D(Point.X, Point.Y));
		}

		const double JunctionRadius = Intersection.RadiusCm + InterpolationReachCm;

		ForEachDiscCell(Tile, FVector2D(Intersection.Location.X, Intersection.Location.Y),
			JunctionRadius,
			[&PavedCeilingCm, &TargetHeightCm, &JunctionCellsLowered, &Tile, &Outline2D,
				&Intersection, InterpolationReachCm, LocalRadiusCm, bHasArea, &Settings](int32 Index)
			{
				const FVector2D CellWorld =
					Tile.CellToWorld(Index % Tile.GridSize, Index / Tile.GridSize);

				// Nur was die Platte wirklich bedeckt - die Kreisscheibe mit
				// Intersection.RadiusCm ist der Abstand des ENTFERNTESTEN Arms
				// und bei asymmetrischen Kreuzungen weit groesser als die Platte.
				if (bHasArea)
				{
					if (!IsWithinMarginOfPolygon(CellWorld, Outline2D, InterpolationReachCm))
					{
						return;
					}
				}

				const double LocalRadiusSq = LocalRadiusCm * LocalRadiusCm;

				double LocalMinZ = TNumericLimits<double>::Max();
				double NearestDistSq = TNumericLimits<double>::Max();
				double NearestZ = 0.0;

				for (const FVector& Corner : Intersection.Polygon)
				{
					const double DistSq = FVector2D::DistSquared(
						CellWorld, FVector2D(Corner.X, Corner.Y));
					if (DistSq < NearestDistSq)
					{
						NearestDistSq = DistSq;
						NearestZ = Corner.Z;
					}
					if (DistSq <= LocalRadiusSq)
					{
						LocalMinZ = FMath::Min(LocalMinZ, Corner.Z);
					}
				}

				// Entartete Umrisse (unter drei Ecken) verhalten sich wie ein
				// einzelner Strassenpunkt: nur die eigene Umgebung.
				if (!bHasArea && NearestDistSq > InterpolationReachCm * InterpolationReachCm)
				{
					return;
				}

				// Keine Ecke in Reichweite: auf die naechstgelegene zurueckfallen.
				const double TargetZ = (LocalMinZ < TNumericLimits<double>::Max())
					? LocalMinZ : NearestZ;
				const float TargetCm = static_cast<float>(TargetZ - Settings.RoadFlattenSinkCm);

				// Als Obergrenze, nicht als Zuweisung: Liegt dort schon eine
				// tiefere Fahrbahn, bleibt die tiefere massgeblich.
				bool bLowered = false;
				if (float* Existing = PavedCeilingCm.Find(Index))
				{
					if (TargetCm < *Existing)
					{
						*Existing = TargetCm;
						bLowered = true;
					}
				}
				else
				{
					PavedCeilingCm.Add(Index, TargetCm);
					bLowered = true;
				}

				// Zellen, die von keinem Fahrbahnband erfasst wurden, brauchen
				// ueberhaupt erst einen Eintrag - sonst werden sie nie
				// geschrieben. Genau das war im Kreuzungsinneren der Fall.
				FCellTarget& Target = TargetHeightCm.FindOrAdd(Index);
				if (Target.NearestDistSq == TNumericLimits<double>::Max())
				{
					Target.HeightCm = TargetCm;
					Target.NearestDistSq = NearestDistSq;
				}

				if (bLowered)
				{
					++JunctionCellsLowered;
				}
			});
	}

	if (JunctionsSkippedAtStructures > 0)
	{
		UE_LOG(LogWbTerrain, Log,
			TEXT("Kreuzungen an Bruecken/Tunneln ausgelassen: %d von %d."),
			JunctionsSkippedAtStructures, Network.Intersections.Num());
	}

	if (JunctionCellsLowered > 0)
	{
		UE_LOG(LogWbTerrain, Log,
			TEXT("Kreuzungsflaechen: %d Zellen unter %d Kreuzungen abgesenkt. ")
			TEXT("Diese Flaechen lagen ausserhalb der getrimmten Fahrbahnbaender."),
			JunctionCellsLowered, Network.Intersections.Num());
	}

	int32 ModifiedCount = 0;
	int32 CeilingApplied = 0;
	for (const TPair<int32, FCellTarget>& Cell : TargetHeightCm)
	{
		float HeightCm = Cell.Value.HeightCm;

		// Nie ueber der tiefsten Fahrbahn, die diese Zelle wirklich bedeckt.
		if (const float* Ceiling = PavedCeilingCm.Find(Cell.Key))
		{
			if (*Ceiling < HeightCm)
			{
				HeightCm = *Ceiling;
				++CeilingApplied;
			}
		}

		if (SetHeightIfChanged(Tile, Cell.Key % Tile.GridSize, Cell.Key / Tile.GridSize, HeightCm))
		{
			++ModifiedCount;
		}
	}

	if (CeilingApplied > 0)
	{
		UE_LOG(LogWbTerrain, Log,
			TEXT("Fahrbahn-Obergrenze: %d von %d Zellen abgesenkt, weil eine tiefere ")
			TEXT("Fahrbahn sie bedeckt."),
			CeilingApplied, TargetHeightCm.Num());
	}

	if (ModifiedCount > 0)
	{
		UE_LOG(LogWbTerrain, Log,
			TEXT("Fahrbahn-Einebnung: %d Zellen auf %d Segmenten, %.0f cm unter der Mittellinie."),
			ModifiedCount, Network.Segments.Num(), Settings.RoadFlattenSinkCm);
	}

	return ModifiedCount;
}


int32 UTerrainGenerator::FlattenUnderBuildings(
	const FOSMDataSet& DataSet,
	const UGeoCoordinateConverter& Converter,
	const FTerrainGenerationSettings& Settings,
	FTerrainTile& Tile) const
{
	if (!Tile.IsValid() || !Converter.IsInitialized())
	{
		UE_LOG(LogWbTerrain, Warning, TEXT("FlattenUnderBuildings: ungueltiges Tile oder Konverter."));
		return 0;
	}

	int32 ModifiedCount = 0;
	const double Margin = Settings.BuildingFlattenMarginCm;

	for (const TPair<FOSMId, FOSMWay>& Pair : DataSet.Ways)
	{
		const FOSMWay& Way = Pair.Value;

		// building=* und building:part=*. Relations (Multipolygone) werden vom
		// BuildingGenerator aufgeloest; fuer die Einebnung genuegt die grobe
		// Behandlung geschlossener Ways - Innenhoefe bleiben bewusst erhalten.
		if (!Way.IsBuilding() || !Way.IsClosed())
		{
			continue;
		}

		TArray<FGeoCoordinate> GeoCoords;
		int32 MissingCount = 0;
		if (!DataSet.ResolveWayCoordinates(Way, GeoCoords, MissingCount))
		{
			continue;
		}

		TArray<FVector2D> Footprint;
		Footprint.Reserve(GeoCoords.Num());
		for (const FGeoCoordinate& GeoCoord : GeoCoords)
		{
			const FVector World = Converter.GeoToUnrealGround(GeoCoord);
			Footprint.Add(FVector2D(World.X, World.Y));
		}

		// Geschlossener OSM-Ring: erster == letzter Node, fuer die
		// Polygonoperationen entfernen und Kollineare rauswerfen.
		FPolygonUtils::RemoveDuplicatePoints(Footprint);
		FPolygonUtils::RemoveCollinearPoints(Footprint);

		if (Footprint.Num() < 3)
		{
			continue;
		}

		// Bezugshoehe aus dem aktuellen Tile am Flaechenschwerpunkt. Die
		// Gebaeudegrundflaeche wird auf diese Hoehe gelegt; der
		// BuildingGenerator extrudiert von exakt derselben Hoehe aus, weil er
		// denselben Sampler verwendet.
		const FVector2D Centroid = FPolygonUtils::ComputeCentroid(Footprint);
		const float BaseHeightCm = Tile.SampleHeightBilinearCm(Centroid);

		const FBox2D Bounds = FPolygonUtils::ComputeBounds2D(Footprint);
		const FVector2D BMin(Bounds.Min.X - Margin, Bounds.Min.Y - Margin);
		const FVector2D BMax(Bounds.Max.X + Margin, Bounds.Max.Y + Margin);

		const double MinX = (BMin.X - Tile.WorldMinXY.X) / Tile.CellSizeCm;
		const double MinY = (BMin.Y - Tile.WorldMinXY.Y) / Tile.CellSizeCm;
		const double MaxX = (BMax.X - Tile.WorldMinXY.X) / Tile.CellSizeCm;
		const double MaxY = (BMax.Y - Tile.WorldMinXY.Y) / Tile.CellSizeCm;

		const int32 X0 = FMath::Max(FMath::FloorToInt(MinX), 0);
		const int32 Y0 = FMath::Max(FMath::FloorToInt(MinY), 0);
		const int32 X1 = FMath::Min(FMath::CeilToInt(MaxX), Tile.GridSize - 1);
		const int32 Y1 = FMath::Min(FMath::CeilToInt(MaxY), Tile.GridSize - 1);

		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				const FVector2D P = Tile.CellToWorld(X, Y);
				if (IsWithinMarginOfPolygon(P, Footprint, Margin))
				{
					if (SetHeightIfChanged(Tile, X, Y, BaseHeightCm))
					{
						++ModifiedCount;
					}
				}
			}
		}
	}

	if (ModifiedCount > 0)
	{
		UE_LOG(LogWbTerrain, Log, TEXT("Gebaeude-Einebnung: %d Zellen."), ModifiedCount);
	}

	return ModifiedCount;
}
