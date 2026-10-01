// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "UI/WiesbadenVehicleHUD.h"

#include "UI/WiesbadenOptions.h"
#include "GameFramework/GameUserSettings.h"
#include "Vehicles/WiesbadenFootPawn.h"
#include "Vehicles/WiesbadenHeliGunComponent.h"
#include "Vehicles/WiesbadenVehicleCameraComponent.h"
#include "Misc/ConfigCacheIni.h"

#include "WiesbadenReal.h"

#include "Audio/WiesbadenAudioSubsystem.h"
#include "CanvasItem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Core/WiesbadenDevActions.h"
#include "Core/WiesbadenInputMap.h"
#include "Core/WiesbadenPlayerController.h"
#include "Engine/Canvas.h"
#include "TextureResource.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "NPC/WiesbadenStoreMerchant.h"
#include "World/WiesbadenDennoShop.h"
#include "Vehicles/WiesbadenCar.h"
#include "Vehicles/WiesbadenBugTankPawn.h"
#include "Vehicles/WiesbadenVehicleControl.h"
#include "Vehicles/WiesbadenCarLightsComponent.h"
#include "Vehicles/WiesbadenHelicopter.h"
#include "World/WiesbadenNerobergbahn.h"
#include "EngineUtils.h"
#include "GIS/WiesbadenWorldBuilder.h"
#include "World/WiesbadenCitySubsystem.h"
#include "NPC/WiesbadenPoliceSubsystem.h"
#include "UI/WiesbadenMinimap.h"
#include "UI/WiesbadenProfilOverlay.h"
#include "Missions/WiesbadenMissionSubsystem.h"
#include "Missions/WiesbadenMissionTypes.h"
#include "Core/WiesbadenGameStateSubsystem.h"
#include "Store/WiesbadenStoreSubsystem.h"
#include "Store/WiesbadenStoreTypes.h"
#include "Store/WiesbadenStore.h"
#include "Engine/GameInstance.h"
#include "UI/WiesbadenWorldMapView.h"
#include "Engine/TextureRenderTarget2D.h"

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

	// Instrumenten-Gehaeuse und kuenstlicher Horizont.
	const FLinearColor BezelDark(0.06f, 0.07f, 0.085f, 1.0f);
	const FLinearColor BezelRim(0.42f, 0.46f, 0.52f, 1.0f);
	const FLinearColor DialFace(0.09f, 0.10f, 0.12f, 1.0f);
	const FLinearColor GaugeWarn(0.90f, 0.62f, 0.15f, 1.0f);
	const FLinearColor HorizonSky(0.22f, 0.52f, 0.82f, 1.0f);
	const FLinearColor HorizonGround(0.42f, 0.30f, 0.16f, 1.0f);
	const FLinearColor HorizonLine(0.96f, 0.97f, 1.0f, 1.0f);
	const FLinearColor AircraftMark(0.98f, 0.82f, 0.18f, 1.0f);
	const FLinearColor PanelEdge(0.34f, 0.58f, 0.68f, 0.95f);

	/** Ab hier faerbt sich das Drehzahlband rot. */
	constexpr float RedlineFraction = 0.82f;

	// Minikarte.
	const FLinearColor MapBackground(0.03f, 0.04f, 0.045f, 0.72f);
	const FLinearColor MapMinorRoad(0.62f, 0.64f, 0.68f, 0.95f);
	const FLinearColor MapMajorRoad(0.95f, 0.82f, 0.35f, 1.0f);
	const FLinearColor MapPlayer(0.95f, 0.25f, 0.20f, 1.0f);
	const FLinearColor MapBorder(0.55f, 0.57f, 0.62f, 0.85f);
	const FLinearColor MapWaypoint(0.25f, 0.90f, 0.55f, 1.0f);
}

FString AWiesbadenVehicleHUD::FormatWaterLevel(float Fuellstand, bool bSchieberOffen)
{
	const float Prozent = FMath::Clamp(Fuellstand, 0.0f, 1.0f) * 100.0f;
	// Auf 5 % gerundet: der Fuellstand aendert sich langsam (2 %/s Abfluss),
	// und eine zappelnde Einerstelle waere im Bild nur unruhig.
	const float Gerundet = FMath::RoundToFloat(Prozent / 5.0f) * 5.0f;
	return FString::Printf(TEXT("Wasserballast %.0f %% - Schieber %s"),
		Gerundet, bSchieberOffen ? TEXT("offen") : TEXT("zu"));
}

FString AWiesbadenVehicleHUD::FormatMapDistance(double DistanceCm)
{
	const double Metres = DistanceCm / 100.0;
	if (Metres < 1000.0)
	{
		return FString::Printf(TEXT("%.0f m"), Metres);
	}
	return FString::Printf(TEXT("%.1f km"), Metres / 1000.0);
}

AWiesbadenVehicleHUD::AWiesbadenVehicleHUD()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

void AWiesbadenVehicleHUD::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bHelicopterLessonActive || bHelicopterLessonOverlayVisible)
	{
		AWiesbadenHelicopter* Heli = LastHelicopterPawn.Get();
		if (!Heli || Heli != GetPlayerHelicopter() || Heli->IsDestroyed())
		{
			EndHelicopterLesson(FString());
		}
	}
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

FString AWiesbadenVehicleHUD::FormatHeading(float Degrees)
{
	float D = FMath::Fmod(Degrees, 360.0f);
	if (D < 0.0f)
	{
		D += 360.0f;
	}
	// Acht-Punkt-Rose (deutsch): N NO O SO S SW W NW. Runden auf 45-Grad-Sektor.
	static const TCHAR* Dirs[] = {
		TEXT("N"), TEXT("NO"), TEXT("O"), TEXT("SO"),
		TEXT("S"), TEXT("SW"), TEXT("W"), TEXT("NW") };
	const int32 Sector = FMath::RoundToInt(D / 45.0f) % 8;
	return FString::Printf(TEXT("%s %03d"), Dirs[Sector], FMath::RoundToInt(D) % 360);
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

FString AWiesbadenVehicleHUD::FormatTractionTellTale(bool bWheelSpin, bool bWheelLock)
{
	// Blockieren vor Radspin: beim Bremsen ist die Leuchte eine ABS-Anzeige,
	// beim Beschleunigen eine Antriebsschlupf-Anzeige. Beides zugleich kommt
	// nicht vor (nie Gas UND Bremse), die Reihenfolge macht sie nur eindeutig.
	if (bWheelLock) { return TEXT("ABS"); }
	if (bWheelSpin) { return TEXT("ASR"); }
	return FString();
}

float AWiesbadenVehicleHUD::AdvanceTellTaleHold(bool bActive, float HoldRemaining, float Dt, float HoldSeconds)
{
	if (bActive)
	{
		return FMath::Max(HoldRemaining, HoldSeconds);
	}
	return FMath::Max(0.0f, HoldRemaining - Dt);
}

IWiesbadenVehicleControl* AWiesbadenVehicleHUD::GetPlayerVehicleControl() const
{
	const APlayerController* PC = GetOwningPlayerController();
	return PC ? Cast<IWiesbadenVehicleControl>(PC->GetPawn()) : nullptr;
}

AWiesbadenHelicopter* AWiesbadenVehicleHUD::GetPlayerHelicopter() const
{
	const APlayerController* PC = GetOwningPlayerController();
	return PC ? Cast<AWiesbadenHelicopter>(PC->GetPawn()) : nullptr;
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

void AWiesbadenVehicleHUD::DrawSpeedometer(const IWiesbadenVehicleControl& Vehicle, float CenterX, float CenterY, float Radius)
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
	const float SpeedKmh = Vehicle.GetSpeedKmh();
	const float NeedleDegrees = StartDegrees + ComputeNeedleAngleDegrees(SpeedKmh, SpeedoMaxKmh, SpeedoSweepDegrees);
	const float NeedleRadians = FMath::DegreesToRadians(NeedleDegrees);

	DrawLine(CenterX, CenterY,
		CenterX + (Radius - 18.0f) * FMath::Sin(NeedleRadians),
		CenterY - (Radius - 18.0f) * FMath::Cos(NeedleRadians),
		NeedleColor, 3.0f);

	// Digitale Ablesung und Gang.
	DrawText(FString::Printf(TEXT("%3d km/h"), FMath::RoundToInt(SpeedKmh)),
		DialText, CenterX - 34.0f, CenterY + Radius * 0.35f, GEngine->GetMediumFont(), 1.0f);

	DrawText(FString::Printf(TEXT("Gang %s"), *FormatGear(Vehicle.GetGear())),
		DialText, CenterX - 28.0f, CenterY + Radius * 0.55f, GEngine->GetSmallFont(), 1.0f);
}

void AWiesbadenVehicleHUD::DrawRpmBar(const IWiesbadenVehicleControl& Vehicle, float X, float Y, float Width, float Height)
{
	DrawRect(DialBackground, X, Y, Width, Height);

	const float Fill = ComputeRpmFill(
		Vehicle.GetEngineRpm(),
		Vehicle.GetEngineIdleRpm(),
		Vehicle.GetEngineMaxRpm());

	const FLinearColor Color = Fill >= RedlineFraction ? RpmRed : RpmSafe;
	DrawRect(Color, X, Y, Width * Fill, Height);

	// Markierung des roten Bereichs.
	const float RedlineX = X + Width * RedlineFraction;
	DrawLine(RedlineX, Y, RedlineX, Y + Height, RpmRed, 2.0f);

	DrawText(TEXT("U/min"), DialText, X, Y - 14.0f, GEngine->GetSmallFont(), 1.0f);
}

void AWiesbadenVehicleHUD::DrawTellTales(const IWiesbadenVehicleControl& Vehicle, float X, float Y)
{
	const UWiesbadenCarLightsComponent* Lights = Vehicle.GetLights();
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

	// Traktions-/ABS-Kontrollleuchte: KONSUMIERT die Modell-Flags der Fahrphysik
	// (Radspin beim Anfahren, Blockieren beim Bremsen) - reine Anzeige, kein
	// Verhalten. Kurzes Nachleuchten gegen das Flackern im ABS-Puls; wenn nichts
	// schlupft, steht sie unauffaellig gedimmt als "TRC" da.
	constexpr float TractionHoldSeconds = 0.4f;
	const float Dt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0f;
	const FString NowLabel = FormatTractionTellTale(
		Vehicle.IsWheelSpinning(), Vehicle.IsWheelLocked());
	if (!NowLabel.IsEmpty())
	{
		TractionTellTaleLabel = NowLabel;
	}
	TractionTellTaleHold = AdvanceTellTaleHold(
		!NowLabel.IsEmpty(), TractionTellTaleHold, Dt, TractionHoldSeconds);
	const bool bTractionLit = TractionTellTaleHold > 0.0f && !TractionTellTaleLabel.IsEmpty();
	DrawText(bTractionLit ? TractionTellTaleLabel : TEXT("TRC"),
		bTractionLit ? GaugeWarn : TellTaleOff,
		X + 150.0f, Y, GEngine->GetMediumFont(), 1.0f);
}

void AWiesbadenVehicleHUD::DrawFilledTri(const FVector2D& A, const FVector2D& B,
	const FVector2D& C, const FLinearColor& Color)
{
	if (!Canvas)
	{
		return;
	}
	FCanvasTriangleItem Tri(A, B, C, GWhiteTexture);
	Tri.SetColor(Color);
	Canvas->DrawItem(Tri);
}

void AWiesbadenVehicleHUD::DrawFilledPoly(const TArray<FVector2D>& Points,
	const FLinearColor& Color)
{
	for (int32 i = 1; i + 1 < Points.Num(); ++i)
	{
		DrawFilledTri(Points[0], Points[i], Points[i + 1], Color);
	}
}

// Gefuellte Scheibe als Vieleck-Faecher (lokal, kein Header noetig).
static void MakeDiscPoints(float CX, float CY, float R, int32 Segments, TArray<FVector2D>& Out)
{
	Out.Reset(Segments);
	for (int32 i = 0; i < Segments; ++i)
	{
		const float A = (2.0f * PI * i) / Segments;
		Out.Add(FVector2D(CX + R * FMath::Cos(A), CY + R * FMath::Sin(A)));
	}
}

void AWiesbadenVehicleHUD::DrawRoundGauge(float CX, float CY, float R,
	float Value, float MinV, float MaxV, float Sweep,
	const FString& Caption, const FString& Reading)
{
	// Gehaeuse: dunkle Scheibe mit hellem Rand, dann das Ziffernblatt.
	TArray<FVector2D> Disc;
	MakeDiscPoints(CX, CY, R + 5.0f, 40, Disc);
	DrawFilledPoly(Disc, BezelDark);
	MakeDiscPoints(CX, CY, R, 40, Disc);
	DrawFilledPoly(Disc, DialFace);
	DrawArc(CX, CY, R + 5.0f, -180.0f, 180.0f, BezelRim, 2.0f);

	const float Start = -Sweep * 0.5f;
	DrawArc(CX, CY, R - 2.0f, Start, Sweep * 0.5f, DialScale, 1.5f);

	// Teilstriche: 20 kleine, jeder fuenfte gross und beschriftet.
	for (int32 i = 0; i <= 20; ++i)
	{
		const float Frac = static_cast<float>(i) / 20.0f;
		const float Rad = FMath::DegreesToRadians(Start + Sweep * Frac);
		const float S = FMath::Sin(Rad);
		const float C = FMath::Cos(Rad);
		const bool bMajor = (i % 5) == 0;
		const float Inner = R - (bMajor ? 12.0f : 6.0f);
		DrawLine(CX + Inner * S, CY - Inner * C, CX + (R - 2.0f) * S, CY - (R - 2.0f) * C,
			bMajor ? DialText : DialScale, bMajor ? 2.0f : 1.0f);
		if (bMajor)
		{
			const float V = MinV + (MaxV - MinV) * Frac;
			const float LabelR = R - 24.0f;
			DrawText(FString::Printf(TEXT("%.0f"), V), DialScale,
				CX + LabelR * S - 7.0f, CY - LabelR * C - 6.0f, GEngine->GetSmallFont(), 0.8f);
		}
	}

	// Zeiger als schmales Dreieck vom Nabenzentrum zur Spitze, mit kurzem
	// Gegengewicht - liest sich sauberer als eine einzelne Linie.
	const float Span = MaxV - MinV;
	const float ValFrac = Span > KINDA_SMALL_NUMBER
		? FMath::Clamp((Value - MinV) / Span, 0.0f, 1.0f) : 0.0f;
	const float NRad = FMath::DegreesToRadians(Start + ValFrac * Sweep);
	const FVector2D Dir(FMath::Sin(NRad), -FMath::Cos(NRad));
	const FVector2D Perp(-Dir.Y, Dir.X);
	const FVector2D Hub(CX, CY);
	const FVector2D Tip = Hub + Dir * (R - 10.0f);
	DrawFilledTri(Hub + Perp * 3.5f, Hub - Perp * 3.5f, Tip, NeedleColor);
	DrawFilledTri(Hub + Perp * 3.0f, Hub - Perp * 3.0f, Hub - Dir * (R * 0.28f), NeedleColor);

	// Nabe.
	TArray<FVector2D> HubDisc;
	MakeDiscPoints(CX, CY, 5.0f, 16, HubDisc);
	DrawFilledPoly(HubDisc, BezelRim);

	DrawText(Reading, DialText, CX - Reading.Len() * 3.6f, CY + R * 0.34f,
		GEngine->GetSmallFont(), 1.0f);
	DrawText(Caption, DialScale, CX - Caption.Len() * 2.7f, CY + R + 6.0f,
		GEngine->GetSmallFont(), 0.85f);
}

void AWiesbadenVehicleHUD::DrawPanelBackdrop(float X, float Y, float W, float H,
	float TopInset, const FLinearColor& Fill, float Alpha)
{
	// Trapez: Oberkante um TopInset schmaler -> die Tafel kippt nach hinten.
	const FVector2D TL(X + TopInset, Y);
	const FVector2D TR(X + W - TopInset, Y);
	const FVector2D BR(X + W, Y + H);
	const FVector2D BL(X, Y + H);

	FLinearColor Top = Fill; Top.A = Alpha;
	FLinearColor Bottom = Fill * 0.45f; Bottom.A = Alpha;
	// Zwei Dreiecke mit leichtem Verlauf (oben heller als unten) durch zwei
	// getrennte Fuellungen entlang der Diagonale.
	DrawFilledTri(TL, TR, BR, Top);
	DrawFilledTri(TL, BR, BL, Bottom);

	// Helle Armaturenbrett-Oberkante als Lichtkante.
	DrawLine(TL.X, TL.Y, TR.X, TR.Y, PanelEdge, 2.5f);
	DrawLine(TL.X, TL.Y, BL.X, BL.Y, FLinearColor(0.16f, 0.18f, 0.22f, Alpha), 1.5f);
	DrawLine(TR.X, TR.Y, BR.X, BR.Y, FLinearColor(0.16f, 0.18f, 0.22f, Alpha), 1.5f);
}

void AWiesbadenVehicleHUD::DrawAttitudeIndicator(float CX, float CY, float R,
	float PitchDeg, float RollDeg)
{
	const float RollRad = FMath::DegreesToRadians(RollDeg);
	// Instrument-Achsen (Bildschirm, y nach unten): Up bei roll=0 = (0,-1).
	const FVector2D Up(FMath::Sin(RollRad), -FMath::Cos(RollRad));
	const FVector2D Right(FMath::Cos(RollRad), FMath::Sin(RollRad));

	// Horizontpunkt: Nase hoch (pitch>0) schiebt den Horizont nach unten.
	constexpr float PixPerDeg = 1.7f;
	const FVector2D Centre(CX, CY);
	const FVector2D Hpt = Centre - Up * (PitchDeg * PixPerDeg);

	// Gehaeuse.
	TArray<FVector2D> Bezel;
	MakeDiscPoints(CX, CY, R + 5.0f, 44, Bezel);
	DrawFilledPoly(Bezel, BezelDark);

	// Scheibe (Boden fuellen), dann Himmel als abgeschnittenes Vieleck darueber.
	TArray<FVector2D> Disc;
	MakeDiscPoints(CX, CY, R, 44, Disc);
	DrawFilledPoly(Disc, HorizonGround);

	// Himmel = Teil der Scheibe oberhalb des Horizonts. Sutherland-Hodgman
	// gegen die Halbebene (P-Hpt).Up >= 0.
	TArray<FVector2D> Sky;
	const int32 N = Disc.Num();
	for (int32 i = 0; i < N; ++i)
	{
		const FVector2D P = Disc[i];
		const FVector2D Q = Disc[(i + 1) % N];
		const float dp = FVector2D::DotProduct(P - Hpt, Up);
		const float dq = FVector2D::DotProduct(Q - Hpt, Up);
		if (dp >= 0.0f) { Sky.Add(P); }
		if ((dp >= 0.0f) != (dq >= 0.0f))
		{
			const float T = dp / (dp - dq);
			Sky.Add(P + (Q - P) * T);
		}
	}
	if (Sky.Num() >= 3) { DrawFilledPoly(Sky, HorizonSky); }

	// Horizontlinie ueber die Scheibe.
	const float ChordDp = FVector2D::DotProduct(Centre - Hpt, Up);
	const float Half = FMath::Sqrt(FMath::Max(R * R - ChordDp * ChordDp, 0.0f));
	const FVector2D HmidOnLine = Centre - Up * ChordDp;
	DrawLine((HmidOnLine - Right * Half).X, (HmidOnLine - Right * Half).Y,
		(HmidOnLine + Right * Half).X, (HmidOnLine + Right * Half).Y, HorizonLine, 2.0f);

	// Nickleiter: kurze Striche bei +-10 und +-20 Grad.
	for (int32 P = -20; P <= 20; P += 10)
	{
		if (P == 0) { continue; }
		const FVector2D Mid = Hpt + Up * (P * PixPerDeg);
		const float Len = (FMath::Abs(P) == 10) ? 14.0f : 22.0f;
		if (FVector2D::Distance(Mid, Centre) > R - 6.0f) { continue; }
		DrawLine((Mid - Right * Len).X, (Mid - Right * Len).Y,
			(Mid + Right * Len).X, (Mid + Right * Len).Y, HorizonLine * 0.8f, 1.2f);
	}

	// Rand.
	DrawArc(CX, CY, R + 5.0f, -180.0f, 180.0f, BezelRim, 2.5f);

	// Bank-Skala oben (feste Striche bei 0, +-30, +-60 Grad).
	for (int32 B = -60; B <= 60; B += 30)
	{
		const float A = FMath::DegreesToRadians(B) - PI * 0.5f; // 0 Grad = oben
		const FVector2D O(CX + FMath::Cos(A) * (R + 4.0f), CY + FMath::Sin(A) * (R + 4.0f));
		const FVector2D I(CX + FMath::Cos(A) * (R - 4.0f), CY + FMath::Sin(A) * (R - 4.0f));
		DrawLine(O.X, O.Y, I.X, I.Y, BezelRim, (B == 0) ? 2.5f : 1.5f);
	}

	// Festes Flugzeugsymbol: Nabenpunkt + zwei Fluegel + Bank-Zeiger oben.
	DrawLine(CX - R * 0.55f, CY, CX - R * 0.16f, CY, AircraftMark, 3.0f);
	DrawLine(CX + R * 0.16f, CY, CX + R * 0.55f, CY, AircraftMark, 3.0f);
	DrawLine(CX - R * 0.16f, CY, CX, CY + 8.0f, AircraftMark, 3.0f);
	DrawLine(CX + R * 0.16f, CY, CX, CY + 8.0f, AircraftMark, 3.0f);
	TArray<FVector2D> Dot;
	MakeDiscPoints(CX, CY, 2.5f, 10, Dot);
	DrawFilledPoly(Dot, AircraftMark);
	// Bank-Zeiger (dreht mit): kleines Dreieck am oberen Rand.
	const FVector2D PtA = Centre + Up * (R - 2.0f);
	DrawFilledTri(PtA, PtA - Up * 10.0f + Right * 5.0f, PtA - Up * 10.0f - Right * 5.0f, AircraftMark);
}

void AWiesbadenVehicleHUD::DrawHeliInstruments(const AWiesbadenHelicopter& Heli,
	bool bCockpit, float Width, float Height)
{
	constexpr float PanelW = 700.0f;
	constexpr float PanelH = 176.0f;
	const float PanelX = Width * 0.5f - PanelW * 0.5f;
	const float PanelY = Height - PanelH - 18.0f;

	// Perspektivisches Armaturenbrett. In der Cockpit-Ansicht deckend, in
	// Follow/Orbit halbtransparent, damit die Stadt nicht zugedeckt wird.
	DrawPanelBackdrop(PanelX, PanelY, PanelW, PanelH, 36.0f,
		FLinearColor(0.05f, 0.055f, 0.07f, 1.0f), bCockpit ? 0.92f : 0.55f);

	// -- Variometer (senkrechte Skala ganz links) ---------------------------
	const float Vs = Heli.GetVerticalSpeedMs();
	const float VarioX = PanelX + 30.0f;
	const float VarioMidY = PanelY + 78.0f;
	constexpr float VarioHalf = 46.0f;
	DrawRect(DialFace, VarioX, VarioMidY - VarioHalf, 20.0f, VarioHalf * 2.0f);
	DrawLine(VarioX - 3.0f, VarioMidY, VarioX + 23.0f, VarioMidY, DialText, 1.8f);
	const float VsBar = VarioHalf * FMath::Clamp(FMath::Abs(Vs) / 8.0f, 0.0f, 1.0f);
	if (Vs >= 0.0f) { DrawRect(RpmSafe, VarioX, VarioMidY - VsBar, 20.0f, VsBar); }
	else            { DrawRect(RpmRed,  VarioX, VarioMidY, 20.0f, VsBar); }
	DrawText(TEXT("VARIO"), DialScale, VarioX - 6.0f, PanelY + 12.0f, GEngine->GetSmallFont(), 0.85f);
	DrawText(FString::Printf(TEXT("%+.1f"), Vs), DialText,
		VarioX - 8.0f, VarioMidY + VarioHalf + 4.0f, GEngine->GetSmallFont(), 0.9f);

	// -- Fahrtmesser (links) -------------------------------------------------
	const float Spd = Heli.GetAirspeedKmh();
	DrawRoundGauge(PanelX + 150.0f, PanelY + 72.0f, 46.0f,
		FMath::Clamp(Spd, 0.0f, 300.0f), 0.0f, 300.0f, 250.0f,
		TEXT("FAHRT km/h"), FString::Printf(TEXT("%.0f"), Spd));

	// -- Kuenstlicher Horizont (Mitte, Hauptinstrument) ----------------------
	const FRotator Att = Heli.GetActorRotation();
	DrawAttitudeIndicator(PanelX + 350.0f, PanelY + 74.0f, 58.0f,
		Att.Pitch, Att.Roll);
	DrawText(TEXT("FLUGLAGE"), DialScale, PanelX + 322.0f, PanelY + 6.0f, GEngine->GetSmallFont(), 0.8f);

	// -- Hoehenmesser (rechts) -----------------------------------------------
	const float Alt = Heli.GetAltitudeMeters();
	DrawRoundGauge(PanelX + 540.0f, PanelY + 72.0f, 46.0f,
		FMath::Clamp(Alt, 0.0f, 300.0f), 0.0f, 300.0f, 250.0f,
		TEXT("HOEHE m"), FString::Printf(TEXT("%.0f"), Alt));

	// -- Kurs (unter dem Horizont) -------------------------------------------
	DrawText(FString::Printf(TEXT("KURS  %s"), *FormatHeading(Heli.GetHeadingDegrees())),
		DialText, PanelX + 306.0f, PanelY + 146.0f, GEngine->GetMediumFont(), 1.1f);

	// -- Kollektiv / Rotor / Triebwerk (rechte Spalte) -----------------------
	const float BarX = PanelX + 616.0f;
	constexpr float BarW = 64.0f;
	constexpr float BarH = 10.0f;

	DrawText(TEXT("MOT"), Heli.IsEngineRunning() ? IndicatorOn : TellTaleOff,
		BarX, PanelY + 16.0f, GEngine->GetMediumFont(), 1.0f);

	const float Coll = Heli.GetCollective();
	DrawText(TEXT("KOLLEKTIV"), DialScale, BarX, PanelY + 54.0f, GEngine->GetSmallFont(), 0.8f);
	DrawRect(DialFace, BarX, PanelY + 70.0f, BarW, BarH);
	DrawRect(RpmSafe, BarX, PanelY + 70.0f, BarW * Coll, BarH);

	const float Rpm = Heli.GetMainRotorRpm();
	const float RpmFrac = FMath::Clamp(Rpm / 300.0f, 0.0f, 1.0f);
	DrawText(TEXT("ROTOR"), DialScale, BarX, PanelY + 92.0f, GEngine->GetSmallFont(), 0.8f);
	DrawRect(DialFace, BarX, PanelY + 108.0f, BarW, BarH);
	DrawRect(RpmFrac < 0.5f ? RpmRed : RpmSafe, BarX, PanelY + 108.0f, BarW * RpmFrac, BarH);
	DrawText(FString::Printf(TEXT("%.0f U/min"), Rpm), DialScale,
		BarX, PanelY + 124.0f, GEngine->GetSmallFont(), 0.8f);

	// -- Bordgeschaeftzustand -------------------------------------------------
	// GEMESSEN am 01.10.2026: die Tafel zeigte Vario, Fahrt, Fluglage, Hoehe,
	// Kurs, MOT, Kollektiv und Rotor - aber kein einziges Feld zum Geschaeft.
	// In der Flugstunde hielt der Spieler den Abzug, es passierte sichtbar
	// nichts, und die Tafel sagte nichts dazu. Der Text kommt aus
	// GetHelicopterGunStatus, damit er ohne Canvas pruefbar ist
	// (Vehicles.LessonSafety, Abschnitt 5).
	if (const UWiesbadenHeliGunComponent* Gun = Heli.GetGunComponent())
	{
		const FString Status = GetHelicopterGunStatus(
			Heli.IsLessonDryFireActive(), Heli.IsDryFireReleasePending(),
			Gun->IsOverheated(), Gun->GetRemainingRounds());
		const bool bSperrt = Heli.IsLessonDryFireActive() || Heli.IsDryFireReleasePending();
		DrawText(TEXT("GESCHUETZ"), DialScale, BarX, PanelY + 142.0f,
			GEngine->GetSmallFont(), 0.8f);
		DrawText(Status, bSperrt ? IndicatorOn : (Gun->IsOverheated() ? RpmRed : DialText),
			BarX, PanelY + 154.0f, GEngine->GetSmallFont(), 0.9f);
	}
}

void AWiesbadenVehicleHUD::DrawCarCockpitDash(float Width, float Height)
{
	// Perspektivisches Armaturenbrett unten. Der Wagen selbst ist fuer den
	// Fahrer ausgeblendet (kein Innenraum modelliert) - dieses Band gibt der
	// Ich-Perspektive den Rahmen eines Cockpits. Als Trapez (Oberkante zur
	// Mitte eingezogen) kippt es glaubhaft nach hinten weg.
	const float DashH = Height * 0.22f;
	DrawPanelBackdrop(0.0f, Height - DashH, Width, DashH, Width * 0.14f,
		FLinearColor(0.02f, 0.02f, 0.028f, 1.0f), 0.94f);
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

	// First-Run-Fuehrung: den Legenden-Timer (ControlLegendSeconds) erst NEU
	// starten, wenn die Stadt wirklich spielbar ist (Streaming fertig). Sonst
	// zaehlt er ab HUD-Start waehrend des ~25-38-s-Ladens durch und die Legende
	// ist beim ersten Fahren schon zum "F1 Steuerung"-Rest verblasst - der
	// Neuling sieht die Steuerung nie. Einmal-Latch, damit spaeteres Nachstreamen
	// sie nicht wieder aufpoppen laesst.
	if (!bLegendArmed)
	{
		const UWorld* W = GetWorld();
		const UWiesbadenCitySubsystem* City =
			W ? W->GetSubsystem<UWiesbadenCitySubsystem>() : nullptr;
		if (City && City->IsCityStreamingComplete())
		{
			UE_LOG(LogWbCore, Log,
				TEXT("HUD: Steuerungs-Legende bei Streaming-fertig neu gestartet (bisher %.1f s gelaufen)."),
				ElapsedSeconds);
			ElapsedSeconds = 0.0f;
			bShowControlLegend = true;
			bLegendArmed = true;
		}
	}

	// Spieleingangshilfe (First-Run): die Zustandsmaschine steht in
	// UpdateFirstRunOnboarding() - DrawHUD zeichnet nur noch. Der Block lag
	// vorher inline hier und machte DrawHUD zur halben State-Machine.
	UpdateFirstRunOnboarding();

	// Gespeicherte Optionen anwenden. Im Sekundentakt, weil Spielfigur und
	// Fahrzeugkamera beim Ein-/Aussteigen neu entstehen und sonst wieder mit
	// der eingebauten Maus-Empfindlichkeit liefen.
	ApplyPersistentOptions();

	// Pausemenue zuerst: es liegt ueber allem und haelt die Zeit an.
	UpdatePauseMenu();

	// Intro, Titelbildschirm und Hauptmenue liegen ueber allem - auch ueber
	// dem Pausemenue. Sie nehmen die Eingabe an sich und geben sie erst frei,
	// wenn der Spieler "Spiel starten" gewaehlt hat. VOR der Flugstunde, weil
	// waehrend des Titels keine Flug-Einladung ueber dem Menue liegen darf.
	if (UpdateTitleMenu())
	{
		DrawTitleScreen(Width, Height);
		return;
	}

	UpdateHelicopterLesson();
	if (bHelicopterLessonOverlayVisible && PauseView == EWbPauseView::Aus)
	{
		DrawHelicopterLessonOffer(Width, Height);
		return;
	}
	if (PauseView == EWbPauseView::Optionen)
	{
		DrawOptions(Width, Height);
		return;
	}
	if (PauseView == EWbPauseView::Menue)
	{
		DrawPauseMenu(Width, Height);
		return;
	}

	// Weltkarte umschalten: M (Tastatur) oder der View/Back/Select-Knopf am
	// Gamepad. Flanke, damit ein Druck einmal umschaltet. Ist sie offen, liegt
	// sie als Vollbild ueber dem uebrigen HUD.
	if (APlayerController* MapPC = GetOwningPlayerController())
	{
		const bool bMapDown = MapPC->IsInputKeyDown(EKeys::M)
			|| MapPC->IsInputKeyDown(EKeys::Gamepad_Special_Left);
		if (bMapDown && !bMapKeyHeld)
		{
			bWorldMapOpen = !bWorldMapOpen;
			if (bWorldMapOpen)
			{
				// Frisch eingepasst oeffnen: Zoom 1, Zentrum aus der ersten
				// Projektion (Netzmitte) zuruecklesen lassen.
				MapZoom = 1.0f;
				bMapCentreInit = false;
			}
		}
		bMapKeyHeld = bMapDown;
	}
	// Headless-Sichtprobe: -WbShowMap erzwingt die offene Karte. Die HUD pollt
	// echte Tasten, die im automatisierten Aufnahmelauf nicht feuern - ohne den
	// Schalter liesse sich das Rendern der Karte nie belegen.
	if (FParse::Param(FCommandLine::Get(), TEXT("WbShowMap")))
	{
		bWorldMapOpen = true;
		// -WbMapZoom=<n>: feste Zoomstufe fuer die Sichtprobe (headless kein Input).
		float ForceZoom = 0.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("WbMapZoom="), ForceZoom) && ForceZoom > 0.0f)
		{
			// Zoomstufe halten; das Zentrum klemmt der erste Frame auf die Netzmitte.
			MapZoom = FMath::Clamp(ForceZoom,
				FWiesbadenMinimap::WorldMapMinZoom, FWiesbadenMinimap::WorldMapMaxZoom);
		}
	}
	if (bWorldMapOpen)
	{
		DrawWorldMap(Width, Height);
		return;
	}

	// Die Bilder je Sekunde (Gruppe DEBUG). Rechts oben ueber allem, weil es
	// die EINZIGE Zahl ist, die beim Optimieren etwas sagt - und weil sie
	// nicht im Tacho steht.
	if (bFpsAnzeige)
	{
		const UWorld* FpsWorld = GetWorld();
		const float Dt = FpsWorld ? FpsWorld->GetDeltaSeconds() : 0.0f;
		const int32 Bilder = (Dt > KINDA_SMALL_NUMBER)
			? FMath::RoundToInt(1.0f / Dt) : 0;
		DrawTextRechts(FString::Printf(TEXT("%d Bilder/s"), Bilder), DialText,
			Width - 16.0f, 12.0f, GEngine->GetSmallFont(), 1.0f);
	}

	// Profil-Tafel (Gruppe DEBUG): halb so gross und halbtransparent, damit
	// das Bild darunter erkennbar bleibt. Die alte Engine-Statistik
	// (stat fps/unit/game) liess sich aus Projektcode weder verkleinern noch
	// aufhellen - ihre undurchsichtigen Tabellen fielen komplett ueber das
	// Bild (siehe AGENTS.md: landeten in Filmaufnahmen). Oben links, damit
	// sie nicht mit der Bildrate-Zeile rechts und der Minikarte kollidiert.
	if (bStatEinblendung)
	{
		DrawProfilTafel(16.0f, 12.0f);
	}

	// F1 schaltet die Legende um (Flanke, damit ein Tastendruck einmal zaehlt).
	//
	// Umgeschaltet wird das, was der Spieler SIEHT - nicht der Merker allein.
	// Nach dem Selbst-Ausblenden (ElapsedSeconds > ControlLegendSeconds) stand
	// bShowControlLegend noch auf true, obwohl nichts mehr zu sehen war: der
	// erste Druck schaltete also eine unsichtbare Legende "aus" und es passierte
	// gar nichts. Der Bildschirm bot die ganze Zeit "F1  Steuerung" an, und man
	// musste zweimal druecken.
	if (APlayerController* PC = GetOwningPlayerController())
	{
		const bool bKeyDown = PC->IsInputKeyDown(EKeys::F1);
		if (bKeyDown && !bLegendKeyHeld)
		{
			bShowControlLegend = ToggleControlLegendVisible(
				bShowControlLegend, ElapsedSeconds, ControlLegendSeconds);

			// Beim Einschalten die Standzeit neu starten, sonst waere die
			// Legende nach Ablauf der Einblenddauer nicht mehr zurueckzuholen.
			if (bShowControlLegend)
			{
				ElapsedSeconds = 0.0f;
			}
		}
		bLegendKeyHeld = bKeyDown;
	}

	if (const UWiesbadenCitySubsystem* City = GetWorld()->GetSubsystem<UWiesbadenCitySubsystem>())
	{
		const UWiesbadenPoliceSubsystem* Police = GetWorld()->GetSubsystem<UWiesbadenPoliceSubsystem>();
		if (City->GetWantedLevel() > 0)
		{
			FString Stars;
			for (int32 i = 0; i < 6; ++i) { Stars += i < City->GetWantedLevel() ? TEXT("* ") : TEXT(". "); }
			DrawText(TEXT("FAHNDUNG  ") + Stars, GaugeWarn, Width - 290, 90, GEngine->GetMediumFont(), 1.0f);
			const FString State = Police && Police->IsPlayerSeen() ? TEXT("POLIZEI HAT SICHTKONTAKT") : TEXT("ENTKOMMEN: AUS DER SICHT BLEIBEN");
			DrawText(State, DialText, Width - 290, 118, GEngine->GetSmallFont(), .85f);
			if (Police && Police->GetArrestProgress() > 0)
			{ DrawText(FString::Printf(TEXT("FESTNAHME %.0f%% - ENTKOMMEN!"), Police->GetArrestProgress() * 100), RpmRed,
				Width - 290, 140, GEngine->GetSmallFont(), 1.0f); }
		}
		else if (Police && Police->WasRecentlyArrested())
		{ DrawText(TEXT("FESTGENOMMEN - FAHNDUNG BEENDET"), GaugeWarn, Width - 340, 95, GEngine->GetMediumFont(), 1.0f); }
	}

	// Fahrzeug ueber die Steuernaht-Familie (Kaefer wie ChaosCar); der Heli hat
	// seine eigene Instrumententafel.
	IWiesbadenVehicleControl* Vehicle = GetPlayerVehicleControl();
	const AWiesbadenHelicopter* Heli = Vehicle ? nullptr : GetPlayerHelicopter();
	const bool bInVehicle = (Vehicle != nullptr) || (Heli != nullptr);

	// Legende zeichnen, solange sie eingeschaltet ist. Nach
	// ControlLegendSeconds blendet sie von selbst aus; F1 holt sie zurueck.
	// Sie erscheint AUCH zu Fuss - dort gab es bisher gar keine Anzeige.
	// Die Lektionstafel NICHT bei offenem Pausenmenue: DrawPauseMenu laeuft
	// weiter oben (Zeile ~706), diese Tafel hier - ohne die Sperre lag sie
	// sichtbar ueber dem Menue, das selbst "Unterricht pausiert - bestaetigen
	// beendet die Flugstunde" verspricht. Die Einladung war schon richtig
	// behandelt (nur bei PauseView == Aus).
	if (bHelicopterLessonActive && PauseView == EWbPauseView::Aus)
	{
		DrawHelicopterLesson(Width, Height);
	}
	else if (bShowControlLegend && ElapsedSeconds <= ControlLegendSeconds)
	{
		DrawControlLegend(bInVehicle, 40.0f, Height - (bInVehicle ? 370.0f : 210.0f));
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
	DrawVehicleBanner(Width * 0.5f, Height * 0.16f);
	DrawTransientHint(Width, Height);
	DrawMissionPanel(Width, Height);

	// Guthaben oben rechts (aus dem persistenten Spielzustand).
	if (const UWorld* HudGameWorld = GetWorld())
	{
		if (const UGameInstance* GI = HudGameWorld->GetGameInstance())
		{
			if (const UWiesbadenGameStateSubsystem* GameState =
				GI->GetSubsystem<UWiesbadenGameStateSubsystem>())
			{
				const FString GuthabenText =
					FString::Printf(TEXT("Guthaben: %d EUR"), GameState->GetGuthaben());
				DrawText(GuthabenText, FLinearColor(1.0f, 0.86f, 0.35f, 1.0f),
					Width - 210.0f, 16.0f, GEngine ? GEngine->GetMediumFont() : nullptr, 1.0f);
			}
		}
	}

	// Freischaltungs-Katalog (per Konsole "Wb.Store" ein-/ausgeblendet).
	DrawStorePanel(Width, Height);

	// -- Helikopter: eigene Cockpit-Instrumententafel -----------------------
	if (Heli)
	{
		const bool bCockpit =
			Heli->GetCameraMode() == EWiesbadenVehicleCameraMode::Cockpit;
		DrawHeliInstruments(*Heli, bCockpit, Width, Height);
		return;
	}

	if (!Vehicle)
	{
		// Zu Fuss: statt Tacho der Hinweis, was hier gerade moeglich ist -
		// und waehrend einer Mitfahrt die Tafel mit Kurbel und Wasserstand.
		DrawFunicularRidePanel(Width * 0.5f, Height - 190.0f);
		DrawFootPrompt(Width * 0.5f, Height - 120.0f);
		return;
	}

	// Cockpit-Ansicht: Armaturenbrett-Band unterlegen (das Fahrzeug ist fuer den
	// Fahrer ausgeblendet). ZUERST, damit Tacho und Drehzahl darauf liegen. Ueber
	// das Interface fuer JEDES Fahrzeug der Familie (Kaefer wie ChaosCar).
	if (Vehicle->GetCameraMode() == EWiesbadenVehicleCameraMode::Cockpit)
	{
		DrawCarCockpitDash(Width, Height);
	}

	const float Radius = FMath::Clamp(Height * 0.16f, 60.0f, 130.0f);
	const float CenterX = Width - MapDiameter - MapMargin * 2.0f - Radius - 20.0f;
	const float CenterY = Height - Radius - 70.0f;

	// Volle Instrumententafel ueber die Familie - identisch fuer beide Autos.
	DrawSpeedometer(*Vehicle, CenterX, CenterY, Radius);
	DrawRpmBar(*Vehicle, CenterX - Radius, CenterY + Radius + 18.0f, Radius * 2.0f, 10.0f);
	DrawTellTales(*Vehicle, CenterX - Radius * 0.5f, CenterY - Radius - 46.0f);
}


