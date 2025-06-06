// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/wayland/host/wayland_input_method_context.h"

#include <text-input-unstable-v1-server-protocol.h>
#include <wayland-server.h>

#include <memory>
#include <optional>
#include <string_view>

#include "base/environment.h"
#include "base/i18n/break_iterator.h"
#include "base/memory/raw_ptr.h"
#include "base/nix/xdg_util.h"
#include "base/strings/utf_string_conversions.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/ime/ime_text_span.h"
#include "ui/base/ime/linux/linux_input_method_context.h"
#include "ui/base/ime/text_input_client.h"
#include "ui/base/ime/text_input_flags.h"
#include "ui/base/ime/text_input_type.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/ozone/events_ozone.h"
#include "ui/gfx/range/range.h"
#include "ui/ozone/platform/wayland/host/wayland_event_source.h"
#include "ui/ozone/platform/wayland/host/wayland_seat.h"
#include "ui/ozone/platform/wayland/host/wayland_window.h"
#include "ui/ozone/platform/wayland/host/zwp_text_input_v1.h"
#include "ui/ozone/platform/wayland/test/mock_surface.h"
#include "ui/ozone/platform/wayland/test/mock_zwp_text_input.h"
#include "ui/ozone/platform/wayland/test/test_wayland_server_thread.h"
#include "ui/ozone/platform/wayland/test/wayland_test.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::InSequence;
using ::testing::Mock;
using ::testing::Optional;
using ::testing::SaveArg;
using ::testing::Values;

namespace ui {

// Returns the number of grapheme clusters in the text.
std::optional<size_t> CountGraphemeCluster(std::u16string_view text) {
  base::i18n::BreakIterator iter(text,
                                 base::i18n::BreakIterator::BREAK_CHARACTER);
  if (!iter.Init())
    return std::nullopt;
  size_t result = 0;
  while (iter.Advance())
    ++result;
  return result;
}

// TODO(crbug.com/40240866): Subclass FakeTextInputClient after pruning deps.
class MockTextInputClient : public TextInputClient {
 public:
  explicit MockTextInputClient(TextInputType text_input_type) {
    text_input_type_ = text_input_type;
  }
  MockTextInputClient(const MockTextInputClient& other) = delete;
  MockTextInputClient& operator=(const MockTextInputClient& other) = delete;
  ~MockTextInputClient() override = default;

  TextInputType GetTextInputType() const override { return text_input_type_; }

  base::WeakPtr<TextInputClient> AsWeakPtr() override {
    return weak_ptr_factory_.GetWeakPtr();
  }

  MOCK_METHOD(void,
              SetCompositionText,
              (const ui::CompositionText&),
              (override));
  MOCK_METHOD(size_t, ConfirmCompositionText, (bool), (override));
  MOCK_METHOD(void, ClearCompositionText, (), (override));
  MOCK_METHOD(void,
              InsertText,
              (const std::u16string&,
               ui::TextInputClient::InsertTextCursorBehavior cursor_behavior),
              (override));
  MOCK_METHOD(void, InsertChar, (const ui::KeyEvent&), (override));
  MOCK_METHOD(ui::TextInputMode, GetTextInputMode, (), (const, override));
  MOCK_METHOD(base::i18n::TextDirection,
              GetTextDirection,
              (),
              (const, override));
  MOCK_METHOD(int, GetTextInputFlags, (), (const, override));
  MOCK_METHOD(bool, CanComposeInline, (), (const, override));
  MOCK_METHOD(gfx::Rect, GetCaretBounds, (), (const, override));
  MOCK_METHOD(gfx::Rect, GetSelectionBoundingBox, (), (const, override));
  MOCK_METHOD(bool,
              GetCompositionCharacterBounds,
              (size_t, gfx::Rect*),
              (const, override));
  MOCK_METHOD(bool, HasCompositionText, (), (const, override));
  MOCK_METHOD(ui::TextInputClient::FocusReason,
              GetFocusReason,
              (),
              (const, override));
  MOCK_METHOD(bool, GetTextRange, (gfx::Range*), (const, override));
  MOCK_METHOD(bool, GetCompositionTextRange, (gfx::Range*), (const, override));
  MOCK_METHOD(bool,
              GetEditableSelectionRange,
              (gfx::Range*),
              (const, override));
  MOCK_METHOD(bool, SetEditableSelectionRange, (const gfx::Range&), (override));
  MOCK_METHOD(bool,
              GetTextFromRange,
              (const gfx::Range&, std::u16string*),
              (const, override));
  MOCK_METHOD(void, OnInputMethodChanged, (), (override));
  MOCK_METHOD(bool,
              ChangeTextDirectionAndLayoutAlignment,
              (base::i18n::TextDirection),
              (override));
  MOCK_METHOD(void, ExtendSelectionAndDelete, (size_t, size_t), (override));
  MOCK_METHOD(void, EnsureCaretNotInRect, (const gfx::Rect&), (override));
  MOCK_METHOD(bool,
              IsTextEditCommandEnabled,
              (TextEditCommand),
              (const, override));
  MOCK_METHOD(void,
              SetTextEditCommandForNextKeyEvent,
              (TextEditCommand),
              (override));
  MOCK_METHOD(ukm::SourceId, GetClientSourceForMetrics, (), (const, override));
  MOCK_METHOD(bool, ShouldDoLearning, (), (override));
  MOCK_METHOD(bool,
              SetCompositionFromExistingText,
              (const gfx::Range&, const std::vector<ui::ImeTextSpan>&),
              (override));
#if BUILDFLAG(IS_CHROMEOS)
  MOCK_METHOD(gfx::Range, GetAutocorrectRange, (), (const, override));
  MOCK_METHOD(gfx::Rect, GetAutocorrectCharacterBounds, (), (const, override));
  MOCK_METHOD(bool, SetAutocorrectRange, (const gfx::Range& range), (override));
  MOCK_METHOD(void,
              GetActiveTextInputControlLayoutBounds,
              (std::optional<gfx::Rect> * control_bounds,
               std::optional<gfx::Rect>* selection_bounds),
              (override));
#endif

 private:
  TextInputType text_input_type_;
  base::WeakPtrFactory<MockTextInputClient> weak_ptr_factory_{this};
};

class MockZwpTextInputV3 : public ZwpTextInputV3 {
 public:
  ~MockZwpTextInputV3() override = default;

  MOCK_METHOD(void, SetClient, (ZwpTextInputV3Client * context), (override));

  MOCK_METHOD(void,
              OnClientDestroyed,
              (ZwpTextInputV3Client * context),
              (override));

  MOCK_METHOD(void, Enable, (), (override));
  MOCK_METHOD(void, Disable, (), (override));
  MOCK_METHOD(void, Reset, (), (override));

  MOCK_METHOD(void, SetCursorRect, (const gfx::Rect& rect), (override));
  MOCK_METHOD(void,
              SetSurroundingText,
              (const std::string& text,
               const gfx::Range& preedit_range,
               const gfx::Range& selection_range),
              (override));
  MOCK_METHOD(bool, HasAdvancedSurroundingTextSupport, (), (const override));
  MOCK_METHOD(void,
              SetContentType,
              (ui::TextInputType type, uint32_t flags, bool should_do_learning),
              (override));
};

class TestInputMethodContextDelegate : public LinuxInputMethodContextDelegate {
 public:
  explicit TestInputMethodContextDelegate(gfx::AcceleratedWidget window_key)
      : client_window_key_(window_key) {}
  TestInputMethodContextDelegate(const TestInputMethodContextDelegate&) =
      delete;
  TestInputMethodContextDelegate& operator=(
      const TestInputMethodContextDelegate&) = delete;
  ~TestInputMethodContextDelegate() override = default;

