# Copyright 2023 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import plistlib
import os
from pathlib import Path
from docx import Document
import json
import re
import ast
import subprocess
import shutil

BASE_DIR = Path(__file__).parents[1]
CHROME_ICON_FILENAME = 'ios/chrome/browser/shared/ui/symbols/symbol_names.mm'
DEFAULT_SF_SYMBOLS_PATH = '/Applications/SF Symbols.app/'


class ValidIcons(object):
    """Metadata about valid icon names.

    Attributes:
        valid_custom: Set of the valid custom icon names from Chrome constants.
        valid_default: Set of the currently valid default icon names from Chrome
            constants.
        full_default: Set of all available default icon names from
            SF Symbols.app, even ones that currently do not exist as constants
            in Chrome.
    """

    def __init__(self, valid_custom, valid_default, full_default):
        self.valid_custom = valid_custom
        self.valid_default = valid_default
        self.full_default = full_default


def UpdateWhatsNewItemAndGetNewTypeValue(feature_dict: dict[str, str]) -> int:
    """Updates whats_new_item.h with the new feature entry and returns
     the new enum value.

  Args:
      feature_dict: Data for the new What's New feature.
  Returns:
      The enum value for the new WhatsNewType
  """
    feature_name = feature_dict['Feature name']
    whats_new_item_file = os.path.join(
        BASE_DIR,
        '../ios/chrome/browser/whats_new/ui/data_source/whats_new_item.h')
    with open(whats_new_item_file, 'r+', encoding='utf-8', newline='') as file:
        file_content = file.read()
        read_whats_new_types_regex = r'enum class WhatsNewType\s*\{\s(.*?)\s\}'
        match_whats_new_types = re.search(read_whats_new_types_regex,
                                          file_content, re.DOTALL)
        assert match_whats_new_types
        matches = match_whats_new_types.group(1).split(',\n')
        max_enum = matches[-1]
        # Minus 3 because the matches will include kError, kMinValue,
        # and KMaxValue.
        next_type_value = len(matches) - 3
        new_enum_definition = []
        new_feature_id = 'k' + feature_name
        new_enum_definition.append(new_feature_id + ' = ' +
                                   str(next_type_value) + ',')
        new_enum_definition.append('kMaxValue = ' + new_feature_id)
        final_str = '\n'.join(new_enum_definition)
        new_file_content = file_content.replace(max_enum, final_str)
        file.seek(0)
        file.write(new_file_content)
        return next_type_value


def _GetPrimaryAction(action: str) -> int:
    """Get the WhatsNewPrimaryAction corresponding to a primary button string.

  Args:
      action: Primary button text string.
  Returns:
      The enum for WhatsNewPrimaryAction.
  """
    if action == 'iOS Settings':
        return 1
    if action == 'Chrome Privacy Settings':
        return 2
    if action == 'Chrome Settings':
        return 3
    if action == 'iOS Credential Provider Settings':
        return 4
    if action == 'Lens':
        return 5
    if action == 'Safe Browsing Settings':
        return 6
    if action == 'Chrome Password Manager':
        return 7
    if action == 'None':
        return 0
    return 0


def CleanUpFeaturesPlist() -> None:
    """Removes all existing features from the plist.

  """
    whats_new_plist_file = os.path.join(
        BASE_DIR, '../ios/chrome/browser/whats_new/ui/data_source/'
        'resources/whats_new_entries.plist')
    with open(whats_new_plist_file, 'rb') as file:
        plist_data = plistlib.load(file)
        plist_data['Features'] = []
        with open(whats_new_plist_file, 'wb') as plist_file:
            plistlib.dump(plist_data, plist_file, sort_keys=False)


