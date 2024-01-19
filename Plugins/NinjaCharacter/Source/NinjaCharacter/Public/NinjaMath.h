// "Ninja Character" plugin, by Javier 'Xaklse' Osset; Copyright 2020


#pragma once


#include "Math/UnrealMathUtility.h"
#include "Math/Matrix.h"
#include "Math/Quat.h"
#include "Math/Vector.h"


/** Determines if two unit vectors are perpendicular; this is cos(89). */
#define NINJA_NORMALS_ORTHOGONAL 0.01745240643f
/** Determines if two unit vectors are parallel; this is cos(1). */
#define NINJA_NORMALS_PARALLEL 0.99984769515f


/**
 * Offers complementary math helper functions.
 */
struct FNinjaMath
{
	/**
	 * Gets the forward direction (X axis) rotated by a quaternion.
	 * @param Quat - the quaternion from which to extract the direction
	 * @return X rotation axis from given quaternion
	 */
	static FORCEINLINE FVector GetAxisX(const FQuat& Quat)
	{
		// Code taken from FQuatRotationTranslationMatrix::FQuatRotationTranslationMatrix

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST) && WITH_EDITORONLY_DATA
		check(Quat.IsNormalized());
#endif

		const FQuat::FReal Y2 = Quat.Y + Quat.Y;
		const FQuat::FReal Z2 = Quat.Z + Quat.Z;

		return FVector(1.0f - (Quat.Y * Y2 + Quat.Z * Z2),
			Quat.X * Y2 + Quat.W * Z2, Quat.X * Z2 - Quat.W * Y2);
	}

	/**
	 * Gets the right direction (Y axis) rotated by a quaternion.
	 * @param Quat - the quaternion from which to extract the direction
	 * @return Y rotation axis from given quaternion
	 */
	static FORCEINLINE FVector GetAxisY(const FQuat& Quat)
	{
		// Code taken from FQuatRotationTranslationMatrix::FQuatRotationTranslationMatrix

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST) && WITH_EDITORONLY_DATA
		check(Quat.IsNormalized());
#endif

		const FQuat::FReal X2 = Quat.X + Quat.X;
		const FQuat::FReal Y2 = Quat.Y + Quat.Y;
		const FQuat::FReal Z2 = Quat.Z + Quat.Z;

		return FVector(Quat.X * Y2 - Quat.W * Z2,
			1.0f - (Quat.X * X2 + Quat.Z * Z2), Quat.Y * Z2 + Quat.W * X2);
	}

	/**
	 * Gets the up direction (Z axis) rotated by a quaternion.
	 * @param Quat - the quaternion from which to extract the direction
	 * @return Z rotation axis from given quaternion
	 */
	static FORCEINLINE FVector GetAxisZ(const FQuat& Quat)
	{
		// Code taken from FQuatRotationTranslationMatrix::FQuatRotationTranslationMatrix

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST) && WITH_EDITORONLY_DATA
		check(Quat.IsNormalized());
#endif

		const FQuat::FReal X2 = Quat.X + Quat.X;
		const FQuat::FReal Y2 = Quat.Y + Quat.Y;
		const FQuat::FReal Z2 = Quat.Z + Quat.Z;

		return FVector(Quat.X * Z2 + Quat.W * Y2,
			Quat.Y * Z2 - Quat.W * X2, 1.0f - (Quat.X * X2 + Quat.Y * Y2));
	}

	/**
	 * Gets the forward direction (X axis) from a rotator.
	 * @param Rot - the rotator from which to extract the direction
	 * @return X rotation axis from given rotator
	 */
	static FORCEINLINE FVector GetAxisX(const FRotator& Rot)
	{
		// Code taken from UKismetMathLibrary::GetForwardVector

		return Rot.Vector();
	}

	/**
	 * Gets the right direction (Y axis) from a rotator.
	 * @param Rot - the rotator from which to extract the direction
	 * @return Y rotation axis from given rotator
	 */
	static FORCEINLINE FVector GetAxisY(const FRotator& Rot)
	{
		// Code taken from UKismetMathLibrary::GetRightVector

		return FRotationMatrix(Rot).GetScaledAxis(EAxis::Y);
	}

	/**
	 * Gets the up direction (Z axis) from a rotator.
	 * @param Rot - the rotator from which to extract the direction
	 * @return Z rotation axis from given rotator
	 */
	static FORCEINLINE FVector GetAxisZ(const FRotator& Rot)
	{
		// Code taken from UKismetMathLibrary::GetUpVector

		return FRotationMatrix(Rot).GetScaledAxis(EAxis::Z);
	}

	/**
	 * Builds a quaternion with given Z and X axes (X from another quaternion).
	 * Z will remain fixed, X may be changed to enforce orthogonality.
	 * @param ZAxis - fixed Z axis
	 * @param Quat - the quaternion from which to extract X axis
	 * @param CosineThreshold - vectors are parallel if cosine of angle between them is greater than this threshold
	 * @return quaternion rotation with given Z axis
	 */
	static FQuat MakeFromZQuat(const FVector& ZAxis, const FQuat& Quat,
		float CosineThreshold = NINJA_NORMALS_PARALLEL)
	{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST) && WITH_EDITORONLY_DATA
		check(ZAxis.IsNormalized());
		check(Quat.IsNormalized());
