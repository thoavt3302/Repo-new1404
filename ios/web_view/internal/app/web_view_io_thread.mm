// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/web_view/internal/app/web_view_io_thread.h"

#import "net/base/network_delegate_impl.h"

namespace ios_web_view {

WebViewIOThread::WebViewIOThread(PrefService* local_state, net::NetLog* net_log)
    : IOSIOThread(local_state, net_log) {}

WebViewIOThread::~WebViewIOThread() = default;

std::unique_ptr<net::NetworkDelegate>
WebViewIOThread::CreateSystemNetworkDelegate() {
  return std::make_unique<net::NetworkDelegateImpl>();
}

std::string WebViewIOThread::GetChannelString() const {
  return std::string();
}

}  // namespace ios_web_view
