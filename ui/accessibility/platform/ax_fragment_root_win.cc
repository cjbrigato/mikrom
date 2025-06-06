// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/accessibility/platform/ax_fragment_root_win.h"

#include <limits>
#include <unordered_map>

#include "base/containers/contains.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/trace_event/memory_allocator_dump.h"
#include "base/trace_event/memory_dump_manager.h"
#include "base/trace_event/memory_dump_provider.h"
#include "base/trace_event/process_memory_dump.h"
#include "base/trace_event/typed_macros.h"
#include "base/win/scoped_safearray.h"
#include "ui/accessibility/accessibility_features.h"
#include "ui/accessibility/platform/ax_fragment_root_delegate_win.h"
#include "ui/accessibility/platform/ax_platform.h"
#include "ui/accessibility/platform/ax_platform_node_win.h"
#include "ui/accessibility/platform/uia_registrar_win.h"
#include "ui/base/win/atl_module.h"

namespace ui {

class AXFragmentRootPlatformNodeWin : public AXPlatformNodeWin,
                                      public IItemContainerProvider,
                                      public IRawElementProviderFragmentRoot,
                                      public IRawElementProviderAdviseEvents {
 public:
  BEGIN_COM_MAP(AXFragmentRootPlatformNodeWin)
  COM_INTERFACE_ENTRY(IItemContainerProvider)
  COM_INTERFACE_ENTRY(IRawElementProviderFragmentRoot)
  COM_INTERFACE_ENTRY(IRawElementProviderAdviseEvents)
  COM_INTERFACE_ENTRY_CHAIN(AXPlatformNodeWin)
  END_COM_MAP()

  static Pointer Create(AXFragmentRootWin& delegate) {
    // Make sure ATL is initialized in this module.
    win::CreateATLModuleIfNeeded();

    CComObject<AXFragmentRootPlatformNodeWin>* instance = nullptr;
    HRESULT hr =
        CComObject<AXFragmentRootPlatformNodeWin>::CreateInstance(&instance);
    CHECK(SUCCEEDED(hr));
    instance->Init(delegate);
    instance->AddRef();
    return Pointer(instance);
  }

  //
  // IItemContainerProvider methods.
  //
  IFACEMETHODIMP FindItemByProperty(
      IRawElementProviderSimple* start_after_element,
      PROPERTYID property_id,
      VARIANT value,
      IRawElementProviderSimple** result) override {
    WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_ITEMCONTAINER_FINDITEMBYPROPERTY);
    UIA_VALIDATE_CALL_1_ARG(result);
    *result = nullptr;

    // We currently only support the custom UIA property ID for unique id.
    if (property_id == UiaRegistrarWin::GetInstance().GetUniqueIdPropertyId() &&
        value.vt == VT_BSTR) {
      int32_t ax_unique_id;
      if (!base::StringToInt(value.bstrVal, &ax_unique_id))
        return S_OK;

      // In the Windows accessibility platform implementation, id 0 represents
      // self; a positive id represents the immediate descendants; and a
      // negative id represents a unique id that can be mapped to any node.
      if (AXPlatformNodeWin* result_platform_node =
              static_cast<AXPlatformNodeWin*>(GetFromUniqueId(-ax_unique_id))) {
        if (start_after_element) {
          Microsoft::WRL::ComPtr<AXPlatformNodeWin> start_after_platform_node;
          if (!SUCCEEDED(start_after_element->QueryInterface(
                  IID_PPV_ARGS(&start_after_platform_node))))
            return E_INVALIDARG;

          // We want |result| to be nullptr if it comes before or is equal to
          // |start_after_element|.
          if (start_after_platform_node->CompareTo(*result_platform_node) >= 0)
            return S_OK;
        }

        return result_platform_node->QueryInterface(IID_PPV_ARGS(result));
      }
    }

    return E_INVALIDARG;
  }

  //
  // IRawElementProviderSimple methods.
  //

  IFACEMETHODIMP get_HostRawElementProvider(
      IRawElementProviderSimple** host_element_provider) override {
    WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_HOST_RAW_ELEMENT_PROVIDER);
    UIA_VALIDATE_CALL_1_ARG(host_element_provider);

