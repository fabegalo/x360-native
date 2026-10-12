#pragma once
namespace rex { class Runtime; }
namespace dash17559 {
// Apply only to a return request from another title, never an initial boot.
bool PrepareReturnContext(rex::Runtime& runtime);
}