FString AWiesbadenVehicleHUD::BuildFootPrompt(
	double NearestVehicleCm, double NearestFunicularCm,
	double VehicleReachCm, double FunicularReachCm,
	bool bVehicleIsHelicopter)
{
	// Der Helikopter wird benannt: er steht 12 m neben dem Auto, sieht aus der
	// Ferne aus wie Kulisse, und mit dem gleichen "F Einsteigen" wie am Wagen
	// steigt man reflexhaft wieder in den Wagen.
	const TCHAR* const VehicleText =
		bVehicleIsHelicopter ? TEXT("F   Helikopter besteigen") : TEXT("F   Einsteigen");
	// Das NAEHERE gewinnt, wenn beides in Reichweite ist - sonst blinkte an
	// der Talstation neben dem geparkten Wagen zweierlei durcheinander.
	const bool bVehicle = NearestVehicleCm >= 0.0 && NearestVehicleCm <= VehicleReachCm;
	const bool bFunicular = NearestFunicularCm >= 0.0 && NearestFunicularCm <= FunicularReachCm;

	if (bVehicle && bFunicular)
	{
		return NearestVehicleCm <= NearestFunicularCm
			? VehicleText
			: TEXT("E   Nerobergbahn - mitfahren");
	}
	if (bVehicle)
	{
		return VehicleText;
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

	// Naehe als Paar: Entfernung UND Actor. Die Entfernung braucht jeder
	// Hinweis, den Actor nur der Haendler-Cue (Reichweite und Text haengen am
	// einzelnen Haendler).
	struct FNearest
	{
		AActor* Actor = nullptr;
		double DistanceCm = -1.0;
	};

	auto NearestOf = [&Here](const TArray<AActor*>& Actors) -> FNearest
	{
		FNearest Best;
		for (AActor* Actor : Actors)
		{
			if (!Actor)
			{
				continue;
			}
			const FVector Delta = Actor->GetActorLocation() - Here;
			const double Flat = FMath::Sqrt(Delta.X * Delta.X + Delta.Y * Delta.Y);
			if (Best.DistanceCm < 0.0 || Flat < Best.DistanceCm)
			{
				Best.Actor = Actor;
				Best.DistanceCm = Flat;
			}
		}
		return Best;
	};

	// Der Actor-Suchlauf (dreimal ueber ALLE Actors der Stadt) ist teuer -
	// darum nur wenige Male je Sekunde, nicht je Bild. Die Entfernung eines
	// Naeherungshinweises darf ein Drittel Sekunde alt sein.
	FootPromptScanAge += HudWorld->GetDeltaSeconds();
	if (FootPromptScanAge > 0.3f)
	{
		TArray<AActor*> Cars;
		TArray<AActor*> Helicopters;
		TArray<AActor*> Funiculars;
		TArray<AActor*> Merchants;
		TArray<AActor*> Shops;
		UGameplayStatics::GetAllActorsOfClass(HudWorld, AWiesbadenDennoShop::StaticClass(), Shops);
		CachedDennoShop = Shops.IsEmpty() ? nullptr : Cast<AWiesbadenDennoShop>(Shops[0]);
		UGameplayStatics::GetAllActorsOfClass(HudWorld, AWiesbadenCar::StaticClass(), Cars);
		UGameplayStatics::GetAllActorsOfClass(HudWorld, AWiesbadenHelicopter::StaticClass(), Helicopters);
		UGameplayStatics::GetAllActorsOfClass(HudWorld, AWiesbadenNerobergbahn::StaticClass(), Funiculars);
		UGameplayStatics::GetAllActorsOfClass(HudWorld, AWiesbadenStoreMerchant::StaticClass(), Merchants);
		// Auto und Helikopter GETRENNT messen: nur so weiss der Hinweis, ob er
		// den Ka-52 oder den Wagen meint (beide zaehlen als "Fahrzeug").
		const FNearest NearestCar = NearestOf(Cars);
		const FNearest NearestHeli = NearestOf(Helicopters);
		const bool bHeliCloser = NearestHeli.DistanceCm >= 0.0
			&& (NearestCar.DistanceCm < 0.0 || NearestHeli.DistanceCm < NearestCar.DistanceCm);
		CachedFootVehicleCm = bHeliCloser ? NearestHeli.DistanceCm : NearestCar.DistanceCm;
		bCachedFootVehicleIsHelicopter = bHeliCloser;
		CachedFootFunicularCm = NearestOf(Funiculars).DistanceCm;

		// Der Haendler wird als Actor gemerkt, nicht nur als Entfernung.
		// Ohne diese Zuweisung blieb CachedFootMerchant dauerhaft leer und
		// ResolveMerchantCue fiel auf den allgemeinen Hinweis zurueck.
		CachedFootMerchant = Cast<AWiesbadenStoreMerchant>(NearestOf(Merchants).Actor);
		// Die Nerobergbahn ebenso: die Mitfahrtafel braucht ihren Wasserstand
		// (siehe DrawFunicularRidePanel).
		CachedFunicular = Cast<AWiesbadenNerobergbahn>(NearestOf(Funiculars).Actor);

		FootPromptScanAge = 0.0f;
	}

	FString Prompt = BuildFootPrompt(
		CachedFootVehicleCm, CachedFootFunicularCm,
		FootVehicleReachCm, FootFunicularReachCm, bCachedFootVehicleIsHelicopter);
	// Vor Dennos Laden gehoert F der Auftragsannahme (GameMode::TryDennoDelivery
	// hat Vorrang vor dem Einsteigen) - der Hinweis sagt dasselbe.
	if (const AWiesbadenDennoShop* Shop = CachedDennoShop.Get())
	{
		if (Shop->IsPlayerInDeliveryReach(Here))
		{
			const UWiesbadenMissionSubsystem* Missions = HudWorld->GetSubsystem<UWiesbadenMissionSubsystem>();
			Prompt = AWiesbadenDennoShop::BuildDeliveryPrompt(Missions && Missions->HasActiveMission());
		}
	}

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

void AWiesbadenVehicleHUD::DrawFunicularRidePanel(float CenterX, float Y)
{
	AWiesbadenNerobergbahn* Bahn = CachedFunicular.Get();
	if (!Bahn || !Bahn->IsPlayerRiding())
	{
		return;
	}

	const int32 Wagen = Bahn->GetRiddenCarIndex();
	float Fuellstand = 0.0f;
	bool bSchieberOffen = false;
	if (!Bahn->GetCarWater(Wagen, Fuellstand, bSchieberOffen))
	{
		return;
	}
	Fuellstand = FMath::Clamp(Fuellstand, 0.0f, 1.0f);

	const FString Kopf = FString::Printf(TEXT("MITFAHRT NEROBERGBAHN - WAGEN %s"),
		Wagen == 0 ? TEXT("A") : TEXT("B"));

	constexpr float BoxWidth = 560.0f;
	constexpr float LineHeight = 20.0f;
	constexpr float Padding = 12.0f;
	constexpr float BarHeight = 10.0f;
	const float BoxHeight = Padding * 2.0f + LineHeight * 3.0f + BarHeight + 16.0f;
	const float X = CenterX - BoxWidth * 0.5f;

	DrawRect(DialBackground, X, Y, BoxWidth, BoxHeight);

	DrawText(Kopf, DialText, X + Padding, Y + Padding,
		GEngine->GetMediumFont(), 1.0f);
	DrawText(TEXT("Kurbel: K drehen (Wasserschieber auf/zu)      Aussteigen: E"),
		DialScale, X + Padding, Y + Padding + LineHeight,
		GEngine->GetSmallFont(), 1.0f);
	DrawText(FormatWaterLevel(Fuellstand, bSchieberOffen),
		bSchieberOffen ? RpmRed : RpmSafe,
		X + Padding, Y + Padding + LineHeight * 2.0f,
		GEngine->GetSmallFont(), 1.0f);

	// Balken mit den Viertelmarken des Schauglases (10/20/30/40).
	const float BarX = X + Padding;
	const float BarY = Y + Padding + LineHeight * 3.0f + 6.0f;
	const float BarWidth = BoxWidth - Padding * 2.0f;
	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.5f), BarX, BarY, BarWidth, BarHeight);
	DrawRect(bSchieberOffen ? RpmRed : RpmSafe, BarX, BarY,
		BarWidth * Fuellstand, BarHeight);
	for (int32 Mark = 1; Mark < 4; ++Mark)
	{
		const float MarkX = BarX + BarWidth * (Mark / 4.0f);
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f), MarkX - 0.5f, BarY, 1.0f, BarHeight);
	}
}

