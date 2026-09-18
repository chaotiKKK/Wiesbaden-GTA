// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "World/WiesbadenWeatherSystem.h"

#include "WiesbadenWeatherFX.generated.h"

class AWiesbadenCityActor;
class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UVolumetricCloudComponent;
class USkyLightComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class APostProcessVolume;

/**
 * Effekt-Typen der Wetter-FX (Reihenfolge = WeatherFXCatalog.json).
 * Jeder Typ hat einen festen Vertrag an User-Parametern (siehe
 * GetRequiredUserParameters) - die Assets muessen genau diese exponieren.
 */
UENUM(BlueprintType)
enum class EWiesbadenWeatherFXType : uint8
{
	/** Regen (NS_WeatherRain). */
	Rain,
	/** Schnee (NS_WeatherSnow). */
	Snow,
	/** Nebel (NS_WeatherFog). */
	Fog,
	/** Wolken (NS_WeatherClouds). */
	Clouds,
	/** Gewitter (NS_WeatherStorm). */
	Storm
};

/**
 * Abgeleitete FX-Parameter aus einem FWiesbadenWeatherState (datenrein,
 * deterministisch, testbar). Die Niagara-Effekte werden ausschliesslich ueber
 * diese Werte getrieben - die Ableitung ist Logik, das Spawnen/Setzen duenne
 * Verdrahtung.
 *
 * NIAGARA-VERTRAG (siehe Skill unreal-niagara): Nur User-Namespace-Parameter
 * sind von C++/Blueprint setzbar. Die Effekt-Assets muessen genau diese
 * User-Parameter exponiert haben:
 *   User.RainSpawnRate   (Float, Partikel/s)      - Regen-Effekt
 *   User.SnowSpawnRate   (Float, Partikel/s)      - Schnee-Effekt
 *   User.FogDensity      (Float, 0..1)            - Nebel-Effekt (Opacity)
 *   User.CloudOpacity    (Float, 0..1)            - Wolken-Effekt
 *   User.LightningInterval (Float, Sekunden; 0 = aus) - Gewitter-Effekt
 *   User.WindSpeed       (Float, m/s)             - alle Partikel
 *   User.SunLightColor   (LinearColor)            - Lichtfarbe der Sonne
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenWeatherFXParams
{
	GENERATED_BODY()

	/** Regen-Partikel pro Sekunde (0 = Effekt aus). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather|FX")
	float RainSpawnRate = 0.0f;

	/** Schnee-Partikel pro Sekunde (0 = Effekt aus). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather|FX")
	float SnowSpawnRate = 0.0f;

	/** Nebeldichte 0..1 (Steuert Fog-Effekt-Intensitaet). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather|FX")
	float FogDensity = 0.0f;

	/** Bewoelkung 0..1 (Steuert Wolken-Effekt-Intensitaet). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather|FX")
	float CloudOpacity = 0.0f;

	/** Blitz-Intervall in Sekunden; 0 = keine Blitze (nur Gewitter). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather|FX")
	float LightningInterval = 0.0f;

	/** Windgeschwindigkeit in m/s (3..27, aus Bewoelkung/Gewitter). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather|FX")
	float WindSpeed = 3.0f;

	/** Sonnenlicht-Farbe (warm am Morgen/Abend, kuehl-blaulich nachts). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather|FX")
	FLinearColor SunLightColor = FLinearColor::White;

	/**
	 * Sonnen-Intensitaet (0 nachts .. ~3.14 am klaren Mittag, Lux-Skala):
	 * folgt dem Sonnenstand und wird von Bewoelkung gedaempft. Treibt die
	 * DirectionalLight im Level (SetIntensity).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Weather|FX")
	float SunIntensity = 0.0f;

	/** Umgebungslicht 0.35 (Nacht) .. 1.0 (Mittag) - direkte Weitergabe. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather|FX")
	float AmbientLightMultiplier = 1.0f;

	/** Leitet die FX-Parameter aus einem Wetter-Zustand ab (deterministisch). */
	static FWiesbadenWeatherFXParams FromWeatherState(const FWiesbadenWeatherState& State);

	/**
	 * Maximale Sonnen-Intensitaet in LUX, erreicht bei klarem Zenitstand.
	 *
	 * Bewusst als Accessor: der Wert wurde zuvor im Test wortwoertlich
	 * wiederholt und lief bei einer Aenderung der Implementierung auseinander.
	 *
	 * In UE5 wird die Intensitaet einer DirectionalLight in Lux angegeben; eine
	 * neu platzierte Sonne hat 10 lx. Frueher stand hier 3.14f - der
	 * einheitenlose UE4-Altwert (Pi), mit dem die Stadt praktisch schwarz blieb.
	 */
	static float GetMaxSunIntensityLux();

	/** Anteil der Tagesstaerke, der nachts als Mondlicht stehen bleibt. */
	static float GetNightSunFloor();

	// -- Himmel engine-nativ (Nebel, Wolken, Himmelslicht) ---------------------
	//
	// Nebel und Bewoelkung laufen NICHT ueber Niagara: Partikel koennen den
	// Himmel nicht veraendern, und genau das ist der sichtbare Teil des Wetters.
	// Hoehennebel und Wolkenschicht sind Engine-Actors und voll skriptbar; die
	// Kurven hier sind datenrein und getestet (Weather.FXSky).

	/**
	 * Dichte des Hoehennebels aus der Nebel-Intensitaet. Klarer Himmel behaelt
	 * die dezente Grundtruebung der Karte (0,008 aus EnsureLightingActors),
	 * dichter Nebel legt eine graue Wand darueber.
	 */
	static float FogDensityFor(float FogIntensity01);

	/** Untergrenze der Wolkenschicht in km - je bedeckter, desto tiefer. */
	static float CloudLayerBottomKm(float CloudOpacity01);

	/** Dicke der Wolkenschicht in km; 0 heisst "keine Wolken zeichnen". */
	static float CloudLayerHeightKm(float CloudOpacity01);

	/** Faktor auf das Himmelslicht: eine geschlossene Decke schluckt Umgebungslicht. */
	static float SkyLightFactorFor(float CloudOpacity01);

	/**
	 * Zusatzhelligkeit eines Blitzes in Lux, die kurz auf das Sonnenlicht
	 * addiert wird (0 = kein Blitz). Ein Blitz setzt hart ein und klingt schnell
	 * ab - deshalb quadratisch ueber die Restdauer, nicht linear. Engine-nativ
	 * statt Niagara: ein Lichtpuls erhellt die ganze Stadt, ein Partikel nicht.
	 */
	static float LightningFlashLux(float FlashRemainingSeconds, float FlashDurationSeconds);

	/**
	 * Gibt die geforderten User-Parameter-Namen fuer einen Effekt-Typ zurueck
	 * (Vertrag aus WeatherFXCatalog.json, praefixfrei wie ApplyParams sie setzt).
	 */
	static TArray<FString> GetRequiredUserParameters(EWiesbadenWeatherFXType Type);

	/**
	 * Liefert die fehlenden Parameter-Namen (sortiert fuer Determinsmus) - die
	 * datenreine Kernlogik der Vertrags-Validierung. Available ist die Menge
	 * der exponierten User-Parameter des Systems (praefixfrei).
	 */
	static TArray<FString> FindMissingParameters(const TArray<FString>& Required,
		const TSet<FString>& Available);

	/** True, wenn irgendein Niederschlags-Effekt aktiv ist (Regen oder Schnee). */
	bool HasPrecipitation() const { return RainSpawnRate > 0.01f || SnowSpawnRate > 0.01f; }
};