#endif

		const FVector QuatZ = FNinjaMath::GetAxisZ(Quat);
		if (!FNinjaMath::Parallel(QuatZ, ZAxis, CosineThreshold))
		{
			return FQuat::FindBetweenNormals(QuatZ, ZAxis) * Quat;
		}
		else
		{
			const FVector QuatX = FNinjaMath::GetAxisX(Quat);
			const FVector YAxis = (ZAxis ^ QuatX).GetSafeNormal();
			return FMatrix(YAxis ^ ZAxis, YAxis, ZAxis, FVector::ZeroVector).ToQuat();
		}
	}

	/**
	 * Checks if two normalized vectors nearly point to the same direction.
	 * @param Vector1 - first normalized vector
	 * @param Vector2 - second normalized vector
	 * @param CosineThreshold - vectors are parallel if cosine of angle between them is greater than this threshold
	 * @return true if angle between given vectors is close to 0 degrees
	 */
	static FORCEINLINE bool Coincident(const FVector& Vector1, const FVector& Vector2,
		float CosineThreshold = NINJA_NORMALS_PARALLEL)
	{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST) && WITH_EDITORONLY_DATA
		check(Vector1.IsNormalized());
		check(Vector2.IsNormalized());
#endif

		return (Vector1 | Vector2) >= CosineThreshold;
	}

	/**
	 * Checks if two normalized vectors are nearly opposite.
	 * @param Vector1 - first normalized vector
	 * @param Vector2 - second normalized vector
	 * @param CosineThreshold - vectors are parallel if cosine of angle between them is greater than this threshold
	 * @return true if angle between given vectors is close to 180 degrees
	 */
	static FORCEINLINE bool Opposite(const FVector& Vector1, const FVector& Vector2,
		float CosineThreshold = NINJA_NORMALS_PARALLEL)
	{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST) && WITH_EDITORONLY_DATA
		check(Vector1.IsNormalized());
		check(Vector2.IsNormalized());
#endif

		return (Vector1 | Vector2) <= -CosineThreshold;
	}

	/**
	 * Checks if two normalized vectors are nearly perpendicular.
	 * @param Vector1 - first normalized vector
	 * @param Vector2 - second normalized vector
	 * @param CosineThreshold - vectors are orthogonal if cosine of angle between them is less than this threshold
	 * @return true if angle between given vectors is close to 90 degrees
	 */
	static FORCEINLINE bool Orthogonal(const FVector& Vector1, const FVector& Vector2,
		float CosineThreshold = NINJA_NORMALS_ORTHOGONAL)
	{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST) && WITH_EDITORONLY_DATA
		check(Vector1.IsNormalized());
		check(Vector2.IsNormalized());
#endif

		return FMath::Abs(Vector1 | Vector2) <= CosineThreshold;
	}

	/**
	 * Checks if two normalized vectors are nearly parallel.
	 * @param Vector1 - first normalized vector
	 * @param Vector2 - second normalized vector
	 * @param CosineThreshold - vectors are parallel if cosine of angle between them is greater than this threshold
	 * @return true if angle between given vectors is close to 0/180 degrees
	 */
	static FORCEINLINE bool Parallel(const FVector& Vector1, const FVector& Vector2,
		float CosineThreshold = NINJA_NORMALS_PARALLEL)
	{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST) && WITH_EDITORONLY_DATA
		check(Vector1.IsNormalized());
		check(Vector2.IsNormalized());
#endif

		return FMath::Abs(Vector1 | Vector2) >= CosineThreshold;
	}
};
