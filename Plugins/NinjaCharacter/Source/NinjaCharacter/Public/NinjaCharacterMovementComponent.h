// "Ninja Character" plugin, by Javier 'Xaklse' Osset; Copyright 2020

// This class heavily uses code from Epic Games:
// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NinjaMath.h"
#include "NinjaTypes.h"
#include "NinjaCharacterMovementComponent.generated.h"


/**
 * A MovementComponent updates the position of the associated PrimitiveComponent
 * during its tick. This type handles the movement for Characters, and is able
 * to handle arbitrary gravity direction and collision capsule orientation.
 */
UCLASS()
class NINJACHARACTER_API UNinjaCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UNinjaCharacterMovementComponent(const FObjectInitializer& ObjectInitializer);

public:
	/** Returns location of controlled actor's "feet" meaning center of bottom of collision bounding box. */
	virtual FVector GetActorFeetLocation() const override;

public:
	/** Returns if the character rotation should be corrected on clients when sending a server move response correction. */
	virtual bool ShouldCorrectRotation() const override;

#if WITH_EDITOR
public:
	/**
	 * Called when a property of this object has been modified externally.
	 * @param PropertyChangedEvent - structure that holds the modified property
	 */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR

public:
	/**
	 * Perform jump. Called by Character when a jump has been detected because Character->bPressedJump was true. Checks CanJump().
	 * Note that you should usually trigger a jump through Character::Jump() instead.
	 * @param bReplayingMoves - true if this is being done as part of replaying moves on a locally controlled client after a server correction
	 * @return true if the jump was triggered successfully
	 */
	virtual bool DoJump(bool bReplayingMoves) override;

public:
	/**
	 * If we have a movement base, get the velocity that should be imparted by that base, usually when jumping off of it.
	 * Only applies the components of the velocity enabled by bImpartBaseVelocityX, bImpartBaseVelocityY, bImpartBaseVelocityZ.
	 */
	virtual FVector GetImpartedMovementBaseVelocity() const override;

public:
	/** Force this pawn to bounce off its current base, which isn't an acceptable base for it. */
	virtual void JumpOff(AActor* MovementBaseActor) override;

public:
	/** Can be overridden to choose to jump based on character velocity, base actor dimensions, etc. */
	virtual FVector GetBestDirectionOffActor(AActor* BaseActor) const override; // Calculates the best direction to go to "jump off" an actor.

public:
	/** Set movement mode to the default based on the current physics volume. */
	virtual void SetDefaultMovementMode() override;

protected:
	/** Called after MovementMode has changed. Base implementation does special handling for starting certain modes, then notifies the CharacterOwner. */
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;

protected:
	/** If true, currently applying a received movement mode. */
	uint32 bApplyingNetworkMovementMode:1;

public:
	virtual void ApplyNetworkMovementMode(const uint8 ReceivedMode) override;

public:
	/** Returns gravity that affects this component. */
	virtual float GetGravityZ() const override;

public:
	/**
	 * Update Velocity and Acceleration to air control in the desired Direction for character using path following.
	 * @param Direction - the desired direction of movement
	 * @param ZDiff - the height difference between the destination and the Pawn's current position
	 * @see RequestDirectMove()
	 */
	virtual void PerformAirControlForPathFollowing(FVector Direction, float ZDiff) override;

	/**
	 * Update Velocity and Acceleration to air control in the desired Direction for character using path following.
	 * @param MoveVelocity - the requested movement
	 * @param GravDir - the normalized direction of gravity
	 * @see RequestDirectMove()
	 */
	virtual void PerformAirControlForPathFollowingEx(const FVector& MoveVelocity, const FVector& GravDir);

	/**
	 * Function called every frame on this ActorComponent. Override this function to implement custom logic to be executed every frame.
	 * Only executes if the component is registered, and also PrimaryComponentTick.bCanEverTick must be set to true.
	 * @param DeltaTime - the time since the last tick
	 * @param TickType - the kind of tick this is, for example, are we paused, or 'simulating' in the editor
	 * @param ThisTickFunction - internal tick function struct that caused this to run
	 */
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

public:
	/**
	 * Constrain components of root motion velocity that may not be appropriate given the current movement mode (e.g. when falling Z may be ignored).
	 */
	virtual FVector ConstrainAnimRootMotionVelocity(const FVector& RootMotionVelocity, const FVector& CurrentVelocity) const override;

protected:
	/** If true, non-owning network clients won't skip simulating movement if base isn't replicated. */
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="NinjaCharacterMovement")
	uint32 bForceSimulateMovement:1;

	/** Simulate movement on a non-owning client. Called by SimulatedTick(). */
	virtual void SimulateMovement(float DeltaSeconds) override;

public:
	/** Update or defer updating of position based on Base movement. */
	virtual void MaybeUpdateBasedMovement(float DeltaSeconds) override;

public:
	/** Update position based on Base movement. */
	virtual void UpdateBasedMovement(float DeltaSeconds) override;

public:
	/** Update controller's view rotation as pawn's base rotates. */
	virtual void UpdateBasedRotation(FRotator& FinalRotation, const FRotator& ReducedRotation) override;

	/** Update controller's view rotation as pawn's base rotates. */
	virtual void UpdateBasedRotation(const FQuat& DeltaRotation);

protected:
	/** Perform movement on an autonomous client. */
	virtual void PerformMovement(float DeltaSeconds) override;

public:
	/**
	 * Checks if new capsule size fits (no encroachment), and call CharacterOwner->OnStartCrouch() if successful.
	 * In general you should set bWantsToCrouch instead to have the crouch persist during movement, or just use the crouch functions on the owning Character.
	 * @param bClientSimulation - true when called when bIsCrouched is replicated to non owned clients, to update collision cylinder and offset
	 */
	virtual void Crouch(bool bClientSimulation = false) override;

public:
	/**
	 * Checks if default capsule size fits (no encroachment), and trigger OnEndCrouch() on the owner if successful.
	 * @param bClientSimulation - true when called when bIsCrouched is replicated to non owned clients, to update collision cylinder and offset
	 */
	virtual void UnCrouch(bool bClientSimulation = false) override;

protected:
	/** Custom version of SlideAlongSurface that handles different movement modes separately; namely during walking physics we might not want to slide up slopes. */
	virtual float SlideAlongSurface(const FVector& Delta, float Time, const FVector& Normal, FHitResult& Hit, bool bHandleImpact) override;

protected:
	/** Custom version that allows upwards slides when walking if the surface is walkable. */
	virtual void TwoWallAdjust(FVector& WorldSpaceDelta, const FHitResult& Hit, const FVector& OldHitNormal) const override;

