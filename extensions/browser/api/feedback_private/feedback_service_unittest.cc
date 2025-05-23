// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/api/feedback_private/feedback_service.h"

#include "base/check.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/weak_ptr.h"
#include "base/run_loop.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/test/mock_callback.h"
#include "build/build_config.h"
#include "components/feedback/feedback_data.h"
#include "components/feedback/feedback_report.h"
#include "content/public/test/test_utils.h"
#include "extensions/browser/api/feedback_private/mock_feedback_service.h"
#include "extensions/browser/api_unittest.h"
#include "extensions/shell/browser/api/feedback_private/shell_feedback_private_delegate.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

#if BUILDFLAG(IS_CHROMEOS)
#include "base/base64.h"
#include "chromeos/ash/components/dbus/debug_daemon/debug_daemon_client.h"
#include "components/user_manager/fake_user_manager.h"
#include "components/user_manager/scoped_user_manager.h"
#include "components/user_manager/user_manager.h"
#include "services/data_decoder/public/cpp/test_support/in_process_data_decoder.h"
#include "third_party/boringssl/src/include/openssl/hpke.h"
#endif  // BUILDFLAG(IS_CHROMEOS)

namespace extensions {

using feedback::FeedbackData;
using feedback::FeedbackUploader;
using testing::_;
using testing::StrictMock;

namespace {

const std::string kFakeKey = "fake key";
const std::string kFakeValue = "fake value";
const std::string kTabTitleValue = "some sensitive info";
#if BUILDFLAG(IS_CHROMEOS)
constexpr char kVariationsFetchHpkeKey[] =
    "https://www.gstatic.com/chromeos-feedback-variations-encryption-key/"
    "public_keyset.json";
const std::string kTestPublicKeyResponseBody =
    "{\"primaryKeyId\":123,\"key\":[{\"keyData\":{\"typeUrl\":\"type."
    "googleapis.com/"
    "google.crypto.tink.HpkePublicKey\",\"value\":"
    "\"EgYIARABGAIaIKeVK4N3icUhM5YF+Pp5S6PWAg9OlY8zP9oLL9qv4IYS\","
    "\"keyMaterialType\":\"ASYMMETRIC_PUBLIC\"},\"status\":\"ENABLED\","
    "\"keyId\":456,\"outputPrefixType\":\"RAW\"}]}";
const std::string kTestBase64HpkePrivateKey =
    "IHbZB+CCrEXra2WGQx/jFQ+a0NSpVCauqy2uC9NH8Hs=";
// Command line variation string returned during testing.
const std::string kTestCommandLineVariations =
    " --enable-features=\"*TestBlinkFeatureDefault,"
    "TestFeatureForBrowserTest1\" "
    "--disable-features=\"TestFeatureForBrowserTest2\" "
    "--disable-field-trial-config";
#endif  // BUILDFLAG(IS_CHROMEOS)

class MockFeedbackUploader : public FeedbackUploader {
 public:
  MockFeedbackUploader(
      bool is_off_the_record,
      const base::FilePath& state_path,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
      : FeedbackUploader(is_off_the_record, state_path, url_loader_factory) {}

  MOCK_METHOD(void,
              QueueReport,
              (std::unique_ptr<std::string>, bool, int),
              (override));

  base::WeakPtr<FeedbackUploader> AsWeakPtr() override {
    return weak_ptr_factory_.GetWeakPtr();
  }

 private:
  base::WeakPtrFactory<MockFeedbackUploader> weak_ptr_factory_{this};
};

class MockFeedbackPrivateDelegate : public ShellFeedbackPrivateDelegate {
 public:
  MockFeedbackPrivateDelegate() {
    ON_CALL(*this, FetchSystemInformation)
        .WillByDefault([](content::BrowserContext* context,
                          system_logs::SysLogsFetcherCallback callback) {
          auto sys_info = std::make_unique<system_logs::SystemLogsResponse>();
          sys_info->emplace(kFakeKey, kFakeValue);
          sys_info->emplace(feedback::FeedbackReport::kMemUsageWithTabTitlesKey,
                            kTabTitleValue);
          std::move(callback).Run(std::move(sys_info));
        });
#if BUILDFLAG(IS_CHROMEOS)
    ON_CALL(*this, FetchExtraLogs)
        .WillByDefault([](scoped_refptr<FeedbackData> feedback_data,
                          FetchExtraLogsCallback callback) {
          std::move(callback).Run(feedback_data);
        });
#endif  // BUILDFLAG(IS_CHROMEOS)
  }

