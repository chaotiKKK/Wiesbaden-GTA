// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "UI/WiesbadenProfilOverlay.h"

/**
 * Die Profil-Tafel des HUDs: halb so gross und halbtransparent.
 *
 * Anlass (26.09.2026): die Engine-Statistik (stat fps/unit/game) deckte das
 * Bild zu - in Filmaufnahmen war die Szene darunter nicht zu erkennen. Die
 * Engine zeichnet ihre Tabellen mit festen Fonts und undurchsichtigen
 * Kacheln, darum ersetzt das HUD sie durch eine eigene Tafel. Hier faellt der
 * Auftrag als Testbedingung: HALBE Groesse (Skala 0.5, Breite wie Hoehe
 * exakt halb gegenueber Standardmasse) und 50 % Deckkraft.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWbProfilOverlayTest,
	"WiesbadenReal.UI.ProfilOverlay",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWbProfilOverlayTest::RunTest(const FString& Parameters)
{
	const FWbProfilOverlayStyle Stil;

	// Die Zielwerte des Auftrags als Festwerte: halb so gross, halbtransparent.
	TestEqual(TEXT("Textskala halb"), Stil.TextSkala, 0.5f);
	TestEqual(TEXT("Hintergrund halbtransparent"), Stil.HintergrundAlpha, 0.5f);

	// Halbe Groesse wirkt auf BEIDE Masse: bei Skala 0.5 ist die Tafel exakt
	// halb so breit und halb so hoch wie bei Skala 1.
	FWbProfilOverlayStyle Voll = Stil;
	Voll.TextSkala = 1.0f;
	const FVector2D Halb = WbProfilTafelGroesse(4, 200.0f, 12.0f, Stil);
	const FVector2D Ganz = WbProfilTafelGroesse(4, 200.0f, 12.0f, Voll);
	TestEqual(TEXT("halbe Breite"), Halb.X, Ganz.X * 0.5f);
	TestEqual(TEXT("halbe Hoehe"), Halb.Y, Ganz.Y * 0.5f);

	// Leeres Fenster: eine ehrliche Vorlauf-Zeile, keine Nullen, die wie
	// gemessene Werte aussahen.
	const TArray<FString> Leer = WbProfilZeilen(FWbFrameReport());
	TestEqual(TEXT("Vorlauf ist eine Zeile"), Leer.Num(), 1);
	TestTrue(TEXT("Vorlauf benannt"), Leer[0].Contains(TEXT("Vorlauf")));

	// Gefuelltes Fenster: die Zeilen tragen die erwarteten Masszahlen.
	FWbFrameReport R;
	R.FrameCount = 100;
	R.Fps = 60.0;
	R.MeanMs = 16.7;
	R.WorstMs = 41.2;
	R.MeanGameThreadMs = 8.1;
	R.MeanRenderThreadMs = 5.2;
	R.MeanGpuMs = 9.3;
	R.MeanSubsystemMs = 1.4;
	R.MeanTrafficMs = 0.6;
	R.MeanPedestrianMs = 0.2;
	R.SpikeCount = 2;
	R.HitchCount = 1;
	const TArray<FString> Zeilen = WbProfilZeilen(R);
	TestEqual(TEXT("vier Zeilen"), Zeilen.Num(), 4);
	TestTrue(TEXT("Bildrate drin"), Zeilen[0].Contains(TEXT("60 Bilder/s")));
	TestTrue(TEXT("schlechtestes drin"), Zeilen[0].Contains(TEXT("41.2")));
	TestTrue(TEXT("Strangzeiten drin"), Zeilen[1].Contains(TEXT("8.1"))
		&& Zeilen[1].Contains(TEXT("5.2")) && Zeilen[1].Contains(TEXT("9.3")));
	TestTrue(TEXT("Ausreisser drin"), Zeilen[3].Contains(TEXT("2"))
		&& Zeilen[3].Contains(TEXT("1")));

	// Leere Zeile duerfte nie ueber die Flaeche hinausragen: die Groesse
	// wachst monoton mit der Zeilenzahl.
	const FVector2D Fuenf = WbProfilTafelGroesse(5, 200.0f, 12.0f, Stil);
	TestTrue(TEXT("monoton in der Hoehe"), Fuenf.Y > Halb.Y);

	return true;
}
