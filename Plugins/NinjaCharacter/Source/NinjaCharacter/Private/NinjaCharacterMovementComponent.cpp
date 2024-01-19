// "Ninja Character" plugin, by Javier 'Xaklse' Osset; Copyright 2020

// This class heavily uses code from Epic Games:
// Copyright Epic Games, Inc. All Rights Reserved.


#include "NinjaCharacterMovementComponent.h"

#include "NinjaCharacter.h"
#include "NinjaMath.h"
#include "NinjaPhysicsVolume.h"

#include "UObject/Package.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PhysicsVolume.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/GameNetworkManager.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Canvas.h"
#include "Navigation/PathFollowingComponent.h"
#include "Components/BrushComponent.h"
#include "Engine/NetworkObjectList.h"
#include "Net/PerfCountersHelpers.h"
#include "Engine/ScopedMovementUpdate.h"
#include "Components/SplineComponent.h"
#include "HAL/IConsoleManager.h"
#include "Chaos/Framework/PhysicsSolverBase.h"


// Log categories
DEFINE_LOG_CATEGORY_STATIC(LogCharacterMovement, Log, All);

// Character stats
DECLARE_CYCLE_STAT(TEXT("Char PerformMovement"), STAT_CharacterMovementPerformMovement, STATGROUP_Character);
DECLARE_CYCLE_STAT(TEXT("Char RootMotionSource Calculate"), STAT_CharacterMovementRootMotionSourceCalculate, STATGROUP_Character);
DECLARE_CYCLE_STAT(TEXT("Char RootMotionSource Apply"), STAT_CharacterMovementRootMotionSourceApply, STATGROUP_Character);
DECLARE_CYCLE_STAT(TEXT("Char StepUp"), STAT_CharStepUp, STATGROUP_Character);
DECLARE_CYCLE_STAT(TEXT("Char AdjustFloorHeight"), STAT_CharAdjustFloorHeight, STATGROUP_Character);
DECLARE_CYCLE_STAT(TEXT("Char PhysWalking"), STAT_CharPhysWalking, STATGROUP_Character);
DECLARE_CYCLE_STAT(TEXT("Char PhysFalling"), STAT_CharPhysFalling, STATGROUP_Character);
DECLARE_CYCLE_STAT(TEXT("Char HandleImpact"), STAT_CharHandleImpact, STATGROUP_Character);
DECLARE_CYCLE_STAT(TEXT("Char GetGravity"), STAT_CharacterMovementGetGravity, STATGROUP_Character);
DECLARE_CYCLE_STAT(TEXT("Char GetGravityDirection"), STAT_CharacterMovementGetGravityDirection, STATGROUP_Character);
DECLARE_CYCLE_STAT(TEXT("Char UpdateComponentRotation"), STAT_CharacterMovementUpdateComponentRotation, STATGROUP_Character);

namespace NinjaCharacterMovementConstants
{
	// Magic numbers
	const float MAX_STEP_SIDE_Z = 0.08f; // Maximum Z value for the normal on the vertical side of steps
	const float SWIMBOBSPEED = -80.0f;
	const float VERTICAL_SLOPE_NORMAL_Z = 0.001f; // Slope is vertical if Abs(Normal.Z) <= this threshold. Accounts for precision problems that sometimes angle normals slightly off horizontal for vertical surface
}

static const FString PerfCounter_NumServerMoves = TEXT("NumServerMoves");
static const FString PerfCounter_NumServerMoveCorrections = TEXT("NumServerMoveCorrections");

// Defines for build configs
#if DO_CHECK && !UE_BUILD_SHIPPING // Disable even if checks in shipping are enabled
	#define devCode(Code)	checkCode(Code)
#else
	#define devCode(...)
#endif

// CVars
namespace NinjaCharacterMovementCVars
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	static int32 ShowGravity = 0;
	FAutoConsoleVariableRef CVarShowGravity(
		TEXT("p.ShowGravity"),
		ShowGravity,
		TEXT("Whether to draw in-world debug information for current character gravity.\n")
		TEXT("0: Disable, 1: Enable"),
		EConsoleVariableFlags::ECVF_Cheat);

	static int32 ShowControlRotation = 0;
	FAutoConsoleVariableRef CVarShowControlRotation(
		TEXT("p.ShowControlRotation"),
		ShowControlRotation,
		TEXT("Whether to draw in-world debug information for controller's control rotation.\n")
		TEXT("0: Disable, 1: Enable"),
		EConsoleVariableFlags::ECVF_Cheat);
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
}


UNinjaCharacterMovementComponent::UNinjaCharacterMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NetworkSmoothingMode = ENetworkSmoothingMode::Disabled;
	RotationRate = FRotator(360.0f, 360.0f, 360.0f);

	bAlignComponentToFloor = false;
	bAlignComponentToGravity = false;
	bAlignGravityToBase = false;
	bAlwaysRotateAroundCenter = false;
	bApplyingNetworkMovementMode = false;
	bDirtyGravityDirection = false;
	bDisableGravityReplication = false;
	bForceSimulateMovement = false;
	bIgnoreOtherGravityScales = false;
	bLandOnAnySurface = false;
	bRevertToDefaultGravity = false;
	bRotateVelocityOnGround = false;
	bTriggerUnwalkableHits = false;
	GravityActor = nullptr;
	GravityDirectionMode = ENinjaGravityDirectionMode::Fixed;
	GravityVectorA = FVector::DownVector;
	GravityVectorB = FVector::ZeroVector;
	LastUnwalkableHitTime = -1.0f;
	OldGravityScale = GravityScale;

	SetThresholdParallelAngle(1.0f);
}

FVector UNinjaCharacterMovementComponent::GetActorFeetLocation() const
{
	return UpdatedComponent ?
		(UpdatedComponent->GetComponentLocation() - GetComponentAxisZ() * CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) :
		FNavigationSystem::InvalidLocation;
}

bool UNinjaCharacterMovementComponent::ShouldCorrectRotation() const
{
	return true;
}

#if WITH_EDITOR
void UNinjaCharacterMovementComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, ThresholdParallelAngle))
	{
		// Compute new threshold values
		SetThresholdParallelAngle(ThresholdParallelAngle);
	}
}
#endif // WITH_EDITOR

bool UNinjaCharacterMovementComponent::DoJump(bool bReplayingMoves)
{
	if (CharacterOwner && CharacterOwner->CanJump())
	{
		const FVector JumpDir = GetComponentAxisZ();

		// If movement isn't constrained or the angle between plane normal and jump direction is between 60 and 120 degrees..
		if (!bConstrainToPlane || FMath::Abs(PlaneConstraintNormal | JumpDir) < 0.5f)
		{
			const float VelocityZ = Velocity | JumpDir;

			// Set to zero the vertical component of velocity
			Velocity = FVector::VectorPlaneProject(Velocity, JumpDir);

			// Perform jump
			Velocity += JumpDir * FMath::Max<FVector::FReal>(VelocityZ, JumpZVelocity);
			SetMovementMode(MOVE_Falling);

			return true;
		}
	}

	return false;
}

FVector UNinjaCharacterMovementComponent::GetImpartedMovementBaseVelocity() const
{
	FVector Result = FVector::ZeroVector;
	if (CharacterOwner)
	{
		UPrimitiveComponent* MovementBase = CharacterOwner->GetMovementBase();
		if (MovementBaseUtility::IsDynamicBase(MovementBase))
		{
			FVector BaseVelocity = MovementBaseUtility::GetMovementBaseVelocity(MovementBase, CharacterOwner->GetBasedMovement().BoneName);

			if (bImpartBaseAngularVelocity)
			{
				const FVector CharacterBasePosition = (UpdatedComponent->GetComponentLocation() - GetComponentAxisZ() * CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
				const FVector BaseTangentialVel = MovementBaseUtility::GetMovementBaseTangentialVelocity(MovementBase, CharacterOwner->GetBasedMovement().BoneName, CharacterBasePosition);
				BaseVelocity += BaseTangentialVel;
			}

			if (bImpartBaseVelocityX)
			{
				Result.X = BaseVelocity.X;
			}
			if (bImpartBaseVelocityY)
			{
				Result.Y = BaseVelocity.Y;
			}
			if (bImpartBaseVelocityZ)
			{
				Result.Z = BaseVelocity.Z;
			}
		}
	}

	return Result;
}

void UNinjaCharacterMovementComponent::JumpOff(AActor* MovementBaseActor)
{
	if (!bPerformingJumpOff)
	{
		bPerformingJumpOff = true;

		if (CharacterOwner)
		{
			const float MaxSpeed = GetMaxSpeed() * 0.85f;
			Velocity += GetBestDirectionOffActor(MovementBaseActor) * MaxSpeed;

			const FVector JumpDir = GetComponentAxisZ();
			FVector Velocity2D = FVector::VectorPlaneProject(Velocity, JumpDir);

			if (Velocity2D.Size() > MaxSpeed)
			{
				Velocity2D = FVector::VectorPlaneProject(Velocity.GetSafeNormal() * MaxSpeed, JumpDir);
			}

			Velocity = Velocity2D + JumpDir * (JumpZVelocity * JumpOffJumpZFactor);
			SetMovementMode(MOVE_Falling);
		}

		bPerformingJumpOff = false;
	}
}

FVector UNinjaCharacterMovementComponent::GetBestDirectionOffActor(AActor* BaseActor) const
{
	// By default, just pick a random direction. Derived character classes can choose to do more complex calculations,
	// such as finding the shortest distance to move in based on the BaseActor's bounding volume
	const float RandAngle = FMath::DegreesToRadians(GetNetworkSafeRandomAngleDegrees());
	const FQuat PawnRotation = UpdatedComponent->GetComponentQuat();
	return PawnRotation.RotateVector(FVector(FMath::Cos(RandAngle), FMath::Sin(RandAngle), 0.5f).GetSafeNormal());
}

void UNinjaCharacterMovementComponent::SetDefaultMovementMode()
{
	// Check for water volume
	if (CanEverSwim() && IsInWater())
	{
		SetMovementMode(DefaultWaterMovementMode);
	}
	else if (!CharacterOwner || MovementMode != DefaultLandMovementMode)
	{
		const FVector SavedVelocity = Velocity;
		SetMovementMode(DefaultLandMovementMode);

		// Avoid 1-frame delay if trying to walk but walking fails at this location
		if (MovementMode == MOVE_Walking && GetMovementBase() == NULL)
		{
			Velocity = SavedVelocity; // Prevent temporary walking state from modifying velocity
			SetMovementMode(MOVE_Falling);
		}
	}
}

void UNinjaCharacterMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	if (!HasValidData())
	{
		return;
	}

	// Update collision settings if needed
	if (MovementMode == MOVE_NavWalking)
	{
		// Reset cached nav location used by NavWalking
		CachedNavLocation = FNavLocation();

//		GroundMovementMode = MovementMode;
		SetGroundMovementMode(MovementMode); //OVERRIDEN

		// @todo arbitrary-gravity: NavWalking not supported
		// Walking uses only XY velocity
		Velocity.Z = 0.0f;
		SetNavWalkingPhysics(true);
	}
	else if (PreviousMovementMode == MOVE_NavWalking)
	{
		if (MovementMode == DefaultLandMovementMode || IsWalking())
		{
			const bool bSucceeded = TryToLeaveNavWalking();
			if (!bSucceeded)
			{
				return;
			}
		}
		else
		{
			SetNavWalkingPhysics(false);
		}
	}

	// React to changes in the movement mode
	if (MovementMode == MOVE_Walking)
	{
		// Walking must be on a walkable floor, with a base
		bCrouchMaintainsBaseLocation = true;
//		GroundMovementMode = MovementMode;
		SetGroundMovementMode(MovementMode); //OVERRIDEN

		// Make sure we update our new floor/base on initial entry of the walking physics
		{
			TGuardValue<bool> LandOnAnySurfaceGuard(bLandOnAnySurface, bLandOnAnySurface || bApplyingNetworkMovementMode);
			FindFloor(UpdatedComponent->GetComponentLocation(), CurrentFloor, false);
		}
		UpdateComponentRotation(GetComponentDesiredAxisZ(), bAlwaysRotateAroundCenter, false);
		AdjustFloorHeight();
		SetBaseFromFloor(CurrentFloor);

		// Walking uses only horizontal velocity
		MaintainHorizontalGroundVelocity();
	}
	else
	{
		CurrentFloor.Clear();
		bCrouchMaintainsBaseLocation = false;

		UpdateComponentRotation(GetComponentDesiredAxisZ(), true, false);

		if (MovementMode == MOVE_Falling)
		{
			DecayingFormerBaseVelocity = GetImpartedMovementBaseVelocity();
			Velocity += DecayingFormerBaseVelocity;
			if (bMovementInProgress && CurrentRootMotion.HasAdditiveVelocity())
			{
				// If we leave a base during movement and we have additive root motion, we need to add the imparted velocity so that it retains it next tick
				CurrentRootMotion.LastPreAdditiveVelocity += DecayingFormerBaseVelocity;
			}
			static IConsoleVariable* AddFormerBaseVelocityToRootMotionOverrideWhenFallingVar =
				IConsoleManager::Get().FindConsoleVariable(TEXT("p.AddFormerBaseVelocityToRootMotionOverrideWhenFalling"));
			if (FormerBaseVelocityDecayHalfLife == 0.0f ||
				(AddFormerBaseVelocityToRootMotionOverrideWhenFallingVar != nullptr && !AddFormerBaseVelocityToRootMotionOverrideWhenFallingVar->GetBool()))
			{
				DecayingFormerBaseVelocity = FVector::ZeroVector;
			}
			CharacterOwner->Falling();
		}

		SetBase(NULL);

		if (MovementMode == MOVE_None)
		{
			// Kill velocity and clear queued up events
			StopMovementKeepPathing();
			CharacterOwner->ResetJumpState();
			ClearAccumulatedForces();
		}
	}

	if (MovementMode == MOVE_Falling && PreviousMovementMode != MOVE_Falling)
	{
		IPathFollowingAgentInterface* PFAgent = GetPathFollowingAgent();
		if (PFAgent)
		{
			PFAgent->OnStartedFalling();
		}
	}

	CharacterOwner->OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
//	ensureMsgf(GroundMovementMode == MOVE_Walking || GroundMovementMode == MOVE_NavWalking, TEXT("Invalid GroundMovementMode %d. MovementMode: %d, PreviousMovementMode: %d"), GroundMovementMode.GetValue(), MovementMode.GetValue(), PreviousMovementMode);
	ensureMsgf(GetGroundMovementMode() == MOVE_Walking || GetGroundMovementMode() == MOVE_NavWalking, TEXT("Invalid GroundMovementMode %d. MovementMode: %d, PreviousMovementMode: %d"), static_cast<uint8>(GetGroundMovementMode()), MovementMode.GetValue(), PreviousMovementMode); //OVERRIDEN
}

void UNinjaCharacterMovementComponent::ApplyNetworkMovementMode(const uint8 ReceivedMode)
{
	bApplyingNetworkMovementMode = true;

	Super::ApplyNetworkMovementMode(ReceivedMode);

	bApplyingNetworkMovementMode = false;
}

float UNinjaCharacterMovementComponent::GetGravityZ() const
{
	return GetUnscaledGravityZ() * GetFinalGravityScale();
}

void UNinjaCharacterMovementComponent::PerformAirControlForPathFollowing(FVector Direction, float ZDiff)
{
	// Abort if no valid gravity can be obtained
	const FVector GravityDir = GetGravityDirection(false);
	if (GravityDir.IsZero())
	{
		return;
	}

	PerformAirControlForPathFollowingEx(Direction, GravityDir);
}

void UNinjaCharacterMovementComponent::PerformAirControlForPathFollowingEx(const FVector& MoveVelocity, const FVector& GravDir)
{
	const float MoveSpeedZ = (MoveVelocity | GravDir) * -1.0f;

	// Use air control if low grav or above destination and falling towards it
	if (CharacterOwner && (Velocity | GravDir) > 0.0f && (MoveSpeedZ < 0.0f || GetGravityMagnitude() < FMath::Abs(0.9f * GetWorld()->GetDefaultGravityZ())))
	{
		if (MoveSpeedZ < 0.0f)
		{
			const FVector Velocity2D = FVector::VectorPlaneProject(Velocity, GravDir);
			if (Velocity2D.SizeSquared() == 0.0f)
			{
				Acceleration = FVector::ZeroVector;
			}
			else
			{
				const float Dist2D = FVector::VectorPlaneProject(MoveVelocity, GravDir).Size();
				Acceleration = MoveVelocity.GetSafeNormal() * GetMaxAcceleration();

				if (Dist2D < 0.5f * FMath::Abs(MoveSpeedZ) && (Velocity | MoveVelocity) > 0.5f * FMath::Square(Dist2D))
				{
					Acceleration *= -1.0f;
				}

				if (Dist2D < 1.5f * CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius())
				{
					Velocity = GravDir * (Velocity | GravDir);
					Acceleration = FVector::ZeroVector;
				}
				else if ((Velocity | MoveVelocity) < 0.0f)
				{
					const float M = FMath::Max(0.0f, 0.2f - GetWorld()->DeltaTimeSeconds);
					Velocity = Velocity2D * M + GravDir * (Velocity | GravDir);
				}
			}
		}
	}
}

void UNinjaCharacterMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (NinjaCharacterMovementCVars::ShowGravity > 0)
	{
		const FVector Gravity = GetGravity();
		if (!Gravity.IsZero())
		{
			DrawDebugDirectionalArrow(GetWorld(), GetActorLocation(), GetActorLocation() + Gravity,
				1000.0f, FColor::Red, false, -1.0f, 0, 7.0f);
		}

		switch (GravityDirectionMode)
		{
			case ENinjaGravityDirectionMode::SplineTangent:
			{
				DrawDebugDirectionalArrow(GetWorld(), GetActorLocation(),
					GetActorLocation() + GravityVectorA * 1000.0f, 100.0f, FColor::Green, false, -1.0f, 0, 4.0f);
				break;
			}

			case ENinjaGravityDirectionMode::Point:
			case ENinjaGravityDirectionMode::Spline:
			case ENinjaGravityDirectionMode::Collision:
			{
				DrawDebugSphere(GetWorld(), GravityVectorA, 4.0, 8, FColor::Green, false, -1.0f, 0, 10.0f);
				break;
			}

			case ENinjaGravityDirectionMode::Line:
			{
				DrawDebugLine(GetWorld(), GravityVectorA + (GravityVectorA - GravityVectorB),
					GravityVectorB + (GravityVectorB - GravityVectorA), FColor::Green, false, -1.0f, 0, 4.0f);
				DrawDebugSphere(GetWorld(), GravityVectorA, 4.0, 8, FColor::Blue, false, -1.0f, 0, 10.0f);
				DrawDebugSphere(GetWorld(), GravityVectorB, 4.0, 8, FColor::Blue, false, -1.0f, 0, 10.0f);
				break;
			}

			case ENinjaGravityDirectionMode::Segment:
			{
				DrawDebugLine(GetWorld(), GravityVectorA, GravityVectorB, FColor::Green, false, -1.0f, 0, 4.0f);
				DrawDebugSphere(GetWorld(), GravityVectorA, 4.0, 8, FColor::Blue, false, -1.0f, 0, 10.0f);
				DrawDebugSphere(GetWorld(), GravityVectorB, 4.0, 8, FColor::Blue, false, -1.0f, 0, 10.0f);
				break;
			}

			case ENinjaGravityDirectionMode::Plane:
			case ENinjaGravityDirectionMode::SplinePlane:
			{
				DrawDebugSolidPlane(GetWorld(), FPlane(GravityVectorA, GravityVectorB), GravityVectorA,
					FVector2D(500.0f, 500.0f), FColor::Green);
				break;
			}

			case ENinjaGravityDirectionMode::Box:
			{
				DrawDebugSolidBox(GetWorld(), GravityVectorA, GravityVectorB, FColor::Green);
				break;
			}
		}
	}

	if (NinjaCharacterMovementCVars::ShowControlRotation > 0)
	{
		const AController* Controller = (CharacterOwner != nullptr) ? CharacterOwner->Controller : nullptr;
		if (Controller != nullptr)
		{
			DrawDebugCoordinateSystem(GetWorld(), GetActorLocation(), Controller->GetControlRotation(),
				100.0f, false, 1.0f, 0, 2.0f);
		}
	}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
}

FVector UNinjaCharacterMovementComponent::ConstrainAnimRootMotionVelocity(const FVector& RootMotionVelocity, const FVector& CurrentVelocity) const
{
	FVector Result = RootMotionVelocity;

	// Do not override vertical velocity if in falling physics, we want to keep the effect of gravity
	if (IsFalling())
	{
		const FVector GravityDir = GetGravityDirection(true);
		Result = FVector::VectorPlaneProject(Result, GravityDir) + GravityDir * (CurrentVelocity | GravityDir);
	}

	return Result;
}

void UNinjaCharacterMovementComponent::SimulateMovement(float DeltaSeconds)
{
	if (!HasValidData() || UpdatedComponent->Mobility != EComponentMobility::Movable || UpdatedComponent->IsSimulatingPhysics())
	{
		return;
	}

	const bool bIsSimulatedProxy = (CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy);
	const FRepMovement& ConstRepMovement = CharacterOwner->GetReplicatedMovement();

	// Workaround for replication not being updated initially
	if (bIsSimulatedProxy &&
		ConstRepMovement.Location.IsZero() &&
		ConstRepMovement.Rotation.IsZero() &&
		ConstRepMovement.LinearVelocity.IsZero())
	{
		return;
	}

	// If base is not resolved on the client, we should not try to simulate at all
	if (!bForceSimulateMovement && CharacterOwner->GetReplicatedBasedMovement().IsBaseUnresolved())
	{
		UE_LOG(LogCharacterMovement, Verbose, TEXT("Base for simulated character '%s' is not resolved on client, skipping SimulateMovement"), *CharacterOwner->GetName());
		return;
	}

	FVector OldVelocity;
	FVector OldLocation;

	// Scoped updates can improve performance of multiple MoveComponent calls
	{
		FScopedMovementUpdate ScopedMovementUpdate(UpdatedComponent, bEnableScopedMovementUpdates ? EScopedUpdate::DeferredUpdates : EScopedUpdate::ImmediateUpdates);

		bool bHandledNetUpdate = false;
		if (bIsSimulatedProxy)
		{
			// Handle network changes
			if (bNetworkUpdateReceived)
			{
				bNetworkUpdateReceived = false;
				bHandledNetUpdate = true;
				UE_LOG(LogCharacterMovement, Verbose, TEXT("Proxy %s received net update"), *CharacterOwner->GetName());
				if (bNetworkMovementModeChanged)
				{
					// Sync Z rotation axis of the updated component
					UpdateComponentRotation(FNinjaMath::GetAxisZ(ConstRepMovement.Rotation), true, false);

					ApplyNetworkMovementMode(CharacterOwner->GetReplicatedMovementMode());
					bNetworkMovementModeChanged = false;
				}
				else if (bJustTeleported || bForceNextFloorCheck)
				{
					// Sync Z rotation axis of the updated component
					UpdateComponentRotation(FNinjaMath::GetAxisZ(ConstRepMovement.Rotation), true, false);

					// Make sure floor is current. We will continue using the replicated base, if there was one
					bJustTeleported = false;
					UpdateFloorFromAdjustment();
				}
			}
			else if (bForceNextFloorCheck)
			{
				// Sync Z rotation axis of the updated component
				UpdateComponentRotation(FNinjaMath::GetAxisZ(ConstRepMovement.Rotation), true, false);

				UpdateFloorFromAdjustment();
			}
		}

		UpdateCharacterStateBeforeMovement(DeltaSeconds);

		if (MovementMode != MOVE_None)
		{
			//TODO: Also ApplyAccumulatedForces()?
			HandlePendingLaunch();
		}
		ClearAccumulatedForces();

		if (MovementMode == MOVE_None)
		{
			return;
		}

		const bool bSimGravityDisabled = (bIsSimulatedProxy && CharacterOwner->bSimGravityDisabled);
		const bool bZeroReplicatedGroundVelocity = (bIsSimulatedProxy && IsMovingOnGround() && ConstRepMovement.LinearVelocity.IsZero());

		// bSimGravityDisabled means velocity was zero when replicated and we were stuck in something. Avoid external changes in velocity as well
		// Being in ground movement with zero velocity, we cannot simulate proxy velocities safely because we might not get any further updates from the server
		if (bSimGravityDisabled || bZeroReplicatedGroundVelocity)
		{
			Velocity = FVector::ZeroVector;
		}

		MaybeUpdateBasedMovement(DeltaSeconds);

		// Simulated pawns predict location
		OldVelocity = Velocity;
		OldLocation = UpdatedComponent->GetComponentLocation();

		UpdateProxyAcceleration();

		// May only need to simulate forward on frames where we haven't just received a new position update
		static IConsoleVariable* NetEnableSkipProxyPredictionOnNetUpdateVar =
			IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetEnableSkipProxyPredictionOnNetUpdate"));
		if (!bHandledNetUpdate || !bNetworkSkipProxyPredictionOnNetUpdate ||
			(NetEnableSkipProxyPredictionOnNetUpdateVar != nullptr && !NetEnableSkipProxyPredictionOnNetUpdateVar->GetInt()))
		{
			UE_LOG(LogCharacterMovement, Verbose, TEXT("Proxy %s simulating movement"), *GetNameSafe(CharacterOwner));
			FStepDownResult StepDownResult;
			MoveSmooth(Velocity, DeltaSeconds, &StepDownResult);

			// Find floor and check if falling
			if (IsMovingOnGround() || MovementMode == MOVE_Falling)
			{
				const FVector Gravity = GetGravity();

				if (StepDownResult.bComputedFloor)
				{
					CurrentFloor = StepDownResult.FloorResult;
				}
				else if (IsMovingOnGround() || bLandOnAnySurface ||
					(!Gravity.IsZero() && ((Velocity | Gravity) * -1.0f) <= 0.0f))
				{
					FindFloor(UpdatedComponent->GetComponentLocation(), CurrentFloor, Velocity.IsZero(), NULL);
				}
				else
				{
					CurrentFloor.Clear();
				}

				// Possible for dynamic movement bases, particularly those that align to slopes while the character does not, to encroach the character
				// Check to see if we can resolve the penetration in those cases, and if so find the floor
				if (CurrentFloor.HitResult.bStartPenetrating && MovementBaseUtility::IsDynamicBase(GetMovementBase()))
				{
					// Follows PhysWalking approach for encroachment on floor tests
					FHitResult Hit(CurrentFloor.HitResult);
					Hit.TraceEnd = Hit.TraceStart + GetComponentAxisZ() * MAX_FLOOR_DIST;
					const FVector RequestedAdjustment = GetPenetrationAdjustment(Hit);
					const bool bResolved = ResolvePenetration(RequestedAdjustment, Hit, UpdatedComponent->GetComponentQuat());
					bForceNextFloorCheck |= bResolved;
				}
				else if (!CurrentFloor.IsWalkableFloor())
				{
					if (!bSimGravityDisabled)
					{
						// No floor, must fall
						if (bApplyGravityWhileJumping || !CharacterOwner->IsJumpProvidingForce() ||
							(!Gravity.IsZero() && ((Velocity | Gravity) * -1.0f) <= 0.0f))
						{
							Velocity = NewFallVelocity(Velocity, Gravity, DeltaSeconds);
						}
					}
					SetMovementMode(MOVE_Falling);
				}
				else
				{
					// Walkable floor
					if (IsMovingOnGround())
					{
						AdjustFloorHeight();
						SetBase(CurrentFloor.HitResult.Component.Get(), CurrentFloor.HitResult.BoneName);
					}
					else if (MovementMode == MOVE_Falling)
					{
						if (CurrentFloor.FloorDist <= MIN_FLOOR_DIST || (bSimGravityDisabled && CurrentFloor.FloorDist <= MAX_FLOOR_DIST))
						{
							// Landed
							SetPostLandedPhysics(CurrentFloor.HitResult);
						}
						else
						{
							if (!bSimGravityDisabled)
							{
								// Continue falling
								Velocity = NewFallVelocity(Velocity, Gravity, DeltaSeconds);
							}
							CurrentFloor.Clear();
						}
					}
				}
			}
		}
		else
		{
			UE_LOG(LogCharacterMovement, Verbose, TEXT("Proxy %s SKIPPING simulate movement"), *GetNameSafe(CharacterOwner));
		}

		UpdateCharacterStateAfterMovement(DeltaSeconds);

		// Consume path following requested velocity
		LastUpdateRequestedVelocity = bHasRequestedVelocity ? RequestedVelocity : FVector::ZeroVector;
		bHasRequestedVelocity = false;

		OnMovementUpdated(DeltaSeconds, OldLocation, OldVelocity);
	} // End scoped movement update

	// Call custom post-movement events. These happen after the scoped movement completes in case the events want to use the current state of overlaps etc
	CallMovementUpdateDelegate(DeltaSeconds, OldLocation, OldVelocity);

	static IConsoleVariable* BasedMovementModeVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.BasedMovementMode"));
	if (BasedMovementModeVar != nullptr && BasedMovementModeVar->GetInt() == 0)
	{
		SaveBaseLocation(); // Behaviour before implementing this fix
	}
	else
	{
		MaybeSaveBaseLocation();
	}
	UpdateComponentVelocity();
	bJustTeleported = false;

	LastUpdateLocation = UpdatedComponent ? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
	LastUpdateRotation = UpdatedComponent ? UpdatedComponent->GetComponentQuat() : FQuat::Identity;
	LastUpdateVelocity = Velocity;
}

void UNinjaCharacterMovementComponent::MaybeUpdateBasedMovement(float DeltaSeconds)
{
	UpdateGravity();

	const bool bMovingOnGround = IsMovingOnGround();
	UpdateComponentRotation(GetComponentDesiredAxisZ(), bAlwaysRotateAroundCenter || !bMovingOnGround,
		bRotateVelocityOnGround && bMovingOnGround);

	Super::MaybeUpdateBasedMovement(DeltaSeconds);
}

void UNinjaCharacterMovementComponent::UpdateBasedMovement(float DeltaSeconds)
{
	if (!HasValidData())
	{
		return;
	}

	const UPrimitiveComponent* MovementBase = CharacterOwner->GetMovementBase();
	if (!MovementBaseUtility::UseRelativeLocation(MovementBase))
	{
		return;
	}

	if (!IsValid(MovementBase) || !IsValid(MovementBase->GetOwner()))
	{
		SetBase(NULL);
		return;
	}

	// Ignore collision with bases during these movements
	TGuardValue<EMoveComponentFlags> ScopedFlagRestore(MoveComponentFlags, MoveComponentFlags | MOVECOMP_IgnoreBases);

	FQuat DeltaQuat = FQuat::Identity;
	FVector DeltaPosition = FVector::ZeroVector;

	FQuat NewBaseQuat;
	FVector NewBaseLocation;
	if (!MovementBaseUtility::GetMovementBaseTransform(MovementBase, CharacterOwner->GetBasedMovement().BoneName, NewBaseLocation, NewBaseQuat))
	{
		return;
	}

	// Find change in rotation
	const bool bRotationChanged = !OldBaseQuat.Equals(NewBaseQuat, 1e-8f);
	if (bRotationChanged)
	{
		DeltaQuat = NewBaseQuat * OldBaseQuat.Inverse();
	}

	// Only if base moved
	if (bRotationChanged || (OldBaseLocation != NewBaseLocation))
	{
		// Calculate new transform matrix of base actor (ignoring scale)
		const FQuatRotationTranslationMatrix OldLocalToWorld(OldBaseQuat, OldBaseLocation);
		const FQuatRotationTranslationMatrix NewLocalToWorld(NewBaseQuat, NewBaseLocation);

		FQuat FinalQuat = UpdatedComponent->GetComponentQuat();

		if (bRotationChanged && !bIgnoreBaseRotation)
		{
			// Apply change in rotation and pipe through FaceRotation to maintain axis restrictions
			const FQuat PawnOldQuat = UpdatedComponent->GetComponentQuat();
			const FQuat TargetQuat = DeltaQuat * FinalQuat;
			FRotator TargetRotator(TargetQuat);
			CharacterOwner->FaceRotation(TargetRotator, 0.0f);
			FinalQuat = UpdatedComponent->GetComponentQuat();

			if (PawnOldQuat.Equals(FinalQuat, 1e-6f))
			{
				// Nothing changed. This means we probably are using another rotation mechanism (bOrientToMovement etc). We should still follow the base object
				if (bOrientRotationToMovement || (bUseControllerDesiredRotation && CharacterOwner->Controller))
				{
					TargetRotator = ConstrainComponentRotation(TargetRotator);
					MoveUpdatedComponent(FVector::ZeroVector, TargetRotator, false);
					FinalQuat = UpdatedComponent->GetComponentQuat();
				}
			}

			// Pipe through ControlRotation, to affect camera
			if (CharacterOwner->Controller)
			{
				const FQuat PawnDeltaRotation = FinalQuat * PawnOldQuat.Inverse();
				UpdateBasedRotation(PawnDeltaRotation);
				FinalQuat = UpdatedComponent->GetComponentQuat();
			}
		}

		// We need to offset the base of the character here, not its origin, so offset by the origin of the bottom sphere
		float HalfHeight, Radius;
		CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleSize(Radius, HalfHeight);

		const FVector CapsuleUp = GetComponentAxisZ();
		const FVector BaseOffset = CapsuleUp * (HalfHeight - Radius);
		const FVector LocalBasePos = OldLocalToWorld.InverseTransformPosition(UpdatedComponent->GetComponentLocation() - BaseOffset);
		const FVector NewWorldPos = ConstrainLocationToPlane(NewLocalToWorld.TransformPosition(LocalBasePos) + BaseOffset);
		DeltaPosition = ConstrainDirectionToPlane(NewWorldPos - UpdatedComponent->GetComponentLocation());

		// Move attached actor
		if (bFastAttachedMove)
		{
			// We're trusting no other obstacle can prevent the move here
			UpdatedComponent->SetWorldLocationAndRotation(NewWorldPos, FinalQuat, false);
		}
		else
		{
			// hack - transforms between local and world space introducing slight error FIXMESTEVE - discuss with engine team: just skip the transforms if no rotation?
			if (!bRotationChanged)
			{
				const FVector BaseMoveDelta = NewBaseLocation - OldBaseLocation;
				if (FVector::VectorPlaneProject(BaseMoveDelta, CapsuleUp).IsNearlyZero())
				{
					DeltaPosition = CapsuleUp * (DeltaPosition | CapsuleUp);
				}
			}

			FHitResult MoveOnBaseHit(1.0f);
			const FVector OldLocation = UpdatedComponent->GetComponentLocation();
			MoveUpdatedComponent(DeltaPosition, FinalQuat, true, &MoveOnBaseHit);
			if (!((UpdatedComponent->GetComponentLocation() - (OldLocation + DeltaPosition)).IsNearlyZero()))
			{
				OnUnableToFollowBaseMove(DeltaPosition, OldLocation, MoveOnBaseHit);
			}
		}

		if (MovementBase->IsSimulatingPhysics() && CharacterOwner->GetMesh())
		{
			CharacterOwner->GetMesh()->ApplyDeltaToAllPhysicsTransforms(DeltaPosition, DeltaQuat);
		}
	}
}

