// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "GIS/IHeightSampler.h"

#include "CoreMinimal.h"
#include "GIS/HeightmapImporter.h"
#include "GIS/OSMTypes.h"
#include "GIS/RoadNetworkTypes.h"
#include "TerrainGenerator.generated.h"

class UGeoCoordinateConverter;

/**
 * Ein quadratisches Landscape-Raster in Weltkoordinaten.
 *
 * Orientierung (konsistent zur Georeferenzierung: +X = Ost, +Y = Sued):
 *  - WorldMinXY ist die NORDWEST-Ecke des Rasters (minimales X, minimales Y).
 *  - Spalten wachsen nach Osten (+X), Zeilen nach Sueden (+Y).
 *  - HeightsCm ist zeilenweise gespeichert, Zeile 0 = Norden.
 *
 * Die Hoehen liegen bereits relativ zur gemeinsamen Bezugshoehe vor
 * (Raster.VerticalReferenceMeters -> Z = 0), in Unreal Units (cm). Dadurch
 * kann die UE-Landscape-Import-API das Raster direkt als Heightmap verwenden
 * und Strassen/Gebaeude (die ueber denselben Sampler hoehenprojiziert wurden)
 * liegen exakt auf der Oberflaeche.
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FTerrainTile
{
	GENERATED_BODY()

	/** Anzahl Samples je Seite (z. B. 4033 = 4032 Quads). */
	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	int32 GridSize = 0;

	/** Nordwest-Ecke in Unreal Units (cm), XY-Ebene. */
	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	FVector2D WorldMinXY = FVector2D::ZeroVector;

	/** Abstand benachbarter Samples in Unreal Units (cm). */
	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	double CellSizeCm = 0.0;

	/**
	 * Hoehen in cm, zeilenweise (Zeile 0 = Norden). Groesse GridSize*GridSize.
	 * Relative Hoehe: Z = 0 entspricht der Bezugshoehe des Quell-DEMs.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	TArray<float> HeightsCm;

	bool IsValid() const
	{
		return GridSize >= 2 && CellSizeCm > 0.0 && HeightsCm.Num() == GridSize * GridSize;
	}

	int32 GetIndex(int32 X, int32 Y) const { return Y * GridSize + X; }

	/** Liefert eine Hoehe ohne Bereichspruefung. */
	float GetHeightCm(int32 X, int32 Y) const { return HeightsCm[GetIndex(X, Y)]; }

	void SetHeightCm(int32 X, int32 Y, float HeightCm) { HeightsCm[GetIndex(X, Y)] = HeightCm; }

	/** Weltposition des Sample-Mittelpunkts (X = Spalte, Y = Zeile). */
	FVector2D CellToWorld(int32 X, int32 Y) const
	{
		return FVector2D(
			WorldMinXY.X + static_cast<double>(X) * CellSizeCm,
			WorldMinXY.Y + static_cast<double>(Y) * CellSizeCm);
	}

	/**
	 * Weltposition -> Sample-Koordinaten. @return false, wenn die Position
	 * ausserhalb des Rasters liegt (dann sind OutX/OutY ungueltig).
	 */
	bool WorldToCell(const FVector2D& WorldXY, int32& OutX, int32& OutY) const
	{
		if (!IsValid())
		{
			return false;
		}

		const double FracX = (WorldXY.X - WorldMinXY.X) / CellSizeCm;
		const double FracY = (WorldXY.Y - WorldMinXY.Y) / CellSizeCm;

		if (FracX < 0.0 || FracY < 0.0 || FracX >= static_cast<double>(GridSize) || FracY >= static_cast<double>(GridSize))
		{
			return false;
		}

		OutX = FMath::Clamp(FMath::FloorToInt(FracX), 0, GridSize - 1);
		OutY = FMath::Clamp(FMath::FloorToInt(FracY), 0, GridSize - 1);
		return true;
	}

	/**
	 * Bilineare Hoehenabfrage an einer Weltposition. Am Rand geklemmt.
	 * Identisch zum Verfahren des FRasterHeightSampler, aber im Weltraum statt
	 * im Georaum - wird fuer die Gebaeude-Fussflaechen-Einebnung gebraucht.
	 */
	float SampleHeightBilinearCm(const FVector2D& WorldXY) const
	{
		if (!IsValid())
		{
			return 0.0f;
		}

		const double FracX = FMath::Clamp((WorldXY.X - WorldMinXY.X) / CellSizeCm, 0.0, static_cast<double>(GridSize - 1));
		const double FracY = FMath::Clamp((WorldXY.Y - WorldMinXY.Y) / CellSizeCm, 0.0, static_cast<double>(GridSize - 1));

		const int32 X0 = FMath::FloorToInt(FracX);
		const int32 Y0 = FMath::FloorToInt(FracY);
		const int32 X1 = FMath::Min(X0 + 1, GridSize - 1);
		const int32 Y1 = FMath::Min(Y0 + 1, GridSize - 1);

		const double Fx = FracX - static_cast<double>(X0);
		const double Fy = FracY - static_cast<double>(Y0);

		const float H00 = GetHeightCm(X0, Y0);
		const float H10 = GetHeightCm(X1, Y0);
		const float H01 = GetHeightCm(X0, Y1);
		const float H11 = GetHeightCm(X1, Y1);

		const double Top = H00 * (1.0 - Fx) + H10 * Fx;
		const double Bottom = H01 * (1.0 - Fx) + H11 * Fx;
		return static_cast<float>(Top * (1.0 - Fy) + Bottom * Fy);
	}

	/** Niedrigster/hoechster Hoehenwert in cm. */
	void GetHeightRange(float& OutMinCm, float& OutMaxCm) const;

	/**
	 * Quantisiert auf das UE-Landscape-uint16-Format [0, 65535].
	 * Die Import-API erwartet ein lineares Mapping; ZScale/ZOffset der
	 * Landscape sind daher so zu setzen, dass [MinCm, MaxCm] auf den
	 * gewuenschten Welt-Z-Bereich abgebildet wird.
	 */
	TArray<uint16> ToUEHeightmap(float MinCm, float MaxCm) const;

	/**
	 * Codiert eine Hoehe (cm) in den uint16-Wert der UE-Landscape bei
	 * gegebener LandscapeZScale. UE interpretiert den Wert als
	 * (value - 32768) / 128 * ZScale in cm; damit worldZ == HeightCm gilt,
	 * ist value = 32768 + HeightCm * 128 / ZScale (StepsPerCm = 128/ZScale).
	 * Randwerte werden auf [0, 65535] geklemmt; ZScale <= 0 liefert den
	 * neutralen Wert 32768 (= 0 cm). Datenrein und testbar - die fruehere
	 * Inline-Codierung im WorldBuilder stauchte das Relief auf ~61 %.
	 */
	static uint16 EncodeLandscapeHeightCm(double HeightCm, double ZScale);
};

