// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/services/auction_worklet/trusted_signals.h"

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/synchronization/waitable_event.h"
#include "base/test/bind.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "base/test/values_test_util.h"
#include "content/common/features.h"
#include "content/services/auction_worklet/auction_v8_helper.h"
#include "content/services/auction_worklet/worklet_test_util.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "net/http/http_status_code.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"
#include "v8/include/v8-context.h"
#include "v8/include/v8-forward.h"

namespace auction_worklet {

std::ostream& operator<<(std::ostream& out, TrustedSignals::UrlField uf) {
  switch (uf) {
    case TrustedSignals::UrlField::kBase:
      return out << "kBase";
    case TrustedSignals::UrlField::kKeys:
      return out << "kKeys";
    case TrustedSignals::UrlField::kInterestGroupNames:
      return out << "kInterestGroupNames";
    case TrustedSignals::UrlField::kRenderUrls:
      return out << "kRenderUrls";
    case TrustedSignals::UrlField::kAdComponentRenderUrls:
      return out << "kAdComponentRenderUrls";
    case TrustedSignals::UrlField::kExperimentGroupId:
      return out << "kExperimentGroupId";
    case TrustedSignals::UrlField::kSlotSizeParam:
      return out << "kSlotSizeParam";
    case TrustedSignals::UrlField::kAdCreativeScanningMetadata:
      return out << "kAdCreativeScanningMetadata";
    case TrustedSignals::UrlField::kAdComponentCreativeScanningMetadata:
      return out << "kAdComponentCreativeScanningMetadata";
    case TrustedSignals::UrlField::kAdSizes:
      return out << "kAdSizes";
    case TrustedSignals::UrlField::kAdComponentSizes:
      return out << "kAdComponentSizes";
    case TrustedSignals::UrlField::kAdBuyer:
      return out << "kAdBuyer";
    case TrustedSignals::UrlField::kAdComponentBuyer:
      return out << "kAdComponentBuyer";
    case TrustedSignals::UrlField::kBuyerAndSellerReportingIds:
      return out << "kBuyerAndSellerReportingIds";
    case TrustedSignals::UrlField::kNumValues:
      return out << "kNumValues";
  };
}

std::ostream& operator<<(std::ostream& out, const TrustedSignals::UrlPiece fv) {
  return out << "{ " << fv.field << ", \"" << fv.text << "\"}";
}

namespace {

// Json response using bidding format version 1. Keys and values should match
// those in kBaseBiddingJson.
const char kBiddingJsonV1[] = R"(
  {
    "key1": 1,
    "key2": [2],
    "key3": null,
    "key5": "value5",
    "key 6": 6,
    "key=7": 7,
    "key,8": 8
  }
)";

// Common JSON used for most bidding signals tests using the latest bidding
// format version (version 2). key4 and name4 entries are deliberately missing.
const char kBaseBiddingJson[] = R"(
  {
    "keys": {
      "key1": 1,
      "key2": [2],
      "key3": null,
      "key5": "value5",
      "key 6": 6,
      "key=7": 7,
      "key,8": 8
    },
    "perInterestGroupData": {
      "name1": {
        "priorityVector": {
          "foo": 1
        },
        "updateIfOlderThanMs": 3600000
      },
      "name2": {
        "priorityVector": {
          "foo": 2,
          "bar": 3,
          "baz": -3.5
        }
      },
      "name3": {
        "priorityVector": {}
      },
      "name 5": {
        "priorityVector": {"foo": 5}
      },
      "name6\u2603": {
        "priorityVector": {"foo": 6}
      },
      "name7": {
        "updateIfOlderThanMs": 7200000
      },
      "name8": {
        "updateIfOlderThanMs": "10800000"
      }
    }
  }
)";

// Common JSON used for most scoring signals tests.
const char kBaseScoringJsonOldNames[] = R"(
  {
    "renderUrls": {
      "https://foo.test/": 1,
      "https://bar.test/": [2],
      "https://baz.test/": null,
      "https://shared.test/": "render url"
    },
    "adComponentRenderUrls": {
      "https://foosub.test/": 2,
      "https://barsub.test/": [3],
      "https://bazsub.test/": null,
      "https://shared.test/": "ad component url"
    }
  }
)";

const char kBaseScoringJson[] = R"(
  {
    "renderURLs": {
      "https://foo.test/": 1,
      "https://bar.test/": [2],
      "https://baz.test/": null,
      "https://shared.test/": "render url"
    },
    "adComponentRenderURLs": {
      "https://foosub.test/": 2,
      "https://barsub.test/": [3],
      "https://bazsub.test/": null,
      "https://shared.test/": "ad component url"
    }
  }
)";

const char kBaseScoringJsonNewAndOldNames[] = R"(
  {
    "renderURLs": {
      "https://foo.test/": 1,
      "https://bar.test/": [2],
      "https://baz.test/": null,
      "https://shared.test/": "render url"
    },
    "renderUrls": {
      "https://foo.test/": 1,
      "https://bar.test/": [2],
      "https://baz.test/": null,
      "https://shared.test/": "render url"
    },
    "adComponentRenderURLs": {
      "https://foosub.test/": 2,
      "https://barsub.test/": [3],
      "https://bazsub.test/": null,
      "https://shared.test/": "ad component url"
    },
    "adComponentRenderUrls": {
      "https://foosub.test/": 2,
      "https://barsub.test/": [3],
      "https://bazsub.test/": null,
      "https://shared.test/": "ad component url"
    }
  }
)";

const char kHostname[] = "publisher";

class TrustedSignalsTest : public testing::Test {
 public:
  TrustedSignalsTest() {
    v8_helper_ = AuctionV8Helper::Create(AuctionV8Helper::CreateTaskRunner());
    feature_list_.InitAndEnableFeature(
        features::kInterestGroupUpdateIfOlderThan);
  }

  ~TrustedSignalsTest() override { task_environment_.RunUntilIdle(); }

  // Sets the HTTP response and then fetches bidding signals and waits for
  // completion. Includes response header indicating later format version ("2")
  // by default.
  scoped_refptr<TrustedSignals::Result> FetchBiddingSignalsWithResponse(
      const GURL& url,
      const std::string& response,
      std::set<std::string> interest_group_names,
      std::set<std::string> trusted_bidding_signals_keys,
      const std::string& hostname,
      std::optional<uint16_t> experiment_group_id = std::nullopt,
      const std::string& trusted_bidding_signals_slot_size_param = "",
      const std::optional<std::string>& format_version_string = "2") {
    AddBidderJsonResponse(&url_loader_factory_, url, response,
                          /*data_version=*/std::nullopt, format_version_string);

    return FetchBiddingSignals(std::move(interest_group_names),
                               std::move(trusted_bidding_signals_keys),
                               hostname, experiment_group_id,
                               trusted_bidding_signals_slot_size_param);
  }

  // Fetches bidding signals and waits for completion. Returns nullptr on
  // failure.
  scoped_refptr<TrustedSignals::Result> FetchBiddingSignals(
      std::set<std::string> interest_group_names,
      std::set<std::string> trusted_bidding_signals_keys,
      const std::string& hostname,
      std::optional<uint16_t> experiment_group_id,
      const std::string& trusted_bidding_signals_slot_size_param = "") {
    CHECK(!load_signals_run_loop_);

    DCHECK(!load_signals_result_);

    std::vector<TrustedSignals::UrlPiece> main_fragments, aux_fragments;
    TrustedSignals::BuildTrustedBiddingSignalsURL(
        hostname, base_url_, interest_group_names, trusted_bidding_signals_keys,
        experiment_group_id, trusted_bidding_signals_slot_size_param,
        main_fragments, aux_fragments);

    auto bidding_signals = TrustedSignals::LoadBiddingSignals(
        &url_loader_factory_, auction_network_events_handler_.CreateRemote(),
        std::move(interest_group_names),
        std::move(trusted_bidding_signals_keys), base_url_,
        std::move(main_fragments), std::move(aux_fragments), v8_helper_,
        base::BindOnce(&TrustedSignalsTest::LoadSignalsCallback,
                       base::Unretained(this)));
    WaitForLoadComplete();
    return std::move(load_signals_result_);
  }

  // Sets the HTTP response and then fetches scoring signals and waits for
  // completion. Returns nullptr on failure.
  scoped_refptr<TrustedSignals::Result> FetchScoringSignalsWithResponse(
      const GURL& url,
      const std::string& response,
      std::set<std::string> render_urls,
      std::set<std::string> ad_component_render_urls,
      const std::string& hostname,
      std::optional<uint16_t> experiment_group_id) {
    AddJsonResponse(&url_loader_factory_, url, response);
    return FetchScoringSignals(std::move(render_urls),
                               std::move(ad_component_render_urls), hostname,
                               experiment_group_id);
  }

  // Fetches scoring signals and waits for completion. Returns nullptr on
  // failure.
  scoped_refptr<TrustedSignals::Result> FetchScoringSignals(
      std::set<std::string> render_urls,
      std::set<std::string> ad_component_render_urls,
      const std::string& hostname,
      std::optional<uint16_t> experiment_group_id) {
    auto ads = CreateCreativeInfoSet(
        std::vector<std::string>(render_urls.begin(), render_urls.end()));
    auto ad_components = CreateCreativeInfoSet(std::vector<std::string>(
        ad_component_render_urls.begin(), ad_component_render_urls.end()));
    return FetchScoringSignals(std::move(ads), std::move(ad_components),
                               hostname, experiment_group_id);
  }

  scoped_refptr<TrustedSignals::Result> FetchScoringSignals(
      std::set<TrustedSignals::CreativeInfo> ads,
      std::set<TrustedSignals::CreativeInfo> ad_components,
      const std::string& hostname,
      std::optional<uint16_t> experiment_group_id,
      bool send_creative_scanning_metadata = false) {
    base::HistogramTester histogram_tester;
    CHECK(!load_signals_run_loop_);
    DCHECK(!load_signals_result_);
    std::vector<TrustedSignals::UrlPiece> main_fragments, aux_fragments;
    TrustedSignals::BuildTrustedScoringSignalsURL(
        send_creative_scanning_metadata, hostname, base_url_, ads,
        ad_components, experiment_group_id, main_fragments, aux_fragments);

    auto scoring_signals = TrustedSignals::LoadScoringSignals(
        &url_loader_factory_, auction_network_events_handler_.CreateRemote(),
        std::move(ads), std::move(ad_components), hostname, base_url_,
        experiment_group_id, std::move(main_fragments),
        std::move(aux_fragments), send_creative_scanning_metadata, v8_helper_,
        base::BindOnce(&TrustedSignalsTest::LoadSignalsCallback,
                       base::Unretained(this)));
    WaitForLoadComplete();
    histogram_tester.ExpectUniqueSample(
        "Ads.InterestGroup.Auction.TrustedScoringSendCreativeScanningMetadata",
        send_creative_scanning_metadata, /*expected_bucket_count=*/1);
    return std::move(load_signals_result_);
  }

  // Wait for LoadSignalsCallback to be invoked.
  void WaitForLoadComplete() {
    // Since LoadSignalsCallback is always invoked asynchronously, fine to
    // create the RunLoop after creating the TrustedSignals object, which will
    // ultimately trigger the invocation.
    load_signals_run_loop_ = std::make_unique<base::RunLoop>();
    load_signals_run_loop_->Run();
    load_signals_run_loop_.reset();
  }

  // Returns the results of calling TrustedSignals::Result::GetBiddingSignals()
  // with `trusted_bidding_signals_keys`. Returns value as a JSON std::string,
  // for easy testing.
  std::string ExtractBiddingSignals(
      TrustedSignals::Result* signals,
      std::vector<std::string> trusted_bidding_signals_keys) {
    base::RunLoop run_loop;

    std::string result;
    v8_helper_->v8_runner()->PostTask(
        FROM_HERE, base::BindLambdaForTesting([&]() {
          AuctionV8Helper::FullIsolateScope isolate_scope(v8_helper_.get());
          v8::Isolate* isolate = v8_helper_->isolate();
          // Could use the scratch context, but using a separate one more
          // closely resembles actual use.
          v8::Local<v8::Context> context = v8::Context::New(isolate);
          v8::Context::Scope context_scope(context);

          v8::Local<v8::Value> value = signals->GetBiddingSignals(
              v8_helper_.get(), context, trusted_bidding_signals_keys);

          if (v8_helper_->ExtractJson(context, value,
                                      /*script_timeout=*/nullptr, &result) !=
              AuctionV8Helper::Result::kSuccess) {
            result = "JSON extraction failed.";
          }
          run_loop.Quit();
        }));
    run_loop.Run();
    return result;
  }

  // Returns the results of calling TrustedSignals::Result::GetScoringSignals()
  // with the provided parameters. Returns value as a JSON std::string, for easy
  // testing.
  std::string ExtractScoringSignals(
      TrustedSignals::Result* signals,
      const GURL& render_url,
      const std::vector<std::string>& ad_component_render_urls) {
    base::RunLoop run_loop;

    std::string result;
    v8_helper_->v8_runner()->PostTask(
        FROM_HERE, base::BindLambdaForTesting([&]() {
          AuctionV8Helper::FullIsolateScope isolate_scope(v8_helper_.get());
          v8::Isolate* isolate = v8_helper_->isolate();
          // Could use the scratch context, but using a separate one more
          // closely resembles actual use.
          v8::Local<v8::Context> context = v8::Context::New(isolate);
          v8::Context::Scope context_scope(context);

          v8::Local<v8::Value> value = signals->GetScoringSignals(
              v8_helper_.get(), context, render_url,
              CreateMojoCreativeInfoWithoutOwnerVector(
                  ad_component_render_urls));

          if (v8_helper_->ExtractJson(context, value,
                                      /*script_timeout=*/nullptr, &result) !=
              AuctionV8Helper::Result::kSuccess) {
            result = "JSON extraction failed.";
          }
          run_loop.Quit();
        }));
    run_loop.Run();
    return result;
  }