  ~MockFeedbackPrivateDelegate() override = default;

  MOCK_METHOD(void,
              FetchSystemInformation,
              (content::BrowserContext*, system_logs::SysLogsFetcherCallback),
              (const, override));
#if BUILDFLAG(IS_CHROMEOS)
  MOCK_METHOD(void,
              FetchExtraLogs,
              (scoped_refptr<feedback::FeedbackData>, FetchExtraLogsCallback),
              (const, override));
#endif  // BUILDFLAG(IS_CHROMEOS)
};

#if BUILDFLAG(IS_CHROMEOS)
const FeedbackCommon::AttachedFile* FindAttachment(
    std::string_view name,
    const scoped_refptr<FeedbackData>& feedback_data) {
  size_t num_attachments = feedback_data->attachments();
  for (size_t i = 0; i < num_attachments; i++) {
    const FeedbackCommon::AttachedFile* file = feedback_data->attachment(i);
    if (file->name == name) {
      return file;
    }
  }
  return nullptr;
}

void VerifyAttachment(std::string_view name,
                      std::string_view data,
                      const scoped_refptr<FeedbackData>& feedback_data) {
  const auto* attachment = FindAttachment(name, feedback_data);
  ASSERT_TRUE(attachment);
  EXPECT_EQ(name, attachment->name);
  EXPECT_EQ(data, attachment->data);
}
#endif  // BUILDFLAG(IS_CHROMEOS)

}  // namespace

class FeedbackServiceTest : public ApiUnitTest {
 protected:
  FeedbackServiceTest() {
#if BUILDFLAG(IS_CHROMEOS)
    test_url_loader_factory_.AddResponse(
        kVariationsFetchHpkeKey, kTestPublicKeyResponseBody, net::HTTP_OK);
#endif  // BUILDFLAG(IS_CHROMEOS)
    test_shared_loader_factory_ =
        base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
            &test_url_loader_factory_);
    CHECK(scoped_temp_dir_.CreateUniqueTempDir());
    mock_uploader_ = std::make_unique<StrictMock<MockFeedbackUploader>>(
        /*is_off_the_record=*/false, scoped_temp_dir_.GetPath(),
        test_shared_loader_factory_);
    feedback_data_ = base::MakeRefCounted<FeedbackData>(
        mock_uploader_->AsWeakPtr(), nullptr);
#if BUILDFLAG(IS_CHROMEOS)
    auto fake_user_manager = std::make_unique<user_manager::FakeUserManager>();
    scoped_user_manager_ = std::make_unique<user_manager::ScopedUserManager>(
        std::move(fake_user_manager));
#endif  // BUILDFLAG(IS_CHROMEOS)
  }

  ~FeedbackServiceTest() override = default;

