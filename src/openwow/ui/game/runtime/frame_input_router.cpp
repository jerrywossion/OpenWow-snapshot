#include "openwow/ui/game/runtime/frame_input_router.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <lua.hpp>
#include <utility>

#include "openwow/foundation/text/ascii.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/game/actions/bindings/adapters/lua/binding_script_executor.h"
#include "openwow/input/input_manager.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/framescript/core/frame_draw_layer_state.h"
#include "openwow/ui/game/framescript/core/frame_runtime_identity.h"
#include "openwow/ui/game/framescript/core/frame_script_invocation.h"
#include "openwow/ui/game/framescript/core/script_region_ownership.h"
#include "openwow/ui/game/framescript/widgets/button_method_support.h"
#include "openwow/ui/game/framescript/widgets/edit_box_state.h"
#include "openwow/ui/game/lua_cpu_profiler.h"
#include "openwow/ui/game/lua_mouse_button_context.h"
#include "openwow/ui/game/runtime/frame_store.h"
#include "openwow/ui/game/runtime/lua_interned_field_key.h"
#include "openwow/ui/game/runtime/frame_traversal_index.h"
#include "openwow/ui/game/secure_execution.h"
#include "openwow/ui/runtime/security/protected_action_gate.h"
#include "openwow/ui/widgets/simple_edit_box.h"
#include "openwow/ui/widgets/simple_frame.h"
#include "openwow/ui/widgets/simple_minimap.h"

namespace openwow::ui::game::runtime {
namespace {

constexpr float kDragThresholdSquared = 100.0F;
constexpr char kRegisteredDragButtonMaskField[] = "__ow_registered_drag_button_mask";
constexpr std::uint32_t kTouchInspectMilliseconds = 475u;
constexpr float kTouchSlopPoints = 9.0F;
constexpr float kTouchScrollPoints = 24.0F;

template <typename PushArguments>
bool FireFrameHandler(lua_State *state, int frame_ref, const char *handler, int argument_count,
                      PushArguments &&push_arguments) {
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, frame_ref);
  if (lua_istable(state, -1) == 0) {
    lua_settop(state, top);
    return false;
  }
  const int frame_index = lua_absindex(state, -1);
  push_arguments();
  const auto invocation = InvokeFrameScriptHandler(state, frame_index, handler, argument_count);
  if (invocation.status != LUA_OK) {
    lua_pop(state, 1);
  }
  lua_settop(state, top);
  return invocation.invoked && invocation.status == LUA_OK;
}

bool FireNoArg(lua_State *state, int frame_ref, const char *handler) {
  return FireFrameHandler(state, frame_ref, handler, 0, [] {});
}

bool FireString(lua_State *state, int frame_ref, const char *handler, std::string_view argument) {
  return FireFrameHandler(state, frame_ref, handler, 1,
                          [&] { lua_pushlstring(state, argument.data(), argument.size()); });
}

bool FireBoolean(lua_State *state, int frame_ref, const char *handler, bool argument) {
  return FireFrameHandler(state, frame_ref, handler, 1,
                          [&] { lua_pushboolean(state, argument ? 1 : 0); });
}

bool FireButton(lua_State *state, int frame_ref, const char *handler, const char *button_name) {
  return FireFrameHandler(state, frame_ref, handler, 1,
                          [&] { lua_pushstring(state, button_name); });
}

bool FireHyperlink(lua_State *state, int frame_ref, const char *handler, std::string_view link,
                   std::string_view text) {
  return FireFrameHandler(state, frame_ref, handler, 2, [&] {
    lua_pushlstring(state, link.data(), link.size());
    lua_pushlstring(state, text.data(), text.size());
  });
}

bool FireHyperlinkClick(lua_State *state, int frame_ref, std::string_view link,
                        std::string_view text, const char *button_name) {
  return FireFrameHandler(state, frame_ref, "OnHyperlinkClick", 3, [&] {
    lua_pushlstring(state, link.data(), link.size());
    lua_pushlstring(state, text.data(), text.size());
    lua_pushstring(state, button_name);
  });
}

void SetFrameHighlightForMouseFocus(lua_State *state, int frame_ref, bool entered) {
  if (state == nullptr || frame_ref == LUA_NOREF) {
    return;
  }
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, frame_ref);
  if (lua_istable(state, -1) != 0) {
    const int frame_index = lua_absindex(state, -1);
    if (!frame_api::IsFrameHighlightLocked(state, frame_index)) {
      frame_api::SetFrameHighlightLayerShown(state, frame_index, entered);
    }
  }
  lua_settop(state, top);
}

bool GetBooleanField(lua_State *state, int index, const char *field) {
  index = lua_absindex(state, index);
  lua_getfield(state, index, field);
  const bool value = lua_toboolean(state, -1) != 0;
  lua_pop(state, 1);
  return value;
}

std::optional<std::string> GetStringField(lua_State *state, int index, const char *field) {
  index = lua_absindex(state, index);
  lua_getfield(state, index, field);
  std::optional<std::string> value;
  if (const char *text = lua_tostring(state, -1); text != nullptr) {
    value.emplace(text);
  }
  lua_pop(state, 1);
  return value;
}

double ReadRegistryNumberField(lua_State *state, int frame_ref, const char *field,
                               double fallback) {
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, frame_ref);
  double value = fallback;
  if (lua_istable(state, -1) != 0) {
    lua_getfield(state, -1, field);
    if (lua_isnumber(state, -1) != 0) {
      value = lua_tonumber(state, -1);
    }
    lua_pop(state, 1);
  }
  lua_settop(state, top);
  return value;
}

bool GetOptionalBooleanField(lua_State *state, int index, const char *field, bool *value) {
  index = lua_absindex(state, index);
  GetInternedLuaField(state, index, field);
  const bool present = lua_isboolean(state, -1) != 0;
  if (present) {
    *value = lua_toboolean(state, -1) != 0;
  }
  lua_pop(state, 1);
  return present;
}

bool FrameUsesMouse(lua_State *state, const FrameStore &frames, std::string_view frame_name) {
  const auto ref = frames.FindLuaRef(frame_name);
  if (state == nullptr || !ref.has_value()) {
    return false;
  }
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, *ref);
  bool enabled = false;
  if (lua_istable(state, -1) != 0 &&
      !(GetOptionalBooleanField(state, -1, "__ow_mouse_enabled", &enabled) ||
        GetOptionalBooleanField(state, -1, "__ow_enableMouse", &enabled))) {

    const auto *frame = frames.FindFrame(frame_name);
    enabled = frame != nullptr && frame->runtime_uses_mouse;
  }
  lua_settop(state, top);
  return enabled;
}

bool FrameIsShown(lua_State *state, const FrameStore &frames, std::string_view frame_name) {
  if (const auto ref = frames.FindLuaRef(frame_name); ref.has_value()) {
    const int top = lua_gettop(state);
    lua_rawgeti(state, LUA_REGISTRYINDEX, *ref);
    const bool shown = lua_istable(state, -1) != 0 && detail::GetLuaWidgetShownState(state, -1);
    lua_settop(state, top);
    return shown;
  }
  const auto *frame = frames.FindFrame(frame_name);
  return frame != nullptr && frame->visible;
}

bool FrameIsEditBox(const openwow::ui::framexml::UiFrame &frame) {
  return frame.runtime_kind == openwow::ui::framexml::UiFrame::RuntimeKind::EditBox ||
         openwow::text::EqualsIgnoreCaseAscii(frame.kind, "EditBox");
}

bool IsButtonFrame(const openwow::ui::framexml::UiFrame &frame) {
  return openwow::text::EqualsIgnoreCaseAscii(frame.kind, "Button") ||
         openwow::text::EqualsIgnoreCaseAscii(frame.kind, "CheckButton");
}

bool IsSliderFrame(const openwow::ui::framexml::UiFrame &frame) {
  return openwow::text::EqualsIgnoreCaseAscii(frame.kind, "Slider");
}

bool FrameIsWorldFrame(const openwow::ui::framexml::UiFrame &frame) {
  return openwow::text::EqualsIgnoreCaseAscii(frame.kind, "WorldFrame");
}

bool InvokeNumberMethod(lua_State *state, int frame_ref, const char *method_name,
                        double argument) {
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, frame_ref);
  if (lua_istable(state, -1) == 0) {
    lua_settop(state, top);
    return false;
  }
  lua_getfield(state, -1, method_name);
  if (lua_isfunction(state, -1) == 0) {
    lua_settop(state, top);
    return false;
  }
  lua_pushvalue(state, -2);
  lua_pushnumber(state, argument);
  const int status = ProfiledPCall(state, 2, 0, 0);
  if (status != LUA_OK) {
    lua_pop(state, 1);
  }
  lua_settop(state, top);
  return true;
}

struct ButtonVisualState {
  bool disabled{false};
  bool locked{false};
};

ButtonVisualState ReadButtonVisualState(lua_State *state, int frame_ref) {
  ButtonVisualState result;
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, frame_ref);
  if (lua_istable(state, -1) != 0) {
    const auto visual = GetStringField(state, -1, "__ow_btn_state").value_or("NORMAL");
    result.disabled = openwow::text::EqualsIgnoreCaseAscii(visual, "DISABLED");

    result.locked = GetBooleanField(state, -1, "__ow_btn_state_locked");
    lua_getfield(state, -1, "__ow_btn_enabled");
    result.disabled |= lua_isboolean(state, -1) != 0 && lua_toboolean(state, -1) == 0;
    lua_pop(state, 1);
  }
  lua_settop(state, top);
  return result;
}

void SetButtonVisualState(lua_State *state, int frame_ref, const char *state_name) {
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, frame_ref);
  if (lua_istable(state, -1) != 0 &&
      !GetBooleanField(state, -1, "__ow_btn_state_locked")) {
    lua_pushstring(state, state_name);
    lua_setfield(state, -2, "__ow_btn_state");
    frame_api::RefreshButtonLabelFont(state, -1);
  }
  lua_settop(state, top);
}

bool FrameRefHasHandler(lua_State *state, int frame_ref, const char *handler) {
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, frame_ref);
  const bool found = lua_istable(state, -1) != 0 && PushFrameScriptHandler(state, -1, handler);
  lua_settop(state, top);
  return found;
}