/**
 * Regen und Schnee OHNE Niagara: Steuerwerte fuer das Post-Process-Overlay
 * (/Game/Materials/PostProcess/M_WbWeatherOverlay).
 *
 * Warum es das gibt: die Niagara-Systeme muessen von Hand im Editor gebaut
 * werden - Python exportiert die Niagara-Editor-API nicht. Ein
 * Post-Process-Material dagegen ist ein gewoehnlicher Materialgraph und
 * vollstaendig skriptbar (Tools/add_weather_postprocess.py). Der Niederschlag
 * entsteht damit im Bildraum statt als Partikel.
 *
 * Grenze der Technik, ehrlich benannt: Bildschirm-Niederschlag hat keine Tiefe.
 * Er verschwindet nicht hinter Haeusern und wird von Bruecken nicht
 * abgeschirmt. Dafuer kostet er einen Vollbild-Durchgang statt zehntausender
 * Partikel und ist ohne jede Handarbeit im Editor da.
 *
 * Die Ableitung ist datenrein und getestet (Weather.Overlay); das Setzen der
 * Material-Parameter ist duenne Verdrahtung.
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenWeatherOverlayParams
{
	GENERATED_BODY()

	/** Regendichte 0..1 (Anteil der Bildspalten, in denen es faellt). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather|Overlay")
	float RainStrength = 0.0f;

	/** Schneedichte 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather|Overlay")
	float SnowStrength = 0.0f;

	/** Schraeglage im Bildraum aus dem Wind (0 = senkrecht, 1 = stark schraeg). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather|Overlay")
	float Slant = 0.0f;

	/** Farbe der Tropfen/Flocken - folgt der Lichtfarbe der Sonne. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather|Overlay")
	FLinearColor Tint = FLinearColor::White;

	/** Helligkeit 0..1: nachts gedaempft, sonst waere der Regen greller als die Stadt. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather|Overlay")
	float Brightness = 1.0f;

	/** Leitet die Overlay-Werte aus den FX-Parametern ab (deterministisch). */
	static FWiesbadenWeatherOverlayParams FromFXParams(const FWiesbadenWeatherFXParams& FX);

	/** True, wenn ueberhaupt etwas zu zeichnen ist (sonst Volume abschalten). */
	bool IsVisible() const { return RainStrength > 0.001f || SnowStrength > 0.001f; }
};

