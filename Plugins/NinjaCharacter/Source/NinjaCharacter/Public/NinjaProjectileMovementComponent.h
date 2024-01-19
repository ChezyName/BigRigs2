// "Ninja Character" plugin, by Javier 'Xaklse' Osset; Copyright 2020


#pragma once

#include "CoreMinimal.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "NinjaProjectileMovementComponent.generated.h"


/**
 * A ProjectileMovementComponent updates the position of another component each
 * frame. This type allows overriding the gravity direction.
 */
UCLASS(ClassGroup=Movement,Meta=(BlueprintSpawnableComponent))
class NINJACHARACTER_API UNinjaProjectileMovementComponent : public UProjectileMovementComponent
{
	GENERATED_BODY()

public:
	UNinjaProjectileMovementComponent(const FObjectInitializer& ObjectInitializer);

public:
	/**
	 * Determine whether or not to use substepping in the projectile motion update.
	 * If true, GetSimulationTimeStep() will be used to time-slice the update. If false, all remaining time will be used during the tick.
	 * @return Whether or not to use substepping in the projectile motion update.
	 * @see GetSimulationTimeStep()
	 */
	virtual bool ShouldUseSubStepping() const override;

public:
	/** If true, rotate projectile trajectory with gravity direction changes. */
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="NinjaProjectileMovement")
	uint32 bFollowGravityDirection:1;

protected:
	/** Stores last calculated gravity direction if needed. */
	FVector OldGravityDirection;

public:
	/**
	 * Given an initial velocity and a time step, compute a new velocity.
	 * @param  InitialVelocity Initial velocity
	 * @param  DeltaTime Time step of the update
	 * @return Velocity after DeltaTime time step
	 */
	virtual FVector ComputeVelocity(FVector InitialVelocity, float DeltaTime) const override;

protected:
	/** Compute the acceleration that will be applied */
	virtual FVector ComputeAcceleration(const FVector& InVelocity, float DeltaTime) const override;

public:
	/** Compute gravity effect given current physics volume, projectile gravity scale, etc. */
	virtual float GetGravityZ() const override;

	/**
	 * Obtains the current gravity.
	 * @note Could return zero gravity
	 * @return current gravity
	 */
	virtual FVector GetGravity() const;

protected:
	/**
	 * Obtains the current unscaled Z gravity that affects this component.
	 * @return current Z gravity without applying scale factors
	 */
	FORCEINLINE float GetUnscaledGravityZ() const
	{
		return UMovementComponent::GetGravityZ();
	}
};
