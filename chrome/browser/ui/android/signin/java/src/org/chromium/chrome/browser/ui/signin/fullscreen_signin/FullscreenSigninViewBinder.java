// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.ui.signin.fullscreen_signin;

import android.text.method.LinkMovementMethod;
import android.transition.AutoTransition;
import android.transition.TransitionManager;
import android.view.View;
import android.view.ViewGroup.LayoutParams;
import android.widget.ProgressBar;

import androidx.annotation.StringRes;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.signin.services.DisplayableProfileData;
import org.chromium.chrome.browser.ui.signin.R;
import org.chromium.chrome.browser.ui.signin.SigninUtils;
import org.chromium.chrome.browser.ui.signin.account_picker.ExistingAccountRowViewBinder;
import org.chromium.ui.modelutil.PropertyKey;
import org.chromium.ui.modelutil.PropertyModel;

/** Stateless FullscreenSignin view binder. */
@NullMarked
class FullscreenSigninViewBinder {
    static void bind(PropertyModel model, FullscreenSigninView view, PropertyKey propertyKey) {
        if (propertyKey == FullscreenSigninProperties.ON_CONTINUE_AS_CLICKED) {
            view.getContinueButtonView()
                    .setOnClickListener(
                            model.get(FullscreenSigninProperties.ON_CONTINUE_AS_CLICKED));
        } else if (propertyKey == FullscreenSigninProperties.ON_DISMISS_CLICKED) {
            view.getDismissButtonView()
                    .setOnClickListener(model.get(FullscreenSigninProperties.ON_DISMISS_CLICKED));
        } else if (propertyKey
                == FullscreenSigninProperties.SHOW_SIGNIN_PROGRESS_SPINNER_WITH_TEXT) {
            updateVisibilityOnButtonClick(view, model);
            updateBottomGroupVisibility(view, model);
        } else if (propertyKey == FullscreenSigninProperties.SHOW_SIGNIN_PROGRESS_SPINNER) {
            updateVisibilityOnButtonClick(view, model);
            updateBottomGroupVisibility(view, model);
        } else if (propertyKey == FullscreenSigninProperties.ON_SELECTED_ACCOUNT_CLICKED) {
            view.getSelectedAccountView()
                    .setOnClickListener(
                            model.get(FullscreenSigninProperties.ON_SELECTED_ACCOUNT_CLICKED));
        } else if (propertyKey == FullscreenSigninProperties.SELECTED_ACCOUNT_DATA) {
            updateSelectedAccount(view, model);
            updateBottomGroupVisibility(view, model);
        } else if (propertyKey == FullscreenSigninProperties.IS_SELECTED_ACCOUNT_SUPERVISED) {
            final boolean isSelectedAccountSupervised =
                    model.get(FullscreenSigninProperties.IS_SELECTED_ACCOUNT_SUPERVISED);
            view.getSelectedAccountView().setEnabled(!isSelectedAccountSupervised);

            updateBrowserManagedHeaderView(view, model);
            updateBottomGroupVisibility(view, model);
        } else if (propertyKey == FullscreenSigninProperties.SHOW_INITIAL_LOAD_PROGRESS_SPINNER) {
            final boolean showInitialLoadProgressSpinner =
                    model.get(FullscreenSigninProperties.SHOW_INITIAL_LOAD_PROGRESS_SPINNER);
            final ProgressBar initialLoadProgressSpinner = view.getInitialLoadProgressSpinnerView();
            if (showInitialLoadProgressSpinner) {
                // The progress spinner is shown at the beginning when layout inflation may not be
                // complete. So it is not possible to use TransitionManager with a startDelay in
                // this case.
                initialLoadProgressSpinner.animate().alpha(1.0f).setStartDelay(500);
            } else {
                TransitionManager.beginDelayedTransition(view);
                initialLoadProgressSpinner.setVisibility(View.GONE);
            }
            updateBottomGroupVisibility(view, model);
            updateBrowserManagedHeaderView(view, model);
        } else if (propertyKey == FullscreenSigninProperties.SHOW_ENTERPRISE_MANAGEMENT_NOTICE) {
            updateBrowserManagedHeaderView(view, model);
        } else if (propertyKey == FullscreenSigninProperties.IS_SIGNIN_SUPPORTED) {
            updateSelectedAccount(view, model);
            updateBottomGroupVisibility(view, model);
        } else if (propertyKey == FullscreenSigninProperties.LOGO_DRAWABLE_ID) {
            int logoId = model.get(FullscreenSigninProperties.LOGO_DRAWABLE_ID);
            LayoutParams params = view.getLogo().getLayoutParams();

            // TODO(crbug.com/390418475): Remove the if block below and
            // fullscreen_signin_logo_default_height when fre_product_logo will be a VectorDrawable
            // with appropriate height.
            if (logoId == 0) {
                logoId = R.drawable.fre_product_logo;
                params.height =
                        view.getContext()
                                .getResources()
                                .getDimensionPixelSize(
                                        R.dimen.fullscreen_signin_logo_default_height);
            } else {
                params.height = LayoutParams.WRAP_CONTENT;
            }
            view.getLogo().setImageResource(logoId);
            view.getLogo().setLayoutParams(params);
        } else if (propertyKey == FullscreenSigninProperties.TITLE_STRING_ID) {
            @StringRes int textId = model.get(FullscreenSigninProperties.TITLE_STRING_ID);
            view.getTitle().setText(textId);
        } else if (propertyKey == FullscreenSigninProperties.SUBTITLE_STRING_ID) {
            @StringRes int textId = model.get(FullscreenSigninProperties.SUBTITLE_STRING_ID);
            if (textId != 0) {
                view.getSubtitle().setText(textId);
            }
            updateBottomGroupVisibility(view, model);
        } else if (propertyKey == FullscreenSigninProperties.DISMISS_BUTTON_STRING_ID) {
            @StringRes int textId = model.get(FullscreenSigninProperties.DISMISS_BUTTON_STRING_ID);
            view.getDismissButtonView().setText(textId);
        } else if (propertyKey == FullscreenSigninProperties.FOOTER_STRING) {
            final CharSequence footerText = model.get(FullscreenSigninProperties.FOOTER_STRING);
            if (footerText == null) {
                view.getFooterView().setVisibility(View.GONE);
            } else {
                view.getFooterView().setVisibility(View.VISIBLE);
                view.getFooterView().setText(footerText);
                view.getFooterView().setMovementMethod(LinkMovementMethod.getInstance());
            }
        } else {
            throw new IllegalArgumentException("Unknown property key:" + propertyKey);
        }
    }

