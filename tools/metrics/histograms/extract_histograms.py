# Copyright 2013 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Extract histogram names from the description XML file.

For more information on the format of the XML file, which is self-documenting,
see histograms.xml; however, here is a simple example to get you started. The
XML below will generate the following five histograms:

    HistogramTime
    HistogramEnum
    HistogramEnum_Chrome
    HistogramEnum_IE
    HistogramEnum_Firefox

<histogram-configuration>

<histograms>

<histogram name="HistogramTime" units="milliseconds">
  <owner>person@chromium.org</owner>
  <owner>some-team@chromium.org</owner>
  <summary>A brief description.</summary>
</histogram>

<histogram name="HistogramEnum" enum="MyEnumType">
  <owner>person@chromium.org</owner>
  <summary>This histogram sports an enum value type.</summary>
</histogram>

</histograms>

<enums>

<enum name="MyEnumType">
  <summary>This is an example enum type, where the values mean little.</summary>
  <int value="1" label="FIRST_VALUE">This is the first value.</int>
  <int value="2" label="SECOND_VALUE">This is the second value.</int>
</enum>

</enums>

<histogram_suffixes_list>

<histogram_suffixes name="BrowserType" separator="_">
  <suffix name="Chrome"/>
  <suffix name="IE"/>
  <suffix name="Firefox"/>
  <affected-histogram name="HistogramEnum"/>
</histogram_suffixes>

</histogram_suffixes_list>

