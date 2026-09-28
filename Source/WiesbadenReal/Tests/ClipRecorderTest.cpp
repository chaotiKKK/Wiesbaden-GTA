// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
//
// Test des Clip-Modus (-WbClip), des Werkzeugs, nicht des Spiels: welche
// Schalter er liest, wann er startet und wie die Bilder heissen. Das
// eigentliche Auslesen aus dem Renderer braucht ein Fenster und wird im
// Probelauf belegt (Saved/Clips/<Name>/clip.json, "vollstaendig": true).

#include "Misc/AutomationTest.h"

#include "World/WiesbadenClipRecorder.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWbClipRecorderTest,
	"WiesbadenReal.World.ClipAufnahme",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWbClipRecorderTest::RunTest(const FString& Parameters)
{
	// -- 1. Ohne -WbClip bleibt der Modus aus ---------------------------------
	{
		FWbClipSettings S;
		TestFalse(TEXT("Ohne -WbClip kein Clip"),
			FWbClipSettings::FromCommandLine(TEXT("-WbShotWhenReady -WbClipFps=10"), S));
	}

	// -- 2. Alle Schalter kommen an -------------------------------------------
	{
		FWbClipSettings S;
		TestTrue(TEXT("-WbClip gefunden"), FWbClipSettings::FromCommandLine(
			TEXT("-WbClip=nerobergbahn -WbClipFps=24 -WbClipSekunden=12.5 -WbClipTempo=4 ")
			TEXT("-WbClipDelay=20 -WbClipAt=70 -WbClipPoseFile=C:\\posen.txt -WbClipPng -WbClipOhneHud -WbClipNoQuit"), S));
		TestEqual(TEXT("Name"), S.Name, FString(TEXT("nerobergbahn")));
		TestEqual(TEXT("Fps"), S.Fps, 24);
		TestEqual(TEXT("Sekunden"), S.Seconds, 12.5f);
		TestEqual(TEXT("Tempo"), S.Tempo, 4.0f);
		TestEqual(TEXT("Vorlauf"), S.DelaySeconds, 20.0f);
		TestEqual(TEXT("Weltzeit"), S.AtWorldSeconds, 70.0f);
		TestEqual(TEXT("Posendatei"), S.PoseFile, FString(TEXT("C:\\posen.txt")));
		TestTrue(TEXT("PNG"), S.bPng);
		TestTrue(TEXT("HUD aus"), S.bHideHud);
		TestTrue(TEXT("Nicht beenden"), S.bNoQuit);
		TestEqual(TEXT("12,5 s x 24 fps = 300 Bilder"), S.FrameCount(), 300);
		TestEqual(TEXT("Spielzeit je Bild = Tempo / Fps"), S.FixedDeltaSeconds(), 4.0 / 24.0, 1e-9);
	}

	// -- 3. Vorgaben und Grenzen ----------------------------------------------
	{
		FWbClipSettings S;
		FWbClipSettings::FromCommandLine(TEXT("-WbClip=a"), S);
		TestEqual(TEXT("Vorgabe 30 fps"), S.Fps, 30);
		TestEqual(TEXT("Vorgabe 8 s = 240 Bilder"), S.FrameCount(), 240);
		TestFalse(TEXT("Vorgabe JPG"), S.bPng);
		TestEqual(TEXT("JPG-Name"), S.FrameFileName(7), FString(TEXT("clip_00007.jpg")));

		FWbClipSettings::FromCommandLine(TEXT("-WbClip=a -WbClipFps=500 -WbClipTempo=100 -WbClipSekunden=-3"), S);
		TestEqual(TEXT("Fps hoechstens 60"), S.Fps, 60);
		TestEqual(TEXT("Tempo hoechstens 8"), S.Tempo, 8.0f);
		TestTrue(TEXT("Negative Laenge wird zur Mindestlaenge"), S.Seconds > 0.0f);
		TestTrue(TEXT("Mindestens ein Bild"), S.FrameCount() >= 1);

		S.bPng = true;
		TestEqual(TEXT("PNG-Name"), S.FrameFileName(12345), FString(TEXT("clip_12345.png")));
	}

	// -- 4. Der Name ist ein Ordner: nichts, was aus Saved/Clips herausfuehrt --
	{
		TestEqual(TEXT("Pfadzeichen werden ersetzt"),
			FWbClipSettings::SanitizeName(TEXT("..\\..\\Windows")), FString(TEXT("______Windows")));
		TestEqual(TEXT("Umlaut und Leerzeichen"),
			FWbClipSettings::SanitizeName(TEXT("Käfer 1")), FString(TEXT("K_fer_1")));
		TestEqual(TEXT("Leer wird 'clip'"), FWbClipSettings::SanitizeName(TEXT("  ")), FString(TEXT("clip")));
	}

	// -- 5. Wann die Aufnahme startet -----------------------------------------
	{
		FWbClipSettings S;
		S.DelaySeconds = 5.0f;
		S.AtWorldSeconds = 0.0f;
		TestFalse(TEXT("Stadt nicht bereit: nie"), S.ShouldStart(-1.0, 1000.0));
		TestFalse(TEXT("Im Vorlauf: noch nicht"), S.ShouldStart(20.0, 24.9));
		TestTrue(TEXT("Nach dem Vorlauf: ja"), S.ShouldStart(20.0, 25.0));

		// Nerobergbahn: die Wagen begegnen sich ~100 s nach Spielstart. Die
		// Weltzeit gewinnt, wenn sie spaeter liegt als der Vorlauf.
		S.AtWorldSeconds = 70.0f;
		TestFalse(TEXT("Vorlauf vorbei, Weltzeit noch nicht"), S.ShouldStart(20.0, 69.0));
		TestTrue(TEXT("Weltzeit erreicht"), S.ShouldStart(20.0, 70.0));
		TestFalse(TEXT("Weltzeit erreicht, Vorlauf nicht"), S.ShouldStart(68.0, 70.0));
	}

	// -- 6. Posendatei: die erste echte Zeile ---------------------------------
	{
		const FString Datei = TEXT("# Ausweiche steil quer\n\n   \n30, -91581, -153781, 37, -55, 4, 217, -55\n12, 0, 0\n");
		TestEqual(TEXT("Kommentare und Leerzeilen werden uebersprungen"),
			FWbClipSettings::FirstPoseLine(Datei), FString(TEXT("30, -91581, -153781, 37, -55, 4, 217, -55")));
		TestEqual(TEXT("Nur Kommentare: leer"),
			FWbClipSettings::FirstPoseLine(TEXT("# nichts\n")), FString());
	}

	// -- 7. Die Welt darf den Clip-Schritt nicht deckeln -----------------------
	{
		// GEMESSEN am 27.09.2026: 2 fps liefen mit 0,4 statt 0,5 s je Bild -
		// AWorldSettings::MaxUndilatedFrameTime (0,4 s) klemmt jeden Schritt.
		TestTrue(TEXT("0,5-s-Schritt hebt die 0,4-s-Grenze darueber"),
			FWbClipSettings::RequiredMaxFrameTime(0.5, 0.4f) >= 0.5f);
		TestTrue(TEXT("Zeitraffer 8 bei 10 fps (0,8 s) ebenso"),
			FWbClipSettings::RequiredMaxFrameTime(0.8, 0.4f) >= 0.8f);
		TestEqual(TEXT("Ein kleiner Schritt laesst die Grenze, wie sie ist"),
			FWbClipSettings::RequiredMaxFrameTime(1.0 / 30.0, 0.4f), 0.4f);
	}

	return true;
}