void AWiesbadenVehicleHUD::GetControlLegendLines(bool bInVehicle, TArray<FString>& OutLines)
{
	OutLines.Reset();

	if (bInVehicle)
	{
		OutLines.Add(TEXT("W / Pfeil hoch      Gas"
			"        Rechter Trigger"));
		OutLines.Add(TEXT("S / Pfeil runter    Bremse, im Stand rueckwaerts"));
		OutLines.Add(TEXT("A D / Pfeile        Lenken"));
		OutLines.Add(TEXT("Leertaste           Handbremse"));
		OutLines.Add(TEXT("Q E                 Blinker      H  Warnblinker"));
		OutLines.Add(TEXT("L                   Licht        R  Rueckwaertsgang"));
		OutLines.Add(TEXT("B                   Hupe         X  Aufblenden"));
		OutLines.Add(TEXT("Maus                Umsehen      F  Aussteigen"));
		OutLines.Add(TEXT("C                   Kamera       M  Karte zeigen/verbergen"));
		OutLines.Add(TEXT("Gamepad   A Hupe  B Handbremse  X Rueckwaerts  Y Aussteigen"));
		OutLines.Add(TEXT("          LB RB Blinker   Kreuz hoch Licht   runter Warnblinker"));
		// Die Belegung bleibt hier kompakt; die vollstaendige Tabelle mit
		// Tastatur- und Gamepad-Spalten steht auf der Steuerungsseite und in
		// jeder Flugstunden-Uebung.
		OutLines.Add(TEXT("Heli  W/S Nick, A/D Roll, Q/E Gier  (Pad: LB/RB Gier)"));
		OutLines.Add(TEXT("      Leertaste/Strg Kollektiv, G/X Motor  (Pad: RT/LT Kollektiv)"));
		OutLines.Add(TEXT("      Linke Maustaste/A Feuer, L/B Lichter"));
		OutLines.Add(TEXT("      C/R3 Kamera, Maus/rechter Stick Umsehen"));
		OutLines.Add(TEXT("      Steuerkreuz hoch/runter Lichter, F/Y Aussteigen"));
		return;
	}

	OutLines.Add(TEXT("W A S D             Gehen"));
	OutLines.Add(TEXT("Umschalt links      Rennen"));
	OutLines.Add(TEXT("Leertaste           Springen"));
	OutLines.Add(TEXT("X (halten)          Ducken"));
	OutLines.Add(TEXT("Maus / Pfeiltasten  Umsehen"));
	OutLines.Add(TEXT("Rechte Maustaste    Zielen (Zoom mit Mausrad)"));
	OutLines.Add(TEXT("Linke Maustaste     Feuern / Kettensaege schwingen"));
	OutLines.Add(TEXT("Mausrad             Waffe wechseln / im Zielmodus zoomen"));
	OutLines.Add(TEXT("F                   Einsteigen - auch in Verkehrsautos"));
	// XBox-Belegung nach RDR2-Vorbild (siehe Core/WiesbadenInputMap.h).
	OutLines.Add(TEXT("  Gamepad  RT Feuer   LT Zielen   A Springen   L3 Rennen"));
	OutLines.Add(TEXT("           R3 Ducken  RB/LB Waffe   D-Pad hoch/runter zoomen"));
	OutLines.Add(TEXT("           X Einsteigen   Y Ansicht wechseln"));
	OutLines.Add(TEXT("E                   Nerobergbahn - mitfahren"));
	// Im Wagen bedient die Kurbel den Wasserschieber (AWiesbadenNerobergbahn).
	OutLines.Add(TEXT("K                   Kurbel drehen - im Nerobergbahn-Wagen"));
}

bool AWiesbadenVehicleHUD::ToggleControlLegendVisible(
	bool bShown, float ElapsedSeconds, float LegendSeconds)
{
	const bool bVisible = bShown && ElapsedSeconds <= LegendSeconds;
	return !bVisible;
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

FString AWiesbadenVehicleHUD::GetHelicopterLessonStepTitle(int32 Step)
{
	const TArray<FWbHelicopterLessonStep>& Steps = WiesbadenHelicopterLesson::Steps();
	const int32 SafeStep = FMath::Clamp(Step, 0, Steps.Num() - 1);
	return Steps[SafeStep].Titel;
}

FString AWiesbadenVehicleHUD::GetHelicopterLessonInstructions(int32 Step)
{
	const TArray<FWbHelicopterLessonStep>& Steps = WiesbadenHelicopterLesson::Steps();
	const int32 SafeStep = FMath::Clamp(Step, 0, Steps.Num() - 1);
	return Steps[SafeStep].Anleitung;
}

int32 AWiesbadenVehicleHUD::AdvanceHelicopterLessonStep(int32 Step, bool bSatisfied, bool bSkip)
{
	return WiesbadenHelicopterLesson::AdvanceStep(Step, bSatisfied, bSkip);
}

FString AWiesbadenVehicleHUD::GetHelicopterLessonConfirmKeys(int32 Step)
{
	const TArray<FWbHelicopterLessonStep>& Steps = WiesbadenHelicopterLesson::Steps();
	return (Steps.IsValidIndex(Step) && Steps[Step].bManualConfirm)
		? FString(TEXT("Enter / A")) : FString();
}

FString AWiesbadenVehicleHUD::GetHelicopterLessonSkipHint(int32 Step)
{
	// Der Abschlussschritt nimmt kein Skip an (bSkip gilt nur fuer die
	// Uebungen) - die Fusszeile darf es dort also nicht bewerben.
	return Step < WiesbadenHelicopterLesson::PracticeStepCount
		? FString(TEXT("Tab / B ueberspringen")) : FString();
}

bool AWiesbadenVehicleHUD::IsLessonLookStepSatisfied(float LookMagnitude, EWiesbadenVehicleCameraMode CameraMode)
{
	return CameraMode != EWiesbadenVehicleCameraMode::Cockpit && LookMagnitude > 0.01f;
}

void AWiesbadenVehicleHUD::EndHelicopterLesson(const FString& Hinweis,
	AWiesbadenHelicopter* ZusaetzlichHeli)
{
	// Nur der von dieser Lektion gespeicherte Pawn traegt unsere Sperre.
	// Ein inzwischen uebernommener anderer Heli bleibt unangetastet.
	AWiesbadenHelicopter* Traeger = LastHelicopterPawn.Get();
	if (Traeger)
	{
		Traeger->SetLessonDryFireActive(false);
	}
	if (ZusaetzlichHeli && ZusaetzlichHeli != Traeger)
	{
		ZusaetzlichHeli->SetLessonDryFireActive(false);
	}

	bHelicopterLessonActive = false;
	bHelicopterLessonOverlayVisible = false;
	bHelicopterLessonDeferredThisSession = true;
	HelicopterLessonStep = 0;
	HelicopterLessonStableTime = 0.0f;
	if (!Hinweis.IsEmpty())
	{
		ShowTransientHint(Hinweis);
	}
}

void AWiesbadenVehicleHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Letzte Gelegenheit, die Feuersperre zu loesen. EndHelicopterLesson
	// nutzt LastHelicopterPawn bewusst fuer den Fall, dass der Spieler den
	// Hubschrauber schon verlassen hat und der HUD trotzdem abgeschaltet wird.
	EndHelicopterLesson(FString(), LastHelicopterPawn.Get());
	Super::EndPlay(EndPlayReason);
}

void AWiesbadenVehicleHUD::RespondToHelicopterLessonOffer(bool bStart)
{
	bHelicopterLessonOverlayVisible = false;
	if (!bStart)
	{
		EndHelicopterLesson(FString());
	}
	if (bStart)
	{
		StartHelicopterLesson();
	}
	else
	{
		bHelicopterLessonDeferredThisSession = true;
		ShowTransientHint(TEXT("Flugstunde spaeter im Pausenmenue unter Steuerung starten."));
	}
}

void AWiesbadenVehicleHUD::StartHelicopterLesson()
{
	AWiesbadenHelicopter* Heli = GetPlayerHelicopter();
	if (!Heli || Heli->IsDestroyed())
	{
		ShowTransientHint(TEXT("Zum Starten der Flugstunde zuerst einen flugfaehigen Helikopter besteigen."));
		return;
	}

	LastHelicopterPawn = Heli;
	bHelicopterLessonOffered = true;
	bHelicopterLessonDeferredThisSession = false;
	bHelicopterLessonOverlayVisible = false;
	bHelicopterLessonActive = true;
	HelicopterLessonStep = 0;
	HelicopterLessonStableTime = 0.0f;
	bHelicopterLessonCollectiveUp = false;
	bHelicopterLessonCollectiveDown = false;
	bHelicopterLessonEngineChanged = false;
	bHelicopterLessonFireObserved = false;
	bHelicopterLessonStartEngineState = Heli->IsEngineRunning();
	bHelicopterLessonStartSearchlightState =
		Heli->GetLightRig() && Heli->GetLightRig()->AreSearchlightsOn();
	bHelicopterLessonStartLandingLightState =
		Heli->GetLightRig() && Heli->GetLightRig()->IsLandingLightOn();
	HelicopterLessonStartCamera = Heli->GetCameraMode();
	HelicopterLessonInitialShots = Heli->GetGunComponent()
		? Heli->GetGunComponent()->GetShotsFired() : 0;
	Heli->SetLessonDryFireActive(true);
	ShowTransientHint(TEXT("Flugstunde gestartet - mit Tab/B ueberspringen, Esc pausieren."));
}

bool AWiesbadenVehicleHUD::IsHelicopterLessonStepSatisfied(const AWiesbadenHelicopter& Heli) const
{
	const TArray<FWbHelicopterLessonStep>& Steps = WiesbadenHelicopterLesson::Steps();
	if (!Steps.IsValidIndex(HelicopterLessonStep))
	{
		return false;
	}
	const FWbHelicopterLessonStep& Step = Steps[HelicopterLessonStep];
	const APlayerController* PC = GetOwningPlayerController();
	switch (HelicopterLessonStep)
	{
	case 0:
		return bHelicopterLessonCollectiveUp && bHelicopterLessonCollectiveDown;
	case 1:
		return HelicopterLessonStableTime >= 0.12f;
	case 2:
		return HelicopterLessonStableTime >= 0.12f;
	case 3:
		return PC && WiesbadenInputMap::IsHelicopterActionDown(PC, EWiesbadenHeliAction::Yaw);
	case 4:
		return Heli.GetCameraMode() != HelicopterLessonStartCamera;
	case 5:
		return Heli.GetVehicleCamera()
			&& IsLessonLookStepSatisfied(
				Heli.GetVehicleCamera()->GetLastLookInputMagnitude(), Heli.GetCameraMode());
	case 6:
		return Heli.GetLightRig()
			&& Heli.GetLightRig()->AreSearchlightsOn() != bHelicopterLessonStartSearchlightState;
	case 7:
		return Heli.GetLightRig()
			&& Heli.GetLightRig()->IsLandingLightOn() != bHelicopterLessonStartLandingLightState;
	case 8:
		return bHelicopterLessonEngineChanged
			&& Heli.IsEngineRunning() == bHelicopterLessonStartEngineState;
	case 9:
		return bHelicopterLessonFireObserved && Heli.GetGunComponent()
			&& Heli.GetGunComponent()->GetShotsFired() == HelicopterLessonInitialShots;
	case WiesbadenHelicopterLesson::PracticeStepCount:
		return PC && (PC->WasInputKeyJustPressed(EKeys::Enter)
			|| PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom));
	default:
		return Step.bManualConfirm;
	}
}

void AWiesbadenVehicleHUD::UpdateHelicopterLesson()
{
	APlayerController* PC = GetOwningPlayerController();
	if (!PC || !GetWorld())
	{
		return;
	}
	if (!bHelicopterLessonPreferenceLoaded)
	{
		bHelicopterLessonCompleted = false;
		if (GConfig)
		{
			GConfig->GetBool(TEXT("WiesbadenReal.Optionen"),
				TEXT("HelikopterFlugstundeAbgeschlossen"), bHelicopterLessonCompleted,
				GGameUserSettingsIni);
		}
		bHelicopterLessonPreferenceLoaded = true;
	}

	AWiesbadenHelicopter* Heli = GetPlayerHelicopter();
	if (LastHelicopterPawn.Get() != Heli)
	{
		if (ShouldLessonBreakOnPawnChange(bHelicopterLessonActive, bHelicopterLessonOverlayVisible))
		{
			// Der Abzug kann am alten Hubschrauber haengen, den der Spieler
			// gerade verlassen hat - deshalb wird er ausdruecklich mitgegeben.
			EndHelicopterLesson(FString(), LastHelicopterPawn.Get());
		}
		LastHelicopterPawn = Heli;
		if (Heli && !bHelicopterLessonCompleted
			&& !bHelicopterLessonDeferredThisSession && !bHelicopterLessonOffered)
		{
			bHelicopterLessonOffered = true;
			bHelicopterLessonOverlayVisible = true;
			// Feuerfreiheit von der Einladung an, nicht erst ab Annahme: die
			// Pad-Taste zum Annehmen ist zugleich der Abzug (Gamepad_FaceButton_Bottom).
			Heli->SetLessonDryFireActive(true);
		}
	}

	// Absturz beendet die Flugstunde. Der Pawnwechsel greift hier nicht: der
	// Spieler sitzt im selben Actor weiter, und bDestroyed setzt TakeDamage -
	// nicht UnPossessed. Ohne diesen Zweig wartete die Lektion auf Schritte,
	// die nicht mehr erfuellbar sind, und die Feuersperre blieb bis zum
	// Aussteigen stehen (Beweis: Vehicles.LessonSafety, Abschnitt 6).
	if (ShouldLessonBreakOnHeliLoss(bHelicopterLessonActive, bHelicopterLessonOverlayVisible,
		Heli != nullptr && !Heli->IsDestroyed()))
	{
		EndHelicopterLesson(TEXT("Flugstunde beendet - der Hubschrauber ging verloren."),
			LastHelicopterPawn.Get());
	}

	if (bHelicopterLessonOverlayVisible)
	{
		if (PauseView != EWbPauseView::Aus)
		{
			return;
		}
		const bool bAccept = PC->WasInputKeyJustPressed(EKeys::Enter)
			|| PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom);
		const bool bDecline = PC->WasInputKeyJustPressed(EKeys::N)
			|| PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Right);
		if (bAccept)
		{
			RespondToHelicopterLessonOffer(true);
		}
		else if (bDecline)
		{
			RespondToHelicopterLessonOffer(false);
		}
		return;
	}

	if (!bHelicopterLessonActive || PauseView != EWbPauseView::Aus)
	{
		return;
	}
	if (!Heli)
	{
		return;
	}
	if (WiesbadenInputMap::IsHelicopterActionDown(PC, EWiesbadenHeliAction::Exit))
	{
		EndHelicopterLesson(TEXT("Flugstunde abgebrochen (Aussteigen)."), Heli);
		return;
	}

	const bool bSkip = HelicopterLessonStep < WiesbadenHelicopterLesson::PracticeStepCount
		&& (PC->WasInputKeyJustPressed(EKeys::Tab)
			|| PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Right));
	const TArray<FWbHelicopterLessonStep>& Steps = WiesbadenHelicopterLesson::Steps();
	if (!Steps.IsValidIndex(HelicopterLessonStep))
	{
		return;
	}

	const FWbHelicopterLessonStep& Step = Steps[HelicopterLessonStep];
	const bool bActionDown = Step.Action != EWiesbadenHeliAction::MAX
		&& WiesbadenInputMap::IsHelicopterActionDown(PC, Step.Action);
	const bool bRelatedDown = Step.RelatedAction != EWiesbadenHeliAction::MAX
		&& WiesbadenInputMap::IsHelicopterActionDown(PC, Step.RelatedAction);
	if (HelicopterLessonStep == 0)
	{
		bHelicopterLessonCollectiveUp |= bActionDown;
		bHelicopterLessonCollectiveDown |= bRelatedDown;
	}
	if (HelicopterLessonStep == 8 && bActionDown)
	{
		bHelicopterLessonEngineChanged |=
			Heli->IsEngineRunning() != bHelicopterLessonStartEngineState;
	}
	if (HelicopterLessonStep == 9 && bActionDown)
	{
		bHelicopterLessonFireObserved = true;
	}
	if (HelicopterLessonStep == 1 || HelicopterLessonStep == 2)
	{
		HelicopterLessonStableTime = bActionDown
			? HelicopterLessonStableTime + GetWorld()->GetDeltaSeconds() : 0.0f;
	}

	const bool bSatisfied = IsHelicopterLessonStepSatisfied(*Heli);
	const bool bFinishConfirmed = HelicopterLessonStep == WiesbadenHelicopterLesson::PracticeStepCount
		&& bSatisfied;
	const int32 PreviousStep = HelicopterLessonStep;
	HelicopterLessonStep = WiesbadenHelicopterLesson::AdvanceStep(
		HelicopterLessonStep, bSatisfied, bSkip);
	if (HelicopterLessonStep != PreviousStep)
	{
		HelicopterLessonStableTime = 0.0f;
		if (HelicopterLessonStep == 8)
		{
			bHelicopterLessonEngineChanged = false;
			bHelicopterLessonStartEngineState = Heli->IsEngineRunning();
		}
		HelicopterLessonStartCamera = Heli->GetCameraMode();
		bHelicopterLessonStartSearchlightState = Heli->GetLightRig()
			&& Heli->GetLightRig()->AreSearchlightsOn();
		bHelicopterLessonStartLandingLightState = Heli->GetLightRig()
			&& Heli->GetLightRig()->IsLandingLightOn();
		if (bFinishConfirmed)
		{
			EndHelicopterLesson(FString());
			bHelicopterLessonCompleted = true;
			SaveHelicopterLessonCompletion();
			ShowTransientHint(TEXT("Flugstunde abgeschlossen. Im Pausenmenue jederzeit wiederholbar."));
		}
	}
}

void AWiesbadenVehicleHUD::DrawHelicopterLesson(float Width, float Height)
{
	const TArray<FWbHelicopterLessonStep>& Steps = WiesbadenHelicopterLesson::Steps();
	if (!Steps.IsValidIndex(HelicopterLessonStep))
	{
		return;
	}
	const FWbHelicopterLessonStep& Step = Steps[HelicopterLessonStep];
	constexpr float BoxW = 700.0f;
	constexpr float BoxH = 164.0f;
	const float X = FMath::Max(20.0f, (Width - BoxW) * 0.5f);
	const float Y = 36.0f;
	DrawRect(FLinearColor(0.02f, 0.025f, 0.035f, 0.88f), X, Y, BoxW, BoxH);
	DrawLine(X, Y, X + BoxW, Y, PanelEdge, 3.0f);
	DrawText(FString::Printf(TEXT("FLUGSTUNDE  %02d / %02d    %s"),
		FMath::Min(HelicopterLessonStep + 1, WiesbadenHelicopterLesson::StepCount),
		WiesbadenHelicopterLesson::StepCount, Step.Titel),
		DialText, X + 18.0f, Y + 14.0f, GEngine->GetMediumFont(), 1.0f);
	DrawText(Step.Anleitung, DialScale, X + 18.0f, Y + 50.0f,
		GEngine->GetSmallFont(), 0.95f);

	const FString Bestaetigung = GetHelicopterLessonConfirmKeys(HelicopterLessonStep);
	const FWiesbadenHeliBinding* Primary = nullptr;
	const FWiesbadenHeliBinding* Related = nullptr;
	for (const FWiesbadenHeliBinding& Binding : WiesbadenInputMap::HelicopterBindings())
	{
		if (Binding.Action == Step.Action) { Primary = &Binding; }
		if (Binding.Action == Step.RelatedAction) { Related = &Binding; }
	}
	// Im Abschlussschritt wird mit Enter/A bestaetigt - dort waere eine
	// Bindingspalte "F / Y" (Exit) eine falsche Zusage.
	const FString Keyboard = !Bestaetigung.IsEmpty()
		? Bestaetigung
		: (Primary
			? FString::Printf(TEXT("%s  %s"), Primary->Tastatur, Related ? Related->Tastatur : TEXT(""))
			: FString(TEXT("Enter bestaetigt")));
	const FString Gamepad = !Bestaetigung.IsEmpty()
		? Bestaetigung
		: (Primary
			? FString::Printf(TEXT("%s  %s"), Primary->Gamepad, Related ? Related->Gamepad : TEXT(""))
			: FString(TEXT("A bestaetigt")));
	DrawText(TEXT("TASTATUR"), DialScale, X + 18.0f, Y + 88.0f, GEngine->GetSmallFont(), 1.0f);
	DrawText(Keyboard, IndicatorOn, X + 18.0f, Y + 108.0f, GEngine->GetSmallFont(), 1.0f);
	DrawText(TEXT("GAMEPAD"), DialScale, X + 360.0f, Y + 88.0f, GEngine->GetSmallFont(), 1.0f);
	DrawText(Gamepad, IndicatorOn, X + 360.0f, Y + 108.0f, GEngine->GetSmallFont(), 1.0f);
	DrawText(FString::Printf(TEXT("%s    Esc pausiert - Flugstunde im Pausemenue abbrechen"),
		*GetHelicopterLessonSkipHint(HelicopterLessonStep)),
		TellTaleOff, X + 18.0f, Y + 140.0f, GEngine->GetSmallFont(), 0.9f);
}

void AWiesbadenVehicleHUD::DrawHelicopterLessonOffer(float Width, float Height)
{
	constexpr float BoxW = 520.0f;
	constexpr float BoxH = 170.0f;
	const float X = (Width - BoxW) * 0.5f;
	const float Y = (Height - BoxH) * 0.5f;
	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.62f), 0.0f, 0.0f, Width, Height);
	DrawRect(DialBackground, X, Y, BoxW, BoxH);
	DrawLine(X, Y, X + BoxW, Y, PanelEdge, 3.0f);
	DrawText(TEXT("HELIKOPTER-FLUGSTUNDE"), IndicatorOn,
		X + 22.0f, Y + 20.0f, GEngine->GetLargeFont(), 1.0f);
	DrawText(TEXT("Gefuehrte Uebungen im freien Flug; Tastatur und Gamepad nebeneinander."),
		DialText, X + 22.0f, Y + 66.0f, GEngine->GetSmallFont(), 1.0f);
	DrawText(TEXT("Feuer wird nur trocken geuebt: keine Schuesse, kein Schaden, keine Munition."),
		DialScale, X + 22.0f, Y + 92.0f, GEngine->GetSmallFont(), 0.95f);
	DrawText(TEXT("Enter / A: starten     N / B: spaeter"), IndicatorOn,
		X + 22.0f, Y + 128.0f, GEngine->GetMediumFont(), 0.95f);
}

