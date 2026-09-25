// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Missions/WiesbadenMissionTypes.h"

struct FGeneratedBuilding;

/** Eine belieferbare Adresse: "Strasse Hausnummer" + Gebaeudeschwerpunkt (cm). */
struct FDennoDeliveryAddress
{
	FString Address;
	FVector Location = FVector::ZeroVector;
};

/** Trinkgeld des Kunden nach Puenktlichkeit + was er dazu sagt. */
struct FDennoTip
{
	int32 Amount = 0;
	FString Thanks;
	/** Mit mindestens der halben Frist uebrig abgegeben (hoechste Stufe). */
	bool bFast = false;
};

/** Ein fertig ausgewuerfelter Lieferauftrag. */
struct FDennoDeliveryJob
{
	FString Address;
	FString Cargo;
	/** Abgabepunkt auf der naechsten befahrbaren Spur vor der Adresse (cm). */
	FVector DropPoint = FVector::ZeroVector;
	/** Schwerpunkt des Gebaeudes - dorthin (zum Haus) steht der Kunde. */
	FVector AddressLocation = FVector::ZeroVector;
	/** Luftlinie Laden -> Abgabepunkt (cm) - Grundlage der Bezahlung. */
	double DistanceCm = 0.0;
	int32 Payout = 0;
};

/**
 * Lieferauftraege bei Denno: Ware im Laden aufnehmen, an eine zufaellige
 * echte Adresse der Stadt bringen, bezahlt nach Entfernung.
 *
 * Alles hier ist datenrein (kein Weltzugriff) und darum direkt testbar; der
 * Laden-Actor (AWiesbadenDennoShop) holt Gebaeude und Strassennetz aus der
 * Welt und reicht den fertigen Auftrag an UWiesbadenMissionSubsystem weiter.
 * Anzeige (Missions-Panel, Raute auf Minikarte und Stadtplan), Frist und
 * Auszahlung macht das vorhandene Missionssystem.
 */
namespace WiesbadenDennoDelivery
{
	/** Kein Auftrag um die Ecke und keiner quer durch den Taunus (Luftlinie, cm). */
	constexpr double MinDistanceCm = 30000.0;    // 300 m
	constexpr double MaxDistanceCm = 400000.0;   // 4 km

	/** Bezahlung: Grundbetrag + je Kilometer, auf 5 EUR gerundet. */
	constexpr int32 BasePayout = 40;
	constexpr int32 PayoutPerKm = 60;

	/** Abgaberadius am Ziel - gross genug, um mit dem Auto davor zu halten. */
	constexpr double DropRadiusCm = 1200.0;
	/** Aufnahme im Laden: der Spieler steht beim Annehmen schon dort. */
	constexpr double PickupRadiusCm = 1500.0;

	/** Alle Gebaeude mit Adresse, jede Adresse nur einmal (erste gewinnt). */
	TArray<FDennoDeliveryAddress> CollectAddresses(const TArray<FGeneratedBuilding>& Buildings);

	/** Bezahlung fuer eine Luftlinie (cm): monoton steigend, auf 5 EUR gerundet. */
	int32 ComputePayout(double DistanceCm);

	/**
	 * Zufaellige Adresse im Entfernungsband [MinDistanceCm, MaxDistanceCm] um
	 * `From` (gleichverteilt ueber alle Adressen im Band). INDEX_NONE, wenn im
	 * Band keine liegt. `Excluded` sind bereits verworfene Indizes (z. B. ohne
	 * erreichbare Strasse) - sie werden nicht erneut gezogen.
	 */
	int32 PickAddress(const TArray<FDennoDeliveryAddress>& Addresses, const FVector& From,
		FRandomStream& Random, const TSet<int32>& Excluded = TSet<int32>());

	/** Was Denno mitgibt - nur Farbe fuer Titel und Hinweis. */
	FString PickCargo(FRandomStream& Random);

	/**
	 * Die Mission: (1) Ware bei Denno aufnehmen, (2) an der Adresse abgeben.
	 * Frist automatisch aus der Route (FMission::AutoDeadline), Belohnung =
	 * Job.Payout. `ShopFront` ist der Aufnahmepunkt vor dem Laden.
	 */
	FMission BuildMission(const FDennoDeliveryJob& Job, const FVector& ShopFront, int32 Number);

	/** Ist das ein Denno-Lieferauftrag (Id aus BuildMission)? */
	bool IsDeliveryMission(FName MissionId);

	// -- Der wartende Kunde ----------------------------------------------------
	/** Ab dieser Naehe des Spielers steht der Kunde vor dem Haus (cm) - erst dann
	 *  ist sein Boden sicher gestreamt. */
	constexpr double CustomerAppearCm = 30000.0;

	/**
	 * Wo der Kunde wartet: vom Abgabepunkt (Fahrspur) Richtung Haus, auf dem
	 * Gehweg - 2,5 bis 6,5 m von der Spur, aber nie im Gebaeude (mindestens
	 * 1,5 m vor dem Schwerpunkt). Z bleibt die des Abgabepunkts; die Hoehe
	 * holt der Actor per Bodenstrahl.
	 */
	FVector ComputeCustomerSpot(const FVector& DropPoint, const FVector& AddressLocation);

	/**
	 * Trinkgeld nach Puenktlichkeit: Anteil der Frist, der bei der Abgabe noch
	 * uebrig war. Ab der Haelfte 25 % der Bezahlung, ab einem Viertel 15 %,
	 * sonst 5 %; ohne Restzeit (oder ohne Frist) nichts. Ganze Euro, mindestens 1.
	 */
	FDennoTip ComputeTip(int32 Payout, double RemainingSeconds, double DeadlineSeconds);

	// -- Nach dem Dank: zurueck ins Haus ---------------------------------------
	/** So lange bleibt er nach dem Dank stehen (s) - der Dank ist zu sehen. */
	constexpr double CustomerThankPauseSeconds = 2.5;
	/** Gehtempo und Schrittlaenge wie die Fussgaenger (WiesbadenPedestrianSimulation:
	 *  1,35 m/s, 75 cm je Gangzyklus). */
	constexpr double CustomerWalkSpeedCmS = 135.0;
	constexpr double CustomerStrideCm = 75.0;
	/** Abstand der Haustuer vor der gemessenen Wand (cm) - dort verschwindet er. */
	constexpr double DoorWallGapCm = 35.0;
	/** Ohne Wandtreffer: hoechstens so weit Richtung Schwerpunkt (cm). */
	constexpr double DoorFallbackMaxCm = 600.0;

	/**
	 * Die Haustuer, zu der der Kunde zurueckgeht: vom Warteplatz Richtung
	 * Gebaeudeschwerpunkt, knapp vor der Wand. WallDistanceCm ist der Abstand
	 * der Wand vom Warteplatz (Strahl des Actors, < 0 = kein Treffer); ohne
	 * Wand hoechstens DoorFallbackMaxCm und nie ueber 1,5 m vor den Schwerpunkt.
	 * Z bleibt die des Warteplatzes.
	 */
	FVector ComputeDoorPoint(const FVector& Spot, const FVector& AddressLocation, double WallDistanceCm);

	/**
	 * Gangbild nach gegangener Strecke: Index 0..3 der Fussgaenger-Posen
	 * (SM_WbPed2*_0..3). Bei 0 cm Pose 1 - die Durchgangsstellung, in der er
	 * gewartet hat -, damit der erste Schritt nicht springt.
	 */
	int32 ComputeWalkPose(double WalkedCm);
}
