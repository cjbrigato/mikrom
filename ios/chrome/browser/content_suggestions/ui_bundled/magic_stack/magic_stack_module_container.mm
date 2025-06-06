// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/content_suggestions/ui_bundled/magic_stack/magic_stack_module_container.h"

#import "base/notreached.h"
#import "base/strings/sys_string_conversions.h"
#import "components/commerce/core/commerce_feature_list.h"
#import "google_apis/gaia/gaia_id.h"
#import "ios/chrome/browser/content_suggestions/ui_bundled/cells/content_suggestions_tile_layout_util.h"
#import "ios/chrome/browser/content_suggestions/ui_bundled/cells/most_visited_tiles_config.h"
#import "ios/chrome/browser/content_suggestions/ui_bundled/content_suggestions_collection_utils.h"
#import "ios/chrome/browser/content_suggestions/ui_bundled/content_suggestions_constants.h"
#import "ios/chrome/browser/content_suggestions/ui_bundled/magic_stack/magic_stack_constants.h"
#import "ios/chrome/browser/content_suggestions/ui_bundled/magic_stack/magic_stack_context_menu_interaction_handler.h"
#import "ios/chrome/browser/content_suggestions/ui_bundled/magic_stack/magic_stack_module.h"
#import "ios/chrome/browser/content_suggestions/ui_bundled/magic_stack/magic_stack_module_container_delegate.h"
#import "ios/chrome/browser/content_suggestions/ui_bundled/magic_stack/magic_stack_module_content_view_delegate.h"
#import "ios/chrome/browser/content_suggestions/ui_bundled/magic_stack/magic_stack_module_contents_factory.h"
#import "ios/chrome/browser/content_suggestions/ui_bundled/magic_stack/magic_stack_utils.h"
#import "ios/chrome/browser/content_suggestions/ui_bundled/safety_check/safety_check_state.h"
#import "ios/chrome/browser/content_suggestions/ui_bundled/safety_check/utils.h"
#import "ios/chrome/browser/content_suggestions/ui_bundled/shop_card/shop_card_data.h"
#import "ios/chrome/browser/content_suggestions/ui_bundled/shop_card/shop_card_item.h"
#import "ios/chrome/browser/content_suggestions/ui_bundled/tab_resumption/tab_resumption_item.h"
#import "ios/chrome/browser/push_notification/model/push_notification_client_id.h"
#import "ios/chrome/browser/push_notification/model/push_notification_settings_util.h"
#import "ios/chrome/browser/shared/public/features/features.h"
#import "ios/chrome/browser/shared/ui/symbols/symbols.h"
#import "ios/chrome/browser/shared/ui/util/rtl_geometry.h"
#import "ios/chrome/browser/shared/ui/util/uikit_ui_util.h"
#import "ios/chrome/common/ui/colors/semantic_color_names.h"
#import "ios/chrome/common/ui/util/constraints_ui_util.h"
#import "ios/chrome/common/ui/util/ui_util.h"
#import "ios/chrome/grit/ios_branded_strings.h"
#import "ios/chrome/grit/ios_strings.h"
#import "ui/base/l10n/l10n_util.h"
#import "ui/base/l10n/l10n_util_mac.h"
#import "url/gurl.h"

namespace {

// The horizontal inset for the content within this container.
const CGFloat kContentHorizontalInset = 20.0f;

// The top inset for the content within this container.
const CGFloat kContentTopInset = 16.0f;

// The bottom inset for the content within this container.
const CGFloat kContentBottomInset = 24.0f;
const CGFloat kReducedContentBottomInset = 10.0f;
const CGFloat kOversizedReducedContentBottomInset = 20.0f;

// Vertical spacing between the content views.
const CGFloat kContentVerticalSpacing = 16.0f;

// The corner radius of this container.
const float kCornerRadius = 24;

const CGFloat kSeparatorHeight = 0.5;

}  // namespace

@interface MagicStackModuleContainer () <MagicStackModuleContentViewDelegate>
@end