/**
 * Treibt Niagara-Wetter-Effekte (Regen, Schnee, Nebel, Wolken, Gewitter) aus
 * der datenreinen FWiesbadenWeatherSystem des City-Subsystems.
 *
 * VERFAHREN (nach unreal-niagara): Jeder Effekt ist ein NS_-Asset mit
 * exponierten User-Parametern. Pro Tick wird der FWiesbadenWeatherState vom
 * City-Subsystem geholt, FWiesbadenWeatherFXParams::FromWeatherState abgeleitet
 * und die aktiven Effekte gespawnt/gesteuert:
 *   - Regen/Schnee: nur aktiv, wenn die SpawnRate > 0 (kein Dauer-Spawn),
 *   - Nebel/Wolken: Opacity ueber User.FogDensity/CloudOpacity,
 *   - Gewitter: User.LightningInterval (0 = kein Blitz),
 *   - alle: User.WindSpeed + User.SunLightColor (Farbtemperatur).
 * Die Komponente haengt am AWiesbadenCityActor (eine pro Stadt).
 */
UCLASS(ClassGroup = "Wiesbaden", meta = (BlueprintSpawnableComponent))
class WIESBADENREAL_API UWiesbadenWeatherFXComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWiesbadenWeatherFXComponent();

	/** Wetter-Zustand holen (vom City-Subsystem) und Effekte anwenden. */
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	// -- Effekt-Assets (User-Parameter siehe FWiesbadenWeatherFXParams) --------

	/**
	 * Kanonischer Content-Pfad eines Effekt-Typs (datenrein, fuer Tests).
	 * In BeginPlay werden nicht manuell zugewiesene Effekte aus diesem Pfad
	 * geladen - sobald die NS_-Assets nach WeatherFXCatalog.json nach
	 * Content/Niagara gebaut sind, braucht die Komponente keine
	 * Details-Panel-Konfiguration (Zuweisung automatisch).
	 */
	static FString GetDefaultAssetPath(EWiesbadenWeatherFXType Type);

	/**
	 * True, wenn eine Fixed-Bounds-Box nutzbar ist (gueltig UND mit Ausdehnung).
	 * Eine nie gesetzte UPROPERTY-FBox ist (0,0,0)-(0,0,0): IsValid() ist wahr,
	 * aber die Extent ist 0 - der Katalog verlangt explizite Fixed Bounds.
	 * Datenrein, testbar.
	 */
	static bool HasUsableFixedBounds(const FBox& Bounds);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|FX|Assets")
	TObjectPtr<UNiagaraSystem> RainSystem = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|FX|Assets")
	TObjectPtr<UNiagaraSystem> SnowSystem = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|FX|Assets")
	TObjectPtr<UNiagaraSystem> FogSystem = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|FX|Assets")
	TObjectPtr<UNiagaraSystem> CloudSystem = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|FX|Assets")
	TObjectPtr<UNiagaraSystem> StormSystem = nullptr;

	// -- Sonnenlicht (DirectionalLight im Level) --------------------------------

	/**
	 * DirectionalLight der Szene, die mit Farbtemperatur + Intensitaet der
	 * Wetter-FX getrieben wird. Wenn leer, wird in BeginPlay automatisch die
	 * erste ADirectionalLight des Levels gefunden (keine Konfiguration noetig).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|FX|Light")
	TObjectPtr<UDirectionalLightComponent> SunLight = nullptr;

	// -- Himmel (in BeginPlay aufgeloest, siehe UpdateSky) ----------------------
	UPROPERTY(Transient)
	TObjectPtr<UExponentialHeightFogComponent> WeatherFog = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UVolumetricCloudComponent> WeatherClouds = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<USkyLightComponent> WeatherSkyLight = nullptr;

	/** Ausgangshelligkeit des Himmelslichts (aus der Karte) als Bezug der Daempfung. */
	float BaseSkyLightIntensity = -1.0f;

	/** Letzter Sichtbarkeitszustand der Wolken - nicht je Bild umschalten. */
	bool bCloudsVisible = true;

	// -- Gewitter (Blitz als Lichtpuls) ----------------------------------------
	/** Zeit seit dem letzten Blitz. */
	float LightningTimer = 0.0f;
	/** Restdauer des laufenden Blitzes. */
	float LightningFlashRemaining = 0.0f;
	/** Einmal-Beleg im Log, dass der Blitz-Takt wirklich feuert. */
	bool bLightningLogged = false;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Wendet die FX-Parameter auf einen gespawnten Effekt an (nur User-Params). */
	static void ApplyParams(UNiagaraComponent* FX, const FWiesbadenWeatherFXParams& Params);

	/** Holt den aktuellen Wetter-Zustand vom City-Subsystem (oder nullptr). */
	const FWiesbadenWeatherState* GetWeatherState() const;

	/** Findet die erste DirectionalLight des Levels (Fallback in BeginPlay). */
	UDirectionalLightComponent* FindSunLight() const;

	/**
	 * Nebel, Wolkenschicht und Himmelslicht aus dem Wetter stellen.
	 *
	 * Der sichtbare Teil des Wetters: ohne das bleibt der Himmel bei Regen
	 * strahlend blau, weil Partikel die Himmelskuppel nicht anfassen koennen.
	 */
	void UpdateSky(const FWiesbadenWeatherFXParams& Params);

	/** Hoehennebel des Levels (EnsureLightingActors legt ihn in jeder Karte an). */
	UExponentialHeightFogComponent* FindFog() const;

	/**
	 * Wolkenschicht des Levels - und legt sie an, wenn keine da ist.
	 *
	 * Gebackene Karten haben keinen Wolken-Actor (der Bake erzeugt nur Sonne,
	 * Himmel und Nebel). Zur Laufzeit nachruesten spart einen zweistuendigen
	 * Re-Bake und wirkt sofort auf jeder bestehenden Karte.
	 */
	UVolumetricCloudComponent* FindOrSpawnClouds();

	/**
	 * Regen/Schnee als Bildschirm-Overlay stellen (ohne Niagara).
	 *
	 * Laeuft UNABHAENGIG von den NS_-Assets: auch wenn kein einziges
	 * Niagara-System existiert, faellt damit sichtbarer Niederschlag.
	 */
	void UpdateOverlay(const FWiesbadenWeatherFXParams& Params);

	/**
	 * Legt Material-Instanz und unbegrenztes PostProcessVolume an (einmalig).
	 * Gibt false zurueck, wenn das Material fehlt - dann bleibt es dabei.
	 */
	bool EnsureOverlay();

	/** Himmelslicht des Levels (fuer die Daempfung unter Wolken). */
	USkyLightComponent* FindSkyLight() const;

	/**
	 * Treibt den Blitz-Takt und liefert die Zusatzhelligkeit dieses Bildes in Lux.
	 * Ausserhalb eines Gewitters (LightningInterval <= 0) immer 0.
	 */
	float UpdateLightning(const FWiesbadenWeatherFXParams& Params, float DeltaTime);

	/** Laedt nicht manuell zugewiesene Effekt-Systeme aus den Default-Pfaden. */
	void LoadDefaultSystems();

	/**
	 * Warnt EINMALIG pro System, wenn es laut Katalog Fixed Bounds braucht,
	 * aber keine nutzbaren gesetzt hat (sonst Culling-Risiko).
	 */
	void WarnMissingFixedBounds(const UNiagaraSystem* System, const FString& EffectName);

	/**
	 * Prueft ein zugewiesenes System gegen den Parameter-Vertrag des Effekt-Typs
	 * (WeatherFXCatalog.json) und warnt bei fehlenden User-Parametern - kein
	 * stummer Vertragsbruch, kein Absturz.
	 */
	void ValidateSystem(UNiagaraSystem* System, EWiesbadenWeatherFXType Type,
		const FString& EffectName) const;

	// -- Bildschirm-Niederschlag (ohne Niagara) --------------------------------

	/** Lebende Instanz von M_WbWeatherOverlay; traegt die Parameter je Bild. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> OverlayMID = nullptr;

	/** Unbegrenztes PostProcessVolume, das das Overlay traegt. */
	UPROPERTY(Transient)
	TObjectPtr<APostProcessVolume> OverlayVolume = nullptr;

	/** Fehlendes Overlay-Material nur einmal melden. */
	bool bOverlayWarned = false;

	/** Einmal-Beleg, dass das Overlay steht (mit den EINGESCHWUNGENEN Werten). */
	bool bOverlayReported = false;

	/** Staerken des letzten Bildes - der Beleg soll nicht waehrend der
	 *  Wetter-Ueberblendung feuern und dann 0,01 statt 0,70 melden. BEIDE
	 *  Werte, denn bei Schnee steht der Regen von Anfang an auf 0 und der
	 *  Beleg haette sich fuer "eingeschwungen" gehalten. */
	float LastOverlayRain = -1.0f;
	float LastOverlaySnow = -1.0f;

	/** Wie viele Bilder die Staerken schon unveraendert sind. */
	int32 OverlaySettledFrames = 0;

	/** Lagebericht ueber die Wetter-Partikel nur einmal je Sitzung ausgeben. */
	bool bReportedFXSetup = false;

	// Gespawnte Effekte (GC-verfolgt). Wiederverwendet ueber die Lebenszeit.
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> RainFX = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> SnowFX = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> FogFX = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> CloudFX = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> StormFX = nullptr;

	/** Zuletzt angewendete Parameter (Vergleich fuer Effekt-Aktiv/Inaktiv). */
	FWiesbadenWeatherFXParams LastAppliedParams;

	/** Bereits gewarnte Systeme (Pfad-Name) - Warnung nur einmalig je System. */
	TSet<FString> FixedBoundsWarningsLogged;
};