bool FrameAcceptsTouchDrag(lua_State* state, const int ref) {
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, ref);
  if (!lua_istable(state, -1)) {
    lua_settop(state, top);
    return false;
  }
  lua_getfield(state, -1, kRegisteredDragButtonMaskField);
  const auto mask = static_cast<std::uint32_t>(lua_tointeger(state, -1));
  lua_settop(state, top);
  return (mask & 1u) != 0u && FrameRefHasHandler(state, ref, "OnDragStart");
}

bool IsRegisteredClickPhase(lua_State *state, int frame_ref, std::string_view button_name,
                            bool is_down) {
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, frame_ref);
  if (lua_istable(state, -1) == 0) {
    lua_settop(state, top);
    return false;
  }
  lua_getfield(state, -1, "__ow_registered_clicks");
  if (lua_istable(state, -1) == 0) {
    lua_settop(state, top);
    return !is_down && openwow::text::EqualsIgnoreCaseAscii(button_name, "LeftButton");
  }
  const std::string specific = std::string(button_name) + (is_down ? "Down" : "Up");
  const std::string any = is_down ? "AnyDown" : "AnyUp";
  const auto count = lua_rawlen(state, -1);
  for (lua_Integer i = 1; i <= static_cast<lua_Integer>(count); ++i) {
    lua_geti(state, -1, i);
    const char *token = lua_tostring(state, -1);
    const bool matches =
        token != nullptr && (openwow::text::EqualsIgnoreCaseAscii(token, specific) ||
                             openwow::text::EqualsIgnoreCaseAscii(token, any));
    lua_pop(state, 1);
    if (matches) {
      lua_settop(state, top);
      return true;
    }
  }
  lua_settop(state, top);
  return false;
}

bool InvokeButtonClick(lua_State *state, int frame_ref, const char *button_name, bool is_down) {
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, frame_ref);
  if (lua_istable(state, -1) == 0) {
    lua_settop(state, top);
    return false;
  }
  lua_getfield(state, -1, "Click");
  if (lua_isfunction(state, -1) == 0) {
    lua_settop(state, top);
    return false;
  }
  lua_pushvalue(state, -2);
  lua_pushstring(state, button_name);
  lua_pushboolean(state, is_down ? 1 : 0);

  const int status = ProfiledPCall(state, 3, 0, 0);
  if (status != LUA_OK) {
    lua_pop(state, 1);
  }
  lua_settop(state, top);
  return true;
}

void ToggleCheckButton(lua_State *state, int frame_ref) {
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, frame_ref);
  if (lua_istable(state, -1) != 0) {
    lua_getfield(state, -1, "__ow_checked");
    const bool checked = lua_toboolean(state, -1) != 0;
    lua_pop(state, 1);
    lua_pushboolean(state, checked ? 0 : 1);
    lua_setfield(state, -2, "__ow_checked");
  }
  lua_settop(state, top);
}

std::uint32_t MouseButtonFlag(int button) {
  switch (button) {
  case 0:
    return 1u;
  case 1:
    return 4u;
  case 2:
    return 2u;
  default:
    return button >= 3 && button <= 30 ? 1u << button : 0u;
  }
}

std::uint32_t LiveButtonMask(std::uint32_t button_flag, bool down) {
  return detail::ApplyMouseButtonEventToScriptMask(detail::GetCurrentScriptMouseButtonMask(),
                                                   button_flag, down);
}

int ResolveSizingAnchor(const openwow::ui::framexml::FrameRect &rect, float x, float y) {
  const float horizontal_band = static_cast<float>(rect.width) * 0.25F;
  const float vertical_band = static_cast<float>(rect.height) * 0.25F;
  const float horizontal_offset = x - static_cast<float>(rect.x);
  const float vertical_offset = y - static_cast<float>(rect.y);
  const int column = horizontal_offset < horizontal_band
                         ? 0
                         : (horizontal_offset >= horizontal_band * 3.0F ? 2 : 1);
  const int row =
      vertical_offset < vertical_band ? 0 : (vertical_offset >= vertical_band * 3.0F ? 2 : 1);
  return row * 3 + column;
}

bool TitleRegionContainsPoint(lua_State *state, int frame_ref, std::string_view frame_name,
                              const RetainedLayout &layout, float x, float y) {
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, frame_ref);
  if (lua_istable(state, -1) == 0) {
    lua_settop(state, top);
    return false;
  }
  lua_getfield(state, -1, "__ow_title_region");
  if (lua_istable(state, -1) == 0) {
    lua_settop(state, top);
    return false;
  }
  const auto rect = layout.ResolveAnonymousRegionRect(state, lua_absindex(state, -1), frame_name);
  lua_settop(state, top);
  return rect.has_value() && x >= static_cast<float>(rect->x) &&
         x <= static_cast<float>(rect->x + rect->width) && y >= static_cast<float>(rect->y) &&
         y <= static_cast<float>(rect->y + rect->height);
}

}

FrameInputRouter::FrameInputRouter(FrameStore &frames, FrameTraversalIndex &traversal,
                                   RetainedLayout &layout)
    : frames_(frames), traversal_(traversal), layout_(layout) {}

void FrameInputRouter::BindLuaState(lua_State *state) noexcept {
  lua_ = state;
  MarkMouseFocusDirty();
}

void FrameInputRouter::Reset() noexcept {
  lua_ = nullptr;
  focused_frame_.clear();
  mouseover_frame_.clear();
  keyboard_captures_.clear();
  mouse_button_captures_ = {};
  button_last_click_time_ms_.clear();
  active_move_sizing_ = {};
  last_mouse_x_ = 0.0F;
  last_mouse_y_ = 0.0F;
  have_last_mouse_position_ = false;
  mouse_focus_dirty_ = true;
  pressed_button_mask_ = 0;
  hyperlink_hit_regions_.clear();
  hovered_hyperlink_.reset();
  pending_edit_box_updates_.clear();
  processing_edit_box_updates_.clear();
  pending_edit_box_update_refs_.clear();
  edit_box_drag_select_frame_.clear();
  touch_capture_active_ = false;
  touch_gesture_.reset();
  touch_context_.reset();
  pending_touch_context_action_.reset();
  touch_pointer_active_ = false;
  running_macro_input_button_provider_ = {};
}

void FrameInputRouter::BeginHyperlinkHitTestFrame() {
  hyperlink_hit_regions_.clear();
}

void FrameInputRouter::MarkMouseFocusDirty() noexcept {
  mouse_focus_dirty_ = true;
}

void FrameInputRouter::ReplayMouseFocusIfDirty() {
  if (lua_ == nullptr || !mouse_focus_dirty_) return;
  if (touch_gesture_ && touch_gesture_->phase != TouchPhase::kInspect &&
      !touch_context_) return;
  if (!have_last_mouse_position_) {
    const auto [mouse_x, mouse_y] =
        openwow::input::InputManager::Get().GetMousePosition();
    last_mouse_x_ = static_cast<float>(mouse_x);
    last_mouse_y_ = static_cast<float>(mouse_y);
    have_last_mouse_position_ = true;
  }
  RebuildTraversalIfDirty();
  RefreshMouseFocusAt(last_mouse_x_, last_mouse_y_, false);
}

void FrameInputRouter::AddHyperlinkHitRegion(std::string frame_name, const float left,
                                             const float top, const float right, const float bottom,
                                             std::string link, std::string text) {
  if (frame_name.empty() || link.empty() || right <= left || bottom <= top) {
    return;
  }
  hyperlink_hit_regions_.push_back({
      .frame_name = std::move(frame_name),
      .left = left,
      .top = top,
      .right = right,
      .bottom = bottom,
      .link = std::move(link),
      .text = std::move(text),
  });
}

const FrameInputRouter::HyperlinkHitRegion *FrameInputRouter::FindHyperlinkAt(const float x,
                                                                              const float y) const {
  for (auto region = hyperlink_hit_regions_.rbegin(); region != hyperlink_hit_regions_.rend();
       ++region) {
    if (x >= region->left && x <= region->right && y >= region->top && y <= region->bottom) {
      return &*region;
    }
  }
  return nullptr;
}

bool FrameInputRouter::FramesShareInputHierarchy(const std::string_view first,
                                                 const std::string_view second) const {
  const auto contains = [&](std::string_view descendant, const std::string_view ancestor) {
    constexpr int kMaxParentDepth = 64;
    for (int depth = 0; depth < kMaxParentDepth && !descendant.empty(); ++depth) {
      if (descendant == ancestor) {
        return true;
      }
      const auto *frame = frames_.FindFrame(descendant);
      if (frame == nullptr || frame->parent.empty()) {
        break;
      }
      descendant = frame->parent;
    }
    return false;
  };
  return contains(first, second) || contains(second, first);
}

const FrameInputRouter::HyperlinkHitRegion *
FrameInputRouter::ResolveHyperlinkAt(const std::string_view ordinary_hit, const float x,
                                     const float y) const {
  const auto *hyperlink = FindHyperlinkAt(x, y);
  if (hyperlink == nullptr) {
    return nullptr;
  }

  return ordinary_hit.empty() || FramesShareInputHierarchy(ordinary_hit, hyperlink->frame_name)
             ? hyperlink
             : nullptr;
}

void FrameInputRouter::UpdateHoveredHyperlink(const float x, const float y) {
  const auto *next = ResolveHyperlinkAt(mouseover_frame_, x, y);
  const bool unchanged = next != nullptr && hovered_hyperlink_.has_value() &&
                         next->frame_name == hovered_hyperlink_->frame_name &&
                         next->link == hovered_hyperlink_->link &&
                         next->text == hovered_hyperlink_->text;
  if (unchanged || (next == nullptr && !hovered_hyperlink_.has_value())) {
    return;
  }
  if (hovered_hyperlink_.has_value()) {
    if (const auto ref = frames_.FindLuaRef(hovered_hyperlink_->frame_name); ref.has_value()) {
      (void)FireHyperlink(lua_, *ref, "OnHyperlinkLeave", hovered_hyperlink_->link,
                          hovered_hyperlink_->text);
    }
  }
  hovered_hyperlink_.reset();
  if (next != nullptr) {
    hovered_hyperlink_ = *next;
    if (const auto ref = frames_.FindLuaRef(next->frame_name); ref.has_value()) {
      (void)FireHyperlink(lua_, *ref, "OnHyperlinkEnter", next->link, next->text);
    }
  }
}

