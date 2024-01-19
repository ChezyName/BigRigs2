// "Ninja Character" plugin, by Javier 'Xaklse' Osset; Copyright 2020


#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PhysicsVolume.h"
#include "NinjaTypes.h"
#include "NinjaPhysicsVolume.generated.h"


/**
 * A PhysicsVolume is a bounding volume that affects Actor physics. This type
 * allows overriding the gravity direction.
 */
UCLASS()
class NINJACHARACTER_API ANinjaPhysicsVolume : public APhysicsVolume
{
	GENERATED_BODY()

public:
	ANinjaPhysicsVolume(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITORONLY_DATA
protected:
	/** The TextRenderComponent subobject. */
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="NinjaPhysicsVolume")
	TObjectPtr<class UTextRenderComponent> TextRenderComponent;

public:
	/** Name of the TextRenderComponent. */
	static FName TextRenderComponentName;
#endif // WITH_EDITORONLY_DATA

public:
	/**
	 * Called every frame.
	 * @param DeltaTime - game time elapsed during last frame modified by the time dilation
	 */
	virtual void Tick(float DeltaTime) override;

protected:
	/** List of tracked Actors that are affected by gravity settings. */
	UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="NinjaPhysicsVolume")
	TArray<TObjectPtr<AActor>> TrackedActors;

	/** List of tracked Ninjas that are affected by gravity settings. */
	UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="NinjaPhysicsVolume")
	TArray<TObjectPtr<class ANinjaCharacter>> TrackedNinjas;

public:
	/**
	 * Called when an Actor enters this volume.
	 * @param Other - Actor that entered this volume
	 */
	virtual void ActorEnteredVolume(class AActor* Other) override;

	/**
	 * Called when an Actor leaves this volume.
	 * @param Other - Actor that left this volume, can be nullptr
	 */
	virtual void ActorLeavingVolume(class AActor* Other) override;

public:
	/**
	 * Obtains the gravity vector that influences a SceneComponent.
	 * @param SceneComponent - component in space affected by gravity
	 * @return current gravity
	 */
	UFUNCTION(BlueprintPure,Category="NinjaPhysicsVolume")
	virtual FVector GetGravity(class USceneComponent* SceneComponent) const;

	/**
	 * Obtains the normalized direction of gravity that influences a SceneComponent.
	 * @note Could return zero gravity
	 * @param SceneComponent - component in space affected by gravity
	 * @return normalized direction of gravity
	 */
	UFUNCTION(BlueprintPure,Category="NinjaPhysicsVolume")
	virtual FVector GetGravityDirection(class USceneComponent* SceneComponent) const;

	/**
	 * Obtains the absolute (positive) magnitude of gravity that influences a
	 * SceneComponent.
	 * @param SceneComponent - component in space affected by gravity
	 * @return magnitude of gravity
	 */
	UFUNCTION(BlueprintPure,Category="NinjaPhysicsVolume")
	virtual float GetGravityMagnitude(class USceneComponent* SceneComponent) const;

	/**
	 * Obtains the scale factor that affects magnitude of current gravity vector
	 * that influences a SceneComponent.
	 * @param SceneComponent - component in space affected by gravity
	 * @return final gravity scale factor
	 */
	UFUNCTION(BlueprintPure,Category="NinjaPhysicsVolume")
	virtual float GetFinalGravityScale(class USceneComponent* SceneComponent) const;

protected:
	/** Mode that determines direction of gravity. */
	UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="NinjaPhysicsVolume")
	ENinjaGravityDirectionMode GravityDirectionMode;

	/** Stores information that determines direction of gravity. */
	UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="NinjaPhysicsVolume")
	FVector GravityVectorA;

	/** Stores additional information that determines direction of gravity. */
	UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="NinjaPhysicsVolume")
	FVector GravityVectorB;

	/** Optional Actor that determines direction of gravity. */
	UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="NinjaPhysicsVolume")
	TObjectPtr<AActor> GravityActor;

public:
	/**
	 * Sets a new fixed gravity direction.
	 * @note It can be influenced by GravityScale
	 * @param NewGravityDirection - new gravity direction, assumes it isn't normalized
	 */
	UFUNCTION(BlueprintCallable,Category="NinjaPhysicsVolume",Meta=(DisplayName="Set Fixed Gravity Direction",ScriptName="SetFixedGravityDirection"))
	virtual void K2_SetFixedGravityDirection(const FVector& NewGravityDirection);

	/**
	 * Sets a new fixed gravity direction.
	 * @note It can be influenced by GravityScale
	 * @param NewFixedGravityDirection - new fixed gravity direction, assumes it is normalized
	 */
	virtual void SetFixedGravityDirection(const FVector& NewFixedGravityDirection);

public:
	/**
	 * Sets a new gravity direction determined by closest spline tangent.
	 * @note It can be influenced by GravityScale
	 * @param NewGravityActor - Actor that provides a spline
	 */
	UFUNCTION(BlueprintCallable,Category="NinjaPhysicsVolume")
	virtual void SetSplineTangentGravityDirection(AActor* NewGravityActor);

