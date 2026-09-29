// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenTrafficCars.h"

namespace
{
	const TCHAR* const WheelNames[4] = { TEXT("FL"), TEXT("FR"), TEXT("RL"), TEXT("RR") };

	/** Fahrer: so fest zieht er die Sollposition nach (1/s) ... */
	constexpr double PositionGain = 1.2;
	/** ... und so fest das Tempo (1/s). */
	constexpr double SpeedGain = 2.0;
	/** Mehr als so viel ueber dem Solltempo faehrt er nie, auch wenn er hinterherhinkt (cm/s). */
	constexpr double MaxCatchUpCmS = 400.0;
	/** Im Stand: naeher als das an der Sollposition = halten (cm). */
	constexpr double HoldDistanceCm = 40.0;

	uint32 HashId(int32 VehicleId, uint32 A, uint32 B)
	{
		uint32 H = static_cast<uint32>(VehicleId) * A;
		H ^= (H >> 15);
		H *= B;
		H ^= (H >> 13);
		return H;
	}

	/** Index nach relativen Gewichten aus einem Hash. */
	int32 PickWeighted(const TArray<float>& Weights, uint32 Hash)
	{
		float Total = 0.0f;
		for (const float W : Weights)
		{
			Total += FMath::Max(0.0f, W);
		}
		if (Total <= 0.0f)
		{
			return 0;
		}
		const float Pick = (static_cast<float>(Hash % 1000000u) / 1000000.0f) * Total;
		float Acc = 0.0f;
		for (int32 I = 0; I < Weights.Num(); ++I)
		{
			Acc += FMath::Max(0.0f, Weights[I]);
			if (Pick < Acc)
			{
				return I;
			}
		}
		return Weights.Num() - 1;
	}

	FWiesbadenPowertrainSpec Golf3()
	{
		// VW Golf III 1.8 (66 kW / 90 PS, 145 Nm bei 2.500/min), 5-Gang.
		FWiesbadenPowertrainSpec S;
		S.MaxTorqueNm = 145.0f;
		S.MaxRpm = 6000.0f;
		S.IdleRpm = 800.0f;
		S.TorqueCurveNormalized = {
			FVector2D(800.0, 0.60), FVector2D(1500.0, 0.82), FVector2D(2500.0, 1.00),
			FVector2D(3500.0, 0.97), FVector2D(4500.0, 0.88), FVector2D(5200.0, 0.76), FVector2D(6000.0, 0.60) };
		S.ForwardGearRatios = { 3.455f, 1.944f, 1.370f, 1.032f, 0.850f };
		S.ReverseGearRatio = 3.167f;
		S.FinalDriveRatio = 3.667f;
		S.MassKg = 1150.0f;
		return S;
	}

	FWiesbadenPowertrainSpec Peugeot207()
	{
		// Peugeot 207 1.6 VTi (88 kW / 120 PS, 160 Nm bei 4.250/min), 5-Gang.
		FWiesbadenPowertrainSpec S;
		S.MaxTorqueNm = 160.0f;
		S.MaxRpm = 6500.0f;
		S.IdleRpm = 800.0f;
		S.TorqueCurveNormalized = {
			FVector2D(800.0, 0.55), FVector2D(1500.0, 0.72), FVector2D(2500.0, 0.86),
			FVector2D(3500.0, 0.95), FVector2D(4250.0, 1.00), FVector2D(5500.0, 0.95), FVector2D(6500.0, 0.78) };
		S.ForwardGearRatios = { 3.417f, 1.810f, 1.276f, 0.975f, 0.767f };
		S.ReverseGearRatio = 3.333f;
		S.FinalDriveRatio = 4.060f;
		S.MassKg = 1200.0f;
		return S;
	}

	FWiesbadenPowertrainSpec T6California()
	{
		// VW T6 California 2.0 TDI (110 kW / 150 PS, 340 Nm bei 1.500-2.750/min), 6-Gang.
		FWiesbadenPowertrainSpec S;
		S.MaxTorqueNm = 340.0f;
		S.MaxRpm = 4500.0f;
		S.IdleRpm = 800.0f;
		S.TorqueCurveNormalized = {
			FVector2D(800.0, 0.45), FVector2D(1250.0, 0.75), FVector2D(1500.0, 1.00),
			FVector2D(2750.0, 1.00), FVector2D(3500.0, 0.85), FVector2D(4000.0, 0.70), FVector2D(4500.0, 0.50) };
		S.ForwardGearRatios = { 3.778f, 2.118f, 1.360f, 0.971f, 0.756f, 0.622f };
		S.ReverseGearRatio = 4.394f;
		S.FinalDriveRatio = 3.938f;
		S.MassKg = 2400.0f;
		return S;
	}

