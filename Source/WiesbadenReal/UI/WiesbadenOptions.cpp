// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "UI/WiesbadenOptions.h"

namespace
{
	/** Die fuenf Stufen der Engine-Skalierbarkeit, in ihrer Reihenfolge. */
	const TCHAR* QualitaetsName(int32 Stufe)
	{
		switch (FMath::Clamp(Stufe, 0, 4))
		{
		case 0:  return TEXT("Sehr niedrig");
		case 1:  return TEXT("Niedrig");
		case 2:  return TEXT("Mittel");
		case 3:  return TEXT("Hoch");
		default: return TEXT("Episch");
		}
	}

	/**
	 * Die angebotenen Bildratengrenzen.
	 *
	 * Eine feste Schrittweite waere hier falsch: zwischen 30 und 60 liegt ein
	 * spuerbarer Unterschied, zwischen 141 und 144 keiner. Darum eine Leiter
	 * aus Werten, die jemand wirklich waehlt.
	 */
	const double Bildraten[] = { 0.0, 30.0, 60.0, 90.0, 120.0, 144.0 };
	constexpr int32 BildratenZahl = UE_ARRAY_COUNT(Bildraten);

	int32 NaechsteBildratenStufe(double Value)
	{
		int32 Best = 0;
		double BestAbstand = TNumericLimits<double>::Max();
		for (int32 i = 0; i < BildratenZahl; ++i)
		{
			const double Abstand = FMath::Abs(Bildraten[i] - Value);
			if (Abstand < BestAbstand)
			{
				BestAbstand = Abstand;
				Best = i;
			}
		}
		return Best;
	}
}

FString WiesbadenOptions::GroupLabel(EWbOptionGroup Group)
{
	switch (Group)
	{
	case EWbOptionGroup::Grafik:    return TEXT("GRAFIK");
	case EWbOptionGroup::Ton:       return TEXT("TON");
	case EWbOptionGroup::Steuerung: return TEXT("STEUERUNG");
	case EWbOptionGroup::Spielwelt: return TEXT("SPIELWELT");
	default:                        return FString();
	}
}

FWbOptionRange WiesbadenOptions::RangeOf(EWbOptionKind Kind)
{
	FWbOptionRange R;
	switch (Kind)
	{
	case EWbOptionKind::Qualitaet:   R.Min = 0.0;   R.Max = 4.0;   R.Step = 1.0;   break;
	case EWbOptionKind::Bildrate:    R.Min = 0.0;   R.Max = 144.0; R.Step = 1.0;   break;
	case EWbOptionKind::Lautstaerke: R.Min = 0.0;   R.Max = 1.0;   R.Step = 0.05;  break;
	case EWbOptionKind::Faktor:      R.Min = 0.25;  R.Max = 3.0;   R.Step = 0.25;  break;
	case EWbOptionKind::Schalter:    R.Min = 0.0;   R.Max = 1.0;   R.Step = 1.0;   break;
	case EWbOptionKind::Anteil:      R.Min = 0.0;   R.Max = 1.0;   R.Step = 0.1;   break;
	case EWbOptionKind::Tageszeit:   R.Min = -1.0;  R.Max = 23.0;  R.Step = 1.0;   break;
	default: break;
	}
	return R;
}

