// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_SETTINGS_UI_BUNDLED_BANDWIDTH_BANDWIDTH_MANAGEMENT_TABLE_VIEW_CONTROLLER_H_
#define IOS_CHROME_BROWSER_SETTINGS_UI_BUNDLED_BANDWIDTH_BANDWIDTH_MANAGEMENT_TABLE_VIEW_CONTROLLER_H_

#import "ios/chrome/browser/settings/ui_bundled/settings_controller_protocol.h"
#import "ios/chrome/browser/settings/ui_bundled/settings_root_table_view_controller.h"

class ProfileIOS;

// Controller for the UI that allows the user to change settings that affect
// bandwidth usage: prefetching and the data reduction proxy.
@interface BandwidthManagementTableViewController
    : SettingsRootTableViewController <SettingsControllerProtocol>

// The designated initializer. `profile` must not be nil.
- (instancetype)initWithProfile:(ProfileIOS*)profile NS_DESIGNATED_INITIALIZER;
- (instancetype)initWithStyle:(UITableViewStyle)style NS_UNAVAILABLE;
@end

#endif  // IOS_CHROME_BROWSER_SETTINGS_UI_BUNDLED_BANDWIDTH_BANDWIDTH_MANAGEMENT_TABLE_VIEW_CONTROLLER_H_
