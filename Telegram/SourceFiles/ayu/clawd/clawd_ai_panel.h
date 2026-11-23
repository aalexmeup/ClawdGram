// This is the source code of ClawdGram for Desktop,
// based on AyuGram for Desktop by Radolyn Labs.
#pragma once

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Clawd {

// Creates the AI side panel that slides in from the left edge of parent.
// Ctrl+Shift+A toggles it.
void SetupAiPanel(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::SessionController*> controller);

// Opens the panel and sends prompt to the AI, showing `shown` as the
// user's bubble. Returns false if this window has no panel.
bool AskInAiPanel(
	not_null<Window::SessionController*> controller,
	const QString &shown,
	const QString &prompt);

void ToggleAiPanel(not_null<Window::SessionController*> controller);

} // namespace Clawd
