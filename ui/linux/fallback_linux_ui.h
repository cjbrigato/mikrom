// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_LINUX_FALLBACK_LINUX_UI_H_
#define UI_LINUX_FALLBACK_LINUX_UI_H_

#include <optional>

#include "ui/gfx/font_render_params.h"
#include "ui/linux/linux_ui.h"

namespace ui {

// This class is meant to be overridden by tests.  It's provided as a
// convenience so that tests don't have to stub lots of methods just to override
// a single one.
class FallbackLinuxUi : public LinuxUiAndTheme {
 public:
  FallbackLinuxUi();
  ~FallbackLinuxUi() override;

  // ui::LinuxUi:
  bool Initialize() override;
  void InitializeFontSettings() override;
  base::TimeDelta GetCursorBlinkInterval() const override;
  gfx::Image GetIconForContentType(const std::string& content_type,
                                   int size,
                                   float scale) const override;
  base::flat_map<std::string, std::string> GetKeyboardLayoutMap() override;
#if BUILDFLAG(ENABLE_PRINTING)
  printing::PrintDialogLinuxInterface* CreatePrintDialog(
      printing::PrintingContextLinux* context) override;
  gfx::Size GetPdfPaperSize(printing::PrintingContextLinux* context) override;
#endif
  ui::SelectFileDialog* CreateSelectFileDialog(
      void* listener,
      std::unique_ptr<ui::SelectFilePolicy> policy) const override;
  std::string GetCursorThemeName() override;
  int GetCursorThemeSize() override;
  std::unique_ptr<ui::LinuxInputMethodContext> CreateInputMethodContext(
      ui::LinuxInputMethodContextDelegate* delegate) const override;
  ui::TextEditCommand GetTextEditCommandForEvent(const ui::Event& event,
                                                 int text_flags) override;
  gfx::FontRenderParams GetDefaultFontRenderParams() override;
  bool AnimationsEnabled() const override;
  void AddWindowButtonOrderObserver(
      ui::WindowButtonOrderObserver* observer) override;
  void RemoveWindowButtonOrderObserver(
      ui::WindowButtonOrderObserver* observer) override;
  WindowFrameAction GetWindowFrameAction(
      WindowFrameActionSource source) override;
  std::vector<std::string> GetCmdLineFlagsForCopy() const override;

  // ui::LinuxUiTheme:
  ui::NativeTheme* GetNativeTheme() const override;
  bool GetColor(int id, SkColor* color, bool use_custom_frame) const override;
  bool GetDisplayProperty(int id, int* result) const override;
  void GetFocusRingColor(SkColor* color) const override;
  void GetActiveSelectionBgColor(SkColor* color) const override;
  void GetActiveSelectionFgColor(SkColor* color) const override;
  void GetInactiveSelectionBgColor(SkColor* color) const override;
  void GetInactiveSelectionFgColor(SkColor* color) const override;
  bool PreferDarkTheme() const override;
  void SetDarkTheme(bool dark) override;
  void SetAccentColor(std::optional<SkColor> accent_color) override;
  std::unique_ptr<ui::NavButtonProvider> CreateNavButtonProvider() override;
  ui::WindowFrameProvider* GetWindowFrameProvider(bool solid_frame,
                                                  bool tiled,
                                                  bool maximized) override;

 private:
  std::optional<gfx::FontRenderParams> default_font_render_params_;
  bool theme_is_dark_ = false;
};

}  // namespace ui

#endif  // UI_LINUX_FALLBACK_LINUX_UI_H_
