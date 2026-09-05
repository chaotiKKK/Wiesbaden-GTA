// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Vehicles/WiesbadenPowertrainSpec.h"

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
    TestEqual(TEXT("Masse 820 kg"), S.MassKg, 820.0f);
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
