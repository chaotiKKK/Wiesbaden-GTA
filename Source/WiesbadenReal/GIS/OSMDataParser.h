// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GIS/OSMTypes.h"
#include "OSMDataParser.generated.h"

/** Ergebnis eines Parse-Vorgangs mit Diagnoseinformationen. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FOSMParseResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GIS")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "GIS")
	FString ErrorMessage;

	UPROPERTY(BlueprintReadOnly, Category = "GIS")
	int32 ErrorLineNumber = 0;

	UPROPERTY(BlueprintReadOnly, Category = "GIS")
	int32 NodeCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "GIS")
	int32 WayCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "GIS")
	int32 RelationCount = 0;

	/** Elemente, die wegen fehlender oder ungueltiger Attribute verworfen wurden. */
	UPROPERTY(BlueprintReadOnly, Category = "GIS")
	int32 SkippedElementCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "GIS")
	double ParseDurationSeconds = 0.0;

	FString ToString() const
	{
		if (!bSuccess)
		{
			return FString::Printf(TEXT("FEHLGESCHLAGEN (Zeile %d): %s"), ErrorLineNumber, *ErrorMessage);
		}
		return FString::Printf(
			TEXT("OK: %d Nodes, %d Ways, %d Relations, %d verworfen, %.2f s"),
			NodeCount, WayCount, RelationCount, SkippedElementCount, ParseDurationSeconds);
	}
};

/** Wird nach Abschluss einer Overpass-Abfrage aufgerufen. */
DECLARE_DELEGATE_TwoParams(FOnOverpassQueryComplete, const FOSMParseResult& /*Result*/, TSharedPtr<FOSMDataSet> /*DataSet*/);

/**
 * Parser fuer OpenStreetMap-Daten.
 *
 * Unterstuetzt drei Eingabewege:
 *  1. .osm / .xml  - OSM-XML, per FFastXml streaming geparst
 *  2. .json        - Overpass-API-JSON, per FJsonSerializer
 *  3. Overpass-API - direkte HTTP-Abfrage zur Laufzeit
 *
 * WARUM STREAMING-XML: Ein Wiesbaden-Extrakt mit allen in der Spezifikation
 * geforderten Tags liegt bei 150-250 MB. FXmlFile baut daraus einen
 * vollstaendigen DOM-Baum im Speicher auf (Faktor 5-8 Overhead, also > 1 GB)
 * und ueberschreitet damit das Speicherbudget aus Abschnitt 12 allein beim
 * Import. FFastXml arbeitet callback-basiert ohne Zwischenbaum.
 *
 * ACHTUNG: FFastXml modifiziert den uebergebenen Puffer in-place (es setzt
 * Null-Terminatoren in den String). ParseXmlString kopiert daher intern.
 *
 * THREADING: Parsing ist blockierend und gehoert auf einen Worker-Thread.
 * ParseFileAsync uebernimmt das. Der Overpass-Download laeuft ueber das
 * HTTP-Modul und ist immer asynchron.
 */
UCLASS(BlueprintType)
class WIESBADENREAL_API UOSMDataParser : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Parst eine OSM-Datei. Format wird an der Dateiendung erkannt
	 * (.json/.geojson -> JSON, alles andere -> XML).
	 * Blockierend - fuer den Editor-Import gedacht.
	 */
	FOSMParseResult ParseFile(const FString& FilePath, FOSMDataSet& OutDataSet);

	/** Parst OSM-XML aus einem String. */
	FOSMParseResult ParseXmlString(const FString& XmlContent, FOSMDataSet& OutDataSet);

	/** Parst Overpass-JSON aus einem String. */
	FOSMParseResult ParseJsonString(const FString& JsonContent, FOSMDataSet& OutDataSet);

	/**
	 * Fuehrt eine Overpass-Abfrage aus und parst das Ergebnis.
	 *
	 * @param Bounds     Abfragebereich.
	 * @param OnComplete Wird im Game-Thread aufgerufen.
	 * @param OverpassEndpoint Standard ist die offizielle Instanz. Bei
	 *        Massenabfragen eine eigene Instanz verwenden - die oeffentliche
	 *        API hat ein Rate-Limit und liefert dann HTTP 429.
	 *
	 * Der Download wird NICHT fuer den Produktionsbetrieb empfohlen: fuer das
	 * ausgelieferte Spiel werden die Daten offline mit Tools/overpass_fetch.py
	 * geholt und als Asset gebacken. Diese Funktion dient dem
	 * Entwicklungsworkflow (Datenaktualisierung ohne Editor-Neustart).
	 */
	void FetchFromOverpassAsync(
		const FGeoBounds& Bounds,
		FOnOverpassQueryComplete OnComplete,
		const FString& OverpassEndpoint = TEXT("https://overpass-api.de/api/interpreter"));

	/**
	 * Baut die Overpass-QL-Abfrage fuer den geforderten Datenumfang
	 * (Spezifikation 4.1). Oeffentlich, damit Tools/overpass_fetch.py und die
	 * Laufzeitabfrage garantiert identische Daten holen.
	 */
	static FString BuildOverpassQuery(const FGeoBounds& Bounds, int32 TimeoutSeconds = 600);

	/** Bricht eine laufende Overpass-Abfrage ab. */
	void CancelPendingRequest();

private:
	/** Handle der laufenden HTTP-Anfrage; fuer Cancel und Reentrancy-Schutz. */
	TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> PendingRequest;
};
