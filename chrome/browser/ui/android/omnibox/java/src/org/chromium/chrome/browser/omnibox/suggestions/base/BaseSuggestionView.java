// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.omnibox.suggestions.base;

import android.content.Context;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;

import androidx.annotation.LayoutRes;
import androidx.annotation.VisibleForTesting;
import androidx.appcompat.content.res.AppCompatResources;
import androidx.appcompat.widget.AppCompatImageView;

import org.chromium.build.annotations.CheckDiscard;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.omnibox.R;
import org.chromium.chrome.browser.omnibox.suggestions.SimpleSelectionController;
import org.chromium.chrome.browser.util.KeyNavigationUtil;
import org.chromium.components.browser_ui.widget.RoundedCornerOutlineProvider;

import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.OptionalInt;

/**
 * Base layout for common suggestion types. Includes support for a configurable suggestion content
 * and the common suggestion patterns shared across suggestion formats.
 *
 * @param <T> The type of View being wrapped by this container.
 */
@NullMarked
public class BaseSuggestionView<T extends View> extends SuggestionLayout {
    public final ImageView decorationIcon;
    public final T contentView;
    public final ActionChipsView actionChipsView;
    public final RoundedCornerOutlineProvider decorationIconOutline;
    private final List<ImageView> mActionButtons;
    private final SimpleSelectionController mActionButtonsHighlighter;
    private Optional<Runnable> mOnFocusViaSelectionListener = Optional.empty();

    /**
     * Constructs a new suggestion view and inflates supplied layout as the contents view.
     *
     * @param context The context used to construct the suggestion view.
     * @param layoutId Layout ID to be inflated as the contents view.
     */
    public BaseSuggestionView(Context context, @LayoutRes int layoutId) {
        this((T) LayoutInflater.from(context).inflate(layoutId, null));
    }

    /**
     * Constructs a new suggestion view.
     *
     * @param view The view wrapped by the suggestion containers.
     */
    public BaseSuggestionView(T view) {
        super(view.getContext());

        setClickable(true);
        setFocusable(true);

        decorationIconOutline = new RoundedCornerOutlineProvider();

        decorationIcon = new ImageView(getContext());
        decorationIcon.setOutlineProvider(decorationIconOutline);
        decorationIcon.setScaleType(ImageView.ScaleType.FIT_CENTER);
        addView(
                decorationIcon,
                LayoutParams.forViewType(LayoutParams.SuggestionViewType.DECORATION));

        actionChipsView = new ActionChipsView(getContext());
        actionChipsView.setVisibility(GONE);
        addView(actionChipsView, LayoutParams.forViewType(LayoutParams.SuggestionViewType.FOOTER));

        mActionButtons = new ArrayList<>();

        contentView = view;
        contentView.setLayoutParams(
                LayoutParams.forViewType(LayoutParams.SuggestionViewType.CONTENT));
        addView(contentView);

        mActionButtonsHighlighter =
                new SimpleSelectionController(
                        this::highlightActionButton,
                        0,
                        SimpleSelectionController.Mode.SATURATING_WITH_SENTINEL);
    }

    /**
     * Prepare (truncate or add) Action views for the Suggestion.
     *
     * @param desiredViewCount Number of action views for this suggestion.
     */
    void setActionButtonsCount(int desiredViewCount) {
        final int currentViewCount = mActionButtons.size();

        if (currentViewCount < desiredViewCount) {
            increaseActionButtonsCount(desiredViewCount);
        } else if (currentViewCount > desiredViewCount) {
            decreaseActionButtonsCount(desiredViewCount);
        }

        mActionButtonsHighlighter.setItemCount(desiredViewCount);
    }

    /**
     * @return List of Action views.
     */
    public List<ImageView> getActionButtons() {
        return mActionButtons;
    }

    /**
     * Applies / removes selection hairline from action button.
     *
     * @param buttonIndex the index of an action button
     * @param isSelected whether to apply hairline
     */
    private void highlightActionButton(int buttonIndex, boolean isHighlighted) {
        mActionButtons
                .get(buttonIndex)
                .setForeground(
                        isHighlighted
                                ? AppCompatResources.getDrawable(
                                        getContext(), R.drawable.hairline_circle)
                                : null);
    }