void AWiesbadenVehicleHUD::SaveHelicopterLessonCompletion()
{
	if (!GConfig)
	{
		return;
	}
	GConfig->SetBool(TEXT("WiesbadenReal.Optionen"),
		TEXT("HelikopterFlugstundeAbgeschlossen"), bHelicopterLessonCompleted,
		GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
	bHelicopterLessonPreferenceLoaded = true;
}

void AWiesbadenVehicleHUD::GetPauseMenuEntries(TArray<FString>& OutEntries)
{
	OutEntries.Reset();
	OutEntries.Add(TEXT("Weiterspielen"));
	OutEntries.Add(TEXT("Steuerung einblenden"));
	OutEntries.Add(TEXT("Optionen"));
	OutEntries.Add(TEXT("Karte zeigen / verbergen (M)"));
	// Entwicklerbefehle. Ohne sie kostet jede Pruefung eine Fahrt quer durch
	// die Stadt - der Weg zur Platter Strasse dauert im Spiel Minuten.
	OutEntries.Add(TEXT("Entwickler: zurueck zur Platter Strasse 146"));
	OutEntries.Add(TEXT("Entwickler: zur Nerobergbahn"));
	OutEntries.Add(TEXT("Entwickler: zum Garten Nerotal 48"));
	OutEntries.Add(TEXT("Entwickler: Fahrzeug aufrichten"));
	OutEntries.Add(TEXT("Entwickler: Verkehr an/aus"));
	OutEntries.Add(TEXT("Helikopter-Flugstunde starten / wiederholen"));
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

	// Flanke ueber WiesbadenOptions::EdgePressed - DIESELBE Funktion, die das
	// Optionsfenster benutzt. Vorher stand die Mechanik hier und dort je
	// einmal; dass beide gleich sind, war eine Behauptung. Jetzt ist es eine
	// Tatsache, die der Uebersetzer haelt.
	auto Edge = [PC](const FKey& Key, bool& bHeld) -> bool
	{
		return WiesbadenOptions::EdgePressed(PC->IsInputKeyDown(Key), bHeld);
	};

	// Escape oder Start am Gamepad schaltet um.
	if (Edge(EKeys::Escape, bPauseKeyHeld)
		|| PC->IsInputKeyDown(EKeys::Gamepad_Special_Right))
	{
		if (PauseView == EWbPauseView::Optionen)
		{
			// Aus den Optionen nur eine Ebene zurueck ins Pausemenue, nicht
			// gleich das Spiel fortsetzen.
			PauseView = EWbPauseView::Menue;
			PauseSelection = 0;
		}
		else
		{
			PauseView = (PauseView == EWbPauseView::Aus)
				? EWbPauseView::Menue
				: EWbPauseView::Aus;
			bPaused = (PauseView != EWbPauseView::Aus);
			PauseSelection = 0;

			// Die Zeit wirklich anhalten. Ein Menue, hinter dem der Verkehr
			// weiterfaehrt, ist keine Pause - und beim Zuruecksetzen der Position
			// waere die Stadt sonst schon woanders.
			PC->SetPause(bPaused);
			PC->bShowMouseCursor = bPaused;
		}
	}

	if (PauseView == EWbPauseView::Aus)
	{
		return;
	}

	// Das Optionsfenster hat Vorrang und wertet seine Tasten selbst aus. Die
	// Bedingung ist DIESELBE, die oben ueber das Zeichnen entscheidet.
	if (PauseView == EWbPauseView::Optionen)
	{
		UpdateOptions();
		return;
	}

	TArray<FString> Entries;
	GetPauseMenuEntries(Entries);

	// Tastatur UND Controller an derselben Zeile. Der Grund ist derselbe wie
	// an der Achse des Wagens: wer wechselt, trifft dieselbe Taste. Ohne
	// den Pad-Teil war das Pausemenue an einem Controller nicht zu bedienen.
	if (Edge(EKeys::Up, bMenuUpHeld) || Edge(EKeys::W, bMenuUpHeld)
		|| Edge(EKeys::Gamepad_DPad_Up, bMenuUpHeld))
	{
		PauseSelection = (PauseSelection + Entries.Num() - 1) % Entries.Num();
	}
	if (Edge(EKeys::Down, bMenuDownHeld) || Edge(EKeys::S, bMenuDownHeld)
		|| Edge(EKeys::Gamepad_DPad_Down, bMenuDownHeld))
	{
		PauseSelection = (PauseSelection + 1) % Entries.Num();
	}
	if (Edge(EKeys::Enter, bMenuEnterHeld) || Edge(EKeys::Gamepad_FaceButton_Bottom, bMenuEnterHeld))
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
		PauseView = EWbPauseView::Aus;
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
		// Ton-Unterfenster oeffnen - NICHT entpausieren: die Lautstaerke wird im
		// angehaltenen Spiel geregelt, Escape fuehrt zurueck ins Pausemenue.
		PauseView = EWbPauseView::Optionen;
		OptionSelection = 0;
		bOptionInputAnnounced = false;
		break;

	case 3:
		// Weltkarte oeffnen (im Spiel sonst per M-Taste); frisch eingepasst wie
		// dort. Ohne diesen Fall loeste "Karte" durch einen alten Index-Versatz
		// einen Teleport aus (Menue war um einen Eintrag verrutscht).
		bWorldMapOpen = true;
		MapZoom = 1.0f;
		bMapCentreInit = false;
		Unpause();
		break;

	case 4:
	case 5:
	case 6:
	{
		// Zuruecksetzen an einen festen Ort. Die Zielpunkte und Fallhoehe leben
		// datenrein in FWiesbadenDevActions - dieselbe Logik nutzt der
		// Konsolenbefehl WbTeleport (DRY, unter Automation getestet).
		const EWiesbadenDevTeleport Target = static_cast<EWiesbadenDevTeleport>(Index - 4);
		if (APawn* Pawn = PC->GetPawn())
		{
			Pawn->SetActorLocation(FWiesbadenDevActions::TeleportSpawnCm(Target),
				/*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
		}
		Unpause();
		break;
	}

	case 7:
	{
		// Fahrzeug aufrichten: Nick/Roll auf 0, leicht anheben und auf die
		// Raeder fallen lassen. Direkte Nothilfe gegen umgekippte oder an der
		// Geometrie verhakte Fahrzeuge - bis die Chassis-Kollision sauber ist.
		// Logik geteilt mit WbResetVehicle (FWiesbadenDevActions::UprightTransform).
		if (APawn* Pawn = PC->GetPawn())
		{
			const FTransform Auf = FWiesbadenDevActions::UprightTransform(Pawn->GetActorTransform());
			Pawn->SetActorLocationAndRotation(Auf.GetLocation(), Auf.Rotator(),
				/*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
		}
		Unpause();
		break;
	}	case 8:
		if (UWiesbadenCitySubsystem* City = HudWorld->GetSubsystem<UWiesbadenCitySubsystem>())
		{
			// Verkehr aus: die schnellste Art zu pruefen, ob ein Ruckler vom
			// Verkehr kommt oder von der Darstellung.
			const float Density = City->TrafficSimulation.GetDensity();
			City->TrafficSimulation.Settings.TrafficDensity = (Density > 0.01f) ? 0.0f : 0.5f;
		}
		Unpause();
		break;

	case 9:
		if (bHelicopterLessonActive)
		{
			EndHelicopterLesson(TEXT("Flugstunde beendet."), LastHelicopterPawn.Get());
			Unpause();
		}
		else if (GetPlayerHelicopter())
		{
			Unpause();
			StartHelicopterLesson();
		}
		else
		{
			ShowTransientHint(TEXT("Zum Starten der Flugstunde zuerst den Helikopter besteigen."));
			Unpause();
		}
		break;

	case 10:
		FPlatformMisc::RequestExit(false);
		break;


	default:
		break;
	}
}

// =====================================================================
// Intro, Titelbildschirm mit Hauptmenue
//
// Alles in diesem Block ist datenrein in WiesbadenMenuFlow (der Bildschirm-
// Ablauf, die Eintraege, der Intro-Takt) - hier passiert nur, was eine Welt
// braucht: lesen, zeichnen, den Sprung ausfuehren. So laesst sich der ganze
// Ablauf ohne Bildschirm pruefen (Test Menu.*).
// =====================================================================

bool AWiesbadenVehicleHUD::StickStep(float Axis, bool& bHeld)
{
	// Eine Flanke, keine Abfrage: ein gehaltener Stick wuerde sonst in jedem
	// Bild eine Zeile weiterspringen.
	const bool bDruecke = FMath::Abs(Axis) > 0.5f;
	const bool bFlanke = bDruecke && !bHeld;
	bHeld = bDruecke;
	return bFlanke;
}

void AWiesbadenVehicleHUD::RebuildMenuEntries()
{
	MenuEntries.Reset();
	MenuRows.Reset();
	switch (MenuScreen)
	{
	case EWbMenuScreen::Titel:
		WiesbadenMenu::BuildHauptmenu(MenuEntries);
		break;

	case EWbMenuScreen::Optionen:
		WiesbadenMenu::BuildGruppenliste(MenuEntries);
		break;

	case EWbMenuScreen::Gruppe:
		{
			TArray<FWbOptionRow> Zeilen;
			BuildOptionRows(Zeilen);
			WiesbadenMenu::BuildGruppe(MenuGruppe, Zeilen, MenuEntries);
			// Die Zeilen der Seite merken: der Wert eines Eintrags wird ueber
			// seine Kennung in genau dieser Liste nachgeschlagen, und die
			// Zeilenliste haengt davon ab, welche Systeme es gerade gibt.
			for (const FWbOptionRow& Zeile : Zeilen)
			{
				if (Zeile.Group == MenuGruppe)
				{
					MenuRows.Add(Zeile);
				}
			}
		}
		break;

	case EWbMenuScreen::Belegung:
		// Die Belegungs-Seite hat keine Liste, sondern Kontext-Reiter; die
		// Zeilen kommen direkt aus der Tabelle.
		break;

	default:
		break;
	}
	MenuSelection = WiesbadenOptions::ClampRow(MenuSelection, MenuEntries.Num());
}

void AWiesbadenVehicleHUD::ActivateMenuEntry()
{
	const FWbMenuEntry* Eintrag = WiesbadenMenu::EntryAt(MenuEntries, MenuSelection);
	if (!Eintrag)
	{
		return;
	}

	if (Eintrag->Kind == EWbMenuEntryKind::Wert)
	{
		// Ein Wert-Eintrag verstellt sich beim BESTAETIGEN um einen Schritt
		// nach rechts. Die Pfeiltasten verstellen ihn ohnehin; damit ist
		// auch der Controller bedienbar, ohne die Schultertasten zu finden.
		for (const FWbOptionRow& Zeile : MenuRows)
		{
			if (Zeile.Id == Eintrag->Option)
			{
				StepOptionValue(Zeile, 1);
				return;
			}
		}
		return;
	}

	bool bGleicherBildschirm = false;
	const EWbMenuScreen Neu = WiesbadenMenu::Advance(MenuScreen, Eintrag, bGleicherBildschirm);
	if (bGleicherBildschirm)
	{
		return;
	}
	if (Eintrag->Action == EWbMenuAction::Beenden)
	{
		UE_LOG(LogWbCore, Log, TEXT("Menue: Beenden gewaehlt - Spiel wird beendet."));
		FGenericPlatformMisc::RequestExit(false);
		return;
	}
	MenuScreen = Neu;
	MenuSelection = 0;
	RebuildMenuEntries();
	UE_LOG(LogWbCore, Log, TEXT("Menue: Bildschirm %d, %d Eintraege, Auswahl %d."),
		static_cast<int32>(MenuScreen), MenuEntries.Num(), MenuSelection);
}

bool AWiesbadenVehicleHUD::UpdateTitleMenu()
{
	APlayerController* PC = GetOwningPlayerController();
	UWorld* HudWorld = GetWorld();
	if (!PC || !HudWorld)
	{
		return false;
	}

	// Einmal beim Start entscheiden. Der Aufruf geht an WiesbadenMenu, weil
	// dort die Regel steht, dass ein Automationslauf KEIN Intro bekommt: der
	// Rauchtest wartet auf Belege aus dem laufenden Spiel, und ein wartender
	// Titelbildschirm wuerde ihn genau dort aufhalten.
	if (!bTitleGeprueft)
	{
		bTitleGeprueft = true;
		if (WiesbadenMenu::ShouldShowIntro(bIntroGewuenscht, FCommandLine::Get()))
		{
			MenuScreen = EWbMenuScreen::Intro;
			MenuOpenedAt = HudWorld->GetTimeSeconds();
			MenuSelection = 0;
			RebuildMenuEntries();
			UE_LOG(LogWbCore, Log, TEXT("HUD: Intro (%.1f s), dann Titelbildschirm mit Hauptmenue."),
				WiesbadenMenu::IntroSeconds());
		}
	}
	if (MenuScreen == EWbMenuScreen::MAX)
	{
		return false;
	}

	// Solange der Titel offen ist, gibt es nichts zu pausieren und keine Karte.
	if (PauseView != EWbPauseView::Aus)
	{
		PauseView = EWbPauseView::Aus;
		bWorldMapOpen = false;
		bMapSearchActive = false;
	}

	const double Jetzt = HudWorld->GetTimeSeconds();
	// Mehrere Tasten je Flanke, eine Flanke je Richtung: Tastatur UND Pad an
	// derselben Stelle. Genau die Regel, die an der Achse des Wagens
	// auskommentiert ist (A hupt wie am Pad) - hier ist es nur die Eingabe.
	auto Runter = [PC](std::initializer_list<FKey> Keys)
	{
		for (const FKey& Key : Keys)
		{
			if (PC->IsInputKeyDown(Key))
			{
				return true;
			}
		}
		return false;
	};

	const bool bHoch = Runter({EKeys::Up, EKeys::W, EKeys::Gamepad_DPad_Up})
		|| StickStep(-PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftY), bTitleStickHochHeld);
	const bool bRunter = Runter({EKeys::Down, EKeys::S, EKeys::Gamepad_DPad_Down})
		|| StickStep(PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftY), bTitleStickRunterHeld);
	const bool bLinks = Runter({EKeys::Left, EKeys::A,
			EKeys::Gamepad_DPad_Left, EKeys::Gamepad_LeftShoulder})
		|| StickStep(-PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX), bTitleStickLinksHeld);
	const bool bRechts = Runter({EKeys::Right, EKeys::D,
			EKeys::Gamepad_DPad_Right, EKeys::Gamepad_RightShoulder})
		|| StickStep(PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX), bTitleStickRechtsHeld);
	const bool bBest = Runter({EKeys::Enter, EKeys::Gamepad_FaceButton_Bottom});
	const bool bZurueck = Runter({EKeys::Escape, EKeys::Gamepad_FaceButton_Right});

	const bool bTasteHoch = WiesbadenOptions::EdgePressed(bHoch, bTitleHochHeld);
	const bool bTasteRunter = WiesbadenOptions::EdgePressed(bRunter, bTitleRunterHeld);
	const bool bTasteLinks = WiesbadenOptions::EdgePressed(bLinks, bTitleLinksHeld);
	const bool bTasteRechts = WiesbadenOptions::EdgePressed(bRechts, bTitleRechtsHeld);
	const bool bTasteBest = WiesbadenOptions::EdgePressed(bBest, bTitleEnterHeld);
	const bool bTasteZurueck = WiesbadenOptions::EdgePressed(bZurueck, bTitleZurueckHeld);

	// --- Intro: laeuft von selbst, jeder Druck ueberspringt -----------------
	if (MenuScreen == EWbMenuScreen::Intro)
	{
		if (bTasteBest || bTasteZurueck
			|| (Jetzt - MenuOpenedAt) >= WiesbadenMenu::IntroSeconds())
		{
			MenuScreen = EWbMenuScreen::Titel;
			MenuOpenedAt = Jetzt;
			MenuSelection = 0;
			RebuildMenuEntries();
			UE_LOG(LogWbCore, Log, TEXT("HUD: Intro vorbei - Titelbildschirm mit Hauptmenue."));
		}
		return true;
	}

	// --- Eine Ebene zurueck (Escape / B) -----------------------------------
	if (bTasteZurueck)
	{
		EWbMenuScreen Neu = MenuScreen;
		WiesbadenMenu::Back(Neu);
		MenuScreen = Neu;
		MenuSelection = 0;
		if (MenuScreen != EWbMenuScreen::MAX)
		{
			RebuildMenuEntries();
		}
		UE_LOG(LogWbCore, Log, TEXT("Menue: zurueck -> Bildschirm %d."), static_cast<int32>(MenuScreen));
		return true;
	}

	// --- Belegungs-Seite: Kontext wechseln, keine Liste --------------------
	if (MenuScreen == EWbMenuScreen::Belegung)
	{
		const int32 Schritt = (bTasteRunter ? 1 : 0) - (bTasteHoch ? 1 : 0);
		if (Schritt != 0)
		{
			const int32 Anzahl = static_cast<int32>(EWbControlContext::MAX);
			MenuBelegung = static_cast<EWbControlContext>(
				((static_cast<int32>(MenuBelegung) + Schritt) % Anzahl + Anzahl) % Anzahl);
		}
		if (bTasteBest)
		{
			MenuScreen = EWbMenuScreen::Titel;
			MenuSelection = 0;
			RebuildMenuEntries();
		}
		return true;
	}

	// --- Hauptmenue, Gruppenliste, Optionsseite -----------------------------
	if (bTasteHoch)
	{
		MenuSelection = WiesbadenMenu::MoveSelection(MenuSelection, MenuEntries, -1);
	}
	if (bTasteRunter)
	{
		MenuSelection = WiesbadenMenu::MoveSelection(MenuSelection, MenuEntries, 1);
	}

	// Links/rechts verstellen einen Wert - nur auf einer Optionsseite, und
	// nur wenn gerade ein Wert-Eintrag markiert ist. Sonst waeren die
	// Pfeiltasten auf dem Titelbildschirm etwas, das ins Leere zeigt.
	if ((bTasteLinks || bTasteRechts) && MenuScreen == EWbMenuScreen::Gruppe)
	{
		const FWbMenuEntry* Eintrag = WiesbadenMenu::EntryAt(MenuEntries, MenuSelection);
		if (Eintrag && Eintrag->Kind == EWbMenuEntryKind::Wert)
		{
			for (const FWbOptionRow& Zeile : MenuRows)
			{
				if (Zeile.Id == Eintrag->Option)
				{
					StepOptionValue(Zeile, bTasteRechts ? 1 : -1);
					break;
				}
			}
		}
	}

	if (bTasteBest)
	{
		ActivateMenuEntry();
	}
	return true;
}

void AWiesbadenVehicleHUD::DrawTextMittig(const FString& Text, FLinearColor Color,
	float MitteX, float Y, UFont* Font, float Scale)
{
	UFont* Genutzt = Font ? Font : (GEngine ? GEngine->GetMediumFont() : nullptr);
	const int32 RohBreite = Genutzt ? Genutzt->GetStringSize(*Text) : 0;
	DrawText(Text, Color, MitteX - RohBreite * 0.5f * Scale, Y, Genutzt, Scale);
}

void AWiesbadenVehicleHUD::DrawTextRechts(const FString& Text, FLinearColor Color,
	float RechtsX, float Y, UFont* Font, float Scale)
{
	UFont* Genutzt = Font ? Font : (GEngine ? GEngine->GetMediumFont() : nullptr);
	const int32 RohBreite = Genutzt ? Genutzt->GetStringSize(*Text) : 0;
	DrawText(Text, Color, RechtsX - RohBreite * Scale, Y, Genutzt, Scale);
}

void AWiesbadenVehicleHUD::DrawProfilTafel(float X, float Y)
{
	// Daten aus dem getesteten Fenster-Profiler des Subsystems - das HUD misst
	// nicht selbst (eine zweite Messstelle waere eine zweite Wahrheit).
	const UWorld* ProfilWorld = GetWorld();
	const UWiesbadenCitySubsystem* City = ProfilWorld
		? ProfilWorld->GetSubsystem<UWiesbadenCitySubsystem>() : nullptr;
	if (!City)
	{
		return;
	}

	const TArray<FString> Zeilen = WbProfilZeilen(City->GetFrameReport());
	UFont* Schrift = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (Zeilen.Num() == 0 || !Schrift)
	{
		return;
	}

	// Rohmasse der Schrift bei Skala 1; die Tafelgroesse rechnet sie mit der
	// Stilskala (0.5 = halb so gross), damit Text und Flaeche zusammenpassen.
	float RohBreite = 0.0f;
	for (const FString& Zeile : Zeilen)
	{
		RohBreite = FMath::Max(RohBreite, static_cast<float>(Schrift->GetStringSize(*Zeile)));
	}
	const float RohHoehe = Schrift->GetMaxCharHeight();
	const FWbProfilOverlayStyle Stil;
	const FVector2D Groesse = WbProfilTafelGroesse(Zeilen.Num(), RohBreite, RohHoehe, Stil);

	// Halbtransparenter Hintergrund - das Bild darunter bleibt erkennbar.
	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, Stil.HintergrundAlpha),
		X, Y, Groesse.X, Groesse.Y);

	float ZeilenY = Y + Stil.RandPx * Stil.TextSkala;
	for (const FString& Zeile : Zeilen)
	{
		DrawText(Zeile, FLinearColor::White,
			X + Stil.RandPx * Stil.TextSkala, ZeilenY, Schrift, Stil.TextSkala);
		ZeilenY += (RohHoehe + Stil.ZeilenAbstandPx) * Stil.TextSkala;
	}
}

void AWiesbadenVehicleHUD::DrawTitleScreen(float Width, float Height)
{
	const UWorld* HudWorld = GetWorld();
	const double Jetzt = HudWorld ? HudWorld->GetTimeSeconds() : 0.0;

	if (MenuScreen == EWbMenuScreen::Intro)
	{
		// Das Intro ist schwarz und hat nur EINEN Satz. Wer es ueberspringt,
		// sieht denselben letzten Satz - nicht etwa gar nichts.
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 1.0f), 0.0f, 0.0f, Width, Height);
		FString Zeile;
		float Deckkraft = 0.0f;
		if (!WiesbadenMenu::IntroPhase(Jetzt - MenuOpenedAt, Zeile, Deckkraft)
			|| Zeile.IsEmpty())
		{
			return;
		}
		DrawTextMittig(Zeile, FLinearColor(1.0f, 1.0f, 1.0f, Deckkraft),
			Width * 0.5f, Height * 0.45f, GEngine->GetLargeFont(), 1.0f);
		DrawTextMittig(TEXT("beliebige Taste ueberspringt"),
			FLinearColor(1.0f, 1.0f, 1.0f, 0.35f * Deckkraft),
			Width * 0.5f, Height * 0.56f, GEngine->GetSmallFont(), 1.0f);
		return;
	}

	// Alle Bildschirme ausser dem Intro liegen auf einer abgedunkelten Stadt.
	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.72f), 0.0f, 0.0f, Width, Height);

	// Der Titel steht IMMER oben: auf jeder Unterseite weiss man, wo man ist.
	DrawTextMittig(WiesbadenMenu::ScreenTitle(MenuScreen), DialText,
		Width * 0.5f, 48.0f, GEngine->GetLargeFont(), 1.0f);

	if (MenuScreen == EWbMenuScreen::Belegung)
	{
		DrawBindingPage(Width, Height);
		return;
	}

	constexpr float ZeilenHoehe = 34.0f;
	const float KastenBreite = FMath::Min(720.0f, Width - 80.0f);
	const float KastenHoehe = MenuEntries.Num() * ZeilenHoehe + 150.0f;
	const float X = (Width - KastenBreite) * 0.5f;
	const float Y = (Height - KastenHoehe) * 0.5f;
	DrawRect(DialBackground, X, Y, KastenBreite, KastenHoehe);

	// Seitenkopf: welche Gruppe ist offen, bzw. dass die Optionen kommen.
	if (MenuScreen == EWbMenuScreen::Gruppe)
	{
		TArray<FWbMenuEntry> Gruppen;
		WiesbadenMenu::BuildGruppenliste(Gruppen);
		FString GruppenName = TEXT("Optionen");
		for (const FWbMenuEntry& Gruppe : Gruppen)
		{
			if (Gruppe.Group == MenuGruppe)
			{
				GruppenName = Gruppe.Label;
				break;
			}
		}
		DrawText(FString::Printf(TEXT("Optionen  /  %s"), *GruppenName), IndicatorOn,
			X + 24.0f, Y + 18.0f, GEngine->GetMediumFont(), 1.0f);
	}

	for (int32 Index = 0; Index < MenuEntries.Num(); ++Index)
	{
		const FWbMenuEntry& Eintrag = MenuEntries[Index];
		const bool bGewaehlt = (Index == MenuSelection);
		const float ZeileY = Y + 62.0f + Index * ZeilenHoehe;
		DrawText((bGewaehlt ? TEXT("> ") : TEXT("  ")) + Eintrag.Label,
			bGewaehlt ? IndicatorOn : DialScale,
			X + 24.0f, ZeileY, GEngine->GetMediumFont(), 1.0f);

		// Bei einer Wertezeile steht der Wert RECHTS - dort, wo das Auge
		// nach der Aenderung sucht, nicht unter dem Label.
		if (Eintrag.Kind == EWbMenuEntryKind::Wert)
		{
			for (const FWbOptionRow& Zeile : MenuRows)
			{
				if (Zeile.Id == Eintrag.Option)
				{
				DrawTextRechts(WiesbadenOptions::FormatValue(Zeile.Kind, ReadOptionValue(Zeile)),
					bGewaehlt ? IndicatorOn : DialText,
					X + KastenBreite - 24.0f, ZeileY,
					GEngine->GetMediumFont(), 1.0f);
					break;
				}
			}
		}
	}

	// Fusszeile: die Erklaerung des gewaehlten Eintrags, darunter die
	// Bedienung. Ohne die Fusszeile weiss man nicht, was "Warp zur Strasse"
	// tun soll, wenn man darauf steht.
	const FWbMenuEntry* Gewaehlt = WiesbadenMenu::EntryAt(MenuEntries, MenuSelection);
	if (Gewaehlt && !Gewaehlt->Hinweis.IsEmpty())
	{
		DrawText(Gewaehlt->Hinweis, TellTaleOff,
			X + 24.0f, Y + KastenHoehe - 62.0f, GEngine->GetSmallFont(), 1.0f);
	}
	DrawText(TEXT("Pfeile waehlen   Enter bestaetigt   Escape zurueck   (Pad: Steuerkreuz, A, B)"),
		TellTaleOff, X + 24.0f, Y + KastenHoehe - 34.0f, GEngine->GetSmallFont(), 1.0f);
}

void AWiesbadenVehicleHUD::DrawBindingPage(float Width, float Height)
{
	TArray<FString> Kontexte;
	WiesbadenMenu::GetContextLabels(Kontexte);

	// Reiter: welcher Kontext offen ist. Der angezeichte Name kommt aus der
	// Tabelle, nicht aus dem Enum - sonst muesste die Seite an zwei Stellen
	// gepflegt werden.
	const int32 KontextIndex = static_cast<int32>(MenuBelegung);
	const FString KontextName = Kontexte.IsValidIndex(KontextIndex)
		? Kontexte[KontextIndex] : TEXT("?");

	const float KastenBreite = FMath::Min(900.0f, Width - 60.0f);
	const float X = (Width - KastenBreite) * 0.5f;
	const float Y = 120.0f;
	TArray<FWbControlBinding> Bindungen;
	WiesbadenMenu::ControlBindings(MenuBelegung, Bindungen);
	const float KastenHoehe = Bindungen.Num() * 24.0f + 96.0f;
	DrawRect(DialBackground, X, Y, KastenBreite, KastenHoehe);
	DrawText(FString::Printf(TEXT("STEUERUNG  /  %s"), *KontextName), IndicatorOn,
		X + 20.0f, Y + 14.0f, GEngine->GetMediumFont(), 1.0f);
	DrawText(TEXT("Aktion"), TellTaleOff, X + 20.0f, Y + 52.0f, GEngine->GetSmallFont(), 1.0f);
	DrawText(TEXT("Tastatur"), TellTaleOff, X + 250.0f, Y + 52.0f, GEngine->GetSmallFont(), 1.0f);
	DrawText(TEXT("Xbox 360"), TellTaleOff, X + 470.0f, Y + 52.0f, GEngine->GetSmallFont(), 1.0f);

	for (int32 Index = 0; Index < Bindungen.Num(); ++Index)
	{
		const FString Text = FormatBindingLine(Bindungen[Index]);
		DrawText(Text, DialScale, X + 20.0f, Y + 70.0f + Index * 24.0f,
			GEngine->GetSmallFont(), 1.0f);
	}
	DrawText(TEXT("Pfeil hoch/runter: anderer Bereich   Enter: zurueck zum Hauptmenue"),
		TellTaleOff, X + 20.0f, Y + KastenHoehe - 26.0f, GEngine->GetSmallFont(), 1.0f);
}

void AWiesbadenVehicleHUD::DrawPauseMenu(float Width, float Height)
{
	TArray<FString> Entries;
	GetPauseMenuEntries(Entries);

	constexpr float LineHeight = 26.0f;
	constexpr float BoxWidth = 520.0f;
	const float BoxHeight = Entries.Num() * LineHeight + 112.0f;

	const float X = (Width - BoxWidth) * 0.5f;
	const float Y = (Height - BoxHeight) * 0.5f;

	// Den ganzen Schirm abdunkeln, damit das Menue lesbar ist.
	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), 0.0f, 0.0f, Width, Height);
	DrawRect(DialBackground, X, Y, BoxWidth, BoxHeight);

	DrawText(TEXT("PAUSE"), DialText, X + 24.0f, Y + 20.0f, GEngine->GetLargeFont(), 1.0f);

	HelicopterLessonMenuIndex = Entries.Num() - 2;
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const bool bSelected = (Index == PauseSelection);		FString Label = Entries[Index];
		if (bHelicopterLessonActive && Index == HelicopterLessonMenuIndex)
		{
			Label = TEXT("Flugstunde abbrechen");
		}
		const FString Line = (bSelected ? TEXT("> ") : TEXT("  ")) + Label;
		DrawText(Line, bSelected ? IndicatorOn : DialScale,
			X + 24.0f, Y + 60.0f + Index * LineHeight,
			GEngine->GetMediumFont(), 1.0f);
	}

	DrawText(TEXT("Pfeile waehlen   Eingabe bestaetigen   Esc schliesst"),
		TellTaleOff, X + 24.0f, Y + BoxHeight - 24.0f, GEngine->GetSmallFont(), 1.0f);

	if (bHelicopterLessonActive && HelicopterLessonMenuIndex >= 0
		&& PauseSelection == HelicopterLessonMenuIndex)
	{
		DrawText(TEXT("Unterricht pausiert - bestaetigen beendet die Flugstunde"),
			GaugeWarn, X + 24.0f, Y + BoxHeight - 42.0f,
			GEngine->GetSmallFont(), 1.0f);
	}
	if (PauseSelection == 3)
	{
		DrawText(TEXT("Karte im Spiel: M (Tastatur) / Gamepad-Start"),
			DialScale, X + 24.0f, Y + 60.0f + (PauseSelection + 1) * LineHeight,
			GEngine->GetSmallFont(), 1.0f);
	}
}

void AWiesbadenVehicleHUD::GetAudioBusLabels(TArray<FString>& OutLabels)
{
	// Reihenfolge == EWbAudioBus 0..Vehicle: Zeile i gehoert zu (EWbAudioBus)i.
	// Der Test Vehicles.HUD.AudioBusLabels haelt fest, dass es genau so viele
	// Zeilen wie Busse gibt - so wandert die Liste mit dem Enum mit.
	OutLabels.Reset();
	OutLabels.Add(TEXT("Gesamt"));     // Master
	OutLabels.Add(TEXT("Musik"));      // Music
	OutLabels.Add(TEXT("Effekte"));    // SFX
	OutLabels.Add(TEXT("Ambiente"));   // Ambience
	OutLabels.Add(TEXT("Bedienung"));  // UI
	OutLabels.Add(TEXT("Stimme"));     // Voice
	OutLabels.Add(TEXT("Fahrzeug"));   // Vehicle
}

FString AWiesbadenVehicleHUD::FormatVolumePercent(float Slider01)
{
	const int32 Pct = FMath::RoundToInt(FMath::Clamp(Slider01, 0.0f, 1.0f) * 100.0f);
	return FString::Printf(TEXT("%d %%"), Pct);
}

void AWiesbadenVehicleHUD::WbOptionen()
{
	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		return;
	}

	// Setzt DENSELBEN Zustand wie der Weg ueber das Pausemenue - damit ist das
	// Fenster auch hier bedienbar, nicht nur sichtbar. Angehalten wird dabei
	// bewusst NICHT: eine Pause ab Bild 0 haelt den Welt-Takt an, die Stadt
	// wird nie fertig gestreamt, und der Lauf koennte weder ein Bild machen
	// noch eine Wirkung zeigen.
	const bool bOeffnen = (PauseView != EWbPauseView::Optionen);
	PauseView = bOeffnen ? EWbPauseView::Optionen : EWbPauseView::Aus;
	if (bOeffnen)
	{
		OptionSelection = 0;
		bOptionInputAnnounced = false;
	}

	TArray<FWbOptionRow> Rows;
	BuildOptionRows(Rows);
	UE_LOG(LogWbCore, Log, TEXT("WbOptionen: Fenster %s, %d Zeilen."),
		bOeffnen ? TEXT("offen") : TEXT("zu"), Rows.Num());

	// Die ganze Liste einmal ins Protokoll - damit ist nachlesbar, welcher
	// Index zu welcher Zeile gehoert, ohne das Bild zu brauchen.
	if (bOeffnen)
	{
		for (int32 i = 0; i < Rows.Num(); ++i)
		{
			UE_LOG(LogWbCore, Log, TEXT("  [%2d] %-22s %s = %s"),
				i, *WiesbadenOptions::GroupLabel(Rows[i].Group), *Rows[i].Label,
				*WiesbadenOptions::FormatValue(Rows[i].Kind, ReadOptionValue(Rows[i])));
		}
	}
}

void AWiesbadenVehicleHUD::WbOption(int32 Zeile, int32 Schritte)
{
	TArray<FWbOptionRow> Rows;
	BuildOptionRows(Rows);
	if (!Rows.IsValidIndex(Zeile))
	{
		UE_LOG(LogWbCore, Warning,
			TEXT("WbOption: Zeile %d gibt es nicht (%d Zeilen)."), Zeile, Rows.Num());
		return;
	}

	OptionSelection = Zeile;
	const FWbOptionRow& Row = Rows[Zeile];
	const double Vorher = ReadOptionValue(Row);

	double Wert = Vorher;
	const int32 Richtung = (Schritte >= 0) ? 1 : -1;
	for (int32 i = 0; i < FMath::Abs(Schritte); ++i)
	{
		Wert = WiesbadenOptions::Step(Row.Kind, Wert, Richtung);
	}
	WriteOptionValue(Row, Wert);

	// ZURUECKGELESEN, nicht behauptet: was hier steht, hat das besitzende
	// System wirklich angenommen.
	const double Nachher = ReadOptionValue(Row);
	UE_LOG(LogWbCore, Log,
		TEXT("WbOption: %s  %s -> %s (gesetzt: %s)%s"),
		*Row.Label,
		*WiesbadenOptions::FormatValue(Row.Kind, Vorher),
		*WiesbadenOptions::FormatValue(Row.Kind, Wert),
		*WiesbadenOptions::FormatValue(Row.Kind, Nachher),
		WiesbadenOptions::ValueArrived(Wert, Nachher)
			? TEXT("") : TEXT("  ACHTUNG: NICHT ANGEKOMMEN"));
}

void AWiesbadenVehicleHUD::BuildOptionRows(TArray<FWbOptionRow>& OutRows) const
{
	// KEINE ZEILE, DEREN SYSTEM ES NICHT GIBT. Ein Regler ohne Mischpult oder
	// ein Verkehrsregler ohne Stadt waere genau die Option, die nichts tut.
	TArray<FString> BusLabels;
	int32 BusCount = 0;
	if (const APlayerController* PC = GetOwningPlayerController())
	{
		if (const UGameInstance* GI = PC->GetGameInstance())
		{
			if (GI->GetSubsystem<UWiesbadenAudioSubsystem>())
			{
				GetAudioBusLabels(BusLabels);
				BusCount = BusLabels.Num();
			}
		}
	}

	WiesbadenOptions::BuildRows(BusCount, BusLabels, OutRows);

	// Ohne Stadt-Subsystem faellt die Gruppe Spielwelt weg.
	const UWorld* HudWorld = GetWorld();
	const UWiesbadenCitySubsystem* City =
		HudWorld ? HudWorld->GetSubsystem<UWiesbadenCitySubsystem>() : nullptr;
	if (!City)
	{
		OutRows.RemoveAll([](const FWbOptionRow& Row)
		{
			return Row.Group == EWbOptionGroup::Spielwelt;
		});
	}

	// Ohne GameUserSettings faellt die Gruppe Grafik weg.
	if (!GEngine || !GEngine->GetGameUserSettings())
	{
		OutRows.RemoveAll([](const FWbOptionRow& Row)
		{
			return Row.Group == EWbOptionGroup::Grafik;
		});
	}
}

namespace
{
	/**
	 * Die vier Qualitaetsstufen - Lesen UND Schreiben in EINER Zeile.
	 *
	 * Hier lagen zwei spiegelbildliche Kaskaden: viermal ein
	 * Beschriftungsvergleich mit einem Getter, und gleich darunter noch einmal
	 * derselbe Vergleich mit dem passenden Setter. Zwei Listen, die
	 * zusammenpassen mussten, ohne dass irgendetwas das erzwungen haette.
	 *
	 * Jetzt steht jedes Paar einmal da. Ein Getter ohne seinen Setter ist
	 * nicht mehr aufschreibbar.
	 */
	struct FWbQualitaetsBindung
	{
		EWbOptionId Id;
		int32 (UGameUserSettings::*Lesen)() const;
		void  (UGameUserSettings::*Schreiben)(int32);
	};

	const FWbQualitaetsBindung QualitaetsBindungen[] =
	{
		{ EWbOptionId::Sichtweite, &UGameUserSettings::GetViewDistanceQuality,
		                           &UGameUserSettings::SetViewDistanceQuality },
		{ EWbOptionId::Schatten,   &UGameUserSettings::GetShadowQuality,
		                           &UGameUserSettings::SetShadowQuality },
		{ EWbOptionId::Effekte,    &UGameUserSettings::GetVisualEffectQuality,
		                           &UGameUserSettings::SetVisualEffectQuality },
		{ EWbOptionId::Texturen,   &UGameUserSettings::GetTextureQuality,
		                           &UGameUserSettings::SetTextureQuality },
	};

	const FWbQualitaetsBindung* FindeQualitaet(EWbOptionId Id)
	{
		for (const FWbQualitaetsBindung& B : QualitaetsBindungen)
		{
			if (B.Id == Id)
			{
				return &B;
			}
		}
		return nullptr;
	}

	/**
	 * Eine Kennung, die an kein System gebunden ist.
	 *
	 * Das darf es nicht geben - eine solche Zeile stuende im Menue, liesse
	 * sich verstellen und bewirkte nichts. Darum schlaegt sie hier an, statt
	 * still eine 0 zurueckzugeben.
	 */
	void MeldeUnversorgt(const FWbOptionRow& Row)
	{
		ensureMsgf(false,
			TEXT("Optionszeile %s (Kennung %d) ist an kein System gebunden - ")
			TEXT("sie braucht einen Zweig in ReadOptionValue UND WriteOptionValue."),
			*Row.Label, static_cast<int32>(Row.Id));
	}

	// Tripwire: eine neue Kennung faellt hier auf, bevor sie im Menue steht.
	// Wer sie hinzufuegt, muss beide Richtungen bedienen und diese Zahl
	// nachziehen - der Uebersetzer laesst ihn sonst nicht durch.
	static_assert(static_cast<int32>(EWbOptionId::MAX) == 16,
		"Neue Optionskennung: sie braucht einen Zweig in ReadOptionValue UND "
		"in WriteOptionValue. Danach diese Zahl nachziehen.");
}

UWiesbadenCitySubsystem* AWiesbadenVehicleHUD::FindCity() const
{
	UWorld* HudWorld = GetWorld();
	return HudWorld ? HudWorld->GetSubsystem<UWiesbadenCitySubsystem>() : nullptr;
}

UWiesbadenAudioSubsystem* AWiesbadenVehicleHUD::FindAudio() const
{
	const APlayerController* PC = GetOwningPlayerController();
	const UGameInstance* GI = PC ? PC->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UWiesbadenAudioSubsystem>() : nullptr;
}

bool AWiesbadenVehicleHUD::StepOptionValue(const FWbOptionRow& Row, int32 Richtung)
{
	// EIN Schritt mit Nachweis - und der Weg, ueber den BEIDE Fenster einen
	// Wert verstellen (das alte Optionsfenster und die neue Optionsseite des
	// Hauptmenues). Zwei Kopien dieser Rechnung waeren zwei Orte, an denen
	// sich die Meldung "gesetzt: ..." unterscheiden koennte.
	if (Richtung == 0)
	{
		return false;
	}
	const double Alt = ReadOptionValue(Row);
	const double Neu = WiesbadenOptions::Step(Row.Kind, Alt, Richtung);
	if (FMath::IsNearlyEqual(Alt, Neu))
	{
		// Am Anschlag: kein "gesetzt: ..." - es wurde nichts bewirkt.
		return false;
	}
	WriteOptionValue(Row, Neu);

	// MIT NACHWEIS: was angekommen ist, wird zurueckgelesen und gemeldet.
	// Eine Option, die nichts bewirkt, faellt damit im Protokoll auf, nicht
	// erst im Bild.
	UE_LOG(LogWbCore, Log,
		TEXT("Optionen: %s %s -> %s (gesetzt: %s)"),
		*Row.Label,
		*WiesbadenOptions::FormatValue(Row.Kind, Alt),
		*WiesbadenOptions::FormatValue(Row.Kind, Neu),
		*WiesbadenOptions::FormatValue(Row.Kind, ReadOptionValue(Row)));
	return true;
}

double AWiesbadenVehicleHUD::ReadOptionValue(const FWbOptionRow& Row) const
{
	switch (Row.Id)
	{
	case EWbOptionId::Sichtweite:
	case EWbOptionId::Schatten:
	case EWbOptionId::Effekte:
	case EWbOptionId::Texturen:
	{
		const UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
		const FWbQualitaetsBindung* Bindung = FindeQualitaet(Row.Id);
		return (Settings && Bindung)
			? static_cast<double>((Settings->*(Bindung->Lesen))())
			: 0.0;
	}

	case EWbOptionId::Bildratengrenze:
	{
		const UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
		return Settings ? Settings->GetFrameRateLimit() : 0.0;
	}

	case EWbOptionId::Vollbild:
	{
		// Gelesen wird die EIGENTLICHE Fenstereinstellung, nicht ein
		// Merker: nur so steht im Menue, was wirklich gilt.
		const UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
		return Settings && Settings->GetFullscreenMode() != EWindowMode::Windowed ? 1.0 : 0.0;
	}

	case EWbOptionId::VSync:
	{
		const UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
		return Settings && Settings->IsVSyncEnabled() ? 1.0 : 0.0;
	}

	case EWbOptionId::Aufloesungsskalierung:
	{
		// Die Engine gibt vier Werte heraus, keinen Strukt-Wert: der
		// tatsaechliche Prozentwert steckt in CurrentScaleValue.
		const UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
		if (!Settings)
		{
			return 1.0;
		}
		float Normalisiert = 0.0f;
		float Prozent = 0.0f;
		float MinProzent = 0.0f;
		float MaxProzent = 0.0f;
		Settings->GetResolutionScaleInformationEx(Normalisiert, Prozent, MinProzent, MaxProzent);
		return static_cast<double>(Prozent) / 100.0;
	}

	case EWbOptionId::FpsAnzeige:
		return bFpsAnzeige ? 1.0 : 0.0;

	case EWbOptionId::StatEinblendung:
		return bStatEinblendung ? 1.0 : 0.0;

	case EWbOptionId::KollisionsOverlay:
		return bKollisionsOverlay ? 1.0 : 0.0;

	case EWbOptionId::TonBus:
	{
		if (Row.BusIndex < 0)
		{
			break;
		}
		const UWiesbadenAudioSubsystem* Audio = FindAudio();
		return Audio ? Audio->GetBusVolume(static_cast<EWbAudioBus>(Row.BusIndex)) : 1.0;
	}

	case EWbOptionId::MausEmpfindlichkeit:
		return MouseSensitivityFactor;

	case EWbOptionId::Steuerungshilfe:
		return bShowControlLegend ? 1.0 : 0.0;

	case EWbOptionId::Verkehrsdichte:
	{
		const UWiesbadenCitySubsystem* City = FindCity();
		return City ? City->TrafficSimulation.Settings.TrafficDensity : 0.0;
	}

	case EWbOptionId::Tageszeit:
	{
		// Gelesen wird die QUELLE, nicht die Uhr: bei Systemzeit laeuft die
		// Stunde weiter, und die Zeile wuerde im Menue vor sich hin zaehlen.
		const UWiesbadenCitySubsystem* City = FindCity();
		if (!City)
		{
			return 0.0;
		}
		return (City->Weather.Settings.TimeSource == EWiesbadenTimeSource::SystemClock)
			? -1.0
			: FMath::RoundToDouble(City->Weather.Settings.FixedHours);
	}

	case EWbOptionId::MAX:
		break;
	}

	MeldeUnversorgt(Row);
	return 0.0;
}

void AWiesbadenVehicleHUD::WriteOptionValue(const FWbOptionRow& Row, double Value)
{
	switch (Row.Id)
	{
	case EWbOptionId::Sichtweite:
	case EWbOptionId::Schatten:
	case EWbOptionId::Effekte:
	case EWbOptionId::Texturen:
	case EWbOptionId::Bildratengrenze:
	{
		UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
		if (!Settings)
		{
			return;
		}
		if (const FWbQualitaetsBindung* Bindung = FindeQualitaet(Row.Id))
		{
			(Settings->*(Bindung->Schreiben))(FMath::Clamp(FMath::RoundToInt(Value), 0, 4));
		}
		else
		{
			Settings->SetFrameRateLimit(static_cast<float>(Value));
		}

		// ApplyNonResolutionSettings, NICHT ApplySettings: letzteres fasst auch
		// Aufloesung und Fenstermodus an und laesst das Fenster flackern, ohne
		// dass jemand daran gedreht haette.
		Settings->ApplyNonResolutionSettings();
		Settings->SaveSettings();
		return;
	}

	case EWbOptionId::TonBus:
	{
		if (Row.BusIndex < 0)
		{
			break;
		}
		if (UWiesbadenAudioSubsystem* Audio = FindAudio())
		{
			// SetBusVolume wendet sofort an UND speichert selbst.
			Audio->SetBusVolume(static_cast<EWbAudioBus>(Row.BusIndex),
				static_cast<float>(Value));
		}
		return;
	}

	case EWbOptionId::Vollbild:
	{
		UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
		if (!Settings)
		{
			return;
		}
		// Hier ist ApplySettings richtig und ApplyNonResolutionSettings
		// falsch: der Fenstermodus IST eine Aufloesungseinstellung, und
		// ApplyNonResolutionSettings wuerde ihn stehen lassen und die Zeile
		// waere eine Anzeige, die nichts bewirkt.
		Settings->SetFullscreenMode(Value >= 0.5
			? EWindowMode::Fullscreen : EWindowMode::Windowed);
		Settings->ApplySettings(false);
		Settings->SaveSettings();
		return;
	}

	case EWbOptionId::VSync:
	{
		UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
		if (!Settings)
		{
			return;
		}
		Settings->SetVSyncEnabled(Value >= 0.5);
		Settings->ApplyNonResolutionSettings();
		Settings->SaveSettings();
		return;
	}

	case EWbOptionId::Aufloesungsskalierung:
	{
		UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
		if (!Settings)
		{
			return;
		}
		const int32 Prozent = FMath::Clamp(FMath::RoundToInt(Value * 100.0), 50, 100);
		Settings->SetResolutionScaleValueEx(static_cast<float>(Prozent));
		Settings->ApplyNonResolutionSettings();
		Settings->SaveSettings();
		return;
	}

	case EWbOptionId::FpsAnzeige:
		bFpsAnzeige = (Value >= 0.5);
		GConfig->SetBool(TEXT("WiesbadenReal.Optionen"), TEXT("FpsAnzeige"),
			bFpsAnzeige, GGameUserSettingsIni);
		return;

	case EWbOptionId::StatEinblendung:
	{
		bStatEinblendung = (Value >= 0.5);
		GConfig->SetBool(TEXT("WiesbadenReal.Optionen"), TEXT("StatEinblendung"),
			bStatEinblendung, GGameUserSettingsIni);
		// Frueher: stat fps/unit/game einschalten. Diese Engine-Tabellen liessen
		// sich weder verkleinern noch halbtransparent machen (feste Fonts,
		// undurchsichtige Hintergrundkacheln, StatsRender2.cpp) und deckten das
		// Bild zu. Jetzt zeichnet das HUD seine eigene Profil-Tafel
		// (DrawProfilTafel); "stat none" raeumt nur noch Reste auf.
		if (UWorld* Welt = GetWorld())
		{
			if (GEngine)
			{
				GEngine->Exec(Welt, TEXT("stat none"));
			}
		}
		GConfig->Flush(false, GGameUserSettingsIni);
		return;
	}

	case EWbOptionId::KollisionsOverlay:
	{
		bKollisionsOverlay = (Value >= 0.5);
		GConfig->SetBool(TEXT("WiesbadenReal.Optionen"), TEXT("KollisionsOverlay"),
			bKollisionsOverlay, GGameUserSettingsIni);
		// "show collision" schaltet UM, und UGameViewportClient::
		// ToggleShowCollision ist privat - der Konsolenbefehl ist der
		// oeffentliche Weg dorthin. Deshalb nur beim Einschalten ausfuehren
		// und den Merker fuehren: sonst waere der Bildschirm nach dem
		// Aus-Schalten beim naechsten Laden wieder an.
		if (bKollisionsOverlay && GEngine)
		{
			GEngine->Exec(GetWorld(), TEXT("show Collision"));
		}
		GConfig->Flush(false, GGameUserSettingsIni);
		return;
	}

	case EWbOptionId::MausEmpfindlichkeit:
		MouseSensitivityFactor = static_cast<float>(Value);
		GConfig->SetFloat(TEXT("WiesbadenReal.Optionen"),
			TEXT("MausEmpfindlichkeit"), MouseSensitivityFactor, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
		return;

	case EWbOptionId::Steuerungshilfe:
		bShowControlLegend = (Value >= 0.5);
		// Dauerhaft eingeblendet heisst: der Verblass-Zaehler darf nicht
		// weiterlaufen, sonst ist die Hilfe nach ein paar Sekunden wieder weg.
		ElapsedSeconds = 0.0f;
		GConfig->SetBool(TEXT("WiesbadenReal.Optionen"),
			TEXT("Steuerungshilfe"), bShowControlLegend, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
		return;

	case EWbOptionId::Verkehrsdichte:
	{
		UWiesbadenCitySubsystem* City = FindCity();
		if (!City)
		{
			return;
		}
		City->TrafficSimulation.Settings.TrafficDensity = static_cast<float>(Value);
		GConfig->SetFloat(TEXT("WiesbadenReal.Optionen"),
			TEXT("Verkehrsdichte"), static_cast<float>(Value), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
		return;
	}

	case EWbOptionId::Tageszeit:
	{
		UWiesbadenCitySubsystem* City = FindCity();
		if (!City)
		{
			return;
		}
		if (Value < 0.0)
		{
			City->Weather.SetTimeSource(EWiesbadenTimeSource::SystemClock);
		}
		else
		{
			City->Weather.SetTimeSource(EWiesbadenTimeSource::FixedHour,
				static_cast<float>(Value));
		}
		// Sofort nachziehen, sonst steht die Sonne bis zum naechsten Takt falsch.
		City->Weather.UpdateClock(FDateTime::UtcNow(), FDateTime::Now());
		StoredTimeOfDay = static_cast<float>(Value);
		GConfig->SetFloat(TEXT("WiesbadenReal.Optionen"),
			TEXT("Tageszeit"), static_cast<float>(Value), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
		return;
	}

	case EWbOptionId::MAX:
		break;
	}

	MeldeUnversorgt(Row);
}

void AWiesbadenVehicleHUD::LoadPersistentOptions()
{
	// NUR WAS WIRKLICH IN DER DATEI STEHT. Fehlt ein Schluessel, hat niemand
	// daran gedreht - dann bleibt der Wert, den das Spiel selbst mitbringt
	// (etwa eine Verkehrsdichte aus dem Stadt-Prompt). Blind Vorgaben zu
	// schreiben hiesse, die Einstellung eines anderen zu ueberfahren.
	float Faktor = 1.0f;
	if (GConfig->GetFloat(TEXT("WiesbadenReal.Optionen"),
		TEXT("MausEmpfindlichkeit"), Faktor, GGameUserSettingsIni))
	{
		MouseSensitivityFactor = FMath::Clamp(Faktor, 0.25f, 3.0f);
	}

	bool bLegende = true;
	if (GConfig->GetBool(TEXT("WiesbadenReal.Optionen"),
		TEXT("Steuerungshilfe"), bLegende, GGameUserSettingsIni))
	{
		bShowControlLegend = bLegende;
	}
	bHelicopterLessonCompleted = false;
	GConfig->GetBool(TEXT("WiesbadenReal.Optionen"),
		TEXT("HelikopterFlugstundeAbgeschlossen"), bHelicopterLessonCompleted,
		GGameUserSettingsIni);
	bHelicopterLessonPreferenceLoaded = true;


	// Die Debug-Schalter stehen VOR dem Stadt-Block: sie gehoeren nicht zur
	// Stadt, und wer sie gesetzt hat, will sie auch ohne geladene Stadt
	// wiederfinden (die Statik laesst sich sofort einschalten, das
	// Kollisions-Overlay mit dem Viewport des Titels).
	GConfig->GetBool(TEXT("WiesbadenReal.Optionen"), TEXT("FpsAnzeige"),
		bFpsAnzeige, GGameUserSettingsIni);
	GConfig->GetBool(TEXT("WiesbadenReal.Optionen"), TEXT("StatEinblendung"),
		bStatEinblendung, GGameUserSettingsIni);
	GConfig->GetBool(TEXT("WiesbadenReal.Optionen"), TEXT("KollisionsOverlay"),
		bKollisionsOverlay, GGameUserSettingsIni);
	// Dasselbe fuer die Kollisionsboxen: nur einschalten. Wer sie AUS
	// gespeichert hat, darf sie nicht durch zweimaliges Aus-Schalten
	// wiederbekommen.
	if (bKollisionsOverlay && GEngine)
	{
		if (UWorld* Welt = GetWorld())
		{
			GEngine->Exec(Welt, TEXT("show Collision"));
		}
	}

	UWorld* HudWorld = GetWorld();
	UWiesbadenCitySubsystem* City =
		HudWorld ? HudWorld->GetSubsystem<UWiesbadenCitySubsystem>() : nullptr;
	if (!City)
	{
		return;
	}

	float Dichte = 0.0f;
	if (GConfig->GetFloat(TEXT("WiesbadenReal.Optionen"),
		TEXT("Verkehrsdichte"), Dichte, GGameUserSettingsIni))
	{
		City->TrafficSimulation.Settings.TrafficDensity = FMath::Clamp(Dichte, 0.0f, 1.0f);
	}

	// Die Tageszeit wird hier nur GEMERKT, nicht gesetzt - siehe
	// StoredTimeOfDay: das Stadt-Subsystem wuerde sie gleich wieder
	// ueberschreiben. Angewendet wird sie im Takt von ApplyPersistentOptions.
	//
	// -WbTime hat Vorrang: wer die Stunde auf der Befehlszeile erzwingt, will
	// genau die - nicht die zuletzt im Menue gewaehlte.
	float Erzwungen = -1.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("WbTime="), Erzwungen))
	{
		return;
	}
	float Stunde = -1.0f;
	if (GConfig->GetFloat(TEXT("WiesbadenReal.Optionen"),
		TEXT("Tageszeit"), Stunde, GGameUserSettingsIni))
	{
		StoredTimeOfDay = Stunde;
	}
}

void AWiesbadenVehicleHUD::ApplyPersistentOptions()
{
	UWorld* HudWorld = GetWorld();
	if (!HudWorld)
	{
		return;
	}

	if (!bOptionsLoaded)
	{
		bOptionsLoaded = true;
		LoadPersistentOptions();
	}

	const float Now = HudWorld->GetTimeSeconds();
	if (Now < NextOptionApplyAt)
	{
		return;
	}
	NextOptionApplyAt = Now + 1.0f;

	// Die gespeicherte Tageszeit nachziehen. Einmal setzen genuegt nicht: das
	// Stadt-Subsystem setzt seine Zeitquelle in seinem ersten Takt und hat den
	// geladenen Wert dabei ueberschrieben (gemessen: gespeicherte 22 Uhr kam
	// als heller Tag zurueck). Geschrieben wird nur, wenn es wirklich abweicht.
	if (StoredTimeOfDay > -1.5f)
	{
		if (UWiesbadenCitySubsystem* City = HudWorld->GetSubsystem<UWiesbadenCitySubsystem>())
		{
			const bool bSollSystem = (StoredTimeOfDay < 0.0f);
			const bool bIstSystem =
				(City->Weather.Settings.TimeSource == EWiesbadenTimeSource::SystemClock);
			const bool bStundeWeicht = !bSollSystem
				&& !FMath::IsNearlyEqual(City->Weather.Settings.FixedHours, StoredTimeOfDay);

			if (bSollSystem != bIstSystem || bStundeWeicht)
			{
				if (bSollSystem)
				{
					City->Weather.SetTimeSource(EWiesbadenTimeSource::SystemClock);
				}
				else
				{
					City->Weather.SetTimeSource(EWiesbadenTimeSource::FixedHour, StoredTimeOfDay);
				}
				City->Weather.UpdateClock(FDateTime::UtcNow(), FDateTime::Now());
			}
		}
	}

	// Die Empfindlichkeit muss NACHGEZOGEN werden: Spielfigur und
	// Fahrzeugkamera entstehen beim Ein- und Aussteigen neu und braechten sonst
	// wieder ihre eingebauten Werte mit.
	APlayerController* PC = GetOwningPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}

	// Die Grundwerte stehen in den Klassen selbst (WiesbadenFootPawn.h: 1,0;
	// WiesbadenVehicleCameraComponent.h: 2,2). Sie sind verschieden, weil sich
	// zu Fuss und im Wagen unterschiedlich schnell umsehen laesst - darum ist
	// die Option ein Faktor darauf und kein absoluter Wert.
	constexpr float FussGrundwert = 1.0f;
	constexpr float WagenGrundwert = 2.2f;

	if (AWiesbadenFootPawn* Foot = Cast<AWiesbadenFootPawn>(Pawn))
	{
		Foot->MouseSensitivity = FussGrundwert * MouseSensitivityFactor;
	}
	if (UWiesbadenVehicleCameraComponent* Cam =
		Pawn->FindComponentByClass<UWiesbadenVehicleCameraComponent>())
	{
		Cam->MouseSensitivity = WagenGrundwert * MouseSensitivityFactor;
	}
}

void AWiesbadenVehicleHUD::UpdateOptions()
{
	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		return;
	}

	// Ein Schritt je Tastendruck (Flanke ueber ZWEI Tasten): links/rechts sollen
	// nudgen, nicht bei gehaltener Taste in einem Bild von 0 auf 100 springen.
	// Zwei Tasten je Richtung (Pfeile UND WASD), sonst dieselbe Flanke wie im
	// Pausemenue - buchstaeblich dieselbe Funktion.
	auto Edge = [PC](const FKey& KeyA, const FKey& KeyB, bool& bHeld) -> bool
	{
		return WiesbadenOptions::EdgePressed(
			PC->IsInputKeyDown(KeyA) || PC->IsInputKeyDown(KeyB), bHeld);
	};

	TArray<FWbOptionRow> Rows;
	BuildOptionRows(Rows);
	if (Rows.IsEmpty())
	{
		return;
	}
	OptionSelection = WiesbadenOptions::ClampRow(OptionSelection, Rows.Num());

	// EINMAL JE OEFFNEN MELDEN, DASS ES HIER ANKOMMT. Vorher konnte das
	// Fenster gezeichnet sein, waehrend diese Funktion nie lief - die Zeile
	// belegt, dass Zeichnen und Eingabe jetzt an derselben Groesse haengen.
	if (!bOptionInputAnnounced)
	{
		bOptionInputAnnounced = true;
		UE_LOG(LogWbCore, Log,
			TEXT("Optionen: Tastenauswertung laeuft (%d Zeilen, Auswahl %d)."),
			Rows.Num(), OptionSelection);
	}

	if (Edge(EKeys::Up, EKeys::W, bMenuUpHeld)
		|| Edge(EKeys::Gamepad_DPad_Up, EKeys::Gamepad_DPad_Up, bMenuUpHeld))
	{
		OptionSelection = WiesbadenOptions::NextRow(OptionSelection, Rows.Num(), -1);
	}
	if (Edge(EKeys::Down, EKeys::S, bMenuDownHeld)
		|| Edge(EKeys::Gamepad_DPad_Down, EKeys::Gamepad_DPad_Down, bMenuDownHeld))
	{
		OptionSelection = WiesbadenOptions::NextRow(OptionSelection, Rows.Num(), 1);
	}

	int32 Richtung = 0;
	if (Edge(EKeys::Left, EKeys::A, bMenuLeftHeld)
		|| Edge(EKeys::Gamepad_LeftShoulder, EKeys::Gamepad_LeftShoulder, bMenuLeftHeld))
	{
		Richtung -= 1;
	}
	if (Edge(EKeys::Right, EKeys::D, bMenuRightHeld)
		|| Edge(EKeys::Gamepad_RightShoulder, EKeys::Gamepad_RightShoulder, bMenuRightHeld))
	{
		Richtung += 1;
	}
	// A (der untere Gesichtsknopf) verstellt ebenfalls - wer die Schulter-
	// tasten nicht findet, kommt sonst an keiner Zeile vorbei.
	if (Richtung == 0
		&& Edge(EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_FaceButton_Bottom, bMenuEnterHeld))
	{
		Richtung += 1;
	}
	if (Richtung == 0)
	{
		return;
	}

	// Der Weg liegt in StepOptionValue - dieselbe Funktion, die auch die
	// Optionsseite des Hauptmenues benutzt (inklusive Nachweis im Protokoll).
	StepOptionValue(Rows[OptionSelection], Richtung);
}

void AWiesbadenVehicleHUD::WbTitel(int32 Bildschirm)
{
	// Der einzelne Befehl darf den Weg durch die Ebenen abkuerzen: "WbTitel 2"
	// soll die Gruppenliste zeigen, nicht erst das Hauptmenue, das man sich
	// erst durchspielen muesste. Deshalb wird der Bildschirm direkt gesetzt
	// statt ueber Back() nachgestellt.
	const int32 Gewuenscht = FMath::Clamp(Bildschirm, 0, 3);
	switch (Gewuenscht)
	{
	case 0:  MenuScreen = EWbMenuScreen::Intro;      break;
	case 1:  MenuScreen = EWbMenuScreen::Titel;      break;
	case 2:  MenuScreen = EWbMenuScreen::Optionen;   break;
	default: MenuScreen = EWbMenuScreen::Belegung;   break;
	}
	if (UWorld* HudWorld = GetWorld())
	{
		MenuOpenedAt = HudWorld->GetTimeSeconds();
	}
	MenuSelection = 0;
	bTitleGeprueft = true;
	RebuildMenuEntries();
	UE_LOG(LogWbCore, Log, TEXT("HUD: WbTitel %d -> Bildschirm %d, %d Eintraege."),
		Gewuenscht, static_cast<int32>(MenuScreen), MenuEntries.Num());
}

void AWiesbadenVehicleHUD::DrawOptions(float Width, float Height)
{
	TArray<FWbOptionRow> Rows;
	BuildOptionRows(Rows);

	constexpr float LineHeight = 26.0f;
	constexpr float GroupGap = 20.0f;
	constexpr float BoxWidth = 760.0f;

	// Hoehe aus dem Inhalt: Zeilen plus je eine Zwischenueberschrift.
	int32 GroupCount = 0;
	{
		EWbOptionGroup Last = EWbOptionGroup::MAX;
		for (const FWbOptionRow& Row : Rows)
		{
			if (Row.Group != Last)
			{
				++GroupCount;
				Last = Row.Group;
			}
		}
	}
	const float BoxHeight = Rows.Num() * LineHeight + GroupCount * GroupGap + 150.0f;

	const float X = (Width - BoxWidth) * 0.5f;
	const float Y = FMath::Max(20.0f, (Height - BoxHeight) * 0.5f);

	// Ganzen Schirm abdunkeln, dann die Tafel - und die DECKEND.
	//
	// DialBackground ist mit Alpha 0,55 fuer Instrumente gedacht, durch die man
	// die Strasse noch sehen soll. Fuer eine Werteliste ist das falsch: im
	// ersten Bild schien der Kaefer durch die Lautstaerkeregler, und die Zahlen
	// waren schwer zu lesen. Eine Tafel, auf der etwas ABGELESEN wird, deckt.
	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.72f), 0.0f, 0.0f, Width, Height);
	DrawRect(FLinearColor(0.03f, 0.035f, 0.045f, 0.97f), X, Y, BoxWidth, BoxHeight);

	// Schmaler heller Rand, damit die Tafel eine Kante hat.
	const FLinearColor Rand(0.30f, 0.62f, 0.80f, 0.85f);
	DrawRect(Rand, X, Y, BoxWidth, 2.0f);
	DrawRect(Rand, X, Y + BoxHeight - 2.0f, BoxWidth, 2.0f);
	DrawRect(Rand, X, Y, 2.0f, BoxHeight);
	DrawRect(Rand, X + BoxWidth - 2.0f, Y, 2.0f, BoxHeight);

	DrawText(TEXT("OPTIONEN"), DialText, X + 24.0f, Y + 18.0f,
		GEngine->GetLargeFont(), 1.0f);

	const float LabelX = X + 30.0f;
	const float ValueX = X + 300.0f;
	const float BarX = X + 470.0f;
	const float BarW = 250.0f;
	const float BarH = 12.0f;
	const FLinearColor BarBack(0.14f, 0.15f, 0.17f, 0.9f);
	const FLinearColor BarFill(0.20f, 0.70f, 0.95f, 0.95f);

	float RowY = Y + 58.0f;
	EWbOptionGroup LastGroup = EWbOptionGroup::MAX;

	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		const FWbOptionRow& Row = Rows[Index];

		if (Row.Group != LastGroup)
		{
			LastGroup = Row.Group;
			RowY += GroupGap * 0.4f;
			DrawText(WiesbadenOptions::GroupLabel(Row.Group), IndicatorOn,
				LabelX, RowY, GEngine->GetSmallFont(), 1.0f);
			RowY += GroupGap * 0.8f;
		}

		const bool bSelected = (Index == OptionSelection);
		const double Value = ReadOptionValue(Row);

		DrawText((bSelected ? TEXT("> ") : TEXT("  ")) + Row.Label,
			bSelected ? IndicatorOn : DialScale,
			LabelX + 14.0f, RowY, GEngine->GetMediumFont(), 1.0f);

		// DER WERT STEHT IMMER DA - auch bei der nicht gewaehlten Zeile. Wer
		// erst auswaehlen muss, um zu sehen, was eingestellt ist, sucht.
		DrawText(WiesbadenOptions::FormatValue(Row.Kind, Value),
			bSelected ? DialText : DialScale,
			ValueX, RowY, GEngine->GetMediumFont(), 1.0f);

		const double Fraction = WiesbadenOptions::BarFraction(Row.Kind, Value);
		if (Fraction >= 0.0)
		{
			const float BarY = RowY + 4.0f;
			DrawRect(BarBack, BarX, BarY, BarW, BarH);
			if (Fraction > 0.0)
			{
				DrawRect(BarFill, BarX, BarY, BarW * static_cast<float>(Fraction), BarH);
			}
		}

		RowY += LineHeight;
	}

	// Fusszeile: was die gewaehlte Zeile bewirkt, und die Tasten.
	const float FootY = Y + BoxHeight - 52.0f;
	if (Rows.IsValidIndex(OptionSelection))
	{
		DrawText(Rows[OptionSelection].Hinweis, DialScale,
			LabelX, FootY, GEngine->GetSmallFont(), 1.0f);
	}
	DrawText(TEXT("Hoch/Runter waehlen   Links/Rechts verstellen   Esc zurueck   (gespeichert)"),
		TellTaleOff, LabelX, FootY + 22.0f, GEngine->GetSmallFont(), 1.0f);
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
			CachedBuildings = &It->Buildings;   // Gebaeude vom selben Actor mitnehmen
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

	// Neuaufbau nur bei spuerbarer Bewegung oder nach kurzer Zeit - nicht je
	// Bild. BuildLines laeuft sonst zweimal ueber ~125.000 Segmente pro Bild
	// (der teuerste Posten des HUD). Bei 250 m Umkreis sind 4 m Versatz ein
	// Drittel Pixel, also unsichtbar.
	const UWorld* HudWorld = GetWorld();
	MinimapCacheAge += HudWorld ? HudWorld->GetDeltaSeconds() : 0.016f;
	const double MovedCm = FVector2D::Distance(
		FVector2D(MapCentre.X, MapCentre.Y),
		FVector2D(CachedMinimapCentre.X, CachedMinimapCentre.Y));
	const double YawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(MapYaw, CachedMinimapYaw));
	if (MinimapCacheAge > 0.4f || MovedCm > 400.0 || YawDelta > 4.0)
	{
		FWiesbadenMinimap::BuildLines(
			*Network, MapCentre, MapYaw, FVector2D(CenterX, CenterY), Settings, CachedMinimapLines);
		CachedMinimapCentre = MapCentre;
		CachedMinimapYaw = MapYaw;
		MinimapCacheAge = 0.0f;
	}

	for (const FMinimapLine& Line : CachedMinimapLines)
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

	// Wegpunkt: Richtung (Marker, am Rand geklemmt wenn ausserhalb) + Distanz.
	if (bWaypointSet)
	{
		const FMinimapWaypoint WP = FWiesbadenMinimap::ProjectWaypointToMinimap(
			MapCentre, MapYaw, WaypointWorld, FVector2D(CenterX, CenterY), Settings);
		const FVector2D M = WP.ScreenPos;

		// Bei Rand-Klemmung ein kurzer Richtungsstrich von innen zum Marker.
		if (WP.bOffMap)
		{
			FVector2D Dir = M - FVector2D(CenterX, CenterY);
			if (!Dir.IsNearlyZero())
			{
				Dir.Normalize();
				DrawLine(M.X - Dir.X * 12.0f, M.Y - Dir.Y * 12.0f, M.X, M.Y, MapWaypoint, 2.2f);
			}
		}

		// Diamant-Marker.
		constexpr float D = 6.0f;
		DrawLine(M.X, M.Y - D, M.X + D, M.Y, MapWaypoint, 2.2f);
		DrawLine(M.X + D, M.Y, M.X, M.Y + D, MapWaypoint, 2.2f);
		DrawLine(M.X, M.Y + D, M.X - D, M.Y, MapWaypoint, 2.2f);
		DrawLine(M.X - D, M.Y, M.X, M.Y - D, MapWaypoint, 2.2f);

		// Distanz rechts unten an der Karte.
		DrawText(FString::Printf(TEXT("WP %s"), *FormatMapDistance(WP.DistanceCm)),
			MapWaypoint, CenterX + Radius - 66.0f, CenterY + Radius - 20.0f,
			GEngine ? GEngine->GetSmallFont() : nullptr, 1.0f);
	}

	// Missions-Ziel: eigener Marker (Bernstein), unabhaengig vom Nutzer-Wegpunkt.
	if (const UWorld* MissionWorld = GetWorld())
	{
		if (const UWiesbadenMissionSubsystem* Missions =
			MissionWorld->GetSubsystem<UWiesbadenMissionSubsystem>())
		{
			if (const FMissionObjective* Obj = Missions->GetCurrentObjective())
			{
				const FLinearColor MissionMarker(1.0f, 0.72f, 0.20f, 1.0f);
				const FMinimapWaypoint MP = FWiesbadenMinimap::ProjectWaypointToMinimap(
					MapCentre, MapYaw, Obj->Location, FVector2D(CenterX, CenterY), Settings);
				const FVector2D M = MP.ScreenPos;
				if (MP.bOffMap)
				{
					FVector2D Dir = M - FVector2D(CenterX, CenterY);
					if (!Dir.IsNearlyZero())
					{
						Dir.Normalize();
						DrawLine(M.X - Dir.X * 12.0f, M.Y - Dir.Y * 12.0f, M.X, M.Y, MissionMarker, 2.4f);
					}
				}
				constexpr float MD = 7.0f;
				DrawLine(M.X, M.Y - MD, M.X + MD, M.Y, MissionMarker, 2.4f);
				DrawLine(M.X + MD, M.Y, M.X, M.Y + MD, MissionMarker, 2.4f);
				DrawLine(M.X, M.Y + MD, M.X - MD, M.Y, MissionMarker, 2.4f);
				DrawLine(M.X - MD, M.Y, M.X, M.Y - MD, MissionMarker, 2.4f);
			}
		}
	}

	// Massstab: Der Umkreis in Metern, damit die Karte lesbar bleibt.
	const FString ScaleText = FString::Printf(TEXT("%.0f m"), Settings.RangeCm / 100.0);
	DrawText(ScaleText, DialScale, CenterX - Radius + 8.0f, CenterY + Radius - 20.0f,
		GEngine ? GEngine->GetSmallFont() : nullptr, 1.0f);
}

