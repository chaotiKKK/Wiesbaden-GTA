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

	// Maschinenlesbaren Gesundheitsbericht ausgeben + als JSON nach Saved/Logs.
	// Ohne Argument (bzw. <=0): sofort. Mit <Sekunden> > 0: Gate-Modus - wartet auf
	// den geladenen Zustand (Stadt da + Streaming fertig), maximal <Sekunden>, dann
	// erst der Dump. So spiegelt das JSON den geladenen Zustand statt eines
	// Startup-Transienten (fuer den Rauchtest-Gate nach dem Laden).
	UFUNCTION(Exec)
	void WbHealth(float MaxWaitSeconds = 0.0f);

	// Kameramodus des besessenen Fahrzeugs: 0=Follow, 1=Orbit, 2=Cockpit.
	UFUNCTION(Exec)
	void WbCam(int32 Modus);

	// Uebernimmt einen Helikopter der Welt (Dev-Hilfe zum Testen).
	//
	// Der INDEX ist noetig, seit es zwei fliegbare Maschinen gibt: ohne ihn
	// erwischte man immer dieselbe, und die zweite waere fuer jede Pruefung
	// unerreichbar. 0 = der erste gefundene, 1 = der zweite.
	UFUNCTION(Exec)
	void WbHeli(int32 Index = 0);

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

	// Startet das Skript-Fahrprofil (Vollgas + Lenk-Sweep) am besessenen Fahrzeug
	// fuer <Sekunden> - weist Fahrphysik und Lenkung ohne Tastatur nach.
	UFUNCTION(Exec)
	void WbDrive(int32 Sekunden);

	// Autopilot: fliegt den besessenen Helikopter zu einem Punkt <dx dy dz> Meter
	// relativ zur aktuellen Position (Weltachsen) und haelt ihn dort.
	UFUNCTION(Exec)
	void WbHeliGoto(int32 DeltaXMeter, int32 DeltaYMeter, int32 DeltaZMeter);

	// Autopilot: haelt die aktuelle Position/Hoehe (Schweben).
	UFUNCTION(Exec)
	void WbHeliHover();

	// Autopilot AUS: gibt die Steuerung an Tastatur/Gamepad zurueck (mitten im Flug).
	UFUNCTION(Exec)
	void WbHeliOff();

	// Spawnt einen reaktiven Verfolger 40 m vor dem Spieler (Dev/Test).
	UFUNCTION(Exec)
	void WbSpawnPursuer();

	// Nimmt bei Dennos Laden einen Lieferauftrag an, ohne dort zu stehen - mit
	// festem Zufallswert, damit ein Lauf dieselbe Adresse wieder zieht. Steht der
	// Laden noch nicht (Streaming), wird der Auftrag vorgemerkt. DelaySeconds
	// schiebt die Annahme auf (Aufnahmen: Dennos Paketuebergabe erst, wenn die
	// Kamera laeuft).
	UFUNCTION(Exec)
	void WbDennoAuftrag(int32 Seed = 1, float DelaySeconds = 0.0f);

private:
	// Schreibt den Gesundheitsbericht JETZT (JSON + Log). Gemeinsame Endstrecke von
	// WbHealth (sofort) und dem Gate-Poll.
	void WriteHealthReport();

	// Poll des Gate-Modus: schreibt den Bericht, sobald der geladene Zustand
	// erreicht ist ODER der Deckel abgelaufen ist.
	void PollHealthGate();

	FTimerHandle HealthGateTimer;
	double HealthGateDeadlineSeconds = 0.0;
};
