// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenSolar.h"

namespace
{
	double Wrap360(double Deg)
	{
		Deg = FMath::Fmod(Deg, 360.0);
		return Deg < 0.0 ? Deg + 360.0 : Deg;
	}
}

WiesbadenSolar::FSunPosition WiesbadenSolar::ComputeSunPosition(const FDateTime& Utc,
	double LatitudeDeg, double LongitudeDeg)
{
	// NOAA / Meeus-Naeherung: Tage seit J2000.0.
	const double N = Utc.GetJulianDay() - 2451545.0;

	const double MeanLongitude = Wrap360(280.460 + 0.9856474 * N);                        // L
	const double MeanAnomaly = FMath::DegreesToRadians(Wrap360(357.528 + 0.9856003 * N)); // g
	const double EclipticLongitude = FMath::DegreesToRadians(
		MeanLongitude + 1.915 * FMath::Sin(MeanAnomaly) + 0.020 * FMath::Sin(2.0 * MeanAnomaly));
	const double Obliquity = FMath::DegreesToRadians(23.439 - 0.0000004 * N);

	// Rektaszension + Deklination der Sonne.
	const double RightAscension = FMath::Atan2(
		FMath::Cos(Obliquity) * FMath::Sin(EclipticLongitude), FMath::Cos(EclipticLongitude));
	const double Declination = FMath::Asin(FMath::Sin(Obliquity) * FMath::Sin(EclipticLongitude));

	// Sternzeit Greenwich -> lokale Sternzeit -> Stundenwinkel (-180..180).
	double GmstHours = FMath::Fmod(18.697374558 + 24.06570982441908 * N, 24.0);
	if (GmstHours < 0.0) { GmstHours += 24.0; }
	const double LocalSiderealDeg = Wrap360(GmstHours * 15.0 + LongitudeDeg);
	double HourAngleDeg = Wrap360(LocalSiderealDeg - FMath::RadiansToDegrees(RightAscension));
	if (HourAngleDeg > 180.0) { HourAngleDeg -= 360.0; }
	const double H = FMath::DegreesToRadians(HourAngleDeg);
	const double Phi = FMath::DegreesToRadians(LatitudeDeg);

	const double SinElevation = FMath::Sin(Phi) * FMath::Sin(Declination)
		+ FMath::Cos(Phi) * FMath::Cos(Declination) * FMath::Cos(H);

	// Azimut ab Nord im Uhrzeigersinn: vormittags Ost, mittags Sued, abends West.
	const double Azimuth = FMath::Atan2(-FMath::Sin(H),
		FMath::Cos(Phi) * FMath::Tan(Declination) - FMath::Sin(Phi) * FMath::Cos(H));

	FSunPosition Out;
	Out.ElevationDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(SinElevation, -1.0, 1.0)));
	Out.AzimuthDeg = Wrap360(FMath::RadiansToDegrees(Azimuth));
	return Out;
}

float WiesbadenSolar::LocalHours(const FDateTime& Local)
{
	return static_cast<float>(Local.GetHour())
		+ static_cast<float>(Local.GetMinute()) / 60.0f
		+ static_cast<float>(Local.GetSecond()) / 3600.0f;
}

FRotator WiesbadenSolar::SunLightRotation(double ElevationDeg, double AzimuthDeg)
{
	const double El = FMath::DegreesToRadians(ElevationDeg);
	const double Az = FMath::DegreesToRadians(AzimuthDeg);
	// Richtung ZUR Sonne: Ost-Anteil sin(Az) auf +X, Nord-Anteil cos(Az) auf -Y.
	const FVector ToSun(
		FMath::Sin(Az) * FMath::Cos(El),
		-FMath::Cos(Az) * FMath::Cos(El),
		FMath::Sin(El));
	// Das Licht faellt von der Sonne zur Stadt.
	return (-ToSun).Rotation();
}
