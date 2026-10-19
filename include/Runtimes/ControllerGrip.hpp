#pragma once
// ControllerGrip -- makes every controller's OpenXR GRIP pose land where a Touch controller's does.
//
// WHY THIS EXISTS (HTC Vive / Index "hands are at the wrong angle")
//
// The OpenXR spec only says the grip pose is "where a hand holds the controller"; each runtime picks the
// exact point and tilt PER CONTROLLER. Everything in the hand/arm code -- the wrist offsets in
// vrik_calibration.ini, the weapon alignment, the holster zones -- was tuned on Touch controllers, so a
// controller whose grip sits somewhere else shows up as a hand rotated and shifted by the difference.
//
// SteamVR publishes those placements. They are in each controller's render-model JSON as a
// "component_local" transform in the controller's own device frame, e.g.
//     SteamVR\resources\rendermodels\oculus_quest2_controller_left\*.json      "openxr_grip"
//     SteamVR\drivers\indexcontroller\resources\rendermodels\valve_controller_knu_ev2_0_left\*.json
//                                                                           "handgrip" (Index defines no openxr_grip,
//                                                                           so SteamVR falls back to its handgrip)
// Measured on the Vive Pro + Index test PC:
//
//                      origin (m, x y z)            rotate_xyz (deg)
//     Touch grip       (+-0.007, -0.0018, 0.1019)   (20.6, 0, 0)        +x is the controller's own side: left/right mirror
//     Index grip       ( 0.000,  -0.0270, 0.1400)   ( 9.7, 0, 0)        the same on both hands
//
// Both live in the SAME device frame, so the grip the rest of the mod expects is reachable from the grip we
// are handed by one fixed transform in grip-local space:
//
//     C = G_actual^-1 * G_touch        corrected = reported * C
//
// ---- HOW TO ADD ANOTHER CONTROLLER -------------------------------------------------------------------------
//   1. Find its render-model JSON under SteamVR (search the folder for "openxr_grip" and "handgrip").
//   2. Add a ControllerGripDef below: the OpenXR interaction profile path, and the left/right origin and
//      X rotation of its grip (use "openxr_grip" if present, otherwise "handgrip").
//   3. Add it to kControllerGrips. Controllers that already match Touch (the Touch family itself) need no entry.
//   If a controller's rotation has a Y or Z part, extend GripLocal -- the maths below only handles pitch
//   because every controller measured so far differs only in pitch and position.
//
// Pure functions, no OpenXR runtime calls: tools/vive_tests checks that the corrected Index grip equals
// the Touch grip exactly.

#include <openxr/openxr.h>
#include <cmath>
#include <cstring>
#include "Utils/XrMath.hpp"

namespace cvr::grip {

struct GripLocal {
    float origin[3];   // metres, in the controller's device frame
    float pitchDeg;    // rotation about the device X axis
};

// Where the mod's hand code was tuned: Touch / Touch Plus / Touch Pro (all identical in SteamVR's data).
// Index 0 = left, 1 = right.
inline constexpr GripLocal kTouchGrip[2] = {
    {{ 0.007f, -0.00182941f, 0.1019482f}, 20.6f},
    {{-0.007f, -0.00182941f, 0.1019482f}, 20.6f},
};

struct ControllerGripDef {
    const char* interactionProfile;   // OpenXR path reported by xrGetCurrentInteractionProfile
    const char* name;                 // for the log
    GripLocal grip[2];                // left, right
};

inline constexpr ControllerGripDef kControllerGrips[] = {
    {"/interaction_profiles/valve/index_controller", "Valve Index (Knuckles)",
     {{{0.0f, -0.027f, 0.14f}, 9.7f}, {{0.0f, -0.027f, 0.14f}, 9.7f}}},
};

inline XrQuaternionf PitchQuat(float degrees) {
    const float half = degrees * 3.14159265358979f / 360.0f;
    return {std::sin(half), 0.0f, 0.0f, std::cos(half)};
}

// C = G_actual^-1 * G_touch, as a pose in grip-local space.
inline XrPosef GripCorrection(const GripLocal& actual, const GripLocal& touch) {
    const XrQuaternionf qa = PitchQuat(actual.pitchDeg);
    const XrQuaternionf qaInv = ConjugateQuat(qa);
    const XrVector3f delta{touch.origin[0] - actual.origin[0], touch.origin[1] - actual.origin[1],
                           touch.origin[2] - actual.origin[2]};
    XrPosef c{};
    c.orientation = MultiplyQuat(qaInv, PitchQuat(touch.pitchDeg));
    c.position = RotateVector(qaInv, delta);
    return c;
}

// pose * correction: moves the point we track from the controller's grip to the Touch-equivalent grip.
inline XrPosef ApplyGripCorrection(const XrPosef& pose, const XrPosef& c) {
    const XrVector3f offset = RotateVector(pose.orientation, c.position);
    XrPosef out{};
    out.orientation = MultiplyQuat(pose.orientation, c.orientation);
    out.position = {pose.position.x + offset.x, pose.position.y + offset.y, pose.position.z + offset.z};
    return out;
}

// Returns the table entry for an interaction profile path, or nullptr when no correction is needed.
inline const ControllerGripDef* FindControllerGrip(const char* profilePath) {
    if (!profilePath) return nullptr;
    for (const auto& def : kControllerGrips)
        if (std::strcmp(def.interactionProfile, profilePath) == 0) return &def;
    return nullptr;
}

}  // namespace cvr::grip
