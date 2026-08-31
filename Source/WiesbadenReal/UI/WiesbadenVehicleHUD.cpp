// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "UI/WiesbadenVehicleHUD.h"

#include "WiesbadenReal.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Vehicles/WiesbadenCar.h"
#include "Vehicles/WiesbadenCarLightsComponent.h"
#include "Vehicles/WiesbadenHelicopter.h"
#include "World/WiesbadenNerobergbahn.h"
#include "EngineUtils.h"
#include "GIS/WiesbadenWorldBuilder.h"
#include "World/WiesbadenCitySubsystem.h"
#include "UI/WiesbadenMinimap.h"

namespace
{
	// Gedeckte Instrumentenfarben - das HUD soll die Stadt nicht ueberstrahlen.
	const FLinearColor DialBackground(0.02f, 0.02f, 0.025f, 0.55f);
	const FLinearColor DialScale(0.72f, 0.74f, 0.78f, 0.9f);
	const FLinearColor DialText(0.92f, 0.93f, 0.95f, 1.0f);
	const FLinearColor NeedleColor(0.90f, 0.22f, 0.16f, 1.0f);
	const FLinearColor RpmSafe(0.35f, 0.72f, 0.45f, 1.0f);
	const FLinearColor RpmRed(0.88f, 0.24f, 0.18f, 1.0f);
	const FLinearColor TellTaleOff(0.20f, 0.21f, 0.23f, 0.7f);
	const FLinearColor IndicatorOn(0.20f, 0.85f, 0.30f, 1.0f);
	const FLinearColor HeadlightOn(0.35f, 0.75f, 0.95f, 1.0f);

	/** Ab hier faerbt sich das Drehzahlband rot. */
	constexpr float RedlineFraction = 0.82f;

	// Minikarte.
	const FLinearColor MapBackground(0.03f, 0.04f, 0.045f, 0.72f);
	const FLinearColor MapMinorRoad(0.62f, 0.64f, 0.68f, 0.95f);
	const FLinearColor MapMajorRoad(0.95f, 0.82f, 0.35f, 1.0f);
	const FLinearColor MapPlayer(0.95f, 0.25f, 0.20f, 1.0f);
	const FLinearColor MapBorder(0.55f, 0.57f, 0.62f, 0.85f);
}

AWiesbadenVehicleHUD::AWiesbadenVehicleHUD()
{
	PrimaryActorTick.bCanEverTick = false;
}

