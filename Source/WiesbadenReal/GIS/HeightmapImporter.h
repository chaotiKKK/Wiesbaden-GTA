// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GIS/IHeightSampler.h"
#include "HeightmapImporter.generated.h"

class UGeoCoordinateConverter;

/**
 * Ein georeferenziertes Hoehenraster (DEM).
 *
 * Orientierung wie in ESRI ASCII Grid: Zeile 0 ist die noerdlichste Zeile
 * (maximale Breite). Die Ecke (MinLongitude, MinLatitude) ist die untere linke
 * Ecke der untersten Zeile; die obere rechte Ecke ergibt sich aus
 * Zellgroesse * (Dimension - 1). Rasterzeilen sind also von Nord nach Sued
 * absteigend, Rasterzellen innerhalb einer Zeile von West nach Ost aufsteigend.
 *
 * Hoehen sind ORTHOMETRISCHE Hoehen in Metern (ueber NN/Geoid), so wie DEM-
 * Produkte sie liefern (Copernicus DEM, SRTM). Die Unterscheidung
 * orthometrisch/ellipsoidisch spielt fuer die Geometrieerzeugung keine Rolle,
 * weil Strassen, Gebaeude und Landschaft alle aus demselben Raster sampeln und
 * die Ablage auf eine gemeinsame Bezugshoehe (VerticalReferenceMeters)
 * erfolgt. Ein konstanter Geoidundulations-Offset faellt damit komplett heraus;
 * wer spaeter ein echtes Geoidgitter anschliessen will, kann das als
 * Korrekturterm vor dem Sampling einbauen.
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FHeightmapRaster
{
	GENERATED_BODY()

	/** Spaltenzahl (Ost-West). */
	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	int32 Width = 0;

	/** Zeilenzahl (Nord-Sued). */
	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	int32 Height = 0;

	/** Untere linke Ecke, Laengengrad (MinLongitude). */
	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	double MinLongitude = 0.0;

	/** Untere linke Ecke, Breitengrad (MinLatitude). */
	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	double MinLatitude = 0.0;

	/** Zellgroesse in Laengengrad je Spalte (> 0). */
	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	double CellSizeX = 0.0;

	/** Zellgroesse in Breitengrad je Zeile (> 0). */
	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	double CellSizeY = 0.0;

	/**
	 * Kennwert fuer fehlende Messwerte (z. B. -9999 in SRTM, -32768 in
	 * Copernicus). Zellen mit diesem Wert werden bei der Interpolation
	 * ausgeschlossen.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	float NoDataValue = -9999.0f;

	/**
	 * Orthometrische Hoehe in Metern, die auf Unreal Z = 0 abgebildet wird.
	 * Gemeinsame Bezugsgroesse fuer Sampler UND TerrainGenerator - beide muessen
	 * denselben Wert verwenden, sonst schweben Strassen ueber der Landschaft
	 * oder versinken darin. Default 0.0 legt den Meeresspiegel auf Z = 0; fuer
	 * bessere Float-Praezision sollte er auf die mittlere Stadthoehe
	 * (~75 m fuer Wiesbaden) gesetzt werden.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	double VerticalReferenceMeters = 0.0;

	/**
	 * Hoehenwerte in Metern, Zeile 0 = Norden, zeilenweise von West nach Ost.
	 * Groesse: Width * Height.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	TArray<float> Samples;

	/** Obere rechte Ecke, Laengengrad. */
	double GetMaxLongitude() const { return MinLongitude + CellSizeX * (Width - 1); }

	/** Obere rechte Ecke, Breitengrad. */
	double GetMaxLatitude() const { return MinLatitude + CellSizeY * (Height - 1); }

	/** True, wenn Dimensionen, Zellgroessen und Sample-Anzahl zusammenpassen. */
	bool IsValid() const
	{
		return Width >= 2 && Height >= 2
			&& CellSizeX > 0.0 && CellSizeY > 0.0
			&& Samples.Num() == Width * Height;
	}

	/** True, wenn ein Wert als NoData-Kennwert gilt. */
	bool IsNoData(float Value) const
	{
		return FMath::IsNearlyEqual(Value, NoDataValue, 1e-3);
	}

	/** Index in Samples fuer Spalte X / Zeile Y (0-basiert, Zeile 0 = Norden). */
	int32 GetSampleIndex(int32 X, int32 Y) const
	{
		return Y * Width + X;
	}

	/** Liefert einen Samplewert ohne Bereichspruefung. */
	float GetSample(int32 X, int32 Y) const
	{
		return Samples[GetSampleIndex(X, Y)];
	}

	/**
	 * Bilineare Interpolation in geographischen Koordinaten.
	 *
	 * NoData-faehig: fehlende Stuetzwerte werden aus der Gewichtung
	 * ausgeschlossen und die restlichen Gewichte renormalisiert. Am Rand wird
	 * auf die aeusserste Zelle geklemmt (keine 0 - eine 0 wuerde am Kartenrand
	 * eine Klippe erzeugen).
	 *
	 * @param OutHeightMeters Orthometrische Hoehe in Metern.
	 * @return false, wenn das Raster ungueltig ist oder alle vier Stuetzwerte
	 *         NoData sind.
	 */
	bool SampleBilinearGeo(double Longitude, double Latitude, float& OutHeightMeters) const;

	/** Hoechster und niedrigster gueltiger Samplewert in Metern. */
	void GetHeightRange(float& OutMinMeters, float& OutMaxMeters, int32& OutNoDataCount) const;
};

