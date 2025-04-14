// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_AUTHENTICATION_UI_BUNDLED_AUTHENTICATION_FLOW_AUTHENTICATION_FLOW_PERFORMER_H_
#define IOS_CHROME_BROWSER_AUTHENTICATION_UI_BUNDLED_AUTHENTICATION_FLOW_AUTHENTICATION_FLOW_PERFORMER_H_

#import <UIKit/UIKit.h>

#import "base/functional/callback_forward.h"
#import "base/ios/block_types.h"
#import "components/signin/public/base/signin_metrics.h"
#import "ios/chrome/browser/authentication/ui_bundled/authentication_flow/authentication_flow_performer_delegate.h"
#import "ios/chrome/browser/authentication/ui_bundled/signin/signin_constants.h"

class Browser;
@protocol ChangeProfileCommands;
class ProfileIOS;
@class SceneState;
enum class SignedInUserState;
@protocol SystemIdentity;

namespace syncer {
class SyncService;
}  // namespace syncer

// Callback called the profile switching succeeded (`success` is true) or failed
// (`success` is false).
// If `success is true:
// `browser` is the browser of the new profile.
using OnProfileSwitchCompletion =
    base::OnceCallback<void(bool success, Browser* new_profile_browser)>;

// Performs the sign-in steps and user interactions as part of the sign-in flow.
@interface AuthenticationFlowPerformer : NSObject

// Initializes a new AuthenticationFlowPerformer. `delegate` will be notified
// when each step completes.
- (instancetype)initWithDelegate:
                    (id<AuthenticationFlowPerformerDelegate>)delegate
            changeProfileHandler:(id<ChangeProfileCommands>)changeProfileHandler
    NS_DESIGNATED_INITIALIZER;

- (instancetype)init NS_UNAVAILABLE;

// Cancels any outstanding work and dismisses an alert view (if shown).
- (void)interrupt;

// Fetches the list of data types with unsync data in the primary account.
// `-[id<AuthenticationFlowPerformerDelegate>
// didFetchUnsyncedDataWithUnsyncedDataTypes:]` is called once the data are
// fetched.
- (void)fetchUnsyncedDataWithSyncService:(syncer::SyncService*)syncService;

// Shows confirmation dialog to leaving the primary account. This dialog
// is used for account switching or profile switching.
// `baseViewController` is used to display the confirmation diolog.
// `anchorView` and `anchorRect` is the position that triggered sign-in. It is
// used to attach the popover dialog with a regular window size (like iPad).
// `-[id<AuthenticationFlowPerformerDelegate>
// didAcceptToLeavePrimaryAccount:]` is called once the user accepts or
// refuses the confirmation dialog.
- (void)showLeavingPrimaryAccountConfirmationWithBaseViewController:
            (UIViewController*)baseViewController
                                                            browser:(Browser*)
                                                                        browser
                                                  signedInUserState:
                                                      (SignedInUserState)
                                                          signedInUserState
                                                         anchorView:
                                                             (UIView*)anchorView
                                                         anchorRect:
                                                             (CGRect)anchorRect;

// Fetches the managed status for `identity`.
- (void)fetchManagedStatus:(ProfileIOS*)profile
               forIdentity:(id<SystemIdentity>)identity;

// Fetches the profile separation policies for the account linked to `identity`.
- (void)fetchProfileSeparationPolicies:(ProfileIOS*)profile
                           forIdentity:(id<SystemIdentity>)identity;

// Signs `identity` with `currentProfile`.
- (void)signInIdentity:(id<SystemIdentity>)identity
         atAccessPoint:(signin_metrics::AccessPoint)accessPoint
        currentProfile:(ProfileIOS*)currentProfile;

// Switches to the profile that `identity` is assigned, for `sceneIdentifier`.
- (void)switchToProfileWithIdentity:(id<SystemIdentity>)identity
                         sceneState:(SceneState*)sceneState;

// Switches to the profile with `profileName`, for `sceneIdentifier`.
- (void)switchToProfileWithName:(const std::string&)profileName
                     sceneState:(SceneState*)sceneState;

// Converts the personal profile to a managed one and attaches `identity` to it.
- (void)makePersonalProfileManagedWithIdentity:(id<SystemIdentity>)identity;

// Signs out of `profile` and sends `didSignOutForAccountSwitch` to the delegate
// when complete.
- (void)signOutForAccountSwitchWithProfile:(ProfileIOS*)profile;

// Immediately signs out `profile` without waiting for dependent services.
- (void)signOutImmediatelyFromProfile:(ProfileIOS*)profile;

// Shows a confirmation dialog for signing in to an account managed by
// `hostedDomain`. The confirmation dialog's content will be different depending
// on the status of User Policy.
- (void)showManagedConfirmationForHostedDomain:(NSString*)hostedDomain
                                      identity:(id<SystemIdentity>)identity
                                viewController:(UIViewController*)viewController
                                       browser:(Browser*)browser
                     skipBrowsingDataMigration:(BOOL)skipBrowsingDataMigration
                    mergeBrowsingDataByDefault:(BOOL)mergeBrowsingDataByDefault
         browsingDataMigrationDisabledByPolicy:
             (BOOL)browsingDataMigrationDisabledByPolicy;

// Completes the post-signin actions. In most cases the action is showing a
// snackbar confirming sign-in with `identity` and an undo button to sign out
// the user.
- (void)completePostSignInActions:(PostSignInActionSet)postSignInActions
                     withIdentity:(id<SystemIdentity>)identity
                          browser:(Browser*)browser
                      accessPoint:(signin_metrics::AccessPoint)accessPoint;

// Shows `error` to the user and calls `callback` on dismiss.
- (void)showAuthenticationError:(NSError*)error
                 withCompletion:(ProceduralBlock)callback
                 viewController:(UIViewController*)viewController
                        browser:(Browser*)browser;

- (void)registerUserPolicy:(ProfileIOS*)profile
               forIdentity:(id<SystemIdentity>)identity;

- (void)fetchUserPolicy:(ProfileIOS*)profile
            withDmToken:(NSString*)dmToken
               clientID:(NSString*)clientID
     userAffiliationIDs:(NSArray<NSString*>*)userAffiliationIDs
               identity:(id<SystemIdentity>)identity;

@property(nonatomic, weak, readonly) id<AuthenticationFlowPerformerDelegate>
    delegate;

@end

#endif  // IOS_CHROME_BROWSER_AUTHENTICATION_UI_BUNDLED_AUTHENTICATION_FLOW_AUTHENTICATION_FLOW_PERFORMER_H_
