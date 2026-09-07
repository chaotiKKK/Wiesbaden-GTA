// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "UI/WiesbadenMinimap.h"
#include "WiesbadenVehicleHUD.generated.h"

class AWiesbadenCar;
class IWiesbadenVehicleControl;
class AWiesbadenHelicopter;
class UWiesbadenWorldMapView;

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

	/**
	 * Steuerkurs als Himmelsrichtung + Grad, z. B. "N 000" oder "SW 225".
	 * Datenrein und statisch, damit die Zuordnung ohne Welt pruefbar ist
	 * (Test Vehicles.HUD.Heading).
	 */
	static FString FormatHeading(float Degrees);

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
	/** Liefert das aktuell gesteuerte Fahrzeug ueber die Steuernaht-Familie
	 *  (Interface) oder nullptr - erfasst BEIDE Autos (Kaefer wie ChaosCar). */
	IWiesbadenVehicleControl* GetPlayerVehicleControl() const;

	/** Liefert den aktuell gesteuerten Helikopter oder nullptr. */
	AWiesbadenHelicopter* GetPlayerHelicopter() const;

	/**
	 * Cockpit-Instrumententafel des Helikopters (Hoehe, Fahrt, Variometer,
	 * Kurs, Kollektiv, Rotordrehzahl, Triebwerk). Wird beim Fliegen gezeichnet;
	 * in der Cockpit-Ansicht bildet sie zusammen mit der ausgeblendeten
	 * Aussenhaut die Innensicht.
	 */
	void DrawHeliInstruments(const AWiesbadenHelicopter& Heli, bool bCockpit,
		float Width, float Height);

	/**
	 * Cockpit-Rahmen des Kaefers: dunkles Armaturenbrett-Band unten, damit die
	 * Ich-Perspektive nach Innenraum aussieht (der Wagen selbst ist fuer den
	 * Fahrer ausgeblendet). Nur in der Cockpit-Ansicht.
	 */
	void DrawCarCockpitDash(float Width, float Height);

	/** Rundinstrument mit Zeiger (Hoehenmesser/Variometer-Stil). */
	void DrawRoundGauge(float CenterX, float CenterY, float Radius,
		float Value, float MinValue, float MaxValue, float SweepDegrees,
		const FString& Caption, const FString& Reading);

	/** Gefuelltes Dreieck (fuer perspektivische Flaechen und den Horizont). */
	void DrawFilledTri(const FVector2D& A, const FVector2D& B, const FVector2D& C,
		const FLinearColor& Color);

	/** Gefuelltes konvexes Vieleck als Dreiecksfaecher. */
	void DrawFilledPoly(const TArray<FVector2D>& Points, const FLinearColor& Color);

	/**
	 * Perspektivisches Armaturenbrett: Trapez (oben schmaler als unten) mit
	 * heller Oberkante - laesst die Tafel nach hinten kippen statt flach zu
	 * wirken.
	 */
	void DrawPanelBackdrop(float X, float Y, float W, float H, float TopInset,
		const FLinearColor& Fill, float Alpha);

	/**
	 * Kuenstlicher Horizont (Fluglage) fuer den Helikopter: Himmel/Boden um die
	 * Rollachse gedreht und um die Nicklage verschoben, mit festem
	 * Flugzeugsymbol und Bank-Skala.
	 */
	void DrawAttitudeIndicator(float CenterX, float CenterY, float Radius,
		float PitchDeg, float RollDeg);

	/**
	 * Tastenlegende links unten.
	 *
	 * Die Belegung war nirgends abzulesen - weder im Spiel noch auf dem
	 * Bildschirm. Wer nicht wusste, dass F ein- und aussteigt oder dass die
	 * Handbremse auf der Leertaste liegt, konnte es nicht herausfinden.
	 * Erscheint automatisch beim Start und laesst sich mit F1 umschalten.
	 */
	void DrawControlLegend(bool bInVehicle, float X, float Y);

	// Die volle Instrumententafel laeuft ueber die Steuernaht-Familie, damit sie
	// fuer JEDES Fahrzeug (Kaefer wie ChaosCar) identisch funktioniert.
	void DrawSpeedometer(const IWiesbadenVehicleControl& Vehicle, float CenterX, float CenterY, float Radius);
	void DrawRpmBar(const IWiesbadenVehicleControl& Vehicle, float X, float Y, float Width, float Height);
	void DrawTellTales(const IWiesbadenVehicleControl& Vehicle, float X, float Y);

	/**
	 * Minikarte unten rechts, gezeichnet aus den Mittellinien des
	 * Strassennetzes.
	 *
	 * Bewusst KEIN SceneCapture: Eine zweite Kameraansicht der Stadt kostet
	 * noch einmal so viel wie das Hauptbild, und das Spiel laeuft bereits mit
	 * 7 Bildern je Sekunde.
	 */
	void DrawMinimap(float CenterX, float CenterY, float Diameter);

	/**
	 * Vollbild-Weltkarte des ganzen Strassennetzes (M / Gamepad-Select).
	 *
	 * Wie die Minikarte aus den Mittellinien gezeichnet (kein SceneCapture), aber
	 * das GANZE Netz norden-oben ins Bild eingepasst statt spielerzentriert. Der
	 * Spieler ist ein Richtungspfeil an seiner projizierten Position. Die Linien
	 * werden EINMAL beim Oeffnen gebaut (bzw. bei Groessen-/Netzwechsel) und je
	 * Bild nur nachgezeichnet - das Projizieren von ~125.000 Segmenten je Bild
	 * waere sonst der teuerste Posten des HUD.
	 */
	void DrawWorldMap(float Width, float Height);

	/** Strassennamen der Hauptstrassen live im Bildschirmraum ueber die Weltkarte
	 *  zeichnen (immer scharf, entzerrt, ein Label je Name, kollisionsarm). */
	void DrawWorldMapLabels(const struct FRoadNetwork& Network,
		const struct FWorldMapProjection& Proj, float Width, float Height);

	/** Strassenname, auf der sich der Spieler befindet - Balken oben mittig. */
	void DrawStreetName(float CenterX, float Y);

	/** Kurze Einblendung "Eingestiegen: <Fahrzeug>" / "Ausgestiegen" beim Wechsel
	 *  des besessenen Pawns (F). Selbst-enthalten: erkennt den Wechsel selbst und
	 *  zeichnet im Stil des Strassennamen-Overlays mit Ausblenden. */
	void DrawVehicleBanner(float CenterX, float Y);

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
	 * Zwischengespeicherte Minikarten-Linien.
	 *
	 * FWiesbadenMinimap::BuildLines laeuft in ZWEI Durchgaengen ueber alle
	 * ~125.000 Strassensegmente. Das je Bild zu tun, ist der teuerste Posten
	 * des HUD auf dem Spiel-Thread. Da die Karte fahrzeug-zentriert ist und der
	 * Spieler sich je Bild nur um wenige Zentimeter bewegt, wird sie NUR nach
	 * spuerbarer Bewegung (Ort/Yaw) oder nach kurzer Zeit neu gebaut und sonst
	 * unveraendert weitergezeichnet - der Versatz bleibt bei 250 m Umkreis unter
	 * einem Pixel.
	 */
	TArray<FMinimapLine> CachedMinimapLines;
	FVector CachedMinimapCentre = FVector(FLT_MAX, FLT_MAX, 0.0f);
	double CachedMinimapYaw = 0.0;
	float MinimapCacheAge = 1000.0f;

	/**
	 * Zu-Fuss-Hinweis: naechste Fahrzeug-/Bahn-Entfernung. Der Suchlauf
	 * (GetAllActorsOfClass ueber alle Actors, dreimal) ist teuer und wird nur
	 * ein paar Mal je Sekunde erneuert, nicht je Bild - ein Naeherungshinweis
	 * braucht keine Bild-genaue Entfernung.
	 */
	float FootPromptScanAge = 1000.0f;
	double CachedFootVehicleCm = -1.0;
	double CachedFootFunicularCm = -1.0;

	/**
	 * Zuletzt bestimmter Strassenname und wann er bestimmt wurde.
	 *
	 * Die Suche laeuft ueber die Segmente im Umkreis; je Bild waere das
	 * verschwendet, weil sich der Name im Sekundentakt kaum aendert.
	 */
	FString CurrentStreetName;
	float StreetNameAge = 0.0f;

	/** Fahrzeug-Wechsel-Einblendung: zuletzt besessener Pawn + aktueller Text/Alter. */
	TWeakObjectPtr<class APawn> LastBannerPawn;
	FString VehicleBannerText;
	float VehicleBannerAge = 0.0f;

	/** Kreisbogen aus kurzen Linien - Canvas kennt keine Bogenprimitive. */
	void DrawArc(float CenterX, float CenterY, float Radius,
		float StartDegrees, float EndDegrees, const FLinearColor& Color, float Thickness);

	/** Laufzeit seit dem ersten gezeichneten Bild - fuer die Einblenddauer. */
	float ElapsedSeconds = 0.0f;

	/** Einmal-Latch: der Steuerungs-Legenden-Timer wird erst neu gestartet, wenn
	 *  die Stadt fertig gestreamt ist (sonst verfaellt die Legende waehrend des
	 *  Ladens, bevor der Neuling handeln kann). */
	bool bLegendArmed = false;

	/** Umschaltzustand und Halte-Flanke der F1-Taste. */
	bool bShowControlLegend = true;
	bool bLegendKeyHeld = false;

	// -- Weltkarte (M / Gamepad-Select) --------------------------------------
	/** True, solange die Vollbild-Weltkarte offen ist. */
	bool bWorldMapOpen = false;
	/** Halte-Flanke der Karten-Taste, damit ein Druck einmal umschaltet. */
	bool bMapKeyHeld = false;
	/** Gebaeude-Metadaten der Stadt (einmal gesucht, mit dem Netz gemerkt). */
	const TArray<struct FGeneratedBuilding>* CachedBuildings = nullptr;

	// -- Zoom & Pan der Weltkarte --------------------------------------------
	/** Zoomstufe: 1 = ganzes Netz eingepasst, groesser = naeher heran. */
	float MapZoom = 1.0f;
	/** Blick-Mittelpunkt der Karte in Welt-cm (per Pan verschoben). */
	FVector2D MapCentreWorld = FVector2D::ZeroVector;
	/** Erst wahr, sobald das Zentrum aus der ersten (netz-zentrierten) Projektion
	 *  zurueckgelesen wurde; davor wird nicht geschwenkt. Beim Oeffnen zurueckgesetzt. */
	bool bMapCentreInit = false;

	// -- Wegpunkt ------------------------------------------------------------
	/** Gesetzter Wegpunkt in Welt-cm (Z ist ohne Belang; Richtung/Distanz sind planar). */
	FVector WaypointWorld = FVector::ZeroVector;
	/** True, solange ein Wegpunkt gesetzt ist. */
	bool bWaypointSet = false;
	/** Halte-Flanken der Setz-/Loesch-Taste, damit ein Druck einmal wirkt. */
	bool bWaypointSetKeyHeld = false;
	bool bWaypointClearKeyHeld = false;

public:
	/**
	 * Entfernung fuer die Karten-Anzeige: unter 1 km in Metern ("340 m"), darueber
	 * in Kilometern mit einer Nachkommastelle ("1.2 km"). Datenrein/testbar.
	 */
	static FString FormatMapDistance(double DistanceCm);

private:

	/** Render-Ziel-Ansicht: rendert Strassen+Gebaeude EINMAL ins RenderTarget,
	 *  statt sie je Bild aus ~16.000 Linien neu zu zeichnen. */
	UPROPERTY(Transient)
	TObjectPtr<UWiesbadenWorldMapView> WorldMapView = nullptr;
};
