// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ntp/ui_bundled/new_tab_page_mediator.h"

#import <memory>

#import "base/apple/foundation_util.h"
#import "base/memory/raw_ptr.h"
#import "base/metrics/user_metrics.h"
#import "base/metrics/user_metrics_action.h"
#import "components/prefs/ios/pref_observer_bridge.h"
#import "components/prefs/pref_change_registrar.h"
#import "components/regional_capabilities/regional_capabilities_service.h"
#import "components/search/search.h"
#import "components/signin/public/base/signin_switches.h"
#import "components/signin/public/identity_manager/objc/identity_manager_observer_bridge.h"
#import "components/strings/grit/components_strings.h"
#import "ios/chrome/browser/content_suggestions/ui_bundled/content_suggestions_mediator.h"
#import "ios/chrome/browser/content_suggestions/ui_bundled/user_account_image_update_delegate.h"
#import "ios/chrome/browser/discover_feed/model/discover_feed_service.h"
#import "ios/chrome/browser/discover_feed/model/discover_feed_service_factory.h"
#import "ios/chrome/browser/metrics/model/new_tab_page_uma.h"
#import "ios/chrome/browser/ntp/model/new_tab_page_state.h"
#import "ios/chrome/browser/ntp/model/new_tab_page_tab_helper.h"
#import "ios/chrome/browser/ntp/model/new_tab_page_util.h"
#import "ios/chrome/browser/ntp/shared/metrics/feed_metrics_constants.h"
#import "ios/chrome/browser/ntp/shared/metrics/feed_metrics_recorder.h"
#import "ios/chrome/browser/ntp/ui_bundled/feed_control_delegate.h"
#import "ios/chrome/browser/ntp/ui_bundled/feed_wrapper_view_controller.h"
#import "ios/chrome/browser/ntp/ui_bundled/new_tab_page_consumer.h"
#import "ios/chrome/browser/ntp/ui_bundled/new_tab_page_content_delegate.h"
#import "ios/chrome/browser/ntp/ui_bundled/new_tab_page_feature.h"
#import "ios/chrome/browser/ntp/ui_bundled/new_tab_page_header_constants.h"
#import "ios/chrome/browser/ntp/ui_bundled/new_tab_page_header_consumer.h"
#import "ios/chrome/browser/ntp/ui_bundled/new_tab_page_view_controller.h"
#import "ios/chrome/browser/policy/model/policy_util.h"
#import "ios/chrome/browser/search_engines/model/search_engine_observer_bridge.h"
#import "ios/chrome/browser/shared/model/prefs/pref_names.h"
#import "ios/chrome/browser/shared/model/url/chrome_url_constants.h"
#import "ios/chrome/browser/shared/public/features/features.h"
#import "ios/chrome/browser/shared/ui/util/uikit_ui_util.h"
#import "ios/chrome/browser/signin/model/authentication_service.h"
#import "ios/chrome/browser/signin/model/chrome_account_manager_service.h"
#import "ios/chrome/browser/sync/model/sync_observer_bridge.h"
#import "ios/chrome/browser/url_loading/model/url_loading_browser_agent.h"
#import "ios/chrome/browser/url_loading/model/url_loading_params.h"
#import "ios/chrome/common/ui/favicon/favicon_attributes.h"
#import "ios/chrome/grit/ios_strings.h"
#import "ios/public/provider/chrome/browser/voice_search/voice_search_api.h"
#import "ios/web/public/navigation/navigation_item.h"
#import "ios/web/public/navigation/navigation_manager.h"
#import "ios/web/public/navigation/referrer.h"
#import "ios/web/public/web_state.h"
#import "ui/base/l10n/l10n_util.h"
#import "url/gurl.h"

@interface NewTabPageMediator () <IdentityManagerObserverBridgeDelegate,
                                  PrefObserverDelegate,
                                  SearchEngineObserving,
                                  SyncObserverModelBridge>

