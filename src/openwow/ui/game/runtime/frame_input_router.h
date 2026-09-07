#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "openwow/game/actions/macros/application/macro_execution_runtime.h"
#include "openwow/ui/game/framescript/widgets/edit_box_state.h"
#include "openwow/ui/game/runtime/retained_layout.h"

struct lua_State;

namespace openwow::ui::game::runtime {

class FrameStore;
class FrameTraversalIndex;

class FrameInputRouter final {
public:
  using RunningMacroInputButtonProvider =
      std::function<std::optional<openwow::game::actions::macros::MacroInputButton>()>;

  FrameInputRouter(FrameStore &frames, FrameTraversalIndex &traversal, RetainedLayout &layout);
  FrameInputRouter(const FrameInputRouter &) = delete;
  FrameInputRouter &operator=(const FrameInputRouter &) = delete;

  void BindLuaState(lua_State *state) noexcept;
  void Reset() noexcept;

  bool HandleMouseDown(float x, float y, int button);
  bool HandleMouseUp(float x, float y, int button);
  bool HandleMouseButtonDownByFlag(float x, float y, std::uint32_t button_flag);
  bool HandleMouseButtonUpByFlag(float x, float y, std::uint32_t button_flag);
  bool HandleMouseMove(float x, float y);
  bool HandleTouchDown(float x, float y, float pixels_per_point_x = 1.0F,
                       float pixels_per_point_y = 1.0F);
  bool HandleTouchMove(float x, float y);
  bool HandleTouchUp(float x, float y);
  [[nodiscard]] bool HitTestTouchTarget(float x, float y);
  void CancelTouch();
  void UpdateTouchInspection();
  void PreviewWorldTouch(float x, float y);
  void DismissWorldTouch();
  void ShowWorldTouchContext(float x, float y,
                             std::function<void(std::uint32_t)> action);
  bool HandleMouseWheel(float x, float y, float delta);
  bool HandleKeyDown(std::uint32_t key, bool shift_down = false, bool ctrl_down = false);
  bool HandleKeyUp(std::uint32_t key);
  bool HandleTextInput(const char *text);

  void BeginHyperlinkHitTestFrame();
  void MarkMouseFocusDirty() noexcept;
  void ReplayMouseFocusIfDirty();
  void AddHyperlinkHitRegion(std::string frame_name, float left, float top, float right,
                             float bottom, std::string link, std::string text);

  void SetFocus(const std::string &frame_name);
  void ClearFocus();
  void ClearFocusIfOwnedBy(std::string_view frame_name);
  [[nodiscard]] const std::string &focused_frame_name() const noexcept {
    return focused_frame_;
  }
  [[nodiscard]] const std::string &mouseover_frame_name() const noexcept {
    return mouseover_frame_;
  }

  [[nodiscard]] bool mouseover_is_world_frame() const noexcept;
  bool PushFocusedFrame(lua_State *state) const;
  bool PushMouseoverFrame(lua_State *state) const;
  [[nodiscard]] std::optional<std::string> ResolveModifiedMouseoverUnitToken();
  void SetRunningMacroInputButtonProvider(RunningMacroInputButtonProvider provider);

  bool BeginFrameMoveSizing(const std::string &name, int mode);
  bool StopFrameMoveSizing(const std::string &name);

  void QueueEditBoxStateUpdate(int lua_ref);
  void UpdateFocusedEditBoxInputLanguage();
  void FlushPendingEditBoxStateUpdates();

  void UpdateFocusedEditBoxCaretBlink(float dt);

  void SetApplicationActive(bool active);
  [[nodiscard]] bool application_active() const noexcept {
    return application_active_;
  }

  [[nodiscard]] bool FocusedFrameIsEffectivelyVisible() const;
  void ReconcileFrameInputMutation(std::string_view frame_name,
                                   bool focused_was_effectively_visible);
  void BeforeFrameBindingRelease(int lua_ref);
  void AfterFrameIdentityRelease(std::string_view frame_name);

private:
  struct KeyboardCaptureState {
    std::string frame_name;
    bool dispatch_key_handlers{false};
  };

  struct MouseButtonCaptureState {
    std::string frame_name;
    std::uint32_t button_flag{0};
    float start_x{0.0F};
    float start_y{0.0F};
    bool active{false};
    bool drag_started{false};

    bool is_slider{false};
    std::string hyperlink_link;
    std::string hyperlink_text;
  };

  struct HyperlinkHitRegion {
    std::string frame_name;
    float left{};
    float top{};
    float right{};
    float bottom{};
    std::string link;
    std::string text;
  };

