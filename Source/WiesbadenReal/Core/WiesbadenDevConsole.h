// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "WiesbadenDevConsole.generated.h"

class APawn;

/**
 * Dev-Konsolenbefehle, per -ExecCmds="WbTeleport 2" automatisierbar.
 *
 * Als GameInstanceSubsystem, damit die Exec-Funktionen ohne Blueprint ueber
 * die Konsole erreichbar sind (UE5 leitet ProcessConsoleExec an die Subsysteme
 * der GameInstance weiter). Sie wenden die datenreinen FWiesbadenDevActions auf
 * den ersten lokalen Pawn an - dieselbe Logik wie das Pause-Menue (DRY).
 */
UCLASS()
class WIESBADENREAL_API UWiesbadenDevConsole : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// Teleportiert den Spieler-Pawn: 0=Platter Strasse, 1=Nerobergbahn, 2=Garten.
	UFUNCTION(Exec)
	void WbTeleport(int32 Ziel);

	// Richtet den Spieler-Pawn auf (Nick/Roll 0) und laesst ihn auf die Raeder fallen.
	UFUNCTION(Exec)
	void WbResetVehicle();

	// Verkehr an (1) oder aus (0).
	UFUNCTION(Exec)
	void WbTraffic(int32 An);

private:
	APawn* LocalPawn() const;
};
