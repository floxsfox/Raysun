#pragma once

#include <atomic>
#include <csetjmp>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <type_traits>

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

namespace vo::engine {

// Baca memori proses sendiri tanpa risiko crash: data ditulis ke pipe oleh kernel.
// Kalau alamat tidak bisa dibaca, write() mengembalikan error (EFAULT), bukan SIGSEGV.
inline bool safeRead(const void* src, void* dst, size_t n) {
    static std::mutex mu;
    static int fds[2] = {-1, -1};
    static bool init = false;
    std::lock_guard<std::mutex> lock(mu);
    if (!init) {
        init = true;
        if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0) fds[0] = fds[1] = -1;
    }
    if (fds[0] < 0 || !src || n == 0 || n > 4096) return false;
    const ssize_t w = write(fds[1], src, n);
    if (w < 0) return false;
    size_t got = 0;
    auto* out = static_cast<char*>(dst);
    while (got < static_cast<size_t>(w)) {
        const ssize_t r = read(fds[0], out + got, static_cast<size_t>(w) - got);
        if (r <= 0) break;
        got += static_cast<size_t>(r);
    }
    return got == n && static_cast<size_t>(w) == n;
}

template <typename T>
inline bool safeReadValue(uintptr_t addr, T& out) {
    return safeRead(reinterpret_cast<const void*>(addr), &out, sizeof(T));
}

// Baca std::string milik game (libc++, layout default Android) di alamat addr.
inline bool readGameString(uintptr_t addr, std::string& out, size_t maxLen = 96) {
    unsigned char raw[24];
    if (!safeRead(reinterpret_cast<const void*>(addr), raw, sizeof(raw))) return false;
    if (raw[0] & 1) { // long string: cap | size | data*
        uint64_t size = 0;
        uintptr_t data = 0;
        std::memcpy(&size, raw + 8, 8);
        std::memcpy(&data, raw + 16, 8);
        if (size == 0 || size > maxLen || data < 0x10000) return false;
        out.resize(static_cast<size_t>(size));
        return safeRead(reinterpret_cast<const void*>(data), out.data(), static_cast<size_t>(size));
    }
    const size_t size = raw[0] >> 1; // short string: size di byte pertama, data setelahnya
    if (size == 0 || size > 22) return false;
    out.assign(reinterpret_cast<const char*>(raw + 1), size);
    return true;
}

// Decode pasangan "ADRP Xrd, page" + "ADD Xrd, Xrd, #imm" di awal fungsi.
// Mengembalikan alamat hasil (alamat data statis), atau 0 kalau pola tidak cocok.
inline uintptr_t decodeAdrpAdd(const uint32_t* code, uint32_t rd) {
    const uint32_t adrp = code[0];
    const uint32_t add = code[1];
    if ((adrp & 0x9F000000u) != 0x90000000u || (adrp & 0x1Fu) != rd) return 0;
    if ((add & 0xFF000000u) != 0x91000000u) return 0;
    if (((add >> 5) & 0x1Fu) != rd || (add & 0x1Fu) != rd) return 0;

    int64_t imm = static_cast<int64_t>(((adrp >> 5) & 0x7FFFFu) << 2 | ((adrp >> 29) & 0x3u));
    if (imm & (1ll << 20)) imm -= (1ll << 21); // sign-extend 21 bit
    const uintptr_t page = (reinterpret_cast<uintptr_t>(code) & ~static_cast<uintptr_t>(0xFFF)) +
                           static_cast<uintptr_t>(imm << 12);
    uintptr_t off = (add >> 10) & 0xFFFu;
    if ((add >> 22) & 1u) off <<= 12;
    return page + off;
}

// ---------------------------------------------------------------------------
// Pengaman crash: kalau kode game yang kita panggil (getBlock, tessellator) kena
// SIGSEGV/SIGBUS saat dijalankan lewat guarded(), eksekusi dikembalikan ke guarded()
// dan hasilnya false -> pemanggil bisa berhenti dengan aman. Di luar guarded(),
// sinyal diteruskan ke handler lama (crash normal / debuggerd).
// Aturan: isi fungsi yang dijaga jangan memegang objek C++ dengan destruktor.
inline thread_local sigjmp_buf *g_tlsJmp = nullptr;
inline struct sigaction g_oldSegv;
inline struct sigaction g_oldBus;
inline std::atomic_bool g_guardInstalled{false};

inline void faultHandler(int sig, siginfo_t *info, void *ctx) {
    if (g_tlsJmp) siglongjmp(*g_tlsJmp, 1);
    const struct sigaction &old = (sig == SIGBUS) ? g_oldBus : g_oldSegv;
    if (old.sa_flags & SA_SIGINFO) {
        if (old.sa_sigaction) {
            old.sa_sigaction(sig, info, ctx);
            return;
        }
    } else if (old.sa_handler != SIG_DFL && old.sa_handler != SIG_IGN) {
        old.sa_handler(sig);
        return;
    }
    signal(sig, SIG_DFL); // return -> instruksi yang sama fault lagi -> crash normal
}

inline bool installFaultGuard() {
    if (g_guardInstalled.exchange(true)) return true;
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = faultHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    const bool ok = sigaction(SIGSEGV, &sa, &g_oldSegv) == 0 && sigaction(SIGBUS, &sa, &g_oldBus) == 0;
    return ok;
}

inline bool runGuarded(void (*fn)(void *), void *arg) {
    sigjmp_buf jb;
    if (sigsetjmp(jb, 0) != 0) {
        g_tlsJmp = nullptr;
        return false;
    }
    g_tlsJmp = &jb;
    fn(arg);
    g_tlsJmp = nullptr;
    return true;
}

template <typename F>
inline bool guarded(F &&f) {
    using Fn = std::remove_reference_t<F>;
    return runGuarded([](void *p) { (*static_cast<Fn *>(p))(); }, &f);
}

} // namespace vo::engine