  struct TouchTarget {
    std::string frame_name;
    int lua_ref{-2};
    float x{};
    float y{};
    std::string hyperlink_link;
    std::string hyperlink_text;
  };

  enum class TouchPhase { kPending, kDirect, kInspect, kDrag, kScroll, kCancelled };
  struct TouchGesture {
    TouchTarget target;
    TouchPhase phase{TouchPhase::kPending};
    std::uint32_t started_at_ms{};
    float pixels_per_point_x{1.0F};
    float pixels_per_point_y{1.0F};
    float current_x{};
    float current_y{};
    float scroll_y{};
    bool can_drag{false};
    std::optional<TouchTarget> scroll_target;
  };

  struct TouchContext {
    TouchTarget target;
    std::string presentation_frame;
    std::function<void(std::uint32_t)> world_action;
  };

  [[nodiscard]] bool TouchTargetIsCurrent(const TouchTarget& target);
  void SetTouchCursorPosition(float x, float y);
  void PublishTouchHover(float x, float y);
  void ClearTouchHover();
  void ClearTouchContext();
  void PresentTouchContext();
  void DispatchTouchContextAction();
  bool BeginTouchDrag();

  static std::size_t ButtonCaptureIndex(std::uint32_t button_flag) noexcept;
  MouseButtonCaptureState *FindCapture(std::uint32_t button_flag) noexcept;
  bool HandlePointerDownByFlag(float x, float y,
                               std::uint32_t button_flag, bool touch);
  bool HandlePointerUpByFlag(float x, float y,
                             std::uint32_t button_flag, bool touch);
  bool HandlePointerMove(float x, float y, bool touch);
  void CancelPointerCapture(std::uint32_t button_flag);
  void TransitionKeyboardFocus(const std::string &new_frame_name);
  void QueueEditBoxCaretRefresh(const std::string &frame_name);
  [[nodiscard]] EditBoxRegionPorts BuildEditBoxRegionPorts(
      std::string_view edit_box_key);
  void MoveEditBoxCursorToPoint(const std::string &frame_name, float x,
                                float y, bool extend);
  void PlaceEditBoxCursorFromClick(const std::string &frame_name, float x,
                                   float y);
  bool PushTrackedFrame(lua_State *state, std::string_view frame_name) const;
  void RebuildTraversalIfDirty();
  void RefreshMouseFocusAt(float x, float y, bool motion);
  [[nodiscard]] const HyperlinkHitRegion *FindHyperlinkAt(float x, float y) const;
  [[nodiscard]] bool FramesShareInputHierarchy(std::string_view first,
                                               std::string_view second) const;
  [[nodiscard]] const HyperlinkHitRegion *ResolveHyperlinkAt(std::string_view ordinary_hit, float x,
                                                             float y) const;
  void UpdateHoveredHyperlink(float x, float y);
  void UpdateSliderValueFromCursor(const std::string &frame_name, float x, float y);
  [[nodiscard]] float viewport_height() const noexcept;
  [[nodiscard]] float root_scale() const noexcept;

  FrameStore &frames_;
  FrameTraversalIndex &traversal_;
  RetainedLayout &layout_;
  lua_State *lua_{nullptr};

  std::string focused_frame_;
  std::string mouseover_frame_;
  std::unordered_map<std::uint32_t, KeyboardCaptureState> keyboard_captures_;
  std::array<MouseButtonCaptureState, 31> mouse_button_captures_{};
  std::unordered_map<std::string, std::uint32_t> button_last_click_time_ms_;
  RetainedLayout::MoveSizingSession active_move_sizing_;
  float last_mouse_x_{0.0F};
  float last_mouse_y_{0.0F};
  bool have_last_mouse_position_{false};
  bool mouse_focus_dirty_{true};
  std::uint32_t pressed_button_mask_{0};
  std::vector<HyperlinkHitRegion> hyperlink_hit_regions_;
  std::optional<HyperlinkHitRegion> hovered_hyperlink_;

  std::vector<int> pending_edit_box_updates_;
  std::vector<int> processing_edit_box_updates_;
  std::unordered_set<int> pending_edit_box_update_refs_;

  std::string edit_box_drag_select_frame_;

  bool application_active_{true};
  bool touch_capture_active_{false};
  std::optional<TouchGesture> touch_gesture_;
  std::optional<TouchContext> touch_context_;
  std::optional<std::uint32_t> pending_touch_context_action_;
  bool touch_pointer_active_{false};
  RunningMacroInputButtonProvider running_macro_input_button_provider_;
};

}
