// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/capture_mode/capture_mode_session_focus_cycler.h"

#include <algorithm>
#include <vector>

#include "ash/accessibility/magnifier/magnifier_utils.h"
#include "ash/accessibility/scoped_a11y_override_window_setter.h"
#include "ash/capture_mode/action_button_container_view.h"
#include "ash/capture_mode/capture_button_view.h"
#include "ash/capture_mode/capture_label_view.h"
#include "ash/capture_mode/capture_mode_bar_view.h"
#include "ash/capture_mode/capture_mode_camera_preview_view.h"
#include "ash/capture_mode/capture_mode_controller.h"
#include "ash/capture_mode/capture_mode_session.h"
#include "ash/capture_mode/capture_mode_settings_view.h"
#include "ash/capture_mode/capture_mode_source_view.h"
#include "ash/capture_mode/capture_mode_type_view.h"
#include "ash/capture_mode/capture_mode_types.h"
#include "ash/capture_mode/capture_mode_util.h"
#include "ash/capture_mode/recording_type_menu_view.h"
#include "ash/capture_mode/search_results_panel.h"
#include "ash/public/cpp/shell_window_ids.h"
#include "ash/shell.h"
#include "ash/strings/grit/ash_strings.h"
#include "ash/style/pill_button.h"
#include "ash/style/style_util.h"
#include "ash/style/tab_slider_button.h"
#include "ash/wm/mru_window_tracker.h"
#include "ash/wm/window_state.h"
#include "base/containers/flat_map.h"
#include "base/containers/flat_set.h"
#include "base/memory/raw_ptr.h"
#include "chromeos/ui/base/chromeos_ui_constants.h"
#include "ui/base/class_property.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/types/event_type.h"
#include "ui/views/accessibility/ax_virtual_view.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/link.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/wm/core/coordinate_conversion.h"

DEFINE_UI_CLASS_PROPERTY_TYPE(
    ash::CaptureModeSessionFocusCycler::HighlightHelper*)

