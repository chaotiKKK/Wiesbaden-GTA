// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "World/WiesbadenBusLine.h"

class UGeoCoordinateConverter;

/**
 * Der EINE Leser der Liniendateien (Data/Raw/Bus/line<ref>.json und
 * line<ref>_schedule.json).
 *
 * Vorher lagen zwei fast gleiche Leser im Bus-Actor und im Haltestellenmonitor:
 * beide oeffneten dieselbe Datei, parsten dasselbe JSON, rechneten dieselbe
 * Polylinie und dieselben Halte-Bogenlaengen. Zwei Leser heisst zwei Wahrheiten -
 * eine Aenderung an der Datei (oder an der Auswertung) musste an beiden Stellen
 * nachgezogen werden, und ein Fehler in einer der beiden Kopien faellt erst im
 * Spiel auf, wenn die Busse andere Zeiten fahren als der Monitor anzeigt.
 *
 * Hier liegt deshalb beides genau einmal, und zwar GECACHT: Bus-Actor und
 * Haltestellenmonitor fragen mit demselben Dateinamen und bekommen dasselbe
 * Objekt (gleiche Adresse, nur lesbar) - nicht zwei gleiche Kopien. Der Schluessel
 * enthaelt Dateiname, Aenderungszeit und Georeferenz-Origin; eine im Editor
 * geaenderte Datei wird also neu gelesen, eine unveraenderte nicht.
 *
 * Die Daten sind reine Werte (kein UObject): die Struktur haelt nur, was in der
 * Datei steht und was daraus folgt. Der Weltzugriff (Datei lesen) ist auf
 * ReadLine/ReadSchedule beschraenkt; wer rechnen will, nimmt WiesbadenBusLine.
 */
namespace WiesbadenBusLineFile
{
	/** Zielschilder einer Linie: Ordner + die drei Materialnamen. */
	struct FBlindNames
	{
		FString Dir;
		FString Forward;
		FString Backward;
		FString Line;
	};

	/** Inhalt einer Liniendatei, wie sie auf der Platte steht. */
	struct FLineFile
	{
		FString FileName;             // wie angefragt ("line6.json")
		FString Ref;                  // Liniennummer, z. B. "6"
		FString Destination;          // `to`   - Zieltext des Monitors (Hinrichtung)
		FString Origin;               // `from` - Zieltext der GEGENrichtung
		TArray<FVector2D> GeoPath;    // je Eintrag (Breite, Laenge)
		TArray<FVector2D> GeoStops;   // je Eintrag (Breite, Laenge)
		TArray<FString> StopNames;    // `stop_names`, gleiche Reihenfolge wie GeoStops
		/**
		 * `monitor_stops`: Halte mit DFI-Saeule (Namen). Der Eintrag "*" steht fuer
		 * ALLE Halte der Linie (siehe ReadLine) - so bekommt auch eine verlaengerte
		 * Linie an jeder neuen Halte eine Saeule.
		 */
		TArray<FString> MonitorStops;
		FBlindNames Blinds;
		/** Takt in Sekunden; 0 = steht nicht in der Datei (Aufrufer behaelt seinen Wert). */
		double HeadwaySeconds = 0.0;
		/** Wendezeit an den Endpunkten in Sekunden; < 0 = steht nicht in der Datei. */
		double TerminusDwellSeconds = -1.0;
		bool bLoaded = false;         // false: Datei fehlt oder ist kein gueltiges JSON
	};

	/**
	 * Geparste Datei plus die daraus abgeleitete Route in Weltkoordinaten - das,
	 * was Bus-Actor und Haltestellenmonitor gemeinsam benutzen.
	 */
	struct FLineRoute
	{
		FLineFile File;
		TArray<FVector> WorldPath;    // cm, Z = 0 (die Hoehe kommt je Tick vom Boden)
		TArray<double> ArcCm;         // kumulierte 2D-Bogenlaenge je Pfadpunkt, cm
		WiesbadenBusLine::FBusRoute Route;   // Halte-Bogenlaengen + Gesamtlaenge
	};

	/**
	 * Liniendatei lesen (beim ersten Mal) und in Weltkoordinaten legen.
	 * Der zurueckgegebene Wert ist beim zweiten Aufruf DASSELBE Objekt (gleiche
	 * Adresse) und ist NUR ZU LESEN - der Zeiger haelt es am Leben.
	 * @param FileName  Name unter Data/Raw/Bus/, z. B. "line6.json"
	 * @param Converter Konverter mit dem Origin der gebackenen Karte; ohne
	 *                  initialisierten Konverter bleibt die Weltroute leer.
	 */
	WIESBADENREAL_API TSharedRef<const FLineRoute> ReadLine(const FString& FileName,
		const UGeoCoordinateConverter& Converter);

	/**
	 * Fahrplandatei lesen (Ab- und Durchfahrtszeiten). Fehlt die Datei, kommt ein
	 * leerer Fahrplan zurueck - die Aufrufer fallen dann auf den Dauerbetrieb
	 * zurueck. Ebenfalls gecacht und nur zu lesen.
	 */
	WIESBADENREAL_API TSharedRef<const WiesbadenBusLine::FBusSchedule> ReadSchedule(const FString& ScheduleFile);

	/** Cache vergessen (Tests; im Spiel nicht noetig). */
	WIESBADENREAL_API void ClearCache();

	/** Vollstaendiger Pfad einer Liniendatei (fuer Diagnose und Tests). */
	WIESBADENREAL_API FString LineFilePath(const FString& FileName);
}