void AWiesbadenVehicleHUD::DrawMissionPanel(float Width, float Height)
{
	const UWorld* W = GetWorld();
	const UWiesbadenMissionSubsystem* Missions =
		W ? W->GetSubsystem<UWiesbadenMissionSubsystem>() : nullptr;
	if (!Missions)
	{
		return;
	}
	const FMissionObjective* Obj = Missions->GetCurrentObjective();
	if (!Obj)
	{
		return;
	}

	// Planare Distanz Spieler -> Ziel (nur wenn ein Pawn existiert).
	FString DistText;
	if (const APlayerController* PC = GetOwningPlayerController())
	{
		if (const APawn* Pawn = PC->GetPawn())
		{
			const FVector PL = Pawn->GetActorLocation();
			const double Dx = PL.X - Obj->Location.X;
			const double Dy = PL.Y - Obj->Location.Y;
			const double DistM = FMath::Sqrt(Dx * Dx + Dy * Dy) / 100.0;
			DistText = DistM >= 1000.0
				? FString::Printf(TEXT("%.1f km"), DistM / 1000.0)
				: FString::Printf(TEXT("%.0f m"), DistM);
		}
	}

	const float PanelW = 360.0f;
	const float PanelH = 56.0f;
	const float X = (Width - PanelW) * 0.5f;
	const float Y = 60.0f;
	DrawPanelBackdrop(X, Y, PanelW, PanelH, 10.0f, FLinearColor(0.08f, 0.10f, 0.13f), 0.72f);

	const FLinearColor TitleColour(1.0f, 0.72f, 0.20f, 1.0f);
	FLinearColor BodyColour(0.92f, 0.94f, 0.96f, 1.0f);
	DrawText(Missions->GetActiveMissionTitle(), TitleColour, X + 16.0f, Y + 8.0f,
		GEngine ? GEngine->GetSmallFont() : nullptr, 1.0f);
	FString ObjLine = DistText.IsEmpty()
		? Obj->Label
		: FString::Printf(TEXT("%s  -  %s"), *Obj->Label, *DistText);

	// Zeitlimit-Countdown (nur bei befristeten Auftraegen): Restzeit anhaengen,
	// unter 15 s die Zeile rot einfaerben. -1 = unbefristet -> nichts anzeigen.
	const double Remaining = Missions->GetActiveMissionRemainingSeconds();
	if (Remaining >= 0.0)
	{
		ObjLine += FString::Printf(TEXT("  -  Rest %02d:%02d"),
			FMath::FloorToInt(Remaining / 60.0),
			FMath::FloorToInt(FMath::Fmod(Remaining, 60.0)));
		if (Remaining < 15.0)
		{
			BodyColour = FLinearColor(1.0f, 0.35f, 0.28f); // knappe Zeit -> rot
		}
	}

	DrawText(ObjLine, BodyColour, X + 16.0f, Y + 28.0f,
		GEngine ? GEngine->GetMediumFont() : nullptr, 1.0f);
}

