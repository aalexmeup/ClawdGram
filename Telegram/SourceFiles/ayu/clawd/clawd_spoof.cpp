// This is the source code of ClawdGram for Desktop,
// based on AyuGram for Desktop by Radolyn Labs.
#include "ayu/clawd/clawd_spoof.h"

#include "apiwrap.h"
#include "ayu/clawd/clawd_settings.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include "core/application.h"
#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "dialogs/dialogs_key.h"
#include "history/history.h"
#include "main/main_session.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

namespace Clawd {
namespace {

constexpr auto kSendEach = crl::time(4000);

struct Target {
	base::weak_ptr<Main::Session> session;
	PeerId peerId;
	MsgId topMsgId = 0;

	explicit operator bool() const {
		return session.get() && peerId;
	}
	friend inline bool operator==(const Target &a, const Target &b) {
		return (a.session.get() == b.session.get())
			&& (a.peerId == b.peerId)
			&& (a.topMsgId == b.topMsgId);
	}
};

Target Last;
int Progress = 0;

[[nodiscard]] Target CurrentTarget() {
	const auto window = Core::App().activePrimaryWindow();
	const auto controller = window ? window->sessionController() : nullptr;
	if (!controller) {
		return {};
	}
	const auto key = controller->activeChatCurrent();
	const auto history = key.history();
	if (!history || history->peer->isSelf()) {
		return {};
	}
	const auto topic = key.topic();
	return {
		.session = base::make_weak(&controller->session()),
		.peerId = history->peer->id,
		.topMsgId = topic ? topic->rootId() : MsgId(0),
	};
}

[[nodiscard]] MTPsendMessageAction ActionFor(SpoofAction action) {
	Progress = (Progress >= 95) ? 5 : (Progress + 7);
	const auto progress = MTP_int(Progress);
	switch (action) {
	case SpoofAction::UploadFile:
		return MTP_sendMessageUploadDocumentAction(progress);
	case SpoofAction::UploadPhoto:
		return MTP_sendMessageUploadPhotoAction(progress);
	case SpoofAction::UploadVideo:
		return MTP_sendMessageUploadVideoAction(progress);
	case SpoofAction::RecordVoice:
		return MTP_sendMessageRecordAudioAction();
	case SpoofAction::RecordRound:
		return MTP_sendMessageRecordRoundAction();
	case SpoofAction::Typing:
		return MTP_sendMessageTypingAction();
	}
	return MTP_sendMessageTypingAction();
}

void Send(const Target &target, MTPsendMessageAction action) {
	const auto session = target.session.get();
	if (!session) {
		return;
	}
	const auto peer = session->data().peerLoaded(target.peerId);
	if (!peer) {
		return;
	}
	using Flag = MTPmessages_SetTyping::Flag;
	session->api().request(MTPmessages_SetTyping(
		MTP_flags(target.topMsgId ? Flag::f_top_msg_id : Flag(0)),
		peer->input(),
		MTP_int(target.topMsgId),
		action
	)).send();
}

void Cancel() {
	if (Last) {
		Send(Last, MTP_sendMessageCancelAction());
	}
	Last = {};
}

void Tick() {
	const auto &values = Get();
	if (!values.spoofEnabled) {
		Cancel();
		return;
	}
	const auto target = CurrentTarget();
	if (!(target == Last)) {
		Cancel();
	}
	if (!target) {
		return;
	}
	Last = target;
	Send(target, ActionFor(values.spoofAction));
}

[[nodiscard]] base::Timer &SpoofTimer() {
	static const auto result = new base::Timer(Tick);
	return *result;
}

void Refresh() {
	static auto wasEnabled = false;
	static auto wasAction = SpoofAction::UploadFile;
	const auto &values = Get();
	if (values.spoofEnabled == wasEnabled
		&& values.spoofAction == wasAction) {
		return;
	}
	wasEnabled = values.spoofEnabled;
	wasAction = values.spoofAction;
	if (values.spoofEnabled) {
		Tick();
		SpoofTimer().callEach(kSendEach);
	} else {
		SpoofTimer().cancel();
		Cancel();
	}
}

} // namespace

void StartActivitySpoofer() {
	static auto started = false;
	static auto lifetime = rpl::lifetime();
	if (started) {
		return;
	}
	started = true;
	Changes() | rpl::on_next([] { Refresh(); }, lifetime);
	Refresh();
}

} // namespace Clawd
