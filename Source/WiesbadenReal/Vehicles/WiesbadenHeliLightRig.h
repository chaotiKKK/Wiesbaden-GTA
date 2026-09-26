// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"

#include "WiesbadenHeliLightRig.generated.h"

class UPointLightComponent;
class USpotLightComponent;
class USpotLightComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

/**
 * Lichtbastel des Hubschraubers: Positionslichter, Stroboskop, Landlicht und
 * zwei starke Suchscheinwerfer.
 *
 * WARUM EIGENE KOMPONENTE UND NICHT EINFACH LICHTER IM PAWN:
 * Das Modell bringt drei Leuchtenkörper als eigene Meshes mit (Nav_Green,
 * Nav_Red, Strobe_White). Die will man an den Rumpf heften UND mit eigenem
 * Leuchtstoff versorgen. Dazu kommen zwei bewegliche Scheinwerfer, die
 * schwenken. Beides ist eine Einheit mit eigenem Leben (Blinktakt,
 * Schwenk, Sichtbarkeit) und gehört darum in eine Komponente, die der Pawn
 * nur aufruft - so bleibt die Steuerung des Flugzeugs im Pawn und die
 * Beleuchtung hier, statt sich in einer 1200-zeiligen Datei zu verlieren.
 *
 * Die Suchscheinwerfer sind bewusst KEIN Post-Process-Volumen, sondern
 * echte USpotLightComponent: sie werfen Licht auf Gelaende und Gebäude,
 * erzeugen einen sichtbaren Kegel durch den Rotorstaub und lassen sich
 * einzeln schalten. Zwei sind es, weil ein einzelner Scheinwerfer an einem
 * Helikopter wie ein fehlendes Bauteil wirkt.
 *
 * Aufbau (alles relativ zur Nabe des Pawns, nicht zum Rumpf):
 *   NavBodies   drei Leuchtenkoerper-Meshes des Modells
 *   StrobeLicht weisses Stroboskop ueber dem Heck
 *   Landlicht   SpotLight nach unten aus dem Rumpf
 *   SuchscheinwerferA/B  zwei SpotLights, symmetrisch am Rumpf
 */
UCLASS(ClassGroup = (Wiesbaden), meta = (BlueprintSpawnableComponent))
class WIESBADENREAL_API UWiesbadenHeliLightRig : public USceneComponent
{
	GENERATED_BODY()

public:
	UWiesbadenHeliLightRig();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/** Schaltet die beiden Suchscheinwerfer ein oder aus. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Heli|Licht")
	void SetSearchlights(bool bOn);

	/** Sind die Suchscheinwerfer an? */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Licht")
	bool AreSearchlightsOn() const { return bSearchlightsOn; }

	/**
	 * Massstab und Gierdrehung des Modells uebernehmen.
	 *
	 * Der Pawn ruft das einmal im Konstruktor mit DENSELBEN Werten, mit denen
	 * er Rumpf und Rotoren aufspannt. Ohne diese Uebergabe muesste die
	 * Bastel ihre eigene Kopie der Modelldrehung fuehren - und die beiden
	 * Kopien laufen auseinander, sobald jemand das Modell dreht. Genau
	 * daran ist vorher gescheitert, dass die Leuchten an der falschen Seite
	 * des Rumpfes hingen.
	 */
	void SetModelTransform(float InScale, const FRotator& InYaw);

	/** Schaltet das Stroboskop ein/aus (Standard: selbstlaeufig). */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Heli|Licht")
	void SetStrobeEnabled(bool bOn);

	/** Licht insgesamt an/aus (z. B. bei Zerstoerung). */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Heli|Licht")
	void SetAllLightsEnabled(bool bOn);

