// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/content_suggestions/ui_bundled/magic_stack/placeholder_config.h"

#import "ios/chrome/browser/content_suggestions/ui_bundled/content_suggestions_constants.h"

@implementation PlaceholderConfig

#pragma mark - MagicStackModule

- (ContentSuggestionsModuleType)type {
  return ContentSuggestionsModuleType::kPlaceholder;
}

@end
