// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "UI/WiesbadenThoughtBubbleWidget.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/SlateFontInfo.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

const TCHAR* UWiesbadenThoughtBubbleWidget::GetThoughtText()
{
	return TEXT("Ich denke an Sebbo <3");
}

int32 UWiesbadenThoughtBubbleWidget::NativePaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	static bool bLoggedPaint = false;
	if (!bLoggedPaint)
	{
		bLoggedPaint = true;
		UE_LOG(LogTemp, Log, TEXT("Sylvia thought bubble NativePaint called (%.0fx%.0f)."),
			AllottedGeometry.GetLocalSize().X, AllottedGeometry.GetLocalSize().Y);
	}

	LayerId = Super::NativePaint(
		Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId,
		InWidgetStyle, bParentEnabled);

	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const FVector2D BubblePosition(24.0f, 12.0f);
	const FVector2D BubbleSize(FMath::Max(80.0f, Size.X - 48.0f), FMath::Max(80.0f, Size.Y - 44.0f));
	const FLinearColor BubbleColor(1.0f, 0.96f, 0.82f, 0.96f);

	const FSlateRoundedBoxBrush BubbleBrush(
		BubbleColor,
		22.0f,
		FLinearColor(0.16f, 0.10f, 0.05f, 0.95f),
		2.0f);
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		++LayerId,
		AllottedGeometry.ToPaintGeometry(BubbleSize, FSlateLayoutTransform(BubblePosition)),
		&BubbleBrush,
		ESlateDrawEffect::None,
		BubbleColor);

	// Two small rounded dots make the tail read as a thought bubble without a
	// second widget or a per-frame allocation.
	const FSlateRoundedBoxBrush DotBrush(FLinearColor(1.0f, 0.96f, 0.82f, 0.96f), 10.0f);
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		++LayerId,
		AllottedGeometry.ToPaintGeometry(
			FVector2D(20.0f, 20.0f),
			FSlateLayoutTransform(FVector2D(54.0f, Size.Y - 40.0f))),
		&DotBrush,
		ESlateDrawEffect::None,
		BubbleColor);
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		++LayerId,
		AllottedGeometry.ToPaintGeometry(
			FVector2D(12.0f, 12.0f),
			FSlateLayoutTransform(FVector2D(40.0f, Size.Y - 25.0f))),
		&DotBrush,
		ESlateDrawEffect::None,
		BubbleColor);

	FSlateFontInfo Font = FCoreStyle::Get().GetFontStyle(TEXT("NormalFont"));
	Font.Size = 29;
	FSlateDrawElement::MakeText(
		OutDrawElements,
		++LayerId,
		AllottedGeometry.ToPaintGeometry(
			FVector2D(1.0f, 1.0f),
			FSlateLayoutTransform(FVector2D(56.0f, 61.0f))),
		FString(GetThoughtText()),
		Font,
		ESlateDrawEffect::None,
		FLinearColor(0.10f, 0.06f, 0.03f, 1.0f));

	return LayerId;
}
