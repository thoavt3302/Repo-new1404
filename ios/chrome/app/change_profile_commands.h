// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_APP_CHANGE_PROFILE_COMMANDS_H_
#define IOS_CHROME_APP_CHANGE_PROFILE_COMMANDS_H_

#import <string_view>

#import "base/functional/callback_forward.h"
#import "ios/chrome/app/change_profile_continuation.h"

@class SceneState;

// App-level commands related to switching profiles.
@protocol ChangeProfileCommands

// Changes the profile used by the scene with `sceneIdentifier` and invoke
// `completion` when the profile is fully initialised (or as soon as the
// operation fails in case of failure).
//
// This can be called even if the profile named `profileName` has not yet
// been created, the method will take care of creating it, loading it and
// then connecting the scene with the profile.
//
// The method may fail if the feature kSeparateProfilesForManagedAccounts
// is disabled or not available (on iOS < 17), if creating the profile is
// impossible or fails, or if no scene named `sceneIdentifier` exists.
//
// The continuation will be called asynchronously, when the profile has
// been switched for the SceneState.
- (void)changeProfile:(std::string_view)profileName
             forScene:(SceneState*)sceneState
         continuation:(ChangeProfileContinuation)continuation;

// Deletes the profile named `profileName` (the data may be deleted at
// a later time and the profile itself will be unloaded asynchronously).
// All the scenes currently connected to this profile will switch to the
// personal profile (with an animation).
- (void)deleteProfile:(std::string_view)profileName;

@end

#endif  // IOS_CHROME_APP_CHANGE_PROFILE_COMMANDS_H_
