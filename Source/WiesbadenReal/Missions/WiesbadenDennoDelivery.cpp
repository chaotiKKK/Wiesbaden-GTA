// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Missions/WiesbadenDennoDelivery.h"

#include "GIS/BuildingGenerator.h"

namespace WiesbadenDennoDelivery
{

TArray<FDennoDeliveryAddress> CollectAddresses(const TArray<FGeneratedBuilding>& Buildings)
{
	TArray<FDennoDeliveryAddress> Out;
	TSet<FString> Seen;
	for (const FGeneratedBuilding& Building : Buildings)
	{
		const FString Address = Building.Address.TrimStartAndEnd();
		// Nur "Strasse Hausnummer": ohne Hausnummer (reine Strassenangabe) gibt
		// es keine Tuer, an der Denno abliefern koennte.
		const int32 LastSpace = Address.Find(TEXT(" "), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (LastSpace <= 0 || LastSpace + 1 >= Address.Len()
			|| !FChar::IsDigit(Address[LastSpace + 1]) || Seen.Contains(Address))
		{
			continue;
		}
		Seen.Add(Address);
		Out.Add({ Address, Building.Centroid });
	}
	return Out;
}

int32 ComputePayout(double DistanceCm)
{
	const double Km = FMath::Max(0.0, DistanceCm) / 100000.0;
	const double Raw = BasePayout + PayoutPerKm * Km;
	return FMath::RoundToInt(Raw / 5.0) * 5;
}

int32 PickAddress(const TArray<FDennoDeliveryAddress>& Addresses, const FVector& From,
	FRandomStream& Random, const TSet<int32>& Excluded)
{
	TArray<int32> InBand;
	for (int32 I = 0; I < Addresses.Num(); ++I)
	{
		if (Excluded.Contains(I))
		{
			continue;
		}
		const double D = FVector::Dist2D(Addresses[I].Location, From);
		if (D >= MinDistanceCm && D <= MaxDistanceCm)
		{
			InBand.Add(I);
		}
	}
	if (InBand.IsEmpty())
	{
		return INDEX_NONE;
	}
	return InBand[Random.RandRange(0, InBand.Num() - 1)];
}

FString PickCargo(FRandomStream& Random)
{
	static const TCHAR* Cargo[] = {
		TEXT("Kuchenpaket"), TEXT("Kaffee zum Mitnehmen"), TEXT("Geburtstagstorte"),
		TEXT("Croissants"), TEXT("Bohnen-Abo"), TEXT("Friseur-Gutschein") };
	return Cargo[Random.RandRange(0, UE_ARRAY_COUNT(Cargo) - 1)];
}

FMission BuildMission(const FDennoDeliveryJob& Job, const FVector& ShopFront, int32 Number)
{
	FMission Mission;
	Mission.Id = FName(*FString::Printf(TEXT("denno_lieferung_%d"), Number));
	Mission.Title = FString::Printf(TEXT("Denno-Lieferung: %s"), *Job.Address);

	FMissionObjective Pickup;
	Pickup.Type = EObjectiveType::PickUpCargo;
	Pickup.Location = ShopFront;
	Pickup.RadiusCm = PickupRadiusCm;
	Pickup.Label = FString::Printf(TEXT("%s bei Denno abholen"), *Job.Cargo);

	FMissionObjective Drop;
	Drop.Type = EObjectiveType::DropOffCargo;
	Drop.Location = Job.DropPoint;
	Drop.RadiusCm = DropRadiusCm;
	Drop.Label = FString::Printf(TEXT("%s nach %s bringen"), *Job.Cargo, *Job.Address);

	Mission.Objectives = { Pickup, Drop };
	Mission.Reward.Guthaben = Job.Payout;
	Mission.DeadlineSeconds = FMission::AutoDeadline;
	return Mission;
}

bool IsDeliveryMission(FName MissionId)
{
	return MissionId.ToString().StartsWith(TEXT("denno_lieferung_"), ESearchCase::CaseSensitive);
}

FVector ComputeCustomerSpot(const FVector& DropPoint, const FVector& AddressLocation)
{
	FVector Dir(AddressLocation.X - DropPoint.X, AddressLocation.Y - DropPoint.Y, 0.0);
	const double Distance = Dir.Size();
	Dir = Distance > 1.0 ? Dir / Distance : FVector(1.0, 0.0, 0.0);
	double Offset = FMath::Clamp(Distance * 0.45, 250.0, 650.0);
	if (Distance > 1.0)
	{
		Offset = FMath::Min(Offset, FMath::Max(Distance - 150.0, Distance * 0.5));
	}
	return DropPoint + Dir * Offset;
}

FVector ComputeDoorPoint(const FVector& Spot, const FVector& AddressLocation, double WallDistanceCm)
{
	FVector Dir(AddressLocation.X - Spot.X, AddressLocation.Y - Spot.Y, 0.0);
	const double Distance = Dir.Size();
	if (Distance <= 1.0)
	{
		return Spot;   // steht schon am Schwerpunkt: an Ort und Stelle hinein
	}
	Dir /= Distance;
	double Reach = FMath::Clamp(Distance - 150.0, 0.0, DoorFallbackMaxCm);
	if (WallDistanceCm >= 0.0)
	{
		Reach = FMath::Min(FMath::Max(WallDistanceCm - DoorWallGapCm, 0.0), Distance);
	}
	return Spot + Dir * Reach;
}

int32 ComputeWalkPose(double WalkedCm)
{
	// Viertel-Versatz: bei 0 cm Pose 1 (Durchgangsstellung), dann 2, 3, 0, ...
	const double Cycles = FMath::Max(WalkedCm, 0.0) / CustomerStrideCm + 0.25;
	const double Phase = Cycles - FMath::FloorToDouble(Cycles);
	return FMath::Clamp(FMath::FloorToInt32(Phase * 4.0), 0, 3);
}

float ComputeWalkPlayRate(double SpeedCmS)
{
	return static_cast<float>(FMath::Clamp(SpeedCmS / CustomerWalkAnimSpeedCmS, 0.5, 2.0));
}

FDennoTip ComputeTip(int32 Payout, double RemainingSeconds, double DeadlineSeconds)
{
	FDennoTip Tip;
	if (DeadlineSeconds <= 0.0 || RemainingSeconds <= 0.0 || Payout <= 0)
	{
		Tip.Thanks = TEXT("Danke.");
		return Tip;
	}
	const double Fraction = FMath::Clamp(RemainingSeconds / DeadlineSeconds, 0.0, 1.0);
	double Share = 0.05;
	Tip.Thanks = TEXT("Gerade noch rechtzeitig - danke trotzdem!");
	if (Fraction >= 0.5)
	{
		Share = 0.25;
		Tip.Thanks = TEXT("Das ging ja flott! Der Rest ist fuer Sie.");
		Tip.bFast = true;
	}
	else if (Fraction >= 0.25)
	{
		Share = 0.15;
		Tip.Thanks = TEXT("Puenktlich wie die Marktkirche - danke!");
	}
	Tip.Amount = FMath::Max(1, FMath::RoundToInt(Payout * Share));
	return Tip;
}

}
