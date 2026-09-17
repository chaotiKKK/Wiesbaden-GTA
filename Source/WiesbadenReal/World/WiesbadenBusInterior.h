// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Innenraum des Fahrgast-Busses - datenrein, ohne Welt.
 *
 * ANLASS: Der Bus ist ein reines AUSSEN-Modell (Tripo/OBJ, convert_bus.py). Als
 * Fahrgast sass die Kamera damit in einer geschlossenen Huelle: man sah vom Bus
 * nichts, sondern schwebte ueber der Fahrbahn - der Eindruck, mit dem Bus in die
 * Luft versetzt zu werden. Die uebrigen Fahrzeuge loesen dasselbe Problem, indem
 * sie in der Cockpit-Ansicht ihre eigene Huelle fuer den Fahrer ausblenden
 * (AddCockpitHiddenMesh) und die Instrumente das HUD liefert; ein Bus braucht
 * dagegen SICHTBARE Waende, sonst bleibt das Gefuehl des Schwebens.
 *
 * Deshalb baut der Bus-Route-Actor aus den hier beschriebenen Kaesten ein
 * Innenraum-Mesh. Die Koordinaten sind WAGENKOORDINATEN relativ zum Bus-Ursprung,
 * also demselben Bezug wie der Mitfahr-Anker: X = Fahrtrichtung vorne, Y = rechts
 * (Fahrerseite), Z = oben, Ursprung auf Hoehe der Radaufstandsflaeche
 * (Mesh-Box Min.Z = 0).
 *
 * OBERFLAECHEN: Zuerst waren alle Flaechen nur vertexgefaerbt - reine Farbkacheln
 * ohne Textur ("im Bus sind keine Texturen"). Jetzt traegt jeder Kasten eine
 * FLaeCHENART (ETile); je Flaechenart gibt es eine kachelnde Textur
 * (Tools/make_bus_interior_textures.py -> Content/Vehicles/Bus/Interior), die im
 * Material mit der Vertexfarbe multipliziert wird: die Textur liefert die
 * Oberflaeche (Koernung, Fugen, Lochung, Stoffbindung), die Vertexfarbe bleibt
 * die Farbe. Deshalb bleibt dieses Modul datenrein - es nennt nur Namen und
 * FlaeCHENART, geladen wird im Actor.
 *
 * Die Fenster sind ABSICHTLICH offen (keine Scheiben): der Fahrgast soll
 * hinaussehen. Der Sichtbereich ist der Streifen zwischen Fensterbank und
 * Dachhimmel bzw. oberhalb der Armaturentafel nach vorn.
 */
namespace WiesbadenBusInterior
{
	/** Mastab der Kabine in cm. */
	struct FSpec
	{
		/** Wagenlaenge (aus WiesbadenBusRoute::BusLengthCm). */
		double LengthCm = 827.0;
		/** Halbe Wagenbreite (aus WiesbadenBusRoute::BusHalfWidthCm). */
		double HalfWidthCm = 127.5;
		/** Hoehe der Aussenhaut ueber dem Ursprung (Mesh-Oberkante). */
		double HeightCm = 225.0;

		/** Stehflaeche (Fusboden-Oberkante) ueber dem Ursprung. */
		double FloorTopCm = 34.0;
		/** Fensterbank: Oberkante der Seitenwand unter dem Fensterband. */
		double SillCm = 110.0;
		/** Dachhimmel-Unterkante. */
		double CeilingCm = 207.0;
		/** Oberkante der Armaturentafel an der Frontscheibe. */
		double DashTopCm = 105.0;

		/** Abstand der Sitzreihen (Rueckenlehne zu Rueckenlehne). */
		double SeatPitchCm = 170.0;
		/**
		 * Abstand der ersten Sitzreihe (der Fahrgastreihe) von der Wagenspitze.
		 *
		 * 260 cm: die Vorderachse eines Stadtbusses liegt rund 2,7 m hinter der
		 * Spitze, die erste Fahrgastreihe also kurz dahinter. Mit 210 cm sass der
		 * Fahrgast fast auf der Armaturentafel - das Bild war zur Haelfte Tafel.
		 */
		double FirstRowFromNoseCm = 260.0;
	};

