// This is the source code of ClawdGram for Desktop,
// based on AyuGram for Desktop by Radolyn Labs.
#pragma once

#include "settings/settings_common.h"
#include "settings/settings_common_session.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {

class AyuClawd : public Section<AyuClawd> {
public:
	AyuClawd(QWidget *parent, not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();
};

[[nodiscard]] Type AyuClawdId();

} // namespace Settings
