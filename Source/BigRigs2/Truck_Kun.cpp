// Fill out your copyright notice in the Description page of Project Settings.


#include "Truck_Kun.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetMathLibrary.h"

// Sets default values
ATruck_Kun::ATruck_Kun()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	bUseControllerRotationYaw = true;
	GetCharacterMovement()->DefaultLandMovementMode = MOVE_Flying;
}

// Called when the game starts or when spawned
void ATruck_Kun::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void ATruck_Kun::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	//DEBUG
	//GEngine->AddOnScreenDebugMessage(-1,0,Drifting ? FColor::Green : FColor::Red,"Drifting?");
	
	//Forward Driving Button Holding
	if(!(HoldingForward && HoldingBackward) && !Drifting)
	{
		if(HoldingForward)
		{
			TimeHoldingForward += DeltaTime;
			TimeHoldingBackward -= DeltaTime;
		}
		else TimeHoldingForward -= (DeltaTime/2);

		//Backward Driving Button Holding
		if(HoldingBackward)
		{
			TimeHoldingBackward += DeltaTime;
			TimeHoldingForward -= DeltaTime;
		}
		else TimeHoldingBackward -= (DeltaTime/2);
	}

	if(Drifting && ForwardSpeed == 0)
	{
		TimeHoldingBackward -= DeltaTime * 1.5;
		TimeHoldingForward -= DeltaTime * 1.5;
	}
	if(Drifting)
	{
		TimeHoldingBackward -= DeltaTime * 0.45;
		TimeHoldingForward -= DeltaTime * 0.45;
	}

	TimeHoldingForward = FMath::Clamp(TimeHoldingForward,0,MaxHoldingTime);
	TimeHoldingBackward = FMath::Clamp(TimeHoldingBackward,0,MaxHoldingTime);

	//Forward Driving
	if(TimeHoldingForward > 0)
	{
		float FSpeed = ForwardSpeedCurve->GetFloatValue(TimeHoldingForward);
		GetCharacterMovement()->MaxWalkSpeed = FSpeed * TruckBaseForwardSpeed;
		if(Drifting)
		{
			if(TurnRot > 0)
			{
				//Turning Right
				AddMovementInput(GetActorForwardVector() + (GetActorRightVector()*0.15),FSpeed);
			}
			else if(TurnRot < 0)
			{
				//Turning Left
				AddMovementInput(GetActorForwardVector() + -(GetActorRightVector()*0.15),FSpeed);
			}
		}
		else
		{
			AddMovementInput(GetActorForwardVector(),FSpeed);
		}
	}
	else if(TimeHoldingBackward > 0)
	{
		float BSpeed = BackwardSpeedCurve->GetFloatValue(TimeHoldingBackward);
		GetCharacterMovement()->MaxWalkSpeed = BSpeed * TruckBaseBackwardSpeed;
		AddMovementInput(GetActorForwardVector(),-BSpeed);
	}

	//VFX
	//Drifting VFX
	if(Drifting && TimeHoldingForward > (MaxHoldingTime*0.45)) onStartDrifting();
	else onEndDrifting();
}

// Called to bind functionality to input
void ATruck_Kun::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	check(PlayerInputComponent);
	
	PlayerInputComponent->BindAxis("MoveForward",this,&ATruck_Kun::ForwardInput);
	PlayerInputComponent->BindAxis("MoveRight",this,&ATruck_Kun::RightInput);

	PlayerInputComponent->BindAction("Handbrake",IE_Pressed,this,&ATruck_Kun::startDrift);
	PlayerInputComponent->BindAction("Handbrake",IE_Released,this,&ATruck_Kun::endDrift);

	PlayerInputComponent->BindAxis("LookRight", this, &ATruck_Kun::CameraYaw);
	PlayerInputComponent->BindAxis("LookUp", this, &ATruck_Kun::CameraPitch);
}

void ATruck_Kun::ForwardInput(float Val)
{
	ForwardSpeed = Val;
	HoldingForward = Val > 0;
	HoldingBackward = Val < 0;
}

void ATruck_Kun::RightInput(float Val)
{
	TurnRot = Val;
	if (Val != 0.0f && GetController() && GetVelocity().Length() > 0)
	{
		FRotator CRotaion = GetActorRotation();
		float TurnSpeed = (TimeHoldingForward/MaxHoldingTime) > (TimeHoldingBackward/MaxHoldingTime) ?
			(TimeHoldingForward/MaxHoldingTime) : (TimeHoldingBackward/MaxHoldingTime);
		
		if(Drifting && TimeHoldingForward > (MaxHoldingTime*0.45)) TurnSpeed *= 2;
		if(Drifting && TimeHoldingForward > (MaxHoldingTime*0.45) && ForwardSpeed == 0) TurnSpeed *= 1.5;
		
		CRotaion.Yaw += Val * (TANK_ROTATION_SPEED * TurnSpeed);
		SetActorRotation(CRotaion);
	}
}

void ATruck_Kun::startDrift()
{
	Drifting = true;
	//onStartDrifting();
}

void ATruck_Kun::endDrift()
{
	Drifting = false;
	//onEndDrifting();
}

void ATruck_Kun::CameraYaw(float _yaw)
{
	AddControllerYawInput(_yaw);
}

void ATruck_Kun::CameraPitch(float _pitch)
{
	AddControllerPitchInput(_pitch);
}