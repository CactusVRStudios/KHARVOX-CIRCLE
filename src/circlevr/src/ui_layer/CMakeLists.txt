# The UI layer (docs/rig-findings/ui-layer.md): the logic that needs no engine, Vulkan or OpenXR (settings,
# the layout tracker, the checks on the game's GUI target), so it is tested on every platform.
circle_vr_add_module(circle_vr_ui_layer
    SOURCES
        additive_wash.cpp
        backdrop.cpp
        gui_target.cpp
        hud_regions.cpp
        layout_tracker.cpp
        motion_target.cpp
        ui_settings.cpp
        weapon_hud.cpp
        wrist_hud.cpp
    DEPENDS
        circle_vr_common)
