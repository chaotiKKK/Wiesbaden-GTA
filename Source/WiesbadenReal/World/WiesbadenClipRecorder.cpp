// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenClipRecorder.h"

#include "WiesbadenReal.h"
#include "World/WiesbadenCitySubsystem.h"

#include "Async/Async.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UnrealClient.h"

namespace
{
	/** Hoechstens so viele Bilder warten aufs Schreiben (je ~3,7 MB bei 720p). */
	constexpr int32 MaxPendingWrites = 24;

	/** Kommt so lange (Echtzeit) kein Bild, gilt die Aufnahme als gescheitert -
	 *  typisch: Fenster minimiert, der Viewport rendert nicht. */
	constexpr double StallTimeoutSeconds = 30.0;

	/** JPEG-Qualitaet: hoch, die GIFs entstehen daraus erst in medien.py. */
	constexpr int32 JpegQuality = 92;
}

// ----------------------------------------------------------------------------
// FWbClipSettings
// ----------------------------------------------------------------------------

FString FWbClipSettings::SanitizeName(const FString& Raw)
{
	FString Out;
	Out.Reserve(Raw.Len());
	for (const TCHAR C : Raw.TrimStartAndEnd())
	{
		const bool bOk = (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z')
			|| (C >= '0' && C <= '9') || C == '_' || C == '-';
		Out.AppendChar(bOk ? C : TEXT('_'));
	}
	return Out.IsEmpty() ? FString(TEXT("clip")) : Out;
}

bool FWbClipSettings::FromCommandLine(const TCHAR* CommandLine, FWbClipSettings& Out)
{
	FString Name;
	if (!FParse::Value(CommandLine, TEXT("WbClip="), Name))
	{
		return false;
	}
	Out = FWbClipSettings();
	Out.Name = SanitizeName(Name);

	FParse::Value(CommandLine, TEXT("WbClipFps="), Out.Fps);
	FParse::Value(CommandLine, TEXT("WbClipSekunden="), Out.Seconds);
	FParse::Value(CommandLine, TEXT("WbClipTempo="), Out.Tempo);
	FParse::Value(CommandLine, TEXT("WbClipDelay="), Out.DelaySeconds);
	FParse::Value(CommandLine, TEXT("WbClipAt="), Out.AtWorldSeconds);
	FParse::Value(CommandLine, TEXT("WbClipPoseFile="), Out.PoseFile);
	Out.bPng = FParse::Param(CommandLine, TEXT("WbClipPng"));
	Out.bHideHud = FParse::Param(CommandLine, TEXT("WbClipOhneHud"));
	Out.bNoQuit = FParse::Param(CommandLine, TEXT("WbClipNoQuit"));

	// Grenzen: 60 fps reichen fuer jedes GIF; ein Tempo ueber 8 macht aus
	// einem Bild 0,27 s Spielzeit - Fahrzeugphysik springt dann sichtbar.
	Out.Fps = FMath::Clamp(Out.Fps, 1, 60);
	Out.Seconds = FMath::Clamp(Out.Seconds, 0.1f, 600.0f);
	Out.Tempo = FMath::Clamp(Out.Tempo, 0.1f, 8.0f);
	Out.DelaySeconds = FMath::Max(0.0f, Out.DelaySeconds);
	Out.AtWorldSeconds = FMath::Max(0.0f, Out.AtWorldSeconds);
	return true;
}

int32 FWbClipSettings::FrameCount() const
{
	return FMath::Max(1, FMath::RoundToInt(Seconds * static_cast<float>(Fps)));
}

double FWbClipSettings::FixedDeltaSeconds() const
{
	return static_cast<double>(Tempo) / static_cast<double>(FMath::Max(1, Fps));
}

FString FWbClipSettings::FrameFileName(int32 Index) const
{
	return FString::Printf(TEXT("clip_%05d.%s"), Index, bPng ? TEXT("png") : TEXT("jpg"));
}

bool FWbClipSettings::ShouldStart(double ReadyWorldSeconds, double WorldSeconds) const
{
	if (ReadyWorldSeconds < 0.0)
	{
		return false;
	}
	return WorldSeconds >= ReadyWorldSeconds + DelaySeconds
		&& WorldSeconds >= AtWorldSeconds;
}

FString FWbClipSettings::FirstPoseLine(const FString& FileContent)
{
	TArray<FString> Lines;
	FileContent.ParseIntoArrayLines(Lines, true);
	for (const FString& Line : Lines)
	{
		const FString Trimmed = Line.TrimStartAndEnd();
		if (!Trimmed.IsEmpty() && !Trimmed.StartsWith(TEXT("#")))
		{
			return Trimmed;
		}
	}
	return FString();
}

// ----------------------------------------------------------------------------
// UWiesbadenClipRecorder
// ----------------------------------------------------------------------------

bool UWiesbadenClipRecorder::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UWiesbadenClipRecorder::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FWbClipSettings FromCmd;
	if (FWbClipSettings::FromCommandLine(FCommandLine::Get(), FromCmd))
	{
		Settings = FromCmd;
		bFromCommandLine = true;
		Phase = EPhase::Warten;
		UE_LOG(LogWbStreaming, Log,
			TEXT("WbClip: '%s' geplant - %d Bilder (%.1f s x %d fps, Tempo %.2f), Vorlauf %.1f s nach Stadt bereit%s."),
			*Settings.Name, Settings.FrameCount(), Settings.Seconds, Settings.Fps, Settings.Tempo,
			Settings.DelaySeconds,
			Settings.AtWorldSeconds > 0.0f
				? *FString::Printf(TEXT(", fruehestens bei Weltzeit %.1f s"), Settings.AtWorldSeconds)
				: TEXT(""));
	}
}

