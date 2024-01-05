// Copyright Epic Games, Inc. All Rights Reserved.

#include "BigRigs2WheelFront.h"
#include "UObject/ConstructorHelpers.h"

UBigRigs2WheelFront::UBigRigs2WheelFront()
{
	AxleType = EAxleType::Front;
	bAffectedBySteering = true;
	MaxSteerAngle = 40.f;
}