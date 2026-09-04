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

The first contact owns its interaction until release. UI always wins hit
testing; the movement region is considered only for touches that did not hit a
FrameXML control. A second finger can therefore move and operate the camera or
an action button at the same time without reclassifying the movement finger.
Application deactivation, focus loss, UI-mode changes and touch cancellation
all release movement, camera and UI capture explicitly.

## Mobile HUD

The right-side HUD is loaded only in iOS world sessions from the internal
`Interface/OpenWoW/MobileUI` TOC. It is never loaded by desktop builds.

- iPhone-sized layouts expose six 58-point action targets and a deliberate
  6+6 layer switch. The inactive layer is shown as a non-interactive preview,
  avoiding accidental activation through undersized controls.
- iPad-sized layouts expose all twelve actions as a 4-by-3 grid.
- Enemy, friendly target, interact and jump controls stay one tap away.
- Character, spellbook, talents, quests, map, bags, chat and system panels live
  in a compact utility drawer.
- Counts, usability tint, current-action highlight, cooldowns, current action
  page and target name are sourced from the normal game APIs.
- Interactive targets are at least 48 points and all fixed controls honor the
  current safe-area insets. Chinese labels are used for `zhCN`; other locales
  receive an English fallback.

The original FrameXML remains loaded and unchanged underneath the mobile HUD.
External keyboard, mouse/trackpad and controller routes stay active. Virtual
movement uses independent source ownership, so releasing one input device does
not cancel the same command still held by another.

## Device acceptance

Use the signed `ios-device-development` build with build-12340 Data already
synchronized into the application container. Validate both landscape
orientations and background/resume:

1. On login, realm and character screens, tap buttons, edit fields and scroll
   or drag controls; no duplicate click should occur.
2. In world, move diagonally and reverse direction, then release inside and
   outside the stick radius. The character must stop immediately.
3. Hold movement while dragging the camera, pinching zoom and activating an
   action. Each contact must retain its own function.
4. Tap an empty-world unit, long-press an interactable, and confirm a
   ground-target spell with a world tap.
5. Verify both iPhone action layers or all twelve iPad actions, including
   cooldown, count, usability and action-page changes.
6. Open every utility panel, operate the original FrameXML underneath, then
   repeat the core checks with external keyboard/mouse/controller input.
7. Background the application while moving or dragging the camera. Resume must
   not leave movement, freelook, pressed visuals or text input stuck.

The log records `iOS internal mobile interaction layer loaded` after the
internal TOC succeeds. A missing or invalid mobile layer is a world-UI startup
error rather than a silent fallback.