void WiesbadenOptions::BuildRows(int32 AudioBusCount,
	const TArray<FString>& BusLabels, TArray<FWbOptionRow>& OutRows)
{
	OutRows.Reset();

	auto Add = [&OutRows](EWbOptionGroup Group, EWbOptionKind Kind,
		const TCHAR* Label, const TCHAR* Hinweis, int32 BusIndex = -1)
	{
		FWbOptionRow Row;
		Row.Group = Group;
		Row.Kind = Kind;
		Row.Label = Label;
		Row.Hinweis = Hinweis;
		Row.BusIndex = BusIndex;
		OutRows.Add(MoveTemp(Row));
	};

	// --- Grafik -------------------------------------------------------------
	// Alle vier Stufen sitzen auf den Skalierbarkeits-Gruppen der Engine; sie
	// greifen sofort und speichern sich in die GameUserSettings.
	Add(EWbOptionGroup::Grafik, EWbOptionKind::Qualitaet, TEXT("Sichtweite"),
		TEXT("Wie weit Gebaeude, Baeume und Strassenmoebel gezeichnet werden."));
	Add(EWbOptionGroup::Grafik, EWbOptionKind::Qualitaet, TEXT("Schatten"),
		TEXT("Aufloesung und Reichweite der Schlagschatten. Auf der untersten Stufe fallen sie ganz weg."));
	Add(EWbOptionGroup::Grafik, EWbOptionKind::Qualitaet, TEXT("Effekte"),
		TEXT("Partikel, Spiegelungen und Nachbearbeitung - dazu gehoert der Niederschlag."));
	Add(EWbOptionGroup::Grafik, EWbOptionKind::Qualitaet, TEXT("Texturen"),
		TEXT("Aufloesung der Oberflaechen. Kostet vor allem Grafikspeicher."));
	Add(EWbOptionGroup::Grafik, EWbOptionKind::Bildrate, TEXT("Bildratengrenze"),
		TEXT("Obergrenze der Bilder je Sekunde. Ohne Grenze laeuft die Karte so schnell sie kann."));

	// --- Ton ----------------------------------------------------------------
	// Eine Zeile je Bus des Mischpults - und KEINE, wenn es kein Mischpult gibt.
	for (int32 Bus = 0; Bus < AudioBusCount; ++Bus)
	{
		const FString Name = BusLabels.IsValidIndex(Bus)
			? BusLabels[Bus]
			: FString::Printf(TEXT("Bus %d"), Bus + 1);

		FWbOptionRow Row;
		Row.Group = EWbOptionGroup::Ton;
		Row.Kind = EWbOptionKind::Lautstaerke;
		Row.Label = Name;
		Row.Hinweis = (Bus == 0)
			? TEXT("Gesamtlautstaerke - liegt ueber allen anderen Reglern.")
			: TEXT("Lautstaerke dieser Gruppe, unabhaengig von den uebrigen.");
		Row.BusIndex = Bus;
		OutRows.Add(MoveTemp(Row));
	}

	// --- Steuerung ----------------------------------------------------------
	Add(EWbOptionGroup::Steuerung, EWbOptionKind::Faktor, TEXT("Maus-Empfindlichkeit"),
		TEXT("Wie schnell die Kamera der Maus folgt - zu Fuss und im Fahrzeug."));
	Add(EWbOptionGroup::Steuerung, EWbOptionKind::Schalter, TEXT("Steuerungshilfe"),
		TEXT("Die Tastenbelegung dauerhaft unten links einblenden."));

	// --- Spielwelt ----------------------------------------------------------
	Add(EWbOptionGroup::Spielwelt, EWbOptionKind::Anteil, TEXT("Verkehrsdichte"),
		TEXT("Wie viele Fahrzeuge je Spurkilometer fahren. Auf 0 bleiben die Strassen leer."));
	// KEINE ZEILE FUER DIE FUSSGAENGERDICHTE. Sie waere die naheliegende
	// Nachbarin der Verkehrsdichte - aber FWiesbadenPedestrianSimulation haelt
	// ihre Settings PRIVAT und bietet nur GetDensity() an, keinen Setter. Ein
	// Regler dafuer liesse sich zeichnen und wuerde nichts bewegen. Erst wenn
	// die Fussgaenger-Sim einen Setter bekommt, gehoert die Zeile hierher.
	Add(EWbOptionGroup::Spielwelt, EWbOptionKind::Tageszeit, TEXT("Tageszeit"),
		TEXT("Der Uhr des Rechners folgen oder eine Stunde festhalten. Die Sonne steht dann echt fuer diese Stunde."));
}

