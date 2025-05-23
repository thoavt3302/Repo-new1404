// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/content_suggestions/ui_bundled/safety_check/safety_check_prefs.h"

#import "components/prefs/pref_registry_simple.h"
#import "components/prefs/pref_service.h"
#import "ios/chrome/browser/shared/model/prefs/pref_names.h"
#import "ios/chrome/browser/shared/public/features/features.h"

namespace safety_check_prefs {

const char kSafetyCheckInMagicStackDisabledPref[] =
    "safety_check_magic_stack.disabled";

void RegisterPrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kSafetyCheckInMagicStackDisabledPref, false);
}

bool IsSafetyCheckInMagicStackDisabled(PrefService* prefs) {
  return !prefs->GetBoolean(
      prefs::kHomeCustomizationMagicStackSafetyCheckEnabled);
}

void DisableSafetyCheckInMagicStack(PrefService* prefs) {
  prefs->SetBoolean(prefs::kHomeCustomizationMagicStackSafetyCheckEnabled,
                    false);
}

}  // namespace safety_check_prefs
