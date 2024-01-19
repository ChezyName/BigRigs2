// "Ninja Character" plugin, by Javier 'Xaklse' Osset; Copyright 2020


#include "NinjaPhysicsVolume.h"

#include "NinjaCharacter.h"
#include "NinjaCharacterMovementComponent.h"

#include "Components/BrushComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SplineComponent.h"

#if WITH_EDITORONLY_DATA
#include "Components/TextRenderComponent.h"
#endif

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#include "DrawDebugHelpers.h"
#endif


DECLARE_CYCLE_STAT(TEXT("PhysicsVolume GetGravity"), STAT_PhysicsVolumeGetGravity, STATGROUP_Game);
DECLARE_CYCLE_STAT(TEXT("PhysicsVolume GetGravityDirection"), STAT_PhysicsVolumeGetGravityDirection, STATGROUP_Game);


#if WITH_EDITORONLY_DATA
FName ANinjaPhysicsVolume::TextRenderComponentName(TEXT("TextRenderComponent"));
#endif

namespace NinjaPhysicsVolumeCVars
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	static int32 ShowGravity = 0;
	FAutoConsoleVariableRef CVarShowGravity(
		TEXT("npv.ShowGravity"),
		ShowGravity,
		TEXT("Whether to draw in-world debug information for calculated gravities.\n")
		TEXT("0: Disable, 1: Enable"),
		EConsoleVariableFlags::ECVF_Cheat);
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
}


ANinjaPhysicsVolume::ANinjaPhysicsVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Create and configure attached text render component
	TextRenderComponent = CreateEditorOnlyDefaultSubobject<UTextRenderComponent>(TextRenderComponentName);
	if (!IsRunningCommandlet() && TextRenderComponent != nullptr)
	{
		TextRenderComponent->bHiddenInGame = true;
		TextRenderComponent->HorizontalAlignment = EHorizTextAligment::EHTA_Center;
		TextRenderComponent->Text = FText::AsCultureInvariant(TEXT("Ninja Physics Volume"));
		TextRenderComponent->SetUsingAbsoluteRotation(true);
		TextRenderComponent->SetupAttachment(GetBrushComponent());
	}
#endif // WITH_EDITORONLY_DATA

	// Change 'tick' configuration
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	GravityActor = nullptr;
	GravityDirectionMode = ENinjaGravityDirectionMode::Fixed;
	GravityScale = 1.0f;
	GravityVectorA = FVector(0.0f, 0.0f, -1.0f);
	GravityVectorB = FVector::ZeroVector;
	NinjaFallVelocity = FVector::ZeroVector;
}

void ANinjaPhysicsVolume::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	for (AActor* TrackedActor : TrackedActors)
	{
		if (IsValid(TrackedActor))
		{
			UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(TrackedActor->GetRootComponent());
			if (Primitive->IsGravityEnabled())
			{
				// Add force combination of reverse engine's gravity and custom gravity
				const FVector GravityForce = FVector(0.0f, 0.0f, GetGravityZ() * -1.0f) +
					GetGravity(Primitive);

				USkeletalMeshComponent* SkeletalMesh = Cast<USkeletalMeshComponent>(Primitive);
				if (SkeletalMesh != nullptr)
				{
					SkeletalMesh->AddForceToAllBodiesBelow(GravityForce, NAME_None, true, true);
				}
				else
				{
					Primitive->AddForce(GravityForce, NAME_None, true);
				}
			}
		}
	}
}

