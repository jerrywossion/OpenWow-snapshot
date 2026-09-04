#include "mobile_ios_platform.h"

#import <UIKit/UIKit.h>

namespace openwow::client::mobile {

SafeAreaInsetsPoints QuerySafeAreaInsetsPoints(void* native_window) noexcept {
  if (native_window == nullptr) {
    return {};
  }

  @autoreleasepool {
    UIWindow* window = (__bridge UIWindow*)native_window;
    UIView* safe_area_view = window.rootViewController.view;
    if (safe_area_view == nil) {
      safe_area_view = window;
    }
    [safe_area_view layoutIfNeeded];
    const UIEdgeInsets insets = safe_area_view.safeAreaInsets;
    return {
        .left = static_cast<float>(insets.left),
        .top = static_cast<float>(insets.top),
        .right = static_cast<float>(insets.right),
        .bottom = static_cast<float>(insets.bottom),
    };
  }
}

}
