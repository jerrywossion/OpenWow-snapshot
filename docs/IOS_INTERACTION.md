# iOS Interaction

OpenWoW's iOS input is a native multi-touch path, not SDL's synthetic mouse
stream. UIKit is restricted to querying the device safe area and producing
haptic feedback. The game HUD remains FrameXML/Lua rendered by OpenWoW, so it
shares action, cooldown, target, secure-execution and panel behavior with the
desktop client.

## Touch map

| Surface | Gesture | Result |
| --- | --- | --- |
| FrameXML button or hyperlink | Short tap | Original left-button down/up; wait 260 ms after release only when a secondary action is supported |
| FrameXML button or hyperlink supporting right click | One-finger double tap on the same target | One right-button down/up on the second release; no preceding left click |
| FrameXML button or hyperlink | Hold for 475 ms without moving | Hover/tooltip only; release leaves a small Click / Right click / Close toolbar |
| Registered draggable frame | Hold for 475 ms, then move | Original left-button `OnDragStart`; release delivers `OnDragStop` and `OnReceiveDrag` |
| Slider, edit box, title region, model or color picker | Touch and drag | Immediate native pointer interaction, including text selection and slider updates |
| UI with a mouse-wheel owner | Vertical swipe before the hold threshold | Scroll through the original wheel handler |
| Visible movement stick | Press the control and move from its center | Forward/backward plus horizontal strafe, with a 20% axis dead zone; retains capture outside the control until release |
| Empty world | Drag | Camera freelook |
| Empty world | Tap | Select or confirm the current ground-target action after the 260 ms double-tap window |
| World exposed outside UI controls | One-finger double tap in the same location | Direct right-click interaction on the second release |
| World exposed outside UI controls | Hold for 475 ms, then release | Keep the world hover and show the same explicit-action toolbar |
| Empty world | Two-finger pinch | Camera zoom |
| Mobile action button | Tap | Runs the corresponding action on the current action-bar page |
| Touch launcher beside the minimap | Tap | Hides or restores the entire mobile HUD, including the utility drawer and movement control; hiding releases held movement |

Movement starts only on the visible, mouse-enabled joystick frame. The internal
HUD explicitly marks that control with `__ow_touch_movement`; the native input
router resolves the ordinary topmost frame hit, respecting UI scale, safe-area
layout, visibility, clipping and occluding panels. There is no percentage-based
movement region. Pressing near the stick center stays in the dead zone;
pressing or dragging toward its edge moves in that direction. Input displacement
uses the resolved frame center and size, while the knob stays inside its base.

The movement capture is separate from the UI pointer, so skills, panels and
camera gestures can use another finger in either arrival order. A second finger
on the occupied stick is consumed. A finger beginning in the world keeps its
world/camera role even if it subsequently crosses the stick. A movement finger
keeps its role outside the stick until release; it never turns into a world click.
Hiding the control or an ancestor, disabling its mouse input or releasing its
Lua binding cancels movement immediately through the frame input lifecycle.
Restoring the HUD does not recapture a finger still down from before hiding.
Application deactivation, focus loss, UI-mode changes and touch cancellation
all release movement, camera and UI capture explicitly.

## Right click, inspect and drag

One-finger double tapping is the direct secondary action for UI and world
targets. Tap and release, then touch the same target again within 260 ms and
release within another 260 ms. The second touch must start within 24 device
points of the first. UI also requires the same frame reference and hyperlink
identity, so neighboring items or links do not form a double tap. The original
right-button down/up or world right-click path runs exactly once on the second
release. The first tap is consumed; it never activates the target first.
Two simultaneous fingers no longer mean right click; world pinching remains
camera zoom.

A single tap on a UI target supporting right click waits for that recognition
window before sending the original left-button down/up. Left-only controls,
including the mobile HUD's skills, jump, target buttons and minimap launcher,
respond on release without this extra wait. Rapidly tapping them keeps sending
left clicks. Secondary support comes from the original registered click phases,
raw mouse handlers or hyperlink handlers, not guesses about a page or item.
World selection and ground-target confirmation also wait 260 ms; double tapping
uses the normal world right-click behavior, including cancelling targeting.

