#include "UbiTest/CollisionAnticipationSpringArm.h"
#include "GameFramework/Pawn.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "Math/RotationMatrix.h"
#include "PhysicsEngine/PhysicsSettings.h"


const FName UCollisionAnticipationSpringArm::SocketName(TEXT("SpringEndpoint"));

// Sets default values for this component's properties
UCollisionAnticipationSpringArm::UCollisionAnticipationSpringArm()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;

	bAutoActivate = true;
	bTickInEditor = true;
	bUsePawnControlRotation = false;
	bDoCollisionTest = true;

	bInheritPitch = true;
	bInheritYaw = true;
	bInheritRoll = true;

	TargetArmLength = 300.0f;
	TraceChannel = ECC_Camera;

	RelativeSocketRotation = FQuat::Identity;

	ReturnTimer = 0;
}

void UCollisionAnticipationSpringArm::BeginPlay()
{
	Super::BeginPlay();

	//AttachedCamera = Cast<UCameraComponent>(GetChildComponent(0));
}

void UCollisionAnticipationSpringArm::OnRegister()
{
	Super::OnRegister();

	// Set initial location.
	UpdateDesiredArmLocation(false, false, 0.f);
}

void UCollisionAnticipationSpringArm::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateDesiredArmLocation(bDoCollisionTest, bDoCollisionPrediction, DeltaTime);

#if WITH_EDITOR
	if (GetWorld() && !GetWorld()->IsGameWorld())
	{
		ShowPreviewLines();
	}
#endif
}


FRotator UCollisionAnticipationSpringArm::GetDesiredRotation() const
{
	return GetComponentRotation();
}

FRotator UCollisionAnticipationSpringArm::GetTargetRotation() const
{
	FRotator DesiredRot = GetDesiredRotation();

	if (bUsePawnControlRotation)
	{
		if (APawn* OwningPawn = Cast<APawn>(GetOwner()))
		{
			const FRotator PawnViewRotation = OwningPawn->GetViewRotation();
			if (DesiredRot != PawnViewRotation)
			{
				DesiredRot = PawnViewRotation;
			}
		}
	}

	// If inheriting rotation, check options for which components to inherit
	if (!IsUsingAbsoluteRotation())
	{
		const FRotator LocalRelativeRotation = GetRelativeRotation();
		if (!bInheritPitch)
		{
			DesiredRot.Pitch = LocalRelativeRotation.Pitch;
		}

		if (!bInheritYaw)
		{
			DesiredRot.Yaw = LocalRelativeRotation.Yaw;
		}

		if (!bInheritRoll)
		{
			DesiredRot.Roll = LocalRelativeRotation.Roll;
		}
	}

	return DesiredRot;
}

