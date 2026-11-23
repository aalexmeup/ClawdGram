// This is the source code of ClawdGram for Desktop,
// based on AyuGram for Desktop by Radolyn Labs.
#include "ayu/clawd/clawd_commands.h"

#include "api/api_common.h"
#include "api/api_editing.h"
#include "apiwrap.h"
#include "ayu/clawd/clawd_ai.h"
#include "ayu/clawd/clawd_ai_panel.h"
#include "base/call_delayed.h"
#include "base/random.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include "core/application.h"
#include "data/data_drafts.h"
#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_thread.h"
#include "dialogs/dialogs_key.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "main/main_session.h"
#include "ui/toast/toast.h"
#include "ui/widgets/fields/input_field.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <QtCore/QLocale>
#include <QtCore/QRegularExpression>

#include <cmath>
#include <deque>

namespace Clawd {
namespace {

constexpr auto kDoxDelay = crl::time(1500);
constexpr auto kReplyDelay = crl::time(400);
constexpr auto kDdosFrame = crl::time(1500);
constexpr auto kDdosTrafficSteps = 5;
constexpr auto kSumupCount = 15;
constexpr auto kSumupMessageLimit = 1000;
constexpr auto kSpamMaxCount = 100;
constexpr auto kSpamMinDelay = 0.5;
constexpr auto kCalcMaxDepth = 64;

// ---------------------------------------------------------------- target

struct Target {
	base::weak_ptr<Main::Session> session;
	PeerId peerId = 0;
	MsgId topicRootId = 0;
};

[[nodiscard]] Window::SessionController *ControllerFor(
		not_null<QWidget*> widget) {
	auto window = Core::App().findWindow(widget);
	if (!window) {
		window = Core::App().activePrimaryWindow();
	}
	return window ? window->sessionController() : nullptr;
}

[[nodiscard]] Target TargetFor(not_null<QWidget*> widget) {
	const auto controller = ControllerFor(widget);
	if (!controller) {
		return {};
	}
	const auto key = controller->activeChatCurrent();
	const auto history = key.history();
	if (!history) {
		return {};
	}
	const auto topic = key.topic();
	return {
		.session = base::make_weak(&controller->session()),
		.peerId = history->peer->id,
		.topicRootId = topic ? topic->rootId() : MsgId(0),
	};
}

[[nodiscard]] Data::Thread *Resolve(const Target &target) {
	const auto session = target.session.get();
	if (!session || !target.peerId) {
		return nullptr;
	}
	const auto history = session->data().historyLoaded(target.peerId);
	if (!history) {
		return nullptr;
	} else if (target.topicRootId) {
		return history->peer->forumTopicFor(target.topicRootId);
	}
	return history;
}

HistoryItem *SendText(const Target &target, const TextWithEntities &text) {
	const auto thread = Resolve(target);
	if (!thread) {
		return nullptr;
	}
	auto action = Api::SendAction(thread);
	action.clearDraft = false;
	auto message = Api::MessageToSend(action);
	message.textWithTags = {
		text.text,
		TextUtilities::ConvertEntitiesToTextTags(text.entities),
	};
	auto &session = thread->session();
	const auto localId = session.data().nextLocalMessageId();
	session.api().sendMessage(std::move(message), localId);
	return session.data().message(thread->peer()->id, localId);
}

void SendTextDelayed(Target target, QString text, crl::time delay) {
	base::call_delayed(delay, [=] {
		SendText(target, { text });
	});
}

[[nodiscard]] TextWithEntities Pre(const QString &text) {
	auto result = TextWithEntities{ text };
	result.entities.push_back({ EntityType::Pre, 0, int(text.size()) });
	return result;
}

void ShowToast(not_null<QWidget*> widget, const QString &text) {
	Ui::Toast::Show(widget->window(), text);
}

// ------------------------------------------------------------------ jobs

void ScheduleJobsCleanup();

class Job {
public:
	explicit Job(Target target)
	: _target(std::move(target))
	, _timer([=] { tick(); }) {
	}
	virtual ~Job() = default;