float AWiesbadenVehicleHUD::ComputeRpmFill(float Rpm, float IdleRpm, float MaxRpm)
{
	// Spanne vom Leerlauf bis zur Hoechstdrehzahl. Ohne diesen Bezug zeigte der
	// Balken im Leerlauf bereits einen sichtbaren Ausschlag.
	const float Span = MaxRpm - IdleRpm;
	if (Span <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	return FMath::Clamp((Rpm - IdleRpm) / Span, 0.0f, 1.0f);
}

float AWiesbadenVehicleHUD::ComputeNeedleAngleDegrees(float SpeedKmh, float MaxSpeedKmh, float SweepDegrees)
{
	if (MaxSpeedKmh <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	const float Fraction = FMath::Clamp(SpeedKmh / MaxSpeedKmh, 0.0f, 1.0f);
	return Fraction * SweepDegrees;
}

FString AWiesbadenVehicleHUD::FormatGear(int32 Gear)
{
	if (Gear < 0)
	{
		return TEXT("R");
	}
	if (Gear == 0)
	{
		return TEXT("N");
	}
	return FString::FromInt(Gear);
}

FString AWiesbadenVehicleHUD::FormatHeadlightMode(uint8 Mode)
{
	switch (static_cast<EWiesbadenHeadlightMode>(Mode))
	{
	case EWiesbadenHeadlightMode::Parking:	return TEXT("STAND");
	case EWiesbadenHeadlightMode::LowBeam:	return TEXT("ABBLEND");
	case EWiesbadenHeadlightMode::HighBeam:	return TEXT("FERN");
	default:								return TEXT("AUS");
	}
}

AWiesbadenCar* AWiesbadenVehicleHUD::GetPlayerCar() const
{
	const APlayerController* PC = GetOwningPlayerController();
	return PC ? Cast<AWiesbadenCar>(PC->GetPawn()) : nullptr;
}

void AWiesbadenVehicleHUD::DrawArc(float CenterX, float CenterY, float Radius,
	float StartDegrees, float EndDegrees, const FLinearColor& Color, float Thickness)
{
	// Canvas kennt keine Bogenprimitive - der Bogen entsteht aus Sehnen. Ein
	// Segment je 6 Grad ist bei Tachogroesse nicht mehr als Kante erkennbar.
	constexpr float StepDegrees = 6.0f;
	const int32 Steps = FMath::Max(1, FMath::CeilToInt(FMath::Abs(EndDegrees - StartDegrees) / StepDegrees));

	float PrevX = 0.0f;
	float PrevY = 0.0f;

	for (int32 Step = 0; Step <= Steps; ++Step)
	{
		const float Alpha = static_cast<float>(Step) / static_cast<float>(Steps);
		const float Degrees = FMath::Lerp(StartDegrees, EndDegrees, Alpha);
		const float Radians = FMath::DegreesToRadians(Degrees);

		const float X = CenterX + Radius * FMath::Sin(Radians);
		const float Y = CenterY - Radius * FMath::Cos(Radians);

		if (Step > 0)
		{
			DrawLine(PrevX, PrevY, X, Y, Color, Thickness);
		}

		PrevX = X;
		PrevY = Y;
	}
}

void AWiesbadenVehicleHUD::DrawSpeedometer(const AWiesbadenCar& Car, float CenterX, float CenterY, float Radius)
{
	// Skala beginnt links unten und laeuft ueber oben nach rechts unten.
	const float StartDegrees = -SpeedoSweepDegrees * 0.5f;
	const float EndDegrees = SpeedoSweepDegrees * 0.5f;

	DrawArc(CenterX, CenterY, Radius, StartDegrees, EndDegrees, DialScale, 2.0f);

	// Teilstriche alle 20 km/h, beschriftet alle 40.
	for (float Kmh = 0.0f; Kmh <= SpeedoMaxKmh + 0.1f; Kmh += 20.0f)
	{
		const float Degrees = StartDegrees + ComputeNeedleAngleDegrees(Kmh, SpeedoMaxKmh, SpeedoSweepDegrees);
		const float Radians = FMath::DegreesToRadians(Degrees);
		const float SinValue = FMath::Sin(Radians);
		const float CosValue = FMath::Cos(Radians);

		const bool bMajor = FMath::IsNearlyZero(FMath::Fmod(Kmh, 40.0f), 0.1f);
		const float Inner = Radius - (bMajor ? 14.0f : 8.0f);

		DrawLine(CenterX + Inner * SinValue, CenterY - Inner * CosValue,
			CenterX + Radius * SinValue, CenterY - Radius * CosValue,
			DialScale, bMajor ? 2.0f : 1.0f);

		if (bMajor)
		{
			const float LabelRadius = Radius - 30.0f;
			DrawText(FString::FromInt(FMath::RoundToInt(Kmh)),
				DialText,
				CenterX + LabelRadius * SinValue - 8.0f,
				CenterY - LabelRadius * CosValue - 7.0f,
				GEngine->GetSmallFont(), 1.0f);
		}
	}

	// Zeiger.
	const float SpeedKmh = Car.GetSpeedKmh();
	const float NeedleDegrees = StartDegrees + ComputeNeedleAngleDegrees(SpeedKmh, SpeedoMaxKmh, SpeedoSweepDegrees);
	const float NeedleRadians = FMath::DegreesToRadians(NeedleDegrees);

	DrawLine(CenterX, CenterY,
		CenterX + (Radius - 18.0f) * FMath::Sin(NeedleRadians),
		CenterY - (Radius - 18.0f) * FMath::Cos(NeedleRadians),
		NeedleColor, 3.0f);

	// Digitale Ablesung und Gang.
	DrawText(FString::Printf(TEXT("%3d km/h"), FMath::RoundToInt(SpeedKmh)),
		DialText, CenterX - 34.0f, CenterY + Radius * 0.35f, GEngine->GetMediumFont(), 1.0f);

	DrawText(FString::Printf(TEXT("Gang %s"), *FormatGear(Car.GetGear())),
		DialText, CenterX - 28.0f, CenterY + Radius * 0.55f, GEngine->GetSmallFont(), 1.0f);
}

void AWiesbadenVehicleHUD::DrawRpmBar(const AWiesbadenCar& Car, float X, float Y, float Width, float Height)
{
	DrawRect(DialBackground, X, Y, Width, Height);

	const float Fill = ComputeRpmFill(
		Car.GetEngineRpm(),
		Car.VehiclePhysics.EngineIdleRpm,
		Car.VehiclePhysics.EngineMaxRpm);

	const FLinearColor Color = Fill >= RedlineFraction ? RpmRed : RpmSafe;
	DrawRect(Color, X, Y, Width * Fill, Height);

	// Markierung des roten Bereichs.
	const float RedlineX = X + Width * RedlineFraction;
	DrawLine(RedlineX, Y, RedlineX, Y + Height, RpmRed, 2.0f);

	DrawText(TEXT("U/min"), DialText, X, Y - 14.0f, GEngine->GetSmallFont(), 1.0f);
}

void AWiesbadenVehicleHUD::DrawTellTales(const AWiesbadenCar& Car, float X, float Y)
{
	const UWiesbadenCarLightsComponent* Lights = Car.GetLights();
	if (!Lights)
	{
		return;
	}

	const EWiesbadenIndicatorMode Mode = Lights->GetIndicatorMode();
	const bool bBlinkOn = Lights->IsBlinkPhaseOn();

	// Die Kontrollleuchten blinken mit. Ein dauerhaft leuchtendes Symbol liesse
	// sich vom Warnblinken nicht unterscheiden - und genau das ist im Verkehr
	// die Information, auf die es ankommt.
	const bool bLeft = bBlinkOn
		&& (Mode == EWiesbadenIndicatorMode::Left || Mode == EWiesbadenIndicatorMode::Hazard);
	const bool bRight = bBlinkOn
		&& (Mode == EWiesbadenIndicatorMode::Right || Mode == EWiesbadenIndicatorMode::Hazard);

	DrawText(TEXT("<<"), bLeft ? IndicatorOn : TellTaleOff, X, Y, GEngine->GetMediumFont(), 1.0f);
	DrawText(TEXT(">>"), bRight ? IndicatorOn : TellTaleOff, X + 96.0f, Y, GEngine->GetMediumFont(), 1.0f);

	const EWiesbadenHeadlightMode Headlights = Lights->GetHeadlightMode();
	DrawText(FormatHeadlightMode(static_cast<uint8>(Headlights)),
		Headlights == EWiesbadenHeadlightMode::Off ? TellTaleOff : HeadlightOn,
		X + 24.0f, Y + 22.0f, GEngine->GetSmallFont(), 1.0f);
}

void AWiesbadenVehicleHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	const float Width = Canvas->ClipX;
	const float Height = Canvas->ClipY;

	if (const UWorld* HudWorld = GetWorld())
	{
		ElapsedSeconds += HudWorld->GetDeltaSeconds();
	}

	// Pausemenue zuerst: es liegt ueber allem und haelt die Zeit an.
	UpdatePauseMenu();
	if (bPaused)
	{
		DrawPauseMenu(Width, Height);
		return;
	}

	// F1 schaltet die Legende um (Flanke, damit ein Tastendruck einmal zaehlt).
	if (APlayerController* PC = GetOwningPlayerController())
	{
		const bool bKeyDown = PC->IsInputKeyDown(EKeys::F1);
		if (bKeyDown && !bLegendKeyHeld)
		{
			bShowControlLegend = !bShowControlLegend;

			// Beim Einschalten die Standzeit neu starten, sonst waere die
			// Legende nach Ablauf der Einblenddauer nicht mehr zurueckzuholen.
			if (bShowControlLegend)
			{
				ElapsedSeconds = 0.0f;
			}
		}
		bLegendKeyHeld = bKeyDown;
	}

	const AWiesbadenCar* Car = GetPlayerCar();

	// Legende zeichnen, solange sie eingeschaltet ist. Nach
	// ControlLegendSeconds blendet sie von selbst aus; F1 holt sie zurueck.
	// Sie erscheint AUCH zu Fuss - dort gab es bisher gar keine Anzeige.
	if (bShowControlLegend && ElapsedSeconds <= ControlLegendSeconds)
	{
		DrawControlLegend(Car != nullptr, 40.0f, Height - 210.0f);
	}
	else
	{
		// Ausgeblendet: nur noch der Hinweis, wie man sie zurueckholt.
		DrawText(TEXT("F1  Steuerung"), TellTaleOff, 40.0f, Height - 40.0f,
			GEngine->GetSmallFont(), 1.0f);
	}

	// Rechts unten, mit Abstand zum Rand - dort verdeckt das Instrument beim
	// Fahren am wenigsten.
	//
	// Die Minikarte belegt die Ecke; der Tacho rueckt deshalb um deren Breite
	// nach links. Uebereinander gezeichnet waren beide unlesbar.
	constexpr float MapDiameter = 260.0f;
	constexpr float MapMargin = 24.0f;

	// Minikarte und Strassenname zeichnen IMMER.
	//
	// Hier stand ein `if (!Car) return;` davor: zu Fuss blieb der Schirm
	// deshalb leer bis auf die Steuerungslegende - keine Karte, kein
	// Strassenname, keine Orientierung. Beide holen ihre Position ohnehin
	// aus dem Blickpunkt des Controllers und brauchen kein Fahrzeug.
	DrawMinimap(
		Width - MapMargin - MapDiameter * 0.5f,
		Height - MapMargin - MapDiameter * 0.5f,
		MapDiameter);

	DrawStreetName(Width * 0.5f, 28.0f);

	if (!Car)
	{
		// Zu Fuss: statt Tacho der Hinweis, was hier gerade moeglich ist.
		DrawFootPrompt(Width * 0.5f, Height - 120.0f);
		return;
	}

	const float Radius = FMath::Clamp(Height * 0.16f, 60.0f, 130.0f);
	const float CenterX = Width - MapDiameter - MapMargin * 2.0f - Radius - 20.0f;
	const float CenterY = Height - Radius - 70.0f;

	DrawSpeedometer(*Car, CenterX, CenterY, Radius);
	DrawRpmBar(*Car, CenterX - Radius, CenterY + Radius + 18.0f, Radius * 2.0f, 10.0f);
	DrawTellTales(*Car, CenterX - Radius * 0.5f, CenterY - Radius - 46.0f);
}


FString AWiesbadenVehicleHUD::BuildFootPrompt(
	double NearestVehicleCm, double NearestFunicularCm,
	double VehicleReachCm, double FunicularReachCm)
{
	// Das NAEHERE gewinnt, wenn beides in Reichweite ist - sonst blinkte an
	// der Talstation neben dem geparkten Wagen zweierlei durcheinander.
	const bool bVehicle = NearestVehicleCm >= 0.0 && NearestVehicleCm <= VehicleReachCm;
	const bool bFunicular = NearestFunicularCm >= 0.0 && NearestFunicularCm <= FunicularReachCm;

	if (bVehicle && bFunicular)
	{
		return NearestVehicleCm <= NearestFunicularCm
			? TEXT("F   Einsteigen")
			: TEXT("E   Nerobergbahn - mitfahren");
	}
	if (bVehicle)
	{
		return TEXT("F   Einsteigen");
	}
	if (bFunicular)
	{
		return TEXT("E   Nerobergbahn - mitfahren");
	}
	return FString();
}


void AWiesbadenVehicleHUD::DrawFootPrompt(float CenterX, float Y)
{
	const UWorld* HudWorld = GetWorld();
	const APlayerController* PC = GetOwningPlayerController();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!HudWorld || !Pawn)
	{
		return;
	}

	const FVector Here = Pawn->GetActorLocation();

	auto NearestOf = [&Here](const TArray<AActor*>& Actors) -> double
	{
		double Best = -1.0;
		for (const AActor* Actor : Actors)
		{
			if (!Actor)
			{
				continue;
			}
			const FVector Delta = Actor->GetActorLocation() - Here;
			const double Flat = FMath::Sqrt(Delta.X * Delta.X + Delta.Y * Delta.Y);
			if (Best < 0.0 || Flat < Best)
			{
				Best = Flat;
			}
		}
		return Best;
	};

	TArray<AActor*> Cars;
	TArray<AActor*> Helicopters;
	TArray<AActor*> Funiculars;
	UGameplayStatics::GetAllActorsOfClass(HudWorld, AWiesbadenCar::StaticClass(), Cars);
	UGameplayStatics::GetAllActorsOfClass(HudWorld, AWiesbadenHelicopter::StaticClass(), Helicopters);
	UGameplayStatics::GetAllActorsOfClass(HudWorld, AWiesbadenNerobergbahn::StaticClass(), Funiculars);
	Cars.Append(Helicopters);

	const FString Prompt = BuildFootPrompt(
		NearestOf(Cars), NearestOf(Funiculars),
		FootVehicleReachCm, FootFunicularReachCm);

	if (Prompt.IsEmpty())
	{
		return;
	}

	float TextWidth = 0.0f;
	float TextHeight = 0.0f;
	GetTextSize(Prompt, TextWidth, TextHeight, GEngine->GetLargeFont(), 1.0f);
	DrawText(Prompt, DialText, CenterX - TextWidth * 0.5f, Y,
		GEngine->GetLargeFont(), 1.0f);
}

