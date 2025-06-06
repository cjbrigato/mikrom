// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/cssom_utils.h"

#include "third_party/blink/renderer/core/css/css_custom_ident_value.h"
#include "third_party/blink/renderer/core/css/css_grid_template_areas_value.h"
#include "third_party/blink/renderer/core/css/css_identifier_value.h"
#include "third_party/blink/renderer/core/css/css_string_value.h"
#include "third_party/blink/renderer/core/css/css_value_list.h"

namespace blink {

// static
bool CSSOMUtils::IncludeDependentGridLineEndValue(const CSSValue* line_start,
                                                  const CSSValue* line_end) {
  const bool line_end_is_initial_value =
      IsA<CSSIdentifierValue>(line_end) &&
      To<CSSIdentifierValue>(line_end)->GetValueID() == CSSValueID::kAuto;

  // "When grid-column-start is omitted, if grid-row-start is a <custom-ident>,
  // all four longhands are set to that value. Otherwise, it is set to auto.
  // When grid-row-end is omitted, if grid-row-start is a <custom-ident>,
  // grid-row-end is set to that <custom-ident>; otherwise, it is set to auto.
  // When grid-column-end is omitted, if grid-column-start is a <custom-ident>,
  // grid-column-end is set to that <custom-ident>; otherwise, it is set to
  // auto."
  //
  // https://www.w3.org/TR/css-grid-2/#placement-shorthands
  //
  // In order to produce a shortest-possible-serialization, we need essentially
  // the converse of that statement, as parsing handles the
  // literal interpretation. In particular, `CSSValueList` values (integer
  // literals) are always included, duplicate `custom-ident` values get
  // dropped, as well as initial values if they match the equivalent
  // `line_start` value.
  return IsA<CSSValueList>(line_end) ||
         ((*line_end != *line_start) &&
          (IsA<CSSCustomIdentValue>(line_start) || !line_end_is_initial_value));
}

// static
bool CSSOMUtils::IsAutoValue(const CSSValue* value) {
  return IsA<CSSIdentifierValue>(value) &&
         To<CSSIdentifierValue>(value)->GetValueID() == CSSValueID::kAuto;
}

// static
bool CSSOMUtils::IsNoneValue(const CSSValue* value) {
  return IsA<CSSIdentifierValue>(value) &&
         To<CSSIdentifierValue>(value)->GetValueID() == CSSValueID::kNone;
}

// static
bool CSSOMUtils::IsEmptyValueList(const CSSValue* value) {
  const CSSValueList* value_list = DynamicTo<CSSValueList>(value);
  return value_list && value_list->length() == 0;
}

// static
bool CSSOMUtils::HasGridRepeatValue(const CSSValueList* value_list) {
  if (value_list) {
    for (const auto& value : *value_list) {
      if (value->IsGridRepeatValue()) {
        return true;
      }
    }
  }
  return false;
}

// static
String CSSOMUtils::NamedGridAreaTextForPosition(
    const NamedGridAreaMap& grid_area_map,
    wtf_size_t row,
    wtf_size_t column) {
  for (const auto& item : grid_area_map) {
    const GridArea& area = item.value;
    if (row >= area.rows.StartLine() && row < area.rows.EndLine() &&
        column >= area.columns.StartLine() && column < area.columns.EndLine()) {
      return item.key;
    }
  }
  return ".";
}

// static
CSSValueList* CSSOMUtils::ComputedValueForGridTemplateShorthand(
    const CSSValue* template_row_values,
    const CSSValue* template_column_values,
    const CSSValue* template_area_values) {
  const bool has_initial_template_rows = IsNoneValue(template_row_values);
  const bool has_initial_template_columns = IsNoneValue(template_column_values);
  const bool has_initial_template_areas =
      !template_area_values || IsNoneValue(template_area_values);

  CSSValueList* list = CSSValueList::CreateSlashSeparated();

  // 1- 'none' case.
  if (has_initial_template_areas && has_initial_template_rows &&
      has_initial_template_columns) {
    list->Append(*template_row_values);
    return list;
  }

  // It is invalid to specify `grid-template-areas` without
  // `grid-template-rows`.
  if (!has_initial_template_areas && has_initial_template_rows) {
    return list;
  }

  // 2- <grid-template-rows> / <grid-template-columns>
  if (!IsA<CSSValueList>(template_row_values) || has_initial_template_areas) {
    list->Append(*template_row_values);
    list->Append(*template_column_values);

    return list;
  }

  // 3- [ <line-names>? <string> <track-size>? <line-names>? ]+
  // [ / <track-list> ]?
  //
  // "Note that the repeat() function isn’t allowed in these track listings, as
  // the tracks are intended to visually line up one-to-one with the
  // rows/columns in the “ASCII art”."
  //
  // https://www.w3.org/TR/css-grid-2/#explicit-grid-shorthand
  const CSSValueList* template_row_value_list =
      DynamicTo<CSSValueList>(template_row_values);
  DCHECK(template_row_value_list);
  if (HasGridRepeatValue(template_row_value_list) ||
      HasGridRepeatValue(DynamicTo<CSSValueList>(template_column_values))) {
    return list;
  }

  // In this serialization, there must be a value for grid-areas.
  const cssvalue::CSSGridTemplateAreasValue* template_areas =
      DynamicTo<cssvalue::CSSGridTemplateAreasValue>(template_area_values);
  DCHECK(template_areas);
  const NamedGridAreaMap& grid_area_map = template_areas->GridAreaMap();
  const wtf_size_t grid_area_column_count = template_areas->ColumnCount();

  // Handle [ <line-names>? <string> <track-size>? <line-names>? ]+
  CSSValueList* template_row_list = CSSValueList::CreateSpaceSeparated();
  wtf_size_t row = 0;
  for (const auto& row_value : *template_row_value_list) {
    if (row_value->IsGridLineNamesValue()) {
      template_row_list->Append(*row_value);
      continue;
    }
    StringBuilder grid_area_text;
    for (wtf_size_t column = 0; column < grid_area_column_count; ++column) {
      grid_area_text.Append(
          NamedGridAreaTextForPosition(grid_area_map, row, column));
      if (column != grid_area_column_count - 1) {
        grid_area_text.Append(' ');
      }
    }
    DCHECK(!grid_area_text.empty());
    template_row_list->Append(
        *MakeGarbageCollected<CSSStringValue>(grid_area_text.ReleaseString()));
    ++row;

    // Omit `auto` values.
    if (!IsAutoValue(row_value.Get())) {
      template_row_list->Append(*row_value);
    }
  }

  // If the actual number of rows serialized via `grid-template-rows` doesn't
  // match the rows defined via grid-areas, the shorthand cannot be serialized
  // and we must return the empty string.
  if (row != template_areas->RowCount()) {
    return list;
  }

  list->Append(*template_row_list);

  // Handle [ / <track-list> ]?
  if (!has_initial_template_columns) {
    list->Append(*template_column_values);
  }

  return list;
}

}  // namespace blink
