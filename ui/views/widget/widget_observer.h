// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_WIDGET_WIDGET_OBSERVER_H_
#define UI_VIEWS_WIDGET_WIDGET_OBSERVER_H_

#include "base/observer_list_types.h"
#include "ui/views/views_export.h"

namespace gfx {
class Rect;
}

namespace views {

class Widget;

// Observers can listen to various events on the Widgets.
class VIEWS_EXPORT WidgetObserver : public base::CheckedObserver {
 public:
  // The closing notification is sent immediately in response to (i.e. in the
  // same call stack as) a request to close the Widget (via Close() or
  // CloseNow()).
  // TODO(crbug.com/40194222): Remove this, this API is too scary. Users of this
  // API can expect it to always be called, but it's only called on the same
  // stack as a close request. If the Widget closes due to OS native-widget
  // destruction this is never called. Replace existing uses with
  // OnWidgetDestroying() or by using ViewTrackers to track View lifetimes.
  // DEPRECATED. Don't use this. See Widget::MakeCloseSynchronous() for
  // details.
  virtual void OnWidgetClosing(Widget* widget) {}

  // Invoked after notification is received from the event loop that the native
  // widget has been created.
  virtual void OnWidgetCreated(Widget* widget) {}

  // The destroying event occurs immediately before the widget is destroyed.
  // This typically occurs asynchronously with respect the the close request, as
  // a result of a later invocation from the event loop.
  // DEPRECATED. Don't use this. See Widget::MakeCloseSynchronous() for
  // details.
  virtual void OnWidgetDestroying(Widget* widget) {}

  // Invoked after notification is received from the event loop that the native
  // widget has been destroyed.
  // This method is guaranteed to be called exactly once before widget
  // destruction for every Widget, regardless of InitParams::Ownership. This is
  // called before ~Widget() for NATIVE_WIDGET_OWNS_WIDGET and
  // WIDGET_OWNS_NATIVE_WIDGET and early in ~Widget() for CLIENT_OWNS_WIDGET.
  // Don't subclass views::Widget, that will cause problems for many reasons,
  // one of which is that subclass constructors run before parent class
  // constructors.
  virtual void OnWidgetDestroyed(Widget* widget) {}

  // Called before RunShellDrag() is called and after it returns.
  virtual void OnWidgetDragWillStart(Widget* widget) {}
  virtual void OnWidgetDragComplete(Widget* widget) {}

  // Called when Widget::IsVisible() changed.
  virtual void OnWidgetVisibilityChanged(Widget* widget, bool visible) {}

  // Called when Widget::IsVisibleOnScreen() changed. Only supported on macOS.
  // TODO(crbug.com/410938804): supports other platforms.
  virtual void OnWidgetVisibilityOnScreenChanged(Widget* widget, bool visible) {
  }

  virtual void OnWidgetActivationChanged(Widget* widget, bool active) {}

  // Invoked when any widget within the tree, rooted at `root_widget`, becomes
  // active. A widget tree is considered active if any widget in the tree is
  // active. `active_widget` is the widget that has just become active.
  virtual void OnWidgetTreeActivated(Widget* root_widget,
                                     Widget* active_widget) {}

  virtual void OnWidgetBoundsChanged(Widget* widget,
                                     const gfx::Rect& new_bounds) {}

  // Invoked when the user started resizing the window.
  virtual void OnWidgetUserResizeStarted() {}

  // Invoked when the user stopped resizing the window.
  virtual void OnWidgetUserResizeEnded() {}

  virtual void OnWidgetThemeChanged(Widget* widget) {}

  virtual void OnWidgetSizeConstraintsChanged(Widget* widget) {}

  // Invoked when a display-state affecting change happens. This can happen when
  // either `ui::mojom::WindowShowState` or `ui::PlatformWindowState` changes
  // depending on the platform in question.
  virtual void OnWidgetShowStateChanged(Widget* widget) {}

  // Called when `widget` becomes parent of `child`.
  virtual void OnWidgetChildAdded(Widget* widget, Widget* child) {}

  // Called when `widget` stopes being the parent of `child`, including when
  // `child` is destroying.
  virtual void OnWidgetChildRemoved(Widget* widget, Widget* child) {}

  // Called when a window-modal child widget's visibility changes.
  virtual void OnWidgetWindowModalVisibilityChanged(Widget* widget,
                                                    bool visible) {}

 protected:
  ~WidgetObserver() override = default;
};

}  // namespace views

#endif  // UI_VIEWS_WIDGET_WIDGET_OBSERVER_H_
