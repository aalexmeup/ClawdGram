// This is the source code of ClawdGram for Desktop,
// based on AyuGram for Desktop by Radolyn Labs.
#pragma once

#include "rpl/producer.h"

namespace Clawd {

enum class AiProvider {
	Claude = 0,
	Gemini = 1,
};

enum class SpoofAction {
	UploadFile = 0,
	UploadPhoto = 1,
	UploadVideo = 2,
	RecordVoice = 3,
	RecordRound = 4,
	Typing = 5,
};

struct Values {
	AiProvider provider = AiProvider::Claude;
	QString claudeKey;
	QString claudeModel = u"claude-opus-5-5"_q;
	QString geminiKey;
	QString geminiModel = u"gemini-2.5-flash"_q;
	QString persona;

	bool aiCommand = true;
	bool aiCommandAutoSend = false;
	bool autocorrect = true;

	bool windowEffects = false;
	int windowOpacity = 90;
	bool windowBlur = true;
	int blurSize = 0;

	bool useRubik = true;

	bool spoofEnabled = false;
	SpoofAction spoofAction = SpoofAction::UploadFile;
};

[[nodiscard]] const Values &Get();
void Update(Fn<void(Values&)> change);
[[nodiscard]] rpl::producer<> Changes();

void Load();

[[nodiscard]] QString ProviderName(AiProvider provider);
[[nodiscard]] QString CurrentModel();

} // namespace Clawd
