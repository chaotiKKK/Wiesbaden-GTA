// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NPC/WiesbadenPursuer.h"
#include "WiesbadenPursuerActor.generated.h"

class UStaticMeshComponent;

/**
 * Einfacher reaktiver Verfolger-NPC. Die Reaktions-Logik (entdecken/verfolgen/
 * verlieren/einholen) liegt datenrein/unit-getestet in FWiesbadenPursuer; dieser
 * Actor ist nur die Welt-Anbindung: je Tick die Spielerposition holen, Step()
 * rufen, kinematisch nachziehen und Modus-Wechsel loggen. Sichtbarer Wuerfel,
 * keine Physik (bewegt sich ueber SetActorLocation).
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenPursuerActor : public AActor
{
	GENERATED_BODY()

public:
	AWiesbadenPursuerActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Verfolger")
	UStaticMeshComponent* Mesh = nullptr;

	FWiesbadenPursuerState State;
	FWiesbadenPursuerParams Params;
	EWiesbadenPursuerMode LastLoggedMode = EWiesbadenPursuerMode::Idle;
};