void FrameInputRouter::UpdateSliderValueFromCursor(const std::string &frame_name, float x,
                                                    float y) {
  if (lua_ == nullptr) {
    return;
  }
  const auto rect_it = layout_.rects().find(frame_name);
  if (rect_it == layout_.rects().end() || rect_it->second.width <= 0 ||
      rect_it->second.height <= 0) {
    return;
  }
  const auto ref = frames_.FindLuaRef(frame_name);
  if (!ref.has_value()) {
    return;
  }
  const auto &rect = rect_it->second;
  bool horizontal = false;
  const int top = lua_gettop(lua_);
  lua_rawgeti(lua_, LUA_REGISTRYINDEX, *ref);
  if (lua_istable(lua_, -1) != 0) {
    lua_getfield(lua_, -1, "__ow_sl_orient");
    if (const char *token = lua_tostring(lua_, -1); token != nullptr) {
      horizontal = openwow::text::EqualsIgnoreCaseAscii(token, "HORIZONTAL");
    }
    lua_pop(lua_, 1);
  }
  lua_settop(lua_, top);

  const double minimum = ReadRegistryNumberField(lua_, *ref, "__ow_sl_min", 0.0);
  const double maximum = ReadRegistryNumberField(lua_, *ref, "__ow_sl_max", 100.0);
  if (!(maximum > minimum)) {
    return;
  }
  const double fraction =
      horizontal
          ? std::clamp((static_cast<double>(x) - rect.x) / static_cast<double>(rect.width),
                       0.0, 1.0)
          : std::clamp((static_cast<double>(y) - rect.y) / static_cast<double>(rect.height),
                       0.0, 1.0);
  const double value = minimum + fraction * (maximum - minimum);
  (void)InvokeNumberMethod(lua_, *ref, "SetValue", value);
}

std::size_t FrameInputRouter::ButtonCaptureIndex(std::uint32_t button_flag) noexcept {
  if (button_flag == 0 || (button_flag & (button_flag - 1u)) != 0) {
    return 31;
  }
  std::size_t index = 0;
  while ((button_flag >>= 1u) != 0) {
    ++index;
  }
  return index;
}

FrameInputRouter::MouseButtonCaptureState *
FrameInputRouter::FindCapture(std::uint32_t button_flag) noexcept {
  const auto index = ButtonCaptureIndex(button_flag);
  return index < mouse_button_captures_.size() ? &mouse_button_captures_[index] : nullptr;
}

float FrameInputRouter::viewport_height() const noexcept {
  return layout_.viewport_height();
}

float FrameInputRouter::root_scale() const noexcept {
  return layout_.root_scale();
}

void FrameInputRouter::RebuildTraversalIfDirty() {
  if (traversal_.order_dirty()) {
    traversal_.Rebuild(root_scale(), viewport_height());
  }
  traversal_.RefreshRectCache();
}

void FrameInputRouter::RefreshMouseFocusAt(const float x, const float y,
                                           const bool motion) {
  mouse_focus_dirty_ = false;
  const std::string next = traversal_.HitTarget(x, y, viewport_height());

  if (next != mouseover_frame_) {
    if (auto* old_minimap = frames_.FindMinimap(mouseover_frame_);
        old_minimap != nullptr) {
      old_minimap->ClearHoverTooltip();
    }
  }

  if (auto* minimap = frames_.FindMinimap(next); minimap != nullptr) {
    const openwow::input::InputEvent event{
        .type = openwow::input::InputEventType::MouseMove,
        .mouseX = static_cast<std::int32_t>(x),
        .mouseY = static_cast<std::int32_t>(y),
    };
    (void)minimap->TrackPresentedMouseMove(&event);
  }

  if (next != mouseover_frame_) {
    const auto old_ref = frames_.FindLuaRef(mouseover_frame_);
    const auto new_ref = frames_.FindLuaRef(next);

    mouseover_frame_ = next;
    if (old_ref.has_value()) {
      SetFrameHighlightForMouseFocus(lua_, *old_ref, false);
      FireBoolean(lua_, *old_ref, "OnLeave", motion);
    }
    if (new_ref.has_value()) {
      SetFrameHighlightForMouseFocus(lua_, *new_ref, true);
      FireBoolean(lua_, *new_ref, "OnEnter", motion);
    }
  }
  UpdateHoveredHyperlink(x, y);
}

bool FrameInputRouter::HandleMouseDown(float x, float y, int button) {
  return HandleMouseButtonDownByFlag(x, y, MouseButtonFlag(button));
}

bool FrameInputRouter::HandleMouseButtonDownByFlag(float x, float y, std::uint32_t button_flag) {
  if (touch_capture_active_ && button_flag == 1u) {
    return true;
  }
  return HandlePointerDownByFlag(x, y, button_flag, false);
}

bool FrameInputRouter::HandleTouchDown(float x, float y,
                                       float pixels_per_point_x,
                                       float pixels_per_point_y) {
  constexpr std::uint32_t kLeftButtonFlag = 1u;
  if (lua_ == nullptr) return false;
  if (touch_capture_active_) {
    return true;
  }
  if (const auto* const capture = FindCapture(kLeftButtonFlag);
      capture != nullptr && capture->active) {
    return true;
  }
  layout_.SolveIfDirty();
  RebuildTraversalIfDirty();
  std::string hit = traversal_.HitTarget(x, y, viewport_height());
  const bool context_control = touch_context_ &&
      traversal_.Contains(touch_context_->presentation_frame, x, y, viewport_height()) &&
      FramesShareInputHierarchy(hit, touch_context_->presentation_frame);
  const bool dismiss_context = touch_context_.has_value() && !context_control;
  if (!context_control) {
    ClearTouchContext();
    ClearTouchHover();
    // OnLeave / context dismissal may change the hit target.
    layout_.SolveIfDirty();
    RebuildTraversalIfDirty();
    hit = traversal_.HitTarget(x, y, viewport_height());
  }
  if (dismiss_context) {
    // Like a touch context menu, its outside-dismiss gesture is consumed. It
    // must not equip an item or become world movement through the closed menu.
    touch_gesture_ = TouchGesture{.phase = TouchPhase::kCancelled};
    touch_capture_active_ = true;
    return true;
  }
  const auto* hyperlink = ResolveHyperlinkAt(hit, x, y);
  if (hyperlink != nullptr) hit = hyperlink->frame_name;
  const auto ref = frames_.FindLuaRef(hit);
  const auto* frame = frames_.FindFrame(hit);
  if (!ref || frame == nullptr || FrameIsWorldFrame(*frame)) return false;

  TouchGesture gesture;
  gesture.target = {.frame_name = hit, .lua_ref = *ref, .x = x, .y = y};
  if (hyperlink != nullptr) {
    gesture.target.hyperlink_link = hyperlink->link;
    gesture.target.hyperlink_text = hyperlink->text;
  }
  gesture.started_at_ms = openwow::core::GameClock::GetTickCount32();
  gesture.pixels_per_point_x = std::max(1.0F, pixels_per_point_x);
  gesture.pixels_per_point_y = std::max(1.0F, pixels_per_point_y);
  gesture.current_x = x;
  gesture.current_y = y;
  gesture.scroll_y = y;
  gesture.can_drag = FrameAcceptsTouchDrag(lua_, *ref) &&
      (!IsButtonFrame(*frame) || !ReadButtonVisualState(lua_, *ref).disabled);
  for (const auto& entry : traversal_.input_snapshot()) {
    if (entry.effective_visible && entry.uses_mouse_wheel && entry.lua_ref != LUA_NOREF &&
        (entry.frame == nullptr || !FrameIsWorldFrame(*entry.frame)) &&
        FramesShareInputHierarchy(hit, entry.key) &&
        traversal_.Contains(entry.key, x, y, viewport_height())) {
      gesture.scroll_target = TouchTarget{
          .frame_name = entry.key, .lua_ref = entry.lua_ref, .x = x, .y = y};
      break;
    }
  }
  using Kind = openwow::ui::framexml::UiFrame::RuntimeKind;
  const bool direct_surface = frame->runtime_kind == Kind::Model ||
      frame->runtime_kind == Kind::PlayerModel || frame->runtime_kind == Kind::DressUpModel ||
      frame->runtime_kind == Kind::TabardModel || frame->runtime_kind == Kind::ColorSelect;
  const bool direct = context_control || direct_surface || IsSliderFrame(*frame) ||
      FrameIsEditBox(*frame) || TitleRegionContainsPoint(lua_, *ref, hit, layout_, x, y);
  gesture.phase = direct ? TouchPhase::kDirect : TouchPhase::kPending;
  touch_gesture_ = std::move(gesture);
  touch_capture_active_ = true;
  if (direct) {
    if (!context_control) {
      const auto target = touch_gesture_->target;
      PublishTouchHover(x, y);
      if (!touch_gesture_ || !TouchTargetIsCurrent(target)) {
        CancelTouch();
        return true;
      }
    }
    (void)HandlePointerDownByFlag(x, y, kLeftButtonFlag, true);
  } else if (IsButtonFrame(*frame)) {
    const auto visual = ReadButtonVisualState(lua_, *ref);
    if (!visual.disabled && !visual.locked) SetButtonVisualState(lua_, *ref, "PUSHED");
  }
  return true;
}

bool FrameInputRouter::HitTestTouchTarget(const float x, const float y) {
  if (lua_ == nullptr) {
    return false;
  }
  layout_.SolveIfDirty();
  RebuildTraversalIfDirty();
  const std::string hit = traversal_.HitTarget(x, y, viewport_height());
  if (hit.empty() || !frames_.FindLuaRef(hit).has_value()) {
    return false;
  }
  const auto* const frame = frames_.FindFrame(hit);
  return frame == nullptr || !FrameIsWorldFrame(*frame);
}

