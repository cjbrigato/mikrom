// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "build/build_config.h"
#include "ui/views/buildflags.h"
#include "ui/views/test/test_views_delegate.h"

#if BUILDFLAG(ENABLE_DESKTOP_AURA)
#include "ui/views/widget/desktop_aura/desktop_native_widget_aura.h"
#endif  // BUILDFLAG(ENABLE_DESKTOP_AURA)

namespace views {

TestViewsDelegate::TestViewsDelegate() = default;

TestViewsDelegate::~TestViewsDelegate() = default;

#if BUILDFLAG(IS_WIN)
HICON TestViewsDelegate::GetSmallWindowIcon() const {
  return nullptr;
}
#endif

void TestViewsDelegate::OnBeforeWidgetInit(
    Widget::InitParams* params,
    internal::NativeWidgetDelegate* delegate) {
#if BUILDFLAG(IS_CHROMEOS)
  if (!params->parent && !params->context) {
    params->context = context_;
  }
#endif
  if (params->opacity == Widget::InitParams::WindowOpacity::kInferred) {
    params->opacity = use_transparent_windows_
                          ? Widget::InitParams::WindowOpacity::kTranslucent
                          : Widget::InitParams::WindowOpacity::kOpaque;
  }
#if BUILDFLAG(ENABLE_DESKTOP_AURA)
  if (!params->native_widget && use_desktop_native_widgets_) {
    params->native_widget = new DesktopNativeWidgetAura(delegate);
  }
#endif  // BUILDFLAG(ENABLE_DESKTOP_AURA)
}

}  // namespace views
