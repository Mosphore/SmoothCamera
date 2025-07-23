#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "CollisionAnticipationSpringArm.generated.h"

//Originally this was inherited from USpringArmComponent, but I just removed too much useless stuff for my purpose so I decided to make a different class, though a lot of it is inspired from USpringArmComponent
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UBITEST_API UCollisionAnticipationSpringArm : public USceneComponent
{
	GENERATED_BODY()
	
public:

	// Sets default values for this component's properties
	UCollisionAnticipationSpringArm();

protected:

	//a struct containing everything needed to compute the final position of the camera after all the collision detection LineTraces 
	struct FCollisionPredictionResult
	{
		//the distance the camera has to move towards the character from it's default uncorrected position
		float PredictedMoveDistance = 0;
		//the ratio applied to the correction depending on the angle of the collision,
		//for example if we do 4 traces the furthest from center will have a ratio of 0.25 and the closest (possibly right behind the character) will be 1
		float CorrectionStrength = 0;

		uint8 bHitSomething:1;
	};

public:

	/** Natural length of the spring arm when there are no collisions */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Camera, meta = (ClampMin = "50.0", ClampMax = "1000.0", UIMin = "50.0", UIMax = "1000.0"))
	float TargetArmLength = 400;

	/** offset at end of spring arm; use this instead of the relative offset of the attached component to ensure the line trace works as desired */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Camera)
	FVector SocketOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Camera, meta = (ClampMin = "50.0", ClampMax = "1000.0", UIMin = "50.0", UIMax = "1000.0"))
	float MaxZoom = 500.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Camera, meta = (ClampMin = "50.0", ClampMax = "1000.0", UIMin = "50.0", UIMax = "1000.0"))
	float MinZoom = 50.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Camera, meta = (ClampMin = "1.0", ClampMax = "100.0", UIMin = "1.0", UIMax = "100.0"))
	float ZoomSpeed = 5.f;

	/** If true, do a collision test to prevent camera clipping into level*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CameraCollision)
	uint32 bDoCollisionTest : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CameraCollision)
	uint32 bDoCollisionPrediction : 1;

	/** How big should the last sphere trace be (in unreal units) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CameraCollision, meta = (editcondition = "bDoCollisionTest"))
	float SphereTraceSize = 12.f;
	/** If true, do a collision prediction to make the camera move smoothly before hitting a wall and teleporting forward abruptly*/

	/** Collision channel of the Line traces (defaults to ECC_Camera) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CameraCollision, meta = (editcondition = "bDoCollisionTest"))
	TEnumAsByte<ECollisionChannel> TraceChannel;

	UPROPERTY(EditAnywhere, Category = CameraCollision, meta = (editcondition = "bDoCollisionPrediction", ClampMin = "0.0", ClampMax = "90.0", UIMin = "0.0", UIMax = "90.0"))
	float PredictionStartAngle = 5.f;

	UPROPERTY(EditAnywhere, Category = CameraCollision, meta = (editcondition = "bDoCollisionPrediction", ClampMin = "0.0", ClampMax = "180.0", UIMin = "0.0", UIMax = "180.0"))
	float PredictionEndAngle = 60.f;

	/** The number of traces we want to do around our character on each side*/
	UPROPERTY(EditAnywhere, Category = CameraCollision, meta = (editcondition = "bDoCollisionPrediction", ClampMin = "1", ClampMax = "20", UIMin = "1", UIMax = "20"))
	int TracesPerSide = 2;

	/** this should be a little fast to avoid walls if we move the camera quickly*/
	UPROPERTY(EditAnywhere, Category = CameraCollision, meta = (editcondition = "bDoCollisionPrediction", ClampMin = "1.0", ClampMax = "100.0", UIMin = "1.0", UIMax = "100.0"))
	float CorrectionSpeedForward = 10.f;

	/** this can be slower than forward*/
	UPROPERTY(EditAnywhere, Category = CameraCollision, meta = (editcondition = "bDoCollisionPrediction", ClampMin = "0.1", ClampMax = "50.0", UIMin = "0.1", UIMax = "50.0"))
	float CorrectionSpeedBack = 1.f;

	/** delay before the camera can start going back to its original position without collisions*/
	UPROPERTY(EditAnywhere, Category = CameraCollision, meta = (editcondition = "bDoCollisionPrediction", ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "2.0"))
	float ReturnDelay = 0.3f;

	/**
	* use a curve to multiply the interpolation speed with,
	* the position of the curve is determined by the angle between the camera and the closest wall
	* the smaller the angle between the wall and the camera, the faster the camera can move*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CameraCollision)
	uint32 bUseSpeedCurve : 1;

	/**
	* use a curve to multiply the distance we correct the camera from it's intended distance,
	* for example if we detect a wall 1 meter from us and the camera is usually 4 meters away,
	* the ideal correction is 3 meters but if the wall is still at a big angle from us we might want to correct less than that
	* this works better with high trace count because on low count you can see the jumps */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CameraCollision)
	uint32 bUsePositionCurve : 1;

	UPROPERTY(EditAnywhere, Category = CameraCollision)
	UCurveFloat* SpeedCurve;

	UPROPERTY(EditAnywhere, Category = CameraCollision)
	UCurveFloat* PositionCurve;

	/**
	 * If this component is placed on a pawn, should it use the view/control rotation of the pawn where possible?
	 * When disabled, the component will revert to using the stored RelativeRotation of the component.
	 * Note that this component itself does not rotate, but instead maintains its relative rotation to its parent as normal,
	 * and just repositions and rotates its children as desired by the inherited rotation settings. Use GetTargetRotation()
	 * if you want the rotation target based on all the settings (UsePawnControlRotation, InheritPitch, etc).
	 *
	 * @see GetTargetRotation(), APawn::GetViewRotation()
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CameraSettings)
	uint32 bUsePawnControlRotation : 1;

	/** Should we inherit pitch from parent component. Does nothing if using Absolute Rotation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CameraSettings)
	uint32 bInheritPitch : 1;

	/** Should we inherit yaw from parent component. Does nothing if using Absolute Rotation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CameraSettings)
	uint32 bInheritYaw : 1;

	/** Should we inherit roll from parent component. Does nothing if using Absolute Rotation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CameraSettings)
	uint32 bInheritRoll : 1;

	/** If true AND the view target is simulating using physics then use the same max timestep cap as the physics system. Prevents camera jitter when delta time is clamped within Chaos Physics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Lag)
	uint32 bClampToMaxPhysicsDeltaTime : 1;

	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bPreviewTracesInEditor = true;

	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bShowDebugInfo = false;

protected:
	//small delay when there is no collision prediction before letting the camera come back to it's default position, else we get crazy jitter on small movements
	float ReturnTimer;
	float PreviousForwardMovement = 0;
	FVector PreviousOffset = FVector::ZeroVector;
	FVector PreviousDesiredLoc = FVector::ZeroVector;
	/** Cached component-space socket location */
	FVector RelativeSocketLocation;
	/** Cached component-space socket rotation */
	FQuat RelativeSocketRotation;

	bool bIsOffset = false;