 protected:
  void LoadSignalsCallback(scoped_refptr<TrustedSignals::Result> result,
                           std::optional<std::string> error_msg) {
    load_signals_result_ = std::move(result);
    error_msg_ = std::move(error_msg);
    if (!expect_nonfatal_error_) {
      EXPECT_EQ(load_signals_result_.get() == nullptr, error_msg_.has_value());
    } else {
      EXPECT_TRUE(load_signals_result_);
      EXPECT_TRUE(error_msg_);
    }
    load_signals_run_loop_->Quit();
  }

  base::test::TaskEnvironment task_environment_;

  // URL without query params attached.
  const GURL base_url_ = GURL("https://url.test/");

  // Reuseable run loop for loading the signals. It's always populated after
  // creating the worklet, to cause a crash if the callback is invoked
  // synchronously.
  std::unique_ptr<base::RunLoop> load_signals_run_loop_;
  scoped_refptr<TrustedSignals::Result> load_signals_result_;
  std::optional<std::string> error_msg_;

  // If false, only one of `result` or `error_msg` is expected to be received in
  // LoadSignalsCallback().
  bool expect_nonfatal_error_ = false;

  TestAuctionNetworkEventsHandler auction_network_events_handler_;

  network::TestURLLoaderFactory url_loader_factory_;
  scoped_refptr<AuctionV8Helper> v8_helper_;

  base::test::ScopedFeatureList feature_list_;
};

TEST_F(TrustedSignalsTest, BiddingSignalsNetworkError) {
  url_loader_factory_.AddResponse(
      "https://url.test/?hostname=publisher&keys=key1&interestGroupNames=name1",
      kBaseBiddingJson, net::HTTP_NOT_FOUND);
  EXPECT_FALSE(FetchBiddingSignals({"name1"}, {"key1"}, kHostname,
                                   /*experiment_group_id=*/std::nullopt));
  ASSERT_TRUE(error_msg_.has_value());
  EXPECT_EQ(
      "Failed to load "
      "https://url.test/?hostname=publisher&keys=key1&interestGroupNames=name1 "
      "HTTP status = 404 Not Found.",
      error_msg_.value());

  // Wait until idle to ensure all requests have been observed within the
  // `auction_network_events_handler_`.
  task_environment_.RunUntilIdle();
  EXPECT_THAT(auction_network_events_handler_.GetObservedRequests(),
              testing::ElementsAre(
                  "Sent URL: "
                  "https://url.test/"
                  "?hostname=publisher&keys=key1&interestGroupNames=name1",
                  "Received URL: "
                  "https://url.test/"
                  "?hostname=publisher&keys=key1&interestGroupNames=name1",
                  "Completion Status: net::ERR_HTTP_RESPONSE_CODE_FAILURE"));
}

TEST_F(TrustedSignalsTest, ScoringSignalsNetworkError) {
  url_loader_factory_.AddResponse(
      "https://url.test/"
      "?hostname=publisher&renderUrls=https%3A%2F%2Ffoo.test%2F",
      kBaseScoringJson, net::HTTP_NOT_FOUND);
  EXPECT_FALSE(FetchScoringSignals(
      /*render_urls=*/{"https://foo.test/"},
      /*ad_component_render_urls=*/{}, kHostname,
      /*experiment_group_id=*/std::nullopt));
  ASSERT_TRUE(error_msg_.has_value());
  EXPECT_EQ(
      "Failed to load "
      "https://url.test/"
      "?hostname=publisher&renderUrls=https%3A%2F%2Ffoo.test%2F "
      "HTTP status = 404 Not Found.",
      error_msg_.value());

  // Wait until idle to ensure all requests have been observed within the
  // `auction_network_events_handler_`.
  task_environment_.RunUntilIdle();
  EXPECT_THAT(auction_network_events_handler_.GetObservedRequests(),
              testing::ElementsAre(
                  "Sent URL: "
                  "https://url.test/"
                  "?hostname=publisher&renderUrls=https%3A%2F%2Ffoo.test%2F",
                  "Received URL: "
                  "https://url.test/"
                  "?hostname=publisher&renderUrls=https%3A%2F%2Ffoo.test%2F",
                  "Completion Status: net::ERR_HTTP_RESPONSE_CODE_FAILURE"));
}

TEST_F(TrustedSignalsTest, BiddingSignalsResponseNotJsonObject) {
  const char* kTestCases[] = {
      "",     "Not JSON",           "null",
      "5",    R"("Not an object")", R"(["Also not an object"])",
      "{} {}"};

  for (const char* test_case : kTestCases) {
    SCOPED_TRACE(test_case);

    EXPECT_FALSE(FetchBiddingSignalsWithResponse(
        GURL("https://url.test/"
             "?hostname=publisher&keys=key1&interestGroupNames=name1"),
        test_case, {"name1"}, {"key1"}, kHostname,
        /*experiment_group_id=*/std::nullopt));
    ASSERT_TRUE(error_msg_.has_value());
    EXPECT_EQ("https://url.test/ Unable to parse as a JSON object.",
              error_msg_.value());
  }
}

TEST_F(TrustedSignalsTest, ScoringSignalsResponseNotJsonObject) {
  const char* kTestCases[] = {
      "",     "Not JSON",           "null",
      "5",    R"("Not an object")", R"(["Also not an object"])",
      "{} {}"};

  for (const char* test_case : kTestCases) {
    SCOPED_TRACE(test_case);

    EXPECT_FALSE(FetchScoringSignalsWithResponse(
        GURL("https://url.test/"
             "?hostname=publisher&renderUrls=https%3A%2F%2Ffoo.test%2F"),
        test_case,
        /*render_urls=*/{"https://foo.test/"},
        /*ad_component_render_urls=*/{}, kHostname,
        /*experiment_group_id=*/std::nullopt));
    ASSERT_TRUE(error_msg_.has_value());
    EXPECT_EQ("https://url.test/ Unable to parse as a JSON object.",
              error_msg_.value());
  }
}

TEST_F(TrustedSignalsTest, BiddingSignalsInvalidVersion) {
  EXPECT_FALSE(FetchBiddingSignalsWithResponse(
      GURL("https://url.test/"
           "?hostname=publisher&keys=key1&interestGroupNames=name1"),
      kBaseBiddingJson, {"name1"}, {"key1"}, kHostname,
      /*experiment_group_id=*/std::nullopt,
      /*trusted_bidding_signals_slot_size_param=*/"",
      /*format_version_string=*/"3"));
  EXPECT_EQ(
      "Rejecting load of https://url.test/ due to unrecognized Format-Version "
      "header: 3",
      error_msg_.value());

  EXPECT_FALSE(FetchBiddingSignalsWithResponse(
      GURL("https://url.test/"
           "?hostname=publisher&keys=key1&interestGroupNames=name1"),
      kBaseBiddingJson, {"name1"}, {"key1"}, kHostname,
      /*experiment_group_id=*/std::nullopt,
      /*trusted_bidding_signals_slot_size_param=*/"",
      /*format_version_string=*/"0"));
  EXPECT_EQ(
      "Rejecting load of https://url.test/ due to unrecognized Format-Version "
      "header: 0",
      error_msg_.value());

  EXPECT_FALSE(FetchBiddingSignalsWithResponse(
      GURL("https://url.test/"
           "?hostname=publisher&keys=key1&interestGroupNames=name1"),
      kBaseBiddingJson, {"name1"}, {"key1"}, kHostname,
      /*experiment_group_id=*/std::nullopt,
      /*trusted_bidding_signals_slot_size_param=*/"",
      /*format_version_string=*/"shiny"));
  EXPECT_EQ(
      "Rejecting load of https://url.test/ due to unrecognized Format-Version "
      "header: shiny",
      error_msg_.value());

  AddResponse(
      &url_loader_factory_,
      GURL("https://url.test/"
           "?hostname=publisher&keys=key1&interestGroupNames=name1"),
      kJsonMimeType, std::nullopt, kBaseBiddingJson,
      base::StringPrintf("%s\nAd-Auction-Bidding-Signals-Format-Version: 100",
                         kAllowFledgeHeader));
  EXPECT_FALSE(FetchBiddingSignals({"name1"}, {"key1"}, kHostname,
                                   /*experiment_group_id=*/std::nullopt));
  EXPECT_EQ(
      "Rejecting load of https://url.test/ due to unrecognized Format-Version "
      "header: 100",
      error_msg_.value());
}

TEST_F(TrustedSignalsTest, BiddingSignalsResponseNotObject) {
  EXPECT_FALSE(FetchBiddingSignalsWithResponse(
      GURL("https://url.test/"
           "?hostname=publisher&keys=key1&interestGroupNames=name1"),
      "42", {"name1"}, {"key1"}, kHostname,
      /*experiment_group_id=*/std::nullopt));
  ASSERT_TRUE(error_msg_.has_value());
  EXPECT_EQ("https://url.test/ Unable to parse as a JSON object.",
            error_msg_.value());
}

TEST_F(TrustedSignalsTest, ScoringSignalsResponseNotObject) {
  EXPECT_FALSE(FetchScoringSignalsWithResponse(
      GURL("https://url.test/"
           "?hostname=publisher&renderUrls=https%3A%2F%2Ffoo.test%2F"),
      "42", /*render_urls=*/{"https://foo.test/"},
      /*ad_component_render_urls=*/{}, kHostname,
      /*experiment_group_id=*/std::nullopt));
  ASSERT_TRUE(error_msg_.has_value());
  EXPECT_EQ("https://url.test/ Unable to parse as a JSON object.",
            error_msg_.value());
}

TEST_F(TrustedSignalsTest, BiddingSignalsExpectedEntriesNotPresent) {
  scoped_refptr<TrustedSignals::Result> signals =
      FetchBiddingSignalsWithResponse(
          GURL("https://url.test/"
               "?hostname=publisher&keys=key1&interestGroupNames=name1"),
          R"({"foo":4,"bar":5})", {"name1"}, {"key1"}, kHostname);
  ASSERT_TRUE(signals);
  EXPECT_EQ(R"({"key1":null})", ExtractBiddingSignals(signals.get(), {"key1"}));
  EXPECT_EQ(nullptr, signals->GetPerGroupData("name1"));
}

TEST_F(TrustedSignalsTest, ScoringSignalsExpectedEntriesNotPresent) {
  scoped_refptr<TrustedSignals::Result> signals =
      FetchScoringSignalsWithResponse(
          GURL("https://url.test/?hostname=publisher"
               "&renderUrls=https%3A%2F%2Ffoo.test%2F"
               "&adComponentRenderUrls=https%3A%2F%2Fbar.test%2F"),
          R"({"foo":4,"bar":5})",
          /*render_urls=*/{"https://foo.test/"},
          /*ad_component_render_urls=*/{"https://bar.test/"}, kHostname,
          /*experiment_group_id=*/std::nullopt);
  ASSERT_TRUE(signals);
  EXPECT_EQ(R"({"renderURL":{"https://foo.test/":null},)"
            R"("renderUrl":{"https://foo.test/":null},)"
            R"("adComponentRenderURLs":{"https://bar.test/":null},)"
            R"("adComponentRenderUrls":{"https://bar.test/":null}})",
            ExtractScoringSignals(
                signals.get(), /*render_url=*/GURL("https://foo.test/"),
                /*ad_component_render_urls=*/{"https://bar.test/"}));
  EXPECT_FALSE(error_msg_.has_value());
}

TEST_F(TrustedSignalsTest, BiddingSignalsNestedEntriesNotObject) {
  const char* kTestCases[] = {
      "4", "[3]",
      // List with a valid priority vector as the first element, which should
      // not be treated as the priority vector of an interest group named "0".
      R"([{"priorityVector" : {"a":1}}])", "null", R"("string")"};

  for (const char* test_case : kTestCases) {
    SCOPED_TRACE(test_case);

    scoped_refptr<TrustedSignals::Result> signals =
        FetchBiddingSignalsWithResponse(
            GURL("https://url.test/?hostname=publisher"
                 "&keys=0,key1,length"
                 "&interestGroupNames=0,length,name1"),
            base::StringPrintf(R"({"keys":%s,"perInterestGroupData":%s})",
                               test_case, test_case),
            {"name1", "0", "length"}, {"key1", "0", "length"}, kHostname);
    ASSERT_TRUE(signals);
    EXPECT_EQ(R"({"key1":null})",
              ExtractBiddingSignals(signals.get(), {"key1"}));
    // These are important to check for the list case.
    EXPECT_EQ(R"({"0":null})", ExtractBiddingSignals(signals.get(), {"0"}));
    EXPECT_EQ(R"({"length":null})",
              ExtractBiddingSignals(signals.get(), {"length"}));

    EXPECT_EQ(nullptr, signals->GetPerGroupData("name1"));
    // These are important to check for the list case.
    EXPECT_EQ(nullptr, signals->GetPerGroupData("0"));
    EXPECT_EQ(nullptr, signals->GetPerGroupData("length"));
  }
}

TEST_F(TrustedSignalsTest, BiddingSignalsInvalidPriorityVectors) {
  // Test the cases were priority vectors are or contain invalid values.
  scoped_refptr<TrustedSignals::Result> signals =
      FetchBiddingSignalsWithResponse(
          GURL("https://url.test/"
               "?hostname=publisher&keys=key1&interestGroupNames=name1,name2,"
               "name3"),
          R"({"perInterestGroupData":{
            "name1" : {"priorityVector" : [2]},
            "name2" : {"priorityVector" : 6},
            "name3" : {"priorityVector" : {"foo": "bar",
                                           "baz": -1}}
          }})",
          {"name1", "name2", "name3"}, {"key1"}, kHostname);
  ASSERT_TRUE(signals);
  EXPECT_EQ(R"({"key1":null})", ExtractBiddingSignals(signals.get(), {"key1"}));
  EXPECT_EQ(nullptr, signals->GetPerGroupData("name1"));
  EXPECT_EQ(nullptr, signals->GetPerGroupData("name2"));
  const auto priority_vector =
      signals->GetPerGroupData("name3")->priority_vector;
  ASSERT_TRUE(priority_vector);
  EXPECT_EQ((TrustedSignals::Result::PriorityVector{{"baz", -1}}),
            *priority_vector);
}

