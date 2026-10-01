// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Vehicles/WiesbadenVehicleControl.h"
#include "WiesbadenPoliceSubsystem.generated.h"
class AWiesbadenPoliceCar;
class AWiesbadenWorldBuilder;
class APawn;
class AWiesbadenHelicopter;
class AWiesbadenPoliceHelicopter;
class AAIController;
struct FRoadNetwork;

namespace WiesbadenPolice
{
	WIESBADENREAL_API int32 PatrolCount(int32 Level);
	WIESBADENREAL_API FWiesbadenCarControl DriveControl(const FVector& Car, const FVector& Forward,
		const FVector& Target, float SpeedKmh, float LimitKmh);

	/**
	 * Einheiten-Soll je Fahndungsstufe (SPEC §8: Polizei -> SEK -> Heli ->
	 * BFE+ -> GSG9/Erbenheim). Ab Stufe 4 raeckt das SEK aus, ab Stufe 5 kommt
	 * der Luft-Verfolger dazu; die Festnahme geht mit den Spezialkraeften
	 * schneller. Reine Zuordnung - headless testbar.
	 */
	struct FWiesbadenPoliceForce
	{
		int32 Streifen = 0;
		int32 Sek = 0;
		int32 Heli = 0;
		float ArrestSecondsNeeded = 5.0f;
	};
	WIESBADENREAL_API FWiesbadenPoliceForce EscalationFor(int32 Level);

	/**
	 * Sichtpruefung Observer -> Ziel (Reichweite cm). Ein Strahl auf Brusthoehe,
	 * der eigene Rumpf wird ignoriert; getroffenes Ziel oder freie Strecke gilt
	 * als gesehen. Dient der Sicht-Verfolgung der Streifen UND des Helis.
	 */
	WIESBADENREAL_API bool CanSee(class UWorld* World, class AActor* Observer,
		class AActor* Target, float RangeCm);

	/**
	 * Nahziel der Verfolgerfahrt: Spieler nur bei Sicht UND in Reichweite UND
	 * auf gleicher Hoehe (sonst wuerde die Streife durch Haeuser heizen oder
	 * sich an Hauswaenden festfahren); sonst bleibt das Routen-Ziel.
	 */
	WIESBADENREAL_API FVector PursuitTarget(const FVector& CarLocation, const FVector& RouteTarget,
		const FVector& PlayerLocation, bool bPlayerSeen,
		float MaxDirectCm = 1000.0f, float MaxHeightDeltaCm = 150.0f);
}
UCLASS()
class WIESBADENREAL_API UWiesbadenPoliceSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual void Tick(float DeltaSeconds) override;
	virtual void Deinitialize() override;
	virtual TStatId GetStatId() const override;
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Polizei") bool IsPlayerSeen() const { return bPlayerSeen; }
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Polizei") float GetArrestProgress() const { return ArrestNeededSeconds > 0 ? ArrestSeconds / ArrestNeededSeconds : 0.0f; }
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Polizei") bool WasRecentlyArrested() const { return ArrestNoticeSeconds > 0; }
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Polizei") int32 GetPatrolCount() const { return Patrols.Num(); }
private:
	struct FPatrol
	{
		TWeakObjectPtr<AWiesbadenPoliceCar> Car;
		int32 LaneId = INDEX_NONE;
		TArray<FVector> Route;
		int32 Waypoint = 0;
		bool bSek = false;
	};
	void PrepareNetwork(AWiesbadenWorldBuilder* Builder);
	bool SpawnPatrol(const FVector& PlayerLocation, const FVector& ViewForward, bool bSek = false);
	bool HasSight(AActor* Observer, APawn* Player) const;
	TArray<FPatrol> Patrols;
	TWeakObjectPtr<AWiesbadenWorldBuilder> NetworkOwner;
	TMap<int32, TArray<int32>> Successors;
	TMap<FIntPoint, TArray<int32>> SpawnCells;
	int32 IndexedLanes = INDEX_NONE;
	float SpawnCooldown = 0;
	float ArrestSeconds = 0;
	float ArrestNeededSeconds = 5.0f;
	float ArrestNoticeSeconds = 0;
	float RamCooldown = 0;
	bool bPlayerSeen = false;
	/** Luft-Verfolger (Eskalation ab Stufe 5); null, solange keiner gebraucht wird. */
	TWeakObjectPtr<AWiesbadenPoliceHelicopter> PoliceHeli;
};
