// This is the source code of ClawdGram for Desktop,
// based on AyuGram for Desktop by Radolyn Labs.
#pragma once

namespace Clawd {

[[nodiscard]] bool IsHyprland();

// Applies window opacity / blur from the settings and keeps them applied
// to windows that are opened later.
void StartWindowEffects();
void ApplyWindowEffects();

} // namespace Clawd
