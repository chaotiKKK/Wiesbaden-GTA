// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenWeatherFX.h"

#include "WiesbadenReal.h"

#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "World/WiesbadenSolar.h"
#include "EngineUtils.h"
#include "Engine/PostProcessVolume.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "World/WiesbadenCitySubsystem.h"

namespace
{
	// Partikelraten bei voller Niederschlagsintensitaet (Schnee faellt langsamer).
	constexpr float FullRainSpawnRate = 1200.0f;
	constexpr float FullSnowSpawnRate = 600.0f;

	// Maximale Sonnen-Intensitaet in Lux, erreicht bei klarem Zenitstand.
	// Wird von Sonnenstand und Bewoelkung heruntergerechnet.
	//
	// ACHTUNG: Hier stand zuvor 3.14f mit dem Vermerk "UE-Default". Das war
	// der einheitenlose UE4-Altwert (Pi). In UE5 wird die Intensitaet einer
	// DirectionalLight in Lux angegeben; eine neu platzierte Sonne hat dort
	// 10 lx. Mit 3.14 lx und zusaetzlich abgedunkelter Lichtfarbe blieb die
	// Stadt praktisch schwarz.
	constexpr float MaxSunIntensity = 10.0f;

	/**
	 * Grundhelligkeit bei Nacht, als Anteil der Tagesstaerke.
	 *
	 * Hier stand vorher implizit null: die Sonnenstaerke wurde mit dem
	 * Sonnenstand multipliziert, und der ist nachts exakt 0. Die Stadt hatte
	 * damit ausser Scheinwerfern und Strassenlaternen KEINE Lichtquelle - kein
	 * Mond, keine Himmelsaufhellung. Sichtbar war nichts.
	 *
	 * Real ist eine klare Vollmondnacht mit rund 0,3 Lux gegenueber 100.000 Lux
	 * am Tag noch dunkler als diese 5 Prozent. Spiele heben das seit jeher an,
	 * weil ein Bildschirm den Kontrastumfang des Auges nicht abbildet und
	 * Dunkeladaption am Monitor nicht stattfindet. 5 Prozent reichen, um
	 * Fahrbahn und Hauskanten zu erkennen, ohne dass die Nacht wie Daemmerung wirkt (bei 5 Prozent tat sie das noch).
	 */
	constexpr float NightSunFloor = 0.03f;

	// Bewoelkungs-Daempfung: 60 % Lichtverlust bei komplett bedecktem Himmel.
	constexpr float CloudDimFactor = 0.6f;

	// Farbtemperatur des Sonnenlichts (sRGB-Basis, ohne Gamma).
	constexpr float NightBase = 0.30f;
	constexpr float DayRed = 0.55f;
	constexpr float DayGreen = 0.55f;
	constexpr float DayBlue = 0.45f;
	constexpr float WarmRedBoost = 0.12f;
	constexpr float WarmGreenBoost = 0.05f;
	constexpr float NightBlueBoost = 0.15f;
}

TArray<FString> FWiesbadenWeatherFXParams::GetRequiredUserParameters(EWiesbadenWeatherFXType Type)
{
	// Vertrag aus Content/Config/WeatherFXCatalog.json (praefixfrei, exakt die
	// Namen, die ApplyParams setzt). Wind + Lichtfarbe braucht jeder Effekt.
	TArray<FString> Required = { TEXT("WindSpeed"), TEXT("SunLightColor") };
	switch (Type)
	{
	case EWiesbadenWeatherFXType::Rain:
		Required.Add(TEXT("RainSpawnRate"));
		break;
	case EWiesbadenWeatherFXType::Snow:
		Required.Add(TEXT("SnowSpawnRate"));
		break;
	case EWiesbadenWeatherFXType::Fog:
		Required.Add(TEXT("FogDensity"));
		break;
	case EWiesbadenWeatherFXType::Clouds:
		Required.Add(TEXT("CloudOpacity"));
		break;
	case EWiesbadenWeatherFXType::Storm:
		Required.Add(TEXT("LightningInterval"));
		Required.Add(TEXT("RainSpawnRate"));
		break;
	}
	return Required;
}

TArray<FString> FWiesbadenWeatherFXParams::FindMissingParameters(const TArray<FString>& Required,
	const TSet<FString>& Available)
{
	TArray<FString> Missing;
	for (const FString& Name : Required)
	{
		if (!Available.Contains(Name))
		{
			Missing.Add(Name);
		}
	}
	// Sortiert fuer Determinsmus (TMap/TSet-Iterationsreihenfolge ist undefiniert).
	Missing.Sort();
	return Missing;
}

float FWiesbadenWeatherFXParams::GetMaxSunIntensityLux()
{
	return MaxSunIntensity;
}

