# bHaptics suits and sleeves (docs/BHAPTICS.md): the effects and the Player's messages. No Windows or network
# here; the layer owns the connection.
circle_vr_add_module(circle_vr_bhaptics
    SOURCES
        ammo.cpp
        bhaptics_message.cpp
        body_haptics.cpp
        crystal.cpp
        landing.cpp
        launch.cpp
        pickup_frames.cpp
        pickups.cpp
    DEPENDS
        circle_vr_common
        circle_vr_input)
