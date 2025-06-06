// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/page_info/page_info_main_view.h"

#include <memory>
#include <optional>

#include "base/feature_list.h"
#include "base/i18n/message_formatter.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/lookalikes/safety_tip_ui_helper.h"
#include "chrome/browser/page_info/page_info_features.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/page_info/chrome_page_info_ui_delegate.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/views/accessibility/non_accessible_image_view.h"
#include "chrome/browser/ui/views/chrome_layout_provider.h"
#include "chrome/browser/ui/views/chrome_typography.h"
#include "chrome/browser/ui/views/controls/rich_hover_button.h"
#include "chrome/browser/ui/views/page_info/chosen_object_view.h"
#include "chrome/browser/ui/views/page_info/page_info_history_controller.h"
#include "chrome/browser/ui/views/page_info/page_info_navigation_handler.h"
#include "chrome/browser/ui/views/page_info/page_info_security_content_view.h"
#include "chrome/browser/ui/views/page_info/page_info_view_factory.h"
#include "chrome/browser/ui/views/page_info/permission_toggle_row_view.h"
#include "chrome/browser/ui/views/page_info/star_rating_view.h"
#include "chrome/browser/vr/vr_tab_helper.h"
#include "chrome/common/url_constants.h"
#include "components/content_settings/core/common/cookie_blocking_3pcd_status.h"
#include "components/page_info/core/about_this_site_service.h"
#include "components/page_info/core/features.h"
#include "components/page_info/page_info_ui_delegate.h"
#include "components/permissions/permission_util.h"
#include "components/privacy_sandbox/privacy_sandbox_features.h"
#include "components/privacy_sandbox/tracking_protection_settings.h"
#include "components/strings/grit/components_branded_strings.h"
#include "components/strings/grit/components_strings.h"
#include "components/strings/grit/privacy_sandbox_strings.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/ui_base_features.h"
#include "ui/compositor/layer.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/bubble/bubble_frame_view.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/separator.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/vector_icons.h"
#include "ui/views/view_class_properties.h"

#if BUILDFLAG(FULL_SAFE_BROWSING)
#include "chrome/browser/safe_browsing/chrome_password_protection_service.h"
#endif

namespace {

using privacy_sandbox::IsTrackingProtectionsUi;

constexpr int kMinPermissionRowHeight = 40;
constexpr float kMaxPermissionRowCount = 10.5;
constexpr int kContainerExtraRightMargin = 2;

int GetSeparatorPadding() {
  return ChromeLayoutProvider::Get()->GetDistanceMetric(
      DISTANCE_HORIZONTAL_SEPARATOR_PADDING_PAGE_INFO_VIEW);
}

}  // namespace

DEFINE_CLASS_ELEMENT_IDENTIFIER_VALUE(PageInfoMainView, kCookieButtonElementId);
DEFINE_CLASS_ELEMENT_IDENTIFIER_VALUE(PageInfoMainView,
                                      kPrivacyAndSiteDataButtonElementId);
DEFINE_CLASS_ELEMENT_IDENTIFIER_VALUE(PageInfoMainView, kMainLayoutElementId);
DEFINE_CLASS_ELEMENT_IDENTIFIER_VALUE(PageInfoMainView, kPermissionsElementId);
DEFINE_CLASS_ELEMENT_IDENTIFIER_VALUE(PageInfoMainView,
                                      kMerchantTrustElementId);

PageInfoMainView::ContainerView::ContainerView() {
  auto box_layout = std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical);
  box_layout->set_inside_border_insets(
      gfx::Insets::TLBR(0, 0, 0, kContainerExtraRightMargin));
  SetLayoutManager(std::move(box_layout));
}

void PageInfoMainView::ContainerView::Update() {
  PreferredSizeChanged();
}

BEGIN_METADATA(PageInfoMainView, ContainerView)
END_METADATA

