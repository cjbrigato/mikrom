// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "printing/backend/mojom/print_backend_mojom_traits.h"

#include <set>

#include "base/containers/contains.h"
#include "base/debug/crash_logging.h"
#include "base/debug/dump_without_crashing.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "build/build_config.h"
#include "ui/gfx/geometry/mojom/geometry.mojom-shared.h"
#include "ui/gfx/geometry/mojom/geometry_mojom_traits.h"

// Implementations of std::less<> here are for purposes of detecting duplicate
// entries in arrays.  They do not require strict checks of all fields, but
// instead focus on identifying attributes that would be used to clearly
// distinguish properties to a user.  E.g., if two entries have the same
// displayable name but different corresponding values, consider that to be a
// duplicate for these purposes.
namespace std {

template <>
struct less<::gfx::Size> {
  bool operator()(const ::gfx::Size& lhs, const ::gfx::Size& rhs) const {
    if (lhs.width() < rhs.width())
      return true;
    return lhs.height() < rhs.height();
  }
};

template <>
struct less<::printing::PrinterSemanticCapsAndDefaults::Paper> {
  bool operator()(
      const ::printing::PrinterSemanticCapsAndDefaults::Paper& lhs,
      const ::printing::PrinterSemanticCapsAndDefaults::Paper& rhs) const {
    if (lhs.display_name() < rhs.display_name()) {
      return true;
    }
    return lhs.vendor_id() < rhs.vendor_id();
  }
};

#if BUILDFLAG(IS_CHROMEOS)
template <>
struct less<::printing::AdvancedCapability> {
  bool operator()(const ::printing::AdvancedCapability& lhs,
                  const ::printing::AdvancedCapability& rhs) const {
    if (lhs.name < rhs.name)
      return true;
    return lhs.display_name < rhs.display_name;
  }
};
#endif  // BUILDFLAG(IS_CHROMEOS)

}  // namespace std

