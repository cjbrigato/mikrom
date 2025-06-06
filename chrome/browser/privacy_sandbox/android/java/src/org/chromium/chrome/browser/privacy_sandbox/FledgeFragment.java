// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.privacy_sandbox;

import android.os.Bundle;
import android.view.View;

import androidx.annotation.VisibleForTesting;
import androidx.preference.Preference;
import androidx.preference.PreferenceCategory;

import org.chromium.base.metrics.RecordUserAction;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.ObservableSupplierImpl;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.preferences.Pref;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.settings.ChromeManagedPreferenceDelegate;
import org.chromium.chrome.browser.ui.messages.snackbar.Snackbar;
import org.chromium.components.browser_ui.settings.ChromeBasePreference;
import org.chromium.components.browser_ui.settings.ChromeSwitchPreference;
import org.chromium.components.browser_ui.settings.ClickableSpansTextMessagePreference;
import org.chromium.components.browser_ui.settings.SettingsFragment;
import org.chromium.components.browser_ui.settings.SettingsUtils;
import org.chromium.components.browser_ui.settings.TextMessagePreference;
import org.chromium.components.embedder_support.util.UrlConstants;
import org.chromium.components.favicon.LargeIconBridge;
import org.chromium.components.prefs.PrefService;
import org.chromium.components.user_prefs.UserPrefs;
import org.chromium.ui.text.ChromeClickableSpan;
import org.chromium.ui.text.SpanApplier;

import java.util.List;