PageInfoMainView::PageInfoMainView(
    PageInfo* presenter,
    ChromePageInfoUiDelegate* ui_delegate,
    PageInfoNavigationHandler* navigation_handler,
    PageInfoHistoryController* history_controller,
    base::OnceClosure initialized_callback,
    bool allow_extended_site_info)
    : presenter_(presenter),
      ui_delegate_(ui_delegate),
      navigation_handler_(navigation_handler) {
  ChromeLayoutProvider* layout_provider = ChromeLayoutProvider::Get();

  // In Harmony, the last view is a HoverButton, which overrides the bottom
  // dialog inset in favor of its own. Note the multi-button value is used here
  // assuming that the "Cookies" & "Site settings" buttons will always be shown.
  const int hover_list_spacing =
      layout_provider->GetDistanceMetric(DISTANCE_CONTENT_LIST_VERTICAL_MULTI);

  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(0, 0, hover_list_spacing, 0)));

  AddChildView(CreateBubbleHeaderView())
      ->SetProperty(views::kMarginsKey,
                    gfx::Insets::TLBR(0, 0, hover_list_spacing, 0));

#if BUILDFLAG(IS_WIN) && BUILDFLAG(ENABLE_VR)
  page_feature_info_view_ = AddChildView(std::make_unique<views::View>());
#endif

  security_container_view_ = AddChildView(CreateContainerView());

  permissions_view_ = AddChildView(std::make_unique<views::View>());
  permissions_view_->SetLayoutManager(std::make_unique<views::FlexLayout>())
      ->SetOrientation(views::LayoutOrientation::kVertical);

  SetProperty(views::kElementIdentifierKey, kMainLayoutElementId);

  site_settings_view_ = AddChildView(CreateContainerView());

  int link_text_id = 0;
  int tooltip_text_id = 0;
  if (ui_delegate_->ShouldShowSiteSettings(&link_text_id, &tooltip_text_id) &&
      !base::FeatureList::IsEnabled(page_info::kPageInfoHideSiteSettings)) {
    site_settings_link_ = AddChildView(std::make_unique<RichHoverButton>(
        base::BindRepeating(
            [](PageInfoMainView* view) {
              view->HandleMoreInfoRequest(view->site_settings_link_);
            },
            this),
        PageInfoViewFactory::GetSiteSettingsIcon(),
        /*title_text=*/l10n_util::GetStringUTF16(link_text_id),
        std::u16string(), PageInfoViewFactory::GetLaunchIcon()));
    site_settings_link_->SetID(
        PageInfoViewFactory::VIEW_ID_PAGE_INFO_LINK_OR_BUTTON_SITE_SETTINGS);
    site_settings_link_->SetProperty(
        views::kMarginsKey,
        gfx::Insets::TLBR(0, 0, 0, kContainerExtraRightMargin));
    site_settings_link_->SetTooltipText(
        l10n_util::GetStringUTF16(tooltip_text_id));
  }

  if (base::FeatureList::IsEnabled(page_info::kPageInfoHistoryDesktop)) {
    history_controller->InitRow(AddChildView(CreateContainerView()));
  }

  extended_site_info_section_ = AddChildView(CreateContainerView());
  extended_site_info_section_->AddChildView(
      PageInfoViewFactory::CreateSeparator(GetSeparatorPadding()));
  extended_site_info_section_->SetID(
      PageInfoViewFactory::VIEW_ID_PAGE_INFO_EXTENDED_SITE_INFO_SECTION);
  // Hide until at least one of the children buttons is visible.
  extended_site_info_section_->SetVisible(false);

  if (allow_extended_site_info &&
      page_info::IsAboutThisSiteFeatureEnabled(
          g_browser_process->GetApplicationLocale())) {
    about_this_site_section_ =
        extended_site_info_section_->AddChildView(CreateContainerView());
  }

  if (allow_extended_site_info && page_info::IsMerchantTrustFeatureEnabled()) {
    merchant_trust_section_ =
        extended_site_info_section_->AddChildView(CreateContainerView());
  }

  presenter_->InitializeUiState(this, std::move(initialized_callback));
}

PageInfoMainView::~PageInfoMainView() = default;

