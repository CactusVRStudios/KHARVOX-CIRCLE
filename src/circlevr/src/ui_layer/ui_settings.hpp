#pragma once

// Settings of the UI layer (docs/rig-findings/ui-layer.md): the game's 2D target (HUD, menus, subtitles)
// captured every frame and shown on its own OpenXR quad instead of inside the eye images.

#include "common/vector.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace circlevr::ui_layer {

// KHARVOX_CIRCLE_HUD: where the HUD is (docs/VR_HANDS_HUD.md). `Panel`: the whole GUI target on the head-locked
// quad. `Wrist`: the corner blocks (health, armor, ammo, equipment) under the off hand's wrist while it
// faces the head, the rest (subtitles, prompts, boss bars, markers, damage) on the head-locked quad.
// `Weapon`: the ammo block (ammo, equipment, flame belch; with KHARVOX_CIRCLE_WEAPON_HUD_VITALS=1 health and
// armor too) above the gun in the weapon hand, the rest on the head-locked quad.
enum class HudMode : std::uint8_t { Panel, Wrist, Weapon };

const char* hudModeName(HudMode mode);

// The wrist HUD (ui_layer/wrist_hud.hpp).
struct WristSettings {
    // KHARVOX_CIRCLE_WRIST_ALWAYS=1: shown whenever the hand is tracked, whichever way the wrist turns.
    bool always = false;
    // KHARVOX_CIRCLE_WRIST_ANGLE: shown once the angle between the panel's normal and the direction to the head
    // is at most this; hidden again above it plus kWristHysteresisDegrees.
    float showDegrees = 40.0f;
    float hideDegrees = 55.0f;
    // KHARVOX_CIRCLE_WRIST_GAZE: and once the panel is at most this far from the middle of the view (the head's
    // forward); hidden again above it plus kWristHysteresisDegrees.
    float gazeShowDegrees = 40.0f;
    float gazeHideDegrees = 55.0f;
    // KHARVOX_CIRCLE_WRIST_FADE: fade-in time in seconds; the fade-out takes 1.5 times as long.
    float fadeInSeconds = 0.12f;
    float fadeOutSeconds = 0.18f;
    // KHARVOX_CIRCLE_WRIST_WIDTH: the width of the vitals + weapon row in metres.
    float widthMetres = 0.16f;
    // KHARVOX_CIRCLE_WRIST_OFFSET=x,y,z: the row's centre in the left hand's grip frame, metres (+X out of the
    // palm, +Y out of the thumb side, +Z toward the elbow); x is mirrored for the right hand.
    Vec3 offset{0.04f, 0.0f, 0.13f};
    // KHARVOX_CIRCLE_WRIST_ABILITIES: the crosshair's ability rings (cooldowns) above the row.
    bool abilities = true;
};
inline constexpr float kWristHysteresisDegrees = 15.0f;

// The weapon HUD (ui_layer/weapon_hud.hpp). The gun's frame is the viewmodel's: the weapon hand's grip
// position with its aim orientation (-Z along the barrel, +Y up, +X to the right).
struct WeaponHudSettings {
    // KHARVOX_CIRCLE_WEAPON_HUD_ANGLE: shown while the angle between the panel's normal and the direction to the
    // head is at most this; hidden above it plus kWristHysteresisDegrees (the panel seen edge-on or from
    // behind). There is no gaze test: like a sight on the gun, it is there whenever it faces the eyes.
    float showDegrees = 60.0f;
    float hideDegrees = 75.0f;
    // KHARVOX_CIRCLE_WEAPON_HUD_WIDTH: the width of the ammo block in metres (health and armor, when shown, at
    // the same scale beside it).
    float widthMetres = 0.10f;
    // KHARVOX_CIRCLE_WEAPON_HUD_OFFSET=x,y,z: the panel's centre in the right hand's gun frame, metres (+X right,
    // +Y up, +Z back toward the player); x is mirrored for the left hand.
    Vec3 offset{0.0f, 0.07f, 0.05f};
    // KHARVOX_CIRCLE_WEAPON_HUD_TILT: how far the panel's face turns from straight back along the barrel (0)
    // toward straight up (90), degrees; tilted toward the eyes above and behind a gun held at the chest.
    float tiltDegrees = 45.0f;
    // KHARVOX_CIRCLE_WEAPON_HUD_VITALS=1: health and armor on the gun too, left of the ammo as on screen.
    bool vitals = false;
};

// The comfort vignette (features/comfort/vignette.hpp): off, or how far it closes in.
enum class VignetteMode : std::uint8_t {
    Off,
    Light,
    Strong,
};

