// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "WiesbadenVehicleHUD.generated.h"

class AWiesbadenCar;

/**
 * Fahrzeug-HUD: Tacho, Drehzahl, Gang und Kontrollleuchten.
 *
 * Bewusst vollstaendig per Canvas gezeichnet statt als UMG-Widget: das HUD
 * braucht damit KEIN Asset und keine Editor-Handarbeit, funktioniert sofort im
 * gebackenen Spiel und laesst sich in Automatisierungslaeufen mit-screenshotten.
 *
 * Die Rechenteile sind bewusst datenrein und statisch, damit sie ohne Welt
 * getestet werden koennen (Test Vehicles.HUD).
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenVehicleHUD : public AHUD
{
	GENERATED_BODY()

public:
	AWiesbadenVehicleHUD();

	virtual void DrawHUD() override;

	/**
	 * Fuellgrad des Drehzahlbands, 0..1.
	 *
	 * Bezugspunkt ist die Leerlaufdrehzahl, nicht null: ein Motor im Leerlauf
	 * soll einen leeren Balken zeigen, nicht schon ein Achtel.
	 */
	static float ComputeRpmFill(float Rpm, float IdleRpm, float MaxRpm);

	/**
	 * Zeigerwinkel des Tachos in Grad, gemessen von der Nullstellung.
	 * Ueber der Skalenendgeschwindigkeit bleibt der Zeiger am Anschlag.
	 */
	static float ComputeNeedleAngleDegrees(float SpeedKmh, float MaxSpeedKmh, float SweepDegrees);

	/** Gangstufe als Text: "R" rueckwaerts, "N" Leerlauf, sonst die Zahl. */
	static FString FormatGear(int32 Gear);

	/** Kuerzel der Lichtstufe fuer die Kontrollleuchte. */
	static FString FormatHeadlightMode(uint8 Mode);

	/**
	 * Zeilen der Tastenlegende - datenrein, damit sie ohne Welt pruefbar sind.
	 *
	 * Die Belegungen selbst stehen an drei Stellen im Code
	 * (AWiesbadenCar::ReadInput, AWiesbadenFootPawn::ReadInput,
	 * AWiesbadenGameMode::Tick). Diese Liste ist ihre Anzeige und muss mit
	 * ihnen mitwandern; der Test Vehicles.HUD.ControlLegend haelt fest, dass
	 * die tragenden Tasten aufgefuehrt bleiben.
	 */
	static void GetControlLegendLines(bool bInVehicle, TArray<FString>& OutLines);

	/**
	 * Waehlt den Handlungshinweis zu Fuss (datenrein, testbar).
	 *
	 * Negative Entfernung heisst "nichts dieser Art in der Welt".
	 *
	 * @return Der anzuzeigende Text, oder leer wenn nichts in Reichweite ist.
	 */
	static FString BuildFootPrompt(
		double NearestVehicleCm, double NearestFunicularCm,
		double VehicleReachCm, double FunicularReachCm);

