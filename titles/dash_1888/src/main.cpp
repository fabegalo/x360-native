#include "generated/dash/dash_1888_init.h"
#include <rex/cvar.h>
REXCVAR_DEFINE_BOOL(dash1888_boot_intro, false, "Dashboard 1888",
                   "Run the original Retail 1888 boot animation before the dashboard");
#include "dash_1888_app.h"
REX_DEFINE_APP(dash_1888, Dash1888App::Create)
