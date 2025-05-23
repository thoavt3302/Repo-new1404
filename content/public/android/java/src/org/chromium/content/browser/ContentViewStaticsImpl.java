// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content.browser;

import org.jni_zero.NativeMethods;

import org.chromium.build.annotations.NullMarked;

/** Implementations of {@link ContentViewStatics}. */
@NullMarked
public class ContentViewStaticsImpl {
    /**
     * Suspends Webkit timers in all renderers.
     * New renderers created after this call will be created with the
     * default options.
     *
     * @param suspend true if timers should be suspended.
     */
    public static void setWebKitSharedTimersSuspended(boolean suspend) {
        ContentViewStaticsImplJni.get().setWebKitSharedTimersSuspended(suspend);
    }

    @NativeMethods
    interface Natives {
        // Native functions
        void setWebKitSharedTimersSuspended(boolean suspend);
    }
}