protected:
	/**
	 * Limit the slide vector when falling if the resulting slide might boost the character faster upwards.
	 * @param SlideResult - vector of movement for the slide (usually the result of ComputeSlideVector)
	 * @param Delta - original attempted move
	 * @param Time - amount of move to apply (between 0 and 1)
	 * @param Normal - normal opposed to movement. Not necessarily equal to Hit.Normal (but usually is)
	 * @param Hit - HitResult of the move that resulted in the slide
	 * @return new slide result
	 */
	virtual FVector HandleSlopeBoosting(const FVector& SlideResult, const FVector& Delta, const float Time, const FVector& Normal, const FHitResult& Hit) const override;

public:
	/**
	 * Determine how deep in water the character is immersed.
	 * @return float in range 0.0 = not in water, 1.0 = fully immersed
	 */
	virtual float ImmersionDepth() const override;

public:
	/** UNavMovementComponent Interface */
	virtual void RequestDirectMove(const FVector& MoveVelocity, bool bForceMaxSpeed) override;

public:
	/** UNavMovementComponent Interface */
	virtual void RequestPathMove(const FVector& MoveInput) override;

public:
	/**
	 * Compute the max jump height based on the JumpZVelocity velocity and gravity.
	 * This does not take into account the CharacterOwner's MaxJumpHoldTime.
	 */
	virtual float GetMaxJumpHeight() const override;

protected:
	/** @note Movement update functions should only be called through StartNewPhysics() */
	virtual void PhysFlying(float deltaTime, int32 Iterations) override;

protected:
	/** Applies root motion from root motion sources to velocity (override and additive). */
	virtual void ApplyRootMotionToVelocity(float deltaTime) override;

protected:
	/** @note Movement update functions should only be called through StartNewPhysics() */
	virtual void PhysSwimming(float deltaTime, int32 Iterations) override;

public:
	/**
	 * Handle start swimming functionality.
	 * @param OldLocation - Location on last tick
	 * @param OldVelocity - velocity at last tick
	 * @param timeTick - time since at OldLocation
	 * @param remainingTime - DeltaTime to complete transition to swimming
	 * @param Iterations - physics iteration count
	 */
	virtual void StartSwimming(FVector OldLocation, FVector OldVelocity, float timeTick, float remainingTime, int32 Iterations) override;

public:
	/**
	 * Get the lateral acceleration to use during falling movement. The Z component of the result is ignored.
	 * Default implementation returns current Acceleration value modified by GetAirControl(), with Z component removed,
	 * with magnitude clamped to GetMaxAcceleration(). This function is used internally by PhysFalling().
	 * @param DeltaTime - time step for the current update
	 * @return acceleration to use during falling movement
	 */
	virtual FVector GetFallingLateralAcceleration(float DeltaTime) override;

	/**
	 * Get the lateral acceleration to use during falling movement. The Z component of the result is ignored.
	 * Default implementation returns current Acceleration value modified by GetAirControl(), with Z component removed,
	 * with magnitude clamped to GetMaxAcceleration(). This function is used internally by PhysFalling().
	 * @param DeltaTime - time step for the current update
	 * @param GravDir - normalized direction of gravity
	 * @return acceleration to use during falling movement
	 */
	virtual FVector GetFallingLateralAccelerationEx(float DeltaTime, const FVector& GravDir) const;

public:
	/**
	 * Returns true if falling movement should limit air control. Limiting air control prevents input acceleration during falling movement
	 * from allowing velocity to redirect forces upwards while falling, which could result in slower falling or even upward boosting.
	 * @see GetFallingLateralAcceleration(), BoostAirControl(), GetAirControl(), LimitAirControl()
	 */
	virtual bool ShouldLimitAirControl(float DeltaTime, const FVector& FallAcceleration) const override;

public:
	/**
	 * Get the air control to use during falling movement.
	 * Given an initial air control (TickAirControl), applies the result of BoostAirControl().
	 * This function is used internally by GetFallingLateralAcceleration().
	 * @param DeltaTime - time step for the current update
	 * @param TickAirControl - current air control value
	 * @param FallAcceleration - acceleration used during movement
	 * @return air control to use during falling movement
	 * @see AirControl, BoostAirControl(), LimitAirControl(), GetFallingLateralAcceleration()
	 */
	virtual FVector GetAirControl(float DeltaTime, float TickAirControl, const FVector& FallAcceleration) override;

	/**
	 * Get the air control to use during falling movement.
	 * Given an initial air control (TickAirControl), applies the result of BoostAirControl().
	 * This function is used internally by GetFallingLateralAcceleration().
	 * @param DeltaTime - time step for the current update
	 * @param TickAirControl - current air control value
	 * @param FallAcceleration - acceleration used during movement
	 * @param GravDir - normalized direction of gravity
	 * @return air control to use during falling movement
	 * @see AirControl, BoostAirControl(), LimitAirControl(), GetFallingLateralAcceleration()
	 */
	virtual FVector GetAirControlEx(float DeltaTime, float TickAirControl, const FVector& FallAcceleration, const FVector& GravDir) const;

public:
	/**
	 * Set a custom, local gravity direction to use during movement simulation.
	 * The gravity direction must be synchronized by external systems between the autonomous
	 * and authority processes. The gravity direction will be corrected as part of movement
	 * corrections should the movement state diverge.
	 * SetGravityDirection is responsible for initializing cached values used to tranform to
	 * from gravity relative space.
	 * @param GravDir - non-zero vector representing the new gravity direction. The vector will be normalized.
	 */
	virtual void SetGravityDirection(const FVector& GravDir) override;

protected:
	/**
	 * Increase air control if conditions of AirControlBoostMultiplier and AirControlBoostVelocityThreshold are met.
	 * This function is used internally by GetAirControl().
	 * @param DeltaTime - time step for the current update
	 * @param TickAirControl - current air control value
	 * @param FallAcceleration - acceleration used during movement
	 * @return modified air control to use during falling movement
	 * @see GetAirControl()
	 */
	virtual float BoostAirControl(float DeltaTime, float TickAirControl, const FVector& FallAcceleration) override;

	/**
	 * Increase air control if conditions of AirControlBoostMultiplier and AirControlBoostVelocityThreshold are met.
	 * This function is used internally by GetAirControl().
	 * @param DeltaTime - time step for the current update
	 * @param TickAirControl - current air control value
	 * @param FallAcceleration - acceleration used during movement
	 * @param GravDir - normalized direction of gravity
	 * @return modified air control to use during falling movement
	 * @see GetAirControl()
	 */
	virtual float BoostAirControlEx(float DeltaTime, float TickAirControl, const FVector& FallAcceleration, const FVector& GravDir) const;

public:
	/** Handle falling movement. */
	virtual void PhysFalling(float deltaTime, int32 Iterations) override;

