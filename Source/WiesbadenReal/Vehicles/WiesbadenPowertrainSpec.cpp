// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenPowertrainSpec.h"

float FWiesbadenPowertrainSpec::TorqueNmAt(float Rpm) const
{
    if (TorqueCurveNormalized.Num() == 0)
    {
        return 0.0f;
    }
    if (Rpm <= TorqueCurveNormalized[0].X)
    {
        return MaxTorqueNm * static_cast<float>(TorqueCurveNormalized[0].Y);
    }
    const int32 Last = TorqueCurveNormalized.Num() - 1;
    if (Rpm >= TorqueCurveNormalized[Last].X)
    {
        return MaxTorqueNm * static_cast<float>(TorqueCurveNormalized[Last].Y);
    }
    for (int32 I = 1; I <= Last; ++I)
    {
        const FVector2D& A = TorqueCurveNormalized[I - 1];
        const FVector2D& B = TorqueCurveNormalized[I];
        if (Rpm <= B.X)
        {
            const double T = (Rpm - A.X) / FMath::Max(B.X - A.X, 1.0);
            return MaxTorqueNm * static_cast<float>(FMath::Lerp(A.Y, B.Y, T));
        }
    }
    return MaxTorqueNm * static_cast<float>(TorqueCurveNormalized[Last].Y);
}

FWiesbadenPowertrainSpec FWiesbadenPowertrainSpec::Kaefer1302()
{
    // Werte identisch zu den bisher im Chaos-Ctor hartkodierten - der Chaos-
    // Wagen faehrt danach BYTE-GLEICH; nur das kinematische Modell aendert sich.
    FWiesbadenPowertrainSpec S;
    S.MaxTorqueNm = 102.0f;
    S.MaxRpm = 4600.0f;
    S.IdleRpm = 800.0f;
    S.TorqueCurveNormalized = {
        FVector2D(800.0, 0.72), FVector2D(1600.0, 0.90), FVector2D(2600.0, 1.00),
        FVector2D(3400.0, 0.95), FVector2D(4000.0, 0.85), FVector2D(4600.0, 0.62) };
    S.ForwardGearRatios = { 3.80f, 2.06f, 1.32f, 0.89f };
    S.ReverseGearRatio = 3.61f;
    S.FinalDriveRatio = 4.375f;
    S.MassKg = 820.0f;
    return S;
}