void ANinjaPhysicsVolume::ActorEnteredVolume(AActor* Other)
{
	Super::ActorEnteredVolume(Other);

	// Remove all empty positions of the TrackedActors list
	TrackedActors.RemoveAll([](AActor* Actor)
	{
		return Actor == nullptr;
	});

	// Remove all empty positions of the TrackedNinjas list
	TrackedNinjas.RemoveAll([](ANinjaCharacter* Ninja)
	{
		return Ninja == nullptr;
	});

	if (IsValid(Other))
	{
		ANinjaCharacter* Ninja = Cast<ANinjaCharacter>(Other);
		if (Ninja != nullptr)
		{
			UNinjaCharacterMovementComponent* NinjaCharMoveComp = Ninja->GetNinjaCharacterMovement();

			// Just change gravity settings of Ninjas
			switch (GravityDirectionMode)
			{
				case ENinjaGravityDirectionMode::Fixed:
				{
					NinjaCharMoveComp->SetFixedGravityDirection(GravityVectorA);
					break;
				}

				case ENinjaGravityDirectionMode::SplineTangent:
				{
					if (IsValid(GravityActor))
					{
						NinjaCharMoveComp->SetSplineTangentGravityDirection(GravityActor);
					}
					else
					{
						NinjaCharMoveComp->SetSplineTangentGravityDirection(this);
					}

					break;
				}

				case ENinjaGravityDirectionMode::Point:
				{
					if (IsValid(GravityActor))
					{
						NinjaCharMoveComp->SetPointGravityDirectionFromActor(GravityActor);
					}
					else
					{
						NinjaCharMoveComp->SetPointGravityDirection(GravityVectorA);
					}

					break;
				}

				case ENinjaGravityDirectionMode::Line:
				{
					NinjaCharMoveComp->SetLineGravityDirection(GravityVectorA, GravityVectorB);
					break;
				}

				case ENinjaGravityDirectionMode::Segment:
				{
					NinjaCharMoveComp->SetSegmentGravityDirection(GravityVectorA, GravityVectorB);
					break;
				}

				case ENinjaGravityDirectionMode::Spline:
				{
					if (IsValid(GravityActor))
					{
						NinjaCharMoveComp->SetSplineGravityDirection(GravityActor);
					}
					else
					{
						NinjaCharMoveComp->SetSplineGravityDirection(this);
					}

					break;
				}

				case ENinjaGravityDirectionMode::Plane:
				{
					NinjaCharMoveComp->SetPlaneGravityDirection(GravityVectorA, GravityVectorB);
					break;
				}

				case ENinjaGravityDirectionMode::SplinePlane:
				{
					if (IsValid(GravityActor))
					{
						NinjaCharMoveComp->SetSplinePlaneGravityDirection(GravityActor);
					}
					else
					{
						NinjaCharMoveComp->SetSplinePlaneGravityDirection(this);
					}

					break;
				}

				case ENinjaGravityDirectionMode::Box:
				{
					if (IsValid(GravityActor))
					{
						NinjaCharMoveComp->SetBoxGravityDirectionFromActor(GravityActor);
					}
					else
					{
						NinjaCharMoveComp->SetBoxGravityDirection(GravityVectorA, GravityVectorB);
					}

					break;
				}

				case ENinjaGravityDirectionMode::Collision:
				{
					if (IsValid(GravityActor))
					{
						NinjaCharMoveComp->SetCollisionGravityDirection(GravityActor);
					}

					break;
				}
			}

			// Launch walking Ninjas if configured
			if (!NinjaFallVelocity.IsZero() && NinjaCharMoveComp->IsWalking())
			{
				NinjaCharMoveComp->Launch(NinjaFallVelocity);
			}

			TrackedNinjas.Add(Ninja);
		}
		else
		{
			// Make sure the received Actor is valid and has physics
			UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Other->GetRootComponent());
			if (Primitive != nullptr && Primitive->IsAnySimulatingPhysics())
			{
				TrackedActors.Add(Other);
			}
		}
	}

	SetActorTickEnabled(TrackedActors.Num() > 0);
}

void ANinjaPhysicsVolume::ActorLeavingVolume(AActor* Other)
{
	Super::ActorLeavingVolume(Other);

	// Remove the received Actor from the TrackedActors list
	if (Other != nullptr)
	{
		ANinjaCharacter* Ninja = Cast<ANinjaCharacter>(Other);
		if (Ninja != nullptr)
		{
			TrackedNinjas.Remove(Ninja);
		}
		else
		{
			TrackedActors.Remove(Other);
		}
	}

	// Remove all empty positions of the TrackedActors list
	TrackedActors.RemoveAll([](AActor* Actor)
	{
		return Actor == nullptr;
	});

	// Remove all empty positions of the TrackedNinjas list
	TrackedNinjas.RemoveAll([](ANinjaCharacter* Ninja)
	{
		return Ninja == nullptr;
	});

	SetActorTickEnabled(TrackedActors.Num() > 0);
}

