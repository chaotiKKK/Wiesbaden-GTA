// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "Missions/WiesbadenMissionLoader.h"

#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

FMissionLoadResult FWiesbadenMissionLoader::ParseMissions(const FString& Json)
{
	FMissionLoadResult Result;

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		Result.Errors.Add(TEXT("Missions-JSON konnte nicht geparst werden."));
		return Result;
	}

	const TArray<TSharedPtr<FJsonValue>>* MissionsArr = nullptr;
	if (!Root->TryGetArrayField(TEXT("missions"), MissionsArr) || !MissionsArr)
	{
		Result.Errors.Add(TEXT("Kein 'missions'-Array im JSON."));
		return Result;
	}

	Result.bParsed = true;

	for (const TSharedPtr<FJsonValue>& MissionVal : *MissionsArr)
	{
		const TSharedPtr<FJsonObject>* MissionObj = nullptr;
		if (!MissionVal.IsValid() || !MissionVal->TryGetObject(MissionObj) || !MissionObj)
		{
			Result.Errors.Add(TEXT("Missions-Eintrag ist kein Objekt - uebersprungen."));
			continue;
		}

		FMission Mission;
		FString IdStr;
		(*MissionObj)->TryGetStringField(TEXT("id"), IdStr);
		Mission.Id = FName(*IdStr);
		(*MissionObj)->TryGetStringField(TEXT("title"), Mission.Title);

		const TSharedPtr<FJsonObject>* RewardObj = nullptr;
		if ((*MissionObj)->TryGetObjectField(TEXT("reward"), RewardObj) && RewardObj)
		{
			double Guthaben = 0.0;
			(*RewardObj)->TryGetNumberField(TEXT("guthaben"), Guthaben);
			Mission.Reward.Guthaben = static_cast<int32>(Guthaben);
		}

		// Optionales Zeitlimit (Sekunden ab Start). Fehlt es, bleibt es 0 = unbefristet.
		(*MissionObj)->TryGetNumberField(TEXT("deadline_seconds"), Mission.DeadlineSeconds);

		const TArray<TSharedPtr<FJsonValue>>* ObjArr = nullptr;
		if ((*MissionObj)->TryGetArrayField(TEXT("objectives"), ObjArr) && ObjArr)
		{
			for (const TSharedPtr<FJsonValue>& ObjVal : *ObjArr)
			{
				const TSharedPtr<FJsonObject>* ObjObj = nullptr;
				if (!ObjVal.IsValid() || !ObjVal->TryGetObject(ObjObj) || !ObjObj)
				{
					Result.Errors.Add(FString::Printf(
						TEXT("Ziel in Mission '%s' ist kein Objekt - uebersprungen."),
						*Mission.Id.ToString()));
					continue;
				}

				FString TypeStr;
				(*ObjObj)->TryGetStringField(TEXT("type"), TypeStr);

				FMissionObjective Objective;
				if (TypeStr == TEXT("reach_location"))
				{
					Objective.Type = EObjectiveType::ReachLocation;
				}
				else
				{
					Result.Errors.Add(FString::Printf(
						TEXT("Unbekannter Ziel-Typ '%s' in Mission '%s' - uebersprungen."),
						*TypeStr, *Mission.Id.ToString()));
					continue;
				}

				(*ObjObj)->TryGetStringField(TEXT("label"), Objective.Label);
				double X = 0.0, Y = 0.0, Radius = 0.0;
				(*ObjObj)->TryGetNumberField(TEXT("x"), X);
				(*ObjObj)->TryGetNumberField(TEXT("y"), Y);
				(*ObjObj)->TryGetNumberField(TEXT("radius_cm"), Radius);
				Objective.Location = FVector(X, Y, 0.0);
				Objective.RadiusCm = Radius;

				Mission.Objectives.Add(Objective);
			}
		}

		Result.Missions.Add(Mission);
	}

	return Result;
}