void PageInfoMainView::SetCookieInfo(const CookiesNewInfo& cookie_info) {
  // Ensure we don't add this button multiple times in error.
  if (cookie_button_ != nullptr) {
    return;
  }

  // If the TP UI is being shown then use the "Privacy and site data" treatment.
  if (IsTrackingProtectionsUi(cookie_info.controls_state)) {
    cookie_button_ =
        site_settings_view_->AddChildView(std::make_unique<RichHoverButton>(
            base::BindRepeating(
                &PageInfoNavigationHandler::OpenPrivacyAndSiteDataPage,
                base::Unretained(navigation_handler_)),
            PageInfoViewFactory::GetImageModel(views::kEyeCrossedRefreshIcon),
            l10n_util::GetStringUTF16(IDS_PAGE_INFO_PRIVACY_SITE_DATA_HEADER),
            /*subtitle_text=*/std::u16string(),
            PageInfoViewFactory::GetOpenSubpageIcon()));
    cookie_button_->SetTooltipText(
        l10n_util::GetStringUTF16(IDS_PAGE_INFO_PRIVACY_SITE_DATA_TOOLTIP));
    cookie_button_->SetID(
        PageInfoViewFactory::
            VIEW_ID_PAGE_INFO_LINK_OR_BUTTON_PRIVACY_SITE_DATA_SUBPAGE);
    cookie_button_->SetProperty(views::kElementIdentifierKey,
                                kPrivacyAndSiteDataButtonElementId);
  } else {
    cookie_button_ =
        site_settings_view_->AddChildView(std::make_unique<RichHoverButton>(
            base::BindRepeating(&PageInfoNavigationHandler::OpenCookiesPage,
                                base::Unretained(navigation_handler_)),
            PageInfoViewFactory::GetImageModel(
                vector_icons::kCookieChromeRefreshIcon),
            l10n_util::GetStringUTF16(IDS_PAGE_INFO_COOKIES_HEADER),
            /*subtitle_text=*/std::u16string(),
            PageInfoViewFactory::GetOpenSubpageIcon()));
    cookie_button_->SetTooltipText(
        l10n_util::GetStringUTF16(IDS_PAGE_INFO_COOKIES_TOOLTIP));
    cookie_button_->SetID(
        PageInfoViewFactory::VIEW_ID_PAGE_INFO_LINK_OR_BUTTON_COOKIES_SUBPAGE);
    cookie_button_->SetProperty(views::kElementIdentifierKey,
                                kCookieButtonElementId);
  }
  cookie_button_->SetTitleTextStyleAndColor(views::style::STYLE_BODY_3_MEDIUM,
                                            kColorPageInfoForeground);
  cookie_button_->SetSubtitleTextStyleAndColor(
      views::style::STYLE_BODY_4, kColorPageInfoSubtitleForeground);
}

