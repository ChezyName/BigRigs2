// Fill out your copyright notice in the Description page of Project Settings.


#include "Checkpoint.h"
#include "Kismet/GameplayStatics.h"

// Sets default values
ACheckpoint::ACheckpoint()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

	GoalRing = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Goal Ring"));
	RootComponent = GoalRing;
}

void ACheckpoint::onCheckpoint(int nextCheckpoint)
{
	if(ThisCheckpointNumber)
}

void ACheckpoint::BeginPlay()
{
	Super::BeginPlay();
	onCheckpoint(0);
}