namespace mojo {

#if BUILDFLAG(IS_CHROMEOS)
// static
bool StructTraits<
    printing::mojom::PaperMarginsDataView,
    printing::PaperMargins>::Read(printing::mojom::PaperMarginsDataView data,
                                  printing::PaperMargins* out) {
  if (data.top_margin_um() < 0 || data.right_margin_um() < 0 ||
      data.bottom_margin_um() < 0 || data.left_margin_um() < 0) {
    return false;
  }
  out->top_margin_um = data.top_margin_um();
  out->right_margin_um = data.right_margin_um();
  out->bottom_margin_um = data.bottom_margin_um();
  out->left_margin_um = data.left_margin_um();
  return true;
}
#endif  // BUILDFLAG(IS_CHROMEOS)

namespace {

template <class Key>
bool HasDuplicateItems(const std::vector<Key>& items) {
  std::set<Key> items_encountered;
  for (const Key& item : items) {
    bool inserted = items_encountered.insert(item).second;
    if (!inserted) {
      return true;
    }
  }
  return false;
}

}  // namespace

// static
bool StructTraits<printing::mojom::PrinterBasicInfoDataView,
                  printing::PrinterBasicInfo>::
    Read(printing::mojom::PrinterBasicInfoDataView data,
         printing::PrinterBasicInfo* out) {
  if (!data.ReadPrinterName(&out->printer_name) ||
      !data.ReadDisplayName(&out->display_name) ||
      !data.ReadPrinterDescription(&out->printer_description) ||
      !data.ReadOptions(&out->options)) {
    return false;
  }

  // There should be a non-empty value for `printer_name` since it needs to
  // uniquely identify the printer with the operating system among multiple
  // possible destinations.
  if (out->printer_name.empty()) {
    DLOG(ERROR) << "The printer name must not be empty.";
    return false;
  }
  // There should be a non-empty value for `display_name` since it needs to
  // uniquely identify the printer in user dialogs among multiple possible
  // destinations.
  if (out->display_name.empty()) {
    DLOG(ERROR) << "The printer's display name must not be empty.";
    return false;
  }

  return true;
}

// static
bool StructTraits<printing::mojom::PaperDataView,
                  printing::PrinterSemanticCapsAndDefaults::Paper>::
    Read(printing::mojom::PaperDataView data,
         printing::PrinterSemanticCapsAndDefaults::Paper* out) {
  std::string display_name;
  std::string vendor_id;
  gfx::Size size_um;
  gfx::Rect printable_area_um;
  // TODO(crbug.com/372062459): Remove debug code in this function when done.
  static auto* const crash_key = base::debug::AllocateCrashKeyString(
      "Bug372062459Paper", base::debug::CrashKeySize::Size64);
  if (!data.ReadDisplayName(&display_name)) {
    base::debug::ScopedCrashKeyString scoped_crash_str(crash_key,
                                                       "display_name");
    base::debug::DumpWithoutCrashing();
    return false;
  }
  if (!data.ReadVendorId(&vendor_id)) {
    base::debug::ScopedCrashKeyString scoped_crash_str(crash_key, "vendor_id");
    base::debug::DumpWithoutCrashing();
    return false;
  }
  if (!data.ReadSizeUm(&size_um)) {
    base::debug::ScopedCrashKeyString scoped_crash_str(crash_key, "size_um");
    base::debug::DumpWithoutCrashing();
    return false;
  }
  if (!data.ReadPrintableAreaUm(&printable_area_um)) {
    base::debug::ScopedCrashKeyString scoped_crash_str(crash_key,
                                                       "printable_area_um");
    base::debug::DumpWithoutCrashing();
    return false;
  }
#if BUILDFLAG(IS_CHROMEOS)
  std::optional<printing::PaperMargins> supported_margins_um;
  if (!data.ReadSupportedMarginsUm(&supported_margins_um)) {
    return false;
  }
#endif  // BUILDFLAG(IS_CHROMEOS)

  int max_height_um = data.max_height_um();
  bool has_borderless_variant = data.has_borderless_variant();

  // Allow empty Papers, since PrinterSemanticCapsAndDefaults can have empty
  // default Papers.
  if (display_name.empty() && vendor_id.empty() && size_um.IsEmpty() &&
      printable_area_um.IsEmpty() && max_height_um == 0) {
    *out = printing::PrinterSemanticCapsAndDefaults::Paper();
    return true;
  }

  // If `max_height_um` is specified, ensure it's larger than size.
  if (max_height_um > 0 && max_height_um < size_um.height()) {
    base::debug::ScopedCrashKeyString scoped_crash_str(
        crash_key, base::NumberToString(max_height_um) + "," +
                       base::NumberToString(size_um.height()));
    base::debug::DumpWithoutCrashing();
    return false;
  }

  // Invalid if the printable area is empty or if the printable area is out of
  // bounds of the paper size.  `max_height_um` doesn't need to be checked here
  // since `printable_area_um` is always relative to `size_um`.
  if (printable_area_um.IsEmpty()) {
    base::debug::ScopedCrashKeyString scoped_crash_str(
        crash_key, "printable_area_um empty");
    base::debug::DumpWithoutCrashing();
    return false;
  }

  if (!gfx::Rect(size_um).Contains(printable_area_um)) {
    base::debug::ScopedCrashKeyString scoped_crash_str(
        crash_key, size_um.ToString() + "," + printable_area_um.ToString());
    base::debug::DumpWithoutCrashing();
    return false;
  }
#if BUILDFLAG(IS_CHROMEOS)
  *out = printing::PrinterSemanticCapsAndDefaults::Paper(
      display_name, vendor_id, size_um, printable_area_um, max_height_um,
      has_borderless_variant, supported_margins_um);
#else
  *out = printing::PrinterSemanticCapsAndDefaults::Paper(
      display_name, vendor_id, size_um, printable_area_um, max_height_um,
      has_borderless_variant);
#endif  // BUILDFLAG(IS_CHROMEOS)
  return true;
}

// static
bool StructTraits<printing::mojom::MediaTypeDataView,
                  printing::PrinterSemanticCapsAndDefaults::MediaType>::
    Read(printing::mojom::MediaTypeDataView data,
         printing::PrinterSemanticCapsAndDefaults::MediaType* out) {
  return data.ReadDisplayName(&out->display_name) &&
         data.ReadVendorId(&out->vendor_id);
}

#if BUILDFLAG(IS_CHROMEOS)
// static
printing::mojom::AdvancedCapabilityType
EnumTraits<printing::mojom::AdvancedCapabilityType,
           ::printing::AdvancedCapability::Type>::
    ToMojom(::printing::AdvancedCapability::Type input) {
  switch (input) {
    case ::printing::AdvancedCapability::Type::kBoolean:
      return printing::mojom::AdvancedCapabilityType::kBoolean;
    case ::printing::AdvancedCapability::Type::kFloat:
      return printing::mojom::AdvancedCapabilityType::kFloat;
    case ::printing::AdvancedCapability::Type::kInteger:
      return printing::mojom::AdvancedCapabilityType::kInteger;
    case ::printing::AdvancedCapability::Type::kString:
      return printing::mojom::AdvancedCapabilityType::kString;
  }
  NOTREACHED();
}

// static
bool EnumTraits<printing::mojom::AdvancedCapabilityType,
                ::printing::AdvancedCapability::Type>::
    FromMojom(printing::mojom::AdvancedCapabilityType input,
              ::printing::AdvancedCapability::Type* output) {
  switch (input) {
    case printing::mojom::AdvancedCapabilityType::kBoolean:
      *output = ::printing::AdvancedCapability::Type::kBoolean;
      return true;
    case printing::mojom::AdvancedCapabilityType::kFloat:
      *output = ::printing::AdvancedCapability::Type::kFloat;
      return true;
    case printing::mojom::AdvancedCapabilityType::kInteger:
      *output = ::printing::AdvancedCapability::Type::kInteger;
      return true;
    case printing::mojom::AdvancedCapabilityType::kString:
      *output = ::printing::AdvancedCapability::Type::kString;
      return true;
  }
  NOTREACHED();
}

// static
bool StructTraits<printing::mojom::AdvancedCapabilityValueDataView,
                  ::printing::AdvancedCapabilityValue>::
    Read(printing::mojom::AdvancedCapabilityValueDataView data,
         ::printing::AdvancedCapabilityValue* out) {
  return data.ReadName(&out->name) && data.ReadDisplayName(&out->display_name);
}

// static
bool StructTraits<printing::mojom::AdvancedCapabilityDataView,
                  ::printing::AdvancedCapability>::
    Read(printing::mojom::AdvancedCapabilityDataView data,
         ::printing::AdvancedCapability* out) {
  return data.ReadName(&out->name) &&
         data.ReadDisplayName(&out->display_name) &&
         data.ReadType(&out->type) &&
         data.ReadDefaultValue(&out->default_value) &&
         data.ReadValues(&out->values);
}
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_WIN)
// static
bool StructTraits<printing::mojom::PageOutputQualityAttributeDataView,
                  printing::PageOutputQualityAttribute>::
    Read(printing::mojom::PageOutputQualityAttributeDataView data,
         printing::PageOutputQualityAttribute* out) {
  return data.ReadDisplayName(&out->display_name) && data.ReadName(&out->name);
}

