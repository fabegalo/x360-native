// Profile avatar of the signed-in local user, for the 17559 avatar service and
// the original Guide.AvatarMiniCreator.xex (whose module DLL resolves its own
// imports). Stateless: the SDK profile owns the stored setting.
//
// XAM 17559 app 0xF3 (messages 0x600003/0x600009, handlers near 819823C0 and
// 81982768) keeps it in dashboard setting 0x63E80044 (binary, 1000 bytes, title
// FFFE07D1). docs/avatar-system.md §19.
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/user_profile.h>

#include <cstring>
#include <memory>
#include <vector>

namespace {
using rex::system::xam::UserProfile;
constexpr uint32_t kAvatarSetting = 0x63E80044, kMetadataSize = 1000;
constexpr uint32_t kInvalid = 0x80070057, kFailure = 0x80004005,
                   kNoSuchUser = 0x80070525, kNotFound = 0x80070490;
uint8_t* Host(uint32_t p) {
  return REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(p);
}
uint32_t Complete(uint32_t overlap, uint32_t hr) {
  if (!overlap) return hr;
  // Submission accepted; XOVERLAPPED carries the Win32 result and HRESULT.
  REX_KERNEL_STATE()->CompleteOverlappedImmediateEx(overlap, hr ? 1627 : 0, hr, 0);
  return 0;
}
UserProfile* SignedIn(uint32_t user) {
  auto* profile = REX_KERNEL_STATE()->user_profile();
  return user == 0 && profile && profile->signin_state() ? profile : nullptr;
}
}  // namespace

extern "C" REX_FUNC(__imp__XamAvatarGetManifestLocalUser) {
  const auto user = ctx.r3.u32, output = ctx.r4.u32, overlap = ctx.r5.u32;
  auto hr = kInvalid;
  if (output) {
    auto* profile = SignedIn(user);
    auto* setting = profile ? profile->GetSetting(kAvatarSetting) : nullptr;
    const auto* value = setting ? &static_cast<UserProfile::BinarySetting*>(setting)->value : nullptr;
    if (!profile) hr = kNoSuchUser;
    else if (!setting || setting->load_failed) hr = kFailure;
    // No saved avatar: the caller's own fallback decides; never invent one.
    else if (!setting->is_set || value->empty()) hr = kNotFound;
    else if (value->size() != kMetadataSize) hr = kFailure;
    else {
      std::memcpy(Host(output), value->data(), kMetadataSize);
      hr = 0;
    }
    hr = Complete(overlap, hr);
  }
  ctx.r3.u64 = hr;
  REXLOG_INFO("avatar-profile: GetManifestLocalUser({}) -> {:08X}", user, hr);
}

extern "C" REX_FUNC(__imp__XamAvatarSetManifest) {
  const auto user = ctx.r3.u32, input = ctx.r4.u32, overlap = ctx.r5.u32;
  auto hr = kInvalid;
  if (input) {
    auto* profile = SignedIn(user);
    if (!profile) hr = kNoSuchUser;
    else if (!profile->AddSetting(std::make_unique<UserProfile::BinarySetting>(
                 kAvatarSetting,
                 std::vector<uint8_t>(Host(input), Host(input) + kMetadataSize))))
      hr = kFailure;  // AddSetting publishes only after a durable write.
    else {
      // Same notification as the SDK's XamUserWriteProfileSettings.
      REX_KERNEL_STATE()->BroadcastNotification(0xE, 1);
      hr = 0;
    }
    hr = Complete(overlap, hr);
  }
  ctx.r3.u64 = hr;
  REXLOG_INFO("avatar-profile: SetManifest({}) -> {:08X}", user, hr);
}