float FWiesbadenWeatherFXParams::FogDensityFor(float FogIntensity01)
{
	// 0,008 ist die Grundtruebung, die der Bake jeder Karte mitgibt
	// (EnsureLightingActors) - klarer Himmel soll genau so aussehen wie bisher.
	// Bei dichtem Nebel (1,0) sinkt die Sicht auf wenige hundert Meter.
	return 0.008f + 0.19f * FMath::Clamp(FogIntensity01, 0.0f, 1.0f);
}

float FWiesbadenWeatherFXParams::CloudLayerBottomKm(float CloudOpacity01)
{
	// Schoenwetterwolken stehen hoch, eine geschlossene Regendecke drueckt tief.
	return FMath::Lerp(6.0f, 2.5f, FMath::Clamp(CloudOpacity01, 0.0f, 1.0f));
}

float FWiesbadenWeatherFXParams::CloudLayerHeightKm(float CloudOpacity01)
{
	// Unter 5 % Bedeckung gar nichts zeichnen: die Volumen-Abtastung kostet
	// Bildzeit, und ein paar Schleierwolken sieht ohnehin niemand.
	const float Cover = FMath::Clamp(CloudOpacity01, 0.0f, 1.0f);
	if (Cover < 0.05f)
	{
		return 0.0f;
	}
	return FMath::Lerp(1.5f, 10.0f, Cover);
}

float FWiesbadenWeatherFXParams::SkyLightFactorFor(float CloudOpacity01)
{
	// Voll bedeckt schluckt die Decke gut die Haelfte des Umgebungslichts.
	return FMath::Lerp(1.0f, 0.45f, FMath::Clamp(CloudOpacity01, 0.0f, 1.0f));
}

float FWiesbadenWeatherFXParams::LightningFlashLux(float FlashRemainingSeconds, float FlashDurationSeconds)
{
	if (FlashRemainingSeconds <= 0.0f || FlashDurationSeconds <= 0.0f)
	{
		return 0.0f;
	}
	// 30 lx liegen deutlich ueber der vollen Mittagssonne (10 lx) - ein Blitz
	// ueberstrahlt die Szene auch am Tag kurz, nachts umso mehr.
	const float T = FMath::Clamp(FlashRemainingSeconds / FlashDurationSeconds, 0.0f, 1.0f);
	return 30.0f * T * T;
}

float FWiesbadenWeatherFXParams::GetNightSunFloor()
{
	return NightSunFloor;
}

namespace
{
	/** Pfad des per Python gebauten Overlay-Materials. */
	const TCHAR* WeatherOverlayMaterialPath =
		TEXT("/Game/Materials/PostProcess/M_WbWeatherOverlay.M_WbWeatherOverlay");

	/** Maximale Schraeglage im Bildraum bei Sturm (Vollausschlag des Windes). */
	constexpr float MaxOverlaySlant = 0.55f;

	/** Windgeschwindigkeit, bei der die Schraeglage voll ausschlaegt (m/s). */
	constexpr float FullSlantWindMs = 27.0f;

	/** Untergrenze der Overlay-Helligkeit - nachts sichtbar, aber nicht grell. */
	constexpr float MinOverlayBrightness = 0.30f;
}

FWiesbadenWeatherOverlayParams FWiesbadenWeatherOverlayParams::FromFXParams(
	const FWiesbadenWeatherFXParams& FX)
{
	FWiesbadenWeatherOverlayParams Out;

	// Die Partikelraten sind der gemeinsame Massstab fuer beide Wege: das
	// Overlay soll bei derselben Wetterlage genauso dicht fallen wie die
	// Niagara-Systeme, wenn sie eines Tages danebenstehen. Regen und Schnee
	// haben dabei EIGENE Vollraten (1200 gegen 600) - mit einer gemeinsamen
	// Zahl waere Schnee bei gleichem Wetter nur halb so dicht.
	Out.RainStrength = FMath::Clamp(FX.RainSpawnRate / FullRainSpawnRate, 0.0f, 1.0f);
	Out.SnowStrength = FMath::Clamp(FX.SnowSpawnRate / FullSnowSpawnRate, 0.0f, 1.0f);

	// Wind schraeg stellen. Im Bildraum, also unabhaengig von der Blickrichtung
	// - eine echte Windrichtung braeuchte die Kamera-Basis und waere bei einer
	// Drehung um 180 Grad trotzdem falsch herum.
	Out.Slant = MaxOverlaySlant
		* FMath::Clamp(FX.WindSpeed / FullSlantWindMs, 0.0f, 1.0f);

	// Farbe vom Licht: bei Abendsonne warme Tropfen, nachts kuehle.
	Out.Tint = FX.SunLightColor;

	// Helligkeit folgt dem Umgebungslicht. Ohne Untergrenze verschwaende der
	// Regen nachts voellig, obwohl gerade dann Strassenlaternen ihn zeigen.
	Out.Brightness = FMath::Max(
		FMath::Clamp(FX.AmbientLightMultiplier, 0.0f, 1.0f), MinOverlayBrightness);

	return Out;
}