void PageInfoMainView::SetPermissionInfo(
    const PermissionInfoList& permission_info_list,
    ChosenObjectInfoList chosen_object_info_list) {
  if (permission_info_list.empty() && chosen_object_info_list.empty()) {
    permissions_view_->RemoveAllChildViews();
    return;
  }

  // When LHS indicators are enabled, permissions usage in PageInfo should be
  // updated to reflect activity indicators.
  if (base::FeatureList::IsEnabled(
          content_settings::features::kLeftHandSideActivityIndicators)) {
    for (const auto& permission : permission_info_list) {
      auto it = syncable_permission_rows_.find(permission.type);
      if (it != syncable_permission_rows_.end()) {
        it->second->UpdatePermission(permission);
      }
    }
  }

  // This method is called when Page Info is constructed/displayed, then called
  // again whenever permissions/chosen objects change while the bubble is still
  // opened. Once Page Info is displaying a non-zero number of permissions, all
  // future calls to this will return early, based on the assumption that
  // permission rows won't need to be added or removed. Theoretically this
  // assumption is incorrect and it is actually possible that the number of
  // permission rows will need to change, but this should be an extremely rare
  // case that can be recovered from by closing & reopening the bubble.
  if (!permissions_view_->children().empty()) {
    UpdateResetButton(permission_info_list);
    return;
  }
  const int separator_padding = GetSeparatorPadding();
  permissions_view_->AddChildView(
      PageInfoViewFactory::CreateSeparator(GetSeparatorPadding()));

  auto* scroll_view =
      permissions_view_->AddChildView(std::make_unique<views::ScrollView>());
  scroll_view->ClipHeightTo(0,
                            kMinPermissionRowHeight * kMaxPermissionRowCount);
  scroll_view->SetDrawOverflowIndicator(false);
  auto* content_view =
      scroll_view->SetContents(std::make_unique<views::View>());
  content_view->SetLayoutManager(std::make_unique<views::FlexLayout>())
      ->SetOrientation(views::LayoutOrientation::kVertical);
  content_view->SetID(PageInfoViewFactory::VIEW_ID_PAGE_INFO_PERMISSION_VIEW);
  content_view->SetProperty(views::kElementIdentifierKey,
                            kPermissionsElementId);

  // If there is a permission that supports one time grants, offset all other
  // permissions to align toggles.
  bool should_show_spacer = false;
  for (const auto& permission : permission_info_list) {
    if (permissions::PermissionUtil::DoesSupportTemporaryGrants(
            permission.type)) {
      should_show_spacer = true;
    }
  }

  for (const auto& permission : permission_info_list) {
    PermissionToggleRowView* toggle_row =
        content_view->AddChildView(std::make_unique<PermissionToggleRowView>(
            ui_delegate_, navigation_handler_, permission, should_show_spacer));
    toggle_row->AddObserver(this);
    toggle_row->SetProperty(views::kCrossAxisAlignmentKey,
                            views::LayoutAlignment::kStretch);
    syncable_permission_rows_.emplace(permission.type, toggle_row);
    toggle_rows_.push_back(toggle_row);
  }

  for (auto& object : chosen_object_info_list) {
    // The view takes ownership of the object info.
    auto object_view = std::make_unique<ChosenObjectView>(
        std::move(object),
        presenter_->GetChooserContextFromUIInfo(*object->ui_info)
            ->GetObjectDisplayName(object->chooser_object->value));
    object_view->AddObserver(this);
    chosen_object_rows_.push_back(
        content_view->AddChildView(std::move(object_view)));
  }

  const int controls_spacing = ChromeLayoutProvider::Get()->GetDistanceMetric(
      views::DISTANCE_RELATED_CONTROL_VERTICAL);
  reset_button_ = content_view->AddChildView(
      std::make_unique<views::MdTextButton>(base::BindRepeating(
          [=](PageInfoMainView* view) {
            for (PermissionToggleRowView* toggle_row : view->toggle_rows_) {
              toggle_row->ResetPermission();
            }
            for (ChosenObjectView* object_row : view->chosen_object_rows_) {
              object_row->ResetPermission();
            }
            view->chosen_object_rows_.clear();
            view->PreferredSizeChanged();
            view->presenter_->RecordPageInfoAction(
                page_info::PAGE_INFO_PERMISSIONS_CLEARED);
          },
          base::Unretained(this))));
  reset_button_->SetProperty(views::kCrossAxisAlignmentKey,
                             views::LayoutAlignment::kStart);
  ChromeLayoutProvider* layout_provider = ChromeLayoutProvider::Get();
  // Offset the reset button by left button padding, icon size and distance
  // between icon and label to match text in the row above.
  const int side_offset =
      layout_provider
          ->GetInsetsMetric(ChromeInsetsMetric::INSETS_PAGE_INFO_HOVER_BUTTON)
          .left() +
      GetLayoutConstant(PAGE_INFO_ICON_SIZE) +
      layout_provider->GetDistanceMetric(
          views::DISTANCE_RELATED_LABEL_HORIZONTAL);
  reset_button_->SetProperty(
      views::kMarginsKey,
      gfx::Insets::TLBR(controls_spacing, side_offset, controls_spacing, 0));
  reset_button_->SetID(
      PageInfoViewFactory::VIEW_ID_PAGE_INFO_RESET_PERMISSIONS_BUTTON);

  // If a permission is in a non-default state or chooser object is present,
  // show reset button.
  reset_button_->SetVisible(false);
  UpdateResetButton(permission_info_list);
  permissions_view_->AddChildView(
      PageInfoViewFactory::CreateSeparator(separator_padding));

  PreferredSizeChanged();
}

void PageInfoMainView::UpdateResetButton(
    const PermissionInfoList& permission_info_list) {
  reset_button_->SetEnabled(false);
  int num_permissions = 0;
  for (const auto& permission : permission_info_list) {
    const bool is_permission_user_managed =
        permission.source == content_settings::SettingSource::kUser &&
        (ui_delegate_->ShouldShowAllow(permission.type) ||
         ui_delegate_->ShouldShowAsk(permission.type));
    if (is_permission_user_managed &&
        permission.setting != CONTENT_SETTING_DEFAULT) {
      reset_button_->SetEnabled(true);
      reset_button_->SetVisible(true);
    }
    num_permissions++;
  }
  for (ChosenObjectView* object_view : chosen_object_rows_) {
    if (object_view->GetVisible()) {
      reset_button_->SetEnabled(true);
      reset_button_->SetVisible(true);
      num_permissions++;
    }
  }
  reset_button_->SetText(l10n_util::GetPluralStringFUTF16(
      IDS_PAGE_INFO_RESET_PERMISSIONS, num_permissions));
}