public:
	/**
	 * Get the target rotation we inherit, used as the base target for the boom rotation.
	 * This is derived from attachment to our parent and considering the UsePawnControlRotation and absolute rotation flags.
	 */
	UFUNCTION(BlueprintCallable, Category = SpringArm)
	FRotator GetTargetRotation() const;

	/** */
	UFUNCTION(BlueprintCallable, Category = SpringArm)
	void ToggleSocketOffset();

	UFUNCTION(BlueprintCallable, Category = SpringArm)
	void Zoom(float value);

	// UActorComponent interface
	virtual void BeginPlay() override;
	virtual void OnRegister() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//virtual void PostLoad() override;
	//virtual void ApplyWorldOffset(const FVector& InOffset, bool bWorldShift) override;
	// End of UActorComponent interface

	// USceneComponent interface
	virtual bool HasAnySockets() const override;
	virtual FTransform GetSocketTransform(FName InSocketName, ERelativeTransformSpace TransformSpace = RTS_World) const override;
	virtual void QuerySupportedSockets(TArray<FComponentSocketDescription>& OutSockets) const override;
	// End of USceneComponent interface

	/** The name of the socket at the end of the spring arm (looking back towards the spring arm origin) */
	static const FName SocketName;

	/** Returns the desired rotation for the spring arm, before the rotation constraints such as bInheritPitch etc are enforced. */
	virtual FRotator GetDesiredRotation() const;

protected:
	/** Updates the desired arm location, calling BlendLocations to do the actual blending if a trace is done */
	virtual void UpdateDesiredArmLocation(bool bDoCollision, bool bPredictCollisions, float DeltaTime);

	// do line traces in a horizontal fan shape to check for walls and calculates how much we need the camera to move forward based on the collisions we hit
	bool CheckSurroundingWallsCollisions(FCollisionPredictionResult& OutResult, const FRotator& cameraRotation, float armLength, float startAngle,  float endAngle, int traceCount);

#if WITH_EDITOR
	void ShowPreviewLines();
#endif
};