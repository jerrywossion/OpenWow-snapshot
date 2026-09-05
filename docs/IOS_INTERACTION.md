# iOS Interaction

OpenWoW's iOS input is a native multi-touch path, not SDL's synthetic mouse
stream. UIKit is restricted to querying the device safe area and producing
haptic feedback. The game HUD remains FrameXML/Lua rendered by OpenWoW, so it
shares action, cooldown, target, secure-execution and panel behavior with the
desktop client.

## Touch map

| Surface | Gesture | Result |
| --- | --- | --- |
| Existing FrameXML | Tap or drag | Uses the frame's existing button, edit-box, slider and drag scripts |
| Empty lower-left world | Drag from any origin | Floating movement stick; vertical movement plus horizontal strafe |
| Empty world | Drag | Camera freelook |
| Empty world | Tap | Select or confirm the current ground-target action |
| Empty world | Hold for 475 ms, then release | Secondary/context interaction |
| Empty world | Two-finger pinch | Camera zoom |
| Mobile action button | Tap | Runs the corresponding action on the current action-bar page |
| Touch launcher beside the minimap | Tap | Hides or restores the entire mobile HUD, including the utility drawer and stick visual |

The first contact owns its interaction until release. UI always wins hit
testing; the movement region is considered only for touches that did not hit a
FrameXML control. A second finger can therefore move and operate the camera or
an action button at the same time without reclassifying the movement finger.
Application deactivation, focus loss, UI-mode changes and touch cancellation
all release movement, camera and UI capture explicitly.

## Mobile HUD

The right-side HUD is loaded only in iOS world sessions from the internal
`Interface/OpenWoW/MobileUI` TOC. It is never loaded by desktop builds.

- iPhone and iPad share a six-action thumb fan with a deliberate 6+6 layer
  switch. The first slot on each layer is a 68-point primary target; five
  56-point actions follow the right thumb's sweep. The layer button sits inside
  the fan and displays the active slot range, `1–6` or `7–12`. Switching layers
  preserves each slot's position and the normal action-bar page mapping.
- Jump and interact sit near the right edge; enemy and friendly targeting sit
  along the outer edge of the fan. All four remain one tap away.
- Character, spellbook, talents, quests, map, bags, chat and system panels live
  in a lower-right utility drawer with 64-point targets. Opening it replaces
  the combat fan, and the same menu button becomes Back. Selecting a panel
  closes the drawer. Its background absorbs touches between menu buttons.
- Counts, usability tint, current-action highlight, cooldowns, current action
  page and bindings use the normal game APIs. The original target frame supplies
  target information; there is no duplicate target caption or inactive-layer
  preview covering the world. Gaps between combat buttons remain available for
  camera gestures, without a solid panel behind the fan.
- A separate 48-point Touch/Hide launcher sits to the left of the minimap,
  clear of its lower-left encounter-journal launcher. It remains visible when
  the mobile root is hidden and falls back to the upper-right safe area if the
  minimap is hidden. Hiding the HUD closes the drawer; restoring it returns to
  the combat fan. `OpenWoWMobileHUDShown` uses the normal account saved-variable
  lifecycle, including logout and UI reload.
- Hiding the HUD changes presentation only: native FrameXML input, floating
  movement, camera, pinch and world taps retain their existing routing. Hidden
  HUD buttons cannot intercept touches, and HUD hiding does not cancel held
  movement or camera contacts. The stick visual is hidden with the HUD.
- Interactive targets are at least 48 points, labels are 11 points, and action
  counts are 12 points. Layout and text scale from device points, including the
  current safe-area insets, in both landscape orientations. Chinese labels are
  used for `zhCN`; other locales receive English labels.

The original FrameXML remains loaded and unchanged underneath the mobile HUD.
External keyboard, mouse/trackpad and controller routes stay active. Virtual
movement uses independent source ownership, so releasing one input device does
not cancel the same command still held by another.

## Device acceptance

Use the signed Release `ios-device-development` build at
`build/ios-device-development/apps/client/Release-iphoneos/OpenWoW.app`, with
build-12340 Data and the `zhCN` locale archive chain already synchronized into
the application container. Use the configured compatible realm and an in-world
character with actions assigned to slots 1–12. Validate both landscape
orientations on iPhone and iPad, plus background/resume. Static/build checks do
not establish device visual, interaction or comfort acceptance.

1. On login, realm and character screens, tap buttons, edit fields and scroll
   or drag controls; no duplicate click should occur.
2. In world, move diagonally and reverse direction, then release inside and
   outside the stick radius. The character must stop immediately.
3. Hold movement while dragging the camera, pinching zoom and activating an
   action. Each contact must retain its own function.
4. Tap an empty-world unit, long-press an interactable, and confirm a
   ground-target spell with a world tap.
5. Verify both action layers on iPhone and iPad, including cooldown, count,
   usability and action-page changes. Check that the thumb can move from the
   primary action to jump, the layer switch and the surrounding skills without
   triggering an adjacent button. Enemy/friendly targeting stays one tap away.
6. Tap the minimap's Hide/收起 launcher while the combat fan is shown, then
   repeat with the utility drawer open. All mobile visuals must disappear;
   operate the exposed original action bars, bags and panels. Touch/触控 must
   restore the fan with current icons and cooldowns, with the drawer closed.
   Repeat while another finger holds movement or camera drag: its ownership
   must remain unchanged. Log out/re-enter and reload UI to check the saved
   visibility choice. Hide/show the minimap and check the launcher remains
   reachable inside the safe area, without covering the journal launcher.
7. Open every utility panel. The drawer must replace the fan; tapping its gaps
   must not trigger world actions, and Back/返回 must restore the fan. Check
   original windows and dialogs with the HUD shown and hidden, then repeat
   the core checks with external keyboard/mouse/controller input.
8. Background the application while moving or dragging the camera. Resume must
   not leave movement, freelook, pressed visuals or text input stuck.

The log records `iOS internal mobile interaction layer loaded` after the
internal TOC succeeds. A missing or invalid mobile layer is a world-UI startup
error rather than a silent fallback.

For a failed device check, return `logs/openwow-client.log` from the resolved
iOS user-data root, covering the latest `log-start`/`Client startup` through the
failure, together with the device, orientation, HUD visibility/layer and steps.
Keep the first Lua/resource error and surrounding context, especially entries
mentioning `OpenWoWMobile`, `RegisterForSave`, `SetFont` or `SavedVariables`.
Device overlap, finger comfort, multi-touch timing and reload/resume behavior
remain user acceptance items until confirmed on this build.