protected:
	/**
	 * Limits the air control to use during falling movement, given an impact while falling.
	 * This function is used internally by PhysFalling().
	 * @param DeltaTime - time step for the current update
	 * @param FallAcceleration - acceleration used during movement
	 * @param HitResult - result of impact
	 * @param bCheckForValidLandingSpot - if true, will use IsValidLandingSpot() to determine if HitResult is a walkable surface; if false, this check is skipped
	 * @return modified air control acceleration to use during falling movement
	 * @see PhysFalling()
	 */
	virtual FVector LimitAirControl(float DeltaTime, const FVector& FallAcceleration, const FHitResult& HitResult, bool bCheckForValidLandingSpot) override;

	/**
	 * Limits the air control to use during falling movement, given an impact while falling.
	 * This function is used internally by PhysFalling().
	 * @param DeltaTime - time step for the current update
	 * @param FallAcceleration - acceleration used during movement
	 * @param HitResult - result of impact
	 * @param GravDir - normalized direction of gravity
	 * @param bCheckForValidLandingSpot - if true, will use IsValidLandingSpot() to determine if HitResult is a walkable surface; if false, this check is skipped
	 * @return modified air control acceleration to use during falling movement
	 * @see PhysFalling()
	 */
	virtual FVector LimitAirControlEx(float DeltaTime, const FVector& FallAcceleration, const FHitResult& HitResult, const FVector& GravDir, bool bCheckForValidLandingSpot) const;

public:
	/** @return true if there is a suitable floor SideStep from current position. */
	virtual bool CheckLedgeDirection(const FVector& OldLocation, const FVector& SideStep, const FVector& GravDir) const override;

public:
	/**
	 * @param Delta is the current move delta (which ended up going over a ledge).
	 * @return new delta which moves along the ledge
	 */
	virtual FVector GetLedgeMove(const FVector& OldLocation, const FVector& Delta, const FVector& GravDir) const override;

public:
	/** Transition from walking to falling */
	virtual void StartFalling(int32 Iterations, float remainingTime, float timeTick, const FVector& Delta, const FVector& subLoc) override;

protected:
	/**
	 * Compute a vector of movement, given a delta and a hit result of the surface we are on.
	 * @param Delta - attempted movement direction
	 * @param RampHit - hit result of sweep that found the ramp below the capsule
	 * @param bHitFromLineTrace - whether the floor trace came from a line trace
	 * @return if on a walkable surface, this returns a vector that moves parallel to the surface; the magnitude may be scaled if bMaintainHorizontalGroundVelocity is true
	 * If a ramp vector can't be computed, this will just return Delta
	 */
	virtual FVector ComputeGroundMovementDelta(const FVector& Delta, const FHitResult& RampHit, const bool bHitFromLineTrace) const override;

	/**
	 * Compute a vector of movement, given a delta and a hit result of the surface we are on.
	 * @param Delta - attempted movement direction
	 * @param DeltaPlaneNormal - normal of the plane that contains Delta
	 * @param RampHit - hit result of sweep that found the ramp below the capsule
	 * @param bHitFromLineTrace - whether the floor trace came from a line trace
	 * @return if on a walkable surface, this returns a vector that moves parallel to the surface; the magnitude may be scaled if bMaintainHorizontalGroundVelocity is true
	 * If a ramp vector can't be computed, this will just return Delta
	 */
	virtual FVector ComputeGroundMovementDeltaEx(const FVector& Delta, const FVector& DeltaPlaneNormal, const FHitResult& RampHit, const bool bHitFromLineTrace) const;

protected:
	/**
	 * Move along the floor, using CurrentFloor and ComputeGroundMovementDelta() to get a movement direction.
	 * If a second walkable surface is hit, it will also be moved along using the same approach.
	 * @param InVelocity - velocity of movement
	 * @param DeltaSeconds - time over which movement occurs
	 * @param OutStepDownResult - if non-null, and a floor check is performed, this will be updated to reflect that result
	 */
	virtual void MoveAlongFloor(const FVector& InVelocity, float DeltaSeconds, FStepDownResult* OutStepDownResult = nullptr) override;

protected:
	/**
	 * Adjusts velocity when walking so that Z velocity is zero.
	 * When bMaintainHorizontalGroundVelocity is false, also rescales the velocity vector to maintain the original magnitude, but in the horizontal direction.
	 */
	virtual void MaintainHorizontalGroundVelocity() override;

protected:
	/** @note Movement update functions should only be called through StartNewPhysics() */
	virtual void PhysWalking(float deltaTime, int32 Iterations) override;

public:
	/** Adjust distance from floor, trying to maintain a slight offset from the floor when walking (based on CurrentFloor). */
	virtual void AdjustFloorHeight() override;

protected:
	/** Use new physics after landing; defaults to swimming if in water, walking otherwise. */
	virtual void SetPostLandedPhysics(const FHitResult& Hit) override;

public:
	/** Called by owning Character upon successful teleport from AActor::TeleportTo(). */
	virtual void OnTeleported() override;

public:
	/** Perform rotation over DeltaTime. */
	virtual void PhysicsRotation(float DeltaTime) override;

public:
	/**
	 * If true, revert to engine's hardcoded gravity direction when entering the DefaultPhysicsVolume.
	 * @note The DefaultPhysicsVolume is found in areas of the level with no PhysicsVolume
	 */
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="NinjaCharacterMovement")
	uint32 bRevertToDefaultGravity:1;

	/** Delegate when PhysicsVolume of UpdatedComponent has been changed. */
	virtual void PhysicsVolumeChanged(class APhysicsVolume* NewVolume) override;

public:
	/** Assign the component we move and update. */
	virtual void SetUpdatedComponent(USceneComponent* NewUpdatedComponent) override;

public:
	/**
	 * Determine whether the Character should jump when exiting water.
	 * @param JumpDir - the desired direction to jump out of water
	 * @return true if Pawn should jump out of water
	 */
	virtual bool ShouldJumpOutOfWater(FVector& JumpDir) override;

	/**
	 * Determine whether the Character should jump when exiting water.
	 * @param JumpDir - the desired direction to jump out of water
	 * @param GravDir - the normalized direction of gravity
	 * @return true if Pawn should jump out of water
	 */
	virtual bool ShouldJumpOutOfWaterEx(FVector& JumpDir, const FVector& GravDir);

public:
	/** Check if swimming pawn just ran into edge of the pool and should jump out. */
	virtual bool CheckWaterJump(FVector CheckPoint, FVector& WallNormal) override;

	/** Check if swimming pawn just ran into edge of the pool and should jump out. */
	virtual bool CheckWaterJumpEx(FVector CheckPoint, const FVector& GravDir, FVector& WallNormal);

public:
	/**
	 * Moves along the given movement direction using simple movement rules based on the current movement mode (usually used by simulated proxies).
	 * @param InVelocity - velocity of movement
	 * @param DeltaSeconds - time over which movement occurs
	 * @param OutStepDownResult - if non-null, and a floor check is performed, this will be updated to reflect that result
	 */
	virtual void MoveSmooth(const FVector& InVelocity, const float DeltaSeconds, FStepDownResult* OutStepDownResult = nullptr) override;

