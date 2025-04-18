// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/test/providers/mini_map/test_mini_map.h"

#import <UIKit/UIKit.h>

#import "ios/public/provider/chrome/browser/mini_map/mini_map_api.h"

namespace {
id<MiniMapControllerFactory> g_mini_map_controller_factory;
}

namespace ios {
namespace provider {

id<MiniMapController> CreateMiniMapController(
    NSString* address,
    MiniMapControllerCompletionWithURL completion,
    MiniMapControllerCompletionWithString completionWithQuery) {
  // Mini map is not supported in Tests.
  return [g_mini_map_controller_factory
      createMiniMapControllerForString:address
                            completion:completion
                   completionWithQuery:completionWithQuery];
}

namespace test {

void SetMiniMapControllerFactory(id<MiniMapControllerFactory> factory) {
  g_mini_map_controller_factory = factory;
}

}  // namespace test
}  // namespace provider
}  // namespace ios