def UpdateWhatsNewPlist(feature_dict: dict[str, str], feature_type: int,
                        path_to_milestone_folder: str) -> None:
    """Updates whats_new_entries.plist with the new feature entry.

  Args:
      feature_dict: Data for the new What's New feature.
      feature_type: Newly added WhatsNewType for the new What's New feature.
      path_to_milestone_folder: Path to the milestone folder.
  """
    # Format and clean up the instructions text field ID.
    instruction_steps = StripWhitespacesAndEmptyLines(feature_dict['Instructions'])
    serialized_animation_texts = feature_dict['Animation texts'].splitlines()
    animation_text_dict = {}
    for animation_text_string in serialized_animation_texts:
        animations_text = json.loads(animation_text_string)
        animation_text_dict[animations_text['key'].strip()] = StripWhitespacesAndEmptyLines(animations_text['value'])
    new_entry = {
        'Type': feature_type,
        'Title': feature_dict['Title'],
        'Subtitle': feature_dict['Subtitle'],
        'ImageName': feature_dict['Feature name'],
        'ImageTexts': animation_text_dict,
        'IconImageName': feature_dict['Icon name'],
        'IconBackgroundColor': feature_dict['Icon background color'],
        'IsSymbol': True,
        'IsSystemSymbol': feature_dict['Icon Type'] == 'Default',
        'InstructionSteps': instruction_steps,
        'PrimaryAction': _GetPrimaryAction(feature_dict['Primary action']),
        'LearnMoreUrlString': feature_dict['Help url']
    }
    whats_new_plist_file = os.path.join(
        BASE_DIR, '../ios/chrome/browser/whats_new/ui/data_source/'
        'resources/whats_new_entries.plist')
    with open(whats_new_plist_file, 'rb') as file:
        plist_data = plistlib.load(file)
        plist_data['Features'].append(new_entry)
        with open(whats_new_plist_file, 'wb') as plist_file:
            plistlib.dump(plist_data, plist_file, sort_keys=False)


def UpdateWhatsNewUtils(feature_dict: dict[str, str]) -> None:
    """Updates whats_new_util.mm with the new feature entry.

  Args:
      feature_dict: Data for the new What's New feature.
  """
    feature_name = feature_dict['Feature name']
    whats_new_util_file = os.path.join(
        BASE_DIR, '..',
        'ios/chrome/browser/whats_new/coordinator/whats_new_util.mm')
    with open(whats_new_util_file, 'r+', encoding='utf-8', newline='') as file:
        read_data = file.read()
        whats_new_type_error_regex = r'case WhatsNewType::kError:'
        case_to_replace = re.search(whats_new_type_error_regex, read_data,
                                    re.DOTALL)
        assert case_to_replace
        new_case = []
        new_case.append('case WhatsNewType::k' + feature_name + ':')
        new_case.append('return "' + feature_name + '";')
        new_case.append('case WhatsNewType::kError:')
        final_str = '\n'.join(new_case)
        data = read_data.replace(case_to_replace.group(0), final_str)
        file.seek(0)
        file.write(data)


def CopyAnimationFilesToResources(feature_dict: dict[str, str],
                                  path_to_milestone_folder: str) -> None:
    """Copy and rename the new feature lottie files.

  Args:
      feature_dict: Data for the new What's New feature.
      path_to_milestone_folder: Path to the milestone folder.
  """
    feature_name = feature_dict['Feature name']
    animation_name_darkmode = feature_dict['Animation darkmode']
    animation_name = feature_dict['Animation']
    milestone = feature_dict['Milestone'].lower()
    DEST_DIR = os.path.join(
        BASE_DIR, '../ios/chrome/browser/whats_new/ui/data_source/resources',
        milestone)
    os.makedirs(DEST_DIR, exist_ok=True)
    darkmode_src_file = os.path.join(path_to_milestone_folder, feature_name,
                                     animation_name_darkmode)
    new_darkmode_dst_file = os.path.join(DEST_DIR,
                                         feature_name + '_darkmode.json')
    shutil.copyfile(darkmode_src_file, new_darkmode_dst_file)
    src_file = os.path.join(path_to_milestone_folder, feature_name,
                            animation_name)
    new_animation_dst_file = os.path.join(DEST_DIR, feature_name + '.json')
    shutil.copyfile(src_file, new_animation_dst_file)


def UpdateResourcesBuildFile(feature_dict: dict[str, str]) -> None:
    """Updates the data source BUILD.gn with the new animation assets.

  Args:
      feature_dict: Data for the new What's New feature.
  """
    feature_name = feature_dict['Feature name']
    screenshots_lists_regex = r'screenshots_lists\s*=\s*(\[.*?\])'
    milestone = feature_dict['Milestone'].lower()
    whats_new_resources_build_file = os.path.join(
        BASE_DIR,
        '../ios/chrome/browser/whats_new/ui/data_source/resources/BUILD.gn')
    with open(whats_new_resources_build_file,
              'r+',
              encoding='utf-8',
              newline='') as file:
        original_file_content = file.read()
        match = re.search(screenshots_lists_regex, original_file_content,
                          re.DOTALL)
        assert match
        in_app_images = ast.literal_eval(match.group(1))
        in_app_images.append(os.path.join(milestone, feature_name + '.json'))
        in_app_images.append(
            os.path.join(milestone, feature_name + '_darkmode.json'))
        in_app_images_serialized = json.dumps(in_app_images)
        data = original_file_content.replace(match.group(1),
                                             in_app_images_serialized)
        file.seek(0)
        file.write(data)


def AddStrings(feature_dict: dict[str, str],
               path_to_milestone_folder: str) -> None:
    """Adds the feature strings.

  Args:
      feature_dict: Data for the new What's New feature.
      path_to_milestone_folder: Path to the milestone folder.
  """
    feature_name = feature_dict['Feature name']
    milestone = feature_dict['Milestone'].lower()
    strings_doc_file = os.path.join(path_to_milestone_folder, feature_name,
                                    'strings.docx')
    paragraphs_string_builder = []
    with open(strings_doc_file, 'rb') as file:
        read_data = Document(file)
        for paragraph in read_data.paragraphs:
            if paragraph.text:
                paragraphs_string_builder.append(paragraph.text)
    milestone_string_grd_file = os.path.join(
        BASE_DIR, '../ios/chrome/browser/whats_new/ui/strings/',
        milestone + '_strings.grdp')
    if not os.path.exists(milestone_string_grd_file):
        #Create new file and add to grd main
        with open(milestone_string_grd_file, 'w') as grd_file_handler:
            grd_content_builder = []
            grd_content_builder.append(
                '<?xml version="1.0" encoding="utf-8"?>')
            grd_content_builder.append('<grit-part>')
            grd_content_builder.append(' <!-- Milestone specific strings -->')
            grd_content_builder.extend(paragraphs_string_builder)
            grd_content_builder.append('</grit-part>')
            grd_file_handler.write('\n'.join(grd_content_builder))
        #open and add to main grd
        whats_new_strings_grd_file = os.path.join(
            BASE_DIR, '../ios/chrome/browser/whats_new/ui',
            'strings/ios_whats_new_strings.grd')
        with open(whats_new_strings_grd_file,
                  'r+',
                  encoding='utf-8',
                  newline='') as file:
            read_data = file.read()
            match_grdp_part = re.search(r'<part file="(.*?)_strings.grdp" />',
                                        read_data, re.DOTALL)
            assert match_grdp_part
            new_str_entry = '<part file="' + milestone + '_strings.grdp" />'
            data = read_data.replace(match_grdp_part.group(0), new_str_entry)
            file.seek(0)
            file.write(data)
    else:
        #search for '</grit-part>' and add above
        feature_strings_grd_file = os.path.join(
            BASE_DIR, '../ios/chrome/browser/whats_new/ui/strings/',
            milestone + '_strings.grdp')
        with open(feature_strings_grd_file, 'r+', encoding='utf-8',
                  newline='') as file:
            read_data = file.read()
            paragraphs_string_builder.append('</grit-part>')
            data = read_data.replace('</grit-part>',
                                     '\n'.join(paragraphs_string_builder))
            file.seek(0)
            file.write(data)