@implementation MagicStackModuleContainer {
  UILabel* _title;
  UILabel* _subtitle;
  BOOL _isPlaceholder;
  UIButton* _seeMoreButton;
  UIButton* _notificationsOptInButton;
  UIView* _contentView;
  UIView* _separator;
  UIStackView* _stackView;
  UIImageView* _placeholderImage;
  UIStackView* _titleStackView;
  MagicStackModuleContentsFactory* _magicStackModuleContentsFactory;
  NSLayoutConstraint* _containerHeightAnchor;
  NSLayoutConstraint* _contentStackViewBottomMarginAnchor;
  ContentSuggestionsModuleType _type;
  BOOL _reducedBottomMargin;
  MagicStackContextMenuInteractionHandler* _contextMenuInteractionHandler;
}

- (instancetype)initWithFrame:(CGRect)frame {
  self = [super initWithFrame:frame];
  if (self) {
    self.maximumContentSizeCategory = UIContentSizeCategoryAccessibilityMedium;
    _magicStackModuleContentsFactory =
        [[MagicStackModuleContentsFactory alloc] init];

    _titleStackView = [[UIStackView alloc] init];
    _titleStackView.alignment = UIStackViewAlignmentTop;
    _titleStackView.axis = UILayoutConstraintAxisHorizontal;
    _titleStackView.distribution = UIStackViewDistributionFill;
    // Resist Vertical expansion so all titles are the same height, allowing
    // content view to fill the rest of the module space.
    [_titleStackView setContentHuggingPriority:UILayoutPriorityRequired
                                       forAxis:UILayoutConstraintAxisVertical];

    _title = [[UILabel alloc] init];
    _title.font = PreferredFontForTextStyle(UIFontTextStyleFootnote,
                                            UIFontWeightSemibold);
    _title.adjustsFontForContentSizeCategory = YES;
    _title.textColor = [UIColor colorNamed:kTextPrimaryColor];
    _title.numberOfLines = 1;
    _title.lineBreakMode = NSLineBreakByTruncatingTail;
    _title.accessibilityTraits |= UIAccessibilityTraitHeader;
    [_title setContentHuggingPriority:UILayoutPriorityDefaultLow
                              forAxis:UILayoutConstraintAxisHorizontal];
    [_title
        setContentCompressionResistancePriority:UILayoutPriorityDefaultLow
                                        forAxis:
                                            UILayoutConstraintAxisHorizontal];
    [_title
        setContentCompressionResistancePriority:UILayoutPriorityRequired
                                        forAxis:UILayoutConstraintAxisVertical];
    [_titleStackView addArrangedSubview:_title];
    // `setContentHuggingPriority:` does not guarantee that _titleStackView
    // completely resists vertical expansion since UIStackViews do not have
    // intrinsic contentSize. Constraining the title label to the StackView will
    // ensure contentView expands.
    [NSLayoutConstraint activateConstraints:@[
      [_title.bottomAnchor constraintEqualToAnchor:_titleStackView.bottomAnchor]
    ]];

    _containerHeightAnchor = [self.heightAnchor
        constraintLessThanOrEqualToConstant:GetMagicStackHeight(_stackView)];

    _seeMoreButton = [self
        actionButton:l10n_util::GetNSString(IDS_IOS_MAGIC_STACK_SEE_MORE)];
    _seeMoreButton.hidden = YES;
    [_seeMoreButton addTarget:self
                       action:@selector(seeMoreButtonWasTapped:)
             forControlEvents:UIControlEventTouchUpInside];
    [_titleStackView addArrangedSubview:_seeMoreButton];

    _notificationsOptInButton =
        [self actionButton:l10n_util::GetNSString(
                               IDS_IOS_MAGIC_STACK_TURN_ON_NOTIFICATIONS)];
    _notificationsOptInButton.hidden = YES;
    [_notificationsOptInButton
               addTarget:self
                  action:@selector(notificationsOptInButtonWasTapped:)
        forControlEvents:UIControlEventTouchUpInside];
    [_titleStackView addArrangedSubview:_notificationsOptInButton];

    _subtitle = [[UILabel alloc] init];
    _subtitle.hidden = YES;
    _subtitle.font =
        PreferredFontForTextStyle(UIFontTextStyleFootnote, UIFontWeightRegular,
                                  kMaxTextSizeForStyleFootnote);
    _subtitle.adjustsFontForContentSizeCategory = YES;
    _subtitle.textColor = [UIColor colorNamed:kTextSecondaryColor];
    _subtitle.numberOfLines = 0;
    _subtitle.lineBreakMode = NSLineBreakByWordWrapping;
    _subtitle.accessibilityTraits |= UIAccessibilityTraitHeader;
    _subtitle.maximumContentSizeCategory = UIContentSizeCategoryMedium;
    [_subtitle setContentHuggingPriority:UILayoutPriorityRequired
                                 forAxis:UILayoutConstraintAxisHorizontal];
    [_subtitle
        setContentCompressionResistancePriority:UILayoutPriorityDefaultLow
                                        forAxis:
                                            UILayoutConstraintAxisHorizontal];
    _subtitle.textAlignment =
        UseRTLLayout() ? NSTextAlignmentLeft : NSTextAlignmentRight;
    [_titleStackView addArrangedSubview:_subtitle];

    _stackView = [[UIStackView alloc] init];
    _stackView.translatesAutoresizingMaskIntoConstraints = NO;
    _stackView.alignment = UIStackViewAlignmentFill;
    _stackView.axis = UILayoutConstraintAxisVertical;
    _stackView.spacing = kContentVerticalSpacing;
    _stackView.distribution = UIStackViewDistributionFill;

    [_stackView addArrangedSubview:_titleStackView];

    _separator = [[UIView alloc] init];
    [_separator setContentHuggingPriority:UILayoutPriorityDefaultHigh
                                  forAxis:UILayoutConstraintAxisVertical];
    _separator.backgroundColor = [UIColor colorNamed:kSeparatorColor];
    [_stackView addArrangedSubview:_separator];
    [NSLayoutConstraint activateConstraints:@[
      [_separator.heightAnchor
          constraintEqualToConstant:AlignValueToPixel(kSeparatorHeight)],
      [_separator.leadingAnchor
          constraintEqualToAnchor:_stackView.leadingAnchor],
      [_separator.trailingAnchor
          constraintEqualToAnchor:_stackView.trailingAnchor],
    ]];

    [self addSubview:_stackView];
    AddSameConstraintsToSidesWithInsets(
        _stackView, self,
        (LayoutSides::kTop | LayoutSides::kLeading | LayoutSides::kTrailing),
        NSDirectionalEdgeInsetsMake(kContentTopInset, kContentHorizontalInset,
                                    0, kContentHorizontalInset));
    _contentStackViewBottomMarginAnchor =
        [_stackView.bottomAnchor constraintEqualToAnchor:self.bottomAnchor
                                                constant:-kContentBottomInset];
    [NSLayoutConstraint
        activateConstraints:@[ _contentStackViewBottomMarginAnchor ]];

    if (@available(iOS 17, *)) {
      NSArray<UITrait>* traits = TraitCollectionSetForTraits(
          @[ UITraitPreferredContentSizeCategory.class ]);
      [self registerForTraitChanges:traits
                         withAction:@selector(updateCardSizing)];
    }
  }
  return self;
}

