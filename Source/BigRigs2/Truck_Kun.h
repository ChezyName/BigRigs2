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
	
	float TurnRot = 0;
	float ForwardSpeed = 0;

	UPROPERTY(Replicated)
	int CheckpointNumber = 0;
	
	float timer = 0;
	bool timeStarted = false;
	bool canDrive = false;

	float ResetTime = 0;
	float MaxResetTime = 0.6;

	bool isReseting;

public:
	UFUNCTION(BlueprintCallable)
	int getCheckpoint(){ return CheckpointNumber; }

	UFUNCTION(BlueprintCallable)
	float getResetPercentage(){ return ResetTime/MaxResetTime; }

	UFUNCTION(BlueprintCallable)
	FString getTime(){
		// Calculate minutes, seconds, and milliseconds
		int32 Minutes = timer / 60000;
		int32 Seconds = (FMath::RoundToInt(timer) % 60000) / 1000;
		int32 Millis = (FMath::RoundToInt(timer) % 1000);

		// Format the string
		return FString::Printf(TEXT("%02d:%02d:%03d"), Minutes, Seconds, Millis);
	}
	
	// Sets default values for this character's properties
	ATruck_Kun(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(Replicated)
	float TimeHoldingForward;

	UPROPERTY(Replicated)
	float TimeHoldingBackward;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	
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
	float TurnAngleMultiplier = 0.75f;

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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	UAudioComponent* EngineSFX;

	void ReOrientCamera();
	void UnReOrientCamera();

	void ResetActivate();
	void DeActivateReset();

	UFUNCTION()
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable)
	AActor* getNextCheckpoint();

	UFUNCTION(BlueprintCallable)
	void startDriving();

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