void UNinjaCharacterMovementComponent::UpdateBasedRotation(FRotator& FinalRotation, const FRotator& ReducedRotation)
{
	UpdateBasedRotation(FQuat(ReducedRotation));
}

void UNinjaCharacterMovementComponent::UpdateBasedRotation(const FQuat& DeltaRotation)
{
	AController* Controller = (CharacterOwner != nullptr) ? CharacterOwner->Controller : nullptr;
	if (Controller != nullptr && !bIgnoreBaseRotation)
	{
		Controller->SetControlRotation((DeltaRotation * FQuat(Controller->GetControlRotation())).Rotator());
	}
}

void UNinjaCharacterMovementComponent::PerformMovement(float DeltaSeconds)
{
	SCOPE_CYCLE_COUNTER(STAT_CharacterMovementPerformMovement);

	const UWorld* MyWorld = GetWorld();
	if (!HasValidData() || MyWorld == nullptr)
	{
		return;
	}

	bTeleportedSinceLastUpdate = UpdatedComponent->GetComponentLocation() != LastUpdateLocation;

	// no movement if we can't move, or if currently doing physical simulation on UpdatedComponent
	if (MovementMode == MOVE_None || UpdatedComponent->Mobility != EComponentMobility::Movable || UpdatedComponent->IsSimulatingPhysics())
	{
		if (!CharacterOwner->bClientUpdating && !CharacterOwner->bServerMoveIgnoreRootMotion)
		{
			// Consume root motion
			if (CharacterOwner->IsPlayingRootMotion() && CharacterOwner->GetMesh())
			{
				TickCharacterPose(DeltaSeconds);
				RootMotionParams.Clear();
			}
			if (CurrentRootMotion.HasActiveRootMotionSources())
			{
				CurrentRootMotion.Clear();
			}
		}
		// Clear pending physics forces
		ClearAccumulatedForces();
		return;
	}

	// Force floor update if we've moved outside of CharacterMovement since last update.
	bForceNextFloorCheck |= (IsMovingOnGround() && bTeleportedSinceLastUpdate);

	// Update saved LastPreAdditiveVelocity with any external changes to character Velocity that happened since last update.
	if (CurrentRootMotion.HasAdditiveVelocity())
	{
		const FVector Adjustment = (Velocity - LastUpdateVelocity);
		CurrentRootMotion.LastPreAdditiveVelocity += Adjustment;

#if ROOT_MOTION_DEBUG
		if (RootMotionSourceDebug::CVarDebugRootMotionSources.GetValueOnGameThread() == 1)
		{
			if (!Adjustment.IsNearlyZero())
			{
				FString AdjustedDebugString = FString::Printf(TEXT("PerformMovement HasAdditiveVelocity LastUpdateVelocityAdjustment LastPreAdditiveVelocity(%s) Adjustment(%s)"),
					*CurrentRootMotion.LastPreAdditiveVelocity.ToCompactString(), *Adjustment.ToCompactString());
				RootMotionSourceDebug::PrintOnScreen(*CharacterOwner, AdjustedDebugString);
			}
		}
#endif
	}

	FVector OldVelocity;
	FVector OldLocation;

	// Scoped updates can improve performance of multiple MoveComponent calls.
	{
		FScopedMovementUpdate ScopedMovementUpdate(UpdatedComponent, bEnableScopedMovementUpdates ? EScopedUpdate::DeferredUpdates : EScopedUpdate::ImmediateUpdates);

		MaybeUpdateBasedMovement(DeltaSeconds);

		// Clean up invalid RootMotion Sources.
		// This includes RootMotion sources that ended naturally.
		// They might want to perform a clamp on velocity or an override,
		// so we want this to happen before ApplyAccumulatedForces and HandlePendingLaunch as to not clobber these.
		const bool bHasRootMotionSources = HasRootMotionSources();
		if (bHasRootMotionSources && !CharacterOwner->bClientUpdating && !CharacterOwner->bServerMoveIgnoreRootMotion)
		{
			SCOPE_CYCLE_COUNTER(STAT_CharacterMovementRootMotionSourceCalculate);

			const FVector VelocityBeforeCleanup = Velocity;
			CurrentRootMotion.CleanUpInvalidRootMotion(DeltaSeconds, *CharacterOwner, *this);

#if ROOT_MOTION_DEBUG
			if (RootMotionSourceDebug::CVarDebugRootMotionSources.GetValueOnGameThread() == 1)
			{
				if (Velocity != VelocityBeforeCleanup)
				{
					const FVector Adjustment = Velocity - VelocityBeforeCleanup;
					FString AdjustedDebugString = FString::Printf(TEXT("PerformMovement CleanUpInvalidRootMotion Velocity(%s) VelocityBeforeCleanup(%s) Adjustment(%s)"),
						*Velocity.ToCompactString(), *VelocityBeforeCleanup.ToCompactString(), *Adjustment.ToCompactString());
					RootMotionSourceDebug::PrintOnScreen(*CharacterOwner, AdjustedDebugString);
				}
			}
#endif
		}

		OldVelocity = Velocity;
		OldLocation = UpdatedComponent->GetComponentLocation();

		ApplyAccumulatedForces(DeltaSeconds);

		// Update the character state before we do our movement
		UpdateCharacterStateBeforeMovement(DeltaSeconds);

		if (MovementMode == MOVE_NavWalking && bWantsToLeaveNavWalking)
		{
			TryToLeaveNavWalking();
		}

		// Character::LaunchCharacter() has been deferred until now.
		HandlePendingLaunch();
		ClearAccumulatedForces();

#if ROOT_MOTION_DEBUG
		if (RootMotionSourceDebug::CVarDebugRootMotionSources.GetValueOnGameThread() == 1)
		{
			if (OldVelocity != Velocity)
			{
				const FVector Adjustment = Velocity - OldVelocity;
				FString AdjustedDebugString = FString::Printf(TEXT("PerformMovement ApplyAccumulatedForces+HandlePendingLaunch Velocity(%s) OldVelocity(%s) Adjustment(%s)"),
					*Velocity.ToCompactString(), *OldVelocity.ToCompactString(), *Adjustment.ToCompactString());
				RootMotionSourceDebug::PrintOnScreen(*CharacterOwner, AdjustedDebugString);
			}
		}
#endif

		// Update saved LastPreAdditiveVelocity with any external changes to character Velocity that happened due to ApplyAccumulatedForces/HandlePendingLaunch
		if (CurrentRootMotion.HasAdditiveVelocity())
		{
			const FVector Adjustment = (Velocity - OldVelocity);
			CurrentRootMotion.LastPreAdditiveVelocity += Adjustment;

#if ROOT_MOTION_DEBUG
			if (RootMotionSourceDebug::CVarDebugRootMotionSources.GetValueOnGameThread() == 1)
			{
				if (!Adjustment.IsNearlyZero())
				{
					FString AdjustedDebugString = FString::Printf(TEXT("PerformMovement HasAdditiveVelocity AccumulatedForces LastPreAdditiveVelocity(%s) Adjustment(%s)"),
						*CurrentRootMotion.LastPreAdditiveVelocity.ToCompactString(), *Adjustment.ToCompactString());
					RootMotionSourceDebug::PrintOnScreen(*CharacterOwner, AdjustedDebugString);
				}
			}
#endif
		}

		// Prepare Root Motion (generate/accumulate from root motion sources to be used later)
		if (bHasRootMotionSources && !CharacterOwner->bClientUpdating && !CharacterOwner->bServerMoveIgnoreRootMotion)
		{
			// Animation root motion - If using animation RootMotion, tick animations before running physics.
			if (CharacterOwner->IsPlayingRootMotion() && CharacterOwner->GetMesh())
			{
				TickCharacterPose(DeltaSeconds);

				// Make sure animation didn't trigger an event that destroyed us
				if (!HasValidData())
				{
					return;
				}

				// For local human clients, save off root motion data so it can be used by movement networking code.
				if (CharacterOwner->IsLocallyControlled() && (CharacterOwner->GetLocalRole() == ROLE_AutonomousProxy) && CharacterOwner->IsPlayingNetworkedRootMotionMontage())
				{
					CharacterOwner->ClientRootMotionParams = RootMotionParams;
				}
			}

			// Generates root motion to be used this frame from sources other than animation
			{
				SCOPE_CYCLE_COUNTER(STAT_CharacterMovementRootMotionSourceCalculate);
				CurrentRootMotion.PrepareRootMotion(DeltaSeconds, *CharacterOwner, *this, true);
			}

			// For local human clients, save off root motion data so it can be used by movement networking code.
			if (CharacterOwner->IsLocallyControlled() && (CharacterOwner->GetLocalRole() == ROLE_AutonomousProxy))
			{
				CharacterOwner->SavedRootMotion = CurrentRootMotion;
			}
		}

		// Apply Root Motion to Velocity
		if (CurrentRootMotion.HasOverrideVelocity() || HasAnimRootMotion())
		{
			// Animation root motion overrides Velocity and currently doesn't allow any other root motion sources
			if (HasAnimRootMotion())
			{
				// Convert to world space (animation root motion is always local)
				USkeletalMeshComponent* SkelMeshComp = CharacterOwner->GetMesh();
				if (SkelMeshComp)
				{
					// Convert Local Space Root Motion to world space. Do it right before used by physics to make sure we use up to date transforms, as translation is relative to rotation.
					RootMotionParams.Set(ConvertLocalRootMotionToWorld(RootMotionParams.GetRootMotionTransform(), DeltaSeconds));
				}

				// Then turn root motion to velocity to be used by various physics modes.
				if (DeltaSeconds > 0.0f)
				{
					AnimRootMotionVelocity = CalcAnimRootMotionVelocity(RootMotionParams.GetRootMotionTransform().GetTranslation(), DeltaSeconds, Velocity);
					Velocity = ConstrainAnimRootMotionVelocity(AnimRootMotionVelocity, Velocity);
					if (IsFalling())
					{
						Velocity += FVector::VectorPlaneProject(DecayingFormerBaseVelocity, GetGravityDirection(true));
					}
				}

				UE_LOG(LogRootMotion, Log, TEXT("PerformMovement WorldSpaceRootMotion Translation: %s, Rotation: %s, Actor Facing: %s, Velocity: %s")
					, *RootMotionParams.GetRootMotionTransform().GetTranslation().ToCompactString()
					, *RootMotionParams.GetRootMotionTransform().GetRotation().Rotator().ToCompactString()
					, *CharacterOwner->GetActorForwardVector().ToCompactString()
					, *Velocity.ToCompactString()
				);
			}
			else
			{
				// We don't have animation root motion so we apply other sources
				if (DeltaSeconds > 0.0f)
				{
					SCOPE_CYCLE_COUNTER(STAT_CharacterMovementRootMotionSourceApply);

					const FVector VelocityBeforeOverride = Velocity;
					FVector NewVelocity = Velocity;
					CurrentRootMotion.AccumulateOverrideRootMotionVelocity(DeltaSeconds, *CharacterOwner, *this, NewVelocity);
					if (IsFalling())
					{
						NewVelocity += CurrentRootMotion.HasOverrideVelocityWithIgnoreZAccumulate() ? FVector::VectorPlaneProject(DecayingFormerBaseVelocity, GetGravityDirection(true)) : DecayingFormerBaseVelocity;
					}
					Velocity = NewVelocity;

#if ROOT_MOTION_DEBUG
					if (RootMotionSourceDebug::CVarDebugRootMotionSources.GetValueOnGameThread() == 1)
					{
						if (VelocityBeforeOverride != Velocity)
						{
							FString AdjustedDebugString = FString::Printf(TEXT("PerformMovement AccumulateOverrideRootMotionVelocity Velocity(%s) VelocityBeforeOverride(%s)"),
								*Velocity.ToCompactString(), *VelocityBeforeOverride.ToCompactString());
							RootMotionSourceDebug::PrintOnScreen(*CharacterOwner, AdjustedDebugString);
						}
					}
#endif
				}
			}
		}

#if ROOT_MOTION_DEBUG
		if (RootMotionSourceDebug::CVarDebugRootMotionSources.GetValueOnGameThread() == 1)
		{
			FString AdjustedDebugString = FString::Printf(TEXT("PerformMovement Velocity(%s) OldVelocity(%s)"),
				*Velocity.ToCompactString(), *OldVelocity.ToCompactString());
			RootMotionSourceDebug::PrintOnScreen(*CharacterOwner, AdjustedDebugString);
		}
#endif

		// NaN tracking
		devCode(ensureMsgf(!Velocity.ContainsNaN(), TEXT("UCharacterMovementComponent::PerformMovement: Velocity contains NaN (%s)\n%s"), *GetPathNameSafe(this), *Velocity.ToString()));

		// Clear jump input now, to allow movement events to trigger it for next update.
		CharacterOwner->ClearJumpInput(DeltaSeconds);
		NumJumpApexAttempts = 0;

		// change position
		StartNewPhysics(DeltaSeconds, 0);

		if (!HasValidData())
		{
			return;
		}

		// Update character state based on change from movement
		UpdateCharacterStateAfterMovement(DeltaSeconds);

		if (bAllowPhysicsRotationDuringAnimRootMotion || !HasAnimRootMotion())
		{
			PhysicsRotation(DeltaSeconds);
		}

		// Apply Root Motion rotation after movement is complete.
		if (HasAnimRootMotion())
		{
			const FQuat OldActorRotationQuat = UpdatedComponent->GetComponentQuat();
			const FQuat RootMotionRotationQuat = RootMotionParams.GetRootMotionTransform().GetRotation();
			if (!RootMotionRotationQuat.IsIdentity())
			{
				const FQuat NewActorRotationQuat = RootMotionRotationQuat * OldActorRotationQuat;
				MoveUpdatedComponent(FVector::ZeroVector, NewActorRotationQuat, true);
			}

#if !(UE_BUILD_SHIPPING)
			// debug
			if (false)
			{
				const FRotator OldActorRotation = OldActorRotationQuat.Rotator();
				const FVector ResultingLocation = UpdatedComponent->GetComponentLocation();
				const FRotator ResultingRotation = UpdatedComponent->GetComponentRotation();

				// Show current position
				DrawDebugCoordinateSystem(MyWorld, CharacterOwner->GetMesh()->GetComponentLocation() + FVector(0, 0, 1), ResultingRotation, 50.0f, false);

				// Show resulting delta move.
				DrawDebugLine(MyWorld, OldLocation, ResultingLocation, FColor::Red, false, 10.0f);

				// Log details.
				UE_LOG(LogRootMotion, Warning, TEXT("PerformMovement Resulting DeltaMove Translation: %s, Rotation: %s, MovementBase: %s"), //-V595
					*(ResultingLocation - OldLocation).ToCompactString(), *(ResultingRotation - OldActorRotation).GetNormalized().ToCompactString(), *GetNameSafe(CharacterOwner->GetMovementBase()));

				const FVector RMTranslation = RootMotionParams.GetRootMotionTransform().GetTranslation();
				const FRotator RMRotation = RootMotionParams.GetRootMotionTransform().GetRotation().Rotator();
				UE_LOG(LogRootMotion, Warning, TEXT("PerformMovement Resulting DeltaError Translation: %s, Rotation: %s"),
					*(ResultingLocation - OldLocation - RMTranslation).ToCompactString(), *(ResultingRotation - OldActorRotation - RMRotation).GetNormalized().ToCompactString());
			}
#endif // !(UE_BUILD_SHIPPING)

			// Root Motion has been used, clear
			RootMotionParams.Clear();
		}
		else if (CurrentRootMotion.HasActiveRootMotionSources())
		{
			FQuat RootMotionRotationQuat;
			if (CharacterOwner && UpdatedComponent && CurrentRootMotion.GetOverrideRootMotionRotation(DeltaSeconds, *CharacterOwner, *this, RootMotionRotationQuat))
			{
				const FQuat OldActorRotationQuat = UpdatedComponent->GetComponentQuat();
				const FQuat NewActorRotationQuat = RootMotionRotationQuat * OldActorRotationQuat;
				MoveUpdatedComponent(FVector::ZeroVector, NewActorRotationQuat, true);
			}
		}

		// Consume path following requested velocity
		LastUpdateRequestedVelocity = bHasRequestedVelocity ? RequestedVelocity : FVector::ZeroVector;
		bHasRequestedVelocity = false;

		OnMovementUpdated(DeltaSeconds, OldLocation, OldVelocity);
	} // End scoped movement update

	// Call external post-movement events. These happen after the scoped movement completes in case the events want to use the current state of overlaps etc.
	CallMovementUpdateDelegate(DeltaSeconds, OldLocation, OldVelocity);

	static IConsoleVariable* BasedMovementModeVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.BasedMovementMode"));
	if (BasedMovementModeVar != nullptr && BasedMovementModeVar->GetInt() == 0)
	{
		SaveBaseLocation(); // Behaviour before implementing this fix
	}
	else
	{
		MaybeSaveBaseLocation();
	}
	UpdateComponentVelocity();

	const bool bHasAuthority = CharacterOwner && CharacterOwner->HasAuthority();

	// If we move we want to avoid a long delay before replication catches up to notice this change, especially if it's throttling our rate.
	if (bHasAuthority && UNetDriver::IsAdaptiveNetUpdateFrequencyEnabled() && UpdatedComponent)
	{
		UNetDriver* NetDriver = MyWorld->GetNetDriver();
		if (NetDriver && NetDriver->IsServer())
		{
			if (!NetDriver->IsPendingNetUpdate(CharacterOwner) && NetDriver->IsNetworkActorUpdateFrequencyThrottled(CharacterOwner))
			{
				if (ShouldCancelAdaptiveReplication())
				{
					NetDriver->CancelAdaptiveReplication(CharacterOwner);
				}
			}
		}
	}

	const FVector NewLocation = UpdatedComponent ? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
	const FQuat NewRotation = UpdatedComponent ? UpdatedComponent->GetComponentQuat() : FQuat::Identity;

	if (bHasAuthority && UpdatedComponent && !IsNetMode(NM_Client))
	{
		const bool bLocationChanged = (NewLocation != LastUpdateLocation);
		const bool bRotationChanged = (NewRotation != LastUpdateRotation);
		if (bLocationChanged || bRotationChanged)
		{
			// Update ServerLastTransformUpdateTimeStamp. This is used by Linear smoothing on clients to interpolate positions with the correct delta time,
			// so the timestamp should be based on the client's move delta (ServerAccumulatedClientTimeStamp), not the server time when receiving the RPC.
			const bool bIsRemotePlayer = (CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy);
			const FNetworkPredictionData_Server_Character* ServerData = bIsRemotePlayer ? GetPredictionData_Server_Character() : nullptr;
			static IConsoleVariable* NetUseClientTimestampForReplicatedTransformVar =
				IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetUseClientTimestampForReplicatedTransform"));
			if (bIsRemotePlayer && ServerData && (NetUseClientTimestampForReplicatedTransformVar == nullptr || NetUseClientTimestampForReplicatedTransformVar->GetInt()))
			{
				ServerLastTransformUpdateTimeStamp = float(ServerData->ServerAccumulatedClientTimeStamp);
			}
			else
			{
				ServerLastTransformUpdateTimeStamp = MyWorld->GetTimeSeconds();
			}
		}
	}

	LastUpdateLocation = NewLocation;
	LastUpdateRotation = NewRotation;
	LastUpdateVelocity = Velocity;
}

void UNinjaCharacterMovementComponent::Crouch(bool bClientSimulation)
{
	if (!HasValidData())
	{
		return;
	}

	if (!bClientSimulation && !CanCrouchInCurrentState())
	{
		return;
	}

	// See if collision is already at desired size
	const float CurrentCrouchedHalfHeight = GetCrouchedHalfHeight();
	if (CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() == CurrentCrouchedHalfHeight)
	{
		if (!bClientSimulation)
		{
			CharacterOwner->bIsCrouched = true;
		}
		CharacterOwner->OnStartCrouch(0.0f, 0.0f);
		return;
	}

	if (bClientSimulation && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		// Restore collision size before crouching
		ACharacter* DefaultCharacter = CharacterOwner->GetClass()->GetDefaultObject<ACharacter>();
		CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight());
		bShrinkProxyCapsule = true;
	}

	// Change collision size to crouching dimensions
	const float ComponentScale = CharacterOwner->GetCapsuleComponent()->GetShapeScale();
	const float OldUnscaledHalfHeight = CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	const float OldUnscaledRadius = CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleRadius();
	// Height is not allowed to be smaller than radius
	const float ClampedCrouchedHalfHeight = FMath::Max3(0.0f, OldUnscaledRadius, CurrentCrouchedHalfHeight);
	CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(OldUnscaledRadius, ClampedCrouchedHalfHeight);
	float HalfHeightAdjust = (OldUnscaledHalfHeight - ClampedCrouchedHalfHeight);
	float ScaledHalfHeightAdjust = HalfHeightAdjust * ComponentScale;

	if (!bClientSimulation)
	{
		const FVector CapsuleDown = GetComponentAxisZ() * -1.0f;

		// Crouching to a larger height? (this is rare)
		if (ClampedCrouchedHalfHeight > OldUnscaledHalfHeight)
		{
			FCollisionQueryParams CapsuleParams(SCENE_QUERY_STAT(CrouchTrace), false, CharacterOwner);
			FCollisionResponseParams ResponseParam;
			InitCollisionParams(CapsuleParams, ResponseParam);
			const bool bEncroached = GetWorld()->OverlapBlockingTestByChannel(UpdatedComponent->GetComponentLocation() + CapsuleDown * ScaledHalfHeightAdjust,
				UpdatedComponent->GetComponentQuat(), UpdatedComponent->GetCollisionObjectType(), GetPawnCapsuleCollisionShape(SHRINK_None), CapsuleParams, ResponseParam);

			// If encroached, cancel
			if (bEncroached)
			{
				CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(OldUnscaledRadius, OldUnscaledHalfHeight);
				return;
			}
		}

		if (bCrouchMaintainsBaseLocation)
		{
			// Intentionally not using MoveUpdatedComponent, where a horizontal plane constraint would prevent the base of the capsule from staying at the same spot
			UpdatedComponent->MoveComponent(CapsuleDown * ScaledHalfHeightAdjust, UpdatedComponent->GetComponentQuat(), true, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
		}

		CharacterOwner->bIsCrouched = true;
	}

	bForceNextFloorCheck = true;

	// OnStartCrouch takes the change from the Default size, not the current one (though they are usually the same)
	const float MeshAdjust = ScaledHalfHeightAdjust;
	ACharacter* DefaultCharacter = CharacterOwner->GetClass()->GetDefaultObject<ACharacter>();
	HalfHeightAdjust = (DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() - ClampedCrouchedHalfHeight);
	ScaledHalfHeightAdjust = HalfHeightAdjust * ComponentScale;

	AdjustProxyCapsuleSize();
	CharacterOwner->OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

	// Don't smooth this change in mesh position
	if ((bClientSimulation && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy) || (IsNetMode(NM_ListenServer) && CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy))
	{
		FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
		if (ClientData)
		{
			ClientData->MeshTranslationOffset -= GetComponentAxisZ() * MeshAdjust;
			ClientData->OriginalMeshTranslationOffset = ClientData->MeshTranslationOffset;
		}
	}
}

void UNinjaCharacterMovementComponent::UnCrouch(bool bClientSimulation)
{
	if (!HasValidData())
	{
		return;
	}

	ACharacter* DefaultCharacter = CharacterOwner->GetClass()->GetDefaultObject<ACharacter>();

	// See if collision is already at desired size
	if (CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() == DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight())
	{
		if (!bClientSimulation)
		{
			CharacterOwner->bIsCrouched = false;
		}
		CharacterOwner->OnEndCrouch(0.0f, 0.0f);
		return;
	}

	const float CurrentCrouchedHalfHeight = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

	const float ComponentScale = CharacterOwner->GetCapsuleComponent()->GetShapeScale();
	const float OldUnscaledHalfHeight = CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	const float HalfHeightAdjust = DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() - OldUnscaledHalfHeight;
	const float ScaledHalfHeightAdjust = HalfHeightAdjust * ComponentScale;
	const FVector PawnLocation = UpdatedComponent->GetComponentLocation();

	// Grow to uncrouched size
	check(CharacterOwner->GetCapsuleComponent());

	if (!bClientSimulation)
	{
		// Try to stay in place and see if the larger capsule fits. We use a slightly taller capsule to avoid penetration
		const UWorld* MyWorld = GetWorld();
		const float SweepInflation = UE_KINDA_SMALL_NUMBER * 10.0f;
		const FQuat PawnRotation = UpdatedComponent->GetComponentQuat();
		const FVector CapsuleDown = GetComponentAxisZ() * -1.0f;
		FCollisionQueryParams CapsuleParams(SCENE_QUERY_STAT(CrouchTrace), false, CharacterOwner);
		FCollisionResponseParams ResponseParam;
		InitCollisionParams(CapsuleParams, ResponseParam);

		// Compensate for the difference between current capsule size and standing size
		const FCollisionShape StandingCapsuleShape = GetPawnCapsuleCollisionShape(SHRINK_HeightCustom, -SweepInflation - ScaledHalfHeightAdjust); // Shrink by negative amount, so actually grow it
		const ECollisionChannel CollisionChannel = UpdatedComponent->GetCollisionObjectType();
		bool bEncroached = true;

		if (!bCrouchMaintainsBaseLocation)
		{
			// Expand in place
			bEncroached = MyWorld->OverlapBlockingTestByChannel(PawnLocation, PawnRotation, CollisionChannel, StandingCapsuleShape, CapsuleParams, ResponseParam);

			if (bEncroached)
			{
				// Try adjusting capsule position to see if we can avoid encroachment
				if (ScaledHalfHeightAdjust > 0.0f)
				{
					// Shrink to a short capsule, sweep down to base to find where that would hit something, and then try to stand up from there
					float PawnRadius, PawnHalfHeight;
					CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleSize(PawnRadius, PawnHalfHeight);
					const float ShrinkHalfHeight = PawnHalfHeight - PawnRadius;
					const float TraceDist = PawnHalfHeight - ShrinkHalfHeight;

					FHitResult Hit(1.0f);
					const FCollisionShape ShortCapsuleShape = GetPawnCapsuleCollisionShape(SHRINK_HeightCustom, ShrinkHalfHeight);
					const bool bBlockingHit = MyWorld->SweepSingleByChannel(Hit, PawnLocation, PawnLocation + CapsuleDown * TraceDist, PawnRotation, CollisionChannel, ShortCapsuleShape, CapsuleParams);
					if (Hit.bStartPenetrating)
					{
						bEncroached = true;
					}
					else
					{
						// Compute where the base of the sweep ended up, and see if we can stand there
						const float DistanceToBase = (Hit.Time * TraceDist) + ShortCapsuleShape.Capsule.HalfHeight;
						const FVector NewLoc = PawnLocation - CapsuleDown * (-DistanceToBase + StandingCapsuleShape.Capsule.HalfHeight + SweepInflation + MIN_FLOOR_DIST / 2.0f);
						bEncroached = MyWorld->OverlapBlockingTestByChannel(NewLoc, PawnRotation, CollisionChannel, StandingCapsuleShape, CapsuleParams, ResponseParam);
						if (!bEncroached)
						{
							// Intentionally not using MoveUpdatedComponent, where a horizontal plane constraint would prevent the base of the capsule from staying at the same spot
							UpdatedComponent->MoveComponent(NewLoc - PawnLocation, PawnRotation, false, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
						}
					}
				}
			}
		}
		else
		{
			// Expand while keeping base location the same
			FVector StandingLocation = PawnLocation - CapsuleDown * (StandingCapsuleShape.GetCapsuleHalfHeight() - CurrentCrouchedHalfHeight);
			bEncroached = MyWorld->OverlapBlockingTestByChannel(StandingLocation, PawnRotation, CollisionChannel, StandingCapsuleShape, CapsuleParams, ResponseParam);

			if (bEncroached)
			{
				if (IsMovingOnGround())
				{
					// Something might be just barely overhead, try moving down closer to the floor to avoid it
					const float MinFloorDist = UE_KINDA_SMALL_NUMBER * 10.0f;
					if (CurrentFloor.bBlockingHit && CurrentFloor.FloorDist > MinFloorDist)
					{
						StandingLocation += CapsuleDown * (CurrentFloor.FloorDist - MinFloorDist);
						bEncroached = MyWorld->OverlapBlockingTestByChannel(StandingLocation, PawnRotation, CollisionChannel, StandingCapsuleShape, CapsuleParams, ResponseParam);
					}
				}
			}

			if (!bEncroached)
			{
				// Commit the change in location
				UpdatedComponent->MoveComponent(StandingLocation - PawnLocation, PawnRotation, false, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
				bForceNextFloorCheck = true;
			}
		}

		// If still encroached then abort
		if (bEncroached)
		{
			return;
		}

		CharacterOwner->bIsCrouched = false;
	}
	else
	{
		bShrinkProxyCapsule = true;
	}

	// Now call SetCapsuleSize() to cause touch/untouch events and actually grow the capsule
	CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(), true);

	const float MeshAdjust = ScaledHalfHeightAdjust;
	AdjustProxyCapsuleSize();
	CharacterOwner->OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

	// Don't smooth this change in mesh position
	if ((bClientSimulation && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy) || (IsNetMode(NM_ListenServer) && CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy))
	{
		FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
		if (ClientData)
		{
			ClientData->MeshTranslationOffset += GetComponentAxisZ() * MeshAdjust;
			ClientData->OriginalMeshTranslationOffset = ClientData->MeshTranslationOffset;
		}
	}
}

float UNinjaCharacterMovementComponent::SlideAlongSurface(const FVector& Delta, float Time, const FVector& InNormal, FHitResult& Hit, bool bHandleImpact)
{
	if (!Hit.bBlockingHit)
	{
		return 0.0f;
	}

	FVector NewNormal(InNormal);
	if (IsMovingOnGround())
	{
		const FVector CapsuleUp = GetComponentAxisZ();
		const float Dot = NewNormal | CapsuleUp;

		// We don't want to be pushed up an unwalkable surface
		if (Dot > 0.0f)
		{
			if (!IsWalkable(Hit))
			{
				NewNormal = FVector::VectorPlaneProject(NewNormal, CapsuleUp).GetSafeNormal();
			}
		}
		else if (Dot < -UE_KINDA_SMALL_NUMBER)
		{
			// Don't push down into the floor when the impact is on the upper portion of the capsule
			if (CurrentFloor.FloorDist < MIN_FLOOR_DIST && CurrentFloor.bBlockingHit)
			{
				const FVector FloorNormal = CurrentFloor.HitResult.Normal;
				const bool bFloorOpposedToMovement = (Delta | FloorNormal) < 0.0f && (FloorNormal | CapsuleUp) < 1.0f - UE_DELTA;
				if (bFloorOpposedToMovement)
				{
					NewNormal = FloorNormal;
				}

				NewNormal = FVector::VectorPlaneProject(NewNormal, CapsuleUp).GetSafeNormal();
			}
		}
	}

	return UPawnMovementComponent::SlideAlongSurface(Delta, Time, NewNormal, Hit, bHandleImpact);
}

void UNinjaCharacterMovementComponent::TwoWallAdjust(FVector& WorldSpaceDelta, const FHitResult& Hit, const FVector& OldHitNormal) const
{
	UPawnMovementComponent::TwoWallAdjust(WorldSpaceDelta, Hit, OldHitNormal);

	if (IsMovingOnGround())
	{
		const FVector CapsuleUp = GetComponentAxisZ();
		const float DotDelta = WorldSpaceDelta | CapsuleUp;

		// Allow slides up walkable surfaces, but not unwalkable ones (treat those as vertical barriers)
		if (DotDelta > 0.0f)
		{
			const float DotHitNormal = Hit.Normal | CapsuleUp;

			if (DotHitNormal > UE_KINDA_SMALL_NUMBER && (DotHitNormal >= GetWalkableFloorZ() || IsWalkable(Hit)))
			{
				// Maintain horizontal velocity
				const float Time = (1.0f - Hit.Time);
				const FVector ScaledDelta = WorldSpaceDelta.GetSafeNormal() * WorldSpaceDelta.Size();
				WorldSpaceDelta = (FVector::VectorPlaneProject(WorldSpaceDelta, CapsuleUp) + CapsuleUp * ((ScaledDelta | CapsuleUp) / DotHitNormal)) * Time;

				// Should never exceed MaxStepHeight in vertical component, so rescale if necessary
				// This should be rare (Hit.Normal.Z above would have been very small) but we'd rather lose horizontal velocity than go too high
				const float DeltaZ = WorldSpaceDelta | CapsuleUp;
				if (DeltaZ > MaxStepHeight)
				{
					const float Rescale = MaxStepHeight / DeltaZ;
					WorldSpaceDelta *= Rescale;
				}
			}
			else
			{
				WorldSpaceDelta = FVector::VectorPlaneProject(WorldSpaceDelta, CapsuleUp);
			}
		}
		else if (DotDelta < 0.0f)
		{
			// Don't push down into the floor
			if (CurrentFloor.FloorDist < MIN_FLOOR_DIST && CurrentFloor.bBlockingHit)
			{
				WorldSpaceDelta = FVector::VectorPlaneProject(WorldSpaceDelta, CapsuleUp);
			}
		}
	}
}

FVector UNinjaCharacterMovementComponent::HandleSlopeBoosting(const FVector& SlideResult, const FVector& Delta, const float Time, const FVector& Normal, const FHitResult& Hit) const
{
	const FVector CapsuleUp = GetComponentAxisZ();
	FVector Result = SlideResult;
	const float Dot = Result | CapsuleUp;

	// Prevent boosting up slopes
	if (Dot > 0.0f)
	{
		// Don't move any higher than we originally intended
		const float ZLimit = (Delta | CapsuleUp) * Time;
		if (Dot - ZLimit > UE_KINDA_SMALL_NUMBER)
		{
			if (ZLimit > 0.0f)
			{
				// Rescale the entire vector (not just the Z component) otherwise we change the direction and likely head right back into the impact
				const float UpPercent = ZLimit / Dot;
				Result *= UpPercent;
			}
			else
			{
				// We were heading down but were going to deflect upwards. Just make the deflection horizontal
				Result = FVector::ZeroVector;
			}

			// Make remaining portion of original result horizontal and parallel to impact normal
			const FVector RemainderXY = FVector::VectorPlaneProject(SlideResult - Result, CapsuleUp);
			const FVector NormalXY = FVector::VectorPlaneProject(Normal, CapsuleUp).GetSafeNormal();
			const FVector Adjust = UPawnMovementComponent::ComputeSlideVector(RemainderXY, 1.0f, NormalXY, Hit);
			Result += Adjust;
		}
	}

	return Result;
}

float UNinjaCharacterMovementComponent::ImmersionDepth() const
{
	float Depth = 0.0f;

	if (CharacterOwner && GetPhysicsVolume()->bWaterVolume)
	{
		const float CollisionHalfHeight = CharacterOwner->GetSimpleCollisionHalfHeight();

		if (CollisionHalfHeight == 0.0f || Buoyancy == 0.0f)
		{
			Depth = 1.0f;
		}
		else
		{
			UBrushComponent* VolumeBrushComp = GetPhysicsVolume()->GetBrushComponent();
			FHitResult Hit(1.0f);
			if (VolumeBrushComp)
			{
				const FVector CapsuleHalfHeight = GetComponentAxisZ() * CollisionHalfHeight;
				const FVector TraceStart = UpdatedComponent->GetComponentLocation() + CapsuleHalfHeight;
				const FVector TraceEnd = UpdatedComponent->GetComponentLocation() - CapsuleHalfHeight;

				FCollisionQueryParams NewTraceParams(SCENE_QUERY_STAT(ImmersionDepth), true);
				VolumeBrushComp->LineTraceComponent(Hit, TraceStart, TraceEnd, NewTraceParams);
			}

			Depth = (Hit.Time == 1.0f) ? 1.0f : (1.0f - Hit.Time);
		}
	}

	return Depth;
}

void UNinjaCharacterMovementComponent::RequestDirectMove(const FVector& MoveVelocity, bool bForceMaxSpeed)
{
	if (MoveVelocity.SizeSquared() < UE_KINDA_SMALL_NUMBER)
	{
		return;
	}

	if (ShouldPerformAirControlForPathFollowing())
	{
		const FVector FallVelocity = MoveVelocity.GetClampedToMaxSize(GetMaxSpeed());
		const FVector GravityDir = GetGravityDirection(false);
		if (!GravityDir.IsZero())
		{
			PerformAirControlForPathFollowingEx(FallVelocity, GravityDir);
		}

		return;
	}

	RequestedVelocity = MoveVelocity;
	bHasRequestedVelocity = true;
	bRequestedMoveWithMaxSpeed = bForceMaxSpeed;

	if (IsMovingOnGround())
	{
		RequestedVelocity = FVector::VectorPlaneProject(RequestedVelocity, GetComponentAxisZ());
	}
}

void UNinjaCharacterMovementComponent::RequestPathMove(const FVector& MoveInput)
{
	FVector AdjustedMoveInput = MoveInput;

	// Preserve magnitude when moving on ground/falling and requested input has vertical component; see ConstrainInputAcceleration for details
	if (IsMovingOnGround())
	{
		AdjustedMoveInput = FVector::VectorPlaneProject(MoveInput, GetComponentAxisZ()).GetSafeNormal() * MoveInput.Size();
	}
	else if (IsFalling())
	{
		const FVector GravDir = GetGravityDirection(false);
		if (!GravDir.IsZero())
		{
			AdjustedMoveInput = FVector::VectorPlaneProject(MoveInput, GravDir).GetSafeNormal() * MoveInput.Size();
		}
	}

	UPawnMovementComponent::RequestPathMove(AdjustedMoveInput);
}

float UNinjaCharacterMovementComponent::GetMaxJumpHeight() const
{
	const float GravityMagnitude = GetGravityMagnitude();
	if (GravityMagnitude > UE_KINDA_SMALL_NUMBER)
	{
		return FMath::Square(JumpZVelocity) / (2.0f * GravityMagnitude);
	}
	else
	{
		return 0.0f;
	}
}

void UNinjaCharacterMovementComponent::PhysFlying(float deltaTime, int32 Iterations)
{
	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	// Abort if no valid gravity can be obtained
	const FVector GravDir = GetGravityDirection(false);
	if (GravDir.IsZero())
	{
		Acceleration = FVector::ZeroVector;
		Velocity = FVector::ZeroVector;
		return;
	}

	RestorePreAdditiveRootMotionVelocity();

	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		if (bCheatFlying && Acceleration.IsZero())
		{
			Velocity = FVector::ZeroVector;
		}
		const float Friction = 0.5f * GetPhysicsVolume()->FluidFriction;
		CalcVelocity(deltaTime, Friction, true, GetMaxBrakingDeceleration());
	}

	ApplyRootMotionToVelocity(deltaTime);

	Iterations++;
	bJustTeleported = false;

	FVector OldLocation = UpdatedComponent->GetComponentLocation();
	const FVector Adjusted = Velocity * deltaTime;
	FHitResult Hit(1.0f);
	SafeMoveUpdatedComponent(Adjusted, UpdatedComponent->GetComponentQuat(), true, Hit);

	if (Hit.Time < 1.0f)
	{
		const float UpDown = GravDir | Velocity.GetSafeNormal();
		bool bSteppedUp = false;

		if (UpDown < 0.5f && UpDown > -0.2f && FMath::Abs(Hit.ImpactNormal | GravDir) < 0.2f && CanStepUp(Hit))
		{
			const FVector StepLocation = UpdatedComponent->GetComponentLocation();

			bSteppedUp = StepUp(GravDir, Adjusted * (1.0f - Hit.Time), Hit);
			if (bSteppedUp)
			{
				OldLocation += GravDir * ((UpdatedComponent->GetComponentLocation() - StepLocation) | GravDir);
			}
		}

		if (!bSteppedUp)
		{
			// Adjust and try again
			HandleImpact(Hit, deltaTime, Adjusted);
			SlideAlongSurface(Adjusted, (1.0f - Hit.Time), Hit.Normal, Hit, true);
		}
	}

	if (!bJustTeleported && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / deltaTime;
	}
}

