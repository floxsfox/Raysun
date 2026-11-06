// Ray Sun v1.0.0 - native mod LeviLauncher (Minecraft Bedrock arm64)
// Credit: floxsfox
//
// Efek matahari: glow, sinar (god rays) realistis saat terbit/terbenam, sinar tipis saat
// siang, dan lens flare (bintik berjajar) saat menatap matahari. Digambar sebagai overlay
// oleh mod sendiri di atas shader apa pun (vanilla, Newb, MaterialBinLoader).
//
//   ClientInstance::update     -> hitung posisi matahari, arah pandang, hujan, oklusi
//   LevelRenderer::renderLevel -> gambar glow/sinar/flare lewat tessellator bawaan game
//
// Signature & offset untuk Minecraft Bedrock 1.26.60.x arm64 (data dari proyek BedrockTools,
// QYCottage; kode ditulis sendiri).

#include "Engine.hpp"
#include "RayMath.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <sys/stat.h>

#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>
#include <pl/memory/Hook.hpp>
#include <pl/memory/Signature.hpp>

namespace {

using Clock = std::chrono::steady_clock;

constexpr const char *kVersion = "1.0.0";
constexpr const char *kCredit = "floxsfox";
constexpr const char *kModuleId = "ray_sun.main";

// ---------------------------------------------------------------- signature
constexpr const char *kLib = "libminecraftpe.so";

constexpr const char *kSigClientInstanceUpdate =
    "? ? ? FC ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 ? ? ? D1 59 D0 3B D5 ? ? ? 52 F5 03 02 2A";
constexpr const char *kSigRenderLevel =
    "? ? ? D1 ? ? ? FD ? ? ? 6D ? ? ? 6D ? ? ? 6D ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 57 D0 3B D5";
constexpr const char *kSigTessBegin =
    "? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 FD 03 00 91 ? ? ? 39 ? ? ? 39 08 01 09 2A";
constexpr const char *kSigTessColor =
    "? ? ? 52 ? ? ? 39 04 01 27 1E 00 08 24 1E 21 08 24 1E 42 08 24 1E 63 08 24 1E 08 00 38 1E";
constexpr const char *kSigTessVertex =
    "? ? ? D1 ? ? ? FD ? ? ? 6D ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 5B D0 3B D5 ? ? ? F9 ? ? ? F9";
constexpr const char *kSigRenderMesh =
    "? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 FD 03 00 91 ? ? ? D1 57 D0 3B D5 F6 03 00 AA E0 03 01 AA ? ? ? F9 F4 03 03 AA";
constexpr const char *kSigMaterialGroup =
    "? ? ? ? ? ? ? 91 ? ? ? D1 ? ? ? D1 ? ? ? 91 E3 03 14 AA ? ? ? 52 ? ? ? ? ? ? ? F9 ? ? ? A9 ? ? ? B4 ? ? ? 91 ? ? ? 92 ? ? ? ? ? ? ? B5 ? ? ? F9 E0 03 14 AA ? ? ? F9 00 01 3F D6 E0 03 14 AA ? ? ? ? ? ? ? F9 ? ? ? F9 ? ? ? B4 ? ? ? 91 ? ? ? 92 ? ? ? ? ? ? ? B5 ? ? ? F9 E0 03 14 AA ? ? ? F9 00 01 3F D6 E0 03 14 AA ? ? ? ? ? ? ? 38 ? ? ? 37 ? ? ? 39 ? ? ? 37 ? ? ? F8 ? ? ? A9 ? ? ? B4 ? ? ? 91 ? ? ? 92 ? ? ? ? ? ? ? B5 ? ? ? F9 E0 03 14 AA ? ? ? F9 00 01 3F D6 E0 03 14 AA ? ? ? ? ? ? ? F8 ? ? ? F8 ? ? ? B4 ? ? ? 91 ? ? ? 92 ? ? ? ? ? ? ? B5 ? ? ? F9 E0 03 14 AA ? ? ? F9 00 01 3F D6 E0 03 14 AA ? ? ? ? ? ? ? ? ? ? ? F9 ? ? ? 38 ? ? ? 36 ? ? ? F8 ? ? ? ? ? ? ? 39 ? ? ? 36 ? ? ? F9 ? ? ? ? ? ? ? F8 ? ? ? A9 ? ? ? B5 ? ? ? F8 ? ? ? F9 ? ? ? A9 ? ? ? F9 ? ? ? F9 ? ? ? B4 ? ? ? A9 ? ? ? B4 ? ? ? 91 ? ? ? 52 ? ? ? ? ? ? ? F9 ? ? ? F9 ? ? ? B4 ? ? ? F9 ? ? ? 91 E1 03 13 AA ? ? ? 91 ? ? ? ? ? ? ? 39 ? ? ? A9 ? ? ? 72 ? ? ? D3 29 15 94 9A 08 01 8A 9A ? ? ? A9 ? ? ? D1 ? ? ? 91 ? ? ? ? ? ? ? F9 ? ? ? F9 ? ? ? F9 E8 03 00 91";
constexpr const char *kSigGetBlock =
    "? ? ? D1 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 56 D0 3B D5 ? ? ? F9 ? ? ? F9 ? ? ? B9 ? ? ? 79 1F 01 09 6B ? ? ? 54 ? ? ? 79 F3 03 00 AA 1F 01 09 6B ? ? ? 54 E0 03 00 91";
constexpr const char *kSigTime =
    "? ? ? D1 ? ? ? A9 ? ? ? F9 ? ? ? A9 ? ? ? 91 55 D0 3B D5 ? ? ? F9 ? ? ? F8 ? ? ? F9 ? ? ? F9 00 01 3F D6 ? ? ? F9";

// ---------------------------------------------------------------- offset (1.26.60.x)
constexpr size_t kVtGetBlockSourceDimId = 18;      // BlockSource::getDimensionId
constexpr size_t kVtClientInstanceGetRegion = 30;
constexpr size_t kVtClientInstanceGetLocalPlayer = 31;
constexpr size_t kActorDimension = 0x1E0;
constexpr size_t kActorLevel = 0x1F0;
constexpr size_t kActorRotationComponent = 0x238;  // pointer -> float pitch,yaw (diverifikasi saat jalan)
constexpr size_t kDimensionWeather = 0x1B8;
constexpr size_t kWeatherRainLevel = 0x38;
constexpr size_t kLevelRendererPlayerOff = 0x430;
constexpr size_t kLrpFogColorR = 0x41C;
constexpr size_t kLrpFogEnd = 0x430;
constexpr size_t kLrpCamPos = 0x614;
constexpr size_t kScreenContextTessellatorOff = 0xB0;
constexpr size_t kScreenContextColorHolderOff = 0x30;
constexpr size_t kShaderColorDirtyOff = 0x10;
constexpr size_t kRenderMaterialGroupOff = 32;
constexpr size_t kBlockTypeOff = 0x68;
constexpr size_t kBlockNameOff = 0x88 + 0x40 + 0x8;

constexpr int kMinY = -64;
constexpr int kMaxY = 319;

// ---------------------------------------------------------------- tipe fungsi game
using UpdateFn = uint64_t (*)(void *, uint64_t, uint64_t, uint64_t, uint64_t);
using RenderLevelFn = void (*)(void *, void *, void *);
using GetBlockFn = const void *(*)(void *, const int *);
using TimeIntFn = uint32_t (*)(void *);
using TimeFloatFn = float (*)(void *);
using TessBeginFn = void (*)(void *, void *, int, int, int);
using TessColorFn = void (*)(void *, float, float, float, float);
using TessVertexFn = void (*)(void *, float, float, float);
using RenderMeshFn = void (*)(void *, void *, void *, char *);

struct HashedString {
    uint64_t hash;
    std::string str;
    const HashedString *lastMatch;
    explicit HashedString(const char *s) : hash(0), str(s ? s : ""), lastMatch(nullptr) {
        if (str.empty()) return;
        uint64_t h = 0xCBF29CE484222325ULL;
        for (const char ch : str) h = static_cast<uint64_t>(static_cast<unsigned char>(ch)) ^ (0x100000001B3ULL * h);
        hash = h;
    }
};

struct MaterialPtr {
    void *p[2]{nullptr, nullptr};
    MaterialPtr() = default;
    MaterialPtr(const MaterialPtr &) = delete;
    MaterialPtr &operator=(const MaterialPtr &) = delete;
    MaterialPtr(MaterialPtr &&o) noexcept : p{o.p[0], o.p[1]} { o.p[0] = o.p[1] = nullptr; }
    MaterialPtr &operator=(MaterialPtr &&o) noexcept {
        if (this != &o) {
            p[0] = o.p[0];
            p[1] = o.p[1];
            o.p[0] = o.p[1] = nullptr;
        }
        return *this;
    }
    ~MaterialPtr() {}
    explicit operator bool() const { return p[0] != nullptr; }
};

struct Settings {
    std::atomic_bool moduleOn{true};
    std::atomic_bool glow{true};
    std::atomic_bool rays{true};
    std::atomic_bool flare{true};
    std::atomic_bool flip{false};
    std::atomic_bool debug{false};
    std::atomic_int intensity{100}; // %
    std::atomic_int day{15};        // % kekuatan sinar saat siang
    std::atomic_int length{100};    // %
    std::atomic_int count{32};
    std::atomic_int flareInt{100};  // %
    std::atomic_int material{0};    // 0 = tertutup medan (depth), 1 = overlay
    std::atomic_int colorMode{0};   // 0 auto, 1 emas, 2 putih, 3 pink
    std::atomic_int timeOverride{0};
};

// Status matahari yang dihitung di thread update, dibaca thread render.
struct SunState {
    bool valid = false;
    ray::Vec3 sun;
    float elev = 0.f;
    bool fwdValid = false;
    ray::Vec3 fwd;
    float vis = 1.f;  // 0..1 matahari terlihat (tidak tertutup blok)
    float rain = 0.f; // 0..1
};

struct TimeProbe {
    int mode = 0; // 0 belum, 1 int tick, 2 float 0..1, 3 float tick, -1 gagal
    bool havePrev = false;
    Clock::time_point prevT{};
    uint32_t prevI = 0;
    float prevF = 0.f;
    int attempts = 0;
    uint32_t lastI = 0;
    float lastF = 0.f;
    Clock::time_point lastChange{};
};

struct Globals {
    Settings cfg;