FWiesbadenWeatherFXParams FWiesbadenWeatherFXParams::FromWeatherState(const FWiesbadenWeatherState& State)
{
	FWiesbadenWeatherFXParams Out;
	Out.AmbientLightMultiplier = State.AmbientLightMultiplier;
	Out.FogDensity = State.Intensity.Fog;
	Out.CloudOpacity = State.Intensity.CloudCover;

	// Niederschlag: Schnee nutzt dieselbe Intensitaet (Rain als Dichte-
	// Stellvertreter), aber ein eigenes Effekt-System mit langsamerer Rate.
	const bool bIsSnow = State.CurrentWeather == ECityWeatherPreset::Snow;
	const float ClampedRain = FMath::Clamp(State.Intensity.Rain, 0.0f, 1.0f);
	if (bIsSnow)
	{
		Out.SnowSpawnRate = FullSnowSpawnRate * ClampedRain;
	}
	else
	{
		Out.RainSpawnRate = FullRainSpawnRate * ClampedRain;
	}

	// Blitze nur im Gewitter; nachts haeufiger (deterministisch aus Tageszeit).
	if (State.CurrentWeather == ECityWeatherPreset::Thunderstorm)
	{
		const float T = FMath::Clamp(State.TimeOfDayHours / 24.0f, 0.0f, 1.0f);
		Out.LightningInterval = 2.0f + 7.0f * (1.0f - T);
	}

	// Wind aus Bewoelkung; Gewitter stuermisch (3..27 m/s).
	const float StormBoost = State.CurrentWeather == ECityWeatherPreset::Thunderstorm ? 2.0f : 1.0f;
	Out.WindSpeed = 3.0f + 12.0f * FMath::Clamp(State.Intensity.CloudCover, 0.0f, 1.0f) * StormBoost;

	// Lichtfarbe: Tag warm (R > B), Nacht kuehl-blaulich (B >= R). Die Warm-
	// Skala ist nur bei tief stehender Sonne (Morgen/Abend) aktiv und nachts 0.
	const float Day = FMath::Clamp(State.SunElevationFactor(), 0.0f, 1.0f);
	const float Night = 1.0f - Day;
	const float Warm = Day * (1.0f - FMath::Abs(State.TimeOfDayHours - 12.0f) / 12.0f);
	Out.SunLightColor = FLinearColor(
		NightBase + DayRed * Day + WarmRedBoost * Warm,
		NightBase + DayGreen * Day + WarmGreenBoost * Warm,
		NightBase + DayBlue * Day + NightBlueBoost * Night);

	// Sonnen-Intensitaet: 0 nachts, voll am klaren Zenit, von Bewoelkung
	// gedaempft. Treibt die DirectionalLight im Level (SetIntensity).
	//
	// Die Untergrenze wirkt als Mondlicht. Ohne sie ist die Stadt nachts
	// vollstaendig schwarz (siehe NightSunFloor). Die Lichtfarbe ist zu diesem
	// Zeitpunkt bereits kuehl-blaeulich, das Ergebnis liest sich also als
	// Nacht und nicht als gedimmter Tag.
	const float Elevation = FMath::Max(Day, NightSunFloor);
	Out.SunIntensity = MaxSunIntensity * Elevation
		* (1.0f - CloudDimFactor * FMath::Clamp(Out.CloudOpacity, 0.0f, 1.0f));
	return Out;
}

FString UWiesbadenWeatherFXComponent::GetDefaultAssetPath(EWiesbadenWeatherFXType Type)
{
	// Kanonische Pfade (siehe WeatherFXCatalog.json, Feld "path"). Sobald die
	// NS_-Assets nach Content/Niagara gebaut sind, werden sie automatisch
	// zugewiesen - keine Details-Panel-Konfiguration noetig.
	switch (Type)
	{
	case EWiesbadenWeatherFXType::Rain:
		return TEXT("/Game/Niagara/NS_WeatherRain.NS_WeatherRain");
	case EWiesbadenWeatherFXType::Snow:
		return TEXT("/Game/Niagara/NS_WeatherSnow.NS_WeatherSnow");
	case EWiesbadenWeatherFXType::Fog:
	case EWiesbadenWeatherFXType::Clouds:
		// Engine-nativ statt Niagara: Hoehennebel und Wolkenschicht sind
		// Engine-Actors (siehe UpdateSky). Ein leerer Pfad heisst "kein
		// Niagara-Asset erwartet" - vorher suchte die Komponente hier zwei
		// Systeme, die es nie gab, und warnte bei jedem Start.
		return FString();
	case EWiesbadenWeatherFXType::Storm:
		return TEXT("/Game/Niagara/NS_WeatherStorm.NS_WeatherStorm");
	}
	return FString();
}