  void TestSendFeedbackConcerningTabTitles(bool send_tab_titles) {
    feedback_data_->AddLog(kFakeKey, kFakeValue);
    feedback_data_->AddLog(feedback::FeedbackReport::kMemUsageWithTabTitlesKey,
                           kTabTitleValue);
    const FeedbackParams params{/*is_internal_email=*/false,
                                /*load_system_info=*/false,
                                /*send_tab_titles=*/send_tab_titles,
                                /*send_histograms=*/true,
                                /*send_bluetooth_logs=*/false,
                                /*send_wifi_debug_logs=*/false,
                                /*send_autofill_metadata=*/false};

    EXPECT_CALL(*mock_uploader_, QueueReport).Times(1);
    base::MockCallback<SendFeedbackCallback> mock_callback;
    EXPECT_CALL(mock_callback, Run(true));

    auto mock_delegate = std::make_unique<MockFeedbackPrivateDelegate>();
#if BUILDFLAG(IS_CHROMEOS)
    EXPECT_CALL(*mock_delegate, FetchExtraLogs(_, _)).Times(1);
#endif  // BUILDFLAG(IS_CHROMEOS)

    auto feedback_service = base::MakeRefCounted<FeedbackService>(
        browser_context(), mock_delegate.get());
    RunUntilFeedbackIsSent(feedback_service, params, mock_callback.Get());
    EXPECT_EQ(1u, feedback_data_->sys_info()->count(kFakeKey));
  }

#if BUILDFLAG(IS_CHROMEOS)
  void TestSendFeedbackConcerningWifiDebugLogs(bool send_wifi_debug_logs) {
    const FeedbackParams params{/*is_internal_email=*/false,
                                /*load_system_info=*/true,
                                /*send_tab_titles=*/false,
                                /*send_histograms=*/false,
                                /*send_bluetooth_logs=*/false,
                                /*send_wifi_debug_logs=*/send_wifi_debug_logs,
                                /*send_autofill_metadata=*/false};

    // Create a test file in sub directory "wifi".
    const base::FilePath test_file_dir =
        scoped_temp_dir_.GetPath().Append("wifi");
    ASSERT_TRUE(base::CreateDirectory(test_file_dir));

    const base::FilePath test_file =
        test_file_dir.Append("wifi_firmware_dumps.tar.zst");
    ASSERT_TRUE(base::WriteFile(test_file, "Test file content"));

    EXPECT_CALL(*mock_uploader_, QueueReport).Times(1);
    base::MockCallback<SendFeedbackCallback> mock_callback;
    EXPECT_CALL(mock_callback, Run(true));

    auto mock_delegate = std::make_unique<MockFeedbackPrivateDelegate>();
    EXPECT_CALL(*mock_delegate, FetchSystemInformation(_, _)).Times(1);
    EXPECT_CALL(*mock_delegate, FetchExtraLogs(_, _)).Times(1);

    if (send_wifi_debug_logs) {
      ash::DebugDaemonClient::InitializeFake();
    }
    auto feedback_service = base::MakeRefCounted<FeedbackService>(
        browser_context(), mock_delegate.get());
    feedback_service->SetLogFilesRootPathForTesting(scoped_temp_dir_.GetPath());

    RunUntilFeedbackIsSent(feedback_service, params, mock_callback.Get());
    if (ash::DebugDaemonClient::Get()) {
      ash::DebugDaemonClient::Shutdown();
    }
    EXPECT_EQ(1u, feedback_data_->sys_info()->count(kFakeKey));

    // Verify the attachment is added if and only if send_wifi_debug_logs is
    // true.
    constexpr char kWifiDumpName[] = "wifi_firmware_dumps.tar.zst";
    if (send_wifi_debug_logs) {
      VerifyAttachment(kWifiDumpName, "TestData", feedback_data_);
    } else {
      EXPECT_FALSE(FindAttachment(kWifiDumpName, feedback_data_));
    }
  }