    private static void updateBrowserManagedHeaderView(
            FullscreenSigninView view, PropertyModel model) {
        // Supervised accounts do not have any enterprise policy set, but they set app restrictions
        // which the policy load listener considers as policy. But if child accounts are loaded
        // dynamically, policy load listener may say there are no policies on device. Because of the
        // entangled nature of IS_SELECTED_ACCOUNT_SUPERVISED and SHOW_ENTERPRISE_MANAGEMENT_NOTICE
        // they are both handled in this function as one of these properties will get updated before
        // the other.

        // Only display the managed header after all other elements have loaded
        if (model.get(FullscreenSigninProperties.SHOW_INITIAL_LOAD_PROGRESS_SPINNER)) {
            return;
        }
        if (model.get(FullscreenSigninProperties.IS_SELECTED_ACCOUNT_SUPERVISED)) {
            view.getBrowserManagedHeaderView().setVisibility(View.VISIBLE);
            view.getPrivacyDisclaimer().setText(R.string.fre_browser_managed_by_parent);
            view.getPrivacyDisclaimer()
                    .setCompoundDrawablesRelativeWithIntrinsicBounds(
                            R.drawable.ic_account_child_20dp, 0, 0, 0);
        } else if (model.get(FullscreenSigninProperties.SHOW_ENTERPRISE_MANAGEMENT_NOTICE)) {
            view.getBrowserManagedHeaderView().setVisibility(View.VISIBLE);
            view.getPrivacyDisclaimer().setText(R.string.fre_browser_managed_by_organization);
            view.getPrivacyDisclaimer()
                    .setCompoundDrawablesRelativeWithIntrinsicBounds(
                            R.drawable.ic_business, 0, 0, 0);
        } else {
            view.getBrowserManagedHeaderView().setVisibility(View.GONE);
        }
    }

    private static void updateSelectedAccount(FullscreenSigninView view, PropertyModel model) {
        if (!model.get(FullscreenSigninProperties.IS_SIGNIN_SUPPORTED)) {
            view.getContinueButtonView().setText(R.string.continue_button);
            return;
        }
        final @Nullable DisplayableProfileData profileData =
                model.get(FullscreenSigninProperties.SELECTED_ACCOUNT_DATA);
        if (profileData == null) {
            view.getContinueButtonView().setText(R.string.signin_add_account_to_device);
        } else {
            ExistingAccountRowViewBinder.bindAccountView(
                    profileData, view.getSelectedAccountView(), /* isCurrentlySelected= */ true);
            view.getContinueButtonView()
                    .setText(SigninUtils.getContinueAsButtonText(view.getContext(), profileData));
        }
    }