    HWND hwnd = GetDelegate()->GetTargetForNativeAccessibilityEvent();
    return UiaHostProviderFromHwnd(hwnd, host_element_provider);
  }

  IFACEMETHODIMP GetPatternProvider(PATTERNID pattern_id,
                                    IUnknown** result) override {
    WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_PATTERN_PROVIDER);
    UIA_VALIDATE_CALL_1_ARG(result);
    *result = nullptr;

    if (pattern_id == UIA_ItemContainerPatternId) {
      AddRef();
      *result = static_cast<IItemContainerProvider*>(this);
      return S_OK;
    }

    return AXPlatformNodeWin::GetPatternProviderImpl(pattern_id, result);
  }

  IFACEMETHODIMP GetPropertyValue(PROPERTYID property_id,
                                  VARIANT* result) override {
    WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_PROPERTY_VALUE);
    UIA_VALIDATE_CALL_1_ARG(result);

    switch (property_id) {
      default:
        // UIA has a built-in provider that will expose values for several
        // properties based on the HWND. This information is useful to someone
        // examining the accessibility tree using tools such as Inspect. Return
        // VT_EMPTY for most properties so that we don't override values from
        // the default provider with blank data.
        result->vt = VT_EMPTY;
        break;

      case UIA_IsControlElementPropertyId:
      case UIA_IsContentElementPropertyId:
        // Override IsControlElement and IsContentElement to fine tune which
        // fragment roots appear in the control and content views.
        result->vt = VT_BOOL;
        result->boolVal =
            static_cast<AXFragmentRootWin*>(GetDelegate())->IsControlElement()
                ? VARIANT_TRUE
                : VARIANT_FALSE;
        break;
    }

    return S_OK;
  }

  //
  // IRawElementProviderFragment methods.
  //

  IFACEMETHODIMP get_FragmentRoot(
      IRawElementProviderFragmentRoot** fragment_root) override {
    WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_FRAGMENTROOT);
    UIA_VALIDATE_CALL_1_ARG(fragment_root);

    QueryInterface(IID_PPV_ARGS(fragment_root));
    return S_OK;
  }

  //
  // IRawElementProviderFragmentRoot methods.
  //
  IFACEMETHODIMP ElementProviderFromPoint(
      double screen_physical_pixel_x,
      double screen_physical_pixel_y,
      IRawElementProviderFragment** element_provider) override {
    UIA_VALIDATE_CALL_1_ARG(element_provider);
    WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_ELEMENT_PROVIDER_FROM_POINT);
    WIN_ACCESSIBILITY_API_PERF_HISTOGRAM(UMA_API_ELEMENT_PROVIDER_FROM_POINT);
    WIN_ACCESSIBILITY_SOURCE_API_PERF_HISTOGRAM(
        UMA_API_ELEMENT_PROVIDER_FROM_POINT);

    *element_provider = nullptr;

    gfx::NativeViewAccessible hit_element = nullptr;

    // Descend the tree until we get a non-hit or can't go any further.
    AXPlatformNode* node_to_test = this;
    do {
      gfx::NativeViewAccessible test_result =
          node_to_test->GetDelegate()->HitTestSync(screen_physical_pixel_x,
                                                   screen_physical_pixel_y);
      if (test_result != nullptr && test_result != hit_element) {
        hit_element = test_result;
        node_to_test = AXPlatformNode::FromNativeViewAccessible(test_result);
      } else {
        node_to_test = nullptr;
      }
    } while (node_to_test);

    if (hit_element)
      hit_element->QueryInterface(element_provider);

    return S_OK;
  }

  IFACEMETHODIMP GetFocus(IRawElementProviderFragment** focus) override {
    WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_FOCUS);
    UIA_VALIDATE_CALL_1_ARG(focus);

    *focus = nullptr;

    gfx::NativeViewAccessible focused_element = nullptr;

    // GetFocus() can return a node at the root of a subtree, for example when
    // transitioning from Views into web content. In such cases we want to
    // continue drilling to retrieve the actual focused element.
    AXPlatformNode* node_to_test = this;
    do {
      gfx::NativeViewAccessible test_result =
          node_to_test->GetDelegate()->GetFocus();
      if (test_result != nullptr && test_result != focused_element) {
        focused_element = test_result;
        node_to_test =
            AXPlatformNode::FromNativeViewAccessible(focused_element);
      } else {
        node_to_test = nullptr;
      }
    } while (node_to_test);

    if (focused_element)
      focused_element->QueryInterface(IID_PPV_ARGS(focus));

    return S_OK;
  }

  //
  // IRawElementProviderAdviseEvents methods.
  //

  IFACEMETHODIMP AdviseEventAdded(EVENTID event_id,
                                  SAFEARRAY* property_ids) override {
    WIN_ACCESSIBILITY_API_TRACE_EVENT("AdviseEventAdded");
    UIA_VALIDATE_CALL();
    WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_ADVISE_EVENT_ADDED);

    AXFragmentRootWin* root = static_cast<AXFragmentRootWin*>(GetDelegate());

    base::win::ScopedSafearray safe_array(property_ids);
    absl::Cleanup release_safe_array(
        [&safe_array]() { (void)safe_array.Release(); });

    auto lock = safe_array.CreateLockScope<VT_I4>();
    root->OnEventListenerAdded(event_id,
                               lock ? *lock : base::span<PROPERTYID>());

    return S_OK;
  }

  IFACEMETHODIMP AdviseEventRemoved(EVENTID event_id,
                                    SAFEARRAY* property_ids) override {
    WIN_ACCESSIBILITY_API_TRACE_EVENT("AdviseEventRemoved");
    UIA_VALIDATE_CALL();
    WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_ADVISE_EVENT_REMOVED);

    AXFragmentRootWin* root = static_cast<AXFragmentRootWin*>(GetDelegate());

    base::win::ScopedSafearray safe_array(property_ids);
    absl::Cleanup release_safe_array(
        [&safe_array]() { (void)safe_array.Release(); });

    auto lock = safe_array.CreateLockScope<VT_I4>();
    root->OnEventListenerRemoved(event_id,
                                 lock ? *lock : base::span<PROPERTYID>());

    return S_OK;
  }
};

