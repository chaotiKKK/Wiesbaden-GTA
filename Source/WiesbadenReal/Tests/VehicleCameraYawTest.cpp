// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Vehicles/WiesbadenVehicleCameraComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleCameraYawTest,
	"WiesbadenReal.Vehicles.CameraYaw",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	// Spielfruehe: 60 Hz bei CameraResponse 4 (Vorgabe der Komponente).
	const float Dt = 1.0f / 60.0f;
	const float Response = 4.0f;
	const float Alpha = Dt * Response;   // 0,0667: FInterpTo-Anteil je Schritt

	/**
	 * Wie weit hat sich der Ausleger tatsaechlich gedreht? FindDeltaAngleDegrees
	 * ist die Masse fuer den zurueckgelegten Weg - unabhaengig davon, wie die
	 * Komponente rechnet.
	 */
	float Weg(float Von, float Nach)
	{
		return FMath::FindDeltaAngleDegrees(Von, Nach);
	}
}

bool FVehicleCameraYawTest::RunTest(const FString& Parameters)
{
	using Kamera = UWiesbadenVehicleCameraComponent;

	// 1) VOLLER KREIS: von jedem Startwinkel zwei Grad nach rechts. Die
	// Bewegung muss ueberall +2*alpha sein, auch dort, wo der Wunschwert am
	// Vorzeichen vorbeigelaufen ist (Start 179 -> Ziel -179).
	//
	// GEMESSEN am 30.09.2026: die rohe Differenz ergibt dort -358 Grad, die
	// Kamera schwenkt also 358 Grad zurueck statt 2 Grad nach vorn.
	{
		int Verletzungen = 0;
		float GroessteAbweichung = 0.0f;
		for (int Ganz = -720; Ganz <= 720; ++Ganz)
		{
			const float Start = FRotator::NormalizeAxis(static_cast<float>(Ganz));
			const float Ziel = FRotator::NormalizeAxis(Start + 2.0f);
			const float Neu = Kamera::SmoothYaw(Start, Ziel, Dt, Response);
			const float Abweichung = FMath::Abs(Weg(Start, Neu) - 2.0f * Alpha);
			GroessteAbweichung = FMath::Max(GroessteAbweichung, Abweichung);
			Verletzungen += (Abweichung > 1e-3f) ? 1 : 0;
			TestTrue(FString::Printf(TEXT("Wert normalisiert bei Start %.0f"), Start),
				FMath::Abs(Neu) <= 180.0f);
		}
		TestEqual(TEXT("2-Grad-Schritt verletzt die Glaettung nie"), Verletzungen, 0);
		AddInfo(FString::Printf(
			TEXT("1441 Startwinkel, groesste Abweichung %.2e Grad, alpha = %.4f; rohe Differenz 179 -> -179 waere %.0f Grad"),
			GroessteAbweichung, Alpha, -179.0f - 179.0f));
	}

	// 2) Der Vorzeichenbruch selbst bis zum Einschwingen: zwei Grad Weg, nicht
	// 358. Die alte Rechnung brauchte dafuer 357,91 Grad.
	{
		float Yaw = 179.0f;
		float Zurueckgelegt = 0.0f;
		for (int Schritt = 0; Schritt < 120; ++Schritt)
		{
			const float Neu = Kamera::SmoothYaw(Yaw, -179.0f, Dt, Response);
			Zurueckgelegt += FMath::Abs(Weg(Yaw, Neu));
			Yaw = Neu;
		}
		AddInfo(FString::Printf(TEXT("Weg 179 -> -179: %.3f Grad"), Zurueckgelegt));
		TestTrue(TEXT("Vorzeichenbruch schwenkt den kurzen Weg (< 3 Grad)"), Zurueckgelegt < 3.0f);
		TestTrue(FString::Printf(TEXT("Endwinkel %.4f"), Yaw), FMath::Abs(Yaw + 179.0f) < 0.01f);
	}

	// 3) POLE: genau 180 Grad Abstand. Beide Richtungen sind gleich lang;
	// welche gewaehlt wird, entscheidet die Eingabe, nicht der Pol. Der Wert
	// muss am Ende normalisiert sein - und zurueck auf 0 laufen, ohne Sprung.
	// (NormalizeAxis laesst -180 bewusst bei -180, deshalb kein festes Vorzeichen.)
	{
		const float Plus = Kamera::SmoothYaw(0.0f, 180.0f, 1.0f, 10.0f);
		const float Minus = Kamera::SmoothYaw(0.0f, -180.0f, 1.0f, 10.0f);
		AddInfo(FString::Printf(TEXT("Pol: 0 -> 180 ergibt %.3f, 0 -> -180 ergibt %.3f"), Plus, Minus));
		TestTrue(TEXT("Pol +180 trifft den Pol"), FMath::IsNearlyEqual(FMath::Abs(Plus), 180.0f, 1e-3f));
		TestTrue(TEXT("Pol -180 trifft den Pol"), FMath::IsNearlyEqual(FMath::Abs(Minus), 180.0f, 1e-3f));
		TestTrue(TEXT("Pol bleibt normalisiert"), FMath::Abs(Plus) <= 180.0f && FMath::Abs(Minus) <= 180.0f);
		TestTrue(TEXT("zurueck vom Pol auf 0"), FMath::IsNearlyZero(
			Kamera::SmoothYaw(Plus, 0.0f, 1.0f, 10.0f), 1e-3f));
		TestTrue(TEXT("zurueck vom Gegenpol auf 0"), FMath::IsNearlyZero(
			Kamera::SmoothYaw(Minus, 0.0f, 1.0f, 10.0f), 1e-3f));
	}

	// 4) AM POL KEIN PENDELN: nach dem Einschwingen darf sich in den letzten
	// 20 von 200 Schritten praktisch nichts mehr bewegen.
	{
		float Yaw = 0.0f;
		for (int Schritt = 0; Schritt < 200; ++Schritt)
		{
			Yaw = Kamera::SmoothYaw(Yaw, 180.0f, Dt, Response);
		}
		float Restweg = 0.0f;
		for (int Schritt = 0; Schritt < 20; ++Schritt)
		{
			const float Neu = Kamera::SmoothYaw(Yaw, 180.0f, Dt, Response);
			Restweg += FMath::Abs(Weg(Yaw, Neu));
			Yaw = Neu;
		}
		AddInfo(FString::Printf(TEXT("Pol-Endwinkel %.5f, Restweg %.6f Grad"), Yaw, Restweg));
		TestTrue(TEXT("Pol konvergiert"), FMath::Abs(Yaw) > 179.0f);
		TestTrue(TEXT("kein Pendeln am Pol"), Restweg < 0.01f);
	}

	// 5) Volle Reaction trifft jedes Ziel im Bereich exakt. Verglichen wird
	// gegen den normalisierten Zielwert, weil -180 und +180 dieselbe Lage sind.
	{
		float GroessteAbweichung = 0.0f;
		for (int Grad = -180; Grad <= 180; ++Grad)
		{
			const float Ziel = static_cast<float>(Grad);
			const float Erwartet = FRotator::NormalizeAxis(Ziel);
			GroessteAbweichung = FMath::Max(GroessteAbweichung, FMath::Abs(
				Kamera::SmoothYaw(0.0f, Ziel, 1.0f, 10.0f) - Erwartet));
		}
		TestTrue(FString::Printf(TEXT("volle Reaction, max. Abweichung %.2e"), GroessteAbweichung),
			GroessteAbweichung < 1e-3f);
	}

	// 6) Leeres Raster und Response 0: FInterpTo springt bei Speed <= 0 auf das
	// Ziel (gleiches Verhalten wie Pitch/Roll), bei DeltaTime 0 bewegt sich
	// nichts. Beides bewusst festgehalten, damit niemand die Sonderfaelle
	// umdeutet.
	{
		TestTrue(TEXT("DeltaTime 0 bewegt nichts"),
			FMath::IsNearlyZero(Weg(30.0f, Kamera::SmoothYaw(30.0f, 60.0f, 0.0f, Response)), 1e-4f));
		TestTrue(TEXT("Response 0 springt auf das Ziel (FInterpTo-Semantik)"),
			FMath::IsNearlyEqual(Kamera::SmoothYaw(30.0f, 60.0f, Dt, 0.0f), 60.0f, 1e-3f));
		TestTrue(TEXT("Response 0 am Vorzeichenbruch springt auf das Ziel"),
			FMath::IsNearlyEqual(Kamera::SmoothYaw(179.0f, -179.0f, Dt, 0.0f),
				FRotator::NormalizeAxis(-179.0f), 1e-3f));
	}

	return true;
}
