// Overlay de FPS + menu de toque para jogos Android (OpenGL ES).
// Faz hook em eglSwapBuffers (desenho) e AInputQueue_getEvent (toque).
#include <android/input.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <chrono>
#include <mutex>
#include <algorithm>
#include <vector>
#include <cstdio>

#include "dobby.h"
#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "FpsMod", __VA_ARGS__)

// ---------------- FPS ----------------
struct Fps {
    using clk = std::chrono::steady_clock;
    clk::time_point last = clk::now();
    float ms[120] = {};
    int idx = 0, n = 0, frames = 0;
    float accum = 0, fps = 0, low1 = 0;

    void Tick() {
        auto now = clk::now();
        float dt = std::chrono::duration<float, std::milli>(now - last).count();
        last = now;
        ms[idx] = dt; idx = (idx + 1) % 120; if (n < 120) n++;
        accum += dt; frames++;
        if (accum >= 500.f) {
            fps = frames * 1000.f / accum; accum = 0; frames = 0;
            std::vector<float> v(ms, ms + n);
            std::sort(v.begin(), v.end(), std::greater<float>());
            size_t k = std::max<size_t>(1, v.size() / 100 + 1);
            float s = 0; for (size_t i = 0; i < k; i++) s += v[i];
            low1 = (s > 0) ? 1000.f * k / s : 0;
        }
    }
    float Last() const { return n ? ms[(idx + 119) % 120] : 0; }
} gFps;

// ---------------- Toque ----------------
static std::mutex gTouchMx;
static float gTx = 0, gTy = 0;
static bool  gDown = false, gDirty = false;

typedef int32_t (*GetEvent_t)(AInputQueue*, AInputEvent**);
static GetEvent_t oGetEvent = nullptr;

static int32_t hkGetEvent(AInputQueue* q, AInputEvent** ev) {
    int32_t r = oGetEvent(q, ev);
    if (r >= 0 && ev && *ev && AInputEvent_getType(*ev) == AINPUT_EVENT_TYPE_MOTION) {
        int act = AMotionEvent_getAction(*ev) & AMOTION_EVENT_ACTION_MASK;
        std::lock_guard<std::mutex> lk(gTouchMx);
        gTx = AMotionEvent_getX(*ev, 0);
        gTy = AMotionEvent_getY(*ev, 0);
        if (act == AMOTION_EVENT_ACTION_DOWN) gDown = true;
        if (act == AMOTION_EVENT_ACTION_UP || act == AMOTION_EVENT_ACTION_CANCEL) gDown = false;
        gDirty = true;
    }
    return r;
}

// ---------------- Render ----------------
typedef EGLBoolean (*Swap_t)(EGLDisplay, EGLSurface);
static Swap_t oSwap = nullptr;
static bool gInit = false, gMenu = false;

static void DrawUI(float w) {
    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowBgAlpha(0.35f);
    ImGui::Begin("##fps", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImVec4 col = gFps.fps >= 55 ? ImVec4(.3f,1,.3f,1) : gFps.fps >= 30 ? ImVec4(1,.85f,.2f,1) : ImVec4(1,.3f,.3f,1);
    ImGui::TextColored(col, "FPS %.0f", gFps.fps);
    if (ImGui::SmallButton(gMenu ? "fechar" : "menu")) gMenu = !gMenu;
    ImGui::End();

    if (!gMenu) return;
    ImGui::SetNextWindowPos(ImVec2(w * 0.25f, 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(w * 0.5f, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Mod Menu");
    ImGui::Text("FPS:        %.1f", gFps.fps);
    ImGui::Text("Frame time: %.2f ms", gFps.Last());
    ImGui::Text("1%% low:     %.1f", gFps.low1);
    ImGui::PlotLines("##ft", gFps.ms, 120, gFps.idx, "ms/frame", 0.f, 50.f, ImVec2(0, 80));
    // Adicione novas funções aqui
    ImGui::End();
}

static EGLBoolean hkSwap(EGLDisplay dpy, EGLSurface surf) {
    EGLint w = 0, h = 0;
    eglQuerySurface(dpy, surf, EGL_WIDTH, &w);
    eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);
    if (w > 0 && h > 0) {
        if (!gInit) {
            ImGui::CreateContext();
            ImGui::GetIO().IniFilename = nullptr;
            ImGui::StyleColorsDark();
            ImGui_ImplOpenGL3_Init("#version 300 es");
            ImGui::GetIO().FontGlobalScale = 2.5f;   // telas de celular são densas
            gInit = true;
        }
        gFps.Tick();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)w, (float)h);
        io.DeltaTime = std::max(gFps.Last() / 1000.f, 1.f / 1000.f);
        {
            std::lock_guard<std::mutex> lk(gTouchMx);
            if (gDirty) {
                io.AddMousePosEvent(gTx, gTy);
                io.AddMouseButtonEvent(0, gDown);
                gDirty = false;
            }
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        DrawUI((float)w);
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
    return oSwap(dpy, surf);
}

// ---------------- Inicialização ----------------
static void* InitThread(void*) {
    sleep(3); // espera o jogo carregar o EGL
    void* egl = dlopen("libEGL.so", RTLD_NOW);
    void* swap = egl ? dlsym(egl, "eglSwapBuffers") : nullptr;
    if (swap) { DobbyHook(swap, (void*)hkSwap, (void**)&oSwap); LOG("hook eglSwapBuffers ok"); }
    else LOG("eglSwapBuffers nao encontrado");

    void* andr = dlopen("libandroid.so", RTLD_NOW);
    void* get = andr ? dlsym(andr, "AInputQueue_getEvent") : nullptr;
    if (get) { DobbyHook(get, (void*)hkGetEvent, (void**)&oGetEvent); LOG("hook input ok"); }
    return nullptr;
}

__attribute__((constructor)) static void OnLoad() {
    pthread_t t; pthread_create(&t, nullptr, InitThread, nullptr);
}
