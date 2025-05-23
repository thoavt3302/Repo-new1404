// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content_public.browser;

import org.chromium.build.annotations.NullMarked;

/** Callback interface for WebContents evaluateJavaScript(). */
@NullMarked
public interface JavaScriptCallback {
    /**
     * Called from native in response to evaluateJavaScript().
     * @param jsonResult json result corresponding to JS execution
     */
    void handleJavaScriptResult(String jsonResult);
}
