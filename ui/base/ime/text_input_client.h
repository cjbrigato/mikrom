// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_BASE_IME_TEXT_INPUT_CLIENT_H_
#define UI_BASE_IME_TEXT_INPUT_CLIENT_H_

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "base/component_export.h"
#include "base/i18n/rtl.h"
#include "base/memory/weak_ptr.h"
#include "build/build_config.h"
#include "services/metrics/public/cpp/ukm_source_id.h"
#include "ui/base/ime/composition_text.h"
#include "ui/base/ime/grammar_fragment.h"
#include "ui/base/ime/ime_key_event_dispatcher.h"
#include "ui/base/ime/text_input_mode.h"
#include "ui/base/ime/text_input_type.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gfx/range/range.h"
#include "url/gurl.h"

namespace gfx {
class Point;
class Rect;
}

namespace ui {

class KeyEvent;
enum class TextEditCommand;

#if BUILDFLAG(IS_WIN)
// Mirrors `dwFlags` for ITextStoreACP::GetACPFromPoint:
// https://learn.microsoft.com/en-us/windows/win32/api/textstor/nf-textstor-itextstoreacp-getacpfrompoint
enum class IndexFromPointFlags : uint8_t {
  kNone = 0,
  // Mirror of: GXFPF_ROUND_NEAREST
  // Overrides the default behavior of `GetACPFromPoint` if and only if a
  // character bounds contains `point`. Finds the index of the character which
  // has the closest origin to `point`.
  kNearestToContainedPoint = 0x01,
  // Mirror of: GXFPF_NEAREST
  // Overrides the default behavior of `GetACPFromPoint` if and only if no
  // character bounds contain `point`. Finds the index of the character which
  // has the closest origin to `point`.
  kNearestToUncontainedPoint = 0x02,
  // Alias for having both flags set. Always overrides the default behavior.
  // Finds the index of the character which has the closest origin to `point`.
  kNearestToPoint = kNearestToContainedPoint | kNearestToUncontainedPoint,
};
#endif  // BUILDFLAG(IS_WIN)

// An interface implemented by a View that needs text input support.
// All strings related to IME operations should be UTF-16 encoded and all
// indices/ranges relative to those strings should be UTF-16 code units.
class COMPONENT_EXPORT(UI_BASE_IME) TextInputClient {
 public:
  // The reason the control was focused, used by the virtual keyboard to detect
  // pen input.
  enum FocusReason {
    // Not focused.
    FOCUS_REASON_NONE,
    // User initiated with mouse.
    FOCUS_REASON_MOUSE,
    // User initiated with touch.
    FOCUS_REASON_TOUCH,
    // User initiated with pen.
    FOCUS_REASON_PEN,
    // All other reasons (e.g. system initiated, mouse)
    FOCUS_REASON_OTHER,
  };

#if BUILDFLAG(IS_CHROMEOS)
  enum SubClass {
    kRenderWidgetHostViewAura = 0,
    kArcImeService = 1,
    kTextField = 2,
    kMaxValue = kTextField,
  };
#endif

  virtual ~TextInputClient();

  // This should be implemented by the most concrete class.
  virtual base::WeakPtr<TextInputClient> AsWeakPtr() = 0;

  // Input method result -------------------------------------------------------

  // Sets composition text and attributes. If there is composition text already,
  // it'll be replaced by the new one. Otherwise, current selection will be
  // replaced. If there is no selection, the composition text will be inserted
  // at the insertion point.
  virtual void SetCompositionText(const ui::CompositionText& composition) = 0;

  // Converts current composition text into final content.
  // If keep_selection is true, keep the selected range unchanged
  // otherwise, set it to be after the newly committed text.
  // If text was committed, return the number of characters committed.
  // If we do not know what the number of characters committed is, return
  // std::numeric_limits<size_t>::max().
  virtual size_t ConfirmCompositionText(bool keep_selection) = 0;

  // Removes current composition text.
  virtual void ClearCompositionText() = 0;

  enum class InsertTextCursorBehavior {
    // Move cursor to the position after the last character in the text.
    // e.g. for "hello", the cursor will be right after "o".
    kMoveCursorAfterText,
    // Move cursor to the position before the first character in the text.
    // e.g. for "hello", the cursor will be right before "h".
    kMoveCursorBeforeText,
  };

  // Inserts a given text at the insertion point. Current composition text or
  // selection will be removed. This method should never be called when the
  // current text input type is TEXT_INPUT_TYPE_NONE.
  virtual void InsertText(const std::u16string& text,
                          InsertTextCursorBehavior cursor_behavior) = 0;