	void start(crl::time each) {
		_timer.callEach(each);
	}
	void finish() {
		if (!_finished) {
			_finished = true;
			_timer.cancel();
			ScheduleJobsCleanup();
		}
	}
	[[nodiscard]] bool finished() const {
		return _finished;
	}

protected:
	virtual void tick() = 0;

	const Target _target;

private:
	base::Timer _timer;
	bool _finished = false;

};

std::vector<std::unique_ptr<Job>> Jobs;

void ScheduleJobsCleanup() {
	crl::on_main([] {
		Jobs.erase(
			ranges::remove_if(Jobs, &Job::finished),
			end(Jobs));
	});
}

int StopAllJobs() {
	auto result = 0;
	for (const auto &job : Jobs) {
		if (!job->finished()) {
			job->finish();
			++result;
		}
	}
	return result;
}

class SpamJob final : public Job {
public:
	SpamJob(Target target, QString text, int count)
	: Job(std::move(target))
	, _text(std::move(text))
	, _left(count) {
	}

private:
	void tick() override {
		if (!Resolve(_target) || _left <= 0) {
			finish();
			return;
		}
		SendText(_target, { _text });
		if (--_left <= 0) {
			finish();
		}
	}

	const QString _text;
	int _left = 0;

};

// Sends the first frame, then edits the same message with the next ones.
class EditedFramesJob final : public Job {
public:
	EditedFramesJob(Target target, std::vector<QString> frames)
	: Job(std::move(target))
	, _frames(std::move(frames)) {
	}

private:
	void tick() override {
		if (_index == 0) {
			sendFirst();
			return;
		} else if (!_item || _index >= int(_frames.size())) {
			finish();
			return;
		} else if (_editing || !_item->isRegular() || _item->isSending()) {
			return; // Not on the server yet or the last edit is in flight.
		}
		_editing = true;
		const auto finishOnLast = (_index + 1 >= int(_frames.size()));
		Api::EditTextMessage(
			_item,
			Pre(_frames[_index++]),
			Data::WebPageDraft{ .removed = true },
			Api::SendOptions(),
			[=](mtpRequestId) {
				_editing = false;
				if (finishOnLast) {
					finish();
				}
			},
			[=](const QString &error, mtpRequestId) {
				_editing = false;
				if (finishOnLast || error != u"MESSAGE_NOT_MODIFIED"_q) {
					finish();
				}
			},
			false);
	}

	void sendFirst() {
		_item = SendText(_target, Pre(_frames.front()));
		_index = 1;
		if (!_item) {
			finish();
			return;
		}
		_item->history()->owner().itemRemoved(
		) | rpl::filter([=](not_null<const HistoryItem*> item) {
			return (item.get() == _item);
		}) | rpl::on_next([=] {
			_item = nullptr;
			finish();
		}, _lifetime);
	}

	const std::vector<QString> _frames;
	HistoryItem *_item = nullptr;
	int _index = 0;
	bool _editing = false;
	rpl::lifetime _lifetime;

};

// ------------------------------------------------------------------ .dox

[[nodiscard]] QString DoxText(bool eng) {
	return eng
		? u"IP: 192.168.1.1\n"
			"DNS: da ya sam hz\n"
			"NETMASK: 255.255.255.0\n"
			"PROVIDER: MUHOSRANSK TELECOM"_q
		: u"IP: 192.168.1.1\n"
			"DNS: da ya sam hz\n"
			"МАСКА ПОДСЕТИ: 255.255.255.0\n"
			"ПРОВАЙДЕР: МУХОСРАНСК ТЕЛЕКОМ"_q;
}

// ----------------------------------------------------------------- .calc

class Calculator final {
public:
	explicit Calculator(const QString &text) : _text(text) {
	}