void UNinjaCharacterMovementComponent::ApplyRootMotionToVelocity(float deltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_CharacterMovementRootMotionSourceApply);

	// Animation root motion is distinct from root motion sources right now and takes precedence
	if (HasAnimRootMotion() && deltaTime > 0.0f)
	{
		Velocity = ConstrainAnimRootMotionVelocity(AnimRootMotionVelocity, Velocity);
		if (IsFalling())
		{
			Velocity += FVector::VectorPlaneProject(DecayingFormerBaseVelocity, GetGravityDirection(true));
		}
		return;
	}

	const FVector OldVelocity = Velocity;

	bool bAppliedRootMotion = false;

	// Apply override velocity
	if (CurrentRootMotion.HasOverrideVelocity())
	{
		CurrentRootMotion.AccumulateOverrideRootMotionVelocity(deltaTime, *CharacterOwner, *this, Velocity);
		if (IsFalling())
		{
			Velocity += CurrentRootMotion.HasOverrideVelocityWithIgnoreZAccumulate() ? FVector::VectorPlaneProject(DecayingFormerBaseVelocity, GetGravityDirection(true)) : DecayingFormerBaseVelocity;
		}
		bAppliedRootMotion = true;

#if ROOT_MOTION_DEBUG
		if (RootMotionSourceDebug::CVarDebugRootMotionSources.GetValueOnGameThread() == 1)
		{
			FString AdjustedDebugString = FString::Printf(TEXT("ApplyRootMotionToVelocity HasOverrideVelocity Velocity(%s)"),
				*Velocity.ToCompactString());
			RootMotionSourceDebug::PrintOnScreen(*CharacterOwner, AdjustedDebugString);
		}
#endif
	}

	// Next apply additive root motion
	if (CurrentRootMotion.HasAdditiveVelocity())
	{
		CurrentRootMotion.LastPreAdditiveVelocity = Velocity; // Save off pre-additive velocity for restoration next tick
		CurrentRootMotion.AccumulateAdditiveRootMotionVelocity(deltaTime, *CharacterOwner, *this, Velocity);
		CurrentRootMotion.bIsAdditiveVelocityApplied = true; // Remember that we have it applied
		bAppliedRootMotion = true;

#if ROOT_MOTION_DEBUG
		if (RootMotionSourceDebug::CVarDebugRootMotionSources.GetValueOnGameThread() == 1)
		{
			FString AdjustedDebugString = FString::Printf(TEXT("ApplyRootMotionToVelocity HasAdditiveVelocity Velocity(%s) LastPreAdditiveVelocity(%s)"),
				*Velocity.ToCompactString(), *CurrentRootMotion.LastPreAdditiveVelocity.ToCompactString());
			RootMotionSourceDebug::PrintOnScreen(*CharacterOwner, AdjustedDebugString);
		}
#endif
	}

	// Switch to falling if we have vertical velocity from root motion so we can lift off the ground
	if (bAppliedRootMotion && IsMovingOnGround())
	{
		const float AppliedVelocityDeltaZ = (Velocity - OldVelocity) | GetComponentAxisZ();

		if (AppliedVelocityDeltaZ > 0.0f)
		{
			float LiftoffBound;
			if (CurrentRootMotion.LastAccumulatedSettings.HasFlag(ERootMotionSourceSettingsFlags::UseSensitiveLiftoffCheck))
			{
				// Sensitive bounds - "any positive force"
				LiftoffBound = UE_SMALL_NUMBER;
			}
			else
			{
				// Default bounds - the amount of force gravity is applying this tick
				LiftoffBound = FMath::Max(GetGravityMagnitude() * deltaTime, UE_SMALL_NUMBER);
			}

			if (AppliedVelocityDeltaZ > LiftoffBound)
			{
				SetMovementMode(MOVE_Falling);
			}
		}
	}
}

void UNinjaCharacterMovementComponent::PhysSwimming(float deltaTime, int32 Iterations)
{
	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	// Abort if no valid gravity can be obtained
	const FVector GravityDir = GetGravityDirection(false);
	if (GravityDir.IsZero())
	{
		Acceleration = FVector::ZeroVector;
		Velocity = FVector::ZeroVector;
		return;
	}

	RestorePreAdditiveRootMotionVelocity();

	FVector::FReal VelocityZ = (Velocity | GravityDir) * -1.0f;
	const FVector::FReal AccelerationZ = (Acceleration | GravityDir) * -1.0f;
	const float Depth = ImmersionDepth();
	const float NetBuoyancy = Buoyancy * Depth;
	const float OriginalAccelZ = AccelerationZ;
	bool bLimitedUpAccel = false;

	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && VelocityZ > 0.33f * MaxSwimSpeed && NetBuoyancy != 0.0f)
	{
		// Damp velocity out of water
		Velocity = FVector::VectorPlaneProject(Velocity, GravityDir) + GravityDir * (FMath::Max<FVector::FReal>(0.33f * MaxSwimSpeed, VelocityZ * Depth * Depth) * -1.0f);
	}
	else if (Depth < 0.65f)
	{
		bLimitedUpAccel = (AccelerationZ > 0.0f);
		Acceleration = FVector::VectorPlaneProject(Acceleration, GravityDir) + GravityDir * (FMath::Min<FVector::FReal>(0.1f, AccelerationZ) * -1.0f);
	}

	Iterations++;
	FVector OldLocation = UpdatedComponent->GetComponentLocation();
	bJustTeleported = false;

	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		const float Friction = 0.5f * GetPhysicsVolume()->FluidFriction * Depth;
		CalcVelocity(deltaTime, Friction, true, GetMaxBrakingDeceleration());
		Velocity += GetGravity() * (deltaTime * (1.0f - NetBuoyancy));
	}

	ApplyRootMotionToVelocity(deltaTime);

	FVector Adjusted = Velocity * deltaTime;
	FHitResult Hit(1.0f);
	const float remainingTime = deltaTime * Swim(Adjusted, Hit);

	// May have left water - if so, script might have set new physics mode
	if (!IsSwimming())
	{
		StartNewPhysics(remainingTime, Iterations);
		return;
	}

	if (Hit.Time < 1.0f && CharacterOwner)
	{
		HandleSwimmingWallHit(Hit, deltaTime);
		VelocityZ = (Velocity | GravityDir) * -1.0f;
		if (bLimitedUpAccel && VelocityZ >= 0.0f)
		{
			// Allow upward velocity at surface if against obstacle
			Velocity = FVector::VectorPlaneProject(Velocity, GravityDir) + GravityDir * ((VelocityZ + OriginalAccelZ * deltaTime) * -1.0f);
			Adjusted = Velocity * (1.0f - Hit.Time) * deltaTime;
			Swim(Adjusted, Hit);
			if (!IsSwimming())
			{
				StartNewPhysics(remainingTime, Iterations);
				return;
			}
		}

		const float UpDown = GravityDir | Velocity.GetSafeNormal();
		bool bSteppedUp = false;

		if (UpDown < 0.5f && UpDown > -0.2f && FMath::Abs(Hit.ImpactNormal | GravityDir) < 0.2f && CanStepUp(Hit))
		{
			const FVector StepLocation = UpdatedComponent->GetComponentLocation();
			const FVector RealVelocity = Velocity;
			Velocity = FVector::VectorPlaneProject(Velocity, GravityDir) - GravityDir; // HACK: since will be moving up, in case pawn leaves the water

			bSteppedUp = StepUp(GravityDir, Adjusted * (1.0f - Hit.Time), Hit);
			if (bSteppedUp)
			{
				// May have left water; if so, script might have set new physics mode
				if (!IsSwimming())
				{
					StartNewPhysics(remainingTime, Iterations);
					return;
				}

				OldLocation += GravityDir * ((UpdatedComponent->GetComponentLocation() - StepLocation) | GravityDir);
			}

			Velocity = RealVelocity;
		}

		if (!bSteppedUp)
		{
			// Adjust and try again
			HandleImpact(Hit, deltaTime, Adjusted);
			SlideAlongSurface(Adjusted, 1.0f - Hit.Time, Hit.Normal, Hit, true);
		}
	}

	if (CharacterOwner && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && !bJustTeleported && (deltaTime - remainingTime) > UE_KINDA_SMALL_NUMBER)
	{
		const FVector::FReal VelZ = Velocity | GravityDir;
		Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / (deltaTime - remainingTime);

		if (!GetPhysicsVolume()->bWaterVolume)
		{
			Velocity = FVector::VectorPlaneProject(Velocity, GravityDir) + GravityDir * VelZ;
		}
	}

	if (!GetPhysicsVolume()->bWaterVolume && IsSwimming())
	{
		SetMovementMode(MOVE_Falling); // In case script didn't change it (w/ zone change)
	}

	// May have left water - if so, script might have set new physics mode
	if (!IsSwimming())
	{
		StartNewPhysics(remainingTime, Iterations);
	}
}

void UNinjaCharacterMovementComponent::StartSwimming(FVector OldLocation, FVector OldVelocity, float timeTick, float remainingTime, int32 Iterations)
{
	if (remainingTime < MIN_TICK_TIME || timeTick < MIN_TICK_TIME)
	{
		return;
	}

	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && !bJustTeleported)
	{
		Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / timeTick; // Actual average velocity
		Velocity = 2.0f * Velocity - OldVelocity; // End velocity has 2x accel of avg
		Velocity = Velocity.GetClampedToMaxSize(GetPhysicsVolume()->TerminalVelocity);
	}

	const FVector End = FindWaterLine(UpdatedComponent->GetComponentLocation(), OldLocation);
	float waterTime = 0.0f;
	if (End != UpdatedComponent->GetComponentLocation())
	{
		const float ActualDist = (UpdatedComponent->GetComponentLocation() - OldLocation).Size();
		if (ActualDist > UE_KINDA_SMALL_NUMBER)
		{
			waterTime = timeTick * (End - UpdatedComponent->GetComponentLocation()).Size() / ActualDist;
			remainingTime += waterTime;
		}

		MoveUpdatedComponent(End - UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentQuat(), true);
	}

	const FVector GravityDir = GetGravityDirection(false);
	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && !GravityDir.IsZero())
	{
		const float Dot = Velocity | GravityDir;
		if (Dot > 0.0f && Dot < NinjaCharacterMovementConstants::SWIMBOBSPEED * -2.0f)
		{
			// Apply smooth bobbing
			const FVector Velocity2D = FVector::VectorPlaneProject(Velocity, GravityDir);
			Velocity = Velocity2D + GravityDir * ((NinjaCharacterMovementConstants::SWIMBOBSPEED - Velocity2D.Size() * 0.7f) * -1.0f);
		}
	}

	if (remainingTime >= MIN_TICK_TIME && Iterations < MaxSimulationIterations)
	{
		PhysSwimming(remainingTime, Iterations);
	}
}

FVector UNinjaCharacterMovementComponent::GetFallingLateralAcceleration(float DeltaTime)
{
	return GetFallingLateralAccelerationEx(DeltaTime, GetGravityDirection(true));
}

FVector UNinjaCharacterMovementComponent::GetFallingLateralAccelerationEx(float DeltaTime, const FVector& GravDir) const
{
	// No vertical acceleration
	FVector FallAcceleration = FVector::VectorPlaneProject(Acceleration, GravDir);

	// Bound acceleration, falling object has minimal ability to impact acceleration
	if (!HasAnimRootMotion() && FallAcceleration.SizeSquared() > 0.0f)
	{
		FallAcceleration = GetAirControlEx(DeltaTime, AirControl, FallAcceleration, GravDir);
		FallAcceleration = FallAcceleration.GetClampedToMaxSize(GetMaxAcceleration());
	}

	return FallAcceleration;
}

bool UNinjaCharacterMovementComponent::ShouldLimitAirControl(float DeltaTime, const FVector& FallAcceleration) const
{
	return (FallAcceleration.SizeSquared() > 0.0f);
}

FVector UNinjaCharacterMovementComponent::GetAirControl(float DeltaTime, float TickAirControl, const FVector& FallAcceleration)
{
	return GetAirControlEx(DeltaTime, TickAirControl, FallAcceleration, GetGravityDirection(true));
}

FVector UNinjaCharacterMovementComponent::GetAirControlEx(float DeltaTime, float TickAirControl, const FVector& FallAcceleration, const FVector& GravDir) const
{
	// Boost
	if (TickAirControl != 0.0f)
	{
		TickAirControl = BoostAirControlEx(DeltaTime, TickAirControl, FallAcceleration, GravDir);
	}

	return TickAirControl * FallAcceleration;
}

void UNinjaCharacterMovementComponent::SetGravityDirection(const FVector& GravDir)
{
	// Disabled
}

float UNinjaCharacterMovementComponent::BoostAirControl(float DeltaTime, float TickAirControl, const FVector& FallAcceleration)
{
	return BoostAirControlEx(DeltaTime, TickAirControl, FallAcceleration, GetGravityDirection(true));
}

float UNinjaCharacterMovementComponent::BoostAirControlEx(float DeltaTime, float TickAirControl, const FVector& FallAcceleration, const FVector& GravDir) const
{
	// Allow a burst of initial acceleration
	if (AirControlBoostMultiplier > 0.0f && FVector::VectorPlaneProject(Velocity, GravDir).SizeSquared() < FMath::Square(AirControlBoostVelocityThreshold))
	{
		TickAirControl = FMath::Min(1.0f, AirControlBoostMultiplier * TickAirControl);
	}

	return TickAirControl;
}

void UNinjaCharacterMovementComponent::PhysFalling(float deltaTime, int32 Iterations)
{
	SCOPE_CYCLE_COUNTER(STAT_CharPhysFalling);

	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	// Abort if no valid gravity can be obtained
	const FVector GravityDir = GetGravityDirection(false);
	if (GravityDir.IsZero())
	{
		Acceleration = FVector::ZeroVector;
		Velocity = FVector::ZeroVector;
		return;
	}

	FVector FallAcceleration = GetFallingLateralAccelerationEx(deltaTime, GravityDir);
	const bool bHasLimitedAirControl = ShouldLimitAirControl(deltaTime, FallAcceleration);

	float RemainingTime = deltaTime;
	while (RemainingTime >= MIN_TICK_TIME && Iterations < MaxSimulationIterations)
	{
		Iterations++;
		float timeTick = GetSimulationTimeStep(RemainingTime, Iterations);
		RemainingTime -= timeTick;

		const FVector OldLocation = UpdatedComponent->GetComponentLocation();
		const FQuat PawnRotation = UpdatedComponent->GetComponentQuat();
		bJustTeleported = false;

		const FVector OldVelocityWithRootMotion = Velocity;
		const FVector::FReal OldVelocityWithRootMotionZ = (OldVelocityWithRootMotion | GravityDir) * -1.0f;

		RestorePreAdditiveRootMotionVelocity();

		const FVector OldVelocity = Velocity;
		const FVector OldVelocityZ = GravityDir * (OldVelocity | GravityDir);

		// Apply input
		const float MaxDecel = GetMaxBrakingDeceleration();
		if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
		{
			// Compute Velocity
			{
				// Acceleration = FallAcceleration for CalcVelocity(), but we restore it after using it
				TGuardValue<FVector> RestoreAcceleration(Acceleration, FallAcceleration);

				Velocity = FVector::VectorPlaneProject(Velocity, GravityDir);
				CalcVelocity(timeTick, FallingLateralFriction, false, MaxDecel);
				Velocity = FVector::VectorPlaneProject(Velocity, GravityDir) + OldVelocityZ;
			}
		}

		// Compute current gravity
		const FVector Gravity = GetGravity();
		float GravityTime = timeTick;

		// If jump is providing force, gravity may be affected
		bool bEndingJumpForce = false;
		if (CharacterOwner->JumpForceTimeRemaining > 0.0f)
		{
			// Consume some of the force time. Only the remaining time (if any) is affected by gravity when bApplyGravityWhileJumping=false
			const float JumpForceTime = FMath::Min(CharacterOwner->JumpForceTimeRemaining, timeTick);
			GravityTime = bApplyGravityWhileJumping ? timeTick : FMath::Max(0.0f, timeTick - JumpForceTime);

			// Update Character state
			CharacterOwner->JumpForceTimeRemaining -= JumpForceTime;
			if (CharacterOwner->JumpForceTimeRemaining <= 0.0f)
			{
				CharacterOwner->ResetJumpState();
				bEndingJumpForce = true;
			}
		}

		// Apply gravity
		Velocity = NewFallVelocity(Velocity, Gravity, GravityTime);
		FVector::FReal VelocityZ = (Velocity | GravityDir) * -1.0f;

		//UE_LOG(LogCharacterMovement, Log, TEXT("dt=(%.6f) OldLocation=(%s) OldVelocity=(%s) OldVelocityWithRootMotion=(%s) NewVelocity=(%s)"), timeTick, *(UpdatedComponent->GetComponentLocation()).ToString(), *OldVelocity.ToString(), *OldVelocityWithRootMotion.ToString(), *Velocity.ToString());
		ApplyRootMotionToVelocity(timeTick);
		DecayFormerBaseVelocity(timeTick);

		// See if we need to sub-step to exactly reach the apex
		// This is important for avoiding "cutting off the top" of the trajectory as framerate varies
		static IConsoleVariable* ForceJumpPeakSubstepVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ForceJumpPeakSubstep"));
		if ((ForceJumpPeakSubstepVar == nullptr || ForceJumpPeakSubstepVar->GetInt()) && OldVelocityWithRootMotionZ > 0.0f && VelocityZ <= 0.0f && NumJumpApexAttempts < MaxJumpApexAttemptsPerSimulation)
		{
			const FVector DerivedAccel = (Velocity - OldVelocityWithRootMotion) / timeTick;
			const FVector::FReal DerivedAccelZ = (DerivedAccel | GravityDir) * -1.0f;
			if (!FMath::IsNearlyZero(DerivedAccelZ))
			{
				const float TimeToApex = -OldVelocityWithRootMotionZ / DerivedAccelZ;

				// The time-to-apex calculation should be precise, and we want to avoid adding a substep when we are basically already at the apex from the previous iteration's work
				const float ApexTimeMinimum = 0.0001f;
				if (TimeToApex >= ApexTimeMinimum && TimeToApex < timeTick)
				{
					const FVector ApexVelocity = OldVelocityWithRootMotion + (DerivedAccel * TimeToApex);
					Velocity = FVector::VectorPlaneProject(ApexVelocity, GravityDir); // ApexVelocity.Z should be nearly zero anyway, but this makes apex notifications consistent
					VelocityZ = 0.0f;

					// We only want to move the amount of time it takes to reach the apex, and refund the unused time for next iteration
					const float TimeToRefund = (timeTick - TimeToApex);

					RemainingTime += TimeToRefund;
					timeTick = TimeToApex;
					Iterations--;
					NumJumpApexAttempts++;

					// Refund time to any active Root Motion Sources as well
					for (TSharedPtr<FRootMotionSource> RootMotionSource : CurrentRootMotion.RootMotionSources)
					{
						const float RewoundRMSTime = FMath::Max(0.0f, RootMotionSource->GetTime() - TimeToRefund);
						RootMotionSource->SetTime(RewoundRMSTime);
					}
				}
			}
		}

		if (bNotifyApex && VelocityZ < 0.0f)
		{
			// Just passed jump apex since now going down
			bNotifyApex = false;
			NotifyJumpApex();
		}

		// Compute change in position (using midpoint integration method)
		FVector Adjusted = 0.5f * (OldVelocityWithRootMotion + Velocity) * timeTick;

		// Special handling if ending the jump force where we didn't apply gravity during the jump
		if (bEndingJumpForce && !bApplyGravityWhileJumping)
		{
			// We had a portion of the time at constant speed then a portion with acceleration due to gravity
			// Account for that here with a more correct change in position
			const float NonGravityTime = FMath::Max(0.0f, timeTick - GravityTime);
			Adjusted = (OldVelocityWithRootMotion * NonGravityTime) + (0.5f * (OldVelocityWithRootMotion + Velocity) * GravityTime);
		}

		// Move
		FHitResult Hit(1.0f);
		SafeMoveUpdatedComponent(Adjusted, PawnRotation, true, Hit);

		if (!HasValidData())
		{
			return;
		}

		float LastMoveTimeSlice = timeTick;
		float SubTimeTickRemaining = timeTick * (1.0f - Hit.Time);

		if (IsSwimming())
		{
			// Just entered water
			RemainingTime += SubTimeTickRemaining;
			StartSwimming(OldLocation, OldVelocity, timeTick, RemainingTime, Iterations);
			return;
		}
		else if (Hit.bBlockingHit)
		{
			if (IsValidLandingSpot(UpdatedComponent->GetComponentLocation(), Hit))
			{
				RemainingTime += SubTimeTickRemaining;
				ProcessLanded(Hit, RemainingTime, Iterations);
				return;
			}
			else
			{
				// Compute impact deflection based on final velocity, not integration step
				// This allows us to compute a new velocity from the deflected vector, and ensures the full gravity effect is included in the slide result
				Adjusted = Velocity * timeTick;

				// See if we can convert a normally invalid landing spot (based on the hit result) to a usable one
				if (!Hit.bStartPenetrating && ShouldCheckForValidLandingSpot(timeTick, Adjusted, Hit))
				{
					const FVector PawnLocation = UpdatedComponent->GetComponentLocation();
					FFindFloorResult FloorResult;
					FindFloor(PawnLocation, FloorResult, false);
					if (FloorResult.IsWalkableFloor() && IsValidLandingSpot(PawnLocation, FloorResult.HitResult))
					{
						RemainingTime += SubTimeTickRemaining;
						ProcessLanded(FloorResult.HitResult, RemainingTime, Iterations);
						return;
					}
				}

				HandleImpact(Hit, LastMoveTimeSlice, Adjusted);

				// If we've changed physics mode, abort
				if (!HasValidData() || !IsFalling())
				{
					return;
				}

				// Limit air control based on what we hit
				// We moved to the impact point using air control, but may want to deflect from there based on a limited air control acceleration
				FVector VelocityNoAirControl = OldVelocity;
				FVector AirControlAccel = Acceleration;
				if (bHasLimitedAirControl)
				{
					// Compute VelocityNoAirControl
					{
						// Find velocity *without* acceleration
						TGuardValue<FVector> RestoreAcceleration(Acceleration, FVector::ZeroVector);
						TGuardValue<FVector> RestoreVelocity(Velocity, OldVelocity);

						Velocity = FVector::VectorPlaneProject(Velocity, GravityDir);
						CalcVelocity(timeTick, FallingLateralFriction, false, MaxDecel);
						VelocityNoAirControl = FVector::VectorPlaneProject(Velocity, GravityDir) + OldVelocityZ;
						VelocityNoAirControl = NewFallVelocity(VelocityNoAirControl, Gravity, GravityTime);
					}

					const bool bCheckLandingSpot = false; // We already checked above
					AirControlAccel = (Velocity - VelocityNoAirControl) / timeTick;
					const FVector AirControlDeltaV = LimitAirControlEx(LastMoveTimeSlice, AirControlAccel, Hit, GravityDir, bCheckLandingSpot) * LastMoveTimeSlice;
					Adjusted = (VelocityNoAirControl + AirControlDeltaV) * LastMoveTimeSlice;
				}

				const FVector OldHitNormal = Hit.Normal;
				const FVector OldHitImpactNormal = Hit.ImpactNormal;
				FVector Delta = ComputeSlideVector(Adjusted, 1.0f - Hit.Time, OldHitNormal, Hit);

				// Compute velocity after deflection (only gravity component for RootMotion)
				const UPrimitiveComponent* HitComponent = Hit.GetComponent();
				static IConsoleVariable* UseTargetVelocityOnImpactVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.UseTargetVelocityOnImpact"));
				if ((UseTargetVelocityOnImpactVar == nullptr || UseTargetVelocityOnImpactVar->GetInt()) && !Velocity.IsNearlyZero() && MovementBaseUtility::IsSimulatedBase(HitComponent))
				{
					const FVector ContactVelocity = MovementBaseUtility::GetMovementBaseVelocity(HitComponent, NAME_None) + MovementBaseUtility::GetMovementBaseTangentialVelocity(HitComponent, NAME_None, Hit.ImpactPoint);
					const FVector NewVelocity = Velocity - Hit.ImpactNormal * FVector::DotProduct(Velocity - ContactVelocity, Hit.ImpactNormal);
					if (HasAnimRootMotion() || CurrentRootMotion.HasOverrideVelocityWithIgnoreZAccumulate())
					{
						Velocity = FVector::VectorPlaneProject(Velocity, GravityDir) + GravityDir * (NewVelocity | GravityDir);
					}
					else
					{
						Velocity = NewVelocity;
					}
				}
				else if (SubTimeTickRemaining > UE_KINDA_SMALL_NUMBER && !bJustTeleported)
				{
					const FVector NewVelocity = (Delta / SubTimeTickRemaining);
					if (HasAnimRootMotion() || CurrentRootMotion.HasOverrideVelocityWithIgnoreZAccumulate())
					{
						Velocity = FVector::VectorPlaneProject(Velocity, GravityDir) + GravityDir * (NewVelocity | GravityDir);
					}
					else
					{
						Velocity = NewVelocity;
					}
				}

				if (SubTimeTickRemaining > UE_KINDA_SMALL_NUMBER && (Delta | Adjusted) > 0.0f)
				{
					// Move in deflected direction
					SafeMoveUpdatedComponent(Delta, PawnRotation, true, Hit);

					if (Hit.bBlockingHit)
					{
						// Hit second wall
						LastMoveTimeSlice = SubTimeTickRemaining;
						SubTimeTickRemaining *= (1.0f - Hit.Time);

						if (IsValidLandingSpot(UpdatedComponent->GetComponentLocation(), Hit))
						{
							RemainingTime += SubTimeTickRemaining;
							ProcessLanded(Hit, RemainingTime, Iterations);
							return;
						}

						HandleImpact(Hit, LastMoveTimeSlice, Delta);

						// If we've changed physics mode, abort
						if (!HasValidData() || !IsFalling())
						{
							return;
						}

						// Act as if there was no air control on the last move when computing new deflection
						if (bHasLimitedAirControl && (Hit.Normal | GravityDir) < -NinjaCharacterMovementConstants::VERTICAL_SLOPE_NORMAL_Z)
						{
							Delta = ComputeSlideVector(VelocityNoAirControl * LastMoveTimeSlice, 1.0f, OldHitNormal, Hit);
						}

						FVector PreTwoWallDelta = Delta;
						TwoWallAdjust(Delta, Hit, OldHitNormal);

						// Limit air control, but allow a slide along the second wall
						if (bHasLimitedAirControl)
						{
							const FVector AirControlDeltaV = LimitAirControlEx(SubTimeTickRemaining, AirControlAccel, Hit, GravityDir, false) * SubTimeTickRemaining;

							// Only allow if not back in to first wall
							if ((AirControlDeltaV | OldHitNormal) > 0.0f)
							{
								Delta += (AirControlDeltaV * SubTimeTickRemaining);
							}
						}

						// Compute velocity after deflection (only gravity component for RootMotion)
						if (SubTimeTickRemaining > UE_KINDA_SMALL_NUMBER && !bJustTeleported)
						{
							const FVector NewVelocity = (Delta / SubTimeTickRemaining);

							if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocityWithIgnoreZAccumulate())
							{
								Velocity = NewVelocity;
							}
							else
							{
								Velocity = FVector::VectorPlaneProject(Velocity, GravityDir) + GravityDir * (NewVelocity | GravityDir);
							}
						}

						// bDitch=true means that pawn is straddling two slopes, neither of which it can stand on
						bool bDitch = ((OldHitImpactNormal | GravityDir) < 0.0f && (Hit.ImpactNormal | GravityDir) < 0.0f &&
							FMath::Abs(Delta | GravityDir) <= UE_KINDA_SMALL_NUMBER && (Hit.ImpactNormal | OldHitImpactNormal) < 0.0f);

						SafeMoveUpdatedComponent(Delta, PawnRotation, true, Hit);

						if (Hit.Time == 0.0f)
						{
							// If we are stuck then try to side step
							FVector SideDelta = FVector::VectorPlaneProject(OldHitNormal + Hit.ImpactNormal, GravityDir).GetSafeNormal();
							if (SideDelta.IsNearlyZero())
							{
								SideDelta = GravityDir ^ (FVector::VectorPlaneProject(OldHitNormal, GravityDir).GetSafeNormal());
							}

							SafeMoveUpdatedComponent(SideDelta, PawnRotation, true, Hit);
						}

						if (bDitch || IsValidLandingSpot(UpdatedComponent->GetComponentLocation(), Hit) || Hit.Time == 0.0f)
						{
							RemainingTime = 0.0f;
							ProcessLanded(Hit, RemainingTime, Iterations);

							return;
						}
						else if (GetPerchRadiusThreshold() > 0.0f && Hit.Time == 1.0f && (OldHitImpactNormal | GravityDir) <= -GetWalkableFloorZ())
						{
							// We might be in a virtual 'ditch' within our perch radius. This is rare
							const FVector PawnLocation = UpdatedComponent->GetComponentLocation();
							const float ZMovedDist = FMath::Abs((PawnLocation - OldLocation) | GravityDir);
							const float MovedDist2DSq = (FVector::VectorPlaneProject(PawnLocation - OldLocation, GravityDir)).SizeSquared();

							if (ZMovedDist <= 0.2f * timeTick && MovedDist2DSq <= 4.0f * timeTick)
							{
								Velocity.X += 0.25f * GetMaxSpeed() * (RandomStream.FRand() - 0.5f);
								Velocity.Y += 0.25f * GetMaxSpeed() * (RandomStream.FRand() - 0.5f);
								Velocity.Z += 0.25f * GetMaxSpeed() * (RandomStream.FRand() - 0.5f);
								Velocity = FVector::VectorPlaneProject(Velocity, GravityDir) + GravityDir * (FMath::Max<float>(JumpZVelocity * 0.25f, 1.0f) * -1.0f);
								Delta = Velocity * timeTick;

								SafeMoveUpdatedComponent(Delta, PawnRotation, true, Hit);
							}
						}
					}
				}
			}
		}

		if ((FVector::VectorPlaneProject(Velocity, GravityDir)).SizeSquared() <= UE_KINDA_SMALL_NUMBER * 10.0f)
		{
			Velocity = GravityDir * (Velocity | GravityDir);
		}
	}
}

