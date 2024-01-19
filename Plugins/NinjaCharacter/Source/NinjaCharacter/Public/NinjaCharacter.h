// "Ninja Character" plugin, by Javier 'Xaklse' Osset; Copyright 2020


#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "NinjaTypes.h"
#include "NinjaCharacter.generated.h"


DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCharMovementAxisChangedSignature, const FVector&, OldAxisZ, const FVector&, CurrentAxisZ);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCharMovementGravityChangedSignature, ENinjaGravityDirectionMode, OldGravityDirectionMode, ENinjaGravityDirectionMode, CurrentGravityDirectionMode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUnwalkableHitSignature, const FHitResult&, Hit);


/**
 * Pawns are the physical representations of players and creatures in a level.
 * Characters are Pawns that have a mesh, collision, and physics. This type is
 * able to handle arbitrary gravity direction and collision capsule orientation.
 */
UCLASS(Blueprintable)
class NINJACHARACTER_API ANinjaCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ANinjaCharacter(const FObjectInitializer& ObjectInitializer);

public:
	/**
	 * Event when play begins for this Actor.
	 */
	virtual void BeginPlay() override;

public:
	/** Rep notify for ReplicatedBasedMovement */
	virtual void OnRep_ReplicatedBasedMovement() override;

public:
	/** Returns vector direction of gravity */
	virtual FVector GetGravityDirection() const override;

public:
	/**
	 * Called when Character stops crouching. Called on non-owned Characters through bIsCrouched replication.
	 * @param HalfHeightAdjust - difference between default collision half-height, and actual crouched capsule half-height
	 * @param ScaledHalfHeightAdjust - difference after component scale is taken in to account
	 */
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

	/**
	 * Called when Character crouches. Called on non-owned Characters through bIsCrouched replication.
	 * @param HalfHeightAdjust - difference between default collision half-height, and actual crouched capsule half-height
	 * @param ScaledHalfHeightAdjust - difference after component scale is taken in to account
	 */
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

public:
	/** Apply momentum caused by damage. */
	virtual void ApplyDamageMomentum(float DamageTaken, const FDamageEvent& DamageEvent, APawn* PawnInstigator, AActor* DamageCauser) override;

public:
	/** @return	Pawn's eye location */
	virtual FVector GetPawnViewLocation() const override;

	/** Updates Pawn's rotation to the given rotation, assumed to be the Controller's ControlRotation. Respects the bUseControllerRotation* settings. */
	virtual void FaceRotation(FRotator NewControlRotation, float DeltaTime = 0.0f) override;

public:
	/**
	 * Set a pending launch velocity on the Character. This velocity will be processed on the next
	 * CharacterMovementComponent tick, and will set it to the "falling" state. Triggers the OnLaunched event.
	 * @note This version has a different behavior for the boolean parameters that take into account the Character's orientation
	 * @param LaunchVelocity - the velocity to impart to the Character
	 * @param bHorizontalOverride - if true, replace the horizontal part of the Character's velocity instead of adding to it
	 * @param bVerticalOverride - if true, replace the vertical part of the Character's velocity instead of adding to it
	 */
	UFUNCTION(BlueprintCallable,Category="NinjaCharacter")
	virtual void LaunchCharacterRotated(FVector LaunchVelocity, bool bHorizontalOverride, bool bVerticalOverride);

protected:
	/** Stores vertical axis of the capsule (movement collider). */
	FVector LastAxisZ;

	/** Stores rotation of the capsule (movement collider). */
	FQuat LastRotation;

	/**
	 * Called when the root component is moved or scaled.
	 * @param UpdatedComponent - scene component that has been moved/scaled
	 * @param UpdateTransformFlags - information about how the transform is being updated
	 * @param Teleport - whether to teleport the physics body or not
	 */
	virtual void TransformUpdated(class USceneComponent* UpdatedComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport);

public:
	/** If true, the aim control rotation of the Controller is rotated whenever the capsule is aligned to something. */
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="NinjaCharacter")
	uint32 bCapsuleRotatesControlRotation:1;

	/** If true, when the aim control rotation of the Controller is flipped, another axis is preserved. */
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="NinjaCharacter",Meta=(EditCondition="bCapsuleRotatesControlRotation"))
	uint32 bAltFlipControlRotation:1;

public:
	/**
	 * Changes the vertical axis of the capsule (movement collider).
	 * @param NewAxisZ - new vertical axis of the capsule
	 * @param bForceFindFloor - force find a floor and attach to it
	 * @return false if couldn't rotate/move the capsule
	 */
	UFUNCTION(BlueprintCallable,Category="NinjaCharacter",Meta=(DisplayName="Change Char Axis",Keywords="rotatecapsule"))
	virtual bool SetCharMovementAxis(const FVector& NewAxisZ, bool bForceFindFloor);