protected:
	/** Skalenendwert des Tachos in km/h. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|HUD", meta = (ClampMin = "10"))
	float SpeedoMaxKmh = 140.0f;

	/** Winkelbereich der Tachoskala in Grad. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|HUD", meta = (ClampMin = "30"))
	float SpeedoSweepDegrees = 240.0f;

	/** Wie lange die Tastenlegende nach dem Start von selbst stehen bleibt. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|HUD", meta = (ClampMin = "0.0"))
	float ControlLegendSeconds = 20.0f;

private:
	/** Liefert das aktuell gesteuerte Fahrzeug oder nullptr. */
	AWiesbadenCar* GetPlayerCar() const;

	/**
	 * Tastenlegende links unten.
	 *
	 * Die Belegung war nirgends abzulesen - weder im Spiel noch auf dem
	 * Bildschirm. Wer nicht wusste, dass F ein- und aussteigt oder dass die
	 * Handbremse auf der Leertaste liegt, konnte es nicht herausfinden.
	 * Erscheint automatisch beim Start und laesst sich mit F1 umschalten.
	 */
	void DrawControlLegend(bool bInVehicle, float X, float Y);

	void DrawSpeedometer(const AWiesbadenCar& Car, float CenterX, float CenterY, float Radius);
	void DrawRpmBar(const AWiesbadenCar& Car, float X, float Y, float Width, float Height);
	void DrawTellTales(const AWiesbadenCar& Car, float X, float Y);

	/**
	 * Minikarte unten rechts, gezeichnet aus den Mittellinien des
	 * Strassennetzes.
	 *
	 * Bewusst KEIN SceneCapture: Eine zweite Kameraansicht der Stadt kostet
	 * noch einmal so viel wie das Hauptbild, und das Spiel laeuft bereits mit
	 * 7 Bildern je Sekunde.
	 */
	void DrawMinimap(float CenterX, float CenterY, float Diameter);

	/** Strassenname, auf der sich der Spieler befindet - Balken oben mittig. */
	void DrawStreetName(float CenterX, float Y);

	/** Zeichnet den Handlungshinweis zu Fuss ("F Einsteigen" und dergleichen). */
	void DrawFootPrompt(float CenterX, float Y);

	/** Zeichnet das Pausemenue mittig. */
	void DrawPauseMenu(float Width, float Height);

	/** Wertet die Tasten des Pausemenues aus (Escape, Pfeile, Eingabe). */
	void UpdatePauseMenu();

	/** Fuehrt den gewaehlten Eintrag aus. */
	void ActivatePauseEntry(int32 Index);

public:
	/**
	 * Eintraege des Pausemenues (datenrein, testbar).
	 *
	 * Getrennt vom Zeichnen, damit sich die Reihenfolge und die
	 * Entwicklerbefehle pruefen lassen, ohne einen Bildschirm zu brauchen.
	 */
	static void GetPauseMenuEntries(TArray<FString>& OutEntries);

private:
	/** True, solange das Spiel pausiert ist. */
	bool bPaused = false;

	/** Flankenerkennung fuer Escape und die Menuetasten. */
	bool bPauseKeyHeld = false;
	bool bMenuUpHeld = false;
	bool bMenuDownHeld = false;
	bool bMenuEnterHeld = false;

	/** Ausgewaehlter Eintrag. */
	int32 PauseSelection = 0;

	/** Reichweite, ab der zu Fuss "F Einsteigen" erscheint, in cm. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|HUD", meta = (ClampMin = "100.0"))
	float FootVehicleReachCm = 600.0f;

	/** Reichweite fuer den Mitfahr-Hinweis an der Nerobergbahn, in cm. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|HUD", meta = (ClampMin = "100.0"))
	float FootFunicularReachCm = 1200.0f;

	/**
	 * Strassennetz der Karte; wird einmal gesucht und gemerkt.
	 *
	 * Ein TActorIterator je Bild ueber alle Actors der Stadt waere bei 1.394
	 * Chunk-Actors nicht umsonst.
	 */
	const struct FRoadNetwork* FindRoadNetwork();

	const struct FRoadNetwork* CachedRoadNetwork = nullptr;

	/**
	 * Zuletzt bestimmter Strassenname und wann er bestimmt wurde.
	 *
	 * Die Suche laeuft ueber die Segmente im Umkreis; je Bild waere das
	 * verschwendet, weil sich der Name im Sekundentakt kaum aendert.
	 */
	FString CurrentStreetName;
	float StreetNameAge = 0.0f;

	/** Kreisbogen aus kurzen Linien - Canvas kennt keine Bogenprimitive. */
	void DrawArc(float CenterX, float CenterY, float Radius,
		float StartDegrees, float EndDegrees, const FLinearColor& Color, float Thickness);

	/** Laufzeit seit dem ersten gezeichneten Bild - fuer die Einblenddauer. */
	float ElapsedSeconds = 0.0f;

	/** Umschaltzustand und Halte-Flanke der F1-Taste. */
	bool bShowControlLegend = true;
	bool bLegendKeyHeld = false;
};
