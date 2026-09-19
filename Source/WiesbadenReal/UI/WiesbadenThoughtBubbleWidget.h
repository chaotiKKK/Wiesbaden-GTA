// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "WiesbadenThoughtBubbleWidget.generated.h"

/**
 * Small native world-space bubble. Keeping the drawing here avoids a map-owned
 * widget asset and keeps the scene usable in a freshly cooked map.
 */
UCLASS()
class WIESBADENREAL_API UWiesbadenThoughtBubbleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	static const TCHAR* GetThoughtText();

protected:
	virtual int32 NativePaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;
};