public:
	/**
	 * Delegate called after changing vertical axis of the capsule (movement collider).
	 * @param OldAxisZ - old vertical axis of the capsule
	 * @param CurrentAxisZ - current vertical axis of the capsule
	 */
	FCharMovementAxisChangedSignature CharMovementAxisChangedDelegate;

protected:
	/**
	 * Called after changing vertical axis of the capsule (movement collider).
	 * @param OldAxisZ - old vertical axis of the capsule
	 * @param CurrentAxisZ - current vertical axis of the capsule
	 */
	void CharMovementAxisChanged(const FVector& OldAxisZ, const FVector& CurrentAxisZ);

	/**
	 * Called after changing vertical axis of the capsule (movement collider).
	 * @note Can be overriden
	 * @param OldAxisZ - old vertical axis of the capsule
	 * @param CurrentAxisZ - current vertical axis of the capsule
	 */
	virtual void OnCharMovementAxisChanged(const FVector& OldAxisZ, const FVector& CurrentAxisZ);

public:
	/**
	 * Event called after changing vertical axis of the capsule (movement collider).
	 * @param OldAxisZ - old vertical axis of the capsule
	 * @param CurrentAxisZ - current vertical axis of the capsule
	 */
	UFUNCTION(BlueprintImplementableEvent,Category="NinjaCharacter",Meta=(DisplayName="On Char Axis Changed",ScriptName="OnCharacterMovementAxisChanged"))
	void K2_OnCharMovementAxisChanged(const FVector& OldAxisZ, const FVector& CurrentAxisZ);

public:
	/**
	 * Delegate called after GravityDirectionMode (or related data) has changed in the movement component.
	 * @param OldGravityDirectionMode - previous value of GravityDirectionMode
	 * @param CurrentGravityDirectionMode - current value of GravityDirectionMode
	 */
	FCharMovementGravityChangedSignature GravityDirectionChangedDelegate;

	/**
	 * Called after GravityDirectionMode (or related data) has changed in the movement component.
	 * @param OldGravityDirectionMode - previous value of GravityDirectionMode
	 * @param CurrentGravityDirectionMode - current value of GravityDirectionMode
	 */
	void GravityDirectionChanged(ENinjaGravityDirectionMode OldGravityDirectionMode, ENinjaGravityDirectionMode CurrentGravityDirectionMode);

protected:
	/**
	 * Called after GravityDirectionMode (or related data) has changed in the movement component.
	 * @note Can be overriden
	 * @param OldGravityDirectionMode - previous value of GravityDirectionMode
	 * @param CurrentGravityDirectionMode - current value of GravityDirectionMode
	 */
	virtual void OnGravityDirectionChanged(ENinjaGravityDirectionMode OldGravityDirectionMode, ENinjaGravityDirectionMode CurrentGravityDirectionMode);

public:
	/**
	 * Event called after GravityDirectionMode (or related data) has changed in the movement component.
	 * @param OldGravityDirectionMode - previous value of GravityDirectionMode
	 * @param CurrentGravityDirectionMode - current value of GravityDirectionMode
	 */
	UFUNCTION(BlueprintImplementableEvent,Category="NinjaCharacter",Meta=(DisplayName="On Char Gravity Direction Changed",ScriptName="OnGravityDirectionChanged"))
	void K2_OnGravityDirectionChanged(ENinjaGravityDirectionMode OldGravityDirectionMode, ENinjaGravityDirectionMode CurrentGravityDirectionMode);

public:
	/**
	 * Delegate called when the capsule (movement collider) bumps into an unwalkable blocking object.
	 * @param Hit - contains info about the hit, such as point of impact and surface normal at that point
	 */
	FUnwalkableHitSignature UnwalkableHitDelegate;

	/**
	 * Called when the capsule (movement collider) bumps into an unwalkable blocking object.
	 * @param Hit - contains info about the hit, such as point of impact and surface normal at that point
	 */
	void UnwalkableHit(const FHitResult& Hit);

protected:
	/**
	 * Called when the capsule (movement collider) bumps into an unwalkable blocking object.
	 * @note Can be overriden
	 * @param Hit - contains info about the hit, such as point of impact and surface normal at that point
	 */
	virtual void OnUnwalkableHit(const FHitResult& Hit);

