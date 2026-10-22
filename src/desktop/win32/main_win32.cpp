#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "ui_theme.h"
#include <commctrl.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <Xinput.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <atomic>
#include <algorithm>
#include <fstream>
#include <map>
#include <memory>
#include <thread>
#include "../model/catalog.h"
#include "../model/import_source.h"
#include "child_process.h"
#include "environment_win32.h"
#include "workspace_lease.h"
#include "sha256.h"
#include "renderer_settings.h"
#include "console_settings.h"
#include "action_trace.h"
#include "digest_batch.h"
#include "preparation.h"
#include "session_notification.h"
#include "update.h"

namespace fs = std::filesystem;
using namespace x360n;
using namespace x360n::desktop;
namespace {
const auto& P = ui::P();
void VerifyDistribution(const fs::path& directory) {
#ifdef X360N_DISTRIBUTION
  VerifyPackage(directory, X360N_PACKAGE_PAYLOAD);
#else
  VerifyPackage(directory);
#endif
}
constexpr UINT kFinished = WM_APP + 1, kProgress = WM_APP + 2, kDetected = WM_APP + 3, kInitialized = WM_APP + 4, kInspected = WM_APP + 5, kOpenEnvironment = WM_APP + 6, kEnvironmentChecked = WM_APP + 7, kSessionStarted = WM_APP + 8, kSessionClosed = WM_APP + 9, kReveal = WM_APP + 10, kUpdateChecked = WM_APP + 11, kUpdateFinished = WM_APP + 12, kHinted = WM_APP + 13, kPreview = WM_APP + 20;
enum { kFolder = 101, kTargets, kCheck, kImport, kPrepare, kLaunch, kDetails, kData, kCancel, kLanguage, kInstall, kStorage, kPrimary, kSettings, kImportFolder, kDashboards, kGames, kHome, kImportPage, kAddContent, kLibrary, kGameList, kRenderer, kNativeDevelopment,
       kSearch, kFilter0, kFilter1, kFilter2, kFilter3, kLangPt, kLangEn, kRendererNative, kRetry, kDismiss, kBannerLibrary, kBannerDetails,
       kConsoleLanguage, kConsoleCountry, kVerifyTarget, kUpdate, kRemove, kFullscreen, kCaptureCrash, kApiD3D12, kApiVulkan };
bool english = false;
std::string Tr(const char* pt, const char* en) { return english ? en : pt; }
std::wstring Wide(const std::string& text) {
  if (text.empty()) return {};
  auto n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
  std::wstring out(n, L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), out.data(), n);
  return out;
}
std::wstring WTr(const char* pt, const char* en) { return Wide(Tr(pt, en)); }

enum class Page { Home, Library, Import, Dashboard, Settings, Diagnostics };
enum class Row { Ready, Preparing, Imported, Unsupported, Error, Pending, Unchecked };
enum class Kind { Secondary, Primary, Nav, Chip, Ghost, Option, Toggle };
enum class Tone { Neutral, Ok, Warn, Err, Info };
struct Style { Kind kind = Kind::Secondary; const wchar_t* glyph = nullptr; int back = 0; };  // back: 0 bg, 1 sidebar, 2 surface
struct Geo {
  RECT banner{}, bar{}, steps{}, elapsed{}, hero{}, stats[4]{}, drop{}, tiles[3]{}, list_card{}, detail{}, search{}, cards[4]{}, details_card{}, empty{}, sidebar{};
  bool banner_on = false;
  int banner_text = 0, title_right = 0, action_w = 0;
  // Settings content scrolls inside the viewport; its controls are clipped to it.
  RECT viewport{};
  int scroll_max = 0;
  std::vector<HWND> scrolled;
};
struct Fonts { HFONT body{}, bold{}, caption{}, headline{}, big{}, icon{}, icon_big{}, mono{}; };

struct App {
  HWND library_list{}, window{}, subtitle{}, root_text{}, status{}, details{}, headline{}, footer{}, search{};
  HWND activity_text{}, drop_text{}, renderer_hint{}, env_text{}, hot{};
  HWND console_language_label{}, console_country_label{}, console_hint{};
  Fonts fonts;
  HICON icon{};
  std::vector<HWND> all;
  std::map<int, Style> styles;
  fs::path self, root, logs, preferences;
  std::string startup_error;
  Environment environment;
  RendererSettings renderer_settings;
  bool capture_crash = false;
  ConsoleSettings console_settings;
  std::vector<Target> targets;
  // Decoded once per icon hash; a failed decode stays cached as empty.
  std::map<std::string, std::unique_ptr<Gdiplus::Bitmap>> thumbnails;
  struct CheckedTarget { Inspection state; std::vector<std::string> library; bool added = false; };
  std::vector<CheckedTarget> checked;
  // Worker writes only pending results; UI adopts them after completion/join.
  std::vector<Target> pending_targets;
  std::vector<CheckedTarget> pending_checked;
  Environment pending_environment;
  RendererSettings pending_renderer;
  ConsoleSettings pending_console;
  bool pending_snapshot = false;
  PayloadStatus pending_payload;
  fs::path pending_root;
  bool pending_payload_current = false;
  bool supervisor_available = false, pending_supervisor_available = false;
  bool launch_verified = false;  // worker result, consumed only after completion
  bool catalog_loaded = false, pending_full_check = false;
  std::vector<size_t> dashboard_indices, rows;
  Inspection current;
  size_t selected = 0;
  std::thread worker;
  std::atomic<bool> cancel{false};
  std::atomic<bool> failed{false};
  std::atomic<int> stage{0};
  bool busy = false, session = false, finishing_session = false, show_details = false;
  UINT dpi = 96;
  WORD buttons_down = 0;
  unsigned anim_ticks = 0;
  unsigned timer_ticks = 0;
  bool allow_downloads = false;
  bool installing = false;
  bool settings = false, games = false, importing = false, initializing = false;
  bool home = true, import_view = false;
  bool import_recognized = false;
  std::string imported_id;
  int next_action = kFolder;
  // Presentation state
  Geo geo;
  std::vector<HWND> clipped;  // controls with a window region set by Layout
  int scroll = 0;
  int filter = 0;
  std::wstring query, chip_text, activity_title, selection_title;
  Tone chip_tone = Tone::Neutral;
  std::vector<std::wstring> stage_names;
  int activity_kind = 0;  // 1 import, 2 prepare, 3 session, 4 check/init
  ULONGLONG started = 0;
  uintmax_t extracted_bytes = 0;
  int result = 0;  // 0 none, 1 success, 2 warning, 3 error
  std::wstring result_title;
  int last_action = 0; fs::path last_source;
  std::string preparing_id, operation_target;
#ifndef X360N_DISTRIBUTION
  bool preview_empty = false;
  fs::path auto_import;  // review aid: import this path once the first verification finishes
  fs::path capture_settings;  // self-capture after loading real preferences; no input automation
  int capture_page = kSettings;  // page shown in the self-capture (Settings or Library)
  unsigned capture_ticks = 0;
#endif
  // Self-update: the release check runs beside the main worker; installing uses the worker.
  std::thread update_check;
  std::atomic<bool> closing{false};
  std::optional<AvailableUpdate> update;
  UpdateChannel update_channel = UpdateChannel::Stable;
  // Listing hints run after the list is shown; results apply only to the
  // snapshot they were computed for, and never while an operation runs.
  std::thread hint_check;
  unsigned hint_generation = 0;
  struct Hints { unsigned generation = 0; std::vector<std::pair<size_t, ReceiptHint>> items; };
  ~App() {
    closing = true;
    if (hint_check.joinable()) hint_check.join();
    if (update_check.joinable()) update_check.join();
    if (worker.joinable()) worker.join();
    for (auto font : {fonts.body, fonts.bold, fonts.caption, fonts.headline, fonts.big, fonts.icon, fonts.icon_big, fonts.mono}) if (font) DeleteObject(font);
    DestroyIcon(icon);
  }
};
UINT g_dpi_override = 0;
void Set(HWND h, const std::string& text) { SetWindowTextW(h, Wide(text).c_str()); }
fs::path Payload(const App& a) { return a.self.parent_path() / "app/workspace"; }
fs::path Supervisor(const App& a) {
  const auto packaged = a.self.parent_path() / "app/x360n.exe";
  return fs::is_regular_file(packaged) ? packaged : a.self.parent_path() / "x360n.exe";
}
Page PageOf(const App& a) {
  if (a.settings) return Page::Settings;
  if (a.show_details) return Page::Diagnostics;
  if (a.home) return Page::Home;
  if (a.import_view) return Page::Import;
  return a.games ? Page::Library : Page::Dashboard;
}
void Post(HWND window, UINT type, const std::string& text) {
  auto* value = new std::string(text);
  if (!PostMessageW(window, type, 0, reinterpret_cast<LPARAM>(value))) delete value;
}
HFONT MakeFontPx(int px, int weight, const wchar_t* face, UINT dpi, DWORD quality = CLEARTYPE_QUALITY) {
  return CreateFontW(-MulDiv(px, int(dpi), 96), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, quality, DEFAULT_PITCH, face);
}
void ApplyFonts(App& a) {
  for (auto font : {a.fonts.body, a.fonts.bold, a.fonts.caption, a.fonts.headline, a.fonts.big, a.fonts.icon, a.fonts.icon_big, a.fonts.mono}) if (font) DeleteObject(font);
  a.fonts.body = MakeFontPx(14, FW_NORMAL, L"Segoe UI", a.dpi);
  a.fonts.bold = MakeFontPx(14, FW_SEMIBOLD, L"Segoe UI", a.dpi);
  a.fonts.caption = MakeFontPx(12, FW_NORMAL, L"Segoe UI", a.dpi);
  a.fonts.headline = MakeFontPx(30, FW_SEMIBOLD, L"Segoe UI", a.dpi);
  a.fonts.big = MakeFontPx(22, FW_SEMIBOLD, L"Segoe UI", a.dpi);
  a.fonts.icon = MakeFontPx(16, FW_NORMAL, L"Segoe MDL2 Assets", a.dpi, ANTIALIASED_QUALITY);
  a.fonts.icon_big = MakeFontPx(26, FW_NORMAL, L"Segoe MDL2 Assets", a.dpi, ANTIALIASED_QUALITY);
  a.fonts.mono = MakeFontPx(13, FW_NORMAL, L"Consolas", a.dpi);
  for (auto h : a.all) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(a.fonts.body), TRUE);
  SendMessageW(a.headline, WM_SETFONT, reinterpret_cast<WPARAM>(a.fonts.headline), TRUE);
  SendMessageW(a.details, WM_SETFONT, reinterpret_cast<WPARAM>(a.fonts.mono), TRUE);
  SendMessageW(a.footer, WM_SETFONT, reinterpret_cast<WPARAM>(a.fonts.caption), TRUE);
}
LRESULT CALLBACK HotProc(HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) {
  auto* a = reinterpret_cast<App*>(ref);
  if (m == WM_MOUSEMOVE && a->hot != h) {
    auto old = a->hot; a->hot = h;
    TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, h, 0}; TrackMouseEvent(&track);
    if (old) InvalidateRect(old, nullptr, FALSE);
    InvalidateRect(h, nullptr, FALSE);
  } else if (m == WM_MOUSELEAVE && a->hot == h) {
    a->hot = nullptr; InvalidateRect(h, nullptr, FALSE);
  } else if (m == WM_ERASEBKGND) return 1;  // DrawButton fills the whole button; a default erase flashes white
  else if (m == WM_SETFOCUS) PostMessageW(a->window, kReveal, reinterpret_cast<WPARAM>(h), 0);
  else if (m == WM_NCDESTROY) RemoveWindowSubclass(h, HotProc, 0);
  return DefSubclassProc(h, m, wp, lp);
}
// A closed list scrolls the page instead of silently changing a saved console setting.
LRESULT CALLBACK ComboProc(HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
  if (m == WM_MOUSEWHEEL && !SendMessageW(h, CB_GETDROPPEDSTATE, 0, 0)) return SendMessageW(GetParent(h), m, wp, lp);
  if (m == WM_NCDESTROY) RemoveWindowSubclass(h, ComboProc, 0);
  return DefSubclassProc(h, m, wp, lp);
}
// Height the wrapped text of a static control needs at `width`, as the control draws it.
int TextHeight(HWND h, int width) {
  wchar_t text[2048]{}; GetWindowTextW(h, text, 2048);
  HDC dc = GetDC(h);
  const auto old = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(h, WM_GETFONT, 0, 0)));
  RECT r{0, 0, std::max(1, width), 0};
  DrawTextW(dc, text, -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
  TEXTMETRICW metrics{}; GetTextMetricsW(dc, &metrics);
  SelectObject(dc, old); ReleaseDC(h, dc);
  return std::max<int>(r.bottom, metrics.tmHeight);
}
int LineHeight(HWND h) {
  HDC dc = GetDC(h);
  const auto old = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(h, WM_GETFONT, 0, 0)));
  TEXTMETRICW metrics{}; GetTextMetricsW(dc, &metrics);
  SelectObject(dc, old); ReleaseDC(h, dc);
  return metrics.tmHeight;
}
HWND Control(App& a, const wchar_t* cls, const wchar_t* text, DWORD style, int id = 0) {
  if (std::wstring_view(cls) == L"BUTTON") style |= BS_OWNERDRAW;
  if (std::wstring_view(cls) == L"STATIC") style |= SS_NOPREFIX;
  auto h = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
                         a.window, reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
  a.all.push_back(h);
  if (std::wstring_view(cls) == L"BUTTON") SetWindowSubclass(h, HotProc, 0, reinterpret_cast<DWORD_PTR>(&a));
  return h;
}
HWND Button(App& a, int id, Kind kind, const wchar_t* glyph = nullptr, int back = 0) {
  a.styles[id] = {kind, glyph, back};
  return Control(a, L"BUTTON", L"", WS_TABSTOP, id);
}
HWND Item(const App& a, int id) { return GetDlgItem(a.window, id); }
// Width an owner-drawn button needs for its label (and glyph), mirroring DrawButton.
int ButtonWidth(const App& a, int id, bool glyph) {
  wchar_t text[256]{}; GetWindowTextW(Item(a, id), text, 256);
  HDC dc = GetDC(a.window);
  const int w = ui::TextWidth(dc, glyph ? a.fonts.bold : a.fonts.body, text);
  ReleaseDC(a.window, dc);
  return w + ui::S(glyph ? 22 + 8 + 24 : 24, a.dpi);
}

