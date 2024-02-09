// Copyright Epic Games, Inc. All Rights Reserved.

#include "BigRigs2GameMode.h"
#include "BigRigs2PlayerController.h"
#include "TruckGameInstance.h"

ABigRigs2GameMode::ABigRigs2GameMode()
{
	PlayerControllerClass = ABigRigs2PlayerController::StaticClass();
}

UClass* ABigRigs2GameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
	UTruckGameInstance* GI = Cast<UTruckGameInstance>(GetGameInstance());
	if(GI->PlayersTruck)
	{
		GEngine->AddOnScreenDebugMessage(-1,120,FColor::Red,FString("Spawing Player's Truck-Kun: ") + FString(GI->PlayersTruck->GetPathName()));
		return GI->PlayersTruck;
	}
	return DefaultPawnClass;
}