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
	//Drive Curves

	//Forward Driving Button Holding
	if(!(HoldingForward && HoldingBackward))
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

	TimeHoldingForward = FMath::Clamp(TimeHoldingForward,0,MaxHoldingTime);
	TimeHoldingBackward = FMath::Clamp(TimeHoldingBackward,0,MaxHoldingTime);

	//Forward Driving
	if(TimeHoldingForward > 0)
	{
		float FSpeed = ForwardSpeedCurve->GetFloatValue(TimeHoldingForward);
		GetCharacterMovement()->MaxWalkSpeed = FSpeed * TruckBaseForwardSpeed;
		AddMovementInput(GetActorForwardVector(),FSpeed);
	}
	else if(TimeHoldingBackward > 0)
	{
		float BSpeed = BackwardSpeedCurve->GetFloatValue(TimeHoldingBackward);
		GetCharacterMovement()->MaxWalkSpeed = BSpeed * TruckBaseBackwardSpeed;
		AddMovementInput(GetActorForwardVector(),-BSpeed);
	}

	//Align To What Is Under It
	FVector Start = GetActorLocation();
	FVector End = GetActorLocation() + (GetActorForwardVector() * 25 + -GetActorUpVector() * 50) * 8;
	End = Start + GetActorUpVector() * -150;
	DrawDebugLine(GetWorld(),Start,End,FColor::Red,false,-1,0,2);

	FCollisionQueryParams CollisionParams;
	CollisionParams.AddIgnoredActor(this);

	FHitResult HitResult;

	// Perform the raycast
	bool bHit = GetWorld()->LineTraceSingleByChannel(HitResult, Start, End, ECC_Visibility, CollisionParams);

	// Debug draw the ray
	if (bHit)
	{
		//Hit Object
		DrawDebugDirectionalArrow(GetWorld(),HitResult.ImpactPoint,
			HitResult.ImpactPoint + HitResult.ImpactNormal * 150,
			5, FColor::Green);

		GetCharacterMovement()->SetGravityDirection(-HitResult.ImpactNormal);

		FVector GroundNormal = HitResult.ImpactNormal;
		GroundNormal.Normalize();

		FRotator NewRot = FQuat::FindBetweenVectors(FVector::UpVector, GroundNormal).Rotator();
		SetActorRotation(UKismetMathLibrary::MakeRotFromZX(HitResult.ImpactNormal,GetActorForwardVector()));
		
		//if(HitResult.GetActor()) GEngine->AddOnScreenDebugMessage(-1,0,FColor::Green,"HIT: " + HitResult.GetActor()->GetName());
	}
	else
	{
		FRotator NewRot = GetActorRotation();
		NewRot.Pitch = 0;
		//SetActorRotation(NewRot);
		//GetCharacterMovement()->SetGravityDirection(FVector::DownVector);
	}
}

// Called to bind functionality to input
void ATruck_Kun::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	check(PlayerInputComponent);
	
	PlayerInputComponent->BindAxis("MoveForward",this,&ATruck_Kun::ForwardInput);
	PlayerInputComponent->BindAxis("MoveRight",this,&ATruck_Kun::RightInput);
}

void ATruck_Kun::ForwardInput(float Val)
{
	HoldingForward = Val > 0;
	HoldingBackward = Val < 0;
}

void ATruck_Kun::RightInput(float Val)
{
	if (Val != 0.0f && GetController() && GetVelocity().Length() > 0)
	{
		FRotator CRotaion = GetControlRotation();
		float TurnSpeed = (TimeHoldingForward/MaxHoldingTime) > (TimeHoldingBackward/MaxHoldingTime) ?
			(TimeHoldingForward/MaxHoldingTime) : (TimeHoldingBackward/MaxHoldingTime);
		CRotaion.Yaw += Val * (TANK_ROTATION_SPEED * TurnSpeed);
		GetController()->SetControlRotation(CRotaion);
	}
}
