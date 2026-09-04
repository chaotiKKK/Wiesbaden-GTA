// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenRailTransport.h"

namespace WiesbadenRailTransport
{
	bool BuildConstrainedGradeProfile(
		const TArray<double>& ArcLengthsCm,
		const TArray<double>& TerrainHeightsCm,
		double StartRailZCm,
		double EndRailZCm,
		double ClearanceCm,
		double MaxGrade,
		TArray<FWiesbadenRailProfilePoint>& OutProfile)
	{
		OutProfile.Reset();
		if (ArcLengthsCm.Num() < 2 || ArcLengthsCm.Num() != TerrainHeightsCm.Num()
			|| MaxGrade <= 0.0 || ClearanceCm < 0.0)
		{
			return false;
		}

		const double TotalLength = ArcLengthsCm.Last() - ArcLengthsCm[0];
		if (TotalLength <= 0.0)
		{
			return false;
		}
		for (int32 Index = 1; Index < ArcLengthsCm.Num(); ++Index)
		{
			if (ArcLengthsCm[Index] <= ArcLengthsCm[Index - 1])
			{
				return false;
			}
		}

		OutProfile.SetNum(ArcLengthsCm.Num());
		for (int32 Index = 0; Index < ArcLengthsCm.Num(); ++Index)
		{
			FWiesbadenRailProfilePoint& Point = OutProfile[Index];
			Point.ArcLengthCm = ArcLengthsCm[Index];
			Point.TerrainZCm = TerrainHeightsCm[Index];
			const double Alpha = (ArcLengthsCm[Index] - ArcLengthsCm[0]) / TotalLength;
			Point.RailZCm = FMath::Max(
				FMath::Lerp(StartRailZCm, EndRailZCm, Alpha),
				Point.TerrainZCm + ClearanceCm);
		}

		// Raise neighbouring points instead of lowering the rail into the
		// terrain. Repeating the two directional relaxations propagates a
		// steep terrain step through the complete profile while preserving the
		// maximum absolute grade. This is done only when terrain is resolved,
		// never in the per-frame movement path.
		for (int32 Pass = 0; Pass < OutProfile.Num(); ++Pass)
		{
			bool bChanged = false;
			for (int32 Index = 1; Index < OutProfile.Num(); ++Index)
			{
				const double Distance = OutProfile[Index].ArcLengthCm
					- OutProfile[Index - 1].ArcLengthCm;
				const double MinimumRailZ = OutProfile[Index - 1].RailZCm
					- Distance * MaxGrade;
				if (OutProfile[Index].RailZCm < MinimumRailZ)
				{
					OutProfile[Index].RailZCm = MinimumRailZ;
					bChanged = true;
				}
			}
			for (int32 Index = OutProfile.Num() - 2; Index >= 0; --Index)
			{
				const double Distance = OutProfile[Index + 1].ArcLengthCm
					- OutProfile[Index].ArcLengthCm;
				const double MinimumRailZ = OutProfile[Index + 1].RailZCm
					- Distance * MaxGrade;
				if (OutProfile[Index].RailZCm < MinimumRailZ)
				{
					OutProfile[Index].RailZCm = MinimumRailZ;
					bChanged = true;
				}
			}
			if (!bChanged)
			{
				break;
			}
		}

		for (int32 Index = 0; Index < OutProfile.Num(); ++Index)
		{
			if (OutProfile[Index].RailZCm + KINDA_SMALL_NUMBER
				< OutProfile[Index].TerrainZCm + ClearanceCm)
			{
				OutProfile.Reset();
				return false;
			}
			if (Index > 0)
			{
				const double Distance = OutProfile[Index].ArcLengthCm
					- OutProfile[Index - 1].ArcLengthCm;
				const double Grade = FMath::Abs(
					(OutProfile[Index].RailZCm - OutProfile[Index - 1].RailZCm) / Distance);
				if (Grade > MaxGrade + KINDA_SMALL_NUMBER)
				{
					OutProfile.Reset();
					return false;
				}
			}
		}

		return true;
	}

	void StationRailEndpoints(
		double TerrainBottomZCm, double TerrainTopZCm, double ClearanceCm,
		double& OutStartRailZCm, double& OutEndRailZCm)
	{
		OutStartRailZCm = TerrainBottomZCm + ClearanceCm;
		OutEndRailZCm = TerrainTopZCm + ClearanceCm;
	}

	double OpposingCablePosition(double CablePositionCm, double TrackLengthCm, bool bOpposingCar)
	{
		const double Position = FMath::Clamp(CablePositionCm, 0.0, FMath::Max(TrackLengthCm, 0.0));
		return bOpposingCar ? FMath::Max(TrackLengthCm, 0.0) - Position : Position;
	}

	bool CanTransitionRideState(ERideState From, ERideState To)
	{
		if (From == To)
		{
			return true;
		}
		return (From == ERideState::OnFoot && To == ERideState::Boarding)
			|| (From == ERideState::Boarding && To == ERideState::Riding)
			|| (From == ERideState::Riding && To == ERideState::Exiting)
			|| (From == ERideState::Exiting && To == ERideState::OnFoot);
	}

	bool FWiesbadenRideSession::BeginBoarding(APawn* InPassenger, int32 InCarIndex)
	{
		if (!InPassenger || InCarIndex < 0 || !CanTransitionRideState(State, ERideState::Boarding))
		{
			return false;
		}
		Passenger = InPassenger;
		CarIndex = InCarIndex;
		State = ERideState::Boarding;
		return true;
	}

	bool FWiesbadenRideSession::ConfirmRiding()
	{
		if (!Passenger.IsValid() || !CanTransitionRideState(State, ERideState::Riding))
		{
			return false;
		}
		State = ERideState::Riding;
		return true;
	}

	bool FWiesbadenRideSession::BeginExiting()
	{
		if (!Passenger.IsValid() || !CanTransitionRideState(State, ERideState::Exiting))
		{
			return false;
		}
		State = ERideState::Exiting;
		return true;
	}

	void FWiesbadenRideSession::CompleteExit()
	{
		State = ERideState::OnFoot;
		Passenger.Reset();
		CarIndex = INDEX_NONE;
	}

	void FWiesbadenRideSession::Reset()
	{
		CompleteExit();
	}
}