/** Diagnose eines DEM-Imports. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FHeightmapImportResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	FString ErrorMessage;

	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	int32 Width = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	int32 Height = 0;

	/** Anzahl Zellen mit NoData-Kennwert. */
	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	int32 NoDataSampleCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	float MinHeightMeters = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	float MaxHeightMeters = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	double DurationSeconds = 0.0;

	FString ToString() const
	{
		if (!bSuccess)
		{
			return FString::Printf(TEXT("FEHLGESCHLAGEN: %s"), *ErrorMessage);
		}
		return FString::Printf(
			TEXT("OK: %dx%d Raster, %.1f..%.1f m, %d NoData, %.2f s"),
			Width, Height, MinHeightMeters, MaxHeightMeters, NoDataSampleCount, DurationSeconds);
	}
};

/**
 * IHeightSampler-Implementierung ueber einem FHeightmapRaster.
 *
 * Wandelt die Unreal-Weltposition ueber den Geokoordinaten-Konverter zurueck
 * nach WGS84 und interpoliert dort bilinear. Dadurch stimmen die Hoehen exakt
 * mit den Positionen ueberein, die der RoadNetworkGenerator und der
 * BuildingGenerator aus GeoToUnrealGround beziehen - eine flat-earth-
 * Approximation des Rasterrasters in Weltkoordinaten wuerde am Rand des
 * Stadtgebiets mehrere Dezimeter abweichen.
 *
 * LEBENSDAUER: Haelt Zeiger auf Raster und Konverter. Beide muessen den
 * Sampler ueberleben. Nach Konstruktion ist der Sampler thread-safe lesbar.
 */
class WIESBADENREAL_API FRasterHeightSampler final : public IHeightSampler
{
public:
	FRasterHeightSampler(const FHeightmapRaster& InRaster, const UGeoCoordinateConverter& InConverter)
		: Raster(&InRaster)
		, Converter(&InConverter)
	{
	}

	virtual double SampleHeightCm(const FVector2D& WorldXY) const override;
	virtual bool HasValidData() const override;

private:
	const FHeightmapRaster* Raster;
	const UGeoCoordinateConverter* Converter;
};

/**
 * Importiert DEM-Daten und stellt daraus FHeightmapRaster bereit.
 *
 * Eingabeformate: ESRI ASCII Grid (das Standard-Exportformat von GDAL fuer
 * GeoTIFF-Quellen) und 16-Bit-Rohdaten. Der Import ist blockierend und gehoert
 * auf einen Worker-Thread (Raster mit 4000x4000 Zellen = 64 MB float).
 */
