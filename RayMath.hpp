#pragma once

// Matematika murni Ray Sun (tanpa dependensi engine): posisi matahari dari waktu,
// kekuatan/warna sinar, dan pembuat geometri (glow, sinar, lens flare).
// Semua koordinat hasil geometri relatif terhadap kamera.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ray {

constexpr float kPi = 3.14159265358979323846f;

struct Vec3 {
    float x = 0, y = 0, z = 0;
};
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a) {
    const float l = length(a);
    return l < 1e-8f ? Vec3{0, 1, 0} : a * (1.0f / l);
}
inline float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
inline float smoothstep(float a, float b, float x) {
    const float t = clamp01((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}
inline Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }

// ----------------------------------------------------------------- matahari
// Sudut langit (0 = tengah hari, 0.25 = matahari terbenam, 0.5 = tengah malam)
// dengan kurva yang sama seperti game (tick 6000 = siang).
inline float celestialAngle(double tickOfDay) {
    double t = std::fmod(tickOfDay, 24000.0);
    if (t < 0) t += 24000.0;
    double f = t / 24000.0 - 0.25;
    if (f < 0) f += 1.0;
    const double d1 = 0.5 - std::cos(f * kPi) / 2.0;
    return static_cast<float>(f + (d1 - f) / 3.0);
}

// Arah matahari (vektor satuan). +X timur, -X barat, +Y atas.
inline Vec3 sunDirection(double tickOfDay) {
    const float a = celestialAngle(tickOfDay) * 2.0f * kPi;
    return normalize({-std::sin(a), std::cos(a), 0.0f});
}
inline float elevationDeg(Vec3 s) { return std::asin(std::clamp(s.y, -1.0f, 1.0f)) * 180.0f / kPi; }

// Kekuatan sinar 0..1: penuh saat matahari rendah, tipis (dayFloor) saat siang,
// memudar saat matahari turun di bawah horizon.
inline float rayStrength(float elevDeg, float dayFloor) {
    const float low = 1.0f - smoothstep(4.0f, 38.0f, elevDeg);
    const float s = dayFloor + (1.0f - dayFloor) * low;
    return s * smoothstep(-9.0f, -1.0f, elevDeg);
}

// Warna sinar menurut ketinggian matahari: oranye merah di horizon -> kuning pucat saat siang.
inline Vec3 sunTint(float elevDeg) {
    const Vec3 horizon{1.00f, 0.42f, 0.12f};
    const Vec3 low{1.00f, 0.68f, 0.32f};
    const Vec3 mid{1.00f, 0.88f, 0.62f};
    const Vec3 high{1.00f, 0.95f, 0.82f};
    if (elevDeg <= 0.0f) return horizon;
    if (elevDeg < 8.0f) return lerp(horizon, low, elevDeg / 8.0f);
    if (elevDeg < 22.0f) return lerp(low, mid, (elevDeg - 8.0f) / 14.0f);
    if (elevDeg < 45.0f) return lerp(mid, high, (elevDeg - 22.0f) / 23.0f);
    return high;
}

// Campur sedikit warna kabut (supaya cocok dengan warna langit shader).
inline Vec3 mixFogTint(Vec3 tint, float fr, float fg, float fb, float mix) {
    const bool valid = std::isfinite(fr) && std::isfinite(fg) && std::isfinite(fb) && fr >= 0 && fg >= 0 && fb >= 0 &&
                       fr <= 4 && fg <= 4 && fb <= 4;
    const float m = std::max({fr, fg, fb});
    if (!valid || m < 0.05f) return tint;
    const Vec3 fogN{fr / m, fg / m, fb / m};
    return lerp(tint, fogN, clamp01(mix));
}

// ----------------------------------------------------------------- kamera
// Yaw 0 = menghadap +Z (selatan), yaw 90 = barat (-X). Pitch positif = melihat ke bawah.
inline Vec3 forwardFromYawPitch(float yawDeg, float pitchDeg) {
    const float y = yawDeg * kPi / 180.0f;
    const float p = pitchDeg * kPi / 180.0f;
    return normalize({-std::sin(y) * std::cos(p), -std::sin(p), std::cos(y) * std::cos(p)});
}

struct Basis {
    Vec3 f, r, u;
};
inline Basis basisFromForward(Vec3 f) {
    f = normalize(f);
    Vec3 r = cross(f, {0, 1, 0});
    if (length(r) < 1e-4f) r = {1, 0, 0};
    r = normalize(r);
    return {f, r, cross(r, f)};
}

// ----------------------------------------------------------------- geometri
struct V {
    float p[3];
    float c[4];
};

inline V makeV(Vec3 p, Vec3 rgb, float a) { return V{{p.x, p.y, p.z}, {rgb.x, rgb.y, rgb.z, clamp01(a)}}; }

// Quad (4 vertex). doubleSided: tambah salinan dengan urutan terbalik supaya tidak
// hilang karena culling.
inline void pushQuad(std::vector<V>& out, const V& a, const V& b, const V& c, const V& d, bool doubleSided) {
    out.push_back(a);
    out.push_back(b);
    out.push_back(c);
    out.push_back(d);
    if (doubleSided) {
        out.push_back(d);
        out.push_back(c);
        out.push_back(b);
        out.push_back(a);
    }
}

inline uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
inline float rnd01(uint32_t seed, uint32_t a, uint32_t b) {
    return static_cast<float>(hash32(seed * 0x9E3779B9U + a * 0x85EBCA6BU + b * 0xC2B2AE35U) & 0xFFFFFF) / 16777215.0f;
}

// Cakram lembut berlapis (glow / bintik flare). radii & alpha: faktor per cincin.
// center, r, u dalam koordinat relatif kamera; radius dalam satuan dunia.
inline void buildDisc(std::vector<V>& out, Vec3 center, Vec3 r, Vec3 u, float radius, const float* radii,
                      const float* alphas, int rings, Vec3 rgb, float alpha, bool dbl, int segs = 24) {
    for (int ri = 0; ri + 1 < rings; ++ri) {
        for (int s = 0; s < segs; ++s) {
            const float a0 = 2.0f * kPi * static_cast<float>(s) / static_cast<float>(segs);
            const float a1 = 2.0f * kPi * static_cast<float>(s + 1) / static_cast<float>(segs);
            auto pt = [&](float rad, float ang) {
                return center + (r * std::cos(ang) + u * std::sin(ang)) * (radius * rad);
            };
            const V i0 = makeV(pt(radii[ri], a0), rgb, alpha * alphas[ri]);
            const V i1 = makeV(pt(radii[ri], a1), rgb, alpha * alphas[ri]);
            const V o1 = makeV(pt(radii[ri + 1], a1), rgb, alpha * alphas[ri + 1]);
            const V o0 = makeV(pt(radii[ri + 1], a0), rgb, alpha * alphas[ri + 1]);
            pushQuad(out, i0, i1, o1, o0, dbl);
        }
    }
}

inline void buildGlow(std::vector<V>& out, Vec3 center, const Basis& bs, float radius, Vec3 rgb, float alpha, bool dbl) {
    static constexpr float radii[4] = {0.0f, 0.35f, 0.70f, 1.0f};
    static constexpr float alphas[4] = {1.0f, 0.5f, 0.17f, 0.0f};
    buildDisc(out, center, bs.r, bs.u, radius, radii, alphas, 4, rgb, alpha, dbl, 24);
}

// Bintik lens flare: cakram halus atau cincin.
inline void buildSprite(std::vector<V>& out, Vec3 center, Vec3 r, Vec3 u, float radius, bool ringShape, Vec3 rgb,
                        float alpha, bool dbl) {
    if (ringShape) {
        static constexpr float radii[3] = {0.70f, 0.85f, 1.0f};
        static constexpr float alphas[3] = {0.0f, 1.0f, 0.0f};
        buildDisc(out, center, r, u, radius, radii, alphas, 3, rgb, alpha, dbl, 20);
    } else {
        static constexpr float radii[3] = {0.0f, 0.78f, 1.0f};
        static constexpr float alphas[3] = {0.85f, 0.85f, 0.0f};
        buildDisc(out, center, r, u, radius, radii, alphas, 3, rgb, alpha, dbl, 16);
    }
}

struct BeamParams {
    int count = 32;
    float baseRadius = 0.03f; // satuan: fraksi D
    float length = 0.9f;
    float width = 0.016f;
    float alpha = 0.3f;
    float time = 0.0f;
    uint32_t seed = 1337;
};

// Sinar memancar dari matahari: segitiga tipis, pangkal terang -> ujung transparan.
inline void buildBeams(std::vector<V>& out, Vec3 center, const Basis& bs, float D, Vec3 rgb, const BeamParams& p, bool dbl) {
    for (int i = 0; i < p.count; ++i) {
        const uint32_t ui = static_cast<uint32_t>(i);
        const float jitter = (rnd01(p.seed, ui, 1) - 0.5f) * 0.8f;
        const float ang = 2.0f * kPi * (static_cast<float>(i) + jitter) / static_cast<float>(p.count);
        const float lenF = 0.55f + 0.95f * rnd01(p.seed, ui, 2);
        const float widF = 0.6f + 0.9f * rnd01(p.seed, ui, 3);
        const float strF = 0.35f + 0.65f * rnd01(p.seed, ui, 4);
        const float phase = rnd01(p.seed, ui, 5) * 2.0f * kPi;
        const float speed = 0.3f + 0.7f * rnd01(p.seed, ui, 6);
        const float shimmer = 0.85f + 0.15f * std::sin(p.time * speed + phase);

        const Vec3 dir = bs.r * std::cos(ang) + bs.u * std::sin(ang);
        const Vec3 perp = bs.r * (-std::sin(ang)) + bs.u * std::cos(ang);
        const float r0 = p.baseRadius * D;
        const float r1 = p.length * lenF * D;
        const float w = p.width * widF * D;
        const float a0 = p.alpha * strF * shimmer;

        const Vec3 b0 = center + dir * r0;
        const V left = makeV(b0 + perp * w, rgb, a0);
        const V right = makeV(b0 - perp * w, rgb, a0);
        const V tip = makeV(center + dir * r1, rgb, 0.0f);
        pushQuad(out, left, right, tip, tip, dbl);
    }
}

// Posisi bintik flare pada garis dari matahari melewati tengah layar. k=1 di matahari,
// k=0 di tengah layar, k<0 di sisi seberang. Hasil relatif kamera, jarak D.
inline Vec3 ghostCenter(const Basis& cam, Vec3 sun, float k, float D) {
    const float sf = std::max(dot(sun, cam.f), 0.05f);
    const float sx = dot(sun, cam.r) / sf;
    const float sy = dot(sun, cam.u) / sf;
    return normalize(cam.f + cam.r * (sx * k) + cam.u * (sy * k)) * D;
}

// 1 saat menatap matahari, 0 saat sudut > ~50 derajat.
inline float flareFacing(Vec3 sun, Vec3 fwd) {
    return smoothstep(std::cos(50.0f * kPi / 180.0f), std::cos(6.0f * kPi / 180.0f), dot(sun, fwd));
}

struct Ghost {
    float k;
    float radius; // satuan: fraksi D
    bool ring;
    Vec3 rgb;
    float alpha;
};
inline constexpr std::array<Ghost, 6> kGhosts = {{
    {0.62f, 0.030f, false, {1.00f, 0.85f, 0.45f}, 0.22f},
    {0.30f, 0.045f, false, {0.55f, 1.00f, 0.65f}, 0.16f},
    {-0.22f, 0.028f, false, {0.55f, 0.75f, 1.00f}, 0.20f},
    {-0.50f, 0.070f, true, {1.00f, 0.55f, 0.85f}, 0.14f},
    {-0.85f, 0.038f, false, {0.90f, 0.80f, 1.00f}, 0.18f},
    {-1.20f, 0.055f, true, {0.50f, 1.00f, 0.90f}, 0.11f},
}};

} // namespace ray