bool UWiesbadenWeatherFXComponent::HasUsableFixedBounds(const FBox& Bounds)
{
	// Der Katalog (WeatherFXCatalog.json, bounds-Feld) verlangt fuer alle
	// Effekte explizite Fixed Bounds. Eine nie gesetzte UPROPERTY-FBox ist
	// (0,0,0)-(0,0,0): IsValid() ist zwar wahr, aber ohne Ausdehnung culled
	// die Engine den Effekt bei Distanz trotzdem weg.
	if (!Bounds.IsValid)
	{
		return false;
	}
	return Bounds.GetExtent().SizeSquared() > KINDA_SMALL_NUMBER;
}

void UWiesbadenWeatherFXComponent::WarnMissingFixedBounds(const UNiagaraSystem* System,
	const FString& EffectName)
{
	if (!System || HasUsableFixedBounds(System->GetFixedBounds()))
	{
		return;
	}
	const FString Key = System->GetPathName();
	if (FixedBoundsWarningsLogged.Contains(Key))
	{
		return; // einmalig je System warnen - nicht pro Tick/Prozess
	}
	FixedBoundsWarningsLogged.Add(Key);
	UE_LOG(LogWbCore, Warning,
		TEXT("WeatherFX: System '%s' (%s) hat keine nutzbaren Fixed Bounds gesetzt - ")
		TEXT("der Effekt kann bei Distanz-Culling verschwinden. ")
		TEXT("Siehe Content/Config/WeatherFXCatalog.json (bounds-Feld)."),
		*System->GetName(), *EffectName);
}

void UWiesbadenWeatherFXComponent::LoadDefaultSystems()
{
	const auto LoadIfMissing = [this](TObjectPtr<UNiagaraSystem>& System, EWiesbadenWeatherFXType Type)
	{
		if (System)
		{
			return; // manuell zugewiesen - nicht anfassen
		}
		const FString Path = GetDefaultAssetPath(Type);
		if (Path.IsEmpty())
		{
			return;
		}
		System = LoadObject<UNiagaraSystem>(nullptr, *Path);
		if (System)
		{
			UE_LOG(LogWbCore, Log, TEXT("WeatherFX: Effekt '%s' automatisch aus %s zugewiesen."),
				*Path, *Path);
		}
		else
		{
			// Laut werden. Ein fehlendes NS_-Asset war bisher voellig stumm:
			// man stellt -WbWeather=Rain ein, es passiert nichts, und nichts im
			// Protokoll sagt warum. Die Systeme muessen von Hand im
			// Niagara-Editor gebaut werden (Python exportiert die Niagara-
			// Editor-API nicht) - siehe docs/Wetter_Niagara_Anleitung.md.
			UE_LOG(LogWbCore, Warning,
				TEXT("WeatherFX: %s FEHLT - dieser Effekt bleibt unsichtbar. ")
				TEXT("Asset im Niagara-Editor anlegen (docs/Wetter_Niagara_Anleitung.md)."),
				*Path);
		}
	};

	LoadIfMissing(RainSystem, EWiesbadenWeatherFXType::Rain);
	LoadIfMissing(SnowSystem, EWiesbadenWeatherFXType::Snow);
	LoadIfMissing(FogSystem, EWiesbadenWeatherFXType::Fog);
	LoadIfMissing(CloudSystem, EWiesbadenWeatherFXType::Clouds);
	LoadIfMissing(StormSystem, EWiesbadenWeatherFXType::Storm);
}

UWiesbadenWeatherFXComponent::UWiesbadenWeatherFXComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UWiesbadenWeatherFXComponent::BeginPlay()
{
	Super::BeginPlay();

	// Nicht manuell zugewiesene Effekte aus den kanonischen Content-Pfaden
	// laden (Zuweisung automatisch, sobald die NS_-Assets gebaut sind).
	LoadDefaultSystems();

	// Keine explizite DirectionalLight gesetzt? Dann die erste des Levels
	// finden (typisch die Sonne) - keine Konfiguration noetig.
	if (!SunLight)
	{
		SunLight = FindSunLight();
		if (!SunLight)
		{
			UE_LOG(LogWbCore, Warning,
				TEXT("WeatherFX: Keine DirectionalLight im Level gefunden - Sonnenlicht bleibt ungesteuert."));
		}
	}

	// Himmel engine-nativ: Nebel und Himmelslicht liegen dank EnsureLightingActors
	// in jeder Karte, die Wolkenschicht wird bei Bedarf nachgeruestet.
	WeatherFog = FindFog();
	WeatherSkyLight = FindSkyLight();
	if (WeatherSkyLight)
	{
		BaseSkyLightIntensity = WeatherSkyLight->Intensity;
	}
	WeatherClouds = FindOrSpawnClouds();
	UE_LOG(LogWbCore, Log,
		TEXT("WeatherFX: Himmel engine-nativ - Nebel=%d Wolken=%d Himmelslicht=%d (Basis %.2f)."),
		WeatherFog ? 1 : 0, WeatherClouds ? 1 : 0, WeatherSkyLight ? 1 : 0, BaseSkyLightIntensity);
}

void UWiesbadenWeatherFXComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Effekte aufraeumen (bAutoDestroy der Spawns deckt One-Shots ab; hier
	// explizit deaktivieren, damit Partikel nicht weiterlaufen).
	for (TObjectPtr<UNiagaraComponent>* FX : { &RainFX, &SnowFX, &FogFX, &CloudFX, &StormFX })
	{
		if (*FX)
		{
			(*FX)->Deactivate();
			(*FX) = nullptr;
		}
	}
	Super::EndPlay(EndPlayReason);
}

const FWiesbadenWeatherState* UWiesbadenWeatherFXComponent::GetWeatherState() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	const UWiesbadenCitySubsystem* Subsystem = World->GetSubsystem<UWiesbadenCitySubsystem>();
	return Subsystem ? &Subsystem->Weather.GetState() : nullptr;
}

void UWiesbadenWeatherFXComponent::ValidateSystem(UNiagaraSystem* System,
	EWiesbadenWeatherFXType Type, const FString& EffectName) const
{
	if (!System)
	{
		return;
	}

	// Exponierte User-Parameter des Systems sammeln (praefixfrei vergleichen:
	// die ExposedParameters tragen den "User."-Namespace, der Vertrag nicht).
	TArray<FNiagaraVariable> Exposed;
	System->GetExposedParameters().GetParameters(Exposed);
	TSet<FString> Available;
	for (const FNiagaraVariable& Var : Exposed)
	{
		FString Name = Var.GetName().ToString();
		Name.RemoveFromStart(TEXT("User."));
		Available.Add(MoveTemp(Name));
	}

	const TArray<FString> Missing = FWiesbadenWeatherFXParams::FindMissingParameters(
		FWiesbadenWeatherFXParams::GetRequiredUserParameters(Type), Available);
	if (Missing.Num() > 0)
	{
		UE_LOG(LogWbCore, Warning,
			TEXT("WeatherFX: System '%s' (%s) erfuellt den Parameter-Vertrag nicht - ")
			TEXT("fehlende User-Parameter: %s. Siehe Content/Config/WeatherFXCatalog.json."),
			*System->GetName(), *EffectName, *FString::Join(Missing, TEXT(", ")));
	}
}

UDirectionalLightComponent* UWiesbadenWeatherFXComponent::FindSunLight() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	// Erste DirectionalLight des Levels (typisch die Sonne der Szene).
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		// GetLightComponent statt GetComponent.
		//
		// ADirectionalLight::GetComponent gibt es nur im Editor-Build. Diese
		// Stelle laeuft im Spiel - der Wetterwechsel regelt die Sonne zur
		// Laufzeit -, und im paketierten Spiel gaebe es sie schlicht nicht.
		// Aufgefallen ist es erst beim ersten Paketieren:
		//   error C2039: "GetComponent" ist kein Member von "ADirectionalLight"
		if (UDirectionalLightComponent* Comp =
			Cast<UDirectionalLightComponent>(It->GetLightComponent()))
		{
			return Comp;
		}
	}
	return nullptr;
}

UExponentialHeightFogComponent* UWiesbadenWeatherFXComponent::FindFog() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
	{
		if (UExponentialHeightFogComponent* Comp = It->GetComponent())
		{
			return Comp;
		}
	}
	return nullptr;
}

USkyLightComponent* UWiesbadenWeatherFXComponent::FindSkyLight() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ASkyLight> It(World); It; ++It)
	{
		if (USkyLightComponent* Comp = It->GetLightComponent())
		{
			return Comp;
		}
	}
	return nullptr;
}

UVolumetricCloudComponent* UWiesbadenWeatherFXComponent::FindOrSpawnClouds()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AVolumetricCloud> It(World); It; ++It)
	{
		// FindComponentByClass statt eines Actor-Getters: AVolumetricCloud haelt
		// die Komponente als Member ohne oeffentlichen Zugriff.
		if (UVolumetricCloudComponent* Comp = It->FindComponentByClass<UVolumetricCloudComponent>())
		{
			return Comp;
		}
	}

	// Keine Wolkenschicht in der Karte: nachruesten. Transient, damit der Actor
	// nicht in eine gebackene Karte zurueckgeschrieben wird.
	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	AVolumetricCloud* Cloud = World->SpawnActor<AVolumetricCloud>(
		FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	return Cloud ? Cloud->FindComponentByClass<UVolumetricCloudComponent>() : nullptr;
}

