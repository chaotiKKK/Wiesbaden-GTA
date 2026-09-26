// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenSebboFigureComponent.h"

#include "WiesbadenReal.h"

#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"

bool UWiesbadenSebboFigureComponent::SetupFigure(float InJumpAirSeconds)
{
	USkeletalMesh* Skeletal = LoadObject<USkeletalMesh>(
		nullptr, TEXT("/Game/Assets/People/Sebbo/Meshes/SK_Sebbo.SK_Sebbo"));
	if (!Skeletal)
	{
		return false;
	}

	Moves.SetNum(static_cast<int32>(EWbSebboMove::Count));
	FString Fehlend;
	int32 Geladen = 0;
	for (int32 I = 0; I < Moves.Num(); ++I)
	{
		const FString Name = MoveName(static_cast<EWbSebboMove>(I));
		Moves[I] = LoadObject<UAnimSequence>(nullptr, *FString::Printf(
			TEXT("/Game/Assets/People/Sebbo/Animations/A_Sebbo_%s.A_Sebbo_%s"), *Name, *Name));
		if (Moves[I]) { ++Geladen; } else { Fehlend += Name + TEXT(" "); }
	}

	// Ohne Stehen, Gehen, Rennen und Springen lohnt die Figur nicht: ein
	// Skelett, das reglos in T-Haltung ueber die Strasse gleitet, waere
	// schlechter als das statische Modell. Die uebrigen sind Zugaben.
	if (!HasMove(EWbSebboMove::Idle) || !HasMove(EWbSebboMove::Walk)
		|| !HasMove(EWbSebboMove::Run) || !HasMove(EWbSebboMove::Jump))
	{
		UE_LOG(LogWbVehicles, Warning,
			TEXT("Spielerfigur: SK_Sebbo ohne Grundbewegungen (es fehlen: %s) - statisches Modell."), *Fehlend);
		Moves.Reset();
		return false;
	}

	SetSkeletalMesh(Skeletal);
	JumpAirSeconds = InJumpAirSeconds;
	bReady = true;
	CurrentMove = EWbSebboMove::Count;
	PlayMove(EWbSebboMove::Idle, true, 1.0f);
	UE_LOG(LogWbVehicles, Log,
		TEXT("Spielerfigur: SK_Sebbo (Tripo, 61 Knochen) mit %d von %d Bewegungen%s%s."),
		Geladen, Moves.Num(), Fehlend.IsEmpty() ? TEXT("") : TEXT(", es fehlen: "), *Fehlend);
	return true;
}

bool UWiesbadenSebboFigureComponent::HasMove(EWbSebboMove Move) const
{
	return Moves.IsValidIndex(static_cast<int32>(Move)) && Moves[static_cast<int32>(Move)] != nullptr;
}

float UWiesbadenSebboFigureComponent::MoveLength(EWbSebboMove Move) const
{
	return HasMove(Move) ? Moves[static_cast<int32>(Move)]->GetPlayLength() : 0.0f;
}

void UWiesbadenSebboFigureComponent::PlayOneShot(EWbSebboMove Move, float Seconds)
{
	if (!bReady || !HasMove(Move))
	{
		return;
	}
	// Einmalig, keine Schleife. CurrentMove auf "keine", damit danach die
	// passende Dauerbewegung NEU startet - sonst bliebe die Figur im letzten
	// Bild stehen.
	OneShotMove = Move;
	OneShotRemaining = FMath::Max(Seconds, 0.1f);
	PlayAnimation(Moves[static_cast<int32>(Move)], false);
	SetPlayRate(MoveLength(Move) / OneShotRemaining);
	CurrentMove = EWbSebboMove::Count;
}

void UWiesbadenSebboFigureComponent::CancelOneShot(EWbSebboMove Move)
{
	if (OneShotMove == Move)
	{
		OneShotRemaining = 0.0f;
		OneShotMove = EWbSebboMove::Count;
	}
}

