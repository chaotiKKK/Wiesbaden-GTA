// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "Missions/WiesbadenCourierStats.h"

namespace WiesbadenCourierStats
{
bool RecordDelivery(FWbCourierStats& Stats, int32 Tip, bool bFast)
{
	++Stats.Delivered;
	if (bFast)
	{
		++Stats.Fast;
	}
	const int32 Amount = FMath::Max(Tip, 0);
	Stats.TipTotal += Amount;
	if (Amount > Stats.TipRecord)
	{
		Stats.TipRecord = Amount;
		return true;
	}
	return false;
}

void RecordMissed(FWbCourierStats& Stats)
{
	++Stats.Missed;
}

int32 PunctualityPercent(const FWbCourierStats& Stats)
{
	const int32 Total = Stats.Delivered + Stats.Missed;
	if (Total <= 0)
	{
		return -1;
	}
	// Abrunden: 99,6 % sind nicht "100 % puenktlich".
	return FMath::FloorToInt32(100.0 * Stats.Delivered / Total);
}

FString Describe(const FWbCourierStats& Stats)
{
	const int32 Punctual = PunctualityPercent(Stats);
	if (Punctual < 0)
	{
		return TEXT("Kurier-Bilanz: noch keine Lieferung - das ist deine erste!");
	}
	FString Line = FString::Printf(TEXT("Kurier-Bilanz: %d geliefert, %d %% puenktlich"),
		Stats.Delivered, Punctual);
	if (Stats.Fast > 0)
	{
		Line += FString::Printf(TEXT(", %d flott"), Stats.Fast);
	}
	if (Stats.TipRecord > 0)
	{
		Line += FString::Printf(TEXT(" - Trinkgeld-Rekord %d EUR"), Stats.TipRecord);
	}
	return Line + TEXT(".");
}
}