bool FrameInputRouter::HandlePointerDownByFlag(float x, float y,
                                               std::uint32_t button_flag,
                                               bool touch) {
  if (lua_ == nullptr || button_flag == 0) {
    return false;
  }

  SecureExecution::SecureScope hardware_input_scope(lua_);

  SecureExecution::HardwareActionGrantScope hardware_action_grant;
  const ::openwow::game::actions::bindings::adapters::lua::ScopedLuaModifierState modifier_snapshot(
      lua_, static_cast<std::uint16_t>(SDL_GetModState()));
  pressed_button_mask_ |= button_flag;
  if (!touch) {
    last_mouse_x_ = x;
    last_mouse_y_ = y;
    have_last_mouse_position_ = true;
  }
  layout_.SolveIfDirty();
  RebuildTraversalIfDirty();
  if (!touch) {
    RefreshMouseFocusAt(x, y, false);
  }
  std::string hit = traversal_.HitTarget(x, y, viewport_height());
  const auto *pressed_hyperlink = ResolveHyperlinkAt(hit, x, y);
  if (pressed_hyperlink != nullptr) {
    hit = pressed_hyperlink->frame_name;
  }

  if (!hit.empty()) {
    if (const auto *frame = frames_.FindFrame(hit); frame != nullptr && FrameIsEditBox(*frame)) {

      PlaceEditBoxCursorFromClick(hit, x, y);
      TransitionKeyboardFocus(hit);
    }
  }

  if (hit.empty()) {
    if (touch) {
      pressed_button_mask_ &= ~button_flag;
    }
    return false;
  }

  const auto ref = frames_.FindLuaRef(hit);
  auto *capture = FindCapture(button_flag);
  if (!ref.has_value() || capture == nullptr) {
    if (touch) {
      pressed_button_mask_ &= ~button_flag;
    }
    return false;
  }
  *capture = {};

  const std::uint16_t modifiers = static_cast<std::uint16_t>(SDL_GetModState());
  const bool layout_ctrl = (modifiers & KMOD_LCTRL) != 0;
  const bool layout_alt = (modifiers & KMOD_LALT) != 0;
  if (layout_.mode() != 0 && (layout_ctrl || layout_alt)) {
    const auto rect = layout_.rects().find(hit);
    if (rect != layout_.rects().end()) {
      bool start = false;
      int mode = 0;
      const int top = lua_gettop(lua_);
      lua_rawgeti(lua_, LUA_REGISTRYINDEX, *ref);
      if (lua_istable(lua_, -1) != 0) {
        if (layout_ctrl && GetBooleanField(lua_, -1, "__ow_resizable")) {
          mode = ResolveSizingAnchor(rect->second, x, y);
          start = true;
        } else if (layout_alt && GetBooleanField(lua_, -1, "__ow_movable")) {
          mode = 4;
          start = true;
        }
      }
      lua_settop(lua_, top);
      if (start) {
        (void)BeginFrameMoveSizing(hit, mode);
      }
    }
    return true;
  }

  if (TitleRegionContainsPoint(lua_, *ref, hit, layout_, x, y)) {
    (void)BeginFrameMoveSizing(hit, 4);
    return true;
  }

  if (const auto *hit_frame = frames_.FindFrame(hit);
      hit_frame != nullptr && FrameIsWorldFrame(*hit_frame)) {

    if (touch) {
      pressed_button_mask_ &= ~button_flag;
      return false;
    }

    const char *wf_button_name = openwow::ui::widgets::MouseButtonName(button_flag);
    const lua_adapter::ScopedMouseButtonOverride button_override(lua_, wf_button_name);
    const detail::ScopedCurrentMouseButtonMaskOverride mask_override(
        lua_, LiveButtonMask(button_flag, true));
    (void)FireButton(lua_, *ref, "OnMouseDown", wf_button_name);
    return false;
  }

  const char *button_name = openwow::ui::widgets::MouseButtonName(button_flag);
  *capture = {.frame_name = hit,
              .button_flag = button_flag,
              .start_x = x,
              .start_y = y,
              .active = true,
              .drag_started = false};
  if (pressed_hyperlink != nullptr) {
    capture->hyperlink_link = pressed_hyperlink->link;
    capture->hyperlink_text = pressed_hyperlink->text;
  }
  const lua_adapter::ScopedMouseButtonOverride button_override(lua_, button_name);
  const detail::ScopedCurrentMouseButtonMaskOverride mask_override(
      lua_, LiveButtonMask(button_flag, true));
  (void)FireButton(lua_, *ref, "OnMouseDown", button_name);
  const auto *frame = frames_.FindFrame(hit);
  if (frame != nullptr && IsSliderFrame(*frame) &&
      openwow::text::EqualsIgnoreCaseAscii(button_name, "LeftButton")) {

    capture->is_slider = true;
    UpdateSliderValueFromCursor(hit, x, y);
  }
  if (frame != nullptr && IsButtonFrame(*frame)) {
    const auto visual = ReadButtonVisualState(lua_, *ref);
    if (!visual.disabled) {
      if (IsRegisteredClickPhase(lua_, *ref, button_name, true)) {
        (void)InvokeButtonClick(lua_, *ref, button_name, true);
      }
      if (!visual.locked) {
        SetButtonVisualState(lua_, *ref, "PUSHED");
      }
    }
  } else if (IsRegisteredClickPhase(lua_, *ref, button_name, true)) {
    (void)FireButton(lua_, *ref, "OnClick", button_name);
  }
  return true;
}

bool FrameInputRouter::HandleMouseUp(float x, float y, int button) {
  return HandleMouseButtonUpByFlag(x, y, MouseButtonFlag(button));
}

bool FrameInputRouter::HandleMouseButtonUpByFlag(float x, float y, std::uint32_t button_flag) {
  if (touch_capture_active_ && button_flag == 1u) {
    return true;
  }
  const bool handled = HandlePointerUpByFlag(x, y, button_flag, false);
  DispatchTouchContextAction();
  return handled;
}

bool FrameInputRouter::HandleTouchUp(float x, float y) {
  if (!touch_capture_active_) {
    return false;
  }
  (void)HandleTouchMove(x, y);
  UpdateTouchInspection();
  touch_capture_active_ = false;
  auto gesture = std::exchange(touch_gesture_, std::nullopt);
  if (!gesture) return true;
  if (gesture->phase == TouchPhase::kInspect) {
    if (TouchTargetIsCurrent(gesture->target)) {
      touch_context_ = TouchContext{.target = std::move(gesture->target)};
      PresentTouchContext();
    } else {
      ClearTouchHover();
    }
    return true;
  }
  if (gesture->phase == TouchPhase::kPending &&
      TouchTargetIsCurrent(gesture->target)) {
    PublishTouchHover(gesture->target.x, gesture->target.y);
    if (TouchTargetIsCurrent(gesture->target)) {
      (void)HandlePointerDownByFlag(gesture->target.x, gesture->target.y, 1u, true);
    }
  }
  if (frames_.FindLuaRef(gesture->target.frame_name) == gesture->target.lua_ref) {
    const auto* frame = frames_.FindFrame(gesture->target.frame_name);
    if (frame != nullptr && IsButtonFrame(*frame) &&
        !ReadButtonVisualState(lua_, gesture->target.lua_ref).disabled) {
      SetButtonVisualState(lua_, gesture->target.lua_ref, "NORMAL");
    }
  }
  if (const auto* capture = FindCapture(1u); capture != nullptr && capture->active) {
    (void)HandlePointerUpByFlag(x, y, 1u, true);
  }
  if (pending_touch_context_action_) {
    DispatchTouchContextAction();
  } else if (!touch_context_) {
    ClearTouchHover();
  } else {
    PublishTouchHover(touch_context_->target.x, touch_context_->target.y);
  }
  return true;
}

