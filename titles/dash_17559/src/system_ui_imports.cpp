#include <rex/ppc/hook.h>
#include <rex/kernel/title_runtime.h>
#include <rex/logging/log_category.h>

// Private to the original 17559 system application. Account, input and loader
// services continue to use the SDK. The XAM entry point is never executed.
namespace {
void CallUi(PPCContext& ctx, uint8_t* base, uint32_t address) {
  auto fn = REX_RUNTIME()->FindFunction(address);
  if (!fn) { REXLOG_ERROR("17559 UI: missing original function {:08X}", address); ctx.r3.u64 = 0x80004001; return; }
  (*fn)(ctx, base);
}
}
REX_HOOK_RAW(__imp__XamGetOnlineSchema) { CallUi(ctx, base, 0x816E2030); }
REX_HOOK_RAW(__imp__XuiNavButtonGetPressPath) { CallUi(ctx, base, 0x817C08C0); }
REX_HOOK_RAW(__imp__XUIElementPropVal_SetUint) { CallUi(ctx, base, 0x817A9238); }
REX_HOOK_RAW(__imp__XamLookupCommonStringByIndex) { CallUi(ctx, base, 0x816E0BF0); }
REX_HOOK_RAW(__imp__XamShowMessageBox) { CallUi(ctx, base, 0x816CB168); }
REX_HOOK_RAW(__imp__XuiControlSetItemAssociation) { CallUi(ctx, base, 0x817C04B0); }
REX_HOOK_RAW(__imp__XamBuildDynamicResourceLocator) { CallUi(ctx, base, 0x816C8B00); }
REX_HOOK_RAW(__imp__XuiAnimRun) { CallUi(ctx, base, 0x817AB6D0); }
REX_HOOK_RAW(__imp__XuiBubbleMessage) { CallUi(ctx, base, 0x817B3D98); }
REX_HOOK_RAW(__imp__XuiCreateObject) { CallUi(ctx, base, 0x817B8B58); }
// Exports 39F/3A0/3A1. The MiniCreator reads the chosen avatar's manifest with
// them before XamAvatarSetManifest; the stubs saved uninitialized memory.
REX_HOOK_RAW(__imp__XuiResourceOpen) { CallUi(ctx, base, 0x817DB560); }
REX_HOOK_RAW(__imp__XuiResourceRead) { CallUi(ctx, base, 0x817DA808); }
REX_HOOK_RAW(__imp__XuiResourceClose) { CallUi(ctx, base, 0x817DA8C8); }
REX_HOOK_RAW(__imp__XuiDestroyObject) { CallUi(ctx, base, 0x817B40D0); }
REX_HOOK_RAW(__imp__XuiDynamicCast) { CallUi(ctx, base, 0x817AEFE8); }
REX_HOOK_RAW(__imp__XuiElementAddChild) { CallUi(ctx, base, 0x817BCB90); }
REX_HOOK_RAW(__imp__XuiElementFindNamedFrame) { CallUi(ctx, base, 0x817BDDE8); }
REX_HOOK_RAW(__imp__XuiElementGetChildById) { CallUi(ctx, base, 0x817BD708); }
REX_HOOK_RAW(__imp__XuiElementGetFocusUser) { CallUi(ctx, base, 0x817BC6E8); }
REX_HOOK_RAW(__imp__XuiElementGetParent) { CallUi(ctx, base, 0x817BCC38); }
REX_HOOK_RAW(__imp__XuiElementGetUserFocus) { CallUi(ctx, base, 0x817BC610); }
REX_HOOK_RAW(__imp__XuiElementInitUserFocus) { CallUi(ctx, base, 0x817C5290); }
REX_HOOK_RAW(__imp__XuiElementPlayTimeline) { CallUi(ctx, base, 0x817BDC78); }
REX_HOOK_RAW(__imp__XuiElementSetBounds) { CallUi(ctx, base, 0x817BD0D8); }
REX_HOOK_RAW(__imp__XuiElementSetFocus) { CallUi(ctx, base, 0x817BC660); }
REX_HOOK_RAW(__imp__XuiElementSetUserFocus) { CallUi(ctx, base, 0x817C47D0); }
REX_HOOK_RAW(__imp__XuiElementTreeGetFocus) { CallUi(ctx, base, 0x817BFCB0); }
REX_HOOK_RAW(__imp__XuiFindClass) { CallUi(ctx, base, 0x817B3770); }
REX_HOOK_RAW(__imp__XuiFreeStringTable) { CallUi(ctx, base, 0x817BE048); }
REX_HOOK_RAW(__imp__XuiGetBaseObject) { CallUi(ctx, base, 0x817AE900); }
REX_HOOK_RAW(__imp__XuiGetOuter) { CallUi(ctx, base, 0x817AF168); }
REX_HOOK_RAW(__imp__XuiInit) { CallUi(ctx, base, 0x817BC088); }
REX_HOOK_RAW(__imp__XuiLoadStringTableFromFile) { CallUi(ctx, base, 0x817C18E0); }
REX_HOOK_RAW(__imp__XuiObjectFromHandle) { CallUi(ctx, base, 0x817AE868); }
REX_HOOK_RAW(__imp__XuiObjectGetPropertyId) { CallUi(ctx, base, 0x817AEAB8); }
REX_HOOK_RAW(__imp__XuiProcessInput) { CallUi(ctx, base, 0x817BFD58); }
REX_HOOK_RAW(__imp__XuiRegisterClass) { CallUi(ctx, base, 0x817B8A58); }
REX_HOOK_RAW(__imp__XuiRenderBegin) { CallUi(ctx, base, 0x8177DA10); }
REX_HOOK_RAW(__imp__XuiRenderCreateDC) { CallUi(ctx, base, 0x8177DAC8); }
REX_HOOK_RAW(__imp__XuiRenderDestroyDC) { CallUi(ctx, base, 0x8177DB78); }
REX_HOOK_RAW(__imp__XuiRenderEnd) { CallUi(ctx, base, 0x8177DA38); }
REX_HOOK_RAW(__imp__XuiRenderGetBackBufferSize) { CallUi(ctx, base, 0x8177DA60); }
REX_HOOK_RAW(__imp__XuiRenderPresent) { CallUi(ctx, base, 0x8177DAA0); }
REX_HOOK_RAW(__imp__XuiRenderUninit) { CallUi(ctx, base, 0x8177D978); }
REX_HOOK_RAW(__imp__XuiSceneCreate) { CallUi(ctx, base, 0x817C4A58); }
REX_HOOK_RAW(__imp__XuiSceneNavigateBack) { CallUi(ctx, base, 0x817C56E0); }
REX_HOOK_RAW(__imp__XuiSceneNavigateFirst) { CallUi(ctx, base, 0x817C5390); }
REX_HOOK_RAW(__imp__XuiSceneNavigateForward) { CallUi(ctx, base, 0x817C54D0); }
REX_HOOK_RAW(__imp__XuiScenePlayBackFromTransition) { CallUi(ctx, base, 0x817C4D90); }
REX_HOOK_RAW(__imp__XuiScenePlayToTransition) { CallUi(ctx, base, 0x817C4E40); }
REX_HOOK_RAW(__imp__XuiSendMessage) { CallUi(ctx, base, 0x817B3D10); }
REX_HOOK_RAW(__imp__XuiUninit) { CallUi(ctx, base, 0x817BAB20); }
REX_HOOK_RAW(__imp__XuiUnregisterClass) { CallUi(ctx, base, 0x817B8A68); }
REX_HOOK_RAW(__imp__XuiSetTimer) { CallUi(ctx, base, 0x817DA200); }
REX_HOOK_RAW(__imp__XuiHandleIsValid) { CallUi(ctx, base, 0x817AE9D8); }
REX_HOOK_RAW(__imp__XuiFree) { CallUi(ctx, base, 0x817AB8C0); }
REX_HOOK_RAW(__imp__XuiCopyString) { CallUi(ctx, base, 0x817ACA98); }
REX_HOOK_RAW(__imp__XuiKillTimer) { CallUi(ctx, base, 0x817DA2C8); }
REX_HOOK_RAW(__imp__XuiElementEnableInput) { CallUi(ctx, base, 0x817C0D70); }
REX_HOOK_RAW(__imp__XUIElementPropVal_Construct) { CallUi(ctx, base, 0x817A90F0); }
REX_HOOK_RAW(__imp__XUIElementPropVal_Destruct) { CallUi(ctx, base, 0x817A9860); }
REX_HOOK_RAW(__imp__XUIElementPropVal_SetString) { CallUi(ctx, base, 0x817A9310); }
REX_HOOK_RAW(__imp__XuiObjectSetProperty) { CallUi(ctx, base, 0x817AED90); }
REX_HOOK_RAW(__imp__XuiElementUnlink) { CallUi(ctx, base, 0x817BCBE8); }
REX_HOOK_RAW(__imp__XuiDestroyBrush) { CallUi(ctx, base, 0x8177E0B8); }
REX_HOOK_RAW(__imp__XuiControlGetItemAssociation) { CallUi(ctx, base, 0x817C0468); }
REX_HOOK_RAW(__imp__XuiResourceLoadAll) { CallUi(ctx, base, 0x817DB7A8); }
REX_HOOK_RAW(__imp__XuiImageElementSetImagePath) { CallUi(ctx, base, 0x817C1348); }
REX_HOOK_RAW(__imp__XuiControlGetVisual) { CallUi(ctx, base, 0x817BFF00); }
REX_HOOK_RAW(__imp__XuiLookupStringTableByIndex) { CallUi(ctx, base, 0x817BE098); }
REX_HOOK_RAW(__imp__XuiLookupStringTable) { CallUi(ctx, base, 0x817BE0D8); }
REX_HOOK_RAW(__imp__XuiCreateSolidBrush) { CallUi(ctx, base, 0x8177DF20); }
REX_HOOK_RAW(__imp__XuiSceneInterruptTransitions) { CallUi(ctx, base, 0x817C0B70); }
REX_HOOK_RAW(__imp__GamerCardStartup) { CallUi(ctx, base, 0x81725388); }
REX_HOOK_RAW(__imp__GamerCardCleanup) { CallUi(ctx, base, 0x817253D8); }
REX_HOOK_RAW(__imp__GamerCardRegisterControls) { CallUi(ctx, base, 0x817253E0); }
REX_HOOK_RAW(__imp__GamerCardUnregisterControls) { CallUi(ctx, base, 0x81725408); }
REX_HOOK_RAW(__imp__XuiElementPlayNamedFrames) { CallUi(ctx, base, 0x817BDCE0); }
REX_HOOK_RAW(__imp__XuiElementLayoutTree) { CallUi(ctx, base, 0x817BCAA0); }
REX_HOOK_RAW(__imp__XuiBrushGetTexture) { CallUi(ctx, base, 0x8177E100); }
REX_HOOK_RAW(__imp__XuiElementGetFocus) { CallUi(ctx, base, 0x817BC600); }
REX_HOOK_RAW(__imp__XuiObjectGetProperty) { CallUi(ctx, base, 0x817AEC98); }
REX_HOOK_RAW(__imp__XuiTextElementSetText) { CallUi(ctx, base, 0x817C1240); }
REX_HOOK_RAW(__imp__XuiElementInputEnabled) { CallUi(ctx, base, 0x817BCB40); }
REX_HOOK_RAW(__imp__XuiBroadcastMessage) { CallUi(ctx, base, 0x817B3FB0); }
REX_HOOK_RAW(__imp__XuiElementSetOpacity) { CallUi(ctx, base, 0x817BD450); }
REX_HOOK_RAW(__imp__XuiControlAttachVisual) { CallUi(ctx, base, 0x817C0008); }
REX_HOOK_RAW(__imp__XuiProgressBarSetValue) { CallUi(ctx, base, 0x817C1538); }
REX_HOOK_RAW(__imp__XamGetDashContext) { CallUi(ctx, base, 0x816E16E0); }
// 17559 export 645 is a three-argument package-statistics request, despite
// the SDK's name. The original wrapper clears both u32 outputs and the
// selector checks a negative HRESULT before using its optional adjustment.
// This backend has no system package partition/statistics service.
REX_HOOK_RAW(__imp__XamPackageManagerHasExtendedPartition) {
  if (!ctx.r4.u32 || !ctx.r5.u32) { ctx.r3.u64 = 0x80070057u; return; }
  rex::memory::store_and_swap<uint32_t>(base + ctx.r4.u32, 0);
  rex::memory::store_and_swap<uint32_t>(base + ctx.r5.u32, 0);
  ctx.r3.u64 = 0x80070032u; // ERROR_NOT_SUPPORTED, no fabricated statistics.
}
REX_HOOK_RAW(__imp__XuiElementTreeHasFocus) { CallUi(ctx, base, 0x817BFC10); }
REX_HOOK_RAW(__imp__XuiSceneIsInTransition) { CallUi(ctx, base, 0x817C0AA0); }
REX_HOOK_RAW(__imp__XuiEditSetTextLimit) { CallUi(ctx, base, 0x817D6FD8); }
REX_HOOK_RAW(__imp__XuiControlGetNavigation) { CallUi(ctx, base, 0x817C0560); }
REX_HOOK_RAW(__imp__XuiElementHasFocus) { CallUi(ctx, base, 0x817BC668); }
REX_HOOK_RAW(__imp__XuiEditSetTextFormatInfo) { CallUi(ctx, base, 0x817D31A8); }
REX_HOOK_RAW(__imp__XuiSetMessageFilter) { CallUi(ctx, base, 0x817ACA80); }
REX_HOOK_RAW(__imp__XuiElementSetPosition) { CallUi(ctx, base, 0x817BD1B0); }
REX_HOOK_RAW(__imp__XuiFreeUnusedTextures) { CallUi(ctx, base, 0x8177E228); }
REX_HOOK_RAW(__imp__XuiElementGetPosition) { CallUi(ctx, base, 0x817BD140); }
REX_HOOK_RAW(__imp__XuiElementGetId) { CallUi(ctx, base, 0x817BD6A8); }
REX_HOOK_RAW(__imp__XuiElementGetNext) { CallUi(ctx, base, 0x817BCE00); }
REX_HOOK_RAW(__imp__XuiElementInsertChild) { CallUi(ctx, base, 0x817BCF30); }
// Query the original package registry. An absent AvatarMiniCreator package
// takes CreateProfile's original completion page instead of a stub's stale r3.
REX_HOOK_RAW(__imp__XamPackageManagerGetFileSize) { CallUi(ctx, base, 0x816EA4A0); }
// The other read-only registry queries, also original. GamerProfile's Avatar item
// asks 646/647 and showed "feature requires update" with empty strings on stubs.
// 816E9AC0 sends message 0x25005 to XAM app 0xFE with a 4-byte buffer holding its
// argument; this host never registers app 0xFE (816BC968 finds no slot, 80070057),
// so the original package dispatcher 816EBC58 is called with the same message.
// shortcut: app 0xFE itself is not registered, upgrade once its procedure is located.
REX_HOOK_RAW(__imp__XamPackageManagerGetExperienceMode) {
  const uint32_t stack = ctx.r1.u32, buffer = stack - 16;
  rex::memory::store_and_swap<uint32_t>(base + buffer, ctx.r3.u32);
  ctx.r1.u64 = stack - 64;  // the buffer stays above the callee's frame
  ctx.r3.u64 = 0x25005; ctx.r4.u64 = buffer; ctx.r5.u64 = 0;
  CallUi(ctx, base, 0x816EBC58);
  ctx.r1.u64 = stack;
  REXLOG_INFO("17559 package manager: GetExperienceMode -> {:08X}", ctx.r3.u32);
}
REX_HOOK_RAW(__imp__XamPackageManagerGetFeatureRequiresUpdateStrings) { CallUi(ctx, base, 0x816E8F28); }
REX_HOOK_RAW(__imp__XamPackageManagerGetFilePathW) { CallUi(ctx, base, 0x816EA588); }
REX_HOOK_RAW(__imp__XamPackageManagerFindPackageContainingIndexedXEX) { CallUi(ctx, base, 0x816E8D40); }
REX_HOOK_RAW(__imp__XamPackageManagerGetAuthoritativeManifestVersion) { CallUi(ctx, base, 0x816E8EC8); }
REX_HOOK_RAW(__imp__XamIsGuideDisabled) { CallUi(ctx, base, 0x816E16F0); }
// Original title/privilege/context decision; the HUD keeps its own confirmation.
REX_HOOK_RAW(__imp__XamLoaderShouldConfirmReboot) { CallUi(ctx, base, 0x8169D250); }
// The local dashboard host has no XAM game-info provider. Report that boundary
// honestly: Guide 913E5518 checks HRESULT before using its icon memory key.
// A stub's unchanged positive r3 was treated as a populated icon and decoded
// address zero as PNG. The original Guide supplies its own absent-info branch.
REX_HOOK_RAW(__imp__XamLoaderGetGameInfo) { ctx.r3.u64 = 0x80004001u; }
REX_HOOK_RAW(__imp__XuiElementGetLastChild) { CallUi(ctx, base, 0x817BCD68); }
REX_HOOK_RAW(__imp__XuiScenePlayBackToTransition) { CallUi(ctx, base, 0x817C4DE8); }
REX_HOOK_RAW(__imp__XuiScenePlayFromTransition) { CallUi(ctx, base, 0x817C4E98); }
REX_HOOK_RAW(__imp__XuiDefault_EmptyString) { CallUi(ctx, base, 0x817B41D8); }
REX_HOOK_RAW(__imp__XuiDefault_IntegerZero) { CallUi(ctx, base, 0x817B4328); }
REX_HOOK_RAW(__imp__XuiSliderSetValue) { CallUi(ctx, base, 0x817C1720); }
REX_HOOK_RAW(__imp__XuiSliderGetValue) { CallUi(ctx, base, 0x817C1788); }
REX_HOOK_RAW(__imp__XuiSliderSetRange) { CallUi(ctx, base, 0x817C1608); }
REX_HOOK_RAW(__imp__XuiSliderGetRange) { CallUi(ctx, base, 0x817C1690); }
REX_HOOK_RAW(__imp__XuiElementGetTimeline) { CallUi(ctx, base, 0x817BDBC0); }
REX_HOOK_RAW(__imp__XuiTabSceneGoto) { CallUi(ctx, base, 0x817C1C18); }
REX_HOOK_RAW(__imp__XuiTabSceneGetCurrentTab) { CallUi(ctx, base, 0x817C1D40); }
REX_HOOK_RAW(__imp__XUITimeline_Run) { CallUi(ctx, base, 0x817AB190); }
