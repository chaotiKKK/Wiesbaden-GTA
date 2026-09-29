// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Missions/WiesbadenParcours.h"

const TCHAR* WbParcoursAbschnittName(EWbParcoursAbschnitt Abschnitt)
{
	switch (Abschnitt)
	{
	case EWbParcoursAbschnitt::Bereit:      return TEXT("Bereit");
	case EWbParcoursAbschnitt::Slalom:      return TEXT("Slalom");
	case EWbParcoursAbschnitt::Bremsen:     return TEXT("Vollbremsung");
	case EWbParcoursAbschnitt::Wende:       return TEXT("Handbremswende");
	case EWbParcoursAbschnitt::Rueckweg:    return TEXT("Rueckweg");
	case EWbParcoursAbschnitt::Ziel:        return TEXT("Ziel");
	case EWbParcoursAbschnitt::Abgebrochen: return TEXT("Abgebrochen");
	}
	return TEXT("?");
}

// -- Layout -----------------------------------------------------------------

FWbParcoursLayout FWbParcoursLayout::Standard()
{
	FWbParcoursLayout L;
	// Fuenf Kegel im Abstand von 15 m: mit 2 m Querversatz der Linie bleibt 1 m
	// Luft zum Kegel, und bei ~27 km/h reicht die Kurvenhaftung (0,7 g) sicher.
	for (int32 I = 0; I < 5; ++I)
	{
		L.SlalomKegel.Add(FVector2D(2500.0f + 1500.0f * I, 0.0f));
	}
	return L;
}

TArray<FVector2D> FWbParcoursLayout::AlleKegel() const
{
	TArray<FVector2D> K = SlalomKegel;
	// Starttor (auch Ziel), Bremslinie, Stoppbox-Ecken, Wendezonen-Ecken.
	K.Add(FVector2D(0.0f, -StartHalbeBreite));
	K.Add(FVector2D(0.0f, StartHalbeBreite));
	K.Add(FVector2D(BremslinieX, -StartHalbeBreite));
	K.Add(FVector2D(BremslinieX, StartHalbeBreite));
	const float BoxKegelY = BoxHalbeBreite + 50.0f;
	K.Add(FVector2D(BoxVonX, -BoxKegelY));
	K.Add(FVector2D(BoxVonX, BoxKegelY));
	K.Add(FVector2D(BoxBisX, -BoxKegelY));
	K.Add(FVector2D(BoxBisX, BoxKegelY));
	K.Add(FVector2D(WendeVonX, -WendeHalbeBreite));
	K.Add(FVector2D(WendeVonX, WendeHalbeBreite));
	K.Add(FVector2D(WendeBisX, -WendeHalbeBreite));
	K.Add(FVector2D(WendeBisX, WendeHalbeBreite));
	return K;
}

// -- Bewertung --------------------------------------------------------------

FWbParcoursBewertung::FWbParcoursBewertung(const FWbParcoursLayout& InLayout)
	: Layout(InLayout)
	, Kegel(InLayout.AlleKegel())
{
	KegelUmgefahren.Init(false, Kegel.Num());
}

bool FWbParcoursBewertung::IstAktiv() const
{
	return Abschnitt != EWbParcoursAbschnitt::Bereit && !IstFertig();
}

void FWbParcoursBewertung::StrafeDazu(float Sekunden, const FString& Grund)
{
	Strafe += Sekunden;
	NeueMeldungen.Add(FString::Printf(TEXT("%s +%.0f s"), *Grund, Sekunden));
}

void FWbParcoursBewertung::PruefeKegel(const FWbParcoursProbe& P)
{
	// Kegel ins Wagensystem drehen und gegen den Grundriss (plus Kegelradius)
	// pruefen - der Kaefer ist ein Rechteck, kein Kreis: seitlich knapp vorbei
	// ist erlaubt, mit dem Heck ueber den Kegel nicht.
	const float Rad = FMath::DegreesToRadians(P.KursGrad);
	const float C = FMath::Cos(Rad);
	const float S = FMath::Sin(Rad);
	const float HL = Layout.WagenHalbeLaengeCm + Layout.KegelRadiusCm;
	const float HB = Layout.WagenHalbeBreiteCm + Layout.KegelRadiusCm;
	for (int32 I = 0; I < Kegel.Num(); ++I)
	{
		if (KegelUmgefahren[I])
		{
			continue;
		}
		const FVector2D D = Kegel[I] - P.PosCm;
		const float Laengs = D.X * C + D.Y * S;
		const float Quer = -D.X * S + D.Y * C;
		if (FMath::Abs(Laengs) <= HL && FMath::Abs(Quer) <= HB)
		{
			KegelUmgefahren[I] = true;
			NeuUmgefahren.Add(I);
			++KegelGetroffen;
			StrafeDazu(StrafeKegel, FString::Printf(TEXT("Kegel %d umgefahren"), I + 1));
		}
	}
}