    /**
     * Create additional action buttons for the suggestion view.
     *
     * @param desiredViewCount Desired number of action buttons.
     */
    private void increaseActionButtonsCount(int desiredViewCount) {
        for (int index = mActionButtons.size(); index < desiredViewCount; index++) {
            ImageView actionView = new AppCompatImageView(getContext());
            actionView.setClickable(true);
            actionView.setFocusable(true);
            actionView.setScaleType(ImageView.ScaleType.CENTER);

            actionView.setLayoutParams(
                    LayoutParams.forViewType(LayoutParams.SuggestionViewType.ACTION_BUTTON));
            mActionButtons.add(actionView);
            addView(actionView);
        }
    }

    /**
     * Remove unused action views from the suggestion view.
     *
     * @param desiredViewCount Desired target number of action buttons.
     */
    private void decreaseActionButtonsCount(int desiredViewCount) {
        for (int index = desiredViewCount; index < mActionButtons.size(); index++) {
            removeView(mActionButtons.get(index));
        }
        mActionButtons.subList(desiredViewCount, mActionButtons.size()).clear();
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        // Pass event to ActionChips first in case this key event is appropriate for ActionChip
        // navigation.
        if (actionChipsView.onKeyDown(keyCode, event)) return true;
        if (KeyNavigationUtil.isEnter(event)) {
            if (!mActionButtonsHighlighter.isParkedAtSentinel()) {
                OptionalInt selection = mActionButtonsHighlighter.getPosition();
                return mActionButtons.get(selection.getAsInt()).performClick();
            }
            return performClick();
        }

        // Allow browsing through right hand side buttons.
        if (keyCode == KeyEvent.KEYCODE_TAB) {
            if (!event.isShiftPressed()) {
                // Pass the TAB key to Action Buttons, then to Action Chips.
                return mActionButtonsHighlighter.selectNextItem()
                        || super_onKeyDown(keyCode, event);
            } else {
                // Pass the TAB key to Action Chips, then to Action Buttons.
                return super_onKeyDown(keyCode, event)
                        || mActionButtonsHighlighter.selectPreviousItem();
            }
        }

        return super_onKeyDown(keyCode, event);
    }

    @CheckDiscard("inlined")
    @VisibleForTesting
    /* package */ boolean super_onKeyDown(int keyCode, KeyEvent event) {
        return super.onKeyDown(keyCode, event);
    }

    @Override
    public void setSelected(boolean selected) {
        super.setSelected(selected);
        if (mActionButtonsHighlighter != null) mActionButtonsHighlighter.reset();
        if (selected) mOnFocusViaSelectionListener.ifPresent(Runnable::run);
    }

    /**
     * Specify the listener receiving a call when the user highlights this Suggestion.
     *
     * @param listener The listener to be notified about selection.
     */
    void setOnFocusViaSelectionListener(@Nullable Runnable listener) {
        mOnFocusViaSelectionListener = Optional.ofNullable(listener);
    }

    /** Set the lead-in spacing for the action chip carousel. */
    public void setActionChipLeadInSpacing(int spacing) {
        actionChipsView.setLeadInSpacing(spacing);
    }

    /**
     * Sets whether the decoration should be "large" or not; see {@link
     * SuggestionLayout#SuggestionLayout(Context)} for the exact size difference.
     */
    public void setUseLargeDecorationIcon(boolean useLargeDecorationIcon) {
        ViewGroup.LayoutParams oldParams = decorationIcon.getLayoutParams();
        if (useLargeDecorationIcon) {
            decorationIcon.setLayoutParams(SuggestionLayout.LayoutParams.forLargeDecorationIcon());
        } else {
            decorationIcon.setLayoutParams(
                    LayoutParams.forViewType(LayoutParams.SuggestionViewType.DECORATION));
        }

        decorationIcon.getLayoutParams().width = oldParams.width;
        decorationIcon.getLayoutParams().height = oldParams.height;
    }

    /** Control whether the decoration icon should be visible. */
    public void setShowDecorationIcon(boolean shouldShow) {
        decorationIcon.setVisibility(shouldShow ? VISIBLE : GONE);
    }
}
