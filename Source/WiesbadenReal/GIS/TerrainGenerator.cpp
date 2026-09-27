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

		// Begleitweg (Doppel des erzeugten Gehwegs, siehe AlignCompanionFootways):
		// hat kein eigenes Pflaster und darf das Gelaende unter dem Strassengehweg
		// nicht an sich ziehen - genau das liess den Gehweg am Hang schweben.
		if (Segment.bBegleitweg)
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
	// Wendeplatten an Sackgassen sind Pflaster wie Kreuzungsplatten - als
	// Kreuzung mit einem Arm laufen sie durch dieselben Durchgaenge.
	TArray<FRoadIntersection> WendeplattenAlsKreuzung;
	WendeplattenAlsKreuzung.Reserve(Network.TurningPlates.Num());
	for (const FRoadTurningPlate& Plate : Network.TurningPlates)
	{
		WendeplattenAlsKreuzung.Add(Plate.AsJunction());
	}
	TArray<const FRoadIntersection*> Platten;
	Platten.Reserve(Network.Intersections.Num() + WendeplattenAlsKreuzung.Num());
	for (const FRoadIntersection& Intersection : Network.Intersections) { Platten.Add(&Intersection); }
	for (const FRoadIntersection& Intersection : WendeplattenAlsKreuzung) { Platten.Add(&Intersection); }

	for (const FRoadIntersection* PlattePtr : Platten)
	{
		const FRoadIntersection& Intersection = *PlattePtr;
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

	// DURCH EIN HAUS LAEUFT KEINE STRASSENBOESCHUNG.
	//
	// Die Einebnung greift bis Fahrbahnbreite/2 + RoadFlattenMarginCm (900 cm)
	// neben die Achse - ein Korridor von ueber 10 m. GEMESSEN am 21.09.2026
	// auf Alkis17: die Wolkenbruch laeuft 2 m vor der Fassade des SebboTower
	// vorbei, ihr Korridor reichte damit 6,5 m in das Erdgeschoss und hob das
	// Gelaende dort bis zu 97 cm UEBER den fertigen Boden. Durch das Portal
	// wuchs Gras; der Hoehenschnitt zeigte einen Erdhuegel im Durchgang mit
	// Scheitel 142 cm ueber dem Turmfuss (Boden 45).
	//
	// Wo ein Bauplateau ausdruecklich einen Grundriss nennt, wird die Strasse
	// darum gedeckelt: sie darf dort absenken, aber nicht mehr ueber das
	// Plateau heben. Ausserhalb des Grundrisses behaelt sie ihr letztes Wort -
	// die Boeschung zum Grundstueck bleibt, nur das Haus bleibt frei.
	struct FGrundriss
	{
		FVector2D MitteCm = FVector2D::ZeroVector;
		double HalbCm = 0.0;
		double Cos = 1.0;
		double Sin = 0.0;
		float DeckelCm = 0.0f;
	};
	TArray<FGrundriss> Grundrisse;
	for (const FTerrainSitePad& Pad : Settings.SitePads)
	{
		if (Pad.BuildingHalfCm <= 0.0)
		{
			continue;
		}
		double PlateauCm = 0.0;
		double RoadZCm = 0.0;
		if (!ResolveSitePadPlateauCm(Network, Pad, PlateauCm, RoadZCm))
		{
			continue;   // ohne Fahrbahn kein Plateau - und nichts zu deckeln
		}
		FGrundriss G;
		G.MitteCm = Pad.CenterCm;
		G.HalbCm = Pad.BuildingHalfCm;
		G.Cos = FMath::Cos(FMath::DegreesToRadians(Pad.BuildingYawDeg));
		G.Sin = FMath::Sin(FMath::DegreesToRadians(Pad.BuildingYawDeg));
		G.DeckelCm = static_cast<float>(PlateauCm);
		Grundrisse.Add(G);
	}

	// GELAENDE AN DAS PFLASTER ANSCHMIEGEN (25.09.2026).
	//
	// Alle Regeln oben arbeiten mit Zellen und "naechstgelegenem Stuetzpunkt".
	// Das Landscape interpoliert aber bilinear zwischen VIER Eckpunkten einer
	// 7,81-m-Masche - und die gehoeren oft verschiedenen Regeln: der naechsten
	// Nachbarstrasse, dem Minimum einer Kreuzungsplatte, einem Fussweg am Hang.
	// Gemessen an der Emser Strasse (Tools/gelaende_probe.py) stach dadurch Gras
	// durch Fahrbahnrand und Gehweg und hingen Raender bis 2,5 m in der Luft,
	// waehrend die Strassenmitte unauffaellig war.
	//
	// Darum zuletzt: Proben AUF dem Pflaster (Fahrbahnmitte, -rand, Gehweg-
	// mitte, -aussenkante, Kreuzungsplatte) mit ihrer echten Oberflaechenhoehe.
	// Jede Probe beansprucht die vier Eckpunkte ihrer Masche; je Eckpunkt
	// gewinnt die NAECHSTE Probe. Teilen sich eine Fahrbahn und ein
	// eigenstaendiger Fuss-, Rad- oder Treppenweg eine Masche, hat die FAHRBAHN
	// Vorrang (samt ihrem Gehweg und den Kreuzungsplatten): am Hang liegen solche
	// Wege oft 1-2 m tiefer direkt daneben, und "naechste gewinnt" bzw. "tiefere
	// gewinnt" liess dann die Fahrbahn in der Luft haengen (Emser Strasse,
	// Alkis29: Fahrbahnmitte schwebte an 34 % der freien Proben). Der Weg
	// verschwindet dort eher unter der Boeschung - das kleinere Uebel.
	// Der Eckpunkt wird genau auf die Zielhoehe minus RoadFlattenSinkCm gesetzt -
	// nach oben wie nach unten. Damit liegt das
	// bilinear interpolierte Gelaende unter dem Pflaster ueberall knapp darunter,
	// statt einmal im Gras und einmal in der Luft. Der Grundriss-Schutz der
	// Bauplateaus (unten) gilt weiter.
	struct FPflasterZiel
	{
		double DistSq = TNumericLimits<double>::Max();
		float HeightCm = 0.0f;
	};
	TMap<int32, FPflasterZiel> PflasterZiele;   // naechste Probe der Fahrbahnen, ihrer Gehwege und Kreuzungen
	TMap<int32, FPflasterZiel> WegZiele;        // naechste Probe eigenstaendiger Fuss-/Rad-/Treppenwege
	auto IstFahrbahn = [](const FRoadSegment& S)
	{
		return (uint8)S.HighwayType <= (uint8)EOSMHighwayType::Pedestrian;
	};
	auto Beanspruche = [&Tile, &Settings](TMap<int32, FPflasterZiel>& Ziele, double X, double Y, double PflasterZ)
	{
		const double FX = (X - Tile.WorldMinXY.X) / Tile.CellSizeCm;
		const double FY = (Y - Tile.WorldMinXY.Y) / Tile.CellSizeCm;
		if (FX < 0.0 || FY < 0.0 || FX >= Tile.GridSize - 1 || FY >= Tile.GridSize - 1)
		{
			return;
		}
		const int32 X0 = FMath::FloorToInt(FX);
		const int32 Y0 = FMath::FloorToInt(FY);
		for (int32 DY = 0; DY <= 1; ++DY)
		{
			for (int32 DX = 0; DX <= 1; ++DX)
			{
				const FVector2D Ecke = Tile.CellToWorld(X0 + DX, Y0 + DY);
				const double DistSq = FVector2D::DistSquared(Ecke, FVector2D(X, Y));
				FPflasterZiel& Ziel = Ziele.FindOrAdd(Tile.GetIndex(X0 + DX, Y0 + DY));
				if (DistSq < Ziel.DistSq)
				{
					Ziel.DistSq = DistSq;
					Ziel.HeightCm = static_cast<float>(PflasterZ - Settings.RoadFlattenSinkCm);
				}
			}
		}
	};

	const double ProbenAbstandCm = FMath::Max(Tile.CellSizeCm / 3.0, 50.0);
	for (const FRoadSegment& Segment : Network.Segments)
	{
		if (!Settings.bConformToPavement)
		{
			break;
		}
		if (Segment.bIsBridge || Segment.bIsTunnel || Segment.Layer != 0 || Segment.bIsArea || Segment.bBegleitweg)
		{
			continue;
		}
		const TArray<FVector>& Linie = Segment.TrimmedCenterline.Num() >= 2
			? Segment.TrimmedCenterline : Segment.Centerline;
		if (Linie.Num() < 2)
		{
			continue;
		}

		// Querproben: Mitte, Fahrbahnrand und - wo die Strasse ihren Gehweg
		// selbst baut - Gehwegmitte und Aussenkante (Fahrbahn + Bordstein).
		const double Halb = Segment.CarriagewayWidthCm * 0.5;
		const bool bLinks = Segment.SidewalkType == EOSMSidewalkType::Both || Segment.SidewalkType == EOSMSidewalkType::Left;
		const bool bRechts = Segment.SidewalkType == EOSMSidewalkType::Both || Segment.SidewalkType == EOSMSidewalkType::Right;
		const double Gehweg = FMath::Max(Segment.SidewalkWidthCm, 100.0);
		struct FQuer { double Seite; double Abstand; double Dz; };
		TArray<FQuer> Quer = { { 1.0, 0.0, 0.0 }, { 1.0, Halb, 0.0 }, { -1.0, Halb, 0.0 } };
		for (const double Seite : { 1.0, -1.0 })
		{
			if ((Seite > 0.0 && bLinks) || (Seite < 0.0 && bRechts))
			{
				Quer.Add({ Seite, Halb + Gehweg * 0.5, Segment.KerbHeightCm });
				Quer.Add({ Seite, Halb + Gehweg, Segment.KerbHeightCm });
			}
		}

		TMap<int32, FPflasterZiel>& Ziele = IstFahrbahn(Segment) ? PflasterZiele : WegZiele;
		for (int32 Step = 1; Step < Linie.Num(); ++Step)
		{
			const FVector& A = Linie[Step - 1];
			const FVector& B = Linie[Step];
			const FVector2D AB(B.X - A.X, B.Y - A.Y);
			const double Laenge = AB.Size();
			if (Laenge < 1.0)
			{
				continue;
			}
			// Links = (Dir.Y, -Dir.X) wie FPolygonUtils::GetLeftNormal - dieselbe
			// Seite, auf der BuildSidewalk(1.0) den linken Gehweg anlegt.
			const FVector2D Dir = AB / Laenge;
			const FVector2D Links(Dir.Y, -Dir.X);
			const int32 Teile = FMath::Max(1, FMath::CeilToInt(Laenge / ProbenAbstandCm));
			for (int32 T = 0; T <= Teile; ++T)
			{
				const double F = static_cast<double>(T) / Teile;
				const FVector P = FMath::Lerp(A, B, F);
				for (const FQuer& Q : Quer)
				{
					const FVector2D XY = FVector2D(P.X, P.Y) + Links * (Q.Seite * Q.Abstand);
					Beanspruche(Ziele, XY.X, XY.Y, P.Z + Q.Dz);
				}
			}
		}
	}

	// Kreuzungs- und Wendeplatten: Faecher vom Schwerpunkt zu den Randpunkten
	// (wie URoadNetworkGenerator::BuildIntersectionMesh) - Proben auf jedem Dreieck.
	for (const FRoadIntersection* PlattePtr : Platten)
	{
		const FRoadIntersection& Intersection = *PlattePtr;
		if (!Settings.bConformToPavement || Intersection.Polygon.Num() < 3)
		{
			continue;
		}
		bool bBauwerk = false;
		bool bMitFahrbahn = false;
		for (const FIntersectionArm& Arm : Intersection.Arms)
		{
			if (Network.Segments.IsValidIndex(Arm.SegmentId))
			{
				const FRoadSegment& S = Network.Segments[Arm.SegmentId];
				bBauwerk |= S.bIsBridge || S.bIsTunnel || S.Layer != 0;
				bMitFahrbahn |= IstFahrbahn(S);
			}
		}
		if (bBauwerk)
		{
			continue;
		}
		FVector Mitte = FVector::ZeroVector;
		for (const FVector& P : Intersection.Polygon) { Mitte += P; }
		Mitte /= static_cast<double>(Intersection.Polygon.Num());
		for (int32 i = 0; i < Intersection.Polygon.Num(); ++i)
		{
			const FVector& B = Intersection.Polygon[i];
			const FVector& C = Intersection.Polygon[(i + 1) % Intersection.Polygon.Num()];
			const double Groesse = FMath::Max(FVector::Dist2D(Mitte, B), FVector::Dist2D(B, C));
			const int32 N = FMath::Max(1, FMath::CeilToInt(Groesse / ProbenAbstandCm));
			for (int32 U = 0; U <= N; ++U)
			{
				for (int32 V = 0; V <= N - U; ++V)
				{
					const double Wb = static_cast<double>(U) / N;
					const double Wc = static_cast<double>(V) / N;
					const FVector P = Mitte * (1.0 - Wb - Wc) + B * Wb + C * Wc;
					Beanspruche(bMitFahrbahn ? PflasterZiele : WegZiele, P.X, P.Y, P.Z);
				}
			}
		}
	}

	// Zielhoehen uebernehmen: das Pflaster hat das letzte Wort ueber seine
	// Eckpunkte - Nachbarstrassen-Obergrenzen und Kreuzungsminima gelten dort
	// nicht mehr (sie waren die Ursache der schwebenden Raender).
	int32 NurWeg = 0;
	for (const TPair<int32, FPflasterZiel>& Ziel : WegZiele)
	{
		if (!PflasterZiele.Contains(Ziel.Key))
		{
			PflasterZiele.Add(Ziel.Key, Ziel.Value);
			++NurWeg;
		}
	}
	for (const TPair<int32, FPflasterZiel>& Ziel : PflasterZiele)
	{
		FCellTarget& Target = TargetHeightCm.FindOrAdd(Ziel.Key);
		Target.HeightCm = Ziel.Value.HeightCm;
		Target.NearestDistSq = 0.0;
		PavedCeilingCm.Remove(Ziel.Key);
	}
	UE_LOG(LogWbTerrain, Log,
		TEXT("Gelaende an Pflaster angeschmiegt: %d Rasterpunkte genau %.0f cm unter die naechste Pflasterprobe gesetzt, ")
		TEXT("davon %d nur von Fuss-/Radwegen beansprucht, %d Wegpunkte an eine Fahrbahn abgegeben."),
		PflasterZiele.Num(), Settings.RoadFlattenSinkCm, NurWeg, WegZiele.Num() - NurWeg);

	int32 ModifiedCount = 0;
	int32 CeilingApplied = 0;
	int32 ImGrundrissGedeckelt = 0;
	double GroessterDeckelCm = 0.0;
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

		if (Grundrisse.Num() > 0)
		{
			const FVector2D P = Tile.CellToWorld(
				Cell.Key % Tile.GridSize, Cell.Key / Tile.GridSize);
			for (const FGrundriss& G : Grundrisse)
			{
				if (HeightCm <= G.DeckelCm)
				{
					continue;
				}
				const FVector2D D = P - G.MitteCm;
				// In die Achsen des Bauwerks drehen, dann Rechteck pruefen.
				const double U = D.X * G.Cos + D.Y * G.Sin;
				const double V = -D.X * G.Sin + D.Y * G.Cos;
				if (FMath::Abs(U) <= G.HalbCm && FMath::Abs(V) <= G.HalbCm)
				{
					GroessterDeckelCm = FMath::Max<double>(
						GroessterDeckelCm, HeightCm - G.DeckelCm);
					HeightCm = G.DeckelCm;
					++ImGrundrissGedeckelt;
					break;
				}
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

	if (ImGrundrissGedeckelt > 0)
	{
		UE_LOG(LogWbTerrain, Log,
			TEXT("Bauplateau schuetzt den Grundriss: %d Zellen auf Plateauhoehe zurueckgenommen, ")
			TEXT("groesster Abtrag %.0f cm. So weit hatte die Strassenboeschung ins Haus gereicht."),
			ImGrundrissGedeckelt, GroessterDeckelCm);
	}

	if (ModifiedCount > 0)
	{
		UE_LOG(LogWbTerrain, Log,
			TEXT("Fahrbahn-Einebnung: %d Zellen auf %d Segmenten, %.0f cm unter der Mittellinie."),
			ModifiedCount, Network.Segments.Num(), Settings.RoadFlattenSinkCm);
	}

	return ModifiedCount;
}


bool UTerrainGenerator::ResolveSitePadPlateauCm(
	const FRoadNetwork& Network,
	const FTerrainSitePad& Pad,
	double& OutPlateauCm,
	double& OutRoadZCm)
{
	double BestDistanceSquared = FMath::Square(Pad.RoadSearchRadiusCm);
	bool bFound = false;
	for (const FRoadSegment& Segment : Network.Segments)
	{
		if (Segment.bIsArea || Segment.bIsBridge || Segment.bIsTunnel || Segment.Layer != 0
			|| (!Pad.PreferredStreetName.IsEmpty()
				&& !Segment.StreetName.Equals(Pad.PreferredStreetName, ESearchCase::IgnoreCase)))
		{
			continue;
		}
		const TArray<FVector>& Line = Segment.TrimmedCenterline.Num() >= 2
			? Segment.TrimmedCenterline : Segment.Centerline;
		for (int32 Index = 0; Index + 1 < Line.Num(); ++Index)
		{
			const FVector2D A(Line[Index].X, Line[Index].Y);
			const FVector2D B(Line[Index + 1].X, Line[Index + 1].Y);
			const FVector2D Delta = B - A;
			const double LengthSquared = Delta.SizeSquared();
			if (LengthSquared <= KINDA_SMALL_NUMBER)
			{
				continue;
			}
			const double T = FMath::Clamp(
				FVector2D::DotProduct(Pad.RoadAnchorCm - A, Delta) / LengthSquared, 0.0, 1.0);
			const FVector2D Projected = A + Delta * T;
			const double DistanceSquared = FVector2D::DistSquared(Pad.RoadAnchorCm, Projected);
			if (DistanceSquared < BestDistanceSquared)
			{
				BestDistanceSquared = DistanceSquared;
				OutRoadZCm = FMath::Lerp(Line[Index].Z, Line[Index + 1].Z, T);
				bFound = true;
			}
		}
	}

	// Das Plateau liegt um die Bodenhoehe des Bauwerks UNTER der Fahrbahn,
	// damit der fertige Boden die Strasse trifft.
	OutPlateauCm = OutRoadZCm - Pad.AccessFloorCm;
	return bFound;
}

int32 UTerrainGenerator::FlattenSitePads(
	const FRoadNetwork& Network,
	const FTerrainGenerationSettings& Settings,
	FTerrainTile& Tile) const
{
	if (!Settings.bFlattenSitePads || !Tile.IsValid() || Settings.SitePads.Num() == 0)
	{
		return 0;
	}

	int32 Changed = 0;
	for (const FTerrainSitePad& Pad : Settings.SitePads)
	{
		if (Pad.RadiusCm <= 0.0)
		{
			continue;
		}

		// Die Hoehe kommt aus der naechsten Fahrbahn am Zufahrtsanker -
		// aus DEMSELBEN Helfer, den auch FlattenUnderRoads befragt.
		double RoadZCm = 0.0;
		double PlateauAusHelfer = 0.0;
		const bool bRoadFound =
			ResolveSitePadPlateauCm(Network, Pad, PlateauAusHelfer, RoadZCm);

		if (!bRoadFound)
		{
			UE_LOG(LogWbTerrain, Warning,
				TEXT("Bauplateau bei (%.0f, %.0f) uebersprungen: keine Fahrbahn im Umkreis von %.0f m."),
				Pad.CenterCm.X, Pad.CenterCm.Y, Pad.RoadSearchRadiusCm / CmPerMeter);
			continue;
		}

		// Das Plateau liegt um die Bodenhoehe des Bauwerks UNTER der Fahrbahn,
		// damit der fertige Boden die Strasse trifft.
		const double PlateauCm = PlateauAusHelfer;
		const double SlopeRun = FMath::Max(0.0, Pad.SlopeRunCm);
		const double AussenRadius = Pad.RadiusCm + SlopeRun;

		int32 EbenCount = 0;
		double AbtragCm = 0.0;
		double AuftragCm = 0.0;

		ForEachDiscCell(Tile, Pad.CenterCm, AussenRadius,
			[&Tile, &Pad, PlateauCm, SlopeRun, &Changed, &EbenCount, &AbtragCm, &AuftragCm](int32 CellIndex)
		{
			const int32 X = CellIndex % Tile.GridSize;
			const int32 Y = CellIndex / Tile.GridSize;
			const FVector2D P = Tile.CellToWorld(X, Y);
			const double Distance = FVector2D::Distance(P, Pad.CenterCm);
			const double Gewachsen = Tile.GetHeightCm(X, Y);

			double ZielCm = PlateauCm;
			if (Distance > Pad.RadiusCm && SlopeRun > 0.0)
			{
				// Boeschung: linear vom Plateau auf das gewachsene Gelaende.
				const double T = FMath::Clamp((Distance - Pad.RadiusCm) / SlopeRun, 0.0, 1.0);
				ZielCm = FMath::Lerp(PlateauCm, Gewachsen, T);
			}
			else
			{
				++EbenCount;
				AbtragCm = FMath::Max(AbtragCm, Gewachsen - PlateauCm);
				AuftragCm = FMath::Max(AuftragCm, PlateauCm - Gewachsen);
			}

			Changed += SetHeightIfChanged(Tile, X, Y, static_cast<float>(ZielCm)) ? 1 : 0;
		});

		UE_LOG(LogWbTerrain, Log,
			TEXT("Bauplateau bei (%.0f, %.0f): Hoehe %.0f cm aus der Fahrbahn (%.0f cm) abgeleitet, ")
			TEXT("%d ebene Stuetzpunkte, groesster Abtrag %.2f m, groesster Auftrag %.2f m."),
			Pad.CenterCm.X, Pad.CenterCm.Y, PlateauCm, RoadZCm,
			EbenCount, AbtragCm / CmPerMeter, AuftragCm / CmPerMeter);
	}

	return Changed;
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
