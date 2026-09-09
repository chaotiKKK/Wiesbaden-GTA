// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Store/WiesbadenStoreTypes.h"
#include "WiesbadenStoreSubsystem.generated.h"

class IConsoleObject;
class UWiesbadenGameStateSubsystem;
enum class EWiesbadenUiSound : uint8;

DECLARE_MULTICAST_DELEGATE(FOnStoreChanged);

/**
 * Die Ausgabe-Senke (TP2 Stueck 3): laedt den Freischaltungs-Katalog (JSON) und
 * kauft daraus - Guthaben abbuchen (ueber den GameState) und die Freischaltung
 * dauerhaft erteilen. Lebt auf GameInstance-Ebene wie der GameState, damit
 * Freischaltungen Level-Wechsel ueberleben.
 *
 * Die Kauf-Entscheidung liegt datenrein in FWiesbadenStore (unit-getestet);
 * dieses Subsystem macht Katalog-Lookup, Persistenz-Anbindung und die
 * Konsolenbefehle. Kaufen im Spiel: Konsole (~) -> "Wb.Buy <id>",
 * Katalog anzeigen/umschalten -> "Wb.Store".
 */
UCLASS()
class WIESBADENREAL_API UWiesbadenStoreSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Freischaltungs-Katalog (fuer HUD/Anzeige). */
	const TArray<FStoreItem>& GetCatalog() const { return Catalog; }

	/** Katalog-Eintrag zur Id oder nullptr. */
	const FStoreItem* FindItem(FName ItemId) const;

	/**
	 * Kauf-Versuch: Katalog-Lookup + Bewertung (FWiesbadenStore) und - bei Erfolg -
	 * Guthaben abbuchen und Freischaltung erteilen. Idempotent gegen Zweitkauf.
	 */
	EPurchaseOutcome TryPurchase(FName ItemId);

	/** Ist das Store-Panel im HUD offen? (per "Wb.Store" umgeschaltet). */
	bool IsPanelOpen() const { return bPanelOpen; }
	void TogglePanel() { bPanelOpen = !bPanelOpen; OnStoreChanged.Broadcast(); }

	FOnStoreChanged OnStoreChanged;

private:
	void LoadCatalog();
	void RegisterConsoleCommands();
	UWiesbadenGameStateSubsystem* GameState() const;

	/** Kurzes Kauf-Feedback: erzeugt den prozeduralen Ton und spielt ihn 2D. */
	void PlayUiSound(EWiesbadenUiSound Kind) const;

	TArray<FStoreItem> Catalog;
	bool bPanelOpen = false;

	// Registrierte Konsolenbefehle (in Deinitialize wieder abgemeldet).
	TArray<IConsoleObject*> ConsoleCommands;
};
