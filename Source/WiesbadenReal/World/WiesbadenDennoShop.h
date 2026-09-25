// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Missions/WiesbadenDennoDelivery.h"
#include "WiesbadenDennoShop.generated.h"

class UGeoCoordinateConverter;
class UStaticMeshComponent;
class UPointLightComponent;
class UMeshComponent;

/** Was der Laden mit dem Ergebnis der Wandsuche tut. */
enum class EDennoShopBuild : uint8
{
	Build,   // Wand gefunden - Laden bauen
	Wait,    // noch nicht gestreamt - naechster Tick
	GiveUp   // nach der Wartezeit keine Wand: KEIN Laden (statt eines schwebenden)
};

/** Denno-Pose relativ zur Grundstellung: Drehung um die Fuesse + Atem-Skalierung. */
struct FDennoIdlePose
{
	FRotator Rotation = FRotator::ZeroRotator;   // Pitch/Roll = Gewicht verlagern, Yaw = umschauen
	FVector Scale = FVector::OneVector;          // XY = Brustkorb weitet sich, Z = hebt sich
};

/**
 * Dennos Laden im Erdgeschoss von Sedanplatz 5 (OSM 175418681): Cafe in der
 * Nordhaelfte, Friseur in der Suedhaelfte, Denno selbst im Cafe.
 *
 * Das Haus ist GEBACKEN - eine geschlossene Chunk-Fassade mit aufgemalten
 * Fenstern. Ohne Re-Bake wird es so geoeffnet: die Fassadenmaterialien der
 * EINEN Chunk-Komponente, die das Haus traegt, bekommen eine Masked-Variante
 * (Tools/create_facade_cut_materials.py), die einen orientierten Kasten um die
 * Erdgeschossfront verwirft. Dahinter steht der Laden aus eigenen Blender-
 * Assets (Tools/Blender/build_denno_shop.py, build_denno_figure.py). Die
 * uebrige Stadt behaelt ihre opaken Fassaden.
 *
 * Lage: die Front zeigt nach Westen auf den Sedanplatz. Der Actor misst die
 * gebackene Wand per Strahl und setzt den Laden genau davor/dahinter.
 * Lokales System der Laden-Meshes: X nach Norden entlang der Front, +Y ins
 * Haus, -Y zur Strasse, Z ab Ladenboden.
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenDennoShop : public AActor
{
	GENERATED_BODY()

public:
	AWiesbadenDennoShop();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// -- Masse ---------------------------------------------------------------
	// Einzige Quelle ist Tools/denno_shop.json (liest auch build_denno_shop.py);
	// der Test WiesbadenReal.World.DennoShop.SharedDims prueft diese Konstanten
	// dagegen. Hier stehen sie nur, weil C++ sie zur Laufzeit ohne Dateizugriff
	// braucht.
	/** Halbe Breite des Ausschnitts = halbe Ladenbreite + Front-Ueberstand. */
	static constexpr double ShopHalfWidthCm = 705.0;
	/** Oberkante des Ausschnitts: knapp unter der Oberkante des Schildbands. */
	static constexpr double CutTopCm = 338.0;
	static constexpr double CutBottomCm = -10.0;
	/** Quer zur Wand: die gebackene Fassade ist eine Flaeche; +-60 cm fangen
	 *  kleine Abweichungen zwischen OSM-Linie und gebackener Wand. */
	static constexpr double CutHalfDepthCm = 60.0;
	/** Denno im Cafe, Blick zur Strasse (ihr Mesh blickt nach lokal +Y). */
	static constexpr double DennoXCm = 300.0;
	static constexpr double DennoYCm = 340.0;
	static constexpr float DennoYawDeg = 180.0f;
	/** So lange wird auf das gestreamte Haus gewartet, dann aufgegeben. */
	static constexpr double WallWaitSeconds = 30.0;
	/** Ein Wandtreffer zaehlt nur so nah an der OSM-Frontlinie (cm). */
	static constexpr double WallToleranceCm = 150.0;

	/** Entscheidung nach der Wandsuche (datenrein, Test). */
	static EDennoShopBuild DecideBuild(bool bWallFound, double WaitedSeconds);
	/** Liegt ein Treffer quer zur Front nah genug an der OSM-Linie? Ein
	 *  Schildmast oder Baum davor ist KEINE Hauswand. */
	static bool IsPlausibleWall(const FVector& Hit, const FVector& OsmFrontMid, const FVector& Outward);

	/** Halbmasse des Ausschnitts (cm): laengs der Front, quer zur Wand, Hoehe. */
	static FVector CutHalfExtentCm();
	/** Liegt ein Weltpunkt im orientierten Ausschnitt? (datenrein, Test) */
	static bool IsInsideCut(const FVector& Point, const FVector& Centre,
		const FVector2D& AxisU, const FVector& HalfExtent);

	// -- Denno lebt: Atmen, Gewicht verlagern, umschauen -------------------
	// Die Figur hat kein Skelett (Tripo-Scan, Tools/Blender/build_denno_figure.py);
	// eine Animation als Asset kaeme mit Rig und Clips auf Megabytes. Stattdessen
	// bewegt der Actor die EINE Mesh-Komponente um ihren Ursprung an den Fuessen -
	// die Fuesse bleiben stehen, Kopf und Schultern bewegen sich um 1-2 cm.
	static constexpr double BreathPeriodSeconds = 4.2;
	/** Brustkorb: Breite/Tiefe weiten sich um 1,2 %, die Figur hebt sich um 0,4 %. */
	static constexpr double BreathWidth = 0.012;
	static constexpr double BreathRise = 0.004;
	static constexpr double SwayRollDeg = 0.5;
	static constexpr double SwayPitchDeg = 0.3;
	static constexpr double LookAroundDeg = 3.5;

	/** Pose zur Spielzeit `Seconds` (datenrein, stetig, beschraenkt; Test). */
	static FDennoIdlePose ComputeDennoIdle(double Seconds);

	// -- Lieferauftraege --------------------------------------------------------
	// Zu Fuss vor dem Laden F druecken: Denno gibt eine Lieferung an eine
	// zufaellige echte Adresse mit (WiesbadenDennoDelivery), bezahlt nach
	// Entfernung. Anzeige/Frist/Auszahlung macht UWiesbadenMissionSubsystem.
	/** So weit vor der Front (auf die Strasse hinaus) gilt man als "am Laden" (cm). */
	static constexpr double DeliveryReachCm = 700.0;
	/** Steht der Spieler (Laden-lokal: X laengs der Front, Y < 0 Strasse) vor dem Laden? */
	static bool IsInDeliveryReach(const FVector& PlayerLocalCm);
	/** Taste und Text der Annahme; laeuft schon ein Auftrag, sagt Denno das. */
	static FString BuildDeliveryPrompt(bool bMissionActive);

	bool IsPlayerInDeliveryReach(const FVector& PlayerWorldCm) const;
	/**
	 * F vor dem Laden. true = der Laden hat die Taste beansprucht (Auftrag
	 * angenommen ODER Denno sagt, dass schon einer laeuft) - dann kein
	 * Fahrzeugwechsel. OutMessage ist der Hinweis fuer das HUD.
	 */
	bool TryAcceptDelivery(const APawn* Player, FString& OutMessage);
	/** Entwicklerpfad (WbDennoAuftrag): ohne Reichweite, fester Zufallswert; vor
	 *  dem Aufbau vorgemerkt und danach eingeloest. */
	void RequestDevDelivery(int32 Seed);

	bool IsBuilt() const { return bBuilt; }

