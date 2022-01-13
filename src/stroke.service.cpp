#include <algorithm>

#include <Arduino.h>

#include "utils.h"
#include "stroke.service.h"

using std::any_of;
using std::minmax;

using StrokeModel::CscData;

// c(2.8) * (distance / time)^3 = P

// driveLinearDistance = Math.pow((dragFactor(1) / rowerSettings.magicConstant(2.8)), 1.0 / 3.0) * ((totalNumberOfImpulses - flankDetector.noImpulsesToBeginFlank()) - drivePhaseStartAngularDisplacement) * (2.0 * Math.PI) / rowerSettings.numOfImpulsesPerRevolution(1)

// driveLinearDistance = Math.pow((dragFactor(1) / rowerSettings.magicConstant(2.8)), 1.0 / 3.0) * ((totalNumberOfImpulses - drivePhaseStartAngularDisplacement"impulses") *  (2.0 * Math.PI) / rowerSettings.numOfImpulsesPerRevolution(1)
// *angularVelocity = (2.0 * Math.PI) / deltaTime
// currentDragFactor = -1 * rowerSettings.flywheelInertia(0.0802) * ((1 / recoveryStartAngularVelocity) - (1 / recoveryEndAngularVelocity)) / recoveryPhaseLength

StrokeService::StrokeService()
{
}

bool StrokeService::isFlywheelUnpowered()
{
    unsigned char numberOfAccelerations = 0;
    unsigned char i = cleanDeltaTimes.size() - 1;
    while (i > 0)
    {
        if (cleanDeltaTimes[i] >= cleanDeltaTimes[i - 1])
        {
            // Oldest interval (dataPoints[i]) is larger than the younger one (datapoint[i-1], as the distance is
            // fixed, we are accelerating
            numberOfAccelerations++;
        }
        i--;
    }

    // If not all of the data points are consistently increasing or they are actually decreasing report unpowered
    if (numberOfAccelerations >= FLYWHEEL_POWER_CHANGE_DETECTION_THRESHOLD)
    {
        return false;
    }

    return true;
}

bool StrokeService::isFlywheelPowered()
{
    unsigned char numberOfDecelerations = 0;
    unsigned char i = cleanDeltaTimes.size() - 1;
    while (i > 0)
    {
        if (cleanDeltaTimes[i] < cleanDeltaTimes[i - 1])
        {
            // Oldest interval (dataPoints[i]) is shorter than the younger one (datapoint[i-1], as the distance is fixed, we
            // discovered a deceleration
            numberOfDecelerations++;
        }
        i--;
    }
    // If not all of the data points are consistently decreasing or they are actually increasing report powered
    if (numberOfDecelerations >= FLYWHEEL_POWER_CHANGE_DETECTION_THRESHOLD)
    {
        return false;
    }

    return true;
}

CscData StrokeService::getCscData() const
{
    // execution time: 8-9 microsec
    // auto start = micros();
    detachRotationInterrupt();
    CscData data = {
        .lastRevTime = lastRevTime,
        .revCount = revCount,
        .lastStrokeTime = lastStrokeTime,
        .strokeCount = strokeCount,
        .deltaTime = cleanDeltaTimes[0],
        .dragFactor = dragFactor * 1.0e+6};
    attachRotationInterrupt();
    // auto stop = micros();

    // Serial.print("getCscData: ");
    // Serial.println(stop - start);
    return data;
}

unsigned long StrokeService::getLastRevTime() const
{
    // execution time: 8-10 microsec
    detachRotationInterrupt();
    auto time = lastRevTime;
    attachRotationInterrupt();

    return time;
}

