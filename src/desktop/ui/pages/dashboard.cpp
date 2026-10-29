#include "../frontend.h"
#include "app_text.h"

namespace x360n::ui {
using namespace desktop;
// Requirements of the selected dashboard version: what is still missing before it can start.
void DashboardNeeds(Ui& ui, const ImRect& detail) {
  auto s = [](float v) { return T().S(v); };
  const auto& a = ui.a;
  const auto& e = a.environment;
  struct Need { std::string text; bool ok; };
  const Need needs[3] = {{Tr("Arquivos originais", "Original files"), a.current.inputs_ok},
                         {Tr("Preparação AOT", "AOT preparation"), a.current.prepared},
                         {Tr("Ambiente do PC", "PC environment"), e.graphics && e.architecture && e.writable}};
  float nx = detail.Min.x + s(24);
  for (const auto& need : needs) {
    Icon(ui.dl, need.ok ? "check" : "warning", ImRect(nx, detail.Max.y - s(44), nx + s(22), detail.Max.y - s(18)), need.ok ? P.ok : P.warn);
    Text(ui.dl, Font::Caption, need.text, ImRect(nx + s(28), detail.Max.y - s(44), nx + s(190), detail.Max.y - s(18)), need.ok ? P.muted : P.text);
    nx += s(200);
  }
}
}  // namespace x360n::ui
