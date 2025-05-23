// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/attribution_reporting/attribution_suitable_context.h"

#include <stdint.h>

#include <optional>
#include <tuple>
#include <utility>

#include "base/check.h"
#include "base/feature_list.h"
#include "base/memory/weak_ptr.h"
#include "components/attribution_reporting/features.h"
#include "components/attribution_reporting/suitable_origin.h"
#include "components/url_matcher/url_util.h"
#include "content/browser/attribution_reporting/attribution_data_host_manager.h"
#include "content/browser/attribution_reporting/attribution_host.h"
#include "content/browser/attribution_reporting/attribution_input_event.h"
#include "content/browser/attribution_reporting/attribution_manager.h"
#include "content/browser/attribution_reporting/attribution_os_level_manager.h"
#include "content/browser/renderer_host/policy_container_host.h"
#include "content/browser/renderer_host/render_frame_host_impl.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "services/metrics/public/cpp/ukm_source_id.h"
#include "services/network/public/mojom/permissions_policy/permissions_policy_feature.mojom-shared.h"

namespace content {

namespace {

using attribution_reporting::SuitableOrigin;

bool UkmSourceIdAllowed(RenderFrameHostImpl* rfh) {
  return rfh->GetLifecycleState() !=
         RenderFrameHost::LifecycleState::kPrerendering;
}

}  // namespace

// static
std::optional<AttributionSuitableContext> AttributionSuitableContext::Create(
    NavigationHandle* navigation_handle) {
  RenderFrameHostImpl* rfh = static_cast<RenderFrameHostImpl*>(
      navigation_handle->GetRenderFrameHost());
  std::optional<AttributionSuitableContext> context = Create(rfh);
  // Override the UKM source ID from the associated ongoing navigation handle as
  // `AttributionHost::GetPageUkmSourceId()` is not updated until the navigation
  // finishes.
  if (context && UkmSourceIdAllowed(rfh)) {
    context->ukm_source_id_ = navigation_handle->GetNextPageUkmSourceId();
  }
  return context;
}

// static
std::optional<AttributionSuitableContext> AttributionSuitableContext::Create(
    RenderFrameHostImpl* initiator_frame) {
  if (!initiator_frame) {
    return std::nullopt;
  }

  if (!base::FeatureList::IsEnabled(
          attribution_reporting::features::kConversionMeasurement)) {
    return std::nullopt;
  }

  if (!initiator_frame->IsFeatureEnabled(
          network::mojom::PermissionsPolicyFeature::kAttributionReporting)) {
    return std::nullopt;
  }
  RenderFrameHostImpl* initiator_root_frame =
      initiator_frame->GetOutermostMainFrame();
  CHECK(initiator_root_frame);

  // We need a suitable origin here because we need to be able to eventually
  // store it as either the source or destination origin. Using
  // `is_web_secure_context` only would allow opaque origins to pass through,
  // but they cannot be handled by the storage layer.
  std::optional<attribution_reporting::SuitableOrigin>
      initiator_root_frame_origin = SuitableOrigin::Create(
          initiator_root_frame->GetLastCommittedOrigin());
  if (!initiator_root_frame_origin.has_value()) {
    return std::nullopt;
  }
  // If the `initiator_frame` is a subframe, it's origin's security isn't
  // covered by the SuitableOrigin check above, we therefore validate that it's
  // origin is secure using `is_web_secure_context`.
  if (initiator_frame != initiator_root_frame &&
      !initiator_frame->policy_container_host()
           ->policies()
           .is_web_secure_context) {
    return std::nullopt;
  }

  auto* web_contents = WebContents::FromRenderFrameHost(initiator_frame);
  if (!web_contents) {
    return std::nullopt;
  }
  auto* manager = AttributionManager::FromWebContents(web_contents);
  CHECK(manager);

  auto* attribution_host = AttributionHost::FromWebContents(web_contents);
  CHECK(attribution_host);

  AttributionDataHostManager* data_host_manager = manager->GetDataHostManager();
  CHECK(data_host_manager);

  return AttributionSuitableContext(
      /*context_origin=*/std::move(initiator_root_frame_origin.value()),
      initiator_frame->IsNestedWithinFencedFrame(),
      initiator_root_frame->GetGlobalId(), initiator_frame->navigation_id(),
      attribution_host->GetMostRecentNavigationInputEvent(),
      AttributionOsLevelManager::GetAttributionReportingOsRegistrars(
          web_contents),
      !url_matcher::util::GetGoogleAmpViewerEmbeddedURL(
           initiator_root_frame->GetLastCommittedURL())
           .is_empty(),
      UkmSourceIdAllowed(initiator_root_frame)
          ? attribution_host->GetPageUkmSourceId()
          : ukm::kInvalidSourceId,
      data_host_manager->AsWeakPtr());
}

// static
AttributionSuitableContext AttributionSuitableContext::CreateForTesting(
    attribution_reporting::SuitableOrigin context_origin,
    bool is_nested_within_fenced_frame,
    GlobalRenderFrameHostId root_render_frame_id,
    int64_t last_navigation_id,
    AttributionInputEvent last_input_event,
    ContentBrowserClient::AttributionReportingOsRegistrars os_registrars,
    AttributionDataHostManager* attribution_data_host_manager,
    bool is_context_google_amp_viewer,
    ukm::SourceId ukm_source_id) {
  return AttributionSuitableContext(
      std::move(context_origin), is_nested_within_fenced_frame,
      root_render_frame_id, last_navigation_id, last_input_event, os_registrars,
      is_context_google_amp_viewer, ukm_source_id,
      attribution_data_host_manager ? attribution_data_host_manager->AsWeakPtr()
                                    : nullptr);
}

bool AttributionSuitableContext::operator==(
    const AttributionSuitableContext& other) const {
  const auto tie = [](const AttributionSuitableContext& c) {
    // We don't check the `attribution_data_host_manager_` property since we'd
    // consider two contexts equal even if the manager is no longer available.
    // We also don't check the `last_input_event_` property which is time
    // sensitive.
    return std::make_tuple(c.context_origin(),
                           c.is_nested_within_fenced_frame(),
                           c.last_navigation_id(), c.root_render_frame_id());
  };
  return tie(*this) == tie(other);
}

AttributionSuitableContext::AttributionSuitableContext(
    attribution_reporting::SuitableOrigin context_origin,
    bool is_nested_within_fenced_frame,
    GlobalRenderFrameHostId root_render_frame_id,
    int64_t last_navigation_id,
    AttributionInputEvent last_input_event,
    ContentBrowserClient::AttributionReportingOsRegistrars os_registrars,
    bool is_context_google_amp_viewer,
    ukm::SourceId ukm_source_id,
    base::WeakPtr<AttributionDataHostManager> attribution_data_host_manager)
    : context_origin_(std::move(context_origin)),
      is_nested_within_fenced_frame_(is_nested_within_fenced_frame),
      root_render_frame_id_(root_render_frame_id),
      last_navigation_id_(last_navigation_id),
      last_input_event_(std::move(last_input_event)),
      os_registrars_(os_registrars),
      is_context_google_amp_viewer_(is_context_google_amp_viewer),
      ukm_source_id_(ukm_source_id),
      attribution_data_host_manager_(attribution_data_host_manager) {}

AttributionSuitableContext::AttributionSuitableContext(
    const AttributionSuitableContext&) = default;

AttributionSuitableContext& AttributionSuitableContext::operator=(
    const AttributionSuitableContext&) = default;

AttributionSuitableContext::AttributionSuitableContext(
    AttributionSuitableContext&&) = default;

AttributionSuitableContext& AttributionSuitableContext::operator=(
    AttributionSuitableContext&&) = default;

AttributionSuitableContext::~AttributionSuitableContext() = default;

}  // namespace content
