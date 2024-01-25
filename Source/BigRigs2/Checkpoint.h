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

	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Checkpoint")
	int ThisCheckpointNumber = 1;

	
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="End Point")
	bool isEndGoal = false;

	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="End Point",meta=(EditCondition="isEndGoal==true",EditConditionHides))
	int FinalCheckpoint = 1;

	UPROPERTY(EditDefaultsOnly,meta=(AllowPrivateAccess))
	UStaticMeshComponent* GoalRing;

	UPROPERTY(EditDefaultsOnly,meta=(AllowPrivateAccess),Category="Goal Materials")
	UMaterial* NonHighlightedMat;

	UPROPERTY(EditDefaultsOnly,meta=(AllowPrivateAccess),Category="Goal Materials")
	UMaterial* NextGoalMat;

	void onCheckpoint(int nextCheckpoint);

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
};