  // Inserts a single char at the insertion point. Unlike above InsertText()
  // method, this method takes an |event| parameter indicating
  // the event that was unprocessed. This method should only be
  // called when a key press is not handled by the input method but still
  // generates a character (eg. by the keyboard driver). In another word, the
  // preceding key press event should not be a VKEY_PROCESSKEY.
  // This method will be called whenever a char is generated by the keyboard,
  // even if the current text input type is TEXT_INPUT_TYPE_NONE.
  virtual void InsertChar(const ui::KeyEvent& event) = 0;

  // Returns whether the current insertion point supports images.
  virtual bool CanInsertImage();

  // Inserts a given image at the insertion point. It should be only called when
  // CanInsertImage returns true.
  virtual void InsertImage(const GURL& src) {}

  // Input context information -------------------------------------------------

  // Returns current text input type. It could be changed and even becomes
  // TEXT_INPUT_TYPE_NONE at runtime.
  virtual ui::TextInputType GetTextInputType() const = 0;

  // Returns current text input mode. It could be changed and even becomes
  // TEXT_INPUT_MODE_DEFAULT at runtime.
  virtual ui::TextInputMode GetTextInputMode() const = 0;

  // Returns the current text direction.
  virtual base::i18n::TextDirection GetTextDirection() const = 0;

  // Returns the current text input flags, which is a bit map of
  // WebTextInputType defined in blink. This is valid only for web input fileds;
  // it will return TEXT_INPUT_FLAG_NONE for native input fields.
  virtual int GetTextInputFlags() const = 0;

  // Returns if the client supports inline composition currently.
  virtual bool CanComposeInline() const = 0;

  // Returns current caret (insertion point) bounds in the universal screen
  // coordinates in DIP (Density Independent Pixel).
  // If there is selection, then the selection bounds will be returned.
  virtual gfx::Rect GetCaretBounds() const = 0;

  // Returns the bounds of the rectangle which encloses the selection region.
  // Bounds are in the screen coordinates. An empty value should be returned if
  // there is not any selection or this function is not implemented.
  virtual gfx::Rect GetSelectionBoundingBox() const = 0;

#if BUILDFLAG(IS_WIN)
  // For StylusHandwritingWin gesture support, this method mirrors the
  // expectations of ITextStoreACP::GetTextExt. Returns the smallest
  // axis-aligned bounding box which contains all of the axis-aligned character
  // bounding boxes specified by the character offset `range` [start, end).
  // The result is in DIP screen coordinates.
  //
  // For renderer content, "ProximateCharacterBounds" uses a cached subset of
  // the actual character bounding boxes, so requests for valid character
  // indices may fall outside of the cached rage. If `range` extends outside the
  // cached range, regardless of whether the character offset is valid for the
  // actual text, std::nullopt is returned.
  //
  // For views content, it's possible to retrieve accurate results for
  // "ProximateCharacterBounds" since the data is readily available. The caching
  // mechanism is to mitigate performance costs (CPU and memory) when processing
  // very large documents.
  virtual std::optional<gfx::Rect> GetProximateCharacterBounds(
      const gfx::Range& range) const = 0;

  // For StylusHandwritingWin gesture support, this method mirrors the
  // expectations of ITextStoreACP::GetACPFromPoint. Depending on which `flags`
  // are provided, returns an appropriate character offset relative to
  // `screen_point_in_dips`. See comments around IndexFromPointFlags and its
  // values for details.
  //
  // For renderer content, "ProximateCharacterBounds" uses a cached subset of
  // the actual character bounding boxes, so requests for `screen_point_in_dips`
  // contained by a character bounding box may not be considered "hit" by this
  // method if that character falls outside the cached range, or what's
  // considered "nearest" may be technically incorrect based on this fact. If
  // no `flags` are provided and `screen_point_in_dips` isn't contained by any
  // cached character bounds, regardless of whether `screen_point_in_dips` is
  // technically valid for the content, std::nullopt is returned. If either or
  // both `flags` are provided, this is guaranteed to return *some* character
  // offset, even if it's not the most appropriate offset based on the actual
  // content.
  //
  // For views content, it's possible to retrieve accurate results for
  // "ProximateCharacterBounds" since the data is readily available. The caching
  // mechanism is to mitigate performance costs (CPU and memory) when processing
  // very large documents.
  virtual std::optional<size_t> GetProximateCharacterIndexFromPoint(
      const gfx::Point& screen_point_in_dips,
      IndexFromPointFlags flags) const = 0;
#endif  // BUILDFLAG(IS_WIN)

  // Retrieves the composition character boundary rectangle in the universal
  // screen coordinates in DIP (Density Independent Pixel).
  // The |index| is zero-based index of character position in composition text.
  // Returns false if there is no composition text or |index| is out of range.
  // The |rect| is not touched in the case of failure.
  virtual bool GetCompositionCharacterBounds(size_t index,
                                             gfx::Rect* rect) const = 0;

  // Returns true if there is composition text.
  virtual bool HasCompositionText() const = 0;

