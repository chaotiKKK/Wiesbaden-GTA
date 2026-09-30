// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Missions/WiesbadenParcours.h"

#include "WiesbadenParcoursActor.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

/**
 * Geschicklichkeitsparcours in der Welt (WbParcours): stellt die Kegel vor dem
 * Wagen auf, tastet dessen Pose ab und gibt sie an FWbParcoursBewertung.
 *
 * Die Kegel haben KEINE Kollision - der kinematische Wagen wuerde an ihnen
 * haengen bleiben; ob er einen beruehrt, entscheidet die Wertung aus dem
 * Grundriss, und der Kegel kippt dann um. Die Strecke laeuft in Blickrichtung
 * des Wagens; sie braucht ~250 m x 30 m freie, ebene Flaeche (Wiese:
 * -WbGoto=-180086,899031).
 *
 * Mit Fahrer (WbParcours 1) faehrt FWbParcoursFahrer die Runde ueber die
 * normale Steuernaht - der Nachweis ohne Tastatur.
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenParcours : public AActor
{
	GENERATED_BODY()

public:
	AWiesbadenParcours();

	/** Fuer diesen Wagen aufbauen, sobald er ruhig steht (nach -WbGoto).
	 *  FahrerModus: 0 = selbst fahren, 1 = Fahrer sauber, 2 = Fahrer mit Fehlern. */
	void Starten(APawn* Fahrzeug, int32 FahrerModus);

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Der zuletzt gestartete Parcours dieser Welt (fuer HUD und Neustart). */
	static AWiesbadenParcours* Aktiver(const UWorld* World);

	/** HUD: soll das Panel stehen, und was steht darin? */
	bool HudSichtbar() const;
	FString HudTitel() const;
	FString HudZeile() const;
	FString HudHinweis() const;

	const FWbParcoursBewertung& GetBewertung() const { return Bewertung; }

private:
	void Aufbauen();
	FVector2D InsParcours(const FVector& Welt) const;
	FVector InDieWelt(const FVector2D& Lokal) const;
	bool BodenZ(const FVector2D& Lokal, float& OutZ) const;
	void KegelUmwerfen(int32 Index);
	void Melden(const FString& Text);
	void AmZiel();

	UPROPERTY()
	TObjectPtr<UStaticMesh> KegelMesh;

	/** M_WbLeitkegel: Grundfarbe und Eigenleuchten aus "Farbe" (x "Glow"). */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> KegelLeuchtMaterial;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Kegel;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> KegelMaterial;

	TWeakObjectPtr<APawn> Wagen;
	bool bMitFahrer = false;
	bool bFahrerFehler = false;
	/** Der Fahrer hat die externe Steuerung gesetzt und noch nicht geloest -
	 *  nur dann loest der Parcours sie (einmal), nie eine fremde (WbDrive). */
	bool bSteuertWagen = false;
	/** Pose des letzten Ticks: ein Sprung > 50 m in einem Bild ist ein
	 *  Versetzen (-WbGoto, WbTeleport), kein Wegfahren. */
	FVector LetztePos = FVector::ZeroVector;
	bool bHatLetztePos = false;
	bool bAufgebaut = false;

	// Aufbau erst, wenn der Wagen 1 s ruhig steht - ein -WbGoto versetzt ihn
	// nach dem Start; springt er vor dem Start weiter weg, wird neu aufgebaut.
	FVector RuhePos = FVector::ZeroVector;
	float RuheSekunden = 0.0f;

	FVector2D Ursprung = FVector2D::ZeroVector;   // Startlinie (Welt-XY, cm)
	float KursGrad = 0.0f;                        // Streckenrichtung (Welt-Yaw)
	float BodenBasisZ = 0.0f;

	FWbParcoursBewertung Bewertung;
	FWbParcoursFahrer Fahrer;
	float TaktSekunden = 0.0f;
	float NachZielSekunden = -1.0f;
	FString LetzteMeldung;
	float LetzteMeldungAlter = 99.0f;

	static TWeakObjectPtr<AWiesbadenParcours> Zuletzt;
};