void UWiesbadenClipRecorder::Deinitialize()
{
	if (Phase == EPhase::Aufnahme)
	{
		EndCapture(true);
	}
	// Schreibauftraege halten nur Kopien der Pixel - trotzdem auslaufen lassen,
	// damit kein halbes Bild auf der Platte bleibt.
	while (PendingWrites.GetValue() > 0)
	{
		FPlatformProcess::Sleep(0.01f);
	}
	Super::Deinitialize();
}

TStatId UWiesbadenClipRecorder::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UWiesbadenClipRecorder, STATGROUP_Tickables);
}

void UWiesbadenClipRecorder::StartNow(const FWbClipSettings& InSettings)
{
	if (Phase == EPhase::Aufnahme || Phase == EPhase::Schreiben)
	{
		UE_LOG(LogWbStreaming, Warning, TEXT("WbClip: es laeuft schon eine Aufnahme ('%s')."), *Settings.Name);
		return;
	}
	Settings = InSettings;
	bFromCommandLine = false;
	bPoseApplied = true;   // interaktiv: die Kamera, die der Spieler gerade hat
	BeginCapture();
}

void UWiesbadenClipRecorder::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();

	switch (Phase)
	{
	case EPhase::Warten:
	{
		if (ReadyWorldSeconds < 0.0)
		{
			const UWiesbadenCitySubsystem* City = World->GetSubsystem<UWiesbadenCitySubsystem>();
			if (City && City->IsCityReady())
			{
				ReadyWorldSeconds = Now;
				UE_LOG(LogWbStreaming, Log, TEXT("WbClip: Stadt bereit bei Weltzeit %.1f s."), Now);
			}
		}
		// Die Pose sofort mit "bereit" setzen: der Vorlauf ist dann zugleich
		// die Zeit, in der Streaming und Belichtung am Zielort einschwingen.
		if (ReadyWorldSeconds >= 0.0 && !bPoseApplied)
		{
			bPoseApplied = true;
			if (!Settings.PoseFile.IsEmpty())
			{
				FString Content;
				const FString Pose = FFileHelper::LoadFileToString(Content, *Settings.PoseFile)
					? FWbClipSettings::FirstPoseLine(Content) : FString();
				UWiesbadenCitySubsystem* City = World->GetSubsystem<UWiesbadenCitySubsystem>();
				if (City && !Pose.IsEmpty())
				{
					City->ApplyClipPose(Pose);
					UE_LOG(LogWbStreaming, Log, TEXT("WbClip: Kamera aus Pose '%s'."), *Pose);
				}
				else
				{
					UE_LOG(LogWbStreaming, Warning,
						TEXT("WbClip: Posendatei nicht lesbar oder leer: %s - nehme die Spielkamera."),
						*Settings.PoseFile);
				}
			}
		}
		if (Settings.ShouldStart(ReadyWorldSeconds, Now))
		{
			BeginCapture();
		}
		break;
	}

	case EPhase::Aufnahme:
	{
		const double Real = FPlatformTime::Seconds();
		if (Real - LastFrameRealSeconds > StallTimeoutSeconds)
		{
			UE_LOG(LogWbStreaming, Error,
				TEXT("WbClip: seit %.0f s kein Bild mehr (%d von %d) - Fenster minimiert? Aufnahme abgebrochen."),
				Real - LastFrameRealSeconds, Received, Settings.FrameCount());
			EndCapture(true);
			break;
		}
		// Gegendruck statt verlorener Bilder: die Spielzeit steht, solange
		// hier gewartet wird (fester Zeitschritt), der Clip bleibt fluessig.
		while (PendingWrites.GetValue() >= MaxPendingWrites)
		{
			FPlatformProcess::Sleep(0.002f);
		}
		// Ein Bild je Tick. Die Anforderung wird in DIESEM Bild gerendert und
		// ausgelesen (Welt-Tick vor dem Zeichnen der Viewports), also stimmt
		// Bild n mit Spielzeit Start + n / fps ueberein.
		if (Requested < Settings.FrameCount() && Requested <= Received)
		{
			FScreenshotRequest::RequestScreenshot(false);
			++Requested;
		}
		break;
	}

	case EPhase::Schreiben:
		if (PendingWrites.GetValue() == 0)
		{
			Finish();
		}
		break;

	default:
		break;
	}
}

