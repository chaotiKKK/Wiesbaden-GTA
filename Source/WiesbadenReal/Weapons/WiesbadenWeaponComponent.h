// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "Components/SceneComponent.h"
#include "CoreMinimal.h"

#include "Weapons/WiesbadenBallistics.h"
#include "Weapons/WiesbadenGunshotSynth.h"
#include "Weapons/WiesbadenWeaponSpec.h"

#include "WiesbadenWeaponComponent.generated.h"

class UAudioComponent;
class UPointLightComponent;
class USoundBase;
class USoundWaveProcedural;
class UStaticMeshComponent;

/**
 * Ein abgefeuertes Projektil mit seiner Leuchtspur (Kopplung Flug/Sicht).
 *
 * Die Flugbahn rechnet WiesbadenBallistics::Step (Gravitation, Restreich-
 * weite); die Leuchtspur folgt der Projektile-Position, statt sofort aufs
 * Ziel zu laufen - so ist die Flugzeit sichtbar (Entwurf 2026-09).
 */
struct FWiesbadenLiveShot
{
	FWiesbadenProjectile Projectile;
	int32 TracerIndex = INDEX_NONE;
};

/**
 * Ein sichtbarer Leuchtspur-Flug vom Lauf zum Einschlag.
 *
 * Der Treffer wird sofort per Strahl bestimmt - das ist genau und faellt bei
 * Entfernungen unter 200 m auch nicht auf. Sichtbar fliegt die Spur trotzdem,
 * denn ohne fliegendes Geschoss sieht man nicht, wohin man schiesst. Genau so
 * arbeiten die meisten Spiele.
 */
USTRUCT()
struct FWiesbadenTracer
{
	GENERATED_BODY()

	/** Startpunkt (Muendung) in Weltkoordinaten. */
	FVector Start = FVector::ZeroVector;

	/** Endpunkt (Einschlag oder Reichweitenende). */
	FVector End = FVector::ZeroVector;

	/** Bisher zurueckgelegter Anteil der Strecke (0..1). */
	float Alpha = 0.0f;

	/** Anteil je Sekunde - aus Muendungsgeschwindigkeit und Strecke. */
	float Speed = 1.0f;

	/** True, sobald der Einschlag erreicht ist. */
	bool bFinished = false;
};

/**
 * Die Waffe des Spielers: Modell, Muendungsfeuer, Schussgeraeusch,
 * Leuchtspuren und Einschlaege.
 *
 * Zuvor bestand die Waffe aus einem flachen Quader und einer
 * DrawDebugLine - kein Ton, kein sichtbares Geschoss, kein Einschlag. Man
 * konnte nicht erkennen, ob ueberhaupt geschossen wurde.
 *
 * Das Modell wird aus Grundkoerpern zusammengesetzt, in den Massen einer
 * Maschinenpistole: Gehaeuse, Lauf, Muendungsbremse, Magazin, Griff,
 * Schulterstuetze und Visier. Alle Teile haengen an dieser Komponente, damit
 * die Waffe als Ganzes bewegt und ausgerichtet werden kann.
 */
