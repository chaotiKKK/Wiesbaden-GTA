// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "WiesbadenPlayerController.generated.h"

/**
 * Spieler-Controller mit den Dev-Konsolenbefehlen.
 *
 * WICHTIG - warum hier und nicht in einem GameInstanceSubsystem:
 * -ExecCmds laeuft ueber UEngine::TickDeferredCommands -> ULocalPlayer::Exec.
 * Diese Kette (Engine/Private/Player.cpp) fragt der Reihe nach PlayerInput,
 * den PlayerController (ExecActor), den Pawn, das HUD, den GameMode, den
 * CheatManager, GameState und CameraManager ab - NICHT aber die GameInstance
 * oder deren Subsysteme. Die GameInstance-Exec-Funktionen erreicht nur die
 * In-Game-Konsole ueber den GameViewportClient. Ein Subsystem waere also per
 * -ExecCmds nicht ansprechbar gewesen (nachgewiesen: Befehle feuerten nie).
 *
 * Der PlayerController wird als ExecActor VOR Pawn/HUD/GameMode geprueft und
 * ist damit der zuverlaessigste, skriptbare Ort. Die Befehle sind reine
 * Dev-Helfer und delegieren an FWiesbadenDevActions - dieselbe Logik wie das
 * Pause-Menue (DRY).
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	// Teleportiert den besessenen Pawn: 0=Platter Strasse, 1=Nerobergbahn, 2=Garten.
	UFUNCTION(Exec)
	void WbTeleport(int32 Ziel);

	// Richtet den besessenen Pawn auf (Nick/Roll 0) und laesst ihn auf die Raeder fallen.
	UFUNCTION(Exec)
	void WbResetVehicle();

	// Verkehr an (1) oder aus (0).
	UFUNCTION(Exec)
	void WbTraffic(int32 An);

	// Kameramodus des besessenen Fahrzeugs: 0=Follow, 1=Orbit, 2=Cockpit.
	UFUNCTION(Exec)
	void WbCam(int32 Modus);

	// Uebernimmt den naechsten Helikopter der Welt (Dev-Hilfe zum Testen).
	UFUNCTION(Exec)
	void WbHeli();

	// Kippt das besessene Fahrzeug um Nick/Roll (Grad) - Testhilfe, um das
	// Aufrichten (WbResetVehicle) sichtbar vorzufuehren.
	UFUNCTION(Exec)
	void WbNudge(int32 NickGrad, int32 RollGrad);

	// Startet die Skript-Gierprobe am besessenen Helikopter fuer <Sekunden>.
	UFUNCTION(Exec)
	void WbHeliYaw(int32 Sekunden);

	// Startet das Skript-Flugprofil (Steigen/Marsch/Sinken) fuer <Sekunden>.
	UFUNCTION(Exec)
	void WbHeliFly(int32 Sekunden);
};
