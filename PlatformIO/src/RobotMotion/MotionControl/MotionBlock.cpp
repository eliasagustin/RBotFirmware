// RBotFirmware
// Rob Dobson 2016-2018

#include "MotionBlock.h"
#include "AxisValues.h"
#include "../AxesParams.h"

static const char* MODULE_PREFIX = "MotionBlock: ";

void MotionBlock::setNumberedCommandIndex(int cmdIdx)
{
    _numberedCommandIndex = cmdIdx;
}
int IRAM_ATTR MotionBlock::getNumberedCommandIndex()
{
    return _numberedCommandIndex;
}
int32_t MotionBlock::getStepsToTarget(int axisIdx)
{
    if (axisIdx >= 0 && axisIdx < RobotConsts::MAX_AXES)
    {
        return _stepsTotalMaybeNeg[axisIdx];
    }
    return 0;
}

int32_t MotionBlock::getAbsStepsToTarget(int axisIdx)
{
    if (axisIdx >= 0 && axisIdx < RobotConsts::MAX_AXES)
    {
        return abs(_stepsTotalMaybeNeg[axisIdx]);
    }
    return 0;
}

void MotionBlock::setStepsToTarget(int axisIdx, int32_t steps)
{
    if (axisIdx >= 0 && axisIdx < RobotConsts::MAX_AXES)
    {
        _stepsTotalMaybeNeg[axisIdx] = steps;
        if (abs(steps) > abs(_stepsTotalMaybeNeg[_axisIdxWithMaxSteps]))
            _axisIdxWithMaxSteps = axisIdx;
    }
}

uint32_t MotionBlock::getExitStepRatePerTTicks()
{
    return _finalStepRatePerTTicks;
}

float MotionBlock::maxAchievableSpeed(float acceleration, float target_velocity, float distance)
{
    return sqrtf(target_velocity * target_velocity + 2.0F * acceleration * distance);
}

void MotionBlock::forceInBounds(float &val, float lowBound, float highBound)
{
    if (val < lowBound)
        val = lowBound;
    if (val > highBound)
        val = highBound;
}

void MotionBlock::setEndStopsToCheck(AxisMinMaxBools &endStopCheck)
{
    Log.verbose("%sSet test endstops %x\n", MODULE_PREFIX, endStopCheck.uintVal());
    _endStopsToCheck = endStopCheck;
}