@property(nonatomic, assign) ChromeAccountManagerService* accountManagerService;
// TemplateURL used to get the search engine.
@property(nonatomic, assign) TemplateURLService* templateURLService;
// Authentication Service to get the current user's avatar.
@property(nonatomic, assign) AuthenticationService* authService;
// This is the object that knows how to update the Identity Disc UI.
@property(nonatomic, weak) id<UserAccountImageUpdateDelegate> imageUpdater;
// Yes if the browser is currently in incognito mode.
@property(nonatomic, assign) BOOL isIncognito;
// DiscoverFeed Service to display the Feed.
@property(nonatomic, assign) DiscoverFeedService* discoverFeedService;

@end

@implementation NewTabPageMediator {
  // Listen for default search engine changes.
  std::unique_ptr<SearchEngineObserverBridge> _searchEngineObserver;
  // Observes changes in identity and updates the Identity Disc.
  std::unique_ptr<signin::IdentityManagerObserverBridge>
      _identityObserverBridge;
  // Used to load URLs.
  raw_ptr<UrlLoadingBrowserAgent> _URLLoader;
  raw_ptr<PrefService> _prefService;
  BOOL _isSafeMode;
  // Pref observer to track changes to prefs.
  std::unique_ptr<PrefObserverBridge> _prefObserverBridge;
  // Registrar for pref changes notifications.
  std::unique_ptr<PrefChangeRegistrar> _prefChangeRegistrar;
  // The current default search engine.
  raw_ptr<const TemplateURL> _defaultSearchEngine;
  // Sync Service.
  raw_ptr<syncer::SyncService> _syncService;
  // Used to check feed configuration based on the country.
  raw_ptr<regional_capabilities::RegionalCapabilitiesService>
      _regionalCapabilitiesService;
  // Observer to keep track of the syncing status.
  std::unique_ptr<SyncObserverBridge> _syncObserver;
  raw_ptr<signin::IdentityManager> _identityManager;
  id<SystemIdentity> _signedInIdentity;
}

// Synthesized from NewTabPageMutator.
@synthesize scrollPositionToSave = _scrollPositionToSave;

- (instancetype)
     initWithTemplateURLService:(TemplateURLService*)templateURLService
                      URLLoader:(UrlLoadingBrowserAgent*)URLLoader
                    authService:(AuthenticationService*)authService
                identityManager:(signin::IdentityManager*)identityManager
          accountManagerService:
              (ChromeAccountManagerService*)accountManagerService
       identityDiscImageUpdater:(id<UserAccountImageUpdateDelegate>)imageUpdater
                    isIncognito:(BOOL)isIncognito
            discoverFeedService:(DiscoverFeedService*)discoverFeedService
                    prefService:(PrefService*)prefService
                    syncService:(syncer::SyncService*)syncService
    regionalCapabilitiesService:
        (regional_capabilities::RegionalCapabilitiesService*)
            regionalCapabilitiesService
                     isSafeMode:(BOOL)isSafeMode {
  self = [super init];
  if (self) {
    CHECK(identityManager);
    CHECK(accountManagerService);
    _templateURLService = templateURLService;
    _defaultSearchEngine = templateURLService->GetDefaultSearchProvider();
    _URLLoader = URLLoader;
    _authService = authService;
    _accountManagerService = accountManagerService;
    _identityManager = identityManager;
    _identityObserverBridge =
        std::make_unique<signin::IdentityManagerObserverBridge>(identityManager,
                                                                self);
    // Listen for default search engine changes.
    _searchEngineObserver = std::make_unique<SearchEngineObserverBridge>(
        self, self.templateURLService);
    _syncService = syncService;
    _syncObserver = std::make_unique<SyncObserverBridge>(self, syncService);
    _imageUpdater = imageUpdater;
    _isIncognito = isIncognito;
    _discoverFeedService = discoverFeedService;
    _prefService = prefService;
    _regionalCapabilitiesService = regionalCapabilitiesService;
    _isSafeMode = isSafeMode;
    _signedInIdentity =
        _authService->GetPrimaryIdentity(signin::ConsentLevel::kSignin);
  }
  return self;
}

