// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/HeightmapImporter.h"

#include "WiesbadenReal.h"

#include "GIS/GeoCoordinateConverter.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	/** Zentimeter pro Meter - zentral, damit Sampler und Generator identisch rechnen. */
	constexpr double CmPerMeter = 100.0;

	/**
	 * Zerlegt eine Zeile in Whitespace-getrennte Tokens. ParseIntoArrayWS
	 * behandelt Leerzeichen UND Tabs; OSM- und GDAL-Exporte mischen beides.
	 */
	int32 TokenizeWhitespace(const FString& Line, TArray<FString>& OutTokens)
	{
		OutTokens.Reset();
		FString Trimmed = Line.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			return 0;
		}
		Trimmed.ParseIntoArrayWS(OutTokens, /*ExtraDelims=*/nullptr, /*bCullEmpty=*/true);
		return OutTokens.Num();
	}

	/**
	 * Zerlegt eine ESRI-Grid-Kopfzeile der Form "KEY value" in Key und Value.
	 * Keys sind case-insensitiv (es existieren NODATA_value, nodata_value und
	 * NODATA_VALUE in freier Wildbahn). @return false bei Leer-/Datenzeilen.
	 */
	bool SplitHeaderLine(const FString& Line, FString& OutKey, FString& OutValue)
	{
		FString Trimmed = Line.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			return false;
		}

		int32 SpaceIndex = INDEX_NONE;
		if (!Trimmed.FindChar(TCHAR(' '), SpaceIndex))
		{
			Trimmed.FindChar(TCHAR('\t'), SpaceIndex);
		}

		if (SpaceIndex == INDEX_NONE || SpaceIndex == 0)
		{
			return false;
		}

		OutKey = Trimmed.Left(SpaceIndex).ToLower();
		OutValue = Trimmed.Mid(SpaceIndex + 1).TrimStartAndEnd();
		return !OutKey.IsEmpty() && !OutValue.IsEmpty();
	}

	/**
	 * Liest ein einzelnes 16-Bit-Wort an Position ByteOffset.
	 * Inline statt FMemory::Memcpy, weil die Endianness ohnehin aufgeloest
	 * werden muss und ein Shift-Paar schneller ist als ein Byte-Swap-Durchlauf.
	 */
	int16 ReadInt16(const TArray<uint8>& Bytes, int32 ByteOffset, bool bBigEndian)
	{
		const uint16 Raw = bBigEndian
			? static_cast<uint16>((static_cast<uint16>(Bytes[ByteOffset]) << 8) | static_cast<uint16>(Bytes[ByteOffset + 1]))
			: static_cast<uint16>(static_cast<uint16>(Bytes[ByteOffset]) | (static_cast<uint16>(Bytes[ByteOffset + 1]) << 8));
		return static_cast<int16>(Raw);
	}
}

bool FHeightmapRaster::SampleBilinearGeo(double Longitude, double Latitude, float& OutHeightMeters) const
{
	OutHeightMeters = 0.0f;

	if (!IsValid() || !FMath::IsFinite(Longitude) || !FMath::IsFinite(Latitude))
	{
		return false;
	}

	// Fraktionale Zellkoordinaten. Latitude ist gegenlaeufig zur Zeile:
	// Zeile 0 ist die noerdlichste (= maximale Breite), daher MaxLatitude - Lat.
	const double MaxLatitude = GetMaxLatitude();
	const double U = FMath::Clamp((Longitude - MinLongitude) / CellSizeX, 0.0, static_cast<double>(Width - 1));
	const double V = FMath::Clamp((MaxLatitude - Latitude) / CellSizeY, 0.0, static_cast<double>(Height - 1));

	const int32 X0 = FMath::FloorToInt(U);
	const int32 Y0 = FMath::FloorToInt(V);
	const int32 X1 = FMath::Min(X0 + 1, Width - 1);
	const int32 Y1 = FMath::Min(Y0 + 1, Height - 1);

	const double FracX = U - static_cast<double>(X0);
	const double FracY = V - static_cast<double>(Y0);

	// Bilineare Gewichte, NoData-faehig renormalisiert.
	double Weight00 = (1.0 - FracX) * (1.0 - FracY);
	double Weight10 = FracX * (1.0 - FracY);
	double Weight01 = (1.0 - FracX) * FracY;
	double Weight11 = FracX * FracY;

	const float H00 = GetSample(X0, Y0);
	const float H10 = GetSample(X1, Y0);
	const float H01 = GetSample(X0, Y1);
	const float H11 = GetSample(X1, Y1);

	double WeightedSum = 0.0;
	double TotalWeight = 0.0;

	if (!IsNoData(H00)) { WeightedSum += Weight00 * H00; TotalWeight += Weight00; }
	if (!IsNoData(H10)) { WeightedSum += Weight10 * H10; TotalWeight += Weight10; }
	if (!IsNoData(H01)) { WeightedSum += Weight01 * H01; TotalWeight += Weight01; }
	if (!IsNoData(H11)) { WeightedSum += Weight11 * H11; TotalWeight += Weight11; }

	if (TotalWeight <= UE_DOUBLE_KINDA_SMALL_NUMBER)
	{
		// Alle vier Stuetzwerte NoData (z. B. Wasserflaechen im SRTM-Quellmaterial).
		return false;
	}

	OutHeightMeters = static_cast<float>(WeightedSum / TotalWeight);
	return true;
}

