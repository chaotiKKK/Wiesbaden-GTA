// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "WiesbadenReal.h"

#include "GIS/WiesbadenTrafficSignCatalog.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogWbCore);
DEFINE_LOG_CATEGORY(LogWbGIS);
DEFINE_LOG_CATEGORY(LogWbRoads);
DEFINE_LOG_CATEGORY(LogWbBuildings);
DEFINE_LOG_CATEGORY(LogWbTerrain);
DEFINE_LOG_CATEGORY(LogWbTraffic);
DEFINE_LOG_CATEGORY(LogWbVehicles);
DEFINE_LOG_CATEGORY(LogWbStreaming);

class FWiesbadenRealModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		FDefaultGameModuleImpl::StartupModule();

#if WITH_EDITOR
		// Beim Editor-Start: Katalog-Ids gegen die vorhandenen Schild-Texturen
		// pruefen und fehlende Zeichen warnen (ASSETS.md/AGENTS.md).
		FWiesbadenTrafficSignCatalog::ValidateTexturesAtStartup();
#endif
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FWiesbadenRealModule, WiesbadenReal, "WiesbadenReal");