UCLASS(BlueprintType)
class WIESBADENREAL_API UHeightmapImporter : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Importiert eine Datei; Format wird anhand der Endung gewaehlt:
	 *  .asc/.txt/.grd -> AsciiGrid
	 *  .hgt            -> SRTM-Kachel (Groesse/Ecke aus Datei und Name)
	 *  .raw/.bin/.dem  -> nicht automatisch: ImportRaw16Bit mit Parametern verwenden
	 */
	FHeightmapImportResult ImportFile(const FString& FilePath, FHeightmapRaster& OutRaster);

	/** Importiert ein ESRI ASCII Grid (.asc). */
	FHeightmapImportResult ImportAsciiGrid(const FString& FilePath, FHeightmapRaster& OutRaster);

	/**
	 * Importiert eine SRTM-Kachel (.hgt). Aufloesung wird aus der Dateigroesse,
	 * die Georeferenz aus dem Dateinamen (Schema [NS]dd[EW]ddd) bestimmt.
	 */
	FHeightmapImportResult ImportSrtmHgt(const FString& FilePath, FHeightmapRaster& OutRaster);

	/**
	 * Liest die Georeferenz einer SRTM-Kachel aus ihrem Dateinamen
	 * (Schema [NS]dd[EW]ddd, z. B. "N50E008").
	 *
	 * KONVENTION: Der Name bezeichnet die SUEDWEST-Ecke. "N50E008" deckt
	 * 50..51 Grad Nord und 8..9 Grad Ost ab - die Kachel spannt von der
	 * genannten Ecke aus nach NORDEN und OSTEN (USGS-Definition fuer .hgt).
	 *
	 * Datenrein und ohne Datei ausfuehrbar, damit die Georeferenz testbar ist:
	 * ein Fehler hier verschiebt die gesamte Welt um Grad-Betraege, sieht aber
	 * wie voellig plausibles Gelaende aus und faellt daher sonst nicht auf.
	 *
	 * @return false, wenn der Name dem Schema nicht folgt.
	 */
	static bool ParseSrtmTileOrigin(const FString& FileNameOrPath, double& OutMinLongitude, double& OutMinLatitude);

	/**
	 * Importiert ein 16-Bit-Rohraster ohne Kopf.
	 *
	 * @param Width/Height    Dimensionen in Zellen.
	 * @param MinLongitude/MinLatitude Untere linke Ecke des untersten Pixels.
	 * @param CellSizeX/Y     Zellgroesse in Grad.
	 * @param NoDataValue     Kennwert fuer fehlende Daten (z. B. -32768).
	 * @param bBigEndian      Byte-Reihenfolge (SRTM .hgt ist Big-Endian).
	 * @param HeightScale     Multiplikator (1.0 = Meter). Bei SRTM-HGT ist der
	 *                        Rohwert bereits Meter, bei manchen Derivaten sind
	 *                        es Dezimeter oder Zentimeter.
	 * @param HeightOffset    Additiver Versatz in Metern.
	 * @param OutRaster       Zielraster.
	 */
	FHeightmapImportResult ImportRaw16Bit(
		const FString& FilePath,
		int32 Width,
		int32 Height,
		double MinLongitude,
		double MinLatitude,
		double CellSizeX,
		double CellSizeY,
		float NoDataValue,
		bool bBigEndian,
		double HeightScale,
		double HeightOffset,
		FHeightmapRaster& OutRaster);

	/**
	 * Erzeugt einen weltraum-basierten Hoehensampler. Der Aufrufer ist fuer die
	 * Lebensdauer von Raster und Konverter verantwortlich.
	 */
	static FRasterHeightSampler CreateSampler(
		const FHeightmapRaster& Raster,
		const UGeoCoordinateConverter& Converter);
};