void UCollisionAnticipationSpringArm::UpdateDesiredArmLocation(bool bDoCollision, bool bPredictCollisions, float DeltaTime)
{
	// If our viewtarget is simulating using physics, we may need to clamp deltatime
	if (bClampToMaxPhysicsDeltaTime)
	{
		// Use the same max timestep cap as the physics system to avoid camera jitter when the viewtarget simulates less time than the camera
		DeltaTime = FMath::Min(DeltaTime, UPhysicsSettings::Get()->MaxPhysicsDeltaTime);
	}

	FRotator DesiredRot = GetTargetRotation();
	FVector DesiredRotForward = DesiredRot.Vector();

	//smoothly move the camera in or out of its offset 
	FVector DesiredOffset = bIsOffset ? SocketOffset : FVector::Zero();
	FVector ResultOffset = DesiredOffset;
	if (!PreviousOffset.Equals(DesiredOffset))
	{
		ResultOffset = FMath::VInterpTo(PreviousOffset, DesiredOffset, DeltaTime, 1);
	}
	PreviousOffset = ResultOffset;

	// Get the spring arm 'origin', the target we want to look at (without offset)
	FVector ArmOrigin = GetComponentLocation();
	// get the desired location of the camera without collisions
	FVector DesiredLoc = ArmOrigin - DesiredRotForward * TargetArmLength;
	// Add socket offset in local space
	DesiredLoc += FRotationMatrix(DesiredRot).TransformVector(ResultOffset);
	//the new length of the arm with added offset
	float OffsetArmLength = (DesiredLoc - ArmOrigin).Length();
	//the forward from the end of the spring arm with offset to the spring arm origin, if there is an offset this is different from the camera forward
	FVector OffsetArmForward = (ArmOrigin - DesiredLoc).GetSafeNormal();
	FRotator OffsetRot = OffsetArmForward.Rotation();

	// the final position of the camera that we will calculate below
	FVector ResultLoc;
	// the final distance moved forward from where the camera should be without any collisions
	float ResultForwardMovement = 0;

	ResultLoc = DesiredLoc;

	// Do collision prediction first
	if (bPredictCollisions && (OffsetArmLength != 0.0f))
	{
		FCollisionPredictionResult PredictionResults;

		//first check if the camera is moving left or right with a dot product of the camera movement vector and its right vector in 2D
		FVector LastCameraMovement = PreviousDesiredLoc - DesiredLoc;
		FVector2D LastCamMovement2D(LastCameraMovement.X, LastCameraMovement.Y);
		FVector CameraRightVector = FRotationMatrix(DesiredRot).GetUnitAxis(EAxis::Y);
		FVector2D CamRightVector2D(CameraRightVector.X, CameraRightVector.Y);//camera has no roll, so right vector will never have a Z and we can just convert it to 2D like this and keep it normalized
		
		LastCamMovement2D.Normalize();

		float DotProd = FVector2D::DotProduct(CamRightVector2D, LastCamMovement2D);
		FString debugText = FString::Printf(TEXT("%d"), bIsOffset);

		//Horizontal Collision Prediction
		if (!FMath::IsNearlyZero(DotProd))
		{
			//check collisions to the left
			if (DotProd > 0.0f)
			{
				CheckSurroundingWallsCollisions(PredictionResults, OffsetRot, OffsetArmLength, PredictionStartAngle, PredictionEndAngle, TracesPerSide);
			}
			else//or to the right
			{
				CheckSurroundingWallsCollisions(PredictionResults, OffsetRot, OffsetArmLength, -PredictionStartAngle, -PredictionEndAngle, TracesPerSide);
			}
		}

		float moveSpeed = CorrectionSpeedForward;
		if (bUseSpeedCurve && IsValid(SpeedCurve))
		{
			moveSpeed *= SpeedCurve->GetFloatValue(PredictionResults.CorrectionStrength);
		}

		float PositionFixDistance = 0;

		//if the camera wants to go back because it has space behind, run a small timer before letting it to avoid weird back and forth
		if (PredictionResults.PredictedMoveDistance <= PreviousForwardMovement)
		{
			//decided to move the camera at a different and slower speed when going back compared to going forward
			moveSpeed = CorrectionSpeedBack;
			if (ReturnTimer < ReturnDelay)
			{
				PredictionResults.PredictedMoveDistance = PreviousForwardMovement;// block position to previous one until timer runs out
				ReturnTimer += DeltaTime;
			}
		}
		else
		{
			ReturnTimer = 0;
		}

		//interpolate the forward movement of the camera to avoid walls smoothly
		ResultForwardMovement = FMath::FInterpTo(PreviousForwardMovement, PredictionResults.PredictedMoveDistance, DeltaTime, moveSpeed);
		ResultLoc = DesiredLoc + OffsetArmForward * ResultForwardMovement;
	}

	//we can do a plain old collision detection, this "wins" over the prediction position if the smooth movement is not enough to get us in front of a wall, so we don't see in the walls
	if (bDoCollision)
	{
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SpringArm), false, GetOwner());
		FHitResult Result;
		GetWorld()->SweepSingleByChannel(Result, ArmOrigin, ResultLoc, FQuat::Identity, TraceChannel, FCollisionShape::MakeSphere(SphereTraceSize), QueryParams);

		if (Result.bBlockingHit)
		{
			float BaseCollisionMoveDistance = OffsetArmLength - Result.Distance;
			if (BaseCollisionMoveDistance > ResultForwardMovement)
			{
				ResultForwardMovement = BaseCollisionMoveDistance;
			}
		}

		ResultLoc = DesiredLoc + OffsetArmForward * ResultForwardMovement;
	}

	PreviousForwardMovement = ResultForwardMovement;
	PreviousDesiredLoc = DesiredLoc;

	// Form a transform for new world transform for camera
	FTransform WorldCamTM(DesiredRot, ResultLoc);
	// Convert to relative to component
	FTransform RelCamTM = WorldCamTM.GetRelativeTransform(GetComponentTransform());

	// Update socket location/rotation
	RelativeSocketLocation = RelCamTM.GetLocation();
	RelativeSocketRotation = RelCamTM.GetRotation();

	UpdateChildTransforms();
}

