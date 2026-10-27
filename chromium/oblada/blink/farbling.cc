#include "oblada/blink/farbling.h"

#include <algorithm>
#include <string>

#include "base/containers/fixed_flat_set.h"
#include "third_party/blink/public/common/fingerprinting_protection/noise_token.h"
#include "third_party/blink/renderer/core/canvas_interventions/noise_hash.h"
#include "third_party/blink/renderer/core/canvas_interventions/noise_helper.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

namespace blink::oblada_farbling {

namespace {

const ObladaFarblingSettings* SettingsOf(const ExecutionContext* context) {
  return context ? &context->GetObladaFarbling() : nullptr;
}

uint64_t SplitMix64(uint64_t x) {
  x += 0x9e3779b97f4a7c15ull;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
  return x ^ (x >> 31);
}

uint64_t Fnv1a(std::string_view text) {
  uint64_t hash = 0xcbf29ce484222325ull;
  for (char c : text) {
    hash = (hash ^ static_cast<uint8_t>(c)) * 0x100000001b3ull;
  }
  return hash;
}

// The fonts every Mac has, and the generic families, which pages always
// get (as Brave's list of macOS's default fonts).
constexpr auto kMacFonts = base::MakeFixedFlatSet<std::string_view>({
    "-apple-system",
    ".applesystemuifont",
    "academy engraved let",
    "al bayan",
    "al nile",
    "al tarikh",
    "american typewriter",
    "andale mono",
    "apple braille",
    "apple chancery",
    "apple color emoji",
    "apple sd gothic neo",
    "apple symbols",
    "applegothic",
    "applemyungjo",
    "arial",
    "arial black",
    "arial hebrew",
    "arial hebrew scholar",
    "arial narrow",
    "arial rounded mt bold",
    "arial unicode ms",
    "avenir",
    "avenir next",
    "avenir next condensed",
    "ayuthaya",
    "baghdad",
    "bangla mn",
    "bangla sangam mn",
    "baskerville",
    "beirut",
    "big caslon",
    "blinkmacsystemfont",
    "bodoni 72",
    "bodoni 72 oldstyle",
    "bodoni 72 smallcaps",
    "bodoni ornaments",
    "bradley hand",
    "brush script mt",
    "chalkboard",
    "chalkboard se",
    "chalkduster",
    "charter",
    "cochin",
    "comic sans ms",
    "copperplate",
    "corsiva hebrew",
    "courier",
    "courier new",
    "cursive",
    "damascus",
    "decotype naskh",
    "devanagari mt",
    "devanagari sangam mn",
    "didot",
    "din alternate",
    "din condensed",
    "diwan kufi",
    "diwan thuluth",
    "emoji",
    "euphemia ucas",
    "fangsong",
    "fantasy",
    "farah",
    "farisi",
    "futura",
    "galvji",
    "geeza pro",
    "geneva",
    "georgia",
    "gill sans",
    "grantha sangam mn",
    "gujarati mt",
    "gujarati sangam mn",
    "gurmukhi mn",
    "gurmukhi mt",
    "gurmukhi sangam mn",
    "heiti sc",
    "heiti tc",
    "helvetica",
    "helvetica neue",
    "herculanum",
    "hiragino kaku gothic pro",
    "hiragino kaku gothic pron",
    "hiragino maru gothic pro",
    "hiragino maru gothic pron",
    "hiragino mincho pro",
    "hiragino mincho pron",
    "hiragino sans",
    "hiragino sans gb",
    "hoefler text",
    "impact",
    "inaimathi",
    "itf devanagari",
    "kailasa",
    "kannada mn",
    "kannada sangam mn",
    "kefa",
    "khmer mn",
    "khmer sangam mn",
    "kohinoor bangla",
    "kohinoor devanagari",
    "kohinoor telugu",
    "kokonor",
    "krungthep",
    "kufistandardgk",
    "lao mn",
    "lao sangam mn",
    "lucida grande",
    "luminari",
    "malayalam mn",
    "malayalam sangam mn",
    "marker felt",
    "math",
    "menlo",
    "microsoft sans serif",
    "mishafi",
    "monaco",
    "monospace",
    "mshtakan",
    "muna",
    "myanmar mn",
    "myanmar sangam mn",
    "nadeem",
    "new peninim mt",
    "noteworthy",
    "noto nastaliq urdu",
    "noto sans kannada",
    "noto sans myanmar",
    "noto sans oriya",
    "noto serif myanmar",
    "optima",
    "oriya mn",
    "oriya sangam mn",
    "palatino",
    "papyrus",
    "phosphate",
    "pingfang hk",
    "pingfang sc",
    "pingfang tc",
    "plantagenet cherokee",
    "pt mono",
    "pt sans",
    "pt sans caption",
    "pt sans narrow",
    "pt serif",
    "pt serif caption",
    "raanana",
    "rockwell",
    "sana",
    "sans-serif",
    "sathu",
    "savoye let",
    "serif",
    "shree devanagari 714",
    "signpainter",
    "silom",
    "sinhala mn",
    "sinhala sangam mn",
    "skia",
    "snell roundhand",
    "songti sc",
    "songti tc",
    "stheiti",
    "stixgeneral",
    "stixintegralsd",
    "stixsizeonesym",
    "sukhumvit set",
    "symbol",
    "system-ui",
    "tahoma",
    "tamil mn",
    "tamil sangam mn",
    "telugu mn",
    "telugu sangam mn",
    "thonburi",
    "times",
    "times new roman",
    "trattatello",
    "trebuchet ms",
    "ui-monospace",
    "ui-rounded",
    "ui-sans-serif",
    "ui-serif",
    "verdana",
    "waseem",
    "webdings",
    "wingdings",
    "wingdings 2",
    "wingdings 3",
    "zapf dingbats",
    "zapfino",
});

}  // namespace

bool IsOn(const ExecutionContext* context, Api api) {
  const ObladaFarblingSettings* settings = SettingsOf(context);
  return settings && settings->token && !(settings->exceptions & api);
}

bool ReducesLanguages(const ExecutionContext* context) {
  const ObladaFarblingSettings* settings = SettingsOf(context);
  return settings && settings->reduce_language && IsOn(context, kLanguages);
}

uint64_t Random(const ExecutionContext* context, std::string_view label) {
  const ObladaFarblingSettings* settings = SettingsOf(context);
  return SplitMix64((settings ? settings->token : 0) ^ Fnv1a(label));
}

void NoiseImage(const ExecutionContext* context,
                base::span<uint8_t> pixels,
                int width,
                int height) {
  if (!IsOn(context, kCanvas) || width <= 0 || height <= 0 ||
      pixels.size() < static_cast<size_t>(width) * height * 4) {
    return;
  }
  NoisePixels(NoiseHash(NoiseToken(Random(context, "canvas"))), pixels, width,
              height);
}

void FarbleAudio(const ExecutionContext* context, base::span<float> samples) {
  if (!IsOn(context, kAudio)) {
    return;
  }
  const float factor =
      0.999f + 0.001f * static_cast<float>(Random(context, "audio") % 1000) /
                   1000.0f;
  for (float& sample : samples) {
    sample *= factor;
  }
}

unsigned HardwareConcurrency(const ExecutionContext* context, unsigned real) {
  // Between 4 and the real number, as Brave (the real one below 4).
  if (!IsOn(context, kHardware) || real <= 4) {
    return real;
  }
  return 4 + Random(context, "hardware-concurrency") % (real - 4 + 1);
}

float DeviceMemory(const ExecutionContext* context, float real) {
  // A valid value between 4 and the real one, as Brave.
  if (!IsOn(context, kHardware) || real <= 4) {
    return real;
  }
  int steps = 0;
  for (float value = 4; value * 2 <= real; value *= 2) {
    ++steps;
  }
  return 4 * static_cast<float>(1 << (Random(context, "device-memory") %
                                      (steps + 1)));
}

int ScreenOffset(const ExecutionContext* context, std::string_view label) {
  return IsOn(context, kScreen) ? static_cast<int>(Random(context, label) % 9)
                                : 0;
}

String ExtraVoiceName(const ExecutionContext* context) {
  if (!IsOn(context, kSpeechSynthesis)) {
    return String();
  }
  // Names no Mac's voice has.
  static constexpr const char* kNames[] = {
      "Assunta", "Carmela",   "Ciro",     "Gennaro",
      "Nunzia",  "Pasquale",  "Salvatore", "Immacolata",
  };
  return String(kNames[Random(context, "voice name") % std::size(kNames)]);
}

String UsbSerialNumber(const ExecutionContext* context, const String& real) {
  if (real.empty() || !IsOn(context, kUsb)) {
    return real;
  }
  // As long as the real one, from its letters and digits.
  static constexpr char kChars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  const std::string label = "usb " + real.Utf8();
  uint64_t random = Random(context, label);
  std::string serial;
  for (unsigned i = 0; i < real.length(); i++) {
    if (i % 12 == 0) {
      random = Random(context, label + static_cast<char>('a' + i / 12));
    }
    serial += kChars[random % (std::size(kChars) - 1)];
    random /= std::size(kChars) - 1;
  }
  return String(serial.c_str());
}

bool AllowsFont(const ExecutionContext* context, const String& family) {
  if (!ReducesLanguages(context) || !IsOn(context, kFonts) ||
      family.empty()) {
    return true;
  }
  const std::string name = family.ToAsciiLower().Utf8();
  if (name.starts_with(".sf") || name.starts_with("sf ")) {
    return true;
  }
  // The family, or a face of it ("Arial Bold", "Helvetica-Light"), as
  // local() names them.
  for (size_t end = name.size(); end != std::string::npos && end > 0;
       end = name.find_last_of(" -", end - 1)) {
    if (kMacFonts.contains(std::string_view(name).substr(0, end))) {
      return true;
    }
  }
  // Others now and then, as Brave: one in twenty, for the site.
  return Random(context, "font:" + name) % 20 == 0;
}

}  // namespace blink::oblada_farbling