FVector UNinjaCharacterMovementComponent::LimitAirControl(float DeltaTime, const FVector& FallAcceleration, const FHitResult& HitResult, bool bCheckForValidLandingSpot)
{
	return LimitAirControlEx(DeltaTime, FallAcceleration, HitResult, GetGravityDirection(true), bCheckForValidLandingSpot);
}

FVector UNinjaCharacterMovementComponent::LimitAirControlEx(float DeltaTime, const FVector& FallAcceleration, const FHitResult& HitResult, const FVector& GravDir, bool bCheckForValidLandingSpot) const
{
	FVector Result = FallAcceleration;

	if (HitResult.IsValidBlockingHit() && (HitResult.Normal | GravDir) < -NinjaCharacterMovementConstants::VERTICAL_SLOPE_NORMAL_Z)
	{
		if ((!bCheckForValidLandingSpot || !IsValidLandingSpot(HitResult.Location, HitResult)) && (FallAcceleration | HitResult.Normal) < 0.0f)
		{
			// If acceleration is into the wall, limit contribution
			// Allow movement parallel to the wall, but not into it because that may push us up
			const FVector Normal2D = FVector::VectorPlaneProject(HitResult.Normal, GravDir).GetSafeNormal();
			Result = FVector::VectorPlaneProject(FallAcceleration, Normal2D);
		}
	}
	else if (HitResult.bStartPenetrating)
	{
		// Allow movement out of penetration
		return ((Result | HitResult.Normal) > 0.0f ? Result : FVector::ZeroVector);
	}

	return Result;
}

bool UNinjaCharacterMovementComponent::CheckLedgeDirection(const FVector& OldLocation, const FVector& SideStep, const FVector& GravDir) const
{
	const FVector SideDest = OldLocation + SideStep;
	const FQuat PawnRotation = UpdatedComponent->GetComponentQuat();
	FCollisionQueryParams CapsuleParams(SCENE_QUERY_STAT(CheckLedgeDirection), false, CharacterOwner);
	FCollisionResponseParams ResponseParam;
	InitCollisionParams(CapsuleParams, ResponseParam);
	const FCollisionShape CapsuleShape = GetPawnCapsuleCollisionShape(SHRINK_None);
	const ECollisionChannel CollisionChannel = UpdatedComponent->GetCollisionObjectType();
	FHitResult Result(1.0f);
	GetWorld()->SweepSingleByChannel(Result, OldLocation, SideDest, PawnRotation, CollisionChannel, CapsuleShape, CapsuleParams, ResponseParam);

	if (!Result.bBlockingHit || IsWalkable(Result))
	{
		if (!Result.bBlockingHit)
		{
			GetWorld()->SweepSingleByChannel(Result, SideDest, SideDest + GravDir * (MaxStepHeight + LedgeCheckThreshold), PawnRotation, CollisionChannel, CapsuleShape, CapsuleParams, ResponseParam);
		}

		if (Result.Time < 1.0f && IsWalkable(Result))
		{
			return true;
		}
	}

	return false;
}

FVector UNinjaCharacterMovementComponent::GetLedgeMove(const FVector& OldLocation, const FVector& Delta, const FVector& GravDir) const
{
	if (!HasValidData() || Delta.IsZero())
	{
		return FVector::ZeroVector;
	}

	FVector SideDir = FVector::VectorPlaneProject(Delta, GravDir);

	// Try left
	SideDir = FQuat(GravDir, UE_HALF_PI).RotateVector(SideDir);
	if (CheckLedgeDirection(OldLocation, SideDir, GravDir))
	{
		return SideDir;
	}

	// Try right
	SideDir *= -1.0f;
	if (CheckLedgeDirection(OldLocation, SideDir, GravDir))
	{
		return SideDir;
	}

	return FVector::ZeroVector;
}

void UNinjaCharacterMovementComponent::StartFalling(int32 Iterations, float remainingTime, float timeTick, const FVector& Delta, const FVector& subLoc)
{
	const float DesiredDist = Delta.Size();

	if (DesiredDist < UE_KINDA_SMALL_NUMBER)
	{
		remainingTime = 0.0f;
	}
	else
	{
		const float ActualDist = (UpdatedComponent->GetComponentLocation() - subLoc).Size();
		remainingTime += timeTick * (1.0f - FMath::Min(1.0f, ActualDist / DesiredDist));
	}

	if (IsMovingOnGround())
	{
		// This is to catch cases where the first frame of PIE is executed, and the
		// level is not yet visible. In those cases, the player will fall out of the
		// world... So, don't set MOVE_Falling straight away
		if (!GIsEditor || (GetWorld()->HasBegunPlay() && GetWorld()->GetTimeSeconds() >= 1.0f))
		{
			SetMovementMode(MOVE_Falling); // Default behavior if script didn't change physics
		}
		else
		{
			// Make sure that the floor check code continues processing during this delay
			bForceNextFloorCheck = true;
		}
	}

	StartNewPhysics(remainingTime, Iterations);
}

FVector UNinjaCharacterMovementComponent::ComputeGroundMovementDelta(const FVector& Delta, const FHitResult& RampHit, const bool bHitFromLineTrace) const
{
	const FVector CapsuleUp = GetComponentAxisZ();
	return ComputeGroundMovementDeltaEx(FVector::VectorPlaneProject(Delta, CapsuleUp), CapsuleUp, RampHit, bHitFromLineTrace);
}

FVector UNinjaCharacterMovementComponent::ComputeGroundMovementDeltaEx(const FVector& Delta, const FVector& DeltaPlaneNormal, const FHitResult& RampHit, const bool bHitFromLineTrace) const
{
	const FVector FloorNormal = RampHit.ImpactNormal;

	if (!bHitFromLineTrace && IsWalkable(RampHit))
	{
		const FVector DeltaNormal = Delta.GetSafeNormal();
		if (DeltaNormal.IsZero())
		{
			return DeltaNormal;
		}

		if (!FNinjaMath::Orthogonal(DeltaNormal, FloorNormal, ThresholdOrthogonalCosine))
		{
			// Compute a vector that moves parallel to the surface, by projecting the horizontal movement direction onto the ramp
			// We can't just project Delta onto the plane defined by FloorNormal because the direction changes on spherical geometry
			FVector NewDelta = FQuat(DeltaPlaneNormal ^ DeltaNormal,
				(((FloorNormal | Delta) > 0.0f) ? 1.0f : -1.0f) * FMath::Acos(FloorNormal | DeltaPlaneNormal)).RotateVector(Delta);

			if (bMaintainHorizontalGroundVelocity)
			{
				const FVector NewDeltaNormal = NewDelta.GetSafeNormal();
				NewDelta = NewDeltaNormal * (Delta.Size() / (DeltaNormal | NewDeltaNormal));
			}

			return NewDelta;
		}
	}

	return Delta;
}

void UNinjaCharacterMovementComponent::MoveAlongFloor(const FVector& InVelocity, float DeltaSeconds, FStepDownResult* OutStepDownResult)
{
	if (!CurrentFloor.IsWalkableFloor())
	{
		return;
	}

	// Move along the current floor
	const FVector CapsuleUp = GetComponentAxisZ();
	const FVector Delta = FVector::VectorPlaneProject(InVelocity, CapsuleUp) * DeltaSeconds;
	FHitResult Hit(1.0f);
	FVector RampVector = ComputeGroundMovementDeltaEx(Delta, CapsuleUp, CurrentFloor.HitResult, CurrentFloor.bLineTrace);
	SafeMoveUpdatedComponent(RampVector, UpdatedComponent->GetComponentQuat(), true, Hit);
	float LastMoveTimeSlice = DeltaSeconds;

	if (Hit.bStartPenetrating)
	{
		// Allow this hit to be used as an impact we can deflect off, otherwise we do nothing the rest of the update and appear to hitch
		HandleImpact(Hit);
		SlideAlongSurface(Delta, 1.0f, Hit.Normal, Hit, true);

		if (Hit.bStartPenetrating)
		{
			OnCharacterStuckInGeometry(&Hit);
		}
	}
	else if (Hit.IsValidBlockingHit())
	{
		// We impacted something (most likely another ramp, but possibly a barrier)
		float PercentTimeApplied = Hit.Time;
		if (Hit.Time > 0.0f && (Hit.Normal | CapsuleUp) > UE_KINDA_SMALL_NUMBER && IsWalkable(Hit))
		{
			// Another walkable ramp
			const float InitialPercentRemaining = 1.0f - PercentTimeApplied;
			RampVector = ComputeGroundMovementDeltaEx(Delta * InitialPercentRemaining, CapsuleUp, Hit, false);
			LastMoveTimeSlice = InitialPercentRemaining * LastMoveTimeSlice;
			SafeMoveUpdatedComponent(RampVector, UpdatedComponent->GetComponentQuat(), true, Hit);

			const float SecondHitPercent = Hit.Time * InitialPercentRemaining;
			PercentTimeApplied = FMath::Clamp(PercentTimeApplied + SecondHitPercent, 0.0f, 1.0f);
		}

		if (Hit.IsValidBlockingHit())
		{
			if (CanStepUp(Hit) || (CharacterOwner->GetMovementBase() != nullptr && Hit.HitObjectHandle == CharacterOwner->GetMovementBase()->GetOwner()))
			{
				// Hit a barrier, try to step up
				const FVector PreStepUpLocation = UpdatedComponent->GetComponentLocation();
				if (!StepUp(CapsuleUp * -1.0f, Delta * (1.0f - PercentTimeApplied), Hit, OutStepDownResult))
				{
					UE_LOG(LogCharacterMovement, Verbose, TEXT("- StepUp (ImpactNormal %s, Normal %s"), *Hit.ImpactNormal.ToString(), *Hit.Normal.ToString());
					HandleImpact(Hit, LastMoveTimeSlice, RampVector);
					SlideAlongSurface(Delta, 1.0f - PercentTimeApplied, Hit.Normal, Hit, true);
				}
				else
				{
					UE_LOG(LogCharacterMovement, Verbose, TEXT("+ StepUp (ImpactNormal %s, Normal %s"), *Hit.ImpactNormal.ToString(), *Hit.Normal.ToString());
					if (!bMaintainHorizontalGroundVelocity)
					{
						// Don't recalculate velocity based on this height adjustment, if considering vertical adjustments; only consider horizontal movement
						bJustTeleported = true;
						const float StepUpTimeSlice = (1.0f - PercentTimeApplied) * DeltaSeconds;
						if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && StepUpTimeSlice >= UE_KINDA_SMALL_NUMBER)
						{
							Velocity = (UpdatedComponent->GetComponentLocation() - PreStepUpLocation) / StepUpTimeSlice;
							Velocity = FVector::VectorPlaneProject(Velocity, CapsuleUp);
						}
					}
				}
			}
			else if (Hit.Component.IsValid() && !Hit.Component.Get()->CanCharacterStepUp(CharacterOwner))
			{
				HandleImpact(Hit, LastMoveTimeSlice, RampVector);
				SlideAlongSurface(Delta, 1.0f - PercentTimeApplied, Hit.Normal, Hit, true);
			}
		}
	}
}

void UNinjaCharacterMovementComponent::MaintainHorizontalGroundVelocity()
{
	if (bMaintainHorizontalGroundVelocity)
	{
		// Just remove the vertical component
		Velocity = FVector::VectorPlaneProject(Velocity, GetComponentAxisZ());
	}
	else
	{
		// Project the vector and maintain its original magnitude
		Velocity = FVector::VectorPlaneProject(Velocity, GetComponentAxisZ()).GetSafeNormal() * Velocity.Size();
	}
}

void UNinjaCharacterMovementComponent::PhysWalking(float deltaTime, int32 Iterations)
{
	SCOPE_CYCLE_COUNTER(STAT_CharPhysWalking);

	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	if (!CharacterOwner || (!CharacterOwner->Controller && !bRunPhysicsWithNoController && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy))
	{
		Acceleration = FVector::ZeroVector;
		Velocity = FVector::ZeroVector;
		return;
	}

	if (!UpdatedComponent->IsQueryCollisionEnabled())
	{
		SetMovementMode(MOVE_Walking);
		return;
	}

	devCode(ensureMsgf(!Velocity.ContainsNaN(), TEXT("PhysWalking: Velocity contains NaN before Iteration (%s)\n%s"), *GetPathNameSafe(this), *Velocity.ToString()));

	bJustTeleported = false;
	bool bCheckedFall = false;
	bool bTriedLedgeMove = false;
	float remainingTime = deltaTime;

	// Perform the move
	while (remainingTime >= MIN_TICK_TIME && Iterations < MaxSimulationIterations && CharacterOwner && (CharacterOwner->Controller || bRunPhysicsWithNoController || HasAnimRootMotion() || CurrentRootMotion.HasOverrideVelocity() || CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy))
	{
		Iterations++;
		bJustTeleported = false;
		const float timeTick = GetSimulationTimeStep(remainingTime, Iterations);
		remainingTime -= timeTick;

		// Save current values
		UPrimitiveComponent* const OldBase = GetMovementBase();
		const FVector PreviousBaseLocation = (OldBase != NULL) ? OldBase->GetComponentLocation() : FVector::ZeroVector;
		const FVector OldLocation = UpdatedComponent->GetComponentLocation();
		const FFindFloorResult OldFloor = CurrentFloor;

		RestorePreAdditiveRootMotionVelocity();

		// Ensure velocity is horizontal
		MaintainHorizontalGroundVelocity();

		const FVector OldVelocity = Velocity;
		Acceleration = FVector::VectorPlaneProject(Acceleration, GetComponentAxisZ());

		// Apply acceleration
		if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
		{
			CalcVelocity(timeTick, GroundFriction, false, GetMaxBrakingDeceleration());
			devCode(ensureMsgf(!Velocity.ContainsNaN(), TEXT("PhysWalking: Velocity contains NaN after CalcVelocity (%s)\n%s"), *GetPathNameSafe(this), *Velocity.ToString()));
		}

		ApplyRootMotionToVelocity(timeTick);
		devCode(ensureMsgf(!Velocity.ContainsNaN(), TEXT("PhysWalking: Velocity contains NaN after Root Motion application (%s)\n%s"), *GetPathNameSafe(this), *Velocity.ToString()));

		if (IsFalling())
		{
			// Root motion could have put us into falling
			// No movement has taken place this movement tick so we pass on full time/past iteration count
			StartNewPhysics(remainingTime + timeTick, Iterations - 1);
			return;
		}

		// Compute move parameters
		const FVector MoveVelocity = Velocity;
		const FVector Delta = timeTick * MoveVelocity;
		const bool bZeroDelta = Delta.IsNearlyZero();
		FStepDownResult StepDownResult;

		if (bZeroDelta)
		{
			remainingTime = 0.0f;
		}
		else
		{
			// Try to move forward
			MoveAlongFloor(MoveVelocity, timeTick, &StepDownResult);

			if (IsFalling())
			{
				// Pawn decided to jump up
				const float DesiredDist = Delta.Size();
				if (DesiredDist > UE_KINDA_SMALL_NUMBER)
				{
					const float ActualDist = FVector::VectorPlaneProject(UpdatedComponent->GetComponentLocation() - OldLocation, GetComponentAxisZ()).Size();
					remainingTime += timeTick * (1.0f - FMath::Min(1.0f, ActualDist / DesiredDist));
				}

				StartNewPhysics(remainingTime, Iterations);
				return;
			}
			else if (IsSwimming())
			{
				//Just entered water
				StartSwimming(OldLocation, OldVelocity, timeTick, remainingTime, Iterations);
				return;
			}
		}

		// Update floor; StepUp might have already done it for us
		if (StepDownResult.bComputedFloor)
		{
			CurrentFloor = StepDownResult.FloorResult;
		}
		else
		{
			FindFloor(UpdatedComponent->GetComponentLocation(), CurrentFloor, bZeroDelta, NULL);
		}

		// Check for ledges here
		const bool bCheckLedges = !CanWalkOffLedges();
		if (bCheckLedges && !CurrentFloor.IsWalkableFloor())
		{
			// Calculate possible alternate movement
			const FVector NewDelta = bTriedLedgeMove ? FVector::ZeroVector : GetLedgeMove(OldLocation, Delta, GetComponentAxisZ() * -1.0f);
			if (!NewDelta.IsZero())
			{
				// First revert this move
				RevertMove(OldLocation, OldBase, PreviousBaseLocation, OldFloor, false);

				// Avoid repeated ledge moves if the first one fails
				bTriedLedgeMove = true;

				// Try new movement direction
				Velocity = NewDelta / timeTick;
				remainingTime += timeTick;
				continue;
			}
			else
			{
				// See if it is OK to jump
				// @todo collision: only thing that can be problem is that OldBase has world collision on
				bool bMustJump = bZeroDelta || OldBase == NULL || (!OldBase->IsQueryCollisionEnabled() && MovementBaseUtility::IsDynamicBase(OldBase));
				if ((bMustJump || !bCheckedFall) && CheckFall(OldFloor, CurrentFloor.HitResult, Delta, OldLocation, remainingTime, timeTick, Iterations, bMustJump))
				{
					return;
				}

				bCheckedFall = true;

				// Revert this move
				RevertMove(OldLocation, OldBase, PreviousBaseLocation, OldFloor, true);
				remainingTime = 0.0f;
				break;
			}
		}
		else
		{
			// Validate the floor check
			if (CurrentFloor.IsWalkableFloor())
			{
				if (ShouldCatchAir(OldFloor, CurrentFloor))
				{
					HandleWalkingOffLedge(OldFloor.HitResult.ImpactNormal, OldFloor.HitResult.Normal, OldLocation, timeTick);
					if (IsMovingOnGround())
					{
						// If still walking, then fall. If not, assume the user set a different mode they want to keep
						StartFalling(Iterations, remainingTime, timeTick, Delta, OldLocation);
					}

					return;
				}

				AdjustFloorHeight();
				SetBase(CurrentFloor.HitResult.Component.Get(), CurrentFloor.HitResult.BoneName);
			}
			else if (CurrentFloor.HitResult.bStartPenetrating && remainingTime <= 0.0f)
			{
				// The floor check failed because it started in penetration
				// We do not want to try to move downward because the downward sweep failed, rather we'd like to try to pop out of the floor
				FHitResult Hit(CurrentFloor.HitResult);
				Hit.TraceEnd = Hit.TraceStart + GetComponentAxisZ() * MAX_FLOOR_DIST;
				const FVector RequestedAdjustment = GetPenetrationAdjustment(Hit);
				ResolvePenetration(RequestedAdjustment, Hit, UpdatedComponent->GetComponentQuat());
				bForceNextFloorCheck = true;
			}

			// Check if just entered water
			if (IsSwimming())
			{
				StartSwimming(OldLocation, Velocity, timeTick, remainingTime, Iterations);
				return;
			}

			// See if we need to start falling
			if (!CurrentFloor.IsWalkableFloor() && !CurrentFloor.HitResult.bStartPenetrating)
			{
				const bool bMustJump = bJustTeleported || bZeroDelta || OldBase == NULL || (!OldBase->IsQueryCollisionEnabled() && MovementBaseUtility::IsDynamicBase(OldBase));
				if ((bMustJump || !bCheckedFall) && CheckFall(OldFloor, CurrentFloor.HitResult, Delta, OldLocation, remainingTime, timeTick, Iterations, bMustJump))
				{
					return;
				}

				bCheckedFall = true;
			}
		}

		// Allow overlap events and such to change physics state and velocity
		if (IsMovingOnGround())
		{
			// Make velocity reflect actual move
			if (!bJustTeleported && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && timeTick >= MIN_TICK_TIME)
			{
				// TODO-RootMotionSource: Allow this to happen during partial override Velocity, but only set allowed axes?
				Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / timeTick;
				MaintainHorizontalGroundVelocity();
			}
		}

		// If we didn't move at all this iteration then abort (since future iterations will also be stuck)
		if (UpdatedComponent->GetComponentLocation() == OldLocation)
		{
			remainingTime = 0.0f;
			break;
		}
	}

	if (IsMovingOnGround())
	{
		MaintainHorizontalGroundVelocity();
	}
}

void UNinjaCharacterMovementComponent::AdjustFloorHeight()
{
	SCOPE_CYCLE_COUNTER(STAT_CharAdjustFloorHeight);

	// If we have a floor check that hasn't hit anything, don't adjust height
	if (!CurrentFloor.IsWalkableFloor())
	{
		return;
	}

	float OldFloorDist = CurrentFloor.FloorDist;
	if (CurrentFloor.bLineTrace)
	{
		if (OldFloorDist < MIN_FLOOR_DIST && CurrentFloor.LineDist >= MIN_FLOOR_DIST)
		{
			// This would cause us to scale unwalkable walls
			UE_LOG(LogCharacterMovement, VeryVerbose, TEXT("Adjust floor height aborting due to line trace with small floor distance (line: %.2f, sweep: %.2f)"), CurrentFloor.LineDist, CurrentFloor.FloorDist);
			return;
		}
		else
		{
			// Falling back to a line trace means the sweep was unwalkable (or in penetration). Use the line distance for the vertical adjustment
			OldFloorDist = CurrentFloor.LineDist;
		}
	}

	// Move up or down to maintain floor height
	if (OldFloorDist < MIN_FLOOR_DIST || OldFloorDist > MAX_FLOOR_DIST)
	{
		FHitResult AdjustHit(1.0f);
		const float AvgFloorDist = (MIN_FLOOR_DIST + MAX_FLOOR_DIST) * 0.5f;
		const float MoveDist = AvgFloorDist - OldFloorDist;
		const FVector CapsuleUp = GetComponentAxisZ();
		const FVector InitialLocation = UpdatedComponent->GetComponentLocation();

		SafeMoveUpdatedComponent(CapsuleUp * MoveDist, UpdatedComponent->GetComponentQuat(), true, AdjustHit);
		UE_LOG(LogCharacterMovement, VeryVerbose, TEXT("Adjust floor height %.3f (Hit = %d)"), MoveDist, AdjustHit.bBlockingHit);

		if (!AdjustHit.IsValidBlockingHit())
		{
			CurrentFloor.FloorDist += MoveDist;
		}
		else if (MoveDist > 0.0f)
		{
			CurrentFloor.FloorDist += (InitialLocation - UpdatedComponent->GetComponentLocation()) | CapsuleUp;
		}
		else
		{
			checkSlow(MoveDist < 0.0f);

			CurrentFloor.FloorDist = (AdjustHit.Location - UpdatedComponent->GetComponentLocation()) | CapsuleUp;
			if (IsWalkable(AdjustHit))
			{
				CurrentFloor.SetFromSweep(AdjustHit, CurrentFloor.FloorDist, true);
			}
		}

		// Don't recalculate velocity based on this height adjustment, if considering vertical adjustments
		// Also avoid it if we moved out of penetration
		bJustTeleported |= !bMaintainHorizontalGroundVelocity || OldFloorDist < 0.0f;

		// If something caused us to adjust our height (especially a depentration) we should ensure another check next frame or we will keep a stale result
		if (CharacterOwner && CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)
		{
			bForceNextFloorCheck = true;
		}
	}
}

void UNinjaCharacterMovementComponent::SetPostLandedPhysics(const FHitResult& Hit)
{
	if (CharacterOwner)
	{
		if (CanEverSwim() && IsInWater())
		{
			SetMovementMode(MOVE_Swimming);
		}
		else
		{
			const FVector PreImpactAccel = Acceleration + (IsFalling() ? GetGravity() : FVector::ZeroVector);
			const FVector PreImpactVelocity = Velocity;

			if (DefaultLandMovementMode == MOVE_Walking || DefaultLandMovementMode == MOVE_NavWalking || DefaultLandMovementMode == MOVE_Falling)
			{
//				SetMovementMode(GroundMovementMode);
				SetMovementMode(GetGroundMovementMode()); //OVERRIDEN
			}
			else
			{
				SetDefaultMovementMode();
			}

			ApplyImpactPhysicsForces(Hit, PreImpactAccel, PreImpactVelocity);
		}
	}
}

void UNinjaCharacterMovementComponent::OnTeleported()
{
	if (!HasValidData())
	{
		return;
	}

	UPawnMovementComponent::OnTeleported();

	bJustTeleported = true;

	// Find floor at current location
	UpdateFloorFromAdjustment();

	// Validate it. We don't want to pop down to walking mode from very high off the ground, but we'd like to keep walking if possible
	UPrimitiveComponent* OldBase = CharacterOwner->GetMovementBase();
	UPrimitiveComponent* NewBase = NULL;

	if (OldBase && CurrentFloor.IsWalkableFloor() && CurrentFloor.FloorDist <= MAX_FLOOR_DIST && (Velocity | GetComponentAxisZ()) <= 0.0f)
	{
		// Close enough to land or just keep walking
		NewBase = CurrentFloor.HitResult.Component.Get();
	}
	else
	{
		CurrentFloor.Clear();
	}

	const bool bWasFalling = (MovementMode == MOVE_Falling);
	const bool bWasSwimming = (MovementMode == DefaultWaterMovementMode) || (MovementMode == MOVE_Swimming);

	if (CanEverSwim() && IsInWater())
	{
		if (!bWasSwimming)
		{
			SetMovementMode(DefaultWaterMovementMode);
		}
	}
	else if (!CurrentFloor.IsWalkableFloor() || (OldBase != nullptr && NewBase == nullptr))
	{
		if (!bWasFalling && MovementMode != MOVE_Flying && MovementMode != MOVE_Custom)
		{
			SetMovementMode(MOVE_Falling);
		}
	}
	else if (NewBase != nullptr)
	{
		if (bWasSwimming)
		{
			SetMovementMode(DefaultLandMovementMode);
		}
		else if (bWasFalling)
		{
			ProcessLanded(CurrentFloor.HitResult, 0.0f, 0);
		}
	}

	SaveBaseLocation();
}

void UNinjaCharacterMovementComponent::PhysicsRotation(float DeltaTime)
{
	if ((!bOrientRotationToMovement && !bUseControllerDesiredRotation) || !HasValidData() || (!CharacterOwner->Controller && !bRunPhysicsWithNoController))
	{
		return;
	}

	FRotator CurrentRotation = UpdatedComponent->GetComponentRotation(); // Normalized
	CurrentRotation.DiagnosticCheckNaN(TEXT("CharacterMovementComponent::PhysicsRotation(): CurrentRotation"));

	FRotator DeltaRot = GetDeltaRotation(DeltaTime);
	DeltaRot.DiagnosticCheckNaN(TEXT("CharacterMovementComponent::PhysicsRotation(): GetDeltaRotation"));

	FRotator DesiredRotation = CurrentRotation;
	if (bOrientRotationToMovement)
	{
		DesiredRotation = ComputeOrientToMovementRotation(CurrentRotation, DeltaTime, DeltaRot);
	}
	else if (CharacterOwner->Controller && bUseControllerDesiredRotation)
	{
		DesiredRotation = CharacterOwner->Controller->GetDesiredRotation();
	}
	else if (!CharacterOwner->Controller && bRunPhysicsWithNoController && bUseControllerDesiredRotation)
	{
		if (AController* ControllerOwner = Cast<AController>(CharacterOwner->GetOwner()))
		{
			DesiredRotation = ControllerOwner->GetDesiredRotation();
		}
	}
	else
	{
		return;
	}

	if (ShouldRemainVertical())
	{
		DesiredRotation = ConstrainComponentRotation(DesiredRotation);
	}
	else
	{
		DesiredRotation.Normalize();
	}

	// Accumulate a desired new rotation
	const float AngleTolerance = 1e-3f;

	if (!CurrentRotation.Equals(DesiredRotation, AngleTolerance))
	{
		if (DeltaRot.Roll == DeltaRot.Yaw && DeltaRot.Yaw == DeltaRot.Pitch)
		{
			// Calculate the spherical interpolation between the two rotators
			const FQuat CurrentQuat(CurrentRotation);
			const FQuat DesiredQuat(DesiredRotation);

			// Get shortest angle between quaternions
			const float Angle = FMath::Acos(FMath::Abs(CurrentQuat | DesiredQuat)) * 2.0f;

			// Calculate percent of interpolation
			const float Alpha = FMath::Min(FMath::DegreesToRadians(DeltaRot.Yaw) / Angle, 1.0f);

			DesiredRotation = (Alpha == 1.0f) ? DesiredRotation : FQuat::Slerp(CurrentQuat, DesiredQuat, Alpha).Rotator();
		}
		else
		{
			// Pitch
			if (!FMath::IsNearlyEqual(CurrentRotation.Pitch, DesiredRotation.Pitch, AngleTolerance))
			{
				DesiredRotation.Pitch = FMath::FixedTurn(CurrentRotation.Pitch, DesiredRotation.Pitch, DeltaRot.Pitch);
			}

			// Yaw
			if (!FMath::IsNearlyEqual(CurrentRotation.Yaw, DesiredRotation.Yaw, AngleTolerance))
			{
				DesiredRotation.Yaw = FMath::FixedTurn(CurrentRotation.Yaw, DesiredRotation.Yaw, DeltaRot.Yaw);
			}

			// Roll
			if (!FMath::IsNearlyEqual(CurrentRotation.Roll, DesiredRotation.Roll, AngleTolerance))
			{
				DesiredRotation.Roll = FMath::FixedTurn(CurrentRotation.Roll, DesiredRotation.Roll, DeltaRot.Roll);
			}
		}

		// Set the new rotation
		DesiredRotation.DiagnosticCheckNaN(TEXT("CharacterMovementComponent::PhysicsRotation(): DesiredRotation"));
		MoveUpdatedComponent(FVector::ZeroVector, DesiredRotation, /*bSweep*/ false);
	}
}

void UNinjaCharacterMovementComponent::PhysicsVolumeChanged(class APhysicsVolume* NewVolume)
{
	if (!HasValidData())
	{
		return;
	}

	if (bRevertToDefaultGravity && NewVolume != nullptr &&
		NewVolume == GetWorld()->GetDefaultPhysicsVolume())
	{
		// Revert to engine's hardcoded gravity direction
		SetFixedGravityDirection(FVector::DownVector);
	}

	if (NewVolume && NewVolume->bWaterVolume)
	{
		// Just entered water
		if (!CanEverSwim())
		{
			// AI needs to stop any current moves
			IPathFollowingAgentInterface* PFAgent = GetPathFollowingAgent();
			if (PFAgent)
			{
				PFAgent->OnUnableToMove(*this);
			}
		}
		else if (!IsSwimming())
		{
			SetMovementMode(MOVE_Swimming);
		}
	}
	else if (IsSwimming())
	{
		SetMovementMode(MOVE_Falling);

		// Just left the water, check if should jump out
		const FVector GravityDir = GetGravityDirection(true);
		FVector JumpDir = FVector::ZeroVector;
		FVector WallNormal = FVector::ZeroVector;

		if ((Acceleration | GravityDir) < 0.0f && ShouldJumpOutOfWaterEx(JumpDir, GravityDir) && (JumpDir | Acceleration) > 0.0f && CheckWaterJumpEx(JumpDir, GravityDir, WallNormal))
		{
			JumpOutOfWater(WallNormal);
			Velocity = FVector::VectorPlaneProject(Velocity, GravityDir) - GravityDir * OutofWaterZ; // Set here so physics uses this for remainder of tick
		}
	}
}

void UNinjaCharacterMovementComponent::SetUpdatedComponent(USceneComponent* NewUpdatedComponent)
{
	// Unsubscribe from hit event
	if (IsValid(UpdatedPrimitive) && UpdatedPrimitive->OnComponentHit.IsBound())
	{
		UpdatedPrimitive->OnComponentHit.RemoveDynamic(this, &ThisClass::OnComponentHit);
	}

	Super::SetUpdatedComponent(NewUpdatedComponent);

	// Subscribe to hit event
	if (IsValid(UpdatedPrimitive))
	{
		UpdatedPrimitive->OnComponentHit.AddUniqueDynamic(this, &ThisClass::OnComponentHit);
	}
}

bool UNinjaCharacterMovementComponent::ShouldJumpOutOfWater(FVector& JumpDir)
{
	return ShouldJumpOutOfWaterEx(JumpDir, GetGravityDirection(true));
}