void StrokeService::processRotation(unsigned long now)
{
    auto currentDeltaTime = now - previousRawRevTime;

    if (currentDeltaTime < ROTATION_DEBOUNCE_TIME_MIN * 1000)
        return;

    auto deltaTimeDiffPair = minmax<volatile unsigned long>(currentDeltaTime, previousDeltaTime);
    auto deltaTimeDiff = deltaTimeDiffPair.second - deltaTimeDiffPair.first;

    previousDeltaTime = currentDeltaTime;
    // We disregard rotation signals that are non sensible (the absolute difference of the current and the previous deltas exceeds the current delta)
    if (deltaTimeDiff > currentDeltaTime)
        return;

    // If we got this far, we must have a sensible delta for flywheel rotation time, updating the deltaTime array
    char i = cleanDeltaTimes.size() - 1;
    while (i > 0)
    {
        cleanDeltaTimes[i] = cleanDeltaTimes[i - 1];
        i--;
    }
    cleanDeltaTimes[0] = currentDeltaTime;

    previousRawRevTime = now;

    // If rotation delta exceeds the max debounce time and we are in Recovery Phase, the rower must have stopped. Setting cylcePhase to "Stpped"
    if (cyclePhase == CyclePhase::Recovery && currentDeltaTime > ROTATION_DEBOUNCE_TIME_MAX * 1000)
    {
        cyclePhase = CyclePhase::Stopped;

        recoveryPhaseStartTime = 0;
        recoveryStartAngularVelocity = 0;
        recoveryPhaseDuration = 0;

        drivePhaseStartTime = 0;
        drivePhaseDuration = 0;

        return;
    }

    if (cyclePhase == CyclePhase::Stopped)
    {
        // We are currently in the "Stopped" phase, as power was not applied for a long period of time or the device just started. Since rotation was detected we check if cleanDeltaTimes array is filled (i.e. whether we have suficient data for determining the next phase) and whether power is being applied to the flywheel
        if (
            any_of(cleanDeltaTimes.begin(), cleanDeltaTimes.end(), [](unsigned long cleanDeltaTime)
                   { return cleanDeltaTime == 0; }) ||
            isFlywheelUnpowered())
            return;

        // Since we detected power, setting to "Drive" phase and adding new rotation
        cyclePhase = CyclePhase::Drive;
        drivePhaseStartTime = now - cleanDeltaTimes[0];

        // TODO: determin if we add new rotation here, or we allow fall thoruhg this if statement and add the new rotation below (consequence of this wuold be that an unnecessary "Drive" phase isFlywheelUnpowered() check is done below)
        lastRevTime = now;
        revCount++;

        return;
    }

    // We add a new rotation since we are not in the "Stopped" phase
    lastRevTime = now;
    revCount++;

    // we implement a finite state machine that goes between "Drive" and "Recovery" phases while paddeling on the machine. This allows a phase-change if sufficient time has passed and there is a plausible flank
    if (cyclePhase == CyclePhase::Drive)
    {
        // We are currently in the "Drive" phase, lets determine what the next phase is (if we come from "Stopped" phase )
        if (isFlywheelUnpowered())
        {
            // It seems that we lost power to the flywheel lets check if drive time was sufficint for detecting a stroke (i.e. drivePhaseDuration exceeds debounce time)
            if (drivePhaseDuration > STROKE_DEBOUNCE_TIME * 1000)
            {
                // Here we can conclude the "Drive" phase as there is no more drive detected to the flywheel (e.g. for calculating power etc.)
                strokeCount++;
                lastStrokeTime = now;
            }

            cyclePhase = CyclePhase::Recovery;
            recoveryPhaseStartTime = now - cleanDeltaTimes[0];
            recoveryStartAngularVelocity = 2 * PI / cleanDeltaTimes[0];
            drivePhaseDuration = 0;
            return;
        }

        drivePhaseDuration = now - drivePhaseStartTime;
        return;
    }

    if (cyclePhase == CyclePhase::Recovery)
    {
        // We are currently in the "Recovery" phase, lets determine what the next phase is
        if (isFlywheelPowered())
        {
            // Here we can conclude the "Recovery" phase as drive to the flywheel is detected (e.g. calculating drag factor)
            auto recoveryEndAngularVelocity = 2 * PI / cleanDeltaTimes[DELTA_TIME_ARRAY_LENGTH - 1];
            if (recoveryStartAngularVelocity > recoveryEndAngularVelocity && recoveryPhaseDuration < MAX_DRAG_FACTOR_RECOVERY_PERIOD * 1000)
            {
                dragFactor = -1 * 0.0802 * ((1 / recoveryStartAngularVelocity) - (1 / recoveryEndAngularVelocity)) / recoveryPhaseDuration;
                // Serial.print("dragFactor: ");
                // Serial.println(dragFactor * 10e6);
                // Serial.print("recoveryStartAngularVelocity: ");
                // Serial.println(2 * PI / recoveryStartAngularVelocity);
                // Serial.print("recoveryEndAngularVelocity: ");
                // Serial.println(2 * PI / recoveryEndAngularVelocity);
                // Serial.print("recoveryPhaseDuration: ");
                // Serial.println(recoveryPhaseDuration);
            }

            cyclePhase = CyclePhase::Drive;
            drivePhaseStartTime = now - cleanDeltaTimes[0];
            recoveryPhaseDuration = 0;
            return;
        }

        recoveryPhaseDuration = now - recoveryPhaseStartTime;
        return;
    }
}