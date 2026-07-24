
DEFINES += PAINTTY_USE_SIMD

mac {
    contains(QMAKE_HOST.arch, aarch64)|contains(QMAKE_HOST.arch, arm64) {
        DEFINES -= PAINTTY_USE_SIMD
        warning("SIMD not supported on macOS arm64")
    }
}

linux {
    DEFINES -= PAINTTY_USE_SIMD
}

win32 {
    DEFINES -= PAINTTY_USE_SIMD
    warning("SIMD not supported on Windows (MSVC lacks <experimental/simd>)")
}

PAINTTY_USE_SIMD {
    # SIMD支持
    QMAKE_CXXFLAGS += -march=native -msse2 -mavx -mavx2
    # 对于支持AVX-512的系统，可以添加: -mavx512f -mavx512dq
    # QMAKE_CXXFLAGS += -mavx512f -mavx512dq

    # 启用更好的优化
    QMAKE_CXXFLAGS_RELEASE += -O3 -ffast-math -funroll-loops
}