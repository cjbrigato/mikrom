// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_ARC_INPUT_METHOD_MANAGER_INPUT_CONNECTION_IMPL_H_
#define CHROME_BROWSER_ASH_ARC_INPUT_METHOD_MANAGER_INPUT_CONNECTION_IMPL_H_

#include <memory>
#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/timer/timer.h"
#include "chrome/browser/ash/arc/input_method_manager/arc_input_method_manager_bridge.h"
#include "chrome/browser/ash/input_method/input_method_engine.h"
#include "chromeos/ash/experiences/arc/mojom/input_method_manager.mojom-forward.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"

namespace arc {

// The implementation of mojom::InputConnection interface. It's generated for
// each text field and accepts text edit commands from the ARC container.
class InputConnectionImpl : public mojom::InputConnection {
 public:
  InputConnectionImpl(ash::input_method::InputMethodEngine* ime_engine,
                      ArcInputMethodManagerBridge* imm_bridge,
                      int input_context_id);

  InputConnectionImpl(const InputConnectionImpl&) = delete;
  InputConnectionImpl& operator=(const InputConnectionImpl&) = delete;

  ~InputConnectionImpl() override;

  // Binds this class to a passed pending remote.
  void Bind(mojo::PendingRemote<mojom::InputConnection>* remote);
  // Sends current text input state to the ARC container.
  void UpdateTextInputState(bool is_input_state_update_requested);
  // Gets current text input state.
  mojom::TextInputStatePtr GetTextInputState(
      bool is_input_state_update_requested) const;

  // mojom::InputConnection overrides:
  void CommitText(const std::u16string& text, int new_cursor_pos) override;
  void DeleteSurroundingText(int before, int after) override;
  void FinishComposingText() override;
  void SetComposingText(
      const std::u16string& text,
      int new_cursor_pos,
      const std::optional<gfx::Range>& new_selection_range) override;
  void RequestTextInputState(
      mojom::InputConnection::RequestTextInputStateCallback callback) override;
  void SetSelection(const gfx::Range& new_selection_range) override;
  void SendKeyEvent(std::unique_ptr<ui::KeyEvent> key_event) override;
  void SetCompositionRange(const gfx::Range& new_composition_range) override;

 private:
  // Starts the timer to send new TextInputState.
  // This method should be called before doing any IME operation to catch state
  // update surely. Some implementations of TextInputClient are synchronous. If
  // starting timer is after API call, the timer won't be cancelled.
  void StartStateUpdateTimer();

  void SendControlKeyEvent(const std::u16string& text);

  const raw_ptr<ash::input_method::InputMethodEngine> ime_engine_;  // Not owned
  const raw_ptr<ArcInputMethodManagerBridge> imm_bridge_;  // Not owned
  const int input_context_id_;

  mojo::Receiver<mojom::InputConnection> receiver_{this};

  base::OneShotTimer state_update_timer_;
};

}  // namespace arc

#endif  // CHROME_BROWSER_ASH_ARC_INPUT_METHOD_MANAGER_INPUT_CONNECTION_IMPL_H_