struct UiSettings {
    // KHARVOX_CIRCLE_UI_LAYER (default: on in Route S stereo).
    bool enabled = false;
    // KHARVOX_CIRCLE_UI_SKIP_COMPOSITE: no GUI composite in the eyes while the quad shows.
    bool skipComposite = true;
    // KHARVOX_CIRCLE_UI_DISTANCE: the quad's distance ahead of the head.
    float distanceMetres = 1.5f;
    // KHARVOX_CIRCLE_UI_WIDTH: the quad's width (its height follows the target's aspect).
    float widthMetres = 2.0f;
    // KHARVOX_CIRCLE_UI_OFFSET_Y: the quad's height offset (negative: lower).
    float offsetYMetres = 0.0f;
    // Under hand aim, a dot on the weapon hand's ray (the game's own crosshair marks the head's ray, so the
    // launcher hides it with +g_reticleMode 2): KHARVOX_CIRCLE_UI_RETICLE, its distance along the ray
    // (KHARVOX_CIRCLE_UI_RETICLE_DISTANCE) and angular diameter (KHARVOX_CIRCLE_UI_RETICLE_SIZE, degrees).
    bool reticle = true;
    float reticleDistanceMetres = 10.0f;
    float reticleDegrees = 1.0f;
    // Menus (docs/VR_MENUS.md): while the game shows its menu cursor, the menu is on a world-locked panel
    // placed in front of the head, and the controllers point at it with a laser (KHARVOX_CIRCLE_MENU_POINTER).
    // The panel's distance and width (KHARVOX_CIRCLE_MENU_DISTANCE, KHARVOX_CIRCLE_MENU_WIDTH) default to the UI
    // quad's, so the pause menu keeps its size when it turns world-locked; KHARVOX_CIRCLE_MENU_BEAM=0 leaves out
    // the beam and keeps only the dot.
    bool menuPointer = true;
    float menuDistanceMetres = 1.5f;
    float menuWidthMetres = 2.0f;
    bool menuBeam = true;
    // KHARVOX_CIRCLE_MENU_FOLLOW=0: the panel stays where it appeared even when the head turns away from it.
    bool menuFollow = true;
    // KHARVOX_CIRCLE_UI_CROP: the UI quad and the menu panel show only the 16:9 band of the GUI target the game's
    // screens are laid out in (wideContentRect), not the empty rows above and below it on a near-square eye
    // image (T-031).
    bool wideCrop = true;
    // KHARVOX_CIRCLE_UI_WASH: the full-screen additive wash (the red hit and low-health vignette) is taken out of
    // the GUI image before it goes on the HUD quad (additive_wash.hpp); 0 shows the image as the game drew
    // it.
    bool removeWash = true;
    // KHARVOX_CIRCLE_HUD=panel|wrist|weapon (panel by default). The wrist and the weapon need the controllers:
    // without them the whole HUD stays on the head-locked quad.
    HudMode hud = HudMode::Panel;
    WristSettings wrist;
    WeaponHudSettings weapon;
    // KHARVOX_CIRCLE_VIGNETTE (off, light, strong): the edges of the view darken while the stick moves or turns
    // the player or the game moves the camera (a dash, a glory kill); off by default.
    VignetteMode vignette = VignetteMode::Off;
};

// Looks up one environment variable: its text, or nullopt when unset.
using EnvLookup = std::function<std::optional<std::wstring>(std::wstring_view name)>;

// Reads the settings. A value that does not parse or lies outside its range keeps the default and adds a
// line to `warnings`. On by default in Route S stereo (KHARVOX_CIRCLE_MODE=stereo, no
// KHARVOX_CIRCLE_STEREO_EXPERIMENT), off otherwise; KHARVOX_CIRCLE_UI_LAYER=1 or 0 overrides.
UiSettings readUiSettings(const EnvLookup& env, std::vector<std::string>& warnings);

// The quad's size in metres for a target of `width` x `height` pixels: `widthMetres` wide, the height from
// the target's aspect. nullopt for an empty target.
struct PanelSize {
    float width = 0.0f;
    float height = 0.0f;
};
std::optional<PanelSize> panelSize(float widthMetres, std::uint32_t width, std::uint32_t height);

// The reticle image: `size` x `size` RGBA8, premultiplied alpha: a white dot with a dark ring (visible on
// light and dark scenes), transparent around it, the ring's outer edge touching the image's edge.
std::vector<std::uint8_t> reticleImage(std::uint32_t size);

// A rectangle of the GUI target, in pixels.
struct PixelRect {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// The rectangles to copy from a `width` x `height` GUI target so that a centred square of side
// `maskFraction` x `height` is left out (the game's crosshair, which marks the head's ray, under hand aim):
// the bands above and below it and the pieces left and right of it. One rectangle, the whole target, for a
// fraction of 0 or less.
std::vector<PixelRect>
copyRegionsWithoutCentre(std::uint32_t width, std::uint32_t height, float maskFraction);

// idSWF lays the game's screens out in a centred 16:9 frame fitted to the target's width: the menus, the
// HUD's corners and the subtitles sit inside it (docs/rig-findings/render-size.md, section 5; the UI captures
// at 2064x2100 and 2560x2100). The part of a `width` x `height` target that holds them: that band when the
// target is taller than 16:9 (its height rounded to whole pixels, centred), the whole target otherwise.
PixelRect wideContentRect(std::uint32_t width, std::uint32_t height);

// A point at (u, v) on a panel that shows `rect` of a `width` x `height` image, as (u, v) on the whole image
// (both 0..1, row 0 at the top).
struct ImageUv {
    float u = 0.0f;
    float v = 0.0f;
};
ImageUv contentToImage(float u, float v, const PixelRect& rect, std::uint32_t width, std::uint32_t height);

// The reticle quad's side in metres for an angular diameter of `degrees` at `distance` metres.
float reticleSideMetres(float distance, float degrees);

} // namespace circlevr::ui_layer
