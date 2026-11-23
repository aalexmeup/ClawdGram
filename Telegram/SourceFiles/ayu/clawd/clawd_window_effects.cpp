// This is the source code of ClawdGram for Desktop,
// based on AyuGram for Desktop by Radolyn Labs.
#include "ayu/clawd/clawd_window_effects.h"

#include "ayu/clawd/clawd_settings.h"
#include "base/timer.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QProcess>
#include <QtGui/QGuiApplication>
#include <QtWidgets/QApplication>

namespace Clawd {
namespace {

constexpr auto kDebounce = crl::time(250);

bool AppliedOnce = false;
int AppliedBlurSize = 0;

[[nodiscard]] QString OpacityValue(int percent) {
	return QString::number(percent / 100., 'f', 2) + u" override"_q;
}

void RunHyprctl(QStringList arguments, Fn<void()> failed = nullptr) {
	const auto process = new QProcess(QCoreApplication::instance());
	QObject::connect(
		process,
		&QProcess::finished,
		[=](int code, QProcess::ExitStatus status) {
			process->deleteLater();
			if ((code != 0 || status != QProcess::NormalExit) && failed) {
				failed();
			}
		});
	QObject::connect(process, &QProcess::errorOccurred, [=] {
		process->deleteLater();
	});
	process->start(u"hyprctl"_q, arguments);
}

void ApplyLegacyHyprland(const QString &opacity, bool noBlur) {
	const auto window = u"pid:%1"_q.arg(QCoreApplication::applicationPid());
	RunHyprctl({
		u"dispatch"_q,
		u"setprop"_q,
		window,
		u"alpha"_q,
		opacity,
	});
	RunHyprctl({
		u"dispatch"_q,
		u"setprop"_q,
		window,
		u"noblur"_q,
		noBlur ? u"1"_q : u"0"_q,
	});
}

void ApplyHyprland(const Values &values) {
	const auto opacity = values.windowEffects
		? OpacityValue(values.windowOpacity)
		: u"1.0"_q;
	const auto noBlur = values.windowEffects && !values.windowBlur;
	const auto pid = QCoreApplication::applicationPid();
	const auto script = u"for _, w in ipairs(hl.get_windows()) do "
		"if w.pid == %1 then "
		"hl.dispatch(hl.dsp.window.set_prop({ prop = \"opacity\", "
		"value = \"%2\", window = w })) "
		"hl.dispatch(hl.dsp.window.set_prop({ prop = \"no_blur\", "
		"value = \"%3\", window = w })) "
		"end end"_q.arg(pid).arg(opacity).arg(noBlur ? 1 : 0);
	RunHyprctl({ u"eval"_q, script }, [=] {
		ApplyLegacyHyprland(opacity, noBlur);
	});

	const auto blurSize = values.windowEffects ? values.blurSize : 0;
	if (blurSize > 0 && blurSize != AppliedBlurSize) {
		AppliedBlurSize = blurSize;
		RunHyprctl({
			u"eval"_q,
			u"hl.config({ decoration = { blur = { size = %1 } } })"_q
				.arg(blurSize),
		}, [=] {
			RunHyprctl({
				u"keyword"_q,
				u"decoration:blur:size"_q,
				QString::number(blurSize),
			});
		});
	}
}

void ApplyQt(const Values &values) {
	const auto opacity = values.windowEffects
		? (values.windowOpacity / 100.)
		: 1.;
	for (const auto widget : QApplication::topLevelWidgets()) {
		if (widget->isWindow() && widget->isVisible()) {
			widget->setWindowOpacity(opacity);
		}
	}
}

[[nodiscard]] base::Timer &DebounceTimer() {
	static const auto result = new base::Timer([] { ApplyWindowEffects(); });
	return *result;
}

} // namespace

bool IsHyprland() {
	return qEnvironmentVariableIsSet("HYPRLAND_INSTANCE_SIGNATURE");
}

void ApplyWindowEffects() {
	const auto &values = Get();
	if (!values.windowEffects && !AppliedOnce) {
		return;
	}
	AppliedOnce = true;
	if (IsHyprland()) {
		ApplyHyprland(values);
	} else {
		ApplyQt(values);
	}
}

void StartWindowEffects() {
	static auto started = false;
	static auto lifetime = rpl::lifetime();
	if (started) {
		return;
	}
	started = true;
	Changes() | rpl::on_next([] {
		DebounceTimer().callOnce(kDebounce);
	}, lifetime);
	QObject::connect(
		qApp,
		&QGuiApplication::focusWindowChanged,
		[] {
			if (Get().windowEffects) {
				DebounceTimer().callOnce(kDebounce);
			}
		});
	DebounceTimer().callOnce(crl::time(1500));
}

} // namespace Clawd