void PageInfoMainView::SetIdentityInfo(const IdentityInfo& identity_info) {
  std::unique_ptr<PageInfoUI::SecurityDescription> security_description =
      GetSecurityDescription(identity_info);

  title_->SetText(presenter_->GetSubjectNameForDisplay());

  security_container_view_->RemoveAllChildViews();
  extended_site_info_section_->SetVisible(false);
  if (security_description->summary_style == SecuritySummaryColor::GREEN) {
    // base::Unretained(navigation_handler_) is safe because navigation_handler_
    // is the bubble view which is the owner of this view and therefore will
    // always exist when this view exists.
    connection_button_ = security_container_view_->AddChildViewRaw(
        std::make_unique<RichHoverButton>(
            base::BindRepeating(&PageInfoNavigationHandler::OpenSecurityPage,
                                base::Unretained(navigation_handler_)),
            PageInfoViewFactory::GetConnectionSecureIcon(),
            security_description->summary, std::u16string(),
            PageInfoViewFactory::GetOpenSubpageIcon())
            .release());
    connection_button_->SetID(
        PageInfoViewFactory::
            VIEW_ID_PAGE_INFO_LINK_OR_BUTTON_SECURITY_INFORMATION);
    connection_button_->SetTooltipText(
        l10n_util::GetStringUTF16(IDS_PAGE_INFO_SECURITY_SUBPAGE_BUTTON));
    connection_button_->SetTitleTextStyleAndColor(
        views::style::STYLE_BODY_3_MEDIUM, kColorPageInfoForeground);

    // Show "About this site" and "Merchant trust" sections only if connection
    // is secure, because security information has higher priority.
    if (about_this_site_section_) {
      auto info = ui_delegate_->GetAboutThisSiteInfo();
      if (info.has_value()) {
        about_this_site_section_->RemoveAllChildViews();
        about_this_site_section_->AddChildView(
            CreateAboutThisSiteButton(info.value()));
        extended_site_info_section_->SetVisible(true);
      }
    }

    // Fetch the data when the UI is enabled or if the control survey may be
    // shown.
    if (merchant_trust_section_ ||
        base::FeatureList::IsEnabled(
            page_info::kMerchantTrustEvaluationControlSurvey)) {
      ui_delegate_->GetMerchantTrustInfo(
          base::BindOnce(&PageInfoMainView::OnMerchantTrustDataFetched,
                         weak_factory_.GetWeakPtr()));
    }
  } else {
    security_content_view_ = security_container_view_->AddChildView(
        std::make_unique<PageInfoSecurityContentView>(
            presenter_, /*is_standalone_page=*/false));
    security_content_view_->SetIdentityInfo(identity_info);
  }

  details_text_ = security_description->details;
  PreferredSizeChanged();
}