	FWiesbadenPowertrainSpec BmwE46_320i()
	{
		// BMW 320i E46 (2,2 l, 125 kW / 170 PS, 210 Nm bei 3.500/min), 5-Gang, Heckantrieb.
		FWiesbadenPowertrainSpec S;
		S.MaxTorqueNm = 210.0f;
		S.MaxRpm = 6500.0f;
		S.IdleRpm = 750.0f;
		S.TorqueCurveNormalized = {
			FVector2D(800.0, 0.55), FVector2D(1500.0, 0.72), FVector2D(2500.0, 0.88),
			FVector2D(3500.0, 1.00), FVector2D(4500.0, 0.97), FVector2D(5500.0, 0.90), FVector2D(6500.0, 0.72) };
		S.ForwardGearRatios = { 4.23f, 2.52f, 1.66f, 1.22f, 1.00f };
		S.ReverseGearRatio = 4.04f;
		S.FinalDriveRatio = 3.07f;
		S.MassKg = 1470.0f;
		return S;
	}

	FWiesbadenPowertrainSpec BmwE46_330d()
	{
		// BMW 330d E46 (3,0 l Diesel, 135 kW / 184 PS, 390 Nm bei 1.750-3.000/min), 5-Gang, Heckantrieb.
		FWiesbadenPowertrainSpec S;
		S.MaxTorqueNm = 390.0f;
		S.MaxRpm = 4750.0f;
		S.IdleRpm = 800.0f;
		S.TorqueCurveNormalized = {
			FVector2D(800.0, 0.45), FVector2D(1250.0, 0.75), FVector2D(1750.0, 1.00),
			FVector2D(3000.0, 1.00), FVector2D(3750.0, 0.85), FVector2D(4250.0, 0.70), FVector2D(4750.0, 0.50) };
		S.ForwardGearRatios = { 4.23f, 2.52f, 1.66f, 1.22f, 1.00f };
		S.ReverseGearRatio = 4.04f;
		S.FinalDriveRatio = 2.56f;
		S.MassKg = 1560.0f;
		return S;
	}

