// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/media_router/media_router_gmc_ui_for_test.h"

#include "base/notimplemented.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/views/global_media_controls/cast_device_selector_view.h"
#include "chrome/browser/ui/views/global_media_controls/media_dialog_view.h"
#include "chrome/browser/ui/views/global_media_controls/media_toolbar_button_view.h"
#include "chrome/browser/ui/views/media_router/media_router_dialog_controller_views.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "components/global_media_controls/public/views/media_item_ui_updated_view.h"

namespace media_router {

MediaRouterGmcUiForTest::MediaRouterGmcUiForTest(
    content::WebContents* web_contents)
    : MediaRouterUiForTestBase(web_contents),
      browser_(chrome::FindBrowserWithTab(web_contents)) {
  DCHECK(browser_);
}

MediaRouterGmcUiForTest::~MediaRouterGmcUiForTest() {
  CHECK(!watch_callback_);
}

void MediaRouterGmcUiForTest::SetUp() {
}

void MediaRouterGmcUiForTest::ShowDialog() {
  CHECK(dialog_ui_.WaitForToolbarIconShown());
  dialog_ui_.ClickToolbarIcon();
  CHECK(dialog_ui_.WaitForDialogOpened());
}

bool MediaRouterGmcUiForTest::IsDialogShown() const {
  return MediaDialogView::IsShowing();
}

void MediaRouterGmcUiForTest::HideDialog() {
  return MediaDialogView::HideDialog();
}

void MediaRouterGmcUiForTest::ChooseSourceType(
    CastDialogView::SourceType source_type) {
  NOTIMPLEMENTED();
}

CastDialogView::SourceType MediaRouterGmcUiForTest::GetChosenSourceType()
    const {
  NOTIMPLEMENTED();
  return CastDialogView::SourceType();
}

void MediaRouterGmcUiForTest::StartCasting(const std::string& sink_name) {
  ClickOnButton(GetSinkButton(sink_name));
}

void MediaRouterGmcUiForTest::StopCasting(const std::string& sink_name) {
  // TODO(issuetracker.google.com/388289776): implement. This is not as simple
  // as just calling StartCasting() again, because for some reason the device
  // selector is nullptr once casting starts. Re-evaluate when spare dev cycles
  // are available.
  NOTIMPLEMENTED();
}

std::string MediaRouterGmcUiForTest::GetRouteIdForSink(
    const std::string& sink_name) const {
  NOTIMPLEMENTED();
  return "";
}

std::string MediaRouterGmcUiForTest::GetStatusTextForSink(
    const std::string& sink_name) const {
  NOTIMPLEMENTED();
  return "";
}

std::string MediaRouterGmcUiForTest::GetIssueTextForSink(
    const std::string& sink_name) const {
  NOTIMPLEMENTED();
  return "";
}

void MediaRouterGmcUiForTest::WaitForSink(const std::string& sink_name) {
  ObserveDialog(WatchType::kSink, sink_name);
}

void MediaRouterGmcUiForTest::WaitForSinkAvailable(
    const std::string& sink_name) {
  ObserveDialog(WatchType::kSinkAvailable, sink_name);
}

void MediaRouterGmcUiForTest::WaitForAnyIssue() {
  ObserveDialog(WatchType::kAnyIssue);
}

void MediaRouterGmcUiForTest::WaitForAnyRoute() {
  ObserveDialog(WatchType::kAnyRoute);
}

void MediaRouterGmcUiForTest::WaitForDialogShown() {
  CHECK(dialog_ui_.WaitForDialogOpened());
}

void MediaRouterGmcUiForTest::WaitForDialogHidden() {
  NOTIMPLEMENTED();
}

views::Button* MediaRouterGmcUiForTest::GetSinkButton(
    const std::string& sink_name) const {
  CHECK(IsDialogShown());

  // Get the device selector associated with the list of cast devices.
  auto& updated_items =
      MediaDialogView::GetDialogViewForTesting()->GetUpdatedItemsForTesting();
  CHECK_GE(updated_items.size(), 1u);
  global_media_controls::MediaItemUIUpdatedView* view =
      updated_items.begin()->second;
  CHECK(view);
  auto* device_selector =
      static_cast<CastDeviceSelectorView*>(view->GetDeviceSelectorForTesting());
  CHECK(device_selector);

  // Then get the device corresponding to `sink_name`.
  for (views::View* child :
       device_selector->GetDeviceContainerViewForTesting()->children()) {
    auto* device_button = static_cast<HoverButton*>(child);
    if (device_button->GetText() == base::UTF8ToUTF16(sink_name) ||
        (device_button->title() &&
         device_button->title()->GetText() == base::UTF8ToUTF16(sink_name))) {
      return device_button;
    }
  }
  return nullptr;
}

// TODO(issuetracker.google.com/388289776): Add support for other types of
// dialog events. Currently, we only support kDialogShown events -- other types
// of events are not currently supported, and may hang or just return
// immediately.
void MediaRouterGmcUiForTest::ObserveDialog(
    WatchType watch_type,
    std::optional<std::string> sink_name) {
  CHECK(!watch_sink_name_);
  CHECK(!watch_callback_);
  CHECK_EQ(watch_type_, WatchType::kNone);
  base::RunLoop run_loop;
  watch_sink_name_ = std::move(sink_name);
  watch_callback_ = run_loop.QuitClosure();
  watch_type_ = watch_type;
  std::move(*watch_callback_).Run();
  watch_callback_.reset();
  watch_sink_name_.reset();
  watch_type_ = WatchType::kNone;
  base::RunLoop().RunUntilIdle();
}

}  // namespace media_router
