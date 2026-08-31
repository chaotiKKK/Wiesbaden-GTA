// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "WiesbadenTrafficSignCatalog.generated.h"

/**
 * Kategorie eines amtlichen Verkehrszeichens (StVO/VzKat).
 *
 * Deckt die vier StVO-Klassen sowie Verkehrseinrichtungen ab. Die Zeichen-
 * Grafiken selbst sind amtliche Werke (oefentliches Werk nach Par. 5 UrhG) und
 * werden als Textur/Sprite separat bereitgestellt (siehe ASSETS.md).
 */
UENUM(BlueprintType)
enum class EWiesbadenSignCategory : uint8
{
	Gefahrzeichen       UMETA(DisplayName = "Gefahrzeichen"),
	Vorschriftzeichen   UMETA(DisplayName = "Vorschriftzeichen"),
	Richtzeichen        UMETA(DisplayName = "Richtzeichen"),
	Zusatzzeichen       UMETA(DisplayName = "Zusatzzeichen"),
	Verkehrseinrichtung UMETA(DisplayName = "Verkehrseinrichtung"),
	Unbekannt           UMETA(DisplayName = "Unbekannt")
};

/** Ein amtliches Verkehrszeichen nach StVO/VzKat. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenTrafficSign
{
	GENERATED_BODY()

	/** VzKat-/StVO-Nummer ohne "DE:"-Prefix, z. B. "206" oder "274-50". */
	UPROPERTY(BlueprintReadOnly, Category = "Verkehrszeichen")
	FString Id;

	/** Amtliche Bezeichnung, z. B. "Halt. Vorfahrt gewaehren." */
	UPROPERTY(BlueprintReadOnly, Category = "Verkehrszeichen")
	FString Name;

	/** StVO-Kategorie. */
	UPROPERTY(BlueprintReadOnly, Category = "Verkehrszeichen")
	EWiesbadenSignCategory Category = EWiesbadenSignCategory::Unbekannt;

	/** OSM-Wert (traffic_sign=*), z. B. "DE:206". */
	UPROPERTY(BlueprintReadOnly, Category = "Verkehrszeichen")
	FString OsmValue;

	/**
	 * In OSM verbreitete Kurz-/Alt-Formen dieser Id, z. B. "325" fuer "325.1"
	 * oder "605" fuer "620-40". Kommt aus der Katalog-JSON (Content/Config).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Verkehrszeichen")
	TArray<FString> Aliases;

	/** True, wenn es sich um ein Tempolimit-Zeichen (274/278) handelt. */
	UPROPERTY(BlueprintReadOnly, Category = "Verkehrszeichen")
	bool bSpeedLimit = false;

	/** Tempolimit in km/h (nur bei bSpeedLimit, sonst 0). */
	UPROPERTY(BlueprintReadOnly, Category = "Verkehrszeichen")
	int32 SpeedLimitKmh = 0;

	/**
	 * True, wenn bewusst keine eigene Grafik existiert (z. B. 600
	 * Absperrpfosten - kein amtliches Flach-SVG). Solche Eintraege werden vom
	 * Textur-Validierungs-Check uebersprungen.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Verkehrszeichen")
	bool bNoTexture = false;
};

/**
 * Standard-Platzierungsdaten fuer Verkehrszeichen (VwV-StVO/RMS-Naeherung).
 *
 * Alle Werte in cm; sie steuern, wo ein Zeichen spaeter vom
 * Strassenausstattungs-Pass platziert wird (Hoehe, Seitenabstand, Mindest-
 * durchfahrtshoehe).
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenSignPlacement
{
	GENERATED_BODY()

	/** Hoehe der Unterkante ueber der Fahrbahn/Gehweg. */
	UPROPERTY(EditAnywhere, Category = "Verkehrszeichen", meta = (ClampMin = "0.0"))
	float HeightAboveGroundCm = 220.0f;

	/** Seitlicher Abstand der Zeichenkante vom Fahrbahnrand. */
	UPROPERTY(EditAnywhere, Category = "Verkehrszeichen", meta = (ClampMin = "0.0"))
	float LateralOffsetCm = 50.0f;

	/** Mindest-Durchfahrtshoehe (frei zu haltender Raum). */
	UPROPERTY(EditAnywhere, Category = "Verkehrszeichen", meta = (ClampMin = "0.0"))
	float MinClearanceHeightCm = 200.0f;
};

/**
 * Katalog + Parser fuer deutsche Verkehrszeichen.
 *
 * Enthaelt einen kuratierten Satz haeufiger urbaner Zeichen (StVO/VzKat) und
 * uebersetzt OSM-`traffic_sign=*`-Werte (z. B. "DE:274-50", "DE:206",
 * "DE:205;DE:1000-32") in aufgeloeste FWiesbadenTrafficSign-Eintraege.
 *
 * Die Eintraege liegen in Content/Config/TrafficSignCatalog.json (mit
 * `aliases`-Feld fuer OSM-Kurzformen) und werden einmalig und thread-sicher
 * geladen; fehlt die Datei, gilt ein Minimal-Fallback (nur 274/278).
 *
 * Der Katalog ist zur Laufzeit erweiterbar: ReloadFromJsonString/-File,
 * AddSign und RemoveSign mutieren das geteilte Registry (kritischer Abschnitt);
 * Blueprints erreichen diese Operationen ueber UWiesbadenTrafficSignLibrary.
 *
 * Reines Datenmodul ohne Welt-/Actor-Zugriff; wird vom
 * Strassenausstattungs-Pass konsumiert und ist in Automation-Tests pruefbar.
 */