	TArray<FWbTrafficCarType> BuildTypes()
	{
		TArray<FWbTrafficCarType> Types;

		// Masse: Data/Raw/Verkehr/<Name>/masse.json (Tools/Blender/build_traffic_cars.py).
		FWbTrafficCarType Golf;
		Golf.Name = TEXT("Golf");
		Golf.Model = TEXT("VW Golf III 5-Tuerer");
		Golf.Weight = 22.0f;
		Golf.FrontCm = 204.8;
		Golf.RearCm = 197.2;
		Golf.BodyWidthCm = 169.5;
		Golf.WheelbaseCm = 247.4;
		Golf.TrackCm = 142.7;
		Golf.WheelRadiusCm = 33.0;
		Golf.HeadLampCm = FVector(192.0, -56.0, 66.0);
		Golf.TailLampCm = FVector(-191.0, -62.0, 88.0);
		Golf.Powertrain = Golf3();
		Golf.DragCoeffAreaM2 = 0.62f;
		Golf.FrontWeightFraction = 0.61f;
		Golf.CgHeightM = 0.52f;
		Golf.YawInertiaKgM2 = 1700.0f;
		Golf.CorneringStiffnessNPerRad = 70000.0f;
		Golf.MaxSteerAngleDeg = 33.0f;
		Types.Add(Golf);

		FWbTrafficCarType Peugeot;
		Peugeot.Name = TEXT("Peugeot");
		Peugeot.Model = TEXT("Peugeot 207 3-Tuerer");
		Peugeot.Weight = 20.0f;
		Peugeot.FrontCm = 208.5;
		Peugeot.RearCm = 194.5;
		Peugeot.BodyWidthCm = 172.0;
		Peugeot.WheelbaseCm = 256.0;
		Peugeot.TrackCm = 147.5;
		Peugeot.WheelRadiusCm = 34.2;
		Peugeot.HeadLampCm = FVector(192.0, -60.0, 76.0);
		Peugeot.TailLampCm = FVector(-188.0, -66.0, 98.0);
		Peugeot.Powertrain = Peugeot207();
		Peugeot.DragCoeffAreaM2 = 0.66f;
		Peugeot.FrontWeightFraction = 0.62f;
		Peugeot.CgHeightM = 0.53f;
		Peugeot.YawInertiaKgM2 = 1850.0f;
		Peugeot.CorneringStiffnessNPerRad = 72000.0f;
		Peugeot.MaxSteerAngleDeg = 33.0f;
		Types.Add(Peugeot);

		FWbTrafficCarType Van;
		Van.Name = TEXT("Transporter");
		Van.Model = TEXT("VW T6 California");
		Van.Weight = 14.0f;
		Van.FrontCm = 244.2;
		Van.RearCm = 246.2;
		Van.BodyWidthCm = 190.4;
		Van.WheelbaseCm = 299.2;
		Van.TrackCm = 162.8;
		Van.WheelRadiusCm = 39.0;
		Van.HeadLampCm = FVector(232.0, -72.0, 96.0);
		Van.TailLampCm = FVector(-240.0, -82.0, 106.0);
		Van.Powertrain = T6California();
		Van.DragCoeffAreaM2 = 1.12f;
		Van.FrontWeightFraction = 0.56f;
		Van.CgHeightM = 0.78f;
		Van.YawInertiaKgM2 = 5300.0f;
		Van.CorneringStiffnessNPerRad = 110000.0f;
		Van.MaxSteerAngleDeg = 30.0f;
		Van.ShiftUpRpm = 2200.0f;
		Van.ShiftDownRpm = 1100.0f;
		Types.Add(Van);

		// Der Kaefer im Verkehr faehrt mit DEMSELBEN Antrieb wie der des Spielers
		// (FWiesbadenPowertrainSpec::Kaefer1302, Heckmotor, hecklastig).
		FWbTrafficCarType Kaefer;
		Kaefer.Name = TEXT("Kaefer");
		Kaefer.Model = TEXT("VW Kaefer 1303");
		Kaefer.Weight = 12.0f;
		Kaefer.FrontCm = 196.6;
		Kaefer.RearCm = 214.5;
		Kaefer.BodyWidthCm = 158.5;
		Kaefer.WheelbaseCm = 237.7;
		Kaefer.TrackCm = 137.9;
		Kaefer.WheelRadiusCm = 34.1;
		Kaefer.HeadLampCm = FVector(166.0, -54.0, 70.0);
		Kaefer.TailLampCm = FVector(-199.0, -58.0, 78.0);
		Kaefer.Powertrain = FWiesbadenPowertrainSpec::Kaefer1302();
		Kaefer.bFrontWheelDrive = false;
		Kaefer.DragCoeffAreaM2 = 1.05f;
		Kaefer.FrontWeightFraction = 0.42f;
		Kaefer.CgHeightM = 0.45f;
		Kaefer.YawInertiaKgM2 = 1150.0f;
		Kaefer.CorneringStiffnessNPerRad = 33000.0f;
		Kaefer.MaxSteerAngleDeg = 35.0f;
		Types.Add(Kaefer);

		FWbTrafficCarType BmwBlau;
		BmwBlau.Name = TEXT("BmwBlau");
		BmwBlau.Model = TEXT("BMW 320i E46 Limousine");
		BmwBlau.Weight = 16.0f;
		BmwBlau.FrontCm = 215.4;
		BmwBlau.RearCm = 231.7;
		BmwBlau.BodyWidthCm = 173.9;
		BmwBlau.WheelbaseCm = 267.4;
		BmwBlau.TrackCm = 148.1;
		BmwBlau.WheelRadiusCm = 34.85;
		BmwBlau.HeadLampCm = FVector(202.0, -60.0, 68.0);
		BmwBlau.TailLampCm = FVector(-220.0, -65.0, 88.0);
		BmwBlau.Powertrain = BmwE46_320i();
		BmwBlau.bFrontWheelDrive = false;
		BmwBlau.DragCoeffAreaM2 = 0.62f;
		BmwBlau.FrontWeightFraction = 0.51f;
		BmwBlau.CgHeightM = 0.52f;
		BmwBlau.YawInertiaKgM2 = 2200.0f;
		BmwBlau.CorneringStiffnessNPerRad = 80000.0f;
		BmwBlau.MaxSteerAngleDeg = 32.0f;
		Types.Add(BmwBlau);

		FWbTrafficCarType BmwGrau = BmwBlau;
		BmwGrau.Name = TEXT("BmwGrau");
		BmwGrau.Model = TEXT("BMW 330d E46 Limousine");
		BmwGrau.FrontCm = 214.5;
		BmwGrau.RearCm = 232.7;
		BmwGrau.WheelbaseCm = 268.0;
		BmwGrau.WheelRadiusCm = 33.7;
		BmwGrau.Powertrain = BmwE46_330d();
		BmwGrau.YawInertiaKgM2 = 2300.0f;
		BmwGrau.ShiftUpRpm = 2200.0f;
		BmwGrau.ShiftDownRpm = 1100.0f;
		Types.Add(BmwGrau);
		return Types;
	}
}

