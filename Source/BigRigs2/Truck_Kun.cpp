// Fill out your copyright notice in the Description page of Project Settings.



#include "Truck_Kun.h"
#include "Checkpoint.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "NinjaCharacter/Public/NinjaCharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Components/AudioComponent.h"

// Sets default values
ATruck_Kun::ATruck_Kun(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UNinjaCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	bUseControllerRotationYaw = true;

	//Ninja Component Thanks To Javier Osset (Xaklse)
	UNinjaCharacterMovementComponent* CharMovement = Cast<UNinjaCharacterMovementComponent>(GetCharacterMovement());
	if(CharMovement)
	{
		CharMovement->bLandOnAnySurface = true;
		CharMovement->bAlignComponentToFloor = true;
		CharMovement->bAlignComponentToGravity = true;
		CharMovement->bAlwaysRotateAroundCenter = true;
	}
	
	//Camera Fixing
	FrontCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("Front Camera"));
	FrontCamera->SetupAttachment(GetMesh());
	FrontCamera->bAutoActivate = false;

	BackCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("Back Camera"));
	BackCamera->SetupAttachment(GetMesh());

	FrontCamera->bUsePawnControlRotation = false;
	BackCamera->bUsePawnControlRotation = false;

	GetCapsuleComponent()->OnComponentBeginOverlap.AddDynamic(this, &ATruck_Kun::OnOverlapBegin);

	//Truck Engine
	EngineSFX = CreateDefaultSubobject<UAudioComponent>(TEXT("EngineSound"));
	EngineSFX->SetupAttachment(GetCapsuleComponent());

	GetMesh()->SetMassScale(NAME_None,999.f);
}

// Called when the game starts or when spawned
void ATruck_Kun::BeginPlay()
{
	Super::BeginPlay();
	timeStarted = true;

	APlayerController* PC = Cast<APlayerController>(GetController());
	if(PC) UWidgetBlueprintLibrary::SetInputMode_GameOnly(PC,false);
}

void ATruck_Kun::ReOrientCamera()
{
	CameraReOrienting = true;
}

void ATruck_Kun::UnReOrientCamera()
{
	CameraReOrienting = false;
}