void FHeightmapRaster::GetHeightRange(float& OutMinMeters, float& OutMaxMeters, int32& OutNoDataCount) const
{
	OutMinMeters = TNumericLimits<float>::Max();
	OutMaxMeters = TNumericLimits<float>::Lowest();
	OutNoDataCount = 0;

	for (const float Sample : Samples)
	{
		if (IsNoData(Sample))
		{
			++OutNoDataCount;
			continue;
		}
		OutMinMeters = FMath::Min(OutMinMeters, Sample);
		OutMaxMeters = FMath::Max(OutMaxMeters, Sample);
	}

	if (OutMinMeters > OutMaxMeters)
	{
		// Raster besteht vollstaendig aus NoData.
		OutMinMeters = 0.0f;
		OutMaxMeters = 0.0f;
	}
}

double FRasterHeightSampler::SampleHeightCm(const FVector2D& WorldXY) const
{
	if (!HasValidData())
	{
		return Raster ? Raster->VerticalReferenceMeters * CmPerMeter : 0.0;
	}

	// Unreal -> WGS84. Die Z-Komponente der Eingabe wird ignoriert: gesampelt
	// wird ausschliesslich die X/Y-Position, sonst wuerde eine bereits auf
	// Terrainhoehe liegende Position durch die Ruecktransformation erneut um
	// ihre eigene Hoehe verschoben (Rueckkopplung).
	const FGeoCoordinate Geo = Converter->UnrealToGeo(FVector(WorldXY.X, WorldXY.Y, 0.0));

	float HeightMeters = 0.0f;
	if (!Raster->SampleBilinearGeo(Geo.Longitude, Geo.Latitude, HeightMeters))
	{
		return Raster->VerticalReferenceMeters * CmPerMeter;
	}

	return (static_cast<double>(HeightMeters) - Raster->VerticalReferenceMeters) * CmPerMeter;
}

bool FRasterHeightSampler::HasValidData() const
{
	return Raster != nullptr
		&& Raster->IsValid()
		&& Converter != nullptr
		&& Converter->IsInitialized();
}

FHeightmapImportResult UHeightmapImporter::ImportFile(const FString& FilePath, FHeightmapRaster& OutRaster)
{
	const FString Extension = FPaths::GetExtension(FilePath).ToLower();

	if (Extension == TEXT("asc") || Extension == TEXT("txt") || Extension == TEXT("grd"))
	{
		return ImportAsciiGrid(FilePath, OutRaster);
	}

	if (Extension == TEXT("hgt"))
	{
		return ImportSrtmHgt(FilePath, OutRaster);
	}

	FHeightmapImportResult Result;
	Result.ErrorMessage = FString::Printf(
		TEXT("Unbekanntes DEM-Format '.%s'. Unterstuetzt: .asc (ESRI ASCII Grid) und .hgt (SRTM). ")
		TEXT("Andere Rohformate ueber ImportRaw16Bit mit expliziten Parametern lesen, GeoTIFF zuvor ")
		TEXT("mit 'gdal_translate -of AAIGrid' konvertieren."),
		*Extension);
	UE_LOG(LogWbTerrain, Error, TEXT("%s"), *Result.ErrorMessage);
	return Result;
}

