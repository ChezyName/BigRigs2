// "Ninja Character" plugin, by Javier 'Xaklse' Osset; Copyright 2020


#include "NinjaProjectileMovementComponent.h"

#include "NinjaMath.h"
#include "NinjaPhysicsVolume.h"


UNinjaProjectileMovementComponent::UNinjaProjectileMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bComponentShouldUpdatePhysicsVolume = true;

	bFollowGravityDirection = false;
	OldGravityDirection = FVector::ZeroVector;
}

bool UNinjaProjectileMovementComponent::ShouldUseSubStepping() const
{
	return bForceSubStepping || (ShouldApplyGravity() && !GetGravity().IsZero()) ||
		(bIsHomingProjectile && HomingTargetComponent.IsValid());
}

FVector UNinjaProjectileMovementComponent::ComputeVelocity(FVector InitialVelocity, float DeltaTime) const
{
	if (bFollowGravityDirection)
	{
		FVector GravityDir;

		const ANinjaPhysicsVolume* NinjaPhysicsVolume = Cast<ANinjaPhysicsVolume>(GetPhysicsVolume());
		if (NinjaPhysicsVolume != nullptr)
		{
			GravityDir = NinjaPhysicsVolume->GetGravityDirection(UpdatedComponent);
		}
		else
		{
			GravityDir = FVector(0.0f, 0.0f, FMath::Sign(GetUnscaledGravityZ()));
		}

		if (!GravityDir.IsZero() && !OldGravityDirection.IsZero())
		{
			// Abort if angle between new and old gravity directions almost equals to 0 degrees
			if (FNinjaMath::Coincident(GravityDir, OldGravityDirection))
			{
				return Super::ComputeVelocity(InitialVelocity, DeltaTime);
			}

			// Figure out if previous angle is less than 180 degrees
			if (!FNinjaMath::Opposite(GravityDir, OldGravityDirection))
			{
				// Obtain quaternion rotation difference and apply it to velocity
				const FQuat QuatRotation = FQuat::FindBetweenNormals(
					OldGravityDirection, GravityDir);
				InitialVelocity = QuatRotation.RotateVector(InitialVelocity);
			}
			else
			{
				// Reverse velocity trajectory
				InitialVelocity *= -1.0f;

				GravityDir = OldGravityDirection * -1.0f;
			}
		}

		// Store calculated gravity direction
		UNinjaProjectileMovementComponent* MutableThis =
			const_cast<UNinjaProjectileMovementComponent*>(this);
		MutableThis->OldGravityDirection = GravityDir;
	}

	return Super::ComputeVelocity(InitialVelocity, DeltaTime);
}

FVector UNinjaProjectileMovementComponent::ComputeAcceleration(const FVector& InVelocity, float DeltaTime) const
{
	FVector Acceleration = GetGravity() + PendingForceThisUpdate;

	if (bIsHomingProjectile && HomingTargetComponent.IsValid())
	{
		Acceleration += ComputeHomingAcceleration(InVelocity, DeltaTime);
	}

	return Acceleration;
}

float UNinjaProjectileMovementComponent::GetGravityZ() const
{
	if (!ShouldApplyGravity())
	{
		return 0.0f;
	}

	const ANinjaPhysicsVolume* NinjaPhysicsVolume = Cast<ANinjaPhysicsVolume>(GetPhysicsVolume());
	if (NinjaPhysicsVolume != nullptr)
	{
		return (NinjaPhysicsVolume->GetGravity(UpdatedComponent) * ProjectileGravityScale).Z;
	}

	return GetUnscaledGravityZ() * ProjectileGravityScale;
}

FVector UNinjaProjectileMovementComponent::GetGravity() const
{
	if (!ShouldApplyGravity())
	{
		return FVector::ZeroVector;
	}

	const ANinjaPhysicsVolume* NinjaPhysicsVolume = Cast<ANinjaPhysicsVolume>(GetPhysicsVolume());
	if (NinjaPhysicsVolume != nullptr)
	{
		return NinjaPhysicsVolume->GetGravity(UpdatedComponent) * ProjectileGravityScale;
	}

	return FVector(0.0f, 0.0f, GetUnscaledGravityZ() * ProjectileGravityScale);
}