TEST_F(TrustedSignalsTest, ScoringSignalsNestedEntriesNotObjects) {
  scoped_refptr<TrustedSignals::Result> signals =
      FetchScoringSignalsWithResponse(
          GURL("https://url.test/?hostname=publisher"
               "&renderUrls=https%3A%2F%2Ffoo.test%2F"
               "&adComponentRenderUrls=https%3A%2F%2Fbar.test%2F"),
          R"({"renderUrls":4,"adComponentRenderURLs":5})",
          /*render_urls=*/{"https://foo.test/"},
          /*ad_component_render_urls=*/{"https://bar.test/"}, kHostname,
          /*experiment_group_id=*/std::nullopt);
  ASSERT_TRUE(signals);
  EXPECT_EQ(R"({"renderURL":{"https://foo.test/":null},)"
            R"("renderUrl":{"https://foo.test/":null},)"
            R"("adComponentRenderURLs":{"https://bar.test/":null},)"
            R"("adComponentRenderUrls":{"https://bar.test/":null}})",
            ExtractScoringSignals(
                signals.get(), /*render_url=*/GURL("https://foo.test/"),
                /*ad_component_render_urls=*/{"https://bar.test/"}));
  EXPECT_FALSE(error_msg_.has_value());
}

TEST_F(TrustedSignalsTest, BiddingSignalsKeyMissing) {
  scoped_refptr<TrustedSignals::Result> signals =
      FetchBiddingSignalsWithResponse(
          GURL("https://url.test/"
               "?hostname=publisher&keys=key4&interestGroupNames=name4,name7,"
               "name8"),
          kBaseBiddingJson, {"name4", "name7", "name8"}, {"key4"}, kHostname,
          /*experiment_group_id=*/std::nullopt);
  ASSERT_TRUE(signals);
  EXPECT_EQ(R"({"key4":null})", ExtractBiddingSignals(signals.get(), {"key4"}));
  EXPECT_EQ(nullptr, signals->GetPerGroupData("name4"));

  const TrustedSignals::Result::PerGroupData* name7_per_group_data =
      signals->GetPerGroupData("name7");
  ASSERT_NE(name7_per_group_data, nullptr);
  EXPECT_EQ(std::nullopt, name7_per_group_data->priority_vector);
  EXPECT_EQ(base::Milliseconds(7200000),
            name7_per_group_data->update_if_older_than);

  const TrustedSignals::Result::PerGroupData* name8_per_group_data =
      signals->GetPerGroupData("name8");
  // Strings aren't valid values for updateIfOlderThanMs, and there's no
  // priorityVector, so there's nothing to return.
  ASSERT_EQ(name8_per_group_data, nullptr);
}

TEST_F(TrustedSignalsTest, BiddingSignalsKeyMissingNameInProto) {
  // Ensure nothing funny happens when the missing signal key name is something
  // in Object.prototype.
  scoped_refptr<TrustedSignals::Result> signals =
      FetchBiddingSignalsWithResponse(
          GURL("https://url.test/"
               "?hostname=publisher&keys=valueOf&interestGroupNames=name4"),
          kBaseBiddingJson, {"name4"}, {"valueOf"}, kHostname,
          /*experiment_group_id=*/std::nullopt);
  ASSERT_TRUE(signals);
  EXPECT_EQ(R"({"valueOf":null})",
            ExtractBiddingSignals(signals.get(), {"valueOf"}));
  EXPECT_EQ(nullptr, signals->GetPerGroupData("name4"));
}

TEST_F(TrustedSignalsTest, ScoringSignalsKeysMissing) {
  scoped_refptr<TrustedSignals::Result> signals =
      FetchScoringSignalsWithResponse(
          GURL("https://url.test/?hostname=publisher"
               "&renderUrls=https%3A%2F%2Ffoo.test%2F"
               "&adComponentRenderUrls=https%3A%2F%2Fbar.test%2F"),
          R"({"renderUrls":{"these":"are not"},")"
          R"(adComponentRenderURLs":{"the values":"you're looking for"}})",
          /*render_urls=*/{"https://foo.test/"},
          /*ad_component_render_urls=*/{"https://bar.test/"}, kHostname,
          /*experiment_group_id=*/std::nullopt);
  ASSERT_TRUE(signals);
  EXPECT_EQ(R"({"renderURL":{"https://foo.test/":null},)"
            R"("renderUrl":{"https://foo.test/":null},)"
            R"("adComponentRenderURLs":{"https://bar.test/":null},)"
            R"("adComponentRenderUrls":{"https://bar.test/":null}})",
            ExtractScoringSignals(
                signals.get(), /*render_url=*/GURL("https://foo.test/"),
                /*ad_component_render_urls=*/{"https://bar.test/"}));
  EXPECT_FALSE(error_msg_.has_value());
}

TEST_F(TrustedSignalsTest, BiddingSignalsOneKey) {
  scoped_refptr<TrustedSignals::Result> signals =
      FetchBiddingSignalsWithResponse(
          GURL("https://url.test/"
               "?hostname=publisher&keys=key1&interestGroupNames=name1"),
          kBaseBiddingJson, {"name1"}, {"key1"}, kHostname,
          /*experiment_group_id=*/std::nullopt);
  ASSERT_TRUE(signals);
  EXPECT_EQ(R"({"key1":1})", ExtractBiddingSignals(signals.get(), {"key1"}));
  const TrustedSignals::Result::PerGroupData* name1_per_group_data =
      signals->GetPerGroupData("name1");
  ASSERT_NE(name1_per_group_data, nullptr);
  auto priority_vector = name1_per_group_data->priority_vector;
  ASSERT_TRUE(priority_vector);
  EXPECT_EQ((TrustedSignals::Result::PriorityVector{{"foo", 1}}),
            *priority_vector);
  EXPECT_EQ(base::Milliseconds(3600000),
            name1_per_group_data->update_if_older_than);

  // Wait until idle to ensure all requests have been observed within the
  // `auction_network_events_handler_`.
  task_environment_.RunUntilIdle();
  EXPECT_THAT(auction_network_events_handler_.GetObservedRequests(),
              testing::ElementsAre(
                  "Sent URL: https://url.test/"
                  "?hostname=publisher&keys=key1&interestGroupNames=name1",
                  "Received URL: https://url.test/"
                  "?hostname=publisher&keys=key1&interestGroupNames=name1",
                  "Completion Status: net::OK"));
}

TEST_F(TrustedSignalsTest, BiddingSignalsOneKeyOldHeaderName) {
  AddResponse(
      &url_loader_factory_,
      GURL("https://url.test/"
           "?hostname=publisher&keys=key1&interestGroupNames=name1"),
      kJsonMimeType, std::nullopt, kBaseBiddingJson,
      base::StringPrintf("%s\nX-Fledge-Bidding-Signals-Format-Version: 2",
                         kAllowFledgeHeader));
  scoped_refptr<TrustedSignals::Result> signals =
      FetchBiddingSignals({"name1"}, {"key1"}, kHostname,
                          /*experiment_group_id=*/std::nullopt);
  ASSERT_TRUE(signals);
  EXPECT_EQ(R"({"key1":1})", ExtractBiddingSignals(signals.get(), {"key1"}));
  auto priority_vector = signals->GetPerGroupData("name1")->priority_vector;
  ASSERT_TRUE(priority_vector);
  EXPECT_EQ((TrustedSignals::Result::PriorityVector{{"foo", 1}}),
            *priority_vector);
}

TEST_F(TrustedSignalsTest, BiddingSignalsOneKeyHeaderName) {
  AddResponse(
      &url_loader_factory_,
      GURL("https://url.test/"
           "?hostname=publisher&keys=key1&interestGroupNames=name1"),
      kJsonMimeType, std::nullopt, kBaseBiddingJson,
      base::StringPrintf("%s\nAd-Auction-Bidding-Signals-Format-Version: 2",
                         kAllowFledgeHeader));
  scoped_refptr<TrustedSignals::Result> signals =
      FetchBiddingSignals({"name1"}, {"key1"}, kHostname,
                          /*experiment_group_id=*/std::nullopt);
  ASSERT_TRUE(signals);
  EXPECT_EQ(R"({"key1":1})", ExtractBiddingSignals(signals.get(), {"key1"}));
  const auto priority_vector =
      signals->GetPerGroupData("name1")->priority_vector;
  ASSERT_TRUE(priority_vector);
  EXPECT_EQ((TrustedSignals::Result::PriorityVector{{"foo", 1}}),
            *priority_vector);
}

TEST_F(TrustedSignalsTest, BiddingSignalsOneKeyBothOldAndNewHeaderNames) {
  AddResponse(
      &url_loader_factory_,
      GURL("https://url.test/"
           "?hostname=publisher&keys=key1&interestGroupNames=name1"),
      kJsonMimeType, std::nullopt, kBaseBiddingJson,
      base::StringPrintf("%s\nAd-Auction-Bidding-Signals-Format-Version: 2\n"
                         "X-Fledge-Bidding-Signals-Format-Version: 2",
                         kAllowFledgeHeader));
  scoped_refptr<TrustedSignals::Result> signals =
      FetchBiddingSignals({"name1"}, {"key1"}, kHostname,
                          /*experiment_group_id=*/std::nullopt);
  ASSERT_TRUE(signals);
  EXPECT_EQ(R"({"key1":1})", ExtractBiddingSignals(signals.get(), {"key1"}));
  const auto priority_vector =
      signals->GetPerGroupData("name1")->priority_vector;
  ASSERT_TRUE(priority_vector);
  EXPECT_EQ((TrustedSignals::Result::PriorityVector{{"foo", 1}}),
            *priority_vector);
}

TEST_F(TrustedSignalsTest, ScoringSignalsForOneRenderUrl) {
  scoped_refptr<TrustedSignals::Result> signals =
      FetchScoringSignalsWithResponse(
          GURL("https://url.test/"
               "?hostname=publisher&renderUrls=https%3A%2F%2Ffoo.test%2F"),
          kBaseScoringJson,
          /*render_urls=*/{"https://foo.test/"},
          /*ad_component_render_urls=*/{}, kHostname,
          /*experiment_group_id=*/std::nullopt);
  ASSERT_TRUE(signals);
  EXPECT_EQ(R"({"renderURL":{"https://foo.test/":1},)"
            R"("renderUrl":{"https://foo.test/":1}})",
            ExtractScoringSignals(signals.get(),
                                  /*render_url=*/GURL("https://foo.test/"),
                                  /*ad_component_render_urls=*/{}));
  EXPECT_FALSE(error_msg_.has_value());

  // Wait until idle to ensure all requests have been observed within the
  // `auction_network_events_handler_`.
  task_environment_.RunUntilIdle();
  EXPECT_THAT(auction_network_events_handler_.GetObservedRequests(),
              testing::ElementsAre(
                  "Sent URL: https://url.test/"
                  "?hostname=publisher&renderUrls=https%3A%2F%2Ffoo.test%2F",
                  "Received URL: https://url.test/"
                  "?hostname=publisher&renderUrls=https%3A%2F%2Ffoo.test%2F",
                  "Completion Status: net::OK"));
}

TEST_F(TrustedSignalsTest, BiddingSignalsMultipleKeys) {
  scoped_refptr<TrustedSignals::Result> signals =
      FetchBiddingSignalsWithResponse(
          GURL(
              "https://url.test/?hostname=publisher"
              "&keys=key1,key2,key3,key5&interestGroupNames=name1,name2,name3"),
          kBaseBiddingJson, {"name1", "name2", "name3"},
          {"key3", "key1", "key5", "key2"}, kHostname,
          /*experiment_group_id=*/std::nullopt);
  ASSERT_TRUE(signals);
  EXPECT_EQ(R"({"key1":1})", ExtractBiddingSignals(signals.get(), {"key1"}));
  EXPECT_EQ(R"({"key2":[2]})", ExtractBiddingSignals(signals.get(), {"key2"}));
  EXPECT_EQ(R"({"key3":null})", ExtractBiddingSignals(signals.get(), {"key3"}));
  EXPECT_EQ(R"({"key5":"value5"})",
            ExtractBiddingSignals(signals.get(), {"key5"}));
  EXPECT_EQ(
      R"({"key1":1,"key2":[2],"key3":null,"key5":"value5"})",
      ExtractBiddingSignals(signals.get(), {"key1", "key2", "key3", "key5"}));

  const TrustedSignals::Result::PerGroupData* name1_per_group_data =
      signals->GetPerGroupData("name1");
  ASSERT_NE(name1_per_group_data, nullptr);
  auto priority_vector = name1_per_group_data->priority_vector;
  ASSERT_TRUE(priority_vector);
  EXPECT_EQ((TrustedSignals::Result::PriorityVector{{"foo", 1}}),
            *priority_vector);
  EXPECT_EQ(base::Milliseconds(3600000),
            name1_per_group_data->update_if_older_than);

  const TrustedSignals::Result::PerGroupData* name2_per_group_data =
      signals->GetPerGroupData("name2");
  ASSERT_NE(name2_per_group_data, nullptr);
  priority_vector = name2_per_group_data->priority_vector;
  ASSERT_TRUE(priority_vector);
  EXPECT_EQ((TrustedSignals::Result::PriorityVector{
                {"foo", 2}, {"bar", 3}, {"baz", -3.5}}),
            *priority_vector);
  EXPECT_EQ(std::nullopt, name2_per_group_data->update_if_older_than);

  const TrustedSignals::Result::PerGroupData* name3_per_group_data =
      signals->GetPerGroupData("name3");
  ASSERT_NE(name3_per_group_data, nullptr);
  priority_vector = name3_per_group_data->priority_vector;
  ASSERT_TRUE(priority_vector);
  EXPECT_EQ(TrustedSignals::Result::PriorityVector(), *priority_vector);
  EXPECT_EQ(std::nullopt, name3_per_group_data->update_if_older_than);
}