/**
 * Ein gerechnetes Bauplateau: ebene Flaeche mit anschliessender Boeschung.
 *
 * Die Hoehe wird NICHT eingetragen, sondern aus der Zufahrt genommen: das
 * Grundstueck trifft die Strasse, nicht umgekehrt. Ein fester Wert waere eine
 * zweite Wahrheit neben dem Strassennetz und liefe beim naechsten
 * DEM-Wechsel davon.
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FTerrainSitePad
{
	GENERATED_BODY()

	/** Mittelpunkt des Grundstuecks in Weltkoordinaten (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	FVector2D CenterCm = FVector2D::ZeroVector;

	/** Radius der EBENEN Flaeche (cm). 0 schaltet das Plateau ab. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "0.0"))
	double RadiusCm = 0.0;

	/**
	 * Breite der Boeschung nach aussen (cm).
	 *
	 * Ohne sie endet das Plateau als senkrechte Kante im Gelaende. Muss
	 * deutlich ueber der Gitterweite (7,81 m) liegen, sonst faellt die
	 * Boeschung zwischen zwei Stuetzpunkte und die Kante bleibt - dieselbe
	 * Falle wie bei RoadFlattenMarginCm.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "0.0"))
	double SlopeRunCm = 2000.0;

	/** Punkt an der Zufahrt; die naechste Fahrbahn dort gibt die Hoehe vor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	FVector2D RoadAnchorCm = FVector2D::ZeroVector;

	/** Groesster Abstand des Ankers zur Fahrbahnachse (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "100.0"))
	double RoadSearchRadiusCm = 5000.0;

	/** Leer: naechster Abschnitt. Sonst dieselbe benannte Strasse wie die Zufahrt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	FString PreferredStreetName;

	/**
	 * Hoehe des privaten Bodens ueber dem Plateau (cm).
	 *
	 * Das Plateau wird um genau diesen Betrag UNTER die Fahrbahn gelegt,
	 * damit der fertige Boden des Bauwerks die Strasse trifft. Sonst haette
	 * man ein ebenes Grundstueck und trotzdem eine Stufe davor.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "0.0"))
	double AccessFloorCm = 0.0;

	/**
	 * Halbe Kantenlaenge des Gebaeudegrundrisses (cm). 0 schaltet die Regel ab.
	 *
	 * WOFUER: In das Plateau wird ein Haus gebaut, und durch ein Haus laeuft
	 * keine Strassenboeschung. FlattenUnderRoads hat das letzte Wort und
	 * ebnet in einem Korridor von Fahrbahnbreite/2 + RoadFlattenMarginCm
	 * (900 cm) ein - GEMESSEN am 21.09.2026 reichte dieser Korridor 6,5 m in
	 * das Erdgeschoss des SebboTower und lag dort bis zu 97 cm UEBER dem
	 * fertigen Boden. Im Portal wuchs Gras. Innerhalb dieses Grundrisses darf
	 * die Strasse das Gelaende darum nicht mehr ueber das Plateau heben.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "0.0"))
	double BuildingHalfCm = 0.0;

	/** Drehung des Grundrisses um Z (Grad), wie der Bau selbst steht. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	double BuildingYawDeg = 0.0;
};

/** Parameter der Landscape-Erzeugung. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FTerrainGenerationSettings
{
	GENERATED_BODY()

	/**
	 * Samples je Seite. 4033 entspricht einem einzelnen UE-Landscape-Tile
	 * ueblicher Groesse (4032 Quads), 8129 dem hochaufloesenden Variantenwert
	 * aus Spezifikation 4.2. Muss >= 2 sein.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	int32 GridSize = 4033;

	/** Einebnung der Flaeche unter Fahrbahnen (empfohlen). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	bool bFlattenUnderRoads = true;

	/** Gerechnete Bauplateaus fuer Grundstuecke ohne OSM-Grundriss. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	bool bFlattenSitePads = true;

	/**
	 * Grundstuecke, die ein eigenes Plateau bekommen.
	 *
	 * FlattenUnderBuildings legt fuer jeden OSM-Grundriss eines an. Bauwerke,
	 * die zur Laufzeit gespawnt werden, stehen in keinem OSM-Datensatz und
	 * bekommen deshalb keines - der SebboTower stand so quer im Hang, seine
	 * Garage 5,1 m in der Boeschung und sein Portal 1,66 m ueber der Strasse.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	TArray<FTerrainSitePad> SitePads;

	/** Einebnung der Flaeche unter Gebaeudegrundrissen (empfohlen). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	bool bFlattenUnderBuildings = true;

	/**
	 * Zusatzbreite ueber die halbe Fahrbahnbreite hinaus, in cm.
	 *
	 * Muss groesser als die Landscape-Gitterweite (7,81 m) sein, nicht nur
	 * groesser als die Fahrbahn.
	 *
	 * Entscheidend ist nicht, dass der Kreis EINEN Gitterpunkt trifft, sondern
	 * dass er die Punkte BEIDERSEITS der Fahrbahn erfasst. Wird nur einer
	 * abgesenkt, interpoliert das Landscape zwischen abgesenktem und
	 * unveraendertem Punkt - auf halber Strecke liegt die Flaeche dann wieder
	 * ueber der Strasse. Genau so blieben trotz Einebnung 30 % der
	 * Fahrbahn-Vertices verdeckt (Verdeckung im Mittel 6,2 cm, maximal 35,5 cm).
	 *
	 * 900 cm ergeben bei einer Anliegerstrasse rund 11,75 m Radius und damit
	 * gut anderthalb Gitterzellen je Seite.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	double RoadFlattenMarginCm = 900.0;

	/**
	 * Absenkung der eingeebneten Flaeche unter die Fahrbahn-Mittellinie, in cm.
	 *
	 * Die Mittellinie liegt NICHT auf Terrainhoehe: der RoadNetworkGenerator
	 * setzt Point.Z = SampleHeightCm(XY) + RoadSurfaceOffsetCm. Wird sie direkt
	 * als Gelaendehoehe geschrieben, sind Gelaende und Fahrbahndecke koplanar
	 * und die Strasse verschwindet streckenweise im Boden - gemessen waren so
	 * 26 %% der Fahrbahn-Vertices unter dem Gelaende.
	 *
	 * 14 cm bei einem Fahrbahnversatz von 20 cm bedeutet: die eingeebnete
	 * Flaeche liegt 6 cm UNTER dem urspruenglichen Gelaende, die Fahrbahn
	 * damit 20 cm darueber - knapp ueber der empirischen Sichtbarkeitsschwelle
	 * von 8 bis 15 cm (siehe RoadSurfaceOffsetCm).
	 *
	 * Vorher standen hier 45 cm bei 30 cm Versatz. Zusammen mit der damaligen
	 * Minimum-Regel in FlattenUnderRoads ergab das eine gemessene Bodenfreiheit
	 * von 91,6 cm: die Fahrbahn schwebte fast einen Meter ueber dem Gelaende und
	 * riss an jeder Kante sichtbar auf. Begruendet war beides mit einer
	 * Verdeckungsstatistik, die per Trace die Fahrbahn gegen sich selbst mass.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	double RoadFlattenSinkCm = 14.0;

	/**
	 * Gelaende zuletzt an das Pflaster anschmiegen: die vier Eckpunkte jeder
	 * Masche unter Fahrbahn, Gehweg und Kreuzungsplatte genau RoadFlattenSinkCm
	 * unter die naechste Pflasterprobe (FlattenUnderRoads). Abschaltbar nur fuer
	 * A/B-Vergleiche und die Gegenprobe im Test.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	bool bConformToPavement = true;

	/** Zusatzbreite um Gebaeudegrundrisse, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	double BuildingFlattenMarginCm = 50.0;

	/**
	 * Obergrenze des Einebnungsradius einer Strasse. Schuetzt vor
	 * pathologischen OSM-Werten (eine residential-Way mit width=200 m).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	double MaxRoadFlattenRadiusCm = 3000.0;

	/**
	 * Randzugabe fuer die Obergrenze unter der Fahrbahn in cm.
	 *
	 * Die Einebnung waehlt je Rasterzelle den NAECHSTGELEGENEN Strassenpunkt.
	 * Laufen zwei Strassen mit Hoehenunterschied dicht nebeneinander - eine
	 * Rampe neben der ebenerdigen Strasse, eine Serpentine am Hang -, kann die
	 * hoehere gewinnen und die tiefere unter der Erde verschwinden lassen.
	 * Gemessen betraf das 64.694 von 1.972.278 ebenerdigen Fahrbahnpunkten
	 * (3,3 %), im schlimmsten Fall 238 cm tief.
	 *
	 * Deshalb eine zweite, schmale Runde: Wo wirklich Asphalt liegt - halbe
	 * Fahrbahnbreite plus diese Zugabe -, darf das Gelaende NIE ueber der
	 * tiefsten dort verlaufenden Fahrbahn liegen. Ausserhalb dieses Streifens
	 * bleibt es beim naechstgelegenen Punkt, damit die Boeschung der Strasse
	 * folgt und nicht ueber 9 m weit abgetragen wird.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "0.0"))
	double PavedCeilingMarginCm = 100.0;

	/**
	 * Zusatzrand um die OSM-Ausdehnung beim Zuschnitt (Crop) in Metern.
	 * Default 200 m - das Terrain ragt dann ein Stueck ueber die Stadt
	 * hinaus. Konfigurierbar, damit die Terrain-Ausdehnung je Setup einstell-
	 * bar ist (0 = exakt die OSM-Ausdehnung, soweit das DEM sie abdeckt).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "0.0"))
	double CropMarginMeters = 200.0;
};

/**
 * Ergebnis der Terrain-Qualitaetskontrolle (BuildCity-Abschluss): Warnt im
 * Log/Details-Panel, wenn das Tile deutlich groesser als die OSM-Ausdehnung
 * ist (Verdacht auf fehlenden Crop) oder die Hoehenspanne unplausibel
 * gross/klein ist. Datenrein (headless testbar).
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FTerrainQualityReport
{
	GENERATED_BODY()

	/** True, wenn das Tile deutlich groesser als die OSM-Ausdehnung ist. */
	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	bool bWarnTileTooLarge = false;

	/** True, wenn die Hoehenspanne unplausibel gross oder klein ist. */
	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	bool bWarnHeightRangeSuspicious = false;

	/** Kompakte Warnmeldung (leer = alles ok). */
	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	FString WarningMessage;

	/**
	 * Haengt die Warnung an einen Basis-Status an (z. B. CityData->Status):
	 * "; Terrain-Warnung: <Meldung>". Ohne Warnung bleibt der Status
	 * unveraendert. Datenrein, damit Editor und Runtime identisch formatieren.
	 */
	FString AppendToStatus(const FString& BaseStatus) const
	{
		if (WarningMessage.IsEmpty())
		{
			return BaseStatus;
		}
		return BaseStatus + TEXT("; Terrain-Warnung: ") + WarningMessage;
	}
};

