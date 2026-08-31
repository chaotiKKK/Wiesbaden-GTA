// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/GeoCoordinateConverter.h"
#include "GIS/HeightmapImporter.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

namespace
{
	/** Schreibt Bytes in eine temporaere Datei. Liefert den Pfad oder einen leeren String. */
	FString WriteTempFile(const TCHAR* Extension, const TArray<uint8>& Bytes)
	{
		const FString Dir = FPaths::ProjectSavedDir();
		IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);
		const FString Path = Dir / FString::Printf(
			TEXT("wb_dem_%s.%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits), Extension);
		return FFileHelper::SaveArrayToFile(Bytes, *Path) ? Path : FString();
	}

	FString WriteTempText(const TCHAR* Extension, const FString& Text)
	{
		const FString Dir = FPaths::ProjectSavedDir();
		IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);
		const FString Path = Dir / FString::Printf(
			TEXT("wb_dem_%s.%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits), Extension);
		return FFileHelper::SaveStringToFile(Text, *Path) ? Path : FString();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeightmapRasterTest,
	"WiesbadenReal.GIS.HeightmapImporter.Raster",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FHeightmapRasterTest::RunTest(const FString& Parameters)
{
	// 2x2-Raster mit einer NoData-Zelle oben rechts.
	FHeightmapRaster Raster;
	Raster.Width = 2;
	Raster.Height = 2;
	Raster.MinLongitude = 0.0;
	Raster.MinLatitude = 0.0;
	Raster.CellSizeX = 1.0;
	Raster.CellSizeY = 1.0;
	Raster.NoDataValue = -9999.0f;
	Raster.Samples = { 10.0f, -9999.0f, 30.0f, 40.0f };

	TestTrue(TEXT("Raster gueltig"), Raster.IsValid());
	TestEqual(TEXT("SampleIndex (0,0)"), Raster.GetSampleIndex(0, 0), 0);
	TestEqual(TEXT("SampleIndex (1,1)"), Raster.GetSampleIndex(1, 1), 3);
	TestTrue(TEXT("IsNoData -9999"), Raster.IsNoData(-9999.0f));
	TestTrue(TEXT("IsNoData 10 -> false"), !Raster.IsNoData(10.0f));
	TestTrue(TEXT("MaxLongitude == 1"), FMath::IsNearlyEqual(Raster.GetMaxLongitude(), 1.0, 1e-12));
	TestTrue(TEXT("MaxLatitude == 1"), FMath::IsNearlyEqual(Raster.GetMaxLatitude(), 1.0, 1e-12));

	// Bilinear in der Rastermitte: die NoData-Zelle faellt heraus, die drei
	// restlichen Stuetzwerte werden renormalisiert (20 / 0.75).
	float Height = 0.0f;
	TestTrue(TEXT("Bilinear Mitte gueltig"), Raster.SampleBilinearGeo(0.5, 0.5, Height));
	TestTrue(TEXT("Bilinear Mitte renormalisiert"), FMath::IsNearlyEqual(Height, 26.6667f, 1e-3f));

	// Suedkante: nur die untere Zeile (30, 40) -> 35.
	TestTrue(TEXT("Bilinear Suedkante"), Raster.SampleBilinearGeo(0.5, 0.0, Height) && FMath::IsNearlyEqual(Height, 35.0f, 1e-3f));

	// Ausserhalb wird auf den Randwert geklemmt (keine Klippe). Ein Punkt
	// sued-oestlich ausserhalb (Lat < MinLatitude, Lon > MaxLongitude) klemmt
	// auf die Suedost-Ecke (1,1) = 40.
	TestTrue(TEXT("Bilinear ausserhalb klemmt"), Raster.SampleBilinearGeo(100.0, -50.0, Height) && FMath::IsNearlyEqual(Height, 40.0f, 1e-3f));

	// Vollstaendig NoData liefert false.
	{
		FHeightmapRaster AllNoData = Raster;
		AllNoData.Samples = { -9999.0f, -9999.0f, -9999.0f, -9999.0f };
		TestTrue(TEXT("Alles NoData -> false"), !AllNoData.SampleBilinearGeo(0.5, 0.5, Height));
	}

	float Min = 0.0f;
	float Max = 0.0f;
	int32 NoDataCount = 0;
	Raster.GetHeightRange(Min, Max, NoDataCount);
	TestTrue(TEXT("Min == 10"), FMath::IsNearlyEqual(Min, 10.0f, 1e-3f));
	TestTrue(TEXT("Max == 40"), FMath::IsNearlyEqual(Max, 40.0f, 1e-3f));
	TestEqual(TEXT("NoDataCount == 1"), NoDataCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeightmapAsciiGridImportTest,
	"WiesbadenReal.GIS.HeightmapImporter.AsciiGrid",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FHeightmapAsciiGridImportTest::RunTest(const FString& Parameters)
{
	const FString Content =
		TEXT("ncols 3\n")
		TEXT("nrows 2\n")
		TEXT("xllcorner 8.0\n")
		TEXT("yllcorner 50.0\n")
		TEXT("cellsize 0.01\n")
		TEXT("NODATA_value -9999\n")
		TEXT("100.0 110.0 120.0\n")
		TEXT("130.0 140.0 150.0\n");

	const FString Path = WriteTempText(TEXT("asc"), Content);
	if (!TestTrue(TEXT("Temp-Datei geschrieben"), !Path.IsEmpty()))
	{
		return false;
	}

	UHeightmapImporter* Importer = NewObject<UHeightmapImporter>();
	FHeightmapRaster Raster;
	const FHeightmapImportResult Result = Importer->ImportFile(Path, Raster);

	TestTrue(TEXT("ASCII-Import erfolgreich"), Result.bSuccess);
	TestEqual(TEXT("Width == 3"), Result.Width, 3);
	TestEqual(TEXT("Height == 2"), Result.Height, 2);
	TestEqual(TEXT("Raster Width"), Raster.Width, 3);
	TestEqual(TEXT("Raster Height"), Raster.Height, 2);
	TestTrue(TEXT("MinLongitude"), FMath::IsNearlyEqual(Raster.MinLongitude, 8.0, 1e-12));
	TestTrue(TEXT("MinLatitude"), FMath::IsNearlyEqual(Raster.MinLatitude, 50.0, 1e-12));
	TestTrue(TEXT("CellSizeX"), FMath::IsNearlyEqual(Raster.CellSizeX, 0.01, 1e-12));
	TestTrue(TEXT("CellSizeY"), FMath::IsNearlyEqual(Raster.CellSizeY, 0.01, 1e-12));

	TestTrue(TEXT("Sample (0,0) Nord-West"), FMath::IsNearlyEqual(Raster.GetSample(0, 0), 100.0f, 1e-3f));
	TestTrue(TEXT("Sample (2,0) Nord-Ost"), FMath::IsNearlyEqual(Raster.GetSample(2, 0), 120.0f, 1e-3f));
	TestTrue(TEXT("Sample (0,1) Sued-West"), FMath::IsNearlyEqual(Raster.GetSample(0, 1), 130.0f, 1e-3f));
	TestTrue(TEXT("Sample (2,1) Sued-Ost"), FMath::IsNearlyEqual(Raster.GetSample(2, 1), 150.0f, 1e-3f));
	TestEqual(TEXT("Keine NoData-Zellen"), Result.NoDataSampleCount, 0);

	// Dateiendung .asc -> ASCII-Import; eine falsche Endung wird abgelehnt.
	// Der Importer loggt dabei bewusst einen Fehler - als erwarteter
	// Fehlerpfad deklarieren, sonst wertet der Automation-Runner das
	// Error-Log als Testfehler.
	AddExpectedErrorPlain(TEXT("Unbekanntes DEM-Format"), EAutomationExpectedErrorFlags::Contains, 1);
	FHeightmapRaster Unused;
	const FHeightmapImportResult Unknown = Importer->ImportFile(TEXT("/nicht/vorhanden/foo.xyz"), Unused);
	TestTrue(TEXT("Unbekanntes Format schlaegt fehl"), !Unknown.bSuccess);
	TestTrue(TEXT("Unbekanntes Format mit Meldung"), Unknown.ErrorMessage.Contains(TEXT("Unbekanntes DEM-Format")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeightmapRawImportTest,
	"WiesbadenReal.GIS.HeightmapImporter.Raw16Bit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FHeightmapRawImportTest::RunTest(const FString& Parameters)
{
	// 2x2 int16 Big-Endian: 10, -32768 (NoData), 30, 40.
	const uint8 RawBytes[] = {
		0x00, 0x0A,
		0x80, 0x00,
		0x00, 0x1E,
		0x00, 0x28,
	};
	TArray<uint8> Bytes;
	Bytes.Append(RawBytes, 8);

	const FString Path = WriteTempFile(TEXT("raw"), Bytes);
	if (!TestTrue(TEXT("Temp-Datei geschrieben"), !Path.IsEmpty()))
	{
		return false;
	}

	UHeightmapImporter* Importer = NewObject<UHeightmapImporter>();
	FHeightmapRaster Raster;
	const FHeightmapImportResult Result = Importer->ImportRaw16Bit(
		Path, 2, 2, 8.0, 50.0, 0.01, 0.01, -32768.0f,
		/*bBigEndian=*/true, /*HeightScale=*/1.0, /*HeightOffset=*/0.0, Raster);

	TestTrue(TEXT("Raw-Import erfolgreich"), Result.bSuccess);
	TestEqual(TEXT("NoDataCount == 1"), Result.NoDataSampleCount, 1);
	TestTrue(TEXT("Sample 10"), FMath::IsNearlyEqual(Raster.Samples[0], 10.0f, 1e-3f));
	TestTrue(TEXT("Sample NoData"), FMath::IsNearlyEqual(Raster.Samples[1], -32768.0f, 1e-3f));
	TestTrue(TEXT("Sample 30"), FMath::IsNearlyEqual(Raster.Samples[2], 30.0f, 1e-3f));
	TestTrue(TEXT("Sample 40"), FMath::IsNearlyEqual(Raster.Samples[3], 40.0f, 1e-3f));
	TestTrue(TEXT("Min == 10"), FMath::IsNearlyEqual(Result.MinHeightMeters, 10.0f, 1e-3f));
	TestTrue(TEXT("Max == 40"), FMath::IsNearlyEqual(Result.MaxHeightMeters, 40.0f, 1e-3f));

	// Zu kleine Datei fuer die angeforderten Dimensionen.
	// Beide Fehlerpfade loggen bewusst einen Fehler (erwartet).
	AddExpectedErrorPlain(TEXT("zu klein"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedErrorPlain(TEXT("SRTM-Kachelgroesse"), EAutomationExpectedErrorFlags::Contains, 1);
	FHeightmapRaster Unused;
	const FHeightmapImportResult TooSmall = Importer->ImportRaw16Bit(
		Path, 4, 4, 8.0, 50.0, 0.01, 0.01, -32768.0f,
		true, 1.0, 0.0, Unused);
	TestTrue(TEXT("Zu kleine Datei schlaegt fehl"), !TooSmall.bSuccess);
	TestTrue(TEXT("Zu kleine Datei mit Meldung"), TooSmall.ErrorMessage.Contains(TEXT("zu klein")));

	// Falsche SRTM-Kachelgroesse wird erkannt.
	FHeightmapRaster SrtmRaster;
	const FHeightmapImportResult WrongSize = Importer->ImportSrtmHgt(Path, SrtmRaster);
	TestTrue(TEXT("Falsche SRTM-Groesse schlaegt fehl"), !WrongSize.bSuccess);
	TestTrue(TEXT("SRTM-Groesse mit Meldung"), WrongSize.ErrorMessage.Contains(TEXT("SRTM-Kachelgroesse")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeightmapSamplerTest,
	"WiesbadenReal.GIS.HeightmapImporter.Sampler",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FHeightmapSamplerTest::RunTest(const FString& Parameters)
{
	// Konstantes 120-m-Raster, Bezugshoehe 100 m -> Abtastung am Origin = 20 m = 2000 cm.
	FHeightmapRaster Raster;
	Raster.Width = 2;
	Raster.Height = 2;
	Raster.MinLongitude = 8.0;
	Raster.MinLatitude = 50.0;
	Raster.CellSizeX = 1.0;
	Raster.CellSizeY = 1.0;
	Raster.NoDataValue = -9999.0f;
	Raster.Samples = { 120.0f, 120.0f, 120.0f, 120.0f };
	Raster.VerticalReferenceMeters = 100.0;

	UGeoCoordinateConverter* Converter = NewObject<UGeoCoordinateConverter>();
	if (!TestTrue(TEXT("Konverter initialisiert"), Converter->InitializeWithWiesbadenOrigin()))
	{
		return false;
	}

	FRasterHeightSampler Sampler = UHeightmapImporter::CreateSampler(Raster, *Converter);
	TestTrue(TEXT("Sampler hat gueltige Daten"), Sampler.HasValidData());
	TestTrue(TEXT("Abtastung Origin == 2000 cm"),
		FMath::IsNearlyEqual(Sampler.SampleHeightCm(FVector2D::ZeroVector), 2000.0, 1e-3));

	// Ohne initialisierten Konverter ist der Sampler ungueltig.
	UGeoCoordinateConverter* Uninitialized = NewObject<UGeoCoordinateConverter>();
	FRasterHeightSampler InvalidSampler = UHeightmapImporter::CreateSampler(Raster, *Uninitialized);
	TestTrue(TEXT("Sampler ohne Konverter ungueltig"), !InvalidSampler.HasValidData());

	return true;
}