TEST_F(TrustedSignalsTest, ScoringSignalsMultipleUrls) {
  // URLs are currently added in lexical order.
  scoped_refptr<TrustedSignals::Result> signals =
      FetchScoringSignalsWithResponse(
          GURL("https://url.test/?hostname=publisher"
               "&renderUrls=https%3A%2F%2Fbar.test%2F,"
               "https%3A%2F%2Fbaz.test%2F,https%3A%2F%2Ffoo.test%2F"
               "&adComponentRenderUrls=https%3A%2F%2Fbarsub.test%2F,"
               "https%3A%2F%2Fbazsub.test%2F,https%3A%2F%2Ffoosub.test%2F"),
          kBaseScoringJson,
          /*render_urls=*/
          {"https://foo.test/", "https://bar.test/", "https://baz.test/"},
          /*ad_component_render_urls=*/
          {"https://foosub.test/", "https://barsub.test/",
           "https://bazsub.test/"},
          kHostname, /*experiment_group_id=*/std::nullopt);
  ASSERT_TRUE(signals);
  EXPECT_FALSE(error_msg_.has_value());
  EXPECT_EQ(R"({"renderURL":{"https://bar.test/":[2]},)"
            R"("renderUrl":{"https://bar.test/":[2]},)"
            R"("adComponentRenderURLs":{"https://foosub.test/":2,)"
            R"("https://barsub.test/":[3],"https://bazsub.test/":null},)"
            R"("adComponentRenderUrls":{"https://foosub.test/":2,)"
            R"("https://barsub.test/":[3],"https://bazsub.test/":null}})",
            ExtractScoringSignals(
                signals.get(), /*render_url=*/GURL("https://bar.test/"),
                /*ad_component_render_urls=*/
                {"https://foosub.test/", "https://barsub.test/",
                 "https://bazsub.test/"}));
}

TEST_F(TrustedSignalsTest, ScoringSignalsOldNames) {
  // URLs are currently added in lexical order.
  scoped_refptr<TrustedSignals::Result> signals =
      FetchScoringSignalsWithResponse(
          GURL("https://url.test/?hostname=publisher"
               "&renderUrls=https%3A%2F%2Fbar.test%2F,"
               "https%3A%2F%2Fbaz.test%2F,https%3A%2F%2Ffoo.test%2F"
               "&adComponentRenderUrls=https%3A%2F%2Fbarsub.test%2F,"
               "https%3A%2F%2Fbazsub.test%2F,https%3A%2F%2Ffoosub.test%2F"),
          kBaseScoringJsonOldNames,
          /*render_urls=*/
          {"https://foo.test/", "https://bar.test/", "https://baz.test/"},
          /*ad_component_render_urls=*/
          {"https://foosub.test/", "https://barsub.test/",
           "https://bazsub.test/"},
          kHostname, /*experiment_group_id=*/std::nullopt);
  ASSERT_TRUE(signals);
  EXPECT_FALSE(error_msg_.has_value());
  EXPECT_EQ(R"({"renderURL":{"https://bar.test/":[2]},)"
            R"("renderUrl":{"https://bar.test/":[2]},)"
            R"("adComponentRenderURLs":{"https://foosub.test/":2,)"
            R"("https://barsub.test/":[3],"https://bazsub.test/":null},)"
            R"("adComponentRenderUrls":{"https://foosub.test/":2,)"
            R"("https://barsub.test/":[3],"https://bazsub.test/":null}})",
            ExtractScoringSignals(
                signals.get(), /*render_url=*/GURL("https://bar.test/"),
                /*ad_component_render_urls=*/
                {"https://foosub.test/", "https://barsub.test/",
                 "https://bazsub.test/"}));
}

TEST_F(TrustedSignalsTest, ScoringSignalsNewAndOldNames) {
  // URLs are currently added in lexical order.
  scoped_refptr<TrustedSignals::Result> signals =
      FetchScoringSignalsWithResponse(
          GURL("https://url.test/?hostname=publisher"
               "&renderUrls=https%3A%2F%2Fbar.test%2F,"
               "https%3A%2F%2Fbaz.test%2F,https%3A%2F%2Ffoo.test%2F"
               "&adComponentRenderUrls=https%3A%2F%2Fbarsub.test%2F,"
               "https%3A%2F%2Fbazsub.test%2F,https%3A%2F%2Ffoosub.test%2F"),
          kBaseScoringJsonNewAndOldNames,
          /*render_urls=*/
          {"https://foo.test/", "https://bar.test/", "https://baz.test/"},
          /*ad_component_render_urls=*/
          {"https://foosub.test/", "https://barsub.test/",
           "https://bazsub.test/"},
          kHostname, /*experiment_group_id=*/std::nullopt);
  ASSERT_TRUE(signals);
  EXPECT_FALSE(error_msg_.has_value());
  EXPECT_EQ(R"({"renderURL":{"https://bar.test/":[2]},)"
            R"("renderUrl":{"https://bar.test/":[2]},)"
            R"("adComponentRenderURLs":{"https://foosub.test/":2,)"
            R"("https://barsub.test/":[3],"https://bazsub.test/":null},)"
            R"("adComponentRenderUrls":{"https://foosub.test/":2,)"
            R"("https://barsub.test/":[3],"https://bazsub.test/":null}})",
            ExtractScoringSignals(
                signals.get(), /*render_url=*/GURL("https://bar.test/"),
                /*ad_component_render_urls=*/
                {"https://foosub.test/", "https://barsub.test/",
                 "https://bazsub.test/"}));
}

TEST_F(TrustedSignalsTest, BiddingSignalsDuplicateKeys) {
  // Unlike most bidding signals tests, only test trusted bidding signals keys,
  // and not interest group names. Since the PriorityVector corresponding to
  // only a single interest group can be requested at a time, unlike
  // TrustedSignals::ExtractJson(), which takes a vector of keys, there's no
  // analogous case for interest group names.
  std::vector<std::string> bidder_signals_vector{"key1", "key2", "key2", "key1",
                                                 "key2"};
  scoped_refptr<TrustedSignals::Result> signals =
      FetchBiddingSignalsWithResponse(
          GURL("https://url.test/?hostname=publisher"
               "&keys=key1,key2&interestGroupNames=name1"),
          kBaseBiddingJson, {"name1"},
          std::set<std::string>{bidder_signals_vector.begin(),
                                bidder_signals_vector.end()},
          kHostname, /*experiment_group_id=*/std::nullopt);
  ASSERT_TRUE(signals);
  EXPECT_EQ(R"({"key1":1,"key2":[2]})",
            ExtractBiddingSignals(signals.get(), bidder_signals_vector));
}

TEST_F(TrustedSignalsTest, ScoringSignalsDuplicateKeys) {
  std::vector<std::string> ad_component_render_urls_vector{
      "https://barsub.test/", "https://foosub.test/", "https://foosub.test/",
      "https://barsub.test/"};
  scoped_refptr<TrustedSignals::Result> signals =
      FetchScoringSignalsWithResponse(
          GURL("https://url.test/?hostname=publisher"
               "&renderUrls=https%3A%2F%2Fbar.test%2F,https%3A%2F%2Ffoo.test%2F"
               "&adComponentRenderUrls=https%3A%2F%2Fbarsub.test%2F,"
               "https%3A%2F%2Ffoosub.test%2F"),
          kBaseScoringJson,
          /*render_urls=*/
          {"https://foo.test/", "https://foo.test/", "https://bar.test/",
           "https://bar.test/", "https://foo.test/"},
          std::set<std::string>{ad_component_render_urls_vector.begin(),
                                ad_component_render_urls_vector.end()},
          kHostname, /*experiment_group_id=*/std::nullopt);
  ASSERT_TRUE(signals);
  EXPECT_FALSE(error_msg_.has_value());
  EXPECT_EQ(R"({"renderURL":{"https://bar.test/":[2]},)"
            R"("renderUrl":{"https://bar.test/":[2]},)"
            R"("adComponentRenderURLs":{)"
            R"("https://barsub.test/":[3],"https://foosub.test/":2},)"
            R"("adComponentRenderUrls":{)"
            R"("https://barsub.test/":[3],"https://foosub.test/":2}})",
            ExtractScoringSignals(signals.get(),
                                  /*render_url=*/GURL("https://bar.test/"),
                                  ad_component_render_urls_vector));
}

// Test when a single URL is used as both a `renderURL` and
// `adComponentRenderURL`.
TEST_F(TrustedSignalsTest, ScoringSignalsSharedUrl) {
  // URLs are currently added in lexical order.
  scoped_refptr<TrustedSignals::Result> signals =
      FetchScoringSignalsWithResponse(
          GURL("https://url.test/?hostname=publisher"
               "&renderUrls=https%3A%2F%2Fshared.test%2F"
               "&adComponentRenderUrls=https%3A%2F%2Fshared.test%2F"),
          kBaseScoringJson,
          /*render_urls=*/
          {"https://shared.test/"},
          /*ad_component_render_urls=*/
          {"https://shared.test/"}, kHostname,
          /*experiment_group_id=*/std::nullopt);
  ASSERT_TRUE(signals);
  EXPECT_FALSE(error_msg_.has_value());
  EXPECT_EQ(
      R"({"renderURL":{"https://shared.test/":"render url"},)"
      R"("renderUrl":{"https://shared.test/":"render url"},)"
      R"("adComponentRenderURLs":{"https://shared.test/":"ad component url"},)"
      R"("adComponentRenderUrls":{"https://shared.test/":"ad component url"}})",
      ExtractScoringSignals(signals.get(),
                            /*render_url=*/GURL("https://shared.test/"),
                            /*ad_component_render_urls=*/
                            {"https://shared.test/"}));
}

TEST_F(TrustedSignalsTest, BiddingSignalsEscapeQueryParams) {
  scoped_refptr<TrustedSignals::Result> signals =
      FetchBiddingSignalsWithResponse(
          GURL("https://url.test/"
               "?hostname=pub+li%26sher&"
               "keys=key+6,key%2C8,key%3D7"
               "&interestGroupNames=name+5,name6%E2%98%83"),
          kBaseBiddingJson, {"name 5", "name6\xE2\x98\x83"},
          {"key 6", "key=7", "key,8"}, "pub li&sher",
          /*experiment_group_id=*/std::nullopt);
  ASSERT_TRUE(signals);
  EXPECT_EQ(R"({"key 6":6})", ExtractBiddingSignals(signals.get(), {"key 6"}));
  EXPECT_EQ(R"({"key=7":7})", ExtractBiddingSignals(signals.get(), {"key=7"}));
  EXPECT_EQ(R"({"key,8":8})", ExtractBiddingSignals(signals.get(), {"key,8"}));

  auto priority_vector = signals->GetPerGroupData("name 5")->priority_vector;
  ASSERT_TRUE(priority_vector);
  EXPECT_EQ((TrustedSignals::Result::PriorityVector{{"foo", 5}}),
            *priority_vector);

  priority_vector =
      signals->GetPerGroupData("name6\xE2\x98\x83")->priority_vector;
  ASSERT_TRUE(priority_vector);
  EXPECT_EQ((TrustedSignals::Result::PriorityVector{{"foo", 6}}),
            *priority_vector);
}

TEST_F(TrustedSignalsTest, ScoringSignalsEscapeQueryParams) {
  scoped_refptr<TrustedSignals::Result> signals =
      FetchScoringSignalsWithResponse(
          GURL("https://url.test/?hostname=pub+li%26sher"
               "&renderUrls=https%3A%2F%2Ffoo.test%2F%3F%26%3D"
               "&adComponentRenderUrls=https%3A%2F%2Fbar.test%2F%3F%26%3D"),
          R"(
  {
    "renderUrls": {
      "https://foo.test/?&=": 4
    },
    "adComponentRenderURLs": {
      "https://bar.test/?&=": 5
    }
  }
)",
          /*render_urls=*/
          {"https://foo.test/?&="}, /*ad_component_render_urls=*/
          {"https://bar.test/?&="}, "pub li&sher",
          /*experiment_group_id=*/std::nullopt);
  ASSERT_TRUE(signals);
  EXPECT_EQ(R"({"renderURL":{"https://foo.test/?&=":4},)"
            R"("renderUrl":{"https://foo.test/?&=":4},)"
            R"("adComponentRenderURLs":{"https://bar.test/?&=":5},)"
            R"("adComponentRenderUrls":{"https://bar.test/?&=":5}})",
            ExtractScoringSignals(
                signals.get(),                /*render_url=*/
                GURL("https://foo.test/?&="), /*ad_component_render_urls=*/
                {"https://bar.test/?&="}));
  EXPECT_FALSE(error_msg_.has_value());
}