  void TestSendFeedbackConcerningBluetoothDebugLogs(bool send_bluetooth_logs) {
    const FeedbackParams params{/*is_internal_email=*/false,
                                /*load_system_info=*/true,
                                /*send_tab_titles=*/false,
                                /*send_histograms=*/false,
                                /*send_bluetooth_logs=*/send_bluetooth_logs,
                                /*send_wifi_debug_logs=*/false,
                                /*send_autofill_metadata=*/false};

    EXPECT_CALL(*mock_uploader_, QueueReport).Times(1);
    base::MockCallback<SendFeedbackCallback> mock_callback;
    EXPECT_CALL(mock_callback, Run(true));

    auto mock_delegate = std::make_unique<MockFeedbackPrivateDelegate>();
    EXPECT_CALL(*mock_delegate, FetchSystemInformation(_, _)).Times(1);
    EXPECT_CALL(*mock_delegate, FetchExtraLogs(_, _)).Times(1);

    if (send_bluetooth_logs) {
      ash::DebugDaemonClient::InitializeFake();
    }
    auto feedback_service = base::MakeRefCounted<FeedbackService>(
        browser_context(), mock_delegate.get());
    feedback_service->SetLogFilesRootPathForTesting(scoped_temp_dir_.GetPath());

    RunUntilFeedbackIsSent(feedback_service, params, mock_callback.Get());
    if (ash::DebugDaemonClient::Get()) {
      ash::DebugDaemonClient::Shutdown();
    }
    EXPECT_EQ(1u, feedback_data_->sys_info()->count(kFakeKey));

    // Verify the attachment is added if and only if send_bluetooth_logs is
    // true.
    constexpr char kBluetoothDumpName[] = "bluetooth_firmware_dumps.tar.zst";
    if (send_bluetooth_logs) {
      VerifyAttachment(kBluetoothDumpName, "TestData", feedback_data_);
    } else {
      EXPECT_FALSE(FindAttachment(kBluetoothDumpName, feedback_data_));
    }
  }
#endif  // BUILDFLAG(IS_CHROMEOS)

  void RunUntilFeedbackIsSent(scoped_refptr<FeedbackService> feedback_service,
                              const FeedbackParams& params,
                              SendFeedbackCallback mock_callback) {
    feedback_service->RedactThenSendFeedback(params, feedback_data_,
                                             std::move(mock_callback));
    base::ThreadPoolInstance::Get()->FlushForTesting();
    task_environment()->RunUntilIdle();
  }

#if BUILDFLAG(IS_CHROMEOS)
  std::unique_ptr<user_manager::ScopedUserManager> scoped_user_manager_;
  data_decoder::test::InProcessDataDecoder in_process_data_decoder_;
#endif  // BUILDFLAG(IS_CHROMEOS)

