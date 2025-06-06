// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/webui/common/sea_pen_resources.h"

#include "chromeos/strings/grit/chromeos_strings.h"
#include "ui/base/webui/web_ui_util.h"

namespace ash::common {

void AddSeaPenStrings(content::WebUIDataSource* source) {
  static constexpr webui::LocalizedString kLocalizedStrings[] = {
      {"seaPenExclusiveWallpapersHeading",
       IDS_PERSONALIZATION_APP_EXCLUSIVE_WALLPAPERS_HEADING},
      {"seaPenChooseAWallpaperHeading",
       IDS_PERSONALIZATION_APP_CHOOSE_A_WALLPAPER_HEADING},
      {"seaPenLabel", IDS_SEA_PEN_LABEL},
      {"vcBackgroundLabel", IDS_VC_BACKGROUND_LABEL},
      {"seaPenPoweredByGoogleAi", IDS_SEA_PEN_POWERED_BY_GOOGLE_AI},
      {"seaPenTemplateHeading", IDS_SEA_PEN_TEMPLATE_HEADING},
      {"seaPenRecentWallpapersHeading", IDS_SEA_PEN_RECENT_WALLPAPERS_HEADING},
      {"vcBackgroundRecentWallpapersHeading",
       IDS_VC_BACKGROUND_RECENT_WALLPAPERS_HEADING},
      {"seaPenInspireMeButton", IDS_SEA_PEN_INSPIRE_ME_BUTTON},
      {"seaPenCreateButton", IDS_SEA_PEN_CREATE_BUTTON},
      {"seaPenRecreateButton", IDS_SEA_PEN_RECREATE_BUTTON},
      {"seaPenWallpaperPoweredByGoogle",
       IDS_SEA_PEN_WALLPAPER_POWERED_BY_GOOGLE},
      {"vcBackgroundPoweredByGoogle", IDS_VC_BACKGROUND_POWERED_BY_GOOGLE},
      {"seaPenDeleteWallpaper", IDS_SEA_PEN_DELETE_WALLPAPER},
      {"seaPenCreateMore", IDS_SEA_PEN_CREATE_MORE},
      {"seaPenAbout", IDS_SEA_PEN_ABOUT},
      {"seaPenAboutDialogTitle", IDS_SEA_PEN_ABOUT_DIALOG_TITLE},
      {"vcBackgroundAboutDialogTitle", IDS_VC_BACKGROUND_ABOUT_DIALOG_TITLE},
      {"seaPenAboutDialogPrompt", IDS_SEA_PEN_ABOUT_DIALOG_PROMPT},
      {"vcBackgroundAboutDialogPrompt", IDS_VC_BACKGROUND_ABOUT_DIALOG_PROMPT},
      {"seaPenAboutDialogDate", IDS_SEA_PEN_ABOUT_DIALOG_DATE},
      {"seaPenAboutDialogClose", IDS_SEA_PEN_ABOUT_DIALOG_CLOSE},
      {"seaPenErrorNoInternet", IDS_SEA_PEN_ERROR_NO_INTERNET},
      {"seaPenErrorResourceExhausted", IDS_SEA_PEN_ERROR_RESOURCE_EXHAUSTED},
      {"seaPenErrorGeneric", IDS_SEA_PEN_ERROR_GENERIC},
      {"seaPenFreeformErrorPerson", IDS_SEA_PEN_FREEFORM_ERROR_PERSON},
      {"seaPenExperimentLabel", IDS_SEA_PEN_EXPERIMENT_LABEL},
      {"seaPenUnavailableLabel", IDS_SEA_PEN_UNAVAILABLE_LABEL},
      {"seaPenThumbnailsLoading", IDS_SEA_PEN_THUMBNAILS_LOADING},
      {"seaPenCreatingHighResImage", IDS_SEA_PEN_CREATING_HIGH_RES_IMAGE},
      {"seaPenExpandOptionsButton", IDS_SEA_PEN_EXPAND_OPTIONS_BUTTON},
      {"seaPenRecentImageMenuButton", IDS_SEA_PEN_RECENT_IMAGE_MENU_BUTTON},
      {"seaPenMenuRoleDescription", IDS_SEA_PEN_MENU_ROLE_DESCRIPTION},
      {"seaPenCustomizeAiPrompt", IDS_SEA_PEN_CUSTOMIZE_AI_PROMPT},
      {"seaPenFeedbackDescription", IDS_SEA_PEN_FEEDBACK_DESCRIPTION},
      {"seaPenFeedbackPositive", IDS_SEA_PEN_FEEDBACK_POSITIVE},
      {"seaPenFeedbackNegative", IDS_SEA_PEN_FEEDBACK_NEGATIVE},
      {"seaPenSetWallpaper", IDS_SEA_PEN_SET_WALLPAPER},
      {"seaPenSetCameraBackground", IDS_SEA_PEN_SET_CAMERA_BACKGROUND},
      {"seaPenLabel", IDS_SEA_PEN_LABEL},
      {"seaPenZeroStateMessage", IDS_SEA_PEN_ZERO_STATE_MESSAGE},
      {"seaPenIntroductionTitle", IDS_SEA_PEN_INTRODUCTION_DIALOG_TITLE},
      {"seaPenIntroductionDialogContent",
       IDS_SEA_PEN_INTRODUCTION_DIALOG_CONTENT},
      {"seaPenIntroductionDialogCloseButton",
       IDS_SEA_PEN_INTRODUCTION_DIALOG_CLOSE_BUTTON},
      {"seaPenFreeformWallpaperLabel", IDS_SEA_PEN_FREEFORM_WALLPAPER_LABEL},
      {"seaPenTemplatesWallpaperLabel", IDS_SEA_PEN_TEMPLATES_WALLPAPER_LABEL},
      {"seaPenAriaDescriptionGeneratedImage",
       IDS_SEA_PEN_ARIA_DESCRIPTION_GENERATED_IMAGE},
      {"seaPenFreeformSamplePromptsLabel",
       IDS_SEA_PEN_FREEFORM_SAMPLE_PROMPTS_LABEL},
      {"seaPenFreeformResultsLabel", IDS_SEA_PEN_FREEFORM_RESULTS_LABEL},
      {"seaPenFreeformShuffle", IDS_SEA_PEN_FREEFORM_SHUFFLE},
      {"seaPenFreeformInputPlaceholder",
       IDS_SEA_PEN_FREEFORM_INPUT_PLACEHOLDER},
      {"seaPenFreeformErrorNoInternet", IDS_SEA_PEN_FREEFORM_ERROR_NO_INTERNET},
      {"seaPenFreeformErrorUnsupportedLanguage",
       IDS_SEA_PEN_FREEFORM_ERROR_UNSUPPORTED_LANGUAGE},
      {"seaPenFreeformErrorBlockedOutputs",
       IDS_SEA_PEN_FREEFORM_ERROR_BLOCKED_OUTPUTS},
      {"seaPenFreeformPreviousPrompts", IDS_SEA_PEN_FREEFORM_PREVIOUS_PROMPTS},
      {"seaPenFreeformPreviousPromptsTooltip",
       IDS_SEA_PEN_FREEFORM_PREVIOUS_PROMPTS_TOOLTIP},
      {"seaPenFreeformPromptingGuide", IDS_SEA_PEN_FREEFORM_PROMPTING_GUIDE},
      {"seaPenFreeformNavigationDescription",
       IDS_SEA_PEN_FREEFORM_NAVIGATION_DESCRIPTION},
      {"seaPenFreeformAriaLabelSamplePrompts",
       IDS_SEA_PEN_FREEFORM_ARIA_LABEL_SAMPLE_PROMPTS},
      {"seaPenFreeformAriaLabelSuggestions",
       IDS_SEA_PEN_FREEFORM_ARIA_LABEL_SUGGESTIONS},
      {"seaPenFreeformAriaLabelShuffleSuggestions",
       IDS_SEA_PEN_FREEFORM_ARIA_LABEL_SHUFFLE_SUGGESTIONS},
      {"seaPenFreeformDisclaimer", IDS_SEA_PEN_FREEFORM_DISCLAIMER},
      {"seaPenFreeformIntroductionDialogContent",
       IDS_SEA_PEN_FREEFORM_INTRODUCTION_DIALOG_CONTENT},
      {"seaPenDismissError", IDS_PERSONALIZATION_APP_DISMISS},
      {"ariaLabelLoading", IDS_PERSONALIZATION_APP_ARIA_LABEL_LOADING},
      {"seaPenManagedLabel", IDS_ASH_ENTERPRISE_DEVICE_MANAGED_SHORT},
      {"ariaAnnounceSamplePromptsShuffled",
       IDS_SEA_PEN_ARIA_ANNOUNCE_SAMPLE_PROMPTS_SHUFFLED},
      {"ariaAnnouncePromptSuggestionsShuffled",
       IDS_SEA_PEN_ARIA_ANNOUNCE_SUGGESTIONS_SHUFFLED},
      {"seaPenFreeformSuggestion4K", IDS_SEA_PEN_FREEFORM_SUGGESTION_4K},
      {"seaPenFreeformSuggestionRealisticPhoto",
       IDS_SEA_PEN_FREEFORM_SUGGESTION_REALISTIC_PHOTO},
      {"seaPenFreeformSuggestionSurreal",
       IDS_SEA_PEN_FREEFORM_SUGGESTION_SURREAL},
      {"seaPenFreeformSuggestionBeautiful",
       IDS_SEA_PEN_FREEFORM_SUGGESTION_BEAUTIFUL},
      {"seaPenFreeformSuggestionMinimal",
       IDS_SEA_PEN_FREEFORM_SUGGESTION_MINIMAL},
      {"seaPenFreeformSuggestionSunset",
       IDS_SEA_PEN_FREEFORM_SUGGESTION_SUNSET},
      {"seaPenFreeformSuggestionPastelColors",
       IDS_SEA_PEN_FREEFORM_SUGGESTION_PASTEL_COLORS},
      {"seaPenFreeformSuggestionGlowing",
       IDS_SEA_PEN_FREEFORM_SUGGESTION_GLOWING},
      {"seaPenFreeformSuggestionStarFilledSky",
       IDS_SEA_PEN_FREEFORM_SUGGESTION_STAR_FILLED_SKY},
      {"seaPenFreeformSuggestionDramaticShadows",
       IDS_SEA_PEN_FREEFORM_SUGGESTION_DRAMATIC_SHADOWS},
      {"seaPenFreeformSuggestionCoveredInSnow",
       IDS_SEA_PEN_FREEFORM_SUGGESTION_COVERED_IN_SNOW},
      {"seaPenFreeformSuggestionBioluminescent",
       IDS_SEA_PEN_FREEFORM_SUGGESTION_BIOLUMINESCENT},
      {"seaPenFreeformSuggestionLongExposure",
       IDS_SEA_PEN_FREEFORM_SUGGESTION_LONG_EXPOSURE},
      {"seaPenFreeformSuggestionFoggy", IDS_SEA_PEN_FREEFORM_SUGGESTION_FOGGY},
      {"seaPenFreeformSuggestionGalaxy",
       IDS_SEA_PEN_FREEFORM_SUGGESTION_GALAXY},
      {"seaPenFreeformSuggestionNeonLights",
       IDS_SEA_PEN_FREEFORM_SUGGESTION_NEON_LIGHTS},
      {"seaPenFreeformSuggestionReflections",
       IDS_SEA_PEN_FREEFORM_SUGGESTION_REFLECTIONS},
      {"seaPenFreeformSuggestionLightning",
       IDS_SEA_PEN_FREEFORM_SUGGESTION_LIGHTNING},
      {"seaPenFreeformSuggestionBokehEffect",
       IDS_SEA_PEN_FREEFORM_SUGGESTION_BOKEH_EFFECT},
      {"seaPenFreeformSuggestionWithColorGrading",
       IDS_SEA_PEN_FREEFORM_SUGGESTION_WITH_COLOR_GRADING},
      {"seaPenFreeformSuggestionVolumetricLight",
       IDS_SEA_PEN_FREEFORM_SUGGESTION_VOLUMETRIC_LIGHT},
      {"seaPenFreeformSuggestionNegativeSpace",
       IDS_SEA_PEN_FREEFORM_SUGGESTION_NEGATIVE_SPACE},
      {"seaPenFreeformSuggestionDigitalArt",
       IDS_SEA_PEN_FREEFORM_SUGGESTION_DIGITAL_ART},
      {"seaPenFreeformSuggestionTRex", IDS_SEA_PEN_FREEFORM_SUGGESTION_T_REX},
      {"seaPenFreeformSuggestionUnicorn",
       IDS_SEA_PEN_FREEFORM_SUGGESTION_UNICORN},
      {"seaPenFreeformSuggestionCats", IDS_SEA_PEN_FREEFORM_SUGGESTION_CATS},
      {"seaPenFreeformSuggestionVectorArtStyle",
       IDS_SEA_PEN_FREEFORM_SUGGESTION_VECTOR_ART_STYLE},
      {"seaPenFreeformSuggestion3DRender",
       IDS_SEA_PEN_FREEFORM_SUGGESTION_3D_RENDER},
      {"seaPenFreeformSampleChromeSpheres",
       IDS_SEA_PEN_FREEFORM_SAMPLE_CHROME_SPHERES},
      {"seaPenFreeformSampleGalaxyWithSpaceship",
       IDS_SEA_PEN_FREEFORM_SAMPLE_GALAXY_WITH_SPACESHIP},
      {"seaPenFreeformSampleCatRidingUnicorn",
       IDS_SEA_PEN_FREEFORM_SAMPLE_CAT_RIDING_UNICORN},
      {"seaPenFreeformSampleAnimatedFlowers",
       IDS_SEA_PEN_FREEFORM_SAMPLE_ANIMATED_FLOWERS},
      {"seaPenFreeformSampleLilyInRain",
       IDS_SEA_PEN_FREEFORM_SAMPLE_LILY_IN_RAIN},
      {"seaPenFreeformSampleDalmation", IDS_SEA_PEN_FREEFORM_SAMPLE_DALMATION},
      {"seaPenFreeformSampleDelorean", IDS_SEA_PEN_FREEFORM_SAMPLE_DELOREAN},
      {"seaPenFreeformSampleBlackMotorcycle",
       IDS_SEA_PEN_FREEFORM_SAMPLE_BLACK_MOTORCYCLE},
      {"seaPenFreeformSampleSpaceshipOverCity",
       IDS_SEA_PEN_FREEFORM_SAMPLE_SPACESHIP_OVER_CITY},
      {"seaPenFreeformSampleCatOnWindowsill",
       IDS_SEA_PEN_FREEFORM_SAMPLE_CAT_ON_WINDOWSILL},
      {"seaPenFreeformSampleBioluminescentBeach",
       IDS_SEA_PEN_FREEFORM_SAMPLE_BIOLUMINESCENT_BEACH},
      {"seaPenFreeformSampleBlackSandDunes",
       IDS_SEA_PEN_FREEFORM_SAMPLE_BLACK_SAND_DUNES},
      {"seaPenFreeformSampleTreeMadeOfStars",
       IDS_SEA_PEN_FREEFORM_SAMPLE_TREE_MADE_OF_STARS},
      {"seaPenFreeformSampleMoonOverLake",
       IDS_SEA_PEN_FREEFORM_SAMPLE_MOON_OVER_LAKE},
      {"seaPenFreeformSampleMarbleArch",
       IDS_SEA_PEN_FREEFORM_SAMPLE_MARBLE_ARCH},
      {"seaPenFreeformSampleSteampunkSpaceship",
       IDS_SEA_PEN_FREEFORM_SAMPLE_STEAMPUNK_SPACESHIP},
      {"seaPenFreeformSampleWhiteTiger",
       IDS_SEA_PEN_FREEFORM_SAMPLE_WHITE_TIGER},
      {"seaPenFreeformSampleAnimePathOverlookingOcean",
       IDS_SEA_PEN_FREEFORM_SAMPLE_ANIME_PATH_OVERLOOKING_OCEAN},
      {"seaPenFreeformSamplePapaverRheaStems",
       IDS_SEA_PEN_FREEFORM_SAMPLE_PAPAVER_RHEA_STEMS},
      {"seaPenFreeformSampleMeteorShower",
       IDS_SEA_PEN_FREEFORM_SAMPLE_METEOR_SHOWER},
  };
  source->AddLocalizedStrings(kLocalizedStrings);
}

}  // namespace ash::common
