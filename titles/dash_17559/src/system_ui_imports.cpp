#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/logging.h>

// Private to the original 17559 system application. Account, input and loader
// services continue to use the SDK. The XAM entry point is never executed.
namespace {
void CallUi(PPCContext& ctx, uint8_t* base, uint32_t address) {
  auto* fn = REX_KERNEL_STATE()->function_dispatcher()->GetFunction(address);
  if (!fn) { REXLOG_ERROR("17559 UI: missing original function {:08X}", address); ctx.r3.u64 = 0x80004001; return; }
  fn(ctx, base);
}
}
extern "C" REX_FUNC(__imp__XamGetOnlineSchema) { CallUi(ctx, base, 0x816E2030); }
extern "C" REX_FUNC(__imp__XuiNavButtonGetPressPath) { CallUi(ctx, base, 0x817C08C0); }
extern "C" REX_FUNC(__imp__XUIElementPropVal_SetUint) { CallUi(ctx, base, 0x817A9238); }
extern "C" REX_FUNC(__imp__XamLookupCommonStringByIndex) { CallUi(ctx, base, 0x816E0BF0); }
extern "C" REX_FUNC(__imp__XamShowMessageBox) { CallUi(ctx, base, 0x816CB168); }
extern "C" REX_FUNC(__imp__XuiControlSetItemAssociation) { CallUi(ctx, base, 0x817C04B0); }
extern "C" REX_FUNC(__imp__XamBuildDynamicResourceLocator) { CallUi(ctx, base, 0x816C8B00); }
extern "C" REX_FUNC(__imp__XuiAnimRun) { CallUi(ctx, base, 0x817AB6D0); }
extern "C" REX_FUNC(__imp__XuiBubbleMessage) { CallUi(ctx, base, 0x817B3D98); }
extern "C" REX_FUNC(__imp__XuiCreateObject) { CallUi(ctx, base, 0x817B8B58); }
extern "C" REX_FUNC(__imp__XuiDestroyObject) { CallUi(ctx, base, 0x817B40D0); }
extern "C" REX_FUNC(__imp__XuiDynamicCast) { CallUi(ctx, base, 0x817AEFE8); }
extern "C" REX_FUNC(__imp__XuiElementAddChild) { CallUi(ctx, base, 0x817BCB90); }
extern "C" REX_FUNC(__imp__XuiElementFindNamedFrame) { CallUi(ctx, base, 0x817BDDE8); }
extern "C" REX_FUNC(__imp__XuiElementGetChildById) { CallUi(ctx, base, 0x817BD708); }
extern "C" REX_FUNC(__imp__XuiElementGetFocusUser) { CallUi(ctx, base, 0x817BC6E8); }
extern "C" REX_FUNC(__imp__XuiElementGetParent) { CallUi(ctx, base, 0x817BCC38); }
extern "C" REX_FUNC(__imp__XuiElementGetUserFocus) { CallUi(ctx, base, 0x817BC610); }
extern "C" REX_FUNC(__imp__XuiElementInitUserFocus) { CallUi(ctx, base, 0x817C5290); }
extern "C" REX_FUNC(__imp__XuiElementPlayTimeline) { CallUi(ctx, base, 0x817BDC78); }
extern "C" REX_FUNC(__imp__XuiElementSetBounds) { CallUi(ctx, base, 0x817BD0D8); }
extern "C" REX_FUNC(__imp__XuiElementSetFocus) { CallUi(ctx, base, 0x817BC660); }
extern "C" REX_FUNC(__imp__XuiElementSetUserFocus) { CallUi(ctx, base, 0x817C47D0); }
extern "C" REX_FUNC(__imp__XuiElementTreeGetFocus) { CallUi(ctx, base, 0x817BFCB0); }
extern "C" REX_FUNC(__imp__XuiFindClass) { CallUi(ctx, base, 0x817B3770); }
extern "C" REX_FUNC(__imp__XuiFreeStringTable) { CallUi(ctx, base, 0x817BE048); }
extern "C" REX_FUNC(__imp__XuiGetBaseObject) { CallUi(ctx, base, 0x817AE900); }
extern "C" REX_FUNC(__imp__XuiGetOuter) { CallUi(ctx, base, 0x817AF168); }
extern "C" REX_FUNC(__imp__XuiInit) { CallUi(ctx, base, 0x817BC088); }
extern "C" REX_FUNC(__imp__XuiLoadStringTableFromFile) { CallUi(ctx, base, 0x817C18E0); }
extern "C" REX_FUNC(__imp__XuiObjectFromHandle) { CallUi(ctx, base, 0x817AE868); }
extern "C" REX_FUNC(__imp__XuiObjectGetPropertyId) { CallUi(ctx, base, 0x817AEAB8); }
extern "C" REX_FUNC(__imp__XuiProcessInput) { CallUi(ctx, base, 0x817BFD58); }
extern "C" REX_FUNC(__imp__XuiRegisterClass) { CallUi(ctx, base, 0x817B8A58); }
extern "C" REX_FUNC(__imp__XuiRenderBegin) { CallUi(ctx, base, 0x8177DA10); }
extern "C" REX_FUNC(__imp__XuiRenderCreateDC) { CallUi(ctx, base, 0x8177DAC8); }
extern "C" REX_FUNC(__imp__XuiRenderDestroyDC) { CallUi(ctx, base, 0x8177DB78); }
extern "C" REX_FUNC(__imp__XuiRenderEnd) { CallUi(ctx, base, 0x8177DA38); }
extern "C" REX_FUNC(__imp__XuiRenderGetBackBufferSize) { CallUi(ctx, base, 0x8177DA60); }
extern "C" REX_FUNC(__imp__XuiRenderPresent) { CallUi(ctx, base, 0x8177DAA0); }
extern "C" REX_FUNC(__imp__XuiRenderUninit) { CallUi(ctx, base, 0x8177D978); }
extern "C" REX_FUNC(__imp__XuiSceneCreate) { CallUi(ctx, base, 0x817C4A58); }
extern "C" REX_FUNC(__imp__XuiSceneNavigateBack) { CallUi(ctx, base, 0x817C56E0); }
extern "C" REX_FUNC(__imp__XuiSceneNavigateFirst) { CallUi(ctx, base, 0x817C5390); }
extern "C" REX_FUNC(__imp__XuiSceneNavigateForward) { CallUi(ctx, base, 0x817C54D0); }
extern "C" REX_FUNC(__imp__XuiScenePlayBackFromTransition) { CallUi(ctx, base, 0x817C4D90); }
extern "C" REX_FUNC(__imp__XuiScenePlayToTransition) { CallUi(ctx, base, 0x817C4E40); }
extern "C" REX_FUNC(__imp__XuiSendMessage) { CallUi(ctx, base, 0x817B3D10); }
extern "C" REX_FUNC(__imp__XuiUninit) { CallUi(ctx, base, 0x817BAB20); }
extern "C" REX_FUNC(__imp__XuiUnregisterClass) { CallUi(ctx, base, 0x817B8A68); }
extern "C" REX_FUNC(__imp__XuiSetTimer) { CallUi(ctx, base, 0x817DA200); }
extern "C" REX_FUNC(__imp__XuiHandleIsValid) { CallUi(ctx, base, 0x817AE9D8); }
extern "C" REX_FUNC(__imp__XuiFree) { CallUi(ctx, base, 0x817AB8C0); }
extern "C" REX_FUNC(__imp__XuiCopyString) { CallUi(ctx, base, 0x817ACA98); }
extern "C" REX_FUNC(__imp__XuiKillTimer) { CallUi(ctx, base, 0x817DA2C8); }
extern "C" REX_FUNC(__imp__XuiElementEnableInput) { CallUi(ctx, base, 0x817C0D70); }
extern "C" REX_FUNC(__imp__XUIElementPropVal_Construct) { CallUi(ctx, base, 0x817A90F0); }
extern "C" REX_FUNC(__imp__XUIElementPropVal_Destruct) { CallUi(ctx, base, 0x817A9860); }
extern "C" REX_FUNC(__imp__XUIElementPropVal_SetString) { CallUi(ctx, base, 0x817A9310); }
extern "C" REX_FUNC(__imp__XuiObjectSetProperty) { CallUi(ctx, base, 0x817AED90); }
extern "C" REX_FUNC(__imp__XuiElementUnlink) { CallUi(ctx, base, 0x817BCBE8); }
extern "C" REX_FUNC(__imp__XuiDestroyBrush) { CallUi(ctx, base, 0x8177E0B8); }
extern "C" REX_FUNC(__imp__XuiControlGetItemAssociation) { CallUi(ctx, base, 0x817C0468); }
extern "C" REX_FUNC(__imp__XuiResourceLoadAll) { CallUi(ctx, base, 0x817DB7A8); }
extern "C" REX_FUNC(__imp__XuiImageElementSetImagePath) { CallUi(ctx, base, 0x817C1348); }
extern "C" REX_FUNC(__imp__XuiControlGetVisual) { CallUi(ctx, base, 0x817BFF00); }
extern "C" REX_FUNC(__imp__XuiLookupStringTableByIndex) { CallUi(ctx, base, 0x817BE098); }
extern "C" REX_FUNC(__imp__XuiLookupStringTable) { CallUi(ctx, base, 0x817BE0D8); }
extern "C" REX_FUNC(__imp__XuiCreateSolidBrush) { CallUi(ctx, base, 0x8177DF20); }
extern "C" REX_FUNC(__imp__XuiSceneInterruptTransitions) { CallUi(ctx, base, 0x817C0B70); }
extern "C" REX_FUNC(__imp__GamerCardStartup) { CallUi(ctx, base, 0x81725388); }
extern "C" REX_FUNC(__imp__GamerCardCleanup) { CallUi(ctx, base, 0x817253D8); }
extern "C" REX_FUNC(__imp__GamerCardRegisterControls) { CallUi(ctx, base, 0x817253E0); }
extern "C" REX_FUNC(__imp__GamerCardUnregisterControls) { CallUi(ctx, base, 0x81725408); }
extern "C" REX_FUNC(__imp__XuiElementPlayNamedFrames) { CallUi(ctx, base, 0x817BDCE0); }
extern "C" REX_FUNC(__imp__XuiElementLayoutTree) { CallUi(ctx, base, 0x817BCAA0); }
extern "C" REX_FUNC(__imp__XuiBrushGetTexture) { CallUi(ctx, base, 0x8177E100); }
extern "C" REX_FUNC(__imp__XuiElementGetFocus) { CallUi(ctx, base, 0x817BC600); }
extern "C" REX_FUNC(__imp__XuiObjectGetProperty) { CallUi(ctx, base, 0x817AEC98); }
extern "C" REX_FUNC(__imp__XuiTextElementSetText) { CallUi(ctx, base, 0x817C1240); }
extern "C" REX_FUNC(__imp__XuiElementInputEnabled) { CallUi(ctx, base, 0x817BCB40); }
extern "C" REX_FUNC(__imp__XuiBroadcastMessage) { CallUi(ctx, base, 0x817B3FB0); }
extern "C" REX_FUNC(__imp__XuiElementSetOpacity) { CallUi(ctx, base, 0x817BD450); }
extern "C" REX_FUNC(__imp__XuiControlAttachVisual) { CallUi(ctx, base, 0x817C0008); }
extern "C" REX_FUNC(__imp__XuiProgressBarSetValue) { CallUi(ctx, base, 0x817C1538); }
extern "C" REX_FUNC(__imp__XamGetDashContext) { CallUi(ctx, base, 0x816E16E0); }
// 17559 export 645 is a three-argument package-statistics request, despite
// the SDK's name. The original wrapper clears both u32 outputs and the
// selector checks a negative HRESULT before using its optional adjustment.
// This backend has no system package partition/statistics service.
extern "C" REX_FUNC(__imp__XamPackageManagerHasExtendedPartition) {
  if (!ctx.r4.u32 || !ctx.r5.u32) { ctx.r3.u64 = 0x80070057u; return; }
  rex::memory::store_and_swap<uint32_t>(base + ctx.r4.u32, 0);
  rex::memory::store_and_swap<uint32_t>(base + ctx.r5.u32, 0);
  ctx.r3.u64 = 0x80070032u; // ERROR_NOT_SUPPORTED, no fabricated statistics.
}
extern "C" REX_FUNC(__imp__XuiElementTreeHasFocus) { CallUi(ctx, base, 0x817BFC10); }
extern "C" REX_FUNC(__imp__XuiSceneIsInTransition) { CallUi(ctx, base, 0x817C0AA0); }
extern "C" REX_FUNC(__imp__XuiEditSetTextLimit) { CallUi(ctx, base, 0x817D6FD8); }
extern "C" REX_FUNC(__imp__XuiControlGetNavigation) { CallUi(ctx, base, 0x817C0560); }
extern "C" REX_FUNC(__imp__XuiElementHasFocus) { CallUi(ctx, base, 0x817BC668); }
extern "C" REX_FUNC(__imp__XuiEditSetTextFormatInfo) { CallUi(ctx, base, 0x817D31A8); }
extern "C" REX_FUNC(__imp__XuiSetMessageFilter) { CallUi(ctx, base, 0x817ACA80); }
extern "C" REX_FUNC(__imp__XuiElementSetPosition) { CallUi(ctx, base, 0x817BD1B0); }
extern "C" REX_FUNC(__imp__XuiFreeUnusedTextures) { CallUi(ctx, base, 0x8177E228); }
extern "C" REX_FUNC(__imp__XuiElementGetPosition) { CallUi(ctx, base, 0x817BD140); }
extern "C" REX_FUNC(__imp__XuiElementGetId) { CallUi(ctx, base, 0x817BD6A8); }
extern "C" REX_FUNC(__imp__XuiElementGetNext) { CallUi(ctx, base, 0x817BCE00); }
extern "C" REX_FUNC(__imp__XuiElementInsertChild) { CallUi(ctx, base, 0x817BCF30); }
// Query the original package registry. An absent AvatarMiniCreator package
// takes CreateProfile's original completion page instead of a stub's stale r3.
extern "C" REX_FUNC(__imp__XamPackageManagerGetFileSize) { CallUi(ctx, base, 0x816EA4A0); }
extern "C" REX_FUNC(__imp__XamIsGuideDisabled) { CallUi(ctx, base, 0x816E16F0); }
// Original title/privilege/context decision; the HUD keeps its own confirmation.
extern "C" REX_FUNC(__imp__XamLoaderShouldConfirmReboot) { CallUi(ctx, base, 0x8169D250); }
// The local dashboard host has no XAM game-info provider. Report that boundary
// honestly: Guide 913E5518 checks HRESULT before using its icon memory key.
// A stub's unchanged positive r3 was treated as a populated icon and decoded
// address zero as PNG. The original Guide supplies its own absent-info branch.
extern "C" REX_FUNC(__imp__XamLoaderGetGameInfo) { ctx.r3.u64 = 0x80004001u; }
extern "C" REX_FUNC(__imp__XuiElementGetLastChild) { CallUi(ctx, base, 0x817BCD68); }
extern "C" REX_FUNC(__imp__XuiScenePlayBackToTransition) { CallUi(ctx, base, 0x817C4DE8); }
extern "C" REX_FUNC(__imp__XuiScenePlayFromTransition) { CallUi(ctx, base, 0x817C4E98); }
extern "C" REX_FUNC(__imp__XuiDefault_EmptyString) { CallUi(ctx, base, 0x817B41D8); }
extern "C" REX_FUNC(__imp__XuiDefault_IntegerZero) { CallUi(ctx, base, 0x817B4328); }
extern "C" REX_FUNC(__imp__XuiSliderSetValue) { CallUi(ctx, base, 0x817C1720); }
extern "C" REX_FUNC(__imp__XuiSliderGetValue) { CallUi(ctx, base, 0x817C1788); }
extern "C" REX_FUNC(__imp__XuiSliderSetRange) { CallUi(ctx, base, 0x817C1608); }
extern "C" REX_FUNC(__imp__XuiSliderGetRange) { CallUi(ctx, base, 0x817C1690); }
extern "C" REX_FUNC(__imp__XuiElementGetTimeline) { CallUi(ctx, base, 0x817BDBC0); }
extern "C" REX_FUNC(__imp__XuiTabSceneGoto) { CallUi(ctx, base, 0x817C1C18); }
extern "C" REX_FUNC(__imp__XuiTabSceneGetCurrentTab) { CallUi(ctx, base, 0x817C1D40); }
extern "C" REX_FUNC(__imp__XUITimeline_Run) { CallUi(ctx, base, 0x817AB190); }