  base::ScopedTempDir scoped_temp_dir_;
  network::TestURLLoaderFactory test_url_loader_factory_;
  scoped_refptr<network::SharedURLLoaderFactory> test_shared_loader_factory_;
  std::unique_ptr<StrictMock<MockFeedbackUploader>> mock_uploader_;
  scoped_refptr<FeedbackData> feedback_data_;
};

TEST_F(FeedbackServiceTest, SendFeedbackWithoutSysInfo) {
  const FeedbackParams params{/*is_internal_email=*/false,
                              /*load_system_info=*/false,
                              /*send_tab_titles=*/true,
                              /*send_histograms=*/true,
                              /*send_bluetooth_logs=*/false,
                              /*send_wifi_debug_logs=*/false,
                              /*send_autofill_metadata=*/false};

  EXPECT_CALL(*mock_uploader_, QueueReport).Times(1);
  base::MockCallback<SendFeedbackCallback> mock_callback;
  EXPECT_CALL(mock_callback, Run(true));

  auto shell_delegate = std::make_unique<ShellFeedbackPrivateDelegate>();
  auto feedback_service = base::MakeRefCounted<FeedbackService>(
      browser_context(), shell_delegate.get());

  RunUntilFeedbackIsSent(feedback_service, params, mock_callback.Get());
}

TEST_F(FeedbackServiceTest, SendFeedbackLoadSysInfo) {
  const FeedbackParams params{/*is_internal_email=*/false,
                              /*load_system_info=*/true,
                              /*send_tab_titles=*/true,
                              /*send_histograms=*/true,
                              /*send_bluetooth_logs=*/false,
                              /*send_wifi_debug_logs=*/false,
                              /*send_autofill_metadata=*/false};

  EXPECT_CALL(*mock_uploader_, QueueReport).Times(1);
  base::MockCallback<SendFeedbackCallback> mock_callback;
  EXPECT_CALL(mock_callback, Run(true));

  auto mock_delegate = std::make_unique<MockFeedbackPrivateDelegate>();
  EXPECT_CALL(*mock_delegate, FetchSystemInformation(_, _)).Times(1);
#if BUILDFLAG(IS_CHROMEOS)
  EXPECT_CALL(*mock_delegate, FetchExtraLogs(_, _)).Times(1);
#endif  // BUILDFLAG(IS_CHROMEOS)

  auto feedback_service = base::MakeRefCounted<FeedbackService>(
      browser_context(), mock_delegate.get());

  RunUntilFeedbackIsSent(feedback_service, params, mock_callback.Get());
  EXPECT_EQ(1u, feedback_data_->sys_info()->count(kFakeKey));
  EXPECT_EQ(1u, feedback_data_->sys_info()->count(
                    feedback::FeedbackReport::kMemUsageWithTabTitlesKey));
}

// TODO(crbug.com/40908623): Re-enable this test
TEST_F(FeedbackServiceTest, DISABLED_SendFeedbackDoNotSendTabTitles) {
  TestSendFeedbackConcerningTabTitles(false);
  EXPECT_EQ(0u, feedback_data_->sys_info()->count(
                    feedback::FeedbackReport::kMemUsageWithTabTitlesKey));
}

TEST_F(FeedbackServiceTest, SendFeedbackDoSendTabTitles) {
  TestSendFeedbackConcerningTabTitles(true);
  EXPECT_EQ(1u, feedback_data_->sys_info()->count(
                    feedback::FeedbackReport::kMemUsageWithTabTitlesKey));
}

TEST_F(FeedbackServiceTest, SendFeedbackAutofillMetadata) {
  const FeedbackParams params{/*is_internal_email=*/true,
                              /*load_system_info=*/false,
                              /*send_tab_titles=*/false,
                              /*send_histograms=*/true,
                              /*send_bluetooth_logs=*/false,
                              /*send_wifi_debug_logs=*/false,
                              /*send_autofill_metadata=*/true};
  feedback_data_->set_autofill_metadata("Autofill Metadata");
  EXPECT_CALL(*mock_uploader_, QueueReport).Times(1);
  base::MockCallback<SendFeedbackCallback> mock_callback;
  EXPECT_CALL(mock_callback, Run(true));

  auto feedback_service =
      base::MakeRefCounted<FeedbackService>(browser_context(), nullptr);

  RunUntilFeedbackIsSent(feedback_service, params, mock_callback.Get());
}

#if BUILDFLAG(IS_CHROMEOS)
TEST_F(FeedbackServiceTest, SendFeedbackWithWifiDebugLogs) {
  TestSendFeedbackConcerningWifiDebugLogs(/*send_wifi_debug_logs=*/true);
}

TEST_F(FeedbackServiceTest, SendFeedbackWithoutWifiDebugLogs) {
  TestSendFeedbackConcerningWifiDebugLogs(/*send_wifi_debug_logs=*/false);
}

TEST_F(FeedbackServiceTest, SendFeedbackWithBluetoothDebugLogs) {
  TestSendFeedbackConcerningBluetoothDebugLogs(/*send_bluetooth_logs=*/true);
}

TEST_F(FeedbackServiceTest, SendFeedbackWithoutBluetoothDebugLogs) {
  TestSendFeedbackConcerningBluetoothDebugLogs(/*send_bluetooth_logs=*/false);
}

// Test that the feedback report contains the variations.binary encrypted
// properly.
TEST_F(FeedbackServiceTest, TestSendFeedbackWithVariationsBinary) {
  const FeedbackParams params{/*is_internal_email=*/false,
                              /*load_system_info=*/true,
                              /*send_tab_titles=*/false,
                              /*send_histograms=*/false,
                              /*send_bluetooth_logs=*/false,
                              /*send_wifi_debug_logs=*/false,
                              /*send_autofill_metadata=*/false};

  EXPECT_CALL(*mock_uploader_, QueueReport).Times(1);
  base::MockCallback<SendFeedbackCallback> mock_callback;
  EXPECT_CALL(mock_callback, Run(true));

  auto mock_delegate = std::make_unique<MockFeedbackPrivateDelegate>();
  EXPECT_CALL(*mock_delegate, FetchSystemInformation(_, _)).Times(1);
  EXPECT_CALL(*mock_delegate, FetchExtraLogs(_, _)).Times(1);

  auto feedback_service = base::MakeRefCounted<FeedbackService>(
      browser_context(), mock_delegate.get());
  feedback_service->SetUrlLoaderFactory(test_shared_loader_factory_);

  RunUntilFeedbackIsSent(feedback_service, params, mock_callback.Get());
  EXPECT_EQ(1u, feedback_data_->sys_info()->count(kFakeKey));

  // Initialize Hpke private key.
  bssl::ScopedEVP_HPKE_KEY base_key;
  std::string decoded_hpke_private_key;
  base::Base64Decode(kTestBase64HpkePrivateKey, &decoded_hpke_private_key);
  std::vector<uint8_t> hpke_private_key;
  hpke_private_key.assign(decoded_hpke_private_key.begin(),
                          decoded_hpke_private_key.end());
  ASSERT_TRUE(EVP_HPKE_KEY_init(/*key=*/base_key.get(),
                                /*kem=*/EVP_hpke_x25519_hkdf_sha256(),
                                /*priv_key=*/hpke_private_key.data(),
                                /*priv_key_len=*/hpke_private_key.size()));

  // Get the encrypted file.
  constexpr char kVariationsBinary[] = "variations.binary";
  const FeedbackCommon::AttachedFile* variatons_binary =
      FindAttachment(kVariationsBinary, feedback_data_);
  ASSERT_TRUE(variatons_binary);
  std::vector<uint8_t> encrypted_data(variatons_binary->data.begin(),
                                      variatons_binary->data.end());

  // Setup recipient context.
  bssl::ScopedEVP_HPKE_CTX recipient_ctx;
  ASSERT_TRUE(EVP_HPKE_CTX_setup_recipient(/*ctx=*/recipient_ctx.get(),
                                           /*key=*/base_key.get(),
                                           /*kdf=*/EVP_hpke_hkdf_sha256(),
                                           /*aead=*/EVP_hpke_aes_256_gcm(),
                                           /*enc=*/encrypted_data.data(),
                                           /*enc_len=*/X25519_PUBLIC_VALUE_LEN,
                                           /*info=*/nullptr,
                                           /*info_len=*/0));

  // Decryption.
  auto ciphertext =
      base::span(encrypted_data).subspan<X25519_PUBLIC_VALUE_LEN>();
  std::vector<uint8_t> plaintext(ciphertext.size());
  size_t plaintext_len;
  ASSERT_TRUE(EVP_HPKE_CTX_open(/*ctx=*/recipient_ctx.get(),
                                /*out=*/plaintext.data(),
                                /*out_len=*/&plaintext_len,
                                /*max_out_len=*/plaintext.size(),
                                /*in=*/ciphertext.data(),
                                /*in_len=*/ciphertext.size(),
                                /*ad=*/nullptr,
                                /*ad_len=*/0));
  plaintext.resize(plaintext_len);
  std::string decrypted_string(plaintext.begin(), plaintext.end());

  // Final check.
  EXPECT_EQ(kTestCommandLineVariations, decrypted_string);
}
#endif  // BUILDFLAG(IS_CHROMEOS)

}  // namespace extensions