UCLASS(ClassGroup = (Wiesbaden), meta = (BlueprintSpawnableComponent))
class WIESBADENREAL_API UWiesbadenWeaponComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UWiesbadenWeaponComponent();

	/** Baut Modell, Muendungslicht und Klang auf. Idempotent. */
	void SetupWeapon();

	/**
	 * Gibt einen Schuss ab: Strahl, Leuchtspur, Muendungsfeuer, Klang.
	 *
	 * Start und Direction kommen aus der KAMERA, nicht aus dem Lauf: Der
	 * Spieler zielt mit dem Blick, und ein Schuss aus der Hueftposition traefe
	 * sichtbar daneben. Die Leuchtspur startet trotzdem am Lauf - sonst
	 * entstuende sie sichtbar im Gesicht des Spielers.
	 */
	void Fire(const FVector& AimStart, const FVector& AimDirection);

	/** Treibt Leuchtspuren, Projektile, Muendungslicht und Rueckstoss weiter. */
	void TickWeapon(float DeltaSeconds);

	/** Waffe aus der Tabelle (WiesbadenWeapons::Spec), Default = die MP. */
	UPROPERTY(EditAnywhere, Category = "Waffe")
	int32 WeaponIndex = static_cast<int32>(EWiesbadenWeaponId::Maschinenpistole);

	/** Waffe waehlen (Tasten 1-9 am FootPawn); meldet die neue Spezifikation. */
	void SetWeaponIndex(int32 InIndex);

	/** Gibt es diese Waffe ueberhaupt? (Tastenbelegung vor dem Umschalten.) */
	static bool IsValidWeaponIndex(int32 Index);

	/** Zahl der gerade fliegenden Projektile (fuer Pruefungen). */
	int32 GetActiveProjectileCount() const { return LiveShots.Num(); }

	/** Weltposition der Muendung - Ursprung der Leuchtspuren. */
	FVector GetMuzzleLocation() const;

	/** Zahl der gerade fliegenden Leuchtspuren (fuer Pruefungen). */
	int32 GetActiveTracerCount() const { return Tracers.Num(); }

	/**
	 * Schrittweite einer Leuchtspur, datenrein.
	 *
	 * Liefert den neuen Streckenanteil. Ausgelagert und statisch, damit der
	 * Flug ohne Welt pruefbar ist.
	 */
	static float AdvanceTracerAlpha(float Alpha, float Speed, float DeltaSeconds);

	/** Reichweite des Strahls in Metern. */
	UPROPERTY(EditAnywhere, Category = "Waffe", meta = (ClampMin = "1.0"))
	float RangeMeters = 200.0f;

	/** Muendungsgeschwindigkeit der Leuchtspur in m/s. */
	UPROPERTY(EditAnywhere, Category = "Waffe", meta = (ClampMin = "10.0"))
	float MuzzleVelocityMetersPerS = 380.0f;

	/**
	 * Streuung in Grad (Radius des Streukreises).
	 *
	 * Ohne Streuung landet jeder Schuss auf demselben Punkt - das sieht nach
	 * Laserpointer aus, nicht nach Waffe.
	 */
	UPROPERTY(EditAnywhere, Category = "Waffe", meta = (ClampMin = "0.0"))
	float SpreadDegrees = 0.7f;

	/** Wie lange das Muendungsfeuer leuchtet, in Sekunden. */
	UPROPERTY(EditAnywhere, Category = "Waffe", meta = (ClampMin = "0.005"))
	float MuzzleFlashSeconds = 0.045f;

	/** Helligkeit des Muendungsfeuers (Candela). */
	UPROPERTY(EditAnywhere, Category = "Waffe", meta = (ClampMin = "0.0"))
	float MuzzleFlashIntensity = 60000.0f;

	/** Rueckstoss: Wie weit die Waffe je Schuss zurueckweicht, in cm. */
	UPROPERTY(EditAnywhere, Category = "Waffe", meta = (ClampMin = "0.0"))
	float RecoilOffsetCm = 2.2f;

	/** Wie schnell die Waffe aus dem Rueckstoss zurueckkehrt (Anteil je Sekunde). */
	UPROPERTY(EditAnywhere, Category = "Waffe", meta = (ClampMin = "0.1"))
	float RecoilRecoveryRate = 9.0f;

	/** Klangliche Kenndaten des Schusses. */
	UPROPERTY(EditAnywhere, Category = "Waffe|Klang")
	FWiesbadenGunshotParams GunshotParams;

private:
	/** Setzt das Modell aus Grundkoerpern zusammen. */
	void BuildWeaponMesh();

	/** Fliegende Projektile einen Schritt weiter; Aufschlag auswerten. */
	void StepProjectiles(float DeltaSeconds);

	/** Treffer am Aufschlag auswerten (Ziel-Adapter, Impuls, Effekt). */
	void ResolveImpact(AActor* HitActor, UPrimitiveComponent* HitComponent,
		const FVector& ImpactPoint, const FWiesbadenProjectile& P);

	/** Explosion am Punkt: alle Ziele im Radius, inkl. Eigenschaden. */
	void ApplyExplosionAt(const FVector& Centre, const FWiesbadenProjectile& P);

	/** Passanten im Aufschlag-Umkreis zu Boden + Tat ins Fahndungskonto. */
	void ReportPedestrianAndWanted(UWorld* World, const FVector& ImpactPoint, float Damage);

	/** Legt die prozedurale Klangquelle an. */
	void SetupAudio();

	/** Erzeugt einen Schuss und schiebt ihn in die Klangquelle. */
	void PlayGunshot();

	/** Ein Teil des Waffenmodells. */
	UStaticMeshComponent* AddPart(
		const TCHAR* Name, const TCHAR* MeshPath,
		const FVector& PartLocation, const FVector& Scale,
		const FRotator& Rotation, UMaterialInterface* Material);

	UPROPERTY(Transient)
	TArray<UStaticMeshComponent*> Parts;

	/** Ursprung der Leuchtspuren - ein leerer Punkt an der Laufspitze. */
	UPROPERTY(Transient)
	USceneComponent* Muzzle = nullptr;

	UPROPERTY(Transient)
	UPointLightComponent* MuzzleLight = nullptr;

	UPROPERTY(Transient)
	UAudioComponent* ShotAudio = nullptr;

	UPROPERTY(Transient)
	USoundWaveProcedural* ShotWave = nullptr;

	/** Echte Schuss-Aufnahme (erste Wahl); null = Synth-Rueckfall. */
	UPROPERTY(Transient)
	USoundBase* ShotSample = nullptr;

	/** Restzeit des Muendungsfeuers. */
	float MuzzleFlashRemaining = 0.0f;

	/** Aktueller Rueckstoss-Versatz in cm (negativ = nach hinten). */
	float RecoilOffset = 0.0f;

	/** Ruhelage des Modells - Bezug fuer den Rueckstoss. */
	FVector RestLocation = FVector::ZeroVector;

	/** Zaehler der Schuesse - dient als Seed, damit sie sich unterscheiden. */
	int32 ShotCounter = 0;

	TArray<FWiesbadenTracer> Tracers;

	/** Fliegende Schuesse (Projektil + gekoppelte Leuchtspur). */
	TArray<FWiesbadenLiveShot> LiveShots;

	bool bSetupDone = false;
};