// ---------- State of listed items ----------
Row StatusOf(const App& a, size_t i) {
  const auto& t = a.targets[i];
  const auto& c = a.checked.at(i);
  if (a.busy && !a.session && !a.preparing_id.empty() && a.preparing_id == t.id) return Row::Preparing;
  if (c.state.verification_pending) {
    // Listing hints only; the full check still runs before preparing or starting.
    if (!t.aot) return Row::Unsupported;
    if (c.state.interrupted) return Row::Error;
    if (!c.state.launch_candidate) return Row::Imported;
    if (!c.state.receipt_current) return c.state.receipt_stale ? Row::Imported : Row::Unchecked;
    for (const auto& dep : t.dependencies) {
      const auto it = std::find_if(a.targets.begin(), a.targets.end(), [&](const auto& d) { return d.id == dep; });
      if (it == a.targets.end()) return Row::Error;
      const auto& state = a.checked.at(size_t(it - a.targets.begin())).state;
      if (!state.receipt_current && !state.prepared) return state.receipt_stale ? Row::Imported : Row::Unchecked;
    }
    // Reuse the preparation receipt for presentation. Starting still completes
    // any pending registration and performs the supervisor's independent check.
    // Do not hash game inventories just to display an already prepared title.
    return c.library.empty() ? Row::Ready : Row::Imported;
  }
  if (!c.state.inputs_ok || c.state.interrupted) return t.kind == "dashboard" && !c.state.interrupted ? Row::Pending : Row::Error;
  if (!t.aot) return Row::Unsupported;
  if (c.state.prepared && c.library.empty()) return Row::Ready;
  return Row::Imported;
}
std::wstring RowLabel([[maybe_unused]] const App& a, Row row, bool dashboard) {
  switch (row) {
    case Row::Ready: return WTr(dashboard ? "Pronta" : "Pronto", "Ready");
    case Row::Preparing: return WTr("Preparando", "Preparing");
    case Row::Imported: return WTr("Preparar", "Prepare");
    case Row::Unsupported: return WTr("Não suportado", "Not supported");
    case Row::Pending: return WTr("Pendente", "Pending");
    case Row::Unchecked: return WTr("Conferir", "Check");
    default: return WTr("Erro", "Error");
  }
}
Tone RowTone(Row row) {
  switch (row) {
    case Row::Ready: return Tone::Ok;
    case Row::Preparing: return Tone::Info;
    case Row::Unchecked: return Tone::Info;
    case Row::Imported: return Tone::Neutral;
    case Row::Unsupported: case Row::Pending: return Tone::Warn;
    default: return Tone::Err;
  }
}
COLORREF ToneColor(Tone tone) {
  switch (tone) {
    case Tone::Ok: return P.ok;
    case Tone::Warn: return P.warn;
    case Tone::Err: return P.err;
    case Tone::Info: return P.info;
    default: return P.muted;
  }
}
std::wstring Lower(std::wstring s) { if (!s.empty()) CharLowerBuffW(s.data(), DWORD(s.size())); return s; }
bool Matches(const App& a, size_t i) {
  if (!a.games) return true;
  if (a.filter != 0) {
    const auto row = StatusOf(a, i);
    const bool ok = a.filter == 1 ? row == Row::Ready :
      a.filter == 2 ? (row == Row::Imported || row == Row::Preparing || row == Row::Unchecked) :
      (row == Row::Error || row == Row::Unsupported || row == Row::Pending);
    if (!ok) return false;
  }
  if (a.query.empty()) return true;
  const auto& t = a.targets[i];
  const auto hay = Lower(Wide(t.label + " " + t.label_en + " " + t.title_id + " " + t.version));
  return hay.find(a.query) != std::wstring::npos;
}
void SyncSelection(App& a) {
  auto it = std::find(a.rows.begin(), a.rows.end(), a.selected);
  SendMessageW(a.library_list, LB_SETCURSEL, it == a.rows.end() ? -1 : WPARAM(it - a.rows.begin()), 0);
}
void RebuildRows(App& a) {
  SendMessageW(a.library_list, WM_SETREDRAW, FALSE, 0);
  SendMessageW(a.library_list, LB_RESETCONTENT, 0, 0);
  a.rows.clear();
  bool pool = a.checked.size() == a.targets.size();
#ifndef X360N_DISTRIBUTION
  if (a.preview_empty) pool = false;
#endif
  if (pool) for (auto i : a.dashboard_indices) if (Matches(a, i)) {
    a.rows.push_back(i);
    const auto name = Wide(english ? a.targets[i].label_en : a.targets[i].label);
    auto at = SendMessageW(a.library_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    SendMessageW(a.library_list, LB_SETITEMDATA, at, LPARAM(i));
  }
  SyncSelection(a);
  SendMessageW(a.library_list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(a.library_list, nullptr, FALSE);
}

// All games use the same persistent import/removal lifecycle.
bool Removable(const App& a) {
  if (a.selected >= a.targets.size() || a.checked.size() != a.targets.size()) return false;
  const auto& t = a.targets[a.selected];
  return InGameLibrary(t);
}
// ---------- Layout ----------
void Layout(App& a) {
  if (!a.details) return;
  RECT client{}; GetClientRect(a.window, &client);
  const int W = client.right, H = client.bottom;
  auto s = [&](int v) { return ui::S(v, a.dpi); };
  std::vector<HWND> placed;
  Geo g;
  RECT clip{};  // non-empty while placing scrolled content
  auto put = [&](HWND h, int x, int y, int w, int hgt, bool visible = true) {
    placed.push_back(h);
    // No intermediate repaint: the whole window is invalidated once at the end.
    SetWindowPos(h, nullptr, x, y, std::max(0, w), std::max(0, hgt), SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    bool clipped = false;
    if (!IsRectEmpty(&clip)) {
      g.scrolled.push_back(h);
      // Measure the real window: a drop-down list keeps only its closed height.
      RECT r{}; GetWindowRect(h, &r);
      MapWindowPoints(nullptr, a.window, reinterpret_cast<POINT*>(&r), 2);
      RECT shown{};
      if (!IntersectRect(&shown, &r, &clip)) shown = {};
      else if (!EqualRect(&shown, &r)) OffsetRect(&shown, -r.left, -r.top);
      if (!EqualRect(&shown, &r)) { SetWindowRgn(h, CreateRectRgnIndirect(&shown), FALSE); clipped = true; }
    }
    const auto known = std::find(a.clipped.begin(), a.clipped.end(), h);
    if (clipped && known == a.clipped.end()) a.clipped.push_back(h);
    if (!clipped && known != a.clipped.end()) { a.clipped.erase(known); SetWindowRgn(h, nullptr, FALSE); }
  };
  auto rect = [](int x, int y, int w, int h) { return RECT{x, y, x + w, y + h}; };
  const Page page = PageOf(a);
  const int sb = s(216), pad = s(32);
  g.sidebar = rect(0, 0, sb, H);
  // Sidebar
  const int nx = s(12), nw = sb - s(24), nh = s(42);
  int ny = s(92);
  for (auto id : {kHome, kGames, kImportPage, kDashboards}) { put(Item(a, id), nx, ny, nw, nh); ny += nh + s(4); }
  int by = H - s(70) - s(8);
  put(a.footer, s(20), H - s(70), sb - s(36), s(54));
  by -= nh; put(Item(a, kLanguage), nx, by, nw, nh);
  by -= nh + s(4); put(Item(a, kDetails), nx, by, nw, nh);
  by -= nh + s(4); put(Item(a, kSettings), nx, by, nw, nh);
  if (a.update) { by -= nh + s(12); put(Item(a, kUpdate), nx, by, nw, nh); }
  // Header
  const int cx = sb + pad, cw = std::max(s(300), W - sb - 2 * pad);
  int y = s(28);
  const bool diag = page == Page::Diagnostics;
  put(a.headline, cx, y, cw - (diag ? s(216) : 0), s(44));
  put(a.subtitle, cx, y + s(46), cw, s(44));
  if (diag) put(Item(a, kData), cx + cw - s(204), y + s(2), s(204), s(40));
  int yc = s(128);
  const int bottom = H - s(28);
  // Activity / result banner
  const bool banner = (a.busy || a.result) && !diag;
  g.banner_on = banner;
  if (banner) {
    const bool busy = a.busy;
    // Actions share the title row; the message starts below them and grows with
    // its wrapped length, so no control covers a button's border.
    const int tw = cw - s(76) - s(24), line = LineHeight(a.activity_text);
    g.banner_text = std::clamp(TextHeight(a.activity_text, tw), line, 4 * line);
    const int text_top = s(58), bar_top = text_top + g.banner_text + s(12);
    const bool stages = busy && !a.stage_names.empty();
    const int right = cx + cw - s(24);
    // Result actions sit beside the title, or in their own row when the title needs the space.
    std::vector<int> actions;
    if (!busy && a.result == 3) actions = {kBannerDetails, kRetry};
    else if (!busy && a.result == 1 && a.importing) actions = {kBannerLibrary};
    int actions_w = 0;
    for (int id : actions) actions_w += ButtonWidth(a, id, true) + s(8);
    HDC title_dc = GetDC(a.window);
    const int title_w = ui::TextWidth(title_dc, a.fonts.big, a.result_title);
    ReleaseDC(a.window, title_dc);
    const bool action_row = !actions.empty() && cx + s(76) + title_w + s(16) > right - s(36) - actions_w;
    const int bh = busy ? bar_top + s(6) + (stages ? s(10) + s(26) : 0) + s(14) :
                   text_top + g.banner_text + (action_row ? s(12) + s(36) : 0) + s(18);
    g.banner = rect(cx, yc, cw, bh);
    put(a.activity_text, cx + s(76), yc + text_top, tw, g.banner_text);
    if (busy) {
      const int cancel_w = std::max(s(130), ButtonWidth(a, kCancel, true));
      put(Item(a, kCancel), right - cancel_w, yc + s(16), cancel_w, s(36), !a.session);
      g.elapsed = rect(right - cancel_w - s(12) - s(150), yc + s(18), s(150), s(28));
      g.title_right = g.elapsed.left - s(8);
      g.bar = rect(cx + s(24), yc + bar_top, cw - s(48), s(6));
      g.steps = stages ? rect(cx + s(24), g.bar.bottom + s(10), cw - s(48), s(26)) : RECT{};
    } else {
      put(Item(a, kDismiss), right - s(36), yc + s(16), s(36), s(36));
      g.title_right = right - s(36) - s(16);
      if (action_row) {
        int bx = cx + s(76);
        for (auto it = actions.rbegin(); it != actions.rend(); ++it) {
          const int w = ButtonWidth(a, *it, true);
          put(Item(a, *it), bx, yc + text_top + g.banner_text + s(12), w, s(36)); bx += w + s(8);
        }
      } else {
        int bx = right - s(36);
        for (int id : actions) { const int w = ButtonWidth(a, id, true); bx -= s(8) + w; put(Item(a, id), bx, yc + s(16), w, s(36)); }
        g.title_right = bx - s(16);
      }
    }
    yc += bh + s(16);
  }
  const bool empty_list = a.rows.empty();
  switch (page) {
    case Page::Home: {
      g.hero = rect(cx, yc, cw, s(188));
      put(a.status, cx + s(28), yc + s(66), cw - s(56) - s(268), s(96));
      put(Item(a, kPrimary), cx + cw - s(28) - s(240), yc + s(66), s(240), s(56));
      if (!banner) {
        const int gap = s(12), sw = (cw - 3 * gap) / 4, sy = yc + s(188) + s(16);
        for (int i = 0; i < 4; ++i) g.stats[i] = rect(cx + i * (sw + gap), sy, sw, s(96));
        const int qy = sy + s(96) + s(20);
        put(Item(a, kAddContent), cx, qy, s(220), s(44));
        put(Item(a, kLibrary), cx + s(232), qy, s(220), s(44));
      }
      break;
    }
    case Page::Library: case Page::Dashboard: {
      int top = yc;
      if (page == Page::Library) {
        const int sw = std::min(s(300), cw / 2);
        g.search = rect(cx, top, sw, s(40));
        const int eh = s(22);
        put(a.search, cx + s(38), top + (s(40) - eh) / 2, sw - s(38) - s(12), eh);
        int fx = cx + sw + s(16);
        HDC dc = GetDC(a.window);
        const std::wstring labels[4] = {WTr("Todos", "All"), WTr("Prontos", "Ready"), WTr("Preparar", "To prepare"), WTr("Atenção", "Attention")};
        for (int i = 0; i < 4; ++i) {
          const int w = ui::TextWidth(dc, a.fonts.body, labels[i]) + s(32);
          put(Item(a, kFilter0 + i), fx, top, w, s(40)); fx += w + s(8);
        }
        ReleaseDC(a.window, dc);
        top += s(52);
      }
      if (empty_list) {
        g.empty = rect(cx, top, cw, std::max(s(160), bottom - top));
        const bool filtered = a.filter != 0 || !a.query.empty();
        wchar_t body[1024]{}; GetWindowTextW(a.status, body, 1024);
        const std::wstring text = filtered ? WTr("Nenhum jogo corresponde à busca ou ao filtro atual.", "No game matches the current search or filter.") : body;
        wchar_t now[1024]{}; GetWindowTextW(a.drop_text, now, 1024);
        if (text != now) SetWindowTextW(a.drop_text, text.c_str());
        put(a.drop_text, cx + s(40), g.empty.top + s(126), cw - s(80), s(56));
        put(Item(a, kPrimary), cx + (cw - s(240)) / 2, g.empty.top + s(196), s(240), s(46), !filtered);
        put(a.library_list, cx, top, 0, 0, false);
      } else {
        const bool removable = page == Page::Library && Removable(a);
        const int dh = page == Page::Dashboard || removable ? s(196) : s(150);
        g.detail = rect(cx, bottom - dh, cw, dh);
        g.list_card = rect(cx, top, cw, std::max<int>(s(96), int(g.detail.top) - s(12) - top));
        put(a.library_list, cx, top, cw, g.list_card.bottom - top);
        SendMessageW(a.library_list, LB_SETITEMHEIGHT, 0, s(76));
        // Both actions share one column wide enough for the longer label.
        const int aw = std::min(cw / 2, std::max({s(220), ButtonWidth(a, kPrimary, true), ButtonWidth(a, kAddContent, true)}));
        g.action_w = aw;
        put(a.status, cx + s(24), g.detail.top + s(58), cw - s(48) - aw - s(24), s(80));
        put(Item(a, kPrimary), cx + cw - s(24) - aw, g.detail.top + s(22), aw, s(46));
        put(Item(a, kAddContent), cx + cw - s(24) - aw, g.detail.top + s(22) + s(46) + s(12), aw, s(40));
        if (removable) put(Item(a, kRemove), cx + cw - s(24) - aw, g.detail.top + s(22) + s(46) + s(12) + s(40) + s(8), aw, s(36));
      }
      break;
    }
    case Page::Import: {
      if (!a.busy) {
        g.drop = rect(cx, yc, cw, s(268));
        put(a.drop_text, g.drop.left + s(40), g.drop.top + s(132), cw - s(80), s(44));
        const int bw = s(220), gap = s(12), bx = cx + (cw - 2 * bw - gap) / 2, byy = g.drop.top + s(196);
        put(Item(a, kPrimary), bx, byy, bw, s(46));
        put(Item(a, kImportFolder), bx + bw + gap, byy, bw, s(46));
        if (!banner) {
          const int gapt = s(12), tw = (cw - 2 * gapt) / 3, ty = g.drop.bottom + s(16);
          for (int i = 0; i < 3; ++i) g.tiles[i] = rect(cx + i * (tw + gapt), ty, tw, s(100));
        }
      }
      break;
    }
    case Page::Settings: {
      const bool two = cw >= s(700);
      const int gap = s(16);
      const int colw = two ? (cw - gap) / 2 : cw;
      const int ow = colw - s(48);
      // Card heights follow their wrapped texts; storage actions pair up only when every label fits.
      const int hint_h = TextHeight(a.renderer_hint, ow);
      const int console_h = TextHeight(a.console_hint, ow);
      const int env_h = TextHeight(a.env_text, ow);
      int need = 0;
      for (int id : {kFolder, kStorage, kData, kCheck}) need = std::max(need, ButtonWidth(a, id, false));
      const bool pairs = 2 * need + s(12) <= ow;
      const int storage_rows = pairs ? 2 : 4;
      const int hg = s(52) + hint_h + s(12) + s(58) + s(8) + s(58) + s(132) + s(16) + s(36) + s(44) + s(20);
      const int hs = s(92) + storage_rows * s(46) - s(8) + s(20);
      const int hl = s(178) + console_h + s(20);
      const int hy = s(96) + env_h + s(20);
      // Lay the cards out from the viewport top, then shift them by the clamped scroll.
      const int top = yc;
      int lx = cx, rx = two ? cx + colw + gap : cx;
      g.cards[0] = rect(lx, top, colw, hg);                     // graphics
      g.cards[1] = two ? rect(rx, top, colw, hs) : rect(lx, top + hg + gap, colw, hs);  // storage
      g.cards[2] = rect(two ? rx : lx, g.cards[1].bottom + gap, colw, hl);              // console
      g.cards[3] = rect(lx, two ? g.cards[0].bottom + gap : g.cards[2].bottom + gap, colw, hy);  // desktop
      LONG content = 0;
      for (const auto& c : g.cards) content = std::max(content, c.bottom);
      g.viewport = RECT{sb + 1, top, W, H};
      g.scroll_max = std::max(0, int(content) + s(28) - H);
      a.scroll = std::clamp(a.scroll, 0, g.scroll_max);
      for (auto& c : g.cards) OffsetRect(&c, 0, -a.scroll);
      clip = g.viewport;
      const RECT& gf = g.cards[0];
      put(a.renderer_hint, gf.left + s(24), gf.top + s(52), ow, hint_h);
      const int oy = gf.top + s(52) + hint_h + s(12);
      put(Item(a, kRenderer), gf.left + s(24), oy, ow, s(58));
      put(Item(a, kRendererNative), gf.left + s(24), oy + s(66), ow, s(58));
      put(Item(a, kApiD3D12), gf.left + s(24), oy + s(132), ow, s(58));
      put(Item(a, kApiVulkan), gf.left + s(24), oy + s(198), ow, s(58));
      put(Item(a, kNativeDevelopment), gf.left + s(24), oy + s(264) + s(8), ow, s(36));
      put(Item(a, kFullscreen), gf.left + s(24), oy + s(264) + s(52), ow, s(36));
      const RECT& st = g.cards[1];
      put(a.root_text, st.left + s(24), st.top + s(56), ow, s(24));
      const int bw2 = pairs ? (ow - s(12)) / 2 : ow;
      int slot = 0;
      for (int id : {kFolder, kStorage, kData, kCheck}) {
        const int col = pairs ? slot % 2 : 0, row = pairs ? slot / 2 : slot;
        put(Item(a, id), st.left + s(24) + col * (bw2 + s(12)), st.top + s(92) + row * s(46), bw2, s(38));
        ++slot;
      }
      const RECT& lg = g.cards[2];
      put(a.console_language_label, lg.left + s(24), lg.top + s(50), ow, s(22));
      put(Item(a, kConsoleLanguage), lg.left + s(24), lg.top + s(76), ow, s(300));
      put(a.console_country_label, lg.left + s(24), lg.top + s(112), ow, s(22));
      put(Item(a, kConsoleCountry), lg.left + s(24), lg.top + s(138), ow, s(300));
      put(a.console_hint, lg.left + s(24), lg.top + s(178), ow, console_h);
      const RECT& sy = g.cards[3];
      const int chip_w = std::max({s(140), ButtonWidth(a, kLangPt, false), ButtonWidth(a, kLangEn, false)});
      put(Item(a, kLangPt), sy.left + s(24), sy.top + s(48), chip_w, s(36));
      put(Item(a, kLangEn), sy.left + s(24) + chip_w + s(8), sy.top + s(48), chip_w, s(36));
      put(a.env_text, sy.left + s(24), sy.top + s(96), ow, env_h);
      clip = {};
      break;
    }
    case Page::Diagnostics: {
      g.details_card = rect(cx, yc, cw, std::max(s(160), bottom - yc));
      put(Item(a, kCaptureCrash), cx + s(16), yc + s(14), cw - s(32), s(36));
      put(a.details, cx + s(16), yc + s(58), cw - s(32), g.details_card.bottom - g.details_card.top - s(72));
      break;
    }
  }
  a.geo = g;
  for (auto h : a.all) if (std::find(placed.begin(), placed.end(), h) == placed.end() &&
      (GetWindowLongPtrW(h, GWL_STYLE) & WS_VISIBLE))
    SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
  // A single invalidation; repeated layouts in one message coalesce into one paint.
  RedrawWindow(a.window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}
void Enable(App& a) {
  bool has = a.selected < a.targets.size();
  for (auto h : a.all) EnableWindow(h, !a.busy);
  EnableWindow(Item(a, kData), !a.root.empty());
  EnableWindow(Item(a, kStorage), !a.busy && !a.root.empty());
  EnableWindow(Item(a, kDetails), TRUE);
  EnableWindow(Item(a, kDismiss), TRUE);
  EnableWindow(Item(a, kCancel), a.busy && !a.session && !a.finishing_session && !a.installing);
  EnableWindow(Item(a, kLanguage), !a.busy);
  EnableWindow(a.details, TRUE);
  EnableWindow(a.status, TRUE); EnableWindow(a.activity_text, TRUE); EnableWindow(a.subtitle, TRUE);
  EnableWindow(a.headline, TRUE); EnableWindow(a.footer, TRUE); EnableWindow(a.root_text, TRUE);
  EnableWindow(a.drop_text, TRUE); EnableWindow(a.renderer_hint, TRUE); EnableWindow(a.env_text, TRUE);
  const bool action_ok = a.next_action == kFolder || a.next_action == kDashboards || a.next_action == kImportPage ||
    (a.next_action == kImport && !a.root.empty()) ||
    (a.next_action == kPrepare && has && (a.targets[a.selected].importable || a.targets[a.selected].generic_aot) && !a.current.prepared && (a.current.inputs_ok || a.current.verification_pending) && a.environment.writable && a.environment.architecture) ||
    (a.next_action == kVerifyTarget && a.current.verification_pending && a.current.launch_candidate) ||
    (a.next_action == kLaunch && (a.current.prepared || a.current.launch_candidate) && a.environment.graphics);
  EnableWindow(Item(a, kPrimary), !a.busy && action_ok);
  EnableWindow(Item(a, kRemove), !a.busy && !a.initializing && Removable(a));
  EnableWindow(Item(a, kSettings), !a.busy);
  EnableWindow(Item(a, kUpdate), !a.busy);
  const bool native = a.renderer_settings.experimental;
  EnableWindow(Item(a, kRenderer), !a.busy && !a.root.empty());
  EnableWindow(Item(a, kRendererNative), !a.busy && !a.root.empty() && native);
  EnableWindow(Item(a, kApiD3D12), !a.busy && !a.root.empty());
  EnableWindow(Item(a, kApiVulkan), !a.busy && !a.root.empty() && native && a.renderer_settings.renderer == Renderer::Xenos);
  EnableWindow(Item(a, kNativeDevelopment), !a.busy && !a.root.empty());
  EnableWindow(Item(a, kFullscreen), !a.busy && !a.root.empty());
  EnableWindow(Item(a, kConsoleLanguage), !a.busy && !a.root.empty());
  EnableWindow(Item(a, kConsoleCountry), !a.busy && !a.root.empty());
  EnableWindow(a.library_list, !a.busy);
  // Navigation reads only the published presentation snapshot, including while
  // verification or a session runs. Mutating actions remain serialized.
  for (int id : {kHome, kDashboards, kGames, kImportPage, kLibrary, kSearch, kFilter0, kFilter1, kFilter2, kFilter3})
    EnableWindow(Item(a, id), !a.initializing);
  EnableWindow(a.library_list, !a.initializing && a.checked.size() == a.targets.size());
  Layout(a);
}

// ---------- Texts per page ----------
void SetPageTexts(App& a) {
  switch (PageOf(a)) {
    case Page::Home:
      Set(a.headline, Tr("Seu Xbox 360, no PC.", "Your Xbox 360, on PC."));
      Set(a.subtitle, Tr("Configure sua dashboard original e adicione seus jogos.\r\nA experiência de console continua nos menus originais do Xbox 360.", "Set up your original dashboard and add your games.\r\nThe console experience stays in the original Xbox 360 menus.")); break;
    case Page::Library:
      Set(a.headline, Tr("Biblioteca", "Library"));
      Set(a.subtitle, Tr("Jogos importados e o estado de preparação de cada um.\r\nPara jogar, use a My Games dentro da dashboard original.", "Imported games and the preparation state of each one.\r\nTo play, use My Games inside the original dashboard.")); break;
    case Page::Import:
      Set(a.headline, Tr("Importar", "Import"));
      Set(a.subtitle, Tr("Adicione uma ISO, um pacote Xbox 360 (XBLA/STFS) ou uma pasta extraída.\r\nO conteúdo é extraído e verificado; seus originais ficam preservados.", "Add an ISO, an Xbox 360 package (XBLA/STFS) or an extracted folder.\r\nContent is extracted and verified; your originals stay untouched.")); break;
    case Page::Dashboard:
      Set(a.headline, Tr("Dashboard", "Dashboard"));
      Set(a.subtitle, Tr("Versões da dashboard original e o estado de cada preparação.\r\nCada versão usa somente seus próprios componentes.", "Original dashboard versions and the preparation state of each.\r\nEach version uses only its own components.")); break;
    case Page::Settings:
      Set(a.headline, Tr("Configurações", "Settings"));
      Set(a.subtitle, Tr("Renderer gráfico, armazenamento e idioma.\r\nTrocar uma pasta nunca apaga dados existentes.", "Graphics renderer, storage and language.\r\nChanging a folder never deletes existing data.")); break;
    case Page::Diagnostics:
      Set(a.headline, Tr("Diagnóstico", "Diagnostics"));
      Set(a.subtitle, Tr("O diagnóstico fica neste computador. Nada é enviado automaticamente.\r\nLogs de execução ficam na pasta local do ambiente.", "Diagnostics stay on this computer. Nothing is uploaded automatically.\r\nExecution logs live in the environment's local folder.")); break;
  }
  Set(a.drop_text, Tr("Tudo acontece neste computador. Apenas conteúdos com uma preparação disponível poderão ser executados; nenhum jogo ou dashboard está incluído.", "Everything stays on this computer. Only content with an available preparation can run; no games or dashboards are included."));
}
void SetActivity(App& a, int kind, const std::wstring& title, const std::string& text, std::vector<std::wstring> stages = {}) {
  a.activity_kind = kind; a.activity_title = title; a.stage_names = std::move(stages);
  a.started = GetTickCount64(); a.extracted_bytes = 0;
  Set(a.activity_text, text);
  SetTimer(a.window, 2, 33, nullptr);
}
void UpdateRendererControls(App& a) {
  Set(a.renderer_hint, Tr("O renderer vale para toda a sessão — dashboard e jogos — e é aplicado na próxima sessão. Não há configuração por título.", "The renderer applies to the whole session — dashboard and games — and takes effect on the next session. There is no per-title setting."));
  std::string env = a.environment.description;
  if (env.empty()) env = Tr("Escolha um ambiente para verificar este computador.", "Choose an environment to check this computer.");
  Set(a.env_text, env);
  InvalidateRect(a.window, nullptr, FALSE);
}
// A snapshot is for presentation only. Navigation never hashes the content.
// Populate it on the worker; operations revalidate before using the files.
std::vector<App::CheckedTarget> CheckContents(const fs::path& root, const std::vector<Target>& targets,
                                            InspectionMode mode = InspectionMode::Full) {
  ActionTrace trace(mode == InspectionMode::Full ? "content.snapshot" : "content.list");
  std::vector<App::CheckedTarget> checked;
  for (const auto& target : targets) {
    App::CheckedTarget item;
    item.state = Inspect(ProjectPaths{root}, target, mode);
    if (mode == InspectionMode::Full && item.state.inputs_ok) item.library = InspectLibrary(ProjectPaths{root}, target, targets);
    if (target.kind == "dashboard" && !target.inputs.empty()) {
      const auto& first = target.inputs.front();
      const auto file = target.preparation_source.empty() ? first.destination :
          target.preparation_source / first.destination.lexically_relative(target.game_root);
      if (mode == InspectionMode::Preview) item.added = fs::is_regular_file(file);
      else { const auto anchor = DigestFile(file); item.added = anchor && anchor->sha256 == first.sha256; }
    }
    item.added = item.added || InGameLibrary(target);
    checked.push_back(std::move(item));
  }
  return checked;
}
void AdoptSnapshot(App& a) {
  if (!a.pending_snapshot) return;
  a.targets = std::move(a.pending_targets);
  a.checked = std::move(a.pending_checked);
  a.catalog_loaded = true;
  a.pending_snapshot = false;
  ++a.hint_generation;
}
// Second phase of a listing: built titles' receipts by file times, so a row can
// say ready or stale without delaying the list. Presentation only.
void StartHints(App& a) {
  if (a.hint_check.joinable()) a.hint_check.join();
  if (a.root.empty() || a.checked.size() != a.targets.size()) return;
  std::vector<std::pair<size_t, Target>> todo;
  for (size_t i = 0; i < a.targets.size(); ++i) {
    const auto& state = a.checked[i].state;
    if (state.verification_pending && state.launch_candidate && !state.receipt_current && !state.receipt_stale)
      todo.emplace_back(i, a.targets[i]);
  }
  if (todo.empty()) return;
  a.hint_check = std::thread([&a, root = a.root, todo = std::move(todo), generation = a.hint_generation] {
    auto* found = new App::Hints{generation, {}};
    try {
      ActionTrace trace("ui.hints");
      ReceiptHints hints{ProjectPaths{root}};
      for (const auto& [index, target] : todo) {
        if (a.closing) break;
        found->items.emplace_back(index, hints.Check(target.id, target.manifest, target.exe));
      }
    } catch (const std::exception&) {}  // a hint never fails the listing
    if (a.closing || !PostMessageW(a.window, kHinted, 0, reinterpret_cast<LPARAM>(found))) delete found;
  });
}
void InspectSelected(App& a) {
  a.current = {};
  a.current.inputs_ok = false;
  const Page page = PageOf(a);
  SetPageTexts(a);
  a.selection_title.clear();
  auto chip = [&](const std::string& text, Tone tone) { a.chip_text = Wide(text); a.chip_tone = tone; };
  auto primary = [&](const std::string& text) { Set(Item(a, kPrimary), text); };
  if (a.selected >= a.targets.size() || a.checked.size() != a.targets.size()) {
    if (!a.root.empty() && (a.games || page == Page::Home || page == Page::Import)) {
      a.next_action = a.games || page == Page::Import ? kImportPage : kDashboards;
      chip(Tr("Biblioteca vazia", "Library is empty"), Tone::Neutral);
      Set(a.status, Tr("Os jogos que você adicionar aparecerão aqui com seu estado de preparação.\r\nTítulos cadastrados como compatíveis não contam como jogos instalados.", "Games you add appear here with their preparation status.\r\nCompatibility entries are not installed games."));
      primary(Tr("Adicionar conteúdo", "Add content"));
    } else {
      a.next_action = kFolder;
      chip(Tr("Ambiente não configurado", "Environment not set up"), Tone::Warn);
      Set(a.status, Tr("Escolha onde guardar seus arquivos. Depois, adicione suas dashboards e jogos.\r\nA preparação acontece neste computador; seus arquivos não são enviados.", "Choose where to keep your files, then add dashboards and games.\r\nPreparation happens on this computer; your files are never uploaded."));
      primary(Tr("Escolher pasta", "Choose folder"));
    }
    if (page == Page::Import) { a.next_action = kImport; primary(Tr("Escolher arquivo", "Choose file")); }
    Enable(a); return;
  }
  const auto& t = a.targets[a.selected];
  a.selection_title = Wide(english ? t.label_en : t.label);
  a.current = a.checked.at(a.selected).state;
  if (a.games && !a.import_view && !a.home && !t.aot) {
    a.next_action = kImportPage;
    const bool identified = a.current.inputs_ok || a.current.verification_pending;
    chip(identified ? Tr("Não suportado", "Not supported") : Tr("Erro", "Error"), identified ? Tone::Warn : Tone::Err);
    Set(a.status, Tr("Este jogo foi cadastrado, mas não tem preparação AOT disponível.\r\nEle não pode ser executado nem aparece na My Games.", "This game is catalogued but has no available AOT preparation.\r\nIt cannot run and does not appear in My Games."));
    primary(Tr("Adicionar conteúdo", "Add content"));
    std::string details = english ? t.limitations_en : t.limitations;
    details += "\r\nTitle ID: " + (t.title_id.empty() ? "—" : t.title_id) + "\r\nVersion: " + (t.version.empty() ? "—" : t.version);
    details += "\r\nMedia ID: " + t.media_id + "\r\nOriginal PE: " + t.original_name + "\r\n" + Utf8(t.game_root);
    for (const auto& issue : a.current.issues) details += "\r\n" + issue;
    Set(a.details, details); Enable(a); return;
  }
  if (a.current.verification_pending) {
    std::string detail = a.environment.description + "\r\n" + (english ? t.limitations_en : t.limitations);
    a.current.launch_candidate &= a.supervisor_available;
    for (const auto& dep : t.dependencies) {
      const auto found = std::find_if(a.targets.begin(), a.targets.end(), [&](const auto& candidate) { return candidate.id == dep; });
      if (found == a.targets.end()) { a.current.launch_candidate = false; detail += "\r\nPerfil necessário ausente: " + dep; }
      else {
        const auto& state = a.checked.at(size_t(found - a.targets.begin())).state;
        a.current.launch_candidate &= state.prepared || state.launch_candidate;
      }
    }
    a.next_action = a.current.launch_candidate && t.session ? kLaunch : kPrepare;
    primary(a.next_action == kLaunch ? Tr("Conferir e iniciar", "Verify and start") :
            a.next_action == kVerifyTarget ? Tr("Conferir preparação", "Verify preparation") : Tr("Conferir e preparar", "Verify and prepare"));
    // The row's listing hint, explained; the full check runs before any action.
    const auto row = StatusOf(a, a.selected);
    if (row == Row::Error) {
      chip(Tr("Falha na última tentativa", "Last attempt failed"), Tone::Err);
      Set(a.status, Tr("A última preparação ou execução falhou. O motivo está no Diagnóstico.\r\nPreparar de novo retoma as etapas válidas.",
                      "The last preparation or run failed. The reason is in Diagnostics.\r\nPreparing again resumes the valid steps."));
    } else if (row == Row::Imported) {
      chip(a.current.receipt_stale ? Tr("Precisa preparar de novo", "Needs preparing again") : Tr("Não preparado", "Not prepared"), Tone::Warn);
      Set(a.status, a.current.receipt_stale ?
          Tr("Arquivos mudaram desde a última preparação, por exemplo numa atualização do aplicativo.\r\nPrepare novamente para usar esta versão.",
             "Files changed since the last preparation, for example in an application update.\r\nPrepare again to use this version.") :
          Tr("Ainda não preparado neste computador.\r\nA preparação compila o título; seus arquivos são conferidos antes.",
             "Not prepared on this computer yet.\r\nPreparation compiles the title; your files are verified first."));
    } else if (row == Row::Ready) {
      chip(t.session ? Tr("Pronta para iniciar", "Ready to start") : Tr("Preparado", "Prepared"), Tone::Ok);
      primary(t.session ? Tr("Iniciar Xbox 360", "Start Xbox 360") : Tr("Preparado para My Games", "Prepared for My Games"));
      Set(a.status, Tr("Preparação em dia. Inicie seus jogos pela My Games da dashboard.",
                      "Preparation up to date. Start your games through the dashboard's My Games."));
    } else {
      chip(Tr("Verificação pendente", "Verification pending"), Tone::Info);
      Set(a.status, Tr("Conteúdo listado. Arquivos e recibos serão conferidos antes de preparar ou iniciar.\r\nEsta listagem ainda não comprova integridade ou compatibilidade.",
                      "Content listed. Files and receipts will be verified before preparation or launch.\r\nThis listing does not yet establish integrity or compatibility."));
    }
    for (const auto& issue : a.current.issues) detail += "\r\n" + issue;
    if (page == Page::Home && !a.current.launch_candidate) { a.next_action = kDashboards; primary(Tr("Configurar Xbox 360", "Set up Xbox 360")); }
    if (page == Page::Import) { a.next_action = kImport; primary(Tr("Escolher arquivo", "Choose file")); }
    if (!t.importable && !t.generic_aot && !a.current.launch_candidate) { a.next_action = kDashboards; primary(Tr("Escolher dashboard", "Choose dashboard")); }
    Set(a.details, detail); Enable(a); return;
  }
  std::string missing = a.current.inputs_ok ? "" : (english ? t.label_en : t.label);
  std::string detail;
  detail += a.environment.description + "\r\n";
  for (const auto& issue : a.current.issues) detail += issue + "\r\n";
  for (const auto& dep : t.dependencies) {
    auto it = std::find_if(a.targets.begin(), a.targets.end(), [&](const auto& candidate) { return candidate.id == dep; });
    if (it == a.targets.end()) {
      a.current.prepared = false; a.current.inputs_ok = false;
      detail += "Perfil necessário não encontrado: " + dep + "\r\n";
      continue;
    }
    auto r = a.checked.at(size_t(it - a.targets.begin())).state;
    if (missing.empty() && !r.inputs_ok) missing = english ? it->label_en : it->label;
    a.current.inputs_ok = a.current.inputs_ok && r.inputs_ok;
    a.current.prepared = a.current.prepared && r.prepared;
    for (const auto& issue : r.issues) detail += dep + ": " + issue + "\r\n";
  }
  if (a.current.inputs_ok) {
    for (const auto& issue : a.checked.at(a.selected).library) {
      a.current.prepared = false; detail += issue + "\r\n";
    }
  }
  if (!a.supervisor_available) {
    a.current.prepared = false;
    detail += "Falta o supervisor ao lado do aplicativo.\r\n";
  }
  a.next_action = !a.current.inputs_ok ? kImport : !a.current.prepared ? kPrepare : t.session ? kLaunch : kPrepare;
  primary(a.next_action == kImport ? Tr("Selecionar arquivos", "Choose files") :
          a.next_action == kPrepare ? Tr("Verificar e preparar", "Verify and prepare") : Tr("Abrir dashboard", "Open dashboard"));
  std::string state = !missing.empty() ? Tr("Próximo conjunto: ", "Next set: ") + missing :
    a.current.prepared ? Tr("Conteúdo verificado. Inicie seus jogos pela My Games da dashboard.\r\nO menu do próprio jogo permite retornar à dashboard.", "Content verified. Launch games through the dashboard's My Games.\r\nReturn to the dashboard from the game's own menu.") :
    Tr("Conteúdo verificado. A preparação será feita nesta pasta.\r\nDownloads e instalações serão informados antes de começar.", "Content verified. Preparation will use this folder.\r\nDownloads and installations will be explained before they start.");
  if (!a.current.inputs_ok) {
    const bool added = a.checked.at(a.selected).added;
    state = (added ? Tr("Conteúdo incompleto", "Content incomplete") : Tr("Aguardando seus arquivos", "Waiting for your files")) + "\r\n" +
      Tr("Adicione seus arquivos originais para continuar. Os componentes necessários estão listados no Diagnóstico.", "Add your original files to continue. Required components are listed in Diagnostics.");
    if (!missing.empty()) state += "\r\n" + Tr("Próximo conjunto: ", "Next set: ") + missing;
    chip(added ? Tr("Conteúdo incompleto", "Content incomplete") : Tr("Aguardando arquivos", "Waiting for files"), added ? Tone::Err : Tone::Warn);
  } else if (!a.current.prepared) chip(Tr("Pronto para preparar", "Ready to prepare"), Tone::Info);
  else chip(t.session ? Tr("Pronta para iniciar", "Ready to start") : Tr("Preparado para a My Games", "Prepared for My Games"), Tone::Ok);
  if (!t.importable && !t.generic_aot) {
    state = Tr("Preparação ainda indisponível para esta versão.\r\nConsulte a pendência no Diagnóstico; seus arquivos serão preservados.", "Preparation is not yet available for this version.\r\nSee Diagnostics for its requirements; your files are preserved.");
    chip(Tr("Indisponível", "Unavailable"), Tone::Warn);
  }
  if (t.session && !t.launch_library && a.current.inputs_ok && a.current.prepared)
    state = Tr("Dashboard original preparada para iniciar.\r\nConsulte as limitações desta versão no Diagnóstico.",
               "Original dashboard prepared to start.\r\nSee this version's limitations in Diagnostics.");
  if (t.generic_aot) state = a.current.prepared ? Tr("Build AOT verificado. Compatibilidade de execução ainda não comprovada.\r\nTente iniciar pela My Games original.", "AOT build verified. Runtime compatibility is not yet proven.\r\nTry launching through original My Games.") :
      Tr("Preparação AOT automática disponível. Compilar não garante execução.\r\nFalhas e estágios ficam registrados no Diagnóstico para nova tentativa.", "Automatic AOT preparation available. Compilation does not guarantee execution.\r\nErrors and stages are saved in Diagnostics for retry.");
  if (a.games && a.current.prepared) primary(Tr("Preparado para My Games", "Prepared for My Games"));
  detail += "\r\n" + (english ? t.limitations_en : t.limitations);
  if (t.catalogued) {
    detail += "\r\nTitle ID: " + (t.title_id.empty() ? "—" : t.title_id) +
      "\r\nVersion: " + (t.version.empty() ? "—" : t.version) +
      "\r\nMedia ID: " + (t.media_id.empty() ? "—" : t.media_id) + "\r\nOriginal PE: " + t.original_name;
  }
  if (!t.user_root.empty()) detail += "\r\nData: " + Utf8(t.user_root);
  Set(a.status, state);
  Set(a.details, detail.empty() ? "Verificação concluída. Nenhum dado é enviado automaticamente.\r\nLogs de execução: " + Utf8(a.root / ".local/evidence/runs") : detail);
  if (page == Page::Home) {
    a.next_action = a.current.prepared ? kLaunch : kDashboards;
    primary(a.current.prepared ? Tr("Iniciar Xbox 360", "Start Xbox 360") : Tr("Configurar Xbox 360", "Set up Xbox 360"));
    if (a.current.prepared) { chip(Tr("Pronta para iniciar", "Ready to start"), Tone::Ok); Set(a.status, (english ? t.label_en : t.label) + "\r\n" + Tr("Abra o menu original do Xbox 360. Seus jogos continuam na My Games, dentro da dashboard.", "Open the original Xbox 360 menu. Your games stay in My Games, inside the dashboard.")); }
    else { chip(Tr("Configuração pendente", "Setup pending"), Tone::Warn); Set(a.status, Tr("Configure uma dashboard original para começar.\r\nJogos são adicionados separadamente, em Importar.", "Set up an original dashboard to get started.\r\nGames are added separately, in Import.")); }
  } else if (page == Page::Import) {
    a.next_action = kImport; primary(Tr("Escolher arquivo", "Choose file"));
  } else if (a.next_action == kImport) {
    a.next_action = kImportPage; primary(Tr("Adicionar conteúdo", "Add content"));
  }
  Enable(a);
}
void Load(App& a, const fs::path& root, InspectionMode mode = InspectionMode::Preview) {
  ActionTrace trace("ui.load");
  if (a.busy && a.initializing) { SetPageTexts(a); Enable(a); return; }
  if (a.root != root) { a.targets.clear(); a.checked.clear(); a.catalog_loaded = false; }
  a.dashboard_indices.clear(); a.rows.clear(); a.selected = 0;
  SendMessageW(a.library_list, LB_RESETCONTENT, 0, 0);
  // Hardware probing is independent of navigation between pages.
  const bool probe = a.environment.description.empty() || a.root != root;
  a.root = root;
  if (!a.busy && (mode == InspectionMode::Full || probe || !a.catalog_loaded || a.checked.size() != a.targets.size())) {
    if (a.worker.joinable()) a.worker.join();
    a.busy = true; a.initializing = true; a.cancel = false;
    SetActivity(a, 4, mode == InspectionMode::Full ? WTr("Conferindo integridade", "Checking integrity") : WTr("Carregando biblioteca", "Loading library"),
      mode == InspectionMode::Full ? Tr("Conferindo arquivos e recibos. Você pode cancelar.", "Verifying files and receipts. You can cancel.") :
      Tr("Listando seu conteúdo. A integridade completa é conferida ao preparar ou iniciar.", "Listing your content. Full integrity is checked when preparing or launching."));
    SetPageTexts(a); Enable(a);
    a.worker = std::thread([&a, root, mode] {
      try {
        DigestBatch batch([&a] { return a.cancel.load(); });
        a.pending_environment = CheckEnvironment(root);
        a.pending_renderer = root.empty() ? RendererSettings{} : LoadRendererSettings(ProjectPaths{root});
        a.pending_console = root.empty() ? ConsoleSettings{} : LoadConsoleSettings(ProjectPaths{root});
        a.pending_supervisor_available = fs::is_regular_file(Supervisor(a));
        a.pending_targets = root.empty() ? std::vector<Target>{} : ReadCatalog(ProjectPaths{root});
        a.pending_checked = CheckContents(root, a.pending_targets, mode);
        a.pending_snapshot = true;
        Post(a.window, kInspected, "");
      } catch (const std::exception& error) { Post(a.window, kInspected, error.what()); }
    });
    return;
  }
  for (int id : {kConsoleLanguage, kConsoleCountry}) {
    const auto combo = Item(a, id);
    const auto selected = id == kConsoleLanguage ? a.console_settings.language : a.console_settings.country;
    for (int i = 0; i < SendMessageW(combo, CB_GETCOUNT, 0, 0); ++i)
      if (SendMessageW(combo, CB_GETITEMDATA, i, 0) == selected) SendMessageW(combo, CB_SETCURSEL, i, 0);
  }
  UpdateRendererControls(a);
  // Display only: a shortcut's "scripts\.." root reads as the folder it names.
  auto shown = root.lexically_normal();
  if (shown.has_relative_path() && !shown.has_filename()) shown = shown.parent_path();
  SetWindowTextW(a.root_text, root.empty() ? WTr("Nenhum ambiente selecionado", "No environment selected").c_str() : shown.c_str());
  a.selected = a.targets.size();
  for (size_t i = 0; i < a.targets.size(); ++i) {
    if ((a.targets[i].kind != "dashboard") != a.games) continue;
    if (a.games && !a.checked.at(i).added) continue;
    a.dashboard_indices.push_back(i);
    if (!a.games && (a.dashboard_indices.size() == 1 || a.targets[i].session)) a.selected = i;
  }
  if (a.games && !a.dashboard_indices.empty()) a.selected = a.dashboard_indices.front();
  RebuildRows(a);
  if (a.games && !a.rows.empty() && std::find(a.rows.begin(), a.rows.end(), a.selected) == a.rows.end()) a.selected = a.rows.front();
  SyncSelection(a);
  InspectSelected(a);
}
void SavePreferences(const App& a) {
  if (a.preferences.empty()) return;
  fs::create_directories(a.preferences.parent_path());
  std::ofstream out(a.preferences, std::ios::binary);
  const bool portable = a.root == a.preferences.parent_path() / "environment";
  out << (english ? "en" : "pt") << '\n' << (portable ? "." : Utf8(a.root)) << '\n';
}
void CheckEnvironmentAsync(App& a, const fs::path& root, bool verify_contents = false) {
  if (a.busy) return;
  if (a.worker.joinable()) a.worker.join();
  a.pending_root = root; a.pending_full_check = verify_contents; a.busy = true; a.initializing = true; a.cancel = false;
  SetActivity(a, 4, WTr("Verificando ambiente", "Checking environment"), Tr("Conferindo a instalação em segundo plano.", "Verifying the installation in the background."));
  Enable(a);
  a.worker = std::thread([&a, root] {
    try {
      DigestBatch batch([&a] { return a.cancel.load(); });
      VerifyDistribution(a.self.parent_path());
      a.pending_payload = InspectEnvironment(Payload(a), root);
      const auto installed = DigestFile(root / ".local/payload.sha256");
      const auto packaged = DigestFile(Payload(a) / "payload.sha256");
      a.pending_payload_current = a.pending_payload.current && installed && packaged && installed->sha256 == packaged->sha256;
      Post(a.window, kEnvironmentChecked, "");
    } catch (const std::exception& error) { Post(a.window, kEnvironmentChecked, error.what()); }
  });
}
// ---------- Self-update (installed packages only) ----------
fs::path SystemTool(const wchar_t* name) {
  wchar_t sys[MAX_PATH]{}; GetSystemDirectoryW(sys, MAX_PATH);
  return fs::path(sys) / name;
}
fs::path Updates(const App& a) { return a.preferences.parent_path() / "updates"; }
// HTTPS only, redirects included; the Windows curl.exe avoids linking a network stack.
void Download(const App& a, const std::string& url, const fs::path& file, int seconds, const std::atomic<bool>& stop) {
  ProcessSpec spec;
  spec.exe = SystemTool(L"curl.exe");
  spec.args = {"-fsSL", "--proto", "=https", "--proto-redir", "=https", "--connect-timeout", "15", "--max-time", std::to_string(seconds),
               "-A", std::string("x360-native/") + X360N_VERSION, "-H", "Accept: application/vnd.github+json", "-o", Utf8(file), url};
  spec.working_dir = file.parent_path();
  spec.stdout_file = Updates(a) / "curl.log"; spec.stderr_file = Updates(a) / "curl.error.log";
  spec.no_console = true;
  spec.should_cancel = [&stop] { return stop.load(); };
  const auto result = RunProcess(spec);
  if (result.cancelled) throw std::runtime_error(Tr("Atualização cancelada. Nada foi alterado.", "Update cancelled. Nothing was changed."));
  if (!result.launched || result.exit_code != 0)
    throw std::runtime_error(Tr("Não foi possível baixar ", "Could not download ") + url + " (" + result.launch_error + " " + DescribeExitCode(result.exit_code) + ")");
}
// Packages ask GitHub Releases once per start; nothing else is sent.
void CheckForUpdate(App& a) {
  if (a.preferences.empty() || a.update_check.joinable()) return;
  a.update_check = std::thread([&a] {
    auto* found = new std::optional<AvailableUpdate>;
    try {
      fs::create_directories(Updates(a));
      const auto list = Updates(a) / "releases.json";
      Download(a, kReleasesApi, list, 30, a.closing);
      std::ifstream in(list, std::ios::binary);
      *found = SelectUpdate({std::istreambuf_iterator<char>(in), {}}, X360N_VERSION, a.update_channel);
    } catch (const std::exception&) { found->reset(); }  // offline or rate limited: try again next start
    if (a.closing || !PostMessageW(a.window, kUpdateChecked, 0, reinterpret_cast<LPARAM>(found))) delete found;
  });
}
void StartUpdate(App& a) {
  if (a.busy || !a.update) return;
  if (a.worker.joinable()) a.worker.join();
  const auto update = *a.update;
  a.cancel = false; a.failed = false; a.busy = true; a.result = 0; a.stage = 0; a.last_action = kUpdate;
  a.show_details = false; a.settings = false;
  SetActivity(a, 1, WTr("Atualizando o aplicativo", "Updating the application"),
    Tr("Baixando a versão ", "Downloading version ") + update.version + Tr(" do GitHub. Seus dados em data\\ são preservados.", " from GitHub. Your data in data\\ is preserved."),
    {WTr("Download", "Download"), WTr("Verificação", "Verification"), WTr("Instalação", "Installation")});
  SetPageTexts(a); Enable(a);
  a.worker = std::thread([&a, update] {
    try {
      const auto dir = Updates(a) / update.version;
      fs::remove_all(dir);
      fs::create_directories(dir / "extract");
      const auto archive = dir / (update.package + ".zip"), checksum = dir / (update.package + ".zip.sha256");
      Download(a, update.checksum_url, checksum, 60, a.cancel);
      Download(a, update.archive_url, archive, 3600, a.cancel);
      a.stage = 1;
      Post(a.window, kProgress, Tr("Conferindo o SHA-256 e o conteúdo do pacote...", "Checking the SHA-256 and the package contents..."));
      VerifyArchive(archive, checksum);
      ProcessSpec tar;
      tar.exe = SystemTool(L"tar.exe"); tar.args = {"-xf", Utf8(archive), "-C", Utf8(dir / "extract")};
      tar.working_dir = dir; tar.stdout_file = dir / "tar.log"; tar.stderr_file = dir / "tar.error.log"; tar.no_console = true;
      tar.should_cancel = [&a] { return a.cancel.load(); };
      const auto extracted = RunProcess(tar);
      if (extracted.cancelled) throw std::runtime_error(Tr("Atualização cancelada. Nada foi alterado.", "Update cancelled. Nothing was changed."));
      if (!extracted.launched || extracted.exit_code != 0) throw std::runtime_error(Tr("Não foi possível extrair a atualização. Nada foi alterado.", "Could not extract the update. Nothing was changed."));
      if (a.cancel) throw std::runtime_error(Tr("Atualização cancelada. Nada foi alterado.", "Update cancelled. Nothing was changed."));
      a.stage = 2;
      Post(a.window, kProgress, Tr("Instalando. A versão anterior fica em data\\updates.", "Installing. The previous version stays in data\\updates."));
      const auto transaction = Updates(a) / ("txn-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
      ApplyUpdate(a.self.parent_path(), dir / "extract" / update.package, update.version, transaction);
      std::error_code ec; fs::remove_all(dir, ec);  // only this download
      Post(a.window, kUpdateFinished, "");
    } catch (const std::exception& error) { Post(a.window, kUpdateFinished, error.what()); }
  });
}
void Labels(App& a) {
  Set(a.console_language_label, Tr("Idioma da dashboard, Guide e jogos", "Dashboard, Guide and game language"));
  Set(a.console_country_label, Tr("Região / localidade", "Region / locale"));
  Set(a.console_hint, Tr("Aplicado ao iniciar uma nova sessão.\r\nIdiomas disponíveis dependem dos recursos originais de cada versão e jogo.", "Applies when starting a new session.\r\nAvailable languages depend on each version's and game's original resources."));
  for (auto [id, pt, en] : {std::tuple{kFolder, "Selecionar ambiente", "Choose environment"},
       {kCheck, "Verificar ambiente", "Check environment"},
       {kDetails, "Diagnóstico", "Diagnostics"}, {kData, "Abrir dados locais", "Open local data"},
       {kCancel, "Cancelar", "Cancel"}, {kStorage, "Pasta de saves", "Save data folder"},
       {kSettings, "Configurações", "Settings"}, {kImportFolder, "Escolher pasta", "Choose folder"},
       {kDashboards, "Dashboard", "Dashboard"}, {kGames, "Biblioteca", "Library"}, {kHome, "Início", "Home"},
       {kImportPage, "Importar", "Import"}, {kAddContent, "Adicionar conteúdo", "Add content"}, {kLibrary, "Ver biblioteca", "View library"},
       {kFilter0, "Todos", "All"}, {kFilter1, "Prontos", "Ready"}, {kFilter2, "Preparar", "To prepare"}, {kFilter3, "Atenção", "Attention"},
       {kLangPt, "Português (BR)", "Portuguese (BR)"}, {kLangEn, "English", "English"},
       {kRenderer, "Renderer · Xenos (ReXGlue)", "Renderer · Xenos (ReXGlue)"}, {kRendererNative, "Renderer · Native (Experimental)", "Renderer · Native (Experimental)"},
       {kApiD3D12, "API gráfica · Direct3D 12", "Graphics API · Direct3D 12"}, {kApiVulkan, "API gráfica · Vulkan", "Graphics API · Vulkan"},
       {kNativeDevelopment, "Acesso de desenvolvimento ao Native", "Native development access"},
       {kFullscreen, "Iniciar sessão em tela cheia", "Start session in fullscreen"},
       {kCaptureCrash, "Capturar pilha de falhas nas próximas sessões", "Capture crash stacks in upcoming sessions"},
       {kRetry, "Tentar novamente", "Try again"}, {kBannerDetails, "Diagnóstico", "Diagnostics"}, {kBannerLibrary, "Ver biblioteca", "View library"},
       {kDismiss, "Fechar", "Dismiss"}, {kRemove, "Remover da biblioteca", "Remove from library"}}) Set(Item(a, id), Tr(pt, en));
  Set(Item(a, kLanguage), english ? "Language: English" : "Idioma: Português");
  if (a.update) Set(Item(a, kUpdate), Tr("Instalar ", "Install ") + a.update->version);
  SendMessageW(a.search, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(WTr("Buscar jogo, Title ID ou versão", "Search game, Title ID or version").c_str()));
  Set(a.footer, Tr("Prévia v", "Preview v") + X360N_VERSION + Tr(" · Windows\r\nSeus arquivos permanecem neste computador.", " · Windows\r\nYour files stay on this computer."));
  UpdateRendererControls(a);
}
fs::path PickFolder(HWND owner, const wchar_t* title, bool folder = true) {
  IFileOpenDialog* dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return {};
  DWORD options = 0; dialog->GetOptions(&options);
  dialog->SetOptions(options | (folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST) | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
  dialog->SetTitle(title);
  fs::path result;
  if (SUCCEEDED(dialog->Show(owner))) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(dialog->GetResult(&item))) {
      PWSTR path = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) { result = path; CoTaskMemFree(path); }
      item->Release();
    }
  }
  dialog->Release(); return result;
}
std::wstring FormatBytes(uintmax_t bytes) {
  wchar_t text[64];
  const double gib = double(bytes) / (1024.0 * 1024.0 * 1024.0);
  if (bytes >= (1ull << 30)) swprintf_s(text, L"%.2f GB", gib);
  else if (bytes >= (1ull << 20)) swprintf_s(text, L"%.0f MB", double(bytes) / (1024.0 * 1024.0));
  else swprintf_s(text, L"%.0f KB", double(bytes) / 1024.0);
  return text;
}
void Start(App& a, int action, fs::path source = {}) {
  ActionTrace trace("ui.start");
  if (a.busy || (action != kImport && a.selected >= a.targets.size())) return;
  if (a.root.empty()) throw std::runtime_error("Choose an environment before importing content.");
  if (action == kLaunch && !a.targets[a.selected].session) return;
  if (a.worker.joinable()) a.worker.join();
  a.logs = a.root / ".local/evidence/logs/desktop" / std::to_string(GetTickCount64());
  a.cancel = false; a.failed = false; a.busy = true; a.session = false; a.installing = action == kInstall; a.importing = action == kImport; a.import_recognized = false; a.imported_id.clear();
  a.result = 0; a.last_action = action; a.last_source = source;
  const auto target = a.selected < a.targets.size() ? a.targets[a.selected] : Target{};
  a.operation_target = target.id;
  a.launch_verified = false;
  a.pending_snapshot = false;
  a.preparing_id = action == kPrepare ? target.id : std::string();
  a.stage = 0;
  if (action == kImport)
    SetActivity(a, 1, WTr("Importando conteúdo", "Importing content"), Tr("Verificando o conjunto selecionado antes de copiar...", "Verifying the selected set before copying..."),
      {WTr("Extração", "Extraction"), WTr("Verificação", "Verification"), WTr("Importação", "Import")});
  else if (action == kLaunch)
    SetActivity(a, 3, WTr("Preparando sessão", "Preparing session"), Tr("Conferindo a integridade antes de abrir a dashboard.", "Verifying integrity before opening the dashboard."));
  else if (action == kVerifyTarget)
    SetActivity(a, 4, WTr("Conferindo preparação", "Checking preparation"), Tr("Conferindo os arquivos e recibos do título selecionado. Você pode cancelar.", "Verifying the selected title's files and receipts. You can cancel."));
  else if (action == kInstall)
    SetActivity(a, 4, WTr("Instalando componentes", "Installing components"), Tr("Conclua ou cancele pelo instalador da Microsoft e aguarde o retorno.", "Finish or cancel in the Microsoft installer and wait for it to return."));
  else if (action == kRemove)
    SetActivity(a, 4, WTr("Removendo da biblioteca", "Removing from library"), Tr("Retirando o jogo da My Games e apagando a cópia importada. Saves e perfis são mantidos.", "Withdrawing the game from My Games and deleting the imported copy. Saves and profiles are kept."));
  else
    SetActivity(a, 2, WTr("Preparando para você", "Preparing for you"), Tr("Preparação em andamento. As etapas reais serão registradas no Diagnóstico.", "Preparation running. Actual stages are recorded in Diagnostics."),
      {WTr("Ferramentas", "Tools"), WTr("Configuração", "Configuration"), WTr("Tradução", "Translation"), WTr("Compilação", "Compilation"), WTr("Registro", "Registration")});
  a.show_details = false; a.settings = false;
  SetPageTexts(a);
  Enable(a);
  SetTimer(a.window, 2, 33, nullptr);
  const bool capture_crash = a.capture_crash;
  a.worker = std::thread([&a, target, action, source, capture_crash] {
    auto verify_target = [&] {
      if (!a.pending_snapshot) {
        a.pending_targets = a.targets; a.pending_checked = a.checked;
      }
      auto check = [&](const std::string& id) -> Inspection {
        auto it = std::find_if(a.pending_targets.begin(), a.pending_targets.end(), [&](const auto& t) { return t.id == id; });
        if (it == a.pending_targets.end()) {
          Inspection missing; missing.inputs_ok = false;
          missing.issues.push_back("Required preparation profile is missing: " + id); return missing;
        }
        Post(a.window, kProgress, Tr("Conferindo ", "Verifying ") + (english ? it->label_en : it->label));
        auto& item = a.pending_checked.at(size_t(it - a.pending_targets.begin()));
        item.state = Inspect(ProjectPaths{a.root}, *it); item.library.clear();
        if (item.state.prepared) item.library = InspectLibrary(ProjectPaths{a.root}, *it, a.pending_targets);
        if (!item.library.empty()) item.state.prepared = false;
        return item.state;
      };
      auto state = check(target.id);
      for (const auto& dep : target.dependencies) {
        const auto dependency = check(dep);
        state.inputs_ok = state.inputs_ok && dependency.inputs_ok;
        state.prepared = state.prepared && dependency.prepared;
        state.issues.insert(state.issues.end(), dependency.issues.begin(), dependency.issues.end());
      }
      a.pending_snapshot = true;
      return state;
    };
    auto finish = [&](const std::string& message) {
      ActionTrace trace("operation.finish");
      // Session completion must never wait for an unrelated catalog/hash scan.
      try { if (action != kLaunch && action != kVerifyTarget && !a.cancel.load()) {
        DigestBatch batch([&a] { return a.cancel.load(); });
        a.pending_targets = action == kImport || action == kRemove ? ReadCatalog(ProjectPaths{a.root}) : a.targets;
        a.pending_checked = CheckContents(a.root, a.pending_targets, InspectionMode::Preview);
        a.pending_snapshot = true;
        if (action == kPrepare && !a.failed) verify_target();
      } }
      catch (const std::exception& error) { a.failed = true; Post(a.window, kFinished, error.what()); return; }
      std::ofstream(a.logs / "result.txt", std::ios::binary) << message << '\n';
      Post(a.window, kFinished, message);
    };
    try {
      fs::create_directories(a.logs);
      if (!a.preferences.empty()) {
        ActionTrace trace("environment.preflight");
        VerifyDistribution(a.self.parent_path());
        if (!InspectEnvironment(Payload(a), a.root).current)
          throw std::runtime_error("Esta instalação precisa ser atualizada. Selecione novamente o ambiente em Configurações. Seus jogos e dados serão preservados.");
      }
      if (action == kRemove) {
        RemoveGame(ProjectPaths{a.root}, a.targets, target.id);
        finish((english ? target.label_en : target.label) + "\r\n" + Tr("Removido da biblioteca e da My Games. Saves, perfis e o arquivo original foram mantidos.", "Removed from the library and My Games. Saves, profiles and the original file were kept.")); return;
      }
      if (action == kPrepare && !target.aot) throw std::runtime_error("Import a valid XEX game before preparation.");
      if (action == kPrepare && !target.preparation_source.empty())
        Import(ProjectPaths{a.root}, target, target.preparation_source, [&] { return a.cancel.load(); });
      if (action == kPrepare || action == kVerifyTarget || action == kLaunch) {
        ActionTrace trace("launch.preflight");
        if (action != kVerifyTarget) {
          Post(a.window, kProgress, Tr("Conferindo a preparação e concluindo o registro na My Games...", "Checking preparation and completing My Games registration..."));
          if (CompleteLibraryPreparation(ProjectPaths{a.root}, target, a.targets, [&] { return a.cancel.load(); })) {
            if (action == kPrepare) {
              finish(Tr("Preparação concluída. Conteúdo verificado e registrado na My Games.", "Preparation complete. Content verified and registered in My Games.")); return;
            }
          } else if (action == kLaunch) {
            const auto state = verify_target();
            std::string reason = Tr("A preparação mudou. Selecione Preparar para atualizar os componentes antes de iniciar.", "Preparation changed. Select Prepare to update components before starting.");
            for (const auto& issue : state.issues) reason += "\r\n" + issue;
            throw std::runtime_error(reason);
          }
        }
        if (action != kLaunch) {
          DigestBatch batch([&a] { return a.cancel.load(); });
          const auto state = verify_target();
          if (action == kVerifyTarget) {
            finish(Tr("Verificação concluída. Consulte o estado e o Diagnóstico do título.", "Verification finished. See the title's status and Diagnostics.")); return;
          }
          if (!state.inputs_ok)
            throw std::runtime_error(Tr("O conteúdo mudou desde a verificação. Confira os detalhes e prepare novamente.", "Content changed since verification. Check details and prepare again."));
        }
      }
      if (action == kImport) {
        ImportSource content(source, Supervisor(a).parent_path() / "x360n-extract.exe", a.logs,
          [&] { return a.cancel.load(); }, [&] {
            Post(a.window, kProgress, Tr("Extraindo o jogo...\r\nIsso pode levar alguns minutos. Você pode cancelar; o arquivo original será preservado.", "Extracting the game...\r\nThis may take a few minutes. You can cancel; the original file will be preserved."));
          });
        a.stage = 1;
        Post(a.window, kProgress, Tr("Identificando o conteúdo...\r\nConferindo versão, Title ID e arquivos antes de adicionar.", "Identifying the content...\r\nChecking version, Title ID and files before adding."));
        if (fs::is_directory(content.path()) && fs::is_regular_file(content.path() / "default.xex")) {
          a.stage = 2;
          a.imported_id = ImportGame(ProjectPaths{a.root}, a.targets, content.path(), source, [&] { return a.cancel.load(); });
          a.import_recognized = true;
          finish(Tr("Jogo adicionado à Biblioteca. Importar não significa poder executar.", "Game added to Library. Importing does not mean it can run.")); return;
        }
        const auto found = IdentifyContent(a.targets, content.identity_path(), [&] { return a.cancel.load(); });
        std::vector<std::string> ids;
        for (const auto& item : found) if (item.recognized && std::find(ids.begin(), ids.end(), item.target_id) == ids.end()) ids.push_back(item.target_id);
        if (ids.size() > 1) throw std::runtime_error(Tr("Há mais de uma versão nesta pasta. Escolha a subpasta da versão que deseja adicionar. Nada foi copiado.", "This folder contains multiple versions. Choose the version's subfolder. Nothing was copied."));
        if (ids.empty()) {
          std::string message = Tr("Nenhum jogo compatível foi encontrado. O original foi preservado. Selecione outra ISO, pacote Xbox 360 ou pasta extraída.", "No compatible game was found. The original was preserved. Choose another ISO, Xbox 360 package, or extracted folder.");
          if (!found.empty()) message = (english ? "Content identified" : found.front().label) + (found.front().version.empty() ? "" : " — " + found.front().version) + "\r\n" + (english ? "No preparation is available for this file. Select supported, complete extracted content." : found.front().problem);
          finish(message); return;
        }
        auto selected = *std::find_if(a.targets.begin(), a.targets.end(), [&](const auto& t) { return t.id == ids.front(); });
        if (selected.kind == "game") {
          a.imported_id = ImportGame(ProjectPaths{a.root}, a.targets, content.path(), source, [&] { return a.cancel.load(); });
          a.import_recognized = true;
          finish(Tr("Jogo adicionado à Biblioteca. Importar não significa poder executar.", "Game added to Library. Importing does not mean it can run.")); return;
        }
        a.imported_id = selected.id;
        if (!selected.importable) { finish((english ? selected.label_en : selected.label) + "\r\n" + Tr("Esta versão foi reconhecida. Preparação ainda indisponível; consulte o Diagnóstico.", "This version was recognized. Preparation is not yet available; see Diagnostics.")); return; }
        a.stage = 2;
        Post(a.window, kProgress, Tr("Conferindo e adicionando o conteúdo extraído...", "Checking and adding the extracted content..."));
        Import(ProjectPaths{a.root}, selected, content.path(), [&] { return a.cancel.load(); }, !content.extracted());
        a.import_recognized = true;
        finish((english ? selected.label_en : selected.label) + "\r\n" + Tr("Conteúdo verificado e adicionado. Seus originais foram preservados.", "Content verified and added. Your originals are preserved.")); return;
      }
      auto run = [&](fs::path exe, std::vector<std::string> args, const std::string& label, uintptr_t closed_event = 0) {
        ProcessSpec spec;
        spec.exe = exe; spec.args = args; spec.working_dir = a.root;
        spec.stdout_file = a.logs / (label + ".log"); spec.stderr_file = a.logs / (label + ".error.log");
        spec.no_console = true;
        if (action != kInstall) spec.should_cancel = [&] { return a.cancel.load(); };
        if (action == kLaunch) spec.on_started = [&] { Post(a.window, kSessionStarted, ""); };
        spec.notification_event = closed_event;
        spec.on_notification = [&] { Post(a.window, kSessionClosed, ""); };
        auto next_sample = std::chrono::steady_clock::now();
        std::string previous_stage;
        if (action == kPrepare) spec.on_tick = [&] {
          const auto now = std::chrono::steady_clock::now();
          if (now < next_sample) return;
          next_sample = now + std::chrono::milliseconds(500);
          // Read only the current step's bounded log tail, on the process
          // worker. Never rescan whole logs or extraction trees on WM_TIMER.
          std::ifstream in(spec.stdout_file, std::ios::binary | std::ios::ate);
          if (!in) return;
          const auto size = in.tellg();
          in.seekg(std::max<std::streamoff>(0, static_cast<std::streamoff>(size) - 65536));
          std::string last;
          for (std::string line; std::getline(in, line);) if (line.starts_with("X360N_STAGE ")) last = line.substr(12);
          if (last.empty() || last == previous_stage) return;
          previous_stage = last;
          const int mapped = last.find("4-build") != std::string::npos ? 3 : last.find("codegen") != std::string::npos ? 2 : last.find("configure") != std::string::npos ? 1 : 0;
          if (mapped > a.stage.load() && a.stage.load() < 4) a.stage = mapped;
          Post(a.window, kProgress, Tr("Preparação: ", "Preparation: ") + last);
        };
        auto result = RunProcess(spec);
        std::string launch_detail;
        if (action == kLaunch) {
          // The supervisor performs the complete preflight. Consume its bounded
          // status record only after exit; never rehash the launch closure here.
          std::ifstream verified(spec.stdout_file, std::ios::binary);
          std::string prefix(65536, '\0');
          verified.read(prefix.data(), std::streamsize(prefix.size()));
          prefix.resize(size_t(verified.gcount()));
          a.launch_verified = prefix.find("X360N_LAUNCH_VERIFIED\n") != std::string::npos ||
                              prefix.find("X360N_LAUNCH_VERIFIED\r\n") != std::string::npos;
          if (result.exit_code != 0 || !result.launched) {
            std::ifstream error(spec.stderr_file, std::ios::binary);
            launch_detail.resize(4096); error.read(launch_detail.data(), std::streamsize(launch_detail.size()));
            launch_detail.resize(size_t(error.gcount()));
          }
        }
        if (result.cancelled) throw std::runtime_error("Preparação cancelada. Os originais e dados de usuário foram preservados; tente preparar novamente para retomar as etapas válidas.");
        if (!result.launched || result.exit_code != 0)
          throw std::runtime_error("A etapa " + label + " não concluiu. " + result.launch_error + " " + DescribeExitCode(result.exit_code) +
              (launch_detail.empty() ? "" : "\r\n" + launch_detail) + "\r\nDiagnóstico local: " + Utf8(a.logs));
      };
      if (action == kLaunch) {
        SessionNotification notification({}, true);
        Post(a.window, kProgress, Tr("Conferindo ", "Verifying ") + (english ? target.label_en : target.label));
        std::vector<std::string> args{"--root", Utf8(a.root), "session", Utf8(target.manifest), "--catalog-target", target.id, "--timeout", "0", "--closed-event", notification.name()};
        if (capture_crash) args.push_back("--crash-stack");
        run(Supervisor(a), args, "session", notification.handle());
        finish("Sessão encerrada. Consulte os logs para os resultados da execução.");
      } else {
        wchar_t sys[MAX_PATH]{}; GetSystemDirectoryW(sys, MAX_PATH);
        fs::path powershell = fs::path(sys) / "WindowsPowerShell/v1.0/powershell.exe";
        if (action == kInstall) {
          run(powershell, {"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", Utf8(a.root / "scripts/setup/install-build-tools.ps1"), "-AllowInstallation"}, "msvc");
          finish(Tr("Instalador encerrado. Verifique as dependências novamente antes de preparar.", "Installer closed. Recheck dependencies before preparing.")); return;
        }
        auto tool_args = std::vector<std::string>{"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", Utf8(a.root / "scripts/prepare-tools.ps1")};
        tool_args.push_back("-UpdateDependencies");
        if (a.allow_downloads) tool_args.push_back("-AllowDownloads");
        // Tool preflight can fail because another build owns the environment.
        // Only build-title.ps1 may publish a title's incomplete marker, after
        // acquiring its leases. Never invalidate another build or a valid receipt.
        Post(a.window, kProgress, Tr("Verificando ferramentas e dependências...", "Checking tools and dependencies..."));
        run(powershell, tool_args, "tools");
        if (target.generic_aot) GenerateAotProject(ProjectPaths{a.root}, target);
        auto prepare = [&](const std::string& id) {
          if (a.cancel) throw std::runtime_error("Preparação cancelada.");
          Target pending; pending.id = id;
          const auto marker = PreparationMarker(ProjectPaths{a.root}, pending);
          auto item = std::find_if(a.targets.begin(), a.targets.end(), [&](const auto& t) { return t.id == id; });
          auto label = item == a.targets.end() ? id : (english ? item->label_en : item->label);
          Post(a.window, kProgress, Tr("Preparando ", "Preparing ") + label + Tr(".\r\nA primeira preparação pode levar vários minutos.", ".\r\nFirst-time preparation can take several minutes."));
          run(powershell, {"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", Utf8(a.root / "scripts/build-title.ps1"), "-Title", id, "-KeepIncompleteMarker"}, id);
          if (id != target.id) fs::remove(marker);
        };
        for (const auto& dep : target.dependencies) prepare(dep);
        prepare(target.id);
        a.stage = 4;
        Post(a.window, kProgress, Tr("Registrando conteúdo verificado na biblioteca original...", "Registering verified content in the original library..."));
        fs::remove(PreparationMarker(ProjectPaths{a.root}, target));
        if (!CompleteLibraryPreparation(ProjectPaths{a.root}, target, a.targets, [&] { return a.cancel.load(); }))
          throw std::runtime_error(Tr("A preparação não produziu componentes válidos. Consulte o Diagnóstico.", "Preparation did not produce valid components. See Diagnostics."));
        fs::remove(PreparationMarker(ProjectPaths{a.root}, target));
        finish(Tr("Preparação concluída. Os componentes serão conferidos antes de habilitar a sessão.", "Preparation finished. Components will be checked before enabling the session."));
      }
    } catch (const std::exception& error) {
      // The driver records failures of builds it actually owns. Tools/import/
      // registration errors stay in this operation's logs and library inspection.
      a.failed = true; finish(error.what());
    }
  });
}

// ---------- Painting ----------
void DrawChip(HDC dc, App& a, int x, int y, const std::wstring& text, Tone tone) {
  auto s = [&](int v) { return ui::S(v, a.dpi); };
  const auto color = ToneColor(tone);
  const int h = s(26), tw = ui::TextWidth(dc, a.fonts.caption, text), w = tw + s(34);
  ui::Round(dc, {x, y, x + w, y + h}, h / 2, ui::Mix(P.surface, color, 16), ui::Mix(P.surface, color, 38));
  ui::Circle(dc, {x + s(11), y + h / 2 - s(3), x + s(17), y + h / 2 + s(3)}, color);
  ui::Text(dc, a.fonts.caption, text, {x + s(24), y, x + w, y + h}, color);
}
int ChipWidth(HDC dc, App& a, const std::wstring& text) { return ui::TextWidth(dc, a.fonts.caption, text) + ui::S(34, a.dpi); }
void DrawHeading(HDC dc, App& a, const RECT& card, const wchar_t* glyph, const std::wstring& text) {
  auto s = [&](int v) { return ui::S(v, a.dpi); };
  ui::Text(dc, a.fonts.icon, glyph, {card.left + s(24), card.top + s(16), card.left + s(48), card.top + s(44)}, P.accent_text);
  ui::Text(dc, a.fonts.bold, text, {card.left + s(52), card.top + s(16), card.right - s(24), card.top + s(44)}, P.text);
}
std::wstring Elapsed(const App& a) {
  const auto seconds = (GetTickCount64() - a.started) / 1000;
  wchar_t text[32]; swprintf_s(text, L"%02llu:%02llu", seconds / 60, seconds % 60); return text;
}
void PaintBanner(HDC dc, App& a) {
  auto s = [&](int v) { return ui::S(v, a.dpi); };
  const RECT& b = a.geo.banner;
  const bool busy = a.busy;
  const Tone tone = busy ? Tone::Info : a.result == 3 ? Tone::Err : a.result == 2 ? Tone::Warn : Tone::Ok;
  const auto color = busy ? P.accent_text : ToneColor(tone);
  ui::Round(dc, b, s(14), P.surface, busy ? P.border : ui::Mix(P.surface, color, 45));
  const wchar_t* glyph = busy ? (a.activity_kind == 1 ? ui::icon::download : a.activity_kind == 2 ? ui::icon::tools : a.activity_kind == 3 ? ui::icon::play : ui::icon::refresh) :
    a.result == 3 ? ui::icon::error : a.result == 2 ? ui::icon::warning : ui::icon::check;
  RECT disc{b.left + s(24), b.top + s(16), b.left + s(64), b.top + s(56)};
  ui::Circle(dc, disc, ui::Mix(P.surface, color, 20));
  ui::Text(dc, a.fonts.icon, glyph, disc, color, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
  ui::Text(dc, a.fonts.big, busy ? a.activity_title : a.result_title, {b.left + s(76), b.top + s(14), a.geo.title_right, b.top + s(48)}, P.text);
  if (!busy) return;
  ui::Text(dc, a.fonts.caption, Elapsed(a) + (a.activity_kind == 1 && a.stage == 0 && a.extracted_bytes ? L"   ·   " + FormatBytes(a.extracted_bytes) : L""),
           a.geo.elapsed, P.muted, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
  // Progress: completed stages are solid; the current stage animates because no percentage is known.
  const RECT& bar = a.geo.bar;
  ui::Round(dc, bar, (bar.bottom - bar.top) / 2, P.surface2);
  const int count = int(a.stage_names.size()), width = bar.right - bar.left;
  const int done = count ? std::min(a.stage.load(), count) : 0;
  if (count && done > 0) ui::Round(dc, {bar.left, bar.top, bar.left + width * done / count, bar.bottom}, (bar.bottom - bar.top) / 2, P.accent);
  const int seg_left = count ? bar.left + width * done / count : bar.left;
  const int seg_right = count ? bar.left + width * std::min(done + 1, count) / count : bar.right;
  if (seg_right > seg_left) {
    const int span = seg_right - seg_left, sweep = std::max(s(40), span / 3);
    const int pos = int((GetTickCount64() % 1400) * (span + sweep) / 1400) - sweep;
    const int l = std::max(seg_left, seg_left + pos), r = std::min(seg_right, seg_left + pos + sweep);
    if (r > l) ui::Round(dc, {l, bar.top, r, bar.bottom}, (bar.bottom - bar.top) / 2, P.accent_text);
  }
  if (count && !IsRectEmpty(&a.geo.steps)) {
    const RECT& st = a.geo.steps; const int sw = (st.right - st.left) / count;
    for (int i = 0; i < count; ++i) {
      const bool d = i < done, cur = i == done;
      const int dx = st.left + i * sw, cy = (st.top + st.bottom) / 2;
      ui::Circle(dc, {dx, cy - s(5), dx + s(10), cy + s(5)}, d ? P.accent_text : cur ? P.info : P.faint);
      ui::Text(dc, cur ? a.fonts.bold : a.fonts.caption, a.stage_names[i], {dx + s(16), st.top, dx + sw - s(4), st.bottom}, d ? P.muted : cur ? P.text : P.faint);
    }
  }
}
void Paint(App& a, HDC capture = nullptr) {
  PAINTSTRUCT ps{}; auto target = capture ? capture : BeginPaint(a.window, &ps);
  auto s = [&](int v) { return ui::S(v, a.dpi); };
  RECT paint = ps.rcPaint;
  if (capture) GetClientRect(a.window, &paint);
  ui::Buffer buffer(target, paint.right - paint.left, paint.bottom - paint.top);
  HDC dc = buffer.dc();
  ui::Origin() = {-paint.left, -paint.top};
  RECT r{}; GetClientRect(a.window, &r);
  ui::Fill(dc, r, P.bg);
  const Geo& g = a.geo;
  const int sb = s(216);
  ui::Fill(dc, {0, 0, sb, r.bottom}, P.sidebar);
  ui::Fill(dc, {sb - 1, 0, sb, r.bottom}, P.border);
  // Brand
  ui::Round(dc, {s(20), s(28), s(56), s(64)}, s(9), P.accent);
  ui::Text(dc, a.fonts.big, L"X", {s(20), s(28), s(56), s(64)}, P.on_accent, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
  ui::Text(dc, a.fonts.bold, L"x360-native", {s(66), s(26), sb - s(12), s(46)}, P.text);
  ui::Text(dc, a.fonts.caption, L"Xbox 360 · AOT", {s(66), s(44), sb - s(12), s(64)}, P.muted);
  const Page page = PageOf(a);
  auto card = [&](const RECT& rc) { ui::Round(dc, rc, s(14), P.surface, P.border); };
  if (g.banner_on) PaintBanner(dc, a);
  switch (page) {
    case Page::Home: {
      card(g.hero);
      DrawChip(dc, a, g.hero.left + s(28), g.hero.top + s(26), a.chip_text, a.chip_tone);
      if (!g.banner_on) {
        size_t dashboards = 0, ready = 0, games = 0, prepared = 0;
        if (a.checked.size() == a.targets.size()) for (size_t i = 0; i < a.targets.size(); ++i) {
          const auto& t = a.targets[i]; const auto& c = a.checked[i];
          if (t.kind == "dashboard") { ++dashboards; if (c.state.inputs_ok && c.state.prepared) ++ready; }
          else if (c.added) { ++games; if (c.state.inputs_ok && c.state.prepared && c.library.empty()) ++prepared; }
        }
        const bool native = a.renderer_settings.renderer == Renderer::Native;
        struct Stat { std::wstring label, value, hint; Tone tone; const wchar_t* glyph; };
        const Stat stats[4] = {
          {WTr("Dashboard", "Dashboard"), ready ? WTr("Pronta", "Ready") : WTr("Pendente", "Pending"), Wide(std::to_string(ready) + "/" + std::to_string(dashboards) + Tr(" versões preparadas", " versions prepared")), ready ? Tone::Ok : Tone::Warn, ui::icon::dashboard},
          {WTr("Biblioteca", "Library"), Wide(std::to_string(games)), WTr("jogos importados", "imported games"), Tone::Neutral, ui::icon::library},
          {WTr("Preparados", "Prepared"), Wide(std::to_string(prepared)), WTr("prontos para a My Games", "ready for My Games"), prepared ? Tone::Ok : Tone::Neutral, ui::icon::check},
          {WTr("Gráficos", "Graphics"), native ? L"Native" : L"Xenos", WTr("renderer global da sessão", "session-wide renderer"), native ? Tone::Warn : Tone::Neutral, ui::icon::chip}};
        for (int i = 0; i < 4; ++i) {
          const RECT& c = g.stats[i]; card(c);
          ui::Text(dc, a.fonts.icon, stats[i].glyph, {c.left + s(18), c.top + s(14), c.left + s(40), c.top + s(36)}, ToneColor(stats[i].tone == Tone::Neutral ? Tone::Info : stats[i].tone));
          ui::Text(dc, a.fonts.caption, stats[i].label, {c.left + s(46), c.top + s(14), c.right - s(12), c.top + s(36)}, P.muted);
          ui::Text(dc, a.fonts.big, stats[i].value, {c.left + s(18), c.top + s(36), c.right - s(12), c.top + s(66)}, P.text);
          ui::Text(dc, a.fonts.caption, stats[i].hint, {c.left + s(18), c.top + s(64), c.right - s(12), c.top + s(88)}, P.muted);
        }
      }
      break;
    }
    case Page::Library: {
      ui::Round(dc, g.search, s(10), P.surface2, P.border);
      ui::Text(dc, a.fonts.icon, ui::icon::search, {g.search.left + s(12), g.search.top, g.search.left + s(34), g.search.bottom}, P.muted);
      [[fallthrough]];
    }
    case Page::Dashboard: {
      if (a.rows.empty()) {
        card(g.empty);
        const bool filtered = a.filter != 0 || !a.query.empty();
        RECT disc{(g.empty.left + g.empty.right) / 2 - s(32), g.empty.top + s(28), (g.empty.left + g.empty.right) / 2 + s(32), g.empty.top + s(92)};
        ui::Circle(dc, disc, ui::Mix(P.surface, P.accent_text, 14));
        ui::Text(dc, a.fonts.icon_big, filtered ? ui::icon::search : page == Page::Library ? ui::icon::gamepad : ui::icon::dashboard, disc, P.accent_text, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        ui::Text(dc, a.fonts.big, filtered ? WTr("Nada encontrado", "Nothing found") : page == Page::Library ? WTr("Sua biblioteca começa aqui", "Your library starts here") : WTr("Nenhuma dashboard disponível", "No dashboard available"),
                 {g.empty.left + s(24), g.empty.top + s(92), g.empty.right - s(24), g.empty.top + s(124)}, P.text, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        break;
      }
      card(g.detail);
      DrawChip(dc, a, g.detail.left + s(24), g.detail.top + s(20), a.chip_text, a.chip_tone);
      const int tx = g.detail.left + s(24) + ChipWidth(dc, a, a.chip_text) + s(12);
      ui::Text(dc, a.fonts.bold, a.selection_title, {tx, g.detail.top + s(18), g.detail.right - s(24) - g.action_w - s(24), g.detail.top + s(46)}, P.text);
      if (page == Page::Dashboard) {
        // Requirements of the selected version: what is still missing before it can start.
        struct Need { std::wstring text; bool ok; };
        const Need needs[3] = {{WTr("Arquivos originais", "Original files"), a.current.inputs_ok},
                               {WTr("Preparação AOT", "AOT preparation"), a.current.prepared},
                               {WTr("Ambiente do PC", "PC environment"), a.environment.graphics && a.environment.architecture && a.environment.writable}};
        int nx = g.detail.left + s(24);
        for (const auto& need : needs) {
          ui::Text(dc, a.fonts.icon, need.ok ? ui::icon::check : ui::icon::warning, {nx, g.detail.bottom - s(44), nx + s(22), g.detail.bottom - s(18)}, need.ok ? P.ok : P.warn, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
          ui::Text(dc, a.fonts.caption, need.text, {nx + s(28), g.detail.bottom - s(44), nx + s(190), g.detail.bottom - s(18)}, need.ok ? P.muted : P.text);
          nx += s(200);
        }
      }
      break;
    }
    case Page::Import: {
      if (a.busy) break;
      ui::Round(dc, g.drop, s(16), P.surface, P.faint, 2, true);
      const int mid = (g.drop.left + g.drop.right) / 2;
      RECT disc{mid - s(32), g.drop.top + s(24), mid + s(32), g.drop.top + s(88)};
      ui::Circle(dc, disc, ui::Mix(P.surface, P.accent_text, 14));
      ui::Text(dc, a.fonts.icon_big, ui::icon::download, disc, P.accent_text, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
      ui::Text(dc, a.fonts.big, WTr("Arraste um arquivo ou pasta para cá", "Drop a file or folder here"), {g.drop.left + s(24), g.drop.top + s(92), g.drop.right - s(24), g.drop.top + s(128)}, P.text, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
      if (!g.banner_on) {
        struct Tile { const wchar_t* glyph; std::wstring title, text; };
        const Tile tiles[3] = {
          {ui::icon::disc, L"ISO / GDFX", WTr("Imagem de disco do Xbox 360", "Xbox 360 disc image")},
          {ui::icon::package, L"XBLA / STFS", WTr("Pacote de conteúdo (CON, LIVE, PIRS)", "Content package (CON, LIVE, PIRS)")},
          {ui::icon::folder, WTr("Pasta extraída", "Extracted folder"), WTr("Jogo já extraído, com default.xex", "Already extracted game with default.xex")}};
        for (int i = 0; i < 3; ++i) {
          const RECT& c = g.tiles[i]; card(c);
          ui::Text(dc, a.fonts.icon, tiles[i].glyph, {c.left + s(18), c.top + s(18), c.left + s(42), c.top + s(44)}, P.accent_text);
          ui::Text(dc, a.fonts.bold, tiles[i].title, {c.left + s(48), c.top + s(18), c.right - s(12), c.top + s(44)}, P.text);
          ui::Text(dc, a.fonts.caption, tiles[i].text, {c.left + s(18), c.top + s(52), c.right - s(14), c.bottom - s(8)}, P.muted, DT_WORDBREAK);
        }
      }
      break;
    }
    case Page::Settings: {
      const auto saved = SaveDC(dc);
      const RECT view = ui::Off(g.viewport);
      IntersectClipRect(dc, view.left, view.top, view.right, view.bottom);
      for (const auto& c : g.cards) card(c);
      DrawHeading(dc, a, g.cards[0], ui::icon::chip, WTr("Gráficos", "Graphics"));
      DrawHeading(dc, a, g.cards[1], ui::icon::save, WTr("Armazenamento e ambiente", "Storage and environment"));
      DrawHeading(dc, a, g.cards[2], ui::icon::language, WTr("Console Xbox 360", "Xbox 360 console"));
      DrawHeading(dc, a, g.cards[3], ui::icon::tools, WTr("Aplicativo desktop", "Desktop application"));
      RestoreDC(dc, saved);
      if (g.scroll_max > 0) {
        // Position indicator for the scrolled cards (wheel, Tab and the gamepad reveal them).
        const RECT& v = g.viewport;
        const int track = v.bottom - v.top - s(16), view_h = v.bottom - v.top;
        const int thumb = std::max(s(32), track * view_h / (view_h + g.scroll_max));
        const int y = v.top + s(8) + (track - thumb) * a.scroll / g.scroll_max;
        ui::Round(dc, {v.right - s(14), v.top + s(8), v.right - s(10), v.top + s(8) + track}, s(2), P.surface);
        ui::Round(dc, {v.right - s(14), y, v.right - s(10), y + thumb}, s(2), P.faint);
      }
      break;
    }
    case Page::Diagnostics: card(g.details_card); break;
  }
  ui::Origin() = {0, 0};
  buffer.Flush(paint.left, paint.top);
  if (!capture) EndPaint(a.window, &ps);
}
void DrawButton(App& a, const DRAWITEMSTRUCT& item) {
  const auto style = a.styles[int(item.CtlID)];
  const int w = item.rcItem.right - item.rcItem.left, h = item.rcItem.bottom - item.rcItem.top;
  ui::Buffer buffer(item.hDC, w, h);
  HDC dc = buffer.dc();
  auto s = [&](int v) { return ui::S(v, a.dpi); };
  COLORREF back = style.back == 1 ? P.sidebar : style.back == 2 ? P.surface : P.bg;
  // Some actions sit on the page in one view and on a card in another.
  if ((item.CtlID == kAddContent && PageOf(a) != Page::Home) || (item.CtlID == kData && PageOf(a) == Page::Settings)) back = P.surface;
  ui::Fill(dc, {0, 0, w, h}, back);
  const bool disabled = item.itemState & ODS_DISABLED, pressed = item.itemState & ODS_SELECTED, hot = a.hot == item.hwndItem;
  const bool focus = (item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT);
  bool selected = false;
  const Page page = PageOf(a);
  switch (item.CtlID) {
    case kHome: selected = page == Page::Home; break;
    case kGames: selected = page == Page::Library; break;
    case kImportPage: selected = page == Page::Import; break;
    case kDashboards: selected = page == Page::Dashboard; break;
    case kSettings: selected = page == Page::Settings; break;
    case kDetails: selected = page == Page::Diagnostics; break;
    case kFilter0: case kFilter1: case kFilter2: case kFilter3: selected = a.filter == int(item.CtlID - kFilter0); break;
    case kLangPt: selected = !english; break;
    case kLangEn: selected = english; break;
    case kRenderer: selected = a.renderer_settings.renderer == Renderer::Xenos; break;
    case kRendererNative: selected = a.renderer_settings.renderer == Renderer::Native; break;
    case kApiD3D12: selected = a.renderer_settings.api == GraphicsApi::D3D12; break;
    case kApiVulkan: selected = a.renderer_settings.api == GraphicsApi::Vulkan; break;
    case kNativeDevelopment: selected = a.renderer_settings.experimental; break;
    case kFullscreen: selected = a.renderer_settings.fullscreen; break;
    case kCaptureCrash: selected = a.capture_crash; break;
  }
  const wchar_t* glyph = style.glyph;
  if (item.CtlID == kPrimary) {
    // The main action follows what it will do next.
    glyph = a.next_action == kLaunch ? ui::icon::play : a.next_action == kPrepare ? ui::icon::tools : a.next_action == kImport ? ui::icon::file :
            a.next_action == kImportPage ? ui::icon::add : a.next_action == kFolder ? ui::icon::folder : ui::icon::settings;
  }
  wchar_t text[256]{}; GetWindowTextW(item.hwndItem, text, 256);
  const std::wstring label = text;
  RECT box{0, 0, w, h};
  COLORREF ink = P.text, glyph_ink = P.text;
  auto white = [&](int pct) { return ui::Mix(P.surface2, RGB(255, 255, 255), pct); };
  switch (style.kind) {
    case Kind::Primary: {
      const COLORREF fill = disabled ? P.surface2 : pressed ? P.accent_press : hot ? P.accent_hover : P.accent;
      ui::Round(dc, box, s(10), fill);
      ink = glyph_ink = disabled ? P.faint : P.on_accent; break;
    }
    case Kind::Secondary: {
      ui::Round(dc, box, s(10), pressed ? white(14) : hot && !disabled ? white(9) : P.surface2, P.border);
      ink = glyph_ink = disabled ? P.faint : P.text; break;
    }
    case Kind::Nav: {
      if (selected) {
        ui::Round(dc, box, s(10), ui::Mix(P.sidebar, P.accent_text, 14));
        ui::Round(dc, {0, h / 2 - s(10), s(4), h / 2 + s(10)}, s(2), P.accent_text);
      } else if (hot && !disabled) ui::Round(dc, box, s(10), ui::Mix(P.sidebar, RGB(255, 255, 255), 6));
      ink = selected ? P.text : disabled ? P.faint : P.muted;
      glyph_ink = selected ? P.accent_text : ink; break;
    }
    case Kind::Chip: {
      if (selected) ui::Round(dc, box, h / 2, ui::Mix(P.bg, P.accent, 55), P.accent_text);
      else ui::Round(dc, box, h / 2, hot ? white(6) : P.surface, P.border);
      ink = selected ? P.on_accent : P.muted; break;
    }
    case Kind::Ghost: {
      if (hot && !disabled) ui::Round(dc, box, s(10), white(8));
      ink = glyph_ink = disabled ? P.faint : hot ? P.text : P.muted; break;
    }
    case Kind::Option: {
      ui::Round(dc, box, s(12), selected ? ui::Mix(P.surface, P.accent, 22) : hot && !disabled ? white(6) : P.surface2, selected ? P.accent_text : P.border, selected ? 2 : 1);
      ink = disabled ? P.faint : P.text; break;
    }
    case Kind::Toggle: {
      ink = disabled ? P.faint : P.text;
      const int tw = s(40), th = s(22), ty = (h - th) / 2;
      ui::Round(dc, {0, ty, tw, ty + th}, th / 2, selected ? P.accent : P.surface2, selected ? P.accent : P.faint);
      const int knob = th - s(8);
      const int kx = selected ? tw - s(4) - knob : s(4);
      ui::Circle(dc, {kx, ty + s(4), kx + knob, ty + s(4) + knob}, selected ? P.on_accent : P.muted);
      box.left = tw + s(12); break;
    }
  }
  if (style.kind == Kind::Option) {
    const bool native = item.CtlID == kRendererNative;
    const int cy = h / 2;
    ui::Circle(dc, {s(16), cy - s(9), s(34), cy + s(9)}, selected ? P.accent_text : disabled ? P.faint : P.muted);
    ui::Circle(dc, {s(18), cy - s(7), s(32), cy + s(7)}, selected ? ui::Mix(P.surface, P.accent, 22) : P.surface2);
    if (selected) ui::Circle(dc, {s(21), cy - s(4), s(29), cy + s(4)}, P.accent_text);
    ui::Text(dc, a.fonts.bold, label, {s(48), s(8), w - s(16), s(30)}, ink);
    const auto caption = item.CtlID == kApiVulkan
        ? (a.renderer_settings.renderer == Renderer::Native
           ? WTr("Indisponível no Native · executor incompleto", "Unavailable in Native · incomplete executor")
           : WTr("Experimental · driver verificado antes de iniciar", "Experimental · driver checked before launch"))
        : item.CtlID == kApiD3D12 ? WTr("Padrão · disponível para Xenos e Native", "Default · available for Xenos and Native")
        : native ? (disabled ? WTr("Requer o acesso de desenvolvimento abaixo", "Requires the development access below") : WTr("Experimental · executor próprio", "Experimental · independent executor"))
        : WTr("Padrão · compatível com o ReXGlue", "Default · ReXGlue compatible");
    ui::Text(dc, a.fonts.caption, caption, {s(48), s(30), w - s(16), s(52)}, disabled ? P.faint : P.muted);
  } else if (style.kind == Kind::Toggle) {
    ui::Text(dc, a.fonts.body, label, {box.left, 0, w, h}, ink);
  } else if (style.kind == Kind::Nav) {
    ui::Text(dc, a.fonts.icon, style.glyph ? style.glyph : L"", {s(14), 0, s(42), h}, glyph_ink, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    ui::Text(dc, selected ? a.fonts.bold : a.fonts.body, label, {s(50), 0, w - s(8), h}, ink);
  } else if (glyph && style.kind != Kind::Chip && w <= h) {
    // Square buttons are icon-only; their label stays the accessible name.
    ui::Text(dc, a.fonts.icon, glyph, box, glyph_ink, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
  } else if (glyph && style.kind != Kind::Chip) {
    const int iw = s(22), gap = s(8), tw = ui::TextWidth(dc, a.fonts.bold, label);
    const int total = iw + gap + std::min(tw, w - iw - gap - s(16));
    const int x0 = std::max(s(8), (w - total) / 2);
    ui::Text(dc, a.fonts.icon, glyph, {x0, 0, x0 + iw, h}, glyph_ink, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    ui::Text(dc, a.fonts.bold, label, {x0 + iw + gap, 0, w - s(8), h}, ink);
  } else if (style.kind != Kind::Toggle) {
    ui::Text(dc, style.kind == Kind::Chip && selected ? a.fonts.bold : a.fonts.body, label, {s(8), 0, w - s(8), h}, ink, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
  }
  if (focus) ui::Round(dc, {0, 0, w, h}, style.kind == Kind::Chip ? h / 2 : s(10), CLR_INVALID, P.accent_text, 2);
  buffer.Flush(item.rcItem.left, item.rcItem.top);
}
// The recorded package icon, decoded into a premultiplied copy that does not
// keep the source stream alive. A missing or changed file keeps the glyph.
Gdiplus::Bitmap* Thumbnail(App& a, const Target& t) {
  if (t.thumbnail_sha256.empty()) return nullptr;
  auto [it, inserted] = a.thumbnails.try_emplace(t.thumbnail_sha256);
  if (!inserted) return it->second.get();
  const auto png = ReadThumbnail(t);
  if (png.empty()) return nullptr;
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, png.size());
  if (!memory) return nullptr;
  void* bytes = GlobalLock(memory);
  if (!bytes) { GlobalFree(memory); return nullptr; }
  memcpy(bytes, png.data(), png.size()); GlobalUnlock(memory);
  IStream* stream = nullptr;
  if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream))) { GlobalFree(memory); return nullptr; }
  {
    Gdiplus::Bitmap decoded(stream);
    const UINT w = decoded.GetWidth(), h = decoded.GetHeight();
    if (decoded.GetLastStatus() == Gdiplus::Ok && w && h && w <= 1024 && h <= 1024) {
      auto copy = std::make_unique<Gdiplus::Bitmap>(INT(w), INT(h), PixelFormat32bppPARGB);
      Gdiplus::Graphics g(copy.get());
      if (copy->GetLastStatus() == Gdiplus::Ok && g.DrawImage(&decoded, 0, 0, INT(w), INT(h)) == Gdiplus::Ok)
        it->second = std::move(copy);
    }
  }
  stream->Release();
  return it->second.get();
}
void DrawRow(App& a, const DRAWITEMSTRUCT& item) {
  if (item.itemID == UINT(-1)) return;
  const int w = item.rcItem.right - item.rcItem.left, h = item.rcItem.bottom - item.rcItem.top;
  ui::Buffer buffer(item.hDC, w, h);
  HDC dc = buffer.dc();
  auto s = [&](int v) { return ui::S(v, a.dpi); };
  ui::Fill(dc, {0, 0, w, h}, P.bg);
  const size_t index = size_t(item.itemData);
  if (index < a.targets.size() && index < a.checked.size()) {
    const auto& t = a.targets[index];
    const bool dashboard = t.kind == "dashboard", sel = item.itemState & ODS_SELECTED;
    const bool focus = (item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT);
    const Row row = StatusOf(a, index); const Tone tone = RowTone(row);
    RECT card{s(8), s(4), w - s(8), h - s(4)};
    ui::Round(dc, card, s(12), sel ? ui::Mix(P.surface, P.accent, 18) : P.surface2, sel ? P.accent_text : P.border, sel && focus ? 2 : 1);
    RECT badge_icon{card.left + s(14), card.top + s(10), card.left + s(14) + s(44), card.top + s(10) + s(44)};
    if (auto* image = dashboard ? nullptr : Thumbnail(a, t)) ui::Image(dc, badge_icon, s(10), *image);
    else {
      ui::Round(dc, badge_icon, s(10), ui::Mix(P.surface2, ToneColor(tone == Tone::Neutral ? Tone::Info : tone), 18));
      ui::Text(dc, a.fonts.icon, dashboard ? ui::icon::dashboard : ui::icon::gamepad, badge_icon, ToneColor(tone == Tone::Neutral ? Tone::Info : tone), DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    }
    const auto status = RowLabel(a, row, dashboard);
    const int bw = ChipWidth(dc, a, status);
    const int chip_x = card.right - s(16) - bw, chip_y = (card.top + card.bottom) / 2 - s(13);
    DrawChip(dc, a, chip_x, chip_y, status, tone);
    const int tx = badge_icon.right + s(16);
    ui::Text(dc, a.fonts.bold, Wide(english ? t.label_en : t.label), {tx, card.top + s(10), chip_x - s(12), card.top + s(32)}, P.text);
    std::wstring line;
    if (dashboard) line = L"Perfil: " + Wide(t.id) + (t.version.empty() ? L"" : L"   ·   " + Wide(t.version));
    else line = L"Title ID: " + (t.title_id.empty() ? std::wstring(L"—") : Wide(t.title_id)) + L"   ·   " + WTr("Versão: ", "Version: ") + (t.version.empty() ? std::wstring(L"—") : Wide(t.version));
    ui::Text(dc, a.fonts.caption, line, {tx, card.top + s(34), chip_x - s(12), card.top + s(56)}, P.muted);
  }
  buffer.Flush(item.rcItem.left, item.rcItem.top);
}
COLORREF BackOf(const App& a, HWND h) {
  if (h == a.footer) return P.sidebar;
  if (h == a.headline || h == a.subtitle) return P.bg;
  if (h == a.search) return P.surface2;
  return P.surface;
}
HBRUSH BrushFor(COLORREF c) {
  static HBRUSH bg = CreateSolidBrush(P.bg), sidebar = CreateSolidBrush(P.sidebar), surface = CreateSolidBrush(P.surface), surface2 = CreateSolidBrush(P.surface2);
  return c == P.bg ? bg : c == P.sidebar ? sidebar : c == P.surface2 ? surface2 : surface;
}
void Navigate(App& a, int id) {
  a.result = 0;
  a.home = id == kHome; a.games = id == kGames || id == kLibrary;
  a.import_view = id == kImportPage || id == kAddContent;
  a.settings = a.show_details = false;
}
void GoBack(App& a) { if (a.settings || a.show_details) { a.settings = a.show_details = false; InspectSelected(a); } }
// Keyboard and gamepad focus scrolls a clipped Settings control fully into view.
void Reveal(App& a, HWND h) {
  const auto& g = a.geo;
  if (g.scroll_max <= 0 || std::find(g.scrolled.begin(), g.scrolled.end(), h) == g.scrolled.end()) return;
  RECT r{}; GetWindowRect(h, &r);
  MapWindowPoints(nullptr, a.window, reinterpret_cast<POINT*>(&r), 2);
  const int margin = ui::S(16, a.dpi);
  const int delta = r.top < g.viewport.top + margin ? r.top - g.viewport.top - margin :
                    r.bottom > g.viewport.bottom - margin ? r.bottom - g.viewport.bottom + margin : 0;
  if (delta) { a.scroll += delta; Layout(a); }
}
// Progress text can wrap to another line; resize the banner only when it does.
void RefreshBanner(App& a) {
  if (!a.geo.banner_on) return;
  const int width = a.geo.banner.right - a.geo.banner.left - ui::S(76 + 24, a.dpi), line = LineHeight(a.activity_text);
  if (std::clamp(TextHeight(a.activity_text, width), line, 4 * line) != a.geo.banner_text) Layout(a);
  else InvalidateRect(a.window, &a.geo.banner, FALSE);
}
void ApplyPreview(App& a, int state) {
#ifndef X360N_DISTRIBUTION
  switch (state) {
    case 1: a.busy = true; a.importing = true; a.session = false; a.initializing = false; a.stage = 0; a.extracted_bytes = 1503238553ull;
      SetActivity(a, 1, WTr("Importando conteúdo", "Importing content"), Tr("Extraindo o jogo...\r\nIsso pode levar alguns minutos. Você pode cancelar; o arquivo original será preservado.", "Extracting the game...\r\nThis may take a few minutes. You can cancel; the original file will be preserved."),
        {WTr("Extração", "Extraction"), WTr("Verificação", "Verification"), WTr("Importação", "Import")});
      a.started = GetTickCount64() - 75000; a.extracted_bytes = 1503238553ull; SetTimer(a.window, 2, 33, nullptr); break;
    case 2: a.busy = true; a.importing = false; a.session = false; a.initializing = false; a.stage = 3;
      SetActivity(a, 2, WTr("Preparando para você", "Preparing for you"), Tr("Compilando a execução para este computador…\r\nVocê pode cancelar e continuar depois. O diagnóstico está no Diagnóstico.", "Compiling native execution for this computer…\r\nYou can cancel and continue later."),
        {WTr("Ferramentas", "Tools"), WTr("Configuração", "Configuration"), WTr("Tradução", "Translation"), WTr("Compilação", "Compilation"), WTr("Registro", "Registration")});
      a.started = GetTickCount64() - 421000; SetTimer(a.window, 2, 33, nullptr); break;
    case 3: a.busy = false; a.result = 3; a.importing = false; a.result_title = WTr("Não foi possível concluir", "Could not finish");
      Set(a.activity_text, Tr("A etapa codegen não concluiu. Seus arquivos foram preservados.\r\nDiagnóstico local: .local\\evidence\\logs\\desktop\\0000", "The codegen step did not finish. Your files were preserved.\r\nLocal diagnostics: .local\\evidence\\logs\\desktop\\0000")); break;
    case 4: a.busy = false; a.result = 1; a.importing = true; a.result_title = WTr("Importação concluída", "Import finished");
      Set(a.activity_text, Tr("Forza Horizon 0.0.0.10\r\nConteúdo verificado e adicionado. Seus originais foram preservados.", "Forza Horizon 0.0.0.10\r\nContent verified and added. Your originals are preserved.")); break;
    case 5: a.busy = false; a.result = 0; a.importing = false; a.preview_empty = false; a.preparing_id.clear(); KillTimer(a.window, 2); break;
    case 6: {
      a.preview_empty = false;
      const char* names[] = {"Castle Crashers", "Forza Horizon", "Halo 3", "Gears of War 3", "Alan Wake", "Fable III", "Minecraft: Xbox 360 Edition", "Red Dead Redemption: Game of the Year Edition with a very long title", "Crimson Skies: High Road to Revenge"};
      const char* ids[] = {"584111F7", "4D5309C9", "4D5307E6", "4D5308AB", "4D53082D", "4D5308CC", "584111F7", "5454082B", "4D53004C"};
      a.targets.erase(std::remove_if(a.targets.begin(), a.targets.end(), [](const Target& t) { return t.id.rfind("preview_", 0) == 0; }), a.targets.end());
      a.checked.resize(a.targets.size());
      for (int i = 0; i < 9; ++i) {
        Target t; t.id = "preview_" + std::to_string(i); t.label = t.label_en = names[i]; t.title_id = ids[i];
        t.version = std::string("0.0.") + std::to_string(i + 1) + ".0"; t.catalogued = true; t.aot = i != 5 && i != 7; t.kind = "game";
        App::CheckedTarget c; c.added = true; c.state.inputs_ok = i != 3; c.state.prepared = i % 3 == 0 || i == 4; c.state.interrupted = i == 8;
        if (i == 2) c.library.push_back("missing");
        a.targets.push_back(t); a.checked.push_back(c);
      }
      a.home = false; a.import_view = false; a.games = true; a.settings = a.show_details = false;
      a.dashboard_indices.clear();
      for (size_t i = 0; i < a.targets.size(); ++i) if (a.targets[i].kind != "dashboard" && a.checked[i].added) a.dashboard_indices.push_back(i);
      RebuildRows(a); if (!a.rows.empty()) a.selected = a.rows.front(); SyncSelection(a); break;
    }
    case 7: a.preview_empty = true; a.games = true; a.home = false; a.import_view = false; a.settings = a.show_details = false; a.selected = a.targets.size(); RebuildRows(a); break;
  }
  if (state == 7) { a.selected = a.targets.size(); }
  InspectSelected(a);
  Enable(a);
#else
  (void)a; (void)state;
#endif
}
LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
  auto* a = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    a = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
    a->window = window; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(a));
  }
  if (!a) return DefWindowProcW(window, message, wp, lp);
  try {
    switch (message) {
      case WM_ERASEBKGND: return 1;
      case WM_PAINT: Paint(*a); return 0;
      case WM_MEASUREITEM: {
        auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
        if (measure->CtlType == ODT_LISTBOX) { measure->itemHeight = UINT(ui::S(76, a->dpi)); return TRUE; }
        if (measure->CtlType == ODT_COMBOBOX) { measure->itemHeight = UINT(ui::S(24, a->dpi)); return TRUE; }
        break;
      }
      case WM_DRAWITEM: {
        auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if (item->CtlType == ODT_COMBOBOX) {
          ui::Fill(item->hDC, item->rcItem, item->itemState & ODS_SELECTED ? ui::Mix(P.surface, P.accent, 18) : P.surface);
          if (item->itemID != UINT(-1)) {
            wchar_t text[256]{}; SendMessageW(item->hwndItem, CB_GETLBTEXT, item->itemID, reinterpret_cast<LPARAM>(text));
            auto rect = item->rcItem; rect.left += ui::S(8, a->dpi);
            ui::Text(item->hDC, a->fonts.body, text, rect, item->itemState & ODS_DISABLED ? P.muted : P.text,
                     DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
          }
          if (item->itemState & ODS_FOCUS) DrawFocusRect(item->hDC, &item->rcItem);
          return TRUE;
        }
        if (item->CtlType == ODT_LISTBOX) DrawRow(*a, *item); else DrawButton(*a, *item);
        return TRUE;
      }
      case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: {
        auto h = reinterpret_cast<HWND>(lp);
        const COLORREF back = message == WM_CTLCOLORLISTBOX ? P.bg : BackOf(*a, h);
        const COLORREF ink = h == a->subtitle || h == a->footer || h == a->root_text || h == a->env_text || h == a->renderer_hint ? P.muted : P.text;
        SetTextColor(reinterpret_cast<HDC>(wp), ink);
        SetBkColor(reinterpret_cast<HDC>(wp), back);
        return reinterpret_cast<LRESULT>(BrushFor(back));
      }
      case WM_CREATE: {
        a->dpi = g_dpi_override ? g_dpi_override : GetDpiForWindow(window);
        BOOL dark = TRUE; DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
        a->icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
        if (a->icon) { SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(a->icon)); a->icon = CopyIcon(a->icon); }
        // Creation order is the keyboard order: navigation, then content.
        const std::tuple<int, const wchar_t*> nav[] = {{kHome, ui::icon::home}, {kGames, ui::icon::library}, {kImportPage, ui::icon::download}, {kDashboards, ui::icon::dashboard},
                                                       {kSettings, ui::icon::settings}, {kDetails, ui::icon::info}, {kLanguage, ui::icon::language}};
        for (auto [id, glyph] : nav) Button(*a, id, Kind::Nav, glyph, 1);
        a->footer = Control(*a, L"STATIC", L"", 0);
        a->headline = Control(*a, L"STATIC", L"", SS_ENDELLIPSIS);
        a->subtitle = Control(*a, L"STATIC", L"", 0);
        Button(*a, kUpdate, Kind::Primary, ui::icon::download, 1);
        Button(*a, kData, Kind::Secondary, nullptr);
        Button(*a, kCancel, Kind::Secondary, ui::icon::cancel, 2);
        Button(*a, kDismiss, Kind::Ghost, ui::icon::cancel, 2);
        Button(*a, kRetry, Kind::Secondary, ui::icon::refresh, 2);
        Button(*a, kBannerDetails, Kind::Secondary, ui::icon::info, 2);
        Button(*a, kBannerLibrary, Kind::Secondary, ui::icon::library, 2);
        a->activity_text = Control(*a, L"STATIC", L"", 0);
        // Home
        a->status = Control(*a, L"STATIC", L"", 0);
        Button(*a, kPrimary, Kind::Primary, ui::icon::play, 2);
        Button(*a, kAddContent, Kind::Secondary, ui::icon::add);
        Button(*a, kRemove, Kind::Secondary, ui::icon::remove, 2);
        Button(*a, kLibrary, Kind::Secondary, ui::icon::library);
        // Library / dashboard
        a->search = Control(*a, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, kSearch);
        for (int i = 0; i < 4; ++i) Button(*a, kFilter0 + i, Kind::Chip);
        a->library_list = Control(*a, L"LISTBOX", L"", LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP | LBS_NOINTEGRALHEIGHT, kGameList);
        // Import
        a->drop_text = Control(*a, L"STATIC", L"", SS_CENTER);
        Button(*a, kImportFolder, Kind::Secondary, ui::icon::folder, 2);
        // Settings
        a->renderer_hint = Control(*a, L"STATIC", L"", 0);
        Button(*a, kRenderer, Kind::Option, nullptr, 2);
        Button(*a, kRendererNative, Kind::Option, nullptr, 2);
        Button(*a, kApiD3D12, Kind::Option, nullptr, 2);
        Button(*a, kApiVulkan, Kind::Option, nullptr, 2);
        Button(*a, kNativeDevelopment, Kind::Toggle, nullptr, 2);
        Button(*a, kFullscreen, Kind::Toggle, nullptr, 2);
        Button(*a, kCaptureCrash, Kind::Toggle, nullptr, 2);
        a->root_text = Control(*a, L"STATIC", L"", SS_PATHELLIPSIS);
        Button(*a, kFolder, Kind::Secondary, nullptr, 2);
        Button(*a, kStorage, Kind::Secondary, nullptr, 2);
        Button(*a, kCheck, Kind::Secondary, nullptr, 2);
        Button(*a, kLangPt, Kind::Chip, nullptr, 2);
        Button(*a, kLangEn, Kind::Chip, nullptr, 2);
        a->console_language_label = Control(*a, L"STATIC", L"", 0);
        a->console_country_label = Control(*a, L"STATIC", L"", 0);
        a->console_hint = Control(*a, L"STATIC", L"", 0);
        for (int id : {kConsoleLanguage, kConsoleCountry}) {
          const auto combo = Control(*a, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, id);
          SetWindowTheme(combo, L"DarkMode_CFD", nullptr);
          SetWindowSubclass(combo, ComboProc, 0, 0);
          auto add = [&](uint32_t value, const char* name) {
            const auto text = Wide(name);
            const auto index = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
            SendMessageW(combo, CB_SETITEMDATA, index, value);
          };
          if (id == kConsoleLanguage) for (const auto& v : ConsoleLanguages()) add(v.id, v.name);
          else for (const auto& v : ConsoleLocales()) add(v.country, v.name);
        }
        a->env_text = Control(*a, L"STATIC", L"", 0);
        a->details = Control(*a, L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP);
        SetWindowTheme(a->library_list, L"DarkMode_Explorer", nullptr);
        SetWindowTheme(a->details, L"DarkMode_Explorer", nullptr);
        ApplyFonts(*a);
        Labels(*a); SetPageTexts(*a); Layout(*a);
        DragAcceptFiles(window, TRUE); SetTimer(window, 1, 100, nullptr);
        // Show the main window before asking about an existing installation.
        // A modal during WM_CREATE has an invisible owner and no taskbar entry.
        PostMessageW(window, kOpenEnvironment, 0, 0);
        return 0;
      }
      case kOpenEnvironment: {
        a->initializing = false;
        if (!a->preferences.empty()) CheckEnvironmentAsync(*a, a->root);
        else Load(*a, a->root);
        CheckForUpdate(*a);
        return 0;
      }
      case kUpdateChecked: {
        std::unique_ptr<std::optional<AvailableUpdate>> found(reinterpret_cast<std::optional<AvailableUpdate>*>(lp));
        if (a->update_check.joinable()) a->update_check.join();
        a->update = std::move(*found);
        Labels(*a); Enable(*a);
        return 0;
      }
      case kUpdateFinished: {
        std::unique_ptr<std::string> error(reinterpret_cast<std::string*>(lp));
        if (a->worker.joinable()) a->worker.join();
        a->busy = false; KillTimer(window, 2);
        if (error->empty()) {
          // The new files are in place; the old launcher image was only renamed.
          if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", a->self.c_str(), nullptr, a->self.parent_path().c_str(), SW_SHOWNORMAL)) <= 32) {
            *error = Tr("A nova versão não iniciou. Feche o aplicativo e execute recover-update.cmd para restaurar a anterior.",
                        "The new version could not start. Close the application and run recover-update.cmd to restore the previous version.");
          } else {
          DestroyWindow(window); return 0;
          }
        }
        a->result = a->cancel ? 2 : 3;
        a->result_title = WTr("Atualização não instalada", "Update not installed");
        Set(a->activity_text, *error);
        Set(a->details, *error + "\r\n" + Utf8(Updates(*a)));
        Enable(*a); return 0;
      }
      case kEnvironmentChecked: {
        std::unique_ptr<std::string> error(reinterpret_cast<std::string*>(lp));
        if (a->worker.joinable()) a->worker.join();
        a->busy = false; a->initializing = false;
        if (!error->empty()) throw std::runtime_error(*error);
        const auto root = a->pending_root;
        if (a->pending_payload_current) {
          a->targets.clear(); a->checked.clear(); a->catalog_loaded = false;
          Load(*a, root, a->pending_full_check ? InspectionMode::Full : InspectionMode::Preview); SavePreferences(*a); return 0;
        }
        if (fs::exists(root / "deps.lock.json")) {
          std::string message = Tr("Esta instalação precisa ser atualizada. Jogos, perfis e saves serão preservados.\n\nComponentes:", "This installation needs an update. Games, profiles and saves will be preserved.\n\nComponents:");
          for (const auto& item : a->pending_payload.components) message += "\n" + item;
          message += Tr("\n\nAtualizar agora?", "\n\nUpdate now?");
          if (MessageBoxW(window, Wide(message).c_str(), L"x360-native", MB_YESNO | MB_ICONINFORMATION) != IDYES) { Enable(*a); return 0; }
        }
        a->root = root; a->targets.clear(); a->checked.clear(); a->catalog_loaded = false;
        a->busy = true; a->initializing = true;
        SetActivity(*a, 4, WTr("Organizando ambiente", "Preparing environment"), Tr("Aguarde a atualização dos componentes do aplicativo.", "Updating application components."));
        Enable(*a);
        a->worker = std::thread([a, root] {
          try { InitializeEnvironment(Payload(*a), root, true, [a] { if (a->cancel.load()) throw std::runtime_error("Environment update cancelled; reopen to resume."); }); Post(a->window, kInitialized, ""); }
          catch (const std::exception& error) { Post(a->window, kInitialized, error.what()); }
        });
        return 0;
      }
      case WM_SIZE: if (a->details) Layout(*a); return 0;
      case WM_DPICHANGED: {
        a->dpi = g_dpi_override ? g_dpi_override : HIWORD(wp);
        ApplyFonts(*a);
        auto rect = reinterpret_cast<RECT*>(lp);
        SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        Layout(*a); return 0;
      }
      case WM_GETMINMAXINFO: {
        auto* size = reinterpret_cast<MINMAXINFO*>(lp); size->ptMinTrackSize = {MulDiv(1000, a->dpi, 96), MulDiv(700, a->dpi, 96)}; return 0;
      }
      case WM_MOUSEWHEEL:
        if (PageOf(*a) == Page::Settings && a->geo.scroll_max > 0) {
          a->scroll -= GET_WHEEL_DELTA_WPARAM(wp) * ui::S(72, a->dpi) / WHEEL_DELTA;
          Layout(*a); return 0;
        }
        break;
      case kReveal: Reveal(*a, reinterpret_cast<HWND>(wp)); return 0;
      case kPreview: ApplyPreview(*a, int(wp)); return 0;
      case WM_COMMAND: {
        ActionTrace trace("ui.command." + std::to_string(LOWORD(wp)));
        auto id = LOWORD(wp);
        if (id == kPrimary) id = a->next_action;
        if (id == IDOK) {
          auto focus = GetFocus();
          if (focus == a->library_list && IsWindowEnabled(Item(*a, kPrimary))) SendMessageW(Item(*a, kPrimary), BM_CLICK, 0, 0);
          else if (focus && focus != a->search && focus != a->details && IsWindowEnabled(focus)) SendMessageW(focus, BM_CLICK, 0, 0);
          return 0;
        }
        if (id == IDCANCEL) { GoBack(*a); return 0; }
        if (id == kDismiss) { a->result = 0; Layout(*a); return 0; }
        if (id == kBannerDetails) id = kDetails;
        if (id == kBannerLibrary) id = kLibrary;
        if (a->busy && id != kDetails && id != kData && id != kCancel && id != kDismiss &&
            id != kHome && id != kDashboards && id != kGames && id != kImportPage && id != kLibrary &&
            id != kSearch && id != kGameList && !(id >= kFilter0 && id <= kFilter3)) return 0;
        if (id == kDetails || id == kSettings) {
          if (id == kDetails) { a->show_details = !a->show_details; a->settings = false; }
          else { if (a->busy) return 0; a->settings = !a->settings; a->show_details = false; a->scroll = 0; }
          SetPageTexts(*a);
          if (!a->show_details && !a->settings && !a->busy) InspectSelected(*a);
          Layout(*a); return 0;
        }
        if (id == kData && !a->root.empty()) { auto p = a->root / ".local"; fs::create_directories(p); ShellExecuteW(window, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL); return 0; }
        if (id == kCancel) { a->cancel = true; Set(a->activity_text, Tr("Cancelando... Aguarde a etapa atual terminar com segurança.", "Cancelling... Wait for the current step to finish safely.")); RefreshBanner(*a); return 0; }
        if (id == kConsoleLanguage || id == kConsoleCountry) {
          if (HIWORD(wp) == CBN_SETFOCUS) { Reveal(*a, reinterpret_cast<HWND>(lp)); return 0; }
          if (a->root.empty() || HIWORD(wp) != CBN_SELCHANGE) return 0;
          const auto combo = Item(*a, id);
          const auto selected = SendMessageW(combo, CB_GETCURSEL, 0, 0);
          if (selected == CB_ERR) return 0;
          auto settings = a->console_settings;
          const auto value = uint32_t(SendMessageW(combo, CB_GETITEMDATA, selected, 0));
          if (id == kConsoleLanguage) settings.language = value; else settings.country = value;
          WorkspaceLease lease(ProjectPaths{a->root});
          SaveConsoleSettings(ProjectPaths{a->root}, settings);
          a->console_settings = settings;
          return 0;
        }
        if (id == kCaptureCrash) {
          a->capture_crash = !a->capture_crash;
          InvalidateRect(window, nullptr, FALSE); return 0;
        }
        if (id == kRenderer || id == kRendererNative || id == kNativeDevelopment || id == kFullscreen || id == kApiD3D12 || id == kApiVulkan) {
          if (a->root.empty()) return 0;
          auto settings = a->renderer_settings;
          if (id == kFullscreen) settings.fullscreen = !settings.fullscreen;
          else if (id == kNativeDevelopment) {
            settings.experimental = !settings.experimental;
            if (!settings.experimental) { settings.renderer = Renderer::Xenos; settings.api = GraphicsApi::D3D12; }
          } else if (id == kRendererNative) {
            if (!settings.experimental) return 0;
            settings.renderer = Renderer::Native;
          } else if (id == kApiD3D12) settings.api = GraphicsApi::D3D12;
          else if (id == kApiVulkan) {
            if (!settings.experimental || settings.renderer == Renderer::Native) return 0;
            settings.api = GraphicsApi::Vulkan;
          } else settings.renderer = Renderer::Xenos;
          SaveRendererSettings(ProjectPaths{a->root}, settings);
          a->renderer_settings = settings;
          UpdateRendererControls(*a); Enable(*a);
          return 0;
        }
        if (id == kHome || id == kDashboards || id == kGames || id == kImportPage || id == kAddContent || id == kLibrary) {
          Navigate(*a, id);
          Load(*a, a->root); return 0;
        }
        if (id == kSearch) {
          if (HIWORD(wp) != EN_CHANGE) return 0;
          wchar_t text[256]{}; GetWindowTextW(a->search, text, 256);
          a->query = Lower(text);
          RebuildRows(*a);
          if (std::find(a->rows.begin(), a->rows.end(), a->selected) == a->rows.end()) a->selected = a->rows.empty() ? a->targets.size() : a->rows.front();
          SyncSelection(*a); InspectSelected(*a); return 0;
        }
        if (id >= kFilter0 && id <= kFilter3) {
          a->filter = id - kFilter0; RebuildRows(*a);
          if (std::find(a->rows.begin(), a->rows.end(), a->selected) == a->rows.end()) a->selected = a->rows.empty() ? a->targets.size() : a->rows.front();
          SyncSelection(*a); InspectSelected(*a); return 0;
        }
        if (id == kStorage) {
          auto path = PickFolder(window, WTr("Selecione a pasta de saves e perfis", "Select the saves and profiles folder").c_str());
          if (!path.empty() && MessageBoxW(window, WTr("Usar esta pasta nas próximas sessões? Os dados anteriores permanecem na pasta original e não serão movidos ou apagados. Para reutilizar saves existentes, escolha a pasta que contém os diretórios dos títulos.", "Use this folder in future sessions? Previous data stays in its original folder and will not be moved or deleted. To reuse existing saves, choose the folder containing the title directories.").c_str(), L"x360-native", MB_YESNO | MB_ICONINFORMATION) == IDYES) {
            WorkspaceLease lease(ProjectPaths{a->root}); SetUserDataRoot(ProjectPaths{a->root}, path); a->targets.clear(); a->catalog_loaded = false; Load(*a, a->root);
          }
          return 0;
        }
        if (id == kLanguage || id == kLangPt || id == kLangEn) {
          english = id == kLanguage ? !english : id == kLangEn;
          Labels(*a); auto selected = a->selected; Load(*a, a->root); a->selected = selected; SyncSelection(*a); InspectSelected(*a); SavePreferences(*a); return 0;
        }
        if (id == kGameList) {
          if (HIWORD(wp) == LBN_SELCHANGE) {
            auto item = size_t(SendMessageW(a->library_list, LB_GETCURSEL, 0, 0));
            if (item < a->rows.size()) { a->selected = a->rows[item]; InspectSelected(*a); }
          } else if (HIWORD(wp) == LBN_DBLCLK && IsWindowEnabled(Item(*a, kPrimary))) SendMessageW(Item(*a, kPrimary), BM_CLICK, 0, 0);
          return 0;
        }
        if (id == kUpdate && a->update) {
          const auto question = Tr("Instalar a versão ", "Install version ") + a->update->version + Tr(" (atual: ", " (current: ") + X360N_VERSION + ")?\n\n" +
            Tr("O pacote é baixado do GitHub Releases do x360-native e conferido pelo SHA-256 publicado e pelo índice do próprio pacote. "
               "Ambiente, jogos, perfis e saves são preservados; a versão atual fica em data\\updates; recover-update.cmd permite restaurá-la. "
               "O aplicativo reinicia ao final; o ambiente pode pedir para ser atualizado em seguida.",
               "The package is downloaded from x360-native's GitHub Releases and checked against the published SHA-256 and its own index. "
               "Environment, games, profiles and saves are preserved; the current version stays in data\\updates; recover-update.cmd can restore it. "
               "The application restarts at the end; the environment may then ask to be updated.");
          if (MessageBoxW(window, Wide(question).c_str(), L"x360-native", MB_YESNO | MB_ICONINFORMATION) == IDYES) StartUpdate(*a);
          return 0;
        }
        if (id == kRemove) {
          if (!Removable(*a)) return 0;
          const auto& t = a->targets[a->selected];
          const auto question = Tr("Remover \"", "Remove \"") + (english ? t.label_en : t.label) + Tr("\" da biblioteca?\n\n", "\" from the library?\n\n") +
            Tr("Serão apagados deste ambiente: a cópia importada do jogo, sua cópia na My Games das dashboards e a preparação AOT gerada para ele. "
               "Saves, perfis e o arquivo original que você importou são mantidos. Uma pasta importada no próprio local não é apagada.\n\n"
               "Para jogar de novo, importe e prepare o jogo outra vez.",
               "This deletes from the environment: the game's imported copy, its copy in the dashboards' My Games and the AOT preparation generated for it. "
               "Saves, profiles and the original file you imported are kept. A folder imported in place is not deleted.\n\n"
               "To play again, import and prepare the game again.");
          if (MessageBoxW(window, Wide(question).c_str(), L"x360-native", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) Start(*a, kRemove);
          return 0;
        }
        if (id == kRetry) {
          a->result = 0;
          if (a->last_action == kUpdate) StartUpdate(*a);
          else if (a->last_action == kImport && !a->last_source.empty()) Start(*a, kImport, a->last_source);
          else if (a->last_action == kPrepare || a->last_action == kLaunch || a->last_action == kVerifyTarget) {
            const auto target = std::find_if(a->targets.begin(), a->targets.end(), [&](const auto& t) { return t.id == a->operation_target; });
            if (target == a->targets.end()) throw std::runtime_error("O alvo da operação não está mais neste catálogo. Verifique o ambiente.");
            const auto index = size_t(target - a->targets.begin());
            Navigate(*a, target->kind == "dashboard" ? kDashboards : kGames);
            Load(*a, a->root); a->selected = index; SyncSelection(*a); InspectSelected(*a);
            PostMessageW(window, WM_COMMAND, WPARAM(a->last_action), 0);
          }
          else Layout(*a);
          return 0;
        }
        if (id == kFolder) {
          auto root = PickFolder(window, WTr("Escolha uma pasta vazia para o ambiente ou uma existente do x360-native", "Choose an empty environment folder or an existing x360-native environment").c_str());
          if (!root.empty()) {
            if (!a->preferences.empty()) {
              CheckEnvironmentAsync(*a, root); return 0;
            }
            a->targets.clear(); a->checked.clear(); a->catalog_loaded = false;
            Load(*a, root);
            SavePreferences(*a);
          }
        }
        if (id == kCheck) {
          if (!a->preferences.empty()) {
            CheckEnvironmentAsync(*a, a->root, true); return 0;
          }
          Load(*a, a->root, InspectionMode::Full);
        }
        if (id == kImport || id == kImportFolder) { auto source = PickFolder(window, WTr(id == kImport ? "Selecione um arquivo original" : "Selecione a pasta com seus arquivos originais", id == kImport ? "Select an original file" : "Select the folder with your original files").c_str(), id == kImportFolder); if (!source.empty()) Start(*a, kImport, source); }
        if (id == kPrepare) {
          if (!a->environment.msvc) {
            if (MessageBoxW(window, Wide(Tr("Falta o componente C++ do Microsoft Build Tools 2022. Abrir o instalador oficial com os componentes necessários selecionados?\n\nEsta é uma instalação no sistema: o Windows pode pedir permissão de administrador. O instalador mostra espaço, downloads e termos antes da instalação. Use o próprio instalador para cancelar. Jogos e dados serão preservados.", "Microsoft Build Tools 2022 C++ is missing. Open the official installer with the required components preselected?\n\nThis changes the system: Windows may request administrator permission. The installer shows space, downloads and terms before installation. Cancel using the installer itself. Games and data are preserved.")).c_str(), L"Microsoft Build Tools 2022", MB_YESNO | MB_ICONINFORMATION) == IDYES) Start(*a, kInstall);
            return 0;
          }
          auto answer = MessageBoxW(window, Wide(Tr("Preparar verifica e obtém Git, CMake, Ninja, LLVM e o SDK fixados. Downloads de ferramentas: até 905 MiB; o SDK e suas dependências são adicionais. Arquivos verificados em cache são reutilizados. Ferramentas incompatíveis serão atualizadas com uma cópia anterior preservada. Reserve espaço para fontes e compilação (recomendação inicial: 30 GiB, não uma medição do mínimo).\n\nPermitir os downloads necessários? Não há instalação global nem elevação. Não = usar somente os arquivos já presentes.", "Preparation verifies and obtains pinned Git, CMake, Ninja, LLVM and SDK. Tool downloads: up to 905 MiB; SDK and its dependencies are additional. Verified cached files are reused. Incompatible tools will be updated with their previous copies preserved. Reserve space for sources and compilation (initial recommendation: 30 GiB, not a measured minimum).\n\nAllow required downloads? No global installation or elevation. No = use only existing files.")).c_str(), L"x360-native", MB_YESNOCANCEL | MB_ICONINFORMATION);
          if (answer == IDCANCEL) return 0;
          a->allow_downloads = answer == IDYES;
        }
        if (id == kPrepare || id == kLaunch || id == kVerifyTarget) {
          InspectSelected(*a);
          if ((id == kPrepare && (a->current.inputs_ok || a->current.verification_pending)) ||
              (id == kLaunch && (a->current.prepared || a->current.launch_candidate)) ||
              (id == kVerifyTarget && a->current.verification_pending)) Start(*a, id);
        }
        return 0;
      }
      case WM_DROPFILES: {
        auto drop = reinterpret_cast<HDROP>(wp); wchar_t path[32768]{};
        auto count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        if (count == 1) DragQueryFileW(drop, 0, path, 32768);
        DragFinish(drop);
        if (!a->busy && count == 1) {
          Navigate(*a, kImportPage); Load(*a, a->root); Start(*a, kImport, path);
        }
        return 0;
      }
      case WM_TIMER: {
        ActionTrace trace("ui.timer");
#ifndef X360N_DISTRIBUTION
        if (!a->capture_settings.empty() && !a->busy && !a->initializing && ++a->capture_ticks > 10) {
          const auto output = std::move(a->capture_settings); a->capture_settings.clear();
          auto state_path = output; state_path += ".state.txt";
          std::ofstream state(state_path);
          for (int id : {kConsoleLanguage, kConsoleCountry}) {
            const auto combo = Item(*a, id);
            state << id << " count=" << SendMessageW(combo, CB_GETCOUNT, 0, 0)
                  << " selected=" << SendMessageW(combo, CB_GETCURSEL, 0, 0) << '\n';
          }
          state.close();
          if (a->capture_page == kGames) { Navigate(*a, kGames); Load(*a, a->root); }
          else { a->settings = true; a->show_details = false; }
          SetPageTexts(*a); Layout(*a);
          ShowWindow(window, SW_SHOWNOACTIVATE);
          RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
          RECT client{}; GetClientRect(window, &client);
          HDC source = GetDC(window), dc = CreateCompatibleDC(source);
          HBITMAP bitmap = CreateCompatibleBitmap(source, client.right, client.bottom);
          const auto old = SelectObject(dc, bitmap);
          Paint(*a, dc);
          // Render our own controls into the same surface, even with a hidden
          // parent. This does not capture or operate any other application.
          for (auto child : a->all) {
            if (!(GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE)) continue;
            RECT rect{}; GetWindowRect(child, &rect);
            MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&rect), 2);
            const auto saved_dc = SaveDC(dc);
            SetViewportOrgEx(dc, rect.left, rect.top, nullptr);
            IntersectClipRect(dc, 0, 0, rect.right - rect.left, rect.bottom - rect.top);
            SendMessageW(child, WM_PRINT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT | PRF_ERASEBKGND | PRF_CHILDREN);
            RestoreDC(dc, saved_dc);
          }
          SelectObject(dc, old); DeleteDC(dc); ReleaseDC(window, source);
          bool saved = false;
          {
            UINT count = 0, size = 0; Gdiplus::GetImageEncodersSize(&count, &size);
            std::vector<uint8_t> storage(size);
            auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data());
            Gdiplus::GetImageEncoders(count, size, codecs);
            Gdiplus::Bitmap image(bitmap, nullptr);
            for (UINT i = 0; i < count; ++i) if (std::wstring_view(codecs[i].MimeType) == L"image/png")
              saved = image.Save(output.c_str(), &codecs[i].Clsid) == Gdiplus::Ok;
          }
          DeleteObject(bitmap);
          DestroyWindow(window); PostQuitMessage(saved ? 0 : 1); return 0;
        }
#endif
        if (wp == 2) {
          if (!a->busy) { KillTimer(window, 2); return 0; }
          if (a->geo.banner_on && a->busy) {
            // Repaint only the animated bar (every tick) and the elapsed-time label (once a second).
            const int pad = ui::S(4, a->dpi);
            RECT bar = a->geo.bar; InflateRect(&bar, 0, pad);
            InvalidateRect(window, &bar, FALSE);
            if (++a->anim_ticks % 30 == 0) InvalidateRect(window, &a->geo.elapsed, FALSE);
          }
          return 0;
        }
        if (GetForegroundWindow() != window || a->busy) { a->buttons_down = 0; return 0; }
        XINPUT_STATE input{};
        if (XInputGetState(0, &input) != ERROR_SUCCESS) { a->buttons_down = 0; return 0; }
        auto pressed = input.Gamepad.wButtons & ~a->buttons_down; a->buttons_down = input.Gamepad.wButtons;
        if (pressed & (XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_RIGHT | XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_LEFT)) {
          bool previous = pressed & (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_LEFT);
          if (GetFocus() == a->library_list) {
            auto row = int(SendMessageW(a->library_list, LB_GETCURSEL, 0, 0)) + (previous ? -1 : 1);
            if (row >= 0 && row < int(a->rows.size())) { a->selected = a->rows[size_t(row)]; SyncSelection(*a); InspectSelected(*a); return 0; }
          }
          SetFocus(GetNextDlgTabItem(window, GetFocus(), previous));
        }
        if (pressed & XINPUT_GAMEPAD_A) SendMessageW(GetFocus(), BM_CLICK, 0, 0);
        if (pressed & XINPUT_GAMEPAD_B) GoBack(*a);
        return 0;
      }
      case kProgress: {
        std::unique_ptr<std::string> text(reinterpret_cast<std::string*>(lp)); Set(a->details, *text);
        if (!a->cancel) Set(a->activity_text, *text);
        RefreshBanner(*a); return 0;
      }
      case kInspected: {
        std::unique_ptr<std::string> error(reinterpret_cast<std::string*>(lp));
        if (a->worker.joinable()) a->worker.join();
        a->busy = false; a->initializing = false;
        if (error->empty()) {
          AdoptSnapshot(*a);
          a->environment = std::move(a->pending_environment);
          a->renderer_settings = a->pending_renderer;
          a->console_settings = a->pending_console;
          a->supervisor_available = a->pending_supervisor_available;
          Load(*a, a->root);
          StartHints(*a);
#ifndef X360N_DISTRIBUTION
          if (!a->auto_import.empty() && !a->busy) {
            const auto source = std::move(a->auto_import); a->auto_import.clear();
            Navigate(*a, kImportPage); Load(*a, a->root); Start(*a, kImport, source);
          }
#endif
        } else {
          a->current = {}; a->current.inputs_ok = false; a->next_action = kFolder;
          a->result = 3; a->result_title = WTr("Não foi possível conferir o conteúdo", "Could not check content");
          Set(a->activity_text, Tr("Consulte o Diagnóstico para o motivo. Seus arquivos não foram alterados.", "See Diagnostics for the reason. Your files were not changed."));
          Set(a->details, *error); Enable(*a);
        }
        return 0;
      }
      case kInitialized: {
        std::unique_ptr<std::string> error(reinterpret_cast<std::string*>(lp));
        if (a->worker.joinable()) a->worker.join();
        a->busy = false; a->initializing = false;
        if (!error->empty()) { a->root.clear(); a->startup_error = *error; }
        Load(*a, a->root, a->pending_full_check ? InspectionMode::Full : InspectionMode::Preview);
        if (error->empty()) SavePreferences(*a);
        else {
          a->result = 3; a->result_title = WTr("Não foi possível organizar esta pasta", "Could not prepare this folder");
          Set(a->activity_text, Tr("Escolha outra pasta em Configurações. Seus arquivos não foram alterados.", "Choose another folder in Settings. Your files were not changed."));
          Set(a->details, *error); Layout(*a);
        }
        return 0;
      }
      case kDetected: {
        std::unique_ptr<std::string> id(reinterpret_cast<std::string*>(lp));
        // Keep the worker's immutable catalog intact while updating visible selection.
        auto match = std::find_if(a->targets.begin(), a->targets.end(), [&](const auto& t) { return t.id == *id; });
        if (match == a->targets.end()) return 0;
        a->games = match->kind != "dashboard";
        a->dashboard_indices.clear();
        for (size_t i = 0; i < a->targets.size(); ++i) if ((a->targets[i].kind != "dashboard") == a->games) a->dashboard_indices.push_back(i);
        RebuildRows(*a);
        for (size_t i = 0; i < a->targets.size(); ++i) if (a->targets[i].id == *id) {
          if (std::find(a->rows.begin(), a->rows.end(), i) != a->rows.end()) { a->selected = i; SyncSelection(*a); }
          break;
        }
        return 0;
      }
      case kFinished: {
        ActionTrace trace("ui.finished");
        std::unique_ptr<std::string> text(reinterpret_cast<std::string*>(lp));
        if (a->worker.joinable()) a->worker.join(); a->busy = false; a->session = false; a->finishing_session = false; a->installing = false; a->preparing_id.clear();
        KillTimer(window, 2);
        AdoptSnapshot(*a);
        if ((a->last_action == kLaunch && a->failed && !a->launch_verified) || a->cancel.load()) {
          const auto it = std::find_if(a->targets.begin(), a->targets.end(), [&](const auto& t) { return t.id == a->operation_target; });
          if (it != a->targets.end()) {
            auto& state = a->checked.at(size_t(it - a->targets.begin())).state;
            state.verification_pending = false;
            state.inputs_ok = false;
            state.prepared = false; state.issues.push_back(*text);
          }
        }
        if (a->last_action == kRemove) Load(*a, a->root);
        if (a->importing) {
          Load(*a, a->root);
          for (size_t i = 0; i < a->targets.size(); ++i) if (a->targets[i].id == a->imported_id) { a->selected = i; SyncSelection(*a); break; }
        }
        InspectSelected(*a);
        const auto report = Wide(*text + "\r\n\r\nLogs: " + Utf8(a->logs) + "\r\n\r\n");
        SendMessageW(a->details, EM_SETSEL, 0, 0); SendMessageW(a->details, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(report.c_str()));
        const bool cancelled = a->cancel.load();
        if (a->failed) {
          a->result = 3;
          a->result_title = cancelled ? WTr("Operação cancelada", "Operation cancelled") : WTr("Não foi possível concluir", "Could not finish");
          if (cancelled) a->result = 2;
        } else if (a->importing && !a->import_recognized) {
          a->result = 2; a->result_title = WTr("Nenhum conteúdo importado", "Nothing was imported");
        } else {
          a->result = 1;
          a->result_title = a->importing ? WTr("Importação concluída", "Import finished") :
            a->last_action == kLaunch ? WTr("Sessão encerrada", "Session ended") :
            a->last_action == kVerifyTarget ? WTr("Verificação concluída", "Verification finished") :
            a->last_action == kRemove ? WTr("Jogo removido", "Game removed") :
            a->last_action == kInstall ? WTr("Instalador encerrado", "Installer closed") : WTr("Preparação concluída", "Preparation finished");
        }
        Set(a->activity_text, *text);
        Layout(*a); Enable(*a);
        StartHints(*a);
        return 0;
      }
      case kHinted: {
        std::unique_ptr<App::Hints> found(reinterpret_cast<App::Hints*>(lp));
        if (a->busy || found->generation != a->hint_generation || a->checked.size() != a->targets.size()) return 0;
        for (const auto& [index, hint] : found->items)
          if (index < a->checked.size()) ApplyReceiptHint(a->checked[index].state, hint);
        // Rows draw their state; only a status filter changes which rows exist.
        if (a->filter != 0) {
          RebuildRows(*a);
          if (std::find(a->rows.begin(), a->rows.end(), a->selected) == a->rows.end()) a->selected = a->rows.empty() ? a->targets.size() : a->rows.front();
          SyncSelection(*a);
        }
        InvalidateRect(a->library_list, nullptr, FALSE);
        if (!a->show_details && !a->settings) InspectSelected(*a);
        return 0;
      }
      case kSessionStarted: {
        std::unique_ptr<std::string> text(reinterpret_cast<std::string*>(lp));
        if (a->busy && a->last_action == kLaunch) {
          a->session = true;
          a->activity_title = WTr("Sessão em andamento", "Session running");
          Set(a->activity_text, Tr("A dashboard está sob supervisão. Feche a janela da plataforma para encerrar.", "The dashboard is supervised. Close the platform window to end the session."));
          Enable(*a);
        }
        return 0;
      }
      case kSessionClosed: {
        std::unique_ptr<std::string> text(reinterpret_cast<std::string*>(lp));
        if (a->busy && a->last_action == kLaunch) {
          a->session = false; a->finishing_session = true;
          a->activity_title = WTr("Sessão encerrada", "Session ended");
          Set(a->activity_text, Tr("A janela foi fechada. Finalizando os registros locais.", "The window closed. Finalizing local diagnostics."));
          ActionTrace::Event("ui.session.closed"); Enable(*a);
        }
        return 0;
      }
      case WM_CLOSE:
        if (a->busy) {
          MessageBoxW(window, a->installing ? L"Conclua ou cancele pelo instalador Microsoft e aguarde o retorno." : a->session ? L"Encerre pela janela da plataforma. Voltar pelo menu do jogo retorna à dashboard." : a->finishing_session ? L"A sessão já encerrou. Aguarde a conclusão dos registros locais." : L"Cancele a operação e aguarde sua conclusão antes de fechar.", L"Operação em andamento", MB_OK | MB_ICONINFORMATION); return 0;
        }
        DestroyWindow(window); return 0;
      case WM_DESTROY: a->closing = true; PostQuitMessage(0); return 0;
    }
  } catch (const std::exception& error) {
    if (!a->worker.joinable()) { a->busy = false; a->session = false; }
    a->result = 3; a->result_title = WTr("Não foi possível concluir", "Could not finish");
    Set(a->activity_text, Tr("Verifique o Diagnóstico e tente novamente. Seus arquivos foram preservados.", "Check Diagnostics and try again. Your files were preserved."));
    Set(a->details, error.what()); Layout(*a); Enable(*a);
  }
  return DefWindowProcW(window, message, wp, lp);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS}; InitCommonControlsEx(&controls);
  Gdiplus::GdiplusStartupInput gdiplus_input; ULONG_PTR gdiplus_token = 0;
  Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr);
  int result = 0;
  {
  App app;
  wchar_t self[32768]{}; GetModuleFileNameW(nullptr, self, 32768); app.self = self;
  if (!fs::is_directory(Payload(app))) {
    if (auto root = FindProjectRoot(app.self)) app.root = root->root;
  } else {
    {
      app.preferences = app.self.parent_path() / "data/preferences.txt";
      std::ifstream in(app.preferences); std::string language, root;
      std::getline(in, language); std::getline(in, root); english = language == "en";
      auto saved = root == "." ? app.preferences.parent_path() / "environment" : fs::path(std::u8string(root.begin(), root.end()));
      if (!root.empty() && fs::is_regular_file(saved / "deps.lock.json")) app.root = saved;
    }
  }
  int count = 0; auto** args = CommandLineToArgvW(GetCommandLineW(), &count);
  for (int i = 1; i < count; ++i) {
    const std::wstring option = args[i];
    if (option == L"--root" && i + 1 < count) app.root = args[++i];
#ifndef X360N_DISTRIBUTION
    // Local review aids: language and DPI override for screenshots; no effect on data.
    else if (option == L"--lang" && i + 1 < count) english = std::wstring(args[++i]) == L"en";
    else if (option == L"--dpi" && i + 1 < count) g_dpi_override = UINT(_wtoi(args[++i]));
    else if (option == L"--import" && i + 1 < count) app.auto_import = args[++i];
    else if (option == L"--capture-settings" && i + 1 < count) app.capture_settings = args[++i];
    else if (option == L"--capture-games" && i + 1 < count) { app.capture_settings = args[++i]; app.capture_page = kGames; }
#endif
  }
  LocalFree(args);
  if (!app.preferences.empty()) {
    std::ifstream channel(app.preferences.parent_path() / "update-channel.txt");
    std::string selected; std::getline(channel, selected);
    if (selected == "preview") app.update_channel = UpdateChannel::Preview;
  }
  // A first-time user shouldn't need to choose a source repository. The package
  // prepares its own verified source payload in the user's normal data folder.
  // An existing installation is preserved; its location can be changed in Settings.
  if (!app.preferences.empty()) {
    const auto initial = app.preferences.parent_path() / "environment";
    try {
      if (app.root.empty()) app.root = initial;
      app.initializing = true;
    } catch (const std::exception& error) {
      // Leave the first-run folder action available. Never overwrite a different
      // existing source version or hide it by deleting the user's environment.
      app.startup_error = error.what();
    }
  }
  WNDCLASSW cls{}; cls.lpfnWndProc = WndProc; cls.hInstance = instance;
  cls.lpszClassName = L"X360NativePreparation"; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  cls.hbrBackground = nullptr; cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1)); RegisterClassW(&cls);
  auto window = CreateWindowExW(WS_EX_CONTROLPARENT, cls.lpszClassName, L"x360-native", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                               CW_USEDEFAULT, CW_USEDEFAULT, 1220, 800, nullptr, nullptr, instance, &app);
  if (!window) { Gdiplus::GdiplusShutdown(gdiplus_token); CoUninitialize(); return 1; }
  ShowWindow(window, show); UpdateWindow(window);
  {
    // An interrupted rename may have removed app/workspace, so this cannot
    // depend on successful payload discovery or initialized preferences.
    try { ConfirmUpdates(app.self.parent_path(), X360N_VERSION); }
    catch (const std::exception& error) {
      MessageBoxW(window, Wide(error.what()).c_str(), L"x360-native", MB_OK | MB_ICONERROR);
      DestroyWindow(window);
      Gdiplus::GdiplusShutdown(gdiplus_token); CoUninitialize(); return 1;
    }
  }
  SetFocus(GetDlgItem(window, kPrimary));
  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (msg.message == WM_KEYDOWN && msg.wParam == 'F' && (GetKeyState(VK_CONTROL) & 0x8000) && GetAncestor(msg.hwnd, GA_ROOT) == window) {
      if (IsWindowVisible(app.search)) SetFocus(app.search);
      continue;
    }
    if (!IsDialogMessageW(window, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  }
  result = int(msg.wParam);
  }
  Gdiplus::GdiplusShutdown(gdiplus_token);
  CoUninitialize(); return result;
}