- (void)dealloc {
  [self resetView];
}

#pragma mark - Setters

- (void)setDelegate:(id<MagicStackModuleContainerDelegate>)delegate {
  _delegate = delegate;
  [self contextMenuInteractionHandler].delegate = delegate;
}

// Creates a button with the specified `title` positioned in the module's
// top-right corner.
//
// NOTE: This helper method does not associate an action with the generated
// button. Use `-addTarget:action:forControlEvents:` to attach an action.
- (UIButton*)actionButton:(NSString*)title {
  UIButton* button = [[UIButton alloc] init];

  UIButtonConfiguration* buttonConfiguration =
      [UIButtonConfiguration plainButtonConfiguration];

  buttonConfiguration.contentInsets = NSDirectionalEdgeInsetsZero;
  buttonConfiguration.titleLineBreakMode = NSLineBreakByWordWrapping;
  buttonConfiguration.attributedTitle = [[NSAttributedString alloc]
      initWithString:title
          attributes:@{
            NSFontAttributeName :
                [UIFont preferredFontForTextStyle:UIFontTextStyleFootnote]
          }];
  button.configuration = buttonConfiguration;

  [button setTitleColor:[UIColor colorNamed:kBlueColor]
               forState:UIControlStateNormal];
  button.titleLabel.numberOfLines = 2;
  button.titleLabel.adjustsFontForContentSizeCategory = YES;

  button.contentHorizontalAlignment =
      UIControlContentHorizontalAlignmentTrailing;
  [button
      setContentCompressionResistancePriority:UILayoutPriorityRequired
                                      forAxis:UILayoutConstraintAxisHorizontal];

  [button setContentHuggingPriority:UILayoutPriorityDefaultHigh
                            forAxis:UILayoutConstraintAxisHorizontal];
  button.pointerInteractionEnabled = YES;
  button.accessibilityIdentifier = button.titleLabel.text;

  return button;
}

