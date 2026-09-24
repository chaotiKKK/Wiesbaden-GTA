// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"

#include "WiesbadenCarLightsComponent.generated.h"

class USpotLightComponent;
class UPointLightComponent;
class UMaterialInstanceDynamic;

/** Schaltstufen des Fahrlichts (StVZO-Reihenfolge). */
UENUM(BlueprintType)
enum class EWiesbadenHeadlightMode : uint8
{
	Off			UMETA(DisplayName = "Aus"),
	Parking		UMETA(DisplayName = "Standlicht"),
	LowBeam		UMETA(DisplayName = "Abblendlicht"),
	HighBeam	UMETA(DisplayName = "Fernlicht"),
	MAX			UMETA(Hidden)
};

/** Zustand der Fahrtrichtungsanzeiger. */
UENUM(BlueprintType)
enum class EWiesbadenIndicatorMode : uint8
{
	Off		UMETA(DisplayName = "Aus"),
	Left	UMETA(DisplayName = "Links"),
	Right	UMETA(DisplayName = "Rechts"),
	Hazard	UMETA(DisplayName = "Warnblinkanlage"),
	MAX		UMETA(Hidden)
};

/**
 * Lichtanlage des Fahrzeugs.
 *
 * Umfasst Fahrlicht (Stand/Abblend/Fern), Bremslicht, Rueckfahrlicht und die
 * Fahrtrichtungsanzeiger einschliesslich Warnblinkanlage.
 *
 * Scheinwerfer werfen gestaffelte breite/enge Lichtkegel auf die Strasse.
 * Kleine emissive Linsen machen Schluss- und Bremslicht auch von hinten
 * sichtbar, ohne einen roten Lichtball auf der Fahrbahn zu erzeugen.
 *
 * Die Blinkfrequenz betraegt 1,5 Hz. Das ist kein gegriffener Wert: die StVZO
 * schreibt 1,5 Hz +/- 0,5 vor, und eine abweichende Frequenz faellt sofort als
 * falsch auf, weil das Blinken ein sehr vertrauter Rhythmus ist.
 */
