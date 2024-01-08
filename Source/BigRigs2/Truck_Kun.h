// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Truck_Kun.generated.h"

UCLASS()
class BIGRIGS2_API ATruck_Kun : public ACharacter
{
	GENERATED_BODY()

	float TANK_ROTATION_SPEED = 0.5f;
	
	float TurnRot = 0;
	float ForwardSpeed = 0;

public:
	// Sets default values for this character's properties
	ATruck_Kun();

	float TimeHoldingForward;
	float TimeHoldingBackward;
	
	bool HoldingForward = false;
	bool HoldingBackward = false;
	bool Drifting = false;

	UPROPERTY(EditAnywhere,Category="Vehicle Driving")
	float MaxHoldingTime = 5;

	UPROPERTY(EditAnywhere,Category="Vehicle Driving")
	float TruckBaseForwardSpeed = 5000;

	UPROPERTY(EditAnywhere,Category="Vehicle Driving")
	float TruckBaseBackwardSpeed = 4250;

	UPROPERTY(EditAnywhere,Category="Vehicle Driving")
	UCurveFloat* ForwardSpeedCurve;

	UPROPERTY(EditAnywhere,Category="Vehicle Driving")
	UCurveFloat* BackwardSpeedCurve;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	
public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	void ForwardInput(float Val);
	void RightInput(float Val);

	void startDrift();
	void endDrift();
	void CameraYaw(float _yaw);
	void CameraPitch(float _pitch);

	UFUNCTION(BlueprintImplementableEvent)
	void onStartDrifting();
	UFUNCTION(BlueprintImplementableEvent)
	void onEndDrifting();
};