void AWiesbadenVehicleHUD::DrawStorePanel(float Width, float Height)
{
	const UWorld* W = GetWorld();
	const UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	const UWiesbadenStoreSubsystem* Store =
		GI ? GI->GetSubsystem<UWiesbadenStoreSubsystem>() : nullptr;
	if (!Store || !Store->IsPanelOpen())
	{
		return;
	}
	const UWiesbadenGameStateSubsystem* GameState =
		GI->GetSubsystem<UWiesbadenGameStateSubsystem>();
	const int32 Guthaben = GameState ? GameState->GetGuthaben() : 0;

	const TArray<FStoreItem>& Catalog = Store->GetCatalog();

	const float PanelW = 420.0f;
	const float RowH = 46.0f;
	const float HeadH = 40.0f;
	const float PanelH = HeadH + RowH * FMath::Max(1, Catalog.Num()) + 12.0f;
	const float X = (Width - PanelW) * 0.5f;
	const float Y = (Height - PanelH) * 0.5f;
	DrawPanelBackdrop(X, Y, PanelW, PanelH, 12.0f, FLinearColor(0.06f, 0.08f, 0.11f), 0.86f);

	const FLinearColor HeadColour(1.0f, 0.72f, 0.20f, 1.0f);
	DrawText(FString::Printf(TEXT("Freischaltungen    Guthaben: %d EUR"), Guthaben),
		HeadColour, X + 16.0f, Y + 10.0f, GEngine ? GEngine->GetMediumFont() : nullptr, 1.0f);

	float RowY = Y + HeadH;
	for (const FStoreItem& Item : Catalog)
	{
		const bool bOwned = GameState && GameState->HasUnlock(Item.Id);
		const bool bAffordable = Guthaben >= Item.Kosten;

		FString StatusText;
		FLinearColor RowColour;
		if (bOwned)
		{
			StatusText = TEXT("im Besitz");
			RowColour = FLinearColor(0.55f, 0.85f, 0.55f, 1.0f);
		}
		else if (bAffordable)
		{
			StatusText = FString::Printf(TEXT("%d EUR  -  Wb.Buy %s"), Item.Kosten, *Item.Id.ToString());
			RowColour = FLinearColor(0.92f, 0.94f, 0.96f, 1.0f);
		}
		else
		{
			StatusText = FString::Printf(TEXT("%d EUR  (zu teuer)"), Item.Kosten);
			RowColour = FLinearColor(0.70f, 0.55f, 0.55f, 1.0f);
		}

		DrawText(Item.Title, RowColour, X + 16.0f, RowY + 4.0f,
			GEngine ? GEngine->GetMediumFont() : nullptr, 1.0f);
		DrawText(StatusText, RowColour, X + 16.0f, RowY + 24.0f,
			GEngine ? GEngine->GetSmallFont() : nullptr, 1.0f);
		RowY += RowH;
	}
}