float UWiesbadenWeatherFXComponent::UpdateLightning(const FWiesbadenWeatherFXParams& Params, float DeltaTime)
{
	// Dauer eines Blitzes. Kurz genug, dass er zuckt statt zu blenden.
	constexpr float FlashSeconds = 0.18f;

	if (Params.LightningInterval <= 0.0f)
	{
		LightningTimer = 0.0f;
		LightningFlashRemaining = 0.0f;
		return 0.0f;
	}

	LightningFlashRemaining = FMath::Max(0.0f, LightningFlashRemaining - DeltaTime);
	LightningTimer += DeltaTime;
	if (LightningTimer >= Params.LightningInterval)
	{
		LightningTimer = 0.0f;
		LightningFlashRemaining = FlashSeconds;
		if (!bLightningLogged)
		{
			bLightningLogged = true;
			UE_LOG(LogWbCore, Log, TEXT("WeatherFX: Gewitter - erster Blitz (Takt %.1f s)."),
				Params.LightningInterval);
		}
	}
	return FWiesbadenWeatherFXParams::LightningFlashLux(LightningFlashRemaining, FlashSeconds);
}

void UWiesbadenWeatherFXComponent::UpdateSky(const FWiesbadenWeatherFXParams& Params)
{
	// Nebel: Dichte aus der Wetterlage, Farbe aus dem Sonnenlicht - so faerbt
	// sich der Dunst abends warm und nachts kuehl mit.
	if (WeatherFog)
	{
		WeatherFog->SetFogDensity(FWiesbadenWeatherFXParams::FogDensityFor(Params.FogDensity));
		WeatherFog->SetFogInscatteringColor(Params.SunLightColor);
	}

	// Wolken: Hoehe und Dicke aus der Bewoelkung. Die Deckung selbst steckt im
	// Wolken-Material der Engine und ist von hier nicht stellbar - eine tiefe,
	// maechtige Schicht liest sich aber als geschlossene Decke.
	if (WeatherClouds)
	{
		const float HeightKm = FWiesbadenWeatherFXParams::CloudLayerHeightKm(Params.CloudOpacity);
		const bool bWantVisible = HeightKm > 0.0f;
		if (bWantVisible)
		{
			WeatherClouds->SetLayerBottomAltitude(
				FWiesbadenWeatherFXParams::CloudLayerBottomKm(Params.CloudOpacity));
			WeatherClouds->SetLayerHeight(HeightKm);
		}
		if (bWantVisible != bCloudsVisible)
		{
			WeatherClouds->SetVisibility(bWantVisible, /*bPropagateToChildren=*/true);
			bCloudsVisible = bWantVisible;
		}
	}

	// Himmelslicht: unter geschlossener Decke wird die Szene sichtbar flauer.
	if (WeatherSkyLight && BaseSkyLightIntensity >= 0.0f)
	{
		WeatherSkyLight->SetIntensity(BaseSkyLightIntensity
			* FWiesbadenWeatherFXParams::SkyLightFactorFor(Params.CloudOpacity));
	}
}

void UWiesbadenWeatherFXComponent::ApplyParams(UNiagaraComponent* FX,
	const FWiesbadenWeatherFXParams& Params)
{
	// Nur User-Namespace-Parameter sind setzbar (siehe Skill unreal-niagara);
	// die Namen muessen exakt den exponierten User-Parametern der Assets
	// entsprechen.
	FX->SetVariableFloat(TEXT("RainSpawnRate"), Params.RainSpawnRate);
	FX->SetVariableFloat(TEXT("SnowSpawnRate"), Params.SnowSpawnRate);
	FX->SetVariableFloat(TEXT("FogDensity"), Params.FogDensity);
	FX->SetVariableFloat(TEXT("CloudOpacity"), Params.CloudOpacity);
	FX->SetVariableFloat(TEXT("LightningInterval"), Params.LightningInterval);
	FX->SetVariableFloat(TEXT("WindSpeed"), Params.WindSpeed);
	FX->SetVariableLinearColor(TEXT("SunLightColor"), Params.SunLightColor);
}

