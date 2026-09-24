// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "WbAudioAssetsCommandlet.generated.h"

/**
 * Erzeugt die Audio-Assets des Projekts an EINER Stelle:
 *
 *   /Game/Audio/Mix/ATT_Near|Mid|Far   Distanzkurven (Occlusion + LPF)
 *   /Game/Audio/Mix/SBX_Reverb         Hall-Submix
 *   /Game/Audio/Mix/SFXP_Reverb        Hall-Preset (Raumklassen-Parameter)
 *   /Game/Audio/Mix/CON_WbSfx          Concurrency-Defaults
 *   /Game/Audio/Meta/MS_Amb*           Ambience-Betten (MetaSound)
 *   /Game/Audio/Meta/MS_EngineBoxer    Fahrzeug-Layer (MetaSound)
 *
 * Als Commandlet statt Python, weil die Struct-Felder (Attenuation-Settings,
 * Reverb-Settings) hier typgeprueft sind und die MetaSound-Graphen ueber den
 * UMetaSoundSourceBuilder mit Namenspruefung entstehen.
 *
 * Aufruf:
 *   UnrealEditor-Cmd.exe WiesbadenReal.uproject -run=WbAudioAssets
 *       -unattended -nop4 -nosplash
 */
UCLASS()
class UWbAudioAssetsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