	[[nodiscard]] std::optional<double> evaluate() {
		const auto result = expression();
		skipSpaces();
		if (!_ok || _index != _text.size() || !std::isfinite(result)) {
			return std::nullopt;
		}
		return result;
	}

private:
	void skipSpaces() {
		while (_index < _text.size() && _text[_index].isSpace()) {
			++_index;
		}
	}
	bool take(char16_t c) {
		skipSpaces();
		if (_index < _text.size() && _text[_index] == QChar(c)) {
			++_index;
			return true;
		}
		return false;
	}
	bool takeAny(std::initializer_list<char16_t> list) {
		for (const auto c : list) {
			if (take(c)) {
				return true;
			}
		}
		return false;
	}
	double fail() {
		_ok = false;
		return 0.;
	}

	double expression() {
		if (++_depth > kCalcMaxDepth) {
			return fail();
		}
		auto result = term();
		while (_ok) {
			if (take(u'+')) {
				result += term();
			} else if (takeAny({ u'-', u'−' })) {
				result -= term();
			} else {
				break;
			}
		}
		--_depth;
		return result;
	}

	double term() {
		auto result = unary();
		while (_ok) {
			if (takeAny({ u'*', u'×', u'·' })) {
				result *= unary();
			} else if (takeAny({ u'/', u'÷', u':' })) {
				const auto divisor = unary();
				if (divisor == 0.) {
					return fail();
				}
				result /= divisor;
			} else if (take(u'%')) {
				const auto divisor = unary();
				if (divisor == 0.) {
					return fail();
				}
				result = std::fmod(result, divisor);
			} else {
				break;
			}
		}
		return result;
	}

	double unary() {
		if (++_depth > kCalcMaxDepth) {
			return fail();
		}
		auto result = 0.;
		if (takeAny({ u'-', u'−' })) {
			result = -unary();
		} else if (take(u'+')) {
			result = unary();
		} else {
			result = power();
		}
		--_depth;
		return result;
	}

	double power() {
		const auto base = primary();
		if (_ok && take(u'^')) {
			return std::pow(base, unary());
		}
		return base;
	}

	double primary() {
		if (take(u'(')) {
			const auto result = expression();
			return take(u')') ? result : fail();
		}
		skipSpaces();
		if (_index >= _text.size()) {
			return fail();
		}
		const auto c = _text[_index];
		if (c.isDigit() || c == '.' || c == ',') {
			return number();
		} else if (c.isLetter()) {
			return function();
		}
		return fail();
	}

	double number() {
		const auto start = _index;
		while (_index < _text.size()
			&& (_text[_index].isDigit()
				|| _text[_index] == '.'
				|| _text[_index] == ',')) {
			++_index;
		}
		auto ok = false;
		const auto result = _text.mid(start, _index - start)
			.replace(',', '.')
			.toDouble(&ok);
		return ok ? result : fail();
	}

	double function() {
		const auto start = _index;
		while (_index < _text.size() && _text[_index].isLetter()) {
			++_index;
		}
		const auto name = _text.mid(start, _index - start).toLower();
		if (name == u"pi"_q || name == u"пи"_q) {
			return std::acos(-1.);
		} else if (name == u"e"_q) {
			return std::exp(1.);
		}
		const auto x = primary();
		if (!_ok) {
			return 0.;
		} else if (name == u"sqrt"_q || name == u"корень"_q) {
			return (x < 0.) ? fail() : std::sqrt(x);
		} else if (name == u"abs"_q) {
			return std::abs(x);
		} else if (name == u"sin"_q) {
			return std::sin(x);
		} else if (name == u"cos"_q) {
			return std::cos(x);
		} else if (name == u"tan"_q || name == u"tg"_q) {
			return std::tan(x);
		} else if (name == u"ln"_q) {
			return (x <= 0.) ? fail() : std::log(x);
		} else if (name == u"log"_q || name == u"lg"_q) {
			return (x <= 0.) ? fail() : std::log10(x);
		} else if (name == u"round"_q) {
			return std::round(x);
		} else if (name == u"floor"_q) {
			return std::floor(x);
		} else if (name == u"ceil"_q) {
			return std::ceil(x);
		}
		return fail();
	}

