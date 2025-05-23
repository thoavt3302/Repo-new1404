// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/shared/model/web_state_list/active_web_state_observation_forwarder.h"

#import <memory>
#import <vector>

#import "base/containers/contains.h"
#import "ios/chrome/browser/shared/model/web_state_list/test/fake_web_state_list_delegate.h"
#import "ios/chrome/browser/shared/model/web_state_list/web_state_list.h"
#import "ios/chrome/browser/shared/model/web_state_list/web_state_opener.h"
#import "ios/web/public/test/fakes/fake_web_state.h"
#import "ios/web/public/web_state_observer.h"
#import "testing/gtest/include/gtest/gtest.h"
#import "testing/gtest_mac.h"
#import "testing/platform_test.h"

namespace {

class TestObserver : public web::WebStateObserver {
 public:
  TestObserver() {}
  ~TestObserver() override {}

  bool WasInvokedFor(web::WebState* web_state) {
    return base::Contains(invoker_web_states_, web_state);
  }

  void Reset() { invoker_web_states_.clear(); }

  // web::WebStateObserver.
  void RenderProcessGone(web::WebState* web_state) override {
    invoker_web_states_.push_back(web_state);
  }

 private:
  std::vector<web::WebState*> invoker_web_states_;
};

class ActiveWebStateObservationForwarderTest : public PlatformTest {
 public:
  ActiveWebStateObservationForwarderTest()
      : web_state_list_(&web_state_list_delegate_) {
    forwarder_ = std::make_unique<ActiveWebStateObservationForwarder>(
        &web_state_list_, &observer_);
  }

  web::FakeWebState* AddWebStateToList(bool activate) {
    auto web_state = std::make_unique<web::FakeWebState>();
    web::FakeWebState* web_state_ptr = web_state.get();
    web_state_list_.InsertWebState(
        std::move(web_state),
        WebStateList::InsertionParams::Automatic().Activate(activate));
    return web_state_ptr;
  }

 protected:
  FakeWebStateListDelegate web_state_list_delegate_;
  WebStateList web_state_list_;
  TestObserver observer_;
  std::unique_ptr<ActiveWebStateObservationForwarder> forwarder_;
};

}  // namespace

TEST_F(ActiveWebStateObservationForwarderTest, TestInsertActiveWebState) {
  // Insert two webstates into the list and mark the second one active.  Send
  // observer notifications for both and verify the result.
  web::FakeWebState* web_state_a = AddWebStateToList(true);
  web::FakeWebState* web_state_b = AddWebStateToList(true);
  ASSERT_EQ(web_state_b, web_state_list_.GetActiveWebState());

  web_state_a->OnRenderProcessGone();
  web_state_b->OnRenderProcessGone();

  // The observer should only be notified for the active web state B.
  EXPECT_TRUE(observer_.WasInvokedFor(web_state_b));
  EXPECT_FALSE(observer_.WasInvokedFor(web_state_a));
}

TEST_F(ActiveWebStateObservationForwarderTest, TestInsertNonActiveWebState) {
  // Insert two webstates into the list, but do not mark the second one active.
  // Send observer notifications for both and verify the result.
  web::FakeWebState* web_state_a = AddWebStateToList(true);
  web::FakeWebState* web_state_b = AddWebStateToList(false);
  ASSERT_EQ(web_state_a, web_state_list_.GetActiveWebState());

  web_state_a->OnRenderProcessGone();
  web_state_b->OnRenderProcessGone();

  // The observer should only be notified for the active web state A.
  EXPECT_TRUE(observer_.WasInvokedFor(web_state_a));
  EXPECT_FALSE(observer_.WasInvokedFor(web_state_b));
}

TEST_F(ActiveWebStateObservationForwarderTest, TestDetachActiveWebState) {
  // Insert three webstates into the list.
  web::FakeWebState* web_state_a = AddWebStateToList(true);
  web::FakeWebState* web_state_b = AddWebStateToList(true);
  web::FakeWebState* web_state_c = AddWebStateToList(true);
  ASSERT_EQ(web_state_c, web_state_list_.GetActiveWebState());

  // Remove the active web state and send observer notifications.
  std::unique_ptr<web::WebState> detached_web_state =
      web_state_list_.DetachWebStateAt(web_state_list_.active_index());
  web::WebState* active_web_state = web_state_list_.GetActiveWebState();
  web::WebState* non_active_web_state =
      (active_web_state == web_state_a ? web_state_b : web_state_a);

  web_state_a->OnRenderProcessGone();
  web_state_b->OnRenderProcessGone();
  web_state_c->OnRenderProcessGone();

  // The observer should only be notified for the new active web state.
  EXPECT_TRUE(observer_.WasInvokedFor(active_web_state));
  EXPECT_FALSE(observer_.WasInvokedFor(non_active_web_state));
  EXPECT_FALSE(observer_.WasInvokedFor(web_state_c));
}

TEST_F(ActiveWebStateObservationForwarderTest, TestDetachNonActiveWebState) {
  // Insert three webstates into the list.
  web::FakeWebState* web_state_a = AddWebStateToList(true);
  web::FakeWebState* web_state_b = AddWebStateToList(true);
  web::FakeWebState* web_state_c = AddWebStateToList(true);
  ASSERT_EQ(web_state_c, web_state_list_.GetActiveWebState());

  // Remove a non-active web state and send observer notifications.
  std::unique_ptr<web::WebState> detached_web_state =
      web_state_list_.DetachWebStateAt(
          web_state_list_.GetIndexOfWebState(web_state_a));
  ASSERT_EQ(web_state_c, web_state_list_.GetActiveWebState());

  web_state_a->OnRenderProcessGone();
  web_state_b->OnRenderProcessGone();
  web_state_c->OnRenderProcessGone();

  // The observer should only be notified for the active web state.
  EXPECT_TRUE(observer_.WasInvokedFor(web_state_c));
  EXPECT_FALSE(observer_.WasInvokedFor(web_state_a));
  EXPECT_FALSE(observer_.WasInvokedFor(web_state_b));
}

TEST_F(ActiveWebStateObservationForwarderTest, TestReplaceActiveWebState) {
  // Insert two webstates into the list and mark the second one active.
  web::FakeWebState* web_state_a = AddWebStateToList(true);
  web::FakeWebState* web_state_b = AddWebStateToList(true);
  ASSERT_EQ(web_state_b, web_state_list_.GetActiveWebState());

  // Replace the active web state.  Send notifications and verify the result.
  auto replacement_web_state = std::make_unique<web::FakeWebState>();
  web::FakeWebState* web_state_c = replacement_web_state.get();

  std::unique_ptr<web::WebState> detached_web_state =
      web_state_list_.ReplaceWebStateAt(
          web_state_list_.GetIndexOfWebState(web_state_b),
          std::move(replacement_web_state));

  web_state_a->OnRenderProcessGone();
  web_state_b->OnRenderProcessGone();
  web_state_c->OnRenderProcessGone();

  // The observer should only be notified for the new active web state C.
  EXPECT_TRUE(observer_.WasInvokedFor(web_state_c));
  EXPECT_FALSE(observer_.WasInvokedFor(web_state_a));
  EXPECT_FALSE(observer_.WasInvokedFor(web_state_b));
}

TEST_F(ActiveWebStateObservationForwarderTest, TestChangeActiveWebState) {
  // Insert two webstates into the list and mark the second one active.
  web::FakeWebState* web_state_a = AddWebStateToList(true);
  web::FakeWebState* web_state_b = AddWebStateToList(true);
  ASSERT_EQ(web_state_b, web_state_list_.GetActiveWebState());

  // Make web state A active and send notifications.
  web_state_list_.ActivateWebStateAt(
      web_state_list_.GetIndexOfWebState(web_state_a));
  web_state_a->OnRenderProcessGone();
  web_state_b->OnRenderProcessGone();

  // The observer should only be notified for the active web state A.
  EXPECT_TRUE(observer_.WasInvokedFor(web_state_a));
  EXPECT_FALSE(observer_.WasInvokedFor(web_state_b));

  // Make web state B active and send notifications.
  observer_.Reset();
  web_state_list_.ActivateWebStateAt(
      web_state_list_.GetIndexOfWebState(web_state_b));
  web_state_a->OnRenderProcessGone();
  web_state_b->OnRenderProcessGone();

  // The observer should only be notified for the active web state B.
  EXPECT_TRUE(observer_.WasInvokedFor(web_state_b));
  EXPECT_FALSE(observer_.WasInvokedFor(web_state_a));
}

TEST_F(ActiveWebStateObservationForwarderTest,
       TestNonEmptyInitialWebStateList) {
  // Insert two webstates into the list.
  web::FakeWebState* web_state_a = AddWebStateToList(true);
  web::FakeWebState* web_state_b = AddWebStateToList(true);
  ASSERT_EQ(web_state_b, web_state_list_.GetActiveWebState());

  // Recreate the multi observer to simulate creation with an already-populated
  // WebStateList.
  forwarder_.reset();
  forwarder_ = std::make_unique<ActiveWebStateObservationForwarder>(
      &web_state_list_, &observer_);

  // Send notifications and verify the result.
  web_state_a->OnRenderProcessGone();
  web_state_b->OnRenderProcessGone();

  // The observer should only be notified for the active web state B.
  EXPECT_TRUE(observer_.WasInvokedFor(web_state_b));
  EXPECT_FALSE(observer_.WasInvokedFor(web_state_a));
}