  void OnCommit(const std::u16string& text) override {
    was_on_commit_called_ = true;
    last_commit_text_ = text;
  }
  void OnConfirmCompositionText(bool keep_selection) override {
    last_on_confirm_composition_arg_ = keep_selection;
  }
  void OnPreeditChanged(const ui::CompositionText& composition_text) override {
    was_on_preedit_changed_called_ = true;
    last_on_preedit_changed_args_ = composition_text;
  }
  void OnInsertImage(const GURL& src) override {
    was_on_insert_image_range_called_ = true;
  }
  void OnPreeditEnd() override {}
  void OnPreeditStart() override {}
  void OnDeleteSurroundingText(size_t before, size_t after) override {
    last_on_delete_surrounding_text_args_ = std::make_pair(before, after);
  }

  void OnSetPreeditRegion(const gfx::Range& range,
                          const std::vector<ImeTextSpan>& spans) override {
    was_on_set_preedit_region_called_ = true;
  }

  void OnSetVirtualKeyboardOccludedBounds(
      const gfx::Rect& screen_bounds) override {
    virtual_keyboard_bounds_ = screen_bounds;
  }

  gfx::AcceleratedWidget GetClientWindowKey() const override {
    return client_window_key_;
  }

  bool was_on_commit_called() const { return was_on_commit_called_; }

  const std::optional<ui::CompositionText>& last_preedit() {
    return last_on_preedit_changed_args_;
  }

  std::optional<std::u16string> last_commit_text() const {
    return last_commit_text_;
  }

  const std::optional<bool>& last_on_confirm_composition_arg() const {
    return last_on_confirm_composition_arg_;
  }

  bool was_on_preedit_changed_called() const {
    return was_on_preedit_changed_called_;
  }

  bool was_on_set_preedit_region_called() const {
    return was_on_set_preedit_region_called_;
  }

  bool was_on_insert_image_called() const {
    return was_on_insert_image_range_called_;
  }

  const std::optional<std::pair<size_t, size_t>>&
  last_on_delete_surrounding_text_args() const {
    return last_on_delete_surrounding_text_args_;
  }

  const std::optional<gfx::Rect>& virtual_keyboard_bounds() const {
    return virtual_keyboard_bounds_;
  }

 private:
  gfx::AcceleratedWidget client_window_key_;
  bool was_on_commit_called_ = false;
  std::optional<std::u16string> last_commit_text_;
  std::optional<bool> last_on_confirm_composition_arg_;
  bool was_on_preedit_changed_called_ = false;
  bool was_on_set_preedit_region_called_ = false;
  bool was_on_insert_image_range_called_ = false;
  std::optional<ui::CompositionText> last_on_preedit_changed_args_;
  std::optional<std::pair<size_t, size_t>>
      last_on_delete_surrounding_text_args_;
  std::optional<gfx::Rect> virtual_keyboard_bounds_;
};

class TestKeyboardDelegate : public WaylandKeyboard::Delegate {
 public:
  TestKeyboardDelegate() = default;
  TestKeyboardDelegate(const TestKeyboardDelegate&) = delete;
  TestKeyboardDelegate& operator=(const TestKeyboardDelegate&) = delete;
  ~TestKeyboardDelegate() override = default;

  void OnKeyboardFocusChanged(WaylandWindow* window, bool focused) override {}
  void OnKeyboardModifiersChanged(int modifiers) override {}
  uint32_t OnKeyboardKeyEvent(EventType type,
                              DomCode dom_code,
                              bool repeat,
                              std::optional<uint32_t> serial,
                              base::TimeTicks timestamp,
                              int device_id,
                              WaylandKeyboard::KeyEventKind kind) override {
    last_event_timestamp_ = timestamp;
    return 0;
  }
  void OnSynthesizedKeyPressEvent(WaylandWindow* window,
                                  DomCode dom_code,
                                  base::TimeTicks timestamp) override {}

  base::TimeTicks last_event_timestamp() const { return last_event_timestamp_; }

 private:
  base::TimeTicks last_event_timestamp_;
};

class WaylandInputMethodContextTest : public WaylandTest {
 public:
  void SetUp() override {
    // TODO(crbug.com/355271570) Most of these tests expect a v1 wrapper, so
    // disable text-input-v3 here. To be cleaned up as part of future
    // refactoring.
    disabled_features_.push_back(features::kWaylandTextInputV3);

    WaylandTest::SetUp();

    surface_id_ = window_->root_surface()->get_surface_id();

    PostToServerAndWait([](wl::TestWaylandServerThread* server) {
      // WaylandInputMethodContext behaves differently when no keyboard is
      // attached.
      wl_seat_send_capabilities(server->seat()->resource(),
                                WL_SEAT_CAPABILITY_KEYBOARD);
    });
    ASSERT_TRUE(connection_->seat()->keyboard());

    SetUpInternal();
  }

 protected:
  void SetUpInternal() {
    input_method_context_delegate_ =
        std::make_unique<TestInputMethodContextDelegate>(window_->GetWidget());
    keyboard_delegate_ = std::make_unique<TestKeyboardDelegate>();
    input_method_context_ = std::make_unique<WaylandInputMethodContext>(
        connection_.get(), keyboard_delegate_.get(),
        input_method_context_delegate_.get());
    text_input_v1_ = connection_->EnsureTextInputV1();
    input_method_context_->SetTextInputV1ForTesting(text_input_v1_.get());
    input_method_context_->SetDesktopEnvironmentForTesting(
        // Ensure by default it doesn't pick the current desktop from the system
        // the tests are running on.
        base::nix::DesktopEnvironment::DESKTOP_ENVIRONMENT_OTHER);
    connection_->Flush();

    WaylandTestBase::SyncDisplay();

    // Unset Keyboard focus.
    connection_->window_manager()->SetKeyboardFocusedWindow(nullptr);

    PostToServerAndWait([](wl::TestWaylandServerThread* server) {
      ASSERT_TRUE(server->text_input_manager_v1()->text_input());
    });

    ASSERT_TRUE(connection_->text_input_manager_v1());
  }

  std::unique_ptr<TestInputMethodContextDelegate>
      input_method_context_delegate_;
  std::unique_ptr<TestKeyboardDelegate> keyboard_delegate_;
  std::unique_ptr<WaylandInputMethodContext> input_method_context_;
  raw_ptr<ZwpTextInputV1> text_input_v1_;

