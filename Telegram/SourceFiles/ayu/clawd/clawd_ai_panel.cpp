// This is the source code of ClawdGram for Desktop,
// based on AyuGram for Desktop by Radolyn Labs.
#include "ayu/clawd/clawd_ai_panel.h"

#include "ayu/clawd/clawd_ai.h"
#include "ayu/clawd/clawd_settings.h"
#include "base/flat_map.h"
#include "ui/effects/animations.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

#include <QtGui/QKeySequence>
#include <QtGui/QShortcut>

namespace Clawd {
namespace {

constexpr auto kPanelWidth = 380;
constexpr auto kPadding = 12;
constexpr auto kBubblePadding = 10;
constexpr auto kBubbleRadius = 12;
constexpr auto kBubbleSkip = 8;

class AiPanel final : public Ui::RpWidget {
public:
	AiPanel(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller);

	void toggle();
	void setShown(bool shown);
	void ask(const QString &shown, const QString &prompt);

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	struct Bubble {
		not_null<Ui::RpWidget*> widget;
		not_null<Ui::FlatLabel*> label;
	};

	void setupControls();
	void updatePosition();
	void updateSubtitle();
	void send();
	void clear();
	Bubble addBubble(bool fromUser, const QString &text);

	const not_null<Ui::RpWidget*> _parent;
	const not_null<Window::SessionController*> _controller;

	object_ptr<Ui::FlatLabel> _title;
	object_ptr<Ui::FlatLabel> _subtitle;
	object_ptr<Ui::IconButton> _close;
	object_ptr<Ui::LinkButton> _clear;
	object_ptr<Ui::ScrollArea> _scroll;
	QPointer<Ui::VerticalLayout> _list;
	object_ptr<Ui::InputField> _input;
	object_ptr<Ui::RoundButton> _send;

	std::vector<AiMessage> _history;
	uint64 _requestId = 0;
	bool _waiting = false;

	Ui::Animations::Simple _shownAnimation;
	bool _shown = false;

};

AiPanel::AiPanel(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _parent(parent)
, _controller(controller)
, _title(this, u"ClawdGram AI"_q, st::boxTitle)
, _subtitle(this, QString(), st::boxDividerLabel)
, _close(this, st::boxTitleClose)
, _clear(this, u"Clear chat"_q)
, _scroll(this, st::defaultScrollArea)
, _input(
	this,
	st::newGroupDescription,
	Ui::InputField::Mode::MultiLine,
	rpl::single(u"Ask anything… (Enter to send)"_q),
	QString())
, _send(this, rpl::single(u"Send"_q), st::defaultActiveButton) {
	setupControls();
	hide();
}

void AiPanel::setupControls() {
	_list = _scroll->setOwnedWidget(object_ptr<Ui::VerticalLayout>(_scroll));
	_scroll->widthValue() | rpl::on_next([=](int width) {
		if (_list) {
			_list->resizeToWidth(width);
		}
	}, lifetime());
	_list->heightValue() | rpl::on_next([=](int height) {
		_scroll->scrollToY(height);
	}, lifetime());

	_send->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	_send->setClickedCallback([=] { send(); });
	_close->setClickedCallback([=] { setShown(false); });
	_clear->setClickedCallback([=] { clear(); });

	_input->setMaxHeight(style::ConvertScale(160));
	_input->setSubmitSettings(Ui::InputField::SubmitSettings::Enter);
	_input->submits() | rpl::on_next([=] { send(); }, _input->lifetime());
	_input->cancelled() | rpl::on_next([=] {
		setShown(false);
	}, _input->lifetime());
	_input->heightChanges() | rpl::on_next([=] {
		resizeEvent(nullptr);
	}, _input->lifetime());

	_parent->sizeValue() | rpl::on_next([=] {
		updatePosition();
	}, lifetime());

	_controller->activeChatValue() | rpl::on_next([=] {
		if (_shown) {
			raise();
		}
	}, lifetime());

	Changes() | rpl::on_next([=] {
		updateSubtitle();
	}, lifetime());
	updateSubtitle();

	addBubble(
		false,
		u"Hi! I'm your ClawdGram assistant. Ask me anything.\n\n"
		"Tips: type \".ai <prompt>\" in any chat to let me write a "
		"message, or press Ctrl+Shift+R in the message field to fix "
		"spelling."_q);
}

void AiPanel::updateSubtitle() {
	const auto error = AiConfigurationError();
	_subtitle->setText(error.isEmpty()
		? (ProviderName(Get().provider) + u" · "_q + CurrentModel())
		: error);
	resizeEvent(nullptr);
}

AiPanel::Bubble AiPanel::addBubble(bool fromUser, const QString &text) {
	const auto skip = style::ConvertScale(kBubbleSkip);
	const auto side = style::ConvertScale(kPadding);
	const auto inner = style::ConvertScale(kBubblePadding);
	const auto widget = _list->add(
		object_ptr<Ui::RpWidget>(_list),
		QMargins(
			fromUser ? side * 3 : side,
			skip,
			fromUser ? side : side * 3,
			0));
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		widget,
		text,
		st::boxLabel);
	label->setSelectable(true);
	label->setBreakEverywhere(true);
	label->move(inner, inner);
	// The bubble is already visible (VerticalLayout::add shows it), and
	// Qt keeps children created under a visible parent hidden.
	label->show();

	widget->widthValue() | rpl::on_next([=](int width) {
		label->resizeToWidth(std::max(width - 2 * inner, 1));
	}, widget->lifetime());
	label->heightValue() | rpl::on_next([=](int height) {
		widget->resize(widget->width(), height + 2 * inner);
	}, widget->lifetime());

	widget->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(widget);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(fromUser ? st::msgOutBg : st::windowBgOver);
		const auto radius = style::ConvertScale(kBubbleRadius);
		p.drawRoundedRect(widget->rect(), radius, radius);
	}, widget->lifetime());

