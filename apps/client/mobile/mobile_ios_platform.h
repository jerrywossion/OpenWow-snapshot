#pragma once

namespace openwow::client::mobile {

struct SafeAreaInsetsPoints {
  float left{0.0F};
  float top{0.0F};
  float right{0.0F};
  float bottom{0.0F};
};

enum class HapticFeedback {
  kSelection,
  kLightImpact,
};

// native_window is the UIWindow returned by WindowManager on iOS. This bridge
// deliberately exposes platform metrics, not UIKit views, to the game UI.
[[nodiscard]] SafeAreaInsetsPoints QuerySafeAreaInsetsPoints(
    void* native_window) noexcept;
void PerformHapticFeedback(HapticFeedback feedback) noexcept;

}
