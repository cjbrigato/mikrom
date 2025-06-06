// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import static org.mockito.Mockito.verify;

import androidx.test.filters.SmallTest;

import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.base.test.util.Feature;

/** Tests for {@link TraceEvent}. */
@RunWith(BaseRobolectricTestRunner.class)
public class TraceEventTest {

    @Mock TraceEvent.Natives mNativeMock;

    @Before
    public void setUp() {
        MockitoAnnotations.initMocks(this);
        TraceEventJni.setInstanceForTesting(mNativeMock);
    }

    @Test
    @SmallTest
    @Feature({"Android-AppBase"})
    public void testDisableEventNameFiltering() {
        TraceEvent.setEventNameFilteringEnabled(false);
        Assert.assertFalse(TraceEvent.eventNameFilteringEnabled());
    }

    @Test
    @SmallTest
    @Feature({"Android-AppBase"})
    public void testEnableEventNameFiltering() {
        TraceEvent.setEventNameFilteringEnabled(true);
        Assert.assertTrue(TraceEvent.eventNameFilteringEnabled());
    }

    @Test
    @SmallTest
    @Feature({"Android-AppBase"})
    public void testEventNameUnfiltered() {
        TraceEvent.setEventNameFilteringEnabled(false);
        Assert.assertFalse(TraceEvent.eventNameFilteringEnabled());

        // Input string format:
        // ">>>>> Finished to (TARGET) {HASH_CODE} TARGET_NAME: WHAT"
        String realEventName =
                ">>>>> Finished to (org.chromium.myClass.myMethod) "
                        + "{HASH_CODE} org.chromium.myOtherClass.instance: message";

        // Output string format:
        // "{TraceEvent.BasicLooperMonitor.LOOPER_TASK_PREFIX} TARGET(TARGET_NAME)"
        String realEventNameExpected =
                TraceEvent.BasicLooperMonitor.LOOPER_TASK_PREFIX
                        + "org.chromium.myClass.myMethod(org.chromium.myOtherClass.instance)";
        Assert.assertEquals(
                TraceEvent.BasicLooperMonitor.getTraceEventName(realEventName),
                realEventNameExpected);
    }

    @Test
    @SmallTest
    @Feature({"Android-AppBase"})
    public void testEventNameFiltered() {
        TraceEvent.setEventNameFilteringEnabled(true);
        Assert.assertTrue(TraceEvent.eventNameFilteringEnabled());

        String realEventName =
                TraceEvent.BasicLooperMonitor.LOOPER_TASK_PREFIX
                        + "org.chromium.myClass.myMethod(org.chromium.myOtherClass.instance)";
        Assert.assertEquals(
                TraceEvent.BasicLooperMonitor.FILTERED_EVENT_NAME,
                TraceEvent.BasicLooperMonitor.getTraceEventName(realEventName));
    }

    @Test
    @SmallTest
    @Feature({"Android-AppBase"})
    public void testScopedTraceEventWithIntArg() {
        TraceEvent.setEnabled(true);
        // Only string literals are allowed in Java event names.
        try (TraceEvent event = TraceEvent.scoped("TestEvent", 15)) {}
        verify(mNativeMock).beginWithIntArg("TestEvent", 15);
        TraceEvent.setEnabled(false);
    }

    @Test
    @SmallTest
    @Feature({"Android-AppBase"})
    public void testWebViewStartupTotalFactoryInit() {
        TraceEvent.setEnabled(true);
        long startTime = 10;
        long duration = 50;
        TraceEvent.webViewStartupTotalFactoryInit(startTime, duration);
        verify(mNativeMock).webViewStartupTotalFactoryInit(startTime, duration);
        TraceEvent.setEnabled(false);
    }

    @Test
    @SmallTest
    @Feature({"Android-AppBase"})
    public void testWebViewStartupStage1() {
        TraceEvent.setEnabled(true);
        long startTime = 10;
        long duration = 50;
        TraceEvent.webViewStartupStage1(startTime, duration);
        verify(mNativeMock).webViewStartupStage1(startTime, duration);
        TraceEvent.setEnabled(false);
    }

    @Test
    @SmallTest
    @Feature({"Android-AppBase"})
    public void testWebViewStartupFirstInstance() {
        TraceEvent.setEnabled(true);
        long startTime = 10;
        long duration = 50;
        boolean includedGlobalStartup = true;
        TraceEvent.webViewStartupFirstInstance(startTime, duration, includedGlobalStartup);
        verify(mNativeMock).webViewStartupFirstInstance(startTime, duration, includedGlobalStartup);
        TraceEvent.setEnabled(false);
    }

    @Test
    @SmallTest
    @Feature({"Android-AppBase"})
    public void testWebViewStartupNotFirstInstance() {
        TraceEvent.setEnabled(true);
        long startTime = 10;
        long duration = 50;
        TraceEvent.webViewStartupNotFirstInstance(startTime, duration);
        verify(mNativeMock).webViewStartupNotFirstInstance(startTime, duration);
        TraceEvent.setEnabled(false);
    }
}
