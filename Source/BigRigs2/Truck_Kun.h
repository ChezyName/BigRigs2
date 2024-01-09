// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Truck_Kun.generated.h"

class UCameraComponent;
class USpringArmComponent;

UCLASS()
class BIGRIGS2_API ATruck_Kun : public ACharacter
{
	GENERATED_BODY()

	float TANK_ROTATION_SPEED = 0.75f;
	
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

	/** Front Camera component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FrontCamera;

	/** Back Camera component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* BackCamera;
	
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

	void ToggleCamera();
	bool bFrontCameraActive = false;

	UFUNCTION(BlueprintImplementableEvent)
	void onStartDrifting();
	UFUNCTION(BlueprintImplementableEvent)
	void onEndDrifting();
};
