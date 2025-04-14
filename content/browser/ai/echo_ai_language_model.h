// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_AI_ECHO_AI_LANGUAGE_MODEL_H_
#define CONTENT_BROWSER_AI_ECHO_AI_LANGUAGE_MODEL_H_

#include "base/memory/weak_ptr.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote_set.h"
#include "third_party/blink/public/mojom/ai/ai_language_model.mojom.h"

namespace content {

// The implementation of `blink::mojom::AILanguageModel` which only echoes
// back the prompt text used for testing.
class EchoAILanguageModel : public blink::mojom::AILanguageModel {
 public:
  explicit EchoAILanguageModel(
      blink::mojom::AILanguageModelSamplingParamsPtr sampling_params);
  EchoAILanguageModel(const EchoAILanguageModel&) = delete;
  EchoAILanguageModel& operator=(const EchoAILanguageModel&) = delete;

  ~EchoAILanguageModel() override;

  // `blink::mojom::AILanguageModel` implementation.
  void Prompt(std::vector<blink::mojom::AILanguageModelPromptPtr> prompts,
              const std::optional<std::string>& response_json_schema,
              mojo::PendingRemote<blink::mojom::ModelStreamingResponder>
                  pending_responder) override;
  void Fork(
      mojo::PendingRemote<blink::mojom::AIManagerCreateLanguageModelClient>
          client) override;
  void Destroy() override;
  void MeasureInputUsage(
      const std::string& input,
      mojo::PendingRemote<blink::mojom::AILanguageModelMeasureInputUsageClient>
          client) override;

 private:
  void DoMockExecution(const std::string& input,
                       mojo::RemoteSetElementId responder_id);

  bool is_destroyed_ = false;
  uint64_t current_tokens_ = 0;
  blink::mojom::AILanguageModelSamplingParamsPtr sampling_params_;

  mojo::RemoteSet<blink::mojom::ModelStreamingResponder> responder_set_;

  base::WeakPtrFactory<EchoAILanguageModel> weak_ptr_factory_{this};
};

}  // namespace content

#endif  // CONTENT_BROWSER_AI_ECHO_AI_LANGUAGE_MODEL_H_
