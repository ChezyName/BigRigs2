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
	float TurningValue = 0;
	bool Drifting = false;

	bool CameraReOrienting = false;

	float TimeTurning = 0;

	UPROPERTY(EditAnywhere,Category="Vehicle Driving|Forward/Backward")
	float MaxHoldingTime = 5;

	UPROPERTY(EditAnywhere,Category="Vehicle Driving|Forward/Backward")
	float TruckBaseForwardSpeed = 5000;

	UPROPERTY(EditAnywhere,Category="Vehicle Driving|Forward/Backward")
	float TruckBaseBackwardSpeed = 4250;

	UPROPERTY(EditAnywhere,Category="Vehicle Driving|Forward/Backward")
	UCurveFloat* ForwardSpeedCurve;

	UPROPERTY(EditAnywhere,Category="Vehicle Driving|Forward/Backward")
	UCurveFloat* BackwardSpeedCurve;

	UPROPERTY(EditAnywhere,Category="Vehicle Driving|Turning")
	float MaxTurnHoldingTime = 5;

	UPROPERTY(EditAnywhere,Category="Vehicle Driving|Turning")
	UCurveFloat* TurnCurve;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	/** Front Camera component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FrontCamera;

	/** Back Camera component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* BackCamera;

	void ReOrientCamera();
	void UnReOrientCamera();
	
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