bool FrameInputRouter::HandlePointerUpByFlag(float x, float y,
                                             std::uint32_t button_flag,
                                             bool touch) {
  if (lua_ == nullptr || button_flag == 0) {
    return false;
  }
  SecureExecution::SecureScope hardware_input_scope(lua_);

  SecureExecution::HardwareActionGrantScope hardware_action_grant;
  const ::openwow::game::actions::bindings::adapters::lua::ScopedLuaModifierState modifier_snapshot(
      lua_, static_cast<std::uint16_t>(SDL_GetModState()));
  pressed_button_mask_ &= ~button_flag;
  layout_.SolveIfDirty();
  if (!touch) {
    last_mouse_x_ = x;
    last_mouse_y_ = y;
    have_last_mouse_position_ = true;
  }
  RebuildTraversalIfDirty();
  if (!touch) {
    RefreshMouseFocusAt(x, y, false);
  }

  edit_box_drag_select_frame_.clear();
  const char *button_name = openwow::ui::widgets::MouseButtonName(button_flag);
  bool handled = false;

  if (active_move_sizing_.active) {
    (void)StopFrameMoveSizing(active_move_sizing_.frame_name);
    handled = true;
  }

  auto *capture = FindCapture(button_flag);
  if (capture != nullptr && capture->active) {
    const std::string capture_name = capture->frame_name;
    const auto ref = frames_.FindLuaRef(capture_name);
    if (ref.has_value()) {
      if (capture->drag_started) {
        const lua_adapter::ScopedMouseButtonOverride button_override(lua_, button_name);
        const detail::ScopedCurrentMouseButtonMaskOverride mask_override(
            lua_, LiveButtonMask(button_flag, false));
        (void)FireNoArg(lua_, *ref, "OnDragStop");
        layout_.SolveIfDirty();
        RebuildTraversalIfDirty();
        const std::string target = traversal_.HitTarget(x, y, viewport_height());
        if (const auto target_ref = frames_.FindLuaRef(target); target_ref.has_value()) {
          (void)FireNoArg(lua_, *target_ref, "OnReceiveDrag");
        }
        handled = true;
      } else {
        const lua_adapter::ScopedMouseButtonOverride button_override(lua_, button_name);
        const detail::ScopedCurrentMouseButtonMaskOverride mask_override(
            lua_, LiveButtonMask(button_flag, false));
        handled = FireButton(lua_, *ref, "OnMouseUp", button_name) || handled;
        bool hyperlink_clicked = false;
        if (!capture->hyperlink_link.empty()) {
          const std::string release_hit = traversal_.HitTarget(x, y, viewport_height());
          if (const auto *hyperlink = ResolveHyperlinkAt(release_hit, x, y);
              hyperlink != nullptr && hyperlink->frame_name == capture_name &&
              hyperlink->link == capture->hyperlink_link &&
              hyperlink->text == capture->hyperlink_text) {
            hyperlink_clicked =
                FireHyperlinkClick(lua_, *ref, hyperlink->link, hyperlink->text, button_name);
            handled = hyperlink_clicked || handled;
          }
        }
        const auto *frame = frames_.FindFrame(capture_name);
        if (frame != nullptr && IsButtonFrame(*frame)) {
          const auto visual = ReadButtonVisualState(lua_, *ref);
          const bool inside = traversal_.Contains(capture_name, x, y, viewport_height());

          if (!visual.disabled && inside &&
              IsRegisteredClickPhase(lua_, *ref, button_name, false)) {
            const std::uint32_t now = openwow::core::GameClock::GetTickCount32();
            auto &last_click = button_last_click_time_ms_[capture_name];
            if (last_click != 0 && now - last_click <= 300 &&
                FrameRefHasHandler(lua_, *ref, "OnDoubleClick")) {
              (void)FireButton(lua_, *ref, "OnDoubleClick", button_name);
              last_click = 0;
            } else {
              if (openwow::text::EqualsIgnoreCaseAscii(frame->kind, "CheckButton")) {
                ToggleCheckButton(lua_, *ref);
              }
              (void)InvokeButtonClick(lua_, *ref, button_name, false);
              last_click = now;
            }
            handled = true;
          }
        } else if (!hyperlink_clicked && IsRegisteredClickPhase(lua_, *ref, button_name, false)) {
          (void)FireButton(lua_, *ref, "OnClick", button_name);
          handled = true;
        }
      }
    }
    if (const auto *frame = frames_.FindFrame(capture_name);
        frame != nullptr && IsButtonFrame(*frame)) {
      if (const auto current_ref = frames_.FindLuaRef(capture_name); current_ref.has_value()) {
        const auto visual = ReadButtonVisualState(lua_, *current_ref);
        if (!visual.disabled && !visual.locked) {
          SetButtonVisualState(lua_, *current_ref, "NORMAL");
        }
      }
    }
    *capture = {};
    return true;
  }

  RebuildTraversalIfDirty();
  const std::string hit = traversal_.HitTarget(x, y, viewport_height());
  if (const auto *hit_frame = frames_.FindFrame(hit);
      hit_frame != nullptr && FrameIsWorldFrame(*hit_frame)) {

    if (touch) {
      return handled;
    }

    if (const auto ref = frames_.FindLuaRef(hit); ref.has_value()) {
      const lua_adapter::ScopedMouseButtonOverride button_override(lua_, button_name);
      const detail::ScopedCurrentMouseButtonMaskOverride mask_override(
          lua_, LiveButtonMask(button_flag, false));
      (void)FireButton(lua_, *ref, "OnMouseUp", button_name);
    }
    return handled;
  }
  const auto ref = frames_.FindLuaRef(hit);
  if (!ref.has_value()) {
    return handled;
  }
  const lua_adapter::ScopedMouseButtonOverride button_override(lua_, button_name);
  const detail::ScopedCurrentMouseButtonMaskOverride mask_override(
      lua_, LiveButtonMask(button_flag, false));
  handled = FireButton(lua_, *ref, "OnMouseUp", button_name) || handled;
  const auto *frame = frames_.FindFrame(hit);
  if ((frame == nullptr || !IsButtonFrame(*frame)) &&
      IsRegisteredClickPhase(lua_, *ref, button_name, false)) {
    (void)FireButton(lua_, *ref, "OnClick", button_name);
  }
  return true;
}

bool FrameInputRouter::HandleMouseMove(float x, float y) {
  if (touch_pointer_active_ || touch_gesture_) CancelTouch();
  return HandlePointerMove(x, y, false);
}

bool FrameInputRouter::HandleTouchMove(float x, float y) {
  UpdateTouchInspection();
  if (!touch_capture_active_ || !touch_gesture_) return false;
  auto& gesture = *touch_gesture_;
  gesture.current_x = x;
  gesture.current_y = y;
  if (gesture.phase == TouchPhase::kDirect || gesture.phase == TouchPhase::kDrag) {
    if (!touch_context_) {
      SetTouchCursorPosition(x, y);
    }
    SecureExecution::SecureScope hardware_input_scope(lua_);
    SecureExecution::HardwareActionGrantScope hardware_action_grant;
    return HandlePointerMove(x, y, true);
  }
  if (gesture.phase == TouchPhase::kCancelled) return true;
  const float dx = (x - gesture.target.x) / gesture.pixels_per_point_x;
  const float dy = (y - gesture.target.y) / gesture.pixels_per_point_y;
  const bool moved = dx * dx + dy * dy >= kTouchSlopPoints * kTouchSlopPoints;
  if (gesture.phase == TouchPhase::kInspect && moved) {
    if (gesture.can_drag) return BeginTouchDrag();
    gesture.phase = TouchPhase::kCancelled;
    ClearTouchHover();
    return true;
  } else if (gesture.phase == TouchPhase::kPending && moved) {
    if (frames_.FindLuaRef(gesture.target.frame_name) == gesture.target.lua_ref) {
      const auto* frame = frames_.FindFrame(gesture.target.frame_name);
      if (frame != nullptr && IsButtonFrame(*frame) &&
          !ReadButtonVisualState(lua_, gesture.target.lua_ref).disabled) {
        SetButtonVisualState(lua_, gesture.target.lua_ref, "NORMAL");
      }
    }
    gesture.phase = gesture.scroll_target && std::fabs(dy) > std::fabs(dx)
                        ? TouchPhase::kScroll : TouchPhase::kCancelled;
    if (gesture.phase == TouchPhase::kScroll) {
      gesture.target = std::move(*gesture.scroll_target);
      gesture.scroll_target.reset();
    }
  }
  if (gesture.phase == TouchPhase::kScroll) {
    const float delta = (y - gesture.scroll_y) / gesture.pixels_per_point_y;
    if (std::fabs(delta) >= kTouchScrollPoints) {
      const float step = delta > 0.0F ? 1.0F : -1.0F;
      // Bound work per input sample, including a coalesced fast swipe.
      const auto owner = gesture.target;
      const int steps = std::min(8, static_cast<int>(std::fabs(delta) / kTouchScrollPoints));
      gesture.scroll_y += steps * step * kTouchScrollPoints * gesture.pixels_per_point_y;
      SecureExecution::SecureScope hardware_input_scope(lua_);
      SecureExecution::HardwareActionGrantScope hardware_action_grant;
      for (int i = 0; i < steps && touch_gesture_; ++i) {
        if (frames_.FindLuaRef(owner.frame_name) != owner.lua_ref ||
            !traversal_.IsEffectivelyVisible(owner.frame_name) ||
            !FireFrameHandler(lua_, owner.lua_ref, "OnMouseWheel", 1,
                              [&] { lua_pushinteger(lua_, step > 0 ? 1 : -1); })) {
          openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
              "Touch scroll cancelled: frame=" + owner.frame_name +
              " source=vertical-swipe reason=wheel-owner-unavailable-or-handler-failed");
          CancelTouch();
          break;
        }
      }
    }
  }
  return true;
}

bool FrameInputRouter::TouchTargetIsCurrent(const TouchTarget& target) {
  if (lua_ == nullptr) return false;
  layout_.SolveIfDirty();
  RebuildTraversalIfDirty();
  const auto hit = traversal_.HitTarget(target.x, target.y, viewport_height());
  if (target.frame_name.empty()) {
    const auto* frame = frames_.FindFrame(hit);
    return hit.empty() || (frame != nullptr && FrameIsWorldFrame(*frame));
  }
  if (frames_.FindLuaRef(target.frame_name) != target.lua_ref ||
      !traversal_.IsEffectivelyVisible(target.frame_name)) return false;
  const auto* hyperlink = ResolveHyperlinkAt(hit, target.x, target.y);
  if (!target.hyperlink_link.empty()) {
    return hyperlink != nullptr && hyperlink->frame_name == target.frame_name &&
        hyperlink->link == target.hyperlink_link && hyperlink->text == target.hyperlink_text;
  }
  return hit == target.frame_name && hyperlink == nullptr;
}

void FrameInputRouter::SetTouchCursorPosition(const float x, const float y) {
  touch_pointer_active_ = true;
  last_mouse_x_ = x;
  last_mouse_y_ = y;
  have_last_mouse_position_ = true;
  openwow::input::InputManager::Get().SetMousePosition(
      static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)));
}

void FrameInputRouter::PublishTouchHover(const float x, const float y) {
  if (lua_ == nullptr) return;
  SetTouchCursorPosition(x, y);
  layout_.SolveIfDirty();
  RebuildTraversalIfDirty();
  RefreshMouseFocusAt(x, y, false);
}

void FrameInputRouter::ClearTouchHover() {
  if (!touch_pointer_active_ || lua_ == nullptr) return;
  touch_pointer_active_ = false;
  last_mouse_x_ = -1.0F;
  last_mouse_y_ = -1.0F;
  have_last_mouse_position_ = true;
  mouse_focus_dirty_ = false;
  openwow::input::InputManager::Get().SetMousePosition(-1, -1);
  const auto previous = std::exchange(mouseover_frame_, {});
  const auto hyperlink = std::exchange(hovered_hyperlink_, std::nullopt);
  if (auto* minimap = frames_.FindMinimap(previous); minimap != nullptr) {
    minimap->ClearHoverTooltip();
  }
  if (hyperlink) {
    if (const auto ref = frames_.FindLuaRef(hyperlink->frame_name); ref) {
      (void)FireHyperlink(lua_, *ref, "OnHyperlinkLeave", hyperlink->link, hyperlink->text);
    }
  }
  if (const auto ref = frames_.FindLuaRef(previous); ref) {
    SetFrameHighlightForMouseFocus(lua_, *ref, false);
    (void)FireBoolean(lua_, *ref, "OnLeave", false);
  }
}