void UWiesbadenClipRecorder::BeginCapture()
{
	UWorld* World = GetWorld();
	UGameViewportClient* Viewport = GEngine ? GEngine->GameViewport : nullptr;
	if (!World || !Viewport)
	{
		UE_LOG(LogWbStreaming, Error, TEXT("WbClip: kein Spiel-Viewport - keine Aufnahme."));
		Phase = EPhase::Fertig;
		return;
	}

	OutputDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Clips") / Settings.Name);
	// Alte Bilder desselben Namens weg - sonst mischt ein kuerzerer Lauf mit
	// den Resten eines laengeren.
	IFileManager::Get().DeleteDirectory(*OutputDir, false, true);
	IFileManager::Get().MakeDirectory(*OutputDir, true);

	// ImageWrapper auf dem Spielstrang laden; die Schreibauftraege laufen im
	// Thread-Pool und sollen das Modul nur noch finden.
	FModuleManager::Get().LoadModule(TEXT("ImageWrapper"));

	Requested = 0;
	Received = 0;
	FrameSize = FIntPoint::ZeroValue;
	FirstFrameWorldSeconds = LastFrameWorldSeconds = -1.0;
	FailedWrites.Reset();
	CaptureStartRealSeconds = LastFrameRealSeconds = FPlatformTime::Seconds();
	CaptureStartWorldSeconds = World->GetTimeSeconds();

	bPrevFixedTimeStep = FApp::UseFixedTimeStep();
	PrevFixedDeltaTime = FApp::GetFixedDeltaTime();
	FApp::SetFixedDeltaTime(Settings.FixedDeltaSeconds());
	FApp::SetUseFixedTimeStep(true);

	if (Settings.bHideHud)
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (AHUD* Hud = PC->GetHUD())
			{
				bPrevShowHud = Hud->bShowHUD;
				Hud->bShowHUD = false;
			}
		}
	}

	CaptureHandle = UGameViewportClient::OnScreenshotCaptured().AddUObject(
		this, &UWiesbadenClipRecorder::OnFrameCaptured);

	Phase = EPhase::Aufnahme;
	UE_LOG(LogWbStreaming, Log,
		TEXT("WbClip: Aufnahme '%s' laeuft ab Weltzeit %.1f s - %d Bilder, %.4f s Spielzeit je Bild, nach %s."),
		*Settings.Name, CaptureStartWorldSeconds, Settings.FrameCount(), Settings.FixedDeltaSeconds(), *OutputDir);
}

void UWiesbadenClipRecorder::OnFrameCaptured(int32 Width, int32 Height, const TArray<FColor>& Bitmap)
{
	if (Phase != EPhase::Aufnahme || Bitmap.Num() != Width * Height || Width <= 0 || Height <= 0)
	{
		return;
	}
	const int32 Index = Received++;
	LastFrameRealSeconds = FPlatformTime::Seconds();
	if (const UWorld* World = GetWorld())
	{
		LastFrameWorldSeconds = World->GetTimeSeconds();
		if (Index == 0)
		{
			FirstFrameWorldSeconds = LastFrameWorldSeconds;
		}
	}
	if (FrameSize == FIntPoint::ZeroValue)
	{
		FrameSize = FIntPoint(Width, Height);
	}

	const FString Path = OutputDir / Settings.FrameFileName(Index);
	PendingWrites.Increment();
	Async(EAsyncExecution::ThreadPool,
		[this, Path, Width, Height, Pixels = TArray<FColor>(Bitmap)]()
		{
			const FImageView View(Pixels.GetData(), Width, Height, EGammaSpace::sRGB);
			if (!FImageUtils::SaveImageByExtension(*Path, View, JpegQuality))
			{
				FailedWrites.Increment();
			}
			PendingWrites.Decrement();
		});

	if (Received >= Settings.FrameCount())
	{
		EndCapture(false);
	}
}

void UWiesbadenClipRecorder::EndCapture(bool bAbgebrochen)
{
	UGameViewportClient::OnScreenshotCaptured().Remove(CaptureHandle);
	CaptureHandle.Reset();

	FApp::SetUseFixedTimeStep(bPrevFixedTimeStep);
	FApp::SetFixedDeltaTime(PrevFixedDeltaTime);

	if (Settings.bHideHud)
	{
		if (UWorld* World = GetWorld())
		{
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				if (AHUD* Hud = PC->GetHUD())
				{
					Hud->bShowHUD = bPrevShowHud;
				}
			}
		}
	}

	if (bAbgebrochen)
	{
		UE_LOG(LogWbStreaming, Warning, TEXT("WbClip: '%s' abgebrochen nach %d Bildern."), *Settings.Name, Received);
	}
	Phase = EPhase::Schreiben;
}