def UploadScreenshots(feature_dict: dict[str, str],
                      path_to_milestone_folder: str) -> None:
    """Upload the screenshots.

  Args:
      feature_dict: Data for the new What's New feature.
      path_to_milestone_folder: Path to the milestone folder.
  """
    titles = []
    milestone = feature_dict['Milestone'].lower()
    feature_name = feature_dict['Feature name']
    titles.append(feature_dict['Title'])
    titles.append(feature_dict['Subtitle'])
    feature_screenshot = feature_dict['Feature screenshot']
    titles.extend(StripWhitespacesAndEmptyLines(feature_dict['Instructions']))
    animation_texts_string = feature_dict['Animation texts'].splitlines()
    titles.extend("".join(StripWhitespacesAndEmptyLines(json.loads(a)['value'])) for a in animation_texts_string)
    screenshot_dir = os.path.join(
        BASE_DIR, '../ios/chrome/browser/whats_new/ui/strings',
        milestone + '_strings_grdp')
    os.makedirs(screenshot_dir, exist_ok=True)
    for title in titles:
        src_file = os.path.join(path_to_milestone_folder, feature_name,
                                feature_screenshot)
        new_dst_file = os.path.join(screenshot_dir, title + '.png')
        shutil.copyfile(src_file, new_dst_file)
    # Run screenshot uploader
    subprocess.run(['python3', 'tools/translation/upload_screenshots.py'],
                   capture_output=True,
                   text=True,
                   input='Y')
    # Remove the pngs
    for title in titles:
        os.remove(os.path.join(screenshot_dir, title + '.png'))


def UpdateWhatsNewFETEvent(milestone: str) -> None:
    """Updates ios_promo_feature_configuration.cc and event_constants.cc
       with the new what's new fet event name.

  Args:
      milestone: What's New milestone.
  """
    whats_new_fet_event_regex = r'"viewed_whats_new_(.*?)"'
    new_event_str = '"viewed_whats_new_' + milestone + '"'
    event_constants_file = os.path.join(
        BASE_DIR, '../components/feature_engagement/public/event_constants.cc')
    with open(event_constants_file, 'r+', encoding='utf-8',
              newline='') as file:
        file_content = file.read()
        match_whats_new_fet_event = re.search(whats_new_fet_event_regex,
                                              file_content, re.DOTALL)
        assert match_whats_new_fet_event
        new_file_content = file_content.replace(
            match_whats_new_fet_event.group(0), new_event_str)
        file.seek(0)
        file.write(new_file_content)


def RemoveStringsForMilestone(milestone: str) -> None:
    """Removes the strings for a specific milestone.

      Args:
        milestone: milestone for which the strings will be removed.
    """
    whats_new_strings_grd_file = os.path.join(
        BASE_DIR, '../ios/chrome/browser/whats_new/ui',
        'strings/ios_whats_new_strings.grd')
    with open(whats_new_strings_grd_file, 'r+', encoding='utf-8',
              newline='') as file:
        read_data = file.read()
        match_grdp_part = re.search(r'<part file="(.*?)_strings.grdp" />',
                                    read_data, re.DOTALL)
        assert match_grdp_part
        if match_grdp_part.group(1) == milestone:
            raise AssertionError(
                'The strings are still being referenced in '
                'ios_whats_new_strings.grd. Please upload new features '
                'to What\s New before removing strings and assets. '
                'Please see "tools/whats_new/upload_whats_new_features" '
                'for more information.')
    try:
        screenshot_milestone_dir = os.path.join(
            BASE_DIR, '../ios/chrome/browser/whats_new/ui/strings',
            milestone + '_strings_grdp')
        shutil.rmtree(screenshot_milestone_dir)
    except:
        print('The strings grdp directory for this milestone has already '
              'been removed.')
    try:
        strings_file = os.path.join(
            BASE_DIR, '../ios/chrome/browser/whats_new/ui/strings',
            milestone + '_strings.grdp')
        os.remove(strings_file)
    except:
        print(
            'The strings grdp file for this milestone has already been removed.'
        )