void FrameInputRouter::UpdateTouchInspection() {
  if (lua_ == nullptr) return;
  if (touch_context_ && !touch_context_->presentation_frame.empty() &&
      !traversal_.IsEffectivelyVisible(touch_context_->presentation_frame)) {
    CancelTouch();
    return;
  }
  const TouchTarget* target = touch_gesture_ ? &touch_gesture_->target :
      (touch_context_ ? &touch_context_->target : nullptr);
  if (target != nullptr && !target->frame_name.empty() &&
      (frames_.FindLuaRef(target->frame_name) != target->lua_ref ||
       !traversal_.IsEffectivelyVisible(target->frame_name))) {
    CancelTouch();
    return;
  }
  if (!touch_gesture_ || touch_gesture_->phase != TouchPhase::kPending ||
      openwow::core::GameClock::GetTickCount32() - touch_gesture_->started_at_ms <
          kTouchInspectMilliseconds) return;
  const auto inspected = touch_gesture_->target;
  if (!TouchTargetIsCurrent(inspected)) {
    CancelTouch();
    return;
  }
  if (!touch_gesture_) return;
  touch_gesture_->phase = TouchPhase::kInspect;
  if (const auto* frame = frames_.FindFrame(inspected.frame_name);
      frame != nullptr && IsButtonFrame(*frame) &&
      !ReadButtonVisualState(lua_, inspected.lua_ref).disabled) {
    SetButtonVisualState(lua_, inspected.lua_ref, "NORMAL");
  }
  // Hover is not a hardware-action grant. No mouse-down or Click is sent.
  PublishTouchHover(inspected.x, inspected.y);
}

bool FrameInputRouter::BeginTouchDrag() {
  if (!touch_gesture_) return false;
  const auto gesture = *touch_gesture_;
  if (!TouchTargetIsCurrent(gesture.target) || !touch_gesture_) {
    CancelTouch();
    return true;
  }
  SecureExecution::SecureScope hardware_input_scope(lua_);
  SecureExecution::HardwareActionGrantScope hardware_action_grant;
  const lua_adapter::ScopedMouseButtonOverride button_override(lua_, "LeftButton");
  const detail::ScopedCurrentMouseButtonMaskOverride mask_override(
      lua_, LiveButtonMask(1u, true));
  auto* capture = FindCapture(1u);
  *capture = {.frame_name = gesture.target.frame_name, .button_flag = 1u,
              .start_x = gesture.target.x, .start_y = gesture.target.y,
              .active = true, .drag_started = true};
  pressed_button_mask_ |= 1u;
  touch_gesture_->phase = TouchPhase::kDrag;
  SetTouchCursorPosition(gesture.target.x, gesture.target.y);
  // Begin directly at the registered drag boundary; a down-click could cast
  // or use the object before its drag script gets a chance to pick it up.
  if (!FireButton(lua_, gesture.target.lua_ref, "OnDragStart", "LeftButton")) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
        "Touch drag rejected: frame=" + gesture.target.frame_name +
        " source=hold-drag reason=OnDragStart-failed");
    CancelTouch();
    return true;
  }
  if (!touch_gesture_) return true;
  ClearTouchHover();
  if (!touch_gesture_) return true;
  SetTouchCursorPosition(gesture.current_x, gesture.current_y);
  return HandlePointerMove(gesture.current_x, gesture.current_y, true);
}

void FrameInputRouter::ClearTouchContext() {
  pending_touch_context_action_.reset();
  const auto context = std::exchange(touch_context_, std::nullopt);
  if (!context || lua_ == nullptr) return;
  const int top = lua_gettop(lua_);
  lua_getglobal(lua_, "OpenWoWMobile_HideTouchContext");
  if (!lua_isfunction(lua_, -1) || ProfiledPCall(lua_, 0, 0, 0) != LUA_OK) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
        "Touch context dismissal failed: source=MobileUI reason=" +
        std::string(lua_tostring(lua_, -1) ? lua_tostring(lua_, -1) : "missing presentation callback"));
  }
  lua_settop(lua_, top);
}

void FrameInputRouter::PresentTouchContext() {
  if (!touch_context_ || lua_ == nullptr) return;
  const auto target = touch_context_->target;
  const bool world = static_cast<bool>(touch_context_->world_action);
  bool secondary = world;
  bool draggable = false;
  if (!world) {
    secondary = IsRegisteredClickPhase(lua_, target.lua_ref, "RightButton", false) ||
        IsRegisteredClickPhase(lua_, target.lua_ref, "RightButton", true) ||
        FrameRefHasHandler(lua_, target.lua_ref, "OnMouseDown") ||
        FrameRefHasHandler(lua_, target.lua_ref, "OnMouseUp") ||
        !target.hyperlink_link.empty();
    draggable = FrameAcceptsTouchDrag(lua_, target.lua_ref);
  }
  const int top = lua_gettop(lua_);
  lua_getglobal(lua_, "OpenWoWMobile_ShowTouchContext");
  bool shown = false;
  if (lua_isfunction(lua_, -1)) {
    lua_pushnumber(lua_, target.x);
    lua_pushnumber(lua_, target.y);
    lua_pushboolean(lua_, secondary);
    lua_pushboolean(lua_, draggable);
    lua_pushlightuserdata(lua_, this);
    lua_pushcclosure(lua_, [](lua_State* state) -> int {
      const auto button = luaL_checkinteger(state, 1);
      if (button != 0 && button != 1 && button != 4) {
        return luaL_error(state, "Touch context: invalid button");
      }
      if (!GameUI_CanPerformHardwareEventAction()) {
        return luaL_error(state, "Touch context requires a hardware action");
      }
      auto* input = static_cast<FrameInputRouter*>(lua_touserdata(state, lua_upvalueindex(1)));
      if (!input->touch_context_) return luaL_error(state, "Touch context has expired");
      // Dispatch after the toolbar's own pointer-up has released its capture.
      input->pending_touch_context_action_ = static_cast<std::uint32_t>(button);
      return 0;
    }, 1);
    if (ProfiledPCall(lua_, 5, 1, 0) == LUA_OK && lua_isstring(lua_, -1) && touch_context_) {
      const std::string name = lua_tostring(lua_, -1);
      if (frames_.FindLuaRef(name)) {
        touch_context_->presentation_frame = name;
        shown = true;
      }
    }
  }
  if (!shown) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
        "Touch context presentation failed: frame=" + target.frame_name +
        " source=MobileUI reason=" +
        std::string(lua_tostring(lua_, -1) ? lua_tostring(lua_, -1) : "missing or invalid presentation"));
  }
  lua_settop(lua_, top);
  if (!shown) CancelTouch();
}

void FrameInputRouter::DispatchTouchContextAction() {
  const auto button = std::exchange(pending_touch_context_action_, std::nullopt);
  if (!button || !touch_context_) return;
  const auto context = *touch_context_;
  ClearTouchContext();
  if (*button == 0u) {
    ClearTouchHover();
    return;
  }
  if (!TouchTargetIsCurrent(context.target)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
        "Touch context action cancelled: frame=" + context.target.frame_name +
        " button=" + std::to_string(*button) +
        " source=context-toolbar reason=target-hidden-replaced-or-covered");
    ClearTouchHover();
    return;
  }
  PublishTouchHover(context.target.x, context.target.y);
  if (context.world_action) {
    SecureExecution::SecureScope hardware_input_scope(lua_);
    SecureExecution::HardwareActionGrantScope hardware_action_grant;
    context.world_action(*button);
  } else if (TouchTargetIsCurrent(context.target)) {
    (void)HandlePointerDownByFlag(context.target.x, context.target.y, *button, true);
    if (const auto* capture = FindCapture(*button); capture != nullptr && capture->active) {
      (void)HandlePointerUpByFlag(context.target.x, context.target.y, *button, true);
    }
  }
  ClearTouchHover();
}

void FrameInputRouter::PreviewWorldTouch(float x, float y) {
  if (!touch_capture_active_) PublishTouchHover(x, y);
}

void FrameInputRouter::DismissWorldTouch() {
  if (touch_capture_active_) return;
  ClearTouchContext();
  ClearTouchHover();
}

void FrameInputRouter::ShowWorldTouchContext(float x, float y,
                                            std::function<void(std::uint32_t)> action) {
  if (touch_capture_active_) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
        "Touch context cancelled: source=world-hold reason=UI-contact-active");
    return;
  }
  ClearTouchContext();
  TouchTarget target{.x = x, .y = y};
  if (!TouchTargetIsCurrent(target)) {
    ClearTouchHover();
    return;
  }
  touch_context_ = TouchContext{.target = target, .world_action = std::move(action)};
  PublishTouchHover(x, y);
  if (touch_context_) PresentTouchContext();
}

bool FrameInputRouter::HandlePointerMove(float x, float y, bool touch) {
  if (lua_ == nullptr) {
    return false;
  }
  layout_.SolveIfDirty();
  if (!touch) {
    last_mouse_x_ = x;
    last_mouse_y_ = y;
    have_last_mouse_position_ = true;
  }

  if (active_move_sizing_.active && layout_.UpdateMoveSizing(&active_move_sizing_, x, y)) {

    traversal_.InvalidateHitTest();
    MarkMouseFocusDirty();
    RebuildTraversalIfDirty();
    return true;
  }

  if (!edit_box_drag_select_frame_.empty()) {
    MoveEditBoxCursorToPoint(edit_box_drag_select_frame_, x, y,
                             true);
  }

  for (auto &capture : mouse_button_captures_) {
    if (!capture.active) {
      continue;
    }
    if (capture.is_slider) {

      UpdateSliderValueFromCursor(capture.frame_name, x, y);
      continue;
    }
    if (capture.drag_started) {
      continue;
    }
    const auto ref = frames_.FindLuaRef(capture.frame_name);
    if (!ref.has_value()) {
      capture = {};
      continue;
    }
    const float dx = x - capture.start_x;
    const float dy = y - capture.start_y;
    if (dx * dx + dy * dy < kDragThresholdSquared) {
      continue;
    }
    std::uint32_t drag_mask = 0;
    const int top = lua_gettop(lua_);
    lua_rawgeti(lua_, LUA_REGISTRYINDEX, *ref);
    if (lua_istable(lua_, -1) != 0) {
      lua_getfield(lua_, -1, kRegisteredDragButtonMaskField);
      if (lua_isnumber(lua_, -1) != 0 && lua_tointeger(lua_, -1) > 0) {
        drag_mask = static_cast<std::uint32_t>(lua_tointeger(lua_, -1));
      }
    }
    lua_settop(lua_, top);
    if ((drag_mask & capture.button_flag) != 0) {
      const char *button_name = openwow::ui::widgets::MouseButtonName(capture.button_flag);
      const lua_adapter::ScopedMouseButtonOverride button_override(lua_, button_name);
      (void)FireButton(lua_, *ref, "OnDragStart", button_name);
      capture.drag_started = true;
      capture.start_x = 0.0F;
      capture.start_y = 0.0F;
    }
  }

  RebuildTraversalIfDirty();
  if (!touch) {
    RefreshMouseFocusAt(x, y, true);
    return !mouseover_frame_.empty();
  }
  return true;
}

