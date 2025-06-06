// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/base/accelerators/accelerator.h"

#include <string>

#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/event.h"
#include "ui/events/types/event_type.h"

namespace ui {

TEST(AcceleratorTest, Repeat) {
  const Accelerator accelerator_a(VKEY_A, EF_NONE);
  EXPECT_FALSE(accelerator_a.IsRepeat());

  const Accelerator accelerator_b(VKEY_B, EF_IS_REPEAT);
  EXPECT_TRUE(accelerator_b.IsRepeat());

  const Accelerator accelerator_b_copy(accelerator_b);
  EXPECT_TRUE(accelerator_b_copy.IsRepeat());
}

TEST(AcceleratorTest, TimeStamp) {
  const Accelerator accelerator_a(VKEY_A, EF_NONE);
  EXPECT_EQ(base::TimeTicks(), accelerator_a.time_stamp());

  const base::TimeTicks event_time = base::TimeTicks() + base::Milliseconds(1);
  KeyEvent keyevent(EventType::kKeyPressed, VKEY_SPACE, EF_NONE, event_time);

  const Accelerator accelerator_b(keyevent);
  EXPECT_EQ(event_time, accelerator_b.time_stamp());
}

// Crash on Android builders. https://crbug.com/980267
#if BUILDFLAG(IS_ANDROID)
#define MAYBE_GetShortcutText DISABLED_GetShortcutText
#else
#define MAYBE_GetShortcutText GetShortcutText
#endif
TEST(AcceleratorTest, MAYBE_GetShortcutText) {
  struct {
    KeyboardCode code;
    int modifiers;
    const char16_t* expected_long;
    const char16_t* expected_short;
  } keys[] = {
      {VKEY_Q, EF_CONTROL_DOWN | EF_SHIFT_DOWN, u"Ctrl+Shift+Q", u"⌃⇧Q"},
      {VKEY_A, EF_ALT_DOWN | EF_SHIFT_DOWN, u"Alt+Shift+A", u"⌥⇧A"},
      // Regression test for https://crbug.com/867732:
      {VKEY_OEM_COMMA, EF_CONTROL_DOWN, u"Ctrl+Comma", u"⌃,"},
#if BUILDFLAG(IS_MAC)
      {VKEY_T, EF_COMMAND_DOWN | EF_CONTROL_DOWN, nullptr, u"⌃⌘T"},
#endif
#if BUILDFLAG(IS_LINUX)
      {VKEY_T, EF_COMMAND_DOWN | EF_CONTROL_DOWN, u"Super+Ctrl+T", nullptr},
#endif
  };

  for (const auto& key : keys) {
    std::u16string text =
        Accelerator(key.code, key.modifiers).GetShortcutText();
#if BUILDFLAG(IS_MAC)
    EXPECT_EQ(text, key.expected_short);
#else
    EXPECT_EQ(text, key.expected_long);
#endif
  }
}

TEST(AcceleratorTest, ShortcutTextForUnknownKey) {
  const Accelerator accelerator(VKEY_UNKNOWN, EF_NONE);
  EXPECT_EQ(std::u16string(), accelerator.GetShortcutText());
}

TEST(AcceleratorTest, VerifyToKeyEventConstructor) {
  const Accelerator accelerator(VKEY_Z, EF_COMMAND_DOWN,
                                Accelerator::KeyState::RELEASED,
                                base::TimeTicks());
  KeyEvent key_event = accelerator.ToKeyEvent();
  // Check key event fields to verift if the right constructor is called.
  EXPECT_EQ(key_event.key_code(), VKEY_Z);
  EXPECT_EQ(key_event.Clone()->type(), ui::EventType::kKeyReleased);
  EXPECT_FALSE(key_event.is_char());
}

TEST(AcceleratorTest, ConversionFromKeyEvent) {
  ui::KeyEvent key_event(
      ui::EventType::kKeyPressed, ui::VKEY_F,
      ui::EF_ALT_DOWN | ui::EF_CONTROL_DOWN | ui::EF_FUNCTION_DOWN);
  Accelerator accelerator(key_event);

  EXPECT_EQ(accelerator.key_code(), ui::VKEY_F);
  EXPECT_EQ(accelerator.modifiers(),
            ui::EF_ALT_DOWN | ui::EF_CONTROL_DOWN | ui::EF_FUNCTION_DOWN);
}

#if BUILDFLAG(IS_MAC)
class AcceleratorTestMac : public testing::Test {
 public:
  AcceleratorTestMac() = default;
  ~AcceleratorTestMac() override = default;

  // Returns a "short" vector representation of the modifier flags in
  // |modifier_mask|.
  std::vector<std::u16string> ShortFormStringForModifiers(int modifier_flags) {
    ui::KeyEvent key_event(ui::EventType::kKeyPressed, ui::VKEY_F,
                           modifier_flags);
    Accelerator accelerator(key_event);

    return accelerator.GetShortFormModifiers();
  }
};

// Checks that a string representation exists for all modifier masks that make
// sense on the Mac.
TEST_F(AcceleratorTestMac, ModifierFlagsShortFormRepresentation) {
  int modifier_flag = 1 << 0;
  while (modifier_flag) {
    // If |modifier_flag| is a valid modifier flag and it's not EF_ALTGR_DOWN
    // (the Linux Alt key on the right side of the keyboard), confirm that
    // a string representation for the modifier flag exists.
    if (Accelerator::MaskOutKeyEventFlags(modifier_flag) &&
        modifier_flag != EF_ALTGR_DOWN) {
      EXPECT_GT(this->ShortFormStringForModifiers(modifier_flag).size(), 0UL);
    }
    modifier_flag <<= 1;
  }
}
#endif

#if BUILDFLAG(IS_CHROMEOS)
TEST(AcceleratorTest, ConversionFromKeyEvent_Ash) {
  ui::KeyEvent key_event(ui::EventType::kKeyPressed, ui::VKEY_F,
                         ui::EF_ALT_DOWN | ui::EF_CONTROL_DOWN);
  Accelerator accelerator(key_event);

  EXPECT_EQ(accelerator.key_code(), ui::VKEY_F);
  EXPECT_EQ(accelerator.modifiers(), ui::EF_ALT_DOWN | ui::EF_CONTROL_DOWN);

  // Code is set when converting from a KeyEvent.
  EXPECT_EQ(accelerator.code(), DomCode::US_F);

  // Test resetting code.
  accelerator.reset_code();
  EXPECT_EQ(accelerator.code(), DomCode::NONE);
}
#endif  // BUILDFLAG(IS_CHROMEOS)

}  // namespace ui