    UpdateFn origUpdate = nullptr;
    RenderLevelFn origRender = nullptr;
    GetBlockFn getBlock = nullptr;
    TimeIntFn timeInt = nullptr;
    TimeFloatFn timeFloat = nullptr;
    TessBeginFn tessBegin = nullptr;
    TessColorFn tessColor = nullptr;
    TessVertexFn tessVertex = nullptr;
    RenderMeshFn renderMesh = nullptr;
    uintptr_t renderMaterialGroup = 0;

    bool resolved = false;
    std::atomic_bool ready{false};
    double probeIntervalSec = 1.5; // bisa diperpendek oleh tes

    std::atomic<void *> clientInstance{nullptr};
    std::atomic<float> cam[3]{0.f, 0.f, 0.f};
    std::atomic<float> fogEnd{0.f};
    std::atomic<float> fog[3]{0.f, 0.f, 0.f};
    std::atomic_bool camValid{false};

    TimeProbe tp;
    bool swapRot = false;
    int updateFaults = 0;
    std::atomic_bool updateBroken{false};
    int drawFaults = 0;
    std::atomic_bool drawBroken{false};
    Clock::time_point nextOcc{};
    Clock::time_point nextDebug{};
    float visSmooth = 1.f;
    std::unordered_map<uintptr_t, bool> clearBlock; // Block* -> udara/kaca (tembus cahaya)

    std::mutex stMutex;
    SunState state;
    Clock::time_point t0 = Clock::now();