void PageInfoMainView::SetPageFeatureInfo(const PageFeatureInfo& info) {
#if BUILDFLAG(IS_WIN) && BUILDFLAG(ENABLE_VR)
  // For now, this has only VR settings.
  if (!info.is_vr_presentation_in_headset) {
    return;
  }

  ChromeLayoutProvider* layout_provider = ChromeLayoutProvider::Get();
  page_feature_info_view_
      ->SetLayoutManager(std::make_unique<views::FlexLayout>())
      ->SetOrientation(views::LayoutOrientation::kVertical);
  auto* content_view =
      page_feature_info_view_->AddChildView(std::make_unique<views::View>());
  auto* flex_layout =
      content_view->SetLayoutManager(std::make_unique<views::FlexLayout>());

  auto icon = std::make_unique<NonAccessibleImageView>();
  icon->SetImage(
      PageInfoViewFactory::GetImageModel(vector_icons::kVrHeadsetIcon));
  content_view->AddChildView(std::move(icon));

  auto label = std::make_unique<views::Label>(
      l10n_util::GetStringUTF16(IDS_PAGE_INFO_VR_PRESENTING_TEXT),
      views::style::CONTEXT_DIALOG_BODY_TEXT, views::style::STYLE_PRIMARY);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  const int icon_label_spacing = layout_provider->GetDistanceMetric(
      views::DISTANCE_RELATED_LABEL_HORIZONTAL);
  label->SetProperty(views::kMarginsKey,
                     gfx::Insets::VH(0, icon_label_spacing));
  label->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded)
          .WithWeight(1));
  content_view->AddChildView(std::move(label));

  auto exit_button = std::make_unique<views::MdTextButton>(
      base::BindRepeating(
          [](PageInfoMainView* view) {
            view->GetWidget()->Close();
#if BUILDFLAG(ENABLE_VR)
            vr::VrTabHelper::ExitVrPresentation();
#endif
          },
          this),
      l10n_util::GetStringUTF16(IDS_PAGE_INFO_VR_TURN_OFF_BUTTON_TEXT));
  exit_button->SetID(PageInfoViewFactory::VIEW_ID_PAGE_INFO_BUTTON_END_VR);
  exit_button->SetStyle(ui::ButtonStyle::kProminent);
  // Set views::kInternalPaddingKey for flex layout to account for internal
  // button padding when calculating margins.
  exit_button->SetProperty(views::kInternalPaddingKey,
                           gfx::Insets::VH(exit_button->GetInsets().top(), 0));
  content_view->AddChildView(std::move(exit_button));

  flex_layout->SetInteriorMargin(layout_provider->GetInsetsMetric(
      ChromeInsetsMetric::INSETS_PAGE_INFO_HOVER_BUTTON));

  // Distance for multi content list is used, but split in half, since there is
  // a separator in the middle of it.
  const int separator_spacing =
      layout_provider->GetDistanceMetric(DISTANCE_CONTENT_LIST_VERTICAL_MULTI) /
      2;
  auto* separator = page_feature_info_view_->AddChildView(
      std::make_unique<views::Separator>());
  separator->SetProperty(views::kMarginsKey,
                         gfx::Insets::VH(separator_spacing, 0));

  PreferredSizeChanged();
#endif
}

void PageInfoMainView::SetAdPersonalizationInfo(
    const AdPersonalizationInfo& info) {
  ads_personalization_section_ =
      site_settings_view_->AddChildView(CreateContainerView());

  ads_personalization_section_->RemoveAllChildViews();

  if (info.is_empty()) {
    return;
  }

  ads_personalization_section_->AddChildView(CreateAdPersonalizationButton());

  PreferredSizeChanged();
}

void PageInfoMainView::OnPermissionChanged(
    const PageInfo::PermissionInfo& permission) {
  presenter_->OnSitePermissionChanged(permission.type, permission.setting,
                                      permission.requesting_origin,
                                      permission.is_one_time);
  // The menu buttons for the permissions might have longer strings now, so we
  // need to layout and size the whole bubble.
  PreferredSizeChanged();
}

void PageInfoMainView::OnChosenObjectDeleted(
    const PageInfoUI::ChosenObjectInfo& info) {
  presenter_->OnSiteChosenObjectDeleted(
      *info.ui_info, base::Value(info.chooser_object->value.Clone()));
  PreferredSizeChanged();
}

std::unique_ptr<views::View> PageInfoMainView::CreateContainerView() {
  return std::make_unique<ContainerView>();
}

void PageInfoMainView::HandleMoreInfoRequest(views::View* source) {
  // The bubble closes automatically when the collected cookies dialog or the
  // certificate viewer opens. So delay handling of the link clicked to avoid
  // a crash in the base class which needs to complete the mouse event handling.
  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE, base::BindOnce(&PageInfoMainView::HandleMoreInfoRequestAsync,
                                weak_factory_.GetWeakPtr(), source->GetID()));
}

void PageInfoMainView::HandleMoreInfoRequestAsync(int view_id) {
  switch (view_id) {
    case PageInfoViewFactory::VIEW_ID_PAGE_INFO_LINK_OR_BUTTON_SITE_SETTINGS:
      presenter_->OpenSiteSettingsView();
      break;
    default:
      NOTREACHED();
  }
}

void PageInfoMainView::OnMerchantTrustDataFetched(
    const GURL& url,
    std::optional<page_info::MerchantData> merchant_data) {
  if (!merchant_data.has_value()) {
    return;
  }

  ui_delegate_->RecordPageInfoWithMerchantTrustOpenTime();

  if (!merchant_trust_section_) {
    return;
  }
  merchant_trust_section_->RemoveAllChildViews();
  merchant_trust_section_->AddChildView(
      CreateMerchantTrustButton(merchant_data.value()));
  extended_site_info_section_->SetVisible(true);
  ui_delegate_->RecordMerchantTrustButtonShown();
}

