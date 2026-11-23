// This is the source code of ClawdGram for Desktop,
// based on AyuGram for Desktop by Radolyn Labs.
#pragma once

namespace Clawd {

struct AiMessage {
	bool fromUser = true;
	QString text;
};

enum class AiTask {
	Chat,
	ComposeMessage,
	Autocorrect,
};

struct AiRequest {
	AiTask task = AiTask::Chat;
	std::vector<AiMessage> messages;
};

// Empty when the selected provider has an API key.
[[nodiscard]] QString AiConfigurationError();

void AskAi(
	AiRequest &&request,
	Fn<void(QString)> done,
	Fn<void(QString)> fail);

} // namespace Clawd