- (void)configureWithConfig:(MagicStackModule*)config {
  [self resetView];
  // Ensures that the modules conforms to the dynamic MS height. For
  // the MVT when it lives outside of the Magic Stack to stay as close to its
  // intrinsic size as possible, the constraint is configured to be less than
  // or equal to.
  if (config.type == ContentSuggestionsModuleType::kMostVisited) {
    self.backgroundColor = [UIColor colorNamed:kBackgroundColor];
    self.layer.cornerRadius = kCornerRadius;
    self.clipsToBounds = YES;
    _containerHeightAnchor.active = NO;
    [NSLayoutConstraint activateConstraints:@[ _containerHeightAnchor ]];
  }

  if (config.type == ContentSuggestionsModuleType::kPlaceholder) {
    _isPlaceholder = YES;
    _placeholderImage = [[UIImageView alloc]
        initWithImage:[UIImage imageNamed:@"magic_stack_placeholder_module"]];
    _placeholderImage.translatesAutoresizingMaskIntoConstraints = NO;
    [self addSubview:_placeholderImage];
    AddSameConstraints(_placeholderImage, self);
    [self bringSubviewToFront:_placeholderImage];
    _separator.hidden = YES;
    return;
  }
  _type = config.type;
  [[self contextMenuInteractionHandler] configureWithType:_type config:config];

  _title.text = [MagicStackModuleContainer titleStringForModule:_type
                                                         config:config];
  _title.accessibilityIdentifier =
      [MagicStackModuleContainer accessibilityIdentifierForModule:_type
                                                           config:config];

  _seeMoreButton.hidden = !config.shouldShowSeeMore;
  [self setCustomAccessibilityLabelForSeeMoreButton:_type config:config];

  // The notifications opt-in button is hidden if either the "See More"
  // button or the module's subtitle is displayed, or if the option is disabled
  // in the configuration. This ensures the user focuses on the primary
  // elements (See More/subtitle) before being presented with the opt-in.
  _notificationsOptInButton.hidden = !_seeMoreButton.isHidden ||
                                     !_subtitle.isHidden ||
                                     !config.showNotificationsOptIn;

  if ([self shouldShowSubtitle]) {
    // TODO(crbug.com/40279482): Update MagicStackModuleContainer to take an id
    // config in its initializer so the container can build itself from a
    // passed config/state object.
    NSString* subtitle = [self subtitleStringForConfig:config];
    _subtitle.text = subtitle;
    _subtitle.accessibilityIdentifier = subtitle;
    _subtitle.hidden = NO;
  }

  if ([_title.text length] == 0) {
    _titleStackView.hidden = YES;
  }

  _separator.hidden = ![self shouldShowSeparator];

  _contentView = [_magicStackModuleContentsFactory
      contentViewForConfig:config
           traitCollection:self.traitCollection
       contentViewDelegate:self];
  [_stackView addArrangedSubview:_contentView];

  // Configures `contentView` to be the view willing to expand if needed to
  // fill extra vertical space in the container.
  [_contentView
      setContentCompressionResistancePriority:UILayoutPriorityDefaultLow
                                      forAxis:UILayoutConstraintAxisVertical];

  [self updateBottomContentMarginsForConfig:config];

  NSMutableArray* accessibilityElements =
      [[NSMutableArray alloc] initWithObjects:_title, nil];
  if (config.shouldShowSeeMore) {
    [accessibilityElements addObject:_seeMoreButton];
  } else if (config.showNotificationsOptIn) {
    [accessibilityElements addObject:_notificationsOptInButton];
  } else if ([self shouldShowSubtitle]) {
    [accessibilityElements addObject:_subtitle];
  }
  [accessibilityElements addObject:_contentView];
  self.accessibilityElements = accessibilityElements;
}

