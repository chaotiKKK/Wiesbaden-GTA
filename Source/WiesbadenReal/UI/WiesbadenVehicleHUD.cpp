// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "UI/WiesbadenVehicleHUD.h"

#include "WiesbadenReal.h"

#include "CanvasItem.h"
#include "Core/WiesbadenDevActions.h"
#include "Engine/Canvas.h"
#include "TextureResource.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Vehicles/WiesbadenCar.h"
#include "Vehicles/WiesbadenVehicleControl.h"
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

	// Pausemenue zuerst: es liegt ueber allem und haelt die Zeit an.
	UpdatePauseMenu();
	if (bPaused)
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
		}
		bMapKeyHeld = bMapDown;
	}
	if (bWorldMapOpen)
	{
		DrawWorldMap(Width, Height);
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

	// Fahrzeug ueber die Steuernaht-Familie (Kaefer wie ChaosCar); der Heli hat
	// seine eigene Instrumententafel.
	IWiesbadenVehicleControl* Vehicle = GetPlayerVehicleControl();
	const AWiesbadenHelicopter* Heli = Vehicle ? nullptr : GetPlayerHelicopter();
	const bool bInVehicle = (Vehicle != nullptr) || (Heli != nullptr);

	// Legende zeichnen, solange sie eingeschaltet ist. Nach
	// ControlLegendSeconds blendet sie von selbst aus; F1 holt sie zurueck.
	// Sie erscheint AUCH zu Fuss - dort gab es bisher gar keine Anzeige.
	if (bShowControlLegend && ElapsedSeconds <= ControlLegendSeconds)
	{
		DrawControlLegend(bInVehicle, 40.0f, Height - 210.0f);
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
		// Zu Fuss: statt Tacho der Hinweis, was hier gerade moeglich ist.
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

	// Der Actor-Suchlauf (dreimal ueber ALLE Actors der Stadt) ist teuer -
	// darum nur wenige Male je Sekunde, nicht je Bild. Die Entfernung eines
	// Naeherungshinweises darf ein Drittel Sekunde alt sein.
	FootPromptScanAge += HudWorld->GetDeltaSeconds();
	if (FootPromptScanAge > 0.3f)
	{
		TArray<AActor*> Cars;
		TArray<AActor*> Helicopters;
		TArray<AActor*> Funiculars;
		UGameplayStatics::GetAllActorsOfClass(HudWorld, AWiesbadenCar::StaticClass(), Cars);
		UGameplayStatics::GetAllActorsOfClass(HudWorld, AWiesbadenHelicopter::StaticClass(), Helicopters);
		UGameplayStatics::GetAllActorsOfClass(HudWorld, AWiesbadenNerobergbahn::StaticClass(), Funiculars);
		Cars.Append(Helicopters);
		CachedFootVehicleCm = NearestOf(Cars);
		CachedFootFunicularCm = NearestOf(Funiculars);
		FootPromptScanAge = 0.0f;
	}

	const FString Prompt = BuildFootPrompt(
		CachedFootVehicleCm, CachedFootFunicularCm,
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
	OutEntries.Add(TEXT("Entwickler: zum Garten Nerotal 48"));
	OutEntries.Add(TEXT("Entwickler: Fahrzeug aufrichten"));
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
	case 4:
	{
		// Zuruecksetzen an einen festen Ort. Die Zielpunkte und Fallhoehe leben
		// datenrein in FWiesbadenDevActions - dieselbe Logik nutzt der
		// Konsolenbefehl WbTeleport (DRY, unter Automation getestet).
		const EWiesbadenDevTeleport Target = static_cast<EWiesbadenDevTeleport>(Index - 2);
		if (APawn* Pawn = PC->GetPawn())
		{
			Pawn->SetActorLocation(FWiesbadenDevActions::TeleportSpawnCm(Target),
				/*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
		}
		Unpause();
		break;
	}

	case 5:
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
	}

	case 6:
		if (UWiesbadenCitySubsystem* City = HudWorld->GetSubsystem<UWiesbadenCitySubsystem>())
		{
			// Verkehr aus: die schnellste Art zu pruefen, ob ein Ruckler vom
			// Verkehr kommt oder von der Darstellung.
			const float Density = City->TrafficSimulation.GetDensity();
			City->TrafficSimulation.Settings.TrafficDensity = (Density > 0.01f) ? 0.0f : 0.5f;
		}
		Unpause();
		break;

	case 7:
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

	// Massstab: Der Umkreis in Metern, damit die Karte lesbar bleibt.
	const FString ScaleText = FString::Printf(TEXT("%.0f m"), Settings.RangeCm / 100.0);
	DrawText(ScaleText, DialScale, CenterX - Radius + 8.0f, CenterY + Radius - 20.0f,
		GEngine ? GEngine->GetSmallFont() : nullptr, 1.0f);
}

void AWiesbadenVehicleHUD::DrawWorldMap(float Width, float Height)
{
	if (!Canvas)
	{
		return;
	}

	// Abgedunkelter Vollbild-Hintergrund - halbtransparent, damit klar ist, dass
	// die Karte ein Overlay ist (das Spiel laeuft dahinter weiter).
	DrawRect(FLinearColor(0.02f, 0.03f, 0.05f, 0.88f), 0.0f, 0.0f, Width, Height);

	const FRoadNetwork* Network = FindRoadNetwork();
	if (!Network)
	{
		DrawText(TEXT("Karte laedt..."), DialText, Width * 0.5f - 48.0f, Height * 0.5f,
			GEngine ? GEngine->GetMediumFont() : nullptr, 1.3f);
		return;
	}

	const FVector2D ScreenCentre(Width * 0.5f, Height * 0.5f);
	const FVector2D ScreenSize(Width, Height);

	// Das ganze Netz EINMAL projizieren (bzw. bei Groessen-/Netzwechsel neu). Je
	// Bild ueber ~125.000 Segmente zu laufen waere der teuerste HUD-Posten.
	if (CachedWorldMapNetwork != Network
		|| !CachedWorldMapSize.Equals(ScreenSize, 1.0f)
		|| CachedWorldMapLines.Num() == 0)
	{
		FVector2D WMin, WMax;
		if (FWiesbadenMinimap::ComputeNetworkBoundsXY(*Network, WMin, WMax))
		{
			CachedWorldMapProj = FWiesbadenMinimap::MakeWorldMapProjection(
				WMin, WMax, ScreenCentre, ScreenSize, /*MarginFrac=*/0.88f);
			FWiesbadenMinimap::BuildWorldMapLines(
				*Network, CachedWorldMapProj, /*MaxLines=*/12000, /*MinSegmentPx=*/1.5f,
				CachedWorldMapLines);
			CachedWorldMapNetwork = Network;
			CachedWorldMapSize = ScreenSize;
		}
	}

	for (const FMinimapLine& Line : CachedWorldMapLines)
	{
		DrawLine(Line.Start.X, Line.Start.Y, Line.End.X, Line.End.Y,
			Line.bMajor ? MapMajorRoad : MapMinorRoad, Line.Thickness);
	}

	// Spielerpunkt + Fahrtrichtung (je Bild neu projiziert - billig).
	if (CachedWorldMapProj.IsValid())
	{
		const APlayerController* PC = GetOwningPlayerController();
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (Pawn)
		{
			const FVector2D P = CachedWorldMapProj.Project(Pawn->GetActorLocation());
			const double YawRad = FMath::DegreesToRadians(Pawn->GetActorRotation().Yaw);
			// Welt +X (Ost) -> Bildschirm +X, Welt +Y (Nord) -> Bildschirm -Y.
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

	// Titel, Nordzeiger und Schliess-Hinweis.
	DrawText(TEXT("WIESBADEN"), DialText, 24.0f, 20.0f,
		GEngine ? GEngine->GetMediumFont() : nullptr, 1.7f);
	DrawLine(Width * 0.5f, 42.0f, Width * 0.5f, 66.0f, MapMajorRoad, 2.0f);
	DrawText(TEXT("N"), DialScale, Width * 0.5f - 5.0f, 22.0f,
		GEngine ? GEngine->GetMediumFont() : nullptr, 1.3f);
	DrawText(TEXT("M / Select: schliessen"), DialScale, 24.0f, Height - 32.0f,
		GEngine ? GEngine->GetSmallFont() : nullptr, 1.1f);

	// Massstab: Kartenbreite in Kilometern.
	if (CachedWorldMapProj.IsValid())
	{
		const double WorldWkm =
			(CachedWorldMapProj.WorldMax.X - CachedWorldMapProj.WorldMin.X) / 100000.0;
		DrawText(FString::Printf(TEXT("Breite: %.1f km"), WorldWkm), DialScale,
			Width - 170.0f, Height - 32.0f, GEngine ? GEngine->GetSmallFont() : nullptr, 1.1f);
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
			if (Cast<AWiesbadenHelicopter>(Cur))
			{
				VehicleBannerText = TEXT("Eingestiegen: Helikopter");
			}
			else if (Cast<IWiesbadenVehicleControl>(Cur))
			{
				// Ueber die Steuernaht - erfasst Kaefer UND ChaosCar.
				VehicleBannerText = TEXT("Eingestiegen: Fahrzeug");
			}
			else
			{
				VehicleBannerText = TEXT("Ausgestiegen - zu Fuss");
			}
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
