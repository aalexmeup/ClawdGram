// This is the source code of ClawdGram for Desktop,
// based on AyuGram for Desktop by Radolyn Labs.
#pragma once

namespace Ui {
class InputField;
} // namespace Ui

namespace Clawd {

// Returns true when the field held a ".ai <prompt>" command,
// in which case the caller must not send the message.
[[nodiscard]] bool InterceptAiCommand(
	not_null<Ui::InputField*> field,
	Fn<void()> send);

// Ctrl+Shift+R rewrites the field text with AI autocorrect.
void AttachAiAutocorrect(not_null<Ui::InputField*> field);

} // namespace Clawd
