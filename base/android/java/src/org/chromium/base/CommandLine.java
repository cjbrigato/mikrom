// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import static org.chromium.build.NullUtil.assumeNonNull;

import android.text.TextUtils;

import androidx.annotation.VisibleForTesting;

import org.jni_zero.JniType;
import org.jni_zero.NativeMethods;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.build.annotations.RequiresNonNull;

import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Java mirror of base/command_line.h. Android applications don't have command line arguments.
 * Instead, they're "simulated" by reading a file at a specific location early during startup.
 * Applications each define their own files, e.g., ContentShellApplication.COMMAND_LINE_FILE.
 *
 * <p>Starts as being backed by a Java Map, and then switches to JNI once the native library is
 * loaded.
 *
 * <p>Thread Safety: The native implementation requires initialization and all writes to occur on
 * the same sequence (e.g. main thread), but reads can happen from any thread (although it's not
 * clear why this is safe...). The Java implementation is thread-safe through the use of locks, but
 * only until switching to the native implementation.
 *
 * <p>TODO(crbug.com/40258939): Enable native sequence checker.
 */
@NullMarked
@SuppressWarnings({"NoSynchronizedMethodCheck", "NoSynchronizedThisCheck"}) // Unnecessary advice.
public class CommandLine {
    private static final String TAG = "CommandLine";
    private static final String SWITCH_PREFIX = "--";
    private static final String SWITCH_TERMINATOR = SWITCH_PREFIX;
    private static final String SWITCH_VALUE_SEPARATOR = "=";
    private static final CommandLine sInstance = new CommandLine();

    // Fields are initialized by initInternal() and set to null upon switching to native impl.
    private @Nullable Map<String, String> mSwitches;
    private @Nullable ArrayList<String> mArgs;

    // The index of the first non-switch argument (first arg that does not start with --).
    private volatile int mArgsBegin;

    /** Returns true if the command line has already been initialized. */
    public static boolean isInitialized() {
        // Not initialized: mArgsBegin == 0
        // Initialized, but pre-native: mArgsBegin > 0 && mArgs != null
        // Initialized & using native impl: mArgsBegin > 0 && mArgs == null
        return sInstance.mArgsBegin != 0;
    }

    /** Determine if the command line is bound to the native (JNI) implementation. */
    public static boolean hasSwitchedToNative() {
        assert isInitialized();
        return sInstance.mArgs == null;
    }

    // Equivalent to CommandLine::ForCurrentProcess in C++.
    public static CommandLine getInstance() {
        assert isInitialized();
        return sInstance;
    }

    /**
     * Initialize from an array of tokenized args.
     *
     * @param args command line flags in 'argv' format: args[0] is the program name.
     */
    public static void init(String @Nullable [] args) {
        sInstance.initInternal(args);
    }

    /**
     * @param fileName the file to read in.
     * @return Array of chars read from the file, or null if the file cannot be read.
     */
    private static char @Nullable [] readFileAsUtf8(String fileName) {
        File f = new File(fileName);
        try (FileReader reader = new FileReader(f)) {
            char[] buffer = new char[(int) f.length()];
            int charsRead = reader.read(buffer);
            // charsRead < f.length() in the case of multibyte characters.
            return Arrays.copyOfRange(buffer, 0, charsRead);
        } catch (IOException e) {
            return null; // Most likely file not found.
        }
    }

    /** Initialize using arguments from the given command-line file. */
    public static void initFromFile(String file) {
        char[] buffer = readFileAsUtf8(file);
        String[] tokenized = buffer == null ? null : tokenizeQuotedArguments(buffer);
        init(tokenized);
        // The file existed, which should never be the case under normal operation.
        // Use a log message to help with debugging if it's the flags that are causing issues.
        if (tokenized != null) {
            Log.i(TAG, "COMMAND-LINE FLAGS: %s (from %s)", Arrays.toString(tokenized), file);
        }
    }

    /** For use by tests that test command-line functionality. */
    public static void resetForTesting(boolean initialize) {
        CommandLine instance = sInstance;
        var origSwitches = instance.mSwitches;
        var origArgs = instance.mArgs;
        var origArgsBegin = instance.mArgsBegin;
        instance.mSwitches = null;
        instance.mArgs = null;
        instance.mArgsBegin = 0;
        if (initialize) {
            instance.initInternal(null);
        }
        ResettersForTesting.register(
                () -> {
                    instance.mSwitches = origSwitches;
                    instance.mArgs = origArgs;
                    instance.mArgsBegin = origArgsBegin;
                });
    }

