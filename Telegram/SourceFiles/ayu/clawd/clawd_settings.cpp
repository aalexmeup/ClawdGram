// This is the source code of ClawdGram for Desktop,
// based on AyuGram for Desktop by Radolyn Labs.
#include "ayu/clawd/clawd_settings.h"

#include "ayu/libs/json.hpp"
#include "rpl/event_stream.h"
#include "settings.h"

#include <QtCore/QFile>
#include <QtCore/QSaveFile>

namespace Clawd {
namespace {

using json = nlohmann::json;

Values Current;
bool Loaded = false;

[[nodiscard]] rpl::event_stream<> &ChangesStream() {
	static auto result = rpl::event_stream<>();
	return result;
}

[[nodiscard]] QString SettingsPath() {
	return cWorkingDir() + u"tdata/clawd_settings.json"_q;
}

[[nodiscard]] std::string Std(const QString &value) {
	return value.toStdString();
}

[[nodiscard]] QString ReadString(
		const json &j,
		const char *key,
		const QString &fallback) {
	const auto i = j.find(key);
	if (i == j.end() || !i->is_string()) {
		return fallback;
	}
	return QString::fromStdString(i->get<std::string>());
}

template <typename T>
[[nodiscard]] T ReadValue(const json &j, const char *key, T fallback) {
	const auto i = j.find(key);
	if (i == j.end()) {
		return fallback;
	}
	try {
		return i->get<T>();
	} catch (...) {
		return fallback;
	}
}

void Save() {
	const auto j = json{
		{ "provider", int(Current.provider) },
		{ "claudeKey", Std(Current.claudeKey) },
		{ "claudeModel", Std(Current.claudeModel) },
		{ "geminiKey", Std(Current.geminiKey) },
		{ "geminiModel", Std(Current.geminiModel) },
		{ "persona", Std(Current.persona) },
		{ "aiCommand", Current.aiCommand },
		{ "aiCommandAutoSend", Current.aiCommandAutoSend },
		{ "autocorrect", Current.autocorrect },
		{ "windowEffects", Current.windowEffects },
		{ "windowOpacity", Current.windowOpacity },
		{ "windowBlur", Current.windowBlur },
		{ "blurSize", Current.blurSize },
		{ "useRubik", Current.useRubik },
		{ "spoofEnabled", Current.spoofEnabled },
		{ "spoofAction", int(Current.spoofAction) },
	};
	auto file = QSaveFile(SettingsPath());
	if (!file.open(QIODevice::WriteOnly)) {
		LOG(("ClawdGram: could not write settings."));
		return;
	}
	const auto data = j.dump(2);
	file.write(data.data(), qint64(data.size()));
	file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
	file.commit();
}

} // namespace

void Load() {
	Loaded = true;
	auto file = QFile(SettingsPath());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto bytes = file.readAll();
	const auto j = json::parse(
		bytes.constData(),
		bytes.constData() + bytes.size(),
		nullptr,
		false);
	if (!j.is_object()) {
		LOG(("ClawdGram: could not parse settings."));
		return;
	}
	const auto defaults = Values();
	Current.provider = AiProvider(
		std::clamp(ReadValue(j, "provider", int(defaults.provider)), 0, 1));
	Current.claudeKey = ReadString(j, "claudeKey", defaults.claudeKey);
	Current.claudeModel = ReadString(j, "claudeModel", defaults.claudeModel);
	Current.geminiKey = ReadString(j, "geminiKey", defaults.geminiKey);
	Current.geminiModel = ReadString(j, "geminiModel", defaults.geminiModel);
	Current.persona = ReadString(j, "persona", defaults.persona);
	Current.aiCommand = ReadValue(j, "aiCommand", defaults.aiCommand);
	Current.aiCommandAutoSend = ReadValue(
		j,
		"aiCommandAutoSend",
		defaults.aiCommandAutoSend);
	Current.autocorrect = ReadValue(j, "autocorrect", defaults.autocorrect);
	Current.windowEffects = ReadValue(
		j,
		"windowEffects",
		defaults.windowEffects);
	Current.windowOpacity = std::clamp(
		ReadValue(j, "windowOpacity", defaults.windowOpacity),
		20,
		100);
	Current.windowBlur = ReadValue(j, "windowBlur", defaults.windowBlur);
	Current.blurSize = std::clamp(
		ReadValue(j, "blurSize", defaults.blurSize),
		0,
		20);
	Current.useRubik = ReadValue(j, "useRubik", defaults.useRubik);
	Current.spoofEnabled = ReadValue(j, "spoofEnabled", defaults.spoofEnabled);
	Current.spoofAction = SpoofAction(std::clamp(
		ReadValue(j, "spoofAction", int(defaults.spoofAction)),
		0,
		5));
}

const Values &Get() {
	if (!Loaded) {
		Load();
	}
	return Current;
}

void Update(Fn<void(Values&)> change) {
	if (!Loaded) {
		Load();
	}
	change(Current);
	Save();
	ChangesStream().fire({});
}

rpl::producer<> Changes() {
	return ChangesStream().events();
}

QString ProviderName(AiProvider provider) {
	switch (provider) {
	case AiProvider::Claude: return u"Claude"_q;
	case AiProvider::Gemini: return u"Gemini"_q;
	}
	return QString();
}

QString CurrentModel() {
	const auto &values = Get();
	return (values.provider == AiProvider::Claude)
		? values.claudeModel
		: values.geminiModel;
}

} // namespace Clawd