/** Diagnose eines Landscape-Erzeugungslaufs. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FTerrainGenerationReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	FString ErrorMessage;

	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	int32 GridSize = 0;

	/** Ausdehnung des erzeugten Tiles in Metern. */
	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	double WorldWidthMeters = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	double WorldHeightMeters = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	float MinHeightCm = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	float MaxHeightCm = 0.0f;

	/** Durch die Einebnung veraenderte Zellen. */
	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	int32 RoadFlattenedCellCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	int32 BuildingFlattenedCellCount = 0;

	/** Durch die gerechneten Bauplateaus veraenderte Zellen. */
	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	int32 SitePadFlattenedCellCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	double DurationSeconds = 0.0;

	FString ToString() const
	{
		if (!bSuccess)
		{
			return FString::Printf(TEXT("FEHLGESCHLAGEN: %s"), *ErrorMessage);
		}
		return FString::Printf(
			TEXT("OK: %dx%d Tile (%.0f x %.0f m), %.1f..%.1f m, %d/%d/%d Zellen eingeebnet ")
			TEXT("(Strasse/Gebaeude/Plateau), %.2f s"),
			GridSize, GridSize, WorldWidthMeters, WorldHeightMeters,
			MinHeightCm / 100.0, MaxHeightCm / 100.0,
			RoadFlattenedCellCount, BuildingFlattenedCellCount, SitePadFlattenedCellCount,
			DurationSeconds);
	}
};