  uint32_t surface_id_ = 0u;
};

INSTANTIATE_TEST_SUITE_P(TextInputV1,
                         WaylandInputMethodContextTest,
                         ::testing::Values(wl::ServerConfig{}));

TEST_P(WaylandInputMethodContextTest, ActivateDeactivate) {
  // Activate is called only when both InputMethod's TextInputClient focus and
  // Wayland's keyboard focus is met.

  // Scenario 1: InputMethod focus is set, then Keyboard focus is set.
  // Unset them in the reversed order.

  InSequence s;
  PostToServerAndWait([id = surface_id_](wl::TestWaylandServerThread* server) {
    auto* zwp_text_input = server->text_input_manager_v1()->text_input();
    ASSERT_TRUE(zwp_text_input);
    EXPECT_CALL(*zwp_text_input,
                Activate(server->GetObject<wl::MockSurface>(id)->resource()))
        .Times(0);
    EXPECT_CALL(*zwp_text_input, ShowInputPanel()).Times(0);
  });

  LinuxInputMethodContext::TextInputClientAttributes attributes;
  attributes.input_type = TEXT_INPUT_TYPE_TEXT;
  input_method_context_->UpdateFocus(true, ui::TEXT_INPUT_TYPE_NONE, attributes,
                                     ui::TextInputClient::FOCUS_REASON_OTHER);
  connection_->Flush();

  PostToServerAndWait([id = surface_id_](wl::TestWaylandServerThread* server) {
    auto* zwp_text_input = server->text_input_manager_v1()->text_input();
    Mock::VerifyAndClearExpectations(zwp_text_input);

    EXPECT_CALL(*zwp_text_input,
                Activate(server->GetObject<wl::MockSurface>(id)->resource()));
    EXPECT_CALL(*zwp_text_input, ShowInputPanel());
  });

  connection_->window_manager()->SetKeyboardFocusedWindow(window_.get());
  connection_->Flush();

  PostToServerAndWait([](wl::TestWaylandServerThread* server) {
    auto* zwp_text_input = server->text_input_manager_v1()->text_input();
    Mock::VerifyAndClearExpectations(zwp_text_input);

    EXPECT_CALL(*zwp_text_input, HideInputPanel());
    EXPECT_CALL(*zwp_text_input, Deactivate());
  });

  connection_->window_manager()->SetKeyboardFocusedWindow(nullptr);
  connection_->Flush();

  PostToServerAndWait([](wl::TestWaylandServerThread* server) {
    auto* zwp_text_input = server->text_input_manager_v1()->text_input();
    Mock::VerifyAndClearExpectations(zwp_text_input);

    EXPECT_CALL(*zwp_text_input, HideInputPanel()).Times(0);
    EXPECT_CALL(*zwp_text_input, Deactivate()).Times(0);
  });

  attributes.input_type = TEXT_INPUT_TYPE_NONE;
  input_method_context_->UpdateFocus(true, ui::TEXT_INPUT_TYPE_TEXT, attributes,
                                     ui::TextInputClient::FOCUS_REASON_NONE);
  connection_->Flush();

  PostToServerAndWait([id = surface_id_](wl::TestWaylandServerThread* server) {
    auto* zwp_text_input = server->text_input_manager_v1()->text_input();
    Mock::VerifyAndClearExpectations(zwp_text_input);

    // Scenario 2: Keyboard focus is set, then InputMethod focus is set.
    // Unset them in the reversed order.
    EXPECT_CALL(*zwp_text_input,
                Activate(server->GetObject<wl::MockSurface>(id)->resource()))
        .Times(0);
    EXPECT_CALL(*zwp_text_input, ShowInputPanel()).Times(0);
  });

  connection_->window_manager()->SetKeyboardFocusedWindow(window_.get());
  connection_->Flush();

  PostToServerAndWait([id = surface_id_](wl::TestWaylandServerThread* server) {
    auto* zwp_text_input = server->text_input_manager_v1()->text_input();
    Mock::VerifyAndClearExpectations(zwp_text_input);

    EXPECT_CALL(*zwp_text_input,
                Activate(server->GetObject<wl::MockSurface>(id)->resource()));
    EXPECT_CALL(*zwp_text_input, ShowInputPanel());
  });

  attributes.input_type = TEXT_INPUT_TYPE_TEXT;
  input_method_context_->UpdateFocus(true, ui::TEXT_INPUT_TYPE_NONE, attributes,
                                     ui::TextInputClient::FOCUS_REASON_OTHER);
  connection_->Flush();

  PostToServerAndWait([](wl::TestWaylandServerThread* server) {
    auto* zwp_text_input = server->text_input_manager_v1()->text_input();
    Mock::VerifyAndClearExpectations(zwp_text_input);

    EXPECT_CALL(*zwp_text_input, HideInputPanel());
    EXPECT_CALL(*zwp_text_input, Deactivate());
  });

  attributes.input_type = TEXT_INPUT_TYPE_NONE;
  input_method_context_->UpdateFocus(true, ui::TEXT_INPUT_TYPE_TEXT, attributes,
                                     ui::TextInputClient::FOCUS_REASON_NONE);
  connection_->Flush();

  PostToServerAndWait([](wl::TestWaylandServerThread* server) {
    auto* zwp_text_input = server->text_input_manager_v1()->text_input();
    Mock::VerifyAndClearExpectations(zwp_text_input);

    EXPECT_CALL(*zwp_text_input, HideInputPanel()).Times(0);
    EXPECT_CALL(*zwp_text_input, Deactivate()).Times(0);
  });

  connection_->window_manager()->SetKeyboardFocusedWindow(nullptr);
  connection_->Flush();

  PostToServerAndWait([](wl::TestWaylandServerThread* server) {
    auto* zwp_text_input = server->text_input_manager_v1()->text_input();
    Mock::VerifyAndClearExpectations(zwp_text_input);
  });
}

TEST_P(WaylandInputMethodContextTest, Reset) {
  PostToServerAndWait([](wl::TestWaylandServerThread* server) {
    EXPECT_CALL(*server->text_input_manager_v1()->text_input(), Reset());
  });
  input_method_context_->Reset();
  connection_->Flush();
}

TEST_P(WaylandInputMethodContextTest, SetCursorLocation) {
  constexpr gfx::Rect cursor_location(50, 20, 1, 1);
  constexpr gfx::Rect window_bounds(20, 10, 100, 100);
  PostToServerAndWait(
      [cursor_location, window_bounds](wl::TestWaylandServerThread* server) {
        EXPECT_CALL(
            *server->text_input_manager_v1()->text_input(),
            SetCursorRect(cursor_location.x() - window_bounds.x(),
                          cursor_location.y() - window_bounds.y(),
                          cursor_location.width(), cursor_location.height()));
      });
  window_->SetBoundsInDIP(window_bounds);
  connection_->window_manager()->SetKeyboardFocusedWindow(window_.get());
  input_method_context_->SetCursorLocation(cursor_location);
  connection_->Flush();
}

TEST_P(WaylandInputMethodContextTest, SetSurroundingTextForShortText) {
  const std::u16string text(50, u'あ');
  constexpr gfx::Range range(20, 30);

  const std::string kExpectedSentText(base::UTF16ToUTF8(text));
  constexpr gfx::Range kExpectedSentRange(60, 90);

  PostToServerAndWait([kExpectedSentText, kExpectedSentRange](
                          wl::TestWaylandServerThread* server) {
    // The text and range sent as wayland protocol must be same to the original
    // text and range where the original text is shorter than 4000 byte.
    EXPECT_CALL(*server->text_input_manager_v1()->text_input(),
                SetSurroundingText(kExpectedSentText, kExpectedSentRange))
        .Times(1);
  });

  input_method_context_->SetSurroundingText(text, gfx::Range(0, 50),
                                            gfx::Range::InvalidRange(), range);
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      text);
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            range);
  connection_->Flush();

  PostToServerAndWait(
      [kExpectedSentRange](wl::TestWaylandServerThread* server) {
        auto* text_input = server->text_input_manager_v1()->text_input();
        Mock::VerifyAndClearExpectations(text_input);

        // Test OnDeleteSurroundingText with this input.
        zwp_text_input_v1_send_delete_surrounding_text(
            text_input->resource(), kExpectedSentRange.start(),
            kExpectedSentRange.length());
      });

  EXPECT_EQ(
      input_method_context_delegate_->last_on_delete_surrounding_text_args(),
      (std::pair<size_t, size_t>(0, 0)));
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      std::u16string(40, u'あ'));
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            gfx::Range(20));
}