void FWbParcoursBewertung::Schritt(const FWbParcoursProbe& P)
{
	if (IstFertig())
	{
		return;
	}
	const FVector2D Pos = P.PosCm;
	if (!bHatVorige)
	{
		Vorige = Pos;
		bHatVorige = true;
		return;
	}

	if (Abschnitt == EWbParcoursAbschnitt::Bereit)
	{
		// Die Uhr laeuft ab der Startlinie - vorwaerts und durchs Starttor.
		if (Vorige.X < 0.0f && Pos.X >= 0.0f && FMath::Abs(Pos.Y) <= Layout.StartHalbeBreite)
		{
			Abschnitt = EWbParcoursAbschnitt::Slalom;
			Fahrzeit = 0.0f;
			NeueMeldungen.Add(TEXT("Start"));
		}
		Vorige = Pos;
		return;
	}

	Fahrzeit += P.DtSekunden;
	PruefeKegel(P);

	switch (Abschnitt)
	{
	case EWbParcoursAbschnitt::Slalom:
	{
		for (int32 I = 0; I < Layout.SlalomKegel.Num(); ++I)
		{
			const FVector2D& K = Layout.SlalomKegel[I];
			if (Vorige.X < K.X && Pos.X >= K.X)
			{
				const bool bLinks = Pos.Y < K.Y;
				const bool bSollLinks = (I % 2) == 0;
				if (bLinks != bSollLinks)
				{
					++TorFehler;
					StrafeDazu(StrafeTor, FString::Printf(TEXT("Slalomkegel %d auf der falschen Seite"), I + 1));
				}
			}
		}
		if (Layout.SlalomKegel.Num() == 0 || Pos.X > Layout.SlalomKegel.Last().X + 500.0f)
		{
			Abschnitt = EWbParcoursAbschnitt::Bremsen;
		}
		break;
	}
	case EWbParcoursAbschnitt::Bremsen:
	{
		if (!bBremslinieUeberfahren && Vorige.X < Layout.BremslinieX && Pos.X >= Layout.BremslinieX)
		{
			bBremslinieUeberfahren = true;
			KmhAnBremslinie = P.Kmh;
			if (P.Kmh < Layout.MindestKmhAnBremslinie)
			{
				bZuLangsam = true;
				StrafeDazu(StrafeZuLangsam, FString::Printf(TEXT("nur %.0f km/h an der Bremslinie"), P.Kmh));
			}
		}
		if (bBremslinieUeberfahren && P.Kmh < 2.0f)
		{
			bAngehalten = true;
			const float PX = static_cast<float>(Pos.X);
			const float PY = static_cast<float>(Pos.Y);
			const float Dx = FMath::Max3(Layout.BoxVonX - PX, 0.0f, PX - Layout.BoxBisX);
			const float Dy = FMath::Max(FMath::Abs(PY) - Layout.BoxHalbeBreite, 0.0f);
			StoppAbweichungM = FMath::Sqrt(Dx * Dx + Dy * Dy) / 100.0f;
			if (StoppAbweichungM > 0.0f)
			{
				StrafeDazu(FMath::Min(StoppAbweichungM * StrafeJeMeterNebenBox, StrafeNichtAngehalten),
					FString::Printf(TEXT("%.1f m neben der Stoppbox"), StoppAbweichungM));
			}
			else
			{
				NeueMeldungen.Add(TEXT("In der Stoppbox"));
			}
			Abschnitt = EWbParcoursAbschnitt::Wende;
		}
		else if (bBremslinieUeberfahren && Pos.X > Layout.BoxBisX + 1500.0f)
		{
			bNichtAngehalten = true;
			StrafeDazu(StrafeNichtAngehalten, TEXT("nicht angehalten"));
			Abschnitt = EWbParcoursAbschnitt::Wende;
		}
		break;
	}
	case EWbParcoursAbschnitt::Wende:
	{
		const bool bInZone = Pos.X >= Layout.WendeVonX && Pos.X <= Layout.WendeBisX
			&& FMath::Abs(Pos.Y) <= Layout.WendeHalbeBreite;
		if (bInZone)
		{
			bWendezoneBetreten = true;
			bHandbremseInWende = bHandbremseInWende || P.bHandbremse;
		}
		// Gewendet = Kurs zeigt zurueck (180 +- 45 Grad).
		if (FMath::Abs(FMath::FindDeltaAngleDegrees(P.KursGrad, 180.0f)) < 45.0f)
		{
			if (!bWendezoneBetreten && !bWendezoneVerfehlt)
			{
				bWendezoneVerfehlt = true;
				StrafeDazu(StrafeWendezone, TEXT("vor der Wendezone gewendet"));
			}
			if (!bHandbremseInWende)
			{
				StrafeDazu(StrafeOhneHandbremse, TEXT("ohne Handbremse gewendet"));
			}
			else
			{
				NeueMeldungen.Add(TEXT("Handbremswende"));
			}
			Abschnitt = EWbParcoursAbschnitt::Rueckweg;
		}
		else if (bWendezoneBetreten && !bInZone && !bWendezoneVerfehlt)
		{
			bWendezoneVerfehlt = true;
			StrafeDazu(StrafeWendezone, TEXT("Wendezone verlassen"));
		}
		break;
	}
	case EWbParcoursAbschnitt::Rueckweg:
	{
		if (Vorige.X >= 0.0f && Pos.X < 0.0f && FMath::Abs(Pos.Y) <= Layout.ZielHalbeBreite)
		{
			Abschnitt = EWbParcoursAbschnitt::Ziel;
			NeueMeldungen.Add(TEXT("Ziel"));
		}
		break;
	}
	default:
		break;
	}

	if (!IstFertig() && Fahrzeit > Layout.ZeitlimitSekunden)
	{
		Abschnitt = EWbParcoursAbschnitt::Abgebrochen;
		NeueMeldungen.Add(TEXT("Zeitlimit - abgebrochen"));
	}
	Vorige = Pos;
}