FString WiesbadenOptions::FormatValue(EWbOptionKind Kind, double Value)
{
	switch (Kind)
	{
	case EWbOptionKind::Qualitaet:
		return QualitaetsName(FMath::RoundToInt(Value));

	case EWbOptionKind::Bildrate:
		return (Value < 1.0)
			? FString(TEXT("ohne Grenze"))
			: FString::Printf(TEXT("%.0f /s"), Value);

	case EWbOptionKind::Lautstaerke:
	case EWbOptionKind::Anteil:
		return FString::Printf(TEXT("%.0f %%"),
			FMath::Clamp(Value, 0.0, 1.0) * 100.0);

	case EWbOptionKind::Faktor:
		return FString::Printf(TEXT("%.2fx"), Value);

	case EWbOptionKind::Schalter:
		return (Value >= 0.5) ? FString(TEXT("an")) : FString(TEXT("aus"));

	case EWbOptionKind::Tageszeit:
		return (Value < 0.0)
			? FString(TEXT("Systemzeit"))
			: FString::Printf(TEXT("%02.0f:00 Uhr"), Value);

	default:
		return FString();
	}
}

double WiesbadenOptions::Step(EWbOptionKind Kind, double Value, int32 Direction)
{
	if (Direction == 0)
	{
		return Value;
	}
	const int32 Richtung = (Direction > 0) ? 1 : -1;
	const FWbOptionRange R = RangeOf(Kind);

	// Die Bildrate laeuft ueber eine Leiter, nicht ueber eine Schrittweite.
	if (Kind == EWbOptionKind::Bildrate)
	{
		const int32 Stufe = FMath::Clamp(
			NaechsteBildratenStufe(Value) + Richtung, 0, BildratenZahl - 1);
		return Bildraten[Stufe];
	}

	// Die Tageszeit laeuft um: hinter 23 Uhr kommt wieder "Systemzeit". Sonst
	// muesste man 24 Mal nach links, um die Uhr des Rechners zurueckzubekommen.
	if (Kind == EWbOptionKind::Tageszeit)
	{
		const int32 Stufen = FMath::RoundToInt(R.Max - R.Min) + 1;   // -1..23 = 25
		int32 Index = FMath::RoundToInt(Value - R.Min) + Richtung;
		Index = (Index % Stufen + Stufen) % Stufen;
		return R.Min + Index;
	}

	return FMath::Clamp(Value + R.Step * Richtung, R.Min, R.Max);
}

double WiesbadenOptions::BarFraction(EWbOptionKind Kind, double Value)
{
	// Schalter und Tageszeit haben keinen Balken - "an" ist kein Fuellstand.
	if (Kind == EWbOptionKind::Schalter || Kind == EWbOptionKind::Tageszeit)
	{
		return -1.0;
	}

	if (Kind == EWbOptionKind::Bildrate)
	{
		const int32 Stufe = NaechsteBildratenStufe(Value);
		return static_cast<double>(Stufe) / static_cast<double>(BildratenZahl - 1);
	}

	const FWbOptionRange R = RangeOf(Kind);
	const double Spanne = R.Max - R.Min;
	if (Spanne <= 0.0)
	{
		return -1.0;
	}
	return FMath::Clamp((Value - R.Min) / Spanne, 0.0, 1.0);
}

int32 WiesbadenOptions::ClampRow(int32 Current, int32 Count)
{
	return (Count <= 0) ? 0 : FMath::Clamp(Current, 0, Count - 1);
}

bool WiesbadenOptions::EdgePressed(bool bIsDown, bool& bHeld)
{
	const bool bPressed = bIsDown && !bHeld;
	bHeld = bIsDown;
	return bPressed;
}

bool WiesbadenOptions::ValueArrived(double Written, double ReadBack)
{
	// Relativ, damit die Toleranz mit der Groesse mitwaechst - eine
	// Bildratengrenze von 144 rundet anders als eine Lautstaerke von 0,6.
	constexpr double RelativeToleranz = 1e-6;
	const double Toleranz = FMath::Max(
		RelativeToleranz, FMath::Abs(Written) * RelativeToleranz);
	return FMath::Abs(Written - ReadBack) <= Toleranz;
}

int32 WiesbadenOptions::NextRow(int32 Current, int32 Count, int32 Direction)
{
	if (Count <= 0)
	{
		return 0;
	}
	const int32 Richtung = (Direction >= 0) ? 1 : -1;
	return ((Current + Richtung) % Count + Count) % Count;
}