- (void)resetView {
  _title.text = nil;
  _titleStackView.hidden = NO;
  _subtitle.text = nil;
  _isPlaceholder = NO;
  if (_placeholderImage) {
    [_placeholderImage removeFromSuperview];
    _placeholderImage = nil;
  }
  if (_contentView) {
    [_contentView removeFromSuperview];
    _contentView = nil;
  }
}

- (MagicStackContextMenuInteractionHandler*)contextMenuInteractionHandler {
  if (!_contextMenuInteractionHandler) {
    _contextMenuInteractionHandler =
        [[MagicStackContextMenuInteractionHandler alloc] init];
    _contextMenuInteractionHandler.delegate = self.delegate;
  }
  return _contextMenuInteractionHandler;
}

// Returns the module's title, if any, given the Magic Stack module `type`.
+ (NSString*)titleStringForModule:(ContentSuggestionsModuleType)type
                           config:(MagicStackModule*)config {
  switch (type) {
    case ContentSuggestionsModuleType::kShortcuts:
      return l10n_util::GetNSString(
          IDS_IOS_CONTENT_SUGGESTIONS_SHORTCUTS_MODULE_TITLE);
    case ContentSuggestionsModuleType::kMostVisited:
      return @"";
    case ContentSuggestionsModuleType::kTabResumption: {
      TabResumptionItem* tabResumptionItem =
          static_cast<TabResumptionItem*>(config);
      // Arm 4 of ShopCard is an alternative to Tab Resumption,
      // triggered by Tab Resumption where the user is given the
      // option to price track a URL for a price trackable URL.
      if (tabResumptionItem.shopCardData &&
          tabResumptionItem.shopCardData.shopCardItemType ==
              ShopCardItemType::kPriceTrackableProductOnTab) {
        return l10n_util::GetNSString(
            IDS_IOS_CONTENT_SUGGESTIONS_SHOPCARD_TRACK_PRICE_ALT_TITLE_2);
      }
      return l10n_util::GetNSString(IDS_IOS_TAB_RESUMPTION_TITLE);
    }
    case ContentSuggestionsModuleType::kSetUpListSync:
    case ContentSuggestionsModuleType::kSetUpListDefaultBrowser:
    case ContentSuggestionsModuleType::kSetUpListAutofill:
    case ContentSuggestionsModuleType::kCompactedSetUpList:
    case ContentSuggestionsModuleType::kSetUpListAllSet:
    case ContentSuggestionsModuleType::kSetUpListNotifications:
      return content_suggestions::SetUpListTitleString();
    case ContentSuggestionsModuleType::kSafetyCheck:
      return l10n_util::GetNSString(IDS_IOS_SAFETY_CHECK_TITLE);
    case ContentSuggestionsModuleType::kParcelTracking:
      return l10n_util::GetNSString(
          IDS_IOS_CONTENT_SUGGESTIONS_PARCEL_TRACKING_MODULE_TITLE);
    case ContentSuggestionsModuleType::kPriceTrackingPromo:
    case ContentSuggestionsModuleType::kSendTabPromo:
      // Send Tab and Price Tracking Promo design do not use title.
      return @"";
    case ContentSuggestionsModuleType::kShopCard: {
      ShopCardItem* shopCardItem = static_cast<ShopCardItem*>(config);
      if (shopCardItem.shopCardData.shopCardItemType ==
          ShopCardItemType::kPriceDropForTrackedProducts) {
        return l10n_util::GetNSString(
            IDS_IOS_CONTENT_SUGGESTIONS_SHOPCARD_PRICE_TRACKING_TITLE);
      } else {
        return l10n_util::GetNSString(
            IDS_IOS_CONTENT_SUGGESTIONS_SHOPCARD_REVIEWS_ALT_TITLE);
      }
    }
    case ContentSuggestionsModuleType::kTipsWithProductImage:
    case ContentSuggestionsModuleType::kTips:
      return l10n_util::GetNSString(IDS_IOS_MAGIC_STACK_TIP_TITLE);
    default:
      NOTREACHED();
  }
}

