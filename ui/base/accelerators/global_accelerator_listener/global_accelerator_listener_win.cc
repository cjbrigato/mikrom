// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/base/accelerators/global_accelerator_listener/global_accelerator_listener_win.h"

#include <memory>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/win/win_util.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/media_keys_listener_manager.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_code_conversion_win.h"
#include "ui/gfx/win/singleton_hwnd.h"
#include "ui/gfx/win/singleton_hwnd_hot_key_observer.h"

using content::BrowserThread;

namespace ui {

// static
GlobalAcceleratorListener* GlobalAcceleratorListener::GetInstance() {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  static GlobalAcceleratorListenerWin* instance =
      new GlobalAcceleratorListenerWin();
  return instance;
}

GlobalAcceleratorListenerWin::GlobalAcceleratorListenerWin()
    : is_listening_(false) {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
}

GlobalAcceleratorListenerWin::~GlobalAcceleratorListenerWin() {
  if (is_listening_) {
    StopListening();
  }
}

void GlobalAcceleratorListenerWin::StartListening() {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!is_listening_);     // Don't start twice.
  DCHECK(!hotkeys_.empty());  // Also don't start if no hotkey is registered.
  is_listening_ = true;
}

void GlobalAcceleratorListenerWin::StopListening() {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(is_listening_);     // No point if we are not already listening.
  DCHECK(hotkeys_.empty());  // Make sure the map is clean before ending.
  is_listening_ = false;
}

void GlobalAcceleratorListenerWin::OnWndProc(HWND hwnd,
                                             UINT message,
                                             WPARAM wparam,
                                             LPARAM lparam) {
  // SingletonHwndHotKeyObservers should only send us hot key messages.
  DCHECK_EQ(WM_HOTKEY, static_cast<int>(message));

  int key_code = HIWORD(lparam);
  int modifiers = 0;
  modifiers |= (LOWORD(lparam) & MOD_SHIFT) ? ui::EF_SHIFT_DOWN : 0;
  modifiers |= (LOWORD(lparam) & MOD_ALT) ? ui::EF_ALT_DOWN : 0;
  modifiers |= (LOWORD(lparam) & MOD_CONTROL) ? ui::EF_CONTROL_DOWN : 0;
  ui::Accelerator accelerator(ui::KeyboardCodeForWindowsKeyCode(key_code),
                              modifiers);

  NotifyKeyPressed(accelerator);
}

bool GlobalAcceleratorListenerWin::StartListeningForAccelerator(
    const ui::Accelerator& accelerator) {
  DCHECK(hotkeys_.find(accelerator) == hotkeys_.end());

  // TODO(crbug.com/40622191): We should be using
  // `media_keys_listener_manager->StartWatchingMediaKey(...)` here, but that
  // currently breaks the GlobalCommandsApiTest.GlobalDuplicatedMediaKey test.
  // Instead, we'll just disable the MediaKeysListenerManager handling here, and
  // listen using the fallback RegisterHotKey method.
  if (content::MediaKeysListenerManager::IsMediaKeysListenerManagerEnabled() &&
      accelerator.IsMediaKey()) {
    content::MediaKeysListenerManager* media_keys_listener_manager =
        content::MediaKeysListenerManager::GetInstance();
    DCHECK(media_keys_listener_manager);

    registered_media_keys_++;
    media_keys_listener_manager->DisableInternalMediaKeyHandling();
  }

  // Convert Accelerator modifiers to OS modifiers.
  int modifiers = 0;
  modifiers |= accelerator.IsShiftDown() ? MOD_SHIFT : 0;
  modifiers |= accelerator.IsCtrlDown() ? MOD_CONTROL : 0;
  modifiers |= accelerator.IsAltDown() ? MOD_ALT : 0;

  // Create an observer that registers a hot key for `accelerator`.
  std::unique_ptr<gfx::SingletonHwndHotKeyObserver> observer =
      gfx::SingletonHwndHotKeyObserver::Create(
          base::BindRepeating(&GlobalAcceleratorListenerWin::OnWndProc,
                              base::Unretained(this)),
          accelerator.key_code(), modifiers);

  if (!observer) {
    // Most likely error: 1409 (Hotkey already registered).
    return false;
  }

  hotkeys_[accelerator] = std::move(observer);
  return true;
}

void GlobalAcceleratorListenerWin::StopListeningForAccelerator(
    const ui::Accelerator& accelerator) {
  HotKeyMap::iterator it = hotkeys_.find(accelerator);
  CHECK(it != hotkeys_.end());

  // TODO(crbug.com/40622191): We should be using
  // `media_keys_listener_manager->StopWatchingMediaKey(...)` here.
  if (content::MediaKeysListenerManager::IsMediaKeysListenerManagerEnabled() &&
      accelerator.IsMediaKey()) {
    registered_media_keys_--;
    DCHECK_GE(registered_media_keys_, 0);
    if (registered_media_keys_ == 0) {
      content::MediaKeysListenerManager* media_keys_listener_manager =
          content::MediaKeysListenerManager::GetInstance();
      DCHECK(media_keys_listener_manager);

      media_keys_listener_manager->EnableInternalMediaKeyHandling();
    }
  }

  hotkeys_.erase(it);
}

void GlobalAcceleratorListenerWin::OnMediaKeysAccelerator(
    const ui::Accelerator& accelerator) {
  // We should not receive media key events that we didn't register for.
  DCHECK(hotkeys_.find(accelerator) != hotkeys_.end());
  NotifyKeyPressed(accelerator);
}

}  // namespace ui
