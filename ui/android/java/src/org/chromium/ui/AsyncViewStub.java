// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.ui;

import android.annotation.SuppressLint;
import android.content.Context;
import android.content.res.TypedArray;
import android.graphics.Canvas;
import android.util.AttributeSet;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewParent;

import androidx.asynclayoutinflater.view.AsyncLayoutInflater;

import org.chromium.base.Callback;
import org.chromium.base.ObserverList;
import org.chromium.base.ThreadUtils;
import org.chromium.base.TraceEvent;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;

/**
 * An implementation of ViewStub that inflates the view in a background thread. Callbacks are still
 * called on the UI thread.
 *
 * <p>TODO(crbug.com/40937701): Deprecate AsyncViewStub or make it per activity.
 */
@NullMarked
public class AsyncViewStub extends View implements AsyncLayoutInflater.OnInflateFinishedListener {
    private int mLayoutResource;
    private @Nullable View mInflatedView;

    private final AsyncLayoutInflater mAsyncLayoutInflater;

    private final ObserverList<Callback<View>> mListeners = new ObserverList<>();
    private boolean mOnBackground;

    public AsyncViewStub(Context context, AttributeSet attrs) {
        super(context, attrs);
        final TypedArray a = context.obtainStyledAttributes(attrs, R.styleable.AsyncViewStub);
        mLayoutResource = a.getResourceId(R.styleable.AsyncViewStub_layout, 0);
        a.recycle();

        setVisibility(GONE);
        setWillNotDraw(true);

        mAsyncLayoutInflater = new AsyncLayoutInflater(getContext());
    }

    /**
     * Specifies the layout resource to inflate when {@link #inflate()} is invoked. The View
     * created by inflating the layout resource is used to replace this AsyncViewStub in its parent.
     *
     * @param layoutResource A valid layout resource identifier (different from 0.)
     */
    public void setLayoutResource(int layoutResource) {
        mLayoutResource = layoutResource;
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        setMeasuredDimension(0, 0);
    }

    @SuppressLint("MissingSuperCall")
    @Override
    public void draw(Canvas canvas) {}

    @Override
    protected void dispatchDraw(Canvas canvas) {}

    @Override
    public void onInflateFinished(View view, int resId, @Nullable ViewGroup parent) {
        assert parent != null;
        mInflatedView = view;
        replaceSelfWithView(view, parent);
        callListeners(view);
    }

    /**
     * Starts background inflation for the stub, the AsyncViewStub must be attached to the window
     * (ie have a parent) before you call inflate on it. Must be called on the UI thread.
     */
    public void inflate() {
        try (TraceEvent te = TraceEvent.scoped("AsyncViewStub.inflate")) {
            ThreadUtils.assertOnUiThread();
            final ViewParent viewParent = getParent();
            assert viewParent != null;
            assert viewParent instanceof ViewGroup;
            assert mLayoutResource != 0;
            if (mOnBackground) {
                // AsyncLayoutInflater uses its own thread and cannot inflate <merge> elements. It
                // might be a good idea to write our own version to use our scheduling primitives
                // and to handle <merge> inflations.
                mAsyncLayoutInflater.inflate(mLayoutResource, (ViewGroup) viewParent, this);
            } else {
                ViewGroup inflatedView =
                        (ViewGroup)
                                LayoutInflater.from(getContext())
                                        .inflate(mLayoutResource, (ViewGroup) viewParent, false);
                onInflateFinished(inflatedView, mLayoutResource, (ViewGroup) viewParent);
            }
        }
    }

    private void callListeners(View view) {
        try (TraceEvent te = TraceEvent.scoped("AsyncViewStub.callListeners")) {
            ThreadUtils.assertOnUiThread();
            for (Callback<View> listener : mListeners) {
                listener.onResult(view);
            }
            mListeners.clear();
        }
    }

    /**
     * This should only be used by {@link AsyncViewProvider}, use {@link
     * AsyncViewProvider#whenLoaded} instead.
     *
     * Adds listener that gets called once the view is inflated and added to the view hierarchy. The
     * listeners are called on the UI thread. This method can only be called on the UI thread.
     *
     * @param listener the listener to add.
     */
    void addOnInflateListener(Callback<View> listener) {
        ThreadUtils.assertOnUiThread();
        if (mInflatedView != null) {
            listener.onResult(mInflatedView);
        } else {
            mListeners.addObserver(listener);
        }
    }

    /**
     * @return the inflated view or null if inflation is not complete yet.
     */
    @Nullable
    View getInflatedView() {
        return mInflatedView;
    }

    private void replaceSelfWithView(View view, ViewGroup parent) {
        try (TraceEvent te = TraceEvent.scoped("AsyncViewStub.replaceSelfWithView")) {
            int index = parent.indexOfChild(this);
            parent.removeViewInLayout(this);
            final ViewGroup.LayoutParams layoutParams = getLayoutParams();
            if (layoutParams != null) {
                parent.addView(view, index, layoutParams);
            } else {
                parent.addView(view, index);
            }
        }
    }

    /**
     * Sets whether the view should be inflated on a background thread or the UI thread (the
     * default). This method should not be called after the view has been inflated.
     * @param shouldInflateOnBackgroundThread True if the view should be inflated on a background
     * thread, false otherwise.
     */
    public void setShouldInflateOnBackgroundThread(boolean shouldInflateOnBackgroundThread) {
        assert mInflatedView == null;
        mOnBackground = shouldInflateOnBackgroundThread;
    }
}