	/**
	 * Flaechenart eines Kastens - bestimmt die Innenraum-Textur und damit den
	 * Mesh-Abschnitt (ein Abschnitt je Art, je Abschnitt ein Material).
	 *
	 * Die Reihenfolge ist die Abschnittsnummer im Mesh: Wer hier umsortiert,
	 * verschiebt die Materialien (Tools/import_bus_interior.py legt sie in
	 * derselben Reihenfolge an).
	 */
	enum class ETile : uint8
	{
		Boden = 0,   /**< Fussboden: Linoleum mit Fugenraster. */
		Sitz,        /**< Sitzflaeche und Rueckenlehne: Stoff. */
		Wand,        /**< Seitenwaende und Heckwand: beplankt. */
		Decke,       /**< Dachhimmel, Lichtbaender, Fensterpfeiler. */
		Technik,     /**< Haltestangen, Fensterbank, Armaturenbrett, Fahrerplatz. */
		Count
	};

	/** Anzahl der Flaechenarten (= Mesh-Abschnitte = Materialien). */
	constexpr int32 TileCount = (int32)ETile::Count;

	/** Ablageort der Texturen/Materialien im Content-Browser. */
	extern const TCHAR* const AssetDir;

	/**
	 * Texturdeckung: eine Kachel deckt 200 cm x 200 cm des Innenraums ab (die UVs
	 * werden daraus gerechnet, die Texturen kacheln). 512 px / 200 cm = 2,6 px/cm.
	 */
	double TexCmPerTile();

	/** Texturname (ohne Pfad), z. B. "T_WbBusIntBoden". */
	const TCHAR* TileTextureName(ETile Tile);

	/** Materialname (ohne Pfad) - Material wie Textur, damit eine Art einen Namen hat. */
	const TCHAR* TileMaterialName(ETile Tile);

	/** Ein achsenparalleler Kasten im Wagenkoordinatensystem. */
	struct FPart
	{
		FVector Min = FVector::ZeroVector;
		FVector Max = FVector::ZeroVector;
		FLinearColor Colour = FLinearColor::White;
		ETile Tile = ETile::Technik;
	};

	/**
	 * Standflaeche im Innenraum (Fusboden) - Bezug fuer den abgesetzten Fussgaenger.
	 * Wird die Figur hierauf gestellt, steht sie im Wagen und nicht darueber.
	 */
	double CabinFloorTopCm(const FSpec& Spec);

	/**
	 * Augenpunkt des Fahrgasts: Fensterplatz vorne rechts, Sitzend.
	 *
	 * Sitzend, weil der Blick sonst durch die Decke ginge: Der Wagen ist nur
	 * 2,25 m hoch, die Steh-Augenhoehe laege bei knapp 2 m und damit knapp unter
	 * dem Himmel - man saehe nur geradeaus ins Dach.
	 */
	FVector PassengerEyeCm(const FSpec& Spec);

	/** Sitzposition des Fahrgasts (Mitte der Sitzflaeche, gleiche X/Y wie die Augen). */
	FVector PassengerSeatCm(const FSpec& Spec);

	/** Die Kaesten des Innenraums in Wagenkoordinaten. */
	void BuildBoxes(const FSpec& Spec, TArray<FPart>& OutBoxes);

	/** Liegt der Punkt innerhalb der Aussenhaut (mit Toleranz)? */
	bool IsInsideBody(const FSpec& Spec, const FVector& PointCm, double ToleranceCm = 0.0);

	/**
	 * Haengt einen Kasten als 6 Flaechen (12 Dreiecke) an die Mesh-Arrays.
	 * Getrennt von BuildBoxes, damit der Kastenbau ohne Welt pruefbar bleibt.
	 *
	 * Die UVs sind echte Flaechenkoordinaten in KACHELWEITE (TexCmPerTile):
	 * eine Seite eines Kastens bekommt u = Achsenkoordinate / 200 cm, also
	 * fortlaufend ueber den ganzen Innenraum - benachbarte Kaesten derselben
	 * Wand liegen damit im selben Texturraster und stossen nicht versetzt an.
	 */
	void AppendBox(TArray<FVector>& Vertices, TArray<int32>& Triangles, TArray<FVector>& Normals,
		TArray<FVector2D>& UV0, TArray<FLinearColor>& Colours, const FPart& Box);

	/**
	 * Mesh EINER Flaechenart (ein Material-Abschnitt) in Wagenkoordinaten.
	 * Der Actor ruft das je ETile einmal auf und legt das Ergebnis in den
	 * Mesh-Abschnitt (int32)ETile.
	 */
	void BuildSection(const FSpec& Spec, ETile Tile, TArray<FVector>& Vertices, TArray<int32>& Triangles,
		TArray<FVector>& Normals, TArray<FVector2D>& UV0, TArray<FLinearColor>& Colours);

	/**
	 * Fertigmaterial fuer eine Flaechenart: Textur x Vertexfarbe. Der Actor
	 * uebernimmt es; dieses Modul kennt nur den Namen (keine Assets).
	 */
	FString TileMaterialPath(ETile Tile);
}