def RemoveAnimationAssetsForMilestone(milestone: str) -> None:
    """Removes the animation assets for a specific milestone.

      Args:
        milestone: milestone for which the animations will be removed.
    """
    try:
        whats_new_milestone_resource_dir = os.path.join(
            BASE_DIR,
            '../ios/chrome/browser/whats_new/ui/data_source/resources',
            milestone)
        shutil.rmtree(whats_new_milestone_resource_dir)
    except:
        print('The animation assets for this milestone have already'
              'been removed.')
    screenshots_lists_regex = r'screenshots_lists\s*=\s*(\[.*?\])'
    whats_new_resources_build_file = os.path.join(
        BASE_DIR,
        '../ios/chrome/browser/whats_new/ui/data_source/resources/BUILD.gn')
    with open(whats_new_resources_build_file,
              'r+',
              encoding='utf-8',
              newline='') as file:
        original_file_content = file.read()
        match = re.search(screenshots_lists_regex, original_file_content,
                          re.DOTALL)
        assert match
        in_app_images = ast.literal_eval(match.group(1))
        regex = re.compile(rf'{milestone}/(.*?)')
        filtered = [i for i in in_app_images if not regex.match(i)]
        in_app_images_serialized = json.dumps(filtered)
        data = original_file_content.replace(match.group(1),
                                             in_app_images_serialized)
        file.seek(0)
        file.write(data)
        file.truncate()


def LoadValidIconNames() -> ValidIcons:
    """Loads the valid icon names from the src code.

      Returns:
        ValidIcons instance representing all the different valid icon names
    """
    valid_custom_icons = set()
    valid_default_icons = set()

    icon_name_regex = re.compile(r"@\"(.*)\";")

    valid_icons_file = os.path.join(BASE_DIR, '..', CHROME_ICON_FILENAME)
    found_default_icons = False
    with open(valid_icons_file, 'r', encoding='utf-8', newline='') as file:
        # The icons file starts with custom icons until reaching the comment
        # "// Default symbol names."
        for line in file:
            if "// Default symbol names." in line:
                found_default_icons = True
                continue
            match = icon_name_regex.search(line)
            if not match:
                continue

            icon_name = match.group(1)
            if found_default_icons:
                valid_default_icons.add(icon_name)
            else:
                valid_custom_icons.add(icon_name)

    # Open SF Symbols.app to find all possible icon names.
    sf_symbols_path = os.path.join(
        DEFAULT_SF_SYMBOLS_PATH,
        'Contents/Resources/Metadata/name_availability.plist')

    if os.path.exists(sf_symbols_path):
        with open(sf_symbols_path, 'rb') as file:
            sf_symbols_data = plistlib.load(file)
        all_icons = set(sf_symbols_data['symbols'].keys())
    else:
        print('Missing SF Symbols.app in /Applications')
        all_icons = set()

    return ValidIcons(valid_custom_icons, valid_default_icons, all_icons)


def ValidateWhatsNewData(feature_dict: dict[str, str],
                         valid_icons: ValidIcons) -> str:
    """Validates the provided data represents a valid What's New feature.

      Args:
        feature_dict: Data for the new What's New feature
        valid_icons: A ValidIcons instance representing which icons are valid
      Returns:
        An empty string if the data is valid
    """
    icon_name = feature_dict['Icon name']
    if feature_dict['Icon Type'] == 'Custom':
        if icon_name not in valid_icons.valid_custom:
            return f'Invalid Custom icon name: {icon_name}'
    elif icon_name not in valid_icons.valid_default:
        if icon_name not in valid_icons.full_default:
            return f'Invalid Default icon name: {icon_name}'
        else:
            return (f'Default icon {icon_name} not present '
                    f'in //{CHROME_ICON_FILENAME}')

    return ""

