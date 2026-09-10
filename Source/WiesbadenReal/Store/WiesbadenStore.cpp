// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "Store/WiesbadenStore.h"

#include "Core/WiesbadenGameStateSubsystem.h" // WiesbadenEconomy::TrySpend

FPurchaseEvaluation FWiesbadenStore::Evaluate(const FStoreItem& Item, int32 Guthaben, bool bAlreadyOwned)
{
	FPurchaseEvaluation Eval;
	Eval.NewGuthaben = Guthaben; // Standard: unveraendert

	if (bAlreadyOwned)
	{
		Eval.Outcome = EPurchaseOutcome::AlreadyOwned;
		return Eval;
	}

	int32 Rest = Guthaben;
	if (!WiesbadenEconomy::TrySpend(Guthaben, Item.Kosten, Rest))
	{
		Eval.Outcome = EPurchaseOutcome::NotEnoughGuthaben;
		return Eval;
	}

	Eval.Outcome = EPurchaseOutcome::Success;
	Eval.NewGuthaben = Rest;
	return Eval;
}

int32 FWiesbadenStore::ApplyLicenseBonus(int32 BaseReward, bool bLicensed)
{
	// +50% abgerundet - reine Ganzzahlarithmetik, damit deterministisch/testbar.
	return bLicensed ? (BaseReward * 3) / 2 : BaseReward;
}

int32 FWiesbadenStore::ComputeFailurePenalty(int32 Reward)
{
	// 20% der entgangenen Praemie, abgerundet - reine Ganzzahlarithmetik. Ohne
	// Praemie (0 oder negativ) faellt keine Strafe an.
	return Reward > 0 ? Reward / 5 : 0;
}

FName FWiesbadenStore::KurierlizenzId()
{
	return FName(TEXT("kurierlizenz"));
}

FName FWiesbadenStore::HelikopterHangarId()
{
	return FName(TEXT("helikopter_hangar"));
}

FName FWiesbadenStore::PremiumStadtplanId()
{
	return FName(TEXT("premium_stadtplan"));
}

bool FWiesbadenStore::MayEnterHelicopter(bool bHasHangarUnlock)
{
	// Standard: Tor nur mit gekauftem Hangar.
	// DEBUG: wenn WbDev_AllowHelicopterWithoutHangar definiert ist, ist das Tor
	// ohne Kauf offen — aber kein Kauf-Hinweis mehr (Entwicklerwahl).
	return bHasHangarUnlock || (true && defined(WbDev_AllowHelicopterWithoutHangar));
}
