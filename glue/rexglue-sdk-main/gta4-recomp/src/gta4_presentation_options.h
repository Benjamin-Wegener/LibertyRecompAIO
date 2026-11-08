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
// Touch SKIP on the intro logos: requested on the UI thread, applied on the guest thread.
void RequestIntroSkip() noexcept;
bool ConsumeIntroSkipRequest() noexcept;
// Guest thread: end the remaining intro screens now (zero their durations).
void SkipIntroNow(unsigned char* base) noexcept;
}  // namespace gta4::presentation