// Testcase where the loader is deleted after it queued the parsing of
// the script on V8 thread, but before it gets to finish.
TEST_F(TrustedSignalsTest, BiddingSignalsDeleteBeforeCallback) {
  GURL url(
      "https://url.test/"
      "?hostname=publisher&keys=key1&interestGroupNames=name1");

  AddJsonResponse(&url_loader_factory_, url, kBaseBiddingJson);

  // Wedge the V8 thread to control when the JSON parsing takes place.
  base::WaitableEvent* event_handle = WedgeV8Thread(v8_helper_.get());

  std::vector<TrustedSignals::UrlPiece> main_fragments, aux_fragments;
  TrustedSignals::BuildTrustedBiddingSignalsURL(
      "publisher", base_url_, {"name1"}, {"key1"},
      /*experiment_group_id=*/std::nullopt,
      /*trusted_bidding_signals_slot_size_param=*/"", main_fragments,
      aux_fragments);

  auto bidding_signals = TrustedSignals::LoadBiddingSignals(
      &url_loader_factory_, auction_network_events_handler_.CreateRemote(),
      {"name1"}, {"key1"}, base_url_, std::move(main_fragments),
      std::move(aux_fragments), v8_helper_,
      base::BindOnce([](scoped_refptr<TrustedSignals::Result> result,
                        std::optional<std::string> error_msg) {
        ADD_FAILURE() << "Callback should not be invoked since loader deleted";
      }));
  base::RunLoop().RunUntilIdle();
  bidding_signals.reset();
  event_handle->Signal();
}

// Testcase where the loader is deleted after it queued the parsing of
// the script on V8 thread, but before it gets to finish.
TEST_F(TrustedSignalsTest, ScoringSignalsDeleteBeforeCallback) {
  GURL url(
      "https://url.test/"
      "?hostname=publisher&renderUrls=https%3A%2F%2Ffoo.test%2F");

  AddJsonResponse(&url_loader_factory_, url, kBaseScoringJson);

  // Wedge the V8 thread to control when the JSON parsing takes place.
  base::WaitableEvent* event_handle = WedgeV8Thread(v8_helper_.get());

  auto ads = CreateCreativeInfoSet({"http://foo.test/"});
  std::vector<TrustedSignals::UrlPiece> main_fragments, aux_fragments;
  TrustedSignals::BuildTrustedScoringSignalsURL(
      /*send_creative_scanning_metadata=*/false, "publisher", base_url_, ads,
      /*component_ads=*/{}, /*experiment_group_id=*/std::nullopt,
      main_fragments, aux_fragments);

  auto scoring_signals = TrustedSignals::LoadScoringSignals(
      &url_loader_factory_, auction_network_events_handler_.CreateRemote(),
      std::move(ads), /*ad_components=*/{}, "publisher", base_url_,
      /*experiment_group_id=*/std::nullopt, std::move(main_fragments),
      std::move(aux_fragments),
      /*send_creative_scanning_metadata=*/false, v8_helper_,
      base::BindOnce([](scoped_refptr<TrustedSignals::Result> result,
                        std::optional<std::string> error_msg) {
        ADD_FAILURE() << "Callback should not be invoked since loader deleted";
      }));
  base::RunLoop().RunUntilIdle();
  scoring_signals.reset();
  event_handle->Signal();
}

TEST_F(TrustedSignalsTest, ScoringSignalsWithDataVersion) {
  const uint32_t kTestCases[] = {0, 2, 42949, 4294967295};
  for (uint32_t test_case : kTestCases) {
    SCOPED_TRACE(test_case);

    AddVersionedJsonResponse(
        &url_loader_factory_,
        GURL("https://url.test/"
             "?hostname=publisher&renderUrls=https%3A%2F%2Ffoo.test%2F"),
        kBaseScoringJson, test_case);
    scoped_refptr<TrustedSignals::Result> signals =
        FetchScoringSignals(/*render_urls=*/{"https://foo.test/"},
                            /*ad_component_render_urls=*/{}, kHostname,
                            /*experiment_group_id=*/std::nullopt);
    ASSERT_TRUE(signals);
    EXPECT_EQ(R"({"renderURL":{"https://foo.test/":1},)"
              R"("renderUrl":{"https://foo.test/":1}})",
              ExtractScoringSignals(signals.get(),
                                    /*render_url=*/GURL("https://foo.test/"),
                                    /*ad_component_render_urls=*/{}));
    EXPECT_FALSE(error_msg_.has_value());
    EXPECT_EQ(test_case, signals->GetDataVersion());
  }
}

TEST_F(TrustedSignalsTest, ScoringSignalsWithInvalidDataVersion) {
  const std::string kTestCases[] = {
      "2.0", "03", "-1", "4294967296", "1 2", "0x4", "", "apple",
  };
  for (const std::string& test_case : kTestCases) {
    SCOPED_TRACE(test_case);
    AddResponse(
        &url_loader_factory_,
        GURL("https://url.test/"
             "?hostname=publisher&renderUrls=https%3A%2F%2Ffoo.test%2F"),
        kJsonMimeType, std::nullopt, kBaseScoringJson,
        "Ad-Auction-Allowed: true\nData-Version: " + test_case);
    scoped_refptr<TrustedSignals::Result> signals =
        FetchScoringSignals(/*render_urls=*/{"https://foo.test/"},
                            /*ad_component_render_urls=*/{}, kHostname,
                            /*experiment_group_id=*/std::nullopt);
    ASSERT_TRUE(error_msg_.has_value());
    EXPECT_EQ(
        "Rejecting load of https://url.test/ due to invalid Data-Version "
        "header: " +
            test_case,
        error_msg_.value());
  }
}

TEST_F(TrustedSignalsTest, BiddingSignalsExperimentId) {
  scoped_refptr<TrustedSignals::Result> signals =
      FetchBiddingSignalsWithResponse(GURL("https://url.test/"
                                           "?hostname=publisher"
                                           "&keys=key1"
                                           "&interestGroupNames=name1"
                                           "&experimentGroupId=1234"),
                                      kBaseBiddingJson, {"name1"}, {"key1"},
                                      kHostname,
                                      /*experiment_group_id=*/1234u);
  ASSERT_TRUE(signals);
  EXPECT_EQ(R"({"key1":1})", ExtractBiddingSignals(signals.get(), {"key1"}));
}

TEST_F(TrustedSignalsTest, ScoringSignalsExperimentId) {
  scoped_refptr<TrustedSignals::Result> signals =
      FetchScoringSignalsWithResponse(
          GURL("https://url.test/"
               "?hostname=publisher&renderUrls=https%3A%2F%2Ffoo.test%2F&"
               "experimentGroupId=2345"),
          kBaseScoringJson,
          /*render_urls=*/{"https://foo.test/"},
          /*ad_component_render_urls=*/{}, kHostname,
          /*experiment_group_id=*/2345u);
  ASSERT_TRUE(signals);
  EXPECT_EQ(R"({"renderURL":{"https://foo.test/":1},)"
            R"("renderUrl":{"https://foo.test/":1}})",
            ExtractScoringSignals(signals.get(),
                                  /*render_url=*/GURL("https://foo.test/"),
                                  /*ad_component_render_urls=*/{}));
  EXPECT_FALSE(error_msg_.has_value());
}

TEST_F(TrustedSignalsTest, BiddingSignalsAdditionalQueryParams) {
  const std::string kTestCases[] = {"", "no-equals", "a=b", "A=B&%20=3"};
  for (const std::string& test_case : kTestCases) {
    SCOPED_TRACE(test_case);

    std::string expected_bidding_signals_url =
        "https://url.test/"
        "?hostname=publisher"
        "&keys=key1"
        "&interestGroupNames=name1" +
        (test_case.empty() ? "" : "&" + test_case);
    scoped_refptr<TrustedSignals::Result> signals =
        FetchBiddingSignalsWithResponse(
            GURL(expected_bidding_signals_url), kBaseBiddingJson, {"name1"},
            {"key1"}, kHostname, /*experiment_group_id=*/std::nullopt,
            test_case);
    ASSERT_TRUE(signals);
    EXPECT_EQ(R"({"key1":1})", ExtractBiddingSignals(signals.get(), {"key1"}));
  }
}

TEST_F(TrustedSignalsTest, BiddingSignalsV1) {
  expect_nonfatal_error_ = true;

  scoped_refptr<TrustedSignals::Result> signals =
      FetchBiddingSignalsWithResponse(
          GURL("https://url.test/?hostname=publisher"
               "&keys=key1,key2,key3,key5&interestGroupNames=name1"),
          kBiddingJsonV1, {"name1"}, {"key1", "key2", "key3", "key5"},
          kHostname,
          /*experiment_group_id=*/std::nullopt,
          /*trusted_bidding_signals_slot_size_param=*/"",
          /*format_version_string=*/std::nullopt);
  EXPECT_EQ(error_msg_,
            "Bidding signals URL https://url.test/ is using outdated bidding "
            "signals format. Consumers should be updated to use bidding "
            "signals format version 2");
  ASSERT_TRUE(signals);
  EXPECT_EQ(R"({"key1":1})", ExtractBiddingSignals(signals.get(), {"key1"}));
  EXPECT_EQ(R"({"key2":[2]})", ExtractBiddingSignals(signals.get(), {"key2"}));
  EXPECT_EQ(R"({"key3":null})", ExtractBiddingSignals(signals.get(), {"key3"}));
  EXPECT_EQ(R"({"key5":"value5"})",
            ExtractBiddingSignals(signals.get(), {"key5"}));
  EXPECT_EQ(
      R"({"key1":1,"key2":[2],"key3":null,"key5":"value5"})",
      ExtractBiddingSignals(signals.get(), {"key1", "key2", "key3", "key5"}));
  // Format V1 doesn't support priority vectors.
  EXPECT_EQ(nullptr, signals->GetPerGroupData("name1"));
}

TEST_F(TrustedSignalsTest, BiddingSignalsV1WithV1Header) {
  expect_nonfatal_error_ = true;

  // Only version 2 officially has a version header, but allow an explicit
  // version of "1" to mean the first version.
  scoped_refptr<TrustedSignals::Result> signals =
      FetchBiddingSignalsWithResponse(
          GURL("https://url.test/?hostname=publisher"
               "&keys=key1,key2,key3,key5&interestGroupNames=name1"),
          kBiddingJsonV1, {"name1"}, {"key1", "key2", "key3", "key5"},
          kHostname,
          /*experiment_group_id=*/std::nullopt,
          /*trusted_bidding_signals_slot_size_param=*/"",
          /*format_version_string=*/"1");
  EXPECT_EQ(error_msg_,
            "Bidding signals URL https://url.test/ is using outdated bidding "
            "signals format. Consumers should be updated to use bidding "
            "signals format version 2");
  ASSERT_TRUE(signals);
  EXPECT_EQ(R"({"key1":1})", ExtractBiddingSignals(signals.get(), {"key1"}));
  EXPECT_EQ(R"({"key2":[2]})", ExtractBiddingSignals(signals.get(), {"key2"}));
  EXPECT_EQ(R"({"key3":null})", ExtractBiddingSignals(signals.get(), {"key3"}));
  EXPECT_EQ(R"({"key5":"value5"})",
            ExtractBiddingSignals(signals.get(), {"key5"}));
  EXPECT_EQ(
      R"({"key1":1,"key2":[2],"key3":null,"key5":"value5"})",
      ExtractBiddingSignals(signals.get(), {"key1", "key2", "key3", "key5"}));
  // Format V1 doesn't support priority vectors.
  EXPECT_EQ(nullptr, signals->GetPerGroupData("name1"));
}

// A V2 header with a V1 body treats all values as null (since it can't find
// keys).
TEST_F(TrustedSignalsTest, BiddingSignalsV2HeaderV1Body) {
  scoped_refptr<TrustedSignals::Result> signals =
      FetchBiddingSignalsWithResponse(
          GURL("https://url.test/"
               "?hostname=publisher&keys=key1&interestGroupNames=name1"),
          kBiddingJsonV1, {"name1"}, {"key1"}, kHostname,
          /*experiment_group_id=*/std::nullopt,
          /*trusted_bidding_signals_slot_size_param=*/"",
          /*format_version_string=*/"2");
  ASSERT_TRUE(signals);
  EXPECT_EQ(R"({"key1":null})", ExtractBiddingSignals(signals.get(), {"key1"}));
  EXPECT_EQ(nullptr, signals->GetPerGroupData("name1"));
}

// A V1 header (i.e., no version header) with a V2 body treats all values as
// null (since it can't find keys).
TEST_F(TrustedSignalsTest, BiddingSignalsV1HeaderV2Body) {
  expect_nonfatal_error_ = true;

  scoped_refptr<TrustedSignals::Result> signals =
      FetchBiddingSignalsWithResponse(
          GURL("https://url.test/"
               "?hostname=publisher&keys=key1&interestGroupNames=name1"),
          kBaseBiddingJson, {"name1"}, {"key1"}, kHostname,
          /*experiment_group_id=*/std::nullopt,
          /*trusted_bidding_signals_slot_size_param=*/"",
          /*format_version_string=*/std::nullopt);
  EXPECT_EQ(error_msg_,
            "Bidding signals URL https://url.test/ is using outdated bidding "
            "signals format. Consumers should be updated to use bidding "
            "signals format version 2");
  ASSERT_TRUE(signals);
  EXPECT_EQ(R"({"key1":null})", ExtractBiddingSignals(signals.get(), {"key1"}));
  EXPECT_EQ(nullptr, signals->GetPerGroupData("name1"));
}

