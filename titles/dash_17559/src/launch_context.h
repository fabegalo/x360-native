#pragma once
namespace rex::kernel { class TitleRuntime; }
namespace dash17559 {
// Apply only to a return request from another title, never an initial boot.
bool PrepareReturnContext(rex::kernel::TitleRuntime& runtime);
}
