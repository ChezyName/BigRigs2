// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Truck_Kun.h"
#include "Engine/GameInstance.h"
#include "TruckGameInstance.generated.h"

USTRUCT(BlueprintType)
struct FTruckInfo : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	FString TruckName;
	
	UPROPERTY(EditAnywhere)
	UTexture* TruckTexture;
	
	UPROPERTY(EditAnywhere)
	TSubclassOf<ATruck_Kun> TruckRef;
};

/**
 * 
 */
UCLASS()
class BIGRIGS2_API UTruckGameInstance : public UGameInstance
{
	GENERATED_BODY()
	
public:
	UPROPERTY(BlueprintReadWrite,EditAnywhere)
	TSubclassOf<ATruck_Kun> PlayersTruck;
};
