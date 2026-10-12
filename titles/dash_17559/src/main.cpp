// dash_17559 - ReXGlue Recompiled Project

#include "generated/dash/dash_17559_init.h"

#include <rex/cvar.h>
REXCVAR_DEFINE_BOOL(dash17559_experimental_region, true, "Dashboard 17559",
                   "Experimental zero-filled system region; actual contents unknown");
REXCVAR_DEFINE_BOOL(dash17559_boot_intro, false, "Dashboard 17559",
                   "Run the original Retail 17559 boot module before the dashboard (experimental)");

#include "dash_17559_app.h"

REX_DEFINE_APP(dash_17559, Dash17559App::Create)