class AXFragmentRootMapWin {
 public:
  static AXFragmentRootMapWin& GetInstance() {
    static base::NoDestructor<AXFragmentRootMapWin> instance;
    return *instance;
  }

  void AddFragmentRoot(gfx::AcceleratedWidget widget,
                       AXFragmentRootWin* fragment_root) {
    map_[widget] = fragment_root;
  }

  void RemoveFragmentRoot(gfx::AcceleratedWidget widget) { map_.erase(widget); }

  AXFragmentRootWin* GetFragmentRoot(gfx::AcceleratedWidget widget) const {
    const auto& entry = map_.find(widget);
    if (entry != map_.end())
      return entry->second;

    return nullptr;
  }

  AXFragmentRootWin* GetFragmentRootParentOf(
      gfx::NativeViewAccessible accessible) const {
    for (const auto& entry : map_) {
      AXPlatformNodeDelegate* child = entry.second->GetChildNodeDelegate();
      if (child && (child->GetNativeViewAccessible() == accessible))
        return entry.second;
    }
    return nullptr;
  }

 private:
  std::unordered_map<gfx::AcceleratedWidget,
                     raw_ptr<AXFragmentRootWin, CtnExperimental>>
      map_;
};

AXFragmentRootWin::AXFragmentRootWin(gfx::AcceleratedWidget widget,
                                     AXFragmentRootDelegateWin* delegate)
    : widget_(widget),
      delegate_(delegate),
      platform_node_(AXFragmentRootPlatformNodeWin::Create(*this)) {
  AXFragmentRootMapWin::GetInstance().AddFragmentRoot(widget, this);
}

AXFragmentRootWin::~AXFragmentRootWin() {
  AXFragmentRootMapWin::GetInstance().RemoveFragmentRoot(widget_);
}

AXFragmentRootWin* AXFragmentRootWin::GetForAcceleratedWidget(
    gfx::AcceleratedWidget widget) {
  return AXFragmentRootMapWin::GetInstance().GetFragmentRoot(widget);
}

// static
AXFragmentRootWin* AXFragmentRootWin::GetFragmentRootParentOf(
    gfx::NativeViewAccessible accessible) {
  return AXFragmentRootMapWin::GetInstance().GetFragmentRootParentOf(
      accessible);
}

gfx::NativeViewAccessible AXFragmentRootWin::GetNativeViewAccessible() {
  // The fragment root is the entry point from the operating system for UI
  // Automation. Signal when we're asked for a platform object on it.
  AXPlatform::GetInstance().OnPropertiesUsedInBrowserUI();
  return static_cast<AXFragmentRootPlatformNodeWin*>(platform_node_.get());
}

bool AXFragmentRootWin::IsControlElement() {
  return delegate_->IsAXFragmentRootAControlElement();
}

gfx::NativeViewAccessible AXFragmentRootWin::GetParent() const {
  return delegate_->GetParentOfAXFragmentRoot();
}

size_t AXFragmentRootWin::GetChildCount() const {
  return delegate_->GetChildOfAXFragmentRoot() ? 1 : 0;
}

gfx::NativeViewAccessible AXFragmentRootWin::ChildAtIndex(size_t index) const {
  if (index == 0) {
    return delegate_->GetChildOfAXFragmentRoot();
  }

  return nullptr;
}

