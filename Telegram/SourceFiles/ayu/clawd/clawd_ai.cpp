// This is the source code of ClawdGram for Desktop,
// based on AyuGram for Desktop by Radolyn Labs.
#include "ayu/clawd/clawd_ai.h"

#include "ayu/clawd/clawd_settings.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Clawd {
namespace {

constexpr auto kTimeoutMs = 180 * 1000;
constexpr auto kAnthropicVersion = "2026-10-21";
constexpr auto kFallbackBeta = "server-side-fallback-2026-07-01";

[[nodiscard]] QNetworkAccessManager &Network() {
	static const auto result = new QNetworkAccessManager(
		QCoreApplication::instance());
	return *result;
}

[[nodiscard]] QString SystemPrompt(AiTask task) {
	const auto persona = Get().persona.trimmed();
	auto result = QString();
	switch (task) {
	case AiTask::Chat:
		result = u"You are ClawdGram AI, a helpful assistant built into "
			"a Telegram client. Answer concisely in plain text; "
			"avoid heavy Markdown."_q;
		break;
	case AiTask::ComposeMessage:
		result = u"You write a Telegram chat message on behalf of the user "
			"from their instruction. Reply with only the message text: "
			"no quotes, no preamble, no explanations. Match the language "
			"of the instruction unless it asks for another one."_q;
		break;
	case AiTask::Autocorrect:
		result = u"You are an autocorrect engine. Fix spelling, grammar and "
			"punctuation in the user's text. Keep the language, meaning, "
			"tone, slang, emoji, line breaks and formatting markers. "
			"If nothing needs fixing, return the text unchanged. Reply "
			"with only the corrected text and nothing else."_q;
		break;
	}
	if (!persona.isEmpty() && task != AiTask::Autocorrect) {
		result += u"\n\nAdditional instructions from the user:\n"_q + persona;
	}
	return result;
}

[[nodiscard]] bool SupportsServerFallback(const QString &model) {
	static const auto models = QStringList{
		u"claude-fable-5-2"_q,
		u"claude-fable-5-1"_q,
		u"claude-opus-5-5"_q,
		u"claude-opus-5"_q,
		u"claude-sonnet-5-5"_q,
	};
	return models.contains(model);
}

[[nodiscard]] QString ErrorFromBody(
		const QJsonObject &root,
		const QString &fallback) {
	const auto error = root.value(u"error"_q);
	if (error.isObject()) {
		const auto message = error.toObject().value(u"message"_q).toString();
		if (!message.isEmpty()) {
			return message;
		}
	}
	return fallback;
}

[[nodiscard]] QNetworkRequest PrepareClaude(
		const AiRequest &request,
		QByteArray &body) {
	const auto &values = Get();
	const auto model = values.claudeModel.trimmed();

	auto messages = QJsonArray();
	for (const auto &message : request.messages) {
		messages.push_back(QJsonObject{
			{ u"role"_q, message.fromUser ? u"user"_q : u"assistant"_q },
			{ u"content"_q, message.text },
		});
	}
	auto object = QJsonObject{
		{ u"model"_q, model },
		{ u"max_tokens"_q, 16000 },
		{ u"system"_q, SystemPrompt(request.task) },
		{ u"messages"_q, messages },
	};
	if (request.task != AiTask::Chat) {
		object.insert(
			u"output_config"_q,
			QJsonObject{ { u"effort"_q, u"low"_q } });
	}
	const auto fallback = SupportsServerFallback(model);
	if (fallback) {
		object.insert(u"fallbacks"_q, u"default"_q);
	}
	body = QJsonDocument(object).toJson(QJsonDocument::Compact);

	auto result = QNetworkRequest(
		QUrl(u"https://api.anthropic.com/v1/messages"_q));
	result.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	result.setRawHeader("x-api-key", values.claudeKey.trimmed().toUtf8());
	result.setRawHeader("anthropic-version", kAnthropicVersion);
	if (fallback) {
		result.setRawHeader("anthropic-beta", kFallbackBeta);
	}
	return result;
}

[[nodiscard]] QNetworkRequest PrepareGemini(
		const AiRequest &request,
		QByteArray &body) {
	const auto &values = Get();
	const auto model = values.geminiModel.trimmed();

	auto contents = QJsonArray();
	for (const auto &message : request.messages) {
		contents.push_back(QJsonObject{
			{ u"role"_q, message.fromUser ? u"user"_q : u"model"_q },
			{ u"parts"_q, QJsonArray{
				QJsonObject{ { u"text"_q, message.text } },
			} },
		});
	}
	const auto object = QJsonObject{
		{ u"systemInstruction"_q, QJsonObject{
			{ u"parts"_q, QJsonArray{
				QJsonObject{ { u"text"_q, SystemPrompt(request.task) } },
			} },
		} },
		{ u"contents"_q, contents },
	};
	body = QJsonDocument(object).toJson(QJsonDocument::Compact);

	const auto url = u"https://generativelanguage.googleapis.com/v1beta/"
		"models/%1:generateContent"_q.arg(
			QString::fromUtf8(QUrl::toPercentEncoding(model)));
	auto result = QNetworkRequest(QUrl(url));
	result.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	result.setRawHeader("x-goog-api-key", values.geminiKey.trimmed().toUtf8());
	return result;
}

struct Parsed {
	QString text;
	QString error;
};

[[nodiscard]] Parsed ParseClaude(const QJsonObject &root) {
	if (root.value(u"type"_q).toString() == u"error"_q) {
		return { .error = ErrorFromBody(root, u"Claude API error."_q) };
	}
	const auto stop = root.value(u"stop_reason"_q).toString();
	if (stop == u"refusal"_q) {
		return { .error = u"Claude declined this request."_q };
	}
	auto text = QString();
	for (const auto &block : root.value(u"content"_q).toArray()) {
		const auto object = block.toObject();
		if (object.value(u"type"_q).toString() == u"text"_q) {
			text += object.value(u"text"_q).toString();
		}
	}
	if (text.trimmed().isEmpty()) {
		return { .error = u"Claude returned an empty answer."_q };
	}
	return { .text = text.trimmed() };
}

[[nodiscard]] Parsed ParseGemini(const QJsonObject &root) {
	if (root.contains(u"error"_q)) {
		return { .error = ErrorFromBody(root, u"Gemini API error."_q) };
	}
	const auto candidates = root.value(u"candidates"_q).toArray();
	if (candidates.isEmpty()) {
		const auto reason = root.value(u"promptFeedback"_q).toObject()
			.value(u"blockReason"_q).toString();
		return {
			.error = reason.isEmpty()
				? u"Gemini returned no answer."_q
				: (u"Gemini blocked this request: "_q + reason),
		};
	}
	auto text = QString();
	const auto parts = candidates.first().toObject()
		.value(u"content"_q).toObject()
		.value(u"parts"_q).toArray();
	for (const auto &part : parts) {
		const auto object = part.toObject();
		if (object.value(u"thought"_q).toBool()) {
			continue;
		}
		text += object.value(u"text"_q).toString();
	}
	if (text.trimmed().isEmpty()) {
		return { .error = u"Gemini returned an empty answer."_q };
	}
	return { .text = text.trimmed() };
}

} // namespace

