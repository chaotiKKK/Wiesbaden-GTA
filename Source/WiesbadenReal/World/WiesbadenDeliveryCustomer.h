// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Missions/WiesbadenDennoDelivery.h"
#include "World/WiesbadenCustomerFigures.h"
#include "WiesbadenDeliveryCustomer.generated.h"

class UInstancedStaticMeshComponent;
class USkeletalMeshComponent;
class UStaticMesh;

/**
 * Der Kunde einer Denno-Lieferung: wartet an der Zieladresse vor dem Haus,
 * bedankt sich bei der Abgabe und gibt Trinkgeld nach Puenktlichkeit
 * (WiesbadenDennoDelivery::ComputeTip).
 *
 * Wer kommt, waehlt der Laden: eine der Kundenfiguren (WiesbadenCustomerFigures,
 * /Game/Assets/People/Kunden/<Name>, z. B. Iris und Mira), abwechselnd von
 * Lieferung zu Lieferung. Jede ist eine Tripo-Figur mit eigenem Skelett und
 * denselben Bewegungen - sie wartet mit A_<Name>_Idle (Atmen; Gewicht verlagern
 * und Umschauen wie Denno im Cafe per AWiesbadenDennoShop::ComputeDennoIdle als
 * Drehung der ganzen Figur, ohne Asset-Byte), dankt mit A_<Name>_Wave (winken
 * oder, bei starren Armen, nicken) und geht mit A_<Name>_Walk heim, dessen Takt
 * dem Gehtempo folgt.
 * Gibt es keine Kundenfigur, steht die Fussgaenger-Figur (SM_WbPed2_*) mit eigener Kleidung da
 * - als EINE Instanz, weil deren Material die Kleidungsfarben aus
 * Per-Instanz-Daten liest - und geht mit deren vier Schrittposen.
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

	/** Vom Laden direkt nach dem Spawnen: zu welchem Auftrag er gehoert, wo er
	 *  wartet und wer er ist (unvollstaendige Figur: Fussgaenger-Figur). */
	void Setup(FName InMissionId, const FVector& InDropPoint, const FVector& InAddressLocation, int32 InSeed,
		const FWbCustomerFigure& InLook);

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
	/** Name der Kundenfigur (leer: Fussgaenger-Figur). */
	const FString& GetFigureName() const { return Look.Name; }

private:
	bool TryPlace(const FVector& PlayerLocation);
	/** Die Kundenfigur mit Skelett aufstellen (wartet mit Idle); false, wenn sie unvollstaendig ist. */
	bool CreateSkeletal();
	/** Ersatz: die Fussgaenger-Figur als EINE Instanz, Kleidung in den Instanz-Daten. */
	void CreateFigure(const TArray<float>& Colors);
	/**
	 * Gangphase zeigen: EINE Komponente tauscht ihr Mesh. Vier abwechselnd
	 * sichtbare Komponenten verwischten beim Gehen - eine eingeblendete hatte
	 * fuer die Bewegungsunschaerfe noch die Lage von ihrem letzten sichtbaren
	 * Bild, Schritte zurueck.
	 */
	void ShowPose(int32 Pose);
	bool HasFigure() const { return Figure != nullptr || SkelFigure != nullptr; }
	/** Kundenfigur: Dennos Gewicht-verlagern-und-Umschauen auf die Figur legen; nach
	 *  dem Dank in CustomerIdleFadeSeconds ausblenden. */
	void ApplyIdleSway();
	/** Zum Spieler drehen, wenn er nah ist, sonst zur Strasse. */
	void FacePlayerOrStreet(const FVector& PlayerLocation, float DeltaSeconds);
	/** Haustuer per Wandstrahl bestimmen, Heimweg starten. */
	void BeginWalkHome();
	void TickWalkHome(float DeltaSeconds);

	UPROPERTY(Transient) USceneComponent* Root = nullptr;
	/** Die Kundenfigur - oder, wenn sie fehlt, die Fussgaenger-Figur (Figure + PoseMeshes). */
	UPROPERTY(Transient) FWbCustomerFigure Look;
	UPROPERTY(Transient) USkeletalMeshComponent* SkelFigure = nullptr;
	UPROPERTY(Transient) UInstancedStaticMeshComponent* Figure = nullptr;
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
	bool bSkelWalking = false;
	/** Eigener Zeitversatz je Kundin fuer ComputeDennoIdle (aus dem Seed). */
	double IdleTimeOffset = 0.0;
};