private:
	bool TryBuild();
	/**
	 * Ist eine Stadtzelle an der Ladenstelle geladen? Nur dann zaehlt die
	 * Wartezeit auf die Wand. Vorher gab der Laden 30 s nach SPIELSTART auf -
	 * wer am Garagenhof startete, hatte die ganze Sitzung keinen Laden, weil
	 * das Haus da noch gar nicht gestreamt war.
	 */
	bool IsShopCellLoaded(const FVector& FrontMid) const;
	/** Chunk-Fassaden um den Laden auf die Ausschnitt-Varianten umstellen. */
	int32 PatchFacades();
	void PatchComponent(UMeshComponent* Mesh);
	FVector WorldXY(const FVector2D& EastNorthM) const;
	UStaticMeshComponent* AddPart(const TCHAR* Name, const TCHAR* MeshPath,
		const FVector& LocalCm, float LocalYaw);
	/** Auftrag auswuerfeln und starten; false + Grund in OutMessage, wenn nicht. */
	bool StartDelivery(FRandomStream& Random, FString& OutMessage);
	void OnMissionCompleted(const FMission& Completed);
	void ShowHint(const FString& Text) const;
	/** Was tatsaechlich gutgeschrieben wird (Kurierlizenz +50 %, wie das Missionssystem). */
	int32 AwardFor(int32 BaseReward) const;

	/** Belieferbare Adressen der Stadt - einmal beim ersten Auftrag gesammelt. */
	TArray<FDennoDeliveryAddress> DeliveryAddresses;
	int32 DeliveryNumber = 0;
	int32 PendingDevSeed = 0;
	bool bPendingDevDelivery = false;
	FDelegateHandle MissionCompletedHandle;
	/** Der wartende Kunde der laufenden Lieferung. */
	TWeakObjectPtr<class AWiesbadenDeliveryCustomer> Customer;

	UPROPERTY(Transient) USceneComponent* Root = nullptr;
	UPROPERTY(Transient) UGeoCoordinateConverter* Converter = nullptr;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> Parts;
	UPROPERTY(Transient) TArray<UPointLightComponent*> Lights;
	/** Denno selbst - die einzige Komponente, die sich jedes Bild bewegt. */
	UPROPERTY(Transient) UStaticMeshComponent* DennoFigure = nullptr;
	/** Bereits umgestellte Chunk-Komponenten (Streaming kann sie ersetzen). */
	TSet<TWeakObjectPtr<UMeshComponent>> PatchedFacades;

	bool bBuilt = false;
	double FirstAttemptSeconds = -1.0;
	double NextPatchSeconds = 0.0;
	FVector CutCentre = FVector::ZeroVector;
	FVector2D CutAxisU = FVector2D(1.0, 0.0);
};
