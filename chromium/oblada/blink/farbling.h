#ifndef OBLADA_BLINK_FARBLING_H_
#define OBLADA_BLINK_FARBLING_H_

#include <cstdint>
#include <string_view>

#include "base/containers/span.h"
#include "third_party/blink/renderer/core/core_export.h"

namespace blink {

class ExecutionContext;
class String;

// The Barrier's fingerprinting protection in Blink ("farbling", as Brave calls
// it; oblada/browser/barrier/farbling.h): what pages could tell devices apart
// by is changed, the same way for a site through a session (from the seed its
// execution contexts have, farbling_settings.h), as Brave's standard
// protection does:
// - canvas readouts get faint noise (getImageData(), toDataURL(), toBlob());
// - audio samples are scaled by a hair (AudioBuffer, AnalyserNode);
// - WebGL's and WebGPU's GPU names are generic;
// - the processor's cores and the memory are fewer, up to the real ones;
// - the screen is the window's size, plus a few pixels, and the window at the
//   screen's corner, give or take a few;
// - with fewer languages, fonts are those of every Mac and of the language,
//   and a few others now and then;
// - cameras and microphones are listed in another order; speech synthesis has
//   one voice more, under a made-up name; USB devices have made-up serial
//   numbers; the keyboard's layout isn't given; the battery is always full;
// - a site's WebSockets and EventSource connections are limited
//   (connection_pools.h).
// (navigator.plugins needs nothing: Chromium lists the same PDF viewers
// everywhere; nor does navigator.storage.estimate(), whose quota is the site's
// usage plus 10 GB for everyone, kStaticStorageQuota.)
// Each is a hook in Blink's own code, which asks here.
namespace oblada_farbling {

// Bits of ObladaFarblingSettings::exceptions, as barrier.h's
// FingerprintingApi.
enum Api : uint32_t {
  kCanvas = 1u << 0,
  kAudio = 1u << 1,
  kWebGl = 1u << 2,
  kWebGpu = 1u << 3,
  kFonts = 1u << 4,
  kScreen = 1u << 5,
  kHardware = 1u << 6,
  kPlugins = 1u << 7,
  kMediaDevices = 1u << 8,
  kSpeechSynthesis = 1u << 9,
  kKeyboard = 1u << 10,
  kLanguages = 1u << 11,
  kUsb = 1u << 12,
  kConnectionPools = 1u << 13,
};

// Whether `context` changes `api`.
CORE_EXPORT bool IsOn(const ExecutionContext* context, Api api);
// Whether `context` sees only the first language (and fewer fonts).
CORE_EXPORT bool ReducesLanguages(const ExecutionContext* context);

// A pseudo-random number for `label`, the same for the context's site through
// the session.
CORE_EXPORT uint64_t Random(const ExecutionContext* context,
                            std::string_view label);

// Adds faint noise to an RGBA (or BGRA) image, `width` by `height` pixels:
// the same for the same image on the same site.
CORE_EXPORT void NoiseImage(const ExecutionContext* context,
                            base::span<uint8_t> pixels,
                            int width,
                            int height);

// Scales audio samples by a hair.
CORE_EXPORT void FarbleAudio(const ExecutionContext* context,
                             base::span<float> samples);

// What the context gets for navigator.hardwareConcurrency and deviceMemory.
CORE_EXPORT unsigned HardwareConcurrency(const ExecutionContext* context,
                                         unsigned real);
CORE_EXPORT float DeviceMemory(const ExecutionContext* context, float real);

// A few pixels (0 to 8) for `label`, which the screen's size and the window's
// position get.
CORE_EXPORT int ScreenOffset(const ExecutionContext* context,
                             std::string_view label);

// Whether the context may use the installed font `family` (not web fonts,
// which are always there).
CORE_EXPORT bool AllowsFont(const ExecutionContext* context,
                            const String& family);

// The made-up name of the voice speech synthesis gets in addition to the
// real ones (a copy of one of them), or an empty string when the context
// doesn't change it.
CORE_EXPORT String ExtraVoiceName(const ExecutionContext* context);

// What the context gets for a USB device's serial number `real`.
CORE_EXPORT String UsbSerialNumber(const ExecutionContext* context,
                                   const String& real);

}  // namespace oblada_farbling

}  // namespace blink

#endif  // OBLADA_BLINK_FARBLING_H_
