#include <cstdint>

namespace __llvm_libc {
    double exp(double);
}

namespace __llvm_libc_basic {
    double exp(double);
}

namespace {
     // 0 = unknown, 1 = no FMA, 2 = has FMA
     int g_fma_status = 0;

     // CPUID + XGETBV detection (no runtime init required)
     __attribute__((noinline))
     bool detect_fma_support()
     {
         uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;

         __asm__ volatile(
             "cpuid"
             : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
             : "a"(1), "c"(0)
         );

         constexpr uint32_t FMA_BIT = 1u << 12;
         constexpr uint32_t OSXSAVE_BIT = 1u << 27;

         if ((ecx & (FMA_BIT | OSXSAVE_BIT)) != (FMA_BIT | OSXSAVE_BIT))
             return false;

         uint32_t xcr0_lo = 0, xcr0_hi = 0;
         __asm__ volatile(
             "xgetbv"
             : "=a"(xcr0_lo), "=d"(xcr0_hi)
             : "c"(0)
         );

         constexpr uint32_t XMM_STATE = 1u << 1;
         constexpr uint32_t YMM_STATE = 1u << 2;

         return (xcr0_lo & (XMM_STATE | YMM_STATE)) == (XMM_STATE | YMM_STATE);
     }

     __attribute__((always_inline))
     inline bool cpu_has_fma()
     {
         int status = g_fma_status;
         if (__builtin_expect(status != 0, 1))
             return status == 2;

         bool has_fma = detect_fma_support();
         g_fma_status = has_fma ? 2 : 1;
         return has_fma;
     }
}

extern "C" double exp(double x)
{
    return cpu_has_fma() ? __llvm_libc::exp(x) : __llvm_libc_basic::exp(x);
}
