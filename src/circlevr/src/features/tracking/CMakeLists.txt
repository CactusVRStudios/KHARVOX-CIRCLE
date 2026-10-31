# Tracking values checked before use: the headset's FOV over a session (fov_watch.hpp), tracked positions that
# jumped (pose_guard.hpp). No OpenXR or Windows here.
circle_vr_add_module(circle_vr_tracking
    SOURCES
        fov_watch.cpp
        pose_guard.cpp
    DEPENDS
        circle_vr_common
        circle_vr_xr_math)