void FrameInputRouter::CancelPointerCapture(const std::uint32_t button_flag) {
  pressed_button_mask_ &= ~button_flag;
  edit_box_drag_select_frame_.clear();
  if (active_move_sizing_.active) {
    (void)StopFrameMoveSizing(active_move_sizing_.frame_name);
  }

  auto* const capture = FindCapture(button_flag);
  if (lua_ == nullptr || capture == nullptr || !capture->active) {
    if (capture != nullptr) {
      *capture = {};
    }
    return;
  }

  const std::string capture_name = capture->frame_name;
  const char* const button_name =
      openwow::ui::widgets::MouseButtonName(button_flag);
  if (const auto ref = frames_.FindLuaRef(capture_name); ref.has_value()) {
    const lua_adapter::ScopedMouseButtonOverride button_override(lua_, button_name);
    const detail::ScopedCurrentMouseButtonMaskOverride mask_override(
        lua_, LiveButtonMask(button_flag, false));
    if (capture->drag_started) {
      (void)FireNoArg(lua_, *ref, "OnDragStop");
    } else {
      (void)FireButton(lua_, *ref, "OnMouseUp", button_name);
    }
    if (const auto* frame = frames_.FindFrame(capture_name);
        frame != nullptr && IsButtonFrame(*frame)) {
      const auto visual = ReadButtonVisualState(lua_, *ref);
      if (!visual.disabled && !visual.locked) {
        SetButtonVisualState(lua_, *ref, "NORMAL");
      }
    }
  }
  *capture = {};
}

void FrameInputRouter::CancelTouch() {
  const bool had_contact = touch_capture_active_;
  touch_capture_active_ = false;
  const auto gesture = std::exchange(touch_gesture_, std::nullopt);
  if (gesture && lua_ != nullptr &&
      frames_.FindLuaRef(gesture->target.frame_name) == gesture->target.lua_ref) {
    const auto* frame = frames_.FindFrame(gesture->target.frame_name);
    if (frame != nullptr && IsButtonFrame(*frame) &&
        !ReadButtonVisualState(lua_, gesture->target.lua_ref).disabled) {
      SetButtonVisualState(lua_, gesture->target.lua_ref, "NORMAL");
    }
  }
  if (had_contact) CancelPointerCapture(1u);
  ClearTouchContext();
  ClearTouchHover();
}

bool FrameInputRouter::HandleMouseWheel(float x, float y, float delta) {
  if (lua_ == nullptr) {
    return false;
  }
  layout_.SolveIfDirty();
  RebuildTraversalIfDirty();
  const lua_Integer normalized_delta = delta >= 0.0F ? 1 : -1;
  for (const auto &entry : traversal_.input_snapshot()) {
    if (!entry.effective_visible || !entry.uses_mouse_wheel || entry.lua_ref == LUA_NOREF ||
        !traversal_.Contains(entry.key, x, y, viewport_height())) {
      continue;
    }
    if (entry.frame != nullptr && FrameIsWorldFrame(*entry.frame)) {

      (void)FireFrameHandler(lua_, entry.lua_ref, "OnMouseWheel", 1,
                             [&] { lua_pushinteger(lua_, normalized_delta); });
      return false;
    }
    if (FireFrameHandler(lua_, entry.lua_ref, "OnMouseWheel", 1,
                         [&] { lua_pushinteger(lua_, normalized_delta); })) {
      return true;
    }
  }
  return false;
}

bool FrameInputRouter::PushTrackedFrame(lua_State *state, std::string_view frame_name) const {
  if (state == nullptr || frame_name.empty()) {
    return false;
  }
  const auto ref = frames_.FindLuaRef(frame_name);
  if (!ref.has_value()) {
    return false;
  }
  lua_rawgeti(state, LUA_REGISTRYINDEX, *ref);
  if (lua_istable(state, -1) == 0) {
    lua_pop(state, 1);
    return false;
  }
  return true;
}

bool FrameInputRouter::PushFocusedFrame(lua_State *state) const {
  return PushTrackedFrame(state, focused_frame_);
}

bool FrameInputRouter::PushMouseoverFrame(lua_State *state) const {
  return PushTrackedFrame(state, mouseover_frame_);
}

bool FrameInputRouter::mouseover_is_world_frame() const noexcept {
  if (mouseover_frame_.empty()) return false;
  const auto *frame = frames_.FindFrame(mouseover_frame_);
  return frame != nullptr && FrameIsWorldFrame(*frame);
}

void FrameInputRouter::QueueEditBoxCaretRefresh(
    const std::string &frame_name) {
  if (lua_ == nullptr || frame_name.empty()) return;
  const auto *const frame = frames_.FindFrame(frame_name);
  if (frame == nullptr || !FrameIsEditBox(*frame)) return;
  const auto ref = frames_.FindLuaRef(frame_name);
  if (!ref.has_value()) return;
  const int top = lua_gettop(lua_);
  lua_rawgeti(lua_, LUA_REGISTRYINDEX, *ref);
  if (lua_istable(lua_, -1) != 0) {
    QueueEditBoxDirtyState(lua_, -1, false, false, true);
  }
  lua_settop(lua_, top);
}

void FrameInputRouter::TransitionKeyboardFocus(const std::string &new_frame_name) {
  if (new_frame_name == focused_frame_) {
    return;
  }
  const std::string old_frame_name = focused_frame_;
  if (new_frame_name.empty()) {
    focused_frame_.clear();
    QueueEditBoxCaretRefresh(old_frame_name);
    if (const auto ref = frames_.FindLuaRef(old_frame_name); ref.has_value()) {
      (void)FireNoArg(lua_, *ref, "OnEditFocusLost");
    }
    return;
  }
  if (const auto ref = frames_.FindLuaRef(old_frame_name); ref.has_value()) {
    (void)FireNoArg(lua_, *ref, "OnEditFocusLost");
  }
  focused_frame_ = new_frame_name;
  QueueEditBoxCaretRefresh(old_frame_name);
  QueueEditBoxCaretRefresh(focused_frame_);
  if (const auto ref = frames_.FindLuaRef(focused_frame_); ref.has_value()) {
    (void)FireNoArg(lua_, *ref, "OnEditFocusGained");
  }
}

void FrameInputRouter::SetFocus(const std::string &frame_name) {
  if (lua_ == nullptr) {
    return;
  }
  const auto *frame = frames_.FindFrame(frame_name);
  if (frame != nullptr && FrameIsEditBox(*frame) && FrameIsShown(lua_, frames_, frame_name)) {
    TransitionKeyboardFocus(frame_name);
  }
}

void FrameInputRouter::ClearFocus() {
  if (lua_ != nullptr) {
    TransitionKeyboardFocus("");
  }
}

void FrameInputRouter::ClearFocusIfOwnedBy(std::string_view frame_name) {
  if (lua_ != nullptr && frame_name == focused_frame_) {
    TransitionKeyboardFocus("");
  }
}

std::optional<std::string> FrameInputRouter::ResolveModifiedMouseoverUnitToken() {
  if (lua_ == nullptr || mouseover_frame_.empty()) {
    return std::nullopt;
  }
  const auto ref = frames_.FindLuaRef(mouseover_frame_);
  if (!ref.has_value()) {
    return std::nullopt;
  }
  const int top = lua_gettop(lua_);
  lua_getglobal(lua_, "SecureButton_GetModifiedUnit");
  if (lua_isfunction(lua_, -1) == 0) {
    lua_settop(lua_, top);
    return std::nullopt;
  }
  lua_rawgeti(lua_, LUA_REGISTRYINDEX, *ref);
  if (lua_istable(lua_, -1) == 0) {
    lua_settop(lua_, top);
    return std::nullopt;
  }
  const auto button =
      running_macro_input_button_provider_ ? running_macro_input_button_provider_() : std::nullopt;
  if (button.has_value()) {
    const auto value = button->value();
    lua_pushlstring(lua_, value.data(), value.size());
  } else {
    lua_pushnil(lua_);
  }
  std::optional<std::string> token;
  SecureExecution::SecureScope scope;
  if (lua_pcall(lua_, 2, 1, 0) == LUA_OK) {
    if (const char *value = lua_tostring(lua_, -1); value != nullptr && value[0] != '\0') {
      token.emplace(value);
    }
  } else {
    lua_pop(lua_, 1);
  }
  lua_settop(lua_, top);
  return token;
}

void FrameInputRouter::SetRunningMacroInputButtonProvider(
    RunningMacroInputButtonProvider provider) {
  running_macro_input_button_provider_ = std::move(provider);
}

bool FrameInputRouter::BeginFrameMoveSizing(const std::string &name, int mode) {
  if (active_move_sizing_.active) {
    return false;
  }
  layout_.SolveIfDirty();
  RebuildTraversalIfDirty();
  float cursor_x = last_mouse_x_;
  float cursor_y = last_mouse_y_;
  if (!have_last_mouse_position_) {
    const auto [mouse_x, mouse_y] = openwow::input::InputManager::Get().GetMousePosition();
    cursor_x = static_cast<float>(mouse_x);
    cursor_y = static_cast<float>(mouse_y);
  }
  return layout_.BeginMoveSizing(name, mode, cursor_x, cursor_y, &active_move_sizing_);
}

bool FrameInputRouter::StopFrameMoveSizing(const std::string &name) {
  if (!active_move_sizing_.active || active_move_sizing_.frame_name != name) {
    return false;
  }
  const bool committed = layout_.CommitMoveSizing(&active_move_sizing_);
  if (committed) {
    RebuildTraversalIfDirty();
  }
  return committed;
}

void FrameInputRouter::QueueEditBoxStateUpdate(int lua_ref) {
  if (lua_ == nullptr || lua_ref == LUA_NOREF || lua_ref == LUA_REFNIL) {
    return;
  }
  if (pending_edit_box_update_refs_.insert(lua_ref).second) {
    pending_edit_box_updates_.push_back(lua_ref);
  }
}

