#include "gui.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <utility>

#include "imgui.h"

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl3.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

namespace {

std::vector<std::string> g_devices;
int g_device_index = 0;

char g_serial[128] = "";
bool g_serial_typed = false;
char g_host[128] = "";

int g_camera_index = 0;
int g_size_index = 0;
int g_fps_index = 0;
const int BITRATE_PRESETS[] = {1000000, 2000000, 4000000, 8000000, 16000000, 32000000};
int g_bitrate_preset = 2;

std::vector<std::string> g_sizes;
std::vector<int> g_size_w, g_size_h;
std::vector<int> g_fps_values;

float g_focus = 0.f;
float g_iso = 400.f;
float g_exposure_ms = 10.f;
int g_af_mode = 1;
const int g_af_mode_values[] = {0, 2, 1};
int g_ae_mode = AE_AUTO;
bool g_torch = false;
std::string g_status;
bool g_connected = false;
bool g_quit_requested = false;

bool g_show_preview = true;
GLuint g_preview_tex = 0;
uint64_t g_preview_seq = 0;
int g_preview_w = 0, g_preview_h = 0;

ImFont* g_font_regular = nullptr;
ImFont* g_font_bold = nullptr;
bool g_has_icons = false;

bool g_dark = true;
ImVec4 g_accent = ImVec4(0.29f, 0.62f, 1.0f, 1.0f);

void rebuild_fps(const std::vector<CameraInfo>& cams) {
    if (cams.empty()) { g_fps_values.clear(); return; }
    const int prev = g_fps_values.empty()
                         ? 0
                         : g_fps_values[std::clamp(g_fps_index, 0, (int)g_fps_values.size() - 1)];
    const CameraInfo& c = cams[std::clamp(g_camera_index, 0, (int)cams.size() - 1)];
    g_fps_values = c.fps;
    g_fps_index = 0;
    for (size_t i = 0; i < g_fps_values.size(); ++i) {
        if (g_fps_values[i] == prev) {
            g_fps_index = (int)i;
            return;
        }
    }
    if (!g_fps_values.empty()) {
        int best = g_fps_values[0];
        for (size_t i = 0; i < g_fps_values.size(); ++i) {
            if (std::abs(g_fps_values[i] - 30) < std::abs(best - 30)) {
                best = g_fps_values[i];
                g_fps_index = (int)i;
            }
        }
    }
}

void rebuild_sizes(const std::vector<CameraInfo>& cams) {
    g_sizes.clear();
    g_size_w.clear();
    g_size_h.clear();
    if (cams.empty()) { rebuild_fps(cams); return; }
    const CameraInfo& c = cams[std::clamp(g_camera_index, 0, (int)cams.size() - 1)];
    for (const CameraSize& s : c.sizes) {
        char b[64];
        snprintf(b, sizeof(b), "%dx%d", s.w, s.h);
        g_sizes.emplace_back(b);
        g_size_w.push_back(s.w);
        g_size_h.push_back(s.h);
    }
    g_size_index = std::clamp(g_size_index, 0, std::max(0, (int)g_sizes.size() - 1));
    rebuild_fps(cams);
}

std::string chosen_serial() {
    if (g_serial_typed && g_serial[0]) return g_serial;
    if (g_device_index >= 0 && g_device_index < (int)g_devices.size()) {
        std::string s = g_devices[g_device_index];
        size_t b = s.find("  [");
        return (b == std::string::npos) ? s : s.substr(0, b);
    }
    return {};
}

SDL_Window* g_window = nullptr;
SDL_GLContext g_gl = nullptr;

bool create_window() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    g_window = SDL_CreateWindow("scrcpy camera client", 932, 912, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!g_window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    g_gl = SDL_GL_CreateContext(g_window);
    if (!g_gl) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_MakeCurrent(g_window, g_gl);
    SDL_GL_SetSwapInterval(1);
    return true;
}

void destroy_window() {
    if (g_gl) SDL_GL_DestroyContext(g_gl);
    if (g_window) SDL_DestroyWindow(g_window);
    g_gl = nullptr;
    g_window = nullptr;
    SDL_Quit();
}

void seed_controls(const CameraInfo& c) {
    if (c.has_iso) g_iso = std::clamp(g_iso, (float)c.iso_min, (float)c.iso_max);
    if (c.has_exp) {
        float lo = (float)c.exp_min_ns / 1e6f;
        float hi = (float)c.exp_max_ns / 1e6f;
        g_exposure_ms = std::clamp(g_exposure_ms, lo, hi);
    }
    if (c.has_focus) g_focus = std::clamp(g_focus, 0.f, std::max(0.f, c.focus_min));
}

ImFont* load_font(ImGuiIO& io, const char* const* paths, float size) {
    for (int i = 0; paths[i]; ++i) {
        FILE* f = fopen(paths[i], "rb");
        if (!f) continue;
        fclose(f);
        if (ImFont* font = io.Fonts->AddFontFromFileTTF(paths[i], size)) return font;
    }
    return nullptr;
}

void load_fonts(ImGuiIO& io) {
    static const char* const regular_fallback[] = {
#ifdef _WIN32
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\tahoma.ttf",
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
#endif
        nullptr};
    static const char* const bold_fallback[] = {
#ifdef _WIN32
        "C:\\Windows\\Fonts\\seguisb.ttf",
        "C:\\Windows\\Fonts\\segoeuib.ttf",
        "C:\\Windows\\Fonts\\tahomabd.ttf",
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
#endif
        nullptr};

    std::string base;
    if (const char* b = SDL_GetBasePath()) base = b;
    auto try_file = [&](const std::string& p, float size) -> ImFont* {
        FILE* f = fopen(p.c_str(), "rb");
        if (!f) return nullptr;
        fclose(f);
        return io.Fonts->AddFontFromFileTTF(p.c_str(), size);
    };

    io.Fonts->AddFontDefault();
    g_font_regular = try_file(base + "fonts/Inter-Regular.ttf", 16.0f);
    if (!g_font_regular) g_font_regular = try_file("third_party/fonts/inter/Inter-Regular.ttf", 16.0f);
    if (!g_font_regular) g_font_regular = load_font(io, regular_fallback, 16.0f);
    if (g_font_regular) io.FontDefault = g_font_regular;

    static const ImWchar kIconRanges[] = {
        0xF011, 0xF011, 0xF021, 0xF021, 0xF030, 0xF030, 0xF03D, 0xF03D,
        0xF042, 0xF042, 0xF04B, 0xF04B, 0xF04D, 0xF04D, 0xF05B, 0xF05B,
        0xF0C1, 0xF0C1, 0xF0EB, 0xF0EB, 0xF185, 0xF185,
        0xF186, 0xF186, 0xF1DE, 0xF1DE, 0xF1EB, 0xF1EB, 0xF2F1, 0xF2F1,
        0xF390, 0xF390, 0xF0C7, 0xF0C7,
        0};
    std::string icon_path;
    if (const char* base = SDL_GetBasePath())
        icon_path = std::string(base) + "fonts/fa-solid-900.ttf";
    FILE* f = icon_path.empty() ? nullptr : fopen(icon_path.c_str(), "rb");
    if (!f) {
        icon_path = "third_party/fonts/fontawesome/fa-solid-900.ttf";
        f = fopen(icon_path.c_str(), "rb");
    }
    if (f && g_font_regular) {
        fclose(f);
        ImFontConfig cfg;
        cfg.MergeMode = true;
        cfg.PixelSnapH = true;
        cfg.GlyphOffset = ImVec2(0.f, 1.f);
        if (io.Fonts->AddFontFromFileTTF(icon_path.c_str(), 16.0f, &cfg, kIconRanges))
            g_has_icons = true;
    }

    g_font_bold = try_file(base + "fonts/Inter-SemiBold.ttf", 16.0f);
    if (!g_font_bold) g_font_bold = try_file("third_party/fonts/inter/Inter-SemiBold.ttf", 16.0f);
    if (!g_font_bold) g_font_bold = load_font(io, bold_fallback, 16.0f);
    if (!g_font_bold) g_font_bold = g_font_regular;
}

static std::string u8(unsigned cp) {
    std::string s;
    if (cp < 0x80) {
        s += (char)cp;
    } else if (cp < 0x800) {
        s += (char)(0xC0 | (cp >> 6));
        s += (char)(0x80 | (cp & 0x3F));
    } else {
        s += (char)(0xE0 | (cp >> 12));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    }
    return s;
}

static std::string label_icon(unsigned cp, const char* text) {
    if (!g_has_icons) return text;
    return u8(cp) + " " + text;
}

struct Palette {
    ImVec4 bg, card, frame, frame_h, frame_a, text, textdim, accent, accent_d, border;
};

const Palette kDark = {
    ImVec4(0.078f, 0.086f, 0.102f, 1.f), ImVec4(0.114f, 0.125f, 0.149f, 1.f),
    ImVec4(0.153f, 0.169f, 0.196f, 1.f), ImVec4(0.196f, 0.216f, 0.251f, 1.f),
    ImVec4(0.227f, 0.251f, 0.290f, 1.f), ImVec4(0.843f, 0.867f, 0.902f, 1.f),
    ImVec4(0.55f, 0.58f, 0.63f, 1.f),    ImVec4(0.29f, 0.62f, 1.0f, 1.f),
    ImVec4(0.20f, 0.45f, 0.78f, 1.f),    ImVec4(0.18f, 0.20f, 0.24f, 1.f)};

const Palette kLight = {
    ImVec4(0.933f, 0.945f, 0.961f, 1.f), ImVec4(1.0f, 1.0f, 1.0f, 1.f),
    ImVec4(0.902f, 0.918f, 0.941f, 1.f), ImVec4(0.859f, 0.882f, 0.914f, 1.f),
    ImVec4(0.812f, 0.843f, 0.886f, 1.f), ImVec4(0.122f, 0.141f, 0.188f, 1.f),
    ImVec4(0.42f, 0.447f, 0.50f, 1.f),   ImVec4(0.184f, 0.486f, 0.965f, 1.f),
    ImVec4(0.122f, 0.353f, 0.769f, 1.f), ImVec4(0.835f, 0.859f, 0.894f, 1.f)};

void apply_theme(bool dark) {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 14.f;
    s.ChildRounding = 12.f;
    s.FrameRounding = 8.f;
    s.GrabRounding = 8.f;
    s.PopupRounding = 8.f;
    s.ScrollbarRounding = 12.f;
    s.WindowPadding = ImVec2(16, 16);
    s.FramePadding = ImVec2(12, 6);
    s.ItemSpacing = ImVec2(10, 10);
    s.ItemInnerSpacing = ImVec2(8, 6);
    s.CellPadding = ImVec2(8, 6);
    s.ScrollbarSize = 12.f;
    s.GrabMinSize = 12.f;
    s.WindowBorderSize = 0.f;
    s.ChildBorderSize = 1.f;
    s.FrameBorderSize = 0.f;
    s.PopupBorderSize = 1.f;

    const Palette& p = dark ? kDark : kLight;
    g_accent = p.accent;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = p.text;
    c[ImGuiCol_TextDisabled] = p.textdim;
    c[ImGuiCol_WindowBg] = p.bg;
    c[ImGuiCol_ChildBg] = p.card;
    c[ImGuiCol_PopupBg] = p.card;
    c[ImGuiCol_Border] = p.border;
    c[ImGuiCol_FrameBg] = p.frame;
    c[ImGuiCol_FrameBgHovered] = p.frame_h;
    c[ImGuiCol_FrameBgActive] = p.frame_a;
    c[ImGuiCol_TitleBg] = p.bg;
    c[ImGuiCol_TitleBgActive] = p.bg;
    c[ImGuiCol_MenuBarBg] = p.bg;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = p.frame_a;
    c[ImGuiCol_ScrollbarGrabHovered] = p.frame_h;
    c[ImGuiCol_ScrollbarGrabActive] = p.accent;
    c[ImGuiCol_CheckMark] = p.accent;
    c[ImGuiCol_SliderGrab] = p.accent;
    c[ImGuiCol_SliderGrabActive] = p.accent;
    c[ImGuiCol_Button] = p.frame;
    c[ImGuiCol_ButtonHovered] = p.frame_h;
    c[ImGuiCol_ButtonActive] = p.accent_d;
    c[ImGuiCol_Header] = p.accent_d;
    c[ImGuiCol_HeaderHovered] = p.frame_h;
    c[ImGuiCol_HeaderActive] = p.accent_d;
    c[ImGuiCol_Separator] = p.border;
    c[ImGuiCol_SeparatorHovered] = p.accent_d;
    c[ImGuiCol_SeparatorActive] = p.accent;
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
}

void begin_card(const char* id, unsigned icon, const char* title, bool fill = false) {
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
    ImGuiChildFlags flags = ImGuiChildFlags_Borders;
    if (!fill) flags |= ImGuiChildFlags_AutoResizeY;
    ImGui::BeginChild(id, ImVec2(0, 0), flags);
    if (icon && g_has_icons) {
        ImGui::TextColored(g_accent, "%s", u8(icon).c_str());
        ImGui::SameLine();
    }
    if (g_font_bold) ImGui::PushFont(g_font_bold, 0.0f);
    ImGui::TextColored(g_accent, "%s", title);
    if (g_font_bold) ImGui::PopFont();
}

void end_card() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}