void AWiesbadenVehicleHUD::ShowTransientHint(const FString& Text)
{
	TransientHintText = Text;
	TransientHintShownAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
}

void AWiesbadenVehicleHUD::DrawTransientHint(float Width, float Height)
{
	if (TransientHintText.IsEmpty())
	{
		return;
	}
	// Mehrzeilig ueber Zeilenumbrueche (z. B. Dennos Auftrag + Kurier-Bilanz);
	// jede weitere Zeile bleibt 2 s laenger stehen, damit sie gelesen wird.
	TArray<FString> Lines;
	TransientHintText.ParseIntoArrayLines(Lines, /*bCullEmpty=*/true);
	if (Lines.IsEmpty())
	{
		return;
	}
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	const float Age = Now - TransientHintShownAt;
	const float HoldSeconds = 3.5f + 2.0f * (Lines.Num() - 1);
	if (Age < 0.0f || Age > HoldSeconds)
	{
		return;
	}
	// Letzte 1 s ausblenden.
	const float Alpha = Age > (HoldSeconds - 1.0f) ? (HoldSeconds - Age) : 1.0f;

	// Das Feld waechst mit dem laengsten Text (mindestens die alten 460 px).
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	float TextW = 0.0f;
	float LineH = 20.0f;
	for (const FString& Line : Lines)
	{
		float LineW = 0.0f;
		float MeasuredH = 0.0f;
		GetTextSize(Line, LineW, MeasuredH, Font, 1.0f);
		TextW = FMath::Max(TextW, LineW);
		LineH = FMath::Max(LineH, MeasuredH);
	}
	const float PanelW = FMath::Min(FMath::Max(460.0f, TextW + 36.0f), Width - 40.0f);
	const float PanelH = 20.0f + LineH * Lines.Num() + 4.0f * (Lines.Num() - 1);
	const float X = (Width - PanelW) * 0.5f;
	const float Y = Height * 0.24f;
	DrawPanelBackdrop(X, Y, PanelW, PanelH, 8.0f, FLinearColor(0.14f, 0.05f, 0.05f), 0.78f * Alpha);
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		// Folgezeilen (Bilanz, Nachsatz) etwas zurueckgenommen.
		const FLinearColor Colour = Index == 0 ? FLinearColor(1.0f, 0.78f, 0.42f, Alpha)
			: FLinearColor(0.92f, 0.86f, 0.74f, Alpha);
		DrawText(Lines[Index], Colour, X + 18.0f, Y + 10.0f + Index * (LineH + 4.0f), Font, 1.0f);
	}
}

void AWiesbadenVehicleHUD::DrawWorldMap(float Width, float Height)
{
	if (!Canvas)
	{
		return;
	}

	const FRoadNetwork* Network = FindRoadNetwork();
	if (!Network)
	{
		DrawRect(FLinearColor(0.04f, 0.05f, 0.07f, 1.0f), 0.0f, 0.0f, Width, Height);
		DrawText(TEXT("Karte laedt..."), DialText, Width * 0.5f - 48.0f, Height * 0.5f,
			GEngine ? GEngine->GetMediumFont() : nullptr, 1.3f);
		return;
	}

	// Statische Ebene (Strassen + Gebaeude) ins RenderTarget rendern und je Bild nur
	// das fertige Texture blitten - neu gerendert nur bei Sichtaenderung (Zoom/Pan).
	if (!WorldMapView)
	{
		WorldMapView = NewObject<UWiesbadenWorldMapView>(this);
	}

	// Basiskarte EINMAL backen; Zoom/Pan rastern danach NICHTS mehr neu, sie
	// transformieren nur das Texture-UV -> fluessig statt ruckelnd.
	UTextureRenderTarget2D* MapRT = WorldMapView->EnsureBaseMap(
		GetWorld(), *Network, CachedBuildings, FVector2D(Width, Height));
	if (!WorldMapView->HasBase())
	{
		DrawRect(FLinearColor(0.04f, 0.05f, 0.07f, 1.0f), 0.0f, 0.0f, Width, Height);
		DrawText(TEXT("Karte laedt..."), DialText, Width * 0.5f - 48.0f, Height * 0.5f,
			GEngine ? GEngine->GetMediumFont() : nullptr, 1.3f);
		return;
	}
	const FWorldMapProjection& BaseFit = WorldMapView->GetBaseFit();

	// Bildschirm-Einpassung aus den GEBACKENEN Netzgrenzen (kein Grenzen-Scan je Bild).
	const FWorldMapProjection ScreenFit = FWiesbadenMinimap::MakeWorldMapProjection(
		BaseFit.WorldMin, BaseFit.WorldMax, FVector2D(Width * 0.5f, Height * 0.5f),
		FVector2D(Width, Height), FWiesbadenMinimap::WorldMapMarginFrac);

	// --- Zoom & Pan aus den Eingaben (nur Sichtfenster, kein Neu-Rastern) ---
	APlayerController* MapPC = GetOwningPlayerController();
	const float Dt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0f;
	// Solange das Suchfeld getippt wird, gehen Pfeile/Enter NICHT an Pan/Wegpunkt.
	const bool bTypingSearch = bMapSearchActive;
	if (MapPC)
	{
		// Zoom: +/- (Tastatur), Mausrad, Gamepad-Schultertasten. Stetig ueber
		// e^(Rate*dt), dazu diskrete Radschritte.
		float ZoomDir = 0.0f;
		if (MapPC->IsInputKeyDown(EKeys::Add) || MapPC->IsInputKeyDown(EKeys::Equals)
			|| MapPC->IsInputKeyDown(EKeys::Gamepad_RightShoulder)) { ZoomDir += 1.0f; }
		if (MapPC->IsInputKeyDown(EKeys::Subtract) || MapPC->IsInputKeyDown(EKeys::Hyphen)
			|| MapPC->IsInputKeyDown(EKeys::Gamepad_LeftShoulder)) { ZoomDir -= 1.0f; }
		const float Wheel = MapPC->GetInputAnalogKeyState(EKeys::MouseWheelAxis);
		MapZoom *= FMath::Exp(ZoomDir * 2.2f * Dt) * FMath::Pow(1.15f, Wheel);
		MapZoom = FMath::Clamp(MapZoom, FWiesbadenMinimap::WorldMapMinZoom, FWiesbadenMinimap::WorldMapMaxZoom);

		// Pan: Pfeiltasten + rechter Stick. Umrechnung px->Welt ueber den aktuellen
		// Massstab, damit das Schwenken bei jedem Zoom gleich schnell wirkt.
		FVector2D PanPx(0.0f, 0.0f);
		if (MapPC->IsInputKeyDown(EKeys::Left))  { PanPx.X -= 1.0f; }
		if (MapPC->IsInputKeyDown(EKeys::Right)) { PanPx.X += 1.0f; }
		if (MapPC->IsInputKeyDown(EKeys::Up))    { PanPx.Y -= 1.0f; }
		if (MapPC->IsInputKeyDown(EKeys::Down))  { PanPx.Y += 1.0f; }
		PanPx.X += MapPC->GetInputAnalogKeyState(EKeys::Gamepad_RightX);
		PanPx.Y -= MapPC->GetInputAnalogKeyState(EKeys::Gamepad_RightY);
		if (bMapCentreInit && !PanPx.IsNearlyZero() && !bTypingSearch)
		{
			const float Scale = FMath::Max(ScreenFit.ScalePxPerCm * MapZoom, KINDA_SMALL_NUMBER);
			constexpr float PanPxPerSec = 900.0f;
			MapCentreWorld.X += PanPx.X * PanPxPerSec * Dt / Scale;
			MapCentreWorld.Y -= PanPx.Y * PanPxPerSec * Dt / Scale;   // Bild runter = Welt -Y
		}

		// --- Strassennamen-Suche (Feature 6): Tab oeffnet/schliesst das Feld; im
		// Feld A-Z + Leertaste tippen, Rueck loescht, Enter zentriert die Karte auf
		// den Treffer (FindStreetCenter -> Blickzentrum + naeherer Zoom). ---
		const bool bToggleSearch = MapPC->IsInputKeyDown(EKeys::Tab);
		if (bToggleSearch && !bMapSearchToggleHeld)
		{
			bMapSearchActive = !bMapSearchActive;
			if (bMapSearchActive) { MapSearchQuery.Empty(); }
		}
		bMapSearchToggleHeld = bToggleSearch;

		if (bMapSearchActive)
		{
			for (TCHAR Ch = 'A'; Ch <= 'Z'; ++Ch)
			{
				if (MapPC->WasInputKeyJustPressed(FKey(*FString::Chr(Ch))))
				{
					MapSearchQuery.AppendChar(Ch);
				}
			}
			if (MapPC->WasInputKeyJustPressed(EKeys::SpaceBar))
			{
				MapSearchQuery.AppendChar(' ');
			}
			if (MapPC->WasInputKeyJustPressed(EKeys::BackSpace) && MapSearchQuery.Len() > 0)
			{
				MapSearchQuery.LeftChopInline(1);
			}

			// Vorschlaege im Takt, nicht je Buchstabe: die Suche laeuft ueber
			// alle ~125.000 Segmente. 0,4 s sind schneller, als ein Mensch
			// tippt, und sie kosten einmal statt viermal je Sekunde.
			const double Jetzt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
			const bool bTextGeaendert = MapPC->WasInputKeyJustPressed(EKeys::BackSpace);
			if (Jetzt - MapSuggestionAge > 0.4 || (bTextGeaendert && Jetzt - MapSuggestionAge > 0.15))
			{
				MapSuggestionAge = static_cast<float>(Jetzt);
				FWiesbadenMinimap::FindStreetSuggestions(
					*Network, MapSearchQuery, MapSuggestions, 6);
				// Die Markierung haengt an der Liste, nicht am Getippten: ein
				// Tipp, der die Liste kuerzt, darf sie nicht springen lassen.
				MapSuggestion = FMath::Clamp(MapSuggestion,
					0, FMath::Max(0, MapSuggestions.Num() - 1));
			}

			// Pfeil hoch/runter waehlt den Vorschlag. Beim ersten Anschlag
			// geht es nach oben - der erste Vorschlag ist der wahrscheinlichste.
			const int32 Schritt =
				(MapPC->WasInputKeyJustPressed(EKeys::Down) ? 1 : 0)
				- (MapPC->WasInputKeyJustPressed(EKeys::Up) ? 1 : 0);
			if (Schritt != 0 && MapSuggestions.Num() > 0)
			{
				MapSuggestion = ((MapSuggestion + Schritt) % MapSuggestions.Num()
					+ MapSuggestions.Num()) % MapSuggestions.Num();
			}

			if (MapPC->WasInputKeyJustPressed(EKeys::Escape))
			{
				bMapSearchActive = false;
			}
			else if (MapPC->WasInputKeyJustPressed(EKeys::Enter)
				|| MapPC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom))
			{
				// Was bestaetigt wird, ist der markierte Vorschlag - oder,
				// wenn keiner da ist, der getippte Text. Sonst wuerde Enter
				// etwas bestaetigen, das man gar nicht gelesen hat.
				const FString Gewaehlt = MapSuggestions.IsValidIndex(MapSuggestion)
					? MapSuggestions[MapSuggestion] : MapSearchQuery;
				FVector2D Hit;
				if (FWiesbadenMinimap::FindStreetCenter(*Network, Gewaehlt, Hit))
				{
					MapCentreWorld = Hit;
					bMapCentreInit = true;
					MapZoom = FMath::Clamp(4.0f,
						FWiesbadenMinimap::WorldMapMinZoom, FWiesbadenMinimap::WorldMapMaxZoom);
					// Bestaetigt heisst: ab jetzt ist der Knopf scharf. Ohne
					// diese Zeile bliebe er aus, und wer ihn sucht, faende ihn
					// nicht - obwohl die Strasse laengst gefunden ist.
					MapSearchStreet = Gewaehlt;
				}
				bMapSearchActive = false;
			}
		}

		// -- Der Knopf "Warp to Location" --------------------------------------
		// Aktiv, sobald eine Strasse bestaetigt wurde. W (Tastatur) oder X
		// (Pad) springen dorthin; danach geht die Karte zu, weil man angekommen
		// ist und sie nun aus der Naehe sieht.
		const bool bWarpDa = !MapSearchStreet.IsEmpty();
		const bool bWarpTaste = MapPC->IsInputKeyDown(EKeys::W)
			|| MapPC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Left);
		if (bWarpTaste && !bWarpKeyHeld && bWarpDa)
		{
			if (AWiesbadenPlayerController* PC = Cast<AWiesbadenPlayerController>(MapPC))
			{
				if (PC->WarpToStreet(MapSearchStreet))
				{
					ShowTransientHint(FString::Printf(
						TEXT("Angekommen: %s"), *MapSearchStreet));
					bWorldMapOpen = false;
				}
			}
		}
		bWarpKeyHeld = bWarpTaste;

		// -- Ansicht wieder auf den Spieler ----------------------------------
		// C oder das Steuerkreuz runter. Ohne das kommt man aus einer
		// gequetschten Ecke der Karte nur ueber Zuschalten heraus.
		const bool bZentrieren = MapPC->IsInputKeyDown(EKeys::C)
			|| MapPC->IsInputKeyDown(EKeys::Gamepad_DPad_Down);
		if (bZentrieren && !bMapResetKeyHeld)
		{
			MapZoom = 2.0f;
			bMapCentreInit = false;
		}
		bMapResetKeyHeld = bZentrieren;
	}

	// Aktuelle Sicht (fuer UV-Fenster UND Overlays); Blickzentrum wird geklemmt.
	const FVector2D DesiredCentre = bMapCentreInit ? MapCentreWorld : ScreenFit.ViewCentreWorld;
	const FWorldMapProjection Proj = FWiesbadenMinimap::MakeZoomedProjection(ScreenFit, MapZoom, DesiredCentre);
	MapCentreWorld = Proj.ViewCentreWorld;
	bMapCentreInit = true;

	// Karte blitten: UV-Teilrechteck der Basis-Textur (ein Quad -> fluessiges Zoomen/Pannen).
	if (MapRT)
	{
		FVector2D UVMin, UVMax;
		FWiesbadenMinimap::ComputeWorldMapUV(
			BaseFit, WorldMapView->GetBaseSize(), Proj, FVector2D(Width, Height), UVMin, UVMax);
		DrawTexture(MapRT, 0.0f, 0.0f, Width, Height,
			UVMin.X, UVMin.Y, UVMax.X - UVMin.X, UVMax.Y - UVMin.Y);
	}
	else
	{
		DrawRect(FLinearColor(0.04f, 0.05f, 0.07f, 1.0f), 0.0f, 0.0f, Width, Height);
	}

	// Feature 5: beim starken Reinzoomen vergroessert der Textur-Blitt nur noch
	// Pixel (unscharf). Dann die im Fenster sichtbaren Strassen LIVE als scharfe
	// Vektoren darueber zeichnen - gecullt auf den sichtbaren Ausschnitt, also nur
	// wenige Segmente (kein Ruckeln wie beim frueheren Voll-Netz-Zeichnen).
	if (Proj.IsValid() && FWiesbadenMinimap::ShouldDrawVectorStreets(Proj, BaseFit))
	{
		FVector2D VisMin, VisMax;
		FWiesbadenMinimap::ComputeVisibleWorldBounds(Proj, FVector2D(Width, Height), VisMin, VisMax);
		const FLinearColor MinorCol(0.66f, 0.68f, 0.72f, 1.0f);
		const FLinearColor MajorCol(0.97f, 0.84f, 0.38f, 1.0f);
		for (const FRoadSegment& Seg : Network->Segments)
		{
			const TArray<FVector>& Line = Seg.Centerline.Num() >= 2
				? Seg.Centerline : Seg.TrimmedCenterline;
			if (Line.Num() < 2)
			{
				continue;
			}
			// Huellbox-Cull gegen das sichtbare Fenster: nur Strassen im Blick zeichnen.
			FVector2D SegMin(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
			FVector2D SegMax(TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest());
			for (const FVector& P : Line)
			{
				SegMin.X = FMath::Min(SegMin.X, P.X); SegMin.Y = FMath::Min(SegMin.Y, P.Y);
				SegMax.X = FMath::Max(SegMax.X, P.X); SegMax.Y = FMath::Max(SegMax.Y, P.Y);
			}
			if (SegMax.X < VisMin.X || SegMin.X > VisMax.X
				|| SegMax.Y < VisMin.Y || SegMin.Y > VisMax.Y)
			{
				continue;
			}
			const bool bMajor = FWiesbadenMinimap::IsMajorRoad(Seg.HighwayType);
			const FLinearColor& Col = bMajor ? MajorCol : MinorCol;
			const float Thick = bMajor ? 2.6f : 1.3f;
			for (int32 i = 1; i < Line.Num(); ++i)
			{
				const FVector2D A = Proj.Project(Line[i - 1]);
				const FVector2D B = Proj.Project(Line[i]);
				DrawLine(A.X, A.Y, B.X, B.Y, Col, Thick);
			}
		}
	}

	// Strassennamen LIVE im Bildschirmraum darueber (immer scharf, unabhaengig vom Zoom).
	DrawWorldMapLabels(*Network, Proj, Width, Height);

	// Spielerpunkt + Fahrtrichtung LIVE ueber dem Texture (bewegt sich je Bild).
	if (Proj.IsValid())
	{
		const APlayerController* PC = GetOwningPlayerController();
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (Pawn)
		{
			const FVector2D P = Proj.Project(Pawn->GetActorLocation());
			const double YawRad = FMath::DegreesToRadians(Pawn->GetActorRotation().Yaw);
			const FVector2D Fwd(FMath::Cos(YawRad), -FMath::Sin(YawRad));
			const FVector2D Right(-Fwd.Y, Fwd.X);
			constexpr float S = 12.0f;
			const FVector2D Tip = P + Fwd * S;
			const FVector2D L = P - Fwd * (S * 0.6f) + Right * (S * 0.6f);
			const FVector2D R = P - Fwd * (S * 0.6f) - Right * (S * 0.6f);
			DrawLine(Tip.X, Tip.Y, L.X, L.Y, MapPlayer, 2.8f);
			DrawLine(Tip.X, Tip.Y, R.X, R.Y, MapPlayer, 2.8f);
			DrawLine(L.X, L.Y, R.X, R.Y, MapPlayer, 2.8f);
		}
	}

	// Wegpunkt setzen/loeschen: Fadenkreuz in der Bildmitte anvisieren (mit Zoom
	// + Pan darueberfahren), Enter / A / Linksklick setzt, Rueck / B / Rechtsklick
	// loescht. Flanken, damit ein Druck einmal wirkt.
	if (MapPC && Proj.IsValid() && !bTypingSearch)
	{
		const bool bSet = MapPC->IsInputKeyDown(EKeys::Enter)
			|| MapPC->IsInputKeyDown(EKeys::LeftMouseButton)
			|| MapPC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Bottom);
		if (bSet && !bWaypointSetKeyHeld)
		{
			const FVector2D W = Proj.Unproject(FVector2D(Width * 0.5f, Height * 0.5f));
			WaypointWorld = FVector(W.X, W.Y, 0.0);
			bWaypointSet = true;
		}
		bWaypointSetKeyHeld = bSet;

		const bool bClear = MapPC->IsInputKeyDown(EKeys::BackSpace)
			|| MapPC->IsInputKeyDown(EKeys::Delete)
			|| MapPC->IsInputKeyDown(EKeys::RightMouseButton)
			|| MapPC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Right);
		if (bClear && !bWaypointClearKeyHeld)
		{
			bWaypointSet = false;
		}
		bWaypointClearKeyHeld = bClear;
	}

	// Ziel-Fadenkreuz in der Bildmitte (der Setz-Punkt).
	{
		const float Cx = Width * 0.5f;
		const float Cy = Height * 0.5f;
		DrawLine(Cx - 10.0f, Cy, Cx + 10.0f, Cy, DialScale, 1.4f);
		DrawLine(Cx, Cy - 10.0f, Cx, Cy + 10.0f, DialScale, 1.4f);
	}

	// Wegpunkt-Marker (Diamant) an seiner projizierten Stelle + Distanz zum Spieler.
	if (bWaypointSet && Proj.IsValid())
	{
		const FVector2D M = Proj.Project(WaypointWorld);
		constexpr float D = 8.0f;
		DrawLine(M.X, M.Y - D, M.X + D, M.Y, MapWaypoint, 2.6f);
		DrawLine(M.X + D, M.Y, M.X, M.Y + D, MapWaypoint, 2.6f);
		DrawLine(M.X, M.Y + D, M.X - D, M.Y, MapWaypoint, 2.6f);
		DrawLine(M.X - D, M.Y, M.X, M.Y - D, MapWaypoint, 2.6f);

		const APawn* WpPawn = MapPC ? MapPC->GetPawn() : nullptr;
		if (WpPawn)
		{
			const double Dist = FVector2D::Distance(
				FVector2D(WaypointWorld.X, WaypointWorld.Y),
				FVector2D(WpPawn->GetActorLocation().X, WpPawn->GetActorLocation().Y));
			DrawText(FString::Printf(TEXT("Wegpunkt  %s"), *FormatMapDistance(Dist)),
				MapWaypoint, M.X + 12.0f, M.Y - 8.0f, GEngine ? GEngine->GetSmallFont() : nullptr, 1.0f);
		}
	}

	// Aktives Missionsziel (z. B. Dennos Lieferadresse) - dieselbe Bernstein-Raute
	// wie auf der Minikarte, damit man die Adresse auf dem Stadtplan findet.
	const UWiesbadenMissionSubsystem* MapMissions =
		GetWorld() ? GetWorld()->GetSubsystem<UWiesbadenMissionSubsystem>() : nullptr;
	const FMissionObjective* MapObjective = MapMissions ? MapMissions->GetCurrentObjective() : nullptr;
	if (MapObjective && Proj.IsValid())
	{
		const FLinearColor MissionMarker(1.0f, 0.72f, 0.20f, 1.0f);
		const FVector2D M = Proj.Project(MapObjective->Location);
		constexpr float D = 9.0f;
		DrawLine(M.X, M.Y - D, M.X + D, M.Y, MissionMarker, 3.0f);
		DrawLine(M.X + D, M.Y, M.X, M.Y + D, MissionMarker, 3.0f);
		DrawLine(M.X, M.Y + D, M.X - D, M.Y, MissionMarker, 3.0f);
		DrawLine(M.X - D, M.Y, M.X, M.Y - D, MissionMarker, 3.0f);
		const APawn* ObjPawn = MapPC ? MapPC->GetPawn() : nullptr;
		const double Dist = ObjPawn ? FVector::Dist2D(MapObjective->Location, ObjPawn->GetActorLocation()) : 0.0;
		DrawText(FString::Printf(TEXT("%s  %s"), *MapObjective->Label, *FormatMapDistance(Dist)),
			MissionMarker, M.X + 13.0f, M.Y - 8.0f, GEngine ? GEngine->GetSmallFont() : nullptr, 1.0f);
	}

	// Chrome.
	DrawText(TEXT("WIESBADEN"), DialText, 24.0f, 20.0f,
		GEngine ? GEngine->GetMediumFont() : nullptr, 1.7f);
	DrawLine(Width * 0.5f, 42.0f, Width * 0.5f, 66.0f, MapMajorRoad, 2.0f);
	DrawText(TEXT("N"), DialScale, Width * 0.5f - 5.0f, 22.0f,
		GEngine ? GEngine->GetMediumFont() : nullptr, 1.3f);

	// Suchfeld (Feature 6): oben zentriert, solange aktiv.
	// -- Die Suchleiste ------------------------------------------------------
	// Sie ist der Grund, warum die Karte bedienbar ist, und deshalb mehr als
	// ein Textfeld: Tippen, Vorschlaege darunter, und sobald eine Strasse
	// bestaetigt ist, der Knopf, der dorthin springt.
	{
		const float BoxW = 520.0f;
		const float ZeilenHoehe = 24.0f;
		const float VorschlagHoehe = MapSuggestions.Num() * ZeilenHoehe;
		const float BoxH = 34.0f + VorschlagHoehe + (bMapSearchActive ? 0.0f : 40.0f);
		const float Bx = Width * 0.5f - BoxW * 0.5f;
		const float By = 66.0f;
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.78f), Bx, By, BoxW, BoxH);

		if (bMapSearchActive)
		{
			DrawText(FString::Printf(TEXT("Strasse: %s_"), *MapSearchQuery), DialText,
				Bx + 12.0f, By + 7.0f, GEngine ? GEngine->GetMediumFont() : nullptr, 1.2f);
		}
		else if (!MapSearchStreet.IsEmpty())
		{
			// Geschlossen, aber eine Strasse gewaehlt: der Name steht da, und
			// darunter der Knopf. So sieht man auch spaeter noch, WOHIN der
			// Knopf springt - ein Knopf ohne Ziel waere eine Lotterie.
			DrawText(FString::Printf(TEXT("Strasse: %s"), *MapSearchStreet), DialText,
				Bx + 12.0f, By + 7.0f, GEngine ? GEngine->GetMediumFont() : nullptr, 1.2f);
		}
		else
		{
			DrawText(TEXT("Tab: Strasse suchen"), TellTaleOff,
				Bx + 12.0f, By + 7.0f, GEngine ? GEngine->GetMediumFont() : nullptr, 1.2f);
		}

		// Vorschlaege: exakter Name zuerst, dann was mit dem Getippten beginnt.
		for (int32 Index = 0; Index < MapSuggestions.Num(); ++Index)
		{
			const bool bMarkiert = (bMapSearchActive && Index == MapSuggestion);
			DrawText((bMarkiert ? TEXT("> ") : TEXT("  ")) + MapSuggestions[Index],
				bMarkiert ? IndicatorOn : DialScale,
				Bx + 12.0f, By + 38.0f + Index * ZeilenHoehe,
				GEngine ? GEngine->GetSmallFont() : nullptr, 1.1f);
		}

		// -- Der Knopf ---------------------------------------------------------
		// Aktiv, sobald eine Strasse bestaetigt wurde: heller Rahmen, und die
		// Beschriftung nennt die Taste. Inaktiv ist er grau und sagt, was
		// fehlt - ein Knopf, der auf einen Druck nichts tut, ist der schlimmste
		// Fall, weil er wie ein Fehler aussieht.
		if (!bMapSearchActive)
		{
			const float KnopfH = 30.0f;
			const float KnopfY = By + BoxH - KnopfH - 6.0f;
			const bool bScharf = !MapSearchStreet.IsEmpty();
			const FLinearColor Rahmen = bScharf ? IndicatorOn : TellTaleOff;
			const FLinearColor Fuellung(0.0f, 0.0f, 0.0f, bScharf ? 0.55f : 0.25f);
			DrawRect(Fuellung, Bx + 12.0f, KnopfY, BoxW - 24.0f, KnopfH);
			// Rahmen aus vier Linien - DrawRect kann nur fuellen.
			DrawRect(Rahmen, Bx + 12.0f, KnopfY, BoxW - 24.0f, 2.0f);
			DrawRect(Rahmen, Bx + 12.0f, KnopfY + KnopfH - 2.0f, BoxW - 24.0f, 2.0f);
			DrawRect(Rahmen, Bx + 12.0f, KnopfY, 2.0f, KnopfH);
			DrawRect(Rahmen, Bx + BoxW - 14.0f, KnopfY, 2.0f, KnopfH);
			DrawText(bScharf
					? TEXT(">  Warp to Location   (W / X)")
					: TEXT("Warp to Location - erst eine Strasse waehlen"),
				Rahmen, Bx + 24.0f, KnopfY + 6.0f,
				GEngine ? GEngine->GetMediumFont() : nullptr, 1.1f);
		}
	}

	DrawText(TEXT("Enter / Y / Klick: Wegpunkt setzen   Rueck / B: loeschen   C: auf den Spieler"),
		DialScale, 24.0f, Height - 62.0f, GEngine ? GEngine->GetSmallFont() : nullptr, 1.1f);
	DrawText(bMapSearchActive
		? TEXT("Tippen: Name   Pfeile: Vorschlag   Enter: bestaetigen   Tab/Esc: schliessen")
		: TEXT("M: schliessen   +/- oder LB/RB: Zoom   Pfeile: schwenken   Tab: suchen   W: ankommen"),
		DialScale, 24.0f, Height - 32.0f, GEngine ? GEngine->GetSmallFont() : nullptr, 1.1f);
	if (Proj.IsValid())
	{
		// Sichtbare Breite bei diesem Zoom (ganze Bildbreite / Massstab).
		const double VisWkm = (Proj.ScalePxPerCm > 0.0f)
			? (Width / Proj.ScalePxPerCm) / 100000.0
			: 0.0;
		DrawText(FString::Printf(TEXT("Zoom %.1fx   Sicht %.1f km"), MapZoom, VisWkm), DialScale,
			Width - 210.0f, Height - 32.0f, GEngine ? GEngine->GetSmallFont() : nullptr, 1.1f);
	}
}