/** Fragment for the Privacy Sandbox -> Fledge preferences. */
@NullMarked
public class FledgeFragment extends PrivacySandboxSettingsBaseFragment
        implements Preference.OnPreferenceChangeListener, Preference.OnPreferenceClickListener {
    @VisibleForTesting static final int MAX_DISPLAYED_SITES = 15;

    private static final String FLEDGE_TOGGLE_PREFERENCE = "fledge_toggle";
    private static final String FLEDGE_DESCRIPTION_PREFERENCE = "fledge_description";
    private static final String HEADING_PREFERENCE = "fledge_heading";
    private static final String CURRENT_SITES_PREFERENCE = "current_fledge_sites";
    private static final String EMPTY_FLEDGE_PREFERENCE = "fledge_empty";
    private static final String DISABLED_FLEDGE_PREFERENCE = "fledge_disabled";
    private static final String ALL_SITES_PREFERENCE = "fledge_all_sites";
    private static final String FOOTER_PREFERENCE = "fledge_page_footer";

    private ChromeSwitchPreference mFledgeTogglePreference;
    private TextMessagePreference mFledgeDescriptionPreference;
    private PreferenceCategoryWithClickableSummary mHeadingPreference;
    private PreferenceCategory mCurrentSitesCategory;
    private TextMessagePreference mEmptyFledgePreference;
    private TextMessagePreference mDisabledFledgePreference;
    private ChromeBasePreference mAllSitesPreference;
    private @Nullable LargeIconBridge mLargeIconBridge;
    private ClickableSpansTextMessagePreference mFooterPreference;
    private boolean mMoreThanMaxSitesToDisplay;
    private final ObservableSupplierImpl<String> mPageTitle = new ObservableSupplierImpl<>();

    static boolean isFledgePrefEnabled(Profile profile) {
        PrefService prefService = UserPrefs.get(profile);
        return prefService.getBoolean(Pref.PRIVACY_SANDBOX_M1_FLEDGE_ENABLED);
    }

    static void setFledgePrefEnabled(Profile profile, boolean isEnabled) {
        PrefService prefService = UserPrefs.get(profile);
        prefService.setBoolean(Pref.PRIVACY_SANDBOX_M1_FLEDGE_ENABLED, isEnabled);
    }

    static boolean isFledgePrefManaged(Profile profile) {
        PrefService prefService = UserPrefs.get(profile);
        return prefService.isManagedPreference(Pref.PRIVACY_SANDBOX_M1_FLEDGE_ENABLED);
    }

    @Override
    public void onCreatePreferences(@Nullable Bundle bundle, @Nullable String s) {
        super.onCreatePreferences(bundle, s);
        mPageTitle.set(getString(R.string.settings_fledge_page_title));
        SettingsUtils.addPreferencesFromResource(this, R.xml.fledge_preference);

        mFledgeTogglePreference = findPreference(FLEDGE_TOGGLE_PREFERENCE);
        mFledgeDescriptionPreference = findPreference(FLEDGE_DESCRIPTION_PREFERENCE);
        mHeadingPreference = findPreference(HEADING_PREFERENCE);
        mCurrentSitesCategory = findPreference(CURRENT_SITES_PREFERENCE);
        mEmptyFledgePreference = findPreference(EMPTY_FLEDGE_PREFERENCE);
        mDisabledFledgePreference = findPreference(DISABLED_FLEDGE_PREFERENCE);
        mAllSitesPreference = findPreference(ALL_SITES_PREFERENCE);
        mFooterPreference = (ClickableSpansTextMessagePreference) findPreference(FOOTER_PREFERENCE);

        mFledgeTogglePreference.setChecked(isFledgePrefEnabled(getProfile()));
        mFledgeTogglePreference.setOnPreferenceChangeListener(this);
        mFledgeTogglePreference.setManagedPreferenceDelegate(createManagedPreferenceDelegate());
        mMoreThanMaxSitesToDisplay = false;

        mHeadingPreference.setSummary(
                SpanApplier.applySpans(
                        getResources()
                                .getString(R.string.settings_fledge_page_current_sites_description),
                        new SpanApplier.SpanInfo(
                                "<link>",
                                "</link>",
                                new ChromeClickableSpan(getContext(), this::onLearnMoreClicked))));
        mFooterPreference.setSummary(
                SpanApplier.applySpans(
                        getResources().getString(R.string.settings_fledge_page_footer_new),
                        new SpanApplier.SpanInfo(
                                "<link1>",
                                "</link1>",
                                new ChromeClickableSpan(
                                        getContext(), this::onFledgeSettingsLinkClicked)),
                        new SpanApplier.SpanInfo(
                                "<link2>",
                                "</link2>",
                                new ChromeClickableSpan(getContext(), this::onCookieSettingsLink)),
                        new SpanApplier.SpanInfo(
                                "<link3>",
                                "</link3>",
                                new ChromeClickableSpan(
                                        getContext(), this::onManagingAdPrivacyClicked))));
        handleAdsApiUxEnhancements();
    }

    @Override
    public ObservableSupplier<String> getPageTitle() {
        return mPageTitle;
    }

    private void handleAdsApiUxEnhancements() {
        if (!ChromeFeatureList.isEnabled(
                ChromeFeatureList.PRIVACY_SANDBOX_ADS_API_UX_ENHANCEMENTS)) {
            return;
        }
        mFledgeTogglePreference.setSummary(
                getContext()
                        .getString(R.string.settings_site_suggested_ads_page_toggle_sub_label_v2));
        mFledgeDescriptionPreference.setSummary(
                SpanApplier.applySpans(
                        getResources()
                                .getString(
                                        R.string
                                                .settings_site_suggested_ads_page_explanation_v2_clank),
                        new SpanApplier.SpanInfo(
                                "<link>",
                                "</link>",
                                new ChromeClickableSpan(getContext(), this::onLearnMoreClicked))));
        mHeadingPreference.setSummary(
                getContext()
                        .getString(
                                R.string.settings_site_suggested_ads_current_sites_description_v2));
        ClickableSpansTextMessagePreference disclaimerPreference =
                findPreference("fledge_page_disclaimer");
        disclaimerPreference.setVisible(true);
        disclaimerPreference.setSummary(
                SpanApplier.applySpans(
                        getResources()
                                .getString(
                                        R.string.settings_site_suggested_ads_page_disclaimer_clank),
                        new SpanApplier.SpanInfo(
                                "<link>",
                                "</link>",
                                new ChromeClickableSpan(
                                        getContext(), this::onPrivacyPolicyLinkClicked))));
        mFooterPreference.setSummary(
                SpanApplier.applySpans(
                        getResources()
                                .getString(R.string.settings_site_suggested_ads_page_footer_v2),
                        new SpanApplier.SpanInfo(
                                "<link1>",
                                "</link1>",
                                new ChromeClickableSpan(
                                        getContext(), this::onFledgeSettingsLinkClicked)),
                        new SpanApplier.SpanInfo(
                                "<link2>",
                                "</link2>",
                                new ChromeClickableSpan(
                                        getContext(), this::onCookieSettingsLink))));
    }

    private void onLearnMoreClicked(View unused) {
        RecordUserAction.record("Settings.PrivacySandbox.Fledge.LearnMoreClicked");
        startSettings(FledgeLearnMoreFragment.class);
    }

    private void onManagingAdPrivacyClicked(View unused) {
        getCustomTabLauncher()
                .openUrlInCct(getContext(), PrivacySandboxSettingsFragment.HELP_CENTER_URL);
    }

    private void onFledgeSettingsLinkClicked(View unused) {
        startSettings(TopicsFragment.class);
    }

    private void onCookieSettingsLink(View unused) {
        launchCookieSettings();
    }

    private void onPrivacyPolicyLinkClicked(View unused) {
        RecordUserAction.record(
                "Settings.PrivacySandbox.SiteSuggestedAds.PrivacyPolicyLinkClicked");
        getCustomTabLauncher()
                .openUrlInCct(
                        getContext(),
                        getPrivacySandboxBridge().shouldUsePrivacyPolicyChinaDomain()
                                ? UrlConstants.GOOGLE_PRIVACY_POLICY_CHINA
                                : UrlConstants.GOOGLE_PRIVACY_POLICY);
    }

    @Override
    public void onViewCreated(View view, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(view, savedInstanceState);

        // Disable animations on preference changes.
        getListView().setItemAnimator(null);
    }

    @Override
    public void onStart() {
        super.onStart();
        getPrivacySandboxBridge().getFledgeJoiningEtldPlusOneForDisplay(this::populateCurrentSites);
        updatePreferenceVisibility();
    }

    @Override
    public void onDestroyView() {
        super.onDestroyView();
        if (mLargeIconBridge != null) {
            mLargeIconBridge.destroy();
        }
        mLargeIconBridge = null;
    }

    @Override
    public boolean onPreferenceChange(Preference preference, Object value) {
        if (preference.getKey().equals(FLEDGE_TOGGLE_PREFERENCE)) {
            boolean enabled = (boolean) value;
            RecordUserAction.record(
                    enabled
                            ? "Settings.PrivacySandbox.Fledge.Enabled"
                            : "Settings.PrivacySandbox.Fledge.Disabled");
            setFledgePrefEnabled(getProfile(), enabled);
            updatePreferenceVisibility();
            return true;
        }

        return false;
    }

    @Override
    public boolean onPreferenceClick(Preference preference) {
        if (preference instanceof FledgePreference) {
            getPrivacySandboxBridge()
                    .setFledgeJoiningAllowed(((FledgePreference) preference).getSite(), false);
            mCurrentSitesCategory.removePreference(preference);
            updatePreferenceVisibility();

            showSnackbar(
                    R.string.settings_fledge_page_block_site_snackbar,
                    null,
                    Snackbar.TYPE_ACTION,
                    Snackbar.UMA_PRIVACY_SANDBOX_REMOVE_SITE,
                    /* actionStringResId= */ 0,
                    /* multiLine= */ true);
            RecordUserAction.record("Settings.PrivacySandbox.Fledge.SiteRemoved");
            return true;
        }

        return false;
    }

    private void populateCurrentSites(List<String> currentSites) {
        if (mLargeIconBridge == null) {
            mLargeIconBridge = new LargeIconBridge(getProfile());
        }

        mCurrentSitesCategory.removeAll();
        mMoreThanMaxSitesToDisplay = currentSites.size() > MAX_DISPLAYED_SITES;

        int nrDisplayedSites = Math.min(currentSites.size(), MAX_DISPLAYED_SITES);
        for (int i = 0; i < nrDisplayedSites; i++) {
            String site = currentSites.get(i);
            FledgePreference preference =
                    new FledgePreference(getContext(), site, mLargeIconBridge);
            preference.setImage(
                    R.drawable.btn_close,
                    getResources()
                            .getString(R.string.settings_fledge_page_block_site_a11y_label, site));
            preference.setDividerAllowedAbove(false);
            preference.setOnPreferenceClickListener(this);
            mCurrentSitesCategory.addPreference(preference);
        }

        updatePreferenceVisibility();
    }

    private void updatePreferenceVisibility() {
        boolean fledgeEnabled = isFledgePrefEnabled(getProfile());
        boolean sitesEmpty = mCurrentSitesCategory.getPreferenceCount() == 0;

        // Visible when Fledge is disabled.
        mDisabledFledgePreference.setVisible(!fledgeEnabled);

        // Visible when Fledge is enabled, but the current sites list is empty.
        mEmptyFledgePreference.setVisible(fledgeEnabled && sitesEmpty);

        // Visible when Fledge is enabled and the current sites list is not empty.
        mCurrentSitesCategory.setVisible(fledgeEnabled && !sitesEmpty);

        // Visible when Fledge is enabled and has more than MAX_DISPLAYED_SITES allowed sites.
        mAllSitesPreference.setVisible(fledgeEnabled && mMoreThanMaxSitesToDisplay);
    }

    private ChromeManagedPreferenceDelegate createManagedPreferenceDelegate() {
        return new ChromeManagedPreferenceDelegate(getProfile()) {
            @Override
            public boolean isPreferenceControlledByPolicy(Preference preference) {
                if (FLEDGE_TOGGLE_PREFERENCE.equals(preference.getKey())) {
                    return isFledgePrefManaged(getProfile());
                }
                return false;
            }
        };
    }

    @Override
    public @SettingsFragment.AnimationType int getAnimationType() {
        return SettingsFragment.AnimationType.PROPERTY;
    }
}