struct WIESBADENREAL_API FWiesbadenTrafficSignCatalog
{
	/**
	 * Liefert eine Momentaufnahme des Katalogs (thread-sicher, Kopie).
	 * Der Katalog wird beim ersten Zugriff geladen und kann danach durch
	 * Reload/AddSign/RemoveSign geaendert werden.
	 */
	static TArray<FWiesbadenTrafficSign> GetCatalog();

	/** Sucht ein Zeichen ueber seine VzKat-Id ("206", "274-50"). */
	static bool FindById(const FString& Id, FWiesbadenTrafficSign& Out);

	/**
	 * Parst einen OSM-`traffic_sign`-Wert. Mehrere Zeichen koennen durch ";"
	 * oder "," getrennt sein. Unbekannte Nummern werden als Eintrag mit
	 * Kategorie Unbekannt uebernommen (damit nichts verloren geht).
	 */
	static void ParseOsmTag(const FString& OsmTag, TArray<FWiesbadenTrafficSign>& Out);

	/**
	 * Extrahiert das Tempolimit aus einem OSM-`traffic_sign`-Wert
	 * (z. B. "DE:274-50" -> 50). Liefert 0, wenn kein Limit vorhanden ist.
	 */
	static int32 ParseSpeedLimitKmh(const FString& OsmTag);

	/** Standardpfad der Katalog-JSON relativ zum Projekt (Content/Config/...). */
	static FString GetDefaultCatalogPath();

	/**
	 * Laedt den Katalog aus einer JSON-Datei (Format siehe
	 * Content/Config/TrafficSignCatalog.json).
	 * @return false mit OutError bei fehlender Datei oder ungueligem Format.
	 */
	static bool LoadFromJsonFile(const FString& Path, TArray<FWiesbadenTrafficSign>& Out, FString& OutError);

	/** Parst einen Katalog aus einem JSON-String (hermetisch fuer Tests). */
	static bool LoadFromJsonString(const FString& Json, TArray<FWiesbadenTrafficSign>& Out, FString& OutError);

	/**
	 * Ersetzt den geteilten Katalog durch den geparsten JSON-String
	 * (Hot-Reload). Der bisherige Katalog bleibt bei einem Parse-Fehler
	 * unveraendert.
	 */
	static bool ReloadFromJsonString(const FString& Json, FString& OutError);

	/** Wie ReloadFromJsonString, liest zuerst die Datei (Hot-Reload aus Content). */
	static bool ReloadFromJsonFile(const FString& Path, FString& OutError);

	/**
	 * Fuegt einen Eintrag hinzu bzw. ersetzt einen bestehenden mit gleicher
	 * Id (case-insensitive). Leere Ids werden ignoriert.
	 */
	static void AddSign(const FWiesbadenTrafficSign& Sign);

	/** Entfernt den Eintrag mit dieser Id. @return true, wenn etwas entfernt wurde. */
	static bool RemoveSign(const FString& Id);

	/** Setzt den Katalog auf den Inhalt der Standard-JSON (bzw. Fallback) zurueck. */
	static void ResetCatalog();

	/** Standard-Platzierungsdaten (VwV-StVO-Naeherung). */
	static FWiesbadenSignPlacement GetDefaultPlacement();

	/** Standard-Dateisystem-Ordner der Schild-Texturen (Content/Textures/TrafficSigns). */
	static FString GetDefaultTextureFolder();

	/**
	 * Liefert die Ids aller Katalog-Eintraege, die eine eigene Textur-Datei
	 * benoetigen. bSpeedLimit-Basen (274/278 -> nur -x-Serie) und
	 * noTexture-Eintraege (z. B. 600) werden uebersprungen.
	 */
	static TArray<FString> GetTexturedIds(const TArray<FWiesbadenTrafficSign>& Catalog);

	/**
	 * Prueft, ob zu jedem zu texturierenden Katalog-Eintrag die Textur-Datei
	 * (Sign_<Id>.png bzw. nach Import .uasset) im Ordner existiert.
	 * @param OutMissing Wird mit den Ids gefuellt, deren Textur fehlt.
	 * @return Anzahl gepruefter Ids.
	 */
	static int32 ValidateTextures(
		const TArray<FWiesbadenTrafficSign>& Catalog,
		const FString& Folder,
		TArray<FString>& OutMissing);

	/**
	 * Editor-Start-Check: prueft GetCatalog() gegen den Standard-Ordner und
	 * warnt bei fehlenden Schild-Texturen (ein Log-Eintrag je Lauf).
	 */
	static void ValidateTexturesAtStartup();
};
