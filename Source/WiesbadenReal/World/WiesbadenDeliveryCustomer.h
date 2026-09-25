// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Missions/WiesbadenDennoDelivery.h"
#include "WiesbadenDeliveryCustomer.generated.h"

class UStaticMesh;
class UStaticMeshComponent;

/**
 * Der Kunde einer Denno-Lieferung: wartet an der Zieladresse vor dem Haus,
 * bedankt sich bei der Abgabe und gibt Trinkgeld nach Puenktlichkeit
 * (WiesbadenDennoDelivery::ComputeTip).
 *
 * Die Kundin ist Iris (/Game/Assets/People/Iris, Tools/Blender/
 * build_customer_iris.py): eine Tripo-Figur ohne Skelett, in Blender zu drei
 * Posen gebogen - stehend und zwei Schrittstellungen -, die sich auf das
 * Vier-Phasen-Schema der Fussgaenger legen (IrisPosePath). Fehlt Iris, steht
 * die Fussgaenger-Figur (SM_WbPed2_*) mit eigener Kleidung da - als EINE
 * Instanz, weil deren Material die Kleidungsfarben aus Per-Instanz-Daten liest.
 *
 * Er erscheint erst, wenn der Spieler auf CustomerAppearCm heran ist (vorher
 * ist sein Boden womoeglich nicht gestreamt), schaut zur Strasse und dreht sich
 * zum Spieler, wenn der naeher kommt. Nach Dank und Trinkgeld geht er zur
 * Haustuer zurueck - mit dem Gang der Fussgaenger (dieselben vier Posen,
 * gewechselt nach gegangener Strecke, 1,35 m/s) - und verschwindet dort im
 * Haus. Endet der Auftrag ohne Abgabe (Frist verpasst), geht er still.
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenDeliveryCustomer : public AActor
{
	GENERATED_BODY()

public:
	AWiesbadenDeliveryCustomer();
	virtual void Tick(float DeltaSeconds) override;

	/** Vom Laden direkt nach dem Spawnen: zu welchem Auftrag er gehoert und wo er wartet. */
	void Setup(FName InMissionId, const FVector& InDropPoint, const FVector& InAddressLocation, int32 InSeed);

	/**
	 * Abgabe: Trinkgeld nach der zuletzt gemessenen Restzeit gutschreiben, den
	 * Dank zurueckgeben (fuer den HUD-Hinweis), kurz stehen bleiben und dann
	 * zur Haustuer zurueckgehen.
	 * Liefert das Trinkgeld (Betrag, Dank, flott) fuer Hinweis und Kurier-Bilanz.
	 */
	FDennoTip ThankAndTip(int32 Payout, double DeadlineSeconds);

	/** So nah dreht er sich zum Spieler (cm). */
	static constexpr double FacePlayerCm = 2500.0;

	bool IsStanding() const { return bPlaced; }

	/** Iris-Mesh je Gangphase 0..3 (Schritt, stehend, Schritt gespiegelt, stehend). */
	static const TCHAR* IrisPosePath(int32 Pose);

private:
	bool TryPlace(const FVector& PlayerLocation);
	/** Colors = Kleidungsfarben der Fussgaenger-Figur (Instanz-Daten); nullptr = Iris
	 *  mit eigenen Texturen als gewoehnliche Mesh-Komponente. */
	void CreateFigure(const TArray<float>* Colors);
	/**
	 * Gangphase zeigen: EINE Komponente tauscht ihr Mesh. Vier abwechselnd
	 * sichtbare Komponenten verwischten beim Gehen - eine eingeblendete hatte
	 * fuer die Bewegungsunschaerfe noch die Lage von ihrem letzten sichtbaren
	 * Bild, Schritte zurueck.
	 */
	void ShowPose(int32 Pose);
	bool HasFigure() const { return Figure != nullptr; }
	/** Zum Spieler drehen, wenn er nah ist, sonst zur Strasse. */
	void FacePlayerOrStreet(const FVector& PlayerLocation, float DeltaSeconds);
	/** Haustuer per Wandstrahl bestimmen, Heimweg starten. */
	void BeginWalkHome();
	void TickWalkHome(float DeltaSeconds);

	UPROPERTY(Transient) USceneComponent* Root = nullptr;
	UPROPERTY(Transient) UStaticMeshComponent* Figure = nullptr;
	/** Index = Gangphase 0..3; fehlende Posen bleiben leer. */
	UPROPERTY(Transient) TArray<UStaticMesh*> PoseMeshes;

	FName MissionId;
	FVector DropPoint = FVector::ZeroVector;
	FVector AddressLocation = FVector::ZeroVector;
	int32 Seed = 0;
	bool bPlaced = false;
	bool bThanked = false;
	/** Zuletzt gelesene Restzeit des Auftrags (s) - bei der Abgabe ist er schon beendet. */
	double LastRemainingSeconds = -1.0;
	double StandYawDeg = 0.0;
	/** Heimweg: sichtbare Pose, Dankzeitpunkt, Tuer, gegangene Strecke. */
	int32 ShownPose = 1;
	double ThankedAtSeconds = 0.0;
	FVector DoorPoint = FVector::ZeroVector;
	double WalkedCm = 0.0;
	double WalkDistanceCm = 0.0;
};