The HUD's Use/交互 button invokes `InteractUnit("target")` and therefore acts on
the selected target. Its tooltip explains the separate world gesture. To gather
a quest object, open a chest or use a world object that cannot be selected,
double tap the visible object itself. The world click picks a GUID under the
touch and passes it directly to object interaction; it does not first select it
or substitute the current target. GameObject activation retains the existing
range, lock/skill, quest and server checks and the normal game-object use/report
requests. The same path works with no target or with an unrelated unit selected.

A second touch on a different target commits the previous single tap, then
starts an independent gesture. If that first action changes the next target's
visibility or hit ownership, the next contact is consumed. For a matching
second touch, moving beyond the existing slop cancels the click or begins an
eligible scroll; holding for 475 ms enters inspection and can continue into a
registered drag. Neither path sends the first tap. Releasing the second touch
between the double-tap and inspection thresholds cancels it without a click.

The input router owns one pending completed tap shared by UI and world input.
SDL event timestamps determine touch duration and double-tap matching; the UI
update uses the same clock to expire single taps. Processing a queued event
batch after a slow frame does not make distant taps into a double tap. Pending
work is removed before invoking Lua and rechecks frame identity, visibility,
hit ownership and hyperlinks before dispatch. Its hardware-action scope comes
only from the completed physical tap, never from a hover or an arbitrary timer.
Focus loss, rotation, UI reload, target teardown and mouse takeover discard
pending taps. Existing movement, camera, pinch and direct-control ownership is
preserved; starting a camera drag/pinch cancels its world tap. Pressing the
movement control commits an earlier completed single tap as a separate input,
then revalidates the control before capturing the movement finger.

Long-press inspection never sends a left-button down, click or right-button
action. This matters for buttons registered for down-clicks: inspecting an
item must not equip/use it, and inspecting a spell must not cast it. Short taps
still use the original down/up sequence after recognition, with immediate
pressed feedback while the finger is down. An early move beyond 9 device points
cancels the tap or scrolls an eligible UI; it does not rearrange action bars accidentally.
Continuous native controls (sliders, edit boxes, title regions, model views
and color pickers) keep their immediate pointer path.

After a hold is released, the contextual toolbar still offers explicit primary
and secondary clicks as a one-finger alternative. Right click is disabled when
the frame has neither a registered right-click phase nor an applicable
pointer/hyperlink handler. It does not guess labels such as Equip, Sell or Cast
from the current page: those actions continue to be decided by the original scripts. Clicking outside the
toolbar dismisses it and consumes that contact; Close also leaves without
executing an action. The toolbar works with the combat HUD hidden and stays
inside the shared safe area. Mobile action icons now supply normal action
tooltips too.

For a drag, keep the inspecting finger down and move it; do not release into
the toolbar first. Only frames with a registered left drag and an
`OnDragStart` handler enter this path. The router publishes actual drawable
cursor coordinates and the left-button input context before invoking the
existing drag handlers. Original action-bar locking, pickup restrictions,
secure execution, cursor payloads and drop handling remain authoritative.
Unsupported drags cancel without firing a click. Cancellation ends an active
drag without delivering `OnReceiveDrag` to an accidental destination.

The input router owns the pending gesture and inspected target. UI update and
incoming touch events advance gesture recognition; draw publication does not
advance a gesture. Hover itself supplies no protected-action grant. Toolbar
activation finishes its own pointer-up capture before dispatching a separate click to the
inspected target, avoiding reentrant capture replacement. Frame identity,
visibility, hit ownership and hyperlink identity are checked again before
dispatch, so an expired or covered UI target is not replaced by whatever is
now underneath it. World inspection follows the live pointer position, as
desktop world hover does. Focus loss, rotation and UI teardown clear inspection
and the toolbar; an actual mouse movement takes over from touch inspection.

## Mobile HUD

The mobile HUD is loaded in iOS world sessions from the internal
`Interface/OpenWoW/MobileUI` TOC. macOS can opt into the same layer with the
`mobileHudPreview` CVar; it is off by default on macOS. Other desktop builds do
not load it.

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
- A visible 118-point movement stick rests at the lower left, 32 points inside
  the safe root. Its 54-point knob returns to center on release. Hiding the HUD
  releases and disables this control; its former position becomes ordinary
  UI/world input. Native FrameXML, camera, pinch and world taps remain available,
  and an existing camera contact continues. Showing the HUD requires a fresh
  press on the stick before movement can resume.