bool UNinjaCharacterMovementComponent::ShouldJumpOutOfWaterEx(FVector& JumpDir, const FVector& GravDir)
{
	// If pawn is going up and looking up, then make it jump
	AController* OwnerController = CharacterOwner->GetController();
	if (OwnerController && (Velocity | GravDir) < 0.0f)
	{
		const FVector ControllerDir = OwnerController->GetControlRotation().Vector();
		if ((ControllerDir | GravDir) < FMath::Cos(FMath::DegreesToRadians(JumpOutOfWaterPitch + 90.0f)))
		{
			JumpDir = ControllerDir;
			return true;
		}
	}

	return false;
}

bool UNinjaCharacterMovementComponent::CheckWaterJump(FVector CheckPoint, FVector& WallNormal)
{
	return CheckWaterJumpEx(CheckPoint, GetGravityDirection(true), WallNormal);
}

bool UNinjaCharacterMovementComponent::CheckWaterJumpEx(FVector CheckPoint, const FVector& GravDir, FVector& WallNormal)
{
	if (!HasValidData())
	{
		return false;
	}

	// Check if there is a wall directly in front of the swimming pawn
	float PawnCapsuleRadius, PawnCapsuleHalfHeight;
	CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleSize(PawnCapsuleRadius, PawnCapsuleHalfHeight);
	CheckPoint = UpdatedComponent->GetComponentLocation() + FVector::VectorPlaneProject(CheckPoint, GravDir).GetSafeNormal() * (PawnCapsuleRadius * 1.2f);

	FCollisionQueryParams CapsuleParams(SCENE_QUERY_STAT(CheckWaterJump), false, CharacterOwner);
	const FCollisionShape CapsuleShape = GetPawnCapsuleCollisionShape(SHRINK_None);
	const ECollisionChannel CollisionChannel = UpdatedComponent->GetCollisionObjectType();
	FCollisionResponseParams ResponseParam;
	InitCollisionParams(CapsuleParams, ResponseParam);

	FHitResult HitInfo(1.0f);
	bool bHit = GetWorld()->SweepSingleByChannel(HitInfo, UpdatedComponent->GetComponentLocation(), CheckPoint, UpdatedComponent->GetComponentQuat(), CollisionChannel, CapsuleShape, CapsuleParams, ResponseParam);

	if (bHit && !HitInfo.HitObjectHandle.DoesRepresentClass(APawn::StaticClass()))
	{
		// Hit a wall, check if it's low enough
		WallNormal = HitInfo.ImpactNormal * -1.0f;
		const FVector Start = UpdatedComponent->GetComponentLocation() + GravDir * -MaxOutOfWaterStepHeight;
		CheckPoint = Start + WallNormal * (PawnCapsuleRadius * 3.2f);

		FCollisionQueryParams LineParams(SCENE_QUERY_STAT(CheckWaterJump), true, CharacterOwner);
		FCollisionResponseParams LineResponseParam;
		InitCollisionParams(LineParams, LineResponseParam);

		HitInfo.Reset(1.0f, false);
		bHit = GetWorld()->LineTraceSingleByChannel(HitInfo, Start, CheckPoint, CollisionChannel, LineParams, LineResponseParam);

		// If no high obstruction, or it's a valid floor, then pawn can jump out of water
		return !bHit || IsWalkable(HitInfo);
	}

	return false;
}

void UNinjaCharacterMovementComponent::MoveSmooth(const FVector& InVelocity, const float DeltaSeconds, FStepDownResult* OutStepDownResult)
{
	if (!HasValidData())
	{
		return;
	}

	// Custom movement mode
	// Custom movement may need an update even if there is zero velocity
	if (MovementMode == MOVE_Custom)
	{
		FScopedMovementUpdate ScopedMovementUpdate(UpdatedComponent, bEnableScopedMovementUpdates ? EScopedUpdate::DeferredUpdates : EScopedUpdate::ImmediateUpdates);
		PhysCustom(DeltaSeconds, 0);
		return;
	}

	FVector Delta = InVelocity * DeltaSeconds;
	if (Delta.IsZero())
	{
		return;
	}

	FScopedMovementUpdate ScopedMovementUpdate(UpdatedComponent, bEnableScopedMovementUpdates ? EScopedUpdate::DeferredUpdates : EScopedUpdate::ImmediateUpdates);

	if (IsMovingOnGround())
	{
		MoveAlongFloor(InVelocity, DeltaSeconds, OutStepDownResult);
	}
	else
	{
		FHitResult Hit(1.0f);
		SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);

		if (Hit.IsValidBlockingHit())
		{
			bool bSteppedUp = false;

			if (IsFlying())
			{
				if (CanStepUp(Hit))
				{
					OutStepDownResult = NULL; // No need for a floor when not walking
					const FVector CapsuleDown = GetComponentAxisZ() * -1.0f;

					if (FMath::Abs(Hit.ImpactNormal | CapsuleDown) < 0.2f)
					{
						const float UpDown = CapsuleDown | Delta.GetSafeNormal();
						if (UpDown < 0.5f && UpDown > -0.2f)
						{
							bSteppedUp = StepUp(CapsuleDown, Delta * (1.0f - Hit.Time), Hit, OutStepDownResult);
						}
					}
				}
			}

			// If StepUp failed, try sliding
			if (!bSteppedUp)
			{
				SlideAlongSurface(Delta, 1.0f - Hit.Time, Hit.Normal, Hit, false);
			}
		}
	}
}

bool UNinjaCharacterMovementComponent::IsWalkable(const FHitResult& Hit) const
{
	if (!Hit.IsValidBlockingHit())
	{
		// No hit, or starting in penetration
		return false;
	}

	const FVector CapsuleUp = GetComponentAxisZ();

	// Never walk up vertical surfaces
	if ((Hit.ImpactNormal | CapsuleUp) < UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}

	float TestWalkableZ = GetWalkableFloorZ();

	// See if this component overrides the walkable floor z
	const UPrimitiveComponent* HitComponent = Hit.Component.Get();
	if (HitComponent)
	{
		const FWalkableSlopeOverride& SlopeOverride = HitComponent->GetWalkableSlopeOverride();
		TestWalkableZ = SlopeOverride.ModifyWalkableFloorZ(TestWalkableZ);
	}

	// Can't walk on this surface if it is too steep
	if ((Hit.ImpactNormal | CapsuleUp) < TestWalkableZ)
	{
		return false;
	}

	// Can't start walking on this surface if gravity direction disallows that
	if (!bLandOnAnySurface && IsFalling() &&
		(Hit.ImpactNormal | (GetGravityDirection(false) * -1.0f)) < TestWalkableZ)
	{
		return false;
	}

	return true;
}

bool UNinjaCharacterMovementComponent::IsWithinEdgeTolerance(const FVector& CapsuleLocation, const FVector& TestImpactPoint, const float CapsuleRadius) const
{
	return IsWithinEdgeToleranceEx(CapsuleLocation, GetComponentAxisZ() * -1.0f, CapsuleRadius, TestImpactPoint);
}

bool UNinjaCharacterMovementComponent::IsWithinEdgeToleranceEx(const FVector& CapsuleLocation, const FVector& CapsuleDown, const float CapsuleRadius, const FVector& TestImpactPoint) const
{
	const float DistFromCenterSq = (CapsuleLocation + CapsuleDown * ((TestImpactPoint - CapsuleLocation) | CapsuleDown) - TestImpactPoint).SizeSquared();
	const float ReducedRadiusSq = FMath::Square(FMath::Max(SWEEP_EDGE_REJECT_DISTANCE + UE_KINDA_SMALL_NUMBER, CapsuleRadius - SWEEP_EDGE_REJECT_DISTANCE));

	return DistFromCenterSq < ReducedRadiusSq;
}

void UNinjaCharacterMovementComponent::ComputeFloorDist(const FVector& CapsuleLocation, float LineDistance, float SweepDistance, FFindFloorResult& OutFloorResult, float SweepRadius, const FHitResult* DownwardSweepResult) const
{
	UE_LOG(LogCharacterMovement, VeryVerbose, TEXT("[Role:%d] ComputeFloorDist: %s at location %s"), (int32)CharacterOwner->GetLocalRole(), *GetNameSafe(CharacterOwner), *CapsuleLocation.ToString());
	OutFloorResult.Clear();

	float PawnRadius, PawnHalfHeight;
	CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleSize(PawnRadius, PawnHalfHeight);

	const FVector CapsuleDown = GetComponentAxisZ() * -1.0f;

	bool bSkipSweep = false;
	if (DownwardSweepResult != NULL && DownwardSweepResult->IsValidBlockingHit())
	{
		// Only if the supplied sweep was vertical and downward
		if (FNinjaMath::Coincident((DownwardSweepResult->TraceEnd - DownwardSweepResult->TraceStart).GetSafeNormal(),
			CapsuleDown, ThresholdParallelCosine))
		{
			// Reject hits that are barely on the cusp of the radius of the capsule
			if (IsWithinEdgeToleranceEx(DownwardSweepResult->Location, CapsuleDown, PawnRadius, DownwardSweepResult->ImpactPoint))
			{
				// Don't try a redundant sweep, regardless of whether this sweep is usable
				bSkipSweep = true;

				const bool bIsWalkable = IsWalkable(*DownwardSweepResult);
				const float FloorDist = (CapsuleLocation - DownwardSweepResult->Location).Size();
				OutFloorResult.SetFromSweep(*DownwardSweepResult, FloorDist, bIsWalkable);

				if (bIsWalkable)
				{
					// Use the supplied downward sweep as the floor hit result
					return;
				}
			}
		}
	}

	// We require the sweep distance to be >= the line distance, otherwise the HitResult can't be interpreted as the sweep result
	if (SweepDistance < LineDistance)
	{
		ensure(SweepDistance >= LineDistance);
		return;
	}

	bool bBlockingHit = false;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ComputeFloorDist), false, CharacterOwner);
	FCollisionResponseParams ResponseParam;
	InitCollisionParams(QueryParams, ResponseParam);
	const ECollisionChannel CollisionChannel = UpdatedComponent->GetCollisionObjectType();

	// Sweep test
	if (!bSkipSweep && SweepDistance > 0.0f && SweepRadius > 0.0f)
	{
		// Use a shorter height to avoid sweeps giving weird results if we start on a surface
		// This also allows us to adjust out of penetrations
		const float ShrinkScale = 0.9f;
		const float ShrinkScaleOverlap = 0.1f;
		float ShrinkHeight = (PawnHalfHeight - PawnRadius) * (1.0f - ShrinkScale);
		float TraceDist = SweepDistance + ShrinkHeight;
		FCollisionShape CapsuleShape = FCollisionShape::MakeCapsule(SweepRadius, PawnHalfHeight - ShrinkHeight);

		FHitResult Hit(1.0f);
		bBlockingHit = FloorSweepTest(Hit, CapsuleLocation, CapsuleLocation + CapsuleDown * TraceDist, CollisionChannel, CapsuleShape, QueryParams, ResponseParam);

		if (bBlockingHit)
		{
			// Reject hits adjacent to us, we only care about hits on the bottom portion of our capsule
			// Check 2D distance to impact point, reject if within a tolerance from radius
			if (Hit.bStartPenetrating || !IsWithinEdgeToleranceEx(CapsuleLocation, CapsuleDown, CapsuleShape.Capsule.Radius, Hit.ImpactPoint))
			{
				// Use a capsule with a slightly smaller radius and shorter height to avoid the adjacent object
				// Capsule must not be nearly zero or the trace will fall back to a line trace from the start point and have the wrong length
				CapsuleShape.Capsule.Radius = FMath::Max(0.0f, CapsuleShape.Capsule.Radius - SWEEP_EDGE_REJECT_DISTANCE - UE_KINDA_SMALL_NUMBER);
				if (!CapsuleShape.IsNearlyZero())
				{
					ShrinkHeight = (PawnHalfHeight - PawnRadius) * (1.0f - ShrinkScaleOverlap);
					TraceDist = SweepDistance + ShrinkHeight;
					CapsuleShape.Capsule.HalfHeight = FMath::Max(PawnHalfHeight - ShrinkHeight, CapsuleShape.Capsule.Radius);
					Hit.Reset(1.0f, false);

					bBlockingHit = FloorSweepTest(Hit, CapsuleLocation, CapsuleLocation + CapsuleDown * TraceDist, CollisionChannel, CapsuleShape, QueryParams, ResponseParam);
				}
			}

			// Reduce hit distance by ShrinkHeight because we shrank the capsule for the trace
			// We allow negative distances here, because this allows us to pull out of penetrations
			const float MaxPenetrationAdjust = FMath::Max(MAX_FLOOR_DIST, PawnRadius);
			const float SweepResult = FMath::Max(-MaxPenetrationAdjust, Hit.Time * TraceDist - ShrinkHeight);

			OutFloorResult.SetFromSweep(Hit, SweepResult, false);
			if (Hit.IsValidBlockingHit() && IsWalkable(Hit))
			{
				if (SweepResult <= SweepDistance)
				{
					// Hit within test distance
					OutFloorResult.bWalkableFloor = true;
					return;
				}
			}
		}
	}

	// Since we require a longer sweep than line trace, we don't want to run the line trace if the sweep missed everything
	// We do however want to try a line trace if the sweep was stuck in penetration
	if (!OutFloorResult.bBlockingHit && !OutFloorResult.HitResult.bStartPenetrating)
	{
		OutFloorResult.FloorDist = SweepDistance;
		return;
	}

	// Line trace
	if (LineDistance > 0.0f)
	{
		const float ShrinkHeight = PawnHalfHeight;
		const FVector LineTraceStart = CapsuleLocation;
		const float TraceDist = LineDistance + ShrinkHeight;
		QueryParams.TraceTag = SCENE_QUERY_STAT_NAME_ONLY(FloorLineTrace);

		FHitResult Hit(1.0f);
		bBlockingHit = GetWorld()->LineTraceSingleByChannel(Hit, LineTraceStart, LineTraceStart + CapsuleDown * TraceDist,
			CollisionChannel, QueryParams, ResponseParam);

		if (bBlockingHit)
		{
			if (Hit.Time > 0.0f)
			{
				// Reduce hit distance by ShrinkHeight because we started the trace higher than the base
				// We allow negative distances here, because this allows us to pull out of penetrations
				const float MaxPenetrationAdjust = FMath::Max(MAX_FLOOR_DIST, PawnRadius);
				const float LineResult = FMath::Max(-MaxPenetrationAdjust, Hit.Time * TraceDist - ShrinkHeight);

				OutFloorResult.bBlockingHit = true;
				if (LineResult <= LineDistance && IsWalkable(Hit))
				{
					OutFloorResult.SetFromLineTrace(Hit, OutFloorResult.FloorDist, LineResult, true);
					return;
				}
			}
		}
	}

	// No hits were acceptable
	OutFloorResult.bWalkableFloor = false;
}

bool UNinjaCharacterMovementComponent::FloorSweepTest(FHitResult& OutHit, const FVector& Start, const FVector& End, ECollisionChannel TraceChannel,
	const FCollisionShape& CollisionShape, const FCollisionQueryParams& Params, const FCollisionResponseParams& ResponseParam) const
{
	bool bBlockingHit = false;

	if (!bUseFlatBaseForFloorChecks)
	{
		bBlockingHit = GetWorld()->SweepSingleByChannel(OutHit, Start, End, UpdatedComponent->GetComponentQuat(), TraceChannel, CollisionShape, Params, ResponseParam);
	}
	else
	{
		// Test with a box that is enclosed by the capsule
		const float CapsuleRadius = CollisionShape.GetCapsuleRadius();
		const float CapsuleHeight = CollisionShape.GetCapsuleHalfHeight();
		const FCollisionShape BoxShape = FCollisionShape::MakeBox(FVector(CapsuleRadius * 0.707f, CapsuleRadius * 0.707f, CapsuleHeight));

		// Use a box rotation that ignores the capsule forward orientation
		const FVector BoxUp = GetComponentAxisZ();
		const FQuat BoxRotation = FRotationMatrix::MakeFromZ(BoxUp).ToQuat();

		// First test with the box rotated so the corners are along the major axes (ie rotated 45 degrees)
		bBlockingHit = GetWorld()->SweepSingleByChannel(OutHit, Start, End, FQuat(BoxUp, UE_PI * 0.25f) * BoxRotation, TraceChannel, BoxShape, Params, ResponseParam);

		if (!bBlockingHit)
		{
			// Test again with the same box, not rotated
			OutHit.Reset(1.0f, false);
			bBlockingHit = GetWorld()->SweepSingleByChannel(OutHit, Start, End, BoxRotation, TraceChannel, BoxShape, Params, ResponseParam);
		}
	}

	return bBlockingHit;
}

bool UNinjaCharacterMovementComponent::IsValidLandingSpot(const FVector& CapsuleLocation, const FHitResult& Hit) const
{
	if (!Hit.bBlockingHit)
	{
		return false;
	}

	const FVector CapsuleDown = GetComponentAxisZ() * -1.0f;

	// Skip some checks if penetrating. Penetration will be handled by the FindFloor call (using a smaller capsule)
	if (!Hit.bStartPenetrating)
	{
		// Reject unwalkable floor normals
		if (!IsWalkable(Hit))
		{
			return false;
		}

		float PawnRadius, PawnHalfHeight;
		CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleSize(PawnRadius, PawnHalfHeight);

		// Get the axis of the capsule bounded by the following two end points
		const FVector BottomPoint = Hit.Location + CapsuleDown * FMath::Max(0.0f, PawnHalfHeight - PawnRadius);
		const FVector TopPoint = Hit.Location - CapsuleDown;
		const FVector Segment = TopPoint - BottomPoint;

		// Project the impact point on the segment
		const float Alpha = ((Hit.ImpactPoint - BottomPoint) | Segment) / Segment.SizeSquared();

		// Reject hits that are above our lower hemisphere (can happen when sliding "down" a vertical surface)
		if (Alpha >= 0.0f)
		{
			return false;
		}

		// Reject hits that are barely on the cusp of the radius of the capsule
		if (!IsWithinEdgeToleranceEx(Hit.Location, CapsuleDown, PawnRadius, Hit.ImpactPoint))
		{
			return false;
		}
	}
	else
	{
		// Penetrating
		if ((Hit.Normal | CapsuleDown) > -UE_KINDA_SMALL_NUMBER)
		{
			// Normal is nearly horizontal or downward, that's a penetration adjustment next to a vertical or overhanging wall. Don't pop to the floor
			return false;
		}
	}

	FFindFloorResult FloorResult;
	FindFloor(CapsuleLocation, FloorResult, false, &Hit);

	// Reject invalid surfaces
	if (!FloorResult.IsWalkableFloor())
	{
		return false;
	}

	return true;
}

bool UNinjaCharacterMovementComponent::ShouldCheckForValidLandingSpot(float DeltaTime, const FVector& Delta, const FHitResult& Hit) const
{
	const FVector CapsuleUp = GetComponentAxisZ();

	// See if we hit an edge of a surface on the lower portion of the capsule
	// In this case the normal will not equal the impact normal, and a downward sweep may find a walkable surface on top of the edge
	if ((Hit.Normal | CapsuleUp) > UE_KINDA_SMALL_NUMBER && !Hit.Normal.Equals(Hit.ImpactNormal) &&
		IsWithinEdgeToleranceEx(UpdatedComponent->GetComponentLocation(), CapsuleUp * -1.0f, CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius(), Hit.ImpactPoint))
	{
		return true;
	}

	return false;
}

bool UNinjaCharacterMovementComponent::ShouldComputePerchResult(const FHitResult& InHit, bool bCheckRadius) const
{
	if (!InHit.IsValidBlockingHit())
	{
		return false;
	}

	// Don't try to perch if the edge radius is very small
	if (GetPerchRadiusThreshold() <= SWEEP_EDGE_REJECT_DISTANCE)
	{
		return false;
	}

	if (bCheckRadius)
	{
		const FVector CapsuleDown = GetComponentAxisZ() * -1.0f;
		const float DistFromCenterSq = (InHit.Location + CapsuleDown * ((InHit.ImpactPoint - InHit.Location) | CapsuleDown) - InHit.ImpactPoint).SizeSquared();
		const float StandOnEdgeRadiusSq = FMath::Square(GetValidPerchRadius());

		if (DistFromCenterSq <= StandOnEdgeRadiusSq)
		{
			// Already within perch radius
			return false;
		}
	}

	return true;
}

bool UNinjaCharacterMovementComponent::ComputePerchResult(const float TestRadius, const FHitResult& InHit, const float InMaxFloorDist, FFindFloorResult& OutPerchFloorResult) const
{
	if (InMaxFloorDist <= 0.0f)
	{
		return false;
	}

	// Sweep further than actual requested distance, because a reduced capsule radius means we could miss some hits that the normal radius would contact
	float PawnRadius, PawnHalfHeight;
	CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleSize(PawnRadius, PawnHalfHeight);
	const FVector CapsuleLocation = (bUseFlatBaseForFloorChecks ? InHit.TraceStart : InHit.Location);

	const FVector CapsuleDown = GetComponentAxisZ() * -1.0f;
	const float InHitAboveBase = (CapsuleLocation + CapsuleDown * ((InHit.ImpactPoint - CapsuleLocation) | CapsuleDown) -
		(CapsuleLocation + CapsuleDown * PawnHalfHeight)).Size();
	const float PerchLineDist = FMath::Max(0.0f, InMaxFloorDist - InHitAboveBase);
	const float PerchSweepDist = FMath::Max(0.0f, InMaxFloorDist);

	const float ActualSweepDist = PerchSweepDist + PawnRadius;
	ComputeFloorDist(CapsuleLocation, PerchLineDist, ActualSweepDist, OutPerchFloorResult, TestRadius);

	if (!OutPerchFloorResult.IsWalkableFloor())
	{
		return false;
	}
	else if (InHitAboveBase + OutPerchFloorResult.FloorDist > InMaxFloorDist)
	{
		// Hit something past max distance
		OutPerchFloorResult.bWalkableFloor = false;
		return false;
	}

	return true;
}

void UNinjaCharacterMovementComponent::OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity)
{
	Super::OnMovementUpdated(DeltaSeconds, OldLocation, OldVelocity);

	UpdateGravity();

	const bool bMovingOnGround = IsMovingOnGround();
	UpdateComponentRotation(GetComponentDesiredAxisZ(), bAlwaysRotateAroundCenter || !bMovingOnGround,
		bRotateVelocityOnGround && bMovingOnGround);

	if (ShouldReplicateGravity())
	{
		ReplicateGravityToClients();
	}
}

bool UNinjaCharacterMovementComponent::StepUp(const FVector& GravDir, const FVector& Delta, const FHitResult& InHit, FStepDownResult* OutStepDownResult)
{
	SCOPE_CYCLE_COUNTER(STAT_CharStepUp);

	if (!CanStepUp(InHit) || MaxStepHeight <= 0.0f)
	{
		return false;
	}

	const FVector OldLocation = UpdatedComponent->GetComponentLocation();
	float PawnRadius, PawnHalfHeight;
	CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleSize(PawnRadius, PawnHalfHeight);

	const FVector CapsuleDown = GetComponentAxisZ() * -1.0f;

	// Get the axis of the capsule bounded by the following two end points
	const FVector BottomPoint = OldLocation + CapsuleDown * PawnHalfHeight;
	const FVector TopPoint = OldLocation - CapsuleDown * FMath::Max(0.0f, PawnHalfHeight - PawnRadius);
	const FVector Segment = TopPoint - BottomPoint;

	// Project the impact point on the segment; don't bother stepping up if top of capsule is hitting something
	if (((InHit.ImpactPoint - BottomPoint) | Segment) / Segment.SizeSquared() > 1.0f)
	{
		return false;
	}

	// Gravity should be a normalized direction
	ensure(GravDir.IsNormalized());

	float StepTravelUpHeight = MaxStepHeight;
	float StepTravelDownHeight = StepTravelUpHeight;
	const float StepSideZ = -1.0f * FVector::DotProduct(InHit.ImpactNormal, GravDir);
	FVector PawnInitialFloorBase = OldLocation + CapsuleDown * PawnHalfHeight;
	FVector PawnFloorPoint = PawnInitialFloorBase;

	if (IsMovingOnGround() && CurrentFloor.IsWalkableFloor())
	{
		// Since we float a variable amount off the floor, we need to enforce max step height off the actual point of impact with the floor
		const float FloorDist = FMath::Max(0.0f, CurrentFloor.GetDistanceToFloor());
		PawnInitialFloorBase += CapsuleDown * FloorDist;
		StepTravelUpHeight = FMath::Max(StepTravelUpHeight - FloorDist, 0.0f);
		StepTravelDownHeight = (MaxStepHeight + MAX_FLOOR_DIST * 2.0f);

		const bool bHitVerticalFace = !IsWithinEdgeToleranceEx(InHit.Location, CapsuleDown, PawnRadius, InHit.ImpactPoint);
		if (!CurrentFloor.bLineTrace && !bHitVerticalFace)
		{
			PawnFloorPoint = CurrentFloor.HitResult.ImpactPoint;
		}
		else
		{
			// Base floor point is the base of the capsule moved down by how far we are hovering over the surface we are hitting
			PawnFloorPoint += CapsuleDown * CurrentFloor.FloorDist;
		}
	}

	// Don't step up if the impact is below us, accounting for distance from floor
	if (((InHit.ImpactPoint - PawnInitialFloorBase) | (TopPoint - PawnInitialFloorBase)) <= 0.0f)
	{
		return false;
	}

	// Scope our movement updates, and do not apply them until all intermediate moves are completed
	FScopedMovementUpdate ScopedStepUpMovement(UpdatedComponent, EScopedUpdate::DeferredUpdates);

	// Step up, treat as vertical wall
	FHitResult SweepUpHit(1.0f);
	const FQuat PawnRotation = UpdatedComponent->GetComponentQuat();
	MoveUpdatedComponent(GravDir * -StepTravelUpHeight, PawnRotation, true, &SweepUpHit);

	if (SweepUpHit.bStartPenetrating)
	{
		// Undo movement
		ScopedStepUpMovement.RevertMove();
		return false;
	}

	// Step forward
	FHitResult Hit(1.0f);
	MoveUpdatedComponent(Delta, PawnRotation, true, &Hit);

	// Check result of forward movement
	if (Hit.bBlockingHit)
	{
		if (Hit.bStartPenetrating)
		{
			// Undo movement
			ScopedStepUpMovement.RevertMove();
			return false;
		}

		// If we hit something above us and also something ahead of us, we should notify about the upward hit as well
		// The forward hit will be handled later (in the bSteppedOver case below)
		// In the case of hitting something above but not forward, we are not blocked from moving so we don't need the notification
		if (SweepUpHit.bBlockingHit && Hit.bBlockingHit)
		{
			HandleImpact(SweepUpHit);
		}

		// Pawn ran into a wall
		HandleImpact(Hit);
		if (IsFalling())
		{
			return true;
		}

		// Adjust and try again
		const float ForwardHitTime = Hit.Time;
		const float ForwardSlideAmount = SlideAlongSurface(Delta, 1.0f - Hit.Time, Hit.Normal, Hit, true);

		if (IsFalling())
		{
			ScopedStepUpMovement.RevertMove();
			return false;
		}

		// If both the forward hit and the deflection got us nowhere, there is no point in this step up
		if (ForwardHitTime == 0.0f && ForwardSlideAmount == 0.0f)
		{
			ScopedStepUpMovement.RevertMove();
			return false;
		}
	}

	// Step down
	MoveUpdatedComponent(GravDir * StepTravelDownHeight, UpdatedComponent->GetComponentQuat(), true, &Hit);

	// If step down was initially penetrating abort the step up
	if (Hit.bStartPenetrating)
	{
		ScopedStepUpMovement.RevertMove();
		return false;
	}

	FStepDownResult StepDownResult;
	if (Hit.IsValidBlockingHit())
	{
		// See if this step sequence would have allowed us to travel higher than our max step height allows
		const float DeltaZ = (PawnFloorPoint - Hit.ImpactPoint) | CapsuleDown;
		if (DeltaZ > MaxStepHeight)
		{
			//UE_LOG(LogCharacterMovement, VeryVerbose, TEXT("- Reject StepUp (too high Height %.3f) up from floor base"), DeltaZ);
			ScopedStepUpMovement.RevertMove();
			return false;
		}

		// Reject unwalkable surface normals here
		if (!IsWalkable(Hit))
		{
			// Reject if normal opposes movement direction
			const bool bNormalTowardsMe = (Delta | Hit.ImpactNormal) < 0.0f;
			if (bNormalTowardsMe)
			{
				//UE_LOG(LogCharacterMovement, VeryVerbose, TEXT("- Reject StepUp (unwalkable normal %s opposed to movement)"), *Hit.ImpactNormal.ToString());
				ScopedStepUpMovement.RevertMove();
				return false;
			}

			// Also reject if we would end up being higher than our starting location by stepping down
			// It's fine to step down onto an unwalkable normal below us, we will just slide off. Rejecting those moves would prevent us from being able to walk off the edge
			if (((OldLocation - Hit.Location) | CapsuleDown) > 0.0f)
			{
				//UE_LOG(LogCharacterMovement, VeryVerbose, TEXT("- Reject StepUp (unwalkable normal %s above old position)"), *Hit.ImpactNormal.ToString());
				ScopedStepUpMovement.RevertMove();
				return false;
			}
		}

		// Reject moves where the downward sweep hit something very close to the edge of the capsule. This maintains consistency with FindFloor as well
		if (!IsWithinEdgeToleranceEx(Hit.Location, CapsuleDown, PawnRadius, Hit.ImpactPoint))
		{
			//UE_LOG(LogCharacterMovement, VeryVerbose, TEXT("- Reject StepUp (outside edge tolerance)"));
			ScopedStepUpMovement.RevertMove();
			return false;
		}

		// Don't step up onto invalid surfaces if traveling higher
		if (DeltaZ > 0.0f && !CanStepUp(Hit))
		{
			//UE_LOG(LogCharacterMovement, VeryVerbose, TEXT("- Reject StepUp (up onto surface with !CanStepUp())"));
			ScopedStepUpMovement.RevertMove();
			return false;
		}

		// See if we can validate the floor as a result of this step down. In almost all cases this should succeed, and we can avoid computing the floor outside this method
		if (OutStepDownResult != NULL)
		{
			FindFloor(UpdatedComponent->GetComponentLocation(), StepDownResult.FloorResult, false, &Hit);

			// Reject unwalkable normals if we end up higher than our initial height
			// It's fine to walk down onto an unwalkable surface, don't reject those moves
			if (((OldLocation - Hit.Location) | CapsuleDown) > 0.0f)
			{
				// We should reject the floor result if we are trying to step up an actual step where we are not able to perch (this is rare)
				// In those cases we should instead abort the step up and try to slide along the stair
				if (!StepDownResult.FloorResult.bBlockingHit && StepSideZ < NinjaCharacterMovementConstants::MAX_STEP_SIDE_Z)
				{
					ScopedStepUpMovement.RevertMove();
					return false;
				}
			}

			StepDownResult.bComputedFloor = true;
		}
	}

	// Copy step down result
	if (OutStepDownResult != NULL)
	{
		*OutStepDownResult = StepDownResult;
	}

	// Don't recalculate velocity based on this height adjustment, if considering vertical adjustments
	bJustTeleported |= !bMaintainHorizontalGroundVelocity;

	return true;
}

void UNinjaCharacterMovementComponent::HandleImpact(const FHitResult& Impact, float TimeSlice, const FVector& MoveDelta)
{
	SCOPE_CYCLE_COUNTER(STAT_CharHandleImpact);

	if (CharacterOwner)
	{
		CharacterOwner->MoveBlockedBy(Impact);
	}

	IPathFollowingAgentInterface* PFAgent = GetPathFollowingAgent();
	if (PFAgent)
	{
		// Also notify path following!
		PFAgent->OnMoveBlockedBy(Impact);
	}

	if (Impact.HitObjectHandle.DoesRepresentClass(APawn::StaticClass()))
	{
		APawn* OtherPawn = Impact.HitObjectHandle.FetchActor<APawn>();
		NotifyBumpedPawn(OtherPawn);
	}

	if (bEnablePhysicsInteraction)
	{
		const FVector ForceAccel = Acceleration + (IsFalling() ? GetGravity() : FVector::ZeroVector);
		ApplyImpactPhysicsForces(Impact, ForceAccel, Velocity);
	}
}

void UNinjaCharacterMovementComponent::ApplyImpactPhysicsForces(const FHitResult& Impact, const FVector& ImpactAcceleration, const FVector& ImpactVelocity)
{
	if (bEnablePhysicsInteraction && Impact.bBlockingHit)
	{
		if (UPrimitiveComponent* ImpactComponent = Impact.GetComponent())
		{
			FVector ForcePoint = Impact.ImpactPoint;
			float BodyMass = 1.0f; // Set to 1 as this is used as a multiplier

			bool bCanBePushed = false;
			FBodyInstance* BI = ImpactComponent->GetBodyInstance(Impact.BoneName);
			if (BI != nullptr && BI->IsInstanceSimulatingPhysics())
			{
				BodyMass = FMath::Max(BI->GetBodyMass(), 1.0f);

				if (bPushForceUsingZOffset)
				{
					FVector Center, Extents;
					BI->GetBodyBounds().GetCenterAndExtents(Center, Extents);

					if (!Extents.IsNearlyZero())
					{
						const FVector CapsuleUp = GetComponentAxisZ();

						// Project impact point onto the horizontal plane defined by center and gravity, then offset from there
						ForcePoint = FVector::PointPlaneProject(ForcePoint, Center, CapsuleUp) +
							CapsuleUp * (FMath::Abs(Extents | CapsuleUp) * PushForcePointZOffsetFactor);
					}
				}

				bCanBePushed = true;
			}
			else
			{
				static IConsoleVariable* CVarGeometryCollectionImpulseWorkAroundVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.CVarGeometryCollectionImpulseWorkAround"));
				if (CVarGeometryCollectionImpulseWorkAroundVar != nullptr && CVarGeometryCollectionImpulseWorkAroundVar->GetBool())
				{
					const FName ClassName = ImpactComponent->GetClass()->GetFName();
					const FName GeometryCollectionClassName("GeometryCollectionComponent");
					if (ClassName == GeometryCollectionClassName && ImpactComponent->BodyInstance.bSimulatePhysics)
					{
						// In some case GetBodyInstance can return null while the BodyInstance still exists (geometry collection component for example)
						// but we cannot check for its component directly here because of modules cyclic dependencies
						// todo(chaos): change this logic to be more driven at the primitive component level to avoid the high level classes to have to be aware of the different component

						// Because of the above limititation we have to ignore bPushForceUsingZOffset

						bCanBePushed = true;
					}
				}
			}

			if (bCanBePushed)
			{
				FVector Force = Impact.ImpactNormal * -1.0f;
				float PushForceModificator = 1.0f;
				const FVector ComponentVelocity = ImpactComponent->GetPhysicsLinearVelocity();
				const FVector VirtualVelocity = ImpactAcceleration.IsZero() ? ImpactVelocity : ImpactAcceleration.GetSafeNormal() * GetMaxSpeed();
				float Dot = 0.0f;

				if (bScalePushForceToVelocity && !ComponentVelocity.IsNearlyZero())
				{
					Dot = ComponentVelocity | VirtualVelocity;

					if (Dot > 0.0f && Dot < 1.0f)
					{
						PushForceModificator *= Dot;
					}
				}

				if (bPushForceScaledToMass)
				{
					PushForceModificator *= BodyMass;
				}
				const bool bEnableNetworkPhysics = Chaos::FPhysicsSolverBase::IsNetworkPhysicsPredictionEnabled();

				Force *= PushForceModificator;
				const float ZeroVelocityTolerance = 1.0f;
				if (ComponentVelocity.IsNearlyZero(ZeroVelocityTolerance))
				{
					Force *= InitialPushForceFactor;
					if (!bEnableNetworkPhysics)
					{
						ImpactComponent->AddImpulseAtLocation(Force, ForcePoint, Impact.BoneName);
					}
					else
					{
						ApplyAsyncPhysicsStateAction(ImpactComponent, Impact.BoneName, EPhysicsStateAction::AddImpulseAtPosition, Force, ForcePoint);
					}
				}
				else
				{
					Force *= PushForceFactor;
					if (!bEnableNetworkPhysics)
					{
						ImpactComponent->AddForceAtLocation(Force, ForcePoint, Impact.BoneName);
					}
					else
					{
						ApplyAsyncPhysicsStateAction(ImpactComponent, Impact.BoneName, EPhysicsStateAction::AddForceAtPosition, Force, ForcePoint);
					}
				}
			}
		}
	}
}

