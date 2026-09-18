// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "World/WiesbadenWeatherFX.h"
#include "World/WiesbadenWeatherSystem.h"

namespace
{
	// Aus der Implementierung geholt statt hier wiederholt - genau diese
	// Doppelpflege liess den Test bei der Umstellung auf Lux auflaufen.
	const float MaxSunIntensityLux = FWiesbadenWeatherFXParams::GetMaxSunIntensityLux();

	/** Baut einen Wetter-Zustand mit gemischten Intensitaeten (wie WeatherSystem::Tick). */
	FWiesbadenWeatherState MakeFXState(ECityWeatherPreset Weather, ECityWeatherPreset Previous,
		float Blend01, float TimeOfDayHours, float SunElevationFactor)
	{
		FWiesbadenWeatherState State;
		State.CurrentWeather = Weather;
		State.PreviousWeather = Previous;
		State.Blend01 = Blend01;
		State.TimeOfDayHours = TimeOfDayHours;
		// Hoehe ist die einzige Darstellung; der Faktor (sin) ist abgeleitet.
		State.SunElevationDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(SunElevationFactor, -1.0f, 1.0f)));
		State.bIsNight = SunElevationFactor < 0.0f;

		const FWiesbadenWeatherIntensity Prev =
			FWiesbadenWeatherSystem::GetIntensityFor(Previous);
		const FWiesbadenWeatherIntensity Curr =
			FWiesbadenWeatherSystem::GetIntensityFor(Weather);
		State.Intensity.Rain = FMath::Lerp(Prev.Rain, Curr.Rain, Blend01);
		State.Intensity.Fog = FMath::Lerp(Prev.Fog, Curr.Fog, Blend01);
		State.Intensity.CloudCover = FMath::Lerp(Prev.CloudCover, Curr.CloudCover, Blend01);

		const float Daylight = FMath::Clamp(SunElevationFactor, 0.0f, 1.0f);
		State.AmbientLightMultiplier = 0.35f + 0.65f * Daylight;
		return State;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeatherFXParamsTest,
	"WiesbadenReal.Weather.FXParams",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWeatherFXParamsTest::RunTest(const FString& Parameters)
{
	// -- Wetterlage -> Effekt-Aktivitaet ------------------------------------
	{
		const FWiesbadenWeatherState Clear = MakeFXState(
			ECityWeatherPreset::Clear, ECityWeatherPreset::Clear, 1.0f, 12.0f, 1.0f);
		const FWiesbadenWeatherFXParams ClearParams = FWiesbadenWeatherFXParams::FromWeatherState(Clear);
		TestTrue(TEXT("Clear: kein Regen/Schnee/Blitz"),
			ClearParams.RainSpawnRate == 0.0f && ClearParams.SnowSpawnRate == 0.0f
			&& ClearParams.LightningInterval == 0.0f);

		const FWiesbadenWeatherState Rain = MakeFXState(
			ECityWeatherPreset::Rain, ECityWeatherPreset::Clear, 1.0f, 12.0f, 1.0f);
		const FWiesbadenWeatherFXParams RainParams = FWiesbadenWeatherFXParams::FromWeatherState(Rain);
		TestTrue(TEXT("Rain: Regen aktiv (1200*0.7=840)"),
			FMath::Abs(RainParams.RainSpawnRate - 840.0f) < 1e-4f && RainParams.SnowSpawnRate == 0.0f);

		const FWiesbadenWeatherState Storm = MakeFXState(
			ECityWeatherPreset::Thunderstorm, ECityWeatherPreset::Rain, 1.0f, 0.0f, -1.0f);
		const FWiesbadenWeatherFXParams StormParams = FWiesbadenWeatherFXParams::FromWeatherState(Storm);
		TestTrue(TEXT("Thunderstorm: Blitze aktiv"),
			StormParams.LightningInterval > 0.0f);

		const FWiesbadenWeatherState Fog = MakeFXState(
			ECityWeatherPreset::Fog, ECityWeatherPreset::Clear, 1.0f, 12.0f, 1.0f);
		const FWiesbadenWeatherFXParams FogParams = FWiesbadenWeatherFXParams::FromWeatherState(Fog);
		TestTrue(TEXT("Fog: hohe Nebeldichte (0.85)"),
			FMath::Abs(FogParams.FogDensity - 0.85f) < 1e-4f);

		const FWiesbadenWeatherState Snow = MakeFXState(
			ECityWeatherPreset::Snow, ECityWeatherPreset::Clear, 1.0f, 12.0f, 1.0f);
		const FWiesbadenWeatherFXParams SnowParams = FWiesbadenWeatherFXParams::FromWeatherState(Snow);
		TestTrue(TEXT("Snow: Schnee aktiv, kein Regen"),
			SnowParams.SnowSpawnRate > 0.0f && SnowParams.RainSpawnRate == 0.0f);
	}

	// -- Sanfter Uebergang (Blend) ------------------------------------------
	{
		const FWiesbadenWeatherState Half = MakeFXState(
			ECityWeatherPreset::Rain, ECityWeatherPreset::Clear, 0.5f, 12.0f, 1.0f);
		const FWiesbadenWeatherFXParams HalfParams = FWiesbadenWeatherFXParams::FromWeatherState(Half);
		TestTrue(TEXT("Uebergang: SpawnRate folgt gemischter Intensitaet (1200*0.35=420)"),
			FMath::Abs(HalfParams.RainSpawnRate - 420.0f) < 1e-4f);
	}

	// -- Licht: Tag warm, Nacht kuehl ----------------------------------------
	{
		const FWiesbadenWeatherState Noon = MakeFXState(
			ECityWeatherPreset::Clear, ECityWeatherPreset::Clear, 1.0f, 12.0f, 1.0f);
		const FWiesbadenWeatherState Midnight = MakeFXState(
			ECityWeatherPreset::Clear, ECityWeatherPreset::Clear, 1.0f, 0.0f, -1.0f);
		const FWiesbadenWeatherFXParams NoonParams = FWiesbadenWeatherFXParams::FromWeatherState(Noon);
		const FWiesbadenWeatherFXParams MidnightParams = FWiesbadenWeatherFXParams::FromWeatherState(Midnight);

		TestTrue(TEXT("Licht: Mittag heller als Mitternacht"),
			NoonParams.AmbientLightMultiplier > MidnightParams.AmbientLightMultiplier);
		TestTrue(TEXT("Licht: Mittag warm-hell (R > B)"),
			NoonParams.SunLightColor.R > NoonParams.SunLightColor.B);
		TestTrue(TEXT("Licht: Nacht kuehl-dunkel (B >= R)"),
			MidnightParams.SunLightColor.B >= MidnightParams.SunLightColor.R);
	}

	// -- Sonnen-Intensitaet (DirectionalLight im Level) -----------------------
	{
		const FWiesbadenWeatherState NoonClear = MakeFXState(
			ECityWeatherPreset::Clear, ECityWeatherPreset::Clear, 1.0f, 12.0f, 1.0f);
		const FWiesbadenWeatherFXParams NoonClearParams =
			FWiesbadenWeatherFXParams::FromWeatherState(NoonClear);
		// Erwartet: 10 lx * 1.0 * (1 - 0.6*0.15) = 9.1.
		//
		// MaxSunIntensity ist die Intensitaet einer DirectionalLight in LUX -
		// UE5-Default einer platzierten Sonne. Hier stand zuvor 3.14f: der
		// einheitenlose UE4-Altwert (Pi). Damit blieb die Stadt praktisch
		// schwarz.
		TestTrue(TEXT("Mittag Clear: volle Intensitaet"),
			FMath::Abs(NoonClearParams.SunIntensity - MaxSunIntensityLux * (1.0f - 0.6f * 0.15f)) < 1e-3f);

		const FWiesbadenWeatherState Night = MakeFXState(
			ECityWeatherPreset::Clear, ECityWeatherPreset::Clear, 1.0f, 0.0f, -1.0f);
		const FWiesbadenWeatherFXParams NightParams =
			FWiesbadenWeatherFXParams::FromWeatherState(Night);
		// Nachts bleibt eine Grundhelligkeit als Mondlicht stehen.
		//
		// Hier stand zuvor "Intensitaet 0 (Sonne untergegangen)" - und genau so
		// war es auch: Die Stadt hatte nachts ausser Scheinwerfern KEINE
		// Lichtquelle und war vollstaendig schwarz. Der Test hat das nicht
		// aufgedeckt, sondern festgeschrieben.
		//
		// Geprueft wird jetzt die Spanne: Licht vorhanden, aber ein Vielfaches
		// unter dem Tagwert. Die Untergrenze selbst haelt
		// Weather.NightLightFloor fest.
		TestTrue(
			FString::Printf(TEXT("Nacht: Restlicht vorhanden (%.3f)"), NightParams.SunIntensity),
			NightParams.SunIntensity > 0.0f);
		TestTrue(
			FString::Printf(TEXT("Nacht: deutlich dunkler als Mittag (%.3f gegen %.3f)"),
				NightParams.SunIntensity, NoonClearParams.SunIntensity),
			NightParams.SunIntensity < NoonClearParams.SunIntensity * 0.2f);

		const FWiesbadenWeatherState CloudyNoon = MakeFXState(
			ECityWeatherPreset::Cloudy, ECityWeatherPreset::Clear, 1.0f, 12.0f, 1.0f);
		const FWiesbadenWeatherFXParams CloudyParams =
			FWiesbadenWeatherFXParams::FromWeatherState(CloudyNoon);
		TestTrue(TEXT("Bewoelkt daempft die Sonne"),
			CloudyParams.SunIntensity < NoonClearParams.SunIntensity);

		const FWiesbadenWeatherState StormNoon = MakeFXState(
			ECityWeatherPreset::Thunderstorm, ECityWeatherPreset::Rain, 1.0f, 12.0f, 1.0f);
		const FWiesbadenWeatherFXParams StormParams =
			FWiesbadenWeatherFXParams::FromWeatherState(StormNoon);
		TestTrue(TEXT("Gewitter: noch daemmeriger"),
			StormParams.SunIntensity < CloudyParams.SunIntensity);

		const FWiesbadenWeatherState Morning = MakeFXState(
			ECityWeatherPreset::Clear, ECityWeatherPreset::Clear, 1.0f, 7.0f, 0.2f);
		const FWiesbadenWeatherFXParams MorningParams =
			FWiesbadenWeatherFXParams::FromWeatherState(Morning);
		TestTrue(TEXT("Frueh: schwaechere Sonne als Mittag, aber > 0"),
			MorningParams.SunIntensity > 0.0f && MorningParams.SunIntensity < NoonClearParams.SunIntensity);
	}

	// -- Determinsmus / Randwerte --------------------------------------------
	{
		const FWiesbadenWeatherState A = MakeFXState(
			ECityWeatherPreset::Thunderstorm, ECityWeatherPreset::Thunderstorm, 1.0f, 12.0f, 0.0f);
		const FWiesbadenWeatherState B = MakeFXState(
			ECityWeatherPreset::Thunderstorm, ECityWeatherPreset::Thunderstorm, 1.0f, 12.0f, 0.0f);
		const FWiesbadenWeatherFXParams P1 = FWiesbadenWeatherFXParams::FromWeatherState(A);
		const FWiesbadenWeatherFXParams P2 = FWiesbadenWeatherFXParams::FromWeatherState(B);
		TestTrue(TEXT("Deterministisch: gleiche Eingabe -> gleiche Ausgabe"),
			P1.RainSpawnRate == P2.RainSpawnRate && P1.WindSpeed == P2.WindSpeed
			&& P1.SunLightColor == P2.SunLightColor && P1.SunIntensity == P2.SunIntensity);

		const FWiesbadenWeatherState ClearNoon = MakeFXState(
			ECityWeatherPreset::Clear, ECityWeatherPreset::Clear, 1.0f, 12.0f, 1.0f);
		const FWiesbadenWeatherFXParams Params = FWiesbadenWeatherFXParams::FromWeatherState(ClearNoon);
		TestTrue(TEXT("RGB im gueltigen Bereich"),
			Params.SunLightColor.R >= 0.0f && Params.SunLightColor.R <= 1.0f
			&& Params.SunLightColor.G >= 0.0f && Params.SunLightColor.G <= 1.0f
			&& Params.SunLightColor.B >= 0.0f && Params.SunLightColor.B <= 1.0f);
		TestTrue(TEXT("SunIntensity im Rahmen 0..MaxSunIntensityLux"),
			Params.SunIntensity >= 0.0f && Params.SunIntensity <= MaxSunIntensityLux);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeatherFXAssetPathsTest,
	"WiesbadenReal.Weather.FXAssetPaths",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWeatherFXAssetPathsTest::RunTest(const FString& Parameters)
{
	// Alle fuenf Effekt-Typen haben einen kanonischen, eindeutigen Content-Pfad
	// (identisch zu WeatherFXCatalog.json, Feld "path").
	const EWiesbadenWeatherFXType Types[] = {
		EWiesbadenWeatherFXType::Rain, EWiesbadenWeatherFXType::Snow,
		EWiesbadenWeatherFXType::Fog, EWiesbadenWeatherFXType::Clouds,
		EWiesbadenWeatherFXType::Storm };
	// Nebel und Wolken sind engine-nativ (Hoehennebel + Wolkenschicht, siehe
	// UpdateSky) und erwarten KEIN Niagara-Asset - leerer Pfad. Vorher suchte
	// die Komponente hier zwei Systeme, die es nie gab.
	TestTrue(TEXT("Nebel: kein Niagara-Pfad"),
		UWiesbadenWeatherFXComponent::GetDefaultAssetPath(EWiesbadenWeatherFXType::Fog).IsEmpty());
	TestTrue(TEXT("Wolken: kein Niagara-Pfad"),
		UWiesbadenWeatherFXComponent::GetDefaultAssetPath(EWiesbadenWeatherFXType::Clouds).IsEmpty());

	TSet<FString> Seen;
	for (const EWiesbadenWeatherFXType Type : Types)
	{
		const FString Path = UWiesbadenWeatherFXComponent::GetDefaultAssetPath(Type);
		if (Path.IsEmpty())
		{
			continue;   // engine-nativ
		}
		TestTrue(TEXT("Pfad eindeutig"), !Seen.Contains(Path));
		Seen.Add(Path);
	}
	TestEqual(TEXT("Drei Partikel-Effekte bleiben"), Seen.Num(), 3);

	// Exakte Pfade (Konvention: /Game/Niagara/NS_Weather<Name>.NS_Weather<Name>).
	TestEqual(TEXT("Rain-Pfad"),
		UWiesbadenWeatherFXComponent::GetDefaultAssetPath(EWiesbadenWeatherFXType::Rain),
		TEXT("/Game/Niagara/NS_WeatherRain.NS_WeatherRain"));
	TestEqual(TEXT("Snow-Pfad"),
		UWiesbadenWeatherFXComponent::GetDefaultAssetPath(EWiesbadenWeatherFXType::Snow),
		TEXT("/Game/Niagara/NS_WeatherSnow.NS_WeatherSnow"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeatherFXFixedBoundsTest,
	"WiesbadenReal.Weather.FXFixedBounds",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWeatherFXFixedBoundsTest::RunTest(const FString& Parameters)
{
	// Ungueltige Box (ForceInit): IsValid == false -> nicht nutzbar.
	TestFalse(TEXT("Ungueltige Box (ForceInit) -> nicht nutzbar"),
		UWiesbadenWeatherFXComponent::HasUsableFixedBounds(FBox(ForceInit)));

	// Null-Box (0,0,0)-(0,0,0): gueltig, aber 0-Extent -> nicht nutzbar
	// (das ist der Zustand, wenn im Editor keine Fixed Bounds gesetzt wurden).
	TestFalse(TEXT("Null-Box (0-Extent) -> nicht nutzbar"),
		UWiesbadenWeatherFXComponent::HasUsableFixedBounds(
			FBox(FVector::ZeroVector, FVector::ZeroVector)));

	// Katalog-Massstab (bounds-Feld): Regen 20 x 20 x 10 km.
	TestTrue(TEXT("Regen-Box 20000^2 x 10000 -> nutzbar"),
		UWiesbadenWeatherFXComponent::HasUsableFixedBounds(
			FBox(FVector::ZeroVector, FVector(20000.0, 20000.0, 10000.0))));

	// Winzige, aber echte Ausdehnung -> nutzbar.
	TestTrue(TEXT("1-cm-Box -> nutzbar"),
		UWiesbadenWeatherFXComponent::HasUsableFixedBounds(
			FBox(FVector::ZeroVector, FVector(1.0, 1.0, 1.0))));

	return true;
}

#if WITH_EDITOR
// Editor-Test: Die in WeatherFXCatalog.json referenzierten Engine-Module muessen
// in UE 5.8 existieren - sonst driftet die Blaupause vom Modul-Bestand und der
// manuelle Asset-Bau im Niagara-Editor findet die Module nicht.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeatherFXCatalogModulesTest,
	"WiesbadenReal.Weather.FXCatalogModules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FWeatherFXCatalogModulesTest::RunTest(const FString& Parameters)
{
	// Alle Modulnamen, die WeatherFXCatalog.json aktuell referenziert (reale
	// UE-5.8-Namen: SpawnBurst_Instantaneous statt SpawnBurst/-
	// SpawnBurstInterval, Color statt SetOpacity, ParticleState statt
	// SetLifeTime).
	const TCHAR* ModulePaths[] = {
		TEXT("/Niagara/Modules/Emitter/SpawnRate.SpawnRate"),
		TEXT("/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous"),
		TEXT("/Niagara/Modules/Spawn/Velocity/AddVelocity.AddVelocity"),
		TEXT("/Niagara/Modules/Update/Forces/GravityForce.GravityForce"),
		TEXT("/Niagara/Modules/Update/Forces/Drag.Drag"),
		TEXT("/Niagara/Modules/Update/Color/Color.Color"),
		TEXT("/Niagara/Modules/Update/Lifetime/ParticleState.ParticleState"),
		TEXT("/Niagara/Modules/Ribbons/RibbonWidth.RibbonWidth") };
	for (const TCHAR* ModulePath : ModulePaths)
	{
		UObject* Module = LoadObject<UObject>(nullptr, ModulePath);
		TestTrue(FString::Printf(TEXT("Katalog-Modul existiert in UE 5.8: %s"), ModulePath),
			Module != nullptr);
	}
	return true;
}
#endif // WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeatherFXValidationTest,
	"WiesbadenReal.Weather.FXValidation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWeatherFXValidationTest::RunTest(const FString& Parameters)
{
	// -- Vollstaendiges System -> nichts fehlt --------------------------------
	{
		const TSet<FString> Complete = {
			TEXT("RainSpawnRate"), TEXT("SnowSpawnRate"), TEXT("FogDensity"),
			TEXT("CloudOpacity"), TEXT("LightningInterval"), TEXT("WindSpeed"),
			TEXT("SunLightColor") };
		TestTrue(TEXT("Vollstaendig: nichts fehlt (Rain)"),
			FWiesbadenWeatherFXParams::FindMissingParameters(
				FWiesbadenWeatherFXParams::GetRequiredUserParameters(EWiesbadenWeatherFXType::Rain),
				Complete).Num() == 0);
		TestTrue(TEXT("Vollstaendig: nichts fehlt (Storm)"),
			FWiesbadenWeatherFXParams::FindMissingParameters(
				FWiesbadenWeatherFXParams::GetRequiredUserParameters(EWiesbadenWeatherFXType::Storm),
				Complete).Num() == 0);
	}

	// -- Fehlende Parameter werden gemeldet -----------------------------------
	{
		const TSet<FString> Partial = { TEXT("RainSpawnRate"), TEXT("WindSpeed") };
		const TArray<FString> Missing = FWiesbadenWeatherFXParams::FindMissingParameters(
			FWiesbadenWeatherFXParams::GetRequiredUserParameters(EWiesbadenWeatherFXType::Rain),
			Partial);
		TestTrue(TEXT("Rain ohne SunLightColor -> genau SunLightColor fehlt"),
			Missing.Num() == 1 && Missing[0] == TEXT("SunLightColor"));

		const TSet<FString> StormPartial = { TEXT("LightningInterval"), TEXT("WindSpeed") };
		const TArray<FString> StormMissing = FWiesbadenWeatherFXParams::FindMissingParameters(
			FWiesbadenWeatherFXParams::GetRequiredUserParameters(EWiesbadenWeatherFXType::Storm),
			StormPartial);
		TestTrue(TEXT("Storm: fehlende sortiert (RainSpawnRate, SunLightColor)"),
			StormMissing.Num() == 2 && StormMissing[0] == TEXT("RainSpawnRate")
			&& StormMissing[1] == TEXT("SunLightColor"));
	}

	// -- Leeres System -> alles fehlt -----------------------------------------
	{
		const TArray<FString> Missing = FWiesbadenWeatherFXParams::FindMissingParameters(
			FWiesbadenWeatherFXParams::GetRequiredUserParameters(EWiesbadenWeatherFXType::Fog),
			TSet<FString>());
		TestTrue(TEXT("Leeres System: alle 3 fehlen (Fog)"), Missing.Num() == 3);
	}

	// -- Determinsmus + sortierte Reihenfolge ---------------------------------
	{
		const TSet<FString> WindOnly = { TEXT("WindSpeed") };
		const TArray<FString> A = FWiesbadenWeatherFXParams::FindMissingParameters(
			FWiesbadenWeatherFXParams::GetRequiredUserParameters(EWiesbadenWeatherFXType::Storm),
			WindOnly);
		const TArray<FString> B = FWiesbadenWeatherFXParams::FindMissingParameters(
			FWiesbadenWeatherFXParams::GetRequiredUserParameters(EWiesbadenWeatherFXType::Storm),
			WindOnly);
		TestTrue(TEXT("Deterministisch"), A == B);
		for (int32 i = 1; i < A.Num(); ++i)
		{
			TestTrue(TEXT("Sortierte Reihenfolge"), A[i] >= A[i - 1]);
		}
	}

	// -- Zusaetzliche Parameter sind ok (Uebererfuellung) ---------------------
	{
		const TSet<FString> Extra = {
			TEXT("RainSpawnRate"), TEXT("SnowSpawnRate"), TEXT("FogDensity"),
			TEXT("CloudOpacity"), TEXT("LightningInterval"), TEXT("WindSpeed"),
			TEXT("SunLightColor"), TEXT("ExtraParam") };
		TestTrue(TEXT("Zusaetzliche Parameter stoeren nicht (Snow)"),
			FWiesbadenWeatherFXParams::FindMissingParameters(
				FWiesbadenWeatherFXParams::GetRequiredUserParameters(EWiesbadenWeatherFXType::Snow),
				Extra).Num() == 0);
	}

	// -- Vertrag pro Typ (Katalog-Konsistenz) ---------------------------------
	{
		const TArray<FString> Rain = FWiesbadenWeatherFXParams::GetRequiredUserParameters(
			EWiesbadenWeatherFXType::Rain);
		TestTrue(TEXT("Rain: RainSpawnRate + Wind + Licht (3)"),
			Rain.Num() == 3 && Rain.Contains(TEXT("RainSpawnRate"))
			&& Rain.Contains(TEXT("WindSpeed")) && Rain.Contains(TEXT("SunLightColor")));

		const TArray<FString> Storm = FWiesbadenWeatherFXParams::GetRequiredUserParameters(
			EWiesbadenWeatherFXType::Storm);
		TestTrue(TEXT("Storm: Blitze + Regen + Wind + Licht (4)"),
			Storm.Num() == 4 && Storm.Contains(TEXT("LightningInterval"))
			&& Storm.Contains(TEXT("RainSpawnRate")));

		const TArray<FString> Snow = FWiesbadenWeatherFXParams::GetRequiredUserParameters(
			EWiesbadenWeatherFXType::Snow);
		TestTrue(TEXT("Snow: kein RainSpawnRate im Vertrag"),
			!Snow.Contains(TEXT("RainSpawnRate")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeatherNightFloorTest,
	"WiesbadenReal.Weather.NightLightFloor",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Nachts muss eine Grundhelligkeit bleiben.
 *
 * Die Sonnenstaerke wurde mit dem Sonnenstand multipliziert, und der ist nachts
 * exakt 0. Damit hatte die Stadt ausser Scheinwerfern und Strassenlaternen
 * KEINE Lichtquelle - kein Mond, keine Himmelsaufhellung. Im Spiel war nichts
 * zu sehen; die Rueckmeldung dazu lautete "ALLES DUNKEL".
 *
 * Geprueft wird beides: dass nachts Licht uebrig bleibt UND dass es deutlich
 * unter dem Tagwert liegt - eine Untergrenze, die den Tag einholt, waere
 * genauso falsch.
 */
bool FWeatherNightFloorTest::RunTest(const FString& Parameters)
{
	const float MaxLux = FWiesbadenWeatherFXParams::GetMaxSunIntensityLux();
	const float Floor = FWiesbadenWeatherFXParams::GetNightSunFloor();

	TestTrue(TEXT("Nacht-Grundhelligkeit ist groesser als null"), Floor > 0.0f);
	TestTrue(TEXT("Nacht bleibt deutlich unter Tag"), Floor < 0.2f);

	// Mitternacht, klarer Himmel: Sonnenstand 0, trotzdem Licht.
	const FWiesbadenWeatherState Night = MakeFXState(
		ECityWeatherPreset::Clear, ECityWeatherPreset::Clear, 1.0f, 0.0f, 0.0f);
	const FWiesbadenWeatherFXParams NightParams =
		FWiesbadenWeatherFXParams::FromWeatherState(Night);

	TestTrue(
		FString::Printf(TEXT("Nachts bleibt Licht uebrig (%.3f)"), NightParams.SunIntensity),
		NightParams.SunIntensity > 0.0f);

	// Mittag: deutlich heller.
	const FWiesbadenWeatherState Noon = MakeFXState(
		ECityWeatherPreset::Clear, ECityWeatherPreset::Clear, 1.0f, 12.0f, 1.0f);
	const FWiesbadenWeatherFXParams NoonParams =
		FWiesbadenWeatherFXParams::FromWeatherState(Noon);

	TestTrue(
		FString::Printf(TEXT("Tag ist deutlich heller als Nacht (%.3f gegen %.3f)"),
			NoonParams.SunIntensity, NightParams.SunIntensity),
		NoonParams.SunIntensity > NightParams.SunIntensity * 5.0f);

	TestTrue(TEXT("Tagwert erreicht die volle Staerke"),
		NoonParams.SunIntensity > MaxLux * 0.5f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeatherFXSkyTest,
	"WiesbadenReal.Weather.FXSky",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Die Kurven des engine-nativen Himmels (Nebel, Wolkenschicht, Himmelslicht).
 *
 * Partikel koennen die Himmelskuppel nicht anfassen - Nebel und Bewoelkung
 * laufen deshalb ueber Hoehennebel und Wolkenschicht der Engine. Hier stehen
 * die Umrechnungen, die UpdateSky auf die Actors schreibt.
 */
bool FWeatherFXSkyTest::RunTest(const FString& Parameters)
{
	using FX = FWiesbadenWeatherFXParams;

	// -- Nebel: klarer Himmel behaelt die Grundtruebung der Karte -------------
	TestTrue(TEXT("klar: Grundtruebung 0,008"),
		FMath::IsNearlyEqual(FX::FogDensityFor(0.0f), 0.008f, 1e-4f));
	TestTrue(TEXT("dichter Nebel ist deutlich dichter als klar"),
		FX::FogDensityFor(1.0f) > FX::FogDensityFor(0.0f) * 10.0f);
	TestTrue(TEXT("Nebel waechst monoton"),
		FX::FogDensityFor(0.8f) > FX::FogDensityFor(0.3f));
	// Ausserhalb 0..1 wird geklemmt - keine negative oder absurde Dichte.
	TestTrue(TEXT("unter 0 geklemmt"),
		FMath::IsNearlyEqual(FX::FogDensityFor(-5.0f), FX::FogDensityFor(0.0f), 1e-6f));
	TestTrue(TEXT("ueber 1 geklemmt"),
		FMath::IsNearlyEqual(FX::FogDensityFor(9.0f), FX::FogDensityFor(1.0f), 1e-6f));

	// -- Wolken: unter 5 % gar nichts zeichnen -------------------------------
	TestTrue(TEXT("wolkenlos: keine Schicht"), FX::CloudLayerHeightKm(0.0f) == 0.0f);
	TestTrue(TEXT("Schleier unter 5 %: keine Schicht"), FX::CloudLayerHeightKm(0.04f) == 0.0f);
	TestTrue(TEXT("bedeckt: Schicht vorhanden"), FX::CloudLayerHeightKm(0.7f) > 0.0f);
	TestTrue(TEXT("mehr Bedeckung = maechtigere Schicht"),
		FX::CloudLayerHeightKm(1.0f) > FX::CloudLayerHeightKm(0.3f));
	// Je bedeckter, desto TIEFER haengt die Decke.
	TestTrue(TEXT("Regendecke haengt tiefer als Schoenwetterwolken"),
		FX::CloudLayerBottomKm(1.0f) < FX::CloudLayerBottomKm(0.2f));

	// -- Himmelslicht: Bedeckung schluckt Umgebungslicht ---------------------
	TestTrue(TEXT("klar: volles Himmelslicht"),
		FMath::IsNearlyEqual(FX::SkyLightFactorFor(0.0f), 1.0f, 1e-4f));
	TestTrue(TEXT("bedeckt: deutlich gedaempft"), FX::SkyLightFactorFor(1.0f) < 0.6f);
	TestTrue(TEXT("Daempfung faellt monoton"),
		FX::SkyLightFactorFor(0.9f) < FX::SkyLightFactorFor(0.2f));

	// -- Gewitter: Blitz setzt hart ein und klingt schnell ab ----------------
	TestTrue(TEXT("kein Gewitter: kein Blitz"), FX::LightningFlashLux(0.0f, 0.18f) == 0.0f);
	TestTrue(TEXT("Blitzbeginn ueberstrahlt die Mittagssonne"),
		FX::LightningFlashLux(0.18f, 0.18f) > FX::GetMaxSunIntensityLux());
	TestTrue(TEXT("Blitz klingt ab"),
		FX::LightningFlashLux(0.05f, 0.18f) < FX::LightningFlashLux(0.15f, 0.18f));
	// Quadratisch, nicht linear: auf halber Restdauer ist es deutlich unter der Haelfte.
	TestTrue(TEXT("Abklingen ist ueberproportional"),
		FX::LightningFlashLux(0.09f, 0.18f) < 0.5f * FX::LightningFlashLux(0.18f, 0.18f));
	TestTrue(TEXT("entartete Dauer -> kein Blitz"), FX::LightningFlashLux(0.1f, 0.0f) == 0.0f);

	// -- Zusammenspiel: Regen ist truebe, klar ist es nicht -------------------
	{
		const FWiesbadenWeatherState Clear = MakeFXState(
			ECityWeatherPreset::Clear, ECityWeatherPreset::Clear, 1.0f, 12.0f, 1.0f);
		const FWiesbadenWeatherState Rain = MakeFXState(
			ECityWeatherPreset::Rain, ECityWeatherPreset::Rain, 1.0f, 12.0f, 1.0f);
		const FX RainParams = FX::FromWeatherState(Rain);
		const FX ClearParams = FX::FromWeatherState(Clear);

		TestTrue(TEXT("Regen: dichterer Nebel als bei klarem Himmel"),
			FX::FogDensityFor(RainParams.FogDensity) > FX::FogDensityFor(ClearParams.FogDensity));
		TestTrue(TEXT("Regen: Wolkendecke vorhanden"),
			FX::CloudLayerHeightKm(RainParams.CloudOpacity) > 0.0f);
		TestTrue(TEXT("klar: keine geschlossene Decke"),
			FX::CloudLayerHeightKm(ClearParams.CloudOpacity) < FX::CloudLayerHeightKm(RainParams.CloudOpacity));
		TestTrue(TEXT("Regen: Himmelslicht gedaempfter"),
			FX::SkyLightFactorFor(RainParams.CloudOpacity) < FX::SkyLightFactorFor(ClearParams.CloudOpacity));
	}

	return true;
}
