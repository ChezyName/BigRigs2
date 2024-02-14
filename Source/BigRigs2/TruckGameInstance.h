// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Truck_Kun.h"
#include "Engine/GameInstance.h"
#include "Kismet/KismetSystemLibrary.h"
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

USTRUCT(BlueprintType)  
struct FMapTime
{  
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	FString MapName;

	UPROPERTY(BlueprintReadOnly)
	float Time = 0;
};

/**
 * 
 */
UCLASS()
class BIGRIGS2_API UTruckGameInstance : public UGameInstance
{
	GENERATED_BODY()

	FString DataPath;

	virtual void Init() override;
	
public:
	UPROPERTY(BlueprintReadWrite,EditAnywhere)
	TSubclassOf<ATruck_Kun> PlayersTruck;

	UPROPERTY(BlueprintReadWrite,EditAnywhere)
	TArray<FMapTime> Maps;

	UPROPERTY(BlueprintReadWrite,EditAnywhere)
	int currentMap = -1;
	
	UFUNCTION(BlueprintCallable)
	void setBestTime(float Time);

	UFUNCTION(BlueprintCallable)
	void getBestTimes();
	
	UFUNCTION(BlueprintCallable)
	FString msToTime(float Time){
		// Calculate minutes, seconds, and milliseconds
		int32 Minutes = Time / 60000;
		int32 Seconds = (FMath::RoundToInt(Time) % 60000) / 1000;
		int32 Millis = (FMath::RoundToInt(Time) % 1000);

		// Format the string
		return FString::Printf(TEXT("%02d:%02d:%03d"), Minutes, Seconds, Millis);
	}
};
