// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Strukturierte Log-Kategorien pro Subsystem (Spezifikation 13: Logging).
 * Jede Kategorie ist zur Laufzeit einzeln steuerbar:
 *   Log LogWbGIS Verbose
 */
DECLARE_LOG_CATEGORY_EXTERN(LogWbCore, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogWbGIS, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogWbRoads, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogWbBuildings, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogWbTerrain, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogWbTraffic, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogWbVehicles, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogWbStreaming, Log, All);
