// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_MENUS_COCOA_TEXT_SERVICES_CONTEXT_MENU_H_
#define UI_MENUS_COCOA_TEXT_SERVICES_CONTEXT_MENU_H_

#include <string_view>

#include "base/component_export.h"
#include "base/i18n/rtl.h"
#include "base/memory/raw_ptr.h"
#include "ui/menus/simple_menu_model.h"

namespace ui {

// This class is used to append and handle the Speech and BiDi submenu for the
// context menu.
class COMPONENT_EXPORT(UI_MENUS) TextServicesContextMenu
    : public SimpleMenuModel::Delegate {
 public:
  enum MenuCommands {
    // These must not overlap with the command IDs used by other menus that
    // incorporate text services.
    // TODO(ellyjones): This is an ugly global dependency, especially on
    // //ui/views. What can we do about this? Can we get rid of the global
    // implicit namespace of command IDs?
    kSpeechMenu = 100,
    kSpeechStartSpeaking,
    kSpeechStopSpeaking,

    kWritingDirectionMenu,
    kWritingDirectionDefault,
    kWritingDirectionLtr,
    kWritingDirectionRtl,
  };

  class COMPONENT_EXPORT(UI_MENUS) Delegate {
   public:
    // Returns the selected text.
    virtual std::u16string_view GetSelectedText() const = 0;

    // Returns true if |direction| should be enabled in the BiDi submenu.
    virtual bool IsTextDirectionEnabled(
        base::i18n::TextDirection direction) const = 0;

    // Returns true if |direction| should be checked in the BiDi submenu.
    virtual bool IsTextDirectionChecked(
        base::i18n::TextDirection direction) const = 0;

    // Updates the text direction to |direction|.
    virtual void UpdateTextDirection(base::i18n::TextDirection direction) = 0;
  };

  explicit TextServicesContextMenu(Delegate* delegate);

  TextServicesContextMenu(const TextServicesContextMenu&) = delete;
  TextServicesContextMenu& operator=(const TextServicesContextMenu&) = delete;

  // Methods for speaking.
  static void SpeakText(std::u16string_view text);
  static void StopSpeaking();
  static bool IsSpeaking();

  // Appends text service items to |model|. A submenu is added for speech,
  // which |this| serves as the delegate for.
  void AppendToContextMenu(SimpleMenuModel* model);

  // Appends items to the context menu applicable to editable content. A
  // submenu is added for bidirection, which |this| serves as a delegate for.
  void AppendEditableItems(SimpleMenuModel* model);

  // Returns true if |command_id| is handled by this class.
  bool SupportsCommand(int command_id) const;

  // SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;
  bool IsCommandIdEnabled(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  // Model for the "Speech" submenu.
  ui::SimpleMenuModel speech_submenu_model_;

  // Model for the BiDi input submenu.
  ui::SimpleMenuModel bidi_submenu_model_;

  raw_ptr<Delegate> delegate_;  // Weak.
};

}  // namespace ui

#endif  // UI_MENUS_COCOA_TEXT_SERVICES_CONTEXT_MENU_H_