namespace ash {

namespace {

DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(
    CaptureModeSessionFocusCycler::HighlightHelper,
    kCaptureModeHighlightHelper)

// The focusable items for the FocusGroup::kSelection group.
constexpr std::array<FineTunePosition, 9> kSelectionTabbingOrder = {
    FineTunePosition::kCenter,     FineTunePosition::kTopLeftVertex,
    FineTunePosition::kTopEdge,    FineTunePosition::kTopRightVertex,
    FineTunePosition::kRightEdge,  FineTunePosition::kBottomRightVertex,
    FineTunePosition::kBottomEdge, FineTunePosition::kBottomLeftVertex,
    FineTunePosition::kLeftEdge};

// The index of the smart actions button within the `FocusGroup::kActionButtons`
// group.
constexpr int kSmartActionsButtonIndex = 0;

// We inset the `window_of_interest` by `kWindowOfInterestInset` and outset any
// other window by `kIntersectingWindowOutset` we intersect with it, so that the
// resulting points of intersections are inside the bounds of
// `window_of_interest` and outside the bounds of the intersecting window.
// `chromeos::kResizeOutsideBoundsSize` is used while outsetting the window as
// it is the size of the window's resize border, and located events on it are
// still targeted to the window even though they're outside the window's bounds.
constexpr int kIntersectingWindowOutset =
    chromeos::kResizeOutsideBoundsSize + 1;
constexpr int kWindowOfInterestInset = 1;

// In addition to the default size of the spoken feedback orange highlight, add
// this amount when focusing the affordance circle with spoken feedback.
constexpr int kSpokenFeedbackAffordanceExtraOutset = 12;

std::vector<raw_ptr<aura::Window, VectorExperimental>>
GetWindowListIgnoreModalForActiveDesk() {
  return Shell::Get()->mru_window_tracker()->BuildWindowListIgnoreModal(
      DesksMruType::kActiveDesk);
}

views::Widget* GetCameraPreviewWidget() {
  return CaptureModeController::Get()
      ->camera_controller()
      ->camera_preview_widget();
}

CameraPreviewView* GetCameraPreviewView() {
  return CaptureModeController::Get()
      ->camera_controller()
      ->camera_preview_view();
}

// Returns true if the `value` is within the inclusive range of `low` and
// `high`.
bool InRange(int value, int low, int high) {
  return value >= low && value <= high;
}

// Returns a vector of intersection points of two bounds.
std::vector<gfx::Point> GetIntersectionPoints(const gfx::Rect& bounds_a,
                                              const gfx::Rect& bounds_b) {
  // Calculate the attributes for the `intersection`.
  const int intersection_x = std::max(bounds_a.x(), bounds_b.x());
  const int intersection_y = std::max(bounds_a.y(), bounds_b.y());
  const int intersection_right = std::min(bounds_a.right(), bounds_b.right());
  const int intersection_bottom =
      std::min(bounds_a.bottom(), bounds_b.bottom());
  const auto intersection = gfx::Rect(intersection_x, intersection_y,
                                      intersection_right - intersection_x,
                                      intersection_bottom - intersection_y);

  if (intersection.width() <= 0 || intersection.height() <= 0)
    return {};

  const std::vector<gfx::Point> candidate_points = {
      intersection.origin(), intersection.bottom_left(),
      intersection.bottom_right(), intersection.top_right()};

  // Iterate the corners of the `intersection` and check if the point falls on
  // the edge of the `intersection` and within the range of the edge.
  std::vector<gfx::Point> intersection_points;
  for (const auto& point : candidate_points) {
    if (point.x() == bounds_a.x() &&
        InRange(point.y(), bounds_a.y(), bounds_a.bottom())) {
      intersection_points.push_back(point);
    }
    if (point.x() == bounds_a.right() &&
        InRange(point.y(), bounds_a.y(), bounds_a.bottom())) {
      intersection_points.push_back(point);
    }
    if (point.y() == bounds_a.y() &&
        InRange(point.x(), bounds_a.x(), bounds_a.right())) {
      intersection_points.push_back(point);
    }
    if (point.y() == bounds_a.bottom() &&
        InRange(point.x(), bounds_a.x(), bounds_a.right())) {
      intersection_points.push_back(point);
    }
  }
  return intersection_points;
}

// Returns true if `window_of_interest` is fully occluded with no point on it
// selectable by hovering the mouse cursor over it, false otherwise. When
// calculating the intersection point, we always inset the bounds of
// `window_of_interest` and outset the bounds of other windows which the
// `window_of_interest` is being compared with to ensure that the intersection
// point falls within the bounds of `window_of_interest`.
bool IsWindowFullyOccluded(aura::Window* window_of_interest) {
  std::stack<gfx::Point> points_stack;
  // Create a set to track the points that have been calculated and inserted
  // into the `points_stack` so that they will not be populated again and cause
  // unnecessary infinite loop.
  base::flat_set<gfx::Point> visited_points;
  gfx::Rect window_of_interest_bounds = window_of_interest->GetBoundsInScreen();
  gfx::Rect insetted_win_of_interest_bounds = window_of_interest_bounds;
  insetted_win_of_interest_bounds.Inset(kWindowOfInterestInset);
  points_stack.push(insetted_win_of_interest_bounds.origin());
  points_stack.push(insetted_win_of_interest_bounds.top_right());
  points_stack.push(insetted_win_of_interest_bounds.bottom_left());
  points_stack.push(insetted_win_of_interest_bounds.bottom_right());

  // Create a map: the key is the intersection point, the value is the window
  // which the `window_of_interest` is being compared with to get the
  // intersection point.
  base::flat_map<gfx::Point, aura::Window*> point_to_intersecting_window_map;
  base::flat_set<aura::Window*> visited;
  // Create a map to track the windows that have been compared so that we won't
  // re-insert the intersection points of the two windows into the
  // `points_stack` again if it has been calculated and inserted before.
  base::flat_set<std::pair<aura::Window*, aura::Window*>>
      compared_window_pair_set;

  while (!points_stack.empty()) {
    const gfx::Point point = points_stack.top();
    points_stack.pop();
    auto* top_window =
        capture_mode_util::GetTopMostCapturableWindowAtPoint(point);
    if (top_window == nullptr)
      continue;
    if (top_window == window_of_interest)
      return false;

    auto outsetted_top_window_bounds = top_window->GetBoundsInScreen();
    outsetted_top_window_bounds.Inset(-kIntersectingWindowOutset);

    if (!visited.insert(top_window).second) {
      auto iter = point_to_intersecting_window_map.find(point);
      if (iter == point_to_intersecting_window_map.end())
        continue;
      // `top_window` has been visited before, so we have already intersected it
      // with `window_of_interest` and added the intersection points (if any) to
      // the `points_stack`. However, the current `point` may have resulted from
      // intersecting `window_of_interest` with a window other than the current
      // `top_window`. We need to intersect `top_window` and that other window
      // to get the intersection points that fall inside the bounds of
      // `window_of_interest` (if any) so that we can check those as well.
      auto* associated_window = iter->second;
      if (associated_window == top_window)
        continue;

      auto outsetted_associated_win_bounds =
          associated_window->GetBoundsInScreen();

      if (!compared_window_pair_set
               .insert(std::make_pair(associated_window, top_window))
               .second ||
          !compared_window_pair_set
               .insert(std::make_pair(top_window, associated_window))
               .second) {
        continue;
      }

      outsetted_associated_win_bounds.Inset(-kIntersectingWindowOutset);
      for (const auto& p : GetIntersectionPoints(
               outsetted_top_window_bounds, outsetted_associated_win_bounds)) {
        if (visited_points.insert(p).second &&
            window_of_interest_bounds.Contains(p)) {
          points_stack.push(p);
        }

        // We don't need to insert `p` into `point_to_intersecting_window_map`
        // here as `p` is not directly from the `window_of_interest`.
      }

      continue;
    }

    if (!compared_window_pair_set
             .insert(std::make_pair(window_of_interest, top_window))
             .second ||
        !compared_window_pair_set
             .insert(std::make_pair(top_window, window_of_interest))
             .second) {
      continue;
    }

    for (const auto& p : GetIntersectionPoints(
             outsetted_top_window_bounds, insetted_win_of_interest_bounds)) {
      DCHECK(window_of_interest_bounds.Contains(p));

      if (visited_points.insert(p).second) {
        point_to_intersecting_window_map.emplace(p, top_window);
        points_stack.push(p);
      }
    }
  }
  return true;
}

bool IsCaptureWindowSelectable(aura::Window* window) {
  if (WindowState::Get(window)->IsMinimized() || !window->IsVisible())
    return false;

  return !IsWindowFullyOccluded(window);
}

std::u16string GetA11yNameForFineTunePosition(
    FineTunePosition fine_tune_position) {
  int message_id;
  switch (fine_tune_position) {
    case FineTunePosition::kCenter:
      message_id = IDS_ASH_SCREEN_CAPTURE_SELECTED_AREA_ACCESSIBLE_NAME;
      break;
    case FineTunePosition::kTopLeftVertex:
      message_id = IDS_ASH_SCREEN_CAPTURE_DRAG_HANDLE_TOP_LEFT_ACCESSIBLE_NAME;
      break;
    case FineTunePosition::kTopRightVertex:
      message_id = IDS_ASH_SCREEN_CAPTURE_DRAG_HANDLE_TOP_RIGHT_ACCESSIBLE_NAME;
      break;
    case FineTunePosition::kBottomRightVertex:
      message_id =
          IDS_ASH_SCREEN_CAPTURE_DRAG_HANDLE_BOTTOM_RIGHT_ACCESSIBLE_NAME;
      break;
    case FineTunePosition::kBottomLeftVertex:
      message_id =
          IDS_ASH_SCREEN_CAPTURE_DRAG_HANDLE_BOTTOM_LEFT_ACCESSIBLE_NAME;
      break;
    case FineTunePosition::kTopEdge:
      message_id = IDS_ASH_SCREEN_CAPTURE_DRAG_HANDLE_TOP_EDGE_ACCESSIBLE_NAME;
      break;
    case FineTunePosition::kRightEdge:
      message_id =
          IDS_ASH_SCREEN_CAPTURE_DRAG_HANDLE_RIGHT_EDGE_ACCESSIBLE_NAME;
      break;
    case FineTunePosition::kBottomEdge:
      message_id =
          IDS_ASH_SCREEN_CAPTURE_DRAG_HANDLE_BOTTOM_EDGE_ACCESSIBLE_NAME;
      break;
    case FineTunePosition::kLeftEdge:
      message_id = IDS_ASH_SCREEN_CAPTURE_DRAG_HANDLE_LEFT_EDGE_ACCESSIBLE_NAME;
      break;
    case FineTunePosition::kNone:
      return u"";
  }

  return l10n_util::GetStringUTF16(message_id);
}

}  // namespace

// -----------------------------------------------------------------------------
// CaptureModeSessionFocusCycler::HighlightableView:

std::unique_ptr<views::HighlightPathGenerator>
CaptureModeSessionFocusCycler::HighlightableView::CreatePathGenerator() {
  return nullptr;
}

void CaptureModeSessionFocusCycler::HighlightableView::
    InvalidateFocusRingPath() {
  needs_highlight_path_ = true;
}

void CaptureModeSessionFocusCycler::HighlightableView::SetUpFocusPredicate() {
  // If the view has a preset focus ring, use it instead of creating a new
  // one.
  views::View* view = GetView();
  auto* preset_focus_ring = views::FocusRing::Get(view);
  focus_ring_ = preset_focus_ring ? preset_focus_ring
                                  : StyleUtil::SetUpFocusRingForView(view);

  // Use a custom focus predicate, as the default one checks if `view` actually
  // has focus, whereas we want to check for pseudo focus if a session is active
  // and direct focus (i.e., not clicked with the mouse) otherwise.
  focus_ring_->SetHasFocusPredicate(base::BindRepeating(
      [](const HighlightableView* highlightable, const views::View* view) {
        // If the session is active, we only need to check if the
        // `HighlightableView` has focus.
        const bool directly_focused =
            view->GetFocusManager()->focus_change_reason() ==
            views::FocusManager::FocusChangeReason::kDirectFocusChange;
        return CaptureModeController::Get()->IsActive()
                   ? (view->GetVisible() && highlightable->has_focus_)
                   : (view->GetVisible() && view->HasFocus() &&
                      !directly_focused);
      },
      base::Unretained(this)));
}

void CaptureModeSessionFocusCycler::HighlightableView::PseudoFocus() {
  has_focus_ = true;

  views::View* view = GetView();
  DCHECK(view);

  // This is lazy initialization of the FocusRing effectively. This is only used
  // for children of HighlightableView, so it will not replace any other style
  // of FocusRing.
  if (!focus_ring_) {
    SetUpFocusPredicate();
  }

  if (needs_highlight_path_) {
    if (auto path_generator = CreatePathGenerator()) {
      focus_ring_->SetPathGenerator(std::move(path_generator));
    }
    needs_highlight_path_ = false;
  }

  // Request actual focus for the textfield so users can interact with it and
  // make multimodal searches.
  if (auto* textfield = views::AsViewClass<views::Textfield>(view)) {
    textfield->RequestFocus();
  }

  focus_ring_->DeprecatedLayoutImmediately();
  focus_ring_->SchedulePaint();

  view->NotifyAccessibilityEventDeprecated(ax::mojom::Event::kSelection, true);

  magnifier_utils::MaybeUpdateActiveMagnifierFocus(
      view->GetBoundsInScreen().CenterPoint());
}

void CaptureModeSessionFocusCycler::HighlightableView::PseudoBlur() {
  has_focus_ = false;

  // If the view is focused, set the stored focus view to nullptr before
  // clearing it, so focus doesn't jump to another view.
  views::View* view = GetView();
  views::FocusManager* focus_manager = view->GetFocusManager();
  if (focus_manager && focus_manager->GetFocusedView() == view) {
    focus_manager->SetStoredFocusView(nullptr);
    focus_manager->ClearFocus();
  }

  if (!focus_ring_)
    return;

  focus_ring_->DeprecatedLayoutImmediately();
  focus_ring_->SchedulePaint();
}

bool CaptureModeSessionFocusCycler::HighlightableView::ClickView() {
  views::View* view = GetView();
  DCHECK(view);

  if (!views::IsViewClass<views::Button>(view) &&
      !views::IsViewClass<views::Link>(view)) {
    return false;
  }

  // Views such as the close button or the capture button may be destroyed
  // after `OnKeyPressed`, which will cause UAF. Use a `WeakPtr` to detect
  // this and skip `NotifyAccessibilityEvent` in this case.
  auto weak_ptr = weak_ptr_factory_.GetWeakPtr();

  bool handled = false;
  if (view->OnKeyPressed(ui::KeyEvent(ui::EventType::kKeyPressed,
                                      ui::VKEY_RETURN, ui::EF_NONE))) {
    handled = true;
    if (weak_ptr) {
      view->NotifyAccessibilityEventDeprecated(
          ax::mojom::Event::kStateChanged, true);
    }
  }

  return handled;
}

CaptureModeSessionFocusCycler::HighlightableView::HighlightableView() = default;

CaptureModeSessionFocusCycler::HighlightableView::~HighlightableView() =
    default;

// -----------------------------------------------------------------------------
// CaptureModeSessionFocusCycler::HighlightableWindow:

CaptureModeSessionFocusCycler::HighlightableWindow::HighlightableWindow(
    aura::Window* window,
    CaptureModeSession* session)
    : window_(window), session_(session) {
  DCHECK(window_);
  window_->AddObserver(this);
}

CaptureModeSessionFocusCycler::HighlightableWindow::~HighlightableWindow() {
  window_->RemoveObserver(this);
}

views::View* CaptureModeSessionFocusCycler::HighlightableWindow::GetView() {
  return nullptr;
}

void CaptureModeSessionFocusCycler::HighlightableWindow::PseudoFocus() {
  has_focus_ = true;

  DCHECK(window_);
  session_->HighlightWindowForTab(window_);

  // TODO(afakhry): Check with a11y team if we need to focus on a
  // different region of the window.
  magnifier_utils::MaybeUpdateActiveMagnifierFocus(
      window_->GetBoundsInScreen().origin());
}

void CaptureModeSessionFocusCycler::HighlightableWindow::PseudoBlur() {
  has_focus_ = false;
}

bool CaptureModeSessionFocusCycler::HighlightableWindow::ClickView() {
  // A HighlightableWindow is not clickable.
  return false;
}

void CaptureModeSessionFocusCycler::HighlightableWindow::OnWindowDestroying(
    aura::Window* window) {
  session_->focus_cycler_->highlightable_windows_.erase(window);
  // `this` will be deleted after the above operation.
}

// -----------------------------------------------------------------------------
// CaptureModeSessionFocusCycler::HighlightHelper:

CaptureModeSessionFocusCycler::HighlightHelper::HighlightHelper(
    views::View* view)
    : view_(view) {}

CaptureModeSessionFocusCycler::HighlightHelper::HighlightHelper(
    views::View* view,
    HighlightPathGeneratorFactory callback)
    : view_(view), highlight_path_generator_factory_(std::move(callback)) {}

CaptureModeSessionFocusCycler::HighlightHelper::~HighlightHelper() = default;

// static
void CaptureModeSessionFocusCycler::HighlightHelper::Install(
    views::View* view) {
  DCHECK(view);
  view->SetProperty(kCaptureModeHighlightHelper, new HighlightHelper(view));
}

// static
void CaptureModeSessionFocusCycler::HighlightHelper::Install(
    views::View* view,
    HighlightPathGeneratorFactory callback) {
  DCHECK(view);
  view->SetProperty(kCaptureModeHighlightHelper,
                    new HighlightHelper(view, std::move(callback)));
}

// static
CaptureModeSessionFocusCycler::HighlightHelper*
CaptureModeSessionFocusCycler::HighlightHelper::Get(views::View* view) {
  DCHECK(view);
  return view->GetProperty(kCaptureModeHighlightHelper);
}

views::View* CaptureModeSessionFocusCycler::HighlightHelper::GetView() {
  return view_;
}

std::unique_ptr<views::HighlightPathGenerator>
CaptureModeSessionFocusCycler::HighlightHelper::CreatePathGenerator() {
  return highlight_path_generator_factory_
             ? highlight_path_generator_factory_.Run()
             : nullptr;
}

// -----------------------------------------------------------------------------
// CaptureModeSessionFocusCycler:

CaptureModeSessionFocusCycler::CaptureModeSessionFocusCycler(
    CaptureModeSession* session)
    : groups_for_fullscreen_{FocusGroup::kNone, FocusGroup::kTypeSource,
                             FocusGroup::kCameraPreview,
                             FocusGroup::kSettingsMenu,
                             FocusGroup::kSettingsClose},
      groups_for_region_{
          FocusGroup::kNone,          FocusGroup::kSearchResultsPanel,
          FocusGroup::kTypeSource,    FocusGroup::kSelection,
          FocusGroup::kCameraPreview, FocusGroup::kCaptureButton,
          FocusGroup::kActionButtons, FocusGroup::kRecordingTypeMenu,
          FocusGroup::kSettingsMenu,  FocusGroup::kSettingsClose,
      },
      groups_for_window_{FocusGroup::kNone, FocusGroup::kTypeSource,
                         FocusGroup::kCaptureWindow, FocusGroup::kSettingsMenu,
                         FocusGroup::kSettingsClose},
      groups_for_game_capture_{
          FocusGroup::kNone, FocusGroup::kStartRecordingButton,
          FocusGroup::kCameraPreview, FocusGroup::kSettingsMenu,
          FocusGroup::kSettingsClose},
      groups_for_sunfish_{FocusGroup::kNone,
                          FocusGroup::kSearchResultsPanel,
                          FocusGroup::kSearchResultsPanelWebContents,
                          FocusGroup::kSelection,
                          FocusGroup::kActionButtons,
                          FocusGroup::kSettingsClose},
      session_(session),
      scoped_a11y_overrider_(
          std::make_unique<ScopedA11yOverrideWindowSetter>()) {
  for (aura::Window* window : GetWindowListIgnoreModalForActiveDesk()) {
    if (!IsCaptureWindowSelectable(window))
      continue;
    highlightable_windows_.emplace(
        window, std::make_unique<HighlightableWindow>(window, session_));
  }
}

CaptureModeSessionFocusCycler::~CaptureModeSessionFocusCycler() = default;

void CaptureModeSessionFocusCycler::AdvanceFocus(bool reverse) {
  // Advancing focus while either the settings or the recording type menus are
  // open will close the menu and clear focus, unless these menus were opened
  // using keyboard navigation.
  if (!menu_opened_with_keyboard_nav_) {
    if (auto* widget = GetSettingsMenuWidget(); widget && widget->IsVisible()) {
      session_->SetSettingsMenuShown(false);
      return;
    }

    if (auto* widget = GetRecordingTypeMenuWidget();
        widget && widget->IsVisible()) {
      session_->SetRecordingTypeMenuShown(false);
      return;
    }
  }

  ClearCurrentVisibleFocus();

  FocusGroup previous_focus_group = current_focus_group_;
  const size_t previous_group_size = GetGroupSize(previous_focus_group);
  const size_t previous_focus_index = focus_index_;

  // Go to the next group if the next index is out of bounds for the current
  // group. Otherwise, update |focus_index_| depending on |reverse|.
  if (!reverse && (previous_group_size == 0u ||
                   previous_focus_index >= previous_group_size - 1u)) {
    current_focus_group_ = GetNextGroup(/*reverse=*/false);
    focus_index_ = 0u;
  } else if (reverse && previous_focus_index == 0u) {
    current_focus_group_ = GetNextGroup(/*reverse=*/true);
    // The size of FocusGroup::kCaptureWindow could be empty.
    focus_index_ = std::max(
        static_cast<int32_t>(GetGroupSize(current_focus_group_)) - 1, 0);
  } else {
    focus_index_ = reverse ? focus_index_ - 1u : focus_index_ + 1u;
  }
  scoped_a11y_overrider_->MaybeUpdateA11yOverrideWindow(
      GetA11yOverrideWindow());

  const std::vector<HighlightableView*> current_views =
      GetGroupItems(current_focus_group_);
  // If `reverse`, focus the HighlightableWindow first before moving the focus
  // to items inside it.
  if (reverse)
    MaybeFocusHighlightableWindow(current_views);

  // Focus the new item. If it's the web view, then we want to request actual
  // focus so the user can properly use the keyboard like they would with a
  // regular search page.
  if (current_focus_group_ == FocusGroup::kSearchResultsPanelWebContents) {
    SearchResultsPanel* panel =
        CaptureModeController::Get()->GetSearchResultsPanel();
    if (panel) {
      // The web contents might not be available when we try to focus them, but
      // we can focus the loading animation instead.
      if (panel->search_results_view()) {
        views::View* web_view = panel->GetWebViewForFocus();
        CHECK(web_view);
        web_view->AboutToRequestFocusFromTabTraversal(reverse);
        web_view->RequestFocus();
      } else {
        focus_index_ = 0u;
        panel->GetHighlightableLoadingAnimation()->PseudoFocus();
      }
    }
  } else if (!current_views.empty()) {
    DCHECK_LT(focus_index_, current_views.size());
    current_views[focus_index_]->PseudoFocus();
  }

  // Selection focus is drawn directly on a layer owned by |session_|. Notify
  // the layer to repaint if necessary.
  const bool current_group_is_selection =
      current_focus_group_ == FocusGroup::kSelection;
  const bool redraw_layer = previous_focus_group == FocusGroup::kSelection ||
                            current_group_is_selection;

  if (redraw_layer)
    session_->RepaintRegion();

  if (current_group_is_selection) {
    const gfx::Rect user_region =
        CaptureModeController::Get()->user_capture_region();
    DCHECK(!user_region.IsEmpty());

    const auto fine_tune_position = GetFocusedFineTunePosition();
    DCHECK_NE(fine_tune_position, FineTunePosition::kNone);

    gfx::Point point_of_interest =
        fine_tune_position == FineTunePosition::kCenter
            ? user_region.CenterPoint()
            : capture_mode_util::GetLocationForFineTunePosition(
                  user_region, fine_tune_position);
    wm::ConvertPointToScreen(session_->current_root(), &point_of_interest);
    magnifier_utils::MaybeUpdateActiveMagnifierFocus(point_of_interest);

    // If the root has changed, remove the old `ax_widget_` so we can create it
    // and its virtual a11y views on the correct root.
    if (ax_widget_ && ax_widget_->GetNativeWindow()->GetRootWindow() !=
                          session_->current_root()) {
      ax_virtual_views_.clear();
      ax_widget_.reset();
    }

    // Lazily create the widget that hosts the virtual a11y views and the
    // virtual a11y root.
    if (!ax_widget_ &&
        Shell::Get()->accessibility_controller()->spoken_feedback().enabled()) {
      ax_widget_ = std::make_unique<views::Widget>();
      views::Widget::InitParams params(
          views::Widget::InitParams::CLIENT_OWNS_WIDGET,
          views::Widget::InitParams::TYPE_POPUP);
      // Using can maximize and a container with a fill layout means the widget
      // bounds will always match the root window bounds.
      auto delegate = std::make_unique<views::WidgetDelegate>();
      delegate->RegisterWindowClosingCallback(
          base::BindOnce(&CaptureModeSessionFocusCycler::OnAXWidgetClosing,
                         weak_ptr_factory_.GetWeakPtr()));
      delegate->SetCanMaximize(true);
      delegate->SetOwnedByWidget(views::WidgetDelegate::OwnedByWidgetPassKey());
      params.delegate = delegate.release();
      params.layer_type = ui::LAYER_NOT_DRAWN;
      params.parent = Shell::GetContainer(session_->current_root(),
                                          kShellWindowId_WallpaperContainer);
      ax_widget_->Init(std::move(params));
      ax_widget_->ShowInactive();

      auto ax_virtual_root = std::make_unique<views::AXVirtualView>();
      ax_virtual_root->SetRole(ax::mojom::Role::kGenericContainer);
      ax_widget_->GetRootView()->GetViewAccessibility().AddVirtualChildView(
          std::move(ax_virtual_root));
    }

    // Lazily create the virtual a11y view for the currently focused
    // `fine_tune_position`.
    if (ax_widget_ && !ax_virtual_views_.contains(fine_tune_position)) {
      auto ax_virtual_view = std::make_unique<views::AXVirtualView>();
      ax_virtual_views_[fine_tune_position] = ax_virtual_view.get();
      ax_virtual_view->SetRole(ax::mojom::Role::kButton);
      ax_virtual_view->SetName(
          GetA11yNameForFineTunePosition(fine_tune_position));
      const views::AXVirtualView::AXVirtualViews& ax_virtual_children =
          ax_widget_->GetRootView()->GetViewAccessibility().virtual_children();
      CHECK_EQ(ax_virtual_children.size(), 1u);
      ax_virtual_children[0]->AddChildView(std::move(ax_virtual_view));
    }
    OnFineTunePositionUpdated(/*notify_selection_event=*/true);

    return;
  }
}

void CaptureModeSessionFocusCycler::OnFineTunePositionUpdated(
    bool notify_selection_event) {
  const FineTunePosition fine_tune_position = GetFocusedFineTunePosition();
  if (!ax_virtual_views_.contains(fine_tune_position)) {
    return;
  }

  views::AXVirtualView* affordance_ax_virtual_view =
      ax_virtual_views_[fine_tune_position];
  if (!affordance_ax_virtual_view) {
    return;
  }
  CHECK(affordance_ax_virtual_view);

  const gfx::Rect user_region =
      CaptureModeController::Get()->user_capture_region();
  gfx::Rect a11y_rect_in_root =
      fine_tune_position == FineTunePosition::kCenter
          ? user_region
          : gfx::Rect(capture_mode_util::GetLocationForFineTunePosition(
                          user_region, fine_tune_position),
                      gfx::Size());
  a11y_rect_in_root.Outset(kSpokenFeedbackAffordanceExtraOutset);

  scoped_a11y_overrider_->MaybeUpdateA11yOverrideWindow(
      ax_widget_->GetNativeWindow());
  affordance_ax_virtual_view->SetBounds(gfx::RectF(a11y_rect_in_root));
  if (notify_selection_event) {
    affordance_ax_virtual_view->NotifyEvent(ax::mojom::Event::kSelection, true);
  }
}

void CaptureModeSessionFocusCycler::ClearFocus() {
  ClearCurrentVisibleFocus();

  if (current_focus_group_ == FocusGroup::kSelection)
    session_->RepaintRegion();

  current_focus_group_ = FocusGroup::kNone;
  focus_index_ = 0u;
}

bool CaptureModeSessionFocusCycler::HasFocus() const {
  return current_focus_group_ != FocusGroup::kNone;
}

bool CaptureModeSessionFocusCycler::MaybeActivateFocusedView(
    views::View* ignore_view) {
  if (current_focus_group_ == FocusGroup::kNone ||
      current_focus_group_ == FocusGroup::kSelection ||
      current_focus_group_ == FocusGroup::kPendingSettings ||
      current_focus_group_ == FocusGroup::kPendingRecordingType) {
    return false;
  }

  std::vector<HighlightableView*> views = GetGroupItems(current_focus_group_);
  if (views.empty())
    return false;

  // If current focused view doesn't exist, return directly.
  if (!FindFocusedViewAndUpdateFocusIndex(views))
    return false;

  DCHECK(!views.empty());
  DCHECK_LT(focus_index_, views.size());
  HighlightableView* view = views[focus_index_];

  auto* underlying_view = view->GetView();
  if (underlying_view && underlying_view == ignore_view) {
    return false;
  }

  // ClickView comes last as it will destroy |this| if |view| is the close
  // button.
  return view->ClickView();
}

bool CaptureModeSessionFocusCycler::RegionGroupFocused() const {
  return current_focus_group_ == FocusGroup::kSelection ||
         current_focus_group_ == FocusGroup::kCaptureButton;
}

bool CaptureModeSessionFocusCycler::CaptureBarFocused() const {
  return current_focus_group_ == FocusGroup::kTypeSource ||
         current_focus_group_ == FocusGroup::kStartRecordingButton ||
         current_focus_group_ == FocusGroup::kSettingsClose ||
         current_focus_group_ == FocusGroup::kPendingSettings;
}

bool CaptureModeSessionFocusCycler::CaptureLabelFocused() const {
  return current_focus_group_ == FocusGroup::kCaptureButton;
}

FineTunePosition CaptureModeSessionFocusCycler::GetFocusedFineTunePosition()
    const {
  if (current_focus_group_ != FocusGroup::kSelection)
    return FineTunePosition::kNone;
  return kSelectionTabbingOrder[focus_index_];
}

void CaptureModeSessionFocusCycler::OnCaptureLabelWidgetUpdated() {
  UpdateA11yAnnotation();
}

void CaptureModeSessionFocusCycler::OnMenuOpened(views::Widget* widget,
                                                 FocusGroup focus_group,
                                                 bool by_key_event) {
  session_widget_observeration_.AddObservation(widget);
  ClearCurrentVisibleFocus();
  current_focus_group_ = focus_group;
  menu_opened_with_keyboard_nav_ = by_key_event;
  focus_index_ = 0u;
  UpdateA11yAnnotation();
}

void CaptureModeSessionFocusCycler::OnSearchResultsPanelCreated(
    views::Widget* panel_widget) {
  session_widget_observeration_.AddObservation(panel_widget);
}

void CaptureModeSessionFocusCycler::OnScannerActionsFetched() {
  // Only focus the first suggested action if the smart actions button was
  // previously focused. This is since we don't need to move focus if the user
  // fetched Scanner actions with a mouse click instead of keyboard navigation,
  // or the user already navigated away from the action container.
  if (current_focus_group_ != FocusGroup::kActionButtons ||
      focus_index_ != kSmartActionsButtonIndex ||
      GetGroupSize(current_focus_group_) == 0) {
    return;
  }

  ClearCurrentVisibleFocus();
  // Focus on the first action button.
  focus_index_ = 0;
  GetGroupItems(current_focus_group_)[focus_index_]->PseudoFocus();
}

void CaptureModeSessionFocusCycler::OnDisclaimerWidgetOpened(
    views::Widget* disclaimer_widget) {
  // Only move focus if the focus cycler previously had focus, i.e. if the user
  // has spoken feedback enabled or opened the disclaimer using keyboard
  // navigation.
  if (HasFocus()) {
    scoped_a11y_overrider_->MaybeUpdateA11yOverrideWindow(
        disclaimer_widget->GetNativeView());
  }
}

void CaptureModeSessionFocusCycler::OnDisclaimerWidgetClosed() {
  // Try to restore focus to the previously focused view if there was one. If
  // there was no previously focused view (e.g. because the user opened the
  // disclaimer using mouse clicks), then there is no need to do anything.
  if (focus_index_ >= GetGroupSize(current_focus_group_)) {
    return;
  }

  scoped_a11y_overrider_->MaybeUpdateA11yOverrideWindow(
      GetA11yOverrideWindow());
  GetGroupItems(current_focus_group_)[focus_index_]->PseudoFocus();
}

void CaptureModeSessionFocusCycler::AdvanceFocusAfterSearchResultsPanel(
    bool reverse) {
  // Fully remove focus from the web view if available.
  SearchResultsPanel* panel =
      CaptureModeController::Get()->GetSearchResultsPanel();
  if (panel) {
    views::FocusManager* focus_manager = panel->GetFocusManager();
    focus_manager->SetStoredFocusView(nullptr);
    focus_manager->ClearFocus();
  }

  // Override the current focus parameters in case we did not naturally cycle
  // focus to the web view before.
  current_focus_group_ = FocusGroup::kSearchResultsPanelWebContents;
  focus_index_ = 0u;
  AdvanceFocus(reverse);
}

void CaptureModeSessionFocusCycler::
    SetA11yOverrideWindowToSearchResultsPanel() {
  auto* search_results_panel_widget =
      CaptureModeController::Get()->search_results_panel_widget();
  CHECK(search_results_panel_widget);
  scoped_a11y_overrider_->MaybeUpdateA11yOverrideWindow(
      search_results_panel_widget->GetNativeWindow());
}

void CaptureModeSessionFocusCycler::OnWidgetClosing(views::Widget* widget) {
  OnWidgetDestroying(widget);
}

void CaptureModeSessionFocusCycler::OnWidgetDestroying(views::Widget* widget) {
  // Note that we implement both `OnWidgetClosing()` and `OnWidgetDestroying()`.
  // - `OnWidgetClosing()` is called synchronously when either `Close()` or
  //   `CloseNow()` are called on the widget.
  // - `OnWidgetDestroying()` is called:
  //     - Synchronously if `CloseNow()` is used.
  //     - Asynchronously if `Close()` is used.
  // - However, `OnWidgetClosing()` may never get called at all if the native
  //   window of the widget gets deleted without calling either `Close()` or
  //   `CloseNow()`. See https://crbug.com/1350743.
  // Implementing both let's us handle the closing synchronously via
  // `OnWidgetClosing()`, and avoid any crashes or UAFs if it was never called.
  if (!session_widget_observeration_.IsObservingSource(widget)) {
    return;
  }

  menu_opened_with_keyboard_nav_ = false;
  session_widget_observeration_.RemoveObservation(widget);

  // Return immediately if the widget is closing by the closing of `session_`.
  if (session_->is_shutting_down())
    return;

  // Remove focus if one of the menu-related groups or the search results panel
  // are currently focused.
  bool should_update_focus = false;
  if (current_focus_group_ == FocusGroup::kPendingSettings ||
      current_focus_group_ == FocusGroup::kSettingsMenu ||
      current_focus_group_ == FocusGroup::kSearchResultsPanel) {
    // If the settings menu or or search results panel are closed while focus is
    // in or about to be in it, we manually put the focus back on the settings
    // button.
    current_focus_group_ = FocusGroup::kSettingsClose;
    focus_index_ = 0u;
    should_update_focus = true;
  } else if (current_focus_group_ == FocusGroup::kPendingRecordingType ||
             current_focus_group_ == FocusGroup::kRecordingTypeMenu) {
    // Similarly, if the recording type menu is closed while focus is in or
    // about to be in it, we manually focus the drop down button as long as it
    // still exists.
    auto* capture_label_view = session_->capture_label_view_.get();
    if (capture_label_view && capture_label_view->GetWidget()->IsVisible() &&
        capture_label_view->IsRecordingTypeDropDownButtonVisible()) {
      current_focus_group_ = FocusGroup::kCaptureButton;
      focus_index_ = 1u;
      should_update_focus = true;
    }
  }

  if (should_update_focus) {
    const auto highlightable_views = GetGroupItems(current_focus_group_);

    // In a Sunfish session, we update the focus back to the settings button,
    // and `FocusGroup::kSettingsClose` should only contain one highlightable
    // view.
    if (session_->active_behavior()->behavior_type() ==
        BehaviorType::kSunfish) {
      DCHECK_EQ(highlightable_views.size(), 1u);
    } else {
      DCHECK_EQ(highlightable_views.size(), 2u);
    }

    scoped_a11y_overrider_->MaybeUpdateA11yOverrideWindow(
        GetA11yOverrideWindow());
    highlightable_views[focus_index_]->PseudoFocus();
  }
  UpdateA11yAnnotation();
}

void CaptureModeSessionFocusCycler::ClearCurrentVisibleFocus() {
  // If the current focused group becomes unavailable for some reason (e.g. the
  // settings or the recording type menu gets closed), there's nothing to clear
  // the focus from.
  if (!IsGroupAvailable(current_focus_group_)) {
    return;
  }

  std::vector<HighlightableView*> views = GetGroupItems(current_focus_group_);
  if (views.empty())
    return;

  // If current focused view doesn't exist, return directly.
  if (!FindFocusedViewAndUpdateFocusIndex(views))
    return;

  DCHECK_LT(focus_index_, views.size());
  views[focus_index_]->PseudoBlur();
}

CaptureModeSessionFocusCycler::FocusGroup
CaptureModeSessionFocusCycler::GetNextGroup(bool reverse) const {
  if (current_focus_group_ == FocusGroup::kPendingSettings) {
    DCHECK(GetSettingsMenuWidget());
    return FocusGroup::kSettingsMenu;
  }

  if (current_focus_group_ == FocusGroup::kPendingRecordingType) {
    DCHECK(GetRecordingTypeMenuWidget());
    return FocusGroup::kRecordingTypeMenu;
  }

  const std::vector<FocusGroup>& groups_list = GetCurrentGroupList();
  const int increment = reverse ? -1 : 1;
  const auto iter = std::ranges::find(groups_list, current_focus_group_);
  DCHECK(iter != groups_list.end());
  size_t next_group_index = std::distance(groups_list.begin(), iter);
  const auto group_size = groups_list.size();
  do {
    next_group_index = (group_size + next_group_index + increment) % group_size;
  } while (!IsGroupAvailable(groups_list[next_group_index]));

  return groups_list[next_group_index];
}

const std::vector<CaptureModeSessionFocusCycler::FocusGroup>&
CaptureModeSessionFocusCycler::GetCurrentGroupList() const {
  // Behavior-specific focus groups.
  BehaviorType active_behavior = session_->active_behavior()->behavior_type();
  if (active_behavior == BehaviorType::kGameDashboard) {
    return groups_for_game_capture_;
  }
  if (active_behavior == BehaviorType::kSunfish) {
    return groups_for_sunfish_;
  }

  switch (session_->controller_->source()) {
    case CaptureModeSource::kFullscreen:
      return groups_for_fullscreen_;
    case CaptureModeSource::kRegion:
      return groups_for_region_;
    case CaptureModeSource::kWindow:
      return groups_for_window_;
  }
}

bool CaptureModeSessionFocusCycler::IsGroupAvailable(FocusGroup group) const {
  switch (group) {
    case FocusGroup::kNone:
    case FocusGroup::kSettingsClose:
    case FocusGroup::kPendingSettings:
    case FocusGroup::kPendingRecordingType:
      return true;
    case FocusGroup::kTypeSource: {
      CaptureModeBarView* bar_view = session_->capture_mode_bar_view_;
      return bar_view->GetCaptureTypeView() && bar_view->GetCaptureSourceView();
    }
    case FocusGroup::kStartRecordingButton:
      return session_->capture_mode_bar_view_->GetStartRecordingButton();
    case FocusGroup::kSelection: {
      return !CaptureModeController::Get()->user_capture_region().IsEmpty();
    }
    case FocusGroup::kCaptureButton: {
      // The capture button is focusable only when it is interactable, meaning
      // it has buttons that can be pressed. The capture label widget can be
      // hidden when it intersects with other capture UIs. In that case, we
      // shouldn't navigate to it via the keyboard.
      auto* capture_label_view = session_->capture_label_view_.get();
      return capture_label_view &&
             capture_label_view->GetWidget()->IsVisible() &&
             capture_label_view->IsViewInteractable();
    }
    case FocusGroup::kCaptureWindow:
      return session_->controller_->source() == CaptureModeSource::kWindow &&
             GetGroupSize(FocusGroup::kCaptureWindow) > 0;
    case FocusGroup::kSettingsMenu:
      return session_->capture_mode_settings_view_;
    case FocusGroup::kCameraPreview: {
      auto* camera_preview_widget = GetCameraPreviewWidget();
      return camera_preview_widget && camera_preview_widget->IsVisible();
    }
    case FocusGroup::kRecordingTypeMenu:
      return !!GetRecordingTypeMenuWidget();
    case FocusGroup::kActionButtons: {
      return session_->action_container_view_ &&
             !session_->action_container_view_->GetFocusableViews().empty();
    }
    case FocusGroup::kSearchResultsPanel: {
      return CaptureModeController::Get()->IsSearchResultsPanelVisible();
    }
    case FocusGroup::kSearchResultsPanelWebContents: {
      return CaptureModeController::Get()->IsSearchResultsPanelVisible();
    }
  }
}

size_t CaptureModeSessionFocusCycler::GetGroupSize(FocusGroup group) const {
  if (group == FocusGroup::kSelection)
    return 9u;
  return GetGroupItems(group).size();
}

std::vector<CaptureModeSessionFocusCycler::HighlightableView*>
CaptureModeSessionFocusCycler::GetGroupItems(FocusGroup group) const {
  std::vector<HighlightableView*> items;
  switch (group) {
    case FocusGroup::kNone:
    case FocusGroup::kSelection:
    case FocusGroup::kPendingSettings:
    case FocusGroup::kPendingRecordingType:
      break;
    case FocusGroup::kTypeSource: {
      CaptureModeBarView* bar_view = session_->capture_mode_bar_view_;
      CaptureModeTypeView* type_view = bar_view->GetCaptureTypeView();
      CaptureModeSourceView* source_view = bar_view->GetCaptureSourceView();
      CHECK(type_view && source_view);
      for (auto* button :
           {type_view->image_toggle_button(), type_view->video_toggle_button(),
            source_view->fullscreen_toggle_button(),
            source_view->region_toggle_button(),
            source_view->window_toggle_button()}) {
        if (button && button->GetEnabled()) {
          auto* highlight_helper = HighlightHelper::Get(button);
          DCHECK(highlight_helper);
          items.push_back(highlight_helper);
        }
      }
      break;
    }
    case FocusGroup::kStartRecordingButton: {
      auto* start_recording_button =
          session_->capture_mode_bar_view_->GetStartRecordingButton();
      CHECK(start_recording_button);
      auto* highlight_helper = HighlightHelper::Get(start_recording_button);
      CHECK(highlight_helper);
      items.push_back(highlight_helper);
      break;
    }
    case FocusGroup::kCaptureButton: {
      auto* capture_label_view = session_->capture_label_view_.get();
      DCHECK(capture_label_view);
      items = capture_label_view->capture_button_container()
                  ->GetHighlightableItems();
      break;
    }
    case FocusGroup::kCaptureWindow: {
      const std::vector<raw_ptr<aura::Window, VectorExperimental>> windows =
          GetWindowListIgnoreModalForActiveDesk();
      if (!windows.empty()) {
        const std::vector<HighlightableView*> camera_items =
            GetGroupItems(FocusGroup::kCameraPreview);
        for (aura::Window* window : windows) {
          auto iter = highlightable_windows_.find(window);
          if (iter != highlightable_windows_.end()) {
            items.push_back(iter->second.get());
            items.insert(items.end(), camera_items.begin(), camera_items.end());
          }
        }
      }
      break;
    }
    case FocusGroup::kSettingsClose: {
      CaptureModeBarView* bar_view = session_->capture_mode_bar_view_;
      for (auto* button :
           {bar_view->settings_button(), bar_view->close_button()}) {
        if (button && button->GetEnabled()) {
          auto* highlight_helper = HighlightHelper::Get(button);
          DCHECK(highlight_helper);
          items.push_back(highlight_helper);
        }
      }
      break;
    }
    case FocusGroup::kSettingsMenu: {
      CaptureModeSettingsView* settings_view =
          session_->capture_mode_settings_view_;
      DCHECK(settings_view);
      items = settings_view->GetHighlightableItems();
      break;
    }
    case FocusGroup::kCameraPreview: {
      auto* camera_preview_view = GetCameraPreviewView();
      if (camera_preview_view) {
        items.push_back(camera_preview_view);
        // The resize button is forced to be hidden if the camera preview is not
        // collapsible. Do not advance the focus to it in this case.
        if (camera_preview_view->is_collapsible())
          items.push_back(camera_preview_view->resize_button());
      }
      break;
    }
    case FocusGroup::kRecordingTypeMenu: {
      DCHECK(session_->recording_type_menu_view_);
      session_->recording_type_menu_view_->AppendHighlightableItems(items);
      break;
    }
    case FocusGroup::kActionButtons: {
      auto* action_container_view = session_->action_container_view_.get();
      if (action_container_view) {
        for (views::View* view : action_container_view->GetFocusableViews()) {
          auto* highlight_helper = HighlightHelper::Get(view);
          CHECK(highlight_helper);
          items.push_back(highlight_helper);
        }
      }
      break;
    }
    case FocusGroup::kSearchResultsPanel: {
      auto* controller = CaptureModeController::Get();
      auto* search_results_panel_widget =
          controller->search_results_panel_widget();
      if (search_results_panel_widget &&
          search_results_panel_widget->IsVisible()) {
        items = controller->GetSearchResultsPanel()->GetHighlightableItems();
      }
      break;
    }
    // The web contents are not highlightable, only the animation view. We'll
    // let `AdvanceFocus` handle the web contents separately.
    case FocusGroup::kSearchResultsPanelWebContents: {
      auto* controller = CaptureModeController::Get();
      auto* search_results_panel_widget =
          controller->search_results_panel_widget();
      if (search_results_panel_widget &&
          search_results_panel_widget->IsVisible()) {
        auto* animation_view = controller->GetSearchResultsPanel()
                                   ->GetHighlightableLoadingAnimation();
        if (animation_view) {
          items.push_back(animation_view);
        }
      }
    }
  }
  return items;
}

views::Widget* CaptureModeSessionFocusCycler::GetSettingsMenuWidget() const {
  return session_->capture_mode_settings_widget_.get();
}

views::Widget* CaptureModeSessionFocusCycler::GetRecordingTypeMenuWidget()
    const {
  return session_->recording_type_menu_widget_.get();
}

aura::Window* CaptureModeSessionFocusCycler::GetA11yOverrideWindow() const {
  switch (current_focus_group_) {
    case FocusGroup::kCaptureButton:
    case FocusGroup::kPendingRecordingType:
      return session_->capture_label_widget()->GetNativeWindow();
    case FocusGroup::kSettingsMenu:
      return session_->capture_mode_settings_widget()->GetNativeWindow();
    case FocusGroup::kNone:
    case FocusGroup::kTypeSource:
    case FocusGroup::kStartRecordingButton:
    case FocusGroup::kSelection:
    case FocusGroup::kCaptureWindow:
    case FocusGroup::kSettingsClose:
    case FocusGroup::kPendingSettings:
      return session_->GetCaptureModeBarWidget()->GetNativeWindow();
    case FocusGroup::kCameraPreview:
      return GetCameraPreviewWidget()->GetNativeWindow();
    case FocusGroup::kRecordingTypeMenu:
      return GetRecordingTypeMenuWidget()->GetNativeWindow();
    case FocusGroup::kActionButtons:
      return session_->action_container_widget()->GetNativeWindow();
    case FocusGroup::kSearchResultsPanel:
    case FocusGroup::kSearchResultsPanelWebContents:
      return CaptureModeController::Get()
          ->search_results_panel_widget()
          ->GetNativeWindow();
  }
}

bool CaptureModeSessionFocusCycler::FindFocusedViewAndUpdateFocusIndex(
    std::vector<HighlightableView*> views) {
  // No need to update `focus_index_` if the corresponding view is focused now.
  if (focus_index_ < views.size() && views[focus_index_]->has_focus())
    return true;

  const size_t current_focus_index =
      std::ranges::find(
          views, true,
          &CaptureModeSessionFocusCycler::HighlightableView::has_focus) -
      views.begin();

  // If current focused view doesn't exist, return false;
  if (current_focus_index == views.size()) {
    // If `focus_index_` is out of bound, update it to the last index of the
    // `views`.
    if (focus_index_ >= views.size())
      focus_index_ = views.size() - 1;
    return false;
  }

  // Update `focus_index_` to ensure it's up to date, since highlightable views
  // of `current_focus_group_` can be updated during keyboard navigation, for
  // example, the custom folder option can be added or removed via the select
  // folder menu item.
  focus_index_ = current_focus_index;
  return true;
}

void CaptureModeSessionFocusCycler::UpdateA11yAnnotation() {
  std::vector<views::Widget*> a11y_widgets;

  auto maybe_add_widget = [&a11y_widgets](views::Widget* widget) {
    if (widget && !widget->IsClosed()) {
      a11y_widgets.push_back(widget);
    }
  };

  // Add the search results panel if it exists.
  maybe_add_widget(CaptureModeController::Get()->search_results_panel_widget());

  // If the bar widget is not available, then this is called while shutting
  // down the capture mode session.
  maybe_add_widget(session_->capture_mode_bar_widget_.get());

  // Add the label widget only if the button is visible.
  if (auto* capture_label_view = session_->capture_label_view_.get();
      capture_label_view && capture_label_view->IsViewInteractable() &&
      capture_label_view->GetWidget()->IsVisible()) {
    maybe_add_widget(capture_label_view->GetWidget());
  }

  // Add the action container widget if it exists and it contains action
  // buttons.
  if (auto* action_container_widget = session_->action_container_widget_.get();
      action_container_widget && session_->action_container_view_ &&
      !session_->action_container_view_->children().empty()) {
    maybe_add_widget(action_container_widget);
  }

  // Add the recording type and settings widgets if they exist.
  maybe_add_widget(session_->recording_type_menu_widget_.get());
  maybe_add_widget(session_->capture_mode_settings_widget_.get());

  // Helper to update |target|'s a11y focus with |previous| and |next|, which
  // can be null.
  auto update_a11y_widget_focus =
      [](views::Widget* target, views::Widget* previous, views::Widget* next) {
        DCHECK(target);
        auto* contents_view = target->GetContentsView();
        auto& view_a11y = contents_view->GetViewAccessibility();
        view_a11y.SetPreviousFocus(previous);
        view_a11y.SetNextFocus(next);
        contents_view->NotifyAccessibilityEventDeprecated(
            ax::mojom::Event::kTreeChanged, true);
      };

  // If there is only one widget left, clear the focus overrides so that they
  // do not point to deleted objects.
  if (a11y_widgets.size() == 1u) {
    update_a11y_widget_focus(a11y_widgets[0], nullptr, nullptr);
    return;
  }

  const int size = a11y_widgets.size();
  for (int i = 0; i < size; ++i) {
    const int previous_index = (i + size - 1) % size;
    const int next_index = (i + 1) % size;
    update_a11y_widget_focus(a11y_widgets[i], a11y_widgets[previous_index],
                             a11y_widgets[next_index]);
  }
}

void CaptureModeSessionFocusCycler::MaybeFocusHighlightableWindow(
    const std::vector<HighlightableView*>& current_views) {
  if (current_focus_group_ != FocusGroup::kCaptureWindow)
    return;

  const std::vector<HighlightableView*> camera_preview_group_items =
      GetGroupItems(FocusGroup::kCameraPreview);
  if (camera_preview_group_items.empty())
    return;

  DCHECK(!current_views.empty());
  // Call HighlightableWindow::PseudoFocus() to highlight the window first
  // before moving the focus to the last focusable item inside the camera
  // preview. This will set the window as the current selected window, which
  // will move the camera preview inside it.
  if (current_views[focus_index_] == camera_preview_group_items.back()) {
    const size_t focusable_items_in_a_window =
        1 + camera_preview_group_items.size();
    const size_t window_index = (focus_index_ / focusable_items_in_a_window) *
                                focusable_items_in_a_window;
    current_views[window_index]->PseudoFocus();
  }
}

void CaptureModeSessionFocusCycler::OnAXWidgetClosing() {
  ax_virtual_views_.clear();
}

}  // namespace ash