FWiesbadenVehiclePhysics FWbTrafficCarType::MakePhysics() const
{
	FWiesbadenVehiclePhysics P;
	P.Powertrain = Powertrain;
	P.bFrontWheelDrive = bFrontWheelDrive;
	P.ShiftUpRpm = ShiftUpRpm;
	P.ShiftDownRpm = ShiftDownRpm;
	P.WheelRadiusM = static_cast<float>(WheelRadiusCm / 100.0);
	P.WheelbaseM = static_cast<float>(WheelbaseCm / 100.0);
	P.DragCoeffAreaM2 = DragCoeffAreaM2;
	P.FrontWeightFraction = FrontWeightFraction;
	P.CgHeightM = CgHeightM;
	P.YawInertiaKgM2 = YawInertiaKgM2;
	P.CorneringStiffnessFrontNPerRad = CorneringStiffnessNPerRad;
	P.CorneringStiffnessRearNPerRad = CorneringStiffnessNPerRad;
	P.MaxSteerAngleDeg = MaxSteerAngleDeg;
	// Heutige Reifen haften besser als die des Kaefers (0,75), Bremse ~0,8 g.
	P.MuTraction = 0.9f;
	// Die Kaefer-Kalibrierung (Seitenhaftung, Triebstrang) gilt nur fuer den
	// Spielerwagen - der Verkehr bleibt wie er war.
	P.LateralGripFactor = 1.0f;
	P.DrivetrainEfficiency = 1.0f;
	P.BrakeForceN = 0.8f * Powertrain.MassKg * P.GravityMetersPerS2;
	P.EngineBrakeTorqueNm = 0.3f * Powertrain.MaxTorqueNm;
	P.Reset();
	return P;
}

namespace WiesbadenTrafficCars
{
	const TArray<FWbTrafficCarType>& Types()
	{
		static const TArray<FWbTrafficCarType> Catalog = BuildTypes();
		return Catalog;
	}

	int32 SelectType(int32 VehicleId)
	{
		TArray<float> Weights;
		for (const FWbTrafficCarType& T : Types())
		{
			Weights.Add(T.Weight);
		}
		// Ganzzahl-Hash bricht "Id mod N": benachbarte Ids bekommen verschiedene Typen.
		return PickWeighted(Weights, HashId(VehicleId, 2654435761u, 2246822519u));
	}

	const TArray<FWbTrafficPaint>& Paints()
	{
		static const TArray<FWbTrafficPaint> Palette = {
			{ TEXT("Werkslack"),   FColor::White,         20.0f, true },
			{ TEXT("Schwarz"),     FColor(18, 18, 20),    18.0f },
			{ TEXT("Silber"),      FColor(184, 186, 191), 16.0f },
			{ TEXT("Anthrazit"),   FColor(76, 79, 84),    14.0f },
			{ TEXT("Weiss"),       FColor(235, 235, 232), 14.0f },
			{ TEXT("Dunkelblau"),  FColor(20, 41, 89),     7.0f },
			{ TEXT("Hellblau"),    FColor(77, 128, 184),   3.0f },
			{ TEXT("Rot"),         FColor(178, 20, 20),    4.0f },
			{ TEXT("Dunkelgruen"), FColor(26, 71, 46),     2.0f },
			{ TEXT("Champagner"),  FColor(184, 168, 140),  1.5f },
			{ TEXT("Gelb"),        FColor(242, 199, 31),   0.5f },
		};
		return Palette;
	}

	int32 SelectPaint(int32 VehicleId)
	{
		TArray<float> Weights;
		for (const FWbTrafficPaint& P : Paints())
		{
			Weights.Add(P.Weight);
		}
		// Andere Hash-Konstanten als SelectType: Lack und Typ sind unabhaengig.
		return PickWeighted(Weights, HashId(VehicleId, 2246822519u, 3266489917u));
	}

	FLinearColor PaintCustomData(int32 PaintIndex)
	{
		const TArray<FWbTrafficPaint>& All = Paints();
		if (!All.IsValidIndex(PaintIndex) || All[PaintIndex].bFactory)
		{
			return FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
		}
		FLinearColor Linear(All[PaintIndex].Srgb);   // sRGB -> linear
		Linear.A = 1.0f;
		return Linear;
	}

