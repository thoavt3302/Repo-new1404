// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_ATTRIBUTION_REPORTING_ATTRIBUTION_HOST_H_
#define CONTENT_BROWSER_ATTRIBUTION_REPORTING_ATTRIBUTION_HOST_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <optional>
#include <string>

#include "base/containers/flat_set.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "build/buildflag.h"
#include "components/attribution_reporting/data_host.mojom-forward.h"
#include "content/browser/attribution_reporting/attribution_suitable_context.h"
#include "content/common/content_export.h"
#include "content/public/browser/render_frame_host_receiver_set.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "services/metrics/public/cpp/ukm_source_id.h"
#include "third_party/blink/public/mojom/conversions/conversions.mojom.h"

namespace url {
class Origin;
}  // namespace url

namespace content {

struct AttributionInputEvent;
class RenderFrameHost;
class WebContents;

#if BUILDFLAG(IS_ANDROID)
class AttributionInputEventTrackerAndroid;
#endif

// Class responsible for listening to conversion events originating from blink,
// and verifying that they are valid. Owned by the WebContents. Lifetime is
// bound to lifetime of the WebContents.
class CONTENT_EXPORT AttributionHost
    : public WebContentsObserver,
      public WebContentsUserData<AttributionHost>,
      public blink::mojom::AttributionHost {
 public:
  explicit AttributionHost(WebContents* web_contents);
  AttributionHost(const AttributionHost&) = delete;
  AttributionHost& operator=(const AttributionHost&) = delete;
  AttributionHost(AttributionHost&&) = delete;
  AttributionHost& operator=(AttributionHost&&) = delete;
  ~AttributionHost() override;

  static void BindReceiver(
      mojo::PendingAssociatedReceiver<blink::mojom::AttributionHost> receiver,
      RenderFrameHost* rfh);

  AttributionInputEvent GetMostRecentNavigationInputEvent() const;

#if BUILDFLAG(IS_ANDROID)
  AttributionInputEventTrackerAndroid* input_event_tracker() {
    return input_event_tracker_android_.get();
  }
#endif

  ukm::SourceId GetPageUkmSourceId() const {
    return last_primary_frame_ukm_source_id_;
  }

 private:
  friend class AttributionHostTestPeer;
  friend class WebContentsUserData<AttributionHost>;

  struct PrimaryMainFrameData {
    PrimaryMainFrameData();
    PrimaryMainFrameData(const PrimaryMainFrameData&) = delete;
    PrimaryMainFrameData& operator=(const PrimaryMainFrameData&) = delete;
    PrimaryMainFrameData(PrimaryMainFrameData&&);
    PrimaryMainFrameData& operator=(PrimaryMainFrameData&&);
    ~PrimaryMainFrameData();

    int num_data_hosts_registered = 0;
    // This is used for DWA metrics.
    std::map<url::Origin, int> num_data_hosts_registered_by_reporting_origin;
    bool has_user_activation = false;
    bool has_user_interaction = false;
  };

  // blink::mojom::AttributionHost:
  void NotifyNavigationWithBackgroundRegistrationsWillStart(
      const blink::AttributionSrcToken& attribution_src_token,
      uint32_t expected_registrations) override;
  void RegisterDataHost(
      mojo::PendingReceiver<attribution_reporting::mojom::DataHost>,
      attribution_reporting::mojom::RegistrationEligibility,
      bool is_for_background_requests,
      const std::vector<url::Origin>& reporting_origins) override;
  void RegisterNavigationDataHost(
      mojo::PendingReceiver<attribution_reporting::mojom::DataHost> data_host,
      const blink::AttributionSrcToken& attribution_src_token) override;

  // WebContentsObserver:
  void DidStartNavigation(NavigationHandle* navigation_handle) override;
  void DidRedirectNavigation(NavigationHandle* navigation_handle) override;
  void DidFinishNavigation(NavigationHandle* navigation_handle) override;
  void FrameReceivedUserActivation(RenderFrameHost* render_frame_host) override;
  void DidGetUserInteraction(const blink::WebInputEvent& event) override;

  void NotifyNavigationRegistrationData(NavigationHandle* navigation_handle);

  void MaybeLogClientBounce(NavigationHandle* navigation_handle) const;

  // Keeps track of navigations for which we can register sources (i.e. All
  // conditions were met in `DidStartNavigation` and
  // `DataHostManager::NotifyNavigationRegistrationStarted` was called). This
  // avoids making useless calls or checks when processing responses in
  // `DidRedirectNavigation` and `DidFinishNavigation` for navigations for which
  // we can't register sources.
  base::flat_set<int64_t> ongoing_registration_eligible_navigations_;

  RenderFrameHostReceiverSet<blink::mojom::AttributionHost> receivers_;

#if BUILDFLAG(IS_ANDROID)
  std::unique_ptr<AttributionInputEventTrackerAndroid>
      input_event_tracker_android_;
#endif

  std::optional<base::Time> last_navigation_time_;
  std::optional<PrimaryMainFrameData> primary_main_frame_data_;
  ukm::SourceId last_primary_frame_ukm_source_id_ = ukm::kInvalidSourceId;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace content

#endif  // CONTENT_BROWSER_ATTRIBUTION_REPORTING_ATTRIBUTION_HOST_H_