TEST_P(WaylandInputMethodContextTest, SetSurroundingTextForLongText) {
  const std::u16string text(5000, u'あ');
  constexpr gfx::Range range(2800, 3200);

  std::string expected_sent_text;
  gfx::Range expected_sent_range;
  // The text sent as wayland protocol must be at most
  // 4000 byte and long enough in the limitation.
  expected_sent_text = base::UTF16ToUTF8(std::u16string(1332, u'あ'));
  // The selection range must be relocated accordingly to the sent text.
  expected_sent_range = gfx::Range(1398, 2598);

  PostToServerAndWait([expected_sent_text, expected_sent_range](
                          wl::TestWaylandServerThread* server) {
    EXPECT_CALL(*server->text_input_manager_v1()->text_input(),
                SetSurroundingText(expected_sent_text, expected_sent_range))
        .Times(1);
  });

  input_method_context_->SetSurroundingText(text, gfx::Range(0, 5000),
                                            gfx::Range::InvalidRange(), range);
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      text);
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            range);
  connection_->Flush();

  PostToServerAndWait(
      [expected_sent_range](wl::TestWaylandServerThread* server) {
        auto* text_input = server->text_input_manager_v1()->text_input();
        Mock::VerifyAndClearExpectations(text_input);

        // Test OnDeleteSurroundingText with this input.
        zwp_text_input_v1_send_delete_surrounding_text(
            text_input->resource(), expected_sent_range.start(),
            expected_sent_range.length());
      });

  EXPECT_EQ(
      input_method_context_delegate_->last_on_delete_surrounding_text_args(),
      (std::pair<size_t, size_t>(0, 0)));
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      std::u16string(4600, u'あ'));
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            gfx::Range(2800));
}

TEST_P(WaylandInputMethodContextTest, SetSurroundingTextForLongTextInLeftEdge) {
  const std::u16string text(5000, u'あ');
  constexpr gfx::Range range(0, 500);

  std::string expected_sent_text;
  gfx::Range expected_sent_range;
  // The text sent as wayland protocol must be at most 4000
  // byte and large enough in the limitation.
  expected_sent_text = base::UTF16ToUTF8(std::u16string(1333, u'あ'));
  // The selection range must be relocated accordingly to the sent text.
  expected_sent_range = gfx::Range(0, 1500);

  PostToServerAndWait([expected_sent_text, expected_sent_range](
                          wl::TestWaylandServerThread* server) {
    EXPECT_CALL(*server->text_input_manager_v1()->text_input(),
                SetSurroundingText(expected_sent_text, expected_sent_range))
        .Times(1);
  });

  input_method_context_->SetSurroundingText(text, gfx::Range(0, 5000),
                                            gfx::Range::InvalidRange(), range);
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      text);
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            range);
  connection_->Flush();

  PostToServerAndWait(
      [expected_sent_range](wl::TestWaylandServerThread* server) {
        auto* text_input = server->text_input_manager_v1()->text_input();
        Mock::VerifyAndClearExpectations(text_input);

        // Test OnDeleteSurroundingText with this input.
        zwp_text_input_v1_send_delete_surrounding_text(
            text_input->resource(), expected_sent_range.start(),
            expected_sent_range.length());
      });

  EXPECT_EQ(
      input_method_context_delegate_->last_on_delete_surrounding_text_args(),
      (std::pair<size_t, size_t>(0, 0)));
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      std::u16string(4500, u'あ'));
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            gfx::Range(0));
}

TEST_P(WaylandInputMethodContextTest,
       SetSurroundingTextForLongTextInRightEdge) {
  const std::u16string text(5000, u'あ');
  constexpr gfx::Range range(4500, 5000);

  std::string expected_sent_text;
  gfx::Range expected_sent_range;
  // The text sent as wayland protocol must be at most
  // 4000 byte and large enough in the limitation.
  expected_sent_text = base::UTF16ToUTF8(std::u16string(1333, u'あ'));
  // The selection range must be relocated accordingly to the sent text.
  expected_sent_range = gfx::Range(2499, 3999);

  PostToServerAndWait([expected_sent_text, expected_sent_range](
                          wl::TestWaylandServerThread* server) {
    EXPECT_CALL(*server->text_input_manager_v1()->text_input(),
                SetSurroundingText(expected_sent_text, expected_sent_range))
        .Times(1);
  });

  input_method_context_->SetSurroundingText(text, gfx::Range(0, 5000),
                                            gfx::Range::InvalidRange(), range);
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      text);
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            range);
  connection_->Flush();

  PostToServerAndWait(
      [expected_sent_range](wl::TestWaylandServerThread* server) {
        auto* text_input = server->text_input_manager_v1()->text_input();
        Mock::VerifyAndClearExpectations(text_input);

        // Test OnDeleteSurroundingText with this input.
        zwp_text_input_v1_send_delete_surrounding_text(
            text_input->resource(), expected_sent_range.start(),
            expected_sent_range.length());
      });

  EXPECT_EQ(
      input_method_context_delegate_->last_on_delete_surrounding_text_args(),
      (std::pair<size_t, size_t>(0, 0)));
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      std::u16string(4500, u'あ'));
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            gfx::Range(4500));
}

TEST_P(WaylandInputMethodContextTest, DeleteSurroundingTextWithExtendedRange) {
  const std::u16string text(50, u'あ');
  const gfx::Range range(20, 30);

  // The text and range sent as wayland protocol must be same to the original
  // text and range where the original text is shorter than 4000 byte.
  const std::string kExpectedSentText(base::UTF16ToUTF8(text));
  // The selection range must be relocated accordingly to the sent text.
  constexpr gfx::Range kExpectedSentRange(60, 90);

  PostToServerAndWait([kExpectedSentText, kExpectedSentRange](
                          wl::TestWaylandServerThread* server) {
    EXPECT_CALL(*server->text_input_manager_v1()->text_input(),
                SetSurroundingText(kExpectedSentText, kExpectedSentRange))
        .Times(1);
  });

  input_method_context_->SetSurroundingText(text, gfx::Range(0, 5000),
                                            gfx::Range::InvalidRange(), range);
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      text);
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            range);
  connection_->Flush();

  PostToServerAndWait([](wl::TestWaylandServerThread* server) {
    auto* text_input = server->text_input_manager_v1()->text_input();
    Mock::VerifyAndClearExpectations(text_input);

    // Test OnDeleteSurroundingText with this input.
    // One char more deletion for each before and after the selection.
    zwp_text_input_v1_send_delete_surrounding_text(text_input->resource(), 57,
                                                   36);
  });

  EXPECT_EQ(
      input_method_context_delegate_->last_on_delete_surrounding_text_args(),
      (std::pair<size_t, size_t>(1, 1)));
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      std::u16string(38, u'あ'));
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            gfx::Range(19));
}

