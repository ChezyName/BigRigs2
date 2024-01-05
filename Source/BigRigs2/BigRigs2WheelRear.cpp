// Copyright Epic Games, Inc. All Rights Reserved.

#include "BigRigs2WheelRear.h"
#include "UObject/ConstructorHelpers.h"

UBigRigs2WheelRear::UBigRigs2WheelRear()
{
	AxleType = EAxleType::Rear;
	bAffectedByHandbrake = true;
	bAffectedByEngine = true;
}