// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.omnibox.suggestions.carousel;

import android.content.Context;
import android.graphics.drawable.Drawable;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;

import androidx.annotation.IntDef;

import org.chromium.build.annotations.NullMarked;
import org.chromium.chrome.browser.omnibox.R;
import org.chromium.chrome.browser.omnibox.styles.OmniboxResourceProvider;
import org.chromium.chrome.browser.omnibox.suggestions.mostvisited.MostVisitedTileViewBinder;
import org.chromium.components.browser_ui.widget.tile.TileView;
import org.chromium.ui.modelutil.MVCListAdapter.ModelList;
import org.chromium.ui.modelutil.SimpleRecyclerViewAdapter;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

/**
 * ViewBuilder for the Carousel suggestion. Its sole responsibility is to inflate appropriate view
 * layouts for supplied view type.
 */
@NullMarked
public class BaseCarouselSuggestionItemViewBuilder {
    /**
     * ViewType defines a list of Views that are understood by the Carousel. Views below can be used
     * by any instance of the carousel, guaranteeing that each instance will look like every other.
     */
    @IntDef({ViewType.TILE_VIEW})
    @Retention(RetentionPolicy.SOURCE)
    public @interface ViewType {
        /** Carousel item is a TileView instance. */
        public int TILE_VIEW = 0;
    }

    /**
     * Create standard Carousel Suggestion View capable of hosting any of the ViewTypes.
     *
     * @param parent ViewGroup that will host the Carousel view.
     * @return BaseCarouselSuggestionView.
     */
    public static BaseCarouselSuggestionView createView(ViewGroup parent) {
        SimpleRecyclerViewAdapter adapter = new SimpleRecyclerViewAdapter(new ModelList());
        adapter.registerType(
                ViewType.TILE_VIEW,
                BaseCarouselSuggestionItemViewBuilder::createTileView,
                MostVisitedTileViewBinder::bind);
        return new BaseCarouselSuggestionView(parent.getContext(), adapter);
    }

    /**
     * Create a standard TileView element.
     *
     * @param parent ViewGroup that will host the Tile.
     * @return A TileView element for the individual URL suggestion.
     */
    private static TileView createTileView(ViewGroup parent) {
        Context context = parent.getContext();
        TileView tile =
                (TileView)
                        LayoutInflater.from(context)
                                .inflate(R.layout.suggestions_tile_view, parent, false);
        tile.setClickable(true);
        tile.setFocusable(true);

        // Update the background color of the solid circle around the icon (typically a favicon).
        Drawable modernizedBackground =
                OmniboxResourceProvider.getDrawable(
                        context, R.drawable.tile_view_icon_background_modern_updated);
        View iconBackground = tile.findViewById(R.id.tile_view_icon_background);
        iconBackground.setBackground(modernizedBackground);

        return tile;
    }
}