- Interactive targets are at least 48 points, labels are 11 points, and action
  counts are 12 points. Layout and text scale from device points, including the
  current safe-area insets, in both landscape orientations. Chinese labels are
  used for `zhCN`; other locales receive English labels.

The original FrameXML remains loaded and unchanged underneath the mobile HUD.
On iOS, the platform publishes the drawable size and UIKit safe-area insets
together to the world UI layout. `UIParent` occupies that safe rectangle, so
the original player/target frames, chat, action bars, minimap and add-ons
anchored to it all avoid the notch and home indicator. Screen-clamped frames
and saved/dragged positions use the same bounds. The world viewport and
world-input frame still cover the full drawable; UI scale is based on that
full drawable too, so adding insets does not shrink fonts or buttons. Insets
survive UI reload and update with orientation/window changes even when the
mobile HUD is hidden. Frames deliberately positioned outside `UIParent` or
anchored to the world viewport need their own presentation policy.

The mobile HUD adds its existing 14-point spacing inside this shared root;
it does not apply the UIKit insets a second time. The joystick anchors to the
same root and hit testing consumes its resolved drawable rectangle.
External keyboard, mouse/trackpad and controller routes stay active. Virtual
movement uses independent source ownership, so releasing one input device does
not cancel the same command still held by another.

## Iterate the HUD on macOS

Build and install the normal Release application using [BUILDING.md](BUILDING.md).
In world, enable the preview with these two chat commands:

```text
/console mobileHudPreview 1
/reload
```

The setting is saved in the normal client configuration. To disable it, run
`/console mobileHudPreview 0` followed by `/reload`. The switch is consumed
when the world UI starts or reloads; it does not create or destroy frames in
the middle of a Lua callback. iOS continues to load its HUD unconditionally.
The existing reload lifecycle recreates FrameXML/Lua while retaining the
current world session, so preview changes do not require another login or
world load.

For source iteration, close the running client, then start the installed app
with the existing override-root environment variable. From the repository
root, run:

```sh
OPENWOW_ENHANCED_ASSETS="$PWD/assets/overrides" \
  ./build/macos-app/OpenWoW.app/Contents/MacOS/openwow-client
```

Edit `assets/overrides/Interface/OpenWoW/MobileUI/OpenWoWMobileUI.lua`, save,
and run `/reload` in game. The UI loader starts a fresh source-cache lifetime
and reads the changed file from the mounted directory; Lua, XML and TOC edits
do not require a build or reinstall. The environment variable must point to
the whole `assets/overrides` root, not its `MobileUI` subdirectory. Without it,
an installed app reads its packaged overrides, which change only after an
install. C++ changes still require compilation and restarting the app.

Use a landscape window sized in macOS logical points, for example about
844 by 390 for a phone layout or 1194 by 834 for a tablet layout. HUD sizes use
those window points, while drawing and mouse hit testing use framebuffer
pixels; Retina scaling is accounted for. Resizing and UI reload republish
the viewport metrics. Desktop safe-area insets default to zero. With preview
enabled, `/console mobileHudPreviewSafeInset 44` simulates a uniform 44-point
inset on all four sides, including the original HUD. Changes apply without a
reload; set it to `0` to restore the full window. This setting is macOS-only
and does not replace the device's actual safe area. This previews the
shared layout, labels, action layers, cooldowns and panel interactions;
device safe areas, multi-touch ownership, haptics and thumb reach still need
an iOS device. Desktop keyboard and mouse keep their usual behavior.

For user acceptance, use `build/macos-app/OpenWoW.app` with build-12340 Data,
the `zhCN` locale archive chain and the configured compatible realm. In world,
enable the preview and verify that the HUD appears; check both action layers,
the utility drawer and Touch/Hide, resize the window, then change a Lua label
or spacing and reload to confirm the source edit appears without leaving the
world. Disable and reload to verify the mobile frames disappear. If the HUD
was previously hidden, restore it with the minimap's Touch/触控 launcher.

