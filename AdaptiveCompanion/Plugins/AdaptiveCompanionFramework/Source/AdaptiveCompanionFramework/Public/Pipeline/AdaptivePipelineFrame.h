#pragma once

#include "CoreMinimal.h"

/** A discrete occurrence reported by the game, e.g. "Combat.DamageTaken" with the damage as value */
struct FAdaptiveTelemetryEvent
{
	FName Name;
	double Value = 1.0;

	/** Session time (s) when it was reported */
	double Time = 0.0;
};

/** One assistance-need dimension, e.g. "Combat" */
struct FAdaptiveNeedState
{
	/** How much help the player needs in this dimension, in [0, 1] */
	double Score = 0.0;

	/** True while the score is above the stage's threshold */
	bool bElevated = false;

	/** True only in the update where bElevated changed */
	bool bChanged = false;
};

/**
 *  The data passed through the stages in one pipeline update. Every stage reads what the stages
 *  before it wrote and adds its own output. Stages add their fields here as they are implemented.
 */
struct FAdaptivePipelineFrame
{
	/** Number of this update within the session, starting at 0 */
	int64 UpdateIndex = 0;

	/** Session time (s), the same clock as "t" in the session log */
	double Time = 0.0;

	/** Seconds since the previous update, 0 for the first one */
	double DeltaTime = 0.0;

	/** 1 Telemetry: events reported since the previous update, oldest first */
	TArray<FAdaptiveTelemetryEvent> Events;

	/** 1 Telemetry: latest value of every continuous signal reported so far */
	TMap<FName, double> Values;

	/** 2 Features: features in [0, 1] by name */
	TMap<FName, double> Features;

	/** 3 Need: state of every need dimension by name */
	TMap<FName, FAdaptiveNeedState> Needs;

	/** 4 Context: contexts the game reported as active, e.g. "Context.Combat", in the order they became active */
	TSet<FName> Contexts;

	/** 4 Context: true only in the update where Contexts differs from the previous update */
	bool bContextChanged = false;

	/** 5 Selection: the intervention to start in this update, NAME_None for none */
	FName Intervention;
};

/**
 *  Decides when a fixed-rate update is due, given a clock that only advances once per frame.
 *  Due times advance by exactly one interval, so the average rate matches the configured rate as
 *  long as the frame rate is higher. After a hitch, missed updates are skipped instead of run in
 *  a burst.
 */
struct FAdaptiveFixedRate
{
	explicit FAdaptiveFixedRate(double InInterval = 0.1)
		: Interval(InInterval)
	{
	}

	/** Returns true if an update is due at Now, at most once per call */
	bool Step(double Now)
	{
		if (Now < NextTime)
		{
			return false;
		}

		NextTime += Interval;
		if (NextTime <= Now)
		{
			NextTime = Now + Interval;
		}
		return true;
	}

	double Interval;

	/** The first call is always due */
	double NextTime = 0.0;
};