FHeightmapImportResult UHeightmapImporter::ImportAsciiGrid(const FString& FilePath, FHeightmapRaster& OutRaster)
{
	FHeightmapImportResult Result;
	const double StartTime = FPlatformTime::Seconds();

	OutRaster = FHeightmapRaster();

	if (FilePath.IsEmpty())
	{
		Result.ErrorMessage = TEXT("Leerer Dateipfad.");
		return Result;
	}

	if (!FPaths::FileExists(FilePath))
	{
		Result.ErrorMessage = FString::Printf(
			TEXT("DEM-Datei nicht gefunden: %s. Copernicus DEM mit 'gdal_translate -of AAIGrid' exportieren."),
			*FilePath);
		UE_LOG(LogWbTerrain, Error, TEXT("%s"), *Result.ErrorMessage);
		return Result;
	}

	FString FileContent;
	if (!FFileHelper::LoadFileToString(FileContent, *FilePath))
	{
		Result.ErrorMessage = FString::Printf(TEXT("DEM-Datei konnte nicht gelesen werden: %s"), *FilePath);
		UE_LOG(LogWbTerrain, Error, TEXT("%s"), *Result.ErrorMessage);
		return Result;
	}

	if (FileContent.IsEmpty())
	{
		Result.ErrorMessage = FString::Printf(TEXT("DEM-Datei ist leer: %s"), *FilePath);
		UE_LOG(LogWbTerrain, Error, TEXT("%s"), *Result.ErrorMessage);
		return Result;
	}

	TArray<FString> Lines;
	FileContent.ParseIntoArrayLines(Lines, /*bCullEmpty=*/false);

	int32 NCols = 0;
	int32 NRows = 0;
	double Xll = 0.0;
	double Yll = 0.0;
	double CellSizeX = 0.0;
	double CellSizeY = 0.0;
	float NoData = -9999.0f;
	bool bXllCenter = false;
	bool bYllCenter = false;
	int32 DataStartLine = INDEX_NONE;

	for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
	{
		FString Key;
		FString Value;
		if (!SplitHeaderLine(Lines[LineIndex], Key, Value))
		{
			// Leere Zeilen zwischen Kopf und Daten sind erlaubt und werden
			// uebersprungen; die erste Zeile ohne "KEY value"-Struktur ist die
			// erste Datenzeile.
			if (Lines[LineIndex].TrimStartAndEnd().IsEmpty())
			{
				continue;
			}
			DataStartLine = LineIndex;
			break;
		}

		if (Key == TEXT("ncols")) { NCols = FCString::Atoi(*Value); }
		else if (Key == TEXT("nrows")) { NRows = FCString::Atoi(*Value); }
		else if (Key == TEXT("xllcorner")) { Xll = FCString::Atod(*Value); bXllCenter = false; }
		else if (Key == TEXT("xllcenter")) { Xll = FCString::Atod(*Value); bXllCenter = true; }
		else if (Key == TEXT("yllcorner")) { Yll = FCString::Atod(*Value); bYllCenter = false; }
		else if (Key == TEXT("yllcenter")) { Yll = FCString::Atod(*Value); bYllCenter = true; }
		else if (Key == TEXT("cellsize")) { CellSizeX = CellSizeY = FCString::Atod(*Value); }
		else if (Key == TEXT("dx")) { CellSizeX = FCString::Atod(*Value); }
		else if (Key == TEXT("dy")) { CellSizeY = FCString::Atod(*Value); }
		else if (Key == TEXT("nodata_value") || Key == TEXT("nodata")) { NoData = FCString::Atof(*Value); }
		// Eine Zeile, deren "Key" keine bekannte Kopfzeile ist, ist keine
		// Header-Zeile: "100.0 110.0 120.0" waere sonst Key="100.0". Nicht-
		// numerische Fremd-Keys (z. B. "byteorder") werden uebersprungen
		// (GDAL schreibt gelegentlich zusaetzliche Zeilen in den Kopf), die
		// erste numerische Zeile beginnt die Rasterdaten.
		else if (Key.IsNumeric())
		{
			// Numerischer "Key" -> Datenzeile (z. B. "100.0 110.0 120.0").
			DataStartLine = LineIndex;
			break;
		}
	}

	if (DataStartLine == INDEX_NONE)
	{
		Result.ErrorMessage = TEXT("ESRI-Grid enthaelt keine Datenzeilen.");
		UE_LOG(LogWbTerrain, Error, TEXT("%s"), *Result.ErrorMessage);
		return Result;
	}

	if (NCols <= 0 || NRows <= 0)
	{
		Result.ErrorMessage = FString::Printf(TEXT("Ungueltige Grid-Dimensionen %dx%d."), NCols, NRows);
		UE_LOG(LogWbTerrain, Error, TEXT("%s"), *Result.ErrorMessage);
		return Result;
	}

	if (CellSizeX <= 0.0 || CellSizeY <= 0.0)
	{
		Result.ErrorMessage = TEXT("Ungueltige Zellgroesse (cellsize/dx/dy fehlt oder <= 0).");
		UE_LOG(LogWbTerrain, Error, TEXT("%s"), *Result.ErrorMessage);
		return Result;
	}

	// xllcenter/yllcenter geben die ZELLMITTE an; der Raster-Ursprung ist die
	// untere linke ECKE. Um eine halbe Zelle korrigieren.
	if (bXllCenter) { Xll -= CellSizeX * 0.5; }
	if (bYllCenter) { Yll -= CellSizeY * 0.5; }

	const int32 SampleCount = NCols * NRows;
	OutRaster.Samples.SetNumUninitialized(SampleCount);

	// Token-Stream statt strikter "eine Zeile = eine Rasterzeile": manche
	// Exporte brechen lange Zeilen um. Es werden exakt NCols*NRows Werte
	// konsumiert; ein abgeschnittenes Dateiende wird weiter unten erkannt.
	int32 SampleIndex = 0;
	for (int32 LineIndex = DataStartLine; LineIndex < Lines.Num() && SampleIndex < SampleCount; ++LineIndex)
	{
		TArray<FString> Tokens;
		TokenizeWhitespace(Lines[LineIndex], Tokens);
		for (const FString& Token : Tokens)
		{
			if (SampleIndex >= SampleCount)
			{
				break;
			}
			OutRaster.Samples[SampleIndex++] = FCString::Atof(*Token);
		}
	}

	if (SampleIndex != SampleCount)
	{
		OutRaster = FHeightmapRaster();
		Result.ErrorMessage = FString::Printf(
			TEXT("ESRI-Grid unvollstaendig: %d von %d Hoehenwerten gefunden (Dateiende abgeschnitten?)."),
			SampleIndex, SampleCount);
		UE_LOG(LogWbTerrain, Error, TEXT("%s"), *Result.ErrorMessage);
		return Result;
	}

	OutRaster.Width = NCols;
	OutRaster.Height = NRows;
	OutRaster.MinLongitude = Xll;
	OutRaster.MinLatitude = Yll;
	OutRaster.CellSizeX = CellSizeX;
	OutRaster.CellSizeY = CellSizeY;
	OutRaster.NoDataValue = NoData;

	OutRaster.GetHeightRange(Result.MinHeightMeters, Result.MaxHeightMeters, Result.NoDataSampleCount);

	Result.bSuccess = OutRaster.IsValid();
	Result.Width = NCols;
	Result.Height = NRows;
	Result.DurationSeconds = FPlatformTime::Seconds() - StartTime;

	UE_LOG(LogWbTerrain, Log,
		TEXT("DEM importiert: %s (%.1f MB Rohdaten)."), *Result.ToString(), FileContent.Len() / (1024.0 * 1024.0));

	return Result;
}

