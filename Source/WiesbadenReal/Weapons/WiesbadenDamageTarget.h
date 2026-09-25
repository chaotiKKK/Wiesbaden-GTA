// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Weapons/WiesbadenBallistics.h"
#include "WiesbadenDamageTarget.generated.h"

/**
 * Ein Ziel des Shooters (Ziel-Adapter je Zieltyp).
 *
 * Die Waffe kennt keine Ziel-Klassen: Am Aufschlag loest sie das Interface
 * auf und uebergibt Projektil, Trefferpunkt und Flugrichtung. Jeder Zieltyp
 * reagiert selbst (Spieler: Gesundheit, Passant: Zu-Boden, Fahrzeug:
 * Zerstoerung, Beamter: Rueckschlag auf das Fahndungskonto).
 *
 * Typen ohne eigenes Verhalten (Instanz-Pools wie Passanten-Schwarm) bekommen
 * spaeter Adapter an den Pool-Systemen - die Waffe bleibt unveraendert.
 */
UINTERFACE(MinimalAPI)
class UWiesbadenDamageTarget : public UInterface
{
	GENERATED_BODY()
};

class WIESBADENREAL_API IWiesbadenDamageTarget
{
	GENERATED_BODY()

public:
	/**
	 * Projektiltreffer annehmen. Rueckgabe true, wenn das Ziel verwundbar war
	 * (die Waffe kann dann den Einschlag-Effekt setzen).
	 */
	virtual bool ApplyProjectileHit(const FWiesbadenProjectile& Projectile,
		const FVector& HitPoint, const FVector& HitDirection) = 0;

	/**
	 * Explosion annehmen (Granatwerfer): linear abfallender Schaden zum
	 * Zentrum plus voller Eigenschaden des Schuetzen (rechnet die Waffe,
	 * sie ruft dies fuer jeden Zieltyp im Radius auf).
	 */
	virtual bool ApplyExplosion(float Damage, const FVector& BlastCentre,
		float BlastRadiusCm) = 0;

	/** Lebt dieses Ziel noch? (Tote Ziele fangen keine Schuesse mehr.) */
	virtual bool IsAlive() const { return true; }
};