</histogram-configuration>
"""

import bisect
import copy
import datetime
import itertools

try:
  import HTMLParser
  html = HTMLParser.HTMLParser()
except ImportError:  # For Py3 compatibility
  import html

import logging
import re
import xml.dom.minidom

import histogram_configuration_model
import xml_utils

BASIC_EMAIL_REGEXP = r'^[\w\-\+\%\.]+\@[\w\-\+\%\.]+$'

MAX_HISTOGRAM_SUFFIX_DEPENDENCY_DEPTH = 5

EXPIRY_DATE_PATTERN = "%Y-%m-%d"
EXPIRY_MILESTONE_RE = re.compile(r'M[0-9]{2,3}\Z')


class Error(Exception):
  pass


class ExtractionErrors(list[str]):
  """A list of error strings, with new entries also logged."""

  def AppendAndLog(self, error: str) -> None:
    """Appends an error to the list after logging it."""
    logging.error(error)
    self.append(error)


def _ExpandHistogramNameWithSuffixes(suffix_name, histogram_name,
                                     histogram_suffixes_node):
  """Creates a new histogram name based on a histogram suffix.

  Args:
    suffix_name: The suffix string to apply to the histogram name. May be empty.
    histogram_name: The name of the histogram. May be of the form
      Group.BaseName or BaseName.
    histogram_suffixes_node: The histogram_suffixes XML node.

  Returns:
    A tuple with:
      * A string with the expanded histogram name.
      * Any errors accumulated during this process.
  """
  errors = ExtractionErrors()

  if histogram_suffixes_node.hasAttribute('separator'):
    separator = histogram_suffixes_node.getAttribute('separator')
  else:
    separator = '_'

  if histogram_suffixes_node.hasAttribute('ordering'):
    ordering = histogram_suffixes_node.getAttribute('ordering')
  else:
    ordering = 'suffix'
  parts = ordering.split(',')
  ordering = parts[0]
  if len(parts) > 1:
    placement = int(parts[1])
  else:
    placement = 1
  if ordering not in ['prefix', 'suffix']:
    errors.AppendAndLog(
        f'ordering needs to be prefix or suffix, value is {ordering}')
    return None, errors

  if not suffix_name:
    return histogram_name, errors

  if ordering == 'suffix':
    return histogram_name + separator + suffix_name, errors

  # For prefixes, the suffix_name is inserted between the "cluster" and the
  # "remainder", e.g. Foo.BarHist expanded with gamma becomes Foo.gamma_BarHist.
  sections = histogram_name.split('.')
  if len(sections) <= placement:
    suffixes_name = histogram_suffixes_node.getAttribute('name')
    errors.AppendAndLog(
        f'Prefix histogram_suffixes expansions require histogram names which '
        f'include a dot separator. Histogram name is {histogram_name}, '
        f'histogram_suffixes is {suffixes_name}, and placment is {placement}')
    return None, errors

  cluster = '.'.join(sections[0:placement]) + '.'
  remainder = '.'.join(sections[placement:])
  return cluster + suffix_name + separator + remainder, errors


def ExtractEnumsFromXmlTree(tree):
  """Extracts all <enum> nodes in the tree into a dictionary.

  Args:
    tree: The XML dom tree.

  Returns:
    A tuple with:
      * A mapping of name -> enum metadata proto.
      * Any errors accumulated during extraction.

    An enum metadata dictionary looks like this:
    {
      'name': string
      'values': {
        (int cast to string) : {
          'summary': string,
          'label': string
        }
        ...
      }
      ...
    },
  """
  enums = {}
  errors = ExtractionErrors()

  for enum in xml_utils.IterElementsWithTag(tree, 'enum'):
    name = enum.getAttribute('name')
    if name in enums:
      errors.AppendAndLog(f'Duplicate enum {name}')
      continue

    enum_dict = {}
    enum_dict['name'] = name
    enum_dict['values'] = {}
    labels = set()

    nodes = list(xml_utils.IterElementsWithTag(enum, 'int'))

    obsolete_nodes = list(xml_utils.IterElementsWithTag(enum, 'obsolete', 1))
    if not nodes and not obsolete_nodes:
      errors.AppendAndLog(
          f'Non-obsolete enum {name} should have at least one <int>')
      continue

    for int_tag in nodes:
      value_dict = {}
      int_value = int(int_tag.getAttribute('value'))
      if int_value in enum_dict['values']:
        errors.AppendAndLog(f'Duplicate enum value {int_value} for enum {name}')
        continue
      label = int_tag.getAttribute('label')
      if label in labels:
        errors.AppendAndLog(f'Duplicate enum label "{label}" for enum {name}')
        continue
      labels.add(label)
      value_dict['label'] = label
      value_dict['summary'] = xml_utils.GetTextFromChildNodes(int_tag)
      enum_dict['values'][int_value] = value_dict

    enum_int_values = sorted(enum_dict['values'].keys())

    last_int_value = None
    for int_tag in nodes:
      int_value = int(int_tag.getAttribute('value'))
      if last_int_value is not None and int_value < last_int_value:
        errors.AppendAndLog(f'Enum {name} int values {last_int_value} and '
                            f'{int_value} are not in numerical order')
        left_item_index = bisect.bisect_left(enum_int_values, int_value)
        if left_item_index == 0:
          logging.warning('Insert value %d at the beginning', int_value)
        else:
          left_int_value = enum_int_values[left_item_index - 1]
          left_label = enum_dict['values'][left_int_value]['label']
          logging.warning('Insert value %d after %d ("%s")', int_value,
                          left_int_value, left_label)
      else:
        last_int_value = int_value

    for summary in xml_utils.IterElementsWithTag(enum, 'summary'):
      enum_dict['summary'] = xml_utils.GetTextFromChildNodes(summary)
      break

    enums[name] = enum_dict

  return enums, errors


def _ExtractOwners(node):
  """Extracts owners information from the given node, if exists.

  Args:
    node: A DOM Element.

  Returns:
    A tuple of owner-related info, e.g. (['alice@chromium.org'], True)

    The first element is a list of the owners' email addresses. The second
    element is a boolean indicating whether the node has an owner.
  """
  email_pattern = re.compile(BASIC_EMAIL_REGEXP)
  owners = []
  has_owner = False

  for owner_node in xml_utils.IterElementsWithTag(node, 'owner', 1):
    child = owner_node.firstChild
    owner_text = (child and child.nodeValue) or ''
    if email_pattern.match(owner_text):
      has_owner = True
      owners.append(owner_text)

  return owners, has_owner


def _ExtractImprovementDirection(histogram_node):
  """Extracts improvement direction from the given histogram element, if any.

  Args:
    histogram_node: A DOM Element corresponding to a histogram.

  Returns:
    A tuple, where the first element is the improvement direction, if any;
    the second element is an error message if the given direction is invalid.
  """
  errors = ExtractionErrors()
  improvement_nodes = histogram_node.getElementsByTagName('improvement')
  if not improvement_nodes:
    return None, errors
  if len(improvement_nodes) > 1:
    histogram_name = histogram_node.getAttribute('name')
    errors.AppendAndLog(
        f'Histogram "{histogram_name}" has multiple <improvement> tags.')
    return None, errors

  improvement_node = improvement_nodes[0]
  direction = improvement_node.getAttribute('direction')
  if (direction not in
      histogram_configuration_model.IMPROVEMENT_DIRECTION_VALID_VALUES):
    histogram_name = histogram_node.getAttribute('name')
    errors.AppendAndLog(
        f'Histogram "{histogram_name}" has an invalid direction '
        f'"{direction}" in its <improvement> tag.')
    return None, errors

  return direction, errors


def _ExtractComponents(histogram):
  """Extracts component information from the given histogram element.

  Components are present when a histogram has a component tag, e.g.
  <component>UI&gt;Browser</component>. Components may also be present when an
  OWNERS file is given as a histogram owner, e.g. <owner>src/dir/OWNERS</owner>;
  in this case the component is extracted from adjacent DIR_METADATA files.
  See _ExtractComponentViaDirmd() in the following file for details:
  chromium/src/tools/metrics/histograms/expand_owners.py.

  Args:
    histogram: A DOM Element corresponding to a histogram.

  Returns:
    A list of the components associated with the histogram, e.g.
    ['UI>Browser>Spellcheck'].
  """
  component_nodes = histogram.getElementsByTagName('component')
  return [
      xml_utils.GetTextFromChildNodes(component_node)
      for component_node in component_nodes
  ]


def _ValidateDateString(date_str):
  """Checks if |date_str| matches 'YYYY-MM-DD'.

  Args:
    date_str: string

  Returns:
    True iff |date_str| matches 'YYYY-MM-DD' format.
  """
  try:
    _ = datetime.datetime.strptime(date_str, EXPIRY_DATE_PATTERN).date()
  except ValueError:
    return False
  return True

def _ValidateMilestoneString(milestone_str):
  """Check if |milestone_str| matches 'M*'."""
  return EXPIRY_MILESTONE_RE.match(milestone_str) is not None

def _ProcessBaseHistogramAttribute(node, histogram_entry):
  if node.hasAttribute('base'):
    is_base = node.getAttribute('base').lower() == 'true'
    histogram_entry['base'] = is_base

# The following code represents several concepts as JSON objects
#
# Token: an analog of <token> tag, represented as a JSON object like:
# {
#   'key': 'token_key',
#   'variants': [a list of Variant objects]
# }
#
# Variant: an analog of <variant> tag, represented as a JSON object like:
# {
#   'name': 'variant_name',
#   'summary': 'variant_summary',
#   'obsolete': 'Obsolete text.',
#   'owners': ['me@chromium.org', 'you@chromium.org']
# }
#
# Variants: an analog of <variants> tag, represented as a JSON object like:
# {
#   'name: 'variants_name',
#   'variants': [a list of Variant objects]
# }


def ExtractTokens(histogram, variants_dict):
  """Extracts tokens and variants from the given histogram element.

  Args:
    histogram: A DOM Element corresponding to a histogram.
    variants_dict: A dictionary of variants extracted from the tree.

  Returns:
    A tuple where the first element is a list of extracted Tokens, and the
        second indicates if any errors were detected while extracting them.
  """
  tokens_seen = set()
  tokens = []
  errors = ExtractionErrors()
  histogram_name = histogram.getAttribute('name')

  for token_node in xml_utils.IterElementsWithTag(histogram, 'token', 1):
    token_key = token_node.getAttribute('key')
    if token_key in tokens_seen:
      errors.AppendAndLog(
          f'Histogram {histogram_name} contains duplicate token key '
          f'{token_key}, please ensure token keys are unique.')
      continue
    tokens_seen.add(token_key)

    token_key_format = '{' + token_key + '}'
    if token_key_format not in histogram_name:
      errors.AppendAndLog(
          f'Histogram {histogram_name} includes a token tag but the token key '
          f'is not present in histogram name. Please insert the token key into '
          f'the histogram name in order for the token to be added.')
      continue

    token = dict(key=token_key)
    token['variants'] = []

    # If 'variants' attribute is set for the <token>, get the list of Variant
    # objects from from the |variants_dict|. Else, extract the <variant>
    # children nodes of the |token_node| as a list of Variant objects.
    if token_node.hasAttribute('variants'):
      variants_name = token_node.getAttribute('variants')
      variant_list = variants_dict.get(variants_name)
      if variant_list:
        token['variants'] = variant_list[:]
      else:
        errors.AppendAndLog(
            f'The variants attribute {variants_name} of token key {token_key} '
            f'of histogram {histogram_name} does not have a corresponding '
            f'<variants> tag.')
        token['variants'] = []
    # Inline and out-of-line variants can be combined.
    token['variants'].extend(_ExtractVariantNodes(token_node))

    tokens.append(token)

  # A histogram name may also reference external tokens implicitly, when the
  # name includes patterns (e.g. Foo.{Bar}) without a corresponding token tag.
  # These are treated as implicit reference to variants with the same name.
  tokens_in_name = re.findall(r'\{(.+?)\}', histogram.getAttribute('name'))
  for token_key in tokens_in_name:
    # If the key has been seen already, it means we've already added the
    # variants for the token to |tokens|, for example if it had an explicit
    # <token> tag.
    if token_key in tokens_seen:
      continue
    tokens_seen.add(token_key)
    variant_list = variants_dict.get(token_key)
    if not variant_list:
      errors.AppendAndLog(
          f'Could not find variant "{token_key}" specified by histogram'
          f' "{histogram_name}".')
      variant_list = []
    token = dict(key=token_key, variants=variant_list)
    tokens.append(token)

  return tokens, errors


def _ExtractVariantNodes(node):
  """Extracts the variants of a given node into a list of variant dictionaries.

  Args:
    node: A DOM element corresponding to <token> node

  Returns:
    A list of Variants.
  """
  variant_list = []
  for variant_node in xml_utils.IterElementsWithTag(node, 'variant', 1):
    name = variant_node.getAttribute('name')
    summary = variant_node.getAttribute('summary') if variant_node.hasAttribute(
        'summary') else name
    variant = dict(name=name, summary=summary)

    obsolete_text = _GetObsoleteReason(variant_node)
    if obsolete_text:
      variant['obsolete'] = obsolete_text

    owners, variant_has_owners = _ExtractOwners(variant_node)
    if variant_has_owners:
      variant['owners'] = owners

    variant_list.append(variant)

  return variant_list


def _ExtractHistogramsFromXmlTree(tree, enums):
  """Extracts all <histogram> nodes in the tree into a dictionary."""

  # Process the histograms. The descriptions can include HTML tags.
  histograms = {}
  variants_dict, errors = ExtractVariantsFromXmlTree(tree)

  for histogram in xml_utils.IterElementsWithTag(tree, 'histogram'):
    name = histogram.getAttribute('name')
    if name in histograms:
      errors.AppendAndLog(f'Duplicate histogram definition {name}')
      continue
    histograms[name] = histogram_entry = {}

    # Handle expiry attribute.
    if histogram.hasAttribute('expires_after'):
      expiry_str = histogram.getAttribute('expires_after')
      if (expiry_str == "never" or _ValidateMilestoneString(expiry_str) or
          _ValidateDateString(expiry_str)):
        histogram_entry['expires_after'] = expiry_str
      else:
        errors.AppendAndLog(
            f'Expiry of histogram {name} does not match expected date format '
            f'("{EXPIRY_DATE_PATTERN}"), milestone format (M*), or "never": '
            'found {expiry_str}')
    else:
      errors.AppendAndLog(f'Your histogram {name} must have an expiry date.')

    # Handle <owner> tags.
    owners, has_owner = _ExtractOwners(histogram)
    if owners:
      histogram_entry['owners'] = owners

    # Handle <improvement> tags.
    improvement_direction, improvement_errors = _ExtractImprovementDirection(
        histogram)
    errors.extend(improvement_errors)
    if improvement_direction:
      histogram_entry['improvement'] = improvement_direction

    # Find <component> tag.
    components = _ExtractComponents(histogram)
    if components:
      histogram_entry['components'] = components

    # Find <summary> tag.
    summary_nodes = list(xml_utils.IterElementsWithTag(histogram, 'summary'))

    if summary_nodes:
      histogram_entry['summary'] = xml_utils.GetTextFromChildNodes(
          summary_nodes[0])
    else:
      histogram_entry['summary'] = 'TBD'

    # Find <obsolete> tag.
    obsolete_nodes = list(
        xml_utils.IterElementsWithTag(histogram, 'obsolete', 1))
    if obsolete_nodes:
      reason = xml_utils.GetTextFromChildNodes(obsolete_nodes[0])
      histogram_entry['obsolete'] = reason

    # Non-obsolete histograms should provide a non-empty <summary>.
    if not obsolete_nodes and (not summary_nodes or
                               not histogram_entry['summary']):
      errors.AppendAndLog(f'histogram {name} should provide a <summary>')

    # Non-obsolete histograms should specify <owner>s.
    if not obsolete_nodes and not has_owner:
      errors.AppendAndLog(f'histogram {name} should specify <owner>s')

    # Histograms should have either units or enum.
    if (not histogram.hasAttribute('units') and
        not histogram.hasAttribute('enum')):
      errors.AppendAndLog(f'histogram {name} should have either units or enum')

    # Histograms should not have both units and enum.
    if (histogram.hasAttribute('units') and
        histogram.hasAttribute('enum')):
      errors.AppendAndLog(
          f'histogram {name} should not have both units and enum')

    # Handle units.
    if histogram.hasAttribute('units'):
      histogram_entry['units'] = histogram.getAttribute('units')

    # Handle enum types.
    if histogram.hasAttribute('enum'):
      enum_name = histogram.getAttribute('enum')
      if enum_name not in enums:
        errors.AppendAndLog(f'Unknown enum {enum_name} in histogram {name}')
      else:
        histogram_entry['enum'] = enums[enum_name]

    # Find <token> tag.
    tokens, token_errors = ExtractTokens(histogram, variants_dict)
    if tokens:
      histogram_entry['tokens'] = tokens
    errors.extend(token_errors)

    _ProcessBaseHistogramAttribute(histogram, histogram_entry)

  return histograms, errors


def ExtractVariantsFromXmlTree(tree):
  """Extracts all <variants> nodes in the tree into a dictionary.

  Args:
    tree: A DOM Element containing histograms and variants nodes.

  Returns:
    A tuple where the first element is a dictionary of extracted Variants, where
        the key is the variants name and the value is a list of Variant objects.
        The second element indicates if any errors were detected while
        extracting them.
  """
  variants_dict = {}
  errors = ExtractionErrors()
  for variants_node in xml_utils.IterElementsWithTag(tree, 'variants'):
    variants_name = variants_node.getAttribute('name')
    if variants_name in variants_dict:
      errors.AppendAndLog(f'Duplicate variants definition {variants_name}')
      continue

    variants_dict[variants_name] = _ExtractVariantNodes(variants_node)

  return variants_dict, errors


def _GetObsoleteReason(node):
  """If the node's histogram is obsolete, returns a string explanation.

  Otherwise, returns None.

  Args:
    node: A DOM Element associated with a histogram.
  """
  for child in node.childNodes:
    if child.localName == 'obsolete':
      # There can be at most 1 obsolete element per node.
      return xml_utils.GetTextFromChildNodes(child)
  return None


def _UpdateHistogramsWithSuffixes(tree, histograms) -> ExtractionErrors:
  """Processes <histogram_suffixes> tags and combines with affected histograms.

  The histograms dictionary will be updated in-place by adding new histograms
  created by combining histograms themselves with histogram_suffixes targeting
  these histograms.

  Args:
    tree: XML dom tree.
    histograms: a dictionary of histograms previously extracted from the tree;

  Returns:
    A list of error messages if any errors were found.
  """
  errors = ExtractionErrors()

  histogram_suffix_tag = 'histogram_suffixes'
  suffix_tag = 'suffix'
  with_tag = 'with-suffix'

  # histogram_suffixes can depend on other histogram_suffixes, so we need to be
  # careful. Make a temporary copy of the list of histogram_suffixes to use as a
  # queue. histogram_suffixes whose dependencies have not yet been processed
  # will get relegated to the back of the queue to be processed later.
  reprocess_queue = []

  def GenerateHistogramSuffixes():
    for f in xml_utils.IterElementsWithTag(tree, histogram_suffix_tag):
      yield 0, f
    for r, f in reprocess_queue:
      yield r, f

  for reprocess_count, histogram_suffixes in GenerateHistogramSuffixes():
    # Check dependencies first.
    dependencies_valid = True
    affected_histograms = list(
        xml_utils.IterElementsWithTag(histogram_suffixes, 'affected-histogram',
                                      1))
    for affected_histogram in affected_histograms:
      histogram_name = affected_histogram.getAttribute('name')
      if histogram_name not in histograms:
        # Base histogram is missing.
        dependencies_valid = False
        missing_dependency = histogram_name
        break
    if not dependencies_valid:
      if reprocess_count < MAX_HISTOGRAM_SUFFIX_DEPENDENCY_DEPTH:
        reprocess_queue.append((reprocess_count + 1, histogram_suffixes))
        continue
      else:
        suffixes_name = histogram_suffixes.getAttribute('name')
        errors.AppendAndLog(
            f'histogram_suffixes {suffixes_name} is missing its '
            f'dependency {missing_dependency}')
        continue

    # If the suffix group has an obsolete tag, all suffixes it generates inherit
    # its reason.
    group_obsolete_reason = _GetObsoleteReason(histogram_suffixes)

    name = histogram_suffixes.getAttribute('name')
    suffix_nodes = list(
        xml_utils.IterElementsWithTag(histogram_suffixes, suffix_tag, 1))
    suffix_labels = {}
    for suffix in suffix_nodes:
      suffix_name = suffix.getAttribute('name')
      if not suffix.hasAttribute('label'):
        errors.AppendAndLog(f'suffix {suffix_name} in histogram_suffixes '
                            f'{name} should have a label')
      suffix_labels[suffix_name] = suffix.getAttribute('label')
    # Find owners list under current histogram_suffixes tag.
    owners, _ = _ExtractOwners(histogram_suffixes)

    for affected_histogram in affected_histograms:
      with_suffixes = list(
          xml_utils.IterElementsWithTag(affected_histogram, with_tag, 1))
      if with_suffixes:
        suffixes_to_add = with_suffixes
      else:
        suffixes_to_add = suffix_nodes

      histogram_name = affected_histogram.getAttribute('name')
      for suffix in suffixes_to_add:
        suffix_name = suffix.getAttribute('name')
        new_histogram_name, expansion_errors = _ExpandHistogramNameWithSuffixes(
            suffix_name, histogram_name, histogram_suffixes)
        errors.extend(expansion_errors)
        if new_histogram_name is None:
          continue
        if new_histogram_name != histogram_name:
          new_histogram = copy.deepcopy(histograms[histogram_name])
          # Do not copy forward base histogram state to suffixed
          # histograms. Any suffixed histograms that wish to remain base
          # histograms must explicitly re-declare themselves as base
          # histograms.
          if new_histogram.get('base', False):
            del new_histogram['base']
          histograms[new_histogram_name] = new_histogram

        suffix_label = suffix_labels.get(suffix_name, '')

        histogram_entry = histograms[new_histogram_name]

        # If no owners are added for this histogram-suffixes, it inherits the
        # owners of its parents.
        if owners:
          histogram_entry['owners'] = owners

        # If a suffix has an obsolete node, it's marked as obsolete for the
        # specified reason, overwriting its group's obsoletion reason if the
        # group itself was obsolete as well.
        obsolete_reason = _GetObsoleteReason(suffix)
        if not obsolete_reason:
          obsolete_reason = _GetObsoleteReason(affected_histogram)
        if not obsolete_reason:
          obsolete_reason = group_obsolete_reason

        # If the suffix has an obsolete tag, all histograms it generates
        # inherit it.
        if obsolete_reason:
          histogram_entry['obsolete'] = obsolete_reason

        _ProcessBaseHistogramAttribute(suffix, histogram_entry)

  return errors


class TokenAssignment(object):
  """Assignment of a Variant for each Token of histogram pattern.

  Attributes:
    pairings: A token_name to Variant map.
  """

  def __init__(self, pairings):
    self.pairings = pairings


def _GetTokenAssignments(tokens):
  """Gets all possible TokenAssignments for the listed tokens.

  Args:
    tokens: The list of Tokens to create assignments for.

  Returns:
    A list of TokenAssignments.
  """
  token_keys = [token['key'] for token in tokens]
  token_variants = [token['variants'] for token in tokens]

  return [
      TokenAssignment(pairings=dict(zip(token_keys, selected_variants)))
      for selected_variants in itertools.product(*token_variants)
  ]


def _AddHistogramOrExpandedVariants(histogram_name, histograms_dict,
                                    new_histograms_dict) -> ExtractionErrors:
  """Adds histogram or all variant expanded histograms to |new_histograms_dict|.

  If the histogram does not reference any variants, it's added directly to the
  new histograms dict. Else, the tokens are expanded to produce all the variants
  of that histogram and these are added to the new histogram dict.

  Args:
    histogram_name: The name of the histogram.
    histograms_dict: The dictionary of all histograms extracted from the tree.
    new_histograms_dict: The dictionary of histograms to add to.

  Returns:
    List of errors, if any.
  """
  errors = ExtractionErrors()
  histogram_node = histograms_dict[histogram_name]

  if 'tokens' not in histogram_node:
    # If the histogram references no variants, simply copy it over.
    new_histograms_dict[histogram_name] = histogram_node
    return errors

  # |token_assignments| contains all the cross-product combinations of token
  # variants, representing all the possible histogram names that could be
  # generated.
  token_assignments = _GetTokenAssignments(histogram_node['tokens'])
  summary_text = histogram_node['summary']

  summary_errors = set()

  # Each |token_assignment| contains one of the cross-product combinations and
  # corresponds to one new generated histogram.
  for token_assignment in token_assignments:
    new_owners = []
    # Dictionaries of pairings used for string formatting of histogram name and
    # summary.
    token_name_pairings = {}
    token_summary_pairings = {}

    for token_key, variant in token_assignment.pairings.items():
      token_name_pairings[token_key] = variant['name']
      token_summary_pairings[token_key] = variant['summary']

      # If a variant has owner(s), append to |new_owners|, overwriting the
      # owners of the original histogram.
      if 'owners' in variant:
        new_owners += variant['owners']

    # Replace token in histogram name with variant name.
    new_histogram_name = histogram_name.format(**token_name_pairings)
    if new_histogram_name in new_histograms_dict:
      errors.AppendAndLog(
          f'Duplicate histogram name {new_histogram_name} generated.'
          f'Please remove identical variants in different tokens in '
          f'{histogram_name}.')
      continue

    # Replace token in summary with variant summary.
    try:
      new_summary_text = summary_text.format(**token_summary_pairings)
    except:
      if histogram_name not in summary_errors:
        summary_errors.add(histogram_name)
        errors.AppendAndLog(
            "Could not format summary text when expanding histogram %s. Please "
            "check that it's not using {Token} syntax for unknown tokens." %
            (histogram_name))
      continue

    new_histogram_node = dict(histogram_node, summary=new_summary_text)
    # Do not copy the <token> nodes to the generated histograms.
    del new_histogram_node['tokens']

    if new_owners:
      new_histogram_node['owners'] = new_owners

    new_histograms_dict[new_histogram_name] = new_histogram_node

  return errors


def _UpdateHistogramsWithTokens(histograms_dict):
  """Processes histograms and combines with variants of tokens.

  Args:
    histograms_dict: A dictionary of all the histograms extracted from the tree.

  Returns:
    A tuple where the first element is the replacement histograms dictionary,
    containing the original histograms without tokens and histograms whose
    tokens are replaced by newly variant combinations. The second element is a
    list of errors detected while extracting them.
  """
  errors = ExtractionErrors()
  # Create new dict instead of modify in place because newly generated
  # histograms will be added when iterating through |histograms_dict|.
  new_histograms_dict = {}
  for histogram_name, histogram_node in histograms_dict.items():
    errors.extend(
        _AddHistogramOrExpandedVariants(histogram_name, histograms_dict,
                                        new_histograms_dict))

  return new_histograms_dict, errors


def ExtractHistogramsFromDom(tree):
  """Computes the histogram names and descriptions from the XML representation.

  Args:
    tree: A DOM tree of XML content.

  Returns:
    a tuple of (histograms, errors) where histograms is a dictionary mapping
    histogram names to dictionaries containing histogram descriptions and
    errors is a list of errors encountered in processing, if any.
  """
  xml_utils.NormalizeAllAttributeValues(tree)

  enums_tree = xml_utils.GetTagSubTree(tree, 'enums', 2)
  histograms_tree = xml_utils.GetTagSubTree(tree, 'histograms', 2)
  histogram_suffixes_tree = xml_utils.GetTagSubTree(tree,
                                                    'histogram_suffixes_list',
                                                    2)
  enums, enum_errors = ExtractEnumsFromXmlTree(enums_tree)
  histograms, histogram_errors = _ExtractHistogramsFromXmlTree(
      histograms_tree, enums)
  histograms, update_token_errors = _UpdateHistogramsWithTokens(histograms)
  # Only expand expand suffixes if there were no token errors.
  if not update_token_errors:
    update_suffix_errors = _UpdateHistogramsWithSuffixes(
        histogram_suffixes_tree, histograms)
  else:
    update_suffix_errors = ExtractionErrors()
  errors = ExtractionErrors([
      *enum_errors,
      *histogram_errors,
      *update_token_errors,
      *update_suffix_errors,
  ])

  return histograms, errors

def ExtractHistograms(filename):
  """Loads histogram definitions from a disk file.

  Args:
    filename: a file path to load data from.

  Returns:
    a dictionary of histogram descriptions.

  Raises:
    Error: if the file is not well-formatted.
  """
  with open(filename, 'r') as f:
    tree = xml.dom.minidom.parse(f)
    histograms, errors = ExtractHistogramsFromDom(tree)
    if errors:
      logging.error('Error parsing %s', filename)
      raise Error()
    return histograms


def ExtractNames(histograms):
  return sorted(histograms.keys())
