// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.privacy_guide;

import static org.chromium.build.NullUtil.assumeNonNull;
import static org.chromium.chrome.browser.privacy_guide.PrivacyGuideUtils.getFragmentFocusViewId;

import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.Menu;
import android.view.MenuInflater;
import android.view.MenuItem;
import android.view.View;
import android.view.ViewGroup;
import android.view.accessibility.AccessibilityEvent;

import androidx.annotation.IntDef;
import androidx.appcompat.app.AppCompatActivity;
import androidx.fragment.app.Fragment;
import androidx.recyclerview.widget.RecyclerView;
import androidx.viewpager2.widget.ViewPager2;

import com.google.android.material.tabs.TabLayout;
import com.google.android.material.tabs.TabLayoutMediator;

import org.chromium.base.ResettersForTesting;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.ObservableSupplierImpl;
import org.chromium.base.supplier.OneshotSupplier;
import org.chromium.build.annotations.Initializer;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.settings.ProfileDependentSetting;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetController;
import org.chromium.components.browser_ui.settings.SettingsFragment;
import org.chromium.components.browser_ui.widget.gesture.BackPressHandler;
import org.chromium.ui.widget.ButtonCompat;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;

/**
 * Fragment containing the Privacy Guide (a walk-through of the most important privacy settings).
 */
