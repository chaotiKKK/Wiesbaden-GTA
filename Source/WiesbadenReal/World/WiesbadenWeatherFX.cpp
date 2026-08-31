// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenWeatherFX.h"

#include "WiesbadenReal.h"

#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "EngineUtils.h"
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

float FWiesbadenWeatherFXParams::GetNightSunFloor()
{
	return NightSunFloor;
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
	const float Day = FMath::Clamp(State.SunElevationFactor, 0.0f, 1.0f);
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
		return TEXT("/Game/Niagara/NS_WeatherFog.NS_WeatherFog");
	case EWiesbadenWeatherFXType::Clouds:
		return TEXT("/Game/Niagara/NS_WeatherClouds.NS_WeatherClouds");
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

	// Effekt aktualisieren: System-Asset vorhanden -> spawnen (einmalig) und
	// Parameter setzen; sonst existierenden Effekt deaktivieren.
	USceneComponent* AttachRoot = GetOwner() ? GetOwner()->GetRootComponent() : nullptr;
	const auto UpdateFX = [this, &Params, AttachRoot](
		TObjectPtr<UNiagaraComponent>& FX, UNiagaraSystem* System,
		EWiesbadenWeatherFXType Type, const FString& EffectName, bool bWanted)
	{
		if (!System)
		{
			return;
		}
		if (!FX)
		{
			if (!bWanted || !AttachRoot)
			{
				return; // nichts zu spawnen, nichts aktiv (oder kein Owner-Root)
			}
			FX = UNiagaraFunctionLibrary::SpawnSystemAttached(
				System, AttachRoot, NAME_None,
				FVector::ZeroVector, FRotator::ZeroRotator,
				EAttachLocation::KeepRelativeOffset, /*bAutoDestroy=*/false);
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
		Sun->SetLightColor(Params.SunLightColor);
		Sun->SetIntensity(Params.SunIntensity);
	}

	LastAppliedParams = Params;
}