FHeightmapImportResult UHeightmapImporter::ImportRaw16Bit(
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
	FHeightmapRaster& OutRaster)
{
	FHeightmapImportResult Result;
	const double StartTime = FPlatformTime::Seconds();

	OutRaster = FHeightmapRaster();

	if (Width <= 0 || Height <= 0)
	{
		Result.ErrorMessage = FString::Printf(TEXT("Ungueltige Raster-Dimensionen %dx%d."), Width, Height);
		return Result;
	}

	if (CellSizeX <= 0.0 || CellSizeY <= 0.0)
	{
		Result.ErrorMessage = TEXT("Ungueltige Zellgroesse (muss > 0 sein).");
		return Result;
	}

	if (!FPaths::FileExists(FilePath))
	{
		Result.ErrorMessage = FString::Printf(TEXT("DEM-Datei nicht gefunden: %s"), *FilePath);
		UE_LOG(LogWbTerrain, Error, TEXT("%s"), *Result.ErrorMessage);
		return Result;
	}

	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *FilePath))
	{
		Result.ErrorMessage = FString::Printf(TEXT("DEM-Datei konnte nicht gelesen werden: %s"), *FilePath);
		UE_LOG(LogWbTerrain, Error, TEXT("%s"), *Result.ErrorMessage);
		return Result;
	}

	const int64 ExpectedBytes = static_cast<int64>(Width) * static_cast<int64>(Height) * 2;
	if (Bytes.Num() < ExpectedBytes)
	{
		Result.ErrorMessage = FString::Printf(
			TEXT("Datei zu klein: %lld Bytes, erwartet %lld fuer %dx%d int16."),
			static_cast<long long>(Bytes.Num()), static_cast<long long>(ExpectedBytes), Width, Height);
		UE_LOG(LogWbTerrain, Error, TEXT("%s"), *Result.ErrorMessage);
		return Result;
	}

	const int32 SampleCount = Width * Height;
	OutRaster.Samples.SetNumUninitialized(SampleCount);
	for (int32 Index = 0; Index < SampleCount; ++Index)
	{
		const int16 Raw = ReadInt16(Bytes, Index * 2, bBigEndian);
		OutRaster.Samples[Index] = static_cast<float>(static_cast<double>(Raw) * HeightScale + HeightOffset);
	}

	OutRaster.Width = Width;
	OutRaster.Height = Height;
	OutRaster.MinLongitude = MinLongitude;
	OutRaster.MinLatitude = MinLatitude;
	OutRaster.CellSizeX = CellSizeX;
	OutRaster.CellSizeY = CellSizeY;
	OutRaster.NoDataValue = NoDataValue;

	OutRaster.GetHeightRange(Result.MinHeightMeters, Result.MaxHeightMeters, Result.NoDataSampleCount);

	Result.bSuccess = true;
	Result.Width = Width;
	Result.Height = Height;
	Result.DurationSeconds = FPlatformTime::Seconds() - StartTime;

	UE_LOG(LogWbTerrain, Log, TEXT("DEM-Rohdaten importiert: %s"), *Result.ToString());

	return Result;
}