	/** Leuchtstaerke der Positionslichter (0 blendet sie aus). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|Licht",
		meta = (ClampMin = "0.0", ClampMax = "50.0"))
	float NavLightIntensity = 4200.0f;

	/** Reichweite der Positionslichter in cm. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|Licht", meta = (ClampMin = "0.0"))
	float NavLightAttenuationRadius = 2600.0f;

	/**
	 * Lichtkegel der Suchscheinwerfer in Grad (halber Winkel).
	 *
	 * 12 Grad ist ein echter, gut gebuendelter Strahl. Man sollte weder die
	 * Gasse der Suchscheinwerfer noch den Lichtkegel eines aufgegebenen
	 * Hubschraubers sehen.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|Licht", meta = (ClampMin = "1.0", ClampMax = "60.0"))
	float SearchlightConeAngle = 12.0f;

	/** Reichweite eines Suchscheinwerfers in cm. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|Licht", meta = (ClampMin = "0.0"))
	float SearchlightRange = 9000.0f;

	/** Wie schnell ein Suchscheinwerfer der Zielrichtung folgt (1/s). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|Licht", meta = (ClampMin = "0.1"))
	float SearchlightAimResponse = 4.5f;

	/** Periode des Stroboskopblitzens in Sekunden. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|Licht", meta = (ClampMin = "0.05"))
	float StrobePeriod = 1.15f;

	/** Laenge des einzelnen Blitzes in Sekunden. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Heli|Licht", meta = (ClampMin = "0.005"))
	float StrobeFlash = 0.055f;

	/** Landlicht an/aus. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Heli|Licht")
	void SetLandingLight(bool bOn);

	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Licht")
	bool IsLandingLightOn() const { return bLandingLightOn; }

	/** Schwenkt beide Suchscheinwerfer (je -1..1, 1 = Anschlag). */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Heli|Licht")
	void SetSearchlightAim(float Horizontal, float Vertical);

	/**
	 * Richtet den Lichtkegel auf einen Weltpunkt.
	 *
	 * Das ist der Weg, den das Spiel benutzt: der Scheinwerfer soll dort
	 * hinzeigen, wo die Rohrmuendung hinzeigt - SetSearchlightAim mit
	 * Handwerten kann das nur annaehern, weil Nase und Pylon 2,6 m
	 * auseinanderliegen. Die Umrechnung geschieht im Modellraum, damit die
	 * Gierdrehung des Hubschraubers mitgedreht wird.
	 */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Heli|Licht")
	void SetSearchlightTarget(const FVector& Weltziel);

	/** Suchscheinwerfer links (Steuerbord-Seite des Rumpfes). */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Licht")
	USpotLightComponent* GetSearchlightA() const { return SearchlightA; }

	/** Suchscheinwerfer rechts. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Licht")
	USpotLightComponent* GetSearchlightB() const { return SearchlightB; }

	/** Knoten im Modellraum: traegt Massstab und Gierdrehung des Modells. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Heli|Licht")
	USceneComponent* GetModelSpace() const { return ModelSpace; }

protected:
	/**
	 * Knoten im Modellraum: alle Leuchten und Koerper haengen hier.
	 *
	 * Er traegt Massstab und Gierdrehung des Modells (SetModelTransform) und
	 * haelt damit die Montage an einer Stelle.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli|Licht")
	USceneComponent* ModelSpace = nullptr;

	/** Rot (Backbord) - Sitz des Leuchtenkoerpers aus dem Modell. */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli|Licht")
	UStaticMeshComponent* NavBodyRed = nullptr;

	/** Gruen (Steuerbord). */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli|Licht")
	UStaticMeshComponent* NavBodyGreen = nullptr;

	/** Weisses Stroboskop am Heck. */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli|Licht")
	UStaticMeshComponent* NavBodyStrobe = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli|Licht")
	UPointLightComponent* NavLightRed = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli|Licht")
	UPointLightComponent* NavLightGreen = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli|Licht")
	UPointLightComponent* StrobeLight = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli|Licht")
	USpotLightComponent* LandingLight = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli|Licht")
	USpotLightComponent* SearchlightA = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Heli|Licht")
	USpotLightComponent* SearchlightB = nullptr;

	/** Leuchtstoff der drei Leuchtenkoerper (MID aus dem Nav-Material). */
	UPROPERTY(Transient)
	UMaterialInstanceDynamic* NavBodyRedMID = nullptr;

	UPROPERTY(Transient)
	UMaterialInstanceDynamic* NavBodyGreenMID = nullptr;

	UPROPERTY(Transient)
	UMaterialInstanceDynamic* NavBodyStrobeMID = nullptr;

	/** Schwenkwinkel der Suchscheinwerfer in Grad (waagerecht/senkrecht). */
	float SearchlightYaw = 0.0f;
	float SearchlightPitch = -12.0f;
	float WantedYaw = 0.0f;
	float WantedPitch = -12.0f;

	bool bSearchlightsOn = false;
	bool bLandingLightOn = false;
	bool bStrobeEnabled = true;
	bool bAllLightsEnabled = true;

	/** Laufzeit des Stroboskopblitzens. */
	float StrobeTimer = 0.0f;
	bool bStrobeFlashOn = false;
};
