// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/performance_controls/memory_saver_chip_view.h"

#include <string>

#include "base/notreached.h"
#include "base/time/time.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/performance_controls/memory_saver_chip_tab_helper.h"
#include "chrome/browser/ui/performance_controls/memory_saver_utils.h"
#include "chrome/browser/ui/performance_controls/performance_controls_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/toolbar_button_provider.h"
#include "chrome/browser/ui/views/location_bar/icon_label_bubble_view.h"
#include "chrome/browser/ui/views/page_action/page_action_icon_view.h"
#include "chrome/browser/ui/views/performance_controls/memory_saver_bubble_view.h"
#include "chrome/grit/generated_resources.h"
#include "components/tabs/public/tab_interface.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/text/bytes_formatting.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view_class_properties.h"

namespace {

// The duration that the chip should be expanded for.
constexpr base::TimeDelta kChipAnimationDuration = base::Seconds(12);

}  // namespace

MemorySaverChipView::MemorySaverChipView(
    CommandUpdater* command_updater,
    Browser* browser,
    IconLabelBubbleView::Delegate* icon_label_bubble_delegate,
    PageActionIconView::Delegate* page_action_icon_delegate)
    : PageActionIconView(command_updater,
                         0,
                         icon_label_bubble_delegate,
                         page_action_icon_delegate,
                         "MemorySaver"),
      browser_(browser),
      chip_accessible_label_(
          l10n_util::GetStringUTF16(IDS_MEMORY_SAVER_CHIP_ACCNAME)) {
  DCHECK(browser_);

  SetUpForInOutAnimation(kChipAnimationDuration);
  SetBackgroundVisibility(BackgroundVisibility::kWithLabel);
  SetProperty(views::kElementIdentifierKey, kMemorySaverChipElementId);
}

MemorySaverChipView::~MemorySaverChipView() = default;

void MemorySaverChipView::OnBubbleShown() {
  PauseAnimation();
}

void MemorySaverChipView::OnBubbleHidden() {
  UnpauseAnimation();
  bubble_ = nullptr;
}

void MemorySaverChipView::UpdateImpl() {
  content::WebContents* const web_contents = GetWebContents();
  if (!web_contents) {
    return;
  }

  auto* const tab = tabs::TabInterface::GetFromContents(web_contents);
  if (!tab) {
    return;
  }
  MemorySaverChipTabHelper* const tab_helper =
      tab->GetTabFeatures()->memory_saver_chip_helper();
  CHECK(tab_helper);
  auto chip_state = tab_helper->chip_state();

  if (chip_state != memory_saver::ChipState::HIDDEN) {
    if (!tab_helper->ShouldChipAnimate()) {
      return;
    }

    switch (chip_state) {
      case memory_saver::ChipState::EXPANDED_EDUCATION: {
        SetVisible(true);
        AnimateIn(IDS_MEMORY_SAVER_CHIP_LABEL);
        RecordMemorySaverChipState(MemorySaverChipState::kExpandedEducation);
        break;
      }
      case memory_saver::ChipState::EXPANDED_WITH_SAVINGS: {
        SetVisible(true);
        int64_t const memory_savings =
            memory_saver::GetDiscardedMemorySavingsInBytes(web_contents);
        std::u16string memory_savings_string = ui::FormatBytes(memory_savings);
        SetLabel(l10n_util::GetStringFUTF16(IDS_MEMORY_SAVER_CHIP_SAVINGS_LABEL,
                                            {memory_savings_string}),
                 l10n_util::GetStringFUTF16(
                     IDS_MEMORY_SAVER_CHIP_WITH_SAVINGS_ACCNAME,
                     {memory_savings_string}));
        AnimateIn(std::nullopt);
        RecordMemorySaverChipState(MemorySaverChipState::kExpandedWithSavings);
        break;
      }
      case memory_saver::ChipState::COLLAPSED_FROM_EXPANDED: {
        SetVisible(true);
        UnpauseAnimation();
        AnimateOut();
        ResetSlideAnimation(false);
        break;
      }
      case memory_saver::ChipState::COLLAPSED: {
        SetVisible(true);
        GetViewAccessibility().SetName(chip_accessible_label_);
        RecordMemorySaverChipState(MemorySaverChipState::kCollapsed);
        break;
      }
      default: {
        NOTREACHED();
      }
    }
  } else {
    AnimateOut();
    ResetSlideAnimation(false);
    SetVisible(false);
  }
}

void MemorySaverChipView::OnExecuting(
    PageActionIconView::ExecuteSource execute_source) {
  // If the dialog bubble is currently open, close it.
  if (IsBubbleShowing()) {
    bubble_->Close();
    return;
  }

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);

  // Open the dialog bubble.
  // TODO(crbug.com/376283619): An action ID should be created and used here
  // when Memory Saver is migrated to the new page actions framework.
  View* anchor_view =
      browser_view->toolbar_button_provider()->GetAnchorView(std::nullopt);
  bubble_ = MemorySaverBubbleView::ShowBubble(browser_, anchor_view, this);
}

const gfx::VectorIcon& MemorySaverChipView::GetVectorIcon() const {
  return kPerformanceSpeedometerIcon;
}

views::BubbleDialogDelegate* MemorySaverChipView::GetBubble() const {
  return bubble_;
}

BEGIN_METADATA(MemorySaverChipView)
END_METADATA
