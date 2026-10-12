// Shared host capability, independent of dashboard assets/version.
// No Vision camera backend exists. xboxkrnl ordinal 0x21D returns a state,
// not a success code: 0 = disconnected, as in the SDK's unbuilt usbcam shim.
// Verified callers: 17559 0x92368568 and 6717 0x92356FA0 only initialize
// capture when the returned state is 2. No device or completion is fabricated.
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

extern "C" REX_FUNC(__imp__XUsbcamGetState) { ctx.r3.u64 = 0; }
