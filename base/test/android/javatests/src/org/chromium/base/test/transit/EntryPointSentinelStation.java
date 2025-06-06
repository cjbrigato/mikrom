// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.test.transit;

import android.app.Activity;

import org.chromium.build.annotations.NullMarked;

/**
 * The first station in all Public Transit tests.
 *
 * <p>No Transition is made to enter this Station; it's a sentinel for the first transition to
 * happen from a non-null origin Station.
 */
@NullMarked
public class EntryPointSentinelStation extends Station<Activity> {

    public EntryPointSentinelStation() {
        super(null);
    }

    /**
     * Set this station as the entry point for Public Transit tests. Only EntryPoints should call
     * this. Will assert if called twice.
     */
    public void setAsEntryPoint() {
        setStateActiveWithoutTransition();
        TrafficControl.notifyEntryPointSentinelStationCreated(this);
    }
}
