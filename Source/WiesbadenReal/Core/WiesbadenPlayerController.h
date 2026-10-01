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
	//
	// ALLE Exec-Parameter haben einen Standardwert. Grund, am 26.09.2026 an
	// der Engine gemessen: ein UFUNCTION(Exec) OHNE Standardwert laesst sich
	// ueber die Konsole gar nicht aufrufen, auch nicht argumentlos. Die
	// Engine meldet dann "Bad or missing property '<Parameter>'", und
	// UObject::CallFunctionByNameWithArguments sucht ein Objekt-Property
	// statt eines Funktionsparameters. Ein Argument kann man ihr trotzdem
	// nicht geben - "WbHeliFly 24" und "WbHeliFly=24" scheitern beide. Die
	// Dauer der Flugbefehle kommt deshalb aus der CVar wb.Sekunden, wenn
	// hier 0 ankommt.
	//
	// Der Kommentar steht VOR dem UFUNCTION und nicht zwischen Makro und
	// Deklaration: Tools/check_wbdev_docs.ps1 liest das Makro nur direkt
	// vor der Zeile und meldete WbTeleport daraufhin als "dokumentiert, aber
	// es gibt keinen UFUNCTION(Exec) dieses Namens" (PHANTOM). Die Engine
	// selbst ist tolerant - der Befehl laeuft, im Rauchtest steht
	// "WbDev: WbTeleport 2 ausgefuehrt" -, aber die Pruefung darf nicht an
	// der Formatierung des Headers haengen.
	UFUNCTION(Exec)
	void WbTeleport(int32 Ziel = 0);

	/**
	 * Setzt den besessenen Pawn auf eine Strasse ("Warp to Location").
	 *
	 * Der Weg fuehrt auf die FAHRBAHN, in Fahrtrichtung, und laesst den
	 * besessenen Pawn Pawn bleiben: wer im Auto ist, landet im Auto, wer zu
	 * Fuss ist, zu Fuss. Ein Moduswechsel waere eine zweite Entscheidung, die
	 * man nicht verlangt hat - und im Auto auf dem Fussweg zu landen waere
	 * der Wagen im Weg.
	 *
	 * Die Strasse muss zum Namen passen (exakt oder als Teiltext). Der Punkt
	 * kommt aus der Mittellinie des laengsten Segments - nicht aus dem
	 * Schwerpunkt der ganzen Strasse, der auf einem Bogen mitten im Garten
	 * liegt. Die Hoehe nimmt die Mittellinie selbst mit; ein Strahl waere nur
	 * eine zweite Meinung, und die Strasse weiss ihre eigene Hoehe.
	 *
	 * @return true, wenn der Sprung stattgefunden hat
	 */
	bool WarpToStreet(const FString& Strasse);

	// Dasselbe als Entwicklerbefehl, damit sich der Sprung ohne Menue belegen
	// laesst (WbWarp "Rheinstrasse"). Die Auswertung steht in WarpToStreet.
	UFUNCTION(Exec)
	void WbWarp(const FString& Strasse = TEXT(""));

	// Richtet den besessenen Pawn auf (Nick/Roll 0) und laesst ihn auf die Raeder fallen.
	UFUNCTION(Exec)
	void WbResetVehicle();

	// Verkehr an (1) oder aus (0).
	UFUNCTION(Exec)
	void WbTraffic(int32 An = 0);

	// Maschinenlesbaren Gesundheitsbericht ausgeben + als JSON nach Saved/Logs.
	// Ohne Argument (bzw. <=0): sofort. Mit <Sekunden> > 0: Gate-Modus - wartet auf
	// den geladenen Zustand (Stadt da + Streaming fertig), maximal <Sekunden>, dann
	// erst der Dump. So spiegelt das JSON den geladenen Zustand statt eines
	// Startup-Transienten (fuer den Rauchtest-Gate nach dem Laden).
	UFUNCTION(Exec)
	void WbHealth(float MaxWaitSeconds = 0.0f);

	// Kameramodus des besessenen Fahrzeugs: 0=Follow, 1=Orbit, 2=Cockpit.
	UFUNCTION(Exec)
	void WbCam(int32 Modus = 0);

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
	void WbNudge(int32 NickGrad = 0, int32 RollGrad = 0);

	// Setzt das Fahndungskonto auf eine Stufe 0..6 (Dev-Hilfe fuer die Polizei).
	//
	// Die Stufe kommt aus dem Argument, wenn eines ankam (-1 = keins), sonst
	// aus der CVar wb.Wanted - ein Exec-Argument ist nicht zuverlaessig
	// (siehe WbSekundenOderVorgabe). 0 loescht das Konto, ab 1 stehen die
	// Streifenwagen sofort bereit.
	UFUNCTION(Exec)
	void WbWanted(int32 Stufe = -1);

	// Startet die Skript-Gierprobe am besessenen Helikopter fuer <Sekunden>.
	UFUNCTION(Exec)
	void WbHeliYaw(int32 Sekunden = 0);

	// Startet das Skript-Flugprofil (Steigen/Marsch/Sinken) fuer <Sekunden>.
	UFUNCTION(Exec)
	void WbHeliFly(int32 Sekunden = 0);

	// Startet das Skript-Fahrprofil (Vollgas + Lenk-Sweep) am besessenen Fahrzeug
	// fuer <Sekunden> - weist Fahrphysik und Lenkung ohne Tastatur nach.
	UFUNCTION(Exec)
	void WbDrive(int32 Sekunden = 0);

	// Autopilot: fliegt den besessenen Helikopter zu einem Punkt <dx dy dz> Meter
	// relativ zur aktuellen Position (Weltachsen) und haelt ihn dort.
	UFUNCTION(Exec)
	void WbHeliGoto(int32 DeltaXMeter = 0, int32 DeltaYMeter = 0, int32 DeltaZMeter = 0);

	// Autopilot: haelt die aktuelle Position/Hoehe (Schweben).
	UFUNCTION(Exec)
	void WbHeliHover();

	// Autopilot AUS: gibt die Steuerung an Tastatur/Gamepad zurueck (mitten im Flug).
	UFUNCTION(Exec)
	void WbHeliOff();

	// Setzt den besessenen Helikopter auf den markierten Helipad des
	// Sebbotower (derselbe Weg wie der Respawn nach einem Absturz) und meldet
	// Lage und Durchmesser der Flaeche ins Log.
	//
	// Am 26.09.2026 stand die Ka-52 zum Start auf einer Wiese - ein Bild, das
	// den markierten Turm-Helipad belegen soll, brauchte diesen Befehl, weil
	// RespawnOnTowerHelipad() keine Konsolenschnittstelle hatte.
	UFUNCTION(Exec)
	void WbHeliTurm();

	// Schaltet die Fahrzeugkamera auf einen festen Modus: 0 = Folge,
	// 1 = Orbit, 2 = Cockpit. Ohne Tastatur und ohne Argument benutzbar -
	// Taste C erreicht das Spiel nicht zuverlaessig (am 26.09.2026 blieb der
	// Modus in einem Lauf auf 0 stehen und das Bild war trotzdem beschriftet
	// "Cockpit"). Wirkt, sobald ein Helikopter besessen ist.
	UFUNCTION(Exec)
	void WbHeliKamera(int32 Modus = -1);

	// Haelt oder loest den Bordabzug des besessenen Helikopters: 1 halten,
	// 0 loslassen. Ohne Argument kommt der Wert aus wb.HeliFeuer.
	//
	// Das Bordgeschoetz haengt an der Maustaste, und die laesst sich von
	// aussen nicht halten (am 26.09.2026 vier Wege gefahren, null Flanken im
	// Log). Ohne diesen Schalter war das Muendungsfeuer nicht als Bild zu
	// belegen. Faengt die echte Kanone - mit Licht, Spur und Schusszaehler.
	UFUNCTION(Exec)
	void WbHeliFeuer(int32 An = -1);

	// Stellt den besessenen Helikopter DistanzMeter vor einen Zielpunkt
	// (Weltkoordinaten in cm, Hoehe ueber dem dortigen Boden) und peilt ihn
	// an. Aufnahmewerkzeug: ohne ihn zeigt die Kanone nur "nach vorn" und ein
	// Schuss auf ein bestimmtes Bauwerk war nicht einstellbar.
	UFUNCTION(Exec)
	void WbHeliZiel(float Xcm, float Ycm, float HoeheUeberBodenCm, float DistanzMeter);

	// Beide Suchscheinwerfer an/aus, ohne Tastatur. Taste L erreicht das
	// Spiel nicht zuverlaessig - dieselbe Eingabeluecke wie beim Abzug.
	UFUNCTION(Exec)
	void WbHeliLicht(int32 An = 1);

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

	// Zu Fuss: Ansicht umschalten (0=Schulter, 1=Ego) - derselbe Pfad wie die
	// C-Taste (ToggleEgoCamera). 2 = umschalten. Skriptbarer Ersatz fuer die
	// Taste, weil Tastatur-Injektion das D3D-Fenster nicht erreicht.
	UFUNCTION(Exec)
	void WbFussAnsicht(int32 Modus = 2);

	// Zu Fuss: Waffe aus der Tabelle waehlen (0-8 = Tasten 1-9). Derselbe Pfad
	// wie SelectWeapon; Log-Marker nennen Anzeige und Masse der gewaehlten
	// Waffe (Pruefung gegen WiesbadenWeapons::Table).
	UFUNCTION(Exec)
	void WbFussWaffe(int32 Index = 0);

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
