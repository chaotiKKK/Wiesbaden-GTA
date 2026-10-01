// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "WiesbadenBugTankPawn.generated.h"

class USphereComponent;
class USceneComponent;
class USpringArmComponent;
class UCameraComponent;
class UPrimitiveComponent;
class UStaticMesh;
class UStaticMeshComponent;
struct FBugTankTeil;

/**
 * Spielbarer, stilisierter Käferpanzer. Die Darstellung sind die 15 statischen
 * Teile des Blender-Assets (`SM_Insekt_<Teil>`, Import über
 * Tools/import_bugtank_teile.py); die Beine und Fühler werden prozedural um
 * ihre Gelenkpunkte gedreht, aus derselben Bewegung, die schon immer den
 * Pawn steuerte.
 *
 * Warum Teile statt Skelett: der Unreal-5.8-Import setzt die Knochenorientierung
 * eines Blender-Rigs um und übernimmt nur die Knochenlänge - FBX wie glTF, beide
 * Parser (Beleg: Saved/Logs/wb_test_bugtankrig5.log, Ruhepose 0/0/0.622 statt
 * 38/30/-39). Deshalb zerlegt Blender/bugtank/export_bugtank_teile.py das Mesh
 * in starre Teile mit denselben Gelenkpunkten; kein Bone-Posing nötig.
 *
 * Die Kollision bleibt bewusst die einfache Kugel (r = 56 cm, Snap 58 cm):
 * das Modell ragt mit Bein- und Fühlerspitzen 8 bis 20 cm über sie hinaus
 * (gemessen im Blender-Prüfbericht). Die Entscheidung dazu, statt die Kugel
 * zu verändern: der Pawn richtet seine lokale Hochachse an der getroffenen
 * Fläche aus und hält 58 cm Abstand - in dieser Stellung stehen die Füße
 * 3 cm ÜBER der Fläche (Sohle -55 cm), nicht darin. Ein dauerhaftes Einklappen
 * würde den Käfer also sichtbar über der Fläche schweben lassen. Eingeklappt
 * wird deshalb nur, wenn die vorhandene Oberflächenabfrage eine ANDERE Fläche
 * trifft als die Standfläche - der Käfer läuft dann in eine Wand oder an eine
 * Decke, wo die überstehenden Spitzen sonst hineinragen würden.
 *
 * WASD bewegt ihn; an Wand- und Deckenflächen richtet er seine lokale Hochachse
 * an der getroffenen Oberfläche aus.
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenBugTankPawn : public APawn
{
	GENERATED_BODY()

public:
	AWiesbadenBugTankPawn();
	virtual void Tick(float DeltaSeconds) override;
	bool HasSurfaceContact() const { return bHasSurfaceContact; }
	UPrimitiveComponent* GetSurfaceHitComponent() const { return SurfaceHitComponent.Get(); }
	float GetSurfaceHitDistance() const { return SurfaceHitDistance; }
	const FVector& GetSurfaceHitPoint() const { return SurfaceHitPoint; }

	/**
	 * Statisches Käferteil in der VisualRoot-Liste (Darstellung; Kollision
	 * bleibt die Kugel). nullptr, wenn das Teil nicht geladen wurde.
	 */
	UStaticMeshComponent* GetInsektTeil(const FBugTankTeil& Teil) const;

	/** Wie viele der 15 Teile liegen als Mesh vor (0 = Asset fehlt). */
	int32 GetGeladeneTeile() const;

	/** Body-Teil: nur zur Groessen-/Diagnoseabfrage im Test. */
	UStaticMeshComponent* GetInsektBody() const { return TeilKomponenten[0]; }

	/**
	 * Einklappgrad der Gliedmassen: 0 = ausgefahren, 1 = eingeklappt.
	 *
	 * Aus der VORHANDENEN Oberflächenabfrage abgeleitet, nie aus einer zweiten
	 * Tracesuche: bei naher Fläche klappen die Beine ein, damit ihre Spitzen
	 * innerhalb der Kugel (r = 56 cm) bleiben.
	 */
	float GetLegFold() const { return LegFold; }
	float GetAntennaFold() const { return AntennaFold; }

	/**
	 * Die letzte Oberflächenabfrage traf nicht die Standfläche, sondern eine
	 * Wand oder Decke - nur dann werden die Gliedmaßen eingeklappt.
	 */
	bool GetFremdeFlaeche() const { return bFremdeFlaeche; }

	/** Beinanimation fortschreiben (auch im Test ohne Controller aufrufbar). */
	void UpdatePose(float DeltaSeconds, float SpeedFraction);

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere, Category = "BugTank")
	USphereComponent* CollisionRoot = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "BugTank")
	USceneComponent* VisualRoot = nullptr;

	/**
	 * Die 15 statischen Kaeferteile, in der Reihenfolge von BugTankTeile
	 * (Body, je Bein Ober/Unter fuer L1..L3 und R1..R3, AntenneL, AntenneR).
	 *
	 * Meshes sind in Koerperkoordinaten exportiert. Jedes haengt mit
	 * -Gelenk an einem Pivot im Gelenk; Unterbein-Pivots erben das Oberbein.
	 */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> TeilKomponenten;

	UPROPERTY()
	TArray<TObjectPtr<USceneComponent>> TeilDrehpunkte;

	/**
	 * Harte Referenzen auf die Mesh-Assets.
	 *
	 * Ohne sie findet der Cooker die Meshes nicht: ConstructorHelpers laedt zur
	 * Laufzeit, aber der Cooker folgt der Referenz des CDO.
	 */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMesh>> TeilAssets;

	UPROPERTY(VisibleAnywhere, Category = "BugTank")
	USpringArmComponent* CameraBoom = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "BugTank")
	UCameraComponent* Camera = nullptr;

	// -- Prozedurales Drehen der Teile ------------------------------------
	// Naht zwischen Asset und Code ist jetzt die Tabelle BugTankTeile
	// (automatisch erzeugt von Blender/bugtank/export_bugtank_teile.py): sie
	// liefert Gelenk (Drehpunkt) und Spitze (Messpunkt) je Teil.
	struct FGliedmasseKette
	{
		/** Indizes in TeilKomponenten: Ober- und Unterteil des Beins. */
		int32 Ober = INDEX_NONE;
		int32 Unter = INDEX_NONE;
		int32 SpitzeIndex = INDEX_NONE;   // Teil, dessen Spitze der Messpunkt ist
		FVector RefKnie = FVector::ZeroVector;   // Gelenk des Unterteils (cm)
		FVector RefFuss = FVector::ZeroVector;   // Fussspitze in Ruhelage
		FVector Ziel = FVector::ZeroVector;      // Ziel beim ganz eingeklappten Bein
		FVector Faltaehse = FVector::ZeroVector; // Achse, um die das Bein einklappt
		float Faltwinkel = 0.0f;                 // Grad
	};

	FGliedmasseKette Beinketten[6];
	FGliedmasseKette Antennenketten[2];
	bool bTeileBereit = false;

	float LegFold = 0.0f;
	float AntennaFold = 0.0f;
	float AntennaPhase = 0.0f;
	float FoldHoldRemaining = 0.0f;

	/** Zielpunkt, auf den ein Bein beim Einklappen gezielt wird (lokal, cm). */
	static FVector EinklappZiel(const FVector& RefKnie);
	bool TeileVorbereiten();
	/** Dreht einen Teil-Drehpunkt auf die Weltlage (lokal, relativ zu VisualRoot). */
	void TeilDrehen(int32 Index, const FQuat& Drehung);
	void UpdateAntennae(float DeltaSeconds, float SpeedFraction);

	FVector SurfaceUp = FVector::UpVector;
	FVector MoveVelocity = FVector::ZeroVector;
	float WalkSpeedCmS = 520.0f;
	float TurnRateDegS = 115.0f;
	float LegPhase = 0.0f;
	float GravityCmS2 = 981.0f;
	bool bHasSurfaceContact = false;
	/** true, wenn der Treffer auf eine andere Fläche zeigt als die Standfläche. */
	bool bFremdeFlaeche = false;
	TWeakObjectPtr<UPrimitiveComponent> SurfaceHitComponent;
	float SurfaceHitDistance = 0.0f;
	FVector SurfaceHitPoint = FVector::ZeroVector;

	bool FindSurface(float ForwardInput, FVector& OutPoint, FVector& OutNormal,
		UPrimitiveComponent*& OutComponent, float& OutDistance) const;
	void UpdateLegs(float DeltaSeconds, float SpeedFraction);
};
