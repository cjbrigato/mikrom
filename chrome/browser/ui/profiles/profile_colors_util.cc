// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/profiles/profile_colors_util.h"

#include "base/containers/contains.h"
#include "base/functional/callback.h"
#include "base/rand_util.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/new_tab_page/chrome_colors/generated_colors_info.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_avatar_icon_util.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/common/themes/autogenerated_theme_util.h"
#include "components/signin/public/base/signin_switches.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_manager.h"
#include "ui/gfx/color_utils.h"
#include "ui/native_theme/native_theme.h"

namespace {

// Minimum saturation for a color to be autoselected (as picking 'colorful'
// colors is needed to distinguish colors from each other).
constexpr double kMinimumSaturation = 0.25;

// Maximum diff in lightness of an autoselected color to the color of the
// current profile (so that the interception UI does not look bad).
constexpr double kMaximumLightnessDiff = 0.35;

// This is the core definition of how ProfileThemeColors are obtained.
ProfileThemeColors GetProfileThemeColorsFromHighlightColor(
    const ui::ColorProvider& color_provider,
    SkColor highlight_color,
    SkColor seed_color = SK_ColorTRANSPARENT) {
  ProfileThemeColors colors;
  colors.profile_highlight_color = highlight_color;

  // Set the avatar's colors so that there is always a contrast between fill
  // color and stroke color.
  DefaultAvatarColors avatar_colors =
      GetDefaultAvatarColors(color_provider, highlight_color);
  colors.default_avatar_stroke_color = avatar_colors.stroke_color;
  colors.default_avatar_fill_color = avatar_colors.fill_color;

  // Use the primary color as seed for default profile themes.
  colors.profile_color_seed =
      seed_color == SK_ColorTRANSPARENT
          ? color_provider.GetColor(ui::kColorSysPrimary)
          : seed_color;

  return colors;
}

size_t GenerateRandomIndex(size_t size) {
  DCHECK_GT(size, 0u);
  return static_cast<size_t>(base::RandInt(0, size - 1));
}

std::vector<int> GetAvailableColorIndices(
    const std::set<ProfileThemeColors>& used_theme_colors,
    std::optional<double> current_color_lightness) {
  std::vector<int> available_color_indices;
  for (size_t i = 0; i < std::size(chrome_colors::kGeneratedColorsInfo); ++i) {
    ProfileThemeColors theme_colors =
        GetProfileThemeColorsForAutogeneratedColor(
            chrome_colors::kGeneratedColorsInfo[i].color);
    if (base::Contains(used_theme_colors, theme_colors)) {
      continue;
    }

    const SkColor highlight = theme_colors.profile_highlight_color;
    if (!IsSaturatedForAutoselection(highlight)) {
      continue;
    }
    if (current_color_lightness &&
        !IsLightForAutoselection(highlight, *current_color_lightness)) {
      continue;
    }

    available_color_indices.push_back(i);
  }
  return available_color_indices;
}

const ui::ColorProvider* GetDefaultColorProvider() {
  // ColorProviders are normally obtained from some kind of window-scoped
  // object, but at this level no such things are available.  So get a color
  // provider via the default NativeTheme, which will know things like whether
  // dark mode is enabled.
  //
  // DO NOT COPY THIS CODE BLINDLY.  This is not an appropriate way to get a
  // ColorProvider in most circumstances, as it will not take into account
  // custom themes, incognito, PWA colors, and various other scenarios.
  return ui::ColorProviderManager::Get().GetColorProviderFor(
      ui::NativeTheme::GetInstanceForNativeUi()->GetColorProviderKey(nullptr));
}

double ExtractCurrentColorLightness(ProfileAttributesEntry* current_profile) {
  ProfileThemeColors current_colors =
      current_profile ? current_profile->GetProfileThemeColors()
                      : GetDefaultProfileThemeColors();

  color_utils::HSL hsl;
  color_utils::SkColorToHSL(current_colors.profile_highlight_color, &hsl);
  return hsl.l;
}

}  // namespace

bool ShouldUseDefaultProfileColors(const ThemeService& theme_service) {
  return theme_service.UsingExtensionTheme() ||
         (theme_service.IsSystemThemeDistinctFromDefaultTheme() &&
          theme_service.UsingSystemTheme());
}

profiles::PlaceholderAvatarIconParams
GetPlaceholderAvatarIconParamsVisibleAgainstColor(SkColor background_color) {
  profiles::AvatarVisibilityAgainstBackground visibility =
      color_utils::IsDark(background_color)
          ? profiles::AvatarVisibilityAgainstBackground::
                kVisibleAgainstDarkTheme
          : profiles::AvatarVisibilityAgainstBackground::
                kVisibleAgainstLightTheme;

  return profiles::PlaceholderAvatarIconParams(
      {.has_padding = false,
       .has_background = false,
       .visibility_against_background = visibility});
}

profiles::PlaceholderAvatarIconParams
GetPlaceholderAvatarIconParamsDependingOnTheme(
    ThemeService* theme_service,
    ui::ColorId background_color_id,
    const ui::ColorProvider& color_provider) {
  // Use an outline silhouette icon that is always visible against the
  // background only for extension and system themes.
  return ShouldUseDefaultProfileColors(*theme_service)
             ? GetPlaceholderAvatarIconParamsVisibleAgainstColor(
                   color_provider.GetColor(background_color_id))
             : profiles::PlaceholderAvatarIconParams{.has_padding = false,
                                                     .has_background = false};
}

