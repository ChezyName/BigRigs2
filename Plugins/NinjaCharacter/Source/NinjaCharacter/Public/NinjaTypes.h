// "Ninja Character" plugin, by Javier 'Xaklse' Osset; Copyright 2020


#pragma once


/** Provides modes that determine direction of gravity. */
UENUM(BlueprintType)
enum class ENinjaGravityDirectionMode : uint8
{
	/** Gravity direction is fixed (it can be aligned to floor normal too). */
	Fixed,
	/** Gravity direction is determined by closest spline tangent. */
	SplineTangent,
	/** Gravity direction points to a fixed location or moving Actor. */
	Point,
	/** Gravity direction points to an infinite line. */
	Line,
	/** Gravity direction points to a line bounded by two points. */
	Segment,
	/** Gravity direction points to a spline. */
	Spline,
	/** Gravity direction points to an infinite plane. */
	Plane,
	/** Gravity direction points to an infinite plane determined by closest spline up vector. */
	SplinePlane,
	/** Gravity direction points to an axis-aligned box. */
	Box,
	/** Gravity direction points to collision geometry of an Actor. */
	Collision,
	/** Mode not used (#1). */
	Unused1,
	/** Mode not used (#2). */
	Unused2,
	/** Mode not used (#3). */
	Unused3,
	/** Mode not used (#4). */
	Unused4,
	/** Mode not used (#5). */
	Unused5,
	/** Mode not used (#6). */
	Unused6,
	/** Mode not used (#7). */
	Unused7,
	/** Mode not used (#8). */
	Unused8,
	/** Mode not used (#9). */
	Unused9,
};