void AWiesbadenVehicleHUD::GetControlLegendLines(bool bInVehicle, TArray<FString>& OutLines)
{
	OutLines.Reset();

	if (bInVehicle)
	{
		OutLines.Add(TEXT("W / Pfeil hoch      Gas"));
		OutLines.Add(TEXT("S / Pfeil runter    Bremse, im Stand rueckwaerts"));
		OutLines.Add(TEXT("A D / Pfeile        Lenken"));
		OutLines.Add(TEXT("Leertaste           Handbremse"));
		OutLines.Add(TEXT("Q E                 Blinker      H  Warnblinker"));
		OutLines.Add(TEXT("L                   Licht        R  Rueckwaertsgang"));
		OutLines.Add(TEXT("B                   Hupe         X  Aufblenden"));
		OutLines.Add(TEXT("Maus                Umsehen      F  Aussteigen"));
		OutLines.Add(TEXT("Gamepad   A Hupe  B Handbremse  X Rueckwaerts  Y Aussteigen"));
		OutLines.Add(TEXT("          LB RB Blinker   Kreuz hoch Licht   runter Warnblinker"));
		return;
	}

	OutLines.Add(TEXT("W A S D             Gehen"));
	OutLines.Add(TEXT("Umschalt links      Rennen"));
	OutLines.Add(TEXT("Leertaste           Springen"));
	OutLines.Add(TEXT("Maus / Pfeiltasten  Umsehen"));
	OutLines.Add(TEXT("Linke Maustaste     Kettensaege schwingen"));
	OutLines.Add(TEXT("F                   Einsteigen - auch in Verkehrsautos"));
	OutLines.Add(TEXT("E                   Nerobergbahn - mitfahren"));
}