	const TCHAR* WheelName(int32 Wheel)
	{
		return Wheel >= 0 && Wheel < 4 ? WheelNames[Wheel] : TEXT("?");
	}

	bool IsFrontWheel(int32 Wheel)
	{
		return Wheel == 0 || Wheel == 1;
	}

	FString BodyMeshPath(const FWbTrafficCarType& Type)
	{
		return FString::Printf(TEXT("/Game/Vehicles/Traffic/%s/Meshes/SM_%s_Body.SM_%s_Body"), Type.Name, Type.Name, Type.Name);
	}

	FString WheelMeshPath(const FWbTrafficCarType& Type, int32 Wheel)
	{
		const TCHAR* W = WheelName(Wheel);
		return FString::Printf(TEXT("/Game/Vehicles/Traffic/%s/Meshes/SM_%s_Wheel_%s.SM_%s_Wheel_%s"),
			Type.Name, Type.Name, W, Type.Name, W);
	}

	FWiesbadenVehiclePhysicsInput ComputeDriverInput(const FWbTrafficDriverView& View)
	{
		FWiesbadenVehiclePhysicsInput Input;
		const FVector2D Forward(FMath::Cos(View.BodyYawRad), FMath::Sin(View.BodyYawRad));

		// Lenkrad: reine Verfolgung - aus dem Winkel zum Zielpunkt folgt der
		// Einschlag, bezogen auf den bei diesem Tempo nutzbaren Anschlag.
		const FVector2D ToTarget = View.PursuitTargetXY - View.BodyXY;
		const double Distance = ToTarget.Size();
		if (Distance > 1.0)
		{
			const double Alpha = FMath::UnwindRadians(FMath::Atan2(ToTarget.Y, ToTarget.X) - View.BodyYawRad);
			const double Steer = FMath::Atan2(2.0 * View.WheelbaseCm * FMath::Sin(Alpha), Distance);
			Input.Steering = static_cast<float>(FMath::Clamp(Steer / FMath::Max(View.UsableSteerRad, 0.01), -1.0, 1.0));
		}

		// Gas und Bremse: Solltempo, dazu den Abstand zur Sollposition aufholen
		// bzw. abbauen. Die Sollbeschleunigung geht vorweg ein, damit er beim
		// Anfahren an der Ampel nicht erst zurueckfaellt.
		const double AlongCm = FVector2D::DotProduct(View.SollXY - View.BodyXY, Forward);
		double WantSpeed = View.SollSpeedCmS + PositionGain * AlongCm;
		WantSpeed = FMath::Clamp(WantSpeed, 0.0, View.SollSpeedCmS + MaxCatchUpCmS);
		const double SollAccel = View.Dt > 0.0
			? FMath::Clamp((View.SollSpeedCmS - View.PrevSollSpeedCmS) / View.Dt, -900.0, 400.0)
			: 0.0;
		const bool bHold = View.SollSpeedCmS < 1.0 && AlongCm < HoldDistanceCm;
		if (bHold)
		{
			// Steht der Vordermann / ist Rot: stehen bleiben, nicht kriechen.
			Input.Brake = View.BodySpeedCmS > 30.0
				? static_cast<float>(FMath::Clamp((View.BodySpeedCmS * SpeedGain + 150.0) / View.FullBrakeCmS2, 0.3, 1.0))
				: 1.0f;
			return Input;
		}
		const double WantAccel = SpeedGain * (WantSpeed - View.BodySpeedCmS) + SollAccel;
		if (WantAccel > 0.0)
		{
			Input.Throttle = static_cast<float>(FMath::Clamp(WantAccel / FMath::Max(View.FullThrottleCmS2, 1.0), 0.0, 1.0));
		}
		else if (WantAccel < -40.0)
		{
			// Kleine Verzoegerungen erledigt die Motorbremse (Gas weg).
			Input.Brake = static_cast<float>(FMath::Clamp(-WantAccel / FMath::Max(View.FullBrakeCmS2, 1.0), 0.0, 1.0));
		}
		return Input;
	}

	FTransform ComputeWheelTransform(const FVector& WheelCenterCm, double SpinRad, double SteerRad)
	{
		// Erst rollen (um die Radachse Y), dann einschlagen (um die Hochachse) -
		// beides um die Radmitte: v' = C + Q (v - C).
		const FQuat Spin(FVector::YAxisVector, static_cast<float>(SpinRad));
		const FQuat Steer(FVector::ZAxisVector, static_cast<float>(SteerRad));
		const FQuat Q = Steer * Spin;
		return FTransform(Q, WheelCenterCm - Q.RotateVector(WheelCenterCm));
	}
}
