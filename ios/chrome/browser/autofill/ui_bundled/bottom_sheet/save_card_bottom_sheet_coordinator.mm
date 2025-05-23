// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/autofill/ui_bundled/bottom_sheet/save_card_bottom_sheet_coordinator.h"

#import <memory>
#import <utility>

#import "ios/chrome/browser/autofill/model/bottom_sheet/autofill_bottom_sheet_tab_helper.h"
#import "ios/chrome/browser/autofill/model/bottom_sheet/save_card_bottom_sheet_model.h"
#import "ios/chrome/browser/autofill/ui_bundled/bottom_sheet/save_card_bottom_sheet_mediator.h"
#import "ios/chrome/browser/autofill/ui_bundled/bottom_sheet/save_card_bottom_sheet_view_controller.h"
#import "ios/chrome/browser/shared/model/browser/browser.h"
#import "ios/chrome/browser/shared/model/web_state_list/web_state_list.h"
#import "ios/chrome/browser/shared/public/commands/autofill_commands.h"
#import "ios/chrome/browser/shared/public/commands/command_dispatcher.h"

// TODO(crbug.com/391366601): Implement SaveCardBottomSheetCoordinator.
@implementation SaveCardBottomSheetCoordinator {
  // The model providing resources and callbacks for save card bottomsheet.
  std::unique_ptr<autofill::SaveCardBottomSheetModel> _saveCardBottomSheetModel;

  // The mediator for save card bottomsheet created and owned by the
  // coordinator.
  SaveCardBottomSheetMediator* _mediator;

  // The view controller to display save card bottomsheet.
  SaveCardBottomSheetViewController* _viewController;
}

- (instancetype)initWithBaseViewController:(UIViewController*)baseViewController
                                   browser:(Browser*)browser {
  self = [super initWithBaseViewController:baseViewController browser:browser];
  if (self) {
    AutofillBottomSheetTabHelper* tabHelper =
        AutofillBottomSheetTabHelper::FromWebState(
            browser->GetWebStateList()->GetActiveWebState());
    _saveCardBottomSheetModel = tabHelper->GetSaveCardBottomSheetModel();
    CHECK(_saveCardBottomSheetModel);
  }
  return self;
}

#pragma mark - ChromeCoordinator

- (void)start {
  _mediator = [[SaveCardBottomSheetMediator alloc]
              initWithUIModel:std::move(_saveCardBottomSheetModel)
      autofillCommandsHandler:HandlerForProtocol(
                                  self.browser->GetCommandDispatcher(),
                                  AutofillCommands)];
  _viewController = [[SaveCardBottomSheetViewController alloc] init];
  _viewController.mutator = _mediator;
  _mediator.consumer = _viewController;
  [self.baseViewController presentViewController:_viewController
                                        animated:YES
                                      completion:nil];
}

- (void)stop {
  [_viewController dismissViewControllerAnimated:YES completion:nil];
  _viewController = nil;
  [_mediator disconnect];
  _mediator.consumer = nil;
  _mediator = nil;
}

@end