	const QString _text;
	int _index = 0;
	int _depth = 0;
	bool _ok = true;

};

[[nodiscard]] QString FormatNumber(double value) {
	if (std::abs(value) < 1e15
		&& std::abs(value - std::round(value)) < 1e-9) {
		return QString::number(qint64(std::llround(value)));
	}
	return QString::number(value, 'g', 12);
}

// ----------------------------------------------------------------- .ddos

[[nodiscard]] QString Bar(int percent) {
	constexpr auto kWidth = 10;
	const auto filled = std::clamp(percent * kWidth / 100, 0, kWidth);
	return QString(filled, QChar(0x2593)) + QString(kWidth - filled, QChar(0x2591));
}

[[nodiscard]] std::vector<QString> DdosFrames(
		const QString &target,
		bool eng) {
	const auto locale = QLocale(QLocale::English);
	auto log = QStringList();
	auto frames = std::vector<QString>();
	const auto push = [&](const QString &line) {
		log.push_back(line);
		frames.push_back(log.join('\n'));
	};
	const auto replace = [&](const QString &line) {
		log.back() = line;
		frames.push_back(log.join('\n'));
	};
	// 203.0.113.0/24 is TEST-NET-3, reserved for documentation.
	const auto ip = u"203.0.113.%1"_q.arg(10 + base::RandomIndex(240));
	const auto bots = 2048 + base::RandomIndex(6000);
	const auto peak = 30. + base::RandomIndex(400) / 10.;
	const auto seconds = [](int frame) {
		return QString::number(frame * kDdosFrame / 1000., 'f', 1);
	};

	push(u"$ ./stress --target %1 --threads 512 --duration 30"_q
		.arg(target));
	push((eng
		? u"[*] Resolving %1…"_q
		: u"[*] Определение хоста %1…"_q).arg(target));
	push(u"[+] %1 → %2"_q.arg(target, ip));
	push(eng
		? u"[*] Port scan: 22/tcp, 80/tcp, 443/tcp open"_q
		: u"[*] Скан портов: открыты 22/tcp, 80/tcp, 443/tcp"_q);
	push((eng
		? u"[*] Connecting to C2… %1 bots online"_q
		: u"[*] Подключение к C2… онлайн %1 ботов"_q)
		.arg(locale.toString(bots)));
	push(eng
		? u"[*] Method: UDP flood + HTTP/2 Rapid Reset"_q
		: u"[*] Метод: UDP flood + HTTP/2 Rapid Reset"_q);

	auto packets = qint64(0);
	for (auto step = 1; step <= kDdosTrafficSteps; ++step) {
		const auto share = double(step) / kDdosTrafficSteps;
		const auto gbps = peak * share
			* (0.9 + base::RandomIndex(200) / 1000.);
		// ~1.49 Mpps per Gbps for minimum-size frames.
		const auto mpps = gbps * 1.49;
		packets += qint64(mpps * 1e6 * (kDdosFrame / 1000.));
		const auto percent = step * 100 / kDdosTrafficSteps;
		const auto line = (eng
			? u"[>] T+%1s | %2 Gbps | %3 Mpps | %4 %5%"_q
			: u"[>] T+%1с | %2 Гбит/с | %3 Mpps | %4 %5%"_q)
			.arg(seconds(step))
			.arg(QString::number(gbps, 'f', 1))
			.arg(QString::number(mpps, 'f', 1))
			.arg(Bar(percent))
			.arg(percent);
		if (step == 1) {
			push(line);
		} else {
			replace(line);
		}
	}
	push((eng
		? u"[!] Target latency: %1 ms, packet loss %2%"_q
		: u"[!] Пинг цели: %1 мс, потери %2%"_q)
		.arg(1800 + base::RandomIndex(1500))
		.arg(60 + base::RandomIndex(35)));
	push((eng
		? u"[+] %1 is DOWN (timeout 30000 ms)"_q
		: u"[+] %1 ЛЕЖИТ (таймаут 30000 мс)"_q).arg(target));
	push((eng
		? u"[✓] Finished in %1 s, packets sent: %2"_q
		: u"[✓] Готово за %1 с, отправлено пакетов: %2"_q)
		.arg(seconds(int(frames.size()) + 1))
		.arg(locale.toString(packets)));
	return frames;
}

// ---------------------------------------------------------------- .sumup

CommandResult Sumup(not_null<Ui::InputField*> field, bool eng) {
	const auto controller = ControllerFor(field);
	const auto target = TargetFor(field);
	const auto thread = Resolve(target);
	if (!controller || !thread) {
		ShowToast(field, eng
			? u"Open a chat first."_q
			: u"Сначала открой чат."_q);
		return CommandResult::Handled;
	} else if (const auto error = AiConfigurationError(); !error.isEmpty()) {
		ShowToast(field, error);
		return CommandResult::Handled;
	}
	const auto history = thread->owningHistory();
	auto lines = std::deque<QString>();
	for (auto b = history->blocks.rbegin(); b != history->blocks.rend(); ++b) {
		const auto &messages = (*b)->messages;
		for (auto m = messages.rbegin(); m != messages.rend(); ++m) {
			if (int(lines.size()) >= kSumupCount) {
				break;
			}
			const auto item = (*m)->data();
			if (item->isService()
				|| (target.topicRootId
					&& !item->inThread(target.topicRootId))) {
				continue;
			}
			auto text = item->notificationText().text.trimmed();
			if (text.isEmpty()) {
				continue;
			} else if (text.size() > kSumupMessageLimit) {
				text = text.left(kSumupMessageLimit) + QChar(0x2026);
			}
			const auto who = item->out()
				? (eng ? u"Me"_q : u"Я"_q)
				: item->from()->name();
			lines.push_front(who + u": "_q + text);
		}
		if (int(lines.size()) >= kSumupCount) {
			break;
		}
	}
	if (lines.empty()) {
		ShowToast(field, eng
			? u"No messages to sum up."_q
			: u"Нечего пересказывать."_q);
		return CommandResult::Handled;
	}
	auto transcript = QStringList();
	for (const auto &line : lines) {
		transcript.push_back(line);
	}
	const auto count = int(lines.size());
	const auto chat = history->peer->name();
	const auto shown = (eng
		? u"📋 Sum up the last %1 messages"_q
		: u"📋 Перескажи последние %1 сообщений"_q).arg(count);
	const auto prompt = (eng
		? u"Summarize these last %1 messages from the Telegram chat "
			"\"%2\" in English: the main topics and who said what. "
			"Be brief, plain text, no Markdown.\n\n%3"_q
		: u"Кратко перескажи на русском языке последние %1 сообщений "
			"из чата Telegram «%2»: главные темы и кто что говорил. "
			"Пиши простым текстом, без Markdown.\n\n%3"_q)
		.arg(count)
		.arg(chat, transcript.join('\n'));
	field->setTextWithTags({}, Ui::InputField::HistoryAction::NewEntry);
	if (!AskInAiPanel(controller, shown, prompt)) {
		ShowToast(field, eng
			? u"AI panel is not available in this window."_q
			: u"AI-панель недоступна в этом окне."_q);
	}
	return CommandResult::Handled;
}

// ----------------------------------------------------------------- .spam

CommandResult Spam(
		not_null<Ui::InputField*> field,
		const Target &target,
		const QString &args,
		bool eng) {
	static const auto parser = QRegularExpression(
		u"^(.+?)\\s+(\\d+)\\s+(\\d+(?:[.,]\\d+)?)$"_q,
		QRegularExpression::DotMatchesEverythingOption);
	const auto match = parser.match(args);
	if (!match.hasMatch()) {
		ShowToast(field, eng
			? u"Usage: .spameng <message> <count> <delay in seconds>"_q
			: u"Формат: .spam <сообщение> <кол-во> <задержка в секундах>"_q);
		return CommandResult::Handled;
	}
	const auto text = match.captured(1).trimmed();
	const auto wantedCount = match.captured(2).toInt();
	const auto wantedDelay = QString(match.captured(3))
		.replace(',', '.')
		.toDouble();
	const auto count = std::clamp(wantedCount, 1, kSpamMaxCount);
	const auto delay = std::max(wantedDelay, kSpamMinDelay);
	if (count != wantedCount || delay != wantedDelay) {
		ShowToast(field, (eng
			? u"Limited to %1 messages, at least %2 s apart "
				"(to avoid a Telegram flood ban)."_q
			: u"Ограничено: до %1 сообщений, не чаще раза в %2 с "
				"(чтобы Telegram не дал флуд-бан)."_q)
			.arg(kSpamMaxCount)
			.arg(kSpamMinDelay));
	}
	auto job = std::make_unique<SpamJob>(target, text, count);
	job->start(crl::time(std::llround(delay * 1000.)));
	Jobs.push_back(std::move(job));
	return CommandResult::SendAsUsual;
}

} // namespace

CommandResult InterceptChatCommand(not_null<Ui::InputField*> field) {
	const auto trimmed = field->getLastText().trimmed();
	if (!trimmed.startsWith('.')) {
		return CommandResult::NotCommand;
	}
	auto split = 0;
	while (split < trimmed.size() && !trimmed[split].isSpace()) {
		++split;
	}
	auto command = trimmed.left(split).toLower();
	const auto args = trimmed.mid(split).trimmed();
	const auto eng = command.endsWith(u"eng"_q);
	if (eng) {
		command.chop(3);
	}
	const auto known = QStringList{
		u".dox"_q,
		u".calc"_q,
		u".ddos"_q,
		u".spam"_q,
		u".sumup"_q,
		u".stop"_q,
	};
	if (!known.contains(command)) {
		return CommandResult::NotCommand;
	}

	if (command == u".sumup"_q) {
		return Sumup(field, eng);
	} else if (command == u".stop"_q) {
		const auto stopped = StopAllJobs();
		field->setTextWithTags({}, Ui::InputField::HistoryAction::NewEntry);
		ShowToast(field, (eng
			? u"Stopped: %1"_q
			: u"Остановлено: %1"_q).arg(stopped));
		return CommandResult::Handled;
	}

	const auto target = TargetFor(field);
	if (!Resolve(target)) {
		ShowToast(field, eng
			? u"Open a chat first."_q
			: u"Сначала открой чат."_q);
		return CommandResult::Handled;
	}

	if (command == u".dox"_q) {
		if (!args.isEmpty()) {
			return CommandResult::NotCommand;
		}
		SendTextDelayed(target, DoxText(eng), kDoxDelay);
		return CommandResult::SendAsUsual;
	} else if (command == u".calc"_q) {
		const auto result = Calculator(args).evaluate();
		if (args.isEmpty() || !result) {
			ShowToast(field, args.isEmpty()
				? (eng
					? u"Usage: .calceng 2+2*3"_q
					: u"Формат: .calc 2+2*3"_q)
				: (eng
					? u"Can't calculate that."_q
					: u"Не получилось посчитать."_q));
			return CommandResult::Handled;
		}
		SendTextDelayed(
			target,
			u"🧮 %1 = %2"_q.arg(args, FormatNumber(*result)),
			kReplyDelay);
		return CommandResult::SendAsUsual;
	} else if (command == u".ddos"_q) {
		const auto thread = Resolve(target);
		const auto victim = args.isEmpty()
			? thread->peer()->name()
			: args;
		auto job = std::make_unique<EditedFramesJob>(
			target,
			DdosFrames(victim, eng));
		job->start(kDdosFrame);
		Jobs.push_back(std::move(job));
		return CommandResult::SendAsUsual;
	} else if (command == u".spam"_q) {
		return Spam(field, target, args, eng);
	}
	return CommandResult::NotCommand;
}

} // namespace Clawd