void AWiesbadenVehicleHUD::DrawWorldMapLabels(
	const FRoadNetwork& Network, const FWorldMapProjection& Proj, float Width, float Height)
{
	if (!Canvas || !Proj.IsValid())
	{
		return;
	}

	// Premium-Stadtplan (Ausgabe-Senke): Strassennamen nur mit gekaufter
	// Freischaltung. Ohne sie bleibt die Karte mit Netz, aber ohne Beschriftung.
	const UWorld* MapWorld = GetWorld();
	const UGameInstance* MapGI = MapWorld ? MapWorld->GetGameInstance() : nullptr;
	const UWiesbadenGameStateSubsystem* MapGameState =
		MapGI ? MapGI->GetSubsystem<UWiesbadenGameStateSubsystem>() : nullptr;
	if (!MapGameState || !MapGameState->HasUnlock(FWiesbadenStore::PremiumStadtplanId()))
	{
		return;
	}
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	const FLinearColor LabelCol(0.97f, 0.97f, 0.92f, 1.0f);
	const FLinearColor ShadowCol(0.0f, 0.0f, 0.0f, 0.85f);

	// Ein Label je Strassenname, kollisionsarm. Nur Hauptstrassen -> das lesbare
	// Skelett; sichtbar im Sichtfenster (mit kleinem Rand). Beschriftungen liegen
	// im BILDSCHIRMRAUM, sind also bei jedem Zoom scharf und unverzerrt.
	TSet<FString> Placed;
	TArray<FVector2D> Positions;
	constexpr float MinGapPx = 96.0f;
	constexpr float Margin = 34.0f;
	constexpr int32 MaxLabels = 44;
	int32 Count = 0;

	for (const FRoadSegment& Segment : Network.Segments)
	{
		if (Count >= MaxLabels)
		{
			break;
		}
		if (Segment.StreetName.IsEmpty() || !FWiesbadenMinimap::IsMajorRoad(Segment.HighwayType))
		{
			continue;
		}
		if (Placed.Contains(Segment.StreetName))
		{
			continue;
		}
		const TArray<FVector>& Line = Segment.Centerline.Num() >= 2
			? Segment.Centerline : Segment.TrimmedCenterline;
		if (Line.Num() < 1)
		{
			continue;
		}
		const FVector2D P = Proj.Project(Line[Line.Num() / 2]);
		if (P.X < Margin || P.X > Width - Margin || P.Y < Margin || P.Y > Height - Margin)
		{
			continue;   // ausserhalb der Sicht
		}
		bool bTooClose = false;
		for (const FVector2D& Q : Positions)
		{
			if (FVector2D::DistSquared(P, Q) < MinGapPx * MinGapPx)
			{
				bTooClose = true;
				break;
			}
		}
		if (bTooClose)
		{
			continue;
		}
		Placed.Add(Segment.StreetName);
		Positions.Add(P);
		++Count;

		const float TextX = P.X - Segment.StreetName.Len() * 3.2f;
		const float TextY = P.Y - 7.0f;
		DrawText(Segment.StreetName, ShadowCol, TextX + 1.0f, TextY + 1.0f, Font, 1.0f);
		DrawText(Segment.StreetName, LabelCol, TextX, TextY, Font, 1.0f);
	}
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

void AWiesbadenVehicleHUD::DrawVehicleBanner(float CenterX, float Y)
{
	if (!Canvas)
	{
		return;
	}
	const APlayerController* PC = GetOwningPlayerController();
	APawn* Cur = PC ? PC->GetPawn() : nullptr;

	// Pawn-Wechsel selbst erkennen: nur bei einem ECHTEN Wechsel zwischen zwei
	// Pawns einblenden - nicht beim ersten Spawn (Prev == nullptr), damit der
	// Start nicht mit der Steuerungs-Legende kollidiert.
	if (Cur != LastBannerPawn.Get())
	{
		const APawn* Prev = LastBannerPawn.Get();
		LastBannerPawn = Cur;
		if (Prev != nullptr && Cur != nullptr)
		{
			VehicleBannerText = ResolveVehicleTransitionBanner(Cur);
			VehicleBannerAge = 0.0f;
		}
	}

	if (VehicleBannerText.IsEmpty())
	{
		return;
	}

	VehicleBannerAge += GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0f;
	constexpr float ShowSeconds = 2.2f;
	constexpr float FadeSeconds = 0.8f;
	if (VehicleBannerAge > ShowSeconds + FadeSeconds)
	{
		VehicleBannerText.Reset();
		return;
	}
	const float Alpha = (VehicleBannerAge <= ShowSeconds)
		? 1.0f
		: FMath::Clamp(1.0f - (VehicleBannerAge - ShowSeconds) / FadeSeconds, 0.0f, 1.0f);

	UFont* Font = GEngine ? GEngine->GetLargeFont() : nullptr;
	float TextWidth = 0.0f;
	float TextHeight = 0.0f;
	Canvas->TextSize(Font, VehicleBannerText, TextWidth, TextHeight);

	constexpr float PadX = 20.0f;
	constexpr float PadY = 8.0f;

	FLinearColor Bg = MapBackground;
	Bg.A *= Alpha;
	FLinearColor Fg = DialText;
	Fg.A *= Alpha;

	DrawRect(Bg, CenterX - TextWidth * 0.5f - PadX, Y - PadY,
		TextWidth + PadX * 2.0f, TextHeight + PadY * 2.0f);
	DrawText(VehicleBannerText, Fg, CenterX - TextWidth * 0.5f, Y, Font, 1.0f);
}

FString AWiesbadenVehicleHUD::ResolveVehicleTransitionBanner(const APawn* CurrentPawn)
{
	if (!CurrentPawn)
	{
		return FString();
	}
	if (Cast<AWiesbadenHelicopter>(CurrentPawn))
	{
		return TEXT("Eingestiegen: Helikopter");
	}
	if (Cast<AWiesbadenBugTankPawn>(CurrentPawn))
	{
		return TEXT("Eingestiegen: BugTank");
	}
	if (Cast<IWiesbadenVehicleControl>(CurrentPawn))
	{
		// Ueber die Steuernaht - erfasst Kaefer UND ChaosCar.
		return TEXT("Eingestiegen: Fahrzeug");
	}
	return TEXT("Ausgestiegen - zu Fuss");
}

namespace
{
	/** Planarer Abstand (XY) in cm - Drift-, Ziel- und Naehe-Rechnungen. */
	double WbPlanarDistanceCm(const FVector2D& A, const FVector2D& B)
	{
		const double Dx = A.X - B.X;
		const double Dy = A.Y - B.Y;
		return FMath::Sqrt(Dx * Dx + Dy * Dy);
	}
}

bool AWiesbadenVehicleHUD::IsPlayerIdle() const
{
	// 'Gas' gibt es an der Steuernaht nicht als Live-Wert - Idle-Proxi ist die
	// Fahrzeuggeschwindigkeit (Tempo ~0 = kein Gas). Zu Fuss oder ohne Fahrzeug
	// gibt es keinen Leerlauf-Hinweis.
	const IWiesbadenVehicleControl* Control = GetPlayerVehicleControl();
	return Control && FMath::IsNearlyZero(Control->GetSpeedKmh());
}

bool AWiesbadenVehicleHUD::TryGetPlayerPlanarPos(FVector2D& OutPlanarPos) const
{
	const APlayerController* PC = GetOwningPlayerController();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		return false;
	}
	const FVector Location = Pawn->GetActorLocation();
	OutPlanarPos = FVector2D(Location.X, Location.Y);
	return true;
}

AWiesbadenVehicleHUD::EFirstRunContext AWiesbadenVehicleHUD::ResolveFirstRunContext() const
{
	// Kontext aus den Naehe-Koerben, die der Zu-Fuss-Pfad ohnehin pflegt.
	if (GetPlayerVehicleControl())
	{
		return EFirstRunContext::VehicleIdle;
	}
	if (CachedFootVehicleCm >= 0.0 && CachedFootVehicleCm <= FootVehicleReachCm)
	{
		return EFirstRunContext::FootNearVehicle;
	}
	if (CachedFootFunicularCm >= 0.0 && CachedFootFunicularCm <= FootFunicularReachCm)
	{
		return EFirstRunContext::FootNearFunicular;
	}
	return EFirstRunContext::FootNearNPC;
}

void AWiesbadenVehicleHUD::UpdateFirstRunOnboarding()
{
	const UWorld* HudWorld = GetWorld();
	if (!HudWorld)
	{
		return;
	}

	const UWiesbadenCitySubsystem* City = HudWorld->GetSubsystem<UWiesbadenCitySubsystem>();
	const bool bPlayerIdle = IsPlayerIdle();

	// Einmaliger Einblend-Hinweis direkt nach dem Streaming.
	if (bPlayerIdle && !bWishPromptShown && City && City->IsCityStreamingComplete())
	{
		bWishPromptShown = true;
		ShowTransientHint(TEXT("W gasen, A/D lenken - F steigt aus"));
	}

	if (!FirstRun.bArmed)
	{
		ArmFirstRunPrompt(*HudWorld, City, bPlayerIdle);
	}

	// HIER STAND DAS ERSTKONTAKT-BANNER MIT DEM STRASSENNAMEN.
	//
	// Beim Start blendete das HUD mittig "Marktstrasse" ein - den Namen der
	// Strasse, auf der die Scharfschaltung zufaellig geschah. Es sagte nichts,
	// was das HUD nicht ohnehin dauerhaft oben anzeigt, und es veraltete: der
	// Name wurde EINMAL beim Scharfschalten genommen und danach nicht mehr
	// nachgefuehrt. Ersatzlos entfernt, zusammen mit ComposeFirstRunText und
	// ComposeFirstRunBanner, die nur dafuer da waren.
	//
	// Der Steuerungshinweis bleibt - er nennt keinen Ort, sondern die Tasten,
	// und haengt jetzt allein an der Scharfschaltung statt an einem Titel.
	if (FirstRun.bArmed)
	{
		ShowFirstRunContextHintOnce();
	}

	// Ehrlich zuruecknehmen: abgelaufen, nicht mehr im Leerlauf oder abgedriftet.
	if (FirstRun.bArmed && ShouldWithdrawFirstRunPrompt(*HudWorld, bPlayerIdle))
	{
		FirstRun.bArmed = false;

		// Und dann WIRKLICH vorbei: ohne diesen Merker griff oben sofort wieder
		// ArmFirstRunPrompt (Bedingung ist nur "Stadt fertig + Leerlauf"), der
		// Hinweis kam sonst bei jedem Halt zurueck.
		FirstRun.bConsumed = true;
	}
}

void AWiesbadenVehicleHUD::ArmFirstRunPrompt(
	const UWorld& World, const UWiesbadenCitySubsystem* City, bool bPlayerIdle)
{
	// Verdient: erst wenn die Stadt fertig gestreamt ist UND der Spieler noch
	// nichts getan hat - und nur EINMAL je Sitzung (bConsumed), sonst ist es
	// keine Erstkontakt-Hilfe mehr, sondern eine Dauerschleife bei jedem Halt.
	if (FirstRun.bConsumed || !City || !City->IsCityStreamingComplete() || !bPlayerIdle)
	{
		return;
	}

	FirstRun.bArmed = true;
	FirstRun.ArmingStartedAt = World.GetTimeSeconds();
	FirstRun.ExpiresAt = FirstRun.ArmingStartedAt + 60.0f;

	FVector2D ArmPos;
	if (TryGetPlayerPlanarPos(ArmPos))
	{
		FirstRun.ArmWorldPos = ArmPos;
	}

	FirstRun.Context = ResolveFirstRunContext();
}

void AWiesbadenVehicleHUD::ShowFirstRunContextHintOnce()
{
	if (FirstRun.bModeSpecificHintShown)
	{
		return;
	}
	FirstRun.bModeSpecificHintShown = true;

	switch (FirstRun.Context)
	{
	case EFirstRunContext::VehicleIdle:
		ShowTransientHint(TEXT("W gasen, A/D lenken — F steigt aus"));
		break;
	case EFirstRunContext::FootNearVehicle:
		ShowTransientHint(TEXT("F einsteigen / E mitfahren"));
		break;
	case EFirstRunContext::FootNearFunicular:
		ShowTransientHint(TEXT("E mitfahren — Nerobergbahn"));
		break;
	case EFirstRunContext::FootNearNPC:
		ShowTransientHint(ResolveMerchantCue());
		break;
	default:
		break;
	}
}

FString AWiesbadenVehicleHUD::ResolveMerchantCue() const
{
	// Der Suchlauf hat den naechsten Haendler hinterlegt; hier wird nur noch
	// sein Text geholt, und zwar ab dem Spielerstandort gemessen - nicht ab
	// dem Weltursprung, sonst liegt jeder Haendler ausserhalb der Reichweite.
	FVector2D PlayerPos;
	if (!TryGetPlayerPlanarPos(PlayerPos))
	{
		return FString(TEXT("nah ran und F"));
	}

	TArray<AActor*> Merchants;
	if (CachedFootMerchant.IsValid())
	{
		Merchants.Add(CachedFootMerchant.Get());
	}

	FString Cue;
	AWiesbadenStoreMerchant::DescribeNearestMerchantInReach(
		Merchants, FVector(PlayerPos.X, PlayerPos.Y, 0.0), Cue);
	return Cue.IsEmpty() ? FString(TEXT("nah ran und F")) : Cue;
}

bool AWiesbadenVehicleHUD::ShouldWithdrawFirstRunPrompt(
	const UWorld& World, bool bPlayerIdle) const
{
	if (World.GetTimeSeconds() >= FirstRun.ExpiresAt || !bPlayerIdle)
	{
		return true;
	}
	if (FirstRun.ArmWorldPos.IsZero())
	{
		return false;
	}

	FVector2D PlayerPos;
	if (!TryGetPlayerPlanarPos(PlayerPos))
	{
		return false;
	}
	return WbPlanarDistanceCm(PlayerPos, FirstRun.ArmWorldPos) > 20000.0;
}