    MaterialPtr mat[2];
    int matFetchFails[2] = {0, 0};
    std::atomic<int64_t> lastRenderMs{0};
    std::atomic<int64_t> streakStartMs{0};
    std::atomic<uint32_t> epoch{0};
    uint32_t updateEpoch = 0;
    uint32_t drawEpoch = 0;
    uint32_t loggedReadyEpoch = 0;
    std::atomic_bool loggedFirstDraw{false};
    pl::memory::HookHandle updateHook;
    pl::memory::HookHandle renderHook;
};

Globals g;
ll::mod::NativeMod *gSelf = nullptr;

#define VO_INFO(...) do { if (gSelf) gSelf->getLogger().info(__VA_ARGS__); } while (0)
#define VO_WARN(...) do { if (gSelf) gSelf->getLogger().warn(__VA_ARGS__); } while (0)

float nowSec() { return std::chrono::duration<float>(Clock::now() - g.t0).count(); }

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

constexpr int64_t kUpdateGraceMs = 3000; // tunggu setelah dunia mulai dirender sebelum memanggil fungsi game
constexpr int64_t kDrawGraceMs = 2000;

// Dipanggil dari hook renderLevel tiap frame. renderLevel hanya jalan saat dunia benar-benar
// ditampilkan; celah > 700 ms (layar loading, ganti dimensi, keluar dunia) = dunia baru.
void noteRenderFrame() {
    const int64_t now = nowMs();
    const int64_t last = g.lastRenderMs.load(std::memory_order_relaxed);
    if (last == 0 || now - last > 700) {
        g.streakStartMs.store(now, std::memory_order_relaxed);
        g.epoch.fetch_add(1, std::memory_order_relaxed);
        vo::engine::trace("render: dunia baru terdeteksi, menunggu stabil");
    }
    g.lastRenderMs.store(now, std::memory_order_relaxed);
}

bool worldReady(int64_t graceMs) {
    const int64_t now = nowMs();
    const int64_t last = g.lastRenderMs.load(std::memory_order_relaxed);
    if (last == 0 || now - last > 400) return false;
    return now - g.streakStartMs.load(std::memory_order_relaxed) >= graceMs;
}


SunState currentState() {
    std::lock_guard<std::mutex> lock(g.stMutex);
    return g.state;
}
void setState(const SunState &s) {
    std::lock_guard<std::mutex> lock(g.stMutex);
    g.state = s;
}

// ---------------------------------------------------------------- panggilan game aman
void onUpdateFault(const char *where) {
    ++g.updateFaults;
    vo::engine::trace(std::string("fault update di ") + where);
    VO_WARN("Error tertangkap di {} (ke-{})", where, g.updateFaults);
    SunState s;
    setState(s);
    if (g.updateFaults >= 3) {
        g.updateBroken = true;
        VO_WARN("Terlalu banyak error, Ray Sun dimatikan sampai game dibuka ulang");
    }
}

bool vtableFn(void *obj, size_t slot, uintptr_t &fn) {
    uintptr_t vt = 0;
    if (!vo::engine::safeReadValue(reinterpret_cast<uintptr_t>(obj), vt) || vt < 0x10000) return false;
    return vo::engine::safeReadValue(vt + slot * sizeof(void *), fn) && fn >= 0x10000;
}

bool callPtrSlot(void *obj, size_t slot, void *&out) {
    uintptr_t fn = 0;
    if (!vtableFn(obj, slot, fn)) return false;
    void *r = nullptr;
    const bool ok = vo::engine::guarded([&] { r = reinterpret_cast<void *(*)(void *)>(fn)(obj); });
    if (!ok) {
        onUpdateFault("vtable call");
        return false;
    }
    out = r;
    return true;
}

bool callIntSlot(void *obj, size_t slot, int &out) {
    uintptr_t fn = 0;
    if (!vtableFn(obj, slot, fn)) return false;
    int r = -1;
    const bool ok = vo::engine::guarded([&] { r = reinterpret_cast<int (*)(void *)>(fn)(obj); });
    if (!ok) {
        onUpdateFault("getDimensionId");
        return false;
    }
    out = r;
    return true;
}

bool readBlockName(const void *block, std::string &name) {
    uintptr_t blockType = 0;
    if (!vo::engine::safeReadValue(reinterpret_cast<uintptr_t>(block) + kBlockTypeOff, blockType)) return false;
    if (blockType < 0x10000) return false;
    return vo::engine::readGameString(blockType + kBlockNameOff, name);
}

// Udara dan kaca dianggap tembus cahaya.
bool isClear(const void *block) {
    const auto key = reinterpret_cast<uintptr_t>(block);
    const auto it = g.clearBlock.find(key);
    if (it != g.clearBlock.end()) return it->second;
    std::string name;
    bool clear = false;
    if (key >= 0x10000 && readBlockName(block, name)) {
        clear = name == "minecraft:air" || name.find("glass") != std::string::npos;
    }
    g.clearBlock.emplace(key, clear);
    return clear;
}

// ---------------------------------------------------------------- waktu
bool sampleTime(void *level, uint32_t &iv, float &fv) {
    uint32_t i = 0;
    float f = 0.f;
    const bool ok = vo::engine::guarded([&] {
        i = g.timeInt(level);
        f = g.timeFloat(level);
    });
    if (!ok) {
        onUpdateFault("fungsi waktu");
        return false;
    }
    iv = i;
    fv = f;
    return true;
}

bool tickDeltaOk(double delta, double dt) {
    const double expected = dt * 20.0;
    return delta >= expected * 0.4 && delta <= expected * 1.8 + 2.0;
}

// Mengunci cara membaca waktu: int tick / float 0..1 / float tick. Mengembalikan true kalau terkunci.
bool probeTime(void *level) {
    TimeProbe &tp = g.tp;
    if (tp.mode > 0) return true;
    if (tp.mode < 0) return false;
    uint32_t iv = 0;
    float fv = 0.f;
    if (!sampleTime(level, iv, fv)) {
        tp.mode = -1;
        VO_WARN("Fungsi waktu error saat probe, pakai 'Time preview' manual");
        return false;
    }
    const auto now = Clock::now();
    if (!tp.havePrev) {
        tp.havePrev = true;
        tp.prevT = now;
        tp.prevI = iv;
        tp.prevF = fv;
        return false;
    }
    const double dt = std::chrono::duration<double>(now - tp.prevT).count();
    if (dt < g.probeIntervalSec) return false;

    const bool intPlausible = iv < 0x7FFFFFFFu && tp.prevI < 0x7FFFFFFFu;
    const double dI = static_cast<double>(iv) - static_cast<double>(tp.prevI);
    const bool f01Plausible = std::isfinite(fv) && std::isfinite(tp.prevF) && fv >= 0.f && fv <= 1.0001f && tp.prevF >= 0.f && tp.prevF <= 1.0001f;
    double dF = static_cast<double>(fv) - static_cast<double>(tp.prevF);
    if (dF < -0.5) dF += 1.0; // wrap
    const bool fTickPlausible = std::isfinite(fv) && std::isfinite(tp.prevF) && fv >= 0.f && fv < 1.0e7f && tp.prevF >= 0.f && tp.prevF < 1.0e7f;
    const double dFT = static_cast<double>(fv) - static_cast<double>(tp.prevF);

    int lock = 0;
    if (intPlausible && tickDeltaOk(dI, dt)) lock = 1;
    else if (f01Plausible && tickDeltaOk(dF * 24000.0, dt)) lock = 2;
    else if (fTickPlausible && tickDeltaOk(dFT, dt)) lock = 3;

    if (lock == 0) {
        ++tp.attempts;
        if (tp.attempts >= 6) {
            // waktu mungkin beku (doDaylightCycle off): terima bentuk yang nilainya masuk akal
            if (f01Plausible && fv > 0.f && std::fabs(dF) < 1e-6) lock = 2;
            else if (intPlausible && iv > 0 && dI == 0.0) lock = 1;
            if (lock) VO_WARN("Waktu tampak beku, memakai nilai statis (mode {})", lock);
        }
        if (lock == 0) {
            if (tp.attempts >= 12) {
                tp.mode = -1;
                VO_WARN("Fungsi waktu tidak cocok (tidak ada nilai yang naik ~20/detik). Pakai 'Time preview' manual");
                return false;
            }
            tp.prevT = now;
            tp.prevI = iv;
            tp.prevF = fv;
            return false;
        }
    }
    tp.mode = lock;
    vo::engine::trace("waktu terkunci mode " + std::to_string(lock));
    tp.lastI = iv;
    tp.lastF = fv;
    tp.lastChange = now;
    VO_INFO("Waktu terkunci: mode={} nilai={}", lock, lock == 1 ? static_cast<double>(iv) : static_cast<double>(fv));
    return true;
}

// Tick hari (pecahan) dengan interpolasi antar tick supaya gerak matahari halus.
bool currentTick(void *level, double &tick) {
    const int ov = g.cfg.timeOverride.load();
    if (ov > 0) {
        tick = static_cast<double>(ov);
        return true;
    }
    if (!level || !probeTime(level)) return false;
    TimeProbe &tp = g.tp;
    uint32_t iv = 0;
    float fv = 0.f;
    if (!sampleTime(level, iv, fv)) return false;
    const auto now = Clock::now();
    if (tp.mode == 1) {
        if (iv != tp.lastI) {
            tp.lastI = iv;
            tp.lastChange = now;
        }
        const double frac = std::min(1.0, std::chrono::duration<double>(now - tp.lastChange).count() * 20.0);
        tick = static_cast<double>(iv) + frac;
    } else {
        if (fv != tp.lastF) {
            tp.lastF = fv;
            tp.lastChange = now;
        }
        const double base = (tp.mode == 2) ? static_cast<double>(fv) * 24000.0 : static_cast<double>(fv);
        const double frac = std::min(1.0, std::chrono::duration<double>(now - tp.lastChange).count() * 20.0);
        tick = base + frac;
    }
    return std::isfinite(tick);
}

// ---------------------------------------------------------------- update (thread game)
bool readRotation(void *player, float &yaw, float &pitch) {
    uintptr_t rp = 0;
    if (!vo::engine::safeReadValue(reinterpret_cast<uintptr_t>(player) + kActorRotationComponent, rp) || rp < 0x10000) return false;
    float v[2];
    if (!vo::engine::safeRead(reinterpret_cast<void *>(rp), v, sizeof(v))) return false;
    if (!std::isfinite(v[0]) || !std::isfinite(v[1])) return false;
    // pitch selalu dalam +-90; kalau nilai pertama keluar dari batas itu, urutannya (yaw, pitch)
    if (std::fabs(v[0]) > 91.f && std::fabs(v[1]) <= 91.f) g.swapRot = true;
    pitch = g.swapRot ? v[1] : v[0];
    yaw = g.swapRot ? v[0] : v[1];
    return std::fabs(pitch) <= 91.f && std::fabs(yaw) <= 3600.f;
}

float readRain(void *player) {
    uintptr_t dim = 0;
    if (!vo::engine::safeReadValue(reinterpret_cast<uintptr_t>(player) + kActorDimension, dim) || dim < 0x10000) return 0.f;
    uintptr_t wp = 0;
    float r = 0.f;
    if (vo::engine::safeReadValue(dim + kDimensionWeather, wp) && wp >= 0x10000 &&
        vo::engine::safeReadValue(wp + kWeatherRainLevel, r) && std::isfinite(r) && r >= 0.f && r <= 1.0001f)
        return std::min(r, 1.f);
    if (vo::engine::safeReadValue(dim + kDimensionWeather + kWeatherRainLevel, r) && std::isfinite(r) && r >= 0.f && r <= 1.0001f)
        return std::min(r, 1.f);
    return 0.f;
}

// Apakah garis pandang dari kamera ke matahari tertutup blok? true = terlihat.
bool sunVisible(void *bs, ray::Vec3 sun, bool &ok) {
    ok = false;
    if (!g.getBlock || !g.camValid.load()) return true;
    const float cx = g.cam[0].load(), cy = g.cam[1].load(), cz = g.cam[2].load();
    constexpr int kN = 40;
    int pos[kN][3];
    const void *res[kN];
    int n = 0;
    for (int i = 1; i <= kN; ++i) {
        const float d = 1.5f * static_cast<float>(i);
        const int y = static_cast<int>(std::floor(cy + sun.y * d));
        if (y < kMinY || y > kMaxY) break;
        pos[n][0] = static_cast<int>(std::floor(cx + sun.x * d));
        pos[n][1] = y;
        pos[n][2] = static_cast<int>(std::floor(cz + sun.z * d));
        ++n;
    }
    bool good = vo::engine::guarded([&] {
        for (int i = 0; i < n; ++i) res[i] = g.getBlock(bs, pos[i]);
    });
    if (!good) {
        onUpdateFault("getBlock (oklusi)");
        return true;
    }
    ok = true;
    for (int i = 0; i < n; ++i) {
        if (!res[i]) continue;
        if (!isClear(res[i])) return false;
    }
    return true;
}

// Bersihkan semua cache yang terikat ke satu dunia.
void resetWorldState() {
    setState(SunState{});
    g.clearBlock.clear();
    g.visSmooth = 1.f;
    g.nextOcc = Clock::time_point{};
}

void updateTick() {
    if (!g.cfg.moduleOn.load() || g.updateBroken.load()) return;
    const uint32_t ep = g.epoch.load(std::memory_order_relaxed);
    if (ep != g.updateEpoch) { // dunia baru / selesai loading: buang cache lama
        g.updateEpoch = ep;
        resetWorldState();
    }
    if (!worldReady(kUpdateGraceMs)) return; // jangan sentuh fungsi game saat dunia masih dimuat
    if (g.loggedReadyEpoch != ep) {
        g.loggedReadyEpoch = ep;
        vo::engine::trace("dunia siap, Ray Sun mulai membaca waktu");
    }
    void *ci = g.clientInstance.load();
    if (!ci) return;

    void *player = nullptr;
    if (!callPtrSlot(ci, kVtClientInstanceGetLocalPlayer, player) || reinterpret_cast<uintptr_t>(player) < 0x10000) {
        SunState s;
        setState(s);
        return;
    }

    // hanya overworld
    void *bs = nullptr;
    bool haveBs = callPtrSlot(ci, kVtClientInstanceGetRegion, bs) && reinterpret_cast<uintptr_t>(bs) >= 0x10000;
    if (haveBs) {
        int dimId = 0;
        if (callIntSlot(bs, kVtGetBlockSourceDimId, dimId) && dimId != 0 && dimId >= 0 && dimId < 16) {
            SunState s;
            setState(s);
            return;
        }
    }

    uintptr_t level = 0;
    vo::engine::safeReadValue(reinterpret_cast<uintptr_t>(player) + kActorLevel, level);
    if (level < 0x10000) level = 0;

    double tick = 0;
    SunState s;
    if (!currentTick(reinterpret_cast<void *>(level), tick)) {
        setState(s); // belum ada waktu valid
        return;
    }
    s.sun = ray::sunDirection(tick);
    s.elev = ray::elevationDeg(s.sun);
    s.rain = readRain(player);

    float yaw = 0, pitch = 0;
    if (readRotation(player, yaw, pitch)) {
        s.fwd = ray::forwardFromYawPitch(yaw, pitch);
        s.fwdValid = true;
    }

    const auto now = Clock::now();
    if (now >= g.nextOcc) {
        g.nextOcc = now + std::chrono::milliseconds(100);
        float target = 1.f;
        if (haveBs && s.elev > -10.f) {
            bool ok = false;
            const bool vis = sunVisible(bs, s.sun, ok);
            if (ok) target = vis ? 1.f : 0.f;
        }
        g.visSmooth += (target - g.visSmooth) * 0.3f;
    }
    s.vis = g.visSmooth;
    s.valid = true;
    setState(s);

    if (g.cfg.debug.load() && now >= g.nextDebug) {
        g.nextDebug = now + std::chrono::seconds(2);
        VO_INFO("Ray Sun: tick={} elev={} vis={} rain={} fwd={} (mode waktu {})", static_cast<int>(std::fmod(tick, 24000.0)),
                static_cast<int>(s.elev), s.vis, s.rain, s.fwdValid ? "ok" : "tidak ada", g.tp.mode);
    }
}

uint64_t updateHook(void *self, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    g.clientInstance.store(self, std::memory_order_relaxed);
    const uint64_t r = g.origUpdate ? g.origUpdate(self, a1, a2, a3, a4) : 0;
    try {
        updateTick();
    } catch (...) {
    }
    return r;
}

// ---------------------------------------------------------------- render (thread render)
MaterialPtr fetchMaterial(const char *name) {
    if (!g.renderMaterialGroup) return {};
    HashedString hs(name);
    uintptr_t vtable = 0, fn = 0;
    if (!vo::engine::safeReadValue(g.renderMaterialGroup, vtable) || vtable < 0x10000) return {};
    if (!vo::engine::safeReadValue(vtable + 2 * sizeof(void *), fn) || fn < 0x10000) return {};
    using GetMaterialFn = MaterialPtr (*)(void *, const HashedString *);
    return reinterpret_cast<GetMaterialFn>(fn)(reinterpret_cast<void *>(g.renderMaterialGroup), &hs);
}

// slot 0 = selection_overlay (alpha blend + depth test), slot 1 = ui_fill_color (alpha blend, tanpa depth).
void *getMat(int slot) {
    MaterialPtr &m = g.mat[slot];
    if (!m && g.matFetchFails[slot] < 30) {
        m = fetchMaterial(slot == 0 ? "selection_overlay" : "ui_fill_color");
        if (!m) ++g.matFetchFails[slot];
    }
    return m ? static_cast<void *>(&m) : nullptr;
}

struct ShaderColorGuard {
    unsigned char *holder = nullptr;
    float saved[4]{};
    explicit ShaderColorGuard(void *screenContext) {
        uintptr_t h = 0;
        if (!vo::engine::safeReadValue(reinterpret_cast<uintptr_t>(screenContext) + kScreenContextColorHolderOff, h) || h < 0x10000) return;
        unsigned char raw[kShaderColorDirtyOff + 1];
        if (!vo::engine::safeRead(reinterpret_cast<void *>(h), raw, sizeof(raw))) return;
        holder = reinterpret_cast<unsigned char *>(h);
        std::memcpy(saved, raw, sizeof(saved));
        const float white[4] = {1.f, 1.f, 1.f, 1.f};
        std::memcpy(holder, white, sizeof(white));
        holder[kShaderColorDirtyOff] = 1;
    }
    ~ShaderColorGuard() {
        if (!holder) return;
        std::memcpy(holder, saved, sizeof(saved));
        holder[kShaderColorDirtyOff] = 1;
    }
    ShaderColorGuard(const ShaderColorGuard &) = delete;
    ShaderColorGuard &operator=(const ShaderColorGuard &) = delete;
};

std::vector<ray::V> gBatchA; // glow + sinar
std::vector<ray::V> gBatchB; // flare

void submitQuads(void *tess, void *screenContext, void *material, const std::vector<ray::V> &v) {
    constexpr size_t kMaxVerts = 4096; // kelipatan 4
    for (size_t first = 0; first < v.size(); first += kMaxVerts) {
        const size_t n = std::min(kMaxVerts, v.size() - first);
        g.tessBegin(tess, nullptr, 1, static_cast<int>(n), 0);
        for (size_t i = 0; i < n; ++i) {
            const ray::V &p = v[first + i];
            g.tessColor(tess, p.c[0], p.c[1], p.c[2], p.c[3]);
            g.tessVertex(tess, p.p[0], p.p[1], p.p[2]);
        }
        char pad[0x58];
        std::memset(pad, 0, sizeof(pad));
        g.renderMesh(screenContext, tess, material, pad);
    }
}

ray::Vec3 pickColor(const SunState &s, float fr, float fg, float fb) {
    switch (g.cfg.colorMode.load()) {
    case 1: return {1.00f, 0.78f, 0.40f};
    case 2: return {1.00f, 1.00f, 0.95f};
    case 3: return {1.00f, 0.55f, 0.70f};
    default: return ray::mixFogTint(ray::sunTint(s.elev), fr, fg, fb, 0.18f);
    }
}

void drawCore(void *levelRenderer, void *screenContext) {
    uintptr_t lrp = 0;
    if (!vo::engine::safeReadValue(reinterpret_cast<uintptr_t>(levelRenderer) + kLevelRendererPlayerOff, lrp) || lrp < 0x1000) return;
    float cam[3];
    if (!vo::engine::safeRead(reinterpret_cast<void *>(lrp + kLrpCamPos), cam, sizeof(cam))) return;
    for (int i = 0; i < 3; ++i) g.cam[i].store(cam[i], std::memory_order_relaxed);
    g.camValid.store(true, std::memory_order_relaxed);

    float fogEnd = 0.f;
    float fog[3] = {0, 0, 0};
    vo::engine::safeReadValue(lrp + kLrpFogEnd, fogEnd);
    vo::engine::safeRead(reinterpret_cast<void *>(lrp + kLrpFogColorR), fog, sizeof(fog));
    g.fogEnd.store(fogEnd, std::memory_order_relaxed);

    const uint32_t ep = g.epoch.load(std::memory_order_relaxed);
    if (ep != g.drawEpoch) { // material lama bisa tidak valid setelah resource/shader dimuat ulang
        g.drawEpoch = ep;
        g.mat[0] = MaterialPtr{};
        g.mat[1] = MaterialPtr{};
        g.matFetchFails[0] = g.matFetchFails[1] = 0;
    }
    if (!worldReady(kDrawGraceMs)) return; // tunggu dunia + material selesai dimuat

    const SunState s = currentState();
    if (!s.valid) return;
    // di bawah air / lava / buta, kabut sangat dekat: tidak digambar
    if (!std::isfinite(fogEnd) || fogEnd < 30.f) return;

    const float master = static_cast<float>(g.cfg.intensity.load()) / 100.f;
    const float dayFloor = static_cast<float>(std::clamp(g.cfg.day.load(), 0, 100)) / 100.f;
    const bool overlayMode = g.cfg.material.load() == 1;
    float strength = ray::rayStrength(s.elev, dayFloor) * master * (1.f - 0.75f * s.rain);
    if (overlayMode) strength *= s.vis; // overlay tidak tertutup medan: pakai oklusi blok
    if (strength < 0.01f && !(g.cfg.flare.load())) return;
    if (s.elev < -10.f) return;

    uintptr_t tess = 0;
    if (!vo::engine::safeReadValue(reinterpret_cast<uintptr_t>(screenContext) + kScreenContextTessellatorOff, tess) || tess < 0x1000) return;
    void *tessPtr = reinterpret_cast<void *>(tess);

    const float D = std::clamp(fogEnd * 0.92f, 40.f, 360.f);
    const ray::Vec3 sunPos = s.sun * D;
    const ray::Basis sb = ray::basisFromForward(s.sun);
    const ray::Vec3 tint = pickColor(s, fog[0], fog[1], fog[2]);
    const float t = nowSec();

    gBatchA.clear();
    gBatchB.clear();
    const bool dbl = !overlayMode; // material depth bisa membuang sisi belakang
    if (strength >= 0.01f) {
        if (g.cfg.glow.load()) {
            // glow lebih besar & hangat saat matahari rendah
            const float low = 1.f - ray::smoothstep(4.f, 40.f, s.elev);
            ray::buildGlow(gBatchA, sunPos, sb, D * (0.12f + 0.10f * low), tint, 0.30f * std::min(strength * 1.2f, 1.f), dbl);
        }
        if (g.cfg.rays.load()) {
            ray::BeamParams bp;
            bp.count = std::clamp(g.cfg.count.load(), 8, 64);
            bp.length = 0.85f * static_cast<float>(std::clamp(g.cfg.length.load(), 30, 250)) / 100.f * (0.8f + 0.5f * (1.f - ray::smoothstep(4.f, 40.f, s.elev)));
            bp.width = 0.016f;
            bp.alpha = 0.34f * std::min(strength, 1.f);
            bp.time = t;
            ray::buildBeams(gBatchA, sunPos, sb, D, tint, bp, dbl);
        }
    }

    // lens flare: hanya saat menatap matahari & matahari tidak tertutup
    if (g.cfg.flare.load() && s.fwdValid) {
        const float facing = ray::flareFacing(s.sun, s.fwd);
        const float fi = static_cast<float>(g.cfg.flareInt.load()) / 100.f;
        const float flareStr = facing * s.vis * (1.f - s.rain) * fi * (0.45f + 0.55f * ray::smoothstep(0.f, 25.f, s.elev)) * ray::smoothstep(-6.f, 2.f, s.elev);
        if (flareStr > 0.01f) {
            ray::Basis cb = ray::basisFromForward(s.fwd);
            ray::Vec3 sunFlip = s.sun;
            if (g.cfg.flip.load()) sunFlip = s.sun - cb.r * (2.f * ray::dot(s.sun, cb.r)); // cermin horizontal
            const float Df = 20.f;
            for (const ray::Ghost &gh : ray::kGhosts) {
                const ray::Vec3 c = ray::ghostCenter(cb, sunFlip, gh.k, Df);
                const ray::Vec3 rgb = ray::lerp(gh.rgb, tint, 0.25f);
                ray::buildSprite(gBatchB, c, cb.r, cb.u, gh.radius * Df, gh.ring, rgb, gh.alpha * flareStr, false);
            }
        }
    }
    if (gBatchA.empty() && gBatchB.empty()) return;

    // vertex relatif kamera: sudah relatif (arah * jarak) karena matriks dunia game berpusat di kamera
    ShaderColorGuard guard(screenContext);
    size_t drawn = 0;
    if (!gBatchA.empty()) {
        void *m = getMat(overlayMode ? 1 : 0);
        if (!m) m = getMat(1);
        if (m) {
            submitQuads(tessPtr, screenContext, m, gBatchA);
            drawn += gBatchA.size() / 4;
        }
    }
    if (!gBatchB.empty()) {
        void *m = getMat(1); // flare selalu tanpa depth
        if (m) {
            submitQuads(tessPtr, screenContext, m, gBatchB);
            drawn += gBatchB.size() / 4;
        }
    }
    if (drawn && !g.loggedFirstDraw.exchange(true)) VO_INFO("Gambar pertama: {} quad", drawn);
}

void renderLevelHook(void *self, void *screenContext, void *a3) {
    noteRenderFrame();
    if (g.origRender) g.origRender(self, screenContext, a3);
    if (!g.cfg.moduleOn.load() || g.drawBroken.load() || !self || !screenContext) return;
    if (reinterpret_cast<uintptr_t>(screenContext) < 0x1000) return;
    try {
        const bool ok = vo::engine::guarded([&] { drawCore(self, screenContext); });
        if (!ok) {
            ++g.drawFaults;
            vo::engine::trace("fault saat gambar");
            VO_WARN("Error tertangkap saat menggambar (ke-{})", g.drawFaults);
            if (g.drawFaults >= 2) {
                g.drawBroken = true;
                VO_WARN("Gambar dimatikan sampai game dibuka ulang");
            }
        }
    } catch (...) {
    }
}

// ---------------------------------------------------------------- pengaturan tersimpan
std::mutex gCfgMutex;

bool parseBool(std::string_view v, bool fallback) {
    if (v == "true" || v == "1" || v == "on") return true;
    if (v == "false" || v == "0" || v == "off") return false;
    return fallback;
}
int parseInt(std::string_view v, int fallback) {
    const std::string s(v);
    char *end = nullptr;
    const long x = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0') return fallback;
    return static_cast<int>(x);
}

bool applyKey(std::string_view key, std::string_view value) {
    auto &c = g.cfg;
    if (key == "enabled") c.moduleOn = parseBool(value, c.moduleOn.load());
    else if (key == "glow") c.glow = parseBool(value, c.glow.load());
    else if (key == "rays") c.rays = parseBool(value, c.rays.load());
    else if (key == "flare") c.flare = parseBool(value, c.flare.load());
    else if (key == "flip") c.flip = parseBool(value, c.flip.load());
    else if (key == "debug") c.debug = parseBool(value, c.debug.load());
    else if (key == "intensity") c.intensity = std::clamp(parseInt(value, c.intensity.load()), 0, 200);
    else if (key == "day") c.day = std::clamp(parseInt(value, c.day.load()), 0, 60);
    else if (key == "length") c.length = std::clamp(parseInt(value, c.length.load()), 50, 200);
    else if (key == "count") c.count = std::clamp(parseInt(value, c.count.load()), 12, 64);
    else if (key == "flareint") c.flareInt = std::clamp(parseInt(value, c.flareInt.load()), 0, 200);
    else if (key == "material") c.material = std::clamp(parseInt(value, c.material.load()), 0, 1);
    else if (key == "color") c.colorMode = std::clamp(parseInt(value, c.colorMode.load()), 0, 3);
    else if (key == "time") c.timeOverride = std::clamp(parseInt(value, c.timeOverride.load()), 0, 24000);
    else return false;
    return true;
}

std::string processName() {
    char buf[256] = {0};
    FILE *f = std::fopen("/proc/self/cmdline", "rb");
    if (!f) return {};
    const size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    buf[n] = '\0';
    return std::string(buf);
}

std::vector<std::string> configCandidates() {
    std::vector<std::string> out;
    const std::string pkg = processName();
    if (pkg.empty()) return out;
    out.push_back("/data/data/" + pkg + "/files/ray_sun.cfg");
    out.push_back("/storage/emulated/0/Android/data/" + pkg + "/files/ray_sun.cfg");
    return out;
}

void loadConfig() {
    std::lock_guard<std::mutex> lock(gCfgMutex);
    for (const auto &path : configCandidates()) {
        FILE *f = std::fopen(path.c_str(), "rb");
        if (!f) continue;
        char line[128];
        while (std::fgets(line, sizeof(line), f)) {
            std::string_view sv(line);
            while (!sv.empty() && (sv.back() == '\n' || sv.back() == '\r' || sv.back() == ' ')) sv.remove_suffix(1);
            const size_t eq = sv.find('=');
            if (eq == std::string_view::npos) continue;
            applyKey(sv.substr(0, eq), sv.substr(eq + 1));
        }
        std::fclose(f);
        return;
    }
}

void saveConfig() {
    std::lock_guard<std::mutex> lock(gCfgMutex);
    auto &c = g.cfg;
    std::string text;
    auto put = [&](const char *k, const std::string &v) { text += std::string(k) + "=" + v + "\n"; };
    auto b = [](bool v) { return std::string(v ? "true" : "false"); };
    put("enabled", b(c.moduleOn.load()));
    put("glow", b(c.glow.load()));
    put("rays", b(c.rays.load()));
    put("flare", b(c.flare.load()));
    put("flip", b(c.flip.load()));
    put("debug", b(c.debug.load()));
    put("intensity", std::to_string(c.intensity.load()));
    put("day", std::to_string(c.day.load()));
    put("length", std::to_string(c.length.load()));
    put("count", std::to_string(c.count.load()));
    put("flareint", std::to_string(c.flareInt.load()));
    put("material", std::to_string(c.material.load()));
    put("color", std::to_string(c.colorMode.load()));
    put("time", std::to_string(c.timeOverride.load()));
    for (const auto &path : configCandidates()) {
        const size_t slash = path.find_last_of('/');
        if (slash != std::string::npos) ::mkdir(path.substr(0, slash).c_str(), 0771);
        FILE *f = std::fopen(path.c_str(), "wb");
        if (!f) continue;
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
        return;
    }
}

void onModuleToggle(std::string_view moduleId, bool enabled) {
    if (moduleId != kModuleId) return;
    g.cfg.moduleOn = enabled;
    if (!enabled) setState(SunState{});
    saveConfig();
}

void onConfigChanged(std::string_view moduleId, std::string_view key, std::string_view value) {
    if (moduleId != kModuleId) return;
    if (applyKey(key, value)) saveConfig();
}

// ---------------------------------------------------------------- mod
class RaySunMod {
public:
    static RaySunMod &instance() {
        static RaySunMod mod;
        return mod;
    }