public:
	/**
	 * Event called when the capsule (movement collider) bumps into an unwalkable blocking object.
	 * @param Hit - contains info about the hit, such as point of impact and surface normal at that point
	 */
	UFUNCTION(BlueprintImplementableEvent,Category="NinjaCharacter",Meta=(DisplayName="On Unwalkable Hit",ScriptName="OnUnwalkableHit"))
	void K2_OnUnwalkableHit(const FHitResult& Hit);

public:
	/**
	 * Returns NinjaCharacterMovement subobject.
	 */
	class UNinjaCharacterMovementComponent* GetNinjaCharacterMovement() const;

public:
	/**
	 * Return the current local X rotation axis of the root component.
	 * @return current X rotation axis of the root component
	 */
	FVector GetActorAxisX() const;

	/**
	 * Return the current local Y rotation axis of the root component.
	 * @return current Y rotation axis of the root component
	 */
	FVector GetActorAxisY() const;

	/**
	 * Return the current local Z rotation axis of the root component.
	 * @return current Z rotation axis of the root component
	 */
	FVector GetActorAxisZ() const;

public:
	/**
	 * Smoothly interpolates location and rotation of an attached component.
	 * @note Doesn't work with the associated "Mesh" if NetworkSmoothingMode of "CharacterMovement" is enabled
	 * @note Use "Set Absolute" and "Set Relative Location and Rotation" on the attached component to cancel the smooth interpolation
	 * @param SceneComponent - attached scene component that will be moved
	 * @param DeltaTime - game time elapsed since last frame
	 * @param LocationSpeed - controls how the component lags behind target location; zero is instant, low value is slower, high value is faster
	 * @param RotationSpeed - controls how the component lags behind target rotation; zero is instant, low value is slower, high value is faster
	 * @param RelativeLocation - location of the component relative to its parent (suggestion: use with "Get Base Translation Offset")
	 * @param RelativeRotation - rotation of the component relative to its parent (suggestion: use with "Get Base Rotation Offset")
	 */
	UFUNCTION(BlueprintCallable,Category="NinjaCharacter")
	virtual void SmoothComponentLocationAndRotation(class USceneComponent* SceneComponent, float DeltaTime, float LocationSpeed, float RotationSpeed, const FVector& RelativeLocation, const FRotator& RelativeRotation);

	/**
	 * Smoothly interpolates location of an attached component.
	 * @note Doesn't work with the associated "Mesh" if NetworkSmoothingMode of "CharacterMovement" is enabled
	 * @note Use "Set Absolute" and "Set Relative Location" on the attached component to cancel the smooth interpolation
	 * @param SceneComponent - attached scene component that will be moved
	 * @param DeltaTime - game time elapsed since last frame
	 * @param LocationSpeed - controls how the component lags behind target location; zero is instant, low value is slower, high value is faster
	 * @param RelativeLocation - location of the component relative to its parent (suggestion: use with "Get Base Translation Offset")
	 */
	UFUNCTION(BlueprintCallable,Category="NinjaCharacter")
	virtual void SmoothComponentLocation(class USceneComponent* SceneComponent, float DeltaTime, float LocationSpeed, const FVector& RelativeLocation);

	/**
	 * Smoothly interpolates rotation of an attached component.
	 * @note Doesn't work with the associated "Mesh" if NetworkSmoothingMode of "CharacterMovement" is enabled
	 * @note Use "Set Absolute" and "Set Relative Rotation" on the attached component to cancel the smooth interpolation
	 * @param SceneComponent - attached scene component that will be moved
	 * @param DeltaTime - game time elapsed since last frame
	 * @param RotationSpeed - controls how the component lags behind target rotation; zero is instant, low value is slower, high value is faster
	 * @param RelativeRotation - rotation of the component relative to its parent (suggestion: use with "Get Base Rotation Offset")
	 */
	UFUNCTION(BlueprintCallable,Category="NinjaCharacter")
	virtual void SmoothComponentRotation(class USceneComponent* SceneComponent, float DeltaTime, float RotationSpeed, const FRotator& RelativeRotation);

public:
	/**
	 * Unused helper function that handles an Axis2D input to apply linear movement to the capsule.
	 * @param InputActionValue2D - axis input value (FInputActionValue.Get<FVector2D>())
	 */
	void NinjaUsualMove(const FVector2D& InputActionValue2D);

	/**
	 * Unused helper function that handles an Axis2D input to apply rotation to the Controller's ControlRotation.
	 * @param InputActionValue2D - axis input value (FInputActionValue.Get<FVector2D>())
	 */
	void NinjaUsualLook(const FVector2D& InputActionValue2D);
};
