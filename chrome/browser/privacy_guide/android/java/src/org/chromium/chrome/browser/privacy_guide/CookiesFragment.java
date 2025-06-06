// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.privacy_guide;

import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.RadioGroup;
import android.widget.TextView;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.components.browser_ui.site_settings.WebsitePreferenceBridge;
import org.chromium.components.browser_ui.widget.RadioButtonWithDescription;
import org.chromium.components.content_settings.ContentSettingsType;
import org.chromium.components.content_settings.CookieControlsMode;
import org.chromium.components.content_settings.PrefNames;
import org.chromium.components.user_prefs.UserPrefs;

/** Controls the behavior of the Cookies privacy guide page. */
@NullMarked
public class CookiesFragment extends PrivacyGuideBasePage
        implements RadioGroup.OnCheckedChangeListener {
    private RadioButtonWithDescription mBlockThirdPartyIncognito;
    private RadioButtonWithDescription mBlockThirdParty;

    @Override
    public View onCreateView(
            LayoutInflater inflater,
            @Nullable ViewGroup container,
            @Nullable Bundle savedInstanceState) {
        return inflater.inflate(R.layout.privacy_guide_cookies_step, container, false);
    }

    @Override
    public void onViewCreated(View view, @Nullable Bundle savedInstanceState) {
        RadioGroup radioGroup = view.findViewById(R.id.cookies_radio_button);
        radioGroup.setOnCheckedChangeListener(this);

        mBlockThirdPartyIncognito = view.findViewById(R.id.block_third_party_incognito);
        mBlockThirdParty = view.findViewById(R.id.block_third_party);

        if (ChromeFeatureList.isEnabled(ChromeFeatureList.ALWAYS_BLOCK_3PCS_INCOGNITO)) {
            TextView header = view.findViewById(R.id.cookies_step_header);
            header.setText(getContext().getString(R.string.privacy_guide_cookies_header));
            int allowSubheaderId =
                    R.string.settings_privacy_guide_cookies_card_block_tpc_allow_subheader;
            mBlockThirdPartyIncognito.setPrimaryText(getContext().getString(allowSubheaderId));
            mBlockThirdPartyIncognito.setDescriptionText(
                    getContext().getString(R.string.privacy_guide_cookies_allow_description));
            int blockSubheaderId =
                    R.string.settings_privacy_guide_cookies_card_block_tpc_block_subheader;
            mBlockThirdParty.setPrimaryText(getContext().getString(blockSubheaderId));
            mBlockThirdParty.setDescriptionText(
                    getContext().getString(R.string.privacy_guide_cookies_block_description));
        }

        boolean allowCookies =
                WebsitePreferenceBridge.isCategoryEnabled(
                        getProfile(), ContentSettingsType.COOKIES);
        if (!allowCookies) {
            assert false : "Cookies page should not be shown if cookies are blocked";
        }
        updateRadioButtonConfig();
    }

    @Override
    public void onCheckedChanged(RadioGroup group, int clickedButtonId) {
        // TODO(crbug.com/370008370): Remove following line, it's a no-op
        WebsitePreferenceBridge.setCategoryEnabled(getProfile(), ContentSettingsType.COOKIES, true);

        if (clickedButtonId == R.id.block_third_party_incognito) {
            setCookieControlsMode(CookieControlsMode.INCOGNITO_ONLY);
        } else if (clickedButtonId == R.id.block_third_party) {
            setCookieControlsMode(CookieControlsMode.BLOCK_THIRD_PARTY);
        } else {
            assert false : "Unexpected clickedButtonId" + clickedButtonId;
        }
    }

    private void updateRadioButtonConfig() {
        @CookieControlsMode
        int cookieControlsMode = PrivacyGuideUtils.getCookieControlsMode(getProfile());
        switch (cookieControlsMode) {
            case CookieControlsMode.INCOGNITO_ONLY:
                mBlockThirdPartyIncognito.setChecked(true);
                break;
            case CookieControlsMode.BLOCK_THIRD_PARTY:
                mBlockThirdParty.setChecked(true);
                break;
            case CookieControlsMode.OFF:
                if (ChromeFeatureList.isEnabled(ChromeFeatureList.ALWAYS_BLOCK_3PCS_INCOGNITO)) {
                    mBlockThirdPartyIncognito.setChecked(true);
                    break;
                }
                assert false : "Cookies page should not be shown when cookie control is off";
                break;
            default:
                assert false : "Unexpected CookieControlsMode " + cookieControlsMode;
        }
    }

    @Override
    public void onResume() {
        super.onResume();
        updateRadioButtonConfig();
    }

    private void setCookieControlsMode(@CookieControlsMode int cookieControlsMode) {
        PrivacyGuideMetricsDelegate.recordMetricsOnCookieControlsChange(cookieControlsMode);
        UserPrefs.get(getProfile()).setInteger(PrefNames.COOKIE_CONTROLS_MODE, cookieControlsMode);
    }
}