bool UWiesbadenWeatherFXComponent::EnsureOverlay()
{
	if (OverlayMID && OverlayVolume)
	{
		return true;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	UMaterialInterface* Base = LoadObject<UMaterialInterface>(
		nullptr, WeatherOverlayMaterialPath);
	if (!Base)
	{
		if (!bOverlayWarned)
		{
			bOverlayWarned = true;
			UE_LOG(LogWbCore, Warning,
				TEXT("WeatherFX: Overlay-Material %s fehlt - Regen/Schnee ohne Niagara ")
				TEXT("bleiben aus. Mit Tools/add_weather_postprocess.py erzeugen."),
				WeatherOverlayMaterialPath);
		}
		return false;
	}

	OverlayMID = UMaterialInstanceDynamic::Create(Base, this);
	if (!OverlayMID)
	{
		return false;
	}

	// Unbegrenztes Volume: der Niederschlag soll ueberall fallen, nicht nur in
	// einem Kasten. Hohe Prioritaet, damit die Post-Process-Volumes der
	// gebackenen Karte das Overlay nicht ueberschreiben.
	FActorSpawnParameters Params;
	Params.Name = TEXT("WbWeatherOverlayVolume");
	Params.ObjectFlags |= RF_Transient;
	OverlayVolume = World->SpawnActor<APostProcessVolume>(Params);
	if (!OverlayVolume)
	{
		return false;
	}
	OverlayVolume->bUnbound = true;
	OverlayVolume->BlendWeight = 1.0f;
	OverlayVolume->Priority = 1000.0f;
	OverlayVolume->Settings.WeightedBlendables.Array.Add(
		FWeightedBlendable(1.0f, OverlayMID));

	UE_LOG(LogWbCore, Log,
		TEXT("WeatherFX: Bildschirm-Niederschlag bereit (%s, unbegrenztes Volume)."),
		WeatherOverlayMaterialPath);
	return true;
}

void UWiesbadenWeatherFXComponent::UpdateOverlay(const FWiesbadenWeatherFXParams& Params)
{
	const FWiesbadenWeatherOverlayParams Overlay =
		FWiesbadenWeatherOverlayParams::FromFXParams(Params);

	if (!EnsureOverlay())
	{
		return;
	}

	// Ganz abschalten, wenn nichts faellt: ein Vollbild-Durchgang, der nur
	// Nullen addiert, kostet trotzdem jedes Bild Fuellrate.
	const bool bVisible = Overlay.IsVisible();
	OverlayVolume->bEnabled = bVisible;
	if (!bVisible)
	{
		return;
	}

	OverlayMID->SetScalarParameterValue(TEXT("RegenStaerke"), Overlay.RainStrength);
	OverlayMID->SetScalarParameterValue(TEXT("SchneeStaerke"), Overlay.SnowStrength);
	OverlayMID->SetScalarParameterValue(TEXT("Schraeglage"), Overlay.Slant);
	OverlayMID->SetScalarParameterValue(TEXT("Helligkeit"), Overlay.Brightness);
	OverlayMID->SetVectorParameterValue(TEXT("Farbe"), Overlay.Tint);

	// Beleg erst, wenn der Wert steht. Ein Wetterwechsel blendet ueber 8 s ein;
	// wer im zweiten Bild protokolliert, schreibt "Regen 0.01" ins Log und
	// sucht danach einen Fehler, den es nicht gibt.
	const bool bSettled =
		FMath::IsNearlyEqual(Overlay.RainStrength, LastOverlayRain, 0.0005f) &&
		FMath::IsNearlyEqual(Overlay.SnowStrength, LastOverlaySnow, 0.0005f);
	LastOverlayRain = Overlay.RainStrength;
	LastOverlaySnow = Overlay.SnowStrength;

	// Ein einzelnes ruhiges Bild reicht nicht: waehrend der Ueberblendung gibt
	// es kurze Bilder, in denen sich der Wert um weniger als die Schwelle
	// bewegt. Der Beleg meldete so "Schnee 0.04" statt 0,70.
	OverlaySettledFrames = bSettled ? OverlaySettledFrames + 1 : 0;
	if (!bOverlayReported && OverlaySettledFrames > 60)
	{
		bOverlayReported = true;
		UE_LOG(LogWbCore, Log,
			TEXT("WeatherFX: Overlay faellt - Regen %.2f, Schnee %.2f, Schraeglage %.2f, ")
			TEXT("Helligkeit %.2f."),
			Overlay.RainStrength, Overlay.SnowStrength, Overlay.Slant, Overlay.Brightness);
	}
}

void UWiesbadenWeatherFXComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const FWiesbadenWeatherState* State = GetWeatherState();
	if (!State)
	{
		return;
	}

	const FWiesbadenWeatherFXParams Params = FWiesbadenWeatherFXParams::FromWeatherState(*State);

	// Regen/Schnee im Bildraum - haengt NICHT an den Niagara-Assets.
	UpdateOverlay(Params);

	// Effekt aktualisieren: System-Asset vorhanden -> spawnen (einmalig) und
	// Parameter setzen; sonst existierenden Effekt deaktivieren.
	//
	// DER EFFEKT FOLGT DER KAMERA, er haengt nicht am Besitzer.
	//
	// Zwei Gruende, beide hart: Erstens ist der Besitzer dieser Komponente der
	// GameMode, und der hat GAR KEINE Wurzelkomponente -
	// GetOwner()->GetRootComponent() ist nullptr, SpawnSystemAttached wurde nie
	// gerufen. Ein fertiges NS_WeatherRain waere nie erschienen. Zweitens
	// verlangt der Katalog feste Bounds von +-200 m; an den Weltursprung
	// gebunden regnete es nur dort, waehrend der Spieler Kilometer entfernt
	// steht. An den Pawn haengen geht auch nicht: der wechselt beim Ein- und
	// Aussteigen (Auto, zu Fuss, Helikopter) und wird dabei zerstoert.
	UWorld* FXWorld = GetWorld();
	FVector ViewLocation = FVector::ZeroVector;
	bool bHasView = false;
	if (FXWorld)
	{
		if (const APlayerController* PC = FXWorld->GetFirstPlayerController())
		{
			FRotator ViewRotation = FRotator::ZeroRotator;
			PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
			bHasView = true;
		}
	}

	if (!bReportedFXSetup && bHasView)
	{
		bReportedFXSetup = true;
		const USceneComponent* OwnerRoot = GetOwner() ? GetOwner()->GetRootComponent() : nullptr;
		UE_LOG(LogWbCore, Log,
			TEXT("WeatherFX: Partikel-Systeme Regen=%d Schnee=%d Gewitter=%d; ")
			TEXT("Besitzer-Wurzel=%s, Effekte folgen der Kamera bei (%.0f, %.0f, %.0f)."),
			RainSystem ? 1 : 0, SnowSystem ? 1 : 0, StormSystem ? 1 : 0,
			OwnerRoot ? TEXT("vorhanden") : TEXT("KEINE (darum Kamera-Nachfuehrung)"),
			ViewLocation.X, ViewLocation.Y, ViewLocation.Z);
	}

	const auto UpdateFX = [this, &Params, FXWorld, ViewLocation, bHasView](
		TObjectPtr<UNiagaraComponent>& FX, UNiagaraSystem* System,
		EWiesbadenWeatherFXType Type, const FString& EffectName, bool bWanted)
	{
		if (!System)
		{
			return;
		}
		if (!FX)
		{
			if (!bWanted || !FXWorld || !bHasView)
			{
				return; // nichts zu spawnen, nichts aktiv (oder noch keine Kamera)
			}
			FX = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				FXWorld, System, ViewLocation, FRotator::ZeroRotator,
				FVector::OneVector, /*bAutoDestroy=*/false);
			if (!FX)
			{
				return;
			}
			// Nach jedem Spawn: Vertrag gegen WeatherFXCatalog.json pruefen
			// (fehlende User-Parameter -> Warn-Log, kein stummer Vertragsbruch).
			ValidateSystem(System, Type, EffectName);
			// Culling-Hinweis: Fixed Bounds laut Katalog noetig, aber nicht
			// gesetzt -> einmalig warnen (sonst verschwindet der Effekt).
			WarnMissingFixedBounds(System, EffectName);
		}
		// Der Kamera nachfuehren - sonst bleibt die Regenbox dort stehen, wo
		// sie einmal entstanden ist, und man faehrt aus dem Wetter heraus.
		if (bHasView)
		{
			FX->SetWorldLocation(ViewLocation);
		}

		// Nur beim Uebergang inaktiv -> aktiv aktivieren. Activate(true) auf einem
		// bereits laufenden System setzt es zurueck (EResetMode::ResetSystem,
		// siehe UNiagaraComponent::ActivateInternal) - ein Pro-Tick-Aufruf wuerde
		// Regen-/Schnee-Stroeme staendig neu starten. Das Spawn-System ist nach
		// SpawnSystemAttached bereits aktiv -> Guard ueberspringt den Reset.
		if (bWanted)
		{
			if (!FX->IsActive())
			{
				FX->Activate(true);
			}
		}
		else
		{
			FX->Deactivate();
		}
		ApplyParams(FX, Params);
	};

	UpdateFX(RainFX, RainSystem, EWiesbadenWeatherFXType::Rain, TEXT("Regen"),
		Params.RainSpawnRate > 0.01f);
	UpdateFX(SnowFX, SnowSystem, EWiesbadenWeatherFXType::Snow, TEXT("Schnee"),
		Params.SnowSpawnRate > 0.01f);
	UpdateFX(FogFX, FogSystem, EWiesbadenWeatherFXType::Fog, TEXT("Nebel"),
		Params.FogDensity > 0.01f);
	UpdateFX(CloudFX, CloudSystem, EWiesbadenWeatherFXType::Clouds, TEXT("Wolken"),
		Params.CloudOpacity > 0.01f);
	UpdateFX(StormFX, StormSystem, EWiesbadenWeatherFXType::Storm, TEXT("Gewitter"),
		Params.LightningInterval > 0.0f);

	// Sonnenlicht: Farbe (Farbtemperatur) + Intensitaet auf die DirectionalLight
	// der Szene anwenden. Nachts ist die Intensitaet 0 -> Sonne aus.
	if (UDirectionalLightComponent* Sun = SunLight)
	{
		// Drehung aus dem Sonnenstand: die Schatten wandern ueber den Tag, die
		// Atmosphaere faerbt sich am Horizont von selbst mit (AtmosphereSunLight).
		Sun->SetWorldRotation(WiesbadenSolar::SunLightRotation(State->SunElevationDeg, State->SunAzimuthDeg));
		Sun->SetLightColor(Params.SunLightColor);
		// Blitz kurz auf die Sonnenstaerke addieren (0 ausserhalb eines Gewitters).
		Sun->SetIntensity(Params.SunIntensity + UpdateLightning(Params, DeltaTime));
	}

	// Himmel (Nebel, Wolken, Himmelslicht) - der sichtbare Teil des Wetters.
	UpdateSky(Params);

	LastAppliedParams = Params;
}
