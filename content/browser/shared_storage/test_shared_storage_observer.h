// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_SHARED_STORAGE_TEST_SHARED_STORAGE_OBSERVER_H_
#define CONTENT_BROWSER_SHARED_STORAGE_TEST_SHARED_STORAGE_OBSERVER_H_

#include <optional>
#include <ostream>
#include <string>
#include <tuple>
#include <vector>

#include "base/time/time.h"
#include "content/browser/shared_storage/shared_storage_event_params.h"
#include "content/browser/shared_storage/shared_storage_runtime_manager.h"
#include "content/public/browser/frame_tree_node_id.h"
#include "third_party/blink/public/common/shared_storage/shared_storage_utils.h"
#include "url/gurl.h"

namespace content {

class TestSharedStorageObserver
    : public SharedStorageRuntimeManager::SharedStorageObserverInterface {
 public:
  using AccessScope = blink::SharedStorageAccessScope;

  struct Access {
    AccessScope scope;
    AccessMethod method;
    FrameTreeNodeId main_frame_id;
    std::string owner_origin;
    SharedStorageEventParams params;
    friend bool operator==(const Access& lhs, const Access& rhs);
    friend std::ostream& operator<<(std::ostream& os, const Access& access);
  };

  TestSharedStorageObserver();
  ~TestSharedStorageObserver() override;

  void OnSharedStorageAccessed(const base::Time& access_time,
                               AccessScope scope,
                               AccessMethod method,
                               FrameTreeNodeId main_frame_id,
                               const std::string& owner_origin,
                               const SharedStorageEventParams& params) override;

  void OnUrnUuidGenerated(const GURL& urn_uuid) override;

  void OnConfigPopulated(
      const std::optional<FencedFrameConfig>& config) override;

  void ExpectAccessObserved(const std::vector<Access>& expected_accesses);

 private:
  std::vector<Access> accesses_;
};

}  // namespace content

#endif  // CONTENT_BROWSER_SHARED_STORAGE_TEST_SHARED_STORAGE_OBSERVER_H_
