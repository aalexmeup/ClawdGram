// This is the source code of ClawdGram for Desktop,
// based on AyuGram for Desktop by Radolyn Labs.
#pragma once

namespace Ui {
class InputField;
} // namespace Ui

namespace Clawd {

enum class CommandResult {
	NotCommand, // Not one of ours, send as usual.
	SendAsUsual, // Ours, the command text itself is sent, extras follow.
	Handled, // Ours, the caller must not send anything.
};

// Chat commands (Russian by default, "eng" suffix for English):
//   .dox / .doxeng          fake "dox" reply after 1.5s
//   .calc / .calceng <expr> calculator, sends the result
//   .ddos / .ddoseng [tgt]  fake stress-test log, edited live
//   .spam / .spameng <text> <count> <delay seconds>
//   .stop                   stops running .spam / .ddos
//   .sumup / .sumupeng      AI summary of last 15 messages (AI panel)
[[nodiscard]] CommandResult InterceptChatCommand(
	not_null<Ui::InputField*> field);

} // namespace Clawd
