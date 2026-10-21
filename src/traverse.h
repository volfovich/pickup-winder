#pragma once

#include <Arduino.h>
#include "traverse_math.h"

// Wire guide on the 28T0601-50 slide. One 40 kHz timer interrupt makes all
// its steps:
// - Move: ramped moves for homing, jog and going to the start position;
// - Follow: while winding, the position is computed from the spindle's live
//   step count (traverse_math.h: even layers, or a scatter layer table), so
//   the guide follows the spindle's ramps, pauses and stops by itself.
// Position 0 is the home switch at the motor end; positions grow away from
// the motor. The soft limits 0..TRAVERSE_TRAVEL_UM apply once homed.
namespace traverse
{
    void begin(); // pins and step timer; the driver itself is tmc::traverse

    enum class Event : uint8_t { None, MoveDone, Homed, HomingFailed };
    // Call every loop pass: runs the homing sequence. Returns an event once.
    Event update();

    // Motion, refused while following. nullptr on success, else the reason.
    const char* home();
    const char* jog(int32_t um);          // relative; before homing at most +-5 mm
    const char* moveTo(int32_t um);       // absolute; needs homing
    const char* moveToSteps(int32_t steps);
    void stop();                          // ramped stop of a move or homing

    // Winding: follow the spindle with this pattern until stopFollow().
    // The guide must already be at position(p, 0).
    void follow(const traverse_math::Pattern& p);
    void stopFollow();
    bool following();
    bool limitHit();         // following tried to pass a soft limit
    int32_t maxFollowError(); // steps, since follow()

    bool homed();
    bool busy(); // homing or moving
    int32_t positionSteps();
    float positionMm();
    bool endstopTriggered();
    const char* lastError(); // why homing or the last move failed

    void enableDriver(bool on); // disabling also forgets the home position
    bool driverEnabled();
}