gfx::Size PageInfoMainView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  if (site_settings_view_ == nullptr && permissions_view_ == nullptr &&
      security_container_view_ == nullptr) {
    return views::View::CalculatePreferredSize(available_size);
  }

  int width = 0;
  if (site_settings_view_) {
    width = std::max(width, site_settings_view_->GetPreferredSize().width());
  }

  if (permissions_view_) {
    width = std::max(width, permissions_view_->GetPreferredSize().width());
  }

  if (security_container_view_) {
    width =
        std::max(width, security_container_view_->GetPreferredSize().width());
  }
  return gfx::Size(width,
                   GetLayoutManager()->GetPreferredHeightForWidth(this, width));
}

void PageInfoMainView::ChildPreferredSizeChanged(views::View* child) {
  PreferredSizeChanged();
}

std::unique_ptr<views::View> PageInfoMainView::CreateBubbleHeaderView() {
  return views::Builder<views::FlexLayoutView>()
      .SetInteriorMargin(gfx::Insets::VH(0, 20))
      .AddChildren(
          views::Builder<views::Label>(
              std::make_unique<views::Label>(
                  std::u16string(), views::style::CONTEXT_DIALOG_TITLE,
                  views::style::STYLE_HEADLINE_4,
                  gfx::DirectionalityMode::DIRECTIONALITY_AS_URL))
              .CopyAddressTo(&title_)
              .SetMultiLine(true)
              .SetAllowCharacterBreak(true)
              .SetHorizontalAlignment(gfx::ALIGN_LEFT)
              .SetProperty(views::kFlexBehaviorKey,
                           views::FlexSpecification(
                               views::LayoutOrientation::kHorizontal,
                               views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded)
                               .WithWeight(1)),
          views::Builder<views::View>(
              views::BubbleFrameView::CreateCloseButton(
                  base::BindRepeating(&PageInfoNavigationHandler::CloseBubble,
                                      base::Unretained(navigation_handler_))))
              .SetID(PageInfoViewFactory::VIEW_ID_PAGE_INFO_CLOSE_BUTTON)
              .SetVisible(true)
              .SetProperty(views::kCrossAxisAlignmentKey,
                           views::LayoutAlignment::kStart)
              .CustomConfigure(base::BindOnce([](views::View* button) {
                // Set views::kInternalPaddingKey for flex layout to account for
                // internal button padding when calculating margins.
                button->SetProperty(views::kInternalPaddingKey,
                                    button->GetInsets());
              })))
      .Build();
}

std::unique_ptr<views::View> PageInfoMainView::CreateAboutThisSiteButton(
    const page_info::proto::SiteInfo& info) {
  const std::u16string title =
      l10n_util::GetStringUTF16(IDS_PAGE_INFO_ABOUT_THIS_PAGE_TITLE);
  const auto& description =
      info.has_description()
          ? base::UTF8ToUTF16(info.description().description())
          : l10n_util::GetStringUTF16(
                IDS_PAGE_INFO_ABOUT_THIS_PAGE_DESCRIPTION_PLACEHOLDER);

  auto about_this_site_button = std::make_unique<RichHoverButton>(
      base::BindRepeating(
          [](PageInfoMainView* view, GURL more_info_url, bool has_description,
             const ui::Event& event) {
            page_info::AboutThisSiteService::OnAboutThisSiteRowClicked(
                has_description);
            view->presenter_->RecordPageInfoAction(
                page_info::PAGE_INFO_ABOUT_THIS_SITE_PAGE_OPENED);
            view->ui_delegate_->OpenMoreAboutThisPageUrl(more_info_url, event);
            view->GetWidget()->Close();
          },
          this, GURL(info.more_about().url()), info.has_description()),
      PageInfoViewFactory::GetImageModel(
          PageInfoViewFactory::GetAboutThisSiteVectorIcon()),
      title, description, PageInfoViewFactory::GetLaunchIcon());
  about_this_site_button->SetID(
      PageInfoViewFactory::VIEW_ID_PAGE_INFO_ABOUT_THIS_SITE_BUTTON);
  about_this_site_button->SetSubtitleMultiline(false);
  about_this_site_button->SetTooltipText(
      l10n_util::GetStringUTF16(IDS_PAGE_INFO_ABOUT_THIS_PAGE_TOOLTIP));
  about_this_site_button->SetTitleTextStyleAndColor(
      views::style::STYLE_BODY_3_MEDIUM, kColorPageInfoForeground);
  about_this_site_button->SetSubtitleTextStyleAndColor(
      views::style::STYLE_BODY_4, kColorPageInfoSubtitleForeground);

  return about_this_site_button;
}