TEST_P(WaylandInputMethodContextTest, DeleteSurroundingTextInIncorrectOrder) {
  // This test aims to check the scenario where OnDeleteSurroundingText event is
  // not received in correct order due to the timing issue.

  constexpr char16_t text[] = u"aあb";
  const gfx::Range range(3);

  input_method_context_->SetSurroundingText(text, gfx::Range(0, 3),
                                            gfx::Range::InvalidRange(), range);
  connection_->Flush();

  // 1. Delete the second character 'b'.
  PostToServerAndWait([](wl::TestWaylandServerThread* server) {
    auto* text_input = server->text_input_manager_v1()->text_input();
    Mock::VerifyAndClearExpectations(text_input);

    zwp_text_input_v1_send_delete_surrounding_text(text_input->resource(), 4,
                                                   1);
  });
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      u"aあ");
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            gfx::Range(2));

  // 2. Delete the third character 'あ'.
  PostToServerAndWait([](wl::TestWaylandServerThread* server) {
    auto* text_input = server->text_input_manager_v1()->text_input();
    Mock::VerifyAndClearExpectations(text_input);

    zwp_text_input_v1_send_delete_surrounding_text(text_input->resource(), 1,
                                                   3);
  });

  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      u"a");
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            gfx::Range(1));

  // 3. Set surrounding text for step 1. Ideally this thould be called before
  // step 2, but the order could be different due to the timing issue.
  input_method_context_->SetSurroundingText(
      u"aあ", gfx::Range(0, 2), gfx::Range::InvalidRange(), gfx::Range(2));
  connection_->Flush();

  // Surrounding text tracker should predict "a" instead of "aあ" here as that
  // is the correct state on server. On setting "aあ" as a surrounding text,
  // surrounding text tracker looks up the expected state queue and consumes the
  // state of "aあ" .
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      u"a");
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            gfx::Range(1));

  // 4. Set surrounding text for step 2.
  input_method_context_->SetSurroundingText(
      u"a", gfx::Range(0, 1), gfx::Range::InvalidRange(), gfx::Range(1));
  connection_->Flush();

  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      u"a");
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            gfx::Range(1));
}

TEST_P(WaylandInputMethodContextTest,
       DeleteSurroundingTextAndCommitInIncorrectOrder) {
  // This test aims to check the scenario where SetSurroundingText event is
  // received from application later than receiving delete/commit event from
  // server.

  // 1. Set CommitString as a initial state. Cursor is between "Commit" and
  // "String".
  input_method_context_->SetSurroundingText(u"CommitString", gfx::Range(0, 12),
                                            gfx::Range::InvalidRange(),
                                            gfx::Range(6));
  connection_->Flush();

  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      u"CommitString");
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            gfx::Range(6));

  // 2. Delete surrounding text for "Commit" received.
  PostToServerAndWait([](wl::TestWaylandServerThread* server) {
    auto* text_input = server->text_input_manager_v1()->text_input();
    Mock::VerifyAndClearExpectations(text_input);

    zwp_text_input_v1_send_delete_surrounding_text(text_input->resource(), 0,
                                                   6);
  });

  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      u"String");
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            gfx::Range(0));

  // 3. Commit for "Updated" received.
  PostToServerAndWait([](wl::TestWaylandServerThread* server) {
    auto* text_input = server->text_input_manager_v1()->text_input();
    Mock::VerifyAndClearExpectations(text_input);

    zwp_text_input_v1_send_commit_string(
        server->text_input_manager_v1()->text_input()->resource(),
        server->GetNextSerial(), "Updated");
  });

  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      u"UpdatedString");
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            gfx::Range(7));

  // 4. Set surrounding text for step 2. Ideally this should be sent before step
  // 3.
  input_method_context_->SetSurroundingText(
      u"String", gfx::Range(0, 6), gfx::Range::InvalidRange(), gfx::Range(0));
  connection_->Flush();

  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      u"UpdatedString");
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            gfx::Range(7));

  // 5. Set surrounding text for step 3.
  input_method_context_->SetSurroundingText(u"UpdatedString", gfx::Range(0, 13),
                                            gfx::Range::InvalidRange(),
                                            gfx::Range(7));
  connection_->Flush();

  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      u"UpdatedString");
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            gfx::Range(7));
}

TEST_P(WaylandInputMethodContextTest, OnPreeditChangedDefaultCompositionStyle) {
  constexpr std::string_view kPreeditString("PreeditString");
  constexpr gfx::Range kSelection{7, 13};
  input_method_context_->OnPreeditString(
      kPreeditString,
      // No composition style provided.
      {{1,
        3,
        {{ImeTextSpan::Type::kMisspellingSuggestion,
          ImeTextSpan::Thickness::kNone}}}},
      kSelection);
  EXPECT_TRUE(input_method_context_delegate_->was_on_preedit_changed_called());
  EXPECT_EQ(input_method_context_delegate_->last_preedit()->ime_text_spans,
            (ImeTextSpans{ImeTextSpan(ImeTextSpan::Type::kMisspellingSuggestion,
                                      1, 4, ImeTextSpan::Thickness::kNone),
                          // Default composition should be applied.
                          ImeTextSpan(ImeTextSpan::Type::kComposition, 0,
                                      kPreeditString.size(),
                                      ImeTextSpan::Thickness::kThin)}));
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      u"PreeditString");
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().composition,
            gfx::Range(0, kPreeditString.size()));
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            kSelection);
}

TEST_P(WaylandInputMethodContextTest, OnPreeditChanged) {
  constexpr std::string_view kPreeditString("PreeditString");
  constexpr gfx::Range kSelection{7, 13};
  input_method_context_->OnPreeditString(
      kPreeditString,
      {{0,
        static_cast<uint32_t>(kPreeditString.size()),
        {{ImeTextSpan::Type::kComposition, ImeTextSpan::Thickness::kThick}}},
       {1,
        3,
        {{ImeTextSpan::Type::kMisspellingSuggestion,
          ImeTextSpan::Thickness::kNone}}}},
      kSelection);
  EXPECT_TRUE(input_method_context_delegate_->was_on_preedit_changed_called());
  EXPECT_EQ(input_method_context_delegate_->last_preedit()->ime_text_spans,
            (ImeTextSpans{ImeTextSpan(ImeTextSpan::Type::kComposition, 0,
                                      kPreeditString.size(),
                                      ImeTextSpan::Thickness::kThick),
                          ImeTextSpan(ImeTextSpan::Type::kMisspellingSuggestion,
                                      1, 4, ImeTextSpan::Thickness::kNone)}));
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      u"PreeditString");
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().composition,
            gfx::Range(0, kPreeditString.size()));
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            kSelection);
}