// Returns the accessibility identifier given the Magic Stack module `type`.
+ (NSString*)accessibilityIdentifierForModule:(ContentSuggestionsModuleType)type
                                       config:(MagicStackModule*)config {
  switch (type) {
    case ContentSuggestionsModuleType::kTabResumption:
      return kMagicStackContentSuggestionsModuleTabResumptionAccessibilityIdentifier;

    default:
      // TODO(crbug.com/40946679): the code should use constants for
      // accessibility identifiers, and not localized strings.
      return [self titleStringForModule:type
                                 config:config];
  }
}

- (void)setCustomAccessibilityLabelForSeeMoreButton:
            (ContentSuggestionsModuleType)type
                                             config:(MagicStackModule*)config {
  switch (type) {
    case ContentSuggestionsModuleType::kShopCard: {
      if (commerce::kShopCardVariation.Get() == commerce::kShopCardArm1) {
        ShopCardItem* shopCardItem = static_cast<ShopCardItem*>(config);
        _seeMoreButton.accessibilityLabel = [@[
          _seeMoreButton.titleLabel.text, shopCardItem.shopCardData.productTitle
        ] componentsJoinedByString:@", "];
      }
      break;
    }
    default:
      // No customized accessibility label
      break;
  }
}

// Updates the bottom content margins if the module contents need it.
- (void)updateBottomContentMarginsForConfig:(MagicStackModule*)config {
  switch (config.type) {
    case ContentSuggestionsModuleType::kMostVisited:
    case ContentSuggestionsModuleType::kShortcuts:
    case ContentSuggestionsModuleType::kCompactedSetUpList:
      _contentStackViewBottomMarginAnchor.constant =
          isContentOversized(_stackView) ? -kOversizedReducedContentBottomInset
                                         : -kReducedContentBottomInset;
      _reducedBottomMargin = true;
      break;
    case ContentSuggestionsModuleType::kSafetyCheck: {
      SafetyCheckState* safetyCheckConfig =
          static_cast<SafetyCheckState*>(config);
      if ([safetyCheckConfig numberOfIssues] > 1) {
        _contentStackViewBottomMarginAnchor.constant =
            isContentOversized(_stackView)
                ? -kOversizedReducedContentBottomInset
                : -kReducedContentBottomInset;
        _reducedBottomMargin = true;
      }
      break;
    }

    default:
      _reducedBottomMargin = false;
      break;
  }
}

#pragma mark - UITraitEnvironment

