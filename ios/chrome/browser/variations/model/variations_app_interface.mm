// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/variations/model/variations_app_interface.h"

#import <string>

#import "base/metrics/field_trial.h"
#import "components/prefs/pref_service.h"
#import "components/variations/pref_names.h"
#import "components/variations/service/variations_service.h"
#import "components/variations/variations_test_utils.h"
#import "ios/chrome/browser/shared/model/application_context/application_context.h"
#import "ios/chrome/test/app/chrome_test_util.h"

@implementation VariationsAppInterface

+ (void)clearVariationsPrefs {
  PrefService* prefService = GetApplicationContext()->GetLocalState();

  // Clear variations seed prefs.
  GetApplicationContext()
      ->GetVariationsService()
      ->GetSeedStoreForTesting()
      ->GetSeedReaderWriterForTesting()
      ->ClearSeed();
  prefService->ClearPref(variations::prefs::kVariationsCountry);
  prefService->ClearPref(variations::prefs::kVariationsLastFetchTime);
  prefService->ClearPref(
      variations::prefs::kVariationsPermanentConsistencyCountry);
  prefService->ClearPref(
      variations::prefs::kVariationsPermanentOverriddenCountry);
  prefService->ClearPref(variations::prefs::kVariationsSeedDate);
  prefService->ClearPref(variations::prefs::kVariationsSeedSignature);

  // Clear variations safe seed prefs.
  GetApplicationContext()
      ->GetVariationsService()
      ->GetSeedStoreForTesting()
      ->GetSafeSeedReaderWriterForTesting()
      ->ClearSeed();
  prefService->ClearPref(variations::prefs::kVariationsSafeSeedDate);
  prefService->ClearPref(variations::prefs::kVariationsSafeSeedFetchTime);
  prefService->ClearPref(variations::prefs::kVariationsSafeSeedLocale);
  prefService->ClearPref(
      variations::prefs::kVariationsSafeSeedPermanentConsistencyCountry);
  prefService->ClearPref(
      variations::prefs::kVariationsSafeSeedSessionConsistencyCountry);
  prefService->ClearPref(variations::prefs::kVariationsSafeSeedSignature);

  // Clear variations policy prefs.
  prefService->ClearPref(variations::prefs::kVariationsRestrictionsByPolicy);
  prefService->ClearPref(variations::prefs::kVariationsRestrictParameter);

  // Clear prefs that may trigger variations safe mode.
  prefService->ClearPref(variations::prefs::kVariationsCrashStreak);
  prefService->ClearPref(variations::prefs::kVariationsFailedToFetchSeedStreak);
}

+ (BOOL)fieldTrialExistsForTestSeed {
  return variations::FieldTrialListHasAllStudiesFrom(variations::kTestSeedData);
}

+ (BOOL)hasSafeSeed {
  return !GetApplicationContext()
              ->GetVariationsService()
              ->GetSeedStoreForTesting()
              ->GetSafeSeedReaderWriterForTesting()
              ->GetSeedData()
              .data.empty();
}

+ (void)setTestSafeSeedAndSignature {
  GetApplicationContext()->GetLocalState()->SetString(
      variations::prefs::kVariationsSafeSeedSignature,
      variations::kTestSeedData.base64_signature);
  GetApplicationContext()
      ->GetVariationsService()
      ->GetSeedStoreForTesting()
      ->GetSafeSeedReaderWriterForTesting()
      ->StoreValidatedSeed(variations::kTestSeedData.GetCompressedData(),
                           variations::kTestSeedData.base64_compressed_data);
}

+ (void)setCrashingRegularSeedAndSignature {
  GetApplicationContext()->GetLocalState()->SetString(
      variations::prefs::kVariationsSeedSignature,
      variations::kCrashingSeedData.base64_signature);
  GetApplicationContext()
      ->GetVariationsService()
      ->GetSeedStoreForTesting()
      ->GetSeedReaderWriterForTesting()
      ->StoreValidatedSeed(
          variations::kCrashingSeedData.GetCompressedData(),
          variations::kCrashingSeedData.base64_compressed_data);
}

+ (int)crashStreak {
  PrefService* prefService = GetApplicationContext()->GetLocalState();
  return prefService->GetInteger(variations::prefs::kVariationsCrashStreak);
}

+ (void)setCrashValue:(int)value {
  PrefService* prefService = GetApplicationContext()->GetLocalState();
  prefService->SetInteger(variations::prefs::kVariationsCrashStreak, value);
}

+ (int)failedFetchStreak {
  PrefService* prefService = GetApplicationContext()->GetLocalState();
  return prefService->GetInteger(
      variations::prefs::kVariationsFailedToFetchSeedStreak);
}

+ (void)setFetchFailureValue:(int)value {
  PrefService* prefService = GetApplicationContext()->GetLocalState();
  prefService->SetInteger(variations::prefs::kVariationsFailedToFetchSeedStreak,
                          value);
}

@end
