// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.customtabs.test;

import static org.chromium.build.NullUtil.assertNonNull;
import static org.chromium.build.NullUtil.assumeNonNull;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.ActivityManager;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Debug;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.RadioButton;

import androidx.browser.customtabs.CustomTabsCallback;
import androidx.browser.customtabs.CustomTabsClient;
import androidx.browser.customtabs.CustomTabsIntent;
import androidx.browser.customtabs.CustomTabsServiceConnection;
import androidx.browser.customtabs.CustomTabsSession;
import androidx.core.app.BundleCompat;

import org.chromium.build.annotations.MonotonicNonNull;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Random;
import java.util.Set;

/**
 * Activity used to benchmark Custom Tabs PLT.
 *
 * <p>This activity contains benchmark code for two modes: <br>
 * 1. Comparison between a basic use of Custom Tabs and a basic use of WebView. <br>
 * 2. Custom Tabs benchmarking under various scenarios.
 *
 * <p>The two modes are not merged into one as the metrics we can extract in the two cases are
 * constrained for the first one by what WebView provides.
 */
@NullMarked
public class MainActivity extends Activity implements View.OnClickListener {
    static final String TAG = "CUSTOMTABSBENCH";
    static final String TAGCSV = "CUSTOMTABSBENCHCSV";
    private static final String MEMORY_TAG = "CUSTOMTABSMEMORY";
    private static final String DEFAULT_URL = "https://www.android.com";
    private static final String DEFAULT_PACKAGE = "com.google.android.apps.chrome";
    private static final int NONE = -1;
    // Common key between the benchmark modes.
    private static final String URL_KEY = "url";
    private static final String PARALLEL_URL_KEY = "parallel_url";
    private static final String DEFAULT_REFERRER_URL = "https://www.google.com";
    // Keys for the WebView / Custom Tabs comparison.
    static final String INTENT_SENT_EXTRA = "intent_sent_ms";
    private static final String USE_WEBVIEW_KEY = "use_webview";
    private static final String WARMUP_KEY = "warmup";

    // extraCommand related constants.
    private static final String SET_PRERENDER_ON_CELLULAR = "setPrerenderOnCellularForSession";
    private static final String SET_SPECULATION_MODE = "setSpeculationModeForSession";
    private static final String SET_IGNORE_URL_FRAGMENTS_FOR_SESSION =
            "setIgnoreUrlFragmentsForSession";

    private static final String ADD_VERIFIED_ORIGN = "addVerifiedOriginForSession";
    private static final String ENABLE_PARALLEL_REQUEST = "enableParallelRequestForSession";
    private static final String PARALLEL_REQUEST_REFERRER_KEY =
            "android.support.customtabs.PARALLEL_REQUEST_REFERRER";
    private static final String PARALLEL_REQUEST_URL_KEY =
            "android.support.customtabs.PARALLEL_REQUEST_URL";
    private static final int PARALLEL_REQUEST_MIN_DELAY_AFTER_WARMUP = 3000;

    private static final int NO_SPECULATION = 0;
    private static final int PRERENDER = 2;
    private static final int HIDDEN_TAB = 3;

    private final Handler mHandler = new Handler(Looper.getMainLooper());

    private EditText mUrlEditText;
    private RadioButton mChromeRadioButton;
    private RadioButton mWebViewRadioButton;
    private CheckBox mWarmupCheckbox;
    private CheckBox mParallelUrlCheckBox;
    private EditText mParallelUrlEditText;
    private long mIntentSentMs;
    private @MonotonicNonNull CustomTabsIntent mIntent;

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        final Intent intent = getIntent();

        setUpUi();

