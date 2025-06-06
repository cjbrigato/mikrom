// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_VIEW_HOST_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_VIEW_HOST_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "build/build_config.h"
#include "extensions/browser/extension_host.h"
#include "extensions/browser/extension_host_registry.h"

#if !BUILDFLAG(IS_ANDROID)
#include "chrome/browser/extensions/extension_view_host_web_modal_handler.h"
#endif  // !BUILDFLAG(IS_ANDROID)

namespace content {
class SiteInstance;
class WebContents;
}

namespace extensions {

class ExtensionView;

// The ExtensionHost for an extension that backs a view in the browser UI. For
// example, this could be an extension popup or dialog, but not a background
// page.
class ExtensionViewHost
    : public ExtensionHost,
      public ExtensionHostRegistry::Observer {
 public:
  class Delegate {
   public:
    Delegate(const Delegate&) = delete;
    Delegate& operator=(const Delegate&) = delete;
    virtual ~Delegate();

    // Opens a URL with the given disposition.
    virtual content::WebContents* OpenURL(
        const content::OpenURLParams& params,
        base::OnceCallback<void(content::NavigationHandle&)>
            navigation_handle_callback) = 0;

    // Allows handling keyboard events before sending to the renderer.
    virtual content::KeyboardEventProcessingResult PreHandleKeyboardEvent(
        content::WebContents* source,
        const input::NativeWebKeyboardEvent& event) = 0;

    // Called when an eye dropper should open. Returns the eye dropper window.
    virtual std::unique_ptr<content::EyeDropper> OpenEyeDropper(
        content::RenderFrameHost* frame,
        content::EyeDropperListener* listener) = 0;

    // Returns the WindowController associated with this ExtensionViewHost, or
    // nullptr if no window is associated with the delegate.
    virtual WindowController* GetExtensionWindowController() const = 0;

   protected:
    Delegate();
  };

  ExtensionViewHost(const Extension* extension,
                    content::SiteInstance* site_instance,
                    content::BrowserContext* browser_context,
                    const GURL& url,
                    mojom::ViewType host_type,
                    std::unique_ptr<Delegate> delegate);

  ExtensionViewHost(const ExtensionViewHost&) = delete;
  ExtensionViewHost& operator=(const ExtensionViewHost&) = delete;

  ~ExtensionViewHost() override;

  void set_view(ExtensionView* view) { view_ = view; }
  ExtensionView* view() { return view_; }

  // ExtensionHost
  void OnDidStopFirstLoad() override;
  void LoadInitialURL() override;
  bool IsBackgroundPage() const override;

  // content::WebContentsObserver:
  void ReadyToCommitNavigation(
      content::NavigationHandle* navigation_handle) override;

  // content::WebContentsDelegate
  content::WebContents* OpenURLFromTab(
      content::WebContents* source,
      const content::OpenURLParams& params,
      base::OnceCallback<void(content::NavigationHandle&)>
          navigation_handle_callback) override;
  bool ShouldAllowRendererInitiatedCrossProcessNavigation(
      bool is_outermost_main_frame_navigation) override;
  content::KeyboardEventProcessingResult PreHandleKeyboardEvent(
      content::WebContents* source,
      const input::NativeWebKeyboardEvent& event) override;
  bool HandleKeyboardEvent(content::WebContents* source,
                           const input::NativeWebKeyboardEvent& event) override;
  bool PreHandleGestureEvent(content::WebContents* source,
                             const blink::WebGestureEvent& event) override;
  void RunFileChooser(content::RenderFrameHost* render_frame_host,
                      scoped_refptr<content::FileSelectListener> listener,
                      const blink::mojom::FileChooserParams& params) override;
  std::unique_ptr<content::EyeDropper> OpenEyeDropper(
      content::RenderFrameHost* frame,
      content::EyeDropperListener* listener) override;
  void ResizeDueToAutoResize(content::WebContents* source,
                             const gfx::Size& new_size) override;

  // content::WebContentsObserver
  void RenderFrameCreated(content::RenderFrameHost* frame_host) override;

  // extensions::ExtensionFunctionDispatcher::Delegate
  WindowController* GetExtensionWindowController() const override;
  content::WebContents* GetVisibleWebContents() const override;

  // ExtensionHostRegistry::Observer:
  void OnExtensionHostDocumentElementAvailable(
      content::BrowserContext* browser_context,
      ExtensionHost* extension_host) override;

 private:
  // Returns whether the provided event is a raw escape keypress in a
  // mojom::ViewType::kExtensionPopup.
  bool IsEscapeInPopup(const input::NativeWebKeyboardEvent& event) const;

  // Handles keyboard events that were not handled by HandleKeyboardEvent().
  // Platform specific implementation may override this method to handle the
  // event in platform specific way. Returns whether the events are handled.
  bool UnhandledKeyboardEvent(content::WebContents* source,
                              const input::NativeWebKeyboardEvent& event);

  std::unique_ptr<Delegate> delegate_;

  // View that shows the rendered content in the UI.
  raw_ptr<ExtensionView, DanglingUntriaged> view_ = nullptr;

#if !BUILDFLAG(IS_ANDROID)
  std::unique_ptr<ExtensionViewHostWebModalHandler> web_modal_handler_;
#endif  // !BUILDFLAG(IS_ANDROID)

  base::ScopedObservation<ExtensionHostRegistry,
                          ExtensionHostRegistry::Observer>
      host_registry_observation_{this};
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_VIEW_HOST_H_