TEST_F(TrustedSignalsTest, BuildTrustedScoringSignalsURLAndComposeURL) {
  for (const bool send_creative_scanning_metadata : {false, true}) {
    SCOPED_TRACE(send_creative_scanning_metadata);

    auto test_data = std::to_array<TrustedSignals::CreativeInfo>(
        {{/*ad_descriptor=*/blink::AdDescriptor(
              GURL("https://creative1.test"),
              std::optional(
                  blink::AdSize(200, blink::AdSize::LengthUnit::kPixels, 100,
                                blink::AdSize::LengthUnit::kScreenHeight))),
          /*creative_scanning_metadata=*/"scan1",
          /*interest_group_owner=*/
          std::optional(url::Origin::Create(GURL("https://bidder1.test"))),
          /*buyer_and_seller_reporting_id=*/"chair"},
         {/*ad_descriptor=*/blink::AdDescriptor(
              GURL("https://creative1.test"),
              std::optional(
                  blink::AdSize(200, blink::AdSize::LengthUnit::kPixels, 100,
                                blink::AdSize::LengthUnit::kScreenHeight))),
          /*creative_scanning_metadata=*/"scan1b",
          /*interest_group_owner=*/
          std::optional(url::Origin::Create(GURL("https://bidder1.test"))),
          /*buyer_and_seller_reporting_id=*/"sofa"},
         {/*ad_descriptor=*/blink::AdDescriptor(
              GURL("https://creative1.test"),
              std::optional(
                  blink::AdSize(200, blink::AdSize::LengthUnit::kPixels, 100,
                                blink::AdSize::LengthUnit::kScreenHeight))),
          /*creative_scanning_metadata=*/"scan1/2",
          /*interest_group_owner=*/
          std::optional(url::Origin::Create(GURL("https://bidder2.test"))),
          /*buyer_and_seller_reporting_id=*/"stool"},
         {/*ad_descriptor=*/blink::AdDescriptor(
              GURL("https://creative2.test"),
              std::optional(
                  blink::AdSize(200, blink::AdSize::LengthUnit::kPixels, 100,
                                blink::AdSize::LengthUnit::kScreenHeight))),
          /*creative_scanning_metadata=*/"scan2",
          /*interest_group_owner=*/
          std::optional(url::Origin::Create(GURL("https://bidder2.test"))),
          /*buyer_and_seller_reporting_id=*/"stool"},
         {// Same thing at different size.
          /*ad_descriptor=*/blink::AdDescriptor(
              GURL("https://creative2.test"),
              std::optional(
                  blink::AdSize(100, blink::AdSize::LengthUnit::kPixels, 50,
                                blink::AdSize::LengthUnit::kScreenHeight))),
          /*creative_scanning_metadata=*/"scan2",
          /*interest_group_owner=*/
          std::optional(url::Origin::Create(GURL("https://bidder2.test"))),
          /*buyer_and_seller_reporting_id=*/"stool"}});

    std::set<TrustedSignals::CreativeInfo> creative_set;
    for (auto& input : test_data) {
      if (!send_creative_scanning_metadata) {
        input.ad_descriptor.size = std::nullopt;
        input.creative_scanning_metadata.clear();
        input.interest_group_owner = std::nullopt;
        input.buyer_and_seller_reporting_id.clear();
      }
      creative_set.insert(std::move(input));
    }

    // Minimal valid input for main ad, to use when mainly testing component-ad
    // specific query params..
    std::set<TrustedSignals::CreativeInfo> single_ad_set;
    TrustedSignals::CreativeInfo single_ad;
    single_ad.ad_descriptor.url = GURL("https://product.test");
    if (send_creative_scanning_metadata) {
      single_ad.interest_group_owner =
          url::Origin::Create(GURL("https://bidder0.test"));
      single_ad.buyer_and_seller_reporting_id = "throne";
    }
    single_ad_set.insert(std::move(single_ad));

    GURL ads_result;
    {
      std::vector<TrustedSignals::UrlPiece> main_fragments, aux_fragments;
      TrustedSignals::BuildTrustedScoringSignalsURL(
          send_creative_scanning_metadata,
          /*hostname=*/"https://publisher.test/",
          /*trusted_scoring_signals_url=*/GURL("https://kv.test"),
          /*ads=*/creative_set,
          /*component_ads=*/{},
          /*experiment_group_id=*/std::nullopt, main_fragments, aux_fragments);
      ads_result =
          TrustedSignals::ComposeURLForTesting(main_fragments, aux_fragments);
    }
    GURL component_ads_result;
    {
      std::vector<TrustedSignals::UrlPiece> main_fragments, aux_fragments;
      TrustedSignals::BuildTrustedScoringSignalsURL(
          send_creative_scanning_metadata,
          /*hostname=*/"https://publisher.test/",
          /*trusted_scoring_signals_url=*/GURL("https://kv.test"),
          /*ads=*/single_ad_set,
          /*component_ads=*/creative_set,
          /*experiment_group_id=*/std::nullopt, main_fragments, aux_fragments);
      component_ads_result =
          TrustedSignals::ComposeURLForTesting(main_fragments, aux_fragments);
    }

    if (send_creative_scanning_metadata) {
      EXPECT_EQ(
          "https://kv.test/?hostname=https%3A%2F%2Fpublisher.test%2F"
          "&renderUrls=https%3A%2F%2Fcreative1.test%2F,"
          "https%3A%2F%2Fcreative1.test%2F,"
          "https%3A%2F%2Fcreative1.test%2F,"
          "https%3A%2F%2Fcreative2.test%2F,"
          "https%3A%2F%2Fcreative2.test%2F&"
          "adCreativeScanningMetadata=scan1,scan1%2F2,scan1b,scan2,scan2"
          "&adSizes=200px,100sh,200px,100sh,200px,100sh,100px,50sh,200px,100sh"
          "&adBuyer=https%3A%2F%2Fbidder1.test,"
          "https%3A%2F%2Fbidder2.test,"
          "https%3A%2F%2Fbidder1.test,"
          "https%3A%2F%2Fbidder2.test,"
          "https%3A%2F%2Fbidder2.test"
          "&adBuyerAndSellerReportingIds=chair,stool,sofa,stool,stool",
          ads_result);
      EXPECT_EQ(
          "https://kv.test/?hostname=https%3A%2F%2Fpublisher.test%2F"
          "&renderUrls=https%3A%2F%2Fproduct.test%2F"
          "&adComponentRenderUrls=https%3A%2F%2Fcreative1.test%2F,"
          "https%3A%2F%2Fcreative1.test%2F,"
          "https%3A%2F%2Fcreative1.test%2F,"
          "https%3A%2F%2Fcreative2.test%2F,"
          "https%3A%2F%2Fcreative2.test%2F"
          "&adCreativeScanningMetadata="
          "&adComponentCreativeScanningMetadata="
          "scan1,scan1%2F2,scan1b,scan2,scan2"
          "&adSizes=,"
          "&adComponentSizes="
          "200px,100sh,200px,100sh,200px,100sh,100px,50sh,200px,100sh"
          "&adBuyer=https%3A%2F%2Fbidder0.test"
          "&adComponentBuyer=https%3A%2F%2Fbidder1.test,"
          "https%3A%2F%2Fbidder2.test,"
          "https%3A%2F%2Fbidder1.test,"
          "https%3A%2F%2Fbidder2.test,"
          "https%3A%2F%2Fbidder2.test"
          "&adBuyerAndSellerReportingIds=throne",
          component_ads_result);
    } else {
      EXPECT_EQ(
          "https://kv.test/?hostname=https%3A%2F%2Fpublisher.test%2F"
          "&renderUrls=https%3A%2F%2Fcreative1.test%2F,"
          "https%3A%2F%2Fcreative2.test%2F",
          ads_result);
      EXPECT_EQ(
          "https://kv.test/?hostname=https%3A%2F%2Fpublisher.test%2F"
          "&renderUrls=https%3A%2F%2Fproduct.test%2F"
          "&adComponentRenderUrls=https%3A%2F%2Fcreative1.test%2F,"
          "https%3A%2F%2Fcreative2.test%2F",
          component_ads_result);
    }
  }
}

// An empty size should be a comma, to make things line up properly.
TEST_F(TrustedSignalsTest, BuildTrustedScoringSignalsURLNoSize) {
  for (const bool both_without_size : {false, true}) {
    SCOPED_TRACE(both_without_size);
    std::set<TrustedSignals::CreativeInfo> input;

    input.insert(TrustedSignals::CreativeInfo(
        /*ad_descriptor=*/blink::AdDescriptor(GURL("https://c1.test"),
                                              /*size=*/std::nullopt),
        /*creative_scanning_metadata=*/"s1",
        /*interest_group_owner=*/url::Origin::Create(GURL("https://b1.test")),
        /*buyer_and_seller_reporting_id=*/"stool"));

    input.insert(TrustedSignals::CreativeInfo(
        /*ad_descriptor=*/blink::AdDescriptor(
            GURL("https://c2.test"),
            /*size=*/both_without_size
                ? std::nullopt
                : std::optional(
                      blink::AdSize(100, blink::AdSize::LengthUnit::kPixels, 50,
                                    blink::AdSize::LengthUnit::kPixels))),
        /*creative_scanning_metadata=*/"s2",
        /*interest_group_owner=*/url::Origin::Create(GURL("https://b2.test")),
        /*buyer_and_seller_reporting_id=*/"chair"));

    std::vector<TrustedSignals::UrlPiece> main_fragments, aux_fragments;
    TrustedSignals::BuildTrustedScoringSignalsURL(
        /*send_creative_scanning_metadata=*/true,
        /*hostname=*/"https://publisher.test/",
        /*trusted_scoring_signals_url=*/GURL("https://kv.test"),
        /*ads=*/input,
        /*component_ads=*/{},
        /*experiment_group_id=*/std::nullopt, main_fragments, aux_fragments);
    GURL result =
        TrustedSignals::ComposeURLForTesting(main_fragments, aux_fragments);

    if (both_without_size) {
      EXPECT_EQ(
          "https://kv.test/?hostname=https%3A%2F%2Fpublisher.test%2F"
          "&renderUrls=https%3A%2F%2Fc1.test%2F,https%3A%2F%2Fc2.test%2F"
          "&adCreativeScanningMetadata=s1,s2"
          "&adSizes=,,,"
          "&adBuyer=https%3A%2F%2Fb1.test,https%3A%2F%2Fb2.test"
          "&adBuyerAndSellerReportingIds=stool,chair",
          result);
    } else {
      EXPECT_EQ(
          "https://kv.test/?hostname=https%3A%2F%2Fpublisher.test%2F"
          "&renderUrls=https%3A%2F%2Fc1.test%2F,https%3A%2F%2Fc2.test%2F"
          "&adCreativeScanningMetadata=s1,s2"
          "&adSizes=,,100px,50px"
          "&adBuyer=https%3A%2F%2Fb1.test,https%3A%2F%2Fb2.test"
          "&adBuyerAndSellerReportingIds=stool,chair",
          result);
    }
  }
}

TEST_F(TrustedSignalsTest,
       BuildTrustedScoringSignalsURLEmptyCreativeScanMetadata) {
  std::set<TrustedSignals::CreativeInfo> input;

  input.insert(TrustedSignals::CreativeInfo(
      /*ad_descriptor=*/blink::AdDescriptor(
          GURL("https://c1.test"),
          blink::AdSize(100, blink::AdSize::LengthUnit::kPixels, 50,
                        blink::AdSize::LengthUnit::kPixels)),
      /*creative_scanning_metadata=*/std::string(),
      /*interest_group_owner=*/url::Origin::Create(GURL("https://b1.test")),
      /*buyer_and_seller_reporting_id=*/"stool"));

  input.insert(TrustedSignals::CreativeInfo(
      /*ad_descriptor=*/blink::AdDescriptor(
          GURL("https://c2.test"),
          blink::AdSize(100, blink::AdSize::LengthUnit::kPixels, 50,
                        blink::AdSize::LengthUnit::kPixels)),
      /*creative_scanning_metadata=*/"s2",
      /*interest_group_owner=*/url::Origin::Create(GURL("https://b2.test")),
      /*buyer_and_seller_reporting_id=*/"recliner"));

  std::vector<TrustedSignals::UrlPiece> main_fragments, aux_fragments;
  TrustedSignals::BuildTrustedScoringSignalsURL(
      /*send_creative_scanning_metadata=*/true,
      /*hostname=*/"https://publisher.test/",
      /*trusted_scoring_signals_url=*/GURL("https://kv.test"),
      /*ads=*/std::move(input),
      /*component_ads=*/{},
      /*experiment_group_id=*/std::nullopt, main_fragments, aux_fragments);
  GURL result =
      TrustedSignals::ComposeURLForTesting(main_fragments, aux_fragments);

  EXPECT_EQ(
      "https://kv.test/?hostname=https%3A%2F%2Fpublisher.test%2F"
      "&renderUrls=https%3A%2F%2Fc1.test%2F,https%3A%2F%2Fc2.test%2F"
      "&adCreativeScanningMetadata=,s2"
      "&adSizes=100px,50px,100px,50px"
      "&adBuyer=https%3A%2F%2Fb1.test,https%3A%2F%2Fb2.test"
      "&adBuyerAndSellerReportingIds=stool,recliner",
      result);
}

TEST_F(TrustedSignalsTest, ScoringSignalsCreativeScanning) {
  std::set<TrustedSignals::CreativeInfo> ads;
  ads.insert(TrustedSignals::CreativeInfo(
      /*ad_descriptor=*/blink::AdDescriptor(
          GURL("https://foo.test"),
          blink::AdSize(100, blink::AdSize::LengthUnit::kPixels, 50,
                        blink::AdSize::LengthUnit::kPixels)),
      /*creative_scanning_metadata=*/"s1",
      /*interest_group_owner=*/url::Origin::Create(GURL("https://b1.test")),
      /*buyer_and_seller_reporting_id=*/"stool"));

  ads.insert(TrustedSignals::CreativeInfo(
      /*ad_descriptor=*/blink::AdDescriptor(
          GURL("https://foo.test"),
          blink::AdSize(100, blink::AdSize::LengthUnit::kPixels, 50,
                        blink::AdSize::LengthUnit::kPixels)),
      /*creative_scanning_metadata=*/"s2",
      /*interest_group_owner=*/url::Origin::Create(GURL("https://b2.test")),
      /*buyer_and_seller_reporting_id=*/"sofa"));

  ads.insert(TrustedSignals::CreativeInfo(
      /*ad_descriptor=*/blink::AdDescriptor(GURL("https://bar.test")),
      /*creative_scanning_metadata=*/"s3",
      /*interest_group_owner=*/url::Origin::Create(GURL("https://b2.test")),
      /*buyer_and_seller_reporting_id=*/"chair"));

  std::set<TrustedSignals::CreativeInfo> ad_components;
  ad_components.insert(TrustedSignals::CreativeInfo(
      /*ad_descriptor=*/blink::AdDescriptor(
          GURL("https://foosub.test"),
          blink::AdSize(30, blink::AdSize::LengthUnit::kPixels, 16,
                        blink::AdSize::LengthUnit::kPixels)),
      /*creative_scanning_metadata=*/"c1",
      /*interest_group_owner=*/url::Origin::Create(GURL("https://b1.test")),
      /*buyer_and_seller_reporting_id=*/std::string()));

  ad_components.insert(TrustedSignals::CreativeInfo(
      /*ad_descriptor=*/blink::AdDescriptor(
          GURL("https://barsub.test"),
          blink::AdSize(60, blink::AdSize::LengthUnit::kPixels, 32,
                        blink::AdSize::LengthUnit::kPixels)),
      /*creative_scanning_metadata=*/"c2",
      /*interest_group_owner=*/url::Origin::Create(GURL("https://b2.test")),
      /*buyer_and_seller_reporting_id=*/std::string()));

  GURL response_url(
      "https://url.test/?hostname=publisher&"
      "renderUrls=https%3A%2F%2Fbar.test%2F,https%3A%2F%2Ffoo.test%2F,"
      "https%3A%2F%2Ffoo.test%2F"
      "&adComponentRenderUrls=https%3A%2F%2Fbarsub.test%2F,"
      "https%3A%2F%2Ffoosub.test%2F"
      "&adCreativeScanningMetadata=s3,s1,s2"
      "&adComponentCreativeScanningMetadata=c2,c1"
      "&adSizes=,,100px,50px,100px,50px"
      "&adComponentSizes=60px,32px,30px,16px"
      "&adBuyer=https%3A%2F%2Fb2.test,https%3A%2F%2Fb1.test,"
      "https%3A%2F%2Fb2.test"
      "&adComponentBuyer=https%3A%2F%2Fb2.test,https%3A%2F%2Fb1.test"
      "&adBuyerAndSellerReportingIds=chair,stool,sofa");

  AddJsonResponse(&url_loader_factory_, response_url, kBaseScoringJson);

  scoped_refptr<TrustedSignals::Result> signals =
      FetchScoringSignals(std::move(ads), std::move(ad_components), kHostname,
                          /*experiment_group_id=*/std::nullopt,
                          /*send_creative_scanning_metadata=*/true);
  ASSERT_TRUE(signals);

  EXPECT_THAT(ExtractScoringSignals(
                  signals.get(),
                  /*render_url=*/GURL("https://foo.test/"),
                  /*ad_component_render_urls=*/{"https://foosub.test/"}),
              base::test::IsJson(R"({
          "renderURL":{"https://foo.test/":1},
          "renderUrl":{"https://foo.test/":1},
          "adComponentRenderURLs":{"https://foosub.test/":2},
          "adComponentRenderUrls":{"https://foosub.test/":2}
      })"));

  EXPECT_THAT(ExtractScoringSignals(
                  signals.get(),
                  /*render_url=*/GURL("https://bar.test/"),
                  /*ad_component_render_urls=*/{"https://barsub.test/"}),
              base::test::IsJson(R"({
          "renderURL":{"https://bar.test/":[2]},
          "renderUrl":{"https://bar.test/":[2]},
          "adComponentRenderURLs":{"https://barsub.test/":[3]},
          "adComponentRenderUrls":{"https://barsub.test/":[3]}
      })"));
}

TEST_F(TrustedSignalsTest, ComposeURL) {
  EXPECT_EQ(
      GURL("https://example.test/?stuff&foo=bar"),
      TrustedSignals::ComposeURLForTesting(
          {{TrustedSignals::UrlField::kKeys, "&foo=bar"},
           {TrustedSignals::UrlField::kBase, "https://example.test/?stuff"}},
          {}));

  EXPECT_EQ(
      GURL("https://example.test/?stuff&foo=bar,baz"
           "&igs=cars,bicycles,biplanes&buyer=pub1.com,pub2.com&bsid=cushion"),
      TrustedSignals::ComposeURLForTesting(
          {{TrustedSignals::UrlField::kKeys, "&foo=bar"},
           {TrustedSignals::UrlField::kBase, "https://example.test/?stuff"},
           {TrustedSignals::UrlField::kKeys, ",baz"},
           {TrustedSignals::UrlField::kBuyerAndSellerReportingIds,
            "&bsid=cushion"}},
          {{TrustedSignals::UrlField::kInterestGroupNames, "&igs=cars"},
           {TrustedSignals::UrlField::kAdBuyer, "&buyer=pub1.com"},
           {TrustedSignals::UrlField::kInterestGroupNames, ",bicycles"},
           {TrustedSignals::UrlField::kInterestGroupNames, ",biplanes"},
           {TrustedSignals::UrlField::kAdBuyer, ",pub2.com"}}));
}

TEST_F(TrustedSignalsTest, IncrementalBuildTrustedSignalsURL) {
  using enum TrustedSignals::UrlField;

  const GURL kBaseURL("https://tkv.com/");
  const uint16_t kExperiment = 123;
  const std::string kSlotSize = "slotSize=giant";

  std::vector<TrustedSignals::UrlPiece> main_fragments;
  std::vector<TrustedSignals::UrlPiece> key_fragments;

  TrustedSignals::BuildTrustedBiddingSignalsURL(
      kHostname, kBaseURL, {"ig1"}, {"k1", "k2"}, kExperiment, kSlotSize,
      main_fragments, key_fragments);
  std::vector<TrustedSignals::UrlPiece> expected_main = {
      {kBase, "https://tkv.com/?hostname=publisher"},
      {kInterestGroupNames, "&interestGroupNames=ig1"},
      {kExperimentGroupId, "&experimentGroupId=123"},
      {kSlotSizeParam, "&slotSize=giant"}};
  std::vector<TrustedSignals::UrlPiece> expected_key = {{kKeys, "&keys=k1,k2"}};
  EXPECT_THAT(main_fragments, testing::ElementsAreArray(expected_main));
  EXPECT_THAT(key_fragments, testing::ElementsAreArray(expected_key));
  EXPECT_EQ(
      "https://tkv.com/"
      "?hostname=publisher&keys=k1,k2&interestGroupNames=ig1&experimentGroupId="
      "123&slotSize=giant",
      TrustedSignals::ComposeURLForTesting(main_fragments, key_fragments));

  TrustedSignals::BuildTrustedBiddingSignalsURL(
      kHostname, kBaseURL, {"ig2"}, {"k3", "k4"}, kExperiment, kSlotSize,
      main_fragments, key_fragments);
  expected_main.push_back({kInterestGroupNames, ",ig2"});
  expected_key.push_back({kKeys, ",k3,k4"});
  EXPECT_THAT(main_fragments, testing::ElementsAreArray(expected_main));
  EXPECT_THAT(key_fragments, testing::ElementsAreArray(expected_key));
  EXPECT_EQ(
      "https://tkv.com/"
      "?hostname=publisher&keys=k1,k2,k3,k4&interestGroupNames=ig1,ig2&"
      "experimentGroupId=123&slotSize=giant",
      TrustedSignals::ComposeURLForTesting(main_fragments, key_fragments));

  TrustedSignals::BuildTrustedBiddingSignalsURL(kHostname, kBaseURL, {"ig3"},
                                                {}, kExperiment, kSlotSize,
                                                main_fragments, key_fragments);
  expected_main.push_back({kInterestGroupNames, ",ig3"});
  EXPECT_THAT(main_fragments, testing::ElementsAreArray(expected_main));
  EXPECT_THAT(key_fragments, testing::ElementsAreArray(expected_key));
  EXPECT_EQ(
      "https://tkv.com/"
      "?hostname=publisher&keys=k1,k2,k3,k4&interestGroupNames=ig1,ig2,ig3&"
      "experimentGroupId=123&slotSize=giant",
      TrustedSignals::ComposeURLForTesting(main_fragments, key_fragments));
}

// Variant where keys is empty on first call.
TEST_F(TrustedSignalsTest, IncrementalBuildTrustedSignalsURL2) {
  using enum TrustedSignals::UrlField;

  const GURL kBaseURL("https://tkv.com/");
  const uint16_t kExperiment = 123;
  const std::string kSlotSize = "slotSize=giant";

  std::vector<TrustedSignals::UrlPiece> main_fragments;
  std::vector<TrustedSignals::UrlPiece> key_fragments;

  TrustedSignals::BuildTrustedBiddingSignalsURL(kHostname, kBaseURL, {"ig1"},
                                                {}, kExperiment, kSlotSize,
                                                main_fragments, key_fragments);
  std::vector<TrustedSignals::UrlPiece> expected_main = {
      {kBase, "https://tkv.com/?hostname=publisher"},
      {kInterestGroupNames, "&interestGroupNames=ig1"},
      {kExperimentGroupId, "&experimentGroupId=123"},
      {kSlotSizeParam, "&slotSize=giant"}};
  std::vector<TrustedSignals::UrlPiece> expected_key = {};
  EXPECT_THAT(main_fragments, testing::ElementsAreArray(expected_main));
  EXPECT_THAT(key_fragments, testing::ElementsAreArray(expected_key));
  EXPECT_EQ(
      "https://tkv.com/"
      "?hostname=publisher&interestGroupNames=ig1&experimentGroupId=123&"
      "slotSize=giant",
      TrustedSignals::ComposeURLForTesting(main_fragments, key_fragments));

  TrustedSignals::BuildTrustedBiddingSignalsURL(
      kHostname, kBaseURL, {"ig2"}, {"k3", "k4"}, kExperiment, kSlotSize,
      main_fragments, key_fragments);
  expected_main.push_back({kInterestGroupNames, ",ig2"});
  expected_key.push_back({kKeys, "&keys=k3,k4"});
  EXPECT_THAT(main_fragments, testing::ElementsAreArray(expected_main));
  EXPECT_THAT(key_fragments, testing::ElementsAreArray(expected_key));
  EXPECT_EQ(
      "https://tkv.com/"
      "?hostname=publisher&keys=k3,k4&interestGroupNames=ig1,ig2&"
      "experimentGroupId=123&slotSize=giant",
      TrustedSignals::ComposeURLForTesting(main_fragments, key_fragments));
}

TEST_F(TrustedSignalsTest, IncrementalBuildScoringSignalsURL) {
  using enum TrustedSignals::UrlField;

  const GURL kBaseURL("https://tkv.com/");
  const uint16_t kExperiment = 123;

  std::vector<TrustedSignals::UrlPiece> main_fragments;
  std::vector<TrustedSignals::UrlPiece> ad_component_fragments;

  TrustedSignals::BuildTrustedScoringSignalsURL(
      /*send_creative_scanning_metadata=*/false, kHostname, kBaseURL,
      CreateCreativeInfoSet({"https://a1.com"}),
      CreateCreativeInfoSet({"https://c1.com", "https://c3.com"}), kExperiment,
      main_fragments, ad_component_fragments);
  std::vector<TrustedSignals::UrlPiece> expected_main = {
      {kBase, "https://tkv.com/?hostname=publisher"},
      {kRenderUrls, "&renderUrls=https%3A%2F%2Fa1.com%2F"},
      {kExperimentGroupId, "&experimentGroupId=123"}};
  std::vector<TrustedSignals::UrlPiece> expected_component = {
      {kAdComponentRenderUrls,
       "&adComponentRenderUrls=https%3A%2F%2Fc1.com%2F,"
       "https%3A%2F%2Fc3.com%2F"}};
  EXPECT_THAT(main_fragments, testing::ElementsAreArray(expected_main));
  EXPECT_THAT(ad_component_fragments,
              testing::ElementsAreArray(expected_component));
  EXPECT_EQ(
      "https://tkv.com/"
      "?hostname=publisher&renderUrls=https%3A%2F%2Fa1.com%2F&"
      "adComponentRenderUrls=https%3A%2F%2Fc1.com%2F,https%3A%2F%2Fc3.com%2F&"
      "experimentGroupId=123",
      TrustedSignals::ComposeURLForTesting(main_fragments,
                                           ad_component_fragments));

  TrustedSignals::BuildTrustedScoringSignalsURL(
      /*send_creative_scanning_metadata=*/false, kHostname, kBaseURL,
      CreateCreativeInfoSet({"https://a2.com"}), {}, kExperiment,
      main_fragments, ad_component_fragments);
  expected_main.push_back({kRenderUrls, ",https%3A%2F%2Fa2.com%2F"});
  EXPECT_THAT(main_fragments, testing::ElementsAreArray(expected_main));
  EXPECT_THAT(ad_component_fragments,
              testing::ElementsAreArray(expected_component));
  EXPECT_EQ(
      "https://tkv.com/"
      "?hostname=publisher&renderUrls=https%3A%2F%2Fa1.com%2F,https%3A%2F%2Fa2."
      "com%2F&adComponentRenderUrls=https%3A%2F%2Fc1.com%2F,https%3A%2F%2Fc3."
      "com%2F&experimentGroupId=123",
      TrustedSignals::ComposeURLForTesting(main_fragments,
                                           ad_component_fragments));

  TrustedSignals::BuildTrustedScoringSignalsURL(
      /*send_creative_scanning_metadata=*/false, kHostname, kBaseURL,
      CreateCreativeInfoSet({"https://a3.com"}),
      CreateCreativeInfoSet({"https://c2.com", "https://c4.com"}), kExperiment,
      main_fragments, ad_component_fragments);
  expected_main.push_back({kRenderUrls, ",https%3A%2F%2Fa3.com%2F"});
  expected_component.push_back(
      {kAdComponentRenderUrls,
       ",https%3A%2F%2Fc2.com%2F,https%3A%2F%2Fc4.com%2F"});
  EXPECT_THAT(main_fragments, testing::ElementsAreArray(expected_main));
  EXPECT_THAT(ad_component_fragments,
              testing::ElementsAreArray(expected_component));
  EXPECT_EQ(
      "https://tkv.com/"
      "?hostname=publisher&renderUrls=https%3A%2F%2Fa1.com%2F,https%3A%2F%2Fa2."
      "com%2F,https%3A%2F%2Fa3.com%2F&adComponentRenderUrls=https%3A%2F%2Fc1."
      "com%2F,https%3A%2F%2Fc3.com%2F,https%3A%2F%2Fc2.com%2F,https%3A%2F%2Fc4."
      "com%2F&experimentGroupId=123",
      TrustedSignals::ComposeURLForTesting(main_fragments,
                                           ad_component_fragments));
}

// Variant where component ads set is empty on first call.
TEST_F(TrustedSignalsTest, IncrementalBuildScoringSignalsURL2) {
  using enum TrustedSignals::UrlField;

  const GURL kBaseURL("https://tkv.com/");
  const uint16_t kExperiment = 123;

  std::vector<TrustedSignals::UrlPiece> main_fragments;
  std::vector<TrustedSignals::UrlPiece> ad_component_fragments;

  TrustedSignals::BuildTrustedScoringSignalsURL(
      /*send_creative_scanning_metadata=*/false, kHostname, kBaseURL,
      CreateCreativeInfoSet({"https://a1.com"}), {}, kExperiment,
      main_fragments, ad_component_fragments);
  std::vector<TrustedSignals::UrlPiece> expected_main = {
      {kBase, "https://tkv.com/?hostname=publisher"},
      {kRenderUrls, "&renderUrls=https%3A%2F%2Fa1.com%2F"},
      {kExperimentGroupId, "&experimentGroupId=123"}};
  std::vector<TrustedSignals::UrlPiece> expected_component;
  EXPECT_THAT(main_fragments, testing::ElementsAreArray(expected_main));
  EXPECT_THAT(ad_component_fragments,
              testing::ElementsAreArray(expected_component));
  EXPECT_EQ(
      "https://tkv.com/"
      "?hostname=publisher&renderUrls=https%3A%2F%2Fa1.com%2F&"
      "experimentGroupId=123",
      TrustedSignals::ComposeURLForTesting(main_fragments,
                                           ad_component_fragments));

  TrustedSignals::BuildTrustedScoringSignalsURL(
      /*send_creative_scanning_metadata=*/false, kHostname, kBaseURL,
      CreateCreativeInfoSet({"https://a2.com"}),
      CreateCreativeInfoSet({"https://c1.com", "https://c3.com"}), kExperiment,
      main_fragments, ad_component_fragments);
  expected_main.push_back({kRenderUrls, ",https%3A%2F%2Fa2.com%2F"});
  expected_component.push_back(
      {kAdComponentRenderUrls,
       "&adComponentRenderUrls=https%3A%2F%2Fc1.com%2F,"
       "https%3A%2F%2Fc3.com%2F"});
  EXPECT_THAT(main_fragments, testing::ElementsAreArray(expected_main));
  EXPECT_THAT(ad_component_fragments,
              testing::ElementsAreArray(expected_component));
  EXPECT_EQ(
      "https://tkv.com/"
      "?hostname=publisher&renderUrls=https%3A%2F%2Fa1.com%2F,https%3A%2F%2Fa2."
      "com%2F&adComponentRenderUrls=https%3A%2F%2Fc1.com%2F,https%3A%2F%2Fc3."
      "com%2F&experimentGroupId=123",
      TrustedSignals::ComposeURLForTesting(main_fragments,
                                           ad_component_fragments));
}

// Variant with creative scanning info.
TEST_F(TrustedSignalsTest, IncrementalBuildScoringSignalsURL3) {
  using enum TrustedSignals::UrlField;

  const GURL kBaseURL("https://tkv.com/");
  const uint16_t kExperiment = 123;

  TrustedSignals::CreativeInfo m1(
      /*ad_descriptor=*/blink::AdDescriptor(
          GURL("https://creative1.test"),
          std::optional(
              blink::AdSize(200, blink::AdSize::LengthUnit::kPixels, 100,
                            blink::AdSize::LengthUnit::kScreenHeight))),
      /*creative_scanning_metadata=*/"scan1",
      /*interest_group_owner=*/
      std::optional(url::Origin::Create(GURL("https://bidder1.test"))),
      /*buyer_and_seller_reporting_id=*/"chair");

  TrustedSignals::CreativeInfo m2(
      /*ad_descriptor=*/blink::AdDescriptor(
          GURL("https://creative1.test"),
          std::optional(
              blink::AdSize(200, blink::AdSize::LengthUnit::kPixels, 100,
                            blink::AdSize::LengthUnit::kScreenHeight))),
      /*creative_scanning_metadata=*/"scan1b",
      /*interest_group_owner=*/
      std::optional(url::Origin::Create(GURL("https://bidder1.test"))),
      /*buyer_and_seller_reporting_id=*/"sofa");

  TrustedSignals::CreativeInfo m3(
      /*ad_descriptor=*/blink::AdDescriptor(GURL("https://creative2.test")),
      /*creative_scanning_metadata=*/"scan2",
      /*interest_group_owner=*/
      std::optional(url::Origin::Create(GURL("https://bidder2.test"))),
      /*buyer_and_seller_reporting_id=*/"stool");

  TrustedSignals::CreativeInfo c1(
      /*ad_descriptor=*/blink::AdDescriptor(
          GURL("https://c1.test"),
          std::optional(
              blink::AdSize(100, blink::AdSize::LengthUnit::kPixels, 50,
                            blink::AdSize::LengthUnit::kScreenHeight))),
      /*creative_scanning_metadata=*/"cs1",
      /*interest_group_owner=*/
      std::optional(url::Origin::Create(GURL("https://c.bidder1.test"))),
      /*buyer_and_seller_reporting_id=*/"c.chair");

  TrustedSignals::CreativeInfo c2(
      /*ad_descriptor=*/blink::AdDescriptor(
          GURL("https://c2.test"),
          std::optional(
              blink::AdSize(99, blink::AdSize::LengthUnit::kPixels, 49,
                            blink::AdSize::LengthUnit::kScreenHeight))),
      /*creative_scanning_metadata=*/"cs2",
      /*interest_group_owner=*/
      std::optional(url::Origin::Create(GURL("https://c.bidder1.test"))),
      /*buyer_and_seller_reporting_id=*/"c.sofa");

  TrustedSignals::CreativeInfo c3(
      /*ad_descriptor=*/blink::AdDescriptor(GURL("https://c3.test")),
      /*creative_scanning_metadata=*/"cs3",
      /*interest_group_owner=*/
      std::optional(url::Origin::Create(GURL("https://c.bidder2.test"))),
      /*buyer_and_seller_reporting_id=*/"c.stool");

  std::vector<TrustedSignals::UrlPiece> main_fragments;
  std::vector<TrustedSignals::UrlPiece> ad_component_fragments;

  TrustedSignals::BuildTrustedScoringSignalsURL(
      /*send_creative_scanning_metadata=*/true, kHostname, kBaseURL, {m1}, {},
      kExperiment, main_fragments, ad_component_fragments);
  std::vector<TrustedSignals::UrlPiece> expected_main = {
      {kBase, "https://tkv.com/?hostname=publisher"},
      {kRenderUrls, "&renderUrls=https%3A%2F%2Fcreative1.test%2F"},
      {kExperimentGroupId, "&experimentGroupId=123"},
      {kAdCreativeScanningMetadata, "&adCreativeScanningMetadata=scan1"},
      {kAdSizes, "&adSizes=200px,100sh"},
      {kAdBuyer, "&adBuyer=https%3A%2F%2Fbidder1.test"},
      {kBuyerAndSellerReportingIds, "&adBuyerAndSellerReportingIds=chair"}};
  std::vector<TrustedSignals::UrlPiece> expected_component = {};
  EXPECT_THAT(main_fragments, testing::ElementsAreArray(expected_main));
  EXPECT_THAT(ad_component_fragments,
              testing::ElementsAreArray(expected_component));
  EXPECT_EQ(
      "https://tkv.com/"
      "?hostname=publisher&renderUrls=https%3A%2F%2Fcreative1.test%2F&"
      "experimentGroupId=123&adCreativeScanningMetadata=scan1&adSizes=200px,"
      "100sh&adBuyer=https%3A%2F%2Fbidder1.test&adBuyerAndSellerReportingIds="
      "chair",
      TrustedSignals::ComposeURLForTesting(main_fragments,
                                           ad_component_fragments));

  TrustedSignals::BuildTrustedScoringSignalsURL(
      /*send_creative_scanning_metadata=*/true, kHostname, kBaseURL, {m2},
      {c1, c2}, kExperiment, main_fragments, ad_component_fragments);
  expected_main.push_back({kRenderUrls, ",https%3A%2F%2Fcreative1.test%2F"});
  expected_main.push_back({kAdCreativeScanningMetadata, ",scan1b"});
  expected_main.push_back({kAdSizes, ",200px,100sh"});
  expected_main.push_back({kAdBuyer, ",https%3A%2F%2Fbidder1.test"});
  expected_main.push_back({kBuyerAndSellerReportingIds, ",sofa"});

  expected_component.push_back({kAdComponentRenderUrls,
                                "&adComponentRenderUrls=https%3A%2F%2Fc1.test%"
                                "2F,https%3A%2F%2Fc2.test%2F"});
  expected_component.push_back(
      {kAdComponentCreativeScanningMetadata,
       "&adComponentCreativeScanningMetadata=cs1,cs2"});
  expected_component.push_back(
      {kAdComponentSizes, "&adComponentSizes=100px,50sh,99px,49sh"});
  expected_component.push_back({kAdComponentBuyer,
                                "&adComponentBuyer=https%3A%2F%2Fc.bidder1."
                                "test,https%3A%2F%2Fc.bidder1.test"});

  EXPECT_THAT(main_fragments, testing::ElementsAreArray(expected_main));
  EXPECT_THAT(ad_component_fragments,
              testing::ElementsAreArray(expected_component));
  EXPECT_EQ(
      "https://tkv.com/"
      "?hostname=publisher&renderUrls=https%3A%2F%2Fcreative1.test%2F,https%3A%"
      "2F%2Fcreative1.test%2F&adComponentRenderUrls=https%3A%2F%2Fc1.test%2F,"
      "https%3A%2F%2Fc2.test%2F&experimentGroupId=123&"
      "adCreativeScanningMetadata=scan1,scan1b&"
      "adComponentCreativeScanningMetadata=cs1,cs2&adSizes=200px,100sh,200px,"
      "100sh&adComponentSizes=100px,50sh,99px,49sh&adBuyer=https%3A%2F%"
      "2Fbidder1.test,https%3A%2F%2Fbidder1.test&adComponentBuyer=https%3A%2F%"
      "2Fc.bidder1.test,https%3A%2F%2Fc.bidder1.test&"
      "adBuyerAndSellerReportingIds=chair,sofa",
      TrustedSignals::ComposeURLForTesting(main_fragments,
                                           ad_component_fragments));

  TrustedSignals::BuildTrustedScoringSignalsURL(
      /*send_creative_scanning_metadata=*/true, kHostname, kBaseURL, {m3}, {c3},
      kExperiment, main_fragments, ad_component_fragments);
  expected_main.push_back({kRenderUrls, ",https%3A%2F%2Fcreative2.test%2F"});
  expected_main.push_back({kAdCreativeScanningMetadata, ",scan2"});
  expected_main.push_back({kAdSizes, ",,"});
  expected_main.push_back({kAdBuyer, ",https%3A%2F%2Fbidder2.test"});
  expected_main.push_back({kBuyerAndSellerReportingIds, ",stool"});

  expected_component.push_back(
      {kAdComponentRenderUrls, ",https%3A%2F%2Fc3.test%2F"});
  expected_component.push_back({kAdComponentCreativeScanningMetadata, ",cs3"});
  expected_component.push_back({kAdComponentSizes, ",,"});
  expected_component.push_back(
      {kAdComponentBuyer, ",https%3A%2F%2Fc.bidder2.test"});

  EXPECT_THAT(main_fragments, testing::ElementsAreArray(expected_main));
  EXPECT_THAT(ad_component_fragments,
              testing::ElementsAreArray(expected_component));
  EXPECT_EQ(
      "https://tkv.com/"
      "?hostname=publisher&renderUrls=https%3A%2F%2Fcreative1.test%2F,https%3A%"
      "2F%2Fcreative1.test%2F,https%3A%2F%2Fcreative2.test%2F&"
      "adComponentRenderUrls=https%3A%2F%2Fc1.test%2F,https%3A%2F%2Fc2.test%2F,"
      "https%3A%2F%2Fc3.test%2F&experimentGroupId=123&"
      "adCreativeScanningMetadata=scan1,scan1b,scan2&"
      "adComponentCreativeScanningMetadata=cs1,cs2,cs3&adSizes=200px,100sh,"
      "200px,100sh,,&adComponentSizes=100px,50sh,99px,49sh,,&adBuyer=https%3A%"
      "2F%2Fbidder1.test,https%3A%2F%2Fbidder1.test,https%3A%2F%2Fbidder2.test&"
      "adComponentBuyer=https%3A%2F%2Fc.bidder1.test,https%3A%2F%2Fc.bidder1."
      "test,https%3A%2F%2Fc.bidder2.test&adBuyerAndSellerReportingIds=chair,"
      "sofa,stool",
      TrustedSignals::ComposeURLForTesting(main_fragments,
                                           ad_component_fragments));
}

}  // namespace
}  // namespace auction_worklet
