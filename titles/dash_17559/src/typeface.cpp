// Narrow import integration for dash 17559's existing font fallback chain.
// Path mapping follows Xenia Canary 2372a541, xam_locale.cc:666. This does not
// replace SegoeXbox-Light or claim that a resource file exists.
#include <string_view>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

extern "C" REX_FUNC(__imp__XamGetLanguageTypeface) {
  const auto language = ctx.r3.u32;
  const auto capacity = ctx.r4.u32;  // UTF-16 code units, as used by dash.
  const auto address = ctx.r5.u32;
  if (language >= 18 || !address || !capacity) {
    ctx.r3.u64 = 0x80070057;  // E_INVALIDARG
    return;
  }
  const std::u16string_view path = language == 17 ? u"file://media:/XenonSCLatin.xtt"
      : language == 8 ? u"file://media:/XenonCLatin.xtt"
                      : u"file://media:/XenonJKLatin.xtt";
  if (capacity <= path.size()) {
    ctx.r3.u64 = 0x8007007A;  // HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER)
    return;
  }
  for (size_t i = 0; i <= path.size(); ++i) {
    const char16_t c = i == path.size() ? 0 : path[i];
    base[address + i * 2] = static_cast<uint8_t>(c >> 8);
    base[address + i * 2 + 1] = static_cast<uint8_t>(c);
  }
  ctx.r3.u64 = 0;
}

extern "C" REX_FUNC(__imp__XamGetLanguageTypefacePatch) {
  // No optional font patch is selected. This is a nullable locator pointer,
  // never the incoming numeric language ID (the SDK stub's former result).
  ctx.r3.u64 = 0;
}