The log should contain `macOS mobile HUD preview loaded (mobileHudPreview=1)`
after each enabled UI start. Verify that `Override content root:` names the
source directory for live editing. For failures, return the latest session
and reload span from the resolved user-data root's `logs/openwow-client.log`
(normally `~/Library/Application Support/OpenWoW/logs/openwow-client.log`),
including the first `OpenWoWMobile` or FrameXML/Lua error and its context.
These visual, interaction and reload checks remain user acceptance items.

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
2. In world, press the visible stick center, then drag diagonally, reverse
   direction and return to center. The center must stop movement; a directional
   press near the edge must start it. Release inside and outside the stick:
   movement must stop immediately, without a world click. Drag from exposed
   world just outside every stick edge: expect camera motion, never movement,
   even when the finger later crosses the stick. Put another UI window over
   the stick and verify the covering control receives input. Start with the
   camera/skill finger first and repeat in the opposite order. A second finger
   on the occupied stick must not steal movement or trigger a click/tooltip.
3. Hold movement while dragging the camera, pinching zoom and activating an
   action. Each contact must retain its own function.
4. Tap a world unit and confirm a ground-target spell with a short world tap.
   Double tap an NPC/object, including exposed lower-left world: expect right-click
   interaction on the second release, with no toolbar or preliminary left-click
   selection/cast. Double tap with a ground-target spell active to check the
   normal right-click cancel. A single tap waits about 260 ms before selection
   or ground-target confirmation. Pinching must never produce a right click.
   Reload/background between taps and during the second touch: releasing the
   old contact must not trigger an action in the resumed/reloaded UI.
   Hold an interactable in any exposed world area, then release: no action
   should occur until Right click/右键 is tapped in the toolbar. Verify the
   normal NPC/object interaction, then repeat with Close/关闭 and outside
   dismissal. Camera drag and two-finger pinch must not open that toolbar.
   Find a quest gathering object that cannot become the selected target. Stand
   within use range, clear the target and double tap the object itself; expect
   its normal use/loot/cast response and the corresponding quest progress after
   the server responds. Repeat with an unrelated NPC selected, with the object
   in the lower-left region, and with the mobile HUD hidden. The selected NPC
   must not receive the object's interaction. Repeat with an ordinary chest or
   another usable GameObject, and check an out-of-range interaction still follows
   the existing approach/error behavior. A scene model obscured by a UI control
   remains a UI hit; hide the mobile HUD or move the camera to expose it.
5. Verify both action layers on iPhone and iPad, including cooldown, count,
   usability and action-page changes. Check that the thumb can move from the
   primary action to jump, the layer switch and the surrounding skills without
   triggering an adjacent button. Enemy/friendly targeting stays one tap away.
6. Tap the minimap's Hide/收起 launcher while the combat fan is shown, then
   repeat with the utility drawer open. All mobile visuals must disappear;
   operate the exposed original action bars, bags and panels. Touch/触控 must
   restore the fan with current icons and cooldowns, with the drawer closed.
   Repeat while another finger holds movement: hiding must stop it immediately;
   showing again while that finger stays down must not restart movement or
   make its release click the world. Drag over the former stick position while
   hidden: expect camera motion, with no invisible movement zone. Repeat during
   camera drag: that camera contact must continue. Reload while holding the
   stick: the old finger must stop and stay inactive through its release.
   Log out/re-enter and reload UI to check the saved visibility choice.
   Hide/show the minimap and check the launcher remains
   reachable inside the safe area, without covering the journal launcher.
7. Open every utility panel. The drawer must replace the fan; tapping its gaps
   must not trigger world actions, and Back/返回 must restore the fan. Check
   original windows and dialogs with the HUD shown and hidden, then repeat
   the core checks with external keyboard/mouse/controller input.
8. Background the application while moving or dragging the camera. Resume must
   not leave movement, freelook, pressed visuals or text input stuck.