TEST_P(WaylandInputMethodContextTest, OnCommit) {
  PostToServerAndWait([](wl::TestWaylandServerThread* server) {
    zwp_text_input_v1_send_commit_string(
        server->text_input_manager_v1()->text_input()->resource(),
        server->GetNextSerial(), "CommitString");
  });
  EXPECT_TRUE(input_method_context_delegate_->was_on_commit_called());
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      u"CommitString");
  // On commit string, selection is placed next to the last character unless the
  // cursor position is specified by OnCursorPosition.
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            gfx::Range(12));
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().composition,
            gfx::Range(0));
}

// Regression test for crbug.com/40263583
TEST_P(WaylandInputMethodContextTest,
       OnCommitAfterEmptyPreeditStringWithoutCursor) {
  input_method_context_->OnPreeditString("", {}, gfx::Range::InvalidRange());
  EXPECT_TRUE(input_method_context_delegate_->was_on_preedit_changed_called());
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      u"");
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().composition,
            gfx::Range(0, 0));
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            gfx::Range(0));
  input_method_context_->OnCommitString("CommitString");
  EXPECT_TRUE(input_method_context_delegate_->was_on_commit_called());
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      u"CommitString");
  // On commit string, selection is placed next to the last character unless the
  // cursor position is specified by OnCursorPosition.
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            gfx::Range(12));
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().composition,
            gfx::Range(0));
}

TEST_P(WaylandInputMethodContextTest, OnCommitAfterPreeditStringWithoutCursor) {
  input_method_context_->OnPreeditString("PreeditString", {},
                                         gfx::Range::InvalidRange());
  EXPECT_TRUE(input_method_context_delegate_->was_on_preedit_changed_called());
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      u"PreeditString");
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().composition,
            gfx::Range(0, 13));
  // Cursor should be at the end of preedit when cursor position is not
  // specified.
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            gfx::Range(13));
  input_method_context_->OnCommitString("CommitString");
  EXPECT_TRUE(input_method_context_delegate_->was_on_commit_called());
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      u"CommitString");
  // On commit string, selection is placed next to the last character unless the
  // cursor position is specified by OnCursorPosition.
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            gfx::Range(12));
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().composition,
            gfx::Range(0));
}

TEST_P(WaylandInputMethodContextTest, DisplayVirtualKeyboard) {
  PostToServerAndWait([](wl::TestWaylandServerThread* server) {
    EXPECT_CALL(*server->text_input_manager_v1()->text_input(),
                ShowInputPanel())
        .Times(1);
  });
  EXPECT_TRUE(input_method_context_->DisplayVirtualKeyboard());
  connection_->Flush();
  WaylandTestBase::SyncDisplay();
}

TEST_P(WaylandInputMethodContextTest, DismissVirtualKeyboard) {
  PostToServerAndWait([](wl::TestWaylandServerThread* server) {
    EXPECT_CALL(*server->text_input_manager_v1()->text_input(),
                HideInputPanel());
  });
  input_method_context_->DismissVirtualKeyboard();
  connection_->Flush();
  WaylandTestBase::SyncDisplay();
}

TEST_P(WaylandInputMethodContextTest, UpdateVirtualKeyboardState) {
  EXPECT_FALSE(input_method_context_->IsKeyboardVisible());
  PostToServerAndWait([](wl::TestWaylandServerThread* server) {
    zwp_text_input_v1_send_input_panel_state(
        server->text_input_manager_v1()->text_input()->resource(), 1);
  });

  EXPECT_TRUE(input_method_context_->IsKeyboardVisible());

  PostToServerAndWait([](wl::TestWaylandServerThread* server) {
    zwp_text_input_v1_send_input_panel_state(
        server->text_input_manager_v1()->text_input()->resource(), 0);
  });

  EXPECT_FALSE(input_method_context_->IsKeyboardVisible());
}

TEST_P(WaylandInputMethodContextTest, OnKeySym) {
#if BUILDFLAG(USE_XKBCOMMON)
  MaybeSetUpXkb();

  uint32_t test_timestamp = 100;
  input_method_context_->OnKeysym(
      XKB_KEY_Shift_L, wl_keyboard_key_state::WL_KEYBOARD_KEY_STATE_PRESSED, 0,
      test_timestamp);

  ASSERT_EQ(wl::EventMillisecondsToTimeTicks(test_timestamp),
            keyboard_delegate_->last_event_timestamp());
#endif
}

namespace {

std::unique_ptr<KeyEvent> CreateKeyEventForCharacterComposer(
    KeyboardCode keyboard_code,
    DomCode dom_code,
    DomKey dom_key) {
  auto event =
      std::make_unique<KeyEvent>(EventType::kKeyPressed, keyboard_code,
                                 dom_code, EF_NONE, dom_key, EventTimeForNow());
  // We need to set this flag to make sure the event is sent to
  // CharacterComposer.
  ui::SetKeyboardImeFlags(event.get(), ui::kPropertyKeyboardImeIgnoredFlag);
  return event;
}

}  // namespace

TEST_P(WaylandInputMethodContextTest, CharacterComposerPreeditStringDeadKey) {
  const char16_t kCombiningAcute = 0x0301;

  auto event = CreateKeyEventForCharacterComposer(
      VKEY_UNKNOWN, DomCode::NONE,
      DomKey::DeadKeyFromCombiningCharacter(kCombiningAcute));
  EXPECT_TRUE(input_method_context_->DispatchKeyEvent(*event));
  EXPECT_TRUE(input_method_context_delegate_->was_on_preedit_changed_called());

  // Preedit string in sequence mode (i.e. using dead keys or the compose key)
  // should only be enabled on Linux ozone/wayland. Everywhere else, the preedit
  // string should always be empty.
#if BUILDFLAG(IS_LINUX)
  // The preedit string should be the non-combining variant of the dead key.
  const char16_t kAcute = 0x00B4;
  std::u16string preedit_string(1, kAcute);
#else
  std::u16string preedit_string = u"";
#endif  // BUILDFLAG(IS_LINUX)
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      preedit_string);

  event = CreateKeyEventForCharacterComposer(VKEY_A, DomCode::US_A,
                                             DomKey::FromCharacter('a'));
  EXPECT_TRUE(input_method_context_->DispatchKeyEvent(*event));
  EXPECT_TRUE(input_method_context_delegate_->was_on_preedit_changed_called());
  EXPECT_TRUE(input_method_context_delegate_->was_on_commit_called());
  // The composed text should be the same on all platforms.
  EXPECT_EQ(input_method_context_delegate_->last_commit_text(), u"á");
}

TEST_P(WaylandInputMethodContextTest,
       CharacterComposerPreeditStringComposeKey) {
  auto event = CreateKeyEventForCharacterComposer(
      VKEY_COMPOSE, DomCode::ALT_RIGHT, DomKey::COMPOSE);
  EXPECT_TRUE(input_method_context_->DispatchKeyEvent(*event));
  EXPECT_TRUE(input_method_context_delegate_->was_on_preedit_changed_called());

#if BUILDFLAG(IS_LINUX)
  std::u16string preedit_string(
      1, ui::CharacterComposer::kPreeditStringComposeKeySymbol);
#else
  std::u16string preedit_string = u"";
#endif  // BUILDFLAG(IS_LINUX)
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      preedit_string);

  event = CreateKeyEventForCharacterComposer(VKEY_OEM_7, DomCode::QUOTE,
                                             DomKey::FromCharacter('\''));
  EXPECT_TRUE(input_method_context_->DispatchKeyEvent(*event));
  EXPECT_TRUE(input_method_context_delegate_->was_on_preedit_changed_called());