void AWiesbadenVehicleHUD::DrawControlLegend(bool bInVehicle, float X, float Y)
{
	TArray<FString> Lines;
	GetControlLegendLines(bInVehicle, Lines);

	const int32 LineCount = Lines.Num();

	constexpr float LineHeight = 16.0f;
	constexpr float PaddingX = 12.0f;
	constexpr float PaddingY = 10.0f;
	constexpr float BoxWidth = 380.0f;

	const float BoxHeight = LineCount * LineHeight + PaddingY * 2.0f + LineHeight;

	DrawRect(DialBackground, X - PaddingX, Y - PaddingY, BoxWidth, BoxHeight);

	DrawText(bInVehicle ? TEXT("STEUERUNG - FAHRZEUG") : TEXT("STEUERUNG - ZU FUSS"),
		DialText, X, Y, GEngine->GetSmallFont(), 1.0f);

	for (int32 Index = 0; Index < LineCount; ++Index)
	{
		DrawText(Lines[Index], DialScale, X, Y + (Index + 1) * LineHeight,
			GEngine->GetSmallFont(), 1.0f);
	}
}

void AWiesbadenVehicleHUD::GetPauseMenuEntries(TArray<FString>& OutEntries)
{
	OutEntries.Reset();
	OutEntries.Add(TEXT("Weiterspielen"));
	OutEntries.Add(TEXT("Steuerung einblenden"));
	// Entwicklerbefehle. Ohne sie kostet jede Pruefung eine Fahrt quer durch
	// die Stadt - der Weg zur Platter Strasse dauert im Spiel Minuten.
	OutEntries.Add(TEXT("Entwickler: zurueck zur Platter Strasse 146"));
	OutEntries.Add(TEXT("Entwickler: zur Nerobergbahn"));
	OutEntries.Add(TEXT("Entwickler: Verkehr an/aus"));
	OutEntries.Add(TEXT("Spiel beenden"));
}

