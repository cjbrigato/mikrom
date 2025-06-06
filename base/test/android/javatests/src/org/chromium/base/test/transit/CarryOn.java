// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.test.transit;

import org.chromium.base.test.transit.Transition.TransitionOptions;
import org.chromium.base.test.transit.Transition.Trigger;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;

import java.util.Collections;
import java.util.List;

/** CarryOn is a lightweight, stand-alone ConditionalState not tied to any Station. */
@NullMarked
public class CarryOn extends ConditionalState {

    private final int mId;
    private final String mName;
    private static int sLastCarryOnId = 2000;

    public CarryOn() {
        mId = ++sLastCarryOnId;
        String className = getClass().getSimpleName();
        mName =
                className.isBlank()
                        ? String.format("<C%d>", mId)
                        : String.format("<C%d: %s>", mId, className);
    }

    @Override
    public String getName() {
        return mName;
    }

    /** Transitions into a CarryOn: runs |trigger| and waits for its ENTER Conditions. */
    public static <T extends CarryOn> T pickUp(T carryOn, @Nullable Trigger trigger) {
        return pickUp(carryOn, TransitionOptions.DEFAULT, trigger);
    }

    /** Version of {@link #pickUp(CarryOn, Trigger)} with {@link TransitionOptions}. */
    public static <T extends CarryOn> T pickUp(
            T carryOn, TransitionOptions options, @Nullable Trigger trigger) {
        PickUp pickUp = new PickUp(carryOn, options, trigger);
        pickUp.transitionSync();
        return carryOn;
    }

    /** Transitions out of a CarryOn: runs |trigger| and waits for its EXIT Conditions. */
    public void drop(@Nullable Trigger trigger) {
        drop(TransitionOptions.DEFAULT, trigger);
    }

    /** Version of {@link #drop(Trigger)} with {@link TransitionOptions}. */
    public void drop(TransitionOptions options, @Nullable Trigger trigger) {
        Drop drop = new Drop(this, options, trigger);
        drop.transitionSync();
    }

    /** Convenience method to create a CarryOn from one or more Conditions. */
    public static CarryOn fromConditions(Condition... conditions) {
        CarryOn carryOn = new CarryOn();
        for (Condition condition : conditions) {
            carryOn.declareEnterCondition(condition);
        }
        return carryOn;
    }

    private static class Drop extends Transition {
        private final CarryOn mCarryOn;

        private Drop(CarryOn carryOn, TransitionOptions options, @Nullable Trigger trigger) {
            super(options, List.of(carryOn), Collections.emptyList(), trigger);
            mCarryOn = carryOn;
        }

        @Override
        public String toDebugString() {
            return "Drop " + mCarryOn.getName();
        }
    }

    private static class PickUp extends Transition {
        private final CarryOn mCarryOn;

        private PickUp(CarryOn carryOn, TransitionOptions options, @Nullable Trigger trigger) {
            super(options, Collections.EMPTY_LIST, List.of(carryOn), trigger);
            mCarryOn = carryOn;
        }

        @Override
        public String toDebugString() {
            return "PickUp " + mCarryOn.getName();
        }
    }
}