#if BUILDFLAG(IS_LINUX)
  preedit_string = u"'";
#endif  // BUILDFLAG(IS_LINUX)
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      preedit_string);

  event = CreateKeyEventForCharacterComposer(VKEY_A, DomCode::US_A,
                                             DomKey::FromCharacter('a'));
  EXPECT_TRUE(input_method_context_->DispatchKeyEvent(*event));
  EXPECT_TRUE(input_method_context_delegate_->was_on_preedit_changed_called());
  EXPECT_TRUE(input_method_context_delegate_->was_on_commit_called());
  // The composed text should be the same on all platforms.
  EXPECT_EQ(input_method_context_delegate_->last_commit_text(), u"á");
}

class WaylandInputMethodContextNoKeyboardTest
    : public WaylandInputMethodContextTest {
 public:
  void SetUp() override {
    // TODO(crbug.com/355271570) Most of these tests expect a v1 wrapper, so
    // disable text-input-v3 here. To be cleaned up as part of future
    // refactoring.
    disabled_features_.push_back(features::kWaylandTextInputV3);

    // Call the skip base implementation to avoid setting up the keyboard.
    WaylandTest::SetUp();

    ASSERT_FALSE(connection_->seat()->keyboard());

    SetUpInternal();
  }
};

INSTANTIATE_TEST_SUITE_P(TextInputV1,
                         WaylandInputMethodContextNoKeyboardTest,
                         ::testing::Values(wl::ServerConfig{}));

TEST_P(WaylandInputMethodContextNoKeyboardTest, ActivateDeactivate) {
  const uint32_t surface_id = window_->root_surface()->get_surface_id();

  // Because there is no keyboard, Activate is called as soon as InputMethod's
  // TextInputClient focus is met.

  InSequence s;
  PostToServerAndWait([id = surface_id](wl::TestWaylandServerThread* server) {
    auto* zwp_text_input = server->text_input_manager_v1()->text_input();
    EXPECT_CALL(*zwp_text_input,
                Activate(server->GetObject<wl::MockSurface>(id)->resource()));
    EXPECT_CALL(*zwp_text_input, ShowInputPanel());
  });

  LinuxInputMethodContext::TextInputClientAttributes attributes;
  attributes.input_type = TEXT_INPUT_TYPE_TEXT;
  input_method_context_->UpdateFocus(true, ui::TEXT_INPUT_TYPE_NONE, attributes,
                                     ui::TextInputClient::FOCUS_REASON_OTHER);
  connection_->Flush();
  PostToServerAndWait([](wl::TestWaylandServerThread* server) {
    auto* zwp_text_input = server->text_input_manager_v1()->text_input();
    Mock::VerifyAndClearExpectations(zwp_text_input);

    EXPECT_CALL(*zwp_text_input, HideInputPanel());
    EXPECT_CALL(*zwp_text_input, Deactivate());
  });

  attributes.input_type = TEXT_INPUT_TYPE_NONE;
  input_method_context_->UpdateFocus(false, ui::TEXT_INPUT_TYPE_TEXT,
                                     attributes,
                                     ui::TextInputClient::FOCUS_REASON_NONE);
  connection_->Flush();

  PostToServerAndWait([](wl::TestWaylandServerThread* server) {
    Mock::VerifyAndClearExpectations(
        server->text_input_manager_v1()->text_input());
  });
}

TEST_P(WaylandInputMethodContextNoKeyboardTest, UpdateFocusBetweenTextFields) {
  const uint32_t surface_id = window_->root_surface()->get_surface_id();

  // Because there is no keyboard, Activate is called as soon as InputMethod's
  // TextInputClient focus is met.

  InSequence s;
  PostToServerAndWait([id = surface_id](wl::TestWaylandServerThread* server) {
    auto* zwp_text_input = server->text_input_manager_v1()->text_input();
    EXPECT_CALL(*zwp_text_input,
                Activate(server->GetObject<wl::MockSurface>(id)->resource()));
    EXPECT_CALL(*zwp_text_input, ShowInputPanel());
  });

  LinuxInputMethodContext::TextInputClientAttributes attributes;
  attributes.input_type = TEXT_INPUT_TYPE_TEXT;
  input_method_context_->UpdateFocus(true, ui::TEXT_INPUT_TYPE_NONE, attributes,
                                     ui::TextInputClient::FOCUS_REASON_OTHER);
  connection_->Flush();

  PostToServerAndWait([id = surface_id](wl::TestWaylandServerThread* server) {
    auto* zwp_text_input = server->text_input_manager_v1()->text_input();
    Mock::VerifyAndClearExpectations(zwp_text_input);

    // Make sure virtual keyboard is not unnecessarily hidden.
    EXPECT_CALL(*zwp_text_input, HideInputPanel()).Times(0);
    EXPECT_CALL(*zwp_text_input, Deactivate());
    EXPECT_CALL(*zwp_text_input,
                Activate(server->GetObject<wl::MockSurface>(id)->resource()));
    EXPECT_CALL(*zwp_text_input, ShowInputPanel()).Times(0);
  });

  attributes.input_type = TEXT_INPUT_TYPE_TEXT;
  input_method_context_->UpdateFocus(false, ui::TEXT_INPUT_TYPE_TEXT,
                                     attributes,
                                     ui::TextInputClient::FOCUS_REASON_OTHER);
  connection_->Flush();

  PostToServerAndWait([](wl::TestWaylandServerThread* server) {
    Mock::VerifyAndClearExpectations(
        server->text_input_manager_v1()->text_input());
  });
}

// For use in tests that test the WaylandInputMethodContext in isolation with a
// mock V3 client.
class WaylandInputMethodContextWithMockV3Test : public WaylandTestSimple {
 public:
  void SetUp() override {
    WaylandTestSimple::SetUp();
    input_method_context_delegate_ =
        std::make_unique<TestInputMethodContextDelegate>(window_->GetWidget());
    keyboard_delegate_ = std::make_unique<TestKeyboardDelegate>();
    mock_wrapper_ = std::make_unique<MockZwpTextInputV3>();
    input_method_context_ = std::make_unique<WaylandInputMethodContext>(
        connection_.get(), keyboard_delegate_.get(),
        input_method_context_delegate_.get());
    input_method_context_->SetTextInputV3ForTesting(mock_wrapper_.get());
    input_method_context_->SetDesktopEnvironmentForTesting(
        // Ensure by default it doesn't pick the current desktop from the system
        // the tests are running on.
        base::nix::DesktopEnvironment::DESKTOP_ENVIRONMENT_OTHER);
  }

 protected:
  std::unique_ptr<TestInputMethodContextDelegate>
      input_method_context_delegate_;
  std::unique_ptr<TestKeyboardDelegate> keyboard_delegate_;
  std::unique_ptr<MockZwpTextInputV3> mock_wrapper_;
  std::unique_ptr<WaylandInputMethodContext> input_method_context_;
};

