// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "World/SebboHqShape.h"

#include "WiesbadenSebboHq.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UGeoCoordinateConverter;

/**
 * Hauptsitz der Sebbo International Mega Company an der Galileistrasse.
 *
 * STANDORT: die freie Flaeche westlich der Galileistrasse 35, an der
 * Einmuendung zur Platter Strasse (K651). Die Hausnummer 39 selbst gibt es im
 * OSM-Bestand NICHT - die Nummern der Strasse enden bei 35. Nach der
 * Nummernfolge (sie waechst nach Westen) laege 39 genau hier; die Flaeche ist
 * im Datenbestand unbebaut, es wird also kein vorhandenes Haus verdraengt.
 * Entschieden am 2026-09-20 anhand der Standortkarte
 * (Saved/Diagnose/galilei_kandidaten.png, Kandidat A).
 *
 * BAUWEISE wie die Wahrzeichen und die Nerobergbahn: Engine-Primitive als
 * StaticMeshComponents, zur Laufzeit gesetzt, Hoehe per Bodentrace. Der Actor
 * wartet, bis seine World-Partition-Zelle gestreamt ist - KEIN Re-Bake noetig.
 *
 * Die Form selbst steht datenrein in SebboHqShape; dieser Actor setzt sie nur
 * an den Ort. Die spaeteren Stufen (Treppenhaus, Halle, Tiefgarage, Tueren,
 * Licht, Fahrstuehle) haengen sich an die Formbeschreibung, damit der Actor
 * nicht zum Sammelbecken wird.
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenSebboHq : public AActor
{
	GENERATED_BODY()

public:
	AWiesbadenSebboHq();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Breitengrad des Grundstuecks (Kandidat A der Standortkarte). */
	UPROPERTY(EditAnywhere, Category = "Sebbo HQ")
	double PlotLatitude = 50.093950;

	/** Laengengrad des Grundstuecks. */
	UPROPERTY(EditAnywhere, Category = "Sebbo HQ")
	double PlotLongitude = 8.224490;

	/**
	 * Drehung des Turms (Grad).
	 *
	 * Die Galileistrasse laeuft hier etwa nach Westsuedwest; mit dieser Drehung
	 * steht die Schmalseite zur Platter Strasse und die Halle zeigt zur
	 * Galileistrasse.
	 */
	UPROPERTY(EditAnywhere, Category = "Sebbo HQ")
	double HeadingDegrees = 250.0;

	UPROPERTY(EditAnywhere, Category = "Sebbo HQ")
	FSebboHqDimensions Dimensions;

	/**
	 * Steigt die Treppe mit der Kapsel der Spielfigur ab - gegen die ECHTE
	 * Kollision des gebauten Turms, nicht gegen die Formbeschreibung.
	 *
	 * WOZU: Ein Test auf den Bauteilen kann nur sagen, ob die Zahlen stimmen.
	 * Ob ein Mensch hinaufkommt, entscheidet die Kollision im laufenden Spiel -
	 * und die erste Fassung der Treppe war rechnerisch tadellos (25-cm-Stufen,
	 * lueckenlos) und trotzdem unbegehbar, weil das Podest des naechsten
	 * Geschosses als Decke darueber lag.
	 *
	 * Die Sonde benutzt dieselbe Kapsel (Radius 40, Halbhoehe 90) und dieselbe
	 * Schrittregel (anheben, vorwaerts, absetzen; hoechstens 40 cm) wie
	 * AWiesbadenFootPawn. Eine eigene, aehnliche Regel waere kein Nachweis.
	 *
	 * Startschalter: -WbTreppenProbe. Ergebnis im Log und in
	 * Saved/Diagnose/treppenprobe.json.
	 */
	void ProbeStaircase() const;

private:
	/** Bodenhoehe am Standort; false, solange die Zelle nicht gestreamt ist. */
	bool ResolveGround(const FVector& WorldXY, double& OutZ) const;

	void Build(const FVector& BaseWorld, const FRotator& BaseYaw);

	UPROPERTY(Transient)
	USceneComponent* Root = nullptr;

	UPROPERTY(Transient)
	TArray<UStaticMeshComponent*> Parts;

	UPROPERTY(Transient)
	UStaticMesh* CubeMesh = nullptr;

	UPROPERTY(Transient)
	UStaticMesh* CylinderMesh = nullptr;

	UPROPERTY(Transient)
	TArray<UMaterialInterface*> Materials;

	UPROPERTY(Transient)
	const UGeoCoordinateConverter* Converter = nullptr;

	/**
	 * Fusspunkt des Turms in der Welt.
	 *
	 * NICHT GetActorLocation(): der Actor wird im Ursprung gespawnt und nie
	 * bewegt, gesetzt werden nur seine Komponenten. Die Treppenprobe fragte
	 * zuerst den Actor und sondierte darum bei (0,0,0) ins Leere - sie
	 * meldete "nichts unter den Fuessen" und das sah aus wie eine kaputte
	 * Treppe.
	 */
	FVector BuiltBase = FVector::ZeroVector;

	bool bBuilt = false;

	/**
	 * Sekunden seit dem Bau; -1 = keine Treppenprobe angefordert.
	 *
	 * Die Sonde muss warten. Im Bild des Bauens sind die Komponenten zwar
	 * registriert, ihre Kollisionskoerper stehen aber noch nicht in der
	 * Physikszene: die erste Fassung scheiterte am ERSTEN Schritt und meldete
	 * "nichts unter den Fuessen" - sie fiel durch einen Turm, den es
	 * physikalisch noch nicht gab. Zwei Sekunden sind reichlich.
	 */
	float SecondsSinceBuild = -1.0f;

	/** Treppenprobe schon gelaufen? Sie gilt genau einmal. */
	bool bProbed = false;
};
