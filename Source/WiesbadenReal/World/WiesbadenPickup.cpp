// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenPickup.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"

AWiesbadenPickup::AWiesbadenPickup()
{
	// Tick nur fuer die langsame Drehung des Mesh - guenstig, aber vorhanden.
	PrimaryActorTick.bCanEverTick = true;

	// Sphere-Trigger ist die Wurzel und zugleich das Aufnahme-Volumen.
	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->InitSphereRadius(80.0f);
	Trigger->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	SetRootComponent(Trigger);

	// Sichtbares Mesh haengt am Trigger; keine eigene Kollision - das Ueberlappen
	// uebernimmt allein der Trigger, damit das Pickup nicht physisch blockiert.
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Trigger);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AWiesbadenPickup::BeginPlay()
{
	Super::BeginPlay();

	// Overlap erst zur Laufzeit binden - AddDynamic braucht die fertige Instanz.
	if (Trigger)
	{
		Trigger->OnComponentBeginOverlap.AddDynamic(this, &AWiesbadenPickup::HandleTriggerBeginOverlap);
	}
}

void AWiesbadenPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (Mesh && SpinDegPerSec != 0.0f)
	{
		Mesh->AddLocalRotation(FRotator(0.0f, SpinDegPerSec * DeltaSeconds, 0.0f));
	}
}

void AWiesbadenPickup::HandleTriggerBeginOverlap(UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	if (!OtherActor || OtherActor == this)
	{
		return;
	}

	// Optional: nur Pawns duerfen aufnehmen - haelt Projektile/Deko fern.
	if (bOnlyPawnsCollect && !OtherActor->IsA<APawn>())
	{
		return;
	}

	Collect(OtherActor);
}

void AWiesbadenPickup::Collect(AActor* Collector)
{
	// Einmalig: mehrere Overlaps im selben Frame duerfen nicht doppelt feuern.
	if (bCollected)
	{
		return;
	}
	bCollected = true;

	OnCollected.Broadcast(this, Collector);

	if (bDestroyOnCollect)
	{
		Destroy();
	}
}