  // Returns how the text input client was focused.
  virtual FocusReason GetFocusReason() const = 0;

  // Document content operations ----------------------------------------------

  // Retrieves the UTF-16 code unit range containing accessible text in
  // the View. It must cover the composition and selection range.
  // Returns false if the information cannot be retrieved right now.
  virtual bool GetTextRange(gfx::Range* range) const = 0;

  // Retrieves the UTF-16 code unit range of current composition text.
  // Returns false if the information cannot be retrieved right now.
  virtual bool GetCompositionTextRange(gfx::Range* range) const = 0;

  // Retrieves the UTF-16 code unit range of current selection in the text
  // input. Returns false if the information cannot be retrieved right now.
  // Returns false if the selected text is outside of the text input (== the
  // text input is not focused)
  virtual bool GetEditableSelectionRange(gfx::Range* range) const = 0;

  // Selects the given UTF-16 code unit range. Current composition text
  // will be confirmed before selecting the range.
  // Returns false if the operation is not supported.
  virtual bool SetEditableSelectionRange(const gfx::Range& range) = 0;

#if BUILDFLAG(IS_MAC)
  // Deletes contents in the given UTF-16 code unit range. Current
  // composition text will be confirmed before deleting the range.
  // The input caret will be moved to the place where the range gets deleted.
  // ExtendSelectionAndDelete should be used instead as far as you are deleting
  // characters around current caret. This function with the range based on
  // GetEditableSelectionRange has a race condition due to asynchronous IPCs
  // between browser and renderer. Returns false if the operation is not
  // supported.
  virtual bool DeleteRange(const gfx::Range& range) = 0;
#endif

  // Retrieves the text content in a given UTF-16 code unit range.
  // The result will be stored into |*text|.
  // Returns false if the operation is not supported or the specified range
  // is out of the text range returned by GetTextRange().
  virtual bool GetTextFromRange(const gfx::Range& range,
                                std::u16string* text) const = 0;

  // Miscellaneous ------------------------------------------------------------

  // Called whenever current keyboard layout or input method is changed,
  // especially the change of input locale and text direction.
  virtual void OnInputMethodChanged() = 0;

  // Called whenever the user requests to change the text direction and layout
  // alignment of the current text box. It's for supporting ctrl-shift on
  // Windows.
  // Returns false if the operation is not supported.
  virtual bool ChangeTextDirectionAndLayoutAlignment(
      base::i18n::TextDirection direction) = 0;

  // Deletes the current selection plus the specified number of char16 values
  // before and after the selection or caret. This function should be used
  // instead of calling DeleteRange with GetEditableSelectionRange, because
  // GetEditableSelectionRange may not be the latest value due to asynchronous
  // of IPC between browser and renderer.
  virtual void ExtendSelectionAndDelete(size_t before, size_t after) = 0;

#if BUILDFLAG(IS_CHROMEOS)
  // Deletes any active composition, and the current selection plus the
  // specified number of char16 values before and after the selection, and
  // replaces it with |replacement_string|.
  // Places the cursor at the end of |replacement_string|.
  //
  // Clients should try to implement this with an atomic operation to ensure
  // that input method features like autocorrection works well. However, it's
  // also okay for clients to fall back to ExtendSelectionAndDelete followed by
  // InsertText for a degraded experience.
  virtual void ExtendSelectionAndReplace(
      size_t length_before_selection,
      size_t length_after_selection,
      std::u16string_view replacement_string);
#endif

  // Ensure the caret is not in |rect|.  |rect| is in screen coordinates in
  // DIP (Density Independent Pixel) and may extend beyond the bounds of this
  // TextInputClient.
  virtual void EnsureCaretNotInRect(const gfx::Rect& rect) = 0;

  // Returns true if |command| is currently allowed to be executed.
  virtual bool IsTextEditCommandEnabled(TextEditCommand command) const = 0;

  // Execute |command| on the next key event. This allows a TextInputClient to
  // be informed of a platform-independent edit command that has been derived
  // from the key event currently being dispatched (but not yet sent to the
  // TextInputClient). The edit command will take into account any OS-specific,
  // or user-specified, keybindings that may be set up.
  virtual void SetTextEditCommandForNextKeyEvent(TextEditCommand command) = 0;

  // Returns a UKM source for identifying the input client (e.g. for web input
  // clients, the source represents the URL of the page).
  virtual ukm::SourceId GetClientSourceForMetrics() const = 0;

  // Returns whether text entered into this text client should be used to
  // improve typing suggestions for the user. This should return false for text
  // fields that are considered 'private' (e.g. in incognito tabs).
  virtual bool ShouldDoLearning() = 0;

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  // Start composition over a given UTF-16 code range from existing text. This
  // should only be used for composition scenario when IME wants to start
  // composition on existing text. Returns whether the operation was successful.
  // Must not be called with an invalid range.
  virtual bool SetCompositionFromExistingText(
      const gfx::Range& range,
      const std::vector<ui::ImeTextSpan>& ui_ime_text_spans) = 0;
#endif

#if BUILDFLAG(IS_CHROMEOS)
  // Return the start and end index of the autocorrect range. If non-existent,
  // return an empty Range.
  virtual gfx::Range GetAutocorrectRange() const = 0;