public:
	/**
	 * Sets a new point which gravity direction points to.
	 * @note It can be influenced by GravityScale
	 * @param NewGravityPoint - new point which gravity direction points to
	 */
	UFUNCTION(BlueprintCallable,Category="NinjaPhysicsVolume")
	virtual void SetPointGravityDirection(const FVector& NewGravityPoint);

	/**
	 * Sets a new point which gravity direction points to.
	 * @note It can be influenced by GravityScale
	 * @param NewGravityActor - Actor that provides its location as gravity point
	 */
	UFUNCTION(BlueprintCallable,Category="NinjaPhysicsVolume")
	virtual void SetPointGravityDirectionFromActor(AActor* NewGravityActor);

public:
	/**
	 * Sets a new infinite line which gravity direction points to.
	 * @note It can be influenced by GravityScale
	 * @param NewGravityLineStart - a point that belongs to the infinite line
	 * @param NewGravityLineEnd - another point that belongs to the infinite line
	 */
	UFUNCTION(BlueprintCallable,Category="NinjaPhysicsVolume")
	virtual void SetLineGravityDirection(const FVector& NewGravityLineStart, const FVector& NewGravityLineEnd);

public:
	/**
	 * Sets a new segment line which gravity direction points to.
	 * @note It can be influenced by GravityScale
	 * @param NewGravitySegmentStart - start point of the segment line
	 * @param NewGravitySegmentEnd - end point of the segment line
	 */
	UFUNCTION(BlueprintCallable,Category="NinjaPhysicsVolume")
	virtual void SetSegmentGravityDirection(const FVector& NewGravitySegmentStart, const FVector& NewGravitySegmentEnd);

public:
	/**
	 * Sets a new spline which gravity direction points to.
	 * @note It can be influenced by GravityScale
	 * @param NewGravityActor - Actor that provides a spline
	 */
	UFUNCTION(BlueprintCallable,Category="NinjaPhysicsVolume")
	virtual void SetSplineGravityDirection(AActor* NewGravityActor);

public:
	/**
	 * Sets a new infinite plane which gravity direction points to.
	 * @note It can be influenced by GravityScale
	 * @param NewGravityPlaneBase - a point that belongs to the plane
	 * @param NewGravityPlaneNormal - normal of the plane, assumes it isn't normalized
	 */
	UFUNCTION(BlueprintCallable,Category="NinjaPhysicsVolume",Meta=(DisplayName="Set Plane Gravity Direction",ScriptName="SetPlaneGravityDirection"))
	virtual void K2_SetPlaneGravityDirection(const FVector& NewGravityPlaneBase, const FVector& NewGravityPlaneNormal);

	/**
	 * Sets a new infinite plane which gravity direction points to.
	 * @note It can be influenced by GravityScale
	 * @param NewGravityPlaneBase - a point that belongs to the plane
	 * @param NewGravityPlaneNormal - normal of the plane, assumes it is normalized
	 */
	virtual void SetPlaneGravityDirection(const FVector& NewGravityPlaneBase, const FVector& NewGravityPlaneNormal);

public:
	/**
	 * Sets a new infinite plane determined by closest spline point and spline
	 * up vector which gravity direction points to.
	 * @note It can be influenced by GravityScale
	 * @param NewGravityActor - Actor that provides a spline
	 */
	UFUNCTION(BlueprintCallable,Category="NinjaPhysicsVolume")
	virtual void SetSplinePlaneGravityDirection(AActor* NewGravityActor);

public:
	/**
	 * Sets a new axis-aligned box which gravity direction points to.
	 * @note It can be influenced by GravityScale
	 * @param NewGravityBoxOrigin - origin of the box
	 * @param NewGravityBoxExtent - extents of the box
	 */
	UFUNCTION(BlueprintCallable,Category="NinjaPhysicsVolume")
	virtual void SetBoxGravityDirection(const FVector& NewGravityBoxOrigin, const FVector& NewGravityBoxExtent);

	/**
	 * Sets a new axis-aligned box which gravity direction points to.
	 * @note It can be influenced by GravityScale
	 * @param NewGravityActor - Actor that provides its collision bounding box as gravity target
	 */
	UFUNCTION(BlueprintCallable,Category="NinjaPhysicsVolume")
	virtual void SetBoxGravityDirectionFromActor(AActor* NewGravityActor);

public:
	/**
	 * Sets a new collision geometry which gravity direction points to.
	 * @note It can be influenced by GravityScale
	 * @param NewGravityActor - Actor that owns the PrimitiveComponent that has collision geometry
	 */
	UFUNCTION(BlueprintCallable,Category="NinjaPhysicsVolume")
	virtual void SetCollisionGravityDirection(AActor* NewGravityActor);

protected:
	/** Gravity vector is multiplied by this amount. */
	UPROPERTY(EditAnywhere,BlueprintReadWrite,BlueprintSetter=SetGravityScale,Category="NinjaPhysicsVolume")
	float GravityScale;

public:
	/**
	 * Obtains the scale factor that affects magnitude of current gravity.
	 * @return gravity scale factor
	 */
	UFUNCTION(BlueprintPure,Category="NinjaPhysicsVolume")
	virtual float GetGravityScale() const;

	/**
	 * Sets a new scale factor that affects magnitude of current gravity.
	 * @param NewGravityScale - new gravity scale factor
	 */
	UFUNCTION(BlueprintSetter,Category="NinjaPhysicsVolume")
	virtual void SetGravityScale(float NewGravityScale);

public:
	/** Imparts this falling velocity to entering walking Ninjas. */
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="NinjaPhysicsVolume")
	FVector NinjaFallVelocity;
};
