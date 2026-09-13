// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class APawn;

/** Datenpunkt eines bereits abgetasteten Gleisprofils. */
struct FWiesbadenRailProfilePoint
{
	double ArcLengthCm = 0.0;
	double TerrainZCm = 0.0;
	double RailZCm = 0.0;
};

/** Rein datenbasierte Helfer fuer Gleisprofil und Fahrgast-Zustand. */
namespace WiesbadenRailTransport
{
	/**
	 * Erzeugt ein glattes, monoton steigendes Hoehenprofil mit begrenzter
	 * Steigung und Mindestabstand zum Terrain.
	 */
	WIESBADENREAL_API bool BuildConstrainedGradeProfile(
		const TArray<double>& ArcLengthsCm,
		const TArray<double>& TerrainHeightsCm,
		double StartRailZCm,
		double EndRailZCm,
		double ClearanceCm,
		double MaxGrade,
		TArray<FWiesbadenRailProfilePoint>& OutProfile);

	/**
	 * Schienenhoehen der beiden Stationsenden.
	 *
	 * Die Schienen RUHEN an beiden Stationen auf dem Gelaende (plus Abstand).
	 * Frueher hob der Aufrufer das obere Ende an, damit die Sehne genau die
	 * Maximalsteigung traf - das haengte das obere Streckendrittel samt
	 * Bergstation bis zu 16 m in die Luft, sobald die echte Durchschnitts-
	 * steigung unter der Maximalsteigung lag (Nerobergbahn: ~19 % gegen 30 %).
	 * Die Enden gehoeren aufs Terrain; ob die Sehne dazwischen fahrbar ist,
	 * entscheidet allein BuildConstrainedGradeProfile.
	 */
	WIESBADENREAL_API void StationRailEndpoints(
		double TerrainBottomZCm, double TerrainTopZCm, double ClearanceCm,
		double& OutStartRailZCm, double& OutEndRailZCm);

	/**
	 * Prueft, ob ein abgetastetes Gleisprofil unplausibel ist.
	 *
	 * Geprueft wird der datumsunabhaengige HOEHENUNTERSCHIED gegen den
	 * erwarteten Klettergewinn (Nerobergbahn: 83 m Vorbild) mit Toleranz -
	 * NICHT die absolute Lage, denn der In-Game-Hoehendatensatz liegt rund
	 * 70 m unter NN. Zusaetzlich eine Untergrenze fuer den Talfuss: liegt er
	 * nahe dem Weltnullpunkt, wurden die Hoehen vor dem Streaming abgefragt
	 * und die Trasse steckt im Gelaende. True = unplausibel.
	 */
	WIESBADENREAL_API bool RailHeightsImplausible(
		double BottomM, double TopM,
		double ExpectedClimbM, double ClimbToleranceM, double MinBottomM);

	/** Position entlang einer gegenlaeufigen Seilbahnstrecke. */
	WIESBADENREAL_API double OpposingCablePosition(
		double CablePositionCm, double TrackLengthCm, bool bOpposingCar);

	/** Zustand eines einzelnen Pendelwagens auf einer Strecke [0, Length]. */
	struct FWiesbadenShuttleState
	{
		double PositionCm = 0.0;      // Bogenlaenge ab Start-Terminus
		int32 Direction = +1;         // +1 vorwaerts, -1 rueckwaerts
		float DwellRemaining = 0.0f;  // restliche Haltezeit am Terminus
	};

	/**
	 * Bewegt den Pendelwagen um einen Zeitschritt. Am Streckenende kehrt er um
	 * und haelt DwellSeconds; waehrend der Haltezeit ruht er. Ueberschiessen bei
	 * grossem DeltaSeconds wird auf das Streckenende geklemmt (kein Runaway) -
	 * anders als eine Seilbahn faehrt der Tram allein, es gibt keinen Gegenwagen.
	 */
	WIESBADENREAL_API void AdvanceShuttle(
		FWiesbadenShuttleState& State, double TrackLengthCm,
		double SpeedCmPerSec, float DwellSeconds, float DeltaSeconds);

	/**
	 * Interpoliert Position und normierte Tangente bei Bogenlaenge S auf einem
	 * Polygonzug. Positions und ArcLengthsCm muessen gleich lang (>= 2) und die
	 * ArcLengthsCm aufsteigend sein; S wird auf [erste, letzte ArcLength]
	 * geklemmt. False bei zu wenigen Punkten - dann liefert Out den Ursprung
	 * bzw. die X-Achse.
	 */
	WIESBADENREAL_API bool SamplePolyline(
		const TArray<FVector>& Positions, const TArray<double>& ArcLengthsCm,
		double S, FVector& OutPos, FVector& OutTangent);

	enum class ERideState : uint8
	{
		OnFoot,
		Boarding,
		Riding,
		Exiting
	};

	/** Erlaubt nur gueltige Zustandswechsel des Fahrgastmodells. */
	WIESBADENREAL_API bool CanTransitionRideState(ERideState From, ERideState To);

	/** Besitz- und Zustandsdaten einer einzelnen Fahrt. */
	struct FWiesbadenRideSession
	{
		ERideState State = ERideState::OnFoot;
		TWeakObjectPtr<APawn> Passenger;
		int32 CarIndex = INDEX_NONE;

		bool BeginBoarding(APawn* InPassenger, int32 InCarIndex);
		bool ConfirmRiding();
		bool BeginExiting();
		void CompleteExit();
		void Reset();
		bool IsRiding() const { return State == ERideState::Riding; }
		APawn* GetPassenger() const { return Passenger.Get(); }
	};
}
