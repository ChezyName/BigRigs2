// "Ninja Character" plugin, by Javier 'Xaklse' Osset; Copyright 2020


#pragma once

#include "CoreMinimal.h"
#include "NinjaPhysicsVolume.h"
#include "NinjaPhysicsVolumeSpline.generated.h"


/**
 * A PhysicsVolume is a bounding volume that affects Actor physics. This type
 * allows overriding the gravity direction with the help of a spline.
 */
UCLASS()
class NINJACHARACTER_API ANinjaPhysicsVolumeSpline : public ANinjaPhysicsVolume
{
	GENERATED_BODY()

public:
	ANinjaPhysicsVolumeSpline(const FObjectInitializer& ObjectInitializer);

protected:
	/** The SplineComponent subobject. */
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="NinjaPhysicsVolume")
	TObjectPtr<class USplineComponent> SplineComponent;

public:
	/** Name of the SplineComponent. */
	static FName SplineComponentName;
};