@NullMarked
public class PrivacyGuideFragment extends Fragment
        implements BackPressHandler, ProfileDependentSetting, SettingsFragment {
    /**
     * The types of fragments supported. Each fragment corresponds to a step in the privacy guide.
     */
    @Retention(RetentionPolicy.SOURCE)
    @IntDef({
        FragmentType.WELCOME,
        FragmentType.MSBB,
        FragmentType.HISTORY_SYNC,
        FragmentType.SAFE_BROWSING,
        FragmentType.COOKIES,
        FragmentType.AD_TOPICS,
        FragmentType.DONE,
    })
    @interface FragmentType {
        int WELCOME = 0;
        int MSBB = 1;
        int HISTORY_SYNC = 2;
        int SAFE_BROWSING = 3;
        int COOKIES = 4;
        int AD_TOPICS = 5;
        int DONE = 6;
        int MAX_VALUE = DONE;
    }

    public static final List<Integer> ALL_FRAGMENT_TYPE_ORDER =
            Collections.unmodifiableList(
                    Arrays.asList(
                            FragmentType.WELCOME,
                            FragmentType.MSBB,
                            FragmentType.HISTORY_SYNC,
                            FragmentType.SAFE_BROWSING,
                            FragmentType.COOKIES,
                            FragmentType.AD_TOPICS,
                            FragmentType.DONE));

    private OneshotSupplier<BottomSheetController> mBottomSheetControllerSupplier;
    private ObservableSupplierImpl<Boolean> mHandleBackPressChangedSupplier;
    private PrivacyGuidePagerAdapter mPagerAdapter;
    private View mView;
    private ViewPager2 mViewPager;
    private TabLayout mTabLayout;
    private ButtonCompat mStartButton;
    private ButtonCompat mNextButton;
    private ButtonCompat mBackButton;
    private ButtonCompat mFinishButton;
    private ButtonCompat mDoneButton;
    private PrivacyGuideMetricsDelegate mPrivacyGuideMetricsDelegate;
    private NavbarVisibilityDelegate mNavbarVisibilityDelegate;
    private Profile mProfile;
    private ViewPager2.OnPageChangeCallback mOnPageChangeCallback;

    @Override
    public void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setHasOptionsMenu(true);
        mPrivacyGuideMetricsDelegate = new PrivacyGuideMetricsDelegate(mProfile);
        if (savedInstanceState != null) {
            mPrivacyGuideMetricsDelegate.restoreState(savedInstanceState);
        }
        mHandleBackPressChangedSupplier = new ObservableSupplierImpl<>();
    }

    @Override
    public @Nullable View onCreateView(
            LayoutInflater inflater,
            @Nullable ViewGroup container,
            @Nullable Bundle savedInstanceState) {
        modifyAppBar();
        mView = inflater.inflate(R.layout.privacy_guide_steps, container, false);

        mViewPager = (ViewPager2) mView.findViewById(R.id.review_viewpager);
        mPagerAdapter =
                new PrivacyGuidePagerAdapter(
                        this, new StepDisplayHandlerImpl(mProfile), ALL_FRAGMENT_TYPE_ORDER);
        mNavbarVisibilityDelegate = new NavbarVisibilityDelegate(mPagerAdapter.getItemCount());
        mViewPager.setAdapter(mPagerAdapter);
        mViewPager.setPageTransformer(new PrivacyGuidePageTransformer());
        mViewPager.setUserInputEnabled(false);

        // Workaround for ViewPager2 bug: https://issuetracker.google.com/issues/284429851.
        for (int i = 0; i < mViewPager.getChildCount(); i++) {
            var child = mViewPager.getChildAt(i);
            if (child instanceof RecyclerView) {
                child.setFocusable(false);
            }
        }

        mOnPageChangeCallback =
                new ViewPager2.OnPageChangeCallback() {
                    @Override
                    public void onPageScrollStateChanged(int state) {
                        super.onPageScrollStateChanged(state);

                        // We only want to send the accessibility event when the view pager
                        // transition is complete.
                        if (state != ViewPager2.SCROLL_STATE_IDLE) {
                            return;
                        }

                        View targetView =
                                mView.findViewById(
                                        getFragmentFocusViewId(
                                                mPagerAdapter.getFragmentType(
                                                        mViewPager.getCurrentItem())));
                        if (targetView != null) {
                            targetView.sendAccessibilityEvent(AccessibilityEvent.TYPE_VIEW_FOCUSED);
                        }
                    }
                };
        mViewPager.registerOnPageChangeCallback(mOnPageChangeCallback);

        mTabLayout = mView.findViewById(R.id.tab_layout);
        mTabLayout.setFocusable(false);
        new TabLayoutMediator(
                        mTabLayout,
                        mViewPager,
                        (tab, position) -> {
                            tab.view.setFocusable(false);
                            tab.view.setClickable(false);
                            tab.view.setImportantForAccessibility(
                                    View.IMPORTANT_FOR_ACCESSIBILITY_NO);
                            if (position == 0 || position == mPagerAdapter.getItemCount() - 1) {
                                tab.view.setVisibility(View.GONE);
                            }
                        })
                .attach();

        mStartButton = (ButtonCompat) mView.findViewById(R.id.start_button);
        mStartButton.setOnClickListener((View v) -> nextStep());

        mNextButton = (ButtonCompat) mView.findViewById(R.id.next_button);
        mNextButton.setOnClickListener((View v) -> nextStep());

        mBackButton = (ButtonCompat) mView.findViewById(R.id.back_button);
        mBackButton.setOnClickListener((View v) -> previousStep());

        mFinishButton = (ButtonCompat) mView.findViewById(R.id.finish_button);
        mFinishButton.setOnClickListener((View v) -> nextStep());

        mDoneButton = (ButtonCompat) mView.findViewById(R.id.done_button);
        mDoneButton.setOnClickListener(
                (View v) -> {
                    PrivacyGuideMetricsDelegate.recordMetricsForDoneButton();
                    getActivity().finish();
                });

        return mView;
    }

    @Override
    public void onStart() {
        super.onStart();
        updateButtonVisibility();
    }

    @Override
    public void onResume() {
        super.onResume();
        mHandleBackPressChangedSupplier.set(shouldHandleBackPress());
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        if (mOnPageChangeCallback != null) {
            mViewPager.unregisterOnPageChangeCallback(mOnPageChangeCallback);
        }
    }

    private void modifyAppBar() {
        AppCompatActivity settingsActivity = (AppCompatActivity) getActivity();
        settingsActivity.setTitle(R.string.privacy_guide_fragment_title);
        assumeNonNull(settingsActivity.getSupportActionBar()).setDisplayHomeAsUpEnabled(false);
    }

    private void nextStep() {
        int currentIdx = mViewPager.getCurrentItem();
        int nextIdx = currentIdx + 1;

        if (nextIdx >= mPagerAdapter.getItemCount()) {
            // There are no allowed next steps.
            return;
        }

        mViewPager.setCurrentItem(nextIdx);
        updateButtonVisibility();
        mHandleBackPressChangedSupplier.set(shouldHandleBackPress());
        recordMetricsOnButtonPress(currentIdx, nextIdx);
    }

    private void previousStep() {
        int currentIdx = mViewPager.getCurrentItem();
        int prevIdx = currentIdx - 1;

        if (currentIdx <= 0) {
            // There are no allowed previous steps.
            return;
        }

        mViewPager.setCurrentItem(prevIdx);
        updateButtonVisibility();
        mHandleBackPressChangedSupplier.set(shouldHandleBackPress());
        recordMetricsOnButtonPress(currentIdx, prevIdx);
    }

    private void updateButtonVisibility() {
        int currentIdx = mViewPager.getCurrentItem();

        mStartButton.setVisibility(mNavbarVisibilityDelegate.getStartButtonVisibility(currentIdx));
        mNextButton.setVisibility(mNavbarVisibilityDelegate.getNextButtonVisibility(currentIdx));
        mBackButton.setVisibility(mNavbarVisibilityDelegate.getBackButtonVisibility(currentIdx));
        mFinishButton.setVisibility(
                mNavbarVisibilityDelegate.getFinishButtonVisibility(currentIdx));
        mDoneButton.setVisibility(mNavbarVisibilityDelegate.getDoneButtonVisibility(currentIdx));

        mTabLayout.setVisibility(
                mNavbarVisibilityDelegate.getProgressIndicatorVisibility(currentIdx));
    }

    private void recordMetricsOnButtonPress(int currentStepIdx, int followingStepIdx) {
        assert currentStepIdx != followingStepIdx : "currentStepIdx is equal to followingStepIdx";

        // Record metrics when the user clicks the next/back button
        if (currentStepIdx > followingStepIdx) {
            PrivacyGuideMetricsDelegate.recordMetricsOnBackForCard(
                    mPagerAdapter.getFragmentType(currentStepIdx));
        } else {
            mPrivacyGuideMetricsDelegate.recordMetricsOnNextForCard(
                    mPagerAdapter.getFragmentType(currentStepIdx));
        }

        // Record the initial state of the next/previous card
        mPrivacyGuideMetricsDelegate.setInitialStateForCard(
                mPagerAdapter.getFragmentType(followingStepIdx));
    }

    private boolean onBackPressed() {
        if (shouldHandleBackPress()) {
            previousStep();
            return true;
        }

        return false;
    }

    @Override
    public void onAttachFragment(Fragment childFragment) {
        if (childFragment instanceof ProfileDependentSetting) {
            ((ProfileDependentSetting) childFragment).setProfile(mProfile);
        }

        if (childFragment instanceof SafeBrowsingFragment) {
            ((SafeBrowsingFragment) childFragment)
                    .setBottomSheetControllerSupplier(mBottomSheetControllerSupplier);
        }
    }

    @Override
    public void onCreateOptionsMenu(Menu menu, MenuInflater inflater) {
        super.onCreateOptionsMenu(menu, inflater);
        menu.clear();
        inflater.inflate(R.menu.privacy_guide_toolbar_menu, menu);
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        if (item.getItemId() == R.id.close_menu_id) {
            getActivity().finish();
            return true;
        }

        return false;
    }

    @Override
    public void onSaveInstanceState(Bundle outState) {
        mPrivacyGuideMetricsDelegate.saveState(outState);
        super.onSaveInstanceState(outState);
    }

    @Override
    public int handleBackPress() {
        return onBackPressed() ? BackPressResult.SUCCESS : BackPressResult.FAILURE;
    }

    @Override
    public ObservableSupplier<Boolean> getHandleBackPressChangedSupplier() {
        return mHandleBackPressChangedSupplier;
    }

    private boolean shouldHandleBackPress() {
        return mViewPager.getCurrentItem() > 0;
    }

    @Initializer
    public void setBottomSheetControllerSupplier(
            OneshotSupplier<BottomSheetController> bottomSheetControllerSupplier) {
        mBottomSheetControllerSupplier = bottomSheetControllerSupplier;
    }

    void setPrivacyGuideMetricsDelegateForTesting(
            PrivacyGuideMetricsDelegate privacyGuideMetricsDelegate) {
        var oldValue = mPrivacyGuideMetricsDelegate;
        mPrivacyGuideMetricsDelegate = privacyGuideMetricsDelegate;
        ResettersForTesting.register(() -> mPrivacyGuideMetricsDelegate = oldValue);
    }

    @Initializer
    @Override
    public void setProfile(Profile profile) {
        mProfile = profile;
    }

    @Override
    public @AnimationType int getAnimationType() {
        return AnimationType.PROPERTY;
    }
}