bool UHeightmapImporter::ParseSrtmTileOrigin(
	const FString& FileNameOrPath,
	double& OutMinLongitude,
	double& OutMinLatitude)
{
	OutMinLongitude = 0.0;
	OutMinLatitude = 0.0;

	const FString Base = FPaths::GetBaseFilename(FileNameOrPath).ToUpper();

	int32 NSIndex = INDEX_NONE;
	if (!Base.FindChar(TCHAR('N'), NSIndex))
	{
		Base.FindChar(TCHAR('S'), NSIndex);
	}
	int32 EWIndex = INDEX_NONE;
	if (!Base.FindChar(TCHAR('E'), EWIndex))
	{
		Base.FindChar(TCHAR('W'), EWIndex);
	}

	if (NSIndex == INDEX_NONE || EWIndex == INDEX_NONE
		|| NSIndex + 3 > Base.Len() || EWIndex + 4 > Base.Len())
	{
		return false;
	}

	const int32 LatDegrees = FCString::Atoi(*Base.Mid(NSIndex + 1, 2));
	const int32 LonDegrees = FCString::Atoi(*Base.Mid(EWIndex + 1, 3));

	if (LatDegrees > 90 || LonDegrees > 180)
	{
		return false;
	}

	// Der Dateiname bezeichnet die SUEDWEST-Ecke; die Kachel spannt von dort
	// 1 Grad nach Norden und Osten. Fuer die Suedhalbkugel bzw. westliche
	// Laengen ist die genannte Zahl der Betrag, das Vorzeichen negativ:
	// "S01W001" deckt -1..0 Grad Breite und -1..0 Grad Laenge ab.
	OutMinLatitude = (Base[NSIndex] == TCHAR('N')) ? LatDegrees : -LatDegrees;
	OutMinLongitude = (Base[EWIndex] == TCHAR('W')) ? -LonDegrees : LonDegrees;

	return true;
}

