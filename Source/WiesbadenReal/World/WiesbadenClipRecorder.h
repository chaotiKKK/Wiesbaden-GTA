// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
//
// Aufnahmemodus -WbClip: fluessige Clips DIREKT aus dem Renderer.
//
// Warum es ihn gibt (27.09.2026): Tools/medien.py filmte den BILDSCHIRM
// (ffmpeg ddagrab). Das bricht ab, sobald ein anderes Fenster vor dem Spiel
// liegt - der Nutzer sitzt am Rechner, die Aufnahme ist dann leer. Der
// Ausweg ueber HighResShot-Serien schaffte nur ein Bild je Sekunde (Zeitraffer).
//
// Hier liest das Spiel jedes Bild selbst aus seinem Viewport
// (FScreenshotRequest + UGameViewportClient::OnScreenshotCaptured) und
// schreibt es im Hintergrund als JPG/PNG. Waehrend der Aufnahme laeuft die
// Spielzeit mit FESTEM Zeitschritt (FApp::SetUseFixedTimeStep): jedes
// gerenderte Bild ist genau 1/fps Spielzeit - wie lange das Auslesen dauert,
// sieht man dem Clip nicht an. Verdeckte Fenster stoeren nicht; nur ein
// MINIMIERTES Fenster rendert nicht (dann bricht die Aufnahme nach einer
// Frist ab, statt zu haengen).
//
//   -WbClip=<Name>          scharf; Bilder nach Saved/Clips/<Name>/
//   -WbClipSekunden=<s>     Laenge in Spielzeit (Default 8)
//   -WbClipFps=<n>          Bilder je Sekunde (Default 30, 1..60)
//   -WbClipTempo=<x>        Spielzeit je Clip-Sekunde (Default 1; 4 = Zeitraffer)
//   -WbClipDelay=<s>        Vorlauf nach "Stadt bereit" (Default 5)
//   -WbClipAt=<s>           fruehestens bei dieser Weltzeit starten
//   -WbClipPoseFile=<Pfad>  erste Posenzeile setzt die Kamera (Format wie
//                           -WbShotPoseFile); ohne: die Spielkamera
//   -WbClipPng              PNG statt JPG (verlustfrei, ~6x groesser)
//   -WbClipOhneHud          HUD waehrend der Aufnahme ausblenden
//   -WbClipNoQuit           nach der Aufnahme weiterspielen
//
// Im laufenden Spiel: Konsolenbefehl `WbClip [Sekunden] [Fps] [Name]`.
//
// FALLE: OnScreenshotCaptured ist ein GLOBALER Delegate. Solange er gebunden
// ist, schreibt auch HighResShot/`shot` KEINE Datei mehr (die Engine gibt die
// Pixel dann nur an den Delegate). Er ist deshalb ausschliesslich waehrend
// der Aufnahme gebunden.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "HAL/ThreadSafeCounter.h"
#include "WiesbadenClipRecorder.generated.h"

/** Einstellungen eines Clips - rein, ohne Engine, damit testbar. */
struct WIESBADENREAL_API FWbClipSettings
{
	FString Name;
	int32 Fps = 30;
	float Seconds = 8.0f;
	float Tempo = 1.0f;
	float DelaySeconds = 5.0f;
	float AtWorldSeconds = 0.0f;
	FString PoseFile;
	bool bPng = false;
	bool bHideHud = false;
	bool bNoQuit = false;

	/** Liest die -WbClip*-Schalter. false = kein -WbClip (Modus aus). */
	static bool FromCommandLine(const TCHAR* CommandLine, FWbClipSettings& Out);

	/** Nur [A-Za-z0-9_-]; alles andere wird '_' (der Name ist ein Ordner). */
	static FString SanitizeName(const FString& Raw);

	/** Zahl der Bilder: Sekunden x Fps, mindestens 1. */
	int32 FrameCount() const;

	/** Spielzeit je Bild: Tempo / Fps. */
	double FixedDeltaSeconds() const;

	/** Dateiname des Bildes Index (clip_00000.jpg ...). */
	FString FrameFileName(int32 Index) const;

	/** Startet die Aufnahme jetzt? ReadyWorldSeconds < 0 = Stadt noch nicht bereit. */
	bool ShouldStart(double ReadyWorldSeconds, double WorldSeconds) const;

	/** Erste echte Posenzeile (ohne Leerzeilen und #-Kommentare), sonst leer. */
	static FString FirstPoseLine(const FString& FileContent);

	/** Obergrenze fuer AWorldSettings::MaxUndilatedFrameTime waehrend der
	 *  Aufnahme. GEMESSEN am 27.09.2026: die Welt deckelt jeden Schritt auf
	 *  0,4 s (FixupDeltaSeconds) - ein 2-fps-Clip lief mit 0,4 statt 0,5 s je
	 *  Bild. Liegt der Clip-Schritt darueber, wird die Grenze knapp darueber
	 *  gehoben; sonst bleibt sie, wie sie ist. */
	static float RequiredMaxFrameTime(double FixedDeltaSeconds, float CurrentMaxFrameTime);
};

UCLASS()
class WIESBADENREAL_API UWiesbadenClipRecorder : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return Phase != EPhase::Aus; }

	/** Aufnahme sofort mit der aktuellen Kamera starten (Konsolenbefehl WbClip). */
	void StartNow(const FWbClipSettings& InSettings);

	bool IsRecording() const { return Phase == EPhase::Aufnahme; }

private:
	enum class EPhase : uint8
	{
		Aus,        // kein Clip geplant
		Warten,     // -WbClip: wartet auf Stadt bereit / Vorlauf / Weltzeit
		Aufnahme,   // je Tick ein Bild anfordern
		Schreiben,  // alle Bilder da, Hintergrund-Schreiben laeuft aus
		Fertig
	};

	void BeginCapture();
	void EndCapture(bool bAbgebrochen);
	void OnFrameCaptured(int32 Width, int32 Height, const TArray<FColor>& Bitmap);
	void Finish();

	FWbClipSettings Settings;
	EPhase Phase = EPhase::Aus;
	bool bFromCommandLine = false;

	/** Weltzeit, zu der die Stadt bereit war (< 0 = noch nicht). */
	double ReadyWorldSeconds = -1.0;
	bool bPoseApplied = false;

	FString OutputDir;
	int32 Requested = 0;
	int32 Received = 0;
	FIntPoint FrameSize = FIntPoint::ZeroValue;
	FThreadSafeCounter PendingWrites;
	FThreadSafeCounter FailedWrites;

	/** Echtzeit zu Beginn der Aufnahme bzw. des letzten eingegangenen Bildes. */
	double CaptureStartRealSeconds = 0.0;
	double LastFrameRealSeconds = 0.0;
	double CaptureStartWorldSeconds = 0.0;

	/** Weltzeit beim ersten und letzten Bild - Beleg fuer den festen
	 *  Zeitschritt: (letztes - erstes) / (Bilder - 1) muss Tempo / Fps sein. */
	double FirstFrameWorldSeconds = -1.0;
	double LastFrameWorldSeconds = -1.0;

	/** Zustand vor der Aufnahme - wird danach wiederhergestellt. */
	bool bPrevFixedTimeStep = false;
	double PrevFixedDeltaTime = 0.0;
	bool bPrevShowHud = true;
	float PrevMaxUndilatedFrameTime = -1.0f;

	FDelegateHandle CaptureHandle;
};