void UNinjaCharacterMovementComponent::DisplayDebug(UCanvas* Canvas, const FDebugDisplayInfo& DebugDisplay, float& YL, float& YPos)
{
	if (CharacterOwner == NULL)
	{
		return;
	}

	FDisplayDebugManager& DisplayDebugManager = Canvas->DisplayDebugManager;
	DisplayDebugManager.SetDrawColor(FColor::White);
	FString T = FString::Printf(TEXT("CHARACTER MOVEMENT Floor %s Crouched %i"), *CurrentFloor.HitResult.ImpactNormal.ToString(), IsCrouching());
	DisplayDebugManager.DrawString(T);

	T = FString::Printf(TEXT("Updated Component: %s"), *UpdatedComponent->GetName());
	DisplayDebugManager.DrawString(T);

	T = FString::Printf(TEXT("Acceleration: %s"), *Acceleration.ToCompactString());
	DisplayDebugManager.DrawString(T);

	T = FString::Printf(TEXT("bForceMaxAccel: %i"), bForceMaxAccel);
	DisplayDebugManager.DrawString(T);

	T = FString::Printf(TEXT("RootMotionSources: %d active"), CurrentRootMotion.RootMotionSources.Num());
	DisplayDebugManager.DrawString(T);

	APhysicsVolume * PhysicsVolume = GetPhysicsVolume();

	const UPrimitiveComponent* BaseComponent = CharacterOwner->GetMovementBase();
	const AActor* BaseActor = BaseComponent ? BaseComponent->GetOwner() : NULL;

	T = FString::Printf(TEXT("%s In physicsvolume %s on base %s component %s gravity %s"), *GetMovementName(), (PhysicsVolume ? *PhysicsVolume->GetName() : TEXT("None")),
		(BaseActor ? *BaseActor->GetName() : TEXT("None")), (BaseComponent ? *BaseComponent->GetName() : TEXT("None")), *GetGravity().ToString());
	DisplayDebugManager.DrawString(T);
}

float UNinjaCharacterMovementComponent::VisualizeMovement() const
{
	if (CharacterOwner == nullptr)
	{
		return 0.0f;
	}

	float HeightOffset = 0.0f;
	const float OffsetPerElement = 10.0f;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	const FVector CapsuleUp = GetComponentAxisZ();
	const FVector TopOfCapsule = GetActorLocation() + CapsuleUp * CharacterOwner->GetSimpleCollisionHalfHeight();

	// Position
	{
		const FColor DebugColor = FColor::White;
		const FVector DebugLocation = TopOfCapsule + CapsuleUp * HeightOffset;
		FString DebugText = FString::Printf(TEXT("Position: %s"), *GetActorLocation().ToCompactString());
		DrawDebugString(GetWorld(), DebugLocation, DebugText, nullptr, DebugColor, 0.0f, true);
	}

	// Rotation
	{
		const FColor DebugColor = FColor::White;
		HeightOffset += OffsetPerElement;
		const FVector DebugLocation = TopOfCapsule + CapsuleUp * HeightOffset;

		DrawDebugCoordinateSystem(GetWorld(), DebugLocation + CapsuleUp * -5.0f, UpdatedComponent->GetComponentRotation(),
			100.0f, false, -1.0f, 0, 2.0f);

		FString DebugText = FString::Printf(TEXT("Rotation: %s"), *UpdatedComponent->GetComponentRotation().ToCompactString());
		DrawDebugString(GetWorld(), DebugLocation, DebugText, nullptr, DebugColor, 0.0f, true);
	}

	// Velocity
	{
		const FColor DebugColor = FColor::Green;
		HeightOffset += OffsetPerElement;
		const FVector DebugLocation = TopOfCapsule + CapsuleUp * HeightOffset;
		DrawDebugDirectionalArrow(GetWorld(), DebugLocation + CapsuleUp * -5.0f, DebugLocation + CapsuleUp * -5.0f + Velocity,
			100.0f, DebugColor, false, -1.0f, (uint8)'\000', 10.0f);

		FString DebugText = FString::Printf(TEXT("Velocity: %s (Speed: %.2f) (Max: %.2f)"), *Velocity.ToCompactString(), Velocity.Size(), GetMaxSpeed());
		DrawDebugString(GetWorld(), DebugLocation, DebugText, nullptr, DebugColor, 0.0f, true);
	}

	// Acceleration
	{
		const FColor DebugColor = FColor::Yellow;
		HeightOffset += OffsetPerElement;
		const float MaxAccelerationLineLength = 200.0f;
		const float CurrentMaxAccel = GetMaxAcceleration();
		const float CurrentAccelAsPercentOfMaxAccel = CurrentMaxAccel > 0.0f ? Acceleration.Size() / CurrentMaxAccel : 1.0f;
		const FVector DebugLocation = TopOfCapsule + CapsuleUp * HeightOffset;
		DrawDebugDirectionalArrow(GetWorld(), DebugLocation + CapsuleUp * -5.0f,
			DebugLocation + CapsuleUp * -5.0f + Acceleration.GetSafeNormal(UE_SMALL_NUMBER) * CurrentAccelAsPercentOfMaxAccel * MaxAccelerationLineLength,
			25.0f, DebugColor, false, -1.0f, (uint8)'\000', 8.0f);

		FString DebugText = FString::Printf(TEXT("Acceleration: %s"), *Acceleration.ToCompactString());
		DrawDebugString(GetWorld(), DebugLocation, DebugText, nullptr, DebugColor, 0.0f, true);
	}

	// Movement Mode
	{
		const FColor DebugColor = FColor::Blue;
		HeightOffset += OffsetPerElement;
		FVector DebugLocation = TopOfCapsule + CapsuleUp * HeightOffset;
		FString DebugText = FString::Printf(TEXT("MovementMode: %s"), *GetMovementName());
		DrawDebugString(GetWorld(), DebugLocation, DebugText, nullptr, DebugColor, 0.0f, true);

		if (IsInWater())
		{
			HeightOffset += OffsetPerElement;
			DebugLocation = TopOfCapsule + CapsuleUp * HeightOffset;
			DebugText = FString::Printf(TEXT("ImmersionDepth: %.2f"), ImmersionDepth());
			DrawDebugString(GetWorld(), DebugLocation, DebugText, nullptr, DebugColor, 0.0f, true);
		}
	}

	// Jump
	{
		const FColor DebugColor = FColor::Blue;
		HeightOffset += OffsetPerElement;
		FVector DebugLocation = TopOfCapsule + CapsuleUp * HeightOffset;
		FString DebugText = FString::Printf(TEXT("bIsJumping: %d Count: %d HoldTime: %.2f"), CharacterOwner->bPressedJump, CharacterOwner->JumpCurrentCount, CharacterOwner->JumpKeyHoldTime);
		DrawDebugString(GetWorld(), DebugLocation, DebugText, nullptr, DebugColor, 0.0f, true);
	}

	// Root motion (additive)
	if (CurrentRootMotion.HasAdditiveVelocity())
	{
		const FColor DebugColor = FColor::Cyan;
		HeightOffset += OffsetPerElement;
		const FVector DebugLocation = TopOfCapsule + CapsuleUp * HeightOffset;

		FVector CurrentAdditiveVelocity(FVector::ZeroVector);
		CurrentRootMotion.AccumulateAdditiveRootMotionVelocity(0.0f, *CharacterOwner, *this, CurrentAdditiveVelocity);

		DrawDebugDirectionalArrow(GetWorld(), DebugLocation, DebugLocation + CurrentAdditiveVelocity,
			100.0f, DebugColor, false, -1.0f, (uint8)'\000', 10.0f);

		FString DebugText = FString::Printf(TEXT("RootMotionAdditiveVelocity: %s (Speed: %.2f)"),
			*CurrentAdditiveVelocity.ToCompactString(), CurrentAdditiveVelocity.Size());
		DrawDebugString(GetWorld(), DebugLocation + CapsuleUp * 5.0f, DebugText, nullptr, DebugColor, 0.0f, true);
	}

	// Root motion (override)
	if (CurrentRootMotion.HasOverrideVelocity())
	{
		const FColor DebugColor = FColor::Green;
		HeightOffset += OffsetPerElement;
		const FVector DebugLocation = TopOfCapsule + CapsuleUp * HeightOffset;
		FString DebugText = FString::Printf(TEXT("Has Override RootMotion"));
		DrawDebugString(GetWorld(), DebugLocation, DebugText, nullptr, DebugColor, 0.0f, true);
	}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

	return HeightOffset;
}

FVector UNinjaCharacterMovementComponent::ConstrainInputAcceleration(const FVector& InputAcceleration) const
{
	FVector NewAccel = InputAcceleration;

	// Walking or falling pawns ignore up/down sliding
	if (IsMovingOnGround() || IsFalling())
	{
		NewAccel = FVector::VectorPlaneProject(NewAccel, GetComponentAxisZ());
	}

	return NewAccel;
}

void UNinjaCharacterMovementComponent::ServerMoveHandleClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel, const FVector& RelativeClientLoc, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode)
{
	if (!ShouldUsePackedMovementRPCs())
	{
		if (RelativeClientLoc == FVector(1.0f, 2.0f, 3.0f)) // First part of double servermove
		{
			return;
		}
	}

	FNetworkPredictionData_Server_Character* ServerData = GetPredictionData_Server_Character();
	check(ServerData);

	// Don't prevent more recent updates from being sent if received this frame
	// We're going to send out an update anyway, might as well be the most recent one
	APlayerController* PC = Cast<APlayerController>(CharacterOwner->GetController());
	if (ServerData->LastUpdateTime != GetWorld()->TimeSeconds)
	{
		const AGameNetworkManager* GameNetworkManager = (const AGameNetworkManager*)(AGameNetworkManager::StaticClass()->GetDefaultObject());
		if (GameNetworkManager->WithinUpdateDelayBounds(PC, ServerData->LastUpdateTime))
		{
			return;
		}
	}

	// Offset may be relative to base component
	FVector ClientLoc = RelativeClientLoc;
	if (MovementBaseUtility::UseRelativeLocation(ClientMovementBase))
	{
		MovementBaseUtility::TransformLocationToWorld(ClientMovementBase, ClientBaseBoneName, RelativeClientLoc, ClientLoc);
	}
	else
	{
		ClientLoc = FRepMovement::RebaseOntoLocalOrigin(ClientLoc, this);
	}

	FVector ServerLoc = UpdatedComponent->GetComponentLocation();

	// Client may send a null movement base when walking on bases with no relative location (to save bandwidth)
	// In this case don't check movement base in error conditions, use the server one (which avoids an error based on differing bases). Position will still be validated
	if (ClientMovementBase == nullptr)
	{
		TEnumAsByte<EMovementMode> NetMovementMode(MOVE_None);
		TEnumAsByte<EMovementMode> NetGroundMode(MOVE_None);
		uint8 NetCustomMode(0);
		UnpackNetworkMovementMode(ClientMovementMode, NetMovementMode, NetCustomMode, NetGroundMode);
		if (NetMovementMode == MOVE_Walking)
		{
			ClientMovementBase = CharacterOwner->GetBasedMovement().MovementBase;
			ClientBaseBoneName = CharacterOwner->GetBasedMovement().BoneName;
		}
	}

	// If base location is out of sync on server and client, changing base can result in a jarring correction
	// So in the case that the base has just changed on server or client, server trusts the client (within a threshold)
	UPrimitiveComponent* MovementBase = CharacterOwner->GetMovementBase();
	FName MovementBaseBoneName = CharacterOwner->GetBasedMovement().BoneName;
	const bool bServerIsFalling = IsFalling();
	const bool bClientIsFalling = ClientMovementMode == MOVE_Falling;
	const bool bServerJustLanded = bLastServerIsFalling && !bServerIsFalling;
	const bool bClientJustLanded = bLastClientIsFalling && !bClientIsFalling;

	FVector RelativeLocation = ServerLoc;
	FVector RelativeVelocity = Velocity;
	bool bUseLastBase = false;
	bool bFallingWithinAcceptableError = false;

	// Potentially trust the client a little when landing
	static IConsoleVariable* ClientAuthorityThresholdOnBaseChangeVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ClientAuthorityThresholdOnBaseChange"));
	static IConsoleVariable* MaxFallingCorrectionLeashVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.MaxFallingCorrectionLeash"));
	const float ClientAuthorityThreshold = (ClientAuthorityThresholdOnBaseChangeVar != nullptr) ? ClientAuthorityThresholdOnBaseChangeVar->GetFloat() : 0.0f;
	const float MaxFallingCorrectionLeash = (MaxFallingCorrectionLeashVar != nullptr) ? MaxFallingCorrectionLeashVar->GetFloat() : 0.0f;
	const bool bDeferServerCorrectionsWhenFalling = ClientAuthorityThreshold > 0.0f || MaxFallingCorrectionLeash > 0.0f;
	if (bDeferServerCorrectionsWhenFalling)
	{
		// Teleports and other movement modes mean we should just trust the server like we normally would
		if (bTeleportedSinceLastUpdate || (MovementMode != MOVE_Walking && MovementMode != MOVE_Falling))
		{
			MaxServerClientErrorWhileFalling = 0.0f;
			bCanTrustClientOnLanding = false;
		}

		// MaxFallingCorrectionLeash indicates we'll use a variable correction size based on the error on take-off and the direction of movement
		// ClientAuthorityThreshold is an static client-trusting correction upon landing
		// If both are set, use the smaller of the two. If only one is set, use that. If neither are set, we wouldn't even be inside this block
		float MaxLandingCorrection = 0.0f;
		if (ClientAuthorityThreshold > 0.0f && MaxFallingCorrectionLeash > 0.0f)
		{
			MaxLandingCorrection = FMath::Min(ClientAuthorityThreshold, MaxServerClientErrorWhileFalling);
		}
		else
		{
			MaxLandingCorrection = FMath::Max(ClientAuthorityThreshold, MaxServerClientErrorWhileFalling);
		}

		if (bCanTrustClientOnLanding && MaxLandingCorrection > 0.0f && (bClientJustLanded || bServerJustLanded))
		{
			// No longer falling; server should trust client up to a point to finish the landing as the client sees it
			const FVector LocDiff = ServerLoc - ClientLoc;

			if (!LocDiff.IsNearlyZero(UE_KINDA_SMALL_NUMBER))
			{
				if (LocDiff.SizeSquared() < FMath::Square(MaxLandingCorrection))
				{
					ServerLoc = ClientLoc;
					UpdatedComponent->MoveComponent(ServerLoc - UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentQuat(), true, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
					bJustTeleported = true;
				}
				else
				{
					const FVector ClampedDiff = LocDiff.GetSafeNormal() * MaxLandingCorrection;
					ServerLoc -= ClampedDiff;
					UpdatedComponent->MoveComponent(ServerLoc - UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentQuat(), true, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
					bJustTeleported = true;
				}
			}

			MaxServerClientErrorWhileFalling = 0.0f;
			bCanTrustClientOnLanding = false;
		}

		if (bServerIsFalling && bLastServerIsWalking && !bTeleportedSinceLastUpdate)
		{
			float ClientForwardFactor = 1.0f;
			UPrimitiveComponent* LastServerMovementBasePtr = LastServerMovementBaseOVERRIDEN.Get();
			if (IsValid(LastServerMovementBasePtr) && MovementBaseUtility::IsDynamicBase(LastServerMovementBasePtr) && MaxWalkSpeed > UE_KINDA_SMALL_NUMBER)
			{
				const FVector LastBaseVelocity = MovementBaseUtility::GetMovementBaseVelocity(LastServerMovementBasePtr, LastServerMovementBaseBoneName);
				RelativeVelocity = Velocity - LastBaseVelocity;
				const FVector BaseDirection = LastBaseVelocity.GetSafeNormal2D();
				const FVector RelativeDirection = RelativeVelocity * (1.0f / MaxWalkSpeed);

				ClientForwardFactor = FMath::Clamp(FVector::DotProduct(BaseDirection, RelativeDirection), 0.0f, 1.0f);

				// To improve position syncing, use old base for take-off
				if (MovementBaseUtility::UseRelativeLocation(LastServerMovementBasePtr))
				{
					// Relative Location
					MovementBaseUtility::TransformLocationToLocal(LastServerMovementBasePtr, LastServerMovementBaseBoneName, UpdatedComponent->GetComponentLocation(), RelativeLocation);
					bUseLastBase = true;
				}
			}

			if (ClientAuthorityThreshold > 0.0f && ClientForwardFactor < 1.0f)
			{
				const float AdjustedClientAuthorityThreshold = ClientAuthorityThreshold * (1.0f - ClientForwardFactor);
				const FVector LocDiff = ServerLoc - ClientLoc;

				// Potentially trust the client a little when taking off in the opposite direction to the base (to help not get corrected back onto the base)
				if (!LocDiff.IsNearlyZero(UE_KINDA_SMALL_NUMBER))
				{
					if (LocDiff.SizeSquared() < FMath::Square(AdjustedClientAuthorityThreshold))
					{
						ServerLoc = ClientLoc;
						UpdatedComponent->MoveComponent(ServerLoc - UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentQuat(), true, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
						bJustTeleported = true;
					}
					else
					{
						const FVector ClampedDiff = LocDiff.GetSafeNormal() * AdjustedClientAuthorityThreshold;
						ServerLoc -= ClampedDiff;
						UpdatedComponent->MoveComponent(ServerLoc - UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentQuat(), true, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
						bJustTeleported = true;
					}
				}
			}

			if (ClientForwardFactor < 1.0f)
			{
				MaxServerClientErrorWhileFalling = FMath::Min((ServerLoc - ClientLoc).Size() * (1.0f - ClientForwardFactor), MaxFallingCorrectionLeash);
				bCanTrustClientOnLanding = true;
			}
			else
			{
				MaxServerClientErrorWhileFalling = 0.0f;
				bCanTrustClientOnLanding = false;
			}
		}
		else if (!bServerIsFalling && bCanTrustClientOnLanding)
		{
			MaxServerClientErrorWhileFalling = 0.0f;
			bCanTrustClientOnLanding = false;
		}

		if (MaxServerClientErrorWhileFalling > 0.0f && (bServerIsFalling || bClientIsFalling))
		{
			const FVector LocDiff = ServerLoc - ClientLoc;
			if (LocDiff.SizeSquared() <= FMath::Square(MaxServerClientErrorWhileFalling))
			{
				ServerLoc = ClientLoc;
				// Still want a velocity update when we first take off
				bFallingWithinAcceptableError = true;
			}
			else
			{
				// Change ServerLoc to be on the edge of the acceptable error rather than doing a full correction
				// This is not actually changing the server position, but changing it as far as corrections are concerned
				// This means we're just holding the client on a longer leash while we're falling
				static IConsoleVariable* MaxFallingCorrectionLeashBufferVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.MaxFallingCorrectionLeashBuffer"));
				ServerLoc = ServerLoc - LocDiff.GetSafeNormal() * FMath::Clamp(MaxServerClientErrorWhileFalling -
					((MaxFallingCorrectionLeashBufferVar != nullptr) ? MaxFallingCorrectionLeashBufferVar->GetFloat() : 10.0f), 0.0f, MaxServerClientErrorWhileFalling);
			}
		}
	}

	// Compute the client error from the server's position
	// If client has accumulated a noticeable positional error, correct them
	bNetworkLargeClientCorrection = ServerData->bForceClientUpdate;
	if (ServerData->bForceClientUpdate || (!bFallingWithinAcceptableError && ServerCheckClientError(ClientTimeStamp, DeltaTime, Accel, ClientLoc, RelativeClientLoc, ClientMovementBase, ClientBaseBoneName, ClientMovementMode)))
	{
		ServerData->PendingAdjustment.NewVel = Velocity;
		ServerData->PendingAdjustment.NewBase = MovementBase;
		ServerData->PendingAdjustment.NewBaseBoneName = MovementBaseBoneName;
		ServerData->PendingAdjustment.NewLoc = FRepMovement::RebaseOntoZeroOrigin(ServerLoc, this);
		ServerData->PendingAdjustment.NewRot = UpdatedComponent->GetComponentRotation();
		// ServerData->PendingAdjustment.GravityDirection = GetGravityDirection(); // UE 5.4

		ServerData->PendingAdjustment.bBaseRelativePosition = (bDeferServerCorrectionsWhenFalling && bUseLastBase) || MovementBaseUtility::UseRelativeLocation(MovementBase);
		ServerData->PendingAdjustment.bBaseRelativeVelocity = false;

		// Relative location?
		if (ServerData->PendingAdjustment.bBaseRelativePosition)
		{
			if (bDeferServerCorrectionsWhenFalling && bUseLastBase)
			{
				ServerData->PendingAdjustment.NewVel = RelativeVelocity;
				ServerData->PendingAdjustment.NewBase = LastServerMovementBaseOVERRIDEN.Get();
				ServerData->PendingAdjustment.NewBaseBoneName = LastServerMovementBaseBoneName;
				ServerData->PendingAdjustment.NewLoc = RelativeLocation;
			}
			else
			{
				ServerData->PendingAdjustment.NewLoc = CharacterOwner->GetBasedMovement().Location;
				static IConsoleVariable* NetUseBaseRelativeVelocityVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetUseBaseRelativeVelocity"));
				if (NetUseBaseRelativeVelocityVar != nullptr && NetUseBaseRelativeVelocityVar->GetInt())
				{
					// Store world velocity converted to local space of movement base
					ServerData->PendingAdjustment.bBaseRelativeVelocity = true;
					const FVector CurrentVelocity = ServerData->PendingAdjustment.NewVel;
					MovementBaseUtility::TransformDirectionToLocal(MovementBase, MovementBaseBoneName, CurrentVelocity, ServerData->PendingAdjustment.NewVel);
				}
			}

			// TODO: this could be a relative rotation, but all client corrections ignore rotation right now except the root motion one, which would need to be updated
			//ServerData->PendingAdjustment.NewRot = CharacterOwner->GetBasedMovement().Rotation;
		}

#if !UE_BUILD_SHIPPING
		static IConsoleVariable* NetShowCorrectionsVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetShowCorrections"));
		if (NetShowCorrectionsVar != nullptr && NetShowCorrectionsVar->GetInt() != 0)
		{
			const FVector LocDiff = UpdatedComponent->GetComponentLocation() - ClientLoc;
			const FString BaseString = MovementBase ? MovementBase->GetPathName(MovementBase->GetOutermost()) : TEXT("None");
			UE_LOG(LogNetPlayerMovement, Warning, TEXT("*** Server: Error for %s at Time=%.3f is %3.3f LocDiff(%s) ClientLoc(%s) ServerLoc(%s) Base: %s Bone: %s Accel(%s) Velocity(%s)"),
				*GetNameSafe(CharacterOwner), ClientTimeStamp, LocDiff.Size(), *LocDiff.ToString(), *ClientLoc.ToString(), *UpdatedComponent->GetComponentLocation().ToString(), *BaseString, *ServerData->PendingAdjustment.NewBaseBoneName.ToString(), *Accel.ToString(), *Velocity.ToString());
			static IConsoleVariable* NetCorrectionLifetimeVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetCorrectionLifetime"));
			const float DebugLifetime = (NetCorrectionLifetimeVar != nullptr) ? NetCorrectionLifetimeVar->GetFloat() : 4.0f;
			DrawDebugCapsule(GetWorld(), UpdatedComponent->GetComponentLocation(), CharacterOwner->GetSimpleCollisionHalfHeight(), CharacterOwner->GetSimpleCollisionRadius(), UpdatedComponent->GetComponentQuat(), FColor(100, 255, 100), false, DebugLifetime);
			DrawDebugCapsule(GetWorld(), ClientLoc, CharacterOwner->GetSimpleCollisionHalfHeight(), CharacterOwner->GetSimpleCollisionRadius(), UpdatedComponent->GetComponentQuat(), FColor(255, 100, 100), false, DebugLifetime);
		}
#endif

		ServerData->LastUpdateTime = GetWorld()->TimeSeconds;
		ServerData->PendingAdjustment.DeltaTime = DeltaTime;
		ServerData->PendingAdjustment.TimeStamp = ClientTimeStamp;
		ServerData->PendingAdjustment.bAckGoodMove = false;
		ServerData->PendingAdjustment.MovementMode = PackNetworkMovementMode();

#if USE_SERVER_PERF_COUNTERS
		PerfCountersIncrement(PerfCounter_NumServerMoveCorrections);
#endif
	}
	else
	{
		if (ServerShouldUseAuthoritativePosition(ClientTimeStamp, DeltaTime, Accel, ClientLoc, RelativeClientLoc, ClientMovementBase, ClientBaseBoneName, ClientMovementMode))
		{
			const FVector LocDiff = UpdatedComponent->GetComponentLocation() - ClientLoc;
			if (!LocDiff.IsZero() || ClientMovementMode != PackNetworkMovementMode() || GetMovementBase() != ClientMovementBase || (CharacterOwner && CharacterOwner->GetBasedMovement().BoneName != ClientBaseBoneName))
			{
				// Just set the position. On subsequent moves we will resolve initially overlapping conditions
				UpdatedComponent->SetWorldLocation(ClientLoc, false);

				// Trust the client's movement mode
				ApplyNetworkMovementMode(ClientMovementMode);

				// Update base and floor at new location
				SetBase(ClientMovementBase, ClientBaseBoneName);
				UpdateFloorFromAdjustment();

				// Even if base has not changed, we need to recompute the relative offsets (since we've moved)
				SaveBaseLocation();

				LastUpdateLocation = UpdatedComponent ? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
				LastUpdateRotation = UpdatedComponent ? UpdatedComponent->GetComponentQuat() : FQuat::Identity;
				LastUpdateVelocity = Velocity;
			}
		}

		// Acknowledge receipt of this successful ServerMove()
		ServerData->PendingAdjustment.TimeStamp = ClientTimeStamp;
		ServerData->PendingAdjustment.bAckGoodMove = true;
	}

#if USE_SERVER_PERF_COUNTERS
	PerfCountersIncrement(PerfCounter_NumServerMoves);
#endif

	ServerData->bForceClientUpdate = false;

	LastServerMovementBaseOVERRIDEN = MovementBase;
	LastServerMovementBaseBoneName = MovementBaseBoneName;
	bLastClientIsFalling = bClientIsFalling;
	bLastServerIsFalling = bServerIsFalling;
	bLastServerIsWalking = MovementMode == MOVE_Walking;
}

void UNinjaCharacterMovementComponent::ClientAdjustRootMotionPosition_Implementation(float TimeStamp, float ServerMontageTrackPosition, FVector ServerLoc, FVector_NetQuantizeNormal ServerRotation, float ServerVelZ, UPrimitiveComponent* ServerBase, FName ServerBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode)
{
	if (!HasValidData() || !IsActive())
	{
		return;
	}

	const FVector DesiredAxisZ = FNinjaMath::GetAxisZ(GetMoveResponseDataContainer().ClientAdjustment.NewRot);
	const FVector ServerVel = DesiredAxisZ * (GetMoveResponseDataContainer().ClientAdjustment.NewVel | DesiredAxisZ);

	// Call ClientAdjustPosition first; this will Ack the move if it's not outdated
	ClientAdjustPosition_Implementation(TimeStamp, ServerLoc, ServerVel, ServerBase, ServerBaseBoneName, bHasBase, bBaseRelativePosition, ServerMovementMode);

	FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
	check(ClientData);

	// If this adjustment wasn't acknowledged (because outdated), then abort
	if (!ClientData->LastAckedMove.IsValid() || (ClientData->LastAckedMove->TimeStamp != TimeStamp))
	{
		return;
	}

	// We're going to replay Root Motion; this is relative to the Pawn's rotation, so we need to reset that as well
	FRotator DecompressedRot(ServerRotation.X * 180.f, ServerRotation.Y * 180.f, ServerRotation.Z * 180.f);
	CharacterOwner->SetActorRotation(DecompressedRot);
	const FVector ServerLocation(FRepMovement::RebaseOntoLocalOrigin(ServerLoc, UpdatedComponent));
	UE_LOG(LogRootMotion, Log, TEXT("ClientAdjustRootMotionPosition_Implementation TimeStamp: %f, ServerMontageTrackPosition: %f, ServerLocation: %s, ServerRotation: %s, ServerVel: %s, ServerBase: %s"),
		TimeStamp, ServerMontageTrackPosition, *ServerLocation.ToCompactString(), *DecompressedRot.ToCompactString(), *ServerVel.ToCompactString(), *GetNameSafe(ServerBase));

	// DEBUG - get some insight on where errors came from
	if (false)
	{
		const FVector DeltaLocation = ServerLocation - ClientData->LastAckedMove->SavedLocation;
		const FRotator DeltaRotation = (DecompressedRot - ClientData->LastAckedMove->SavedRotation).GetNormalized();
		const float DeltaTrackPosition = (ServerMontageTrackPosition - ClientData->LastAckedMove->RootMotionTrackPosition);

		UE_LOG(LogRootMotion, Log, TEXT("\tErrors DeltaLocation: %s, DeltaRotation: %s, DeltaTrackPosition: %f"),
			*DeltaLocation.ToCompactString(), *DeltaRotation.ToCompactString(), DeltaTrackPosition);
	}

	// Server disagrees with Client on the Root Motion AnimMontage Track position
	if (CharacterOwner->bClientResimulateRootMotion || (ServerMontageTrackPosition != ClientData->LastAckedMove->RootMotionTrackPosition))
	{
		// Not much we can do there unfortunately, just jump to server's track position
		FAnimMontageInstance* RootMotionMontageInstance = CharacterOwner->GetRootMotionAnimMontageInstance();
		if (RootMotionMontageInstance && !RootMotionMontageInstance->IsRootMotionDisabled())
		{
			UE_LOG(LogRootMotion, Log, TEXT("\tServer disagrees with Client's track position!! ServerTrackPosition: %f, ClientTrackPosition: %f, DeltaTrackPosition: %f. TimeStamp: %f, Character: %s, Montage: %s"),
				ServerMontageTrackPosition, ClientData->LastAckedMove->RootMotionTrackPosition, (ServerMontageTrackPosition - ClientData->LastAckedMove->RootMotionTrackPosition), TimeStamp, *GetNameSafe(CharacterOwner), *GetNameSafe(RootMotionMontageInstance->Montage));

			RootMotionMontageInstance->SetPosition(ServerMontageTrackPosition);
			CharacterOwner->bClientResimulateRootMotion = true;
		}
	}
}

void UNinjaCharacterMovementComponent::ClientAdjustRootMotionSourcePosition_Implementation(float TimeStamp, FRootMotionSourceGroup ServerRootMotion, bool bHasAnimRootMotion, float ServerMontageTrackPosition, FVector ServerLoc, FVector_NetQuantizeNormal ServerRotation, float ServerVelZ, UPrimitiveComponent* ServerBase, FName ServerBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode)
{
	if (!HasValidData() || !IsActive())
	{
		return;
	}

#if ROOT_MOTION_DEBUG
	if (RootMotionSourceDebug::CVarDebugRootMotionSources.GetValueOnGameThread() == 1)
	{
		FString AdjustedDebugString = FString::Printf(TEXT("ClientAdjustRootMotionSourcePosition_Implementation TimeStamp(%f)"),
			TimeStamp);
		RootMotionSourceDebug::PrintOnScreen(*CharacterOwner, AdjustedDebugString);
	}
#endif

	const FVector DesiredAxisZ = FNinjaMath::GetAxisZ(GetMoveResponseDataContainer().ClientAdjustment.NewRot);
	const FVector ServerVel = DesiredAxisZ * (GetMoveResponseDataContainer().ClientAdjustment.NewVel | DesiredAxisZ);

	// Call ClientAdjustPosition first; this will Ack the move if it's not outdated
	ClientAdjustPosition_Implementation(TimeStamp, ServerLoc, ServerVel, ServerBase, ServerBaseBoneName, bHasBase, bBaseRelativePosition, ServerMovementMode);

	FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
	check(ClientData);

	// If this adjustment wasn't acknowledged (because outdated), then abort
	if (!ClientData->LastAckedMove.IsValid() || (ClientData->LastAckedMove->TimeStamp != TimeStamp))
	{
		return;
	}

	// We're going to replay Root Motion; this can be relative to the Pawn's rotation, so we need to reset that as well
	FRotator DecompressedRot(ServerRotation.X * 180.f, ServerRotation.Y * 180.f, ServerRotation.Z * 180.f);
	CharacterOwner->SetActorRotation(DecompressedRot);
	const FVector ServerLocation(FRepMovement::RebaseOntoLocalOrigin(ServerLoc, UpdatedComponent));
	UE_LOG(LogRootMotion, Log, TEXT("ClientAdjustRootMotionSourcePosition_Implementation TimeStamp: %f, NumRootMotionSources: %d, ServerLocation: %s, ServerRotation: %s, ServerVel: %s, ServerBase: %s"),
		TimeStamp, ServerRootMotion.RootMotionSources.Num(), *ServerLocation.ToCompactString(), *DecompressedRot.ToCompactString(), *ServerVel.ToCompactString(), *GetNameSafe(ServerBase));

	// Handle AnimRootMotion correction
	if (bHasAnimRootMotion)
	{
		// DEBUG - get some insight on where errors came from
		if (false)
		{
			const FVector DeltaLocation = ServerLocation - ClientData->LastAckedMove->SavedLocation;
			const FRotator DeltaRotation = (DecompressedRot - ClientData->LastAckedMove->SavedRotation).GetNormalized();
			const float DeltaTrackPosition = (ServerMontageTrackPosition - ClientData->LastAckedMove->RootMotionTrackPosition);

			UE_LOG(LogRootMotion, Log, TEXT("\tErrors DeltaLocation: %s, DeltaRotation: %s, DeltaTrackPosition: %f"),
				*DeltaLocation.ToCompactString(), *DeltaRotation.ToCompactString(), DeltaTrackPosition);
		}

		// Server disagrees with Client on the Root Motion AnimMontage Track position
		if (CharacterOwner->bClientResimulateRootMotion || (ServerMontageTrackPosition != ClientData->LastAckedMove->RootMotionTrackPosition))
		{
			UE_LOG(LogRootMotion, Log, TEXT("\tServer disagrees with Client's track position!! ServerTrackPosition: %f, ClientTrackPosition: %f, DeltaTrackPosition: %f. TimeStamp: %f"),
				ServerMontageTrackPosition, ClientData->LastAckedMove->RootMotionTrackPosition, (ServerMontageTrackPosition - ClientData->LastAckedMove->RootMotionTrackPosition), TimeStamp);

			// Not much we can do there unfortunately, just jump to server's track position
			FAnimMontageInstance* RootMotionMontageInstance = CharacterOwner->GetRootMotionAnimMontageInstance();
			if (RootMotionMontageInstance && !RootMotionMontageInstance->IsRootMotionDisabled())
			{
				RootMotionMontageInstance->SetPosition(ServerMontageTrackPosition);
				CharacterOwner->bClientResimulateRootMotion = true;
			}
		}
	}

	// First we need to convert Server IDs -> Local IDs in ServerRootMotion for comparison
	ConvertRootMotionServerIDsToLocalIDs(ClientData->LastAckedMove->SavedRootMotion, ServerRootMotion, TimeStamp);

	// Cull ServerRootMotion of any root motion sources that don't match ones we have in this move
	ServerRootMotion.CullInvalidSources();

	// Server disagrees with Client on Root Motion state
	if (CharacterOwner->bClientResimulateRootMotionSources || (ServerRootMotion != ClientData->LastAckedMove->SavedRootMotion))
	{
		if (!CharacterOwner->bClientResimulateRootMotionSources)
		{
			UE_LOG(LogNetPlayerMovement, VeryVerbose, TEXT("ClientAdjustRootMotionSourcePosition called, server/LastAckedMove mismatch"));
		}

		CharacterOwner->SavedRootMotion = ServerRootMotion;
		CharacterOwner->bClientResimulateRootMotionSources = true;
	}
}

void UNinjaCharacterMovementComponent::OnClientCorrectionReceived(FNetworkPredictionData_Client_Character& ClientData, float TimeStamp, FVector NewLocation, FVector NewVelocity, UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode, FVector ServerGravityDirection)
{
#if !UE_BUILD_SHIPPING
	static IConsoleVariable* NetShowCorrectionsVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetShowCorrections"));
	if (NetShowCorrectionsVar != nullptr && NetShowCorrectionsVar->GetInt() != 0)
	{
		const FVector ClientLocAtCorrectedMove = ClientData.LastAckedMove.IsValid() ? ClientData.LastAckedMove->SavedLocation : UpdatedComponent->GetComponentLocation();
		const FVector LocDiff = ClientLocAtCorrectedMove - NewLocation;
		const FString NewBaseString = NewBase ? NewBase->GetPathName(NewBase->GetOutermost()) : TEXT("None");
		UE_LOG(LogNetPlayerMovement, Warning, TEXT("*** Client: Error for %s at Time=%.3f is %3.3f LocDiff(%s) ClientLoc(%s) ServerLoc(%s) NewBase: %s NewBone: %s ClientVel(%s) ServerVel(%s) SavedMoves %d"),
			*GetNameSafe(CharacterOwner), TimeStamp, LocDiff.Size(), *LocDiff.ToString(), *ClientLocAtCorrectedMove.ToString(), *NewLocation.ToString(), *NewBaseString, *NewBaseBoneName.ToString(), *Velocity.ToString(), *NewVelocity.ToString(), ClientData.SavedMoves.Num());
		static IConsoleVariable* NetCorrectionLifetimeVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetCorrectionLifetime"));
		const float DebugLifetime = (NetCorrectionLifetimeVar != nullptr) ? NetCorrectionLifetimeVar->GetFloat() : 4.0f;
		if (!LocDiff.IsNearlyZero())
		{
			// When server corrects us to a new location, draw red at location where client thought they were, green where the server corrected us to
			DrawDebugCapsule(GetWorld(), ClientLocAtCorrectedMove, CharacterOwner->GetSimpleCollisionHalfHeight(), CharacterOwner->GetSimpleCollisionRadius(), UpdatedComponent->GetComponentQuat(), FColor(255, 100, 100), false, DebugLifetime);
			DrawDebugCapsule(GetWorld(), NewLocation, CharacterOwner->GetSimpleCollisionHalfHeight(), CharacterOwner->GetSimpleCollisionRadius(), UpdatedComponent->GetComponentQuat(), FColor(100, 255, 100), false, DebugLifetime);
		}
		else
		{
			// When we receive a server correction that doesn't change our position from where our client move had us, draw yellow (otherwise would be overlapping)
			// This occurs when we receive an initial correction, replay moves to get us into the right location, and then receive subsequent corrections by the server (who doesn't know if we corrected already
			// so continues to send corrections). This is a "no-op" server correction with regards to location since we already corrected (occurs with latency)
			DrawDebugCapsule(GetWorld(), NewLocation, CharacterOwner->GetSimpleCollisionHalfHeight(), CharacterOwner->GetSimpleCollisionRadius(), UpdatedComponent->GetComponentQuat(), FColor(255, 255, 100), false, DebugLifetime);
		}
	}
#endif //!UE_BUILD_SHIPPING

#if ROOT_MOTION_DEBUG
	if (RootMotionSourceDebug::CVarDebugRootMotionSources.GetValueOnGameThread() == 1)
	{
		const FVector VelocityCorrection = NewVelocity - Velocity;
		FString AdjustedDebugString = FString::Printf(TEXT("PerformMovement ClientAdjustPosition_Implementation Velocity(%s) OldVelocity(%s) Correction(%s) TimeStamp(%f)"),
			*NewVelocity.ToCompactString(), *Velocity.ToCompactString(), *VelocityCorrection.ToCompactString(), TimeStamp);
		RootMotionSourceDebug::PrintOnScreen(*CharacterOwner, AdjustedDebugString);
	}
#endif
}

void UNinjaCharacterMovementComponent::CapsuleTouched(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!bEnablePhysicsInteraction)
	{
		return;
	}

	if (OtherComp != NULL && OtherComp->IsAnySimulatingPhysics())
	{
		const FVector OtherLoc = OtherComp->GetComponentLocation();
		const FVector Loc = UpdatedComponent->GetComponentLocation();
		const FVector CapsuleUp = GetComponentAxisZ();

		FVector ImpulseDir = FVector::VectorPlaneProject(OtherLoc - Loc, CapsuleUp) + CapsuleUp * 0.25f;
		ImpulseDir = (ImpulseDir.GetSafeNormal() + FVector::VectorPlaneProject(Velocity, CapsuleUp).GetSafeNormal()) * 0.5f;
		ImpulseDir.Normalize();

		FName BoneName = NAME_None;
		if (OtherBodyIndex != INDEX_NONE)
		{
			BoneName = ((USkinnedMeshComponent*)OtherComp)->GetBoneName(OtherBodyIndex);
		}

		float TouchForceFactorModified = TouchForceFactor;

		if (bTouchForceScaledToMass)
		{
			FBodyInstance* BI = OtherComp->GetBodyInstance(BoneName);
			TouchForceFactorModified *= BI ? BI->GetBodyMass() : 1.0f;
		}

		float ImpulseStrength = FMath::Clamp<FVector::FReal>(FVector::VectorPlaneProject(Velocity, CapsuleUp).Size() * TouchForceFactorModified,
			MinTouchForce > 0.0f ? MinTouchForce : -FLT_MAX, MaxTouchForce > 0.0f ? MaxTouchForce : FLT_MAX);

		FVector Impulse = ImpulseDir * ImpulseStrength;

		OtherComp->AddImpulse(Impulse, BoneName);
	}
}

void UNinjaCharacterMovementComponent::ApplyDownwardForce(float DeltaSeconds)
{
	if (StandingDownwardForceScale != 0.0f && CurrentFloor.HitResult.IsValidBlockingHit())
	{
		UPrimitiveComponent* BaseComp = CurrentFloor.HitResult.GetComponent();
		const FVector Gravity = GetGravity();

		if (BaseComp && BaseComp->IsAnySimulatingPhysics() && !Gravity.IsZero())
		{
			BaseComp->AddForceAtLocation(Gravity * Mass * StandingDownwardForceScale, CurrentFloor.HitResult.ImpactPoint, CurrentFloor.HitResult.BoneName);
		}
	}
}

void UNinjaCharacterMovementComponent::ApplyRepulsionForce(float DeltaSeconds)
{
	if (UpdatedPrimitive && RepulsionForce > 0.0f && CharacterOwner != nullptr)
	{
		const TArray<FOverlapInfo>& Overlaps = UpdatedPrimitive->GetOverlapInfos();
		if (Overlaps.Num() > 0)
		{
			FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CMC_ApplyRepulsionForce));
			QueryParams.bReturnFaceIndex = false;
			QueryParams.bReturnPhysicalMaterial = false;

			float CapsuleRadius = 0.0f;
			float CapsuleHalfHeight = 0.0f;
			CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleSize(CapsuleRadius, CapsuleHalfHeight);
			const float RepulsionForceRadius = CapsuleRadius * 1.2f;
			const float StopBodyDistance = 2.5f;
			const FVector MyLocation = UpdatedPrimitive->GetComponentLocation();
			const FVector CapsuleDown = GetComponentAxisZ() * -1.0f;

			for (int32 i = 0; i < Overlaps.Num(); i++)
			{
				const FOverlapInfo& Overlap = Overlaps[i];

				UPrimitiveComponent* OverlapComp = Overlap.OverlapInfo.Component.Get();
				if (!OverlapComp || OverlapComp->Mobility < EComponentMobility::Movable)
				{
					continue;
				}

				// Use the body instead of the component for cases where we have multi-body overlaps enabled
				FBodyInstance* OverlapBody = nullptr;
				const int32 OverlapBodyIndex = Overlap.GetBodyIndex();
				const USkeletalMeshComponent* SkelMeshForBody = (OverlapBodyIndex != INDEX_NONE) ? Cast<USkeletalMeshComponent>(OverlapComp) : nullptr;
				if (SkelMeshForBody != nullptr)
				{
					OverlapBody = SkelMeshForBody->Bodies.IsValidIndex(OverlapBodyIndex) ? SkelMeshForBody->Bodies[OverlapBodyIndex] : nullptr;
				}
				else
				{
					OverlapBody = OverlapComp->GetBodyInstance();
				}

				if (!OverlapBody)
				{
					UE_LOG(LogCharacterMovement, Warning, TEXT("%s could not find overlap body for body index %d"), *GetName(), OverlapBodyIndex);
					continue;
				}

				if (!OverlapBody->IsInstanceSimulatingPhysics())
				{
					continue;
				}

				FTransform BodyTransform = OverlapBody->GetUnrealWorldTransform();

				const FVector BodyVelocity = OverlapBody->GetUnrealWorldVelocity();
				const FVector BodyLocation = BodyTransform.GetLocation();
				const FVector LineTraceEnd = MyLocation + CapsuleDown * ((BodyLocation - MyLocation) | CapsuleDown);

				// Trace to get the hit location on the capsule
				FHitResult Hit;
				bool bHasHit = UpdatedPrimitive->LineTraceComponent(Hit, BodyLocation, LineTraceEnd, QueryParams);

				FVector HitLoc = Hit.ImpactPoint;
				bool bIsPenetrating = Hit.bStartPenetrating || Hit.PenetrationDepth > StopBodyDistance;

				// If we didn't hit the capsule, we're inside the capsule
				if (!bHasHit)
				{
					HitLoc = BodyLocation;
					bIsPenetrating = true;
				}

				const float DistanceNow = FVector::VectorPlaneProject(HitLoc - BodyLocation, CapsuleDown).SizeSquared();
				const float DistanceLater = FVector::VectorPlaneProject(HitLoc - (BodyLocation + BodyVelocity * DeltaSeconds), CapsuleDown).SizeSquared();

				if (bHasHit && DistanceNow < StopBodyDistance && !bIsPenetrating)
				{
					OverlapBody->SetLinearVelocity(FVector::ZeroVector, false);
				}
				else if (DistanceLater <= DistanceNow || bIsPenetrating)
				{
					FVector ForceCenter = MyLocation;

					if (bHasHit)
					{
						ForceCenter += CapsuleDown * ((HitLoc - MyLocation) | CapsuleDown);
					}
					else
					{
						// Get the axis of the capsule bounded by the following two end points
						const FVector BottomPoint = ForceCenter + CapsuleDown * CapsuleHalfHeight;
						const FVector TopPoint = ForceCenter - CapsuleDown * CapsuleHalfHeight;
						const FVector Segment = TopPoint - BottomPoint;

						// Project the foreign body location on the segment
						const float Alpha = ((BodyLocation - BottomPoint) | Segment) / Segment.SizeSquared();

						if (Alpha < 0.0f)
						{
							ForceCenter = BottomPoint;
						}
						else if (Alpha > 1.0f)
						{
							ForceCenter = TopPoint;
						}
					}

					OverlapBody->AddRadialForceToBody(ForceCenter, RepulsionForceRadius, RepulsionForce * Mass, ERadialImpulseFalloff::RIF_Constant);
				}
			}
		}
	}
}

void UNinjaCharacterMovementComponent::ApplyAccumulatedForces(float DeltaSeconds)
{
	if ((!PendingImpulseToApply.IsZero() || !PendingForceToApply.IsZero()) && IsMovingOnGround())
	{
		const FVector Impulse = PendingImpulseToApply + PendingForceToApply * DeltaSeconds + GetGravity() * DeltaSeconds;

		// Check to see if applied momentum is enough to overcome gravity
		if ((Impulse | GetComponentAxisZ()) > UE_SMALL_NUMBER)
		{
			SetMovementMode(MOVE_Falling);
		}
	}

	Velocity += PendingImpulseToApply + PendingForceToApply * DeltaSeconds;

	// Don't call ClearAccumulatedForces() because it could affect launch velocity
	PendingImpulseToApply = FVector::ZeroVector;
	PendingForceToApply = FVector::ZeroVector;
}

void UNinjaCharacterMovementComponent::OnComponentHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!bTriggerUnwalkableHits)
	{
		return;
	}

	// Try to walk on unwalkable blocking object if needed
	const float CurrentHitTime = GetWorld()->GetRealTimeSeconds();
	if (CurrentHitTime - LastUnwalkableHitTime >= MIN_TICK_TIME && !IsWalkable(Hit) &&
		((Hit.TraceEnd - Hit.TraceStart) | Hit.ImpactNormal) < -UE_KINDA_SMALL_NUMBER)
	{
		// Store current timestamp
		LastUnwalkableHitTime = CurrentHitTime;

		UnwalkableHit(Hit);
	}
}