std::unique_ptr<views::View> PageInfoMainView::CreateAdPersonalizationButton() {
  auto ads_personalization_button = std::make_unique<RichHoverButton>(
      base::BindRepeating(&PageInfoNavigationHandler::OpenAdPersonalizationPage,
                          base::Unretained(navigation_handler_)),
      PageInfoViewFactory::GetImageModel(vector_icons::kAdsClickIcon),
      l10n_util::GetStringUTF16(IDS_PAGE_INFO_AD_PRIVACY_HEADER),
      std::u16string(), PageInfoViewFactory::GetOpenSubpageIcon());
  ads_personalization_button->SetID(
      PageInfoViewFactory::VIEW_ID_PAGE_INFO_AD_PERSONALIZATION_BUTTON);
  ads_personalization_button->SetTooltipText(
      l10n_util::GetStringUTF16(IDS_PAGE_INFO_AD_PRIVACY_TOOLTIP));

  ads_personalization_button->SetTitleTextStyleAndColor(
      views::style::STYLE_BODY_3_MEDIUM, kColorPageInfoForeground);
  ads_personalization_button->SetSubtitleTextStyleAndColor(
      views::style::STYLE_BODY_4, kColorPageInfoSubtitleForeground);

  return ads_personalization_button;
}

std::unique_ptr<views::View> PageInfoMainView::CreateMerchantTrustButton(
    page_info::MerchantData value) {
  auto merchant_trust_button =
      value.reviews_summary.empty()
          ? CreateMerchantTrustLaunchButton(value.page_url)
          : CreateMerchantTrustSubpageButton(value);

  merchant_trust_button->SetTitleTextStyleAndColor(
      views::style::STYLE_BODY_3_MEDIUM, kColorPageInfoForeground);
  merchant_trust_button->SetProperty(views::kElementIdentifierKey,
                                     kMerchantTrustElementId);

  auto* star_rating_view =
      merchant_trust_button->SetCustomView(std::make_unique<StarRatingView>());
  star_rating_view->SetRating(value.star_rating);
  merchant_trust_button->GetViewAccessibility().SetName(
      base::i18n::MessageFormatter::FormatWithNumberedArgs(
          l10n_util::GetStringUTF16(
              IDS_PAGE_INFO_MERCHANT_TRUST_STAR_RATING_A11Y_DESCRIPTION),
          value.star_rating));
  return merchant_trust_button;
}

std::unique_ptr<RichHoverButton>
PageInfoMainView::CreateMerchantTrustSubpageButton(
    page_info::MerchantData value) {
  auto button = std::make_unique<RichHoverButton>(
      base::BindRepeating(&PageInfoNavigationHandler::OpenMerchantTrustPage,
                          base::Unretained(navigation_handler_),
                          page_info::MerchantBubbleOpenReferrer::kPageInfo),
      PageInfoViewFactory::GetImageModel(vector_icons::kStorefrontIcon),
      l10n_util::GetStringUTF16(IDS_PAGE_INFO_MERCHANT_TRUST_HEADER),
      std::u16string(), PageInfoViewFactory::GetOpenSubpageIcon());

  return button;
}

std::unique_ptr<RichHoverButton>
PageInfoMainView::CreateMerchantTrustLaunchButton(GURL page_url) {
  auto button = std::make_unique<RichHoverButton>(
      base::BindRepeating(&PageInfoMainView::OpenMerchantTrustSidePanel,
                          weak_factory_.GetWeakPtr(), page_url),
      PageInfoViewFactory::GetImageModel(vector_icons::kStorefrontIcon),
      l10n_util::GetStringUTF16(IDS_PAGE_INFO_MERCHANT_TRUST_HEADER),
      std::u16string(), PageInfoViewFactory::GetLaunchIcon());
  return button;
}

void PageInfoMainView::OpenMerchantTrustSidePanel(const GURL& url) {
  ui_delegate_->OpenMerchantTrustSidePanel(url);
  ui_delegate_->RecordMerchantTrustSidePanelOpened();
}

BEGIN_METADATA(PageInfoMainView)
END_METADATA