        // Automated mode, 1s later to leave time for the app to settle.
        if (intent.getStringExtra(URL_KEY) != null) {
            mHandler.postDelayed(
                    new Runnable() {
                        @Override
                        public void run() {
                            processArguments(intent);
                        }
                    },
                    1000);
        }
    }

    /** Displays the UI and registers the click listeners. */
    private void setUpUi() {
        setContentView(R.layout.main);

        mUrlEditText = findViewById(R.id.url_text);
        mChromeRadioButton = findViewById(R.id.radio_chrome);
        mWebViewRadioButton = findViewById(R.id.radio_webview);
        mWarmupCheckbox = findViewById(R.id.warmup_checkbox);
        mParallelUrlCheckBox = findViewById(R.id.parallel_url_checkbox);
        mParallelUrlEditText = findViewById(R.id.parallel_url_text);

        Button goButton = findViewById(R.id.go_button);

        mUrlEditText.setOnClickListener(this);
        mChromeRadioButton.setOnClickListener(this);
        mWebViewRadioButton.setOnClickListener(this);
        mWarmupCheckbox.setOnClickListener(this);
        mParallelUrlCheckBox.setOnClickListener(this);
        mParallelUrlEditText.setOnClickListener(this);
        goButton.setOnClickListener(this);
    }

    @Override
    public void onClick(View v) {
        int id = v.getId();

        boolean warmup = mWarmupCheckbox.isChecked();
        boolean useChrome = mChromeRadioButton.isChecked();
        boolean useWebView = mWebViewRadioButton.isChecked();
        String url = mUrlEditText.getText().toString();
        boolean willRequestParallelUrl = mParallelUrlCheckBox.isChecked();
        String parallelUrl = null;
        if (willRequestParallelUrl) {
            parallelUrl = mParallelUrlEditText.getText().toString();
        }

        if (id == R.id.go_button) {
            customTabsWebViewBenchmark(url, useChrome, useWebView, warmup, parallelUrl);
        }
    }

    /** Routes to either of the benchmark modes. */
    private void processArguments(Intent intent) {
        if (intent.hasExtra(USE_WEBVIEW_KEY)) {
            startCustomTabsWebViewBenchmark(intent);
        } else {
            startCustomTabsBenchmark(intent);
        }
    }

    /**
     * Start the CustomTabs / WebView comparison benchmark.
     *
     * <p>NOTE: Methods below are for the first benchmark mode.
     */
    private void startCustomTabsWebViewBenchmark(Intent intent) {
        Bundle extras = intent.getExtras();
        assumeNonNull(extras);
        String url = extras.getString(URL_KEY);
        String parallelUrl = extras.getString(PARALLEL_URL_KEY);
        boolean useWebView = extras.getBoolean(USE_WEBVIEW_KEY);
        boolean useChrome = !useWebView;
        boolean warmup = extras.getBoolean(WARMUP_KEY);
        assertNonNull(url);
        customTabsWebViewBenchmark(url, useChrome, useWebView, warmup, parallelUrl);
    }

    /** Start the CustomTabs / WebView comparison benchmark. */
    private void customTabsWebViewBenchmark(
            String url,
            boolean useChrome,
            boolean useWebView,
            boolean warmup,
            @Nullable String parallelUrl) {
        if (useChrome) {
            launchChrome(url, warmup, parallelUrl);
        } else {
            assert useWebView;
            launchWebView(url);
        }
    }

    private void launchWebView(@Nullable String url) {
        Intent intent = new Intent();
        intent.setData(Uri.parse(url));
        intent.setClass(this, WebViewActivity.class);
        intent.putExtra(INTENT_SENT_EXTRA, now());
        startActivity(intent);
    }

    private void launchChrome(@Nullable String url, boolean warmup, @Nullable String parallelUrl) {
        CustomTabsServiceConnection connection =
                new CustomTabsServiceConnection() {
                    @Override
                    public void onCustomTabsServiceConnected(
                            ComponentName name, CustomTabsClient client) {
                        launchChromeIntent(url, warmup, client, parallelUrl);
                    }

                    @Override
                    public void onServiceDisconnected(ComponentName name) {}
                };
        CustomTabsClient.bindCustomTabsService(this, DEFAULT_PACKAGE, connection);
    }

    private static void maybePrepareParallelUrlRequest(
            @Nullable String parallelUrl,
            CustomTabsClient client,
            CustomTabsIntent intent,
            IBinder sessionBinder) {
        if (parallelUrl == null || parallelUrl.length() == 0) {
            Log.w(TAG, "null or empty parallelUrl");
            return;
        }

        Uri parallelUri = Uri.parse(parallelUrl);
        Bundle params = new Bundle();
        BundleCompat.putBinder(params, "session", sessionBinder);

        Uri referrerUri = Uri.parse(DEFAULT_REFERRER_URL);
        params.putParcelable("origin", referrerUri);

        Bundle result = client.extraCommand(ADD_VERIFIED_ORIGN, params);
        boolean ok = (result != null) && result.getBoolean(ADD_VERIFIED_ORIGN);
        if (!ok) throw new RuntimeException("Cannot add verified origin");

        result = client.extraCommand(ENABLE_PARALLEL_REQUEST, params);
        ok = (result != null) && result.getBoolean(ENABLE_PARALLEL_REQUEST);
        if (!ok) throw new RuntimeException("Cannot enable Parallel Request");
        Log.w(TAG, "enabled Parallel Request");

        intent.intent.putExtra(PARALLEL_REQUEST_URL_KEY, parallelUri);
        intent.intent.putExtra(PARALLEL_REQUEST_REFERRER_KEY, referrerUri);
    }

    private void launchChromeIntent(
            @Nullable String url,
            boolean warmup,
            CustomTabsClient client,
            @Nullable String parallelUrl) {
        CustomTabsCallback callback =
                new CustomTabsCallback() {
                    private long mNavigationStartOffsetMs;

                    @Override
                    public void onNavigationEvent(int navigationEvent, @Nullable Bundle extras) {
                        long offsetMs = now() - mIntentSentMs;
                        switch (navigationEvent) {
                            case CustomTabsCallback.NAVIGATION_STARTED:
                                mNavigationStartOffsetMs = offsetMs;
                                Log.w(TAG, "navigationStarted = " + offsetMs);
                                break;
                            case CustomTabsCallback.NAVIGATION_FINISHED:
                                Log.w(TAG, "navigationFinished = " + offsetMs);
                                Log.w(TAG, "CHROME," + mNavigationStartOffsetMs + "," + offsetMs);
                                break;
                            default:
                                break;
                        }
                    }
                };
        CustomTabsSession session = client.newSession(callback);
        final CustomTabsIntent customTabsIntent = new CustomTabsIntent.Builder(session).build();
        final Uri uri = Uri.parse(url);

        IBinder sessionBinder =
                BundleCompat.getBinder(
                        assertNonNull(customTabsIntent.intent.getExtras()),
                        CustomTabsIntent.EXTRA_SESSION);
        assert sessionBinder != null;
        maybePrepareParallelUrlRequest(parallelUrl, client, customTabsIntent, sessionBinder);

        if (warmup) {
            client.warmup(0);
            mHandler.postDelayed(
                    new Runnable() {
                        @Override
                        public void run() {
                            mIntentSentMs = now();
                            customTabsIntent.launchUrl(MainActivity.this, uri);
                        }
                    },
                    3000);
        } else {
            mIntentSentMs = now();
            customTabsIntent.launchUrl(MainActivity.this, uri);
        }
    }

    static long now() {
        return System.currentTimeMillis();
    }

    /** Holds the file and the range for pinning. Used only in the 'Pinning Benchmark' mode. */
    private static class PinInfo {
        public boolean pinningBenchmark;
        public @Nullable String fileName;
        public int offset;
        public int length;

        public PinInfo() {}

        public PinInfo(
                boolean pinningBenchmark, @Nullable String fileName, int offset, int length) {
            this.pinningBenchmark = pinningBenchmark;
            this.fileName = fileName;
            this.offset = offset;
            this.length = length;
        }
    }

    /**
     * Holds immutable parameters of the benchmark that are not needed after launching an intent.
     *
     * There are a few parameters that need to be written to the CSV line, those better fit in the
     * {@link CustomCallback}.
     */
    private static class LaunchInfo {
        public final String url;
        public final String speculatedUrl;
        @Nullable public final String parallelUrl;
        public final int timeoutSeconds;

        public LaunchInfo(
                String url,
                String speculatedUrl,
                @Nullable String parallelUrl,
                int timeoutSeconds) {
            this.url = url;
            this.speculatedUrl = speculatedUrl;
            this.parallelUrl = parallelUrl;
            this.timeoutSeconds = timeoutSeconds;
        }
    }

    /**
     * Start the second benchmark mode.
     *
     * <p>NOTE: Methods below are for the second mode.
     */
    private void startCustomTabsBenchmark(Intent intent) {
        String url = intent.getStringExtra(URL_KEY);
        if (url == null) url = DEFAULT_URL;
        String parallelUrl = intent.getStringExtra(PARALLEL_URL_KEY);

        String speculatedUrl = intent.getStringExtra("speculated_url");
        if (speculatedUrl == null) speculatedUrl = url;
        String packageName = intent.getStringExtra("package_name");
        if (packageName == null) packageName = DEFAULT_PACKAGE;
        boolean warmup = intent.getBooleanExtra("warmup", false);

        boolean skipLauncherActivity = intent.getBooleanExtra("skip_launcher_activity", false);
        int delayToMayLaunchUrl = intent.getIntExtra("delay_to_may_launch_url", NONE);
        int delayToLaunchUrl = intent.getIntExtra("delay_to_launch_url", NONE);
        String speculationMode = intent.getStringExtra("speculation_mode");
        if (speculationMode == null) speculationMode = "prerender";
        int timeoutSeconds = intent.getIntExtra("timeout", NONE);

        PinInfo pinInfo;
        if (!intent.getBooleanExtra("pinning_benchmark", false)) {
            pinInfo = new PinInfo();
        } else {
            pinInfo =
                    new PinInfo(
                            true,
                            intent.getStringExtra("pin_filename"),
                            intent.getIntExtra("pin_offset", NONE),
                            intent.getIntExtra("pin_length", NONE));
        }
        int extraBriefMemoryMb = intent.getIntExtra("extra_brief_memory_mb", 0);

        if (parallelUrl != null && !parallelUrl.equals("") && !warmup) {
            if (pinInfo.pinningBenchmark) {
                String message = "Warming up while pinning is not interesting";
                Log.e(TAG, message);
                throw new RuntimeException(message);
            }
            Log.w(TAG, "Parallel URL provided, forcing warmup");
            warmup = true;
            delayToLaunchUrl = Math.max(delayToLaunchUrl, PARALLEL_REQUEST_MIN_DELAY_AFTER_WARMUP);
            delayToMayLaunchUrl =
                    Math.max(delayToMayLaunchUrl, PARALLEL_REQUEST_MIN_DELAY_AFTER_WARMUP);
        }

        final CustomCallback cb =
                new CustomCallback(
                        packageName,
                        warmup,
                        skipLauncherActivity,
                        speculationMode,
                        delayToMayLaunchUrl,
                        delayToLaunchUrl,
                        pinInfo,
                        extraBriefMemoryMb);
        launchCustomTabs(cb, new LaunchInfo(url, speculatedUrl, parallelUrl, timeoutSeconds));
    }

    private final class CustomCallback extends CustomTabsCallback {
        public final String packageName;
        public final boolean warmup;
        public final boolean skipLauncherActivity;
        public final String speculationMode;
        public final int delayToMayLaunchUrl;
        public final int delayToLaunchUrl;
        public boolean warmupCompleted;
        public long intentSentMs = NONE;
        public long pageLoadStartedMs = NONE;
        public long pageLoadFinishedMs = NONE;
        public long firstContentfulPaintMs = NONE;
        public long largeContentfulPaintMs = NONE;
        public final PinInfo pinInfo;
        public final long extraBriefMemoryMb;

        public CustomCallback(
                String packageName,
                boolean warmup,
                boolean skipLauncherActivity,
                String speculationMode,
                int delayToMayLaunchUrl,
                int delayToLaunchUrl,
                PinInfo pinInfo,
                long extraBriefMemoryMb) {
            this.packageName = packageName;
            this.warmup = warmup;
            this.skipLauncherActivity = skipLauncherActivity;
            this.speculationMode = speculationMode;
            this.delayToMayLaunchUrl = delayToMayLaunchUrl;
            this.delayToLaunchUrl = delayToLaunchUrl;
            this.pinInfo = pinInfo;
            this.extraBriefMemoryMb = extraBriefMemoryMb;
        }

        public void recordIntentHasBeenSent() {
            intentSentMs = SystemClock.uptimeMillis();
        }

        @Override
        public void onNavigationEvent(int navigationEvent, @Nullable Bundle extras) {
            switch (navigationEvent) {
                case CustomTabsCallback.NAVIGATION_STARTED:
                    if (pageLoadStartedMs != NONE) return;
                    pageLoadStartedMs = SystemClock.uptimeMillis();
                    break;
                case CustomTabsCallback.NAVIGATION_FINISHED:
                    if (pageLoadFinishedMs != NONE) return;
                    pageLoadFinishedMs = SystemClock.uptimeMillis();
                    mHandler.postDelayed(
                            () -> {
                                // In order to record LCP, we need to navigate away, so give a few
                                // seconds to let LCP settle after page load finishes.
                                assumeNonNull(mIntent);
                                mIntent.launchUrl(MainActivity.this, Uri.parse("about:blank"));
                            },
                            3000);
                    break;
                default:
                    break;
            }
            if (allSet()) logMetricsAndFinish();
        }

        @Override
        public void extraCallback(String callbackName, @Nullable Bundle args) {
            if ("onWarmupCompleted".equals(callbackName)) {
                warmupCompleted = true;
                return;
            }

            if (!"NavigationMetrics".equals(callbackName)) {
                Log.w(TAG, "Unknown extra callback skipped: " + callbackName);
                return;
            }
            assumeNonNull(args);
            long firstPaintMs = args.getLong("firstContentfulPaint", NONE);
            long largePaintMs = args.getLong("largestContentfulPaint", NONE);
            long navigationStartMs = args.getLong("navigationStart", NONE);
            if ((firstPaintMs == NONE && largePaintMs == NONE) || navigationStartMs == NONE) return;
            // Can be reported several times, only record the first one.
            if (firstContentfulPaintMs == NONE && firstPaintMs != NONE) {
                firstContentfulPaintMs = navigationStartMs + firstPaintMs;
            }
            if (largeContentfulPaintMs == NONE && largePaintMs != NONE) {
                largeContentfulPaintMs = navigationStartMs + largePaintMs;
            }
            if (allSet()) logMetricsAndFinish();
        }

        private boolean allSet() {
            return intentSentMs != NONE
                    && pageLoadStartedMs != NONE
                    && firstContentfulPaintMs != NONE
                    && pageLoadFinishedMs != NONE
                    && largeContentfulPaintMs != NONE;
        }

        /** Outputs the available metrics, and die. Unavalaible metrics are set to -1. */
        private void logMetricsAndFinish() {
            if (MainActivity.this.isFinishing()) return;
            String logLine =
                    (warmup ? "1" : "0")
                            + ","
                            + (skipLauncherActivity ? "1" : "0")
                            + ","
                            + speculationMode
                            + ","
                            + delayToMayLaunchUrl
                            + ","
                            + delayToLaunchUrl
                            + ","
                            + intentSentMs
                            + ","
                            + pageLoadStartedMs
                            + ","
                            + pageLoadFinishedMs
                            + ","
                            + firstContentfulPaintMs
                            + ","
                            + largeContentfulPaintMs;
            if (pinInfo.pinningBenchmark) {
                logLine += ',' + extraBriefMemoryMb + ',' + pinInfo.length;
            }
            Log.w(TAGCSV, logLine);
            logMemory(packageName, "AfterMetrics");
            MainActivity.this.finish();
        }

        /** Same as {@link #logMetricsAndFinish()} with a set delay in ms. */
        public void logMetricsAndFinishDelayed(int delayMs) {
            mHandler.postDelayed(
                    new Runnable() {
                        @Override
                        public void run() {
                            logMetricsAndFinish();
                        }
                    },
                    delayMs);
        }
    }

    /**
     * Sums all the memory usage of a package, and returns (PSS, Private Dirty).
     *
     * Only works for packages where a service is exported by each process, which is the case for
     * Chrome. Also, doesn't work on O and above, as
     * {@link ActivityManager#getRunningServices(int)}} is restricted.
     *
     * @param context Application context
     * @param packageName the package to query
     * @return {pss, privateDirty} in kB, or null.
     */
    private static int @Nullable [] getPackagePssAndPrivateDirty(
            Context context, String packageName) {
        if (Build.VERSION.SDK_INT > Build.VERSION_CODES.N_MR1) return null;

        ActivityManager am = (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
        List<ActivityManager.RunningServiceInfo> services = am.getRunningServices(1000);
        if (services == null) return null;

        Set<Integer> pids = new HashSet<>();
        for (ActivityManager.RunningServiceInfo info : services) {
            if (packageName.equals(info.service.getPackageName())) pids.add(info.pid);
        }

        int[] pidsArray = new int[pids.size()];
        int i = 0;
        for (int pid : pids) pidsArray[i++] = pid;
        Debug.MemoryInfo[] infos = am.getProcessMemoryInfo(pidsArray);
        if (infos == null || infos.length == 0) return null;

        int pss = 0;
        int privateDirty = 0;
        for (Debug.MemoryInfo info : infos) {
            pss += info.getTotalPss();
            privateDirty += info.getTotalPrivateDirty();
        }

        return new int[] {pss, privateDirty};
    }

    private void logMemory(String packageName, String message) {
        int[] pssAndPrivateDirty =
                getPackagePssAndPrivateDirty(getApplicationContext(), packageName);
        if (pssAndPrivateDirty == null) return;
        Log.w(MEMORY_TAG, message + "," + pssAndPrivateDirty[0] + "," + pssAndPrivateDirty[1]);
    }

    private static void forceSpeculationMode(
            CustomTabsClient client, IBinder sessionBinder, String speculationMode) {
        // The same bundle can be used for all calls, as the commands only look for their own
        // arguments in it.
        Bundle params = new Bundle();
        BundleCompat.putBinder(params, "session", sessionBinder);
        params.putBoolean("ignoreFragments", true);
        params.putBoolean("prerender", true);

        int speculationModeValue =
                switch (speculationMode) {
                    case "disabled" -> NO_SPECULATION;
                    case "prerender" -> PRERENDER;
                    case "hidden_tab" -> HIDDEN_TAB;
                    default -> throw new RuntimeException("Invalid speculation mode");
                };
        params.putInt("speculationMode", speculationModeValue);

        boolean ok = client.extraCommand(SET_PRERENDER_ON_CELLULAR, params) != null;
        if (!ok) throw new RuntimeException("Cannot set cellular prerendering");
        ok = client.extraCommand(SET_IGNORE_URL_FRAGMENTS_FOR_SESSION, params) != null;
        if (!ok) throw new RuntimeException("Cannot set ignoreFragments");
        ok = client.extraCommand(SET_SPECULATION_MODE, params) != null;
        if (!ok) throw new RuntimeException("Cannot set the speculation mode");
    }

    // Declare as public and volatile to prevent it from being optimized out.
    private static volatile ArrayList<byte[]> sExtraArrays = new ArrayList<>();

    private static final int MAX_ALLOCATION_ALLOWED = 1 << 23; // 8 MiB

    private static byte[] createRandomlyFilledArray(int size, Random random) {
        // Fill in small chunks to avoid allocating 2x the size.
        byte[] array = new byte[size];
        final int chunkSize = 1 << 15; // 32 KiB
        byte[] randomBytes = new byte[chunkSize];
        for (int i = 0; i < size / chunkSize; i++) {
            random.nextBytes(randomBytes);
            System.arraycopy(
                    /* src= */ randomBytes,
                    /* srcPos= */ 0,
                    /* dest= */ array,
                    /* destPos= */ i * chunkSize,
                    /* length= */ chunkSize);
        }
        return array;
    }

    // In order for this method to work, the Android system image needs to be modified to export
    // PinnerService and allow any app to call pinRangeFromFile(). Usually pinning is requested by
    // Chrome (in LibraryPrefetcher), but the call is reimplemented here to avoid restarting
    // Chrome unnecessarily.
    @SuppressLint("WrongConstant")
    private boolean pinChrome(@Nullable String fileName, int startOffset, int length) {
        Context context = getApplicationContext();
        Object pinner = context.getSystemService("pinner");
        if (pinner == null) {
            Log.w(TAG, "Cannot get PinnerService.");
            return false;
        }

        try {
            Method pinRangeFromFile =
                    pinner.getClass()
                            .getMethod("pinRangeFromFile", String.class, int.class, int.class);
            boolean ok = (Boolean) pinRangeFromFile.invoke(pinner, fileName, startOffset, length);
            if (!ok) {
                Log.e(TAG, "Not allowed to call the method, should not happen");
                return false;
            } else {
                Log.w(TAG, "Successfully pinned ordered code");
            }
        } catch (NoSuchMethodException | IllegalAccessException | InvocationTargetException ex) {
            Log.w(TAG, "Error invoking the method. " + ex.getMessage());
            return false;
        }
        return true;
    }

    // In order for this method to work, the Android system image needs to be modified to export
    // PinnerService and allow any app to call unpinChromeFiles().
    @SuppressLint("WrongConstant")
    private boolean unpinChrome() {
        Context context = getApplicationContext();
        Object pinner = context.getSystemService("pinner");
        if (pinner == null) {
            Log.w(TAG, "Cannot get PinnerService for unpinning.");
            return false;
        }
        try {
            Method unpinChromeFiles = pinner.getClass().getMethod("unpinChromeFiles");
            boolean ok = (Boolean) unpinChromeFiles.invoke(pinner);
            if (!ok) {
                Log.e(TAG, "Could not make a reflection call to unpinChromeFiles()");
                return false;
            } else {
                Log.i(TAG, "Unpinned Chrome files");
            }
        } catch (NoSuchMethodException | IllegalAccessException | InvocationTargetException ex) {
            Log.w(TAG, "Error invoking the method. " + ex.getMessage());
            return false;
        }
        return true;
    }

    private void consumeExtraMemoryBriefly(long amountMb) {
        // Allocate memory and fill with random data. Randomization is needed to avoid efficient
        // compression of the data in ZRAM.
        Log.i(TAG, "Consuming extra memory (MiB) = " + amountMb);
        int bytesToUse = (int) (amountMb * (1 << 20));
        long beforeFill = SystemClock.uptimeMillis();
        Random random = new Random();
        do {
            // Limit every allocation in size in case there is a per-allocation limit.
            int size = bytesToUse < MAX_ALLOCATION_ALLOWED ? bytesToUse : MAX_ALLOCATION_ALLOWED;
            bytesToUse -= MAX_ALLOCATION_ALLOWED;
            sExtraArrays.add(createRandomlyFilledArray(size, random));
        } while (bytesToUse > 0);
        long afterFill = SystemClock.uptimeMillis() - beforeFill;
        Log.i(TAG, "Time to fill extra memory (ms) = " + afterFill);

        // Allow a number of background apps to be killed.
        int amountToWaitForBackgroundKilling = 3000;
        syncSleepMs(amountToWaitForBackgroundKilling);

        // Free up memory to give Chrome the room to start without killing even more background
        // apps.
        sExtraArrays.clear();
        System.gc();
    }

    private void onCustomTabsServiceConnected(
            CustomTabsClient client, CustomCallback cb, LaunchInfo launchInfo) {
        logMemory(cb.packageName, "OnServiceConnected");

        final CustomTabsSession session = client.newSession(cb);
        mIntent = new CustomTabsIntent.Builder(session).build();
        IBinder sessionBinder =
                BundleCompat.getBinder(
                        assertNonNull(mIntent.intent.getExtras()), CustomTabsIntent.EXTRA_SESSION);
        assert sessionBinder != null;
        forceSpeculationMode(client, sessionBinder, cb.speculationMode);

        final Runnable launchRunnable =
                () -> {
                    logMemory(cb.packageName, "BeforeLaunch");

                    if (cb.warmupCompleted) {
                        maybePrepareParallelUrlRequest(
                                launchInfo.parallelUrl, client, mIntent, sessionBinder);
                    } else {
                        Log.e(TAG, "not warmed up yet!");
                    }

                    mIntent.launchUrl(MainActivity.this, Uri.parse(launchInfo.url));
                    cb.recordIntentHasBeenSent();
                    if (launchInfo.timeoutSeconds != NONE) {
                        cb.logMetricsAndFinishDelayed(launchInfo.timeoutSeconds * 1000);
                    }
                };

        if (cb.pinInfo.pinningBenchmark) {
            mHandler.post(launchRunnable); // Already waited for the delay.
        } else {
            if (cb.warmup) client.warmup(0);
            if (cb.delayToMayLaunchUrl != NONE) {
                final Runnable mayLaunchRunnable =
                        () -> {
                            logMemory(cb.packageName, "BeforeMayLaunchUrl");
                            assumeNonNull(session);
                            session.mayLaunchUrl(Uri.parse(launchInfo.speculatedUrl), null, null);
                            mHandler.postDelayed(launchRunnable, cb.delayToLaunchUrl);
                        };
                mHandler.postDelayed(mayLaunchRunnable, cb.delayToMayLaunchUrl);
            } else {
                mHandler.postDelayed(launchRunnable, cb.delayToLaunchUrl);
            }
        }
    }

    private static void syncSleepMs(int delay) {
        try {
            Thread.sleep(delay);
        } catch (InterruptedException e) {
            Log.w(TAG, "Interrupted: " + e);
        }
    }

    private void continueWithServiceConnection(
            final CustomCallback cb, final LaunchInfo launchInfo) {
        CustomTabsClient.bindCustomTabsService(
                this,
                cb.packageName,
                new CustomTabsServiceConnection() {
                    @Override
                    public void onCustomTabsServiceConnected(
                            ComponentName name, final CustomTabsClient client) {
                        MainActivity.this.onCustomTabsServiceConnected(client, cb, launchInfo);
                    }

                    @Override
                    public void onServiceDisconnected(ComponentName name) {}
                });
    }

    private void launchCustomTabs(CustomCallback cb, LaunchInfo launchInfo) {
        final PinInfo pinInfo = cb.pinInfo;
        if (!pinInfo.pinningBenchmark) {
            continueWithServiceConnection(cb, launchInfo);
            return;
        }
        // Execute off the UI thread to allow slow operations like pinning or eating RAM for
        // dinner.
        Thread thread =
                new Thread(
                        () -> {
                            if (pinInfo.length > 0) {
                                boolean ok =
                                        pinChrome(pinInfo.fileName, pinInfo.offset, pinInfo.length);
                                if (!ok) {
                                    throw new RuntimeException("Failed to pin Chrome file.");
                                }
                            } else {
                                boolean ok = unpinChrome();
                                if (!ok) {
                                    throw new RuntimeException("Failed to unpin Chrome file.");
                                }
                            }
                            // Pinning is async, wait until hopefully it finishes.
                            syncSleepMs(3000);
                            if (cb.extraBriefMemoryMb != 0) {
                                consumeExtraMemoryBriefly(cb.extraBriefMemoryMb);
                            }
                            Log.i(
                                    TAG,
                                    "Waiting for "
                                            + cb.delayToLaunchUrl
                                            + "ms before launching URL");
                            syncSleepMs(cb.delayToLaunchUrl);
                            continueWithServiceConnection(cb, launchInfo);
                        });
        thread.start();
    }
}
