// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "UI/WiesbadenProfilOverlay.h"

TArray<FString> WbProfilZeilen(const FWbFrameReport& Report)
{
	TArray<FString> Zeilen;

	// Vorlauf: das Fenster sammelt noch - eine ehrliche Zeile statt Nullen,
	// die wie gemessene Werte aussahen.
	if (Report.FrameCount <= 0)
	{
		Zeilen.Add(TEXT("Vorlauf - noch keine Messung"));
		return Zeilen;
	}

	Zeilen.Add(FString::Printf(TEXT("%.0f Bilder/s   %.1f ms   schlechtestes %.1f ms"),
		Report.Fps, Report.MeanMs, Report.WorstMs));
	Zeilen.Add(FString::Printf(TEXT("Spiel %.1f   Draw %.1f   GPU %.1f"),
		Report.MeanGameThreadMs, Report.MeanRenderThreadMs, Report.MeanGpuMs));
	Zeilen.Add(FString::Printf(TEXT("Stadt %.1f   Verkehr %.1f   Fussgaenger %.1f"),
		Report.MeanSubsystemMs, Report.MeanTrafficMs, Report.MeanPedestrianMs));
	Zeilen.Add(FString::Printf(TEXT("Ausreisser %d   Aussetzer %d"),
		Report.SpikeCount, Report.HitchCount));
	return Zeilen;
}

FVector2D WbProfilTafelGroesse(int32 ZeilenAnzahl, float RohTextBreite,
	float RohZeilenHoehe, const FWbProfilOverlayStyle& Stil)
{
	const float Zeilen = FMath::Max(0, ZeilenAnzahl);
	// Jedes Mass - auch Rand und Zeilenabstand - laeuft mit der Textskala:
	// bei Skala 0.5 ist die Tafel exakt halb so breit und halb so hoch.
	const float Hoehe = Zeilen * (RohZeilenHoehe + Stil.ZeilenAbstandPx) * Stil.TextSkala
		+ 2.0f * Stil.RandPx * Stil.TextSkala;
	const float Breite = RohTextBreite * Stil.TextSkala
		+ 2.0f * Stil.RandPx * Stil.TextSkala;
	return FVector2D(Breite, Hoehe);
}