gfx::NativeViewAccessible AXFragmentRootWin::GetNextSibling() const {
  size_t child_index = GetIndexInParentOfChild();
  AXPlatformNodeDelegate* parent = GetParentNodeDelegate();
  if (parent && (child_index + 1) < parent->GetChildCount())
    return GetParentNodeDelegate()->ChildAtIndex(child_index + 1);

  return nullptr;
}

gfx::NativeViewAccessible AXFragmentRootWin::GetPreviousSibling() const {
  size_t child_index = GetIndexInParentOfChild();
  if (child_index > 0)
    return GetParentNodeDelegate()->ChildAtIndex(child_index - 1);

  return nullptr;
}

gfx::NativeViewAccessible AXFragmentRootWin::HitTestSync(int x, int y) const {
  AXPlatformNodeDelegate* child_delegate = GetChildNodeDelegate();
  if (child_delegate)
    return child_delegate->HitTestSync(x, y);

  return nullptr;
}

gfx::NativeViewAccessible AXFragmentRootWin::GetFocus() const {
  AXPlatformNodeDelegate* child_delegate = GetChildNodeDelegate();
  if (child_delegate)
    return child_delegate->GetFocus();

  return nullptr;
}

AXPlatformNodeId AXFragmentRootWin::GetUniqueId() const {
  return unique_id_;
}

gfx::AcceleratedWidget
AXFragmentRootWin::GetTargetForNativeAccessibilityEvent() {
  return widget_;
}

AXPlatformNode* AXFragmentRootWin::GetFromTreeIDAndNodeID(
    const AXTreeID& ax_tree_id,
    int32_t node_id) {
  AXPlatformNodeDelegate* child_delegate = GetChildNodeDelegate();
  if (child_delegate)
    return child_delegate->GetFromTreeIDAndNodeID(ax_tree_id, node_id);

  return nullptr;
}

AXPlatformNodeDelegate* AXFragmentRootWin::GetParentNodeDelegate() const {
  gfx::NativeViewAccessible parent = delegate_->GetParentOfAXFragmentRoot();
  if (parent)
    return AXPlatformNode::FromNativeViewAccessible(parent)->GetDelegate();

  return nullptr;
}

AXPlatformNodeDelegate* AXFragmentRootWin::GetChildNodeDelegate() const {
  gfx::NativeViewAccessible child = delegate_->GetChildOfAXFragmentRoot();
  if (child)
    return AXPlatformNode::FromNativeViewAccessible(child)->GetDelegate();

  return nullptr;
}

void AXFragmentRootWin::OnEventListenerAdded(
    EVENTID event_id,
    base::span<const PROPERTYID> property_ids) {
  CHECK_LT(event_listener_count_[event_id], std::numeric_limits<int>::max());
  ++event_listener_count_[event_id];

  for (PROPERTYID property_id : property_ids) {
    CHECK_LT(property_listener_count_[property_id],
             std::numeric_limits<int>::max());
    ++property_listener_count_[property_id];
  }
}

void AXFragmentRootWin::OnEventListenerRemoved(
    EVENTID event_id,
    base::span<const PROPERTYID> property_ids) {
  auto event_it = event_listener_count_.find(event_id);
  if (event_it != event_listener_count_.end() && (--event_it->second) <= 0) {
    event_listener_count_.erase(event_it);
  }

  for (PROPERTYID property_id : property_ids) {
    auto prop_it = property_listener_count_.find(property_id);
    if (prop_it != property_listener_count_.end() && (--prop_it->second) <= 0) {
      property_listener_count_.erase(prop_it);
    }
  }
}

bool AXFragmentRootWin::HasEventListenerForEvent(EVENTID event_id) {
  return base::Contains(event_listener_count_, event_id);
}

bool AXFragmentRootWin::HasEventListenerForProperty(PROPERTYID property_id) {
  return base::Contains(property_listener_count_, property_id);
}

size_t AXFragmentRootWin::GetIndexInParentOfChild() const {
  AXPlatformNodeDelegate* parent = GetParentNodeDelegate();

  if (!parent)
    return 0;

  AXPlatformNodeDelegate* child = GetChildNodeDelegate();
  if (child) {
    size_t child_count = parent->GetChildCount();
    for (size_t child_index = 0; child_index < child_count; child_index++) {
      if (AXPlatformNode::FromNativeViewAccessible(
              parent->ChildAtIndex(child_index))
              ->GetDelegate() == child) {
        return child_index;
      }
    }
  }
  return 0;
}

}  // namespace ui