public:
	/** If false when landing on a surface, gravity direction is also checked to know if the surface is walkable. */
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="NinjaCharacterMovement")
	bool bLandOnAnySurface;

	/** Return true if the hit result should be considered a walkable surface for the character. */
	virtual bool IsWalkable(const FHitResult& Hit) const override;

public:
	/**
	 * Return true if the 2D distance to the impact point is inside the edge tolerance (CapsuleRadius minus a small rejection threshold).
	 * Useful for rejecting adjacent hits when finding a floor or landing spot.
	 */
	virtual bool IsWithinEdgeTolerance(const FVector& CapsuleLocation, const FVector& TestImpactPoint, const float CapsuleRadius) const override;

	/**
	 * Return true if the 2D distance to the impact point is inside the edge tolerance (CapsuleRadius minus a small rejection threshold).
	 * Useful for rejecting adjacent hits when finding a floor or landing spot.
	 */
	virtual bool IsWithinEdgeToleranceEx(const FVector& CapsuleLocation, const FVector& CapsuleDown, const float CapsuleRadius, const FVector& TestImpactPoint) const;

public:
	/**
	 * Compute distance to the floor from bottom sphere of capsule and store the result in OutFloorResult.
	 * This distance is the swept distance of the capsule to the first point impacted by the lower hemisphere, or distance from the bottom of the capsule in the case of a line trace.
	 * This function does not care if collision is disabled on the capsule (unlike FindFloor).
	 * @see FindFloor
	 * @param CapsuleLocation - location of the capsule used for the query
	 * @param LineDistance - if non-zero, max distance to test for a simple line check from the capsule base; used only if the sweep test fails to find a walkable floor, and only returns a valid result if the impact normal is a walkable normal
	 * @param SweepDistance - if non-zero, max distance to use when sweeping a capsule downwards for the test; MUST be greater than or equal to the line distance
	 * @param OutFloorResult - result of the floor check; the HitResult will contain the valid sweep or line test upon success, or the result of the sweep upon failure
	 * @param SweepRadius - the radius to use for sweep tests; should be <= capsule radius
	 * @param DownwardSweepResult - if non-null and it contains valid blocking hit info, this will be used as the result of a downward sweep test instead of doing it as part of the update
	 */
	virtual void ComputeFloorDist(const FVector& CapsuleLocation, float LineDistance, float SweepDistance, FFindFloorResult& OutFloorResult, float SweepRadius, const FHitResult* DownwardSweepResult = nullptr) const override;

public:
	/**
	 * Sweep against the world and return the first blocking hit.
	 * Intended for tests against the floor, because it may change the result of impacts on the lower area of the test (especially if bUseFlatBaseForFloorChecks is true).
	 * @param OutHit - first blocking hit found
	 * @param Start - start location of the capsule
	 * @param End - end location of the capsule
	 * @param TraceChannel - the 'channel' that this trace is in, used to determine which components to hit
	 * @param CollisionShape - capsule collision shape
	 * @param Params - additional parameters used for the trace
	 * @param ResponseParam - response container to be used for this trace
	 * @return true if OutHit contains a blocking hit entry
	 */
	virtual bool FloorSweepTest(struct FHitResult& OutHit, const FVector& Start, const FVector& End, ECollisionChannel TraceChannel,
		const FCollisionShape& CollisionShape, const FCollisionQueryParams& Params, const FCollisionResponseParams& ResponseParam) const override;

public:
	/** Verify that the supplied hit result is a valid landing spot when falling. */
	virtual bool IsValidLandingSpot(const FVector& CapsuleLocation, const FHitResult& Hit) const override;

public:
	/**
	 * Determine whether we should try to find a valid landing spot after an impact with an invalid one (based on the Hit result).
	 * For example, landing on the lower portion of the capsule on the edge of geometry may be a walkable surface, but could have reported an unwalkable impact normal.
	 */
	virtual bool ShouldCheckForValidLandingSpot(float DeltaTime, const FVector& Delta, const FHitResult& Hit) const override;

public:
	/**
	 * Check if the result of a sweep test (passed in InHit) might be a valid location to perch, in which case we should use ComputePerchResult to validate the location.
	 * @see ComputePerchResult
	 * @param InHit - result of the last sweep test before this query
	 * @param bCheckRadius - if true, only allow the perch test if the impact point is outside the radius returned by GetValidPerchRadius()
	 * @return whether perching may be possible, such that ComputePerchResult can return a valid result
	 */
	virtual bool ShouldComputePerchResult(const FHitResult& InHit, bool bCheckRadius = true) const override;

public:
	/**
	 * Compute the sweep result of the smaller capsule with radius specified by GetValidPerchRadius(),
	 * and return true if the sweep contacts a valid walkable normal within InMaxFloorDist of InHit.ImpactPoint.
	 * This may be used to determine if the capsule can or cannot stay at the current location if perched on the edge of a small ledge or unwalkable surface.
	 * @note Only returns a valid result if ShouldComputePerchResult returned true for the supplied hit value
	 * @param TestRadius - radius to use for the sweep, usually GetValidPerchRadius()
	 * @param InHit - result of the last sweep test before the query
	 * @param InMaxFloorDist - max distance to floor allowed by perching, from the supplied contact point (InHit.ImpactPoint)
	 * @param OutPerchFloorResult - contains the result of the perch floor test
	 * @return true if the current location is a valid spot at which to perch
	 */
	virtual bool ComputePerchResult(const float TestRadius, const FHitResult& InHit, const float InMaxFloorDist, FFindFloorResult& OutPerchFloorResult) const override;

protected:
	/**
	 * Event triggered at the end of a movement update. If scoped movement updates are enabled (bEnableScopedMovementUpdates), this is within such a scope.
	 * If that is not desired, bind to the CharacterOwner's OnMovementUpdated event instead, as that is triggered after the scoped movement update.
	 */
	virtual void OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity) override;

public:
	/**
	 * Move up steps or slope. Does nothing and returns false if CanStepUp(Hit) returns false.
	 * @param GravDir - gravity vector direction (assumed normalized or zero)
	 * @param Delta - requested move
	 * @param Hit - the hit before the step up
	 * @param OutStepDownResult - if non-null, a floor check will be performed if possible as part of the final step down, and it will be updated to reflect this result
	 * @return true if the step up was successful
	 */
	virtual bool StepUp(const FVector& GravDir, const FVector& Delta, const FHitResult &Hit, struct FStepDownResult* OutStepDownResult = nullptr) override;

protected:
	/** Handle a blocking impact. Calls ApplyImpactPhysicsForces for the hit, if bEnablePhysicsInteraction is true. */
	virtual void HandleImpact(const FHitResult& Hit, float TimeSlice = 0.0f, const FVector& MoveDelta = FVector::ZeroVector) override;