/**
 * Erzeugt aus einem DEM-Raster eine UE-Landscape-Heightmap.
 *
 * Zwei Aufgaben:
 *  1. RESAMPLING: Das DEM liegt in geographischen Koordinaten (Grad) vor, die
 *     Landscape ist ein achsenparalleles Quadrat in Weltkoordinaten. Die
 *     Meridiankonvergenz wuerde bei naiver 1:1-Uebernahme eine sichtbare
 *     Verzerrung erzeugen; hier wird jede Weltzelle ueber den Konverter zurueck
 *     nach WGS84 gerechnet und dort bilinear aus dem DEM gesampelt (exakt).
 *  2. EINEBNUNG: Unter Fahrbahnen und Gebaeudegrundrissen wird das Terrain auf
 *     die jeweilige Bauhoehe gesetzt, damit die separat erzeugte Strassen- und
 *     Gebaeudegeometrie nicht mit der Landscape-Triangulierung z-fightet.
 *
 * Die Schritte sind getrennt und einzeln testbar: Generate() ist rein
 * datenverarbeitend, die Einebnung sind separate Methoden, die ein vorhandenes
 * Tile nachbearbeiten.
 */
/**
 * Hoehenquelle auf Basis des TERRAIN-TILES statt des rohen DEM-Rasters.
 *
 * Warum das noetig ist: Strassen und Gebaeude holten ihre Hoehe bisher aus dem
 * DEM-Raster, waehrend das Landscape ein daraus abgeleitetes Gitter mit 7,81 m
 * Maschenweite rendert. Das sind ZWEI verschiedene Oberflaechen, die nie exakt
 * uebereinstimmen - Strassen zerfielen dadurch in Flecken und Gebaeude wurden
 * horizontal abgeschnitten. Jeder Hoehenversatz war nur ein Pflaster darauf.
 *
 * Ueber diesen Sampler sitzt die Stadt auf DERSELBEN Flaeche, die auch
 * gezeichnet wird; es bleibt nur noch der Unterschied zwischen bilinearer und
 * Dreiecks-Interpolation, also wenige Zentimeter statt Metern.
 */
