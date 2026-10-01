// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Vehicles/WiesbadenCar.h"
#include "WiesbadenPoliceCar.generated.h"
class UPointLightComponent;
class UAudioComponent;
class USoundWaveProcedural;
class UTextRenderComponent;
UCLASS()
class WIESBADENREAL_API AWiesbadenPoliceCar : public AWiesbadenCar
{
	GENERATED_BODY()
public:
	AWiesbadenPoliceCar();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual float TakeDamage(float Damage, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer) override;
private:
	UPROPERTY() TObjectPtr<UPointLightComponent> BlueLeft;
	UPROPERTY() TObjectPtr<UPointLightComponent> BlueRight;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> LightBar;
	UPROPERTY() TObjectPtr<UTextRenderComponent> PoliceLabel;
	UPROPERTY() TObjectPtr<UAudioComponent> Siren;
	UPROPERTY() TObjectPtr<USoundWaveProcedural> SirenWave;
	double SirenTime = 0.0;
	double SirenPhase = 0.0;
	float FlashTime = 0.0f;
	float CrimeCooldown = 0.0f;
};
