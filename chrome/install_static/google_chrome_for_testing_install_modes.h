// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Brand-specific types and constants for Google Chrome for Testing

#ifndef CHROME_INSTALL_STATIC_GOOGLE_CHROME_FOR_TESTING_INSTALL_MODES_H_
#define CHROME_INSTALL_STATIC_GOOGLE_CHROME_FOR_TESTING_INSTALL_MODES_H_

#include <array>

#include "chrome/app/chrome_dll_resource.h"
#include "chrome/common/chrome_icon_resources_win.h"
#include "chrome/install_static/install_constants.h"

namespace install_static {

// Note: This list of indices must be kept in sync with the brand-specific
// resource strings in chrome/installer/util/prebuild/create_string_rc.
enum InstallConstantIndex {
  GOOGLE_CHROME_FOR_TESTING_INDEX,
  NUM_INSTALL_MODES,
};

inline constexpr auto kInstallModes = std::to_array<InstallConstants>({
    // The primary install mode for stable Google Chrome.
    {
        .size = sizeof(InstallConstants),
        .index = GOOGLE_CHROME_FOR_TESTING_INDEX,  // The one and only mode for
                                                   // Google Chrome for Testing.
        .install_switch =
            "",  // No install switch for the primary install mode.
        .install_suffix =
            L"",  // Empty install_suffix for the primary install mode.
        .logo_suffix = L"",  // No logo suffix for the primary install mode.
        .app_guid =
            L"",  // Empty app_guid since no integration with Google Update.
        .base_app_name =
            L"Google Chrome for Testing",      // A distinct base_app_name.
        .base_app_id = L"ChromeForTesting",    // A distinct base_app_id.
        .browser_prog_id_prefix = L"CfTHTML",  // Browser ProgID prefix.
        .browser_prog_id_description =
            L"Chrome for Testing HTML Document",  // Browser ProgID
                                                  // description.
        .pdf_prog_id_prefix = L"CfTPDF",          // PDF ProgID prefix.
        .pdf_prog_id_description =
            L"Chrome for Testing PDF Document",  // PDF ProgID description.
        .active_setup_guid =
            L"{E25CFD4E-D9D4-4123-936A-286FBB19BA5B}",  // Active Setup
                                                        // GUID.
        .legacy_command_execute_clsid = L"",  // CommandExecuteImpl CLSID.
        .toast_activator_clsid = {0x77ED8F9B,
                                  0xE27A,
                                  0x499F,
                                  {0x8E, 0x2F, 0xD7, 0xC0, 0x41, 0x57, 0xCF,
                                   0x64}},  // Toast Activator CLSID.
        // {77ED8F9B-E27A-499F-8E2F-D7C04157CF64}
        .elevator_clsid = {0x724349BF,
                           0xE1CF,
                           0x4481,
                           {0xA6, 0x4D, 0x8C, 0xD1, 0x01, 0x83, 0xCA,
                            0x03}},  // Elevator CLSID.
        // {724349BF-E1CF-4481-A64D-8CD10183CA03}
        .elevator_iid = {0x3DC48E97,
                         0x47D0,
                         0x476F,
                         {0x8F, 0x89, 0x07, 0x92, 0xFC, 0x61, 0x15,
                          0x67}},  // IElevator IID and TypeLib
                                   // {3DC48E97-47D0-476F-8F89-0792FC611567}
        .default_channel_name =
            L"",  // Empty default channel name since no update integration.
        .channel_strategy = ChannelStrategy::UNSUPPORTED,
        .supports_system_level =
            false,  // Does not support system-level installs.
        .supports_set_as_default_browser =
            false,  // Does not support in-product set as default browser
                    // UX.
        .app_icon_resource_index =
            icon_resources::kApplicationIndex,  // App icon resource index.
        .app_icon_resource_id = IDR_MAINFRAME,  // App icon resource id.
        .html_doc_icon_resource_index =
            icon_resources::kHtmlDocIndex,  // HTML doc icon resource index.
        .pdf_doc_icon_resource_index =
            icon_resources::kPDFDocIndex,  // PDF doc icon resource index.
        .sandbox_sid_prefix =
            L"S-1-15-2-3251537155-1984446955-2931258699-841473695-"
            L"1938553385-"
            L"924012153-",  // App container sid prefix for sandbox.
    },
});

}  // namespace install_static

#endif  // CHROME_INSTALL_STATIC_GOOGLE_CHROME_FOR_TESTING_INSTALL_MODES_H_
