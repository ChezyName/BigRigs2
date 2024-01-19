// "Ninja Character" plugin, by Javier 'Xaklse' Osset; Copyright 2020


#include "NinjaPhysicsVolumeSpline.h"

#include "Components/BrushComponent.h"
#include "Components/SplineComponent.h"


FName ANinjaPhysicsVolumeSpline::SplineComponentName(TEXT("SplineComponent"));


ANinjaPhysicsVolumeSpline::ANinjaPhysicsVolumeSpline(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Create and configure attached spline component
	SplineComponent = CreateDefaultSubobject<USplineComponent>(SplineComponentName);
	SplineComponent->Mobility = EComponentMobility::Static;
	SplineComponent->SetupAttachment(GetBrushComponent());

	GravityActor = nullptr;
	GravityDirectionMode = ENinjaGravityDirectionMode::Spline;
	GravityVectorA = FVector::ZeroVector;
	GravityVectorB = FVector::ZeroVector;
}