  // Return the location of the autocorrect range as a gfx::Rect object.
  // If gfx::Rect is empty, then the autocorrect character bounds have not been
  // set.
  // These bounds are in screen coordinates.
  virtual gfx::Rect GetAutocorrectCharacterBounds() const = 0;

  // Sets the autocorrect range to |range|. Clients should show some visual
  // indication of the range, such as flashing or underlining. If |range| is
  // empty, then the autocorrect range is cleared.
  // Returns true if the operation was successful. If |range| is invalid, then
  // no modifications are made and this function returns false.
  virtual bool SetAutocorrectRange(const gfx::Range& range) = 0;

  // Returns the grammar fragment which contains the current cursor. If
  // non-existent, returns nullopt.
  virtual std::optional<GrammarFragment> GetGrammarFragmentAtCursor() const;

  // Clears all the grammar fragments in |range|, returns whether the operation
  // is successful. Should return true if the there is no fragment in the range.
  virtual bool ClearGrammarFragments(const gfx::Range& range);

  // Adds new grammar markers according to |fragments|. Clients should show
  // some visual indications such as underlining. Returns whether the operation
  // is successful.
  virtual bool AddGrammarFragments(
      const std::vector<GrammarFragment>& fragments);

  // Does the current text client support always confirming a composition, even
  // if there isn't a composition currently set?
  virtual bool SupportsAlwaysConfirmComposition();
#endif

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_CHROMEOS)
  // Returns false if either the focused editable element or the EditContext
  // bounds is not available, else it returns true with the control and
  // selection bounds for the EditContext or control bounds of the active
  // editable element. This is used to report the layout bounds of the text
  // input control to TSF on Windows and to the Virtual Keyboard extension on
  // ChromeOS.
  virtual void GetActiveTextInputControlLayoutBounds(
      std::optional<gfx::Rect>* control_bounds,
      std::optional<gfx::Rect>* selection_bounds) = 0;
#endif

#if BUILDFLAG(IS_WIN)
  // Notifies accessibility about active composition. This API is currently
  // only defined for TSF which is available only on Windows
  // https://docs.microsoft.com/en-us/windows/desktop/api/UIAutomationCore/
  // nf-uiautomationcore-itexteditprovider-getactivecomposition
  // It notifies the composition range, composition text and whether the
  // composition has been committed or not.
  virtual void SetActiveCompositionForAccessibility(
      const gfx::Range& range,
      const std::u16string& active_composition_text,
      bool is_composition_committed) = 0;
#endif

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_CHROMEOS)
  struct EditingContext {
    // Contains the active web content's URL.
    GURL page_url;
  };

  virtual ui::TextInputClient::EditingContext GetTextEditingContext();

  // Notifies TSF when a frame with a committed Url receives focus.
  virtual void NotifyOnFrameFocusChanged() {}
#endif

  // Called before ui::InputMethod dispatches a not-consumed event to PostIME
  // phase. This method gives TextInputClient a chance to intercept event
  // dispatching.
  virtual void OnDispatchingKeyEventPostIME(ui::KeyEvent* event) {}
};

#if BUILDFLAG(IS_WIN)
COMPONENT_EXPORT(UI_BASE_IME)
extern std::ostream& operator<<(std::ostream& os, IndexFromPointFlags flags);
#endif  // BUILDFLAG(IS_WIN)

}  // namespace ui

#if BUILDFLAG(IS_WIN)
inline constexpr ui::IndexFromPointFlags operator&(ui::IndexFromPointFlags a,
                                                   ui::IndexFromPointFlags b) {
  using T = std::underlying_type_t<ui::IndexFromPointFlags>;
  return static_cast<ui::IndexFromPointFlags>(static_cast<T>(a) &
                                              static_cast<T>(b));
}

inline constexpr ui::IndexFromPointFlags operator|(ui::IndexFromPointFlags a,
                                                   ui::IndexFromPointFlags b) {
  using T = std::underlying_type_t<ui::IndexFromPointFlags>;
  return static_cast<ui::IndexFromPointFlags>(static_cast<T>(a) |
                                              static_cast<T>(b));
}

inline constexpr ui::IndexFromPointFlags& operator|=(
    ui::IndexFromPointFlags& a,
    ui::IndexFromPointFlags b) {
  a = a | b;
  return a;
}
#endif  // BUILDFLAG(IS_WIN)

#endif  // UI_BASE_IME_TEXT_INPUT_CLIENT_H_