protected:
	/**
	 * Apply physics forces to the impacted component, if bEnablePhysicsInteraction is true.
	 * @param Impact - HitResult that resulted in the impact
	 * @param ImpactAcceleration - acceleration of the character at the time of impact
	 * @param ImpactVelocity - velocity of the character at the time of impact
	 */
	virtual void ApplyImpactPhysicsForces(const FHitResult& Impact, const FVector& ImpactAcceleration, const FVector& ImpactVelocity) override;

public:
	/**
	 * Draw important variables on canvas.
	 * Character will call DisplayDebug() on the current ViewTarget when the ShowDebug exec is used.
	 * @param Canvas - Canvas to draw on
	 * @param DebugDisplay - contains information about what debug data to display
	 * @param YL - height of the current font
	 * @param YPos - Y position on Canvas; YPos += YL, gives position to draw text for next debug line
	 */
	virtual void DisplayDebug(UCanvas* Canvas, const FDebugDisplayInfo& DebugDisplay, float& YL, float& YPos) override;

public:
	/**
	 * Draw in-world debug information for character movement (called with p.VisualizeMovement > 0).
	 */
	virtual float VisualizeMovement() const override;

protected:
	/** Enforce constraints on input given current state. For instance, don't move upwards if walking and looking up. */
	virtual FVector ConstrainInputAcceleration(const FVector& InputAcceleration) const override;

private:
	/** Remember last server movement base so we can detect mounts/dismounts and respond accordingly. */
//	TWeakObjectPtr<UPrimitiveComponent> LastServerMovementBase = nullptr;
	TWeakObjectPtr<UPrimitiveComponent> LastServerMovementBaseOVERRIDEN = nullptr;

protected:
	/**
	 * Have the server check if the client is outside an error tolerance, and queue a client adjustment if so.
	 * If either GetPredictionData_Server_Character()->bForceClientUpdate or ServerCheckClientError() are true, the client adjustment will be sent.
	 * RelativeClientLocation will be a relative location if MovementBaseUtility::UseRelativePosition(ClientMovementBase) is true, or a world location if false.
	 * @see ServerCheckClientError()
	 */
	virtual void ServerMoveHandleClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel, const FVector& RelativeClientLocation, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode) override;

public:
	/* Replicate position correction to client when using root motion for movement. (animation root motion specific) */
	virtual void ClientAdjustRootMotionPosition_Implementation(float TimeStamp, float ServerMontageTrackPosition, FVector ServerLoc, FVector_NetQuantizeNormal ServerRotation, float ServerVelZ, UPrimitiveComponent* ServerBase, FName ServerBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode) override;

public:
	/* Replicate root motion source correction to client when using root motion for movement. */
	virtual void ClientAdjustRootMotionSourcePosition_Implementation(float TimeStamp, FRootMotionSourceGroup ServerRootMotion, bool bHasAnimRootMotion, float ServerMontageTrackPosition, FVector ServerLoc, FVector_NetQuantizeNormal ServerRotation, float ServerVelZ, UPrimitiveComponent* ServerBase, FName ServerBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode) override;

protected:
	/** Event notification when client receives a correction from the server. Base implementation logs relevant data and draws debug info if "p.NetShowCorrections" is not equal to 0. */
	virtual void OnClientCorrectionReceived(class FNetworkPredictionData_Client_Character& ClientData, float TimeStamp, FVector NewLocation, FVector NewVelocity, UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode, FVector ServerGravityDirection) override;

protected:
	/** Called when the collision capsule touches another primitive component. */
	virtual void CapsuleTouched(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult) override;

public:
	/**
	 * Applies downward force when walking on top of physics objects.
	 * @param DeltaSeconds - time elapsed since last frame
	 */
	virtual void ApplyDownwardForce(float DeltaSeconds) override;

public:
	/** Applies repulsion force to all touched components. */
	virtual void ApplyRepulsionForce(float DeltaSeconds) override;

public:
	/** Applies momentum accumulated through AddImpulse() and AddForce(), then clears those forces. Does *not* use ClearAccumulatedForces() since that would clear pending launch velocity as well. */
	virtual void ApplyAccumulatedForces(float DeltaSeconds) override;

public:
	/** If true, when the Character bumps into an unwalkable blocking object, triggers unwalkable hit events. */
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="NinjaCharacterMovement")
	uint32 bTriggerUnwalkableHits:1;

protected:
	/** Stores the last time a walk unwalkable hit is attempted, to avoid multiple hit triggers per frame. */
	float LastUnwalkableHitTime;

public:
	/**
	 * Called when the updated component hits (or is hit by) something solid.
	 * This could happen due to things like Character movement, using Set Location with 'sweep' enabled, or physics simulation.
	 * @note For collisions during physics simulation to generate hit events, 'Simulation Generates Hit Events' must be enabled
	 * @note When receiving a hit from another object's movement (bSelfMoved is false), directions of Hit.Normal and Hit.ImpactNormal are adjusted to indicate force from the other object against this object
	 */
	UFUNCTION()
	void OnComponentHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

protected:
	/**
	 * Called when the updated component bumps into an unwalkable blocking object.
	 * @param Hit - contains info about the hit, such as point of impact and surface normal at that point
	 */
	void UnwalkableHit(const FHitResult& Hit);

	/**
	 * Called when the updated component bumps into an unwalkable blocking object.
	 * @note Can be overriden
	 * @param Hit - contains info about the hit, such as point of impact and surface normal at that point
	 */
	virtual void OnUnwalkableHit(const FHitResult& Hit);

protected:
	/** Mode that determines direction of gravity. */
	UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="NinjaCharacterMovement")
	ENinjaGravityDirectionMode GravityDirectionMode;

	/** Stores information that determines direction of gravity. */
	UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="NinjaCharacterMovement")
	FVector GravityVectorA;

	/** Stores additional information that determines direction of gravity. */
	UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="NinjaCharacterMovement")
	FVector GravityVectorB;

	/** Optional Actor that determines direction of gravity. */
	UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="NinjaCharacterMovement")
	TObjectPtr<AActor> GravityActor;

protected:
	/** If true, gravity direction changed and needs to be replicated. */
	uint32 bDirtyGravityDirection:1;

	/** If true, gravity data isn't replicated from server to clients. */
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="NinjaCharacterMovement")
	uint32 bDisableGravityReplication:1;

	/**
	 * Asks if gravity data should be replicated from server to clients.
	 * @return true if gravity data should be replicated
	 */
	virtual bool ShouldReplicateGravity() const;

