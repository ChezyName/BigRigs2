// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Checkpoint.generated.h"

UCLASS()
class BIGRIGS2_API ACheckpoint : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ACheckpoint();

	UPROPERTY(EditAnywhere,BlueprintReadOnly)
	int CheckpointNumber = 1;

	UStaticMeshComponent* GoalRing;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

};
