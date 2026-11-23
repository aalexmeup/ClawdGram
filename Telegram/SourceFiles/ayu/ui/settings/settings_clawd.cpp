// This is the source code of ClawdGram for Desktop,
// based on AyuGram for Desktop by Radolyn Labs.
#include "ayu/ui/settings/settings_clawd.h"

#include "ayu/clawd/clawd_ai_panel.h"
#include "ayu/clawd/clawd_settings.h"
#include "ayu/clawd/clawd_window_effects.h"
#include "ayu/ui/settings/ayu_builder.h"
#include "ayu/ui/settings/settings_ayu_utils.h"
#include "ayu/ui/settings/settings_main.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

namespace Settings {

using namespace Builder;
using namespace AyuBuilder;

namespace {

struct TextFieldArgs {
	QString id;
	QString placeholder;
	Fn<QString(const Clawd::Values&)> getter;
	Fn<void(Clawd::Values&, QString)> setter;
	bool secret = false;
};

void AddTextField(SectionBuilder &builder, TextFieldArgs &&args) {
	const auto getter = std::move(args.getter);
	const auto setter = std::move(args.setter);
	const auto secret = args.secret;
	const auto placeholder = args.placeholder;
	const auto save = [=](const QString &value) {
		if (getter(Clawd::Get()) != value) {
			Clawd::Update([&](Clawd::Values &values) {
				setter(values, value);
			});
		}
	};
	builder.addControl({
		.factory = [=](not_null<Ui::VerticalLayout*> container)
		-> object_ptr<Ui::RpWidget> {
			const auto initial = getter(Clawd::Get());
			if (secret) {
				auto wrap = object_ptr<Ui::RpWidget>(container);
				const auto raw = Ui::CreateChild<Ui::PasswordInput>(
					wrap.data(),
					st::defaultInputField,
					rpl::single(placeholder),
					initial);
				QObject::connect(raw, &Ui::MaskedInputField::changed, [=] {
					save(raw->getLastText().trimmed());
				});
				wrap->resize(raw->width(), raw->height());
				wrap->widthValue() | rpl::on_next([=](int width) {
					raw->resize(width, raw->height());
				}, raw->lifetime());
				return wrap;
			}
			auto result = object_ptr<Ui::InputField>(
				container,
				st::defaultInputField,
				rpl::single(placeholder),
				initial);
			const auto raw = result.data();
			raw->changes() | rpl::on_next([=] {
				save(raw->getLastText().trimmed());
			}, raw->lifetime());
			return object_ptr<Ui::RpWidget>::fromRaw(result.release());
		},
		.id = std::move(args.id),
		.title = rpl::single(placeholder),
		.margin = style::margins(
			st::boxRowPadding.left(),
			0,
			st::boxRowPadding.right(),
			st::boxRowPadding.left() / 2),
	});
}

void AddClawdToggle(
		AyuSectionBuilder &ayu,
		QString id,
		QString title,
		Fn<bool(const Clawd::Values&)> getter,
		Fn<void(Clawd::Values&, bool)> setter,
		IconDescriptor icon = {}) {
	ayu.addToggle({
		.id = std::move(id),
		.title = rpl::single(std::move(title)),
		.getter = [=] { return getter(Clawd::Get()); },
		.setter = [=](bool value) {
			Clawd::Update([&](Clawd::Values &values) {
				setter(values, value);
			});
		},
		.icon = std::move(icon),
	});
}

void BuildAi(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	const auto controller = builder.controller();

	builder.addSkip();
	builder.addSubsectionTitle(rpl::single(u"AI assistant"_q));

	ayu.addChooseButton({
		.id = u"clawd/provider"_q,
		.title = rpl::single(u"AI provider"_q),
		.boxTitle = rpl::single(u"AI provider"_q),
		.initialSelection = int(Clawd::Get().provider),
		.options = {
			Clawd::ProviderName(Clawd::AiProvider::Claude),
			Clawd::ProviderName(Clawd::AiProvider::Gemini),
		},
		.setter = [=](int index) {
			Clawd::Update([&](Clawd::Values &values) {
				values.provider = Clawd::AiProvider(index);
			});
		},
		.icon = { &st::menuIconPremium },
	});
	builder.addSkip();

	AddTextField(builder, {
		.id = u"clawd/claudeKey"_q,
		.placeholder = u"Claude API key"_q,
		.getter = [](const Clawd::Values &v) { return v.claudeKey; },
		.setter = [](Clawd::Values &v, QString s) { v.claudeKey = s; },
		.secret = true,
	});
	AddTextField(builder, {
		.id = u"clawd/claudeModel"_q,
		.placeholder = u"Claude model"_q,
		.getter = [](const Clawd::Values &v) { return v.claudeModel; },
		.setter = [](Clawd::Values &v, QString s) { v.claudeModel = s; },
	});
	AddTextField(builder, {
		.id = u"clawd/geminiKey"_q,
		.placeholder = u"Gemini API key"_q,
		.getter = [](const Clawd::Values &v) { return v.geminiKey; },
		.setter = [](Clawd::Values &v, QString s) { v.geminiKey = s; },
		.secret = true,
	});
	AddTextField(builder, {
		.id = u"clawd/geminiModel"_q,
		.placeholder = u"Gemini model"_q,
		.getter = [](const Clawd::Values &v) { return v.geminiModel; },
		.setter = [](Clawd::Values &v, QString s) { v.geminiModel = s; },
	});
	AddTextField(builder, {
		.id = u"clawd/persona"_q,
		.placeholder = u"Extra instructions for the AI (optional)"_q,
		.getter = [](const Clawd::Values &v) { return v.persona; },
		.setter = [](Clawd::Values &v, QString s) { v.persona = s; },
	});
	builder.addSkip();
	builder.addDividerText(rpl::single(
		u"Keys are stored only on this computer (tdata/clawd_settings.json) "
		"and are sent only to the provider you choose."_q));
	builder.addSkip();

	builder.addButton({
		.id = u"clawd/openPanel"_q,
		.title = rpl::single(u"Open AI panel"_q),
		.icon = { &st::menuIconChatBubble },
		.label = rpl::single(u"Ctrl+Shift+A"_q),
		.onClick = [=] {
			controller->hideLayer();
			Clawd::ToggleAiPanel(controller);
		},
	});
	AddClawdToggle(
		ayu,
		u"clawd/aiCommand"_q,
		u"\".ai <prompt>\" command in chats"_q,
		[](const Clawd::Values &v) { return v.aiCommand; },
		[](Clawd::Values &v, bool b) { v.aiCommand = b; });
	AddClawdToggle(
		ayu,
		u"clawd/aiCommandAutoSend"_q,
		u"Send .ai replies automatically"_q,
		[](const Clawd::Values &v) { return v.aiCommandAutoSend; },
		[](Clawd::Values &v, bool b) { v.aiCommandAutoSend = b; });
	AddClawdToggle(
		ayu,
		u"clawd/autocorrect"_q,
		u"AI autocorrect (Ctrl+Shift+R)"_q,
		[](const Clawd::Values &v) { return v.autocorrect; },
		[](Clawd::Values &v, bool b) { v.autocorrect = b; });
	builder.addSkip();
	builder.addDividerText(rpl::single(
		u"Type \".ai write a polite reply\" in a chat and press Enter: "
		"the AI answer replaces your text so you can check it before "
		"sending. Ctrl+Shift+R fixes spelling in the message field."_q));
}

void BuildWindow(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	builder.addSkip();
	builder.addSubsectionTitle(rpl::single(u"Window"_q));

	AddClawdToggle(
		ayu,
		u"clawd/windowEffects"_q,
		u"Custom opacity and blur"_q,
		[](const Clawd::Values &v) { return v.windowEffects; },
		[](Clawd::Values &v, bool b) { v.windowEffects = b; },
		{ &st::menuIconPalette });

	const auto setOpacity = [](int value) {
		Clawd::Update([&](Clawd::Values &values) {
			values.windowOpacity = value;
		});
	};
	ayu.addSlider({
		.id = u"clawd/windowOpacity"_q,
		.title = rpl::single(u"Window opacity"_q),
		.steps = 81,
		.current = Clawd::Get().windowOpacity,
		.indexToValue = [](int index) { return 20 + index; },
		.onChanged = setOpacity,
		.onFinalChanged = setOpacity,
		.formatLabel = [](int value) {
			return QString::number(value) + '%';
		},
	});

	AddClawdToggle(
		ayu,
		u"clawd/windowBlur"_q,
		u"Blur behind the window"_q,
		[](const Clawd::Values &v) { return v.windowBlur; },
		[](Clawd::Values &v, bool b) { v.windowBlur = b; });

	const auto setBlur = [](int value) {
		Clawd::Update([&](Clawd::Values &values) {
			values.blurSize = value;
		});
	};
	ayu.addSlider({
		.id = u"clawd/blurSize"_q,
		.title = rpl::single(u"Blur strength"_q),
		.steps = 21,
		.current = Clawd::Get().blurSize,
		.onChanged = nullptr,
		.onFinalChanged = setBlur,
		.formatLabel = [](int value) {
			return value ? QString::number(value) : u"Auto"_q;
		},
	});
	builder.addSkip();
	builder.addDividerText(rpl::single(Clawd::IsHyprland()
		? u"Applied through hyprctl. Blur strength changes Hyprland's "
			"blur size for all windows until Hyprland reloads; "
			"\"Auto\" keeps your config."_q
		: u"Blur needs Hyprland. On other systems only opacity is "
			"applied."_q));
}

void BuildFont(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	const auto controller = builder.controller();

	builder.addSkip();
	builder.addSubsectionTitle(rpl::single(u"Font"_q));
	ayu.addToggle({
		.id = u"clawd/useRubik"_q,
		.title = rpl::single(u"Use Rubik font"_q),
		.getter = [] { return Clawd::Get().useRubik; },
		.setter = [=](bool value) {
			Clawd::Update([&](Clawd::Values &values) {
				values.useRubik = value;
			});
			ShowRestartPrompt(controller);
		},
	});
	builder.addSkip();
	builder.addDividerText(rpl::single(
		u"Rubik replaces the font chosen in Chat Settings. "
		"Needs a restart."_q));
}

void BuildSpoof(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	builder.addSkip();
	builder.addSubsectionTitle(rpl::single(u"Fake activity"_q));

	AddClawdToggle(
		ayu,
		u"clawd/spoof"_q,
		u"Show fake activity in the open chat"_q,
		[](const Clawd::Values &v) { return v.spoofEnabled; },
		[](Clawd::Values &v, bool b) { v.spoofEnabled = b; },
		{ &st::menuIconFave });
	ayu.addChooseButton({
		.id = u"clawd/spoofAction"_q,
		.title = rpl::single(u"Activity"_q),
		.boxTitle = rpl::single(u"Fake activity"_q),
		.initialSelection = int(Clawd::Get().spoofAction),
		.options = {
			u"Sending a file"_q,
			u"Sending a photo"_q,
			u"Sending a video"_q,
			u"Recording a voice message"_q,
			u"Recording a video message"_q,
			u"Typing"_q,
		},
		.setter = [=](int index) {
			Clawd::Update([&](Clawd::Values &values) {
				values.spoofAction = Clawd::SpoofAction(index);
			});
		},
	});
	builder.addSkip();
	builder.addDividerText(rpl::single(
		u"While on, the person in the chat you have open sees you "
		"sending a file (or the activity you pick), although you "
		"send nothing. Works even with Ghost Mode on."_q));
	builder.addSkip();
}

const auto kMeta = BuildHelper({
	.id = AyuClawd::Id(),
	.parentId = AyuMain::Id(),
	.title = u"ClawdGram Extras"_q,
	.icon = &st::menuIconPremium,
}, [](SectionBuilder &builder) {
	auto ayu = AyuSectionBuilder(builder);

	BuildAi(builder, ayu);
	ayu.addSectionDivider();
	BuildWindow(builder, ayu);
	ayu.addSectionDivider();
	BuildFont(builder, ayu);
	ayu.addSectionDivider();
	BuildSpoof(builder, ayu);
});

} // namespace

rpl::producer<QString> AyuClawd::title() {
	return rpl::single(u"ClawdGram Extras"_q);
}

AyuClawd::AyuClawd(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void AyuClawd::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type AyuClawdId() {
	return AyuClawd::Id();
}

} // namespace Settings