    RaySunMod() : mSelf(*ll::mod::NativeMod::current()) { gSelf = &mSelf; }

    [[nodiscard]] ll::mod::NativeMod &getSelf() const { return mSelf; }

    bool load() { return true; }

    bool enable() {
        auto &self = getSelf();
        vo::engine::traceInit("ray_sun_trace.txt");
        vo::engine::trace("enable");
        self.getLogger().info("Ray Sun v{} by {}", kVersion, kCredit);
        loadConfig();

        using pl::modmenu::ConfigType;
        auto on = [](bool v) { return std::string(v ? "true" : "false"); };
        auto num = [](int v) { return std::to_string(v); };
        auto &c = g.cfg;
        const bool menuOk =
            pl::modmenu::ModuleBuilder(kModuleId, "Ray Sun")
                .modId(self.getId())
                .description("v1.0.0 - by floxsfox | god rays, sun glow & lens flare")
                .defaultEnabled(c.moduleOn.load())
                .onToggle(onModuleToggle)
                .config("intensity", "Master intensity (%)", ConfigType::SliderInt, num(c.intensity.load()), "0", "200")
                .config("day", "Noon ray strength (%)", ConfigType::SliderInt, num(c.day.load()), "0", "60")
                .config("length", "Ray length (%)", ConfigType::SliderInt, num(c.length.load()), "50", "200")
                .config("count", "Ray count", ConfigType::SliderInt, num(c.count.load()), "12", "64")
                .config("glow", "Sun glow", ConfigType::Toggle, on(c.glow.load()))
                .config("rays", "God rays", ConfigType::Toggle, on(c.rays.load()))
                .config("flare", "Lens flare", ConfigType::Toggle, on(c.flare.load()))
                .config("flareint", "Flare intensity (%)", ConfigType::SliderInt, num(c.flareInt.load()), "0", "200")
                .config("color", "Ray color (0 auto, 1 gold, 2 white, 3 pink)", ConfigType::SliderInt, num(c.colorMode.load()), "0", "3")
                .config("material", "Material (0 hidden by terrain, 1 overlay)", ConfigType::SliderInt, num(c.material.load()), "0", "1")
                .config("time", "Time preview (0 = real time, 12000 = sunset)", ConfigType::SliderInt, num(c.timeOverride.load()), "0", "24000")
                .config("flip", "Flip flare (if dots are mirrored)", ConfigType::Toggle, on(c.flip.load()))
                .config("debug", "Debug log", ConfigType::Toggle, on(c.debug.load()))
                .onConfigChanged(onConfigChanged)
                .registerModule();
        if (!menuOk) self.getLogger().error("Gagal mendaftarkan menu Ray Sun");

        vo::engine::installFaultGuard();
        if (!g.resolved && !resolveEngine()) return true;
        g.updateHook = pl::memory::HookHandle(reinterpret_cast<void *>(mUpdateTarget), reinterpret_cast<void *>(&updateHook),
                                              reinterpret_cast<void **>(&g.origUpdate));
        g.renderHook = pl::memory::HookHandle(reinterpret_cast<void *>(mRenderTarget), reinterpret_cast<void *>(&renderLevelHook),
                                              reinterpret_cast<void **>(&g.origRender));
        g.ready = g.updateHook.installed() && g.renderHook.installed();
        vo::engine::trace(g.ready ? "hook terpasang" : "hook GAGAL");
        if (g.ready) self.getLogger().info("Hook terpasang (update + renderLevel)");
        else self.getLogger().error("Hook gagal: update={} render={}", g.updateHook.installed(), g.renderHook.installed());
        return true;
    }