TArray<int32> FWbParcoursBewertung::HoleNeuUmgefahrene()
{
	TArray<int32> Out = MoveTemp(NeuUmgefahren);
	NeuUmgefahren.Reset();
	return Out;
}

TArray<FString> FWbParcoursBewertung::HoleNeueMeldungen()
{
	TArray<FString> Out = MoveTemp(NeueMeldungen);
	NeueMeldungen.Reset();
	return Out;
}

int32 FWbParcoursBewertung::BerechneSauberkeit(const FWbParcoursErgebnis& E)
{
	float S = 100.0f;
	S -= 8.0f * E.KegelGetroffen;
	S -= 15.0f * E.TorFehler;
	S -= E.bAngehalten ? FMath::Min(6.0f * E.StoppAbweichungM, 30.0f) : 20.0f;
	S -= E.KmhAnBremslinie > 0.0f && E.KmhAnBremslinie < FWbParcoursLayout().MindestKmhAnBremslinie ? 10.0f : 0.0f;
	S -= E.bHandbremseGenutzt ? 0.0f : 10.0f;
	S -= E.bWendezoneVerfehlt ? 10.0f : 0.0f;
	return FMath::Clamp(FMath::RoundToInt(S), 0, 100);
}

FString FWbParcoursBewertung::BerechneMedaille(float GesamtSekunden, int32 Sauberkeit)
{
	if (GesamtSekunden <= GoldSekunden && Sauberkeit >= 90) { return TEXT("Gold"); }
	if (GesamtSekunden <= SilberSekunden) { return TEXT("Silber"); }
	if (GesamtSekunden <= BronzeSekunden) { return TEXT("Bronze"); }
	return TEXT("ohne Medaille");
}

FWbParcoursErgebnis FWbParcoursBewertung::GetErgebnis() const
{
	FWbParcoursErgebnis E;
	E.bImZiel = Abschnitt == EWbParcoursAbschnitt::Ziel;
	E.bAbgebrochen = Abschnitt == EWbParcoursAbschnitt::Abgebrochen;
	E.FahrzeitSekunden = Fahrzeit;
	E.StrafSekunden = Strafe;
	E.GesamtSekunden = Fahrzeit + Strafe;
	E.KegelGetroffen = KegelGetroffen;
	E.TorFehler = TorFehler;
	E.KmhAnBremslinie = KmhAnBremslinie;
	E.bAngehalten = bAngehalten;
	E.StoppAbweichungM = StoppAbweichungM;
	E.bHandbremseGenutzt = bHandbremseInWende;
	E.bWendezoneVerfehlt = bWendezoneVerfehlt;
	E.Sauberkeit = BerechneSauberkeit(E);
	E.Medaille = E.bImZiel ? BerechneMedaille(E.GesamtSekunden, E.Sauberkeit) : FString();
	return E;
}

// -- Fahrer -----------------------------------------------------------------

FWbParcoursFahrer::FWbParcoursFahrer(const FWbParcoursLayout& InLayout)
	: Layout(InLayout)
{
}

float FWbParcoursFahrer::SlalomLinieY(float X) const
{
	// Kosinus durch die Kegel: am Kegel 0 (links) -200 cm, am naechsten +200 cm,
	// eine halbe Teilung davor und danach wieder auf der Mittellinie.
	if (Layout.SlalomKegel.Num() < 2)
	{
		return 0.0f;
	}
	const float X0 = Layout.SlalomKegel[0].X;
	const float Teilung = Layout.SlalomKegel[1].X - X0;
	const float Von = X0 - 0.5f * Teilung;
	const float Bis = Layout.SlalomKegel.Last().X + 0.5f * Teilung;
	if (X <= Von || X >= Bis)
	{
		return 0.0f;
	}
	return -200.0f * FMath::Cos(PI * (X - X0) / Teilung);
}

