#include "gui.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>

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
bool g_high_speed = false;
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

void rebuild_fps(const std::vector<CameraInfo>& cams) {
    if (cams.empty()) { g_fps_values.clear(); return; }
    const CameraInfo& c = cams[std::clamp(g_camera_index, 0, (int)cams.size() - 1)];
    const std::vector<CameraSize>& src =
        (g_high_speed && !c.high_speed_sizes.empty()) ? c.high_speed_sizes : c.sizes;
    g_fps_values.clear();
    if (g_high_speed && !src.empty() && !src[g_size_index].fps.empty()) {
        g_fps_values = src[g_size_index].fps;
    } else {
        g_fps_values = c.fps;
    }
    g_fps_index = 0;
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
    const std::vector<CameraSize>& src =
        (g_high_speed && !c.high_speed_sizes.empty()) ? c.high_speed_sizes : c.sizes;
    for (const CameraSize& s : src) {
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

    g_window = SDL_CreateWindow("scrcpy camera client", 820, 700, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
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

#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "imgui.h"

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl3.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

void draw_device_panel(const GuiHooks& hooks, std::vector<CameraInfo>* cams) {
    ImGui::SeparatorText("Device");

    ImGui::SetNextItemWidth(330.f);
    if (ImGui::Button("Scan for adb devices")) {
        g_devices.clear();
        for (auto& d : hooks.scan_devices()) {
            char b[256];
            snprintf(b, sizeof(b), "%s  [%s]", d.serial.c_str(), d.state.c_str());
            g_devices.emplace_back(b);
        }
        if (g_devices.empty()) g_devices.emplace_back("(no devices found)");
        g_device_index = 0;
    }

    if (g_devices.empty()) {
        ImGui::TextDisabled("(press Scan for adb devices)");
    } else {
        std::vector<const char*> device_ptrs;
        for (auto& s : g_devices) device_ptrs.push_back(s.c_str());
        g_device_index = std::clamp(g_device_index, 0, (int)device_ptrs.size() - 1);
        ImGui::SetNextItemWidth(330.f);
        if (ImGui::Combo("Detected devices", &g_device_index, device_ptrs.data(),
                         (int)device_ptrs.size())) {
            g_serial_typed = false;
        }
    }

    ImGui::SeparatorText("Manual connection");
    ImGui::SetNextItemWidth(330.f);
    if (ImGui::InputText("adb serial", g_serial, sizeof(g_serial))) g_serial_typed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Typing here overrides the dropdown above");

    ImGui::SetNextItemWidth(220.f);
    ImGui::InputText("host:port", g_host, sizeof(g_host));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("ip:port to dial over the network, or an existing adb serial");
    ImGui::SameLine();
    if (ImGui::Button("Connect host")) {
        g_host[strcspn(g_host, " \r\n")] = '\0';
        // No colon means this is a serial adb already knows, not an address to
        // dial: adb connect would reject it, and would fail silently here.
        if (g_host[0] && (std::strchr(g_host, ':') || hooks.connect_host(g_host))) {
            snprintf(g_serial, sizeof(g_serial), "%s", g_host);
            g_serial_typed = true;
        }
    }

    std::string serial = chosen_serial();
    ImGui::Text("Target: %s", serial.empty() ? "(none)" : serial.c_str());

ImGui::BeginDisabled(g_connected);
if (ImGui::Button("Connect")) {
        if (!serial.empty()) {
            if (hooks.query_caps(serial)) {
                g_camera_index = g_size_index = g_fps_index = 0;
                rebuild_sizes(*cams);
                g_status = "capabilities loaded";
            } else {
                g_status = "capability query failed";
            }
        } else {
            g_status = "pick or type a device first";
        }
    }
    ImGui::EndDisabled();

    if (!g_status.empty()) ImGui::TextColored(ImVec4(1.f, 0.8f, 0.3f, 1.f), "%s", g_status.c_str());
}

static std::string camera_label(const CameraInfo& c);

void draw_stream_panel(const GuiHooks& hooks, Session* session,
                       const std::vector<CameraInfo>& cams) {
    ImGui::SeparatorText("Stream");
    ImGui::BeginDisabled(g_connected || cams.empty());

    static std::vector<std::string> g_camera_label_store;
    static std::vector<const char*> g_camera_labels;
    g_camera_label_store.clear();
    g_camera_label_store.reserve(cams.size());
    for (auto& c : cams) g_camera_label_store.push_back(camera_label(c));
    g_camera_labels.clear();
    for (auto& s : g_camera_label_store) g_camera_labels.push_back(s.c_str());

    ImGui::SetNextItemWidth(300.f);
    if (ImGui::Combo("Camera", &g_camera_index, g_camera_labels.data(),
                     (int)g_camera_labels.size())) {
        rebuild_sizes(cams);
    }

    ImGui::SameLine();
    bool prev_hs = g_high_speed;
    ImGui::Checkbox("High speed", &g_high_speed);
    if (g_high_speed != prev_hs) rebuild_sizes(cams);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Burst capture; restricted sizes/fps");

    std::vector<const char*> size_ptrs;
    for (auto& s : g_sizes) size_ptrs.push_back(s.c_str());
    ImGui::SetNextItemWidth(200.f);
    if (!g_sizes.empty() &&
        ImGui::Combo("Resolution", &g_size_index, size_ptrs.data(), (int)size_ptrs.size())) {
        if (g_high_speed) rebuild_fps(cams);
    }

    ImGui::SameLine();
    static std::vector<std::string> fps_labels;
    fps_labels.clear();
    fps_labels.reserve(g_fps_values.size());
    for (int f : g_fps_values) fps_labels.push_back(std::to_string(f));
    std::vector<const char*> fps_ptrs;
    for (auto& l : fps_labels) fps_ptrs.push_back(l.c_str());
    ImGui::SetNextItemWidth(90.f);
    if (!g_fps_values.empty())
        ImGui::Combo("FPS", &g_fps_index, fps_ptrs.data(), (int)fps_ptrs.size());

static const char* kBitrates[] = {"1 Mbps", "2 Mbps", "4 Mbps",
                                      "8 Mbps", "16 Mbps", "32 Mbps"};
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.f);
    ImGui::Combo("Bitrate##br", &g_bitrate_preset, kBitrates, IM_ARRAYSIZE(kBitrates));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(g_connected ? "Encoder bitrate is set when the server starts.\n"
                                        "Restart the stream to apply a change."
                                      : "Encoder bitrate passed to the scrcpy server.");
    if (g_connected) ImGui::TextDisabled("  (restart the stream to apply a new bitrate)");
    ImGui::EndDisabled();

    std::string serial = chosen_serial();
    if (!g_connected && !serial.empty() && !g_sizes.empty()) {
        if (ImGui::Button("Start stream")) {
            char size[32];
            snprintf(size, sizeof(size), "%dx%d", g_size_w[g_size_index], g_size_h[g_size_index]);
            int fps = g_fps_values.empty() ? 0 : g_fps_values[g_fps_index];
            int bitrate = BITRATE_PRESETS[g_bitrate_preset];
            hooks.start_stream(serial, cams[g_camera_index].id, size, fps, g_high_speed,
                               bitrate);
        }
    }

    if (g_connected) {
        ImGui::SameLine();
        if (ImGui::Button("Stop stream")) session->stop();
        ImGui::Text("%dx%d @ %d fps%s", session->stream_w.load(), session->stream_h.load(),
                    session->measured_fps.load(),
                    session->frames_flowing.load() ? "" : "  (waiting for frames)");
    }
}

void draw_controls(Session* session, const std::vector<CameraInfo>& cams) {
    ImGui::SeparatorText("Manual camera controls");
    bool live = g_connected && session->frames_flowing.load();
    ImGui::BeginDisabled(!live);
    ImGui::TextDisabled("Sliders use the ranges the device reports. Setting focus forces AF off; "
                        "exposure/ISO force AE off.");

    const CameraInfo* cam = cams.empty() ? nullptr : &cams[std::clamp(g_camera_index, 0, (int)cams.size() - 1)];
    const float focus_min = cam && cam->has_focus ? cam->focus_min : 0.f;
    const float focus_max = std::max(focus_min, 1.f);
    const float iso_min = cam && cam->has_iso ? (float)cam->iso_min : 50.f;
    const float iso_max = cam && cam->has_iso ? (float)cam->iso_max : 6400.f;
    const float exp_min_ms = cam && cam->has_exp ? (float)cam->exp_min_ns / 1e6f : 0.1f;
    const float exp_max_ms = cam && cam->has_exp ? (float)cam->exp_max_ns / 1e6f : 250.f;

    ImGui::SetNextItemWidth(300.f);
    ImGui::SliderFloat("Focus (0 = infinity)", &g_focus, 0.f, focus_max, "%.2f D");
    ImGui::SameLine();
    if (ImGui::Button("Apply")) session->send_param(PARAM_FOCUS_DISTANCE, (int32_t)(g_focus * 1000));
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

    ImGui::SetNextItemWidth(300.f);
    ImGui::SliderFloat("Sensitivity (ISO)", &g_iso, iso_min, iso_max, "%.0f");
    ImGui::SameLine();
    if (ImGui::Button("Apply##iso")) session->send_param(PARAM_SENSITIVITY, (int32_t)g_iso);

    ImGui::SetNextItemWidth(300.f);
    ImGui::SliderFloat("Exposure time (ms)", &g_exposure_ms, exp_min_ms, exp_max_ms, "%.3f ms");
    ImGui::SameLine();
    if (ImGui::Button("Apply##exp"))
        session->send_param(PARAM_EXPOSURE_TIME, (int32_t)(g_exposure_ms * 1000000.f));

    ImGui::SetNextItemWidth(200.f);
    ImGui::Combo("AF mode", &g_af_mode, "off\0continuous\0single-shot\0\0");
    ImGui::SameLine();
    if (ImGui::Button("Apply##af")) session->send_param(PARAM_AF_MODE, g_af_mode_values[g_af_mode]);
    ImGui::SameLine();
    if (ImGui::Button("Trigger AF")) session->send_param(PARAM_AF_TRIGGER, 0);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Runs one autofocus pass (needed for single-shot mode)");

    ImGui::SetNextItemWidth(200.f);
    ImGui::Combo("AE mode", &g_ae_mode, "off\0auto\0\0");
    ImGui::SameLine();
    if (ImGui::Button("Apply##ae")) session->send_param(PARAM_AE_MODE, g_ae_mode);

    ImGui::SameLine();
    if (ImGui::Button("Zoom +")) session->zoom_in();
    ImGui::SameLine();
    if (ImGui::Button("Zoom -")) session->zoom_out();
    ImGui::SameLine();
    if (ImGui::Checkbox("Torch", &g_torch)) session->set_torch(g_torch);
    ImGui::EndDisabled();
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
    ImGui::SeparatorText("Virtual camera output");
    if (!session->output_ok()) {
        ImGui::TextColored(ImVec4(1.f, 0.6f, 0.3f, 1.f),
#ifdef _WIN32
                           "VCam SDK not available - no virtual camera will be created");
#else
                           "no output yet - starts when you begin streaming");
#endif
    }

#ifdef _WIN32
    bool flip_x = session->vcam.cur_mirror != 0;
    bool flip_y = session->vcam.cur_flip != 0;
    if (ImGui::Checkbox("Flip X (mirror)", &flip_x)) session->vcam.set_mirror(flip_x ? 1 : 0);
    ImGui::SameLine();
    if (ImGui::Checkbox("Flip Y (flip)", &flip_y)) session->vcam.set_flip(flip_y ? 1 : 0);

    bool rotate = session->vcam.cur_rotate != 0;
    ImGui::SameLine();
    if (ImGui::Checkbox("Rotate 90 CW", &rotate)) session->vcam.set_rotate(rotate ? 1 : 0);
#endif

    ImGui::Text("%d x %d @ %d fps%s", session->stream_w.load(), session->stream_h.load(),
                session->measured_fps.load(), session->frames_flowing.load() ? "" : "  (stopped)");
}

}

void run_gui(Session* session, std::vector<CameraInfo>* cams, const GuiHooks& hooks) {
    if (!create_window()) {
        fprintf(stderr, "Could not create the window\n");
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
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
        if (quit) break;

        g_connected = session->connected.load();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("scrcpy camera client", &quit,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        draw_device_panel(hooks, cams);
        draw_stream_panel(hooks, session, *cams);
        draw_output_panel(session);
        draw_controls(session, *cams);

        ImGui::SameLine();
        if (ImGui::Button("Quit")) quit = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Stops the stream, closes the adb forward and exits");

        ImGui::End();
        ImGui::Render();

        int dw, dh;
        SDL_GetWindowSizeInPixels(g_window, &dw, &dh);
        glViewport(0, 0, dw, dh);
        glClearColor(0.06f, 0.06f, 0.06f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(g_window);
    }

    session->stop();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    destroy_window();
}
