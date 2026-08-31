// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "GIS/WiesbadenTrafficSignCatalog.h"

#include "WiesbadenTrafficSignLibrary.generated.h"

/**
 * Blueprint-Zugriff auf den Verkehrszeichen-Katalog.
 *
 * FWiesbadenTrafficSignCatalog ist ein geteiltes, mutables Registry: Blueprints
 * koennen Zeichen ergaenzen, entfernen und den Katalog zur Laufzeit neu laden
 * (Hot-Reload aus JSON), ohne C++ anzufassen.
 */
UCLASS()
class WIESBADENREAL_API UWiesbadenTrafficSignLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Liefert eine Momentaufnahme aller Katalog-Eintraege. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Verkehrszeichen")
	static TArray<FWiesbadenTrafficSign> GetTrafficSignCatalog();

	/** Sucht einen Eintrag ueber seine VzKat-Id ("206", "325.1"). */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Verkehrszeichen")
	static bool FindTrafficSignById(const FString& Id, FWiesbadenTrafficSign& OutSign);

	/** Fuegt einen Eintrag hinzu bzw. ersetzt einen bestehenden mit gleicher Id. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Verkehrszeichen")
	static void AddTrafficSign(
		const FString& Id,
		const FString& Name,
		EWiesbadenSignCategory Category,
		const TArray<FString>& Aliases);

	/** Entfernt den Eintrag mit dieser Id. @return true, wenn etwas entfernt wurde. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Verkehrszeichen")
	static bool RemoveTrafficSign(const FString& Id);

	/** Hot-Reload: ersetzt den Katalog durch den geparsten JSON-String. */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Verkehrszeichen")
	static bool ReloadTrafficSignCatalogFromJsonString(const FString& Json, FString& OutError);

	/** Hot-Reload aus der Standard-JSON (Content/Config/TrafficSignCatalog.json). */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Verkehrszeichen")
	static bool ReloadTrafficSignCatalog();
};
