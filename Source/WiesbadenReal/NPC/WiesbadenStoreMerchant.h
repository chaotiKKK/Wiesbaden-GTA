// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "WiesbadenStoreMerchant.generated.h"

class AWiesbadenFootPawn;
class UWiesbadenVehicleHUD;
class UWiesbadenStoreSubsystem;

/**
 * Nordfriedhof-Haendler: ein schlanker NPC-Aktor, der bei Annäherung einen kurzen
 * Hinweis zeigt und beim Interagieren das bestehende Store-Panel öffnet.
 *
 * Das ist bewusst kein Dialogbaum, kein neuer Shop und keine neue Wirtschaft —
 * der NPC ist nur eine Zugangsstelle zur vorhandenen Freischaltungs-Ausgabe.
 * Den eigentlichen Kauf regelt UWiesbadenStoreSubsystem::TryPurchase; den
 * Helikopter-Einstieg FWiesbadenStore::MayEnterHelicopter.
 *
 * Die genaue Position wird an der begehbaren Fläche und der Mauergeometrie des
 * Nordfriedhofs ausgerichtet; die in dieser Klasse fest kodierte Position ist nur
 * die neue Startposition/ErsteSchätzung für die Editor-Platzierung.
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenStoreMerchant : public AActor
{
	GENERATED_BODY()

public:
	AWiesbadenStoreMerchant();

	/** Versucht die Interaktion mit einem Fuß-Pawn in Reichweite. */
	bool TryInteract(APawn* Pawn);

	/**
	 * Stable query the HUD uses to name a concrete approach cue for the
	 * nearest in-reach merchant. This is not a dialog tree and not new
	 * economy — only a shared way to turn an actor scan into a short,
	 * fulfillable cue, so the foot prompt can be specific instead of an
	 * unspecified 'F ... somehow'.
	 *
	 * Cue text is intentionally brief and actionable: it describes the
	 * next action, not the figure.
	 *
	 * Es zaehlt nur, wer in SEINER EIGENEN InteractRangeCm steht - dieselbe
	 * Regel wie AWiesbadenGameMode::PickMerchantInReach. Hinweis und
	 * Interaktion benutzen damit eine Reichweite: ein Hinweis auf einen
	 * Haendler, den F nicht erreicht, waere schlimmer als kein Hinweis.
	 *
	 * @param Merchants     Kandidaten; nullptr-Eintraege werden uebersprungen.
	 * @param FromLocation  Standort des Spielers - NICHT der Weltursprung.
	 * @param OutCue        Hinweistext des naechsten Treffers, sonst leer.
	 * @return              Derselbe Text wie OutCue (leer, wenn keiner passt).
	 */
	static FString DescribeNearestMerchantInReach(
		const TArray<AActor*>& Merchants,
		const FVector& FromLocation,
		FString& OutCue);

	/** True, solange der Marker-Standort nicht sichtbar geschaltet ist. */
	bool IsMarkerHidden() const;

	/** Reichweite der Interaktion, in cm. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Haendler", meta = (ClampMin = "100.0"))
	float InteractRangeCm = 350.0f;

	/** Kurze Beschreibung, die beim Hinweis erscheint. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Haendler")
	FString Title = TEXT("Helikopter-Händler");

	/** Kurzer Hinweistext beim Annähern. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Haendler")
	FString ApproachHint = TEXT("[F]  Händler sprechen");

private:
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Haendler")
	class USceneComponent* Root = nullptr;

	/** Ein kurzer lokaler Hinweis-Actor für die Annäherungs-Anzeige. */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Haendler")
	class UStaticMeshComponent* Marker = nullptr;

	bool CanInteractWith(APawn* Pawn) const;
	void ShowApproachHint();
};