#if !defined(__IPHONE_17_0) || __IPHONE_OS_VERSION_MIN_REQUIRED < __IPHONE_17_0
- (void)traitCollectionDidChange:(UITraitCollection*)previousTraitCollection {
  [super traitCollectionDidChange:previousTraitCollection];
  if (@available(iOS 17, *)) {
    return;
  }

  if (previousTraitCollection.preferredContentSizeCategory !=
      self.traitCollection.preferredContentSizeCategory) {
    [self updateCardSizing];
  }
}
#endif

#pragma mark - MagicStackModuleContentViewDelegate

- (void)updateNotificationsOptInVisibility:(BOOL)showNotificationsOptIn {
  _notificationsOptInButton.hidden = !showNotificationsOptIn;
  _subtitle.hidden = ![self shouldShowSubtitle];
}

- (void)setSubtitle:(NSString*)subtitle {
  _subtitle.text = subtitle;
  _subtitle.accessibilityIdentifier = subtitle;
}

- (void)updateSeparatorVisibility:(BOOL)isHidden {
  // Do nothing if the new value is the same as the old value
  if (isHidden == _separator.hidden) {
    return;
  }

  _separator.hidden = isHidden;
}

- (NSArray<UIMenuElement*>*)contextMenuElementsForCurrentModule {
  return [self.contextMenuInteractionHandler menuElements];
}

- (void)notifyContextMenuInteractionEndWithAnimator:
    (id<UIContextMenuInteractionAnimating>)animator {
  [self.contextMenuInteractionHandler
      notifyContextMenuInteractionEndWithAnimator:animator];
}

#pragma mark - Helpers

// Handles taps on the "See More" button.
- (void)seeMoreButtonWasTapped:(UIButton*)button {
  [_delegate seeMoreWasTappedForModuleType:_type];
}

// Handles taps on the notifications opt-in button.
- (void)notificationsOptInButtonWasTapped:(UIButton*)button {
  [_delegate enableNotifications:_type viaContextMenu:NO];
}

// Determines if a subtitle should be displayed. Currently, a subtitle is
// shown only when both the "See More" button and the notifications opt-in
// button are hidden.
- (BOOL)shouldShowSubtitle {
  if (_seeMoreButton.isHidden && _notificationsOptInButton.isHidden) {
    return YES;
  }

  return NO;
}

// Returns the module's subtitle, if any, given the Magic Stack module `type`.
- (NSString*)subtitleStringForConfig:(MagicStackModule*)config {
  if (config.type == ContentSuggestionsModuleType::kSafetyCheck) {
    SafetyCheckState* safetyCheckConfig =
        static_cast<SafetyCheckState*>(config);
    return FormatElapsedTimeSinceLastSafetyCheck(safetyCheckConfig.lastRunTime);
  }

  return @"";
}

// Based on ContentSuggestionsModuleType, returns YES if a separator should be
// shown between the module title/subtitle row, and the remaining bottom-half of
// the module.
- (BOOL)shouldShowSeparator {
  switch (_type) {
    case ContentSuggestionsModuleType::kSetUpListSync:
    case ContentSuggestionsModuleType::kSetUpListDefaultBrowser:
    case ContentSuggestionsModuleType::kSetUpListAutofill:
    case ContentSuggestionsModuleType::kSetUpListAllSet:
    case ContentSuggestionsModuleType::kSetUpListNotifications:
    case ContentSuggestionsModuleType::kSafetyCheck:
    case ContentSuggestionsModuleType::kTips:
      return YES;
    case ContentSuggestionsModuleType::kTabResumption:
    case ContentSuggestionsModuleType::kTipsWithProductImage:
      return NO;
    default:
      return NO;
  }
}

// Updates the card sizing based on the dynamic Magic Stack Height.
- (void)updateCardSizing {
  _containerHeightAnchor.constant = GetMagicStackHeight(_stackView);
  if (_reducedBottomMargin) {
    _contentStackViewBottomMarginAnchor.constant =
        isContentOversized(_stackView) ? -kOversizedReducedContentBottomInset
                                       : -kReducedContentBottomInset;
  }
}

@end