bool UCollisionAnticipationSpringArm::CheckSurroundingWallsCollisions(FCollisionPredictionResult& OutResult, const FRotator& CameraRotation, float ArmLength, float StartAngle, float EndAngle, int TraceCount)
{
	OutResult.bHitSomething = false;
	OutResult.CorrectionStrength = 0;
	OutResult.PredictedMoveDistance = 0;
	float FinalMoveDistance = 0;

	FVector DesiredUpVector = FRotationMatrix(CameraRotation).GetUnitAxis(EAxis::Z);
	FVector ArmOrigin = GetComponentLocation();

	for (int i = 0; i < TraceCount; ++i)
	{
		//get angle for the next trace
		float TraceAngle = 180 + StartAngle;
		if (TraceCount > 1)// trace count 1 means a div by zero so let's not do it 
			TraceAngle += i * (EndAngle - StartAngle) / (TraceCount - 1.0f);

		//rotate around camera up
		FQuat QuatRotation = FQuat(DesiredUpVector, FMath::DegreesToRadians(TraceAngle));
		FVector TraceDirection = QuatRotation.RotateVector(CameraRotation.Vector());
		FVector TraceEnd = ArmOrigin + (TraceDirection * ArmLength);

		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SpringArm), false, GetOwner());
		FHitResult Result;
		GetWorld()->LineTraceSingleByChannel(Result, ArmOrigin, TraceEnd, TraceChannel, QueryParams);

		if (Result.bBlockingHit)
		{
			//get a ratio on how far an angle the wall is from our current position (1 for the closest trace to us, 1 / TraceCount for the furthest)
			float CorrectionStrength = (TraceCount - i) / (float)TraceCount;
			
			float moveDistance = (TargetArmLength - Result.Distance);

			if (bUsePositionCurve && IsValid(PositionCurve))
			{
				moveDistance *= PositionCurve->GetFloatValue(CorrectionStrength);
			}

			//only keep the data if it is the biggest correction found so far
			if (FinalMoveDistance < moveDistance)
			{
				FinalMoveDistance = moveDistance;

				OutResult.PredictedMoveDistance = moveDistance;
				OutResult.CorrectionStrength = CorrectionStrength;
			}

			//draw line a bit below so we can see it (else it goes straight in the camera and all lines are superposed when playing)
			if(bShowDebugInfo)
				DrawDebugLine(GetWorld(), ArmOrigin + FVector::UpVector * -20, TraceEnd + FVector::UpVector * -20, FColor::Red, false, 0.0f, 0, 1.0f);
		}
		else
		{
			if(bShowDebugInfo)
				DrawDebugLine(GetWorld(), ArmOrigin + FVector::UpVector * -20, TraceEnd + FVector::UpVector * -20, FColor::Green, false, 0.0f, 0, 1.0f);
		}
	}
	return OutResult.bHitSomething;
}

FTransform UCollisionAnticipationSpringArm::GetSocketTransform(FName InSocketName, ERelativeTransformSpace TransformSpace) const
{
	FTransform RelativeTransform(RelativeSocketRotation, RelativeSocketLocation);

	switch (TransformSpace)
	{
	case RTS_World:
	{
		return RelativeTransform * GetComponentTransform();
	}
	case RTS_Actor:
	{
		if (const AActor* Actor = GetOwner())
		{
			FTransform SocketTransform = RelativeTransform * GetComponentTransform();
			return SocketTransform.GetRelativeTransform(Actor->GetTransform());
		}
		break;
	}
	case RTS_Component:
	{
		return RelativeTransform;
	}
	}
	return RelativeTransform;
}

bool UCollisionAnticipationSpringArm::HasAnySockets() const
{
	return true;
}

void UCollisionAnticipationSpringArm::QuerySupportedSockets(TArray<FComponentSocketDescription>& OutSockets) const
{
	new (OutSockets) FComponentSocketDescription(SocketName, EComponentSocketType::Socket);
}

void UCollisionAnticipationSpringArm::ToggleSocketOffset()
{
	bIsOffset = !bIsOffset;
}

void UCollisionAnticipationSpringArm::Zoom(float value)
{
	TargetArmLength = FMath::Clamp(TargetArmLength + value * ZoomSpeed, MinZoom, MaxZoom);
}

#if WITH_EDITOR
//quickly hacked function to preview the collision prediction line traces inside the blueprint viewport
void UCollisionAnticipationSpringArm::ShowPreviewLines()
{
	FVector DesiredUpVector = FRotationMatrix(GetTargetRotation()).GetUnitAxis(EAxis::Z);
	FVector ArmOrigin = GetComponentLocation();

	// twice because left and right
	for (int i = 0; i < TracesPerSide; ++i)
	{
		//get angle for the next trace
		float TraceAngle = 180 + PredictionStartAngle;
		if (TracesPerSide > 1)// trace count 1 means a div by zero so let's not do it 
			TraceAngle += i * (PredictionEndAngle - PredictionStartAngle) / (TracesPerSide - 1.0f);
		FQuat QuatRotation = FQuat(DesiredUpVector, FMath::DegreesToRadians(TraceAngle));
		FVector TraceDirection = QuatRotation.RotateVector(GetTargetRotation().Vector());
		FVector TraceEnd = ArmOrigin + (TraceDirection * TargetArmLength);
		DrawDebugLine(GetWorld(), ArmOrigin, TraceEnd + FVector::UpVector * -10, FColor::Red, false, 0.0f, 0, 1.0f);
	}

	for (int i = 0; i < TracesPerSide; ++i)
	{
		float TraceAngle = 180 - PredictionStartAngle;
		if (TracesPerSide > 1)// trace count 1 means a div by zero so let's not do it 
			TraceAngle -= i * (PredictionEndAngle - PredictionStartAngle) / (TracesPerSide - 1.0f);
		FQuat QuatRotation = FQuat(DesiredUpVector, FMath::DegreesToRadians(TraceAngle));
		FVector TraceDirection = QuatRotation.RotateVector(GetTargetRotation().Vector());
		FVector TraceEnd = ArmOrigin + (TraceDirection * TargetArmLength);
		DrawDebugLine(GetWorld(), ArmOrigin, TraceEnd + FVector::UpVector * -10, FColor::Red, false, 0.0f, 0, 1.0f);
	}
}
#endif