// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/credential_provider_promo/ui_bundled/credential_provider_promo_mediator.h"

#import "base/strings/sys_string_conversions.h"
#import "base/test/with_feature_override.h"
#import "components/password_manager/core/common/password_manager_pref_names.h"
#import "components/prefs/pref_registry_simple.h"
#import "components/prefs/testing_pref_service.h"
#import "ios/chrome/browser/credential_provider_promo/ui_bundled/credential_provider_promo_constants.h"
#import "ios/chrome/browser/credential_provider_promo/ui_bundled/credential_provider_promo_consumer.h"
#import "ios/chrome/browser/promos_manager/model/mock_promos_manager.h"
#import "ios/chrome/browser/shared/model/application_context/application_context.h"
#import "ios/chrome/browser/shared/model/prefs/pref_names.h"
#import "ios/chrome/browser/shared/public/commands/credential_provider_promo_commands.h"
#import "ios/chrome/browser/shared/public/features/features.h"
#import "ios/chrome/grit/ios_branded_strings.h"
#import "ios/chrome/grit/ios_strings.h"
#import "ios/chrome/test/ios_chrome_scoped_testing_local_state.h"
#import "ios/public/provider/chrome/browser/branded_images/branded_images_api.h"
#import "testing/platform_test.h"
#import "third_party/ocmock/OCMock/OCMock.h"
#import "third_party/ocmock/gtest_support.h"
#import "ui/base/l10n/l10n_util.h"

namespace {

NSString* const kFirstStepAnimation = @"CPE_promo_animation_edu_autofill";
NSString* const kLearnMoreAnimation = @"CPE_promo_animation_edu_how_to_enable";
UIImage* kImage = ios::provider::GetBrandedImage(
    ios::provider::BrandedImage::kPasswordSuggestionKey);

struct PromoStrings {
  NSString* title_string;
  NSString* subtitle_string;
  NSString* primary_action_string;
  NSString* secondary_action_string;
  NSString* tertiary_action_string;
};

// Returns the expected set of strings for the `kFirstStep` promo context.
PromoStrings ExpectedFirstStepPromoStrings() {
  NSString* title_string;
  NSString* subtitle_string;
  NSString* primary_action_string;
  NSString* secondary_action_string;
  NSString* tertiary_action_string;

  if (IOSPasskeysM2Enabled()) {
    if (@available(iOS 18.0, *)) {
      title_string =
          l10n_util::GetNSString(IDS_IOS_CREDENTIAL_PROVIDER_PROMO_TITLE_IOS18);
      primary_action_string = l10n_util::GetNSString(
          IDS_IOS_CREDENTIAL_PROVIDER_SETTINGS_TURN_ON_AUTOFILL);
    } else {
      title_string =
          l10n_util::GetNSString(IDS_IOS_CREDENTIAL_PROVIDER_PROMO_TITLE);
      subtitle_string =
          l10n_util::GetNSString(IDS_IOS_CREDENTIAL_PROVIDER_PROMO_SUBTITLE);
      primary_action_string =
          l10n_util::GetNSString(IDS_IOS_CREDENTIAL_PROVIDER_PROMO_LEARN_HOW);
    }
  } else {
    title_string =
        l10n_util::GetNSString(IDS_IOS_CREDENTIAL_PROVIDER_PROMO_INITIAL_TITLE);
    subtitle_string = l10n_util::GetNSString(
        IDS_IOS_CREDENTIAL_PROVIDER_PROMO_INITIAL_SUBTITLE);
    primary_action_string =
        l10n_util::GetNSString(IDS_IOS_CREDENTIAL_PROVIDER_PROMO_LEARN_HOW);
  }

  secondary_action_string =
      l10n_util::GetNSString(IDS_IOS_CREDENTIAL_PROVIDER_PROMO_NO_THANKS);
  tertiary_action_string =
      l10n_util::GetNSString(IDS_IOS_CREDENTIAL_PROVIDER_PROMO_REMIND_ME_LATER);

  return {title_string, subtitle_string, primary_action_string,
          secondary_action_string, tertiary_action_string};
}

// Returns the expected set of strings for the `kLearnMore` promo context.
PromoStrings ExpectedLearnMorePromoStrings() {
  NSString* title_string;
  NSString* subtitle_string;
  NSString* primary_action_string;
  NSString* secondary_action_string;
  NSString* tertiary_action_string;

  title_string = l10n_util::GetNSString(
      IDS_IOS_CREDENTIAL_PROVIDER_PROMO_LEARN_MORE_TITLE);

  NSString* ios_settings_title = l10n_util::GetNSString(
      IDS_IOS_CREDENTIAL_PROVIDER_PROMO_OS_PASSWORDS_SETTINGS_TITLE_IOS16);
  subtitle_string = l10n_util::GetNSStringF(
      IOSPasskeysM2Enabled()
          ? IDS_IOS_CREDENTIAL_PROVIDER_PROMO_INSTRUCTIONS_SUBTITLE
          : IDS_IOS_CREDENTIAL_PROVIDER_PROMO_LEARN_MORE_SUBTITLE_WITH_PH,
      base::SysNSStringToUTF16(ios_settings_title));

  primary_action_string =
      l10n_util::GetNSString(IDS_IOS_CREDENTIAL_PROVIDER_PROMO_GO_TO_SETTINGS);
  secondary_action_string =
      l10n_util::GetNSString(IDS_IOS_CREDENTIAL_PROVIDER_PROMO_NO_THANKS);
  tertiary_action_string =
      l10n_util::GetNSString(IDS_IOS_CREDENTIAL_PROVIDER_PROMO_REMIND_ME_LATER);

  return {title_string, subtitle_string, primary_action_string,
          secondary_action_string, tertiary_action_string};
}

}  // namespace

// Test fixture for testing the CredentialProviderPromoMediator class.
class CredentialProviderPromoMediatorTest
    : public PlatformTest,
      public base::test::WithFeatureOverride {
 protected:
  CredentialProviderPromoMediatorTest()
      : base::test::WithFeatureOverride(kIOSPasskeysM2) {
    CreateCredentialProviderPromoMediator();
  }

  PrefService* local_state() {
    return GetApplicationContext()->GetLocalState();
  }

 protected:
  IOSChromeScopedTestingLocalState scoped_testing_local_state_;
  std::unique_ptr<MockPromosManager> promos_manager_;
  CredentialProviderPromoMediator* mediator_;
  id consumer_;

  void CreateCredentialProviderPromoMediator() {
    promos_manager_ = std::make_unique<MockPromosManager>();
    consumer_ = OCMProtocolMock(@protocol(CredentialProviderPromoConsumer));
    mediator_ = [[CredentialProviderPromoMediator alloc]
        initWithPromosManager:promos_manager_.get()];
    mediator_.consumer = consumer_;
  }

  void ExpectConsumerSetFieldsForFirstStep() {
    PromoStrings promo_strings = ExpectedFirstStepPromoStrings();
    OCMExpect([consumer_ setTitleString:promo_strings.title_string
                         subtitleString:promo_strings.subtitle_string
                    primaryActionString:promo_strings.primary_action_string
                  secondaryActionString:promo_strings.secondary_action_string
                   tertiaryActionString:promo_strings.tertiary_action_string
                                  image:nil]);
    OCMExpect([consumer_ setAnimation:kFirstStepAnimation]);
  }

  void ExpectConsumerSetFieldsForFirstStepNoAnimation() {
    PromoStrings promo_strings = ExpectedFirstStepPromoStrings();
    OCMExpect([consumer_ setTitleString:promo_strings.title_string
                         subtitleString:promo_strings.subtitle_string
                    primaryActionString:promo_strings.primary_action_string
                  secondaryActionString:promo_strings.secondary_action_string
                   tertiaryActionString:promo_strings.tertiary_action_string
                                  image:kImage]);
  }

  void ExpectConsumerSetFieldsForLearnMore() {
    PromoStrings promo_strings = ExpectedLearnMorePromoStrings();
    OCMExpect([consumer_ setTitleString:promo_strings.title_string
                         subtitleString:promo_strings.subtitle_string
                    primaryActionString:promo_strings.primary_action_string
                  secondaryActionString:promo_strings.secondary_action_string
                   tertiaryActionString:promo_strings.tertiary_action_string
                                  image:nil]);
    OCMExpect([consumer_ setAnimation:kLearnMoreAnimation]);
  }
};

#pragma mark - Tests

// Tests that promo is NOT displayed when the user has already enabled the
// Credential Provider Extension.
TEST_P(CredentialProviderPromoMediatorTest,
       CredentialProviderExtensionEnabled) {
  local_state()->SetBoolean(
      password_manager::prefs::kCredentialProviderEnabledOnStartup, true);

  EXPECT_FALSE([mediator_
      canShowCredentialProviderPromoWithTrigger:
          CredentialProviderPromoTrigger::SuccessfulLoginUsingExistingPassword
                                      promoSeen:NO]);
}

// Tests that promo will NOT be displayed when the promo has already been
// displayed in the current app session.
TEST_P(CredentialProviderPromoMediatorTest,
       CredentialProviderPromoAlreadySeen) {
  EXPECT_FALSE([mediator_
      canShowCredentialProviderPromoWithTrigger:
          CredentialProviderPromoTrigger::SuccessfulLoginUsingExistingPassword
                                      promoSeen:YES]);
}

// Tests that promo will NOT be displayed when the user has previously seen the
// promo and selected "No Thanks".
TEST_P(CredentialProviderPromoMediatorTest,
       CredentialProviderPromoNoThanksSelected) {
  local_state()->SetBoolean(prefs::kIosCredentialProviderPromoStopPromo, true);

  EXPECT_FALSE([mediator_
      canShowCredentialProviderPromoWithTrigger:
          CredentialProviderPromoTrigger::SuccessfulLoginUsingExistingPassword
                                      promoSeen:NO]);
}

// Tests that the promo will be displayed when all the trigger requirements are
// met.
TEST_P(CredentialProviderPromoMediatorTest,
       CredentialProviderPromoRequirementsMet) {
  EXPECT_TRUE([mediator_
      canShowCredentialProviderPromoWithTrigger:
          CredentialProviderPromoTrigger::SuccessfulLoginUsingExistingPassword
                                      promoSeen:NO]);
}

// Tests that the promo will always be displayed when the trigger is SetUpList.
TEST_P(CredentialProviderPromoMediatorTest,
       CredentialProviderPromoSetUpListTrigger) {
  local_state()->SetBoolean(prefs::kIosCredentialProviderPromoStopPromo, true);
  EXPECT_TRUE([mediator_ canShowCredentialProviderPromoWithTrigger:
                             CredentialProviderPromoTrigger::SetUpList
                                                         promoSeen:YES]);
}

// Tests that the consumer content is correctly set when the promo:
// - Is a “First Step” promo
// - Was triggered by the user successfully logging in using an existing
// password
TEST_P(CredentialProviderPromoMediatorTest,
       ConsumerContent_FirstStep_SuccessfulLoginUsingExistingPassword) {
  ExpectConsumerSetFieldsForFirstStepNoAnimation();

  [mediator_
      configureConsumerWithTrigger:CredentialProviderPromoTrigger::
                                       SuccessfulLoginUsingExistingPassword
                           context:CredentialProviderPromoContext::kFirstStep];

  EXPECT_OCMOCK_VERIFY(consumer_);
}

// Tests that the consumer content is correctly set when the user selects
// “Remind Me Later” AFTER seeing a promo that:
//  - Was a “First Step” promo
//  - Was triggered by the user successfully logging in using an existing
//  password
TEST_P(
    CredentialProviderPromoMediatorTest,
    ConsumerContent_FirstStep_RemindMeLater_AfterFirstStepSuccessfulLoginUsingExistingPassword) {
  // Need to check for this method call twice: once for the promo
  // triggered by successful login and once for the promo triggered by selecting
  // "Remind Me Later".
  ExpectConsumerSetFieldsForFirstStepNoAnimation();
  ExpectConsumerSetFieldsForFirstStepNoAnimation();

  [mediator_
      configureConsumerWithTrigger:CredentialProviderPromoTrigger::
                                       SuccessfulLoginUsingExistingPassword
                           context:CredentialProviderPromoContext::kFirstStep];
  [mediator_
      configureConsumerWithTrigger:CredentialProviderPromoTrigger::RemindMeLater
                           context:CredentialProviderPromoContext::kFirstStep];

  EXPECT_OCMOCK_VERIFY(consumer_);
}

// Tests that the consumer content is correctly set when the promo:
//  - Is a “Learn More” promo
//  - Was triggered by the user successfully logging in using an existing
//  password
TEST_P(CredentialProviderPromoMediatorTest,
       ConsumerContent_LearnMore_SuccessfulLoginUsingExistingPassword) {
  ExpectConsumerSetFieldsForLearnMore();

  [mediator_
      configureConsumerWithTrigger:CredentialProviderPromoTrigger::
                                       SuccessfulLoginUsingExistingPassword
                           context:CredentialProviderPromoContext::kLearnMore];

  EXPECT_OCMOCK_VERIFY(consumer_);
}

// Tests that the consumer content is correctly set when the user selects
// “Remind Me Later” after seeing a promo that:
//  - Was a “Learn More” promo
//  - Was triggered by the user successfully logging in using an existing
//  password
TEST_P(
    CredentialProviderPromoMediatorTest,
    ConsumerContent_LearnMore_RemindMeLater_AfterSuccessfulLoginUsingExistingPassword) {
  // Need to check for each of these method calls twice: once for the promo
  // triggered by successful login and once for the promo triggered by selecting
  // "Remind Me Later".
  ExpectConsumerSetFieldsForLearnMore();
  ExpectConsumerSetFieldsForLearnMore();

  [mediator_
      configureConsumerWithTrigger:CredentialProviderPromoTrigger::
                                       SuccessfulLoginUsingExistingPassword
                           context:CredentialProviderPromoContext::kLearnMore];
  [mediator_
      configureConsumerWithTrigger:CredentialProviderPromoTrigger::RemindMeLater
                           context:CredentialProviderPromoContext::kLearnMore];

  EXPECT_OCMOCK_VERIFY(consumer_);
}

INSTANTIATE_FEATURE_OVERRIDE_TEST_SUITE(CredentialProviderPromoMediatorTest);