ProfileThemeColors GetProfileThemeColorsForAutogeneratedColor(
    SkColor autogenerated_color) {
  return GetProfileThemeColorsFromHighlightColor(
      *GetDefaultColorProvider(),
      GetAutogeneratedThemeColors(autogenerated_color).frame_color);
}

ProfileThemeColors GetCurrentProfileThemeColors(
    const ui::ColorProvider& color_provider,
    const ThemeService& theme_service) {
  return GetProfileThemeColorsFromHighlightColor(
      color_provider, color_provider.GetColor(ui::kColorFrameActive),
      theme_service.GetUserColor().value_or(SK_ColorTRANSPARENT));
}

ProfileThemeColors GetDefaultProfileThemeColors(
    const ui::ColorProvider* color_provider) {
  if (!color_provider) {
    color_provider = GetDefaultColorProvider();
  }
  return GetProfileThemeColorsFromHighlightColor(
      *color_provider, color_provider->GetColor(ui::kColorFrameActiveUnthemed));
}

SkColor GetProfileForegroundTextColor(SkColor profile_highlight_color) {
  return color_utils::GetColorWithMaxContrast(profile_highlight_color);
}

SkColor GetProfileForegroundIconColor(SkColor profile_highlight_color) {
  SkColor text_color = GetProfileForegroundTextColor(profile_highlight_color);
  SkColor icon_color = color_utils::DeriveDefaultIconColor(text_color);
  return color_utils::BlendForMinContrast(icon_color, profile_highlight_color,
                                          text_color)
      .color;
}

DefaultAvatarColors GetDefaultAvatarColors(
    const ui::ColorProvider& color_provider,
    SkColor profile_highlight_color) {
  DefaultAvatarColors result;

  if (color_utils::IsDark(profile_highlight_color)) {
    result.stroke_color = color_provider.GetColor(kColorAvatarStroke);
    result.fill_color =
        color_utils::GetContrastRatio(profile_highlight_color,
                                      result.stroke_color) >=
                color_utils::kMinimumVisibleContrastRatio
            ? profile_highlight_color
            : color_provider.GetColor(kColorAvatarFillForContrast);
    return result;
  }

  color_utils::HSL color_hsl;
  color_utils::SkColorToHSL(profile_highlight_color, &color_hsl);
  color_hsl.l = std::max(0., color_hsl.l - 0.5);
  result.stroke_color = color_utils::HSLToSkColor(
      color_hsl, SkColorGetA(profile_highlight_color));
  result.fill_color = profile_highlight_color;
  return result;
}

bool IsSaturatedForAutoselection(SkColor color) {
  color_utils::HSL hsl;
  color_utils::SkColorToHSL(color, &hsl);
  return hsl.s >= kMinimumSaturation;
}

bool IsLightForAutoselection(SkColor color, double reference_lightness) {
  color_utils::HSL hsl;
  color_utils::SkColorToHSL(color, &hsl);
  return std::abs(hsl.l - reference_lightness) <= kMaximumLightnessDiff;
}

chrome_colors::ColorInfo GenerateNewProfileColorWithGenerator(
    ProfileAttributesStorage& storage,
    base::OnceCallback<size_t(size_t count)> random_generator,
    ProfileAttributesEntry* current_profile) {
  // TODO(crbug.com/40141230): Return only a SkColor if the full ColorInfo is
  // not needed.
  std::set<ProfileThemeColors> used_theme_colors;
  for (ProfileAttributesEntry* entry : storage.GetAllProfilesAttributes()) {
    std::optional<ProfileThemeColors> current_colors =
        entry->GetProfileThemeColorsIfSet();
    if (current_colors) {
      used_theme_colors.insert(*current_colors);
    }
  }

  double current_color_lightness =
      ExtractCurrentColorLightness(current_profile);

  // Collect indices of profile colors that match all the filters.
  std::vector<int> available_color_indices =
      GetAvailableColorIndices(used_theme_colors, current_color_lightness);
  // Relax the constraints until some colors become available.
  if (available_color_indices.empty()) {
    available_color_indices =
        GetAvailableColorIndices(used_theme_colors, std::nullopt);
  }
  if (available_color_indices.empty()) {
    // If needed, we could allow unsaturated colors (shades of grey) before
    // allowing a duplicate color.
    available_color_indices =
        GetAvailableColorIndices(std::set<ProfileThemeColors>(), std::nullopt);
  }
  DCHECK(!available_color_indices.empty());

  size_t size = available_color_indices.size();
  size_t available_index = std::move(random_generator).Run(size);
  DCHECK_LT(available_index, size);
  size_t index = available_color_indices[available_index];
  return chrome_colors::kGeneratedColorsInfo[index];
}

chrome_colors::ColorInfo GenerateNewProfileColor(
    ProfileAttributesEntry* current_profile) {
  return GenerateNewProfileColorWithGenerator(
      g_browser_process->profile_manager()->GetProfileAttributesStorage(),
      base::BindOnce(&GenerateRandomIndex), current_profile);
}