QString AiConfigurationError() {
	const auto &values = Get();
	switch (values.provider) {
	case AiProvider::Claude:
		if (values.claudeKey.trimmed().isEmpty()) {
			return u"Add your Claude API key in Settings → ClawdGram Extras."_q;
		} else if (values.claudeModel.trimmed().isEmpty()) {
			return u"Set a Claude model in Settings → ClawdGram Extras."_q;
		}
		break;
	case AiProvider::Gemini:
		if (values.geminiKey.trimmed().isEmpty()) {
			return u"Add your Gemini API key in Settings → ClawdGram Extras."_q;
		} else if (values.geminiModel.trimmed().isEmpty()) {
			return u"Set a Gemini model in Settings → ClawdGram Extras."_q;
		}
		break;
	}
	return QString();
}

void AskAi(
		AiRequest &&request,
		Fn<void(QString)> done,
		Fn<void(QString)> fail) {
	if (const auto error = AiConfigurationError(); !error.isEmpty()) {
		fail(error);
		return;
	} else if (request.messages.empty()) {
		fail(u"Nothing to send."_q);
		return;
	}
	const auto provider = Get().provider;
	auto body = QByteArray();
	auto networkRequest = (provider == AiProvider::Claude)
		? PrepareClaude(request, body)
		: PrepareGemini(request, body);
	networkRequest.setTransferTimeout(kTimeoutMs);

	const auto reply = Network().post(networkRequest, body);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		const auto bytes = reply->readAll();
		const auto document = QJsonDocument::fromJson(bytes);
		const auto root = document.object();
		if (reply->error() != QNetworkReply::NoError) {
			const auto fallback = reply->errorString();
			fail(document.isObject()
				? ErrorFromBody(root, fallback)
				: fallback);
			return;
		} else if (!document.isObject()) {
			fail(u"Could not read the AI response."_q);
			return;
		}
		const auto parsed = (provider == AiProvider::Claude)
			? ParseClaude(root)
			: ParseGemini(root);
		if (!parsed.error.isEmpty()) {
			fail(parsed.error);
		} else {
			done(parsed.text);
		}
	});
}

} // namespace Clawd