- (void)setUp {
  _feedHeaderVisible = [self updatedFeedHeaderVisible];
  self.templateURLService->Load();
  [self updateModuleVisibilityForConsumer];
  [self.headerConsumer setLogoIsShowing:search::DefaultSearchProviderIsGoogle(
                                            self.templateURLService)];
  [self.headerConsumer
      setVoiceSearchIsEnabled:ios::provider::IsVoiceSearchEnabled()];
  [self updateAccountImage];
  [self updateAccountErrorBadge];
  [self startObservingPrefs];
}

- (void)shutdown {
  _searchEngineObserver.reset();
  _identityObserverBridge.reset();
  self.accountManagerService = nil;
  self.discoverFeedService = nullptr;
  _prefChangeRegistrar.reset();
  _prefObserverBridge.reset();
  _prefService = nullptr;
  _syncObserver.reset();
  _syncService = nullptr;
  _regionalCapabilitiesService = nullptr;
  _identityManager = nullptr;
  self.feedControlDelegate = nil;
}

- (void)saveNTPStateForWebState:(web::WebState*)webState {
  NewTabPageState* NTPState = [[NewTabPageState alloc]
      initWithScrollPosition:self.scrollPositionToSave
                selectedFeed:[self.feedControlDelegate selectedFeed]
       followingFeedSortType:[self.feedControlDelegate followingFeedSortType]];
  self.feedMetricsRecorder.NTPState = NTPState;
  NewTabPageTabHelper::FromWebState(webState)->SetNTPState(NTPState);
}

- (void)restoreNTPStateForWebState:(web::WebState*)webState {
  NewTabPageState* NTPState =
      NewTabPageTabHelper::FromWebState(webState)->GetNTPState();
  self.feedMetricsRecorder.NTPState = NTPState;
  if ([self.feedControlDelegate isFollowingFeedAvailable]) {
    [self.NTPContentDelegate updateForSelectedFeed:NTPState.selectedFeed];
  }

  if (NTPState.shouldScrollToTopOfFeed) {
    [self.consumer restoreScrollPositionToTopOfFeed];
    // Prevent next NTP from being scrolled to the top of feed.
    NTPState.shouldScrollToTopOfFeed = NO;
    NewTabPageTabHelper::FromWebState(webState)->SetNTPState(NTPState);
  } else {
    [self.consumer restoreScrollPosition:NTPState.scrollPosition];
  }
}

#pragma mark - SearchEngineObserving

- (void)searchEngineChanged {
  const TemplateURL* updatedDefaultSearchEngine =
      self.templateURLService->GetDefaultSearchProvider();
  if (_defaultSearchEngine == updatedDefaultSearchEngine) {
    return;
  }
  _defaultSearchEngine = updatedDefaultSearchEngine;
  [self.headerConsumer setLogoIsShowing:search::DefaultSearchProviderIsGoogle(
                                            self.templateURLService)];
  [self setFeedHeaderVisible:[self updatedFeedHeaderVisible]];
  [self.feedControlDelegate updateFeedForDefaultSearchEngineChanged];
}

#pragma mark - IdentityManagerObserverBridgeDelegate

- (void)onEndBatchOfPrimaryAccountChanges {
  _signedInIdentity =
      self.authService->GetPrimaryIdentity(signin::ConsentLevel::kSignin);
  [self updateAccountImage];
  [self updateAccountErrorBadge];
}

- (void)onExtendedAccountInfoUpdated:(const AccountInfo&)info {
  if (info.gaia != GaiaId(_signedInIdentity.gaiaID)) {
    return;
  }
  [self updateAccountImage];
  [self updateAccountErrorBadge];
}

#pragma mark - PrefObserverDelegate

- (void)onPreferenceChanged:(const std::string&)preferenceName {
  [self setFeedHeaderVisible:[self updatedFeedHeaderVisible]];

  // Handle customization prefs
  if (preferenceName == prefs::kHomeCustomizationMostVisitedEnabled ||
      preferenceName == prefs::kHomeCustomizationMagicStackEnabled ||
      preferenceName == prefs::kArticlesForYouEnabled) {
    [self updateModuleVisibilityForConsumer];
    [self.NTPContentDelegate updateModuleVisibility];
  }
}

#pragma mark - SyncObserverModelBridge

- (void)onSyncStateChanged {
  [self updateAccountErrorBadge];
}

#pragma mark - Private

