// RBotFirmware
// Rob Dobson 2016

#pragma once

#include "application.h"
#include "AxesParams.h"
#include "AxisPosition.h"
#include "RobotCommandArgs.h"
#include "MotionPlanner.h"
#include "MotionIO.h"
#include "MotionActuator.h"

class MotionHelper
{
public:
	static constexpr float blockDistanceMM_default = 1.0f;
	static constexpr float junctionDeviation_default = 0.05f;
	static constexpr float distToTravelMM_ignoreBelow = 0.01f;
	static constexpr int pipelineLen_default = 100;

private:
	// Pause
	bool _isPaused;
    // Robot dimensions
    float _xMaxMM;
    float _yMaxMM;
	// Block distance
	float _blockDistanceMM;
	// Axes parameters
	AxesParams _axesParams;
	// Callbacks for coordinate conversion etc
	ptToActuatorFnType _ptToActuatorFn;
	actuatorToPtFnType _actuatorToPtFn;
	correctStepOverflowFnType _correctStepOverflowFn;
	// Relative motion
	bool _moveRelative;
	// Planner used to plan the pipeline of motion
	MotionPlanner _motionPlanner;
	// Axis Current Motion
	AxisPosition _curAxisPosition;
	// Motion pipeline
	MotionPipeline _motionPipeline;
	// Motion IO (Motors and end-stops)
	MotionIO _motionIO;
	// Motion
	MotionActuator _motionActuator;
	// Debug
	unsigned long _debugLastPosDispMs;

public:
	MotionHelper();
	~MotionHelper();

	void setTransforms(ptToActuatorFnType ptToActuatorFn, actuatorToPtFnType actuatorToPtFn,
		correctStepOverflowFnType correctStepOverflowFn);

	void configure(const char* robotConfigJSON);

	// Can accept
	bool canAccept();
	// Pause (or un-pause) all motion
	void pause(bool pauseIt);
	// Check if paused
	bool isPaused();
	// Stop
	void stop();
	// Check if idle
	bool isIdle();

	double getStepsPerUnit(int axisIdx)
	{
		return _axesParams.getStepsPerUnit(axisIdx);
	}

	double getStepsPerRotation(int axisIdx)
	{
		return _axesParams.getStepsPerRotation(axisIdx);
	}

	double getUnitsPerRotation(int axisIdx)
	{
		return _axesParams.getUnitsPerRotation(axisIdx);
	}

	long getHomeOffsetSteps(int axisIdx)
	{
		return _axesParams.getHomeOffsetSteps(axisIdx);
	}

	bool moveTo(RobotCommandArgs& args);
	void setMotionParams(RobotCommandArgs& args);
	void getCurStatus(RobotCommandArgs& args);
	void service(bool processPipeline);

	unsigned long getLastActiveUnixTime()
	{
		return _motionIO.getLastActiveUnixTime();
	}

	void debugShowBlocks();
	int testGetPipelineCount();
	void testGetPipelineBlock(int elIdx, MotionBlock& elem);

private:
	bool isInBounds(double v, double b1, double b2)
	{
		return (v > std::min(b1, b2) && v < std::max(b1, b2));
	}

	bool configureRobot(const char* robotConfigJSON);
	bool configureAxis(const char* robotConfigJSON, int axisIdx);
	void pipelineService(bool hasBeenPaused);
};