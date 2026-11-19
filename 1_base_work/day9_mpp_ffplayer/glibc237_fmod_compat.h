/*
 * 让交叉编译出来的 FFmpeg 能在板子的 glibc 2.37 上运行。
 *
 * 问题（2026-11-19 上板实测）：
 *   板子 rootfs 是 glibc 2.37，工具链 sysroot 里的 glibc >= 2.38。glibc 2.38 给 fmod / fmodf 加了一个新的符号版本
 *   （fmod@GLIBC_2.38，新实现），链接时默认绑到这个最新版本。板子上运行就报：
 *     /lib/libm.so.6: version `GLIBC_2.38' not found (required by /userdata/ffmpeg6/lib/libavutil.so.58)
 *   sysroot 的 libm.so.6 里两个版本都有（objdump -T 看到 fmod 有 GLIBC_2.17 和 GLIBC_2.38），
 *   GLIBC_2.17 是 aarch64 上 glibc 最早的版本，板子上一定有。
 *
 * 办法：.symver 告诉汇编器“本文件里引用的 fmod，绑到 fmod@GLIBC_2.17 这个版本”。
 *   编 FFmpeg 时用 --extra-cflags="-include 本文件" 加到每个 .c 文件最前面（见 build_ffmpeg6_for_board.sh）。
 *
 * 和 /Users/dev/rk-toolchain/glibc237_compat.h 的区别：那个解决的是 sscanf / strtol 被重定向到 __isoc23_*@GLIBC_2.38，
 *   FFmpeg 没有这个问题（编完 objdump -T 没看到 __isoc23_*），只有 fmod / fmodf。
 */
#ifndef GLIBC237_FMOD_COMPAT_H
#define GLIBC237_FMOD_COMPAT_H

/* FFmpeg 的 aarch64 汇编文件（.S）也会经过预处理器，汇编文件里不能出现 C 的 __asm__ 语句，所以只在 C 文件里生效 */
#ifndef __ASSEMBLER__
__asm__(".symver fmod,fmod@GLIBC_2.17");
__asm__(".symver fmodf,fmodf@GLIBC_2.17");
#endif

#endif
