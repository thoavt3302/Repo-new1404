// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_STORAGE_ACCESS_STORAGE_ACCESS_HANDLE_H_
#define CONTENT_BROWSER_STORAGE_ACCESS_STORAGE_ACCESS_HANDLE_H_

#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "components/services/storage/public/cpp/quota_error_or.h"
#include "content/public/browser/document_service.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "third_party/blink/public/mojom/broadcastchannel/broadcast_channel.mojom-forward.h"
#include "third_party/blink/public/mojom/cache_storage/cache_storage.mojom-forward.h"
#include "third_party/blink/public/mojom/indexeddb/indexeddb.mojom-forward.h"
#include "third_party/blink/public/mojom/locks/lock_manager.mojom-forward.h"
#include "third_party/blink/public/mojom/quota/quota_types.mojom-forward.h"
#include "third_party/blink/public/mojom/storage_access/storage_access_handle.mojom.h"
#include "third_party/blink/public/mojom/worker/shared_worker_connector.mojom-forward.h"

namespace storage {
struct BucketInfo;
}  // namespace storage

namespace content {

class StorageAccessHandle
    : public DocumentService<blink::mojom::StorageAccessHandle> {
 public:
  static void Create(
      RenderFrameHost* host,
      mojo::PendingReceiver<blink::mojom::StorageAccessHandle> receiver);

  // Returns true if `host` is considered to have access to unpartitioned
  // storage. For more see:
  // third_party/blink/renderer/modules/storage_access/README.md
  //
  // NOTE: This handles the general permissions check for the entire interface.
  // Specific binding sights (e.g., IndexedDB) should not need their own
  // additional checks once the StorageAccessHandle interface has been bound.
  // One important exception to this is the `DOMStorageContextWrapper`, which
  // is handled outside of this interface so does need to trigger this check.
  static bool DoesDocumentHaveStorageAccess(RenderFrameHost* host);

  StorageAccessHandle(const StorageAccessHandle&) = delete;
  StorageAccessHandle& operator=(const StorageAccessHandle&) = delete;

  void BindIndexedDB(
      mojo::PendingReceiver<blink::mojom::IDBFactory> receiver) override;
  void BindLocks(
      mojo::PendingReceiver<blink::mojom::LockManager> receiver) override;
  void BindCaches(
      mojo::PendingReceiver<blink::mojom::CacheStorage> receiver) override;
  void GetDirectory(GetDirectoryCallback callback) override;
  void Estimate(EstimateCallback callback) override;
  void BindBlobStorage(
      mojo::PendingAssociatedReceiver<blink::mojom::BlobURLStore> receiver)
      override;
  void BindBroadcastChannel(
      mojo::PendingAssociatedReceiver<blink::mojom::BroadcastChannelProvider>
          receiver) override;
  void BindSharedWorker(
      mojo::PendingReceiver<blink::mojom::SharedWorkerConnector> receiver)
      override;

 private:
  StorageAccessHandle(
      RenderFrameHost& host,
      mojo::PendingReceiver<blink::mojom::StorageAccessHandle> receiver);
  ~StorageAccessHandle() override;

  void EstimateImpl(
      EstimateCallback callback,
      storage::QuotaErrorOr<std::set<storage::BucketInfo>> bucket_set) const;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<StorageAccessHandle> weak_factory_{this};
};

}  // namespace content

#endif  // CONTENT_BROWSER_STORAGE_ACCESS_STORAGE_ACCESS_HANDLE_H_