    bool disable() {
        g.ready = false;
        g.updateHook.reset();
        g.renderHook.reset();
        setState(SunState{});
        pl::modmenu::unregisterModule(kModuleId);
        getSelf().getLogger().info("Ray Sun dimatikan");
        return true;
    }

    bool unload() {
        disable();
        return true;
    }

private:
    ll::mod::NativeMod &mSelf;
    uintptr_t mUpdateTarget = 0;
    uintptr_t mRenderTarget = 0;

    bool resolveEngine() {
        auto &log = getSelf().getLogger();
        struct Item {
            const char *name;
            const char *pattern;
            bool required;
        };
        const Item items[] = {
            {"ClientInstanceUpdate", kSigClientInstanceUpdate, true},
            {"RenderLevel", kSigRenderLevel, true},
            {"TessellatorBegin", kSigTessBegin, true},
            {"TessellatorColor", kSigTessColor, true},
            {"TessellatorVertex", kSigTessVertex, true},
            {"RenderMesh", kSigRenderMesh, true},
            {"MaterialGroup", kSigMaterialGroup, true},
            {"BlockSourceGetBlock", kSigGetBlock, false}, // tanpa ini: tidak ada oklusi blok
            {"Time", kSigTime, false},                    // tanpa ini: pakai Time preview manual
        };
        constexpr size_t kN = sizeof(items) / sizeof(items[0]);
        std::vector<std::string> patterns;
        for (const auto &it : items) patterns.emplace_back(it.pattern);
        const auto resolved = pl::memory::resolveSignatures(patterns, kLib);

        uintptr_t addr[kN] = {};
        bool allOk = true;
        for (size_t i = 0; i < kN; ++i) {
            const auto found = resolved.find(std::string(items[i].pattern));
            addr[i] = (found != resolved.end()) ? static_cast<uintptr_t>(found->second) : 0;
            if (!addr[i]) {
                if (items[i].required) {
                    log.error("Signature tidak ketemu: {} (versi game mungkin tidak cocok, target 1.26.60.x)", items[i].name);
                    allOk = false;
                } else {
                    log.warn("Signature opsional tidak ketemu: {}", items[i].name);
                }
            }
        }
        if (!allOk) return false;

        mUpdateTarget = addr[0];
        mRenderTarget = addr[1];
        g.tessBegin = reinterpret_cast<TessBeginFn>(addr[2]);
        g.tessColor = reinterpret_cast<TessColorFn>(addr[3]);
        g.tessVertex = reinterpret_cast<TessVertexFn>(addr[4]);
        g.renderMesh = reinterpret_cast<RenderMeshFn>(addr[5]);
        g.getBlock = addr[7] ? reinterpret_cast<GetBlockFn>(addr[7]) : nullptr;
        g.timeInt = addr[8] ? reinterpret_cast<TimeIntFn>(addr[8]) : nullptr;
        g.timeFloat = addr[8] ? reinterpret_cast<TimeFloatFn>(addr[8]) : nullptr;

        const uintptr_t groupBase = vo::engine::decodeAdrpAdd(reinterpret_cast<const uint32_t *>(addr[6]), 0);
        if (groupBase) g.renderMaterialGroup = groupBase + kRenderMaterialGroupOff;
        else log.warn("RenderMaterialGroup tidak terdecode, gambar dinonaktifkan");

        if (!g.timeInt) g.tp.mode = -1; // hanya Time preview manual
        g.resolved = true;
        return true;
    }
};

} // namespace

PL_REGISTER_MOD(RaySunMod, RaySunMod::instance())