public:
	/**
	 * Obtains the current gravity.
	 * @note Could return zero gravity
	 * @return current gravity
	 */
	virtual FVector GetGravity() const;

	/**
	 * Obtains the normalized direction of the current gravity.
	 * @note Could return no gravity direction due to zero gravity
	 * @param bAvoidZeroGravity - if true, a gravity direction is always returned, even with zero gravity scale
	 * @return normalized direction of current gravity
	 */
	UFUNCTION(BlueprintPure,Category="Pawn|Components|NinjaCharacterMovement",Meta=(DisplayName="Get Gravity Direction (Ninja)",ScriptName="GetGravityDirection",Keywords="getgravitydirection"))
	virtual FVector K2_GetGravityDirection(bool bAvoidZeroGravity = false) const;

	/**
	 * Obtains the normalized direction of the current gravity.
	 * @note Could return no gravity direction due to zero gravity
	 * @param bAvoidZeroGravity - if true, a gravity direction is always returned, even with zero gravity scale
	 * @return normalized direction of current gravity
	 */
	virtual FVector GetGravityDirection(bool bAvoidZeroGravity) const;

	/**
	 * Obtains the absolute (positive) magnitude of the current gravity.
	 * @return magnitude of current gravity
	 */
	UFUNCTION(BlueprintPure,Category="Pawn|Components|NinjaCharacterMovement")
	virtual float GetGravityMagnitude() const;

	/**
	 * Obtains the scale factor that affects magnitude of current gravity.
	 * @note Returns GravityScale multiplied by other gravity scales
	 * @return final gravity scale factor
	 */
	UFUNCTION(BlueprintPure,Category="Pawn|Components|NinjaCharacterMovement")
	virtual float GetFinalGravityScale() const;

protected:
	/**
	 * Obtains the current unscaled Z gravity that affects this component.
	 * @note Calculated by the engine, do not use if possible
	 * @return current Z gravity without applying scale factors
	 */
	FORCEINLINE float GetUnscaledGravityZ() const
	{
		return UPawnMovementComponent::GetGravityZ();
	}

public:
	/**
	 * Sets a new fixed gravity direction.
	 * @note It can be influenced by GravityScale
	 * @param NewGravityDirection - new gravity direction, assumes it isn't normalized
	 */
	UFUNCTION(BlueprintCallable,Category="Pawn|Components|NinjaCharacterMovement",Meta=(DisplayName="Set Fixed Gravity Direction",ScriptName="SetFixedGravityDirection",Keywords="setgravitydirection"))
	virtual void K2_SetFixedGravityDirection(const FVector& NewGravityDirection);

	/**
	 * Sets a new fixed gravity direction.
	 * @note It can be influenced by GravityScale
	 * @param NewFixedGravityDirection - new fixed gravity direction, assumes it is normalized
	 */
	virtual void SetFixedGravityDirection(const FVector& NewFixedGravityDirection);

protected:
	/**
	 * Replicates a new fixed gravity direction to clients.
	 * @param NewFixedGravityDirection - new fixed gravity direction, assumes it is normalized
	 */
	UFUNCTION(NetMulticast,Reliable)
	virtual void MulticastSetFixedGravityDirection(const FVector& NewFixedGravityDirection);

public:
	/**
	 * Sets a new gravity direction determined by closest spline tangent.
	 * @note It can be influenced by GravityScale
	 * @param NewGravityActor - Actor that provides a spline
	 */
	UFUNCTION(BlueprintCallable,Category="Pawn|Components|NinjaCharacterMovement")
	virtual void SetSplineTangentGravityDirection(AActor* NewGravityActor);

protected:
	/**
	 * Replicates a new spline gravity direction to clients.
	 * @param NewGravityActor - Actor that provides a spline
	 */
	UFUNCTION(NetMulticast,Reliable)
	virtual void MulticastSetSplineTangentGravityDirection(AActor* NewGravityActor);

public:
	/**
	 * Sets a new point which gravity direction points to.
	 * @note It can be influenced by GravityScale
	 * @param NewGravityPoint - new point which gravity direction points to
	 */
	UFUNCTION(BlueprintCallable,Category="Pawn|Components|NinjaCharacterMovement")
	virtual void SetPointGravityDirection(const FVector& NewGravityPoint);

	/**
	 * Sets a new point which gravity direction points to.
	 * @note It can be influenced by GravityScale
	 * @param NewGravityActor - Actor that provides its location as gravity point
	 */
	UFUNCTION(BlueprintCallable,Category="Pawn|Components|NinjaCharacterMovement")
	virtual void SetPointGravityDirectionFromActor(AActor* NewGravityActor);

protected:
	/**
	 * Replicates a new gravity point to clients.
	 * @param NewGravityPoint - new point which gravity direction points to
	 */
	UFUNCTION(NetMulticast,Reliable)
	virtual void MulticastSetPointGravityDirection(const FVector& NewGravityPoint);

	/**
	 * Replicates a new gravity point to clients.
	 * @param NewGravityActor - Actor that provides its location as gravity point
	 */
	UFUNCTION(NetMulticast,Reliable)
	virtual void MulticastSetPointGravityDirectionFromActor(AActor* NewGravityActor);

public:
	/**
	 * Sets a new infinite line which gravity direction points to.
	 * @note It can be influenced by GravityScale
	 * @param NewGravityLineStart - a point that belongs to the infinite line
	 * @param NewGravityLineEnd - another point that belongs to the infinite line
	 */
	UFUNCTION(BlueprintCallable,Category="Pawn|Components|NinjaCharacterMovement")
	virtual void SetLineGravityDirection(const FVector& NewGravityLineStart, const FVector& NewGravityLineEnd);

protected:
	/**
	 * Replicates a new infinite line for gravity to clients.
	 * @param NewGravityLineStart - a point that belongs to the infinite line
	 * @param NewGravityLineEnd - another point that belongs to the infinite line
	 */
	UFUNCTION(NetMulticast,Reliable)
	virtual void MulticastSetLineGravityDirection(const FVector& NewGravityLineStart, const FVector& NewGravityLineEnd);

public:
	/**
	 * Sets a new segment line which gravity direction points to.
	 * @note It can be influenced by GravityScale
	 * @param NewGravitySegmentStart - start point of the segment line
	 * @param NewGravitySegmentEnd - end point of the segment line
	 */
	UFUNCTION(BlueprintCallable,Category="Pawn|Components|NinjaCharacterMovement")
	virtual void SetSegmentGravityDirection(const FVector& NewGravitySegmentStart, const FVector& NewGravitySegmentEnd);

protected:
	/**
	 * Replicates a new segment line for gravity to clients.
	 * @param NewGravitySegmentStart - start point of the segment line
	 * @param NewGravitySegmentEnd - end point of the segment line
	 */
	UFUNCTION(NetMulticast,Reliable)
	virtual void MulticastSetSegmentGravityDirection(const FVector& NewGravitySegmentStart, const FVector& NewGravitySegmentEnd);

public:
	/**
	 * Sets a new spline which gravity direction points to.
	 * @note It can be influenced by GravityScale
	 * @param NewGravityActor - Actor that provides a spline
	 */
	UFUNCTION(BlueprintCallable,Category="Pawn|Components|NinjaCharacterMovement")
	virtual void SetSplineGravityDirection(AActor* NewGravityActor);

protected:
	/**
	 * Replicates a new spline for gravity to clients.
	 * @param NewGravityActor - Actor that provides a spline
	 */
	UFUNCTION(NetMulticast,Reliable)
	virtual void MulticastSetSplineGravityDirection(AActor* NewGravityActor);

