#pragma once

namespace gta4::presentation {
// Called after native.toml is loaded, before guest execution starts.
void InitializeOptions();
bool SkipIntroAtLaunch() noexcept;
bool MotionBlurEnabled() noexcept;
bool DisableTladFilmGrain() noexcept;
bool TraceEnabled() noexcept;
// True while the intro logo screens run (touch overlay shows its skip button).
void PublishIntroScreens(bool active) noexcept;
bool IntroScreensActive() noexcept;
}  // namespace gta4::presentation