    /**
     * Parse command line flags from a flat buffer, supporting double-quote enclosed strings
     * containing whitespace. argv elements are derived by splitting the buffer on whitepace; double
     * quote characters may enclose tokens containing whitespace; a double-quote literal may be
     * escaped with back-slash. (Otherwise backslash is taken as a literal).
     *
     * @param buffer A command line in command line file format as described above.
     * @return the tokenized arguments, suitable for passing to init().
     */
    @VisibleForTesting
    static String[] tokenizeQuotedArguments(char[] buffer) {
        // Just field trials can take over 60K of command line.
        if (buffer.length > 96 * 1024) {
            // Check that our test runners are setting a reasonable number of flags.
            throw new RuntimeException("Flags file too big: " + buffer.length);
        }

        ArrayList<String> args = new ArrayList<String>();
        StringBuilder arg = null;
        final char noQuote = '\0';
        final char singleQuote = '\'';
        final char doubleQuote = '"';
        char currentQuote = noQuote;
        for (char c : buffer) {
            // Detect start or end of quote block.
            if ((currentQuote == noQuote && (c == singleQuote || c == doubleQuote))
                    || c == currentQuote) {
                if (arg != null && arg.length() > 0 && arg.charAt(arg.length() - 1) == '\\') {
                    // Last char was a backslash; pop it, and treat c as a literal.
                    arg.setCharAt(arg.length() - 1, c);
                } else {
                    currentQuote = currentQuote == noQuote ? c : noQuote;
                }
            } else if (currentQuote == noQuote && Character.isWhitespace(c)) {
                if (arg != null) {
                    args.add(arg.toString());
                    arg = null;
                }
            } else {
                if (arg == null) arg = new StringBuilder();
                arg.append(c);
            }
        }
        if (arg != null) {
            if (currentQuote != noQuote) {
                Log.w(TAG, "Unterminated quoted string: %s", arg);
            }
            args.add(arg.toString());
        }
        return args.toArray(new String[args.size()]);
    }

    /** Switch from Java->Native CommandLine implementation. Safe to call redundantly. */
    public synchronized void switchToNativeImpl() {
        if (hasSwitchedToNative()) {
            // Already switched to native.
            return;
        }
        CommandLineJni.get().init(assumeNonNull(mArgs));
        mArgs = null;
        mSwitches = null;
        Log.v(TAG, "Switched to native command-line");
    }

    /** Returns the list of current switches. Cannot be called after enableNativeProxy(). */
    public static String[] getJavaSwitchesForTesting() {
        CommandLine commandLine = sInstance;
        if (commandLine == null) {
            return new String[0];
        }
        return assumeNonNull(commandLine.mArgs).toArray(new String[0]);
    }

    private synchronized void initInternal(String @Nullable [] args) {
        // TODO(agrieve): Make it an error to be called twice in all cases.
        assert !isInitialized() || !hasSwitchedToNative()
                : "Cannot reinitialize after having switched to native.";
        mArgs = new ArrayList<>();
        mSwitches = new HashMap<>();
        mArgsBegin = 1;
        // Invariant: we always have the argv[0] program name element.
        if (args == null || args.length == 0 || args[0] == null) {
            mArgs.add("");
        } else {
            mArgs.add(args[0]);
            appendSwitchesInternalLocked(args, 1);
        }
    }

    /**
     * Return the value associated with the given switch, or {@code defaultValue} if the switch was
     * not specified.
     *
     * @param switchString The switch key to lookup. It should NOT start with '--' !
     * @param defaultValue The default value to return if the switch isn't set.
     * @return Switch value, or {@code defaultValue} if the switch is not set or set to empty.
     */
    public String getSwitchValue(String switchString, String defaultValue) {
        String value = getSwitchValue(switchString);
        return TextUtils.isEmpty(value) ? defaultValue : value;
    }

    /**
     * Returns true if this command line contains the given switch. (Switch names ARE
     * case-sensitive).
     */
    public boolean hasSwitch(String switchString) {
        Map<String, String> switches = mSwitches;
        if (switches == null) {
            return CommandLineJni.get().hasSwitch(switchString);
        }
        synchronized (this) {
            return switches.containsKey(switchString);
        }
    }

    /**
     * Return the value associated with the given switch, or null.
     *
     * @param switchString The switch key to lookup. It should NOT start with '--' !
     * @return switch value, or null if the switch is not set or set to empty.
     */
    public @Nullable String getSwitchValue(String switchString) {
        Map<String, String> switches = mSwitches;
        String ret;
        if (switches == null) {
            ret = CommandLineJni.get().getSwitchValue(switchString);
        } else {
            synchronized (this) {
                ret = switches.get(switchString);
            }
        }
        // Native does not distinguish empty values from key not present.
        return TextUtils.isEmpty(ret) ? null : ret;
    }

