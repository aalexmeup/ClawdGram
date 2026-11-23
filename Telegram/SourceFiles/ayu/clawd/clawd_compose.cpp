// This is the source code of ClawdGram for Desktop,
// based on AyuGram for Desktop by Radolyn Labs.
#include "ayu/clawd/clawd_compose.h"

#include "ayu/clawd/clawd_ai.h"
#include "ayu/clawd/clawd_commands.h"
#include "ayu/clawd/clawd_settings.h"
#include "base/event_filter.h"
#include "ui/toast/toast.h"
#include "ui/widgets/fields/input_field.h"

#include <QtGui/QTextCursor>
#include <QtWidgets/QTextEdit>

namespace Clawd {
namespace {

const auto kCommand = u".ai"_q;
void ShowToast(not_null<QWidget*> widget, const QString &text) {
	Ui::Toast::Show(widget->window(), text);
}

void PutText(not_null<Ui::InputField*> field, const QString &text) {
	field->setTextWithTags(
		{ text, {} },
		Ui::InputField::HistoryAction::NewEntry);
	auto cursor = field->rawTextEdit()->textCursor();
	cursor.movePosition(QTextCursor::End);
	field->rawTextEdit()->setTextCursor(cursor);
	field->setFocus();
}

[[nodiscard]] bool IsAutocorrectKey(not_null<QEvent*> e) {
	const auto key = static_cast<QKeyEvent*>(e.get());
	const auto modifiers = key->modifiers()
		& (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier);
	return (key->key() == Qt::Key_R)
		&& (modifiers == (Qt::ControlModifier | Qt::ShiftModifier));
}

void RunAutocorrect(not_null<Ui::InputField*> field) {
	const auto original = field->getLastText();
	if (original.trimmed().isEmpty()) {
		return;
	}
	const auto weak = QPointer<Ui::InputField>(field.get());
	ShowToast(field, u"ClawdGram AI: fixing your text…"_q);
	AskAi({
		.task = AiTask::Autocorrect,
		.messages = { { .fromUser = true, .text = original } },
	}, [=](QString result) {
		const auto strong = weak.data();
		if (!strong) {
			return;
		} else if (strong->getLastText() != original) {
			ShowToast(strong, u"Text changed while fixing, skipped."_q);
			return;
		}
		PutText(strong, result);
	}, [=](QString error) {
		if (const auto strong = weak.data()) {
			ShowToast(strong, u"AI autocorrect failed: "_q + error);
		}
	});
}

} // namespace

bool InterceptAiCommand(
		not_null<Ui::InputField*> field,
		Fn<void()> send) {
	switch (InterceptChatCommand(field)) {
	case CommandResult::Handled: return true;
	case CommandResult::SendAsUsual: return false;
	case CommandResult::NotCommand: break;
	}
	if (!Get().aiCommand) {
		return false;
	}
	const auto text = field->getLastText();
	const auto trimmed = text.trimmed();
	if (!trimmed.startsWith(kCommand, Qt::CaseInsensitive)) {
		return false;
	} else if (trimmed.size() > kCommand.size()
		&& !trimmed[kCommand.size()].isSpace()) {
		return false;
	}
	const auto prompt = trimmed.mid(kCommand.size()).trimmed();
	if (prompt.isEmpty()) {
		ShowToast(field, u"Write a prompt after .ai, e.g. \".ai say hi\""_q);
		return true;
	}
	const auto weak = QPointer<Ui::InputField>(field.get());
	field->setTextWithTags({}, Ui::InputField::HistoryAction::NewEntry);
	ShowToast(field, u"ClawdGram AI is writing…"_q);
	AskAi({
		.task = AiTask::ComposeMessage,
		.messages = { { .fromUser = true, .text = prompt } },
	}, [=](QString result) {
		const auto strong = weak.data();
		if (!strong) {
			return;
		} else if (!strong->getLastText().trimmed().isEmpty()) {
			ShowToast(strong, u"Field is not empty, AI reply skipped."_q);
			return;
		}
		PutText(strong, result);
		const auto loops = result.trimmed().startsWith(
			kCommand,
			Qt::CaseInsensitive);
		if (Get().aiCommandAutoSend && send && !loops) {
			send();
		}
	}, [=](QString error) {
		const auto strong = weak.data();
		if (!strong) {
			return;
		}
		if (strong->getLastText().trimmed().isEmpty()) {
			PutText(strong, text);
		}
		ShowToast(strong, u"ClawdGram AI failed: "_q + error);
	});
	return true;
}

void AttachAiAutocorrect(not_null<Ui::InputField*> field) {
	const auto raw = field->rawTextEdit();
	base::install_event_filter(field, raw, [=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type != QEvent::KeyPress && type != QEvent::ShortcutOverride) {
			return base::EventFilterResult::Continue;
		} else if (!Get().autocorrect || !IsAutocorrectKey(e)) {
			return base::EventFilterResult::Continue;
		}
		e->accept();
		if (type == QEvent::KeyPress) {
			RunAutocorrect(field);
		}
		return base::EventFilterResult::Cancel;
	});
}

} // namespace Clawd