// The block's entry and exit speed are now known
// The block can accelerate and decelerate as required as long as these criteria are met
// We now compute the stepping parameters to make motion happen
bool MotionBlock::prepareForStepping(AxesParams &axesParams)
{
    // If block is currently being executed don't change it
    if (_isExecuting)
        return false;

    // Find the max number of steps for any axis
    uint32_t absMaxStepsForAnyAxis = abs(_stepsTotalMaybeNeg[_axisIdxWithMaxSteps]);

    // Get the initial step rate, final step rate and max acceleration for the axis with max steps
    double stepDistMM = _moveDistPrimaryAxesMM * _unitVecAxisWithMaxDist / abs(_stepsTotalMaybeNeg[_axisIdxWithMaxSteps]);
    float initialStepRatePerSec = _entrySpeedMMps * _unitVecAxisWithMaxDist / stepDistMM;
    if (initialStepRatePerSec > axesParams.getMaxStepRatePerSec(_axisIdxWithMaxSteps))
        initialStepRatePerSec = axesParams.getMaxStepRatePerSec(_axisIdxWithMaxSteps);
    float finalStepRatePerSec = _exitSpeedMMps * _unitVecAxisWithMaxDist / stepDistMM;
    if (finalStepRatePerSec > axesParams.getMaxStepRatePerSec(_axisIdxWithMaxSteps))
        finalStepRatePerSec = axesParams.getMaxStepRatePerSec(_axisIdxWithMaxSteps);
    float maxAccStepsPerSec2 = axesParams.getMaxAccel(_axisIdxWithMaxSteps) * _unitVecAxisWithMaxDist / stepDistMM;

    // Calculate the distance decelerating and ensure within bounds
    // Using the facts for the block ... (assuming max accleration followed by max deceleration):
    //		Vmax * Vmax = Ventry * Ventry + 2 * Amax * Saccelerating
    //		Vexit * Vexit = Vmax * Vmax - 2 * Amax * Sdecelerating
    //      Stotal = Saccelerating + Sdecelerating
    // And solving for Saccelerating (distance accelerating)
    uint32_t stepsAccelerating = 0;
    float stepsAcceleratingFloat =
        ceilf((powf(finalStepRatePerSec, 2) - powf(initialStepRatePerSec, 2)) / 4 /
                    maxAccStepsPerSec2 +
                absMaxStepsForAnyAxis / 2);
    if (stepsAcceleratingFloat > 0)
    {
        stepsAccelerating = uint32_t(stepsAcceleratingFloat);
        if (stepsAccelerating > absMaxStepsForAnyAxis)
            stepsAccelerating = absMaxStepsForAnyAxis;
    }

    // Decelerating steps
    uint32_t stepsDecelerating = 0;

    // Find max possible rate for axis with max steps
    float axisMaxStepRatePerSec = _feedrateMMps * _unitVecAxisWithMaxDist / stepDistMM;
    if (axisMaxStepRatePerSec > axesParams.getMaxStepRatePerSec(_axisIdxWithMaxSteps))
        axisMaxStepRatePerSec = axesParams.getMaxStepRatePerSec(_axisIdxWithMaxSteps);

    // See if max speed will be reached
    uint32_t stepsToMaxSpeed =
        uint32_t((powf(axisMaxStepRatePerSec, 2) - powf(initialStepRatePerSec, 2)) /
                    2 / maxAccStepsPerSec2);
    if (stepsAccelerating > stepsToMaxSpeed)
    {
        // Max speed will be reached
        stepsAccelerating = stepsToMaxSpeed;

        // Decelerating steps
        stepsDecelerating =
            uint32_t((powf(axisMaxStepRatePerSec, 2) - powf(finalStepRatePerSec, 2)) /
                        2 / maxAccStepsPerSec2);
    }
    else
    {
        // Calculate max speed that will be reached
        axisMaxStepRatePerSec =
            sqrtf(powf(initialStepRatePerSec, 2) + 2.0F * maxAccStepsPerSec2 * stepsAccelerating);

        // Decelerating steps
        stepsDecelerating = absMaxStepsForAnyAxis - stepsAccelerating;
    }

    // Fill in the step values for this axis
    _initialStepRatePerTTicks = uint32_t((initialStepRatePerSec * TTICKS_VALUE) / TICKS_PER_SEC);
    _maxStepRatePerTTicks = uint32_t((axisMaxStepRatePerSec * TTICKS_VALUE) / TICKS_PER_SEC);
    _finalStepRatePerTTicks = uint32_t((finalStepRatePerSec * TTICKS_VALUE) / TICKS_PER_SEC);
    _accStepsPerTTicksPerMS = uint32_t((maxAccStepsPerSec2 * TTICKS_VALUE) / TICKS_PER_SEC / 1000);
    _stepsBeforeDecel = absMaxStepsForAnyAxis - stepsDecelerating;
    _stepDistMM = stepDistMM;

    return true;
}

void MotionBlock::debugShowBlkHead()
{
    Log.notice("#i EntMMps ExtMMps StTotX StTotY StTotZ St>Dec    Init     (perTT)      Pk     (perTT)     Fin     (perTT)     Acc     (perTT) UnitVecMaxIx FeedRtMMps StepDistMM  MaxStepRate\n");
}

void MotionBlock::debugShowBlock(int elemIdx, AxesParams &axesParams)
{
    char tmpBuf[200];
    sprintf(tmpBuf, "%2d%8.3f%8.3f%7d%7d%7d%7u%8.3f(%10d)%8.3f(%10d)%8.3f(%10d)%8.3f(%10u)%13.8f%11.6f%11.8f%11.3f", elemIdx,
                _entrySpeedMMps,
                _exitSpeedMMps,
                getStepsToTarget(0),
                getStepsToTarget(1),
                getStepsToTarget(2),
                _stepsBeforeDecel,
                debugStepRateToMMps(_initialStepRatePerTTicks), _initialStepRatePerTTicks,
                debugStepRateToMMps(_maxStepRatePerTTicks), _maxStepRatePerTTicks,
                debugStepRateToMMps(_finalStepRatePerTTicks), _finalStepRatePerTTicks,
                debugStepRateToMMps2(_accStepsPerTTicksPerMS),_accStepsPerTTicksPerMS,
                _unitVecAxisWithMaxDist,
                _feedrateMMps,
                _stepDistMM,
                axesParams.getMaxStepRatePerSec(0));
    Log.notice("%s\n", tmpBuf);
}
