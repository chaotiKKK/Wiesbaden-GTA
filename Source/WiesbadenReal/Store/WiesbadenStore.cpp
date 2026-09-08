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

FName FWiesbadenStore::KurierlizenzId()
{
	return FName(TEXT("kurierlizenz"));
}
