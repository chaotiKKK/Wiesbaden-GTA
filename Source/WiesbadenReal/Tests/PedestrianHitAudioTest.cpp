// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Engine/World.h"
#include "Sound/SoundBase.h"
#include "World/WiesbadenCitySubsystem.h"

/**
 * Regressionstest der Passanten-Trefferklaenge.
 *
 * WOZU: Die Gewalt-Quellen (Ueberfahren, Saege, Schuss, Explosion) spielen
 * ihre Toene ueber UWiesbadenCitySubsystem::PlayPedestrianHitSound bzw.
 * PlayPedestrianBurstSound. Beide laden die Aufnahme per Pfad aus
 * /Game/Audio/Samples. Stimmt der Pfad nach einem Neu-Import nicht mehr
 * (umbenanntes Asset, neuer Ordner), faellt der Klang still aus - im Spiel
 * merkt man das nur, wenn man gerade einen Passanten trifft. Dieser Test
 * laedt genau die drei Pfade, die der Code benutzt, und ruft die Methoden
 * auf, damit ein Absturz beim Spawnen im Test auffaellt. Hoerbar wird der
 * Klang natuerlich erst im Spiel - dort protokollieren die Methoden jede
 * gespielte Aufnahme ("Passanten-Treffer: Aufnahme ... gespielt.").
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPedestrianHitAudioTest,
	"WiesbadenReal.Weapons.PedestrianHitAudio",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPedestrianHitAudioTest::RunTest(const FString& Parameters)
{
	// -- Die drei Aufnahmen liegen unter genau den Pfaden des Codes ----------
	const TCHAR* SampleNames[] = {
		TEXT("A_PedestrianBurst"),      // Zerplatzen: Ueberfahren, Saege
		TEXT("A_PedestrianHit"),        // leichter Treffer: Geschoss
		TEXT("A_PedestrianHitHeavy")    // schwerer Treffer: Explosion, Volltreffer
	};
	for (const TCHAR* Name : SampleNames)
	{
		const FString Path = FString::Printf(TEXT("/Game/Audio/Samples/%s.%s"), Name, Name);
		USoundBase* Sample = LoadObject<USoundBase>(nullptr, *Path);
		TestNotNull(*FString::Printf(TEXT("Aufnahme '%s' laedt (%s)"), Name, *Path), Sample);
	}

	// -- Die Abspielpfade selbst (Laden + Spawn) stuerzen nicht --------------
	// GetSubsystem kann in einer nackten Test-Welt nullptr liefern; dann
	// bleibt der Aufruf hier aus - die Pfad-Pruefung oben ist der Kern.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld*/ false);
	if (TestNotNull(TEXT("Test-Welt erstellt"), World))
	{
		if (UWiesbadenCitySubsystem* City = World->GetSubsystem<UWiesbadenCitySubsystem>())
		{
			const FVector Where(1200.0, -800.0, 150.0);
			City->PlayPedestrianHitSound(Where, /*bHeavy=*/false);
			City->PlayPedestrianHitSound(Where, /*bHeavy=*/true);
			City->PlayPedestrianBurstSound(Where);
		}
		World->DestroyWorld(false);
	}

	return true;
}
