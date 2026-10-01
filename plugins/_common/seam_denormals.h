//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · seam_denormals.h — subnormals flushed to zero, for one scope
//
// A recursive structure that loses little or no energy (an all-pass chain, a
// long reverberation) lets its tails sink through the subnormal range, below
// 2.2e-308 in double, and keeps them there. On x86 an operation on a
// subnormal costs about a hundred cycles: stunedrev's 588 MiB of memory,
// silent after a burst, went from 4.7 % to 15 % of a core in 30 minutes.
// Nothing guarantees that the host sets flush-to-zero on its audio thread.
//
// ScopedNoDenormals sets flush-to-zero and denormals-are-zero for its scope
// and restores the caller's state when it ends: on x86 the FTZ (bit 15) and
// DAZ (bit 6) bits of MXCSR, on arm64 the FZ bit (bit 24) of FPCR. Values
// below 2.2e-308 become 0; every number above is untouched.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
#include <xmmintrin.h>
#define SEAM_DENORMALS_X86 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#define SEAM_DENORMALS_ARM64 1
#endif

namespace Seam {

class ScopedNoDenormals {
public:
    ScopedNoDenormals() {
#if SEAM_DENORMALS_X86
        saved_ = _mm_getcsr();
        _mm_setcsr(saved_ | 0x8040u);                       // FTZ | DAZ
#elif SEAM_DENORMALS_ARM64
        uint64_t v;
        asm volatile("mrs %0, fpcr" : "=r"(v));
        saved_ = v;
        asm volatile("msr fpcr, %0" : : "r"(v | (1ull << 24)));   // FZ
#endif
    }
    ~ScopedNoDenormals() {
#if SEAM_DENORMALS_X86
        _mm_setcsr(saved_);
#elif SEAM_DENORMALS_ARM64
        asm volatile("msr fpcr, %0" : : "r"(saved_));
#endif
    }
    ScopedNoDenormals(const ScopedNoDenormals&) = delete;
    ScopedNoDenormals& operator=(const ScopedNoDenormals&) = delete;

private:
#if SEAM_DENORMALS_X86
    unsigned int saved_ = 0;
#else
    uint64_t saved_ = 0;
#endif
};

} // namespace Seam
