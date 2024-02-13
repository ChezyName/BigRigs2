// Fill out your copyright notice in the Description page of Project Settings.


#include "TruckGameInstance.h"
#include "JsonObjectConverter.h"

void UTruckGameInstance::Init()
{
	DataPath = UKismetSystemLibrary::GetProjectDirectory();
	getBestTimes();
	GEngine->AddOnScreenDebugMessage(-1,120,FColor::Green,"PATH -> "+DataPath);
	Super::Init();
}

void UTruckGameInstance::setBestTime(float Time)
{
	if(currentMap < 0) return;
	FMapTime Map = Maps[currentMap];
	Map.Time = Time;
	//Set This Map's File as Best Time if currentTime < Map.BestTime
	FString FileName = DataPath + "/" + Map.MapName + ".json";
	FJsonObjectConverter::UStructToJsonObjectString(Map, FileName);
	UE_LOG( LogTemp, Warning, TEXT("Saved File: %s"), *FileName);
}

void UTruckGameInstance::getBestTimes()
{
	//Load All Times from Map
	for (int i = 0; i < Maps.Num(); i++)
	{
		FString FileName = DataPath + "/" + Maps[i].MapName + ".json";
		FMapTime LoadedMapTime;
		if(!FJsonObjectConverter::JsonObjectStringToUStruct(FileName, &LoadedMapTime, 0, 0))
		{
			// ERROR!
			UE_LOG(LogTemp, Warning, TEXT("Error Loading File: %s"), *FileName);
		}

		Maps[i] = LoadedMapTime;
	}
}