	widget->show();
	return { widget, label };
}

void AiPanel::send() {
	const auto text = _input->getLastText().trimmed();
	if (text.isEmpty() || _waiting) {
		return;
	}
	_input->setTextWithTags({}, Ui::InputField::HistoryAction::Clear);
	ask(text, text);
}

void AiPanel::ask(const QString &shown, const QString &prompt) {
	if (_waiting) {
		// A new request replaces the one still in flight.
		++_requestId;
		_waiting = false;
		if (!_history.empty() && _history.back().fromUser) {
			_history.pop_back();
		}
	}
	addBubble(true, shown);
	_history.push_back({ .fromUser = true, .text = prompt });

	const auto pending = addBubble(false, u"Thinking…"_q);
	const auto label = QPointer<Ui::FlatLabel>(pending.label.get());
	const auto requestId = ++_requestId;
	_waiting = true;

	auto messages = _history;
	AskAi({
		.task = AiTask::Chat,
		.messages = std::move(messages),
	}, crl::guard(this, [=](QString reply) {
		if (requestId != _requestId) {
			return;
		}
		_waiting = false;
		_history.push_back({ .fromUser = false, .text = reply });
		if (label) {
			label->setText(reply);
		}
	}), crl::guard(this, [=](QString error) {
		if (requestId != _requestId) {
			return;
		}
		_waiting = false;
		_history.pop_back();
		if (label) {
			label->setText(u"⚠ "_q + error);
		}
	}));
}

void AiPanel::clear() {
	++_requestId;
	_waiting = false;
	_history.clear();
	if (_list) {
		_list->clear();
	}
}

void AiPanel::toggle() {
	setShown(!_shown);
}

void AiPanel::setShown(bool shown) {
	if (_shown == shown) {
		return;
	}
	_shown = shown;
	if (shown) {
		// The main menu and settings live in the layer stack above us.
		_controller->window().hideSettingsAndLayer();
		show();
		raise();
		_input->setFocusFast();
	}
	_shownAnimation.start(
		[=] { updatePosition(); },
		shown ? 0. : 1.,
		shown ? 1. : 0.,
		st::slideDuration,
		anim::easeOutCirc);
	updatePosition();
}

void AiPanel::updatePosition() {
	const auto full = style::ConvertScale(kPanelWidth);
	const auto width = std::min(full, _parent->width());
	const auto height = _parent->height();
	if (this->width() != width || this->height() != height) {
		resize(width, height);
	}
	const auto progress = _shownAnimation.value(_shown ? 1. : 0.);
	move(int(base::SafeRound(-width * (1. - progress))), 0);
	if (!_shown && !_shownAnimation.animating()) {
		hide();
	}
}

void AiPanel::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.fillRect(rect(), st::windowBg);
	p.fillRect(
		QRect(width() - st::lineWidth, 0, st::lineWidth, height()),
		st::shadowFg);
}

void AiPanel::resizeEvent(QResizeEvent *e) {
	const auto padding = style::ConvertScale(kPadding);
	const auto inner = width() - 2 * padding;
	if (inner <= 0) {
		return;
	}
	_close->moveToRight(0, 0);
	_title->resizeToWidth(inner - _close->width());
	_title->moveToLeft(padding, padding);
	_subtitle->resizeToWidth(inner);
	_subtitle->moveToLeft(padding, _title->y() + _title->height());
	_clear->moveToLeft(
		padding,
		_subtitle->y() + _subtitle->height() + padding / 2);

	const auto top = _clear->y() + _clear->height() + padding / 2;
	_send->moveToRight(padding, height() - padding - _send->height());
	_input->resize(inner, _input->height());
	_input->moveToLeft(
		padding,
		_send->y() - padding / 2 - _input->height());
	const auto bottom = _input->y() - padding / 2;
	_scroll->setGeometry(0, top, width() - st::lineWidth, bottom - top);
}

base::flat_map<
	not_null<Window::SessionController*>,
	QPointer<AiPanel>> Panels;

} // namespace

void SetupAiPanel(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller) {
	const auto panel = Ui::CreateChild<AiPanel>(parent.get(), controller);
	Panels[controller] = panel;
	panel->lifetime().add([=] {
		Panels.remove(controller);
	});

	const auto shortcut = new QShortcut(
		QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A),
		parent.get());
	shortcut->setContext(Qt::WindowShortcut);
	QObject::connect(shortcut, &QShortcut::activated, panel, [=] {
		panel->toggle();
	});
}

bool AskInAiPanel(
		not_null<Window::SessionController*> controller,
		const QString &shown,
		const QString &prompt) {
	const auto i = Panels.find(controller);
	if (i == end(Panels) || !i->second) {
		return false;
	}
	i->second->setShown(true);
	i->second->ask(shown, prompt);
	return true;
}

void ToggleAiPanel(not_null<Window::SessionController*> controller) {
	const auto i = Panels.find(controller);
	if (i != end(Panels) && i->second) {
		i->second->toggle();
	}
}

} // namespace Clawd