UCLASS(ClassGroup = (Wiesbaden), meta = (BlueprintSpawnableComponent))
class WIESBADENREAL_API UWiesbadenCarLightsComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UWiesbadenCarLightsComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaSeconds, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	// -- Bedienung ----------------------------------------------------------

	/** Schaltet eine Stufe weiter: Aus -> Stand -> Abblend -> Fern -> Aus. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Fahrzeug|Licht")
	void CycleHeadlights();

	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Fahrzeug|Licht")
	void SetHeadlightMode(EWiesbadenHeadlightMode NewMode);

	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Fahrzeug|Licht")
	EWiesbadenHeadlightMode GetHeadlightMode() const { return HeadlightMode; }

	/**
	 * Blinker links schalten. Erneutes Betaetigen schaltet aus - so wie ein
	 * Blinkerhebel, der beim Zurueckstellen ausrastet.
	 */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Fahrzeug|Licht")
	void ToggleIndicatorLeft();

	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Fahrzeug|Licht")
	void ToggleIndicatorRight();

	/** Warnblinkanlage. Hat Vorrang vor einem gesetzten Einzelblinker. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Fahrzeug|Licht")
	void ToggleHazardLights();

	/**
	 * Lichthupe: solange gedrueckt Fernlicht, danach der Modus von vorher.
	 *
	 * Aufblenden fehlte dem Projekt - es gab nur das Durchschalten der
	 * Lichtstufen auf L, und das laesst sich im Verkehr nicht als Signal
	 * benutzen.
	 */
	void SetHeadlightFlash(bool bPressed);

	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Fahrzeug|Licht")
	EWiesbadenIndicatorMode GetIndicatorMode() const { return IndicatorMode; }

	/** True, wenn die Blinkleuchten gerade hell sind (fuer HUD-Kontrollleuchte). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Fahrzeug|Licht")
	bool IsBlinkPhaseOn() const { return bBlinkOn; }

	/** Bremslicht setzen (wird vom Fahrzeug je Tick aus dem Bremspedal gespeist). */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Fahrzeug|Licht")
	void SetBraking(bool bInBraking);

	/** Rueckfahrlicht setzen. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Fahrzeug|Licht")
	void SetReversing(bool bInReversing);

	// -- Datenreine Hilfsfunktionen (testbar ohne Welt) ---------------------

	/**
	 * Hell-Phase des Blinkers zu einem Zeitpunkt.
	 * Erste Halbwelle hell, zweite dunkel - Tastverhaeltnis 50 %.
	 */
	static bool ComputeBlinkOn(float TimeSeconds, float FrequencyHz);

	/** Naechste Stufe des Fahrlichts. */
	static EWiesbadenHeadlightMode GetNextHeadlightMode(EWiesbadenHeadlightMode Current);

	/**
	 * Braucht es bei diesem Sonnenstand Abblendlicht?
	 *
	 * Datenrein, damit die Schwelle ohne Welt pruefbar ist (Test
	 * Vehicles.CarLights.AutomaticHeadlights).
	 *
	 * Der Grenzwert liegt bewusst UEBER null: Bei Sonnenstand 0 ist es bereits
	 * finster. Licht gehoert in der Daemmerung an, nicht erst danach.
	 */
	static bool ShouldUseHeadlights(float SunElevationFactor);

	/** Sonnenstand, unterhalb dessen automatisch Abblendlicht gesetzt wird. */
	static constexpr float AutoHeadlightSunThreshold = 0.18f;

	/**
	 * Setzt das Licht nach Tageszeit - solange der Fahrer nicht selbst
	 * eingegriffen hat.
	 *
	 * Ohne diese Automatik fuhr man nach Einbruch der Dunkelheit ohne
	 * Beleuchtung durch eine voellig unbeleuchtete Stadt: Die Tageszeit
	 * schreitet mit einer Stunde je 2,5 Realminuten fort, nach rund einer
	 * halben Stunde Spielzeit war es Nacht, und die Scheinwerfer standen
	 * weiter auf AUS. Ein echtes Auto hat dafuer einen Lichtsensor.
	 */
	void SetAutomaticHeadlights(bool bWantHeadlights);

	/**
	 * Ergebnis einer Blinker-Betaetigung.
	 * Gleiche Richtung erneut = aus; Warnblinker ueberschreibt jede Richtung.
	 */
	static EWiesbadenIndicatorMode ApplyIndicatorToggle(
		EWiesbadenIndicatorMode Current, EWiesbadenIndicatorMode Requested);

	/** True, wenn bei diesem Modus die linke Seite blinkt. */
	static bool IndicatorAffectsLeft(EWiesbadenIndicatorMode Mode);

	/** True, wenn bei diesem Modus die rechte Seite blinkt. */
	static bool IndicatorAffectsRight(EWiesbadenIndicatorMode Mode);

	// -- Einbaulage ---------------------------------------------------------
	// Vorgaben passen zum Kaefer-Platzhalter (4,15 m lang, 1,54 m breit).
	// Alle Angaben in cm im Fahrzeug-Lokalsystem: +X vorwaerts, +Y rechts.

	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Licht|Einbau")
	FVector HeadlightOffset = FVector(170.0, 50.0, 80.0);

	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Licht|Einbau")
	FVector TailLightOffset = FVector(-190.0, 50.0, 74.0);

	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Licht|Einbau")
	FVector FrontIndicatorOffset = FVector(145.0, 66.0, 67.0);

	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Licht|Einbau")
	FVector RearIndicatorOffset = FVector(-190.0, 50.0, 87.0);

	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Licht|Einbau")
	FVector ReverseLightOffset = FVector(-200.0, 30.0, 62.0);

	// -- Kennwerte ----------------------------------------------------------

	/** Blinkfrequenz in Hz. StVZO: 1,5 +/- 0,5. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Licht", meta = (ClampMin = "0.5", ClampMax = "3.0"))
	float BlinkFrequencyHz = 1.5f;

	/** Lichtstaerke des Abblendlichts in Candela. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Licht", meta = (ClampMin = "0.0"))
	float LowBeamIntensity = 12000.0f;

	/** Lichtstaerke des Fernlichts. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Licht", meta = (ClampMin = "0.0"))
	float HighBeamIntensity = 34000.0f;

	/** Lichtstaerke des Standlichts. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Licht", meta = (ClampMin = "0.0"))
	float ParkingIntensity = 1200.0f;

	/**
	 * Lichtstaerke des Bremslichts.
	 *
	 * Hier standen 5200 Candela - als PUNKTLICHT, das in alle Richtungen
	 * strahlt. Zum Vergleich: Das Abblendlicht hat 12000, aber als eng
	 * gebuendelter Scheinwerfer. Ein reales Bremslicht liegt bei 80 bis 100 cd.
	 *
	 * Nachts war der stehende Wagen deshalb in einen roten Lichtball gehuellt,
	 * der Karosserie, Fahrbahn und halben Bildschirm ueberstrahlte. 400 cd
	 * liegen bewusst ueber dem realen Wert, damit das Licht im Spiel deutlich
	 * ablesbar bleibt, ohne die Umgebung einzufaerben.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Licht", meta = (ClampMin = "0.0"))
	float BrakeLightIntensity = 400.0f;

	/** Lichtstaerke des Schlusslichts (real 4 bis 8 cd). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Licht", meta = (ClampMin = "0.0"))
	float TailLightIntensity = 80.0f;

	/**
	 * Reichweite der Signalleuchten (Schluss-, Brems-, Blink-, Rueckfahrlicht).
	 *
	 * Deutlich kleiner als bei den Scheinwerfern: Eine Signalleuchte soll
	 * GESEHEN werden, nicht die Umgebung ausleuchten. Mit den urspruenglichen
	 * 450 cm faerbte das Bremslicht die Fahrbahn im Umkreis rot.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Licht", meta = (ClampMin = "10.0"))
	float SignalLightRadiusCm = 160.0f;

	/** Oeffnungswinkel des Abblendlichts (aussen) in Grad. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Licht", meta = (ClampMin = "1.0", ClampMax = "80.0"))
	float LowBeamOuterConeAngle = 38.0f;

	/** Oeffnungswinkel des Fernlichts - enger und dadurch weiter reichend. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Licht", meta = (ClampMin = "1.0", ClampMax = "80.0"))
	float HighBeamOuterConeAngle = 24.0f;

	/**
	 * Neigung des Abblendlichts nach unten in Grad. Ohne Absenkung blendet
	 * das Licht den Gegenverkehr und leuchtet in den Himmel statt auf die
	 * Fahrbahn.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Licht", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float LowBeamDownwardPitch = 7.0f;

	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Licht")
	FColor HeadlightColor = FColor(255, 244, 214);

	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Licht")
	FColor TailLightColor = FColor(255, 32, 24);

	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Fahrzeug|Licht")
	FColor IndicatorColor = FColor(255, 150, 20);

private:
	void CreateLights();
	void ApplyHeadlightState();
	void ApplyIndicatorState();
	void ApplyRearLightState();

	USpotLightComponent* MakeSpotLight(const TCHAR* Name, const FVector& Offset, const FRotator& Rotation);
	UPointLightComponent* MakePointLight(const TCHAR* Name, const FVector& Offset, const FColor& Color);
	UMaterialInstanceDynamic* MakeLens(const TCHAR* Name, const FVector& Offset,
		const FVector& Scale, const FLinearColor& Color);

	UPROPERTY(Transient)
	TArray<USpotLightComponent*> Headlights;
	UPROPERTY(Transient)
	TArray<USpotLightComponent*> FocusedBeams;
	UPROPERTY(Transient)
	TArray<USpotLightComponent*> HighBeams;
	UPROPERTY(Transient)
	TArray<UMaterialInstanceDynamic*> HeadlightLenses;
	UPROPERTY(Transient)
	TArray<UMaterialInstanceDynamic*> TailLenses;
	UPROPERTY(Transient)
	TArray<UMaterialInstanceDynamic*> LeftIndicatorLenses;
	UPROPERTY(Transient)
	TArray<UMaterialInstanceDynamic*> RightIndicatorLenses;

	UPROPERTY(Transient)
	TArray<UPointLightComponent*> ParkingLights;

	UPROPERTY(Transient)
	TArray<UPointLightComponent*> TailLights;

	UPROPERTY(Transient)
	TArray<UPointLightComponent*> LeftIndicators;

	UPROPERTY(Transient)
	TArray<UPointLightComponent*> RightIndicators;

	UPROPERTY(Transient)
	UPointLightComponent* ReverseLight = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Fahrzeug|Licht")
	EWiesbadenHeadlightMode HeadlightMode = EWiesbadenHeadlightMode::Off;

	/**
	 * True, solange die Lichtautomatik zustaendig ist. Der erste Druck auf die
	 * Lichttaste uebergibt die Kontrolle dauerhaft an den Fahrer - eine
	 * Automatik, die eine bewusste Wahl wieder ueberschreibt, waere schlimmer
	 * als gar keine.
	 */
	bool bAutomaticHeadlights = true;

	/** True, solange die Lichthupe gedrueckt ist. */
	bool bFlashing = false;

	/** Lichtstufe vor dem Aufblenden - dorthin geht es danach zurueck. */
	EWiesbadenHeadlightMode ModeBeforeFlash = EWiesbadenHeadlightMode::Off;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Fahrzeug|Licht")
	EWiesbadenIndicatorMode IndicatorMode = EWiesbadenIndicatorMode::Off;

	/** Laufzeit seit Einschalten des Blinkers - Basis der Blinkphase. */
	float BlinkTime = 0.0f;

	bool bBlinkOn = false;
	bool bBraking = false;
	bool bReversing = false;
};