void AWiesbadenVehicleHUD::UpdatePauseMenu()
{
	APlayerController* PC = GetOwningPlayerController();
	UWorld* HudWorld = GetWorld();
	if (!PC || !HudWorld)
	{
		return;
	}

	auto Edge = [PC](const FKey& Key, bool& bHeld) -> bool
	{
		const bool bDown = PC->IsInputKeyDown(Key);
		const bool bPressed = bDown && !bHeld;
		bHeld = bDown;
		return bPressed;
	};

	// Escape oder Start am Gamepad schaltet um.
	if (Edge(EKeys::Escape, bPauseKeyHeld)
		|| PC->IsInputKeyDown(EKeys::Gamepad_Special_Right))
	{
		bPaused = !bPaused;
		PauseSelection = 0;

		// Die Zeit wirklich anhalten. Ein Menue, hinter dem der Verkehr
		// weiterfaehrt, ist keine Pause - und beim Zuruecksetzen der Position
		// waere die Stadt sonst schon woanders.
		PC->SetPause(bPaused);
		PC->bShowMouseCursor = bPaused;
	}

	if (!bPaused)
	{
		return;
	}

	TArray<FString> Entries;
	GetPauseMenuEntries(Entries);

	if (Edge(EKeys::Up, bMenuUpHeld) || Edge(EKeys::W, bMenuUpHeld))
	{
		PauseSelection = (PauseSelection + Entries.Num() - 1) % Entries.Num();
	}
	if (Edge(EKeys::Down, bMenuDownHeld) || Edge(EKeys::S, bMenuDownHeld))
	{
		PauseSelection = (PauseSelection + 1) % Entries.Num();
	}
	if (Edge(EKeys::Enter, bMenuEnterHeld))
	{
		ActivatePauseEntry(PauseSelection);
	}
}