void UNinjaCharacterMovementComponent::UnwalkableHit(const FHitResult& Hit)
{
	OnUnwalkableHit(Hit);

	// Call owner delegate
	ANinjaCharacter* Ninja = Cast<ANinjaCharacter>(CharacterOwner);
	if (Ninja != nullptr)
	{
		Ninja->UnwalkableHit(Hit);
	}
}

void UNinjaCharacterMovementComponent::OnUnwalkableHit(const FHitResult& Hit)
{
}

bool UNinjaCharacterMovementComponent::ShouldReplicateGravity() const
{
	return (!bDisableGravityReplication && CharacterOwner != nullptr &&
		CharacterOwner->HasAuthority() && GetNetMode() != ENetMode::NM_Standalone);
}

FVector UNinjaCharacterMovementComponent::GetGravity() const
{
	SCOPE_CYCLE_COUNTER(STAT_CharacterMovementGetGravity);

	if (!HasValidData())
	{
		return FVector(0.0f, 0.0f, GetGravityZ());
	}

	const float GravScale = GetFinalGravityScale();
	if (GravScale == 0.0f)
	{
		return FVector::ZeroVector;
	}

	FVector Gravity = FVector::ZeroVector;

	switch (GravityDirectionMode)
	{
		case ENinjaGravityDirectionMode::Fixed:
		{
			Gravity = GravityVectorA * (FMath::Abs(GetUnscaledGravityZ()) * GravScale);
			break;
		}

		case ENinjaGravityDirectionMode::SplineTangent:
		{
			if (IsValid(GravityActor))
			{
				const USplineComponent* Spline = Cast<USplineComponent>(
					GravityActor->GetComponentByClass(USplineComponent::StaticClass()));
				if (Spline != nullptr)
				{
					UNinjaCharacterMovementComponent* MutableThis = const_cast<UNinjaCharacterMovementComponent*>(this);
					MutableThis->GravityVectorA = Spline->FindDirectionClosestToWorldLocation(
						UpdatedComponent->GetComponentLocation(), ESplineCoordinateSpace::Type::World);
				}
			}

			Gravity = GravityVectorA * (FMath::Abs(GetUnscaledGravityZ()) * GravScale);
			break;
		}

		case ENinjaGravityDirectionMode::Point:
		{
			if (IsValid(GravityActor))
			{
				UNinjaCharacterMovementComponent* MutableThis = const_cast<UNinjaCharacterMovementComponent*>(this);
				MutableThis->GravityVectorA = GravityActor->GetActorLocation();
			}

			const FVector GravityDir = GravityVectorA - UpdatedComponent->GetComponentLocation();
			if (!GravityDir.IsZero())
			{
				Gravity = GravityDir.GetSafeNormal() * (FMath::Abs(GetUnscaledGravityZ()) * GravScale);
			}

			break;
		}

		case ENinjaGravityDirectionMode::Line:
		{
			const FVector GravityDir = FMath::ClosestPointOnInfiniteLine(GravityVectorA,
				GravityVectorB, UpdatedComponent->GetComponentLocation()) - UpdatedComponent->GetComponentLocation();
			if (!GravityDir.IsZero())
			{
				Gravity = GravityDir.GetSafeNormal() * (FMath::Abs(GetUnscaledGravityZ()) * GravScale);
			}

			break;
		}

		case ENinjaGravityDirectionMode::Segment:
		{
			const FVector GravityDir = FMath::ClosestPointOnLine(GravityVectorA,
				GravityVectorB, UpdatedComponent->GetComponentLocation()) - UpdatedComponent->GetComponentLocation();
			if (!GravityDir.IsZero())
			{
				Gravity = GravityDir.GetSafeNormal() * (FMath::Abs(GetUnscaledGravityZ()) * GravScale);
			}

			break;
		}

		case ENinjaGravityDirectionMode::Spline:
		{
			if (IsValid(GravityActor))
			{
				const USplineComponent* Spline = Cast<USplineComponent>(
					GravityActor->GetComponentByClass(USplineComponent::StaticClass()));
				if (Spline != nullptr)
				{
					UNinjaCharacterMovementComponent* MutableThis = const_cast<UNinjaCharacterMovementComponent*>(this);
					MutableThis->GravityVectorA = Spline->FindLocationClosestToWorldLocation(
						UpdatedComponent->GetComponentLocation(), ESplineCoordinateSpace::Type::World);
				}
			}

			const FVector GravityDir = GravityVectorA - UpdatedComponent->GetComponentLocation();
			if (!GravityDir.IsZero())
			{
				Gravity = GravityDir.GetSafeNormal() * (FMath::Abs(GetUnscaledGravityZ()) * GravScale);
			}

			break;
		}

		case ENinjaGravityDirectionMode::Plane:
		{
			const FVector GravityDir = FVector::PointPlaneProject(UpdatedComponent->GetComponentLocation(),
				GravityVectorA, GravityVectorB) - UpdatedComponent->GetComponentLocation();
			if (!GravityDir.IsZero())
			{
				Gravity = GravityDir.GetSafeNormal() * (FMath::Abs(GetUnscaledGravityZ()) * GravScale);
			}

			break;
		}

		case ENinjaGravityDirectionMode::SplinePlane:
		{
			if (IsValid(GravityActor))
			{
				const USplineComponent* Spline = Cast<USplineComponent>(
					GravityActor->GetComponentByClass(USplineComponent::StaticClass()));
				if (Spline != nullptr)
				{
					const float InputKey = Spline->FindInputKeyClosestToWorldLocation(
						UpdatedComponent->GetComponentLocation());
					const FVector ClosestLocation = Spline->GetLocationAtSplineInputKey(
						InputKey, ESplineCoordinateSpace::Type::World);
					const FVector ClosestUpVector = Spline->GetUpVectorAtSplineInputKey(
						InputKey, ESplineCoordinateSpace::Type::World);

					UNinjaCharacterMovementComponent* MutableThis = const_cast<UNinjaCharacterMovementComponent*>(this);
					MutableThis->GravityVectorA = FVector::PointPlaneProject(UpdatedComponent->GetComponentLocation(),
						ClosestLocation, ClosestUpVector);
					MutableThis->GravityVectorB = ClosestUpVector;
				}
			}

			const FVector GravityDir = GravityVectorA - UpdatedComponent->GetComponentLocation();
			if (!GravityDir.IsZero())
			{
				Gravity = GravityDir.GetSafeNormal() * (FMath::Abs(GetUnscaledGravityZ()) * GravScale);
			}

			break;
		}

		case ENinjaGravityDirectionMode::Box:
		{
			if (IsValid(GravityActor))
			{
				UNinjaCharacterMovementComponent* MutableThis = const_cast<UNinjaCharacterMovementComponent*>(this);
				GravityActor->GetActorBounds(true, MutableThis->GravityVectorA, MutableThis->GravityVectorB);
			}

			const FVector GravityDir = FBox(GravityVectorA - GravityVectorB,
				GravityVectorA + GravityVectorB).GetClosestPointTo(UpdatedComponent->GetComponentLocation()) -
				UpdatedComponent->GetComponentLocation();
			if (!GravityDir.IsZero())
			{
				Gravity = GravityDir.GetSafeNormal() * (FMath::Abs(GetUnscaledGravityZ()) * GravScale);
			}

			break;
		}

		case ENinjaGravityDirectionMode::Collision:
		{
			if (IsValid(GravityActor))
			{
				FVector ClosestPoint;
				if (Cast<UPrimitiveComponent>(GravityActor->GetRootComponent())->GetClosestPointOnCollision(
					UpdatedComponent->GetComponentLocation(), ClosestPoint) > 0.0f)
				{
					UNinjaCharacterMovementComponent* MutableThis = const_cast<UNinjaCharacterMovementComponent*>(this);
					MutableThis->GravityVectorA = ClosestPoint;
				}
			}

			const FVector GravityDir = GravityVectorA - UpdatedComponent->GetComponentLocation();
			if (!GravityDir.IsZero())
			{
				Gravity = GravityDir.GetSafeNormal() * (FMath::Abs(GetUnscaledGravityZ()) * GravScale);
			}

			break;
		}
	}

	return Gravity;
}

FVector UNinjaCharacterMovementComponent::K2_GetGravityDirection(bool bAvoidZeroGravity) const
{
	return GetGravityDirection(bAvoidZeroGravity);
}

FVector UNinjaCharacterMovementComponent::GetGravityDirection(bool bAvoidZeroGravity) const
{
	SCOPE_CYCLE_COUNTER(STAT_CharacterMovementGetGravityDirection);

	if (!HasValidData())
	{
		return FVector::DownVector;
	}

	FVector GravityDir = FVector::ZeroVector;
	const float GravScale = GetFinalGravityScale();

	// Gravity direction can be influenced by gravity scales
	if (GravScale != 0.0f)
	{
		switch (GravityDirectionMode)
		{
			case ENinjaGravityDirectionMode::Fixed:
			{
				GravityDir = GravityVectorA * ((GravScale > 0.0f) ? 1.0f : -1.0f);
				break;
			}

			case ENinjaGravityDirectionMode::SplineTangent:
			{
				if (IsValid(GravityActor))
				{
					const USplineComponent* Spline = Cast<USplineComponent>(
						GravityActor->GetComponentByClass(USplineComponent::StaticClass()));
					if (Spline != nullptr)
					{
						UNinjaCharacterMovementComponent* MutableThis = const_cast<UNinjaCharacterMovementComponent*>(this);
						MutableThis->GravityVectorA = Spline->FindDirectionClosestToWorldLocation(
							UpdatedComponent->GetComponentLocation(), ESplineCoordinateSpace::Type::World);
					}
				}

				GravityDir = GravityVectorA * ((GravScale > 0.0f) ? 1.0f : -1.0f);
				break;
			}

			case ENinjaGravityDirectionMode::Point:
			{
				if (IsValid(GravityActor))
				{
					UNinjaCharacterMovementComponent* MutableThis = const_cast<UNinjaCharacterMovementComponent*>(this);
					MutableThis->GravityVectorA = GravityActor->GetActorLocation();
				}

				GravityDir = GravityVectorA - UpdatedComponent->GetComponentLocation();
				if (!GravityDir.IsZero())
				{
					GravityDir = GravityDir.GetSafeNormal() * ((GravScale > 0.0f) ? 1.0f : -1.0f);
				}

				break;
			}

			case ENinjaGravityDirectionMode::Line:
			{
				GravityDir = FMath::ClosestPointOnInfiniteLine(GravityVectorA,
					GravityVectorB, UpdatedComponent->GetComponentLocation()) - UpdatedComponent->GetComponentLocation();
				if (!GravityDir.IsZero())
				{
					GravityDir = GravityDir.GetSafeNormal() * ((GravScale > 0.0f) ? 1.0f : -1.0f);
				}

				break;
			}

			case ENinjaGravityDirectionMode::Segment:
			{
				GravityDir = FMath::ClosestPointOnLine(GravityVectorA,
					GravityVectorB, UpdatedComponent->GetComponentLocation()) - UpdatedComponent->GetComponentLocation();
				if (!GravityDir.IsZero())
				{
					GravityDir = GravityDir.GetSafeNormal() * ((GravScale > 0.0f) ? 1.0f : -1.0f);
				}

				break;
			}

			case ENinjaGravityDirectionMode::Spline:
			{
				if (IsValid(GravityActor))
				{
					const USplineComponent* Spline = Cast<USplineComponent>(
						GravityActor->GetComponentByClass(USplineComponent::StaticClass()));
					if (Spline != nullptr)
					{
						UNinjaCharacterMovementComponent* MutableThis = const_cast<UNinjaCharacterMovementComponent*>(this);
						MutableThis->GravityVectorA = Spline->FindLocationClosestToWorldLocation(
							UpdatedComponent->GetComponentLocation(), ESplineCoordinateSpace::Type::World);
					}
				}

				GravityDir = GravityVectorA - UpdatedComponent->GetComponentLocation();
				if (!GravityDir.IsZero())
				{
					GravityDir = GravityDir.GetSafeNormal() * ((GravScale > 0.0f) ? 1.0f : -1.0f);
				}

				break;
			}

			case ENinjaGravityDirectionMode::Plane:
			{
				GravityDir = FVector::PointPlaneProject(UpdatedComponent->GetComponentLocation(),
					GravityVectorA, GravityVectorB) - UpdatedComponent->GetComponentLocation();
				if (!GravityDir.IsZero())
				{
					GravityDir = GravityDir.GetSafeNormal() * ((GravScale > 0.0f) ? 1.0f : -1.0f);
				}

				break;
			}

			case ENinjaGravityDirectionMode::SplinePlane:
			{
				if (IsValid(GravityActor))
				{
					const USplineComponent* Spline = Cast<USplineComponent>(
						GravityActor->GetComponentByClass(USplineComponent::StaticClass()));
					if (Spline != nullptr)
					{
						const float InputKey = Spline->FindInputKeyClosestToWorldLocation(
							UpdatedComponent->GetComponentLocation());
						const FVector ClosestLocation = Spline->GetLocationAtSplineInputKey(
							InputKey, ESplineCoordinateSpace::Type::World);
						const FVector ClosestUpVector = Spline->GetUpVectorAtSplineInputKey(
							InputKey, ESplineCoordinateSpace::Type::World);

						UNinjaCharacterMovementComponent* MutableThis = const_cast<UNinjaCharacterMovementComponent*>(this);
						MutableThis->GravityVectorA = FVector::PointPlaneProject(UpdatedComponent->GetComponentLocation(),
							ClosestLocation, ClosestUpVector);
						MutableThis->GravityVectorB = ClosestUpVector;
					}
				}

				GravityDir = GravityVectorA - UpdatedComponent->GetComponentLocation();
				if (!GravityDir.IsZero())
				{
					GravityDir = GravityDir.GetSafeNormal() * ((GravScale > 0.0f) ? 1.0f : -1.0f);
				}

				break;
			}

			case ENinjaGravityDirectionMode::Box:
			{
				if (IsValid(GravityActor))
				{
					UNinjaCharacterMovementComponent* MutableThis = const_cast<UNinjaCharacterMovementComponent*>(this);
					GravityActor->GetActorBounds(true, MutableThis->GravityVectorA, MutableThis->GravityVectorB);
				}

				GravityDir = FBox(GravityVectorA - GravityVectorB, GravityVectorA + GravityVectorB).GetClosestPointTo(
					UpdatedComponent->GetComponentLocation()) - UpdatedComponent->GetComponentLocation();
				if (!GravityDir.IsZero())
				{
					GravityDir = GravityDir.GetSafeNormal() * ((GravScale > 0.0f) ? 1.0f : -1.0f);
				}

				break;
			}

			case ENinjaGravityDirectionMode::Collision:
			{
				if (IsValid(GravityActor))
				{
					FVector ClosestPoint;
					if (Cast<UPrimitiveComponent>(GravityActor->GetRootComponent())->GetClosestPointOnCollision(
						UpdatedComponent->GetComponentLocation(), ClosestPoint) > 0.0f)
					{
						UNinjaCharacterMovementComponent* MutableThis = const_cast<UNinjaCharacterMovementComponent*>(this);
						MutableThis->GravityVectorA = ClosestPoint;
					}
				}

				GravityDir = GravityVectorA - UpdatedComponent->GetComponentLocation();
				if (!GravityDir.IsZero())
				{
					GravityDir = GravityDir.GetSafeNormal() * ((GravScale > 0.0f) ? 1.0f : -1.0f);
				}

				break;
			}
		}

		if (bAvoidZeroGravity && GravityDir.IsZero())
		{
			GravityDir = FVector(0.0f, 0.0f,
				((GetUnscaledGravityZ() > 0.0f) ? 1.0f : -1.0f) * ((GravScale > 0.0f) ? 1.0f : -1.0f));
		}
	}
	else
	{
		if (bAvoidZeroGravity)
		{
			switch (GravityDirectionMode)
			{
				case ENinjaGravityDirectionMode::Fixed:
				{
					GravityDir = GravityVectorA;
					break;
				}

				case ENinjaGravityDirectionMode::SplineTangent:
				{
					if (IsValid(GravityActor))
					{
						const USplineComponent* Spline = Cast<USplineComponent>(
							GravityActor->GetComponentByClass(USplineComponent::StaticClass()));
						if (Spline != nullptr)
						{
							UNinjaCharacterMovementComponent* MutableThis = const_cast<UNinjaCharacterMovementComponent*>(this);
							MutableThis->GravityVectorA = Spline->FindDirectionClosestToWorldLocation(
								UpdatedComponent->GetComponentLocation(), ESplineCoordinateSpace::Type::World);
						}
					}

					GravityDir = GravityVectorA;
					break;
				}

				case ENinjaGravityDirectionMode::Point:
				{
					if (IsValid(GravityActor))
					{
						UNinjaCharacterMovementComponent* MutableThis = const_cast<UNinjaCharacterMovementComponent*>(this);
						MutableThis->GravityVectorA = GravityActor->GetActorLocation();
					}

					GravityDir = GravityVectorA - UpdatedComponent->GetComponentLocation();
					if (!GravityDir.IsZero())
					{
						GravityDir = GravityDir.GetSafeNormal();
					}

					break;
				}

				case ENinjaGravityDirectionMode::Line:
				{
					GravityDir = FMath::ClosestPointOnInfiniteLine(GravityVectorA,
						GravityVectorB, UpdatedComponent->GetComponentLocation()) -
						UpdatedComponent->GetComponentLocation();
					if (!GravityDir.IsZero())
					{
						GravityDir = GravityDir.GetSafeNormal();
					}

					break;
				}

				case ENinjaGravityDirectionMode::Segment:
				{
					GravityDir = FMath::ClosestPointOnLine(GravityVectorA,
						GravityVectorB, UpdatedComponent->GetComponentLocation()) -
						UpdatedComponent->GetComponentLocation();
					if (!GravityDir.IsZero())
					{
						GravityDir = GravityDir.GetSafeNormal();
					}

					break;
				}

				case ENinjaGravityDirectionMode::Spline:
				{
					if (IsValid(GravityActor))
					{
						const USplineComponent* Spline = Cast<USplineComponent>(
							GravityActor->GetComponentByClass(USplineComponent::StaticClass()));
						if (Spline != nullptr)
						{
							UNinjaCharacterMovementComponent* MutableThis = const_cast<UNinjaCharacterMovementComponent*>(this);
							MutableThis->GravityVectorA = Spline->FindLocationClosestToWorldLocation(
								UpdatedComponent->GetComponentLocation(), ESplineCoordinateSpace::Type::World);
						}
					}

					GravityDir = GravityVectorA - UpdatedComponent->GetComponentLocation();
					if (!GravityDir.IsZero())
					{
						GravityDir = GravityDir.GetSafeNormal();
					}

					break;
				}

				case ENinjaGravityDirectionMode::Plane:
				{
					GravityDir = FVector::PointPlaneProject(UpdatedComponent->GetComponentLocation(),
						GravityVectorA, GravityVectorB) - UpdatedComponent->GetComponentLocation();
					if (!GravityDir.IsZero())
					{
						GravityDir = GravityDir.GetSafeNormal();
					}

					break;
				}

				case ENinjaGravityDirectionMode::SplinePlane:
				{
					if (IsValid(GravityActor))
					{
						const USplineComponent* Spline = Cast<USplineComponent>(
							GravityActor->GetComponentByClass(USplineComponent::StaticClass()));
						if (Spline != nullptr)
						{
							const float InputKey = Spline->FindInputKeyClosestToWorldLocation(
								UpdatedComponent->GetComponentLocation());
							const FVector ClosestLocation = Spline->GetLocationAtSplineInputKey(
								InputKey, ESplineCoordinateSpace::Type::World);
							const FVector ClosestUpVector = Spline->GetUpVectorAtSplineInputKey(
								InputKey, ESplineCoordinateSpace::Type::World);

							UNinjaCharacterMovementComponent* MutableThis = const_cast<UNinjaCharacterMovementComponent*>(this);
							MutableThis->GravityVectorA = FVector::PointPlaneProject(UpdatedComponent->GetComponentLocation(),
								ClosestLocation, ClosestUpVector);
							MutableThis->GravityVectorB = ClosestUpVector;
						}
					}

					GravityDir = GravityVectorA - UpdatedComponent->GetComponentLocation();
					if (!GravityDir.IsZero())
					{
						GravityDir = GravityDir.GetSafeNormal();
					}

					break;
				}

				case ENinjaGravityDirectionMode::Box:
				{
					if (IsValid(GravityActor))
					{
						UNinjaCharacterMovementComponent* MutableThis = const_cast<UNinjaCharacterMovementComponent*>(this);
						GravityActor->GetActorBounds(true, MutableThis->GravityVectorA, MutableThis->GravityVectorB);
					}

					GravityDir = FBox(GravityVectorA - GravityVectorB, GravityVectorA + GravityVectorB).GetClosestPointTo(
						UpdatedComponent->GetComponentLocation()) - UpdatedComponent->GetComponentLocation();
					if (!GravityDir.IsZero())
					{
						GravityDir = GravityDir.GetSafeNormal();
					}

					break;
				}

				case ENinjaGravityDirectionMode::Collision:
				{
					if (IsValid(GravityActor))
					{
						FVector ClosestPoint;
						if (Cast<UPrimitiveComponent>(GravityActor->GetRootComponent())->GetClosestPointOnCollision(
							UpdatedComponent->GetComponentLocation(), ClosestPoint) > 0.0f)
						{
							UNinjaCharacterMovementComponent* MutableThis = const_cast<UNinjaCharacterMovementComponent*>(this);
							MutableThis->GravityVectorA = ClosestPoint;
						}
					}

					GravityDir = GravityVectorA - UpdatedComponent->GetComponentLocation();
					if (!GravityDir.IsZero())
					{
						GravityDir = GravityDir.GetSafeNormal();
					}

					break;
				}
			}

			if (GravityDir.IsZero())
			{
				GravityDir = FVector(0.0f, 0.0f, ((GetUnscaledGravityZ() > 0.0f) ? 1.0f : -1.0f));
			}
		}
	}

	return GravityDir;
}