TEST_F(WaylandInputMethodContextWithMockV3Test,
       SetSurroundingShortTextWithCompositionRange) {
  const std::u16string text(50, u'あ');
  constexpr gfx::Range range(20, 30);

  const std::string kExpectedSentText(base::UTF16ToUTF8(text));
  constexpr gfx::Range kExpectedSentRange(60, 90);

  EXPECT_CALL(*mock_wrapper_,
              SetSurroundingText(kExpectedSentText, kExpectedSentRange,
                                 kExpectedSentRange));
  input_method_context_->SetSurroundingText(text, gfx::Range(0, 50), range,
                                            range);
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      text);
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            range);
  connection_->Flush();
}

TEST_F(WaylandInputMethodContextWithMockV3Test,
       SetSurroundingLongTextWithCompositionRange) {
  const std::u16string text(5000, u'あ');
  constexpr gfx::Range kRange(2800, 3200);

  const std::string kExpectedSentText(
      base::UTF16ToUTF8(std::u16string(1332, u'あ')));
  constexpr gfx::Range kExpectedSentRange(1398, 2598);

  EXPECT_CALL(*mock_wrapper_,
              SetSurroundingText(kExpectedSentText, kExpectedSentRange,
                                 kExpectedSentRange));
  input_method_context_->SetSurroundingText(text, gfx::Range(0, 5000), kRange,
                                            kRange);
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      text);
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            kRange);
}

TEST_F(WaylandInputMethodContextWithMockV3Test,
       SetSurroundingLongTextWithCompositionRangeOutsideSurroundingTextRange) {
  const std::u16string text(5000, u'あ');
  constexpr gfx::Range kSelectionRange(2800, 3200);
  // composition range before surrounding text range.
  constexpr gfx::Range kCompositionRange(10, 20);

  const std::string kExpectedSentText(
      base::UTF16ToUTF8(std::u16string(1332, u'あ')));
  constexpr gfx::Range kExpectedSentSelectionRange(1398, 2598);

  EXPECT_CALL(*mock_wrapper_,
              SetSurroundingText(kExpectedSentText, gfx::Range::InvalidRange(),
                                 kExpectedSentSelectionRange));
  input_method_context_->SetSurroundingText(text, gfx::Range(0, 5000),
                                            kCompositionRange, kSelectionRange);
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      text);
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            kSelectionRange);

  // composition range after surrounding text range.
  constexpr gfx::Range kCompositionRange2(4500, 4600);

  EXPECT_CALL(*mock_wrapper_,
              SetSurroundingText(kExpectedSentText, gfx::Range::InvalidRange(),
                                 kExpectedSentSelectionRange));
  input_method_context_->SetSurroundingText(
      text, gfx::Range(0, 5000), kCompositionRange2, kSelectionRange);
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      text);
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            kSelectionRange);
}

TEST_F(WaylandInputMethodContextWithMockV3Test,
       SetSurroundingWithCompositionRangeOutideText) {
  const std::u16string text(5000, u'あ');
  constexpr gfx::Range kSelectionRange(2800, 3200);

  EXPECT_CALL(*mock_wrapper_, SetSurroundingText(_, _, _)).Times(0);
  input_method_context_->SetSurroundingText(
      text, gfx::Range(0, 5000), gfx::Range(6000, 7000), kSelectionRange);
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      text);
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            kSelectionRange);
}

TEST_F(WaylandInputMethodContextWithMockV3Test,
       SetSurroundingWithCompositionRangeInvalid) {
  const std::u16string text(5000, u'あ');
  constexpr gfx::Range kSelectionRange(2800, 3200);

  const std::string kExpectedSentText(
      base::UTF16ToUTF8(std::u16string(1332, u'あ')));
  constexpr gfx::Range kExpectedSentSelectionRange(1398, 2598);

  EXPECT_CALL(*mock_wrapper_,
              SetSurroundingText(kExpectedSentText, gfx::Range::InvalidRange(),
                                 kExpectedSentSelectionRange));
  // invalid composition range
  input_method_context_->SetSurroundingText(
      text, gfx::Range(0, 5000), gfx::Range::InvalidRange(), kSelectionRange);
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      text);
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            kSelectionRange);
}

TEST_F(WaylandInputMethodContextWithMockV3Test, OnPreeditChanged) {
  const std::u16string text(50, u'あ');
  const std::string text_utf8 = base::UTF16ToUTF8(text);

  input_method_context_->OnPreeditString(text_utf8, {}, {30, 60});
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      text);
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            gfx::Range(10, 20));

  // cursor end before begin
  input_method_context_->OnPreeditString(text_utf8, {}, {60, 30});
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      text);
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            gfx::Range(20, 10));
}

TEST_F(WaylandInputMethodContextWithMockV3Test,
       OnPreeditChangedInvalidCursorEnd) {
  const std::u16string text(50, u'あ');
  const std::string text_utf8 = base::UTF16ToUTF8(text);

  // Cursor end is outside of preedit text. So neither surrounding text nor
  // selection should be updated.
  input_method_context_->OnPreeditString(text_utf8, {}, {30, 999999});
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      u"");
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            gfx::Range(0, 0));

  // Cursor end is in the middle of a character. So neither surrounding text nor
  // selection should be updated.
  input_method_context_->OnPreeditString(text_utf8, {}, {30, 32});
  EXPECT_EQ(
      input_method_context_->predicted_state_for_testing().surrounding_text,
      u"");
  EXPECT_EQ(input_method_context_->predicted_state_for_testing().selection,
            gfx::Range(0, 0));
}

TEST_F(WaylandInputMethodContextWithMockV3Test,
       OnPreeditChangedGnomeWorkaround) {
  const std::u16string text(50, u'あ');
  const std::string text_utf8 = base::UTF16ToUTF8(text);

  // workaround should NOT be used when desktop is not gnome.
  std::unique_ptr<WaylandInputMethodContext> input_method_context;
  input_method_context = std::make_unique<WaylandInputMethodContext>(
      connection_.get(), keyboard_delegate_.get(),
      input_method_context_delegate_.get());
  input_method_context->SetDesktopEnvironmentForTesting(
      base::nix::DESKTOP_ENVIRONMENT_KDE3);

  input_method_context->OnPreeditString(text_utf8, {}, {60, 30});
  EXPECT_EQ(
      input_method_context->predicted_state_for_testing().surrounding_text,
      text);
  EXPECT_EQ(input_method_context->predicted_state_for_testing().selection,
            gfx::Range(20, 10));

  // workaround should be used when desktop is gnome.
  input_method_context = std::make_unique<WaylandInputMethodContext>(
      connection_.get(), keyboard_delegate_.get(),
      input_method_context_delegate_.get());
  input_method_context->SetDesktopEnvironmentForTesting(
      base::nix::DESKTOP_ENVIRONMENT_GNOME);
  input_method_context->OnPreeditString(text_utf8, {}, {60, 30});
  EXPECT_EQ(
      input_method_context->predicted_state_for_testing().surrounding_text,
      text);
  EXPECT_EQ(input_method_context->predicted_state_for_testing().selection,
            gfx::Range(20, 20));
}

}  // namespace ui
