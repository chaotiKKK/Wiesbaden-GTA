// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "World/WiesbadenSolar.h"
#include "World/WiesbadenWeatherSystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWiesbadenSolarTest,
	"WiesbadenReal.Weather.Solar",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// Echter Sonnenstand ueber Wiesbaden an Referenztagen + Uhr-Kopplung des Wettersystems.
bool FWiesbadenSolarTest::RunTest(const FString& Parameters)
{
	using namespace WiesbadenSolar;
	const double Lat = WiesbadenLatitudeDeg, Lon = WiesbadenLongitudeDeg;

	// Aequinoktium 2026-03-20, wahrer Mittag (~11:34 UTC = 12:00 - Laenge/15 h + Zeitgleichung):
	// Sonne im Sueden, Hoehe = 90 - Breite.
	{
		const FSunPosition S = ComputeSunPosition(FDateTime(2026, 3, 20, 11, 34, 0), Lat, Lon);
		TestTrue(TEXT("Aequinoktium Mittag: Hoehe ~ 90 - Breite"), FMath::Abs(S.ElevationDeg - (90.0 - Lat)) < 1.0);
		TestTrue(TEXT("Aequinoktium Mittag: Azimut ~ Sued"), FMath::Abs(S.AzimuthDeg - 180.0) < 3.0);
	}
	// Sommersonnenwende Mittag ~63,4 Grad; Wintersonnenwende Mittag ~16,5 Grad, Mitternacht tief unten.
	{
		const FSunPosition Summer = ComputeSunPosition(FDateTime(2026, 6, 21, 11, 29, 0), Lat, Lon);
		TestTrue(TEXT("Sommer Mittag: Hoehe ~63 Grad"), FMath::Abs(Summer.ElevationDeg - 63.4) < 1.0);
		const FSunPosition Winter = ComputeSunPosition(FDateTime(2026, 12, 21, 11, 33, 0), Lat, Lon);
		TestTrue(TEXT("Winter Mittag: Hoehe ~16 Grad"), FMath::Abs(Winter.ElevationDeg - 16.5) < 1.0);
		const FSunPosition Night = ComputeSunPosition(FDateTime(2026, 12, 21, 0, 0, 0), Lat, Lon);
		TestTrue(TEXT("Winter Mitternacht: tief unter dem Horizont"), Night.ElevationDeg < -40.0);
	}
	// Morgens Osten (Azimut < 180), abends Westen (> 180).
	{
		const FSunPosition Morning = ComputeSunPosition(FDateTime(2026, 6, 21, 6, 0, 0), Lat, Lon);
		const FSunPosition Evening = ComputeSunPosition(FDateTime(2026, 6, 21, 17, 0, 0), Lat, Lon);
		TestTrue(TEXT("morgens Ost"), Morning.AzimuthDeg > 45.0 && Morning.AzimuthDeg < 135.0);
		TestTrue(TEXT("abends West"), Evening.AzimuthDeg > 225.0 && Evening.AzimuthDeg < 315.0);
		TestTrue(TEXT("Juni 6 Uhr UTC ueber dem Horizont"), Morning.ElevationDeg > 10.0);
	}
	// Lichtrotation: Sued-Sonne -> Licht faellt nach NORDEN (-Y, Yaw -90), Pitch = -Hoehe;
	// Ost-Sonne -> Licht faellt nach Westen (-X, Yaw 180).
	{
		const FRotator South = SunLightRotation(40.0, 180.0);
		TestTrue(TEXT("Sued-Sonne: Pitch -40"), FMath::IsNearlyEqual(South.Pitch, -40.0, 0.1));
		TestTrue(TEXT("Sued-Sonne: Yaw -90"), FMath::IsNearlyEqual(FRotator::NormalizeAxis(South.Yaw), -90.0, 0.1));
		const FRotator East = SunLightRotation(10.0, 90.0);
		TestTrue(TEXT("Ost-Sonne: Yaw 180"), FMath::IsNearlyEqual(FMath::Abs(FRotator::NormalizeAxis(East.Yaw)), 180.0, 0.1));
	}
	TestTrue(TEXT("14:30 -> 14,5 h"), FMath::IsNearlyEqual(LocalHours(FDateTime(2026, 1, 1, 14, 30, 0)), 14.5f, 0.001f));

	// Zeitquelle im Wettersystem: Systemuhr folgt der Ortszeit, feste Stunde steht
	// still und rechnet die Sonne fuer denselben Tag zu dieser Stunde.
	{
		const FDateTime Utc(2026, 6, 21, 13, 15, 0), Local(2026, 6, 21, 15, 15, 0);   // MESZ
		FWiesbadenWeatherSystem Weather;
		Weather.SetTimeSource(EWiesbadenTimeSource::SystemClock);
		Weather.UpdateClock(Utc, Local);
		TestTrue(TEXT("Systemuhr: 15,25 h"), FMath::IsNearlyEqual(Weather.GetState().TimeOfDayHours, 15.25f, 0.001f));
		TestTrue(TEXT("Systemuhr: Nachmittag, Sonne im Suedwesten"), Weather.GetState().SunAzimuthDeg > 200.0f && Weather.GetState().SunAzimuthDeg < 260.0f);
		TestFalse(TEXT("Systemuhr: Tag"), Weather.GetState().bIsNight);

		Weather.SetTimeSource(EWiesbadenTimeSource::FixedHour, 23.0f);
		Weather.UpdateClock(Utc, Local);
		Weather.Tick(10.0f);
		TestTrue(TEXT("feste Stunde: 23 h bleibt"), FMath::IsNearlyEqual(Weather.GetState().TimeOfDayHours, 23.0f, 0.001f));
		TestTrue(TEXT("feste Stunde: Nacht"), Weather.GetState().bIsNight);
	}
	return true;
}
