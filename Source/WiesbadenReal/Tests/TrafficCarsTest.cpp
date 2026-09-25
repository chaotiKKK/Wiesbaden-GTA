// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Vehicles/WiesbadenTrafficCars.h"
#include "World/TrafficVehicleSpawnerComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInterface.h"

using namespace WiesbadenTrafficCars;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrafficCarsTypesTest,
	"WiesbadenReal.Vehicles.TrafficCars.Types",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTrafficCarsTypesTest::RunTest(const FString& Parameters)
{
	const TArray<FWbTrafficCarType>& All = Types();
	TestEqual(TEXT("drei Verkehrsfahrzeuge"), All.Num(), 3);
	for (const FWbTrafficCarType& T : All)
	{
		const double Length = T.FrontCm + T.RearCm;
		TestTrue(FString::Printf(TEXT("%s: Laenge 3,8-5,2 m (%.0f cm)"), T.Name, Length), Length > 380.0 && Length < 520.0);
		TestTrue(FString::Printf(TEXT("%s: Radstand 55-65 %% der Laenge"), T.Name),
			T.WheelbaseCm / Length > 0.55 && T.WheelbaseCm / Length < 0.65);
		TestTrue(FString::Printf(TEXT("%s: Lampen vorn vorn, hinten hinten, links (-Y)"), T.Name),
			T.HeadLampCm.X > 0.8 * T.FrontCm && T.TailLampCm.X < -0.8 * T.RearCm
			&& T.HeadLampCm.Y < 0.0 && T.TailLampCm.Y < 0.0
			&& FMath::Abs(T.HeadLampCm.Y) < 0.5 * T.BodyWidthCm && FMath::Abs(T.TailLampCm.Y) < 0.5 * T.BodyWidthCm);
		const FWiesbadenVehiclePhysics P = T.MakePhysics();
		TestTrue(FString::Printf(TEXT("%s: Frontantrieb, voller Tank, erster Gang"), T.Name),
			P.bFrontWheelDrive && P.HasFuel() && P.Gear == 1 && P.SpeedMetersPerS == 0.0f);
		TestEqual(FString::Printf(TEXT("%s: Radradius Physik = Mesh"), T.Name), P.WheelRadiusM, static_cast<float>(T.WheelRadiusCm / 100.0));
	}
	// Der T6 ist schwerer und traeger als der Golf.
	TestTrue(TEXT("Transporter schwerer als Golf"), All[2].Powertrain.MassKg > 1.8f * All[0].Powertrain.MassKg);

	// Typwahl: deterministisch, Verteilung nach den Gewichten.
	TArray<int32> Counts;
	Counts.SetNumZeroed(All.Num());
	constexpr int32 N = 20000;
	float Total = 0.0f;
	for (const FWbTrafficCarType& T : All)
	{
		Total += T.Weight;
	}
	for (int32 Id = 0; Id < N; ++Id)
	{
		const int32 Type = SelectType(Id);
		TestTrue(TEXT("Typ gueltig"), Type >= 0 && Type < All.Num());
		TestEqual(TEXT("gleiche Id -> gleicher Typ"), SelectType(Id), Type);
		Counts[FMath::Clamp(Type, 0, All.Num() - 1)]++;
	}
	for (int32 T = 0; T < All.Num(); ++T)
	{
		const float Share = static_cast<float>(Counts[T]) / N;
		TestTrue(FString::Printf(TEXT("%s: Anteil %.2f ~ %.2f"), All[T].Name, Share, All[T].Weight / Total),
			FMath::Abs(Share - All[T].Weight / Total) < 0.04f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrafficCarsWheelTransformTest,
	"WiesbadenReal.Vehicles.TrafficCars.WheelTransform",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTrafficCarsWheelTransformTest::RunTest(const FString& Parameters)
{
	const FVector Center(124.0, -71.0, 33.0);   // Golf vorn links
	const FTransform Rolled = ComputeWheelTransform(Center, 0.5, 0.0);
	TestTrue(TEXT("die Radmitte bleibt stehen"), Rolled.TransformPosition(Center).Equals(Center, 0.01));
	// Vorwaerts rollen: der oberste Punkt des Reifens wandert nach vorn (+X), der unterste nach hinten.
	const FVector Top = Rolled.TransformPosition(Center + FVector(0.0, 0.0, 33.0));
	const FVector Bottom = Rolled.TransformPosition(Center - FVector(0.0, 0.0, 33.0));
	TestTrue(FString::Printf(TEXT("oben nach vorn (%.1f)"), Top.X - Center.X), Top.X > Center.X + 10.0);
	TestTrue(FString::Printf(TEXT("unten nach hinten (%.1f)"), Bottom.X - Center.X), Bottom.X < Center.X - 10.0);
	TestTrue(TEXT("Rollen bleibt in der Radebene"), FMath::IsNearlyEqual(Top.Y, Center.Y, 0.01));

	// Einschlag wie die Gier: positiv = nach +Y (Unreal: rechts herum).
	const FTransform Steered = ComputeWheelTransform(Center, 0.0, FMath::DegreesToRadians(30.0));
	const FVector Front = Steered.TransformPosition(Center + FVector(30.0, 0.0, 0.0));
	TestTrue(FString::Printf(TEXT("Vorderkante schwenkt nach +Y (%.1f)"), Front.Y - Center.Y), Front.Y > Center.Y + 10.0);
	TestTrue(TEXT("Einschlag um die Radmitte"), Steered.TransformPosition(Center).Equals(Center, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrafficCarsDriverTest,
	"WiesbadenReal.Vehicles.TrafficCars.Driver",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTrafficCarsDriverTest::RunTest(const FString& Parameters)
{
	FWbTrafficDriverView View;
	View.BodySpeedCmS = 1000.0;
	View.SollSpeedCmS = 1000.0;
	View.PrevSollSpeedCmS = 1000.0;
	View.PursuitTargetXY = FVector2D(800.0, 0.0);

	// Genau auf Soll: kein Pedal, geradeaus.
	View.SollXY = FVector2D(0.0, 0.0);
	FWiesbadenVehiclePhysicsInput In = ComputeDriverInput(View);
	TestTrue(TEXT("auf Soll: weder Gas noch Bremse"), In.Throttle < 0.05f && In.Brake == 0.0f);
	TestTrue(TEXT("auf Soll: geradeaus"), FMath::Abs(In.Steering) < 0.01f);

	// Soll 3 m voraus: Gas. Soll 3 m zurueck: Bremse.
	View.SollXY = FVector2D(300.0, 0.0);
	In = ComputeDriverInput(View);
	TestTrue(FString::Printf(TEXT("hinterher: Gas (%.2f)"), In.Throttle), In.Throttle > 0.3f && In.Brake == 0.0f);
	View.SollXY = FVector2D(-300.0, 0.0);
	In = ComputeDriverInput(View);
	TestTrue(FString::Printf(TEXT("voraus: Bremse (%.2f)"), In.Brake), In.Brake > 0.3f && In.Throttle == 0.0f);

	// Ziel rechts (+Y): nach rechts lenken (positiv, wie die Gier).
	View.SollXY = FVector2D(0.0, 0.0);
	View.PursuitTargetXY = FVector2D(800.0, 200.0);
	In = ComputeDriverInput(View);
	TestTrue(FString::Printf(TEXT("Ziel rechts: Lenkung positiv (%.2f)"), In.Steering), In.Steering > 0.1f);

	// Soll steht, Karosserie rollt noch: bremsen; steht sie, festhalten.
	View.PursuitTargetXY = FVector2D(800.0, 0.0);
	View.SollSpeedCmS = 0.0;
	View.PrevSollSpeedCmS = 0.0;
	View.SollXY = FVector2D(10.0, 0.0);
	In = ComputeDriverInput(View);
	TestTrue(TEXT("Soll steht, noch in Fahrt: Bremse"), In.Brake > 0.2f && In.Throttle == 0.0f);
	View.BodySpeedCmS = 0.0;
	In = ComputeDriverInput(View);
	TestEqual(TEXT("im Stand: voll festhalten"), In.Brake, 1.0f);
	return true;
}

namespace
{
	/**
	 * Geschlossener Kreis ohne Welt: Soll faehrt eine Strecke (Gerade, dann
	 * 90-Grad-Rechtskurve mit 25 m Radius) mit SollSpeed ab, die Karosserie folgt
	 * mit Fahrer + Physik. Liefert die groesste seitliche Abweichung und die
	 * Zeit bis 45 km/h beim Anfahren.
	 */
	struct FDriveResult { double MaxOffsetCm = 0.0; double FinalGapCm = 0.0; double FinalSpeedCmS = 0.0; };

	FVector2D PathPoint(double S)
	{
		constexpr double Straight = 6000.0;
		constexpr double Radius = 2500.0;
		if (S <= Straight)
		{
			return FVector2D(S, 0.0);
		}
		const double Arc = FMath::Min((S - Straight) / Radius, 0.5 * PI);
		const FVector2D End = FVector2D(Straight + Radius * FMath::Sin(Arc), Radius - Radius * FMath::Cos(Arc));
		const double Rest = S - Straight - 0.5 * PI * Radius;
		return Rest > 0.0 ? End + FVector2D(0.0, Rest) : End;
	}

	FDriveResult Drive(const FWbTrafficCarType& Type, double SollSpeedCmS, double Seconds, double StopAfterSeconds)
	{
		FWiesbadenVehiclePhysics P = Type.MakePhysics();
		P.SpeedMetersPerS = static_cast<float>(SollSpeedCmS / 100.0);
		P.Gear = P.Powertrain.ForwardGearRatios.Num() - 2;
		FVector2D XY(0.0, 0.0);
		double Yaw = 0.0;
		double S = 0.0;
		double Speed = SollSpeedCmS;
		double Prev = Speed;
		FDriveResult R;
		const double Dt = 1.0 / 60.0;
		for (double T = 0.0; T < Seconds; T += Dt)
		{
			Prev = Speed;
			if (T > StopAfterSeconds)
			{
				Speed = FMath::Max(0.0, Speed - 500.0 * Dt);   // sanft anhalten wie an der Ampel
			}
			S += Speed * Dt;
			FWbTrafficDriverView View;
			View.BodyXY = XY;
			View.BodyYawRad = Yaw;
			View.BodySpeedCmS = P.SpeedMetersPerS * 100.0;
			View.SollXY = PathPoint(S);
			View.SollSpeedCmS = Speed;
			View.PrevSollSpeedCmS = Prev;
			View.PursuitTargetXY = PathPoint(S + 320.0 + FMath::Max(Speed, 0.0) * 0.55);
			View.Dt = Dt;
			View.UsableSteerRad = FMath::DegreesToRadians(FWiesbadenVehiclePhysics::ComputeUsableSteerAngleDeg(
				P.MaxSteerAngleDeg, P.SpeedMetersPerS, P.SteerFalloffSpeedMetersPerS));
			View.WheelbaseCm = Type.WheelbaseCm;
			View.FullBrakeCmS2 = P.BrakeForceN / P.Powertrain.MassKg * 100.0;
			View.FullThrottleCmS2 = 300.0;
			const FWiesbadenVehiclePhysicsInput In = ComputeDriverInput(View);
			FWiesbadenVehiclePhysicsOutput Out;
			P.Tick(In, static_cast<float>(Dt), Out);
			Yaw += Out.YawRateRadPerS * Dt;
			const FVector2D Forward(FMath::Cos(Yaw), FMath::Sin(Yaw));
			const FVector2D Right(-FMath::Sin(Yaw), FMath::Cos(Yaw));
			XY += (Forward * Out.ForwardSpeedMetersPerS + Right * Out.LateralVelocityMetersPerS) * (100.0 * Dt);
			R.MaxOffsetCm = FMath::Max(R.MaxOffsetCm, FVector2D::Distance(XY, PathPoint(S)));
		}
		R.FinalGapCm = FVector2D::Distance(XY, PathPoint(S));
		R.FinalSpeedCmS = P.SpeedMetersPerS * 100.0;
		return R;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrafficCarsFollowTest,
	"WiesbadenReal.Vehicles.TrafficCars.FollowsPath",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTrafficCarsFollowTest::RunTest(const FString& Parameters)
{
	// Jeder Typ faehrt mit der Spielerphysik die Strecke ab (Gerade, Kurve,
	// Gerade), haelt sich nah an der Sollposition und kommt am Ende zum Stehen.
	for (const FWbTrafficCarType& Type : Types())
	{
		const FDriveResult R = Drive(Type, 1100.0, 20.0, 12.0);   // 40 km/h, ab 12 s anhalten
		TestTrue(FString::Printf(TEXT("%s: bleibt nah an der Sollbahn (hoechstens %.0f cm ab)"), Type.Name, R.MaxOffsetCm),
			R.MaxOffsetCm < 250.0);
		TestTrue(FString::Printf(TEXT("%s: steht am Ende (%.0f cm/s)"), Type.Name, R.FinalSpeedCmS), R.FinalSpeedCmS < 20.0);
		TestTrue(FString::Printf(TEXT("%s: steht an der Sollposition (%.0f cm)"), Type.Name, R.FinalGapCm), R.FinalGapCm < 120.0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrafficCarsMeshDimsTest,
	"WiesbadenReal.Vehicles.TrafficCars.MeshDims",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTrafficCarsMeshDimsTest::RunTest(const FString& Parameters)
{
	// Katalog (C++) und Meshes (Tools/import_traffic_cars.py) muessen
	// zusammenpassen: die Raeder drehen um die Mitte ihrer Bounds, Physik
	// und Radstand kommen aus dem Katalog.
	for (const FWbTrafficCarType& Type : Types())
	{
		const UStaticMesh* Body = LoadObject<UStaticMesh>(nullptr, *BodyMeshPath(Type));
		if (!TestNotNull(*FString::Printf(TEXT("SM_%s_Body"), Type.Name), Body))
		{
			continue;
		}
		const FBox Box = Body->GetBoundingBox();
		TestTrue(FString::Printf(TEXT("%s: Front %.1f ~ %.1f cm"), Type.Name, Box.Max.X, Type.FrontCm),
			FMath::Abs(Box.Max.X - Type.FrontCm) < 4.0);
		TestTrue(FString::Printf(TEXT("%s: Heck %.1f ~ %.1f cm"), Type.Name, -Box.Min.X, Type.RearCm),
			FMath::Abs(-Box.Min.X - Type.RearCm) < 4.0);
		for (int32 W = 0; W < 4; ++W)
		{
			const UStaticMesh* Wheel = LoadObject<UStaticMesh>(nullptr, *WheelMeshPath(Type, W));
			if (!TestNotNull(*FString::Printf(TEXT("SM_%s_Wheel_%s"), Type.Name, WheelName(W)), Wheel))
			{
				continue;
			}
			const FVector C = Wheel->GetBounds().Origin;
			const double WantX = (IsFrontWheel(W) ? 0.5 : -0.5) * Type.WheelbaseCm;
			// Unreal: links = -Y.
			const bool bLeft = W == 0 || W == 2;
			TestTrue(FString::Printf(TEXT("%s %s: Achse bei X %.1f ~ %.1f"), Type.Name, WheelName(W), C.X, WantX),
				FMath::Abs(C.X - WantX) < 5.0);
			TestTrue(FString::Printf(TEXT("%s %s: Seite (Y %.1f)"), Type.Name, WheelName(W), C.Y),
				bLeft ? C.Y < -0.4 * Type.TrackCm : C.Y > 0.4 * Type.TrackCm);
			TestTrue(FString::Printf(TEXT("%s %s: Radmitte %.1f ~ Radius %.1f cm"), Type.Name, WheelName(W), C.Z, Type.WheelRadiusCm),
				FMath::Abs(C.Z - Type.WheelRadiusCm) < 1.5);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrafficCarsPaintTest,
	"WiesbadenReal.Vehicles.TrafficCars.Paint",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTrafficCarsPaintTest::RunTest(const FString& Parameters)
{
	// Je Fahrzeug ein Lack: deterministisch, nach Gewicht verteilt, und
	// unabhaengig vom Typ - sonst gaebe es z. B. nur schwarze Golfs.
	const TArray<FWbTrafficPaint>& All = Paints();
	TestTrue(TEXT("mindestens acht Lacke"), All.Num() >= 8);
	TestTrue(TEXT("Eintrag 0 = Werkslack"), All.Num() > 0 && All[0].bFactory);
	float Total = 0.0f;
	for (int32 P = 0; P < All.Num(); ++P)
	{
		Total += All[P].Weight;
		const FLinearColor CD = PaintCustomData(P);
		TestEqual(FString::Printf(TEXT("%s: umfaerben nur ohne Werkslack"), All[P].Name), CD.A, All[P].bFactory ? 0.0f : 1.0f);
		TestTrue(FString::Printf(TEXT("%s: linear 0..1"), All[P].Name),
			CD.R >= 0.0f && CD.R <= 1.0f && CD.G >= 0.0f && CD.G <= 1.0f && CD.B >= 0.0f && CD.B <= 1.0f);
	}
	// sRGB -> linear: Silber 184 ~ 0,48, nicht 0,72.
	const int32 Silber = All.IndexOfByPredicate([](const FWbTrafficPaint& P) { return FCString::Strcmp(P.Name, TEXT("Silber")) == 0; });
	if (TestTrue(TEXT("Silber im Faecher"), Silber != INDEX_NONE))
	{
		TestTrue(TEXT("Silber linear ~0,48"), FMath::IsNearlyEqual(PaintCustomData(Silber).R, 0.48f, 0.02f));
	}
	TestEqual(TEXT("ungueltiger Index = Werkslack"), PaintCustomData(-1).A, 0.0f);

	constexpr int32 N = 20000;
	TArray<int32> Counts;
	Counts.Init(0, All.Num());
	TArray<TSet<int32>> PaintsPerType;
	PaintsPerType.SetNum(Types().Num());
	int32 SameAsNeighbour = 0;
	for (int32 Id = 0; Id < N; ++Id)
	{
		const int32 P = SelectPaint(Id);
		if (!TestTrue(TEXT("Lack gueltig"), All.IsValidIndex(P)))
		{
			return false;
		}
		TestEqual(TEXT("gleiche Id -> gleicher Lack"), SelectPaint(Id), P);
		Counts[P]++;
		PaintsPerType[SelectType(Id)].Add(P);
		SameAsNeighbour += (Id > 0 && SelectPaint(Id - 1) == P) ? 1 : 0;
	}
	for (int32 P = 0; P < All.Num(); ++P)
	{
		const float Share = static_cast<float>(Counts[P]) / N;
		TestTrue(FString::Printf(TEXT("%s: Anteil %.3f ~ %.3f"), All[P].Name, Share, All[P].Weight / Total),
			FMath::Abs(Share - All[P].Weight / Total) < 0.02f);
	}
	for (int32 T = 0; T < PaintsPerType.Num(); ++T)
	{
		TestEqual(FString::Printf(TEXT("%s: jeder Lack kommt vor"), Types()[T].Name), PaintsPerType[T].Num(), All.Num());
	}
	TestTrue(FString::Printf(TEXT("Nachbar-Ids selten gleich lackiert (%d von %d)"), SameAsNeighbour, N),
		SameAsNeighbour < N / 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrafficCarsPaintMaterialTest,
	"WiesbadenReal.Vehicles.TrafficCars.PaintMaterial",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTrafficCarsPaintMaterialTest::RunTest(const FString& Parameters)
{
	// Lack nur an der Karosserie (M_WbTrafficCarLack mit Maske), nie an den
	// Raedern - dort blieben sonst Reifen und Felgen im Lackton.
	const UMaterialInterface* LackMaster = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/Vehicles/Traffic/Mats/M_WbTrafficCarLack.M_WbTrafficCarLack"));
	if (!TestNotNull(TEXT("M_WbTrafficCarLack"), LackMaster))
	{
		return false;
	}
	auto IsLack = [LackMaster](UMaterialInterface* M) { return M && M->GetBaseMaterial() == LackMaster; };
	for (const FWbTrafficCarType& Type : Types())
	{
		const UStaticMesh* Body = LoadObject<UStaticMesh>(nullptr, *BodyMeshPath(Type));
		if (!TestNotNull(*FString::Printf(TEXT("SM_%s_Body"), Type.Name), Body))
		{
			continue;
		}
		int32 Lack = 0;
		for (const FStaticMaterial& Slot : Body->GetStaticMaterials())
		{
			if (!IsLack(Slot.MaterialInterface))
			{
				continue;
			}
			++Lack;
			UTexture* Mask = nullptr;
			Slot.MaterialInterface->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("LackMaske")), Mask);
			TestTrue(FString::Printf(TEXT("%s %s: eigene Lackmaske"), Type.Name, *Slot.MaterialSlotName.ToString()),
				Mask && Mask->GetName().StartsWith(FString::Printf(TEXT("L_%s_Part"), Type.Name)));
			TestFalse(FString::Printf(TEXT("%s: Maske linear"), Type.Name), Mask && Mask->SRGB);
		}
		TestTrue(FString::Printf(TEXT("%s: Karosserie hat Lack-Schlitze (%d)"), Type.Name, Lack), Lack > 0);
		for (int32 W = 0; W < 4; ++W)
		{
			const UStaticMesh* Wheel = LoadObject<UStaticMesh>(nullptr, *WheelMeshPath(Type, W));
			if (!Wheel)
			{
				continue;
			}
			for (const FStaticMaterial& Slot : Wheel->GetStaticMaterials())
			{
				TestFalse(FString::Printf(TEXT("%s %s: Rad ohne Lack"), Type.Name, WheelName(W)), IsLack(Slot.MaterialInterface));
			}
		}
	}
	return true;
}