public:
	/**
	 * Sets a new infinite plane which gravity direction points to.
	 * @note It can be influenced by GravityScale
	 * @param NewGravityPlaneBase - a point that belongs to the plane
	 * @param NewGravityPlaneNormal - normal of the plane, assumes it isn't normalized
	 */
	UFUNCTION(BlueprintCallable,Category="Pawn|Components|NinjaCharacterMovement",Meta=(DisplayName="Set Plane Gravity Direction",ScriptName="SetPlaneGravityDirection"))
	virtual void K2_SetPlaneGravityDirection(const FVector& NewGravityPlaneBase, const FVector& NewGravityPlaneNormal);

	/**
	 * Sets a new infinite plane which gravity direction points to.
	 * @note It can be influenced by GravityScale
	 * @param NewGravityPlaneBase - a point that belongs to the plane
	 * @param NewGravityPlaneNormal - normal of the plane, assumes it is normalized
	 */
	virtual void SetPlaneGravityDirection(const FVector& NewGravityPlaneBase, const FVector& NewGravityPlaneNormal);

protected:
	/**
	 * Replicates a new infinite plane for gravity to clients.
	 * @param NewGravityPlaneBase - a point that belongs to the plane
	 * @param NewGravityPlaneNormal - normal of the plane, assumes it is normalized
	 */
	UFUNCTION(NetMulticast,Reliable)
	virtual void MulticastSetPlaneGravityDirection(const FVector& NewGravityPlaneBase, const FVector& NewGravityPlaneNormal);

public:
	/**
	 * Sets a new infinite plane determined by closest spline point and spline
	 * up vector which gravity direction points to.
	 * @note It can be influenced by GravityScale
	 * @param NewGravityActor - Actor that provides a spline
	 */
	UFUNCTION(BlueprintCallable,Category="Pawn|Components|NinjaCharacterMovement")
	virtual void SetSplinePlaneGravityDirection(AActor* NewGravityActor);

protected:
	/**
	 * Replicates a new infinite plane determined by closest spline point for
	 * gravity to clients.
	 * @param NewGravityActor - Actor that provides a spline
	 */
	UFUNCTION(NetMulticast,Reliable)
	virtual void MulticastSetSplinePlaneGravityDirection(AActor* NewGravityActor);

public:
	/**
	 * Sets a new axis-aligned box which gravity direction points to.
	 * @note It can be influenced by GravityScale
	 * @param NewGravityBoxOrigin - origin of the box
	 * @param NewGravityBoxExtent - extents of the box
	 */
	UFUNCTION(BlueprintCallable,Category="Pawn|Components|NinjaCharacterMovement")
	virtual void SetBoxGravityDirection(const FVector& NewGravityBoxOrigin, const FVector& NewGravityBoxExtent);

	/**
	 * Sets a new axis-aligned box which gravity direction points to.
	 * @note It can be influenced by GravityScale
	 * @param NewGravityActor - Actor that provides its collision bounding box as gravity target
	 */
	UFUNCTION(BlueprintCallable,Category="Pawn|Components|NinjaCharacterMovement")
	virtual void SetBoxGravityDirectionFromActor(AActor* NewGravityActor);

protected:
	/**
	 * Replicates a new axis-aligned box for gravity to clients.
	 * @param NewGravityBoxOrigin - origin of the box
	 * @param NewGravityBoxExtent - extents of the box
	 */
	UFUNCTION(NetMulticast,Reliable)
	virtual void MulticastSetBoxGravityDirection(const FVector& NewGravityBoxOrigin, const FVector& NewGravityBoxExtent);

	/**
	 * Replicates a new axis-aligned box for gravity to clients.
	 * @param NewGravityActor - Actor that provides its collision bounding box as gravity target
	 */
	UFUNCTION(NetMulticast,Reliable)
	virtual void MulticastSetBoxGravityDirectionFromActor(AActor* NewGravityActor);

public:
	/**
	 * Sets a new collision geometry which gravity direction points to.
	 * @note It can be influenced by GravityScale
	 * @param NewGravityActor - Actor that owns the PrimitiveComponent that has collision geometry
	 */
	UFUNCTION(BlueprintCallable,Category="Pawn|Components|NinjaCharacterMovement")
	virtual void SetCollisionGravityDirection(AActor* NewGravityActor);

protected:
	/**
	 * Replicates a new collision geometry for gravity to clients.
	 * @param NewGravityActor - Actor that owns the PrimitiveComponent that has collision geometry
	 */
	UFUNCTION(NetMulticast,Reliable)
	virtual void MulticastSetCollisionGravityDirection(AActor* NewGravityActor);

protected:
	/**
	 * Called after GravityDirectionMode (or related data) has changed.
	 * @param OldGravityDirectionMode - previous value of GravityDirectionMode
	 */
	void GravityDirectionChanged(ENinjaGravityDirectionMode OldGravityDirectionMode);

	/**
	 * Called after GravityDirectionMode (or related data) has changed.
	 * @note Can be overriden
	 * @param OldGravityDirectionMode - previous value of GravityDirectionMode
	 * @param CurrentGravityDirectionMode - current value of GravityDirectionMode
	 */
	virtual void OnGravityDirectionChanged(ENinjaGravityDirectionMode OldGravityDirectionMode, ENinjaGravityDirectionMode CurrentGravityDirectionMode);

protected:
	/** If true, custom gravity scales from other Actors are ignored. */
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="NinjaCharacterMovement")
	uint32 bIgnoreOtherGravityScales:1;

	/**
	 * Stores last known value of GravityScale.
	 * @see GravityScale
	 */
	float OldGravityScale;

protected:
	/**
	 * Replicates gravity scale factor to clients.
	 * @param NewGravityScale - new gravity scale factor
	 */
	UFUNCTION(NetMulticast,Reliable)
	virtual void MulticastSetGravityScale(float NewGravityScale);

public:
	/**
	 * If true and a floor is found, rotate gravity direction and align it to floor base.
	 * @note For "Fixed" gravity mode, gravity direction is set to reverse floor normal vector
	 * @note For "Point" gravity mode, gravity direction points to base's location
	 * @note For "Box" gravity mode, gravity direction points to base's bounding box
	 * @note For "Collision" gravity mode, gravity direction points to base's collision geometry
	 */
	UPROPERTY(EditAnywhere,BlueprintReadWrite,BlueprintSetter=SetAlignGravityToBase,Category="NinjaCharacterMovement")
	uint32 bAlignGravityToBase:1;

	/**
	 * Sets a new state for bAlignGravityToBase flag.
	 * @param bNewAlignGravityToBase - new value for bAlignGravityToBase flag
	 */
	UFUNCTION(BlueprintSetter,Category="Pawn|Components|NinjaCharacterMovement")
	virtual void SetAlignGravityToBase(bool bNewAlignGravityToBase);