def StripWhitespacesAndEmptyLines(text: str) -> list[str]:
    """Strips all whitespace and empty lines from text including whitespace
    between words.

    Args:
        text: The raw string.

    Returns:
        A formatted string with no whitespace or empty lines.
    """
    lines = text.splitlines()
    return ["".join(line.split()) for line in lines if line.strip()]


def ExtractTextLayerIDs(lottie_json: dict) -> set[str]:
    """Extracts all text layer IDs from a Lottie JSON file.

    Args:
        lottie_json: Parsed Lottie JSON data

    Returns:
        Set of text layer IDs found in the Lottie JSON
    """
    text_layer_ids = set()

    def process_layers(layers: list[dict]):
        for layer in layers:
            if layer.get('ty') == 5:  # Type 5 represents text layers
                if 'nm' in layer:  # 'nm' is the name/ID field
                    text_layer_ids.add(layer['nm'])
            if 'layers' in layer:
                process_layers(layer['layers'])

    if 'layers' in lottie_json:
        process_layers(lottie_json['layers'])

    return text_layer_ids


def ValidateLottieTextLayers(feature_dict: dict[str, str],
                             path_to_milestone_folder: str) -> None:
    """Validates that all animation text keys correspond to text layers in
    Lottie files.

    Args:
        feature_dict: Data for the new What's New feature
        path_to_milestone_folder: Path to the milestone folder

    Raises:
        AssertionError: if validation fails
    """
    feature_name = feature_dict['Feature name']
    animation_name = feature_dict['Animation']
    animation_name_darkmode = feature_dict['Animation darkmode']

    # Validate that the animation names are not empty
    if not animation_name or not animation_name_darkmode:
        raise AssertionError(
            "Animation and Animation darkmode names cannot be empty")

    # Get the provided animation text keys
    animation_texts = feature_dict['Animation texts'].splitlines()
    provided_keys = set()
    for text in animation_texts:
        try:
            animation_text = json.loads(text)
            if 'key' in animation_text:
                provided_keys.add(animation_text['key'])
            else:
                # Handle case where 'key' is missing
                raise AssertionError(
                    f"Missing 'key' in animation text: {text}")
        except json.JSONDecodeError as e:
            raise AssertionError(
                f"Invalid JSON format in animation texts: {text}") from e

    # Read and validate both light and dark mode Lottie files
    lottie_files = [(os.path.join(path_to_milestone_folder, feature_name,
                                  animation_name), "light mode"),
                    (os.path.join(path_to_milestone_folder, feature_name,
                                  animation_name_darkmode), "dark mode")]

    for lottie_file_path, mode in lottie_files:
        try:
            with open(lottie_file_path, 'r', encoding='utf-8') as file:
                lottie_data = json.load(file)
        except FileNotFoundError as e:
            raise AssertionError(
                f"Could not find {mode} Lottie file: {lottie_file_path}"
            ) from e
        except json.JSONDecodeError as e:
            raise AssertionError(
                f"Invalid JSON in {mode} Lottie file: {lottie_file_path}"
            ) from e

        # Extract text layer IDs from the Lottie file
        text_layer_ids = ExtractTextLayerIDs(lottie_data)

        # Validate that all provided keys exist in the Lottie file
        missing_keys = provided_keys - text_layer_ids
        if missing_keys:
            raise AssertionError(
                f"The following animation text keys were not found as text layers in the {mode} "
                f"Lottie file: {', '.join(missing_keys)}")

        # Warn about text layers without corresponding keys
        unused_layers = text_layer_ids - provided_keys
        if unused_layers:
            print(
                f"Warning: The following text layers in the {mode} Lottie file don't have corresponding animation texts: {', '.join(unused_layers)}"
            )
