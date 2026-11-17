#include "@TITLE@_init.h"
#include "session_title_app.h"

class ImportedGameApp : public x360n::SessionTitleApp {
 public:
  ImportedGameApp() : SessionTitleApp("@TITLE@", kPPCImageConfig, @HOST_DISPATCH@ != 0) {}
};
REX_TITLE_APP(ImportedGameApp)