// Fetches and update user's avatar on NTP, or use default avatar if user is
// not signed in.
- (void)updateAccountImage {
  // Fetches user's identity from Authentication Service.
  if (_signedInIdentity) {
    // Only show an avatar if the user is signed in.
    UIImage* image = self.accountManagerService->GetIdentityAvatarWithIdentity(
        _signedInIdentity, IdentityAvatarSize::SmallSize);
    [self.imageUpdater updateAccountImage:image
                                     name:_signedInIdentity.userFullName
                                    email:_signedInIdentity.userEmail];
  } else {
    [self.imageUpdater setSignedOutAccountImage];
    signin_metrics::LogSignInOffered(
        signin_metrics::AccessPoint::kNtpSignedOutIcon,
        signin_metrics::PromoAction::PROMO_ACTION_NO_SIGNIN_PROMO);
  }
}

// Opens web page for a menu item in the NTP.
- (void)openMenuItemWebPage:(GURL)URL {
  _URLLoader->Load(UrlLoadParams::InCurrentTab(URL));
  // TODO(crbug.com/40693626): Add metrics.
}

// Returns an updated value for feedHeaderVisible.
- (BOOL)updatedFeedHeaderVisible {
  return _prefService->GetBoolean(prefs::kArticlesForYouEnabled) &&
         _prefService->GetBoolean(prefs::kNTPContentSuggestionsEnabled) &&
         !IsFeedAblationEnabled() &&
         IsContentSuggestionsForSupervisedUserEnabled(_prefService) &&
         !_isSafeMode &&
         !ShouldHideFeedWithSearchChoice(self.templateURLService,
                                         _regionalCapabilitiesService);
}

// Sets whether the feed header should be visible.
- (void)setFeedHeaderVisible:(BOOL)feedHeaderVisible {
  if (feedHeaderVisible == _feedHeaderVisible) {
    return;
  }

  _feedHeaderVisible = feedHeaderVisible;
  [self.feedControlDelegate setFeedAndHeaderVisibility:_feedHeaderVisible];
}

// Updates the consumer with the current visibility of the NTP modules.
- (void)updateModuleVisibilityForConsumer {
  self.consumer.mostVisitedVisible =
      _prefService->GetBoolean(prefs::kHomeCustomizationMostVisitedEnabled);
  self.consumer.magicStackVisible =
      _prefService->GetBoolean(prefs::kHomeCustomizationMagicStackEnabled);
}

// Starts observing some prefs.
- (void)startObservingPrefs {
  _prefChangeRegistrar = std::make_unique<PrefChangeRegistrar>();
  _prefChangeRegistrar->Init(_prefService);
  _prefObserverBridge = std::make_unique<PrefObserverBridge>(self);

  // Observe feed visibility prefs.
  _prefObserverBridge->ObserveChangesForPreference(
      prefs::kArticlesForYouEnabled, _prefChangeRegistrar.get());
  _prefObserverBridge->ObserveChangesForPreference(
      prefs::kNTPContentSuggestionsEnabled, _prefChangeRegistrar.get());
  _prefObserverBridge->ObserveChangesForPreference(
      prefs::kNTPContentSuggestionsForSupervisedUserEnabled,
      _prefChangeRegistrar.get());

  // Observe customization prefs.
  _prefObserverBridge->ObserveChangesForPreference(
      prefs::kHomeCustomizationMostVisitedEnabled, _prefChangeRegistrar.get());
  _prefObserverBridge->ObserveChangesForPreference(
      prefs::kHomeCustomizationMagicStackEnabled, _prefChangeRegistrar.get());
}

- (void)updateAccountErrorBadge {
  if (!base::FeatureList::IsEnabled(
          switches::kEnableErrorBadgeOnIdentityDisc)) {
    return;
  }
  BOOL primaryIdentityHasError =
      _signedInIdentity && _syncService->GetUserActionableError() !=
                               syncer::SyncService::UserActionableError::kNone;
  [self.headerConsumer
      updateADPBadgeWithErrorFound:primaryIdentityHasError
                              name:_signedInIdentity.userFullName
                             email:_signedInIdentity.userEmail];
}

@end
