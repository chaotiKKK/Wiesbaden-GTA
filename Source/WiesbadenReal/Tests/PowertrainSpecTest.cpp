// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Vehicles/WiesbadenPowertrainSpec.h"
#include "Vehicles/WiesbadenChaosCar.h"
#include "ChaosWheeledVehicleMovementComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPowertrainSpecKaefer1302Test,
    "WiesbadenReal.Vehicles.Powertrain.Spec",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPowertrainSpecKaefer1302Test::RunTest(const FString& Parameters)
{
    const FWiesbadenPowertrainSpec S = FWiesbadenPowertrainSpec::Kaefer1302();

    // Kanonische Werte des Kaefer 1302 (die bisher im Chaos-Ctor standen).
    TestEqual(TEXT("Max-Drehmoment 102 Nm"), S.MaxTorqueNm, 102.0f);
    TestEqual(TEXT("Hoechstdrehzahl 4600"), S.MaxRpm, 4600.0f);
    TestEqual(TEXT("Achsantrieb 4.375"), S.FinalDriveRatio, 4.375f);
    TestEqual(TEXT("Masse 970 kg (Leergewicht laut R&T + Fahrer)"), S.MassKg, 970.0f);
    if (TestEqual(TEXT("Vier Vorwaertsgaenge"), S.ForwardGearRatios.Num(), 4))
    {
        TestEqual(TEXT("1. Gang 3.80"), S.ForwardGearRatios[0], 3.80f);
        TestEqual(TEXT("4. Gang 0.89"), S.ForwardGearRatios[3], 0.89f);
    }

    // Das Drehmoment gipfelt bei 2600 U/min bei vollem Wert.
    TestTrue(TEXT("Gipfel-Drehmoment ~102 Nm bei 2600"),
        FMath::IsNearlyEqual(S.TorqueNmAt(2600.0f), 102.0f, 0.5f));

    // 2600 ist das Maximum ueber den ganzen Drehzahlbereich.
    float Best = 0.0f; float BestRpm = 0.0f;
    for (float Rpm = 800.0f; Rpm <= 4600.0f; Rpm += 100.0f)
    {
        const float T = S.TorqueNmAt(Rpm);
        if (T > Best) { Best = T; BestRpm = Rpm; }
    }
    TestEqual(TEXT("Drehmoment-Gipfel liegt bei 2600 U/min"), BestRpm, 2600.0f);

    // Ausserhalb der Kurve wird geklemmt (kein Extrapolieren).
    TestTrue(TEXT("Unter Leerlauf = erster Kurvenpunkt"),
        FMath::IsNearlyEqual(S.TorqueNmAt(0.0f), 102.0f * 0.72f, 0.5f));
    TestTrue(TEXT("Ueber Drehzahlgrenze = letzter Kurvenpunkt"),
        FMath::IsNearlyEqual(S.TorqueNmAt(9000.0f), 102.0f * 0.62f, 0.5f));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPowertrainSpecFeedsChaosCarTest,
    "WiesbadenReal.Vehicles.Powertrain.SpecFeedsChaosCar",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FPowertrainSpecFeedsChaosCarTest::RunTest(const FString& Parameters)
{
    // Das Class Default Object hat die im Ctor gesetzte Fahrzeug-Konfiguration -
    // kein Spawn, keine Welt noetig.
    const AWiesbadenChaosCar* CDO = GetDefault<AWiesbadenChaosCar>();
    if (!TestNotNull(TEXT("ChaosCar-CDO"), CDO)) { return false; }
    const UChaosWheeledVehicleMovementComponent* M =
        Cast<UChaosWheeledVehicleMovementComponent>(CDO->GetVehicleMovementComponent());
    if (!TestNotNull(TEXT("Chaos-Bewegungskomponente"), M)) { return false; }

    const FWiesbadenPowertrainSpec S = FWiesbadenPowertrainSpec::Kaefer1302();
    TestEqual(TEXT("Chaos MaxTorque == Spec"), M->EngineSetup.MaxTorque, S.MaxTorqueNm);
    TestEqual(TEXT("Chaos MaxRPM == Spec"), M->EngineSetup.MaxRPM, S.MaxRpm);
    TestEqual(TEXT("Chaos FinalRatio == Spec"), M->TransmissionSetup.FinalRatio, S.FinalDriveRatio);
    TestEqual(TEXT("Chaos Mass == Spec"), M->Mass, S.MassKg);
    if (TestEqual(TEXT("Gangzahl == Spec"),
        M->TransmissionSetup.ForwardGearRatios.Num(), S.ForwardGearRatios.Num()))
    {
        for (int32 I = 0; I < S.ForwardGearRatios.Num(); ++I)
        {
            TestEqual(FString::Printf(TEXT("Gang %d == Spec"), I + 1),
                M->TransmissionSetup.ForwardGearRatios[I], S.ForwardGearRatios[I]);
        }
    }
    return true;
}