    /** Return a copy of all switches, along with their values. */
    public Map<String, String> getSwitches() {
        Map<String, String> switches = mSwitches;
        if (switches == null) {
            return CommandLineJni.get().getSwitches();
        }
        synchronized (this) {
            return new HashMap<>(switches);
        }
    }

    /**
     * Append a switch to the command line. There is no guarantee this action happens before the
     * switch is needed.
     *
     * @param switchString the switch to add. It should NOT start with '--' !
     */
    public void appendSwitch(String switchString) {
        appendSwitchWithValue(switchString, null);
    }

    /**
     * Append a switch and value to the command line. There is no guarantee this action happens
     * before the switch is needed.
     *
     * @param switchString the switch to add. It should NOT start with '--' !
     * @param value the value for this switch. For example, --foo=bar becomes 'foo', 'bar'.
     */
    public synchronized void appendSwitchWithValue(String switchString, @Nullable String value) {
        if (value == null) {
            value = "";
        }

        if (mSwitches == null) {
            CommandLineJni.get().appendSwitchWithValue(switchString, value);
            return;
        }
        mSwitches.put(switchString, value);

        // Append the switch and update the switches/arguments divider mArgsBegin.
        String combinedSwitchString = SWITCH_PREFIX + switchString;
        if (!value.isEmpty()) {
            combinedSwitchString += SWITCH_VALUE_SEPARATOR + value;
        }

        assumeNonNull(mArgs);
        mArgs.add(mArgsBegin++, combinedSwitchString);
    }

    /**
     * Append switch/value items in "command line" format (excluding argv[0] program name). E.g. {
     * '--gofast', '--username=fred' }
     *
     * @param array an array of switch or switch/value items in command line format. Unlike the
     *     other append routines, these switches SHOULD start with '--' . Unlike init(), this does
     *     not include the program name in array[0].
     */
    public synchronized void appendSwitchesAndArguments(String[] array) {
        if (mArgs == null) {
            CommandLineJni.get().appendSwitchesAndArguments(array);
        } else {
            appendSwitchesInternalLocked(array, 0);
        }
    }

    // Add the specified arguments, but skipping the first |skipCount| elements.
    @RequiresNonNull("mArgs")
    private void appendSwitchesInternalLocked(String[] array, int skipCount) {
        boolean parseSwitches = true;
        for (String arg : array) {
            if (skipCount > 0) {
                --skipCount;
                continue;
            }

            if (arg.equals(SWITCH_TERMINATOR)) {
                parseSwitches = false;
            }

            if (parseSwitches && arg.startsWith(SWITCH_PREFIX)) {
                String[] parts = arg.split(SWITCH_VALUE_SEPARATOR, 2);
                String value = parts.length > 1 ? parts[1] : null;
                appendSwitchWithValue(parts[0].substring(SWITCH_PREFIX.length()), value);
            } else {
                mArgs.add(arg);
            }
        }
    }

    /**
     * Remove the switch from the command line. If no such switch is present, this has no effect.
     *
     * @param switchString The switch key to lookup. It should NOT start with '--' !
     */
    public synchronized void removeSwitch(String switchString) {
        ArrayList<String> args = mArgs;
        if (args == null) {
            CommandLineJni.get().removeSwitch(switchString);
            return;
        }
        assumeNonNull(mSwitches);
        mSwitches.remove(switchString);
        String combinedSwitchString = SWITCH_PREFIX + switchString;

        // Since we permit a switch to be added multiple times, we need to remove all instances
        // from mArgs.
        for (int i = mArgsBegin - 1; i > 0; i--) {
            if (args.get(i).equals(combinedSwitchString)
                    || args.get(i).startsWith(combinedSwitchString + SWITCH_VALUE_SEPARATOR)) {
                --mArgsBegin;
                args.remove(i);
            }
        }
    }

    @NativeMethods
    interface Natives {
        void init(@JniType("std::vector<std::string>") List<String> args);

        boolean hasSwitch(@JniType("std::string") String switchString);

        @JniType("std::string")
        String getSwitchValue(@JniType("std::string") String switchString);

        @JniType("base::CommandLine::SwitchMap")
        Map<String, String> getSwitches();

        void appendSwitchWithValue(
                @JniType("std::string") String switchString, @JniType("std::string") String value);

        void appendSwitchesAndArguments(@JniType("std::vector<std::string>") String[] array);

        void removeSwitch(@JniType("std::string") String switchString);
    }
}