9. On a notched iPhone, check the original player/target frames, chat, action
   bars, minimap and opened panels with both landscape orientations. The HUD
   should move inside the current safe area while the world continues behind
   it to the screen edges. Repeat with the touch HUD hidden and after `/reload`;
   check that touch hit targets and joystick visuals stay aligned. Drag a
   screen-clamped panel to each edge and reload to check its saved position.
   Repeat the drag/reload check with a custom UI scale enabled.
   On iPad, confirm that zero side insets do not create artificial side gaps.
10. Hold an inventory item, a spell/action button, a unit frame and a chat
    hyperlink. Inspect their normal tooltip without activating them; after
    release, check explicit left/right actions and both dismissal paths.
    Include a button registered for `LeftButtonDown`: only a short tap or an
    explicit toolbar click may trigger it. Repeat with the combat HUD hidden.
    Double tap the same targets and check their original right-click behavior
    exactly once, never a preliminary `LeftButtonDown`. Single tap and wait:
    expect one left click, with no repeat on later updates. Tap neighboring
    items/links quickly: expect separate left clicks, never a right click on
    either. Rapidly tap the left-only mobile skill/jump buttons: every release
    must respond immediately. Repeat during held movement, with custom UI scale
    and with the combat HUD hidden. Hold or move the matching second touch:
    expect inspection/scroll/drag, with no first-tap action. Hide or replace a
    queued target, reload or background before the timeout: no delayed action
    may reach a newly exposed control. Try double taps during a slow frame and
    check the recognition window remains tied to the physical touch timing.
    If a deferred click focuses an edit box, the system keyboard must appear
    without requiring another touch.
11. With normal pickup permissions and action-bar locking configured for the
    intended drag, hold a draggable item/action then move to a valid slot.
    Verify pickup and drop exactly once, with no click/cast afterwards. Moving
    before the hold threshold must not start that drag. Cancel a drag by
    backgrounding or rotating and check that no unintended drop occurs.
12. Swipe a scrollable list/chat view; drag a slider, text selection, title
    region and model control. Check hit/cursor alignment at device scale,
    including during simultaneous movement with the other finger. Hide or
    replace the inspected frame before toolbar activation: it must cancel
    rather than click the newly exposed control. Reload/background while a
    tooltip or toolbar is pinned and verify it is cleared.

The log records `iOS internal mobile interaction layer loaded` after the
internal TOC succeeds. A missing or invalid mobile layer is a world-UI startup
error rather than a silent fallback.
`Touch movement cancelled` records control/lifecycle cancellation with the frame,
input source and reason. Include it for hidden-HUD, reload or stuck-movement
failures; normal finger release does not require a cancellation entry.
`HUD viewport safe insets` records changed drawable dimensions and the
left/top/right/bottom insets in framebuffer pixels. Include those entries when
reporting notch, rotation or hit-target alignment failures.

`World secondary click` records each dispatched coordinate-based right click,
the drawable touch coordinates, hit kind, picked GUID and separately selected
GUID. `hit=gameobject` with a nonzero picked GUID confirms model picking reached
object interaction; it does not prove server acceptance. `hit=terrain` or
`hit=none` means that click did not pick the object. If this entry is absent,
include preceding `Touch action` cancellations. Existing debug-level
`interaction send CMSG_GAMEOBJ_USE` / `CMSG_GAMEOBJ_REPORT_USE` entries, when
present, identify outgoing object requests. For a gathering failure include
the quest/object name, any use/range/skill error, whether loot or casting began,
and whether the quest counter changed.

For a failed device check, return `logs/openwow-client.log` from the resolved
iOS user-data root, covering the latest `log-start`/`Client startup` through the
failure, together with the device, orientation, HUD visibility/layer and steps.
Keep the first Lua/resource error and surrounding context, especially entries
mentioning `OpenWoWMobile`, `RegisterForSave`, `SetFont` or `SavedVariables`.
For double-tap/inspection/drag failures also include `Touch action`,
`source=double-tap`, `source=single-tap-timeout`, `Touch context`, `Touch drag`,
`Touch scroll`, and the first originating FrameXML/Lua error. Record whether
the failure occurred on the first/second tap or release, at the single-tap
timeout, before the hold, during inspection, on toolbar activation, at drag
start, on drop or on cancellation.
Device overlap, finger comfort, multi-touch timing and reload/resume behavior
remain user acceptance items until confirmed on this build.