protected:
	/**
	 * Enables bAlignGravityToBase flag for clients.
	 */
	UFUNCTION(NetMulticast,Reliable)
	virtual void MulticastEnableAlignGravityToBase();

	/**
	 * Disables bAlignGravityToBase flag for clients.
	 */
	UFUNCTION(NetMulticast,Reliable)
	virtual void MulticastDisableAlignGravityToBase();

public:
	/**
	 * Update values related to gravity.
	 */
	virtual void UpdateGravity();

	/**
	 * Sends gravity data from server to clients.
	 */
	virtual void ReplicateGravityToClients();

public:
	/**
	 * Calculate a constrained rotation for the updated component.
	 * @param Rotation - initial rotation
	 * @return new rotation to use
	 */
	virtual FRotator ConstrainComponentRotation(const FRotator& Rotation) const;

public:
	/**
	 * Return the current local X rotation axis of the updated component.
	 * @return current X rotation axis of the updated component
	 */
	FORCEINLINE FVector GetComponentAxisX() const
	{
		return FNinjaMath::GetAxisX(UpdatedComponent->GetComponentQuat());
	}

	/**
	 * Return the current local Y rotation axis of the updated component.
	 * @return current Y rotation axis of the updated component
	 */
	FORCEINLINE FVector GetComponentAxisY() const
	{
		return FNinjaMath::GetAxisY(UpdatedComponent->GetComponentQuat());
	}

	/**
	 * Return the current local Z rotation axis of the updated component.
	 * @return current Z rotation axis of the updated component
	 */
	FORCEINLINE FVector GetComponentAxisZ() const
	{
		return FNinjaMath::GetAxisZ(UpdatedComponent->GetComponentQuat());
	}

public:
	/**
	 * If true and a floor is found, rotate the Character and align it to floor normal vector.
	 * @note Activation of "Use Flat Base for Floor Checks" should be avoided.
	 */
	UPROPERTY(EditAnywhere,BlueprintReadWrite,BlueprintSetter=SetAlignComponentToFloor,Category="NinjaCharacterMovement")
	uint32 bAlignComponentToFloor:1;

	/**
	 * Sets a new state for bAlignComponentToFloor flag.
	 * @param bNewAlignComponentToFloor - new value for bAlignComponentToFloor flag
	 */
	UFUNCTION(BlueprintSetter,Category="Pawn|Components|NinjaCharacterMovement")
	virtual void SetAlignComponentToFloor(bool bNewAlignComponentToFloor);

protected:
	/**
	 * Enables bAlignComponentToFloor flag for clients.
	 */
	UFUNCTION(NetMulticast,Reliable)
	virtual void MulticastEnableAlignComponentToFloor();

	/**
	 * Disables bAlignComponentToFloor flag for clients.
	 */
	UFUNCTION(NetMulticast,Reliable)
	virtual void MulticastDisableAlignComponentToFloor();

public:
	/** If true, rotate the Character and align it to the gravity direction. */
	UPROPERTY(EditAnywhere,BlueprintReadWrite,BlueprintSetter=SetAlignComponentToGravity,Category="NinjaCharacterMovement")
	uint32 bAlignComponentToGravity:1;

	/**
	 * Sets a new state for bAlignComponentToGravity flag.
	 * @param bNewAlignComponentToGravity - new value for bAlignComponentToGravity flag
	 */
	UFUNCTION(BlueprintSetter,Category="Pawn|Components|NinjaCharacterMovement")
	virtual void SetAlignComponentToGravity(bool bNewAlignComponentToGravity);

protected:
	/**
	 * Enables bAlignComponentToGravity flag for clients.
	 */
	UFUNCTION(NetMulticast,Reliable)
	virtual void MulticastEnableAlignComponentToGravity();

	/**
	 * Disables bAlignComponentToGravity flag for clients.
	 */
	UFUNCTION(NetMulticast,Reliable)
	virtual void MulticastDisableAlignComponentToGravity();

public:
	/** If true and the Character is aligned to something, always rotate the Character around its center. */
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="NinjaCharacterMovement")
	uint32 bAlwaysRotateAroundCenter:1;

	/**
	 * If true and the Character is aligned to something while walking, velocity direction is also rotated.
	 * @note Activating this prevents speed loss on component rotation change.
	 */
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="NinjaCharacterMovement")
	uint32 bRotateVelocityOnGround:1;

	/**
	 * Return the desired local Z rotation axis wanted for the updated component.
	 * @return desired Z rotation axis
	 */
	virtual FVector GetComponentDesiredAxisZ() const;

public:
	/**
	 * Sets a new local Z rotation axis for the updated component.
	 * @param NewComponentAxisZ - new local Z rotation wanted for the updated component
	 * @param bForceFindFloor - force find a floor and attach to it
	 * @return true if updated component was moved
	 */
	virtual bool SetComponentAxisZ(const FVector& NewComponentAxisZ, bool bForceFindFloor);

	/**
	 * Updates the rotation of the updated component.
	 * @param DesiredAxisZ - desired local Z rotation axis wanted for the updated component
	 * @param bRotateAroundCenter - if true, rotation happens around center of updated component
	 * @param bRotateVelocity - if true and rotation happens, velocity direction is also affected
	 * @return true if updated component was moved
	 */
	virtual bool UpdateComponentRotation(const FVector& DesiredAxisZ, bool bRotateAroundCenter, bool bRotateVelocity);

protected:
	/**
	 * Angle in degrees that determines if any two vectors are parallel.
	 * @note Reducing this improves smoothness of certain rotation calculations
	 */
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="NinjaCharacterMovement",Meta=(NoSpinbox=true,ClampMin=0.25f,ClampMax=1.0f,UIMin=0.25f,UIMax=1.0f))
	float ThresholdParallelAngle;

	/**
	 * Sets a new value for ThresholdParallelAngle.
	 * @note The new value is clamped
	 * @param NewThresholdParallelAngle - new value for ThresholdParallelAngle
	 */
	virtual void SetThresholdParallelAngle(float NewThresholdParallelAngle);

	/** Threshold that determines if two unit vectors are perpendicular. */
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="NinjaCharacterMovement")
	float ThresholdOrthogonalCosine;

	/** Threshold that determines if two unit vectors are parallel. */
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="NinjaCharacterMovement")
	float ThresholdParallelCosine;

public:
	/**
	 * Return the current threshold that determines if two unit vectors are orthogonal.
	 * @return cosine threshold for orthogonal unit vectors
	 */
	FORCEINLINE float GetThresholdOrthogonalCosine() const
	{
		return ThresholdOrthogonalCosine;
	}

	/**
	 * Return the current threshold that determines if two unit vectors are parallel.
	 * @return cosine threshold for parallel unit vectors
	 */
	FORCEINLINE float GetThresholdParallelCosine() const
	{
		return ThresholdParallelCosine;
	}
};