FHeightmapImportResult UHeightmapImporter::ImportSrtmHgt(const FString& FilePath, FHeightmapRaster& OutRaster)
{
	FHeightmapImportResult Result;
	const double StartTime = FPlatformTime::Seconds();

	// SRTM-Kacheln haben eine feste Groesse: 3601x3601 (1 Bogensekunde,
	// SRTM-1) bzw. 1201x1201 (3 Bogensekunden, SRTM-3). Die Dateigroesse
	// identifiziert die Aufloesung eindeutig.
	const int64 FileSize = IFileManager::Get().FileSize(*FilePath);
	int32 Dimension = 0;
	if (FileSize == static_cast<int64>(3601) * 3601 * 2)
	{
		Dimension = 3601;
	}
	else if (FileSize == static_cast<int64>(1201) * 1201 * 2)
	{
		Dimension = 1201;
	}

	if (Dimension == 0)
	{
		Result.ErrorMessage = FString::Printf(
			TEXT("Datei %s hat keine SRTM-Kachelgroesse (%lld Bytes). Erwartet 3601x3601 oder 1201x1201 int16."),
			*FilePath, static_cast<long long>(FileSize));
		UE_LOG(LogWbTerrain, Error, TEXT("%s"), *Result.ErrorMessage);
		return Result;
	}

	// Georeferenz aus dem Dateinamen. Der Name bezeichnet die SUEDWEST-Ecke:
	// "N50E008.hgt" deckt 50..51 Grad Nord und 8..9 Grad Ost ab.
	//
	// Hier stand zuvor "Nordwest-Ecke" und entsprechend MinLatitude = Breite - 1.
	// Das verschob das gesamte Hoehenmodell um 1 Grad (~111 km) nach Sueden;
	// Wiesbaden bekam damit das Relief einer voellig anderen Gegend. Der Fehler
	// war rein geografisch: die Geometrie war technisch einwandfrei und sah wie
	// glaubwuerdiges Mittelgebirge aus. Auffaellig wurde er erst am Hoehen-
	// maximum des Rasters von 891 m - der Grosse Feldberg (881 m) liegt bei
	// 50.23 Grad Nord, suedlich von 50 Grad gibt es diese Hoehe nicht.
	// Die Laenge war uebrigens schon immer als Suedwest-Ecke behandelt; nur die
	// Breite wich davon ab.
	const FString Base = FPaths::GetBaseFilename(FilePath).ToUpper();

	double MinLongitude = 0.0;
	double MinLatitude = 0.0;

	if (!ParseSrtmTileOrigin(FilePath, MinLongitude, MinLatitude))
	{
		Result.ErrorMessage = FString::Printf(
			TEXT("SRTM-Dateiname %s folgt nicht dem Schema [NS]dd[EW]ddd (z. B. N50E008.hgt).")
			TEXT("ImportRaw16Bit mit expliziter Georeferenz verwenden."),
			*Base);
		UE_LOG(LogWbTerrain, Error, TEXT("%s"), *Result.ErrorMessage);
		return Result;
	}

	// Bogensekunden -> Grad.
	const double CellSize = 1.0 / static_cast<double>(Dimension - 1);

	UE_LOG(LogWbTerrain, Log, TEXT("SRTM-Kachel %s erkannt: %dx%d, Ecke (%.3f, %.3f)."),
		*Base, Dimension, Dimension, MinLongitude, MinLatitude);

	// SRTM .hgt ist int16, Big-Endian, Meter, NoData = -32768.
	Result = ImportRaw16Bit(
		FilePath, Dimension, Dimension, MinLongitude, MinLatitude,
		CellSize, CellSize, /*NoDataValue=*/-32768.0f,
		/*bBigEndian=*/true, /*HeightScale=*/1.0, /*HeightOffset=*/0.0, OutRaster);

	Result.DurationSeconds = FPlatformTime::Seconds() - StartTime;
	return Result;
}

FRasterHeightSampler UHeightmapImporter::CreateSampler(
	const FHeightmapRaster& Raster,
	const UGeoCoordinateConverter& Converter)
{
	return FRasterHeightSampler(Raster, Converter);
}
