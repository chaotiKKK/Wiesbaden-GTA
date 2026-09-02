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

	/** Position entlang einer gegenlaeufigen Seilbahnstrecke. */
	WIESBADENREAL_API double OpposingCablePosition(
		double CablePositionCm, double TrackLengthCm, bool bOpposingCar);

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