// static
bool StructTraits<printing::mojom::PageOutputQualityDataView,
                  printing::PageOutputQuality>::
    Read(printing::mojom::PageOutputQualityDataView data,
         printing::PageOutputQuality* out) {
  return data.ReadQualities(&out->qualities) &&
         data.ReadDefaultQuality(&out->default_quality);
}
#endif

// static
bool StructTraits<printing::mojom::PrinterSemanticCapsAndDefaultsDataView,
                  printing::PrinterSemanticCapsAndDefaults>::
    Read(printing::mojom::PrinterSemanticCapsAndDefaultsDataView data,
         printing::PrinterSemanticCapsAndDefaults* out) {
  std::optional<printing::PrinterSemanticCapsAndDefaults::MediaTypes>
      media_types;
  std::optional<printing::PrinterSemanticCapsAndDefaults::MediaType>
      default_media_type;

  out->collate_capable = data.collate_capable();
  out->collate_default = data.collate_default();
  out->copies_max = data.copies_max();
  if (!data.ReadDuplexModes(&out->duplex_modes) ||
      !data.ReadDuplexDefault(&out->duplex_default)) {
    return false;
  }
  out->color_changeable = data.color_changeable();
  out->color_default = data.color_default();
  if (!data.ReadColorModel(&out->color_model) ||
      !data.ReadBwModel(&out->bw_model) || !data.ReadPapers(&out->papers) ||
      !data.ReadUserDefinedPapers(&out->user_defined_papers) ||
      !data.ReadDefaultPaper(&out->default_paper) ||
      !data.ReadDpis(&out->dpis) || !data.ReadDefaultDpi(&out->default_dpi)) {
    return false;
  }

#if BUILDFLAG(IS_CHROMEOS)
  out->pin_supported = data.pin_supported();
  if (!data.ReadAdvancedCapabilities(&out->advanced_capabilities) ||
      !data.ReadPrintScalingTypes(&out->print_scaling_types) ||
      !data.ReadPrintScalingTypeDefault(&out->print_scaling_type_default)) {
    return false;
  }
#endif  // BUILDFLAG(IS_CHROMEOS)

  // Extra validity checks.

  // Can not have less than one copy.
  if (out->copies_max < 1) {
    DLOG(ERROR) << "Must have copies_max greater than zero.";
    return false;
  }

  // There should not be duplicates in certain arrays.
  if (HasDuplicateItems(out->duplex_modes)) {
    DLOG(ERROR) << "Duplicate duplex_modes detected.";
    return false;
  }

  if (HasDuplicateItems(out->user_defined_papers)) {
    DLOG(ERROR) << "Duplicate user_defined_papers detected.";
    // TODO(crbug.com/372062459): Remove debug code when done.
    std::string names;
    for (const auto& user_defined_paper : out->user_defined_papers) {
      names += user_defined_paper.display_name();
      names += ' ';
    }
    static auto* const crash_key = base::debug::AllocateCrashKeyString(
        "Bug372062459UserDefinedPaper", base::debug::CrashKeySize::Size1024);
    base::debug::ScopedCrashKeyString scoped_crash_str(crash_key, names);
    base::debug::DumpWithoutCrashing();
    return false;
  }

#if BUILDFLAG(IS_CHROMEOS)
  if (HasDuplicateItems(out->advanced_capabilities)) {
    DLOG(ERROR) << "Duplicate advanced_capabilities detected.";
    return false;
  }
  if (HasDuplicateItems(out->print_scaling_types)) {
    DLOG(ERROR) << "Duplicate print_scaling_types detected.";
    return false;
  }
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_WIN)
  if (!data.ReadPageOutputQuality(&out->page_output_quality)) {
    return false;
  }
  if (out->page_output_quality) {
    printing::PageOutputQualityAttributes qualities =
        out->page_output_quality->qualities;
    std::optional<std::string> default_quality =
        out->page_output_quality->default_quality;

    // If non-null `default_quality`, there should be a matching element in
    // `qualities` array.
    if (default_quality) {
      if (!base::Contains(qualities, *default_quality,
                          &printing::PageOutputQualityAttribute::name)) {
        DLOG(ERROR) << "Non-null default quality, but page output qualities "
                       "does not contain default quality";
        return false;
      }
    }

    // There should be no duplicates in `qualities` array.
    if (HasDuplicateItems(qualities)) {
      DLOG(ERROR) << "Duplicate page output qualities detected.";
      return false;
    }
  }
#endif

  if (!data.ReadMediaTypes(&media_types) ||
      !data.ReadDefaultMediaType(&default_media_type)) {
    return false;
  }

  if (media_types.has_value()) {
    out->media_types = media_types.value();
  }
  if (default_media_type.has_value()) {
    out->default_media_type = default_media_type.value();
  }

  return true;
}

}  // namespace mojo
