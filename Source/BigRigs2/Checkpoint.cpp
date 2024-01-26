// Fill out your copyright notice in the Description page of Project Settings.


#include "Checkpoint.h"
#include "Kismet/GameplayStatics.h"

// Sets default values
ACheckpoint::ACheckpoint()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

	GoalRing = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Goal Ring"));
	GoalRing->SetCollisionProfileName("OverlapAll");
	RootComponent = GoalRing;
}

void ACheckpoint::onCheckpoint(int nextCheckpoint)
{
	if(ThisCheckpointNumber == nextCheckpoint)
	{
		//This is our Next Checkpoint - GLOW
		if(NextGoalMat) GoalRing->SetMaterial(0,NextGoalMat);
		GoalRing->SetVisibility(true);
	}
	else if(ThisCheckpointNumber == (nextCheckpoint+1))
	{
		//This is our Next-Next Checkpoint - SEMI Glow
		if(NonHighlightedMat) GoalRing->SetMaterial(0,NonHighlightedMat);
		GoalRing->SetVisibility(true);
	}
	else
	{
		//HIDE
		GoalRing->SetVisibility(false);
	}
}

void ACheckpoint::BeginPlay()
{
	Super::BeginPlay();
	onCheckpoint(0);
}

