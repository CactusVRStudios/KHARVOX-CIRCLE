# Comfort options: the vignette for artificial motion and how glory kills are shown. No OpenXR or Windows here.
circle_vr_add_module(circle_vr_comfort
    SOURCES
        glory_kill.cpp
        vignette.cpp
    DEPENDS
        circle_vr_common)
