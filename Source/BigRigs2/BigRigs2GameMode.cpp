// Copyright Epic Games, Inc. All Rights Reserved.

#include "BigRigs2GameMode.h"
#include "BigRigs2PlayerController.h"

ABigRigs2GameMode::ABigRigs2GameMode()
{
	PlayerControllerClass = ABigRigs2PlayerController::StaticClass();
}