EditBoxRegionPorts FrameInputRouter::BuildEditBoxRegionPorts(
    const std::string_view edit_box_key) {
  return EditBoxRegionPorts{
      .frames = &frames_,
      .layout = &layout_,
      .traversal = &traversal_,
      .focused = !edit_box_key.empty() && edit_box_key == focused_frame_,
      .application_active = application_active_,
  };
}

void FrameInputRouter::FlushPendingEditBoxStateUpdates() {
  if (lua_ == nullptr) {
    return;
  }
  processing_edit_box_updates_.clear();
  processing_edit_box_updates_.swap(pending_edit_box_updates_);
  for (const int ref : processing_edit_box_updates_) {
    pending_edit_box_update_refs_.erase(ref);
    const int top = lua_gettop(lua_);
    lua_rawgeti(lua_, LUA_REGISTRYINDEX, ref);
    if (lua_istable(lua_, -1) != 0) {

      const char *const borrowed =
          frame_api::GetFrameRuntimeKeyOrName(lua_, lua_absindex(lua_, -1));
      const std::string edit_box_key = borrowed != nullptr ? borrowed : "";
      FlushEditBoxDirtyState(lua_, -1, edit_box_key,
                             BuildEditBoxRegionPorts(edit_box_key));
    }
    lua_settop(lua_, top);
  }
  processing_edit_box_updates_.clear();
}

void FrameInputRouter::MoveEditBoxCursorToPoint(const std::string &frame_name,
                                                const float x, const float y,
                                                const bool extend) {
  if (lua_ == nullptr || frame_name.empty()) return;
  const auto ref = frames_.FindLuaRef(frame_name);
  if (!ref.has_value()) return;
  layout_.SolveIfDirty();
  const int top = lua_gettop(lua_);
  lua_rawgeti(lua_, LUA_REGISTRYINDEX, *ref);
  if (lua_istable(lua_, -1) == 0) {
    lua_settop(lua_, top);
    return;
  }
  const int edit_box = lua_absindex(lua_, -1);

  lua_getfield(lua_, edit_box, "__ow_eb_ime_composing");
  const bool composing = lua_toboolean(lua_, -1) != 0;
  lua_pop(lua_, 1);
  if (composing) {
    lua_settop(lua_, top);
    return;
  }
  const auto index = ResolveEditBoxCharacterIndexAtPoint(lua_, edit_box,
                                                         frame_name, x, y,
                                                         layout_);
  if (!index.has_value()) {
    lua_settop(lua_, top);
    return;
  }
  lua_getfield(lua_, edit_box, "__ow_eb_text");
  std::size_t text_size = 0;
  (void)lua_tolstring(lua_, -1, &text_size);
  lua_pop(lua_, 1);
  const int clamped =
      std::clamp(*index, 0, static_cast<int>(text_size));

  int selection_start = clamped;
  int selection_end = clamped;
  if (extend) {
    const auto read = [&](const char *field) {
      lua_getfield(lua_, edit_box, field);
      const int value = lua_isnumber(lua_, -1) != 0
                            ? static_cast<int>(lua_tointeger(lua_, -1))
                            : clamped;
      lua_pop(lua_, 1);
      return std::clamp(value, 0, static_cast<int>(text_size));
    };
    selection_start = read("__ow_eb_hl_start");
    selection_end = read("__ow_eb_hl_end");
    if (selection_start > selection_end) {
      std::swap(selection_start, selection_end);
    }
    if (clamped < selection_start) {
      selection_start = clamped;
    } else if (clamped > selection_end) {
      selection_end = clamped;
    } else {
      selection_start = clamped;
    }
  }
  lua_pushinteger(lua_, clamped);
  lua_setfield(lua_, edit_box, "__ow_eb_cursor");
  lua_pushinteger(lua_, selection_start);
  lua_setfield(lua_, edit_box, "__ow_eb_hl_start");
  lua_pushinteger(lua_, selection_end);
  lua_setfield(lua_, edit_box, "__ow_eb_hl_end");
  QueueEditBoxDirtyState(lua_, edit_box, false, false, true);
  lua_settop(lua_, top);
}

void FrameInputRouter::PlaceEditBoxCursorFromClick(
    const std::string &frame_name, const float x, const float y) {
  MoveEditBoxCursorToPoint(frame_name, x, y, false);

  edit_box_drag_select_frame_ = frame_name;
}

void FrameInputRouter::SetApplicationActive(const bool active) {
  if (application_active_ == active) return;
  application_active_ = active;
  if (!application_active_) {
    keyboard_captures_.clear();
    CancelTouch();
  }

  QueueEditBoxCaretRefresh(focused_frame_);
}

void FrameInputRouter::UpdateFocusedEditBoxCaretBlink(const float dt) {
  if (lua_ == nullptr || focused_frame_.empty()) {
    return;
  }
  const auto *const frame = frames_.FindFrame(focused_frame_);
  if (frame == nullptr || !FrameIsEditBox(*frame)) {
    return;
  }
  const auto ref = frames_.FindLuaRef(focused_frame_);
  if (!ref.has_value()) {
    return;
  }
  const int top = lua_gettop(lua_);
  lua_rawgeti(lua_, LUA_REGISTRYINDEX, *ref);
  if (lua_istable(lua_, -1) != 0) {
    UpdateEditBoxCaretBlink(lua_, -1, focused_frame_, dt,
                            BuildEditBoxRegionPorts(focused_frame_));
  }
  lua_settop(lua_, top);
}

void FrameInputRouter::UpdateFocusedEditBoxInputLanguage() {
  if (lua_ == nullptr || focused_frame_.empty() || !FrameIsShown(lua_, frames_, focused_frame_)) {
    return;
  }
  const auto *frame = frames_.FindFrame(focused_frame_);
  const auto ref = frames_.FindLuaRef(focused_frame_);
  if (frame == nullptr || !FrameIsEditBox(*frame) || !ref.has_value()) {
    return;
  }
  const char *next = openwow::ui::widgets::EditBoxInputLanguageToken(
      openwow::ui::widgets::DetectActiveEditBoxInputLanguage());
  std::string current = openwow::ui::widgets::EditBoxInputLanguageToken(
      openwow::ui::widgets::EditBoxInputLanguage::Roman);
  const int top = lua_gettop(lua_);
  lua_rawgeti(lua_, LUA_REGISTRYINDEX, *ref);
  if (lua_istable(lua_, -1) == 0) {
    lua_settop(lua_, top);
    return;
  }
  lua_getfield(lua_, -1, "__ow_eb_input_language");
  if (const char *stored = lua_tostring(lua_, -1); stored != nullptr) {
    current = stored;
  }
  lua_pop(lua_, 1);
  const bool changed = current != next;
  if (changed || current.empty()) {
    lua_pushstring(lua_, next);
    lua_setfield(lua_, -2, "__ow_eb_input_language");
  }
  lua_settop(lua_, top);
  if (changed) {
    (void)FireString(lua_, *ref, "OnInputLanguageChanged", next);
  }
}

bool FrameInputRouter::FocusedFrameIsEffectivelyVisible() const {
  return !focused_frame_.empty() && traversal_.IsEffectivelyVisible(focused_frame_);
}

void FrameInputRouter::ReconcileFrameInputMutation(std::string_view frame_name,
                                                   bool focused_was_effectively_visible) {
  MarkMouseFocusDirty();
  const auto unavailable = [&](const TouchTarget& target, const bool needs_mouse) {
    return !target.frame_name.empty() &&
        (!traversal_.IsEffectivelyVisible(target.frame_name) ||
         (needs_mouse && target.hyperlink_link.empty() &&
          !FrameUsesMouse(lua_, frames_, target.frame_name)));
  };
  if ((touch_gesture_ && unavailable(touch_gesture_->target,
                                    touch_gesture_->phase != TouchPhase::kScroll)) ||
      (touch_context_ && unavailable(touch_context_->target, true))) {
    CancelTouch();
  }
  for (auto &capture : mouse_button_captures_) {
    if (capture.active && capture.frame_name == frame_name &&
        !FrameUsesMouse(lua_, frames_, frame_name)) {
      capture = {};
    }
  }
  if (mouseover_frame_ == frame_name &&
      !(FrameIsShown(lua_, frames_, frame_name) && FrameUsesMouse(lua_, frames_, frame_name))) {
    const auto ref = frames_.FindLuaRef(frame_name);

    mouseover_frame_.clear();
    if (ref.has_value()) {
      SetFrameHighlightForMouseFocus(lua_, *ref, false);
      FireBoolean(lua_, *ref, "OnLeave", false);
    }
  }
  if (focused_was_effectively_visible && !focused_frame_.empty() &&
      !traversal_.IsEffectivelyVisible(focused_frame_)) {
    TransitionKeyboardFocus("");
  }
}

void FrameInputRouter::BeforeFrameBindingRelease(int lua_ref) {
  if (lua_ref == LUA_NOREF || lua_ref == LUA_REFNIL) {
    return;
  }
  if ((touch_gesture_ && touch_gesture_->target.lua_ref == lua_ref) ||
      (touch_context_ && touch_context_->target.lua_ref == lua_ref)) {
    CancelTouch();
  }
  pending_edit_box_update_refs_.erase(lua_ref);
  pending_edit_box_updates_.erase(
      std::remove(pending_edit_box_updates_.begin(), pending_edit_box_updates_.end(), lua_ref),
      pending_edit_box_updates_.end());
}

void FrameInputRouter::AfterFrameIdentityRelease(std::string_view frame_name) {
  MarkMouseFocusDirty();
  if (mouseover_frame_ == frame_name) {
    mouseover_frame_.clear();
  }
  if (hovered_hyperlink_.has_value() &&
      hovered_hyperlink_->frame_name == frame_name) {
    hovered_hyperlink_.reset();
  }
  if (focused_frame_ == frame_name) {
    focused_frame_.clear();
  }
  for (auto &[key, capture] : keyboard_captures_) {
    static_cast<void>(key);
    if (capture.frame_name == frame_name) {
      capture.frame_name.clear();
      capture.dispatch_key_handlers = false;
    }
  }
  if (active_move_sizing_.frame_name == frame_name) {
    active_move_sizing_ = {};
  }
  for (auto &capture : mouse_button_captures_) {
    if (capture.frame_name == frame_name) {
      capture = {};
    }
  }
}

}