// Called every frame
void ATruck_Kun::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	//DEBUG
	//GEngine->AddOnScreenDebugMessage(-1,0,Drifting ? FColor::Green : FColor::Red,"Drifting?");

	if(timeStarted) timer += (DeltaTime*1000);
	
	//Forward Driving Button Holding
	if(!(HoldingForward && HoldingBackward) && !Drifting)
	{
		if(HoldingForward)
		{
			TimeHoldingForward += DeltaTime;
			TimeHoldingBackward -= DeltaTime;
		}
		else TimeHoldingForward -= (DeltaTime/2);

		//Backward Driving Button Holding
		if(HoldingBackward)
		{
			TimeHoldingBackward += DeltaTime;
			TimeHoldingForward -= DeltaTime;
		}
		else TimeHoldingBackward -= (DeltaTime/2);
	}

	if(Drifting && ForwardSpeed == 0)
	{
		TimeHoldingBackward -= DeltaTime * 1.5;
		TimeHoldingForward -= DeltaTime * 1.5;
	}
	if(Drifting)
	{
		TimeHoldingBackward -= DeltaTime * 0.45;
		TimeHoldingForward -= DeltaTime * 0.45;
	}

	TimeHoldingForward = FMath::Clamp(TimeHoldingForward,0,MaxHoldingTime);
	TimeHoldingBackward = FMath::Clamp(TimeHoldingBackward,0,MaxHoldingTime);

	//Engine Sounds
	EngineSFX->SetPitchMultiplier((((TimeHoldingForward/MaxHoldingTime)*1) - ((TimeHoldingBackward/MaxHoldingTime)*0.25)) + 1);
	GEngine->AddOnScreenDebugMessage(-1,-1,FColor::Orange,"Engine Pitch: " + FString::SanitizeFloat(EngineSFX->PitchMultiplier));

	//Forward Driving
	if(TimeHoldingForward > 0)
	{
		float FSpeed = ForwardSpeedCurve->GetFloatValue(TimeHoldingForward);
		GetCharacterMovement()->MaxWalkSpeed = FSpeed * TruckBaseForwardSpeed;
		if(Drifting)
		{
			if(TurnRot > 0)
			{
				//Turning Right
				AddMovementInput(GetActorForwardVector() + (GetActorRightVector()*0.15),FSpeed);
			}
			else if(TurnRot < 0)
			{
				//Turning Left
				AddMovementInput(GetActorForwardVector() + -(GetActorRightVector()*0.15),FSpeed);
			}
		}
		else
		{
			AddMovementInput(GetActorForwardVector(),FSpeed);
		}
	}
	else if(TimeHoldingBackward > 0)
	{
		float BSpeed = BackwardSpeedCurve->GetFloatValue(TimeHoldingBackward);
		GetCharacterMovement()->MaxWalkSpeed = BSpeed * TruckBaseBackwardSpeed;
		AddMovementInput(GetActorForwardVector(),-BSpeed);
	}

	//VFX
	//Drifting VFX
	if(Drifting && TimeHoldingForward > (MaxHoldingTime*0.45)) onStartDrifting();
	else onEndDrifting();

	//Camera Realignment
	if(CameraReOrienting)
	{
		float CameraYaw = FrontCamera->GetRelativeRotation().Yaw;
		CameraYaw = FMath::FInterpTo(CameraYaw, 0.0f, DeltaTime, 1.0f);

		float CameraPitch = FrontCamera->GetRelativeRotation().Pitch;
		CameraPitch = FMath::FInterpTo(CameraPitch, 0.0f, DeltaTime, 1.0f);
	
		FrontCamera->SetRelativeRotation(FRotator(CameraPitch, CameraYaw, 0.0f));
	}

	FrontCamera->SetRelativeRotation(FRotator(
		FrontCamera->GetRelativeRotation().Pitch,
		FrontCamera->GetRelativeRotation().Yaw, 0));

	BackCamera->SetWorldRotation(FRotator(0,GetActorRotation().Yaw,0));
}

// Called to bind functionality to input
void ATruck_Kun::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	check(PlayerInputComponent);
	
	PlayerInputComponent->BindAxis("MoveForward",this,&ATruck_Kun::ForwardInput);
	PlayerInputComponent->BindAxis("MoveRight",this,&ATruck_Kun::RightInput);

	PlayerInputComponent->BindAction("Handbrake",IE_Pressed,this,&ATruck_Kun::startDrift);
	PlayerInputComponent->BindAction("Handbrake",IE_Released,this,&ATruck_Kun::endDrift);

	PlayerInputComponent->BindAction("ReOrientCamera",IE_Pressed,this,&ATruck_Kun::ReOrientCamera);
	PlayerInputComponent->BindAction("ReOrientCamera",IE_Released,this,&ATruck_Kun::UnReOrientCamera);

	PlayerInputComponent->BindAction("ChangeCamera",IE_Released,this,&ATruck_Kun::ToggleCamera);

	PlayerInputComponent->BindAxis("LookRight", this, &ATruck_Kun::CameraYaw);
	PlayerInputComponent->BindAxis("LookUp", this, &ATruck_Kun::CameraPitch);
}

void ATruck_Kun::ForwardInput(float Val)
{
	ForwardSpeed = Val;
	HoldingForward = Val > 0;
	HoldingBackward = Val < 0;
}