class WIESBADENREAL_API FTileHeightSampler final : public IHeightSampler
{
public:
	explicit FTileHeightSampler(const FTerrainTile& InTile) : Tile(&InTile) {}

	virtual bool HasValidData() const override
	{
		return Tile && Tile->IsValid();
	}

	virtual double SampleHeightCm(const FVector2D& WorldXY) const override
	{
		return (Tile && Tile->IsValid()) ? Tile->SampleHeightBilinearCm(WorldXY) : 0.0;
	}

private:
	const FTerrainTile* Tile = nullptr;
};

UCLASS(BlueprintType)
class WIESBADENREAL_API UTerrainGenerator : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Erzeugt das Landscape-Raster aus dem DEM.
	 *
	 * @param Dem         Importiertes Hoehenraster.
	 * @param Converter   Initialisierte Georeferenzierung.
	 * @param Settings    Generierungsparameter.
	 * @param OutTile     Ergebnis-Raster.
	 * @param CropBounds  Optionaler geo-Bereich (z. B. die OSM-Ausdehnung): das
	 *                    Tile deckt dann nur diesen Bereich (+ Settings.Crop-
	 *                    MarginMeters Rand, beschnitten auf das DEM) ab statt
	 *                    des kompletten DEM-Rasters. Ohne Angabe oder bei
	 *                    leerem Schnitt wird die volle DEM-Ausdehnung verwendet
	 *                    (Fallback).
	 */
	FTerrainGenerationReport Generate(
		const FHeightmapRaster& Dem,
		const UGeoCoordinateConverter& Converter,
		const FTerrainGenerationSettings& Settings,
		FTerrainTile& OutTile,
		const FGeoBounds* CropBounds = nullptr);

	/**
	 * Terrain-Qualitaetskontrolle fuer den BuildCity-Abschluss (datenrein):
	 * setzt bWarnTileTooLarge, wenn das Tile deutlich groesser als die
	 * OSM-Ausdehnung ist (Verdacht auf fehlenden Crop), und
	 * bWarnHeightRangeSuspicious, wenn die Hoehenspanne ausserhalb
	 * [MinHeightSpanMeters, MaxHeightSpanMeters] liegt (flaches/fehlendes DEM
	 * bzw. NoData-Spikes). WarningMessage fasst beide Faelle kompakt zusammen.
	 * Leerer Report = alles ok.
	 */
	static FTerrainQualityReport CheckTerrainQuality(
		const FTerrainGenerationReport& TerrainReport,
		const FGeoBounds& OsmBounds,
		const UGeoCoordinateConverter& Converter,
		double MaxTileToOsmRatio = 2.0,
		double MinHeightSpanMeters = 1.0,
		double MaxHeightSpanMeters = 3000.0);

	/**
	 * Ebnet das Terrain unter allen Fahrbahnen ein.
	 * @return Anzahl veraenderter Zellen.
	 */
	int32 FlattenUnderRoads(
		const FRoadNetwork& Network,
		const FTerrainGenerationSettings& Settings,
		FTerrainTile& Tile) const;

	/**
	 * Legt die gerechneten Bauplateaus an.
	 *
	 * Muss VOR FlattenUnderRoads laufen: die Strasse hat das letzte Wort,
	 * sonst hebt das Plateau sie am Grundstuecksrand wieder zu.
	 *
	 * @return Anzahl veraenderter Zellen.
	 */
	int32 FlattenSitePads(
		const FRoadNetwork& Network,
		const FTerrainGenerationSettings& Settings,
		FTerrainTile& Tile) const;

	/**
	 * Die Plateauhoehe eines Bauplateaus aus der naechsten Fahrbahn am Anker.
	 *
	 * Steht hier, weil ZWEI Paesse sie brauchen: FlattenSitePads legt das
	 * Plateau damit an, und FlattenUnderRoads muss wissen, wie hoch es liegt,
	 * um es im Gebaeudegrundriss nicht wieder zuzuschuetten. Zwei Kopien
	 * derselben Rechnung waeren genau die stille Zweitwahrheit, an der die
	 * Hoehe eines Tages auseinanderliefe.
	 *
	 * @return false, wenn im Suchradius keine Fahrbahn liegt. Dann wird NICHT
	 *         geraten - ein Plateau auf geratener Hoehe waere ein zweiter,
	 *         stiller Hang.
	 */
	static bool ResolveSitePadPlateauCm(
		const FRoadNetwork& Network,
		const FTerrainSitePad& Pad,
		double& OutPlateauCm,
		double& OutRoadZCm);

	/**
	 * Ebnet das Terrain unter allen geschlossenen Gebaeudegrundrissen ein.
	 * @return Anzahl veraenderter Zellen.
	 */
	int32 FlattenUnderBuildings(
		const FOSMDataSet& DataSet,
		const UGeoCoordinateConverter& Converter,
		const FTerrainGenerationSettings& Settings,
		FTerrainTile& Tile) const;
};