void UWiesbadenSebboFigureComponent::Animate(float DeltaSeconds, const FWbFigureInput& Input)
{
	if (!bReady || DeltaSeconds <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// Drehrate aus der Blickrichtung, geglaettet: die Maus liefert je Bild
	// sprunghafte Werte, und ohne Glaettung flackerte Turn/Idle.
	const float RawRate = bHasPreviousYaw
		? FMath::FindDeltaAngleDegrees(PreviousYaw, Input.YawDeg) / DeltaSeconds : 0.0f;
	PreviousYaw = Input.YawDeg;
	bHasPreviousYaw = true;
	YawRateDegS = FMath::Lerp(YawRateDegS, RawRate, FMath::Clamp(DeltaSeconds / 0.15f, 0.0f, 1.0f));

	// Ein Bild ohne Vorankommen (Bordsteinkante, Stufe) ist kein Anhalten:
	// sonst sprang die Figur mitten im Rennen fuer ein Bild ins Idle, und der
	// Laufzyklus begann danach von vorn. Erst 0,2 s ohne Vorankommen gelten
	// als Stehen.
	float SpeedMps = Input.SpeedMps;
	if (SpeedMps > 0.4f)
	{
		StopSeconds = 0.0f;
		LastMovingSpeedMps = SpeedMps;
	}
	else
	{
		StopSeconds += DeltaSeconds;
		if (StopSeconds < 0.2f && (CurrentMove == EWbSebboMove::Walk || CurrentMove == EWbSebboMove::Run
			|| CurrentMove == EWbSebboMove::CrouchWalk))
		{
			SpeedMps = LastMovingSpeedMps;
		}
	}

	const bool bStill = SpeedMps <= 0.4f && FMath::Abs(YawRateDegS) < 25.0f && !Input.bAirborne
		&& !Input.bCrouching;
	StillSeconds = bStill ? StillSeconds + DeltaSeconds : 0.0f;

	// Trefferreaktion bei jedem Abfall der Gesundheit. Heute kennt das Spiel
	// noch keinen Schaden am Spieler (Gesundheit steigt nur ueber Pickups) -
	// die Bewegung steht bereit, sobald es einen gibt.
	if (LastHealthPoints >= 0.0f && Input.HealthPoints < LastHealthPoints - 0.5f)
	{
		PlayOneShot(EWbSebboMove::Hit, MoveLength(EWbSebboMove::Hit));
	}
	LastHealthPoints = Input.HealthPoints;

	// Waehrend Tritt oder Treffer nichts ueberschreiben.
	if (OneShotRemaining > 0.0f)
	{
		OneShotRemaining = FMath::Max(0.0f, OneShotRemaining - DeltaSeconds);
		return;
	}
	OneShotMove = EWbSebboMove::Count;

	FWbSebboMoveState State;
	State.SpeedMps = SpeedMps;
	State.YawRateDegS = YawRateDegS;
	State.StillSeconds = StillSeconds;
	State.bAirborne = Input.bAirborne;
	State.bRiding = Input.bRiding;
	State.bCrouching = Input.bCrouching;
	State.Previous = CurrentMove;
	EWbSebboMove Move = ChooseMove(State, RunFromMps,
		MoveLength(EWbSebboMove::Swagger), MoveLength(EWbSebboMove::Call));
	if (!HasMove(Move))
	{
		Move = EWbSebboMove::Idle;   // Zugabe fehlt: stehen statt T-Haltung
	}

	switch (Move)
	{
	case EWbSebboMove::Walk:
	case EWbSebboMove::CrouchWalk:   // gebaut auf den Fussbahnen von Walk: gleicher Schritt
		// Schritttakt ans Tempo: sonst glitten die Fuesse ueber den Asphalt.
		PlayMove(Move, true, FMath::Clamp(SpeedMps / WalkAnimSpeedMps, 0.5f, 2.0f));
		break;
	case EWbSebboMove::Run:
		PlayMove(Move, true, FMath::Clamp(SpeedMps / RunAnimSpeedMps, 0.6f, 1.6f));
		break;
	case EWbSebboMove::Jump:
		// Einmal je Luftphase. Die Aufnahme (2,2 s) zeigt Absprung, Flug und
		// Landung; die Flugzeit bekommt 0,4 s fuer Absprung und Landung dazu.
		PlayMove(Move, false, FMath::Clamp(MoveLength(Move) / (JumpAirSeconds + 0.4f), 1.0f, 2.5f));
		break;
	default:
		PlayMove(Move, true, 1.0f);
		break;
	}
}

void UWiesbadenSebboFigureComponent::PlayMove(EWbSebboMove Move, bool bLoop, float PlayRate)
{
	if (!HasMove(Move))
	{
		return;
	}
	if (Move != CurrentMove)
	{
		PlayAnimation(Moves[static_cast<int32>(Move)], bLoop);
		CurrentMove = Move;
	}
	SetPlayRate(PlayRate);
}

EWbSebboMove UWiesbadenSebboFigureComponent::ChooseMove(const FWbSebboMoveState& State, float RunFromMps,
	float SwaggerSeconds, float CallSeconds)
{
	if (State.bRiding)
	{
		return EWbSebboMove::Surf;
	}
	if (State.bAirborne)
	{
		return EWbSebboMove::Jump;
	}
	if (State.bCrouching)
	{
		return State.SpeedMps > 0.4f ? EWbSebboMove::CrouchWalk : EWbSebboMove::CrouchIdle;
	}
	if (State.SpeedMps > RunFromMps)
	{
		return EWbSebboMove::Run;
	}
	if (State.SpeedMps > 0.4f)
	{
		return EWbSebboMove::Walk;
	}
	// Drehen im Stand, mit Hysterese: an ab 60 Grad/s, aus unter 25.
	const float TurnFrom = State.Previous == EWbSebboMove::Turn ? 25.0f : 60.0f;
	if (FMath::Abs(State.YawRateDegS) >= TurnFrom)
	{
		return EWbSebboMove::Turn;
	}

	// Stehen: Idle, ab 10 s einmal Stolzieren, ab 30 s das Telefonat, dann
	// von vorn.
	constexpr float SwaggerAt = 10.0f;
	constexpr float CallAt = 30.0f;
	const float Period = CallAt + FMath::Max(CallSeconds, 0.0f);
	const float T = FMath::Fmod(FMath::Max(State.StillSeconds, 0.0f), Period);
	if (T >= SwaggerAt && T < SwaggerAt + SwaggerSeconds)
	{
		return EWbSebboMove::Swagger;
	}
	if (T >= CallAt && CallSeconds > 0.0f)
	{
		return EWbSebboMove::Call;
	}
	return EWbSebboMove::Idle;
}

FString UWiesbadenSebboFigureComponent::MoveName(EWbSebboMove Move)
{
	return StaticEnum<EWbSebboMove>()->GetNameStringByValue(static_cast<int64>(Move));
}