void ATruck_Kun::RightInput(float Val)
{
	TurnRot = Val;
	if (Val != 0.0f && GetController() && GetVelocity().Length() > 0)
	{
		FRotator CRotaion = GetActorRotation();
		float TurnSpeed = (TimeHoldingForward/MaxHoldingTime) > (TimeHoldingBackward/MaxHoldingTime) ?
			(TimeHoldingForward/MaxHoldingTime) : (TimeHoldingBackward/MaxHoldingTime);

		//Turn Negative
		TurnSpeed = FMath::Clamp(TurnSpeed,0.3,1);

		if(Drifting && TimeHoldingForward > (MaxHoldingTime*0.45) && ForwardSpeed == 0) TurnSpeed *= 5;
		else if(Drifting && TimeHoldingForward > (MaxHoldingTime*0.45)) TurnSpeed *= 2;

if(TimeHoldingForward < TimeHoldingBackward) TurnSpeed *= 1;
		
		CRotaion.Yaw += Val * (TurnAngleMultiplier * (TurnSpeed));
		SetActorRotation(CRotaion);
	}
	TurningValue = Val;
}

void ATruck_Kun::startDrift()
{
	Drifting = true;
	//onStartDrifting();
}

void ATruck_Kun::endDrift()
{
	Drifting = false;
	//onEndDrifting();
}

void ATruck_Kun::CameraYaw(float _yaw)
{
	AddControllerYawInput(_yaw);
	if(FrontCamera->IsActive()) FrontCamera->AddLocalRotation(FRotator(0.0f, _yaw, 0.0f));
}

void ATruck_Kun::CameraPitch(float _pitch)
{
	AddControllerPitchInput(-_pitch);
	if(FrontCamera->IsActive()) FrontCamera->AddLocalRotation(FRotator(_pitch, 0.0f, 0.0f));

	//Clamp Rotation
	float P = FrontCamera->GetRelativeRotation().Pitch;
	P = FMath::ClampAngle(P,-80,80);
	FrontCamera->SetRelativeRotation(FRotator(P, FrontCamera->GetRelativeRotation().Yaw, 0.f));
}

void ATruck_Kun::ToggleCamera()
{
	// toggle the active camera flag
	bFrontCameraActive = !bFrontCameraActive;

	FrontCamera->SetActive(false);
	BackCamera->SetActive(true);
}

void ATruck_Kun::OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	//Check Collisions
	if(ACheckpoint* Checkpoint = Cast<ACheckpoint>(OtherActor))
	{
		if(Checkpoint->isEndGoal)
		{
			//Final Goal
			if(Checkpoint->FinalCheckpoint == (CheckpointNumber+1))
			{
				//End Map
				timeStarted = false;
			}
		}
		else
		{
			//Regular Checkpoint
			if(Checkpoint->ThisCheckpointNumber == (CheckpointNumber+1))
			{
				CheckpointNumber = Checkpoint->ThisCheckpointNumber;
				
				TArray<AActor*> Checkpoints;
				UGameplayStatics::GetAllActorsOfClass(GetWorld(), ACheckpoint::StaticClass(), Checkpoints);
				for(AActor* CheckpointActor : Checkpoints)
				{
					ACheckpoint* CP = Cast<ACheckpoint>(CheckpointActor);
					if(CP && IsLocallyControlled()) CP->onCheckpoint(CheckpointNumber + 1);
				}
			}
		}
	}
}

AActor* ATruck_Kun::getNextCheckpoint()
{
	TArray<AActor*> Checkpoints;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ACheckpoint::StaticClass(), Checkpoints);
	for(AActor* CheckpointActor : Checkpoints)
	{
		ACheckpoint* CP = Cast<ACheckpoint>(CheckpointActor);
		if(IsValid(CP))
		{
			if(CP->ThisCheckpointNumber == (CheckpointNumber + 1) ||
				(CP->isEndGoal && CP->FinalCheckpoint == (CheckpointNumber + 1)))
			{
				GEngine->AddOnScreenDebugMessage(-1,0,FColor::Cyan,
					"CP: " + FString::SanitizeFloat(CheckpointNumber) + " ~ " +
					CP->GetActorNameOrLabel());
				return CP;
			}
		}
	}
	return nullptr;
}