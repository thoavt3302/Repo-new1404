// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/authentication/ui_bundled/signin/promo/signin_fullscreen_promo_scene_agent.h"

#import "base/memory/raw_ptr.h"
#import "ios/chrome/app/profile/profile_state.h"
#import "ios/chrome/app/profile/profile_state_test_utils.h"
#import "ios/chrome/browser/authentication/ui_bundled/signin/signin_utils.h"
#import "ios/chrome/browser/promos_manager/model/constants.h"
#import "ios/chrome/browser/promos_manager/model/features.h"
#import "ios/chrome/browser/promos_manager/model/mock_promos_manager.h"
#import "ios/chrome/browser/shared/coordinator/scene/scene_state.h"
#import "ios/chrome/browser/shared/coordinator/scene/test/stub_browser_provider_interface.h"
#import "ios/chrome/browser/shared/model/application_context/application_context.h"
#import "ios/chrome/browser/shared/model/browser/browser.h"
#import "ios/chrome/browser/shared/model/browser/test/test_browser.h"
#import "ios/chrome/browser/shared/model/profile/test/test_profile_ios.h"
#import "ios/chrome/browser/signin/model/authentication_service_factory.h"
#import "ios/chrome/browser/signin/model/chrome_account_manager_service_factory.h"
#import "ios/chrome/browser/signin/model/fake_authentication_service_delegate.h"
#import "ios/chrome/browser/signin/model/fake_system_identity.h"
#import "ios/chrome/browser/signin/model/fake_system_identity_manager.h"
#import "ios/chrome/browser/signin/model/identity_manager_factory.h"
#import "ios/chrome/test/ios_chrome_scoped_testing_local_state.h"
#import "ios/web/public/test/web_task_environment.h"
#import "testing/platform_test.h"
#import "third_party/ocmock/OCMock/OCMock.h"
#import "third_party/ocmock/gtest_support.h"

class SigninFullscreenPromoSceneAgentTest : public PlatformTest {
 public:
  SigninFullscreenPromoSceneAgentTest() : PlatformTest() {
    TestProfileIOS::Builder builder;
    builder.AddTestingFactory(
        AuthenticationServiceFactory::GetInstance(),
        AuthenticationServiceFactory::GetFactoryWithDelegate(
            std::make_unique<FakeAuthenticationServiceDelegate>()));

    SceneState* scene_state = [[SceneState alloc] initWithAppState:nil];
    scene_state_ = OCMPartialMock(scene_state);
    scene_state_.scene = static_cast<UIWindowScene*>(
        [[[UIApplication sharedApplication] connectedScenes] anyObject]);
    profile_ = std::move(builder).Build();
    browser_ = std::make_unique<TestBrowser>(profile_.get(), scene_state_);
    stub_browser_interface_provider_ =
        [[StubBrowserProviderInterface alloc] init];
    stub_browser_interface_provider_.mainBrowserProvider.browser =
        browser_.get();
    OCMStub([scene_state_ browserProviderInterface])
        .andReturn(stub_browser_interface_provider_);
    promos_manager_ = std::make_unique<MockPromosManager>();
    agent_ = [[SigninFullscreenPromoSceneAgent alloc]
        initWithPromosManager:promos_manager_.get()];

    agent_.sceneState = scene_state_;

    profile_state_ = CreateMockProfileState(ProfileInitStage::kFinal);
    profile_state_.profile = profile_.get();
    scene_state_.profileState = profile_state_;

    identity_manager_ = IdentityManagerFactory::GetForProfile(profile_.get());
    account_manager_service_ =
        ChromeAccountManagerServiceFactory::GetForProfile(profile_.get());
  }

  void TearDown() override {
    NSUserDefaults* standardDefaults = [NSUserDefaults standardUserDefaults];
    [standardDefaults removeObjectForKey:kDisplayedSSORecallForMajorVersionKey];
    [standardDefaults removeObjectForKey:kLastShownAccountGaiaIdVersionKey];
    [standardDefaults removeObjectForKey:kSigninPromoViewDisplayCountKey];
    [standardDefaults synchronize];
  }

  ProfileState* CreateMockProfileState(ProfileInitStage init_stage) {
    ProfileState* mock_profile_state = OCMClassMock([ProfileState class]);
    OCMStub([mock_profile_state initStage]).andReturn(init_stage);
    OCMStub([mock_profile_state profile]).andReturn(profile_.get());
    return mock_profile_state;
  }

 protected:
  IOSChromeScopedTestingLocalState scoped_testing_local_state_;
  SigninFullscreenPromoSceneAgent* agent_;
  web::WebTaskEnvironment task_environment_;
  StubBrowserProviderInterface* stub_browser_interface_provider_;
  raw_ptr<signin::IdentityManager> identity_manager_;
  raw_ptr<ChromeAccountManagerService> account_manager_service_;
  std::unique_ptr<TestProfileIOS> profile_;
  std::unique_ptr<Browser> browser_;
  ProfileState* profile_state_;
  SceneState* scene_state_;
  std::unique_ptr<MockPromosManager> promos_manager_;
};

// Tests that the sign-in fullscreen promo registers with the promo manager when
// the eligibility criteria are met.
TEST_F(SigninFullscreenPromoSceneAgentTest,
       TestSigninFullscreenPromoRegistration) {
  const base::Version version_1_0("1.0");
  FakeSystemIdentity* fake_identity1 = [FakeSystemIdentity fakeIdentity1];
  FakeSystemIdentityManager::FromSystemIdentityManager(
      GetApplicationContext()->GetSystemIdentityManager())
      ->AddIdentity(fake_identity1);
  signin::RecordUpgradePromoSigninStarted(
      identity_manager_, account_manager_service_, version_1_0);

  EXPECT_CALL(*promos_manager_.get(),
              RegisterPromoForContinuousDisplay(
                  promos_manager::Promo::SigninFullscreen))
      .Times(1);
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;
}

// Tests that the sign-in fullscreen promo is not registered in the promo
// manager when the eligibility criteria are not met.
TEST_F(SigninFullscreenPromoSceneAgentTest,
       TestSigninFullscreenPromoNoRegistration) {
  EXPECT_CALL(*promos_manager_.get(),
              RegisterPromoForContinuousDisplay(
                  promos_manager::Promo::SigninFullscreen))
      .Times(0);
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;
}