float UNinjaCharacterMovementComponent::GetGravityMagnitude() const
{
	return FMath::Abs(GetUnscaledGravityZ() * GetFinalGravityScale());
}

float UNinjaCharacterMovementComponent::GetFinalGravityScale() const
{
	if (!HasValidData() || bIgnoreOtherGravityScales || GravityScale == 0.0f)
	{
		return GravityScale;
	}

	ANinjaPhysicsVolume* NinjaPhysicsVolume = Cast<ANinjaPhysicsVolume>(GetPhysicsVolume());
	if (NinjaPhysicsVolume != nullptr)
	{
		return NinjaPhysicsVolume->GetFinalGravityScale(CharacterOwner->GetRootComponent()) * GravityScale;
	}

	return GravityScale;
}

void UNinjaCharacterMovementComponent::K2_SetFixedGravityDirection(const FVector& NewGravityDirection)
{
	SetFixedGravityDirection(NewGravityDirection.GetSafeNormal());
}

void UNinjaCharacterMovementComponent::SetFixedGravityDirection(const FVector& NewFixedGravityDirection)
{
	if (NewFixedGravityDirection.IsZero() ||
		(GravityDirectionMode == ENinjaGravityDirectionMode::Fixed &&
		GravityVectorA == NewFixedGravityDirection))
	{
		return;
	}

	const ENinjaGravityDirectionMode OldGravityDirectionMode = GravityDirectionMode;

	bDirtyGravityDirection = true;
	GravityDirectionMode = ENinjaGravityDirectionMode::Fixed;
	GravityVectorA = NewFixedGravityDirection;

	GravityDirectionChanged(OldGravityDirectionMode);
}

void UNinjaCharacterMovementComponent::MulticastSetFixedGravityDirection_Implementation(const FVector& NewFixedGravityDirection)
{
	if (GravityDirectionMode == ENinjaGravityDirectionMode::Fixed &&
		GravityVectorA == NewFixedGravityDirection)
	{
		return;
	}

	const ENinjaGravityDirectionMode OldGravityDirectionMode = GravityDirectionMode;

	GravityDirectionMode = ENinjaGravityDirectionMode::Fixed;
	GravityVectorA = NewFixedGravityDirection;

	GravityDirectionChanged(OldGravityDirectionMode);
}

void UNinjaCharacterMovementComponent::SetSplineTangentGravityDirection(AActor* NewGravityActor)
{
	if (NewGravityActor == nullptr ||
		(GravityDirectionMode == ENinjaGravityDirectionMode::SplineTangent &&
		GravityActor == NewGravityActor))
	{
		return;
	}

	const USplineComponent* Spline = Cast<USplineComponent>(
		NewGravityActor->GetComponentByClass(USplineComponent::StaticClass()));
	if (Spline != nullptr)
	{
		const ENinjaGravityDirectionMode OldGravityDirectionMode = GravityDirectionMode;

		bDirtyGravityDirection = true;
		GravityDirectionMode = ENinjaGravityDirectionMode::SplineTangent;
		GravityActor = NewGravityActor;

		GravityDirectionChanged(OldGravityDirectionMode);
	}
}

void UNinjaCharacterMovementComponent::MulticastSetSplineTangentGravityDirection_Implementation(AActor* NewGravityActor)
{
	if (GravityDirectionMode == ENinjaGravityDirectionMode::SplineTangent &&
		GravityActor == NewGravityActor)
	{
		return;
	}

	const ENinjaGravityDirectionMode OldGravityDirectionMode = GravityDirectionMode;

	GravityDirectionMode = ENinjaGravityDirectionMode::SplineTangent;
	GravityActor = NewGravityActor;

	GravityDirectionChanged(OldGravityDirectionMode);
}

void UNinjaCharacterMovementComponent::SetPointGravityDirection(const FVector& NewGravityPoint)
{
	if (GravityDirectionMode == ENinjaGravityDirectionMode::Point &&
		GravityVectorA == NewGravityPoint)
	{
		return;
	}

	const ENinjaGravityDirectionMode OldGravityDirectionMode = GravityDirectionMode;

	bDirtyGravityDirection = true;
	GravityDirectionMode = ENinjaGravityDirectionMode::Point;
	GravityVectorA = NewGravityPoint;
	GravityActor = nullptr;

	GravityDirectionChanged(OldGravityDirectionMode);
}

void UNinjaCharacterMovementComponent::SetPointGravityDirectionFromActor(AActor* NewGravityActor)
{
	if (GravityDirectionMode == ENinjaGravityDirectionMode::Point &&
		GravityActor == NewGravityActor)
	{
		return;
	}

	const ENinjaGravityDirectionMode OldGravityDirectionMode = GravityDirectionMode;

	bDirtyGravityDirection = true;
	GravityDirectionMode = ENinjaGravityDirectionMode::Point;
	GravityActor = NewGravityActor;

	GravityDirectionChanged(OldGravityDirectionMode);
}

void UNinjaCharacterMovementComponent::MulticastSetPointGravityDirection_Implementation(const FVector& NewGravityPoint)
{
	if (GravityDirectionMode == ENinjaGravityDirectionMode::Point &&
		GravityVectorA == NewGravityPoint)
	{
		return;
	}

	const ENinjaGravityDirectionMode OldGravityDirectionMode = GravityDirectionMode;

	GravityDirectionMode = ENinjaGravityDirectionMode::Point;
	GravityVectorA = NewGravityPoint;
	GravityActor = nullptr;

	GravityDirectionChanged(OldGravityDirectionMode);
}

void UNinjaCharacterMovementComponent::MulticastSetPointGravityDirectionFromActor_Implementation(AActor* NewGravityActor)
{
	if (GravityDirectionMode == ENinjaGravityDirectionMode::Point &&
		GravityActor == NewGravityActor)
	{
		return;
	}

	const ENinjaGravityDirectionMode OldGravityDirectionMode = GravityDirectionMode;

	GravityDirectionMode = ENinjaGravityDirectionMode::Point;
	GravityActor = NewGravityActor;

	GravityDirectionChanged(OldGravityDirectionMode);
}

void UNinjaCharacterMovementComponent::SetLineGravityDirection(const FVector& NewGravityLineStart, const FVector& NewGravityLineEnd)
{
	if (NewGravityLineStart == NewGravityLineEnd ||
		(GravityDirectionMode == ENinjaGravityDirectionMode::Line &&
		GravityVectorA == NewGravityLineStart && GravityVectorB == NewGravityLineEnd))
	{
		return;
	}

	const ENinjaGravityDirectionMode OldGravityDirectionMode = GravityDirectionMode;

	bDirtyGravityDirection = true;
	GravityDirectionMode = ENinjaGravityDirectionMode::Line;
	GravityVectorA = NewGravityLineStart;
	GravityVectorB = NewGravityLineEnd;

	GravityDirectionChanged(OldGravityDirectionMode);
}

void UNinjaCharacterMovementComponent::MulticastSetLineGravityDirection_Implementation(const FVector& NewGravityLineStart, const FVector& NewGravityLineEnd)
{
	if (GravityDirectionMode == ENinjaGravityDirectionMode::Line &&
		GravityVectorA == NewGravityLineStart && GravityVectorB == NewGravityLineEnd)
	{
		return;
	}

	const ENinjaGravityDirectionMode OldGravityDirectionMode = GravityDirectionMode;

	GravityDirectionMode = ENinjaGravityDirectionMode::Line;
	GravityVectorA = NewGravityLineStart;
	GravityVectorB = NewGravityLineEnd;

	GravityDirectionChanged(OldGravityDirectionMode);
}

void UNinjaCharacterMovementComponent::SetSegmentGravityDirection(const FVector& NewGravitySegmentStart, const FVector& NewGravitySegmentEnd)
{
	if (NewGravitySegmentStart == NewGravitySegmentEnd ||
		(GravityDirectionMode == ENinjaGravityDirectionMode::Segment &&
		GravityVectorA == NewGravitySegmentStart && GravityVectorB == NewGravitySegmentEnd))
	{
		return;
	}

	const ENinjaGravityDirectionMode OldGravityDirectionMode = GravityDirectionMode;

	bDirtyGravityDirection = true;
	GravityDirectionMode = ENinjaGravityDirectionMode::Segment;
	GravityVectorA = NewGravitySegmentStart;
	GravityVectorB = NewGravitySegmentEnd;

	GravityDirectionChanged(OldGravityDirectionMode);
}

void UNinjaCharacterMovementComponent::MulticastSetSegmentGravityDirection_Implementation(const FVector& NewGravitySegmentStart, const FVector& NewGravitySegmentEnd)
{
	if (GravityDirectionMode == ENinjaGravityDirectionMode::Segment &&
		GravityVectorA == NewGravitySegmentStart && GravityVectorB == NewGravitySegmentEnd)
	{
		return;
	}

	const ENinjaGravityDirectionMode OldGravityDirectionMode = GravityDirectionMode;

	GravityDirectionMode = ENinjaGravityDirectionMode::Segment;
	GravityVectorA = NewGravitySegmentStart;
	GravityVectorB = NewGravitySegmentEnd;

	GravityDirectionChanged(OldGravityDirectionMode);
}

void UNinjaCharacterMovementComponent::SetSplineGravityDirection(AActor* NewGravityActor)
{
	if (NewGravityActor == nullptr ||
		(GravityDirectionMode == ENinjaGravityDirectionMode::Spline &&
		GravityActor == NewGravityActor))
	{
		return;
	}

	const USplineComponent* Spline = Cast<USplineComponent>(
		NewGravityActor->GetComponentByClass(USplineComponent::StaticClass()));
	if (Spline != nullptr)
	{
		const ENinjaGravityDirectionMode OldGravityDirectionMode = GravityDirectionMode;

		bDirtyGravityDirection = true;
		GravityDirectionMode = ENinjaGravityDirectionMode::Spline;
		GravityActor = NewGravityActor;

		GravityDirectionChanged(OldGravityDirectionMode);
	}
}

void UNinjaCharacterMovementComponent::MulticastSetSplineGravityDirection_Implementation(AActor* NewGravityActor)
{
	if (GravityDirectionMode == ENinjaGravityDirectionMode::Spline &&
		GravityActor == NewGravityActor)
	{
		return;
	}

	const ENinjaGravityDirectionMode OldGravityDirectionMode = GravityDirectionMode;

	GravityDirectionMode = ENinjaGravityDirectionMode::Spline;
	GravityActor = NewGravityActor;

	GravityDirectionChanged(OldGravityDirectionMode);
}

void UNinjaCharacterMovementComponent::K2_SetPlaneGravityDirection(const FVector& NewGravityPlaneBase, const FVector& NewGravityPlaneNormal)
{
	SetPlaneGravityDirection(NewGravityPlaneBase, NewGravityPlaneNormal.GetSafeNormal());
}

void UNinjaCharacterMovementComponent::SetPlaneGravityDirection(const FVector& NewGravityPlaneBase, const FVector& NewGravityPlaneNormal)
{
	if (NewGravityPlaneNormal.IsZero() ||
		(GravityDirectionMode == ENinjaGravityDirectionMode::Plane &&
		GravityVectorA == NewGravityPlaneBase && GravityVectorB == NewGravityPlaneNormal))
	{
		return;
	}

	const ENinjaGravityDirectionMode OldGravityDirectionMode = GravityDirectionMode;

	bDirtyGravityDirection = true;
	GravityDirectionMode = ENinjaGravityDirectionMode::Plane;
	GravityVectorA = NewGravityPlaneBase;
	GravityVectorB = NewGravityPlaneNormal;

	GravityDirectionChanged(OldGravityDirectionMode);
}

void UNinjaCharacterMovementComponent::MulticastSetPlaneGravityDirection_Implementation(const FVector& NewGravityPlaneBase, const FVector& NewGravityPlaneNormal)
{
	if (GravityDirectionMode == ENinjaGravityDirectionMode::Plane &&
		GravityVectorA == NewGravityPlaneBase && GravityVectorB == NewGravityPlaneNormal)
	{
		return;
	}

	const ENinjaGravityDirectionMode OldGravityDirectionMode = GravityDirectionMode;

	GravityDirectionMode = ENinjaGravityDirectionMode::Plane;
	GravityVectorA = NewGravityPlaneBase;
	GravityVectorB = NewGravityPlaneNormal;

	GravityDirectionChanged(OldGravityDirectionMode);
}

void UNinjaCharacterMovementComponent::SetSplinePlaneGravityDirection(AActor* NewGravityActor)
{
	if (NewGravityActor == nullptr ||
		(GravityDirectionMode == ENinjaGravityDirectionMode::SplinePlane &&
		GravityActor == NewGravityActor))
	{
		return;
	}

	const USplineComponent* Spline = Cast<USplineComponent>(
		NewGravityActor->GetComponentByClass(USplineComponent::StaticClass()));
	if (Spline != nullptr)
	{
		const ENinjaGravityDirectionMode OldGravityDirectionMode = GravityDirectionMode;

		bDirtyGravityDirection = true;
		GravityDirectionMode = ENinjaGravityDirectionMode::SplinePlane;
		GravityActor = NewGravityActor;

		GravityDirectionChanged(OldGravityDirectionMode);
	}
}

void UNinjaCharacterMovementComponent::MulticastSetSplinePlaneGravityDirection_Implementation(AActor* NewGravityActor)
{
	if (GravityDirectionMode == ENinjaGravityDirectionMode::SplinePlane &&
		GravityActor == NewGravityActor)
	{
		return;
	}

	const ENinjaGravityDirectionMode OldGravityDirectionMode = GravityDirectionMode;

	GravityDirectionMode = ENinjaGravityDirectionMode::SplinePlane;
	GravityActor = NewGravityActor;

	GravityDirectionChanged(OldGravityDirectionMode);
}

void UNinjaCharacterMovementComponent::SetBoxGravityDirection(const FVector& NewGravityBoxOrigin, const FVector& NewGravityBoxExtent)
{
	if (GravityDirectionMode == ENinjaGravityDirectionMode::Box &&
		GravityVectorA == NewGravityBoxOrigin && GravityVectorB == NewGravityBoxExtent)
	{
		return;
	}

	const ENinjaGravityDirectionMode OldGravityDirectionMode = GravityDirectionMode;

	bDirtyGravityDirection = true;
	GravityDirectionMode = ENinjaGravityDirectionMode::Box;
	GravityVectorA = NewGravityBoxOrigin;
	GravityVectorB = NewGravityBoxExtent;
	GravityActor = nullptr;

	GravityDirectionChanged(OldGravityDirectionMode);
}

void UNinjaCharacterMovementComponent::SetBoxGravityDirectionFromActor(AActor* NewGravityActor)
{
	if (GravityDirectionMode == ENinjaGravityDirectionMode::Box && GravityActor == NewGravityActor)
	{
		return;
	}

	const ENinjaGravityDirectionMode OldGravityDirectionMode = GravityDirectionMode;

	bDirtyGravityDirection = true;
	GravityDirectionMode = ENinjaGravityDirectionMode::Box;
	GravityActor = NewGravityActor;

	GravityDirectionChanged(OldGravityDirectionMode);
}

void UNinjaCharacterMovementComponent::MulticastSetBoxGravityDirection_Implementation(const FVector& NewGravityBoxOrigin, const FVector& NewGravityBoxExtent)
{
	if (GravityDirectionMode == ENinjaGravityDirectionMode::Box &&
		GravityVectorA == NewGravityBoxOrigin && GravityVectorB == NewGravityBoxExtent)
	{
		return;
	}

	const ENinjaGravityDirectionMode OldGravityDirectionMode = GravityDirectionMode;

	GravityDirectionMode = ENinjaGravityDirectionMode::Box;
	GravityVectorA = NewGravityBoxOrigin;
	GravityVectorB = NewGravityBoxExtent;
	GravityActor = nullptr;

	GravityDirectionChanged(OldGravityDirectionMode);
}

void UNinjaCharacterMovementComponent::MulticastSetBoxGravityDirectionFromActor_Implementation(AActor* NewGravityActor)
{
	if (GravityDirectionMode == ENinjaGravityDirectionMode::Box && GravityActor == NewGravityActor)
	{
		return;
	}

	const ENinjaGravityDirectionMode OldGravityDirectionMode = GravityDirectionMode;

	GravityDirectionMode = ENinjaGravityDirectionMode::Box;
	GravityActor = NewGravityActor;

	GravityDirectionChanged(OldGravityDirectionMode);
}

void UNinjaCharacterMovementComponent::SetCollisionGravityDirection(AActor* NewGravityActor)
{
	if (NewGravityActor == nullptr ||
		(GravityDirectionMode == ENinjaGravityDirectionMode::Collision &&
		GravityActor == NewGravityActor))
	{
		return;
	}

	const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(NewGravityActor->GetRootComponent());
	if (Primitive != nullptr)
	{
		const ENinjaGravityDirectionMode OldGravityDirectionMode = GravityDirectionMode;

		bDirtyGravityDirection = true;
		GravityDirectionMode = ENinjaGravityDirectionMode::Collision;
		GravityActor = NewGravityActor;

		GravityDirectionChanged(OldGravityDirectionMode);
	}
}

void UNinjaCharacterMovementComponent::MulticastSetCollisionGravityDirection_Implementation(AActor* NewGravityActor)
{
	if (GravityDirectionMode == ENinjaGravityDirectionMode::Collision && GravityActor == NewGravityActor)
	{
		return;
	}

	const ENinjaGravityDirectionMode OldGravityDirectionMode = GravityDirectionMode;

	GravityDirectionMode = ENinjaGravityDirectionMode::Collision;
	GravityActor = NewGravityActor;

	GravityDirectionChanged(OldGravityDirectionMode);
}

void UNinjaCharacterMovementComponent::GravityDirectionChanged(ENinjaGravityDirectionMode OldGravityDirectionMode)
{
	OnGravityDirectionChanged(OldGravityDirectionMode, GravityDirectionMode);

	// Call owner delegate
	ANinjaCharacter* Ninja = Cast<ANinjaCharacter>(CharacterOwner);
	if (Ninja != nullptr)
	{
		Ninja->GravityDirectionChanged(OldGravityDirectionMode, GravityDirectionMode);
	}
}

void UNinjaCharacterMovementComponent::OnGravityDirectionChanged(ENinjaGravityDirectionMode OldGravityDirectionMode, ENinjaGravityDirectionMode CurrentGravityDirectionMode)
{
}

void UNinjaCharacterMovementComponent::MulticastSetGravityScale_Implementation(float NewGravityScale)
{
	GravityScale = NewGravityScale;
}

void UNinjaCharacterMovementComponent::SetAlignGravityToBase(bool bNewAlignGravityToBase)
{
	if (bAlignGravityToBase == bNewAlignGravityToBase)
	{
		return;
	}

	bAlignGravityToBase = bNewAlignGravityToBase;

	if (ShouldReplicateGravity())
	{
		if (!bAlignGravityToBase)
		{
			MulticastDisableAlignGravityToBase();
		}
		else
		{
			MulticastEnableAlignGravityToBase();
		}
	}
}

void UNinjaCharacterMovementComponent::MulticastEnableAlignGravityToBase_Implementation()
{
	bAlignGravityToBase = true;
}

void UNinjaCharacterMovementComponent::MulticastDisableAlignGravityToBase_Implementation()
{
	bAlignGravityToBase = false;
}

void UNinjaCharacterMovementComponent::UpdateGravity()
{
	if (!bAlignGravityToBase || !IsMovingOnGround())
	{
		return;
	}

	switch (GravityDirectionMode)
	{
		case ENinjaGravityDirectionMode::Fixed:
		{
			if (!CurrentFloor.HitResult.ImpactNormal.IsZero())
			{
				// Set the fixed gravity direction to reversed floor normal vector
				SetFixedGravityDirection(CurrentFloor.HitResult.ImpactNormal * -1.0f);
			}

			break;
		}

		case ENinjaGravityDirectionMode::Point:
		{
			if (CurrentFloor.HitResult.GetActor() != nullptr)
			{
				// Set the point gravity direction from base
				SetPointGravityDirectionFromActor(CurrentFloor.HitResult.GetActor());
			}

			break;
		}

		case ENinjaGravityDirectionMode::Box:
		{
			if (CurrentFloor.HitResult.GetActor() != nullptr)
			{
				// Set the box gravity direction from base
				SetBoxGravityDirectionFromActor(CurrentFloor.HitResult.GetActor());
			}

			break;
		}

		case ENinjaGravityDirectionMode::Collision:
		{
			if (CurrentFloor.HitResult.GetActor() != nullptr)
			{
				// Set the collision gravity direction from base
				SetCollisionGravityDirection(CurrentFloor.HitResult.GetActor());
			}

			break;
		}
	}
}

void UNinjaCharacterMovementComponent::ReplicateGravityToClients()
{
	if (bDirtyGravityDirection)
	{
		// Replicate gravity direction to clients
		switch (GravityDirectionMode)
		{
			case ENinjaGravityDirectionMode::Fixed:
			{
				MulticastSetFixedGravityDirection(GravityVectorA);
				break;
			}

			case ENinjaGravityDirectionMode::SplineTangent:
			{
				MulticastSetSplineTangentGravityDirection(GravityActor);
				break;
			}

			case ENinjaGravityDirectionMode::Point:
			{
				MulticastSetPointGravityDirection(GravityVectorA);
				break;
			}

			case ENinjaGravityDirectionMode::Line:
			{
				MulticastSetLineGravityDirection(GravityVectorA, GravityVectorB);
				break;
			}

			case ENinjaGravityDirectionMode::Segment:
			{
				MulticastSetSegmentGravityDirection(GravityVectorA, GravityVectorB);
				break;
			}

			case ENinjaGravityDirectionMode::Spline:
			{
				MulticastSetSplineGravityDirection(GravityActor);
				break;
			}

			case ENinjaGravityDirectionMode::Plane:
			{
				MulticastSetPlaneGravityDirection(GravityVectorA, GravityVectorB);
				break;
			}

			case ENinjaGravityDirectionMode::SplinePlane:
			{
				MulticastSetSplinePlaneGravityDirection(GravityActor);
				break;
			}

			case ENinjaGravityDirectionMode::Box:
			{
				MulticastSetBoxGravityDirection(GravityVectorA, GravityVectorB);
				break;
			}

			case ENinjaGravityDirectionMode::Collision:
			{
				MulticastSetCollisionGravityDirection(GravityActor);
				break;
			}
		}

		bDirtyGravityDirection = false;
	}

	if (OldGravityScale != GravityScale)
	{
		// Replicate gravity scale to clients
		MulticastSetGravityScale(GravityScale);
		OldGravityScale = GravityScale;
	}
}

FRotator UNinjaCharacterMovementComponent::ConstrainComponentRotation(const FRotator& Rotation) const
{
	if (!HasValidData())
	{
		return Rotation;
	}

	const FRotator CapsuleRotation = UpdatedComponent->GetComponentRotation();
	if (CapsuleRotation.Equals(Rotation, SCENECOMPONENT_ROTATOR_TOLERANCE))
	{
		// Rotations are almost equal, don't rotate the capsule
		return CapsuleRotation;
	}

	const FVector CapsuleUp = GetComponentAxisZ();
	if (CapsuleUp.Z == 1.0f)
	{
		// Optimization; keep yaw rotation only
		return FRotator(0.0f, FRotator::NormalizeAxis(Rotation.Yaw), 0.0f);
	}

	// Keep current Z rotation axis of capsule, try to keep X axis of rotation
	return FNinjaMath::MakeFromZQuat(CapsuleUp, Rotation.Quaternion(),
		ThresholdParallelCosine).Rotator();
}

void UNinjaCharacterMovementComponent::SetAlignComponentToFloor(bool bNewAlignComponentToFloor)
{
	if (bAlignComponentToFloor == bNewAlignComponentToFloor)
	{
		return;
	}

	bAlignComponentToFloor = bNewAlignComponentToFloor;

	if (ShouldReplicateGravity())
	{
		if (!bAlignComponentToFloor)
		{
			MulticastDisableAlignComponentToFloor();
		}
		else
		{
			MulticastEnableAlignComponentToFloor();
		}
	}
}

void UNinjaCharacterMovementComponent::MulticastEnableAlignComponentToFloor_Implementation()
{
	bAlignComponentToFloor = true;
}

void UNinjaCharacterMovementComponent::MulticastDisableAlignComponentToFloor_Implementation()
{
	bAlignComponentToFloor = false;
}

void UNinjaCharacterMovementComponent::SetAlignComponentToGravity(bool bNewAlignComponentToGravity)
{
	if (bAlignComponentToGravity == bNewAlignComponentToGravity)
	{
		return;
	}

	bAlignComponentToGravity = bNewAlignComponentToGravity;

	if (ShouldReplicateGravity())
	{
		if (!bAlignComponentToGravity)
		{
			MulticastDisableAlignComponentToGravity();
		}
		else
		{
			MulticastEnableAlignComponentToGravity();
		}
	}
}

void UNinjaCharacterMovementComponent::MulticastEnableAlignComponentToGravity_Implementation()
{
	bAlignComponentToGravity = true;
}

void UNinjaCharacterMovementComponent::MulticastDisableAlignComponentToGravity_Implementation()
{
	bAlignComponentToGravity = false;
}

FVector UNinjaCharacterMovementComponent::GetComponentDesiredAxisZ() const
{
	FVector DesiredAxisZ;
	if (bAlignComponentToFloor && IsMovingOnGround() && !CurrentFloor.HitResult.ImpactNormal.IsZero())
	{
		// Align character rotation to floor normal vector
		DesiredAxisZ = CurrentFloor.HitResult.ImpactNormal;
	}
	else if (bAlignComponentToGravity)
	{
		DesiredAxisZ = GetGravityDirection(true) * -1.0f;
	}
	else
	{
		DesiredAxisZ = GetComponentAxisZ();
	}

	if (DesiredAxisZ.Z == 1.0f ||
		FNinjaMath::Coincident(DesiredAxisZ, FVector::UpVector, ThresholdParallelCosine))
	{
		// Optimization; avoids usage of several complex calculations in other places
		DesiredAxisZ = FVector::UpVector;
	}

	return DesiredAxisZ;
}

bool UNinjaCharacterMovementComponent::SetComponentAxisZ(const FVector& NewComponentAxisZ, bool bForceFindFloor)
{
	if (!HasValidData())
	{
		return false;
	}

	// Try to rotate the updated component
	const bool bMovingOnGround = IsMovingOnGround();
	const bool bUpdateResult = UpdateComponentRotation(NewComponentAxisZ, true,
		bRotateVelocityOnGround && bMovingOnGround);

	// If rotation was successful, find floor if needed
	if (bUpdateResult && (bForceFindFloor || bMovingOnGround))
	{
		{
			TGuardValue<bool> LandOnAnySurfaceGuard(bLandOnAnySurface, bLandOnAnySurface || bForceFindFloor);
			FindFloor(UpdatedComponent->GetComponentLocation(), CurrentFloor, false);
		}

		if (!CurrentFloor.IsWalkableFloor())
		{
			// Invalid floor, start falling if moving on ground
			if (bMovingOnGround)
			{
				SetMovementMode(MOVE_Falling);
			}
		}
		else
		{
			AdjustFloorHeight();
			SetBase(CurrentFloor.HitResult.Component.Get(), CurrentFloor.HitResult.BoneName);

			// Land on new floor if not moving on ground
			if (!bMovingOnGround)
			{
				if (CharacterOwner->ShouldNotifyLanded(CurrentFloor.HitResult))
				{
					CharacterOwner->Landed(CurrentFloor.HitResult);
				}

				SetPostLandedPhysics(CurrentFloor.HitResult);
			}
		}
	}

	return bUpdateResult;
}

bool UNinjaCharacterMovementComponent::UpdateComponentRotation(const FVector& DesiredAxisZ, bool bRotateAroundCenter, bool bRotateVelocity)
{
	SCOPE_CYCLE_COUNTER(STAT_CharacterMovementUpdateComponentRotation);

	if (!HasValidData())
	{
		return false;
	}

	const FQuat PawnRotation = UpdatedComponent->GetComponentQuat();
	const FVector CurrentAxisZ = FNinjaMath::GetAxisZ(PawnRotation);

	// Abort if angle between new and old capsule 'up' axes almost equals to 0 degrees
	if (FNinjaMath::Coincident(DesiredAxisZ, CurrentAxisZ, ThresholdParallelCosine))
	{
		return false;
	}

	FVector Delta = FVector::ZeroVector;

	// Make sure actual shape isn't a sphere to calculate delta offset
	float PawnRadius, PawnHalfHeight;
	CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleSize(PawnRadius, PawnHalfHeight);
	if (PawnHalfHeight > PawnRadius)
	{
		if (!bRotateAroundCenter)
		{
			// Rotate capsule around the origin of the bottom sphere
			const float SphereHeight = PawnHalfHeight - PawnRadius;
			Delta = CurrentAxisZ * (SphereHeight * -1.0f) + DesiredAxisZ * SphereHeight;
		}
		else
		{
			// Rotate capsule around the origin of the capsule, but avoid floor penetrations
			const FVector TraceStart = UpdatedComponent->GetComponentLocation();
			const float TraceDistance = PawnHalfHeight - PawnRadius;

			FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(UpdateComponentRotation), false, CharacterOwner);
			FCollisionResponseParams ResponseParam;
			InitCollisionParams(QueryParams, ResponseParam);

			FHitResult Hit(1.0f);
			const bool bBlockingHit = FloorSweepTest(Hit, TraceStart, TraceStart - DesiredAxisZ * TraceDistance,
				UpdatedComponent->GetCollisionObjectType(), FCollisionShape::MakeSphere(PawnRadius), QueryParams, ResponseParam);
			if (bBlockingHit)
			{
				const float AvgFloorDist = (MIN_FLOOR_DIST + MAX_FLOOR_DIST) * 0.5f;
				Delta = DesiredAxisZ * (TraceDistance * (1.0f - Hit.Time) + AvgFloorDist);
			}
		}
	}

	// Take desired Z rotation axis of capsule, try to keep current X rotation axis of capsule
	const FQuat NewRotation = FNinjaMath::MakeFromZQuat(DesiredAxisZ, PawnRotation, ThresholdParallelCosine);

	// Try to rotate the capsule now, but don't sweep because penetrations are handled properly
	FHitResult Hit(1.0f);
	const bool bMoveResult = SafeMoveUpdatedComponent(Delta, NewRotation, false, Hit, ETeleportType::TeleportPhysics);

	if (bMoveResult && bRotateVelocity && !Velocity.IsZero())
	{
		// Modify Velocity direction to prevent losing speed on rotation change
		Velocity = FQuat::FindBetweenNormals(CurrentAxisZ, DesiredAxisZ).RotateVector(Velocity);
	}

	return bMoveResult;
}

void UNinjaCharacterMovementComponent::SetThresholdParallelAngle(float NewThresholdParallelAngle)
{
	ThresholdParallelAngle = FMath::Clamp(NewThresholdParallelAngle, 0.25f, 1.0f);

	ThresholdOrthogonalCosine = FMath::Cos(FMath::DegreesToRadians(90.0f - ThresholdParallelAngle));
	ThresholdParallelCosine = FMath::Cos(FMath::DegreesToRadians(ThresholdParallelAngle));
}
