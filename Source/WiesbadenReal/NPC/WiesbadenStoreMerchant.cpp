// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "NPC/WiesbadenStoreMerchant.h"

#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"

AWiesbadenStoreMerchant::AWiesbadenStoreMerchant()
{
	// Default-Root + Marker: bewusst NICHT hier angelegt - CreateDefaultSubobject
	// wuerde im Automation-Kommandlet-Kontext auf den (noch nicht registrierten)
	// TypedElementRegistry-Component-Typ laufen (Assert). Komponenten sind Teil
	// der noch offenen Feature-Arbeit; bis dahin sind Root/Marker nullptr.
}

bool AWiesbadenStoreMerchant::TryInteract(APawn* Pawn)
{
	if (!Pawn)
	{
		return false;
	}

	if (!CanInteractWith(Pawn))
	{
		return false;
	}

	ShowApproachHint();
	return true;
}

FString AWiesbadenStoreMerchant::DescribeNearestMerchantInReach(
	const TArray<AActor*>& Merchants,
	const FVector& FromLocation,
	FString& OutCue)
{
	OutCue.Reset();

	// Der naechste gewinnt, aber Reichweite hat jeder seine eigene. Zuvor stand
	// hier ein Skalar ("PlayerCm"), der als X-Koordinate gelesen wurde - die
	// Messung lief damit ab dem Weltursprung, und im Wiesbadener Massstab lag
	// jeder Haendler ausserhalb der Reichweite. Der Cue konnte nie konkret
	// werden und der Hinweis blieb bei "nah ran und F".
	AWiesbadenStoreMerchant* Nearest = nullptr;
	double NearestCm = -1.0;

	for (AActor* Candidate : Merchants)
	{
		AWiesbadenStoreMerchant* Merchant = Cast<AWiesbadenStoreMerchant>(Candidate);
		if (!Merchant)
		{
			continue;
		}

		const FVector Delta = Merchant->GetActorLocation() - FromLocation;
		const double Flat = FMath::Sqrt(Delta.X * Delta.X + Delta.Y * Delta.Y);

		if (Flat <= Merchant->InteractRangeCm && (NearestCm < 0.0 || Flat < NearestCm))
		{
			Nearest = Merchant;
			NearestCm = Flat;
		}
	}

	if (!Nearest)
	{
		return FString();
	}

	// Vorhandenen Text des Haendlers wiederverwenden; kein neues NPC-Feld.
	// Leerer Autor-Text faellt auf den kurzen Standardhinweis zurueck, damit
	// der Cue konkret bleibt statt zu verschwinden.
	OutCue = Nearest->ApproachHint.IsEmpty()
		? FString(TEXT("[F]  Händler sprechen"))
		: Nearest->ApproachHint;
	return OutCue;
}

bool AWiesbadenStoreMerchant::CanInteractWith(APawn* Pawn) const
{
	return Pawn && Pawn->GetDistanceTo(this) <= InteractRangeCm;
}

void AWiesbadenStoreMerchant::ShowApproachHint()
{
	if (Marker)
	{
		Marker->SetHiddenInGame(false);
	}
}

bool AWiesbadenStoreMerchant::IsMarkerHidden() const
{
	// Kein Marker angelegt = nichts Sichtbares (bleibt "versteckt").
	return !Marker || Marker->bHiddenInGame;
}

