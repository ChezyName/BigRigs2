// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Truck_Kun.generated.h"

UCLASS()
class BIGRIGS2_API ATruck_Kun : public ACharacter
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	ATruck_Kun();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	FVector2d MovementInput;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	void ForwardInput(float Val);
	void RightInput(float Val);

};