FVector ANinjaPhysicsVolume::GetGravity(USceneComponent* SceneComponent) const
{
	SCOPE_CYCLE_COUNTER(STAT_PhysicsVolumeGetGravity);

	if (GetGravityScale() == 0.0f)
	{
		return FVector::ZeroVector;
	}

	FVector Gravity = FVector::ZeroVector;

	switch (GravityDirectionMode)
	{
		case ENinjaGravityDirectionMode::Fixed:
		{
			Gravity = GravityVectorA * (FMath::Abs(GetGravityZ()) * GetFinalGravityScale(SceneComponent));
			break;
		}

		case ENinjaGravityDirectionMode::SplineTangent:
		{
			USplineComponent* Spline = nullptr;
			if (IsValid(GravityActor))
			{
				Spline = Cast<USplineComponent>(
					GravityActor->GetComponentByClass(USplineComponent::StaticClass()));
			}
			else
			{
				Spline = Cast<USplineComponent>(GetComponentByClass(USplineComponent::StaticClass()));
			}

			if (Spline != nullptr)
			{
				const FVector Point = SceneComponent->GetComponentLocation();
				const FVector GravityDir = Spline->FindDirectionClosestToWorldLocation(
					Point, ESplineCoordinateSpace::Type::World);
				if (!GravityDir.IsZero())
				{
					Gravity = GravityDir * (FMath::Abs(GetGravityZ()) * GetFinalGravityScale(SceneComponent));
				}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
				if (NinjaPhysicsVolumeCVars::ShowGravity > 0 && !GravityDir.IsZero())
				{
					DrawDebugDirectionalArrow(GetWorld(), Point, Point + GravityDir * 100.0f,
						1000.0f, FColor::Green, false, 0.02f, 0, 4.0f);
				}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			}

			break;
		}

		case ENinjaGravityDirectionMode::Point:
		{
			FVector GravityPoint = GravityVectorA;

			if (IsValid(GravityActor))
			{
				GravityPoint = GravityActor->GetActorLocation();
			}

			const FVector GravityDir = GravityPoint - SceneComponent->GetComponentLocation();
			if (!GravityDir.IsZero())
			{
				Gravity = GravityDir.GetSafeNormal() *
					(FMath::Abs(GetGravityZ()) * GetFinalGravityScale(SceneComponent));
			}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			if (NinjaPhysicsVolumeCVars::ShowGravity > 0)
			{
				DrawDebugSphere(GetWorld(), GravityPoint, 4.0, 8, FColor::Green, false, 0.02f, 0, 10.0f);
			}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

			break;
		}

		case ENinjaGravityDirectionMode::Line:
		{
			const FVector Point = SceneComponent->GetComponentLocation();
			const FVector GravityDir = FMath::ClosestPointOnInfiniteLine(
				GravityVectorA, GravityVectorB, Point) - Point;
			if (!GravityDir.IsZero())
			{
				Gravity = GravityDir.GetSafeNormal() *
					(FMath::Abs(GetGravityZ()) * GetFinalGravityScale(SceneComponent));
			}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			if (NinjaPhysicsVolumeCVars::ShowGravity > 0)
			{
				DrawDebugLine(GetWorld(), GravityVectorA + (GravityVectorA - GravityVectorB),
					GravityVectorB + (GravityVectorB - GravityVectorA), FColor::Green, false, 0.02f, 0, 4.0f);
				DrawDebugSphere(GetWorld(), GravityVectorA, 4.0, 8, FColor::Blue, false, 0.02f, 0, 10.0f);
				DrawDebugSphere(GetWorld(), GravityVectorB, 4.0, 8, FColor::Blue, false, 0.02f, 0, 10.0f);
			}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

			break;
		}

		case ENinjaGravityDirectionMode::Segment:
		{
			const FVector Point = SceneComponent->GetComponentLocation();
			const FVector GravityDir = FMath::ClosestPointOnLine(GravityVectorA,
				GravityVectorB, Point) - Point;
			if (!GravityDir.IsZero())
			{
				Gravity = GravityDir.GetSafeNormal() *
					(FMath::Abs(GetGravityZ()) * GetFinalGravityScale(SceneComponent));
			}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			if (NinjaPhysicsVolumeCVars::ShowGravity > 0)
			{
				DrawDebugLine(GetWorld(), GravityVectorA, GravityVectorB, FColor::Green, false, 0.02f, 0, 4.0f);
				DrawDebugSphere(GetWorld(), GravityVectorA, 4.0, 8, FColor::Blue, false, 0.02f, 0, 10.0f);
				DrawDebugSphere(GetWorld(), GravityVectorB, 4.0, 8, FColor::Blue, false, 0.02f, 0, 10.0f);
			}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

			break;
		}

		case ENinjaGravityDirectionMode::Spline:
		{
			USplineComponent* Spline = nullptr;
			if (IsValid(GravityActor))
			{
				Spline = Cast<USplineComponent>(
					GravityActor->GetComponentByClass(USplineComponent::StaticClass()));
			}
			else
			{
				Spline = Cast<USplineComponent>(GetComponentByClass(USplineComponent::StaticClass()));
			}

			if (Spline != nullptr)
			{
				const FVector Point = SceneComponent->GetComponentLocation();
				const FVector GravityPoint = Spline->FindLocationClosestToWorldLocation(
					Point, ESplineCoordinateSpace::Type::World);
				const FVector GravityDir = GravityPoint - Point;
				if (!GravityDir.IsZero())
				{
					Gravity = GravityDir.GetSafeNormal() *
						(FMath::Abs(GetGravityZ()) * GetFinalGravityScale(SceneComponent));
				}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
				if (NinjaPhysicsVolumeCVars::ShowGravity > 0)
				{
					DrawDebugSphere(GetWorld(), GravityPoint, 4.0, 8, FColor::Green, false, 0.02f, 0, 10.0f);
				}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			}

			break;
		}

		case ENinjaGravityDirectionMode::Plane:
		{
			const FVector Point = SceneComponent->GetComponentLocation();
			const FVector GravityDir = FVector::PointPlaneProject(Point,
				GravityVectorA, GravityVectorB) - Point;
			if (!GravityDir.IsZero())
			{
				Gravity = GravityDir.GetSafeNormal() *
					(FMath::Abs(GetGravityZ()) * GetFinalGravityScale(SceneComponent));
			}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			if (NinjaPhysicsVolumeCVars::ShowGravity > 0)
			{
				DrawDebugSolidPlane(GetWorld(), FPlane(GravityVectorA, GravityVectorB), GravityVectorA,
					FVector2D(500.0f, 500.0f), FColor::Green);
			}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

			break;
		}

		case ENinjaGravityDirectionMode::SplinePlane:
		{
			USplineComponent* Spline = nullptr;
			if (IsValid(GravityActor))
			{
				Spline = Cast<USplineComponent>(
					GravityActor->GetComponentByClass(USplineComponent::StaticClass()));
			}
			else
			{
				Spline = Cast<USplineComponent>(GetComponentByClass(USplineComponent::StaticClass()));
			}

			if (Spline != nullptr)
			{
				const FVector Point = SceneComponent->GetComponentLocation();

				const float InputKey = Spline->FindInputKeyClosestToWorldLocation(Point);
				const FVector ClosestLocation = Spline->GetLocationAtSplineInputKey(
					InputKey, ESplineCoordinateSpace::Type::World);
				const FVector ClosestUpVector = Spline->GetUpVectorAtSplineInputKey(
					InputKey, ESplineCoordinateSpace::Type::World);

				const FVector GravityDir = FVector::PointPlaneProject(Point,
					ClosestLocation, ClosestUpVector) - Point;
				if (!GravityDir.IsZero())
				{
					Gravity = GravityDir.GetSafeNormal() *
						(FMath::Abs(GetGravityZ()) * GetFinalGravityScale(SceneComponent));
				}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
				if (NinjaPhysicsVolumeCVars::ShowGravity > 0)
				{
					DrawDebugSolidPlane(GetWorld(), FPlane(ClosestLocation, ClosestUpVector),
						ClosestLocation, FVector2D(500.0f, 500.0f), FColor::Green);
				}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			}

			break;
		}

		case ENinjaGravityDirectionMode::Box:
		{
			FVector BoxOrigin = GravityVectorA;
			FVector BoxExtent = GravityVectorB;

			if (IsValid(GravityActor))
			{
				GravityActor->GetActorBounds(true, BoxOrigin, BoxExtent);
			}

			const FVector Point = SceneComponent->GetComponentLocation();
			const FVector GravityDir = FBox(BoxOrigin - BoxExtent,
				BoxOrigin + BoxExtent).GetClosestPointTo(Point) - Point;
			if (!GravityDir.IsZero())
			{
				Gravity = GravityDir.GetSafeNormal() *
					(FMath::Abs(GetGravityZ()) * GetFinalGravityScale(SceneComponent));
			}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			if (NinjaPhysicsVolumeCVars::ShowGravity > 0)
			{
				DrawDebugSolidBox(GetWorld(), BoxOrigin, BoxExtent, FColor::Green);
			}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

			break;
		}

		case ENinjaGravityDirectionMode::Collision:
		{
			if (IsValid(GravityActor))
			{
				const FVector Point = SceneComponent->GetComponentLocation();

				FVector ClosestPoint;
				if (Cast<UPrimitiveComponent>(GravityActor->GetRootComponent())->GetClosestPointOnCollision(
					Point, ClosestPoint) > 0.0f)
				{
					const FVector GravityDir = ClosestPoint - Point;
					if (!GravityDir.IsZero())
					{
						Gravity = GravityDir.GetSafeNormal() *
							(FMath::Abs(GetGravityZ()) * GetFinalGravityScale(SceneComponent));
					}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
					if (NinjaPhysicsVolumeCVars::ShowGravity > 0)
					{
						DrawDebugSphere(GetWorld(), ClosestPoint, 4.0, 8, FColor::Green, false, 0.02f, 0, 10.0f);
					}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
				}
			}

			break;
		}
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (NinjaPhysicsVolumeCVars::ShowGravity > 0 && !Gravity.IsZero())
	{
		const FVector Point = SceneComponent->GetComponentLocation();
		DrawDebugDirectionalArrow(GetWorld(), Point, Point + Gravity, 1000.0f,
			FColor::Red, false, 0.02f, 0, 7.0f);
	}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

	return Gravity;
}

FVector ANinjaPhysicsVolume::GetGravityDirection(USceneComponent* SceneComponent) const
{
	SCOPE_CYCLE_COUNTER(STAT_PhysicsVolumeGetGravityDirection);

	if (GetGravityScale() == 0.0f)
	{
		return FVector::ZeroVector;
	}

	FVector GravityDir = FVector::ZeroVector;

	switch (GravityDirectionMode)
	{
		case ENinjaGravityDirectionMode::Fixed:
		{
			GravityDir = GravityVectorA * ((GetFinalGravityScale(SceneComponent) > 0.0f) ? 1.0f : -1.0f);
			break;
		}

		case ENinjaGravityDirectionMode::SplineTangent:
		{
			USplineComponent* Spline = nullptr;
			if (IsValid(GravityActor))
			{
				Spline = Cast<USplineComponent>(
					GravityActor->GetComponentByClass(USplineComponent::StaticClass()));
			}
			else
			{
				Spline = Cast<USplineComponent>(GetComponentByClass(USplineComponent::StaticClass()));
			}

			if (Spline != nullptr)
			{
				GravityDir = Spline->FindDirectionClosestToWorldLocation(
					SceneComponent->GetComponentLocation(), ESplineCoordinateSpace::Type::World) *
					((GetFinalGravityScale(SceneComponent) > 0.0f) ? 1.0f : -1.0f);
			}

			break;
		}

		case ENinjaGravityDirectionMode::Point:
		{
			FVector GravityPoint = GravityVectorA;

			if (IsValid(GravityActor))
			{
				GravityPoint = GravityActor->GetActorLocation();
			}

			GravityDir = GravityPoint - SceneComponent->GetComponentLocation();
			if (!GravityDir.IsZero())
			{
				GravityDir = GravityDir.GetSafeNormal() *
					((GetFinalGravityScale(SceneComponent) > 0.0f) ? 1.0f : -1.0f);
			}

			break;
		}

		case ENinjaGravityDirectionMode::Line:
		{
			const FVector Point = SceneComponent->GetComponentLocation();
			GravityDir = FMath::ClosestPointOnInfiniteLine(
				GravityVectorA, GravityVectorB, Point) - Point;
			if (!GravityDir.IsZero())
			{
				GravityDir = GravityDir.GetSafeNormal() *
					((GetFinalGravityScale(SceneComponent) > 0.0f) ? 1.0f : -1.0f);
			}

			break;
		}

		case ENinjaGravityDirectionMode::Segment:
		{
			const FVector Point = SceneComponent->GetComponentLocation();
			GravityDir = FMath::ClosestPointOnLine(GravityVectorA,
				GravityVectorB, Point) - Point;
			if (!GravityDir.IsZero())
			{
				GravityDir = GravityDir.GetSafeNormal() *
					((GetFinalGravityScale(SceneComponent) > 0.0f) ? 1.0f : -1.0f);
			}

			break;
		}

		case ENinjaGravityDirectionMode::Spline:
		{
			USplineComponent* Spline = nullptr;
			if (IsValid(GravityActor))
			{
				Spline = Cast<USplineComponent>(
					GravityActor->GetComponentByClass(USplineComponent::StaticClass()));
			}
			else
			{
				Spline = Cast<USplineComponent>(GetComponentByClass(USplineComponent::StaticClass()));
			}

			if (Spline != nullptr)
			{
				const FVector Point = SceneComponent->GetComponentLocation();
				const FVector GravityPoint = Spline->FindLocationClosestToWorldLocation(
					Point, ESplineCoordinateSpace::Type::World);
				GravityDir = GravityPoint - Point;
				if (!GravityDir.IsZero())
				{
					GravityDir = GravityDir.GetSafeNormal() *
						((GetFinalGravityScale(SceneComponent) > 0.0f) ? 1.0f : -1.0f);
				}
			}

			break;
		}

		case ENinjaGravityDirectionMode::Plane:
		{
			const FVector Point = SceneComponent->GetComponentLocation();
			GravityDir = FVector::PointPlaneProject(Point, GravityVectorA,
				GravityVectorB) - Point;
			if (!GravityDir.IsZero())
			{
				GravityDir = GravityDir.GetSafeNormal() *
					((GetFinalGravityScale(SceneComponent) > 0.0f) ? 1.0f : -1.0f);
			}

			break;
		}

		case ENinjaGravityDirectionMode::SplinePlane:
		{
			USplineComponent* Spline = nullptr;
			if (IsValid(GravityActor))
			{
				Spline = Cast<USplineComponent>(
					GravityActor->GetComponentByClass(USplineComponent::StaticClass()));
			}
			else
			{
				Spline = Cast<USplineComponent>(GetComponentByClass(USplineComponent::StaticClass()));
			}

			if (Spline != nullptr)
			{
				const FVector Point = SceneComponent->GetComponentLocation();

				const float InputKey = Spline->FindInputKeyClosestToWorldLocation(Point);
				const FVector ClosestLocation = Spline->GetLocationAtSplineInputKey(
					InputKey, ESplineCoordinateSpace::Type::World);
				const FVector ClosestUpVector = Spline->GetUpVectorAtSplineInputKey(
					InputKey, ESplineCoordinateSpace::Type::World);

				GravityDir = FVector::PointPlaneProject(Point,
					ClosestLocation, ClosestUpVector) - Point;
				if (!GravityDir.IsZero())
				{
					GravityDir = GravityDir.GetSafeNormal() *
						((GetFinalGravityScale(SceneComponent) > 0.0f) ? 1.0f : -1.0f);
				}
			}

			break;
		}

		case ENinjaGravityDirectionMode::Box:
		{
			FVector BoxOrigin = GravityVectorA;
			FVector BoxExtent = GravityVectorB;

			if (IsValid(GravityActor))
			{
				GravityActor->GetActorBounds(true, BoxOrigin, BoxExtent);
			}

			const FVector Point = SceneComponent->GetComponentLocation();
			GravityDir = FBox(BoxOrigin - BoxExtent,
				BoxOrigin + BoxExtent).GetClosestPointTo(Point) - Point;
			if (!GravityDir.IsZero())
			{
				GravityDir = GravityDir.GetSafeNormal() *
					((GetFinalGravityScale(SceneComponent) > 0.0f) ? 1.0f : -1.0f);
			}

			break;
		}

		case ENinjaGravityDirectionMode::Collision:
		{
			if (IsValid(GravityActor))
			{
				const FVector Point = SceneComponent->GetComponentLocation();

				FVector ClosestPoint;
				if (Cast<UPrimitiveComponent>(GravityActor->GetRootComponent())->GetClosestPointOnCollision(
					Point, ClosestPoint) > 0.0f)
				{
					GravityDir = ClosestPoint - Point;
					if (!GravityDir.IsZero())
					{
						GravityDir = GravityDir.GetSafeNormal() *
							((GetFinalGravityScale(SceneComponent) > 0.0f) ? 1.0f : -1.0f);
					}
				}
			}

			break;
		}
	}

	return GravityDir;
}

float ANinjaPhysicsVolume::GetGravityMagnitude(USceneComponent* SceneComponent) const
{
	return FMath::Abs(GetGravityZ() * GetFinalGravityScale(SceneComponent));
}

float ANinjaPhysicsVolume::GetFinalGravityScale(USceneComponent* SceneComponent) const
{
	return GetGravityScale();
}

void ANinjaPhysicsVolume::K2_SetFixedGravityDirection(const FVector& NewGravityDirection)
{
	SetFixedGravityDirection(NewGravityDirection.GetSafeNormal());
}

void ANinjaPhysicsVolume::SetFixedGravityDirection(const FVector& NewFixedGravityDirection)
{
	if (NewFixedGravityDirection.IsZero() ||
		(GravityDirectionMode == ENinjaGravityDirectionMode::Fixed &&
		GravityVectorA == NewFixedGravityDirection))
	{
		return;
	}

	GravityDirectionMode = ENinjaGravityDirectionMode::Fixed;
	GravityVectorA = NewFixedGravityDirection;

	// Change gravity settings of Ninjas
	for (ANinjaCharacter* Ninja : TrackedNinjas)
	{
		if (IsValid(Ninja))
		{
			Ninja->GetNinjaCharacterMovement()->SetFixedGravityDirection(NewFixedGravityDirection);
		}
	}
}

void ANinjaPhysicsVolume::SetSplineTangentGravityDirection(AActor* NewGravityActor)
{
	if (NewGravityActor == nullptr ||
		(GravityDirectionMode == ENinjaGravityDirectionMode::SplineTangent &&
		GravityActor == NewGravityActor))
	{
		return;
	}

	const USplineComponent* Spline = Cast<USplineComponent>(
		NewGravityActor->GetComponentByClass(USplineComponent::StaticClass()));
	if (Spline != nullptr)
	{
		GravityDirectionMode = ENinjaGravityDirectionMode::SplineTangent;
		GravityActor = NewGravityActor;

		// Change gravity settings of Ninjas
		for (ANinjaCharacter* Ninja : TrackedNinjas)
		{
			if (IsValid(Ninja))
			{
				Ninja->GetNinjaCharacterMovement()->SetSplineTangentGravityDirection(NewGravityActor);
			}
		}
	}
}

void ANinjaPhysicsVolume::SetPointGravityDirection(const FVector& NewGravityPoint)
{
	if (GravityDirectionMode == ENinjaGravityDirectionMode::Point &&
		GravityVectorA == NewGravityPoint)
	{
		return;
	}

	GravityDirectionMode = ENinjaGravityDirectionMode::Point;
	GravityVectorA = NewGravityPoint;
	GravityActor = nullptr;

	// Change gravity settings of Ninjas
	for (ANinjaCharacter* Ninja : TrackedNinjas)
	{
		if (IsValid(Ninja))
		{
			Ninja->GetNinjaCharacterMovement()->SetPointGravityDirection(NewGravityPoint);
		}
	}
}

void ANinjaPhysicsVolume::SetPointGravityDirectionFromActor(AActor* NewGravityActor)
{
	if (GravityDirectionMode == ENinjaGravityDirectionMode::Point &&
		GravityActor == NewGravityActor)
	{
		return;
	}

	GravityDirectionMode = ENinjaGravityDirectionMode::Point;
	GravityActor = NewGravityActor;

	// Change gravity settings of Ninjas
	for (ANinjaCharacter* Ninja : TrackedNinjas)
	{
		if (IsValid(Ninja))
		{
			Ninja->GetNinjaCharacterMovement()->SetPointGravityDirectionFromActor(NewGravityActor);
		}
	}
}

void ANinjaPhysicsVolume::SetLineGravityDirection(const FVector& NewGravityLineStart, const FVector& NewGravityLineEnd)
{
	if (NewGravityLineStart == NewGravityLineEnd ||
		(GravityDirectionMode == ENinjaGravityDirectionMode::Line &&
		GravityVectorA == NewGravityLineStart && GravityVectorB == NewGravityLineEnd))
	{
		return;
	}

	GravityDirectionMode = ENinjaGravityDirectionMode::Line;
	GravityVectorA = NewGravityLineStart;
	GravityVectorB = NewGravityLineEnd;

	// Change gravity settings of Ninjas
	for (ANinjaCharacter* Ninja : TrackedNinjas)
	{
		if (IsValid(Ninja))
		{
			Ninja->GetNinjaCharacterMovement()->SetLineGravityDirection(
				NewGravityLineStart, NewGravityLineEnd);
		}
	}
}

void ANinjaPhysicsVolume::SetSegmentGravityDirection(const FVector& NewGravitySegmentStart, const FVector& NewGravitySegmentEnd)
{
	if (NewGravitySegmentStart == NewGravitySegmentEnd ||
		(GravityDirectionMode == ENinjaGravityDirectionMode::Segment &&
		GravityVectorA == NewGravitySegmentStart && GravityVectorB == NewGravitySegmentEnd))
	{
		return;
	}

	GravityDirectionMode = ENinjaGravityDirectionMode::Segment;
	GravityVectorA = NewGravitySegmentStart;
	GravityVectorB = NewGravitySegmentEnd;

	// Change gravity settings of Ninjas
	for (ANinjaCharacter* Ninja : TrackedNinjas)
	{
		if (IsValid(Ninja))
		{
			Ninja->GetNinjaCharacterMovement()->SetSegmentGravityDirection(
				NewGravitySegmentStart, NewGravitySegmentEnd);
		}
	}
}

void ANinjaPhysicsVolume::SetSplineGravityDirection(AActor* NewGravityActor)
{
	if (NewGravityActor == nullptr ||
		(GravityDirectionMode == ENinjaGravityDirectionMode::Spline &&
		GravityActor == NewGravityActor))
	{
		return;
	}

	const USplineComponent* Spline = Cast<USplineComponent>(
		NewGravityActor->GetComponentByClass(USplineComponent::StaticClass()));
	if (Spline != nullptr)
	{
		GravityDirectionMode = ENinjaGravityDirectionMode::Spline;
		GravityActor = NewGravityActor;

		// Change gravity settings of Ninjas
		for (ANinjaCharacter* Ninja : TrackedNinjas)
		{
			if (IsValid(Ninja))
			{
				Ninja->GetNinjaCharacterMovement()->SetSplineGravityDirection(NewGravityActor);
			}
		}
	}
}

void ANinjaPhysicsVolume::K2_SetPlaneGravityDirection(const FVector& NewGravityPlaneBase, const FVector& NewGravityPlaneNormal)
{
	SetPlaneGravityDirection(NewGravityPlaneBase, NewGravityPlaneNormal.GetSafeNormal());
}

void ANinjaPhysicsVolume::SetPlaneGravityDirection(const FVector& NewGravityPlaneBase, const FVector& NewGravityPlaneNormal)
{
	if (NewGravityPlaneNormal.IsZero() ||
		(GravityDirectionMode == ENinjaGravityDirectionMode::Plane &&
		GravityVectorA == NewGravityPlaneBase && GravityVectorB == NewGravityPlaneNormal))
	{
		return;
	}

	GravityDirectionMode = ENinjaGravityDirectionMode::Plane;
	GravityVectorA = NewGravityPlaneBase;
	GravityVectorB = NewGravityPlaneNormal;

	// Change gravity settings of Ninjas
	for (ANinjaCharacter* Ninja : TrackedNinjas)
	{
		if (IsValid(Ninja))
		{
			Ninja->GetNinjaCharacterMovement()->SetPlaneGravityDirection(
				NewGravityPlaneBase, NewGravityPlaneNormal);
		}
	}
}

void ANinjaPhysicsVolume::SetSplinePlaneGravityDirection(AActor* NewGravityActor)
{
	if (NewGravityActor == nullptr ||
		(GravityDirectionMode == ENinjaGravityDirectionMode::SplinePlane &&
		GravityActor == NewGravityActor))
	{
		return;
	}

	const USplineComponent* Spline = Cast<USplineComponent>(
		NewGravityActor->GetComponentByClass(USplineComponent::StaticClass()));
	if (Spline != nullptr)
	{
		GravityDirectionMode = ENinjaGravityDirectionMode::SplinePlane;
		GravityActor = NewGravityActor;

		// Change gravity settings of Ninjas
		for (ANinjaCharacter* Ninja : TrackedNinjas)
		{
			if (IsValid(Ninja))
			{
				Ninja->GetNinjaCharacterMovement()->SetSplinePlaneGravityDirection(NewGravityActor);
			}
		}
	}
}

void ANinjaPhysicsVolume::SetBoxGravityDirection(const FVector& NewGravityBoxOrigin, const FVector& NewGravityBoxExtent)
{
	if (GravityDirectionMode == ENinjaGravityDirectionMode::Box &&
		GravityVectorA == NewGravityBoxOrigin && GravityVectorB == NewGravityBoxExtent)
	{
		return;
	}

	GravityDirectionMode = ENinjaGravityDirectionMode::Box;
	GravityVectorA = NewGravityBoxOrigin;
	GravityVectorB = NewGravityBoxExtent;
	GravityActor = nullptr;

	// Change gravity settings of Ninjas
	for (ANinjaCharacter* Ninja : TrackedNinjas)
	{
		if (IsValid(Ninja))
		{
			Ninja->GetNinjaCharacterMovement()->SetBoxGravityDirection(
				NewGravityBoxOrigin, NewGravityBoxExtent);
		}
	}
}

void ANinjaPhysicsVolume::SetBoxGravityDirectionFromActor(AActor* NewGravityActor)
{
	if (GravityDirectionMode == ENinjaGravityDirectionMode::Box &&
		GravityActor == NewGravityActor)
	{
		return;
	}

	GravityDirectionMode = ENinjaGravityDirectionMode::Box;
	GravityActor = NewGravityActor;

	// Change gravity settings of Ninjas
	for (ANinjaCharacter* Ninja : TrackedNinjas)
	{
		if (IsValid(Ninja))
		{
			Ninja->GetNinjaCharacterMovement()->SetBoxGravityDirectionFromActor(NewGravityActor);
		}
	}
}

void ANinjaPhysicsVolume::SetCollisionGravityDirection(AActor* NewGravityActor)
{
	if (NewGravityActor == nullptr ||
		(GravityDirectionMode == ENinjaGravityDirectionMode::Collision &&
		GravityActor == NewGravityActor))
	{
		return;
	}

	const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(NewGravityActor->GetRootComponent());
	if (Primitive != nullptr)
	{
		GravityDirectionMode = ENinjaGravityDirectionMode::Collision;
		GravityActor = NewGravityActor;

		// Change gravity settings of Ninjas
		for (ANinjaCharacter* Ninja : TrackedNinjas)
		{
			if (IsValid(Ninja))
			{
				Ninja->GetNinjaCharacterMovement()->SetCollisionGravityDirection(NewGravityActor);
			}
		}
	}
}

float ANinjaPhysicsVolume::GetGravityScale() const
{
	return GravityScale;
}

void ANinjaPhysicsVolume::SetGravityScale(float NewGravityScale)
{
	GravityScale = NewGravityScale;
}
