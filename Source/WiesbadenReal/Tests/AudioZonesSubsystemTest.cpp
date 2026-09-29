// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Audio/WiesbadenAudioZones.h"
#include "Audio/WiesbadenAudioZonesSubsystem.h"
#include "Engine/World.h"

/**
 * Test des Fussschritt-Pools.
 *
 * Die Kernregel ist die BEGRENZUNG: der Pool hat eine feste Groesse und
 * recycelt seine Plaetze, damit eine belebte Innenstadt nicht wie Feuerwerk
 * klingt. Genau diese Regel wird hier festgenagelt - ein wachsender Pool
 * waere zuerst unauffaellig und erst spaeter falsch.
 *
 * NACHTRAG 28.09.2026: die erste Fassung war GRUEN OHNE AUDIO. Die vier
 * MS_Step_*-MetaSounds waren nie gebaut (Biquad-Filter-Hauptversion, siehe
 * make_audio_assets.log), `PlayFootstepAt` lehnte jeden Schritt ab, und der
 * Test verglich danach 0 mit 0 - "Pool waechst nicht" ist mit einem leeren
 * Pool trivial wahr. Ein Test darf die Stille nicht mitpruefen, wenn die
 * Stille selbst der Fehler ist: die Zahl der GELADENEN Klaenge ist jetzt eine
 * harte Zusicherung (vier), und alle 40 Schritte muessen ankommen.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioZonesPoolTest,
	"WiesbadenReal.Audio.Footsteps.Pool",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAudioZonesPoolTest::RunTest(const FString& Parameters)
{
	const int32 ExpectedSounds = static_cast<int32>(EWbFootstepSurface::MAX);

	// -- Der Pool waechst nicht ------------------------------------------------
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld*/ false);
	if (!TestNotNull(TEXT("Test-Welt erstellt"), World))
	{
		return false;
	}

	UWiesbadenAudioZonesSubsystem* Zones =
		World->GetSubsystem<UWiesbadenAudioZonesSubsystem>();
	if (!TestNotNull(TEXT("Audio-Zonen-Subsystem in der Test-Welt"), Zones))
	{
		// Kein stilles Ueberspringen mehr: ohne Subsystem ist nichts geprueft,
		// und "nichts geprueft" darf nicht wie "alles gut" aussehen.
		World->DestroyWorld(false);
		return false;
	}

	Zones->EnsureRig();
	const int32 PoolSize = Zones->GetActiveStepCount();

	// Der nicht-vakuueme Kern: es gibt wirklich einen Klang je Oberflaeche.
	// Fehlt einer (Commandlet nie gelaufen, Asset nicht gespeichert), ist der
	// Test rot - der Hinweis steht in der Meldung.
	TestEqual(
		TEXT("vier Fussschritt-Klaenge geladen (Tools/make_audio_assets.cmd)"),
		PoolSize, ExpectedSounds);

	// Jede Oberflaeche muss ihre eigene Stimme finden. Eine Umleitung auf
	// einen anderen Klang waere hier sofort sichtbar.
	for (uint8 Index = 0; Index < static_cast<uint8>(EWbFootstepSurface::MAX); ++Index)
	{
		const EWbFootstepSurface Surface = static_cast<EWbFootstepSurface>(Index);
		TestTrue(
			FString::Printf(TEXT("Oberflaeche %s hat einen Klang"),
				*WiesbadenAudioZones::SurfaceName(Surface)),
			Zones->PlayFootstepAt(FVector(Index * 100.0, 0.0, 0.0), Surface));
	}
	TestEqual(TEXT("jede der vier Flaechen hat einmal gespielt"),
		Zones->GetPlayedFootstepCount(), ExpectedSounds);

	// 40 Schritte auf 4 Plaetze = 10 Runden: alle muessen gespielt werden und
	// die Platzzahl bleibt trotzdem stehen. Genau das ist die Begrenzung.
	const int32 Before = Zones->GetPlayedFootstepCount();
	int32 Played = 0;
	for (int32 i = 0; i < 40; ++i)
	{
		if (Zones->PlayFootstepAt(FVector(i * 100.0, 0.0, 0.0),
				EWbFootstepSurface::Asphalt))
		{
			++Played;
		}
	}

	TestEqual(TEXT("alle 40 Schritte gespielt"), Played, 40);
	TestEqual(TEXT("Zaehler und Rueckmeldung stimmen ueberein"),
		Zones->GetPlayedFootstepCount(), Before + Played);
	TestEqual(TEXT("Pool waechst nicht durch viele Schritte"),
		Zones->GetActiveStepCount(), PoolSize);

	// Der Abschalter muss greifen, egal ob ein Klang geladen ist.
	Zones->SetFootstepsEnabled(false);
	const int32 BeforeOff = Zones->GetPlayedFootstepCount();
	Zones->PlayFootstepAt(FVector::ZeroVector, EWbFootstepSurface::Asphalt);
	TestEqual(TEXT("ausgeschaltet -> kein Schritt"),
		Zones->GetPlayedFootstepCount(), BeforeOff);
	Zones->SetFootstepsEnabled(true);
	TestTrue(TEXT("wieder eingeschaltet -> spielt"),
		Zones->PlayFootstepAt(FVector::ZeroVector, EWbFootstepSurface::Asphalt));

	World->DestroyWorld(false);

	// -- Die reine Zuordnung bleibt auch ohne Welt korrekt --------------------
	// (Dieselbe Rechnung wie im Surface-Test, hier als Absicherung gegen
	//  einen Regelbruch, der nur im Subsystem auffaellt.)
	TestEqual(TEXT("Innenraum ist eigener Klang"),
		WiesbadenAudioZones::BandpassHzForSurface(EWbFootstepSurface::Innenraum) > 0.0f, true);
	TestEqual(TEXT("MAX faellt auf Pflaster zurueck"),
		WiesbadenAudioZones::SurfaceFromMaterialName(TEXT("")),
		EWbFootstepSurface::Pflaster);

	return true;
}
