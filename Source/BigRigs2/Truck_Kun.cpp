// Fill out your copyright notice in the Description page of Project Settings.


#include "Truck_Kun.h"

// Sets default values
ATruck_Kun::ATruck_Kun()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

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
	if (Val != 0.0f)
	{
		// add movement in that direction
		AddMovementInput(GetActorForwardVector(), Val);
	}
}

void ATruck_Kun::RightInput(float Val)
{
	if (Val != 0.0f)
	{
		// add movement in that direction
		AddMovementInput(GetActorRightVector(), Val);
	}
}