void draw_device_panel(const GuiHooks& hooks, std::vector<CameraInfo>* cams) {
    if (ImGui::Button(label_icon(0xF021, "Scan for adb devices").c_str())) {
        g_devices.clear();
        for (auto& d : hooks.scan_devices()) {
            char b[256];
            snprintf(b, sizeof(b), "%s  [%s]", d.serial.c_str(), d.state.c_str());
            g_devices.emplace_back(b);
        }
        if (g_devices.empty()) g_devices.emplace_back("(no devices found)");
        g_device_index = 0;
    }
    ImGui::SameLine();
    if (g_devices.empty()) {
        ImGui::TextDisabled("no devices scanned yet");
    } else {
        std::vector<const char*> device_ptrs;
        for (auto& s : g_devices) device_ptrs.push_back(s.c_str());
        g_device_index = std::clamp(g_device_index, 0, (int)device_ptrs.size() - 1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##devices", &g_device_index, device_ptrs.data(),
                         (int)device_ptrs.size())) {
            g_serial_typed = false;
        }
    }

    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##serial", "adb serial", g_serial, sizeof(g_serial)))
        g_serial_typed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Typing here overrides the dropdown above");

    const std::string connect_label = label_icon(0xF0C1, "Connect host");
    const float connect_w =
        ImGui::CalcTextSize(connect_label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.f;
    ImGui::SetNextItemWidth(-(connect_w + ImGui::GetStyle().ItemSpacing.x));
    ImGui::InputTextWithHint("##host", "host:port", g_host, sizeof(g_host));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("ip:port to dial over the network, or an existing adb serial");
    ImGui::SameLine();
    if (ImGui::Button(connect_label.c_str())) {
        g_host[strcspn(g_host, " \r\n")] = '\0';
        if (g_host[0]) {
            bool is_addr = std::strchr(g_host, ':') != nullptr;
            if (!is_addr || hooks.connect_host(g_host)) {
                snprintf(g_serial, sizeof(g_serial), "%s", g_host);
                g_serial_typed = true;
                g_status = is_addr ? "connected host" : "using adb serial";
            } else {
                g_status = "could not connect to host";
            }
        }
    }

    std::string serial = chosen_serial();
    ImGui::TextDisabled("Target:");
    ImGui::SameLine();
    ImGui::Text("%s", serial.empty() ? "(none)" : serial.c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(g_connected || serial.empty());
    if (ImGui::Button(label_icon(0xF0C1, "Connect").c_str())) {
        if (hooks.query_caps(serial)) {
            g_camera_index = g_size_index = g_fps_index = 0;
            rebuild_sizes(*cams);
            if (!cams->empty()) seed_controls((*cams)[g_camera_index]);
            g_status = "capabilities loaded";
        } else {
            g_status = "capability query failed";
        }
    }
    ImGui::EndDisabled();

    if (!g_status.empty())
        ImGui::TextColored(ImVec4(1.f, 0.75f, 0.35f, 1.f), "%s", g_status.c_str());
}

static std::string camera_label(const CameraInfo& c);

void draw_stream_panel(const GuiHooks& hooks, Session* session,
                       const std::vector<CameraInfo>& cams) {
    ImGui::BeginDisabled(g_connected || cams.empty());

    static std::vector<std::string> g_camera_label_store;
    static std::vector<const char*> g_camera_labels;
    g_camera_label_store.clear();
    g_camera_label_store.reserve(cams.size());
    for (auto& c : cams) g_camera_label_store.push_back(camera_label(c));
    g_camera_labels.clear();
    for (auto& s : g_camera_label_store) g_camera_labels.push_back(s.c_str());

    float cam_w = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Camera").x
                  - ImGui::GetStyle().ItemInnerSpacing.x;
    ImGui::SetNextItemWidth(std::max(80.f, cam_w));
    if (ImGui::Combo("Camera", &g_camera_index, g_camera_labels.data(),
                     (int)g_camera_labels.size())) {
        rebuild_sizes(cams);
        if (!cams.empty()) seed_controls(cams[g_camera_index]);
    }

    std::vector<const char*> size_ptrs;
    for (auto& s : g_sizes) size_ptrs.push_back(s.c_str());

    static std::vector<std::string> fps_labels;
    fps_labels.clear();
    fps_labels.reserve(g_fps_values.size());
    for (int f : g_fps_values) fps_labels.push_back(std::to_string(f));
    std::vector<const char*> fps_ptrs;
    for (auto& l : fps_labels) fps_ptrs.push_back(l.c_str());

    static const char* kBitrates[] = {"1 Mbps", "2 Mbps", "4 Mbps",
                                      "8 Mbps", "16 Mbps", "32 Mbps"};

    if (ImGui::BeginTable("##format", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("Resolution");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("FPS");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextDisabled("Bitrate");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (!g_sizes.empty())
            ImGui::Combo("##res", &g_size_index, size_ptrs.data(), (int)size_ptrs.size());
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (!g_fps_values.empty())
            ImGui::Combo("##fps", &g_fps_index, fps_ptrs.data(), (int)fps_ptrs.size());
        ImGui::TableSetColumnIndex(2);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::Combo("##br", &g_bitrate_preset, kBitrates, IM_ARRAYSIZE(kBitrates));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(g_connected ? "Encoder bitrate is set when the server starts.\n"
                                            "Restart the stream to apply a change."
                                          : "Encoder bitrate passed to the scrcpy server.");
        ImGui::EndTable();
    }
    if (g_connected) ImGui::TextDisabled("restart the stream to apply a new bitrate");
    ImGui::EndDisabled();

    std::string serial = chosen_serial();
    if (!g_connected && !serial.empty() && !g_sizes.empty()) {
        if (ImGui::Button(label_icon(0xF04B, "Start stream").c_str(), ImVec2(-FLT_MIN, 0))) {
            char size[32];
            snprintf(size, sizeof(size), "%dx%d", g_size_w[g_size_index], g_size_h[g_size_index]);
            int fps = g_fps_values.empty() ? 0 : g_fps_values[g_fps_index];
            int bitrate = BITRATE_PRESETS[g_bitrate_preset];
            bool ok = hooks.start_stream(serial, cams[g_camera_index].id, size, fps, bitrate);
            g_status = ok ? "stream started" : "stream failed to start (see console)";
        }
    }

    if (g_connected) {
        if (ImGui::Button(label_icon(0xF04D, "Stop stream").c_str(), ImVec2(-FLT_MIN, 0))) session->stop();
    }
}

void draw_controls(Session* session, const std::vector<CameraInfo>& cams) {
    bool live = g_connected && session->frames_flowing.load();
    ImGui::BeginDisabled(!live);

    const CameraInfo* cam = cams.empty() ? nullptr : &cams[std::clamp(g_camera_index, 0, (int)cams.size() - 1)];
    const float focus_min = cam && cam->has_focus ? cam->focus_min : 0.f;
    const float focus_max = std::max(focus_min, 1.f);
    const float iso_min = cam && cam->has_iso ? (float)cam->iso_min : 50.f;
    const float iso_max = cam && cam->has_iso ? (float)cam->iso_max : 6400.f;
    const float exp_min_ms = cam && cam->has_exp ? (float)cam->exp_min_ns / 1e6f : 0.1f;
    const float exp_max_ms = cam && cam->has_exp ? (float)cam->exp_max_ns / 1e6f : 250.f;

    auto icon_cell = [](unsigned cp) {
        ImGui::TableSetColumnIndex(0);
        if (g_has_icons) {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.f);
            ImGui::TextUnformatted(u8(cp).c_str());
        }
        ImGui::TableSetColumnIndex(1);
    };

    if (ImGui::BeginTable("##ctrls", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("##icon", ImGuiTableColumnFlags_WidthFixed,
                                g_has_icons ? 24.f : 0.f);
        ImGui::TableSetupColumn("##ctrl", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        icon_cell(0xF05B);
        ImGui::SetNextItemWidth(300.f);
        ImGui::SliderFloat("Focus (0 = infinity)", &g_focus, 0.f, focus_max, "%.2f D");
        ImGui::SameLine();
        if (ImGui::Button("Apply"))
            session->send_param(PARAM_FOCUS_DISTANCE, (int32_t)(g_focus * 1000));
        ImGui::SameLine();
        if (ImGui::Button("Far")) {
            g_focus = 0.f;
            session->send_param(PARAM_FOCUS_DISTANCE, 0);
        }
        ImGui::SameLine();
        if (ImGui::Button("Nearest")) {
            g_focus = focus_max;
            session->send_param(PARAM_FOCUS_DISTANCE, (int32_t)(focus_max * 1000));
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Closest the camera can focus");

        ImGui::TableNextRow();
        icon_cell(0xF042);
        ImGui::SetNextItemWidth(300.f);
        ImGui::SliderFloat("Sensitivity (ISO)", &g_iso, iso_min, iso_max, "%.0f");
        ImGui::SameLine();
        if (ImGui::Button("Apply##iso")) session->send_param(PARAM_SENSITIVITY, (int32_t)g_iso);

        ImGui::TableNextRow();
        icon_cell(0xF185);
        ImGui::SetNextItemWidth(300.f);
        ImGui::SliderFloat("Exposure time (ms)", &g_exposure_ms, exp_min_ms, exp_max_ms,
                           "%.3f ms");
        ImGui::SameLine();
        if (ImGui::Button("Apply##exp"))
            session->send_param(PARAM_EXPOSURE_TIME, (int32_t)(g_exposure_ms * 1000000.f));

        ImGui::TableNextRow();
        icon_cell(0xF030);
        ImGui::SetNextItemWidth(200.f);
        ImGui::Combo("AF mode", &g_af_mode, "off\0continuous\0single-shot\0\0");
        ImGui::SameLine();
        if (ImGui::Button("Apply##af"))
            session->send_param(PARAM_AF_MODE, g_af_mode_values[g_af_mode]);
        ImGui::SameLine();
        if (ImGui::Button("Trigger AF")) session->send_param(PARAM_AF_TRIGGER, 0);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Runs one autofocus pass (needed for single-shot mode)");

        ImGui::TableNextRow();
        icon_cell(0xF185);
        ImGui::SetNextItemWidth(200.f);
        ImGui::Combo("AE mode", &g_ae_mode, "off\0auto\0\0");
        ImGui::SameLine();
        if (ImGui::Button("Apply##ae")) session->send_param(PARAM_AE_MODE, g_ae_mode);
        ImGui::SameLine();
        if (ImGui::Button("Zoom +")) session->zoom_in();
        ImGui::SameLine();
        if (ImGui::Button("Zoom -")) session->zoom_out();
        ImGui::SameLine();
        if (ImGui::Checkbox(label_icon(0xF0EB, "Torch").c_str(), &g_torch)) session->set_torch(g_torch);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(label_icon(0xF011, "Quit").c_str())) g_quit_requested = true;

        ImGui::EndTable();
    } else {
        ImGui::EndDisabled();
    }
}

static std::string camera_label(const CameraInfo& c) {
    char b[192];
    const char* lens = "standard";
    if (c.focal_mm >= 8.f) {
        lens = "tele";
    } else if (c.focal_mm <= 2.f) {
        lens = "ultrawide";
    } else if (c.focal_mm <= 6.f) {
        lens = "wide";
    }

    char extra[48] = "";
    if (c.has_focus && c.focus_min > 0.f) {
        float min_cm = 100.f / c.focus_min;
        if (min_cm <= 5.f) {
            snprintf(extra, sizeof(extra), "  [macro %.1fcm]", min_cm);
        }
    }

    snprintf(b, sizeof(b), "%s  %s  %s  %.1fmm  %dx%d%s", c.id.c_str(), c.facing.c_str(),
             lens, c.focal_mm, c.sensor_w, c.sensor_h, extra);
    return b;
}

void draw_output_panel(Session* session) {
    if (!session->output_ok()) {
        ImGui::TextColored(ImVec4(1.f, 0.6f, 0.3f, 1.f),
#ifdef _WIN32
                           "VCam SDK not available - no virtual camera will be created");
#else
                           "no output yet - starts when you begin streaming");
#endif
    }

    bool flip_x = session->flip_x;
    if (ImGui::Checkbox("Flip X (mirror)", &flip_x)) session->set_flip_x(flip_x);
    ImGui::SameLine();
    bool flip_y = session->flip_y;
    if (ImGui::Checkbox("Flip Y (flip)", &flip_y)) session->set_flip_y(flip_y);
    ImGui::SameLine();
    bool rotate = session->rotate;
    if (ImGui::Checkbox("Rotate 90 CW", &rotate)) session->set_rotate(rotate);
}

void draw_theme_bar() {
    std::string label = label_icon(g_dark ? 0xF185 : 0xF186,
                                   g_dark ? "Switch to light" : "Switch to dark");
    float w = ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                         std::max(0.f, ImGui::GetContentRegionAvail().x - w));
    if (ImGui::Button(label.c_str())) {
        g_dark = !g_dark;
        apply_theme(g_dark);
    }
}

void draw_preview_feed(Session* session) {
    if (!g_show_preview) {
        ImGui::TextDisabled("preview hidden");
        return;
    }

    {
        std::lock_guard<std::mutex> lk(session->preview_mutex);
        if (session->preview_seq != g_preview_seq && session->preview_w > 0) {
            g_preview_seq = session->preview_seq;
            g_preview_w = session->preview_w;
            g_preview_h = session->preview_h;
            if (!g_preview_tex) glGenTextures(1, &g_preview_tex);
            glBindTexture(GL_TEXTURE_2D, g_preview_tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, g_preview_w, g_preview_h, 0, GL_BGR,
                         GL_UNSIGNED_BYTE, session->preview_rgb.data());
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }

    if (!g_preview_tex || g_preview_w <= 0) {
        ImGui::TextDisabled("waiting for frames");
        return;
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x <= 1.f || avail.y <= 1.f) return;

    bool rot = session->rotate;
    float disp_w = rot ? (float)g_preview_h : (float)g_preview_w;
    float disp_h = rot ? (float)g_preview_w : (float)g_preview_h;
    float scale = std::min(avail.x / disp_w, avail.y / disp_h);
    float draw_w = disp_w * scale;
    float draw_h = disp_h * scale;

    ImVec2 p = ImGui::GetCursorScreenPos();
    p.x += (avail.x - draw_w) * 0.5f;
    p.y += (avail.y - draw_h) * 0.5f;
    ImVec2 pmin = p;
    ImVec2 pmax = ImVec2(p.x + draw_w, p.y + draw_h);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rounding = 10.f;
    if (!rot) {
        ImVec2 uv0(session->flip_x ? 1.f : 0.f, session->flip_y ? 1.f : 0.f);
        ImVec2 uv1(session->flip_x ? 0.f : 1.f, session->flip_y ? 0.f : 1.f);
        dl->AddImageRounded((ImTextureID)g_preview_tex, pmin, pmax, uv0, uv1, IM_COL32_WHITE,
                            rounding);
    } else {
        ImVec2 tl(0, 0), tr(1, 0), br(1, 1), bl(0, 1);
        if (session->flip_x) {
            std::swap(tl.x, tr.x);
            std::swap(bl.x, br.x);
        }
        if (session->flip_y) {
            std::swap(tl.y, bl.y);
            std::swap(tr.y, br.y);
        }
        ImVec2 ntl = bl, ntr = tl, nbr = tr, nbl = br;
        dl->AddImageQuad((ImTextureID)g_preview_tex, pmin, ImVec2(pmax.x, pmin.y), pmax,
                         ImVec2(pmin.x, pmax.y), ntl, ntr, nbr, nbl);
    }
    dl->AddRect(pmin, pmax, IM_COL32(255, 255, 255, 18), rounding, 0, 1.f);
    ImGui::Dummy(avail);
}

void draw_preview_bar(Session* session) {
    ImGui::Checkbox("Show preview", &g_show_preview);
    session->preview_enabled = g_show_preview;
    ImGui::SameLine();
    if (session->frames_flowing.load()) {
        ImGui::TextColored(g_accent, "%dx%d @ %d fps", session->stream_w.load(),
                           session->stream_h.load(), session->measured_fps.load());
    } else {
        ImGui::TextDisabled("waiting for frames");
    }
}

}

void run_gui(Session* session, std::vector<CameraInfo>* cams, const GuiHooks& hooks) {
    if (!create_window()) {
        fprintf(stderr, "Could not create the window\n");
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    load_fonts(io);
    apply_theme(g_dark);
    ImGui_ImplSDL3_InitForOpenGL(g_window, g_gl);
    ImGui_ImplOpenGL3_Init("#version 150");

    bool quit = false;
    while (!quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL3_ProcessEvent(&ev);
            if (ev.type == SDL_EVENT_QUIT) quit = true;
            if (ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) quit = true;
        }
        if (quit || g_quit_requested) break;

        g_connected = session->connected.load();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("scrcpy camera client", &quit,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        float total_h = ImGui::GetContentRegionAvail().y;
        float controls_h = std::clamp(total_h * 0.34f, 220.f, 320.f);
        float top_h = std::max(140.f, total_h - controls_h - ImGui::GetStyle().ItemSpacing.y);

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::BeginChild("toprow", ImVec2(0, top_h), ImGuiChildFlags_None);
        ImGui::PopStyleColor();

        float left_w = std::clamp(ImGui::GetContentRegionAvail().x * 0.47f, 360.f, 600.f);

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::BeginChild("leftcol", ImVec2(left_w, 0), ImGuiChildFlags_None);
        ImGui::PopStyleColor();

        begin_card("##device", 0xF1EB, "Device");
        draw_device_panel(hooks, cams);
        end_card();

        begin_card("##stream", 0xF03D, "Stream");
        draw_stream_panel(hooks, session, *cams);
        end_card();

        begin_card("##output", 0xF390, "Virtual camera output");
        draw_output_panel(session);
        end_card();

        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::BeginChild("rightcol", ImVec2(0, 0), ImGuiChildFlags_None);
        ImGui::PopStyleColor();

        draw_theme_bar();

        float feed_h = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing();
        if (feed_h < 60.f) feed_h = 60.f;

        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
        ImGui::BeginChild("##feed", ImVec2(0, feed_h), ImGuiChildFlags_Borders);
        ImGui::PopStyleVar(2);
        draw_preview_feed(session);
        ImGui::EndChild();

        draw_preview_bar(session);

        ImGui::EndChild();
        ImGui::EndChild();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::BeginChild("controlsrow", ImVec2(0, controls_h), ImGuiChildFlags_None);
        ImGui::PopStyleColor();

        begin_card("##controls", 0xF1DE, "Manual Camera Controls", true);
        draw_controls(session, *cams);
        end_card();

        ImGui::EndChild();

        ImGui::End();
        ImGui::Render();

        int dw, dh;
        SDL_GetWindowSizeInPixels(g_window, &dw, &dh);
        glViewport(0, 0, dw, dh);
        const ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        glClearColor(bg.x, bg.y, bg.z, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(g_window);
    }

    session->stop();
    if (g_preview_tex) {
        glDeleteTextures(1, &g_preview_tex);
        g_preview_tex = 0;
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    destroy_window();
}