    private static void updateBottomGroupVisibility(
            FullscreenSigninView view, PropertyModel model) {
        // TODO(crbug.com/349973162): Add a regression test that updates profile data after continue
        // button has been pressed.
        final boolean showSigninProgressSpinner =
                model.get(FullscreenSigninProperties.SHOW_SIGNIN_PROGRESS_SPINNER_WITH_TEXT)
                        || model.get(FullscreenSigninProperties.SHOW_SIGNIN_PROGRESS_SPINNER);
        if (showSigninProgressSpinner) {
            // Don't update the bottom group when sign-in progress spinner is being shown.
            return;
        }

        final boolean showInitialLoadProgressSpinner =
                model.get(FullscreenSigninProperties.SHOW_INITIAL_LOAD_PROGRESS_SPINNER);
        final boolean isSelectedAccountSupervised =
                model.get(FullscreenSigninProperties.IS_SELECTED_ACCOUNT_SUPERVISED);
        final boolean showManagementNotice =
                model.get(FullscreenSigninProperties.SHOW_ENTERPRISE_MANAGEMENT_NOTICE);
        final @StringRes int textId = model.get(FullscreenSigninProperties.SUBTITLE_STRING_ID);
        view.getTitle().setVisibility(showInitialLoadProgressSpinner ? View.GONE : View.VISIBLE);
        view.getSubtitle()
                .setVisibility(
                        !showInitialLoadProgressSpinner
                                        && !isSelectedAccountSupervised
                                        && !showManagementNotice
                                        && textId != 0
                                ? View.VISIBLE
                                : View.GONE);

        final int selectedAccountVisibility =
                !showInitialLoadProgressSpinner
                                && model.get(FullscreenSigninProperties.SELECTED_ACCOUNT_DATA)
                                        != null
                                && model.get(FullscreenSigninProperties.IS_SIGNIN_SUPPORTED)
                        ? View.VISIBLE
                        : View.GONE;
        view.getSelectedAccountView().setVisibility(selectedAccountVisibility);
        view.getExpandIconView()
                .setVisibility(
                        selectedAccountVisibility == View.VISIBLE && isSelectedAccountSupervised
                                ? View.INVISIBLE
                                : View.VISIBLE);
        final int dismissButtonVisibility =
                !showInitialLoadProgressSpinner
                                && model.get(FullscreenSigninProperties.IS_SIGNIN_SUPPORTED)
                                && !isSelectedAccountSupervised
                        ? View.VISIBLE
                        : View.GONE;
        view.getDismissButtonView().setVisibility(dismissButtonVisibility);

        final int otherElementsVisibility =
                showInitialLoadProgressSpinner ? View.GONE : View.VISIBLE;
        view.getContinueButtonView().setVisibility(otherElementsVisibility);
        final CharSequence footerText = model.get(FullscreenSigninProperties.FOOTER_STRING);
        view.getFooterView()
                .setVisibility(footerText == null ? View.GONE : otherElementsVisibility);
    }

    private static void updateVisibilityOnButtonClick(
            FullscreenSigninView view, PropertyModel model) {
        final boolean showSigninProgressSpinner =
                model.get(FullscreenSigninProperties.SHOW_SIGNIN_PROGRESS_SPINNER_WITH_TEXT)
                        || model.get(FullscreenSigninProperties.SHOW_SIGNIN_PROGRESS_SPINNER);
        final boolean isSelectedAccountSupervised =
                model.get(FullscreenSigninProperties.IS_SELECTED_ACCOUNT_SUPERVISED);
        final boolean showSigningInText =
                model.get(FullscreenSigninProperties.SHOW_SIGNIN_PROGRESS_SPINNER_WITH_TEXT);

        if (showSigninProgressSpinner) {
            // Transition is only used when the progress spinner is shown.
            TransitionManager.beginDelayedTransition(
                    view, new AutoTransition().setStartDelay(300).setDuration(300));
        }
        final int bottomGroupVisibility = showSigninProgressSpinner ? View.INVISIBLE : View.VISIBLE;
        view.getSelectedAccountView().setVisibility(bottomGroupVisibility);
        if (!isSelectedAccountSupervised) {
            // Only adjust dismiss button visibility if it's not already removed for a child user.
            view.getDismissButtonView().setVisibility(bottomGroupVisibility);
        }
        view.getContinueButtonView().setVisibility(bottomGroupVisibility);
        final CharSequence footerText = model.get(FullscreenSigninProperties.FOOTER_STRING);
        view.getFooterView().setVisibility(footerText == null ? View.GONE : bottomGroupVisibility);

        view.getSigninProgressSpinner()
                .setVisibility(showSigninProgressSpinner ? View.VISIBLE : View.GONE);
        view.getSigninProgressText().setVisibility(showSigningInText ? View.VISIBLE : View.GONE);
    }

    private FullscreenSigninViewBinder() {}
}
