// "Ninja Character" plugin, by Javier 'Xaklse' Osset; Copyright 2020


#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "NinjaPlayerCameraManager.generated.h"


/**
 * Object that defines the master camera that the player actually uses to look
 * through. This type is able to handle arbitrary collision capsule orientation.
 */
UCLASS()
class NINJACHARACTER_API ANinjaPlayerCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()

public:
	/**
	 * Called to adjust view rotation updates before they are applied.
	 * e.g. The base implementation enforces view rotation limits using LimitViewPitch, et al.
	 * @param DeltaTime - frame time in seconds
	 * @param OutViewRotation - the view rotation to modify
	 * @param OutDeltaRot - how much the rotation changed this frame
	 */
	virtual void ProcessViewRotation(float DeltaTime, FRotator& OutViewRotation, FRotator& OutDeltaRot) override;
};