void AWiesbadenVehicleHUD::ActivatePauseEntry(int32 Index)
{
	APlayerController* PC = GetOwningPlayerController();
	UWorld* HudWorld = GetWorld();
	if (!PC || !HudWorld)
	{
		return;
	}

	auto Unpause = [this, PC]()
	{
		bPaused = false;
		PC->SetPause(false);
		PC->bShowMouseCursor = false;
	};

	switch (Index)
	{
	case 0:
		Unpause();
		break;

	case 1:
		bShowControlLegend = true;
		ElapsedSeconds = 0.0f;
		Unpause();
		break;

	case 2:
	case 3:
	{
		// Zuruecksetzen an einen festen Ort.
		//
		// Die Platter Strasse 146 ist der Spielerstart; die Nerobergbahn
		// liegt rund 250 m davon entfernt und war bislang nur durch Hinfahren
		// erreichbar.
		const FVector Target = (Index == 2)
			? FVector(-121474.0, -119312.0, 11347.0)
			: FVector(-104083.0, -137317.0, 8800.0);

		if (APawn* Pawn = PC->GetPawn())
		{
			// Erst hoch genug ansetzen und dann fallen lassen: die Zielhoehe
			// stammt aus dem Protokoll und muss nicht auf den Zentimeter
			// stimmen. Ein Punkt IM Boden liesse den Wagen steckenbleiben.
			Pawn->SetActorLocation(Target + FVector(0.0, 0.0, 300.0),
				/*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
		}
		Unpause();
		break;
	}

	case 4:
		if (UWiesbadenCitySubsystem* City = HudWorld->GetSubsystem<UWiesbadenCitySubsystem>())
		{
			// Verkehr aus: die schnellste Art zu pruefen, ob ein Ruckler vom
			// Verkehr kommt oder von der Darstellung.
			const float Density = City->TrafficSimulation.GetDensity();
			City->TrafficSimulation.Settings.TrafficDensity = (Density > 0.01f) ? 0.0f : 0.5f;
		}
		Unpause();
		break;

	case 5:
		FPlatformMisc::RequestExit(false);
		break;

	default:
		break;
	}
}

void AWiesbadenVehicleHUD::DrawPauseMenu(float Width, float Height)
{
	TArray<FString> Entries;
	GetPauseMenuEntries(Entries);

	constexpr float LineHeight = 26.0f;
	constexpr float BoxWidth = 520.0f;
	const float BoxHeight = Entries.Num() * LineHeight + 96.0f;

	const float X = (Width - BoxWidth) * 0.5f;
	const float Y = (Height - BoxHeight) * 0.5f;

	// Den ganzen Schirm abdunkeln, damit das Menue lesbar ist.
	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), 0.0f, 0.0f, Width, Height);
	DrawRect(DialBackground, X, Y, BoxWidth, BoxHeight);

	DrawText(TEXT("PAUSE"), DialText, X + 24.0f, Y + 20.0f, GEngine->GetLargeFont(), 1.0f);

	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const bool bSelected = (Index == PauseSelection);
		const FString Line = (bSelected ? TEXT("> ") : TEXT("  ")) + Entries[Index];
		DrawText(Line, bSelected ? IndicatorOn : DialScale,
			X + 24.0f, Y + 60.0f + Index * LineHeight,
			GEngine->GetMediumFont(), 1.0f);
	}

	DrawText(TEXT("Pfeile waehlen   Eingabe bestaetigen   Esc schliesst"),
		TellTaleOff, X + 24.0f, Y + BoxHeight - 24.0f, GEngine->GetSmallFont(), 1.0f);
}

const FRoadNetwork* AWiesbadenVehicleHUD::FindRoadNetwork()
{
	if (CachedRoadNetwork)
	{
		return CachedRoadNetwork;
	}

	UWorld* HudWorld = GetWorld();
	if (!HudWorld)
	{
		return nullptr;
	}

	for (TActorIterator<AWiesbadenWorldBuilder> It(HudWorld); It; ++It)
	{
		if (!It->RoadNetwork.Segments.IsEmpty())
		{
			CachedRoadNetwork = &It->RoadNetwork;
			break;
		}
	}

	return CachedRoadNetwork;
}

void AWiesbadenVehicleHUD::DrawMinimap(float CenterX, float CenterY, float Diameter)
{
	const FRoadNetwork* Network = FindRoadNetwork();
	if (!Network || !Canvas)
	{
		return;
	}

	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		return;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);

	// Bezugspunkt ist das gesteuerte Fahrzeug, nicht die Kamera: Der
	// Verfolgerarm haengt mehrere Meter hinter dem Fahrzeug, und die Karte
	// wuerde sonst spuerbar hinterherlaufen.
	FVector MapCentre = ViewLocation;
	double MapYaw = ViewRotation.Yaw;
	if (const APawn* Pawn = PC->GetPawn())
	{
		MapCentre = Pawn->GetActorLocation();
		MapYaw = Pawn->GetActorRotation().Yaw;
	}

	const float Radius = Diameter * 0.5f;

	// Runder Hintergrund.
	//
	// Ein Rechteck haette Ecken gezeigt, die nicht zur Karte gehoeren - die
	// Linien werden am Kreis abgeschnitten, der Hintergrund muss dieselbe Form
	// haben. Gezeichnet als Faecher aus schmalen Streifen, weil Canvas keine
	// gefuellte Scheibe kennt.
	constexpr int32 DiscSteps = 48;
	for (int32 Step = 0; Step < DiscSteps; ++Step)
	{
		const float A0 = 2.0f * PI * Step / DiscSteps;
		const float A1 = 2.0f * PI * (Step + 1) / DiscSteps;

		FCanvasTriangleItem Wedge(
			FVector2D(CenterX, CenterY),
			FVector2D(CenterX + FMath::Cos(A0) * Radius, CenterY + FMath::Sin(A0) * Radius),
			FVector2D(CenterX + FMath::Cos(A1) * Radius, CenterY + FMath::Sin(A1) * Radius),
			GWhiteTexture);
		Wedge.SetColor(MapBackground);
		Canvas->DrawItem(Wedge);
	}

	DrawArc(CenterX, CenterY, Radius, 0.0f, 360.0f, MapBorder, 2.0f);

	FMinimapSettings Settings;
	Settings.DiameterPx = Diameter;

	TArray<FMinimapLine> Lines;
	FWiesbadenMinimap::BuildLines(
		*Network, MapCentre, MapYaw, FVector2D(CenterX, CenterY), Settings, Lines);

	for (const FMinimapLine& Line : Lines)
	{
		// Ausserhalb des Kreises abschneiden - ohne das ragen die Linien in
		// das uebrige Bild hinein.
		const float StartDist = FVector2D::Distance(Line.Start, FVector2D(CenterX, CenterY));
		const float EndDist = FVector2D::Distance(Line.End, FVector2D(CenterX, CenterY));
		if (StartDist > Radius && EndDist > Radius)
		{
			continue;
		}

		DrawLine(Line.Start.X, Line.Start.Y, Line.End.X, Line.End.Y,
			Line.bMajor ? MapMajorRoad : MapMinorRoad, Line.Thickness);
	}

	// Spielerzeiger: ein Dreieck, das nach oben zeigt.
	constexpr float ArrowSize = 9.0f;
	DrawLine(CenterX, CenterY - ArrowSize, CenterX - ArrowSize * 0.6f, CenterY + ArrowSize * 0.6f,
		MapPlayer, 2.5f);
	DrawLine(CenterX, CenterY - ArrowSize, CenterX + ArrowSize * 0.6f, CenterY + ArrowSize * 0.6f,
		MapPlayer, 2.5f);
	DrawLine(CenterX - ArrowSize * 0.6f, CenterY + ArrowSize * 0.6f,
		CenterX + ArrowSize * 0.6f, CenterY + ArrowSize * 0.6f, MapPlayer, 2.5f);

	// Massstab: Der Umkreis in Metern, damit die Karte lesbar bleibt.
	const FString ScaleText = FString::Printf(TEXT("%.0f m"), Settings.RangeCm / 100.0);
	DrawText(ScaleText, DialScale, CenterX - Radius + 8.0f, CenterY + Radius - 20.0f,
		GEngine ? GEngine->GetSmallFont() : nullptr, 1.0f);
}

void AWiesbadenVehicleHUD::DrawStreetName(float CenterX, float Y)
{
	const FRoadNetwork* Network = FindRoadNetwork();
	if (!Network || !Canvas)
	{
		return;
	}

	APlayerController* PC = GetOwningPlayerController();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}

	// Nicht jedes Bild neu suchen: Die Suche laeuft ueber die Segmente im
	// Umkreis, und der Name aendert sich hoechstens im Sekundentakt.
	if (const UWorld* HudWorld = GetWorld())
	{
		StreetNameAge += HudWorld->GetDeltaSeconds();
	}

	if (StreetNameAge > 0.4f || CurrentStreetName.IsEmpty())
	{
		StreetNameAge = 0.0f;
		CurrentStreetName = FWiesbadenMinimap::FindStreetName(*Network, Pawn->GetActorLocation());
	}

	if (CurrentStreetName.IsEmpty())
	{
		return;
	}

	UFont* Font = GEngine ? GEngine->GetLargeFont() : nullptr;

	float TextWidth = 0.0f;
	float TextHeight = 0.0f;
	Canvas->TextSize(Font, CurrentStreetName, TextWidth, TextHeight);

	constexpr float PadX = 18.0f;
	constexpr float PadY = 6.0f;

	DrawRect(MapBackground,
		CenterX - TextWidth * 0.5f - PadX, Y - PadY,
		TextWidth + PadX * 2.0f, TextHeight + PadY * 2.0f);

	DrawText(CurrentStreetName, DialText, CenterX - TextWidth * 0.5f, Y, Font, 1.0f);
}