void UWiesbadenClipRecorder::Finish()
{
	const double RealSeconds = FPlatformTime::Seconds() - CaptureStartRealSeconds;
	const int32 Soll = Settings.FrameCount();
	const int32 Ok = Received - FailedWrites.GetValue();
	const bool bVollstaendig = Ok == Soll;
	// Gemessene Spielzeit je Bild: muss Tempo / Fps sein, sonst hat der feste
	// Zeitschritt nicht gegriffen (dann ruckelt der Clip, sobald das Auslesen
	// langsamer ist als die Bildrate).
	const double SchrittGemessen = Received > 1
		? (LastFrameWorldSeconds - FirstFrameWorldSeconds) / static_cast<double>(Received - 1) : 0.0;

	// Beipackzettel fuer medien.py: genug, um aus dem Ordner ohne Raten ein
	// GIF oder MP4 zu bauen.
	const FString Json = FString::Printf(
		TEXT("{\n  \"name\": \"%s\",\n  \"fps\": %d,\n  \"tempo\": %.3f,\n  \"bilder\": %d,\n  \"soll\": %d,\n")
		TEXT("  \"vollstaendig\": %s,\n  \"breite\": %d,\n  \"hoehe\": %d,\n  \"muster\": \"clip_%%05d.%s\",\n")
		TEXT("  \"weltzeit_start\": %.3f,\n  \"weltzeit_erstes_bild\": %.4f,\n  \"weltzeit_letztes_bild\": %.4f,\n")
		TEXT("  \"spielzeit_je_bild_soll\": %.5f,\n  \"spielzeit_je_bild_gemessen\": %.5f,\n  \"echtzeit_s\": %.1f\n}\n"),
		*Settings.Name, Settings.Fps, Settings.Tempo, Ok, Soll, bVollstaendig ? TEXT("true") : TEXT("false"),
		FrameSize.X, FrameSize.Y, Settings.bPng ? TEXT("png") : TEXT("jpg"),
		CaptureStartWorldSeconds, FirstFrameWorldSeconds, LastFrameWorldSeconds,
		Settings.FixedDeltaSeconds(), SchrittGemessen, RealSeconds);
	FFileHelper::SaveStringToFile(Json, *(OutputDir / TEXT("clip.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	UE_LOG(LogWbStreaming, Log,
		TEXT("WbClip: fertig - %d von %d Bildern (%dx%d), %.4f s Spielzeit je Bild (Soll %.4f), %.1f s Echtzeit, nach %s%s."),
		Ok, Soll, FrameSize.X, FrameSize.Y, SchrittGemessen, Settings.FixedDeltaSeconds(), RealSeconds, *OutputDir,
		bVollstaendig ? TEXT("") : TEXT(" - UNVOLLSTAENDIG"));

	Phase = EPhase::Fertig;

	if (bFromCommandLine && !Settings.bNoQuit)
	{
		if (UWorld* World = GetWorld())
		{
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				PC->ConsoleCommand(TEXT("quit"));
			}
		}
	}
}

// ----------------------------------------------------------------------------
// Konsolenbefehl: WbClip [Sekunden] [Fps] [Name]
// ----------------------------------------------------------------------------

static FAutoConsoleCommandWithWorldAndArgs GWbClipCommand(
	TEXT("WbClip"),
	TEXT("Nimmt einen Clip direkt aus dem Renderer auf: WbClip [Sekunden=8] [Fps=30] [Name=konsole]. ")
	TEXT("Bilder nach Saved/Clips/<Name>/, danach Tools/medien.py clip."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* World)
		{
			UWiesbadenClipRecorder* Recorder = World ? World->GetSubsystem<UWiesbadenClipRecorder>() : nullptr;
			if (!Recorder)
			{
				UE_LOG(LogWbStreaming, Warning, TEXT("WbClip: nur in einer Spielwelt."));
				return;
			}
			FWbClipSettings S;
			S.Name = TEXT("konsole");
			if (Args.Num() > 0) { S.Seconds = FMath::Clamp(FCString::Atof(*Args[0]), 0.1f, 600.0f); }
			if (Args.Num() > 1) { S.Fps = FMath::Clamp(FCString::Atoi(*Args[1]), 1, 60); }
			if (Args.Num() > 2) { S.Name = FWbClipSettings::SanitizeName(Args[2]); }
			Recorder->StartNow(S);
		}));