float FWbParcoursFahrer::LenkungZu(const FWbParcoursProbe& P, const FVector2D& Ziel) const
{
	// Pure Pursuit: Kreisbogen zum Zielpunkt, daraus der Radeinschlag, geteilt
	// durch den bei diesem Tempo nutzbaren Lenkwinkel.
	const FVector2D D = Ziel - P.PosCm;
	const float AbstandM = FMath::Max(D.Size() / 100.0f, 1.0f);
	const float Richtung = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
	const float Alpha = FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(P.KursGrad, Richtung));
	const float Kruemmung = 2.0f * FMath::Sin(Alpha) / AbstandM;
	const float EinschlagGrad = FMath::RadiansToDegrees(FMath::Atan(2.4f * Kruemmung));
	const float Nutzbar = MaxLenkGrad / (1.0f + (P.Kmh / 3.6f) / FMath::Max(LenkAbfallMS, 1.0f));
	return FMath::Clamp(EinschlagGrad / FMath::Max(Nutzbar, 1.0f), -1.0f, 1.0f);
}

void FWbParcoursFahrer::TempoHalten(FWbParcoursSteuerung& S, float IstKmh, float SollKmh)
{
	const float Diff = SollKmh - IstKmh;
	if (Diff >= -1.0f)
	{
		S.Gas = FMath::Clamp(0.25f + 0.08f * Diff, 0.0f, 1.0f);
	}
	else
	{
		S.Bremse = FMath::Clamp(-0.06f * Diff, 0.0f, 1.0f);
	}
}

FWbParcoursSteuerung FWbParcoursFahrer::Steuern(const FWbParcoursProbe& P, EWbParcoursAbschnitt Abschnitt)
{
	FWbParcoursSteuerung S;
	const float X = P.PosCm.X;
	const float Vorausschau = FMath::Clamp(P.Kmh / 3.6f * 80.0f, 450.0f, 1200.0f);

	switch (Abschnitt)
	{
	case EWbParcoursAbschnitt::Bereit:
	case EWbParcoursAbschnitt::Slalom:
	{
		const float Zx = X + Vorausschau;
		float Zy = SlalomLinieY(Zx);
		if (bFehlerMachen && Layout.SlalomKegel.Num() > 1
			&& FMath::Abs(Zx - Layout.SlalomKegel[1].X) < 600.0f)
		{
			Zy = Layout.SlalomKegel[1].Y;   // geradewegs ueber Kegel 2
		}
		S.Lenkung = LenkungZu(P, FVector2D(Zx, Zy));
		TempoHalten(S, P.Kmh, 27.0f);
		break;
	}
	case EWbParcoursAbschnitt::Bremsen:
	{
		S.Lenkung = LenkungZu(P, FVector2D(X + Vorausschau, 0.0f));
		if (X < Layout.BremslinieX)
		{
			// 55 km/h an der Linie: Bremsweg ~16 m -> Halt mitten in der Box
			// (mit Vollgas kam er mit 64 km/h an und stand 10 cm dahinter).
			TempoHalten(S, P.Kmh, bFehlerMachen ? 30.0f : 55.0f);
		}
		else
		{
			S.Bremse = 1.0f;   // Vollbremsung ab der Linie
		}
		break;
	}
	case EWbParcoursAbschnitt::Wende:
	{
		if (!bWendeBegonnen && X >= Layout.WendeVonX + 600.0f)
		{
			bWendeBegonnen = true;
		}
		if (!bWendeBegonnen)
		{
			S.Lenkung = LenkungZu(P, FVector2D(X + Vorausschau, 0.0f));
			TempoHalten(S, P.Kmh, 38.0f);
		}
		else if (FMath::Abs(P.KursGrad) < 120.0f && P.Kmh > 8.0f)
		{
			// Einlenken und Handbremse: das Heck kommt, die Vorderachse lenkt.
			S.Lenkung = -1.0f;
			S.bHandbremse = true;
		}
		else
		{
			// Rest der Drehung mit etwas Gas und vollem Einschlag.
			S.Lenkung = -1.0f;
			S.Gas = 0.4f;
		}
		break;
	}
	case EWbParcoursAbschnitt::Rueckweg:
	{
		S.Lenkung = LenkungZu(P, FVector2D(X - Vorausschau, -1200.0f));
		TempoHalten(S, P.Kmh, 60.0f);
		break;
	}
	default:
		S.Bremse = 1.0f;
		break;
	}
	return S;
}
