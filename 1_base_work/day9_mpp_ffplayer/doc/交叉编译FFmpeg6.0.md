# 交叉编译 FFmpeg 6.0（给 RK3588 板子用）

> 2026-11-19 · day9_mpp_ffplayer
> 一键脚本：[build_ffmpeg6_for_board.sh](../build_ffmpeg6_for_board.sh)；推到板子：[push_ffmpeg6_to_board.sh](../push_ffmpeg6_to_board.sh)

## 1. 为什么要自己编

| | 版本 | 在哪 |
|---|---|---|
| 板子 `/usr/lib` 和 sysroot 里自带的 | **4.4.1**（libavcodec 58） | 正点原子 buildroot 编好的 |
| MppPlayer 的源码（SimpleFFPlay6.0） | **6.0** | 照 FFmpeg 6.0 的 `fftools/ffplay.c` 拆出来的 |

ffplay 6.0 用了不少 4.4 里没有的接口。只算播放器代码（不算 fftools），一共 **65 处**（2026-11-19 grep 统计），例如：

| 6.0 的写法 | 4.4 里对应的旧写法 | 用在哪 |
|---|---|---|
| `AVChannelLayout`、`frame->ch_layout`、`av_channel_layout_copy` | `uint64_t channel_layout` + `int channels` | 音频参数、重采样 |
| `swr_alloc_set_opts2` | `swr_alloc_set_opts` | 音频重采样 |
| `av_fifo_alloc2` / `av_fifo_write` | `av_fifo_alloc` / `av_fifo_generic_write` | 包队列 |

两条路：把这 65 处改回 4.4 的写法，或者给板子编一份 6.0。选了后者：**代码和 Mac 上的 SimpleFFPlay6.0 一模一样**，对照着学方便，以后升级也只要重编库。

## 2. 结果一览

| 项 | 值 |
|---|---|
| 版本 | FFmpeg 6.0（`ffversion.h` 里写的是 `6.0.2`），源码是本机 `/Users/dev/Documents/AV/FFmpeg-6.0` 的 HEAD（`638687b56c`，带本地定制补丁） |
| 编译器 | 正点原子工具链 `gcc 10.4.0 (Buildroot -g8f917de0)`，在 OrbStack 的 rk-dev 里跑 |
| 源码副本 | `/Users/dev/rk-toolchain/ffmpeg6-src`（编译目录 `build-aarch64/`） |
| 安装目录（Mac 上） | `/Users/dev/rk-toolchain/ffmpeg6-rk3588`（`include/` 1.4 MB，`lib/` 131 MB） |
| 板子上的位置 | `/userdata/ffmpeg6/lib`，MppPlayer 用 rpath 写死了这个目录 |
| 耗时 | make + install 约 5 分钟（实测：configure 写完 `config.h` 13:46:56 → `libavcodec.so` 装好 13:51:46） |

编出来的 8 个库（2026-11-19 实测大小，带调试符号没 strip，所以比较大）：

| 库 | soname（程序运行时按这个名字找） | 大小 |
|---|---|---|
| libavcodec | `libavcodec.so.60` | 80.9 MB |
| libavfilter | `libavfilter.so.9` | 25.0 MB |
| libavformat | `libavformat.so.60` | 22.5 MB |
| libavutil | `libavutil.so.58` | 3.8 MB |
| libswscale | `libswscale.so.7` | 3.4 MB |
| libswresample | `libswresample.so.4` | 0.6 MB |
| libavdevice | `libavdevice.so.60` | 0.4 MB |
| libpostproc | `libpostproc.so.57` | 0.4 MB |

依赖（`readelf -d` 看 NEEDED）：除了 FFmpeg 自己的几个库，只有 `libc.so.6`、`libm.so.6`、`libz.so.1`，板子上都有。

## 3. 整体流程

```
Mac                                    rk-dev（OrbStack 里的 x86_64 Ubuntu）           板子
───────────────────────────────        ──────────────────────────────────────        ─────────────────────
FFmpeg-6.0 仓库
  │ git archive HEAD
  ▼
~/rk-toolchain/ffmpeg6-src  ────────▶  configure（交叉编译参数）
  （Mac 和 rk-dev 路径一样）              make -j9
                                        make install
                                          │
~/rk-toolchain/ffmpeg6-rk3588  ◀─────────┘
  ├─ include/   ──▶ CMake 编译 MppPlayer 时找头文件
  └─ lib/       ──▶ CMake 链接；push_ffmpeg6_to_board.sh ──adb push──▶ /userdata/ffmpeg6/lib
```

## 4. 一键编译

```bash
cd 1_base_work/day9_mpp_ffplayer
./build_ffmpeg6_for_board.sh
```

脚本做了三件事，下面一件一件说。

### 4.1 导出一份干净的源码

```bash
mkdir -p /Users/dev/rk-toolchain/ffmpeg6-src
(cd /Users/dev/Documents/AV/FFmpeg-6.0 && git archive HEAD) | tar -x -C /Users/dev/rk-toolchain/ffmpeg6-src
```

**为什么不直接用原来的源码目录**：那个目录之前给 Mac 编过（SimpleFFPlay6.0 用的 `install_macos`），源码目录里留着 configure 生成的 `config.h`。FFmpeg 的 configure 有一条规定：源码目录里有 `config.h` 时，不允许在别的目录（out-of-tree）编译，会报

```
Out of tree builds are impossible with config.h in source dir.
```

在原目录里重新 configure 又会把 Mac 的编译结果冲掉。所以用 `git archive` 导出一份只有 git 跟踪文件的副本：没有 `config.h`、没有 `.o`，也不会动原来的目录。

> `git archive` 只导出**已提交**的内容。改了 FFmpeg 源码想让板子版也带上，要先在原仓库提交，再删掉 `ffmpeg6-src` 重新跑脚本。

### 4.2 在 rk-dev 里 configure

```bash
orb -m rk-dev bash -c "
export PATH=/Users/dev/rk-toolchain/atk-dlrk3588-toolchain/bin:\$PATH
mkdir -p /Users/dev/rk-toolchain/ffmpeg6-src/build-aarch64
cd /Users/dev/rk-toolchain/ffmpeg6-src/build-aarch64
../configure ...（参数见下表）
"
```

**为什么进 rk-dev 编，不在 Mac 上用转发脚本**：`mac-bin/aarch64-buildroot-linux-gnu-gcc` 是个转发脚本，每调用一次都要 `orb -m rk-dev` 进一次虚拟机。CMake 工程只编几十个文件，这点开销无所谓；FFmpeg 的 configure 要试编译几千次，make 还要编两千多个源文件（实测 `build-aarch64/` 里 2035 个 `.o`），每次都转发会慢很多。所以干脆整个流程都放到 rk-dev 里跑。Mac 和 rk-dev 用同一个路径访问 `/Users/dev`，路径不用转换。

**为什么在 `build-aarch64/` 里编（out-of-tree）**：编出来的 `.o`、`config.h` 都放在这个子目录里，源码目录保持干净。想从头再编，删掉 `build-aarch64/` 就行。

configure 参数：

| 参数 | 意思 |
|---|---|
| `--prefix=/Users/dev/rk-toolchain/ffmpeg6-rk3588` | `make install` 装到哪。也会写进 `.pc` 文件，不影响板子（板子靠 rpath 找库，不看 prefix） |
| `--enable-cross-compile` | 交叉编译：编出来的程序在别的机器上跑，configure 不去运行试编译出来的程序 |
| `--arch=aarch64` `--target-os=linux` | 目标机器：64 位 ARM、Linux。决定打开哪些汇编优化（NEON） |
| `--cross-prefix=aarch64-buildroot-linux-gnu-` | 工具名前缀：gcc、ar、nm、strip 都在前面加上它，例如 `aarch64-buildroot-linux-gnu-gcc` |
| `--sysroot=.../aarch64-buildroot-linux-gnu/sysroot` | 板子根文件系统的副本，头文件（`zlib.h`）和库（`libz.so`）从这里找，不会误用 rk-dev 自己的 |
| `--enable-shared --disable-static` | 编成动态库 `.so`。板子上和程序分开放，程序只有几百 KB，改代码后推得快 |
| `--enable-gpl` | 打开 GPL 部分。libpostproc 要求 GPL，SimpleFFPlay 的 CMake 链接了它 |
| `--disable-autodetect` | **不自动探测**第三方库。不关的话 configure 会在 sysroot 里找到 SDL、pulse、vaapi、xcb 等，自动链上，库就多了一堆依赖 |
| `--enable-zlib` | 关掉自动探测后，明确打开 zlib：mov/mp4 拆封装遇到压缩的 moov 头要用 |
| `--disable-programs` | 不编 `ffmpeg`、`ffprobe`、`ffplay` 命令行工具（我们自己的播放器就是 ffplay） |
| `--disable-doc` | 不生成文档 |
| `--enable-debug=3` `--disable-stripping` | 保留完整调试信息，`make install` 时不 strip。gdb 调 MppPlayer 时能单步进到 FFmpeg 函数里 |
| `--extra-cflags="-include .../glibc237_fmod_compat.h"` | 每个 `.c` 最前面先包含这个头文件，把 `fmod` / `fmodf` 绑到老的符号版本，板子的 glibc 2.37 才能加载（见第 9 节第 1 行，2026-11-19 上板踩坑后加的） |

没加 `--disable-optimizations`：保留 `-O3`（`ffbuild/config.mak` 的 CFLAGS 里核对过）和 NEON 汇编，音频重采样、滤镜在板子上跑得快一些。代价是在 FFmpeg 函数里单步时，有些变量会显示 `<optimized out>`。

configure 跑完会打印配置摘要，确认这几行：

```
External libraries:
iconv                   zlib
```

`iconv` 是 glibc 自带的字符集转换函数（不是额外的库），`--disable-autodetect` 不管它。

### 4.3 make + install

```bash
make -j$(nproc)    # rk-dev 有 9 个核
make install
```

装好后 `ffmpeg6-rk3588/` 下有 `include/`（头文件）、`lib/`（`.so` 和 `pkgconfig/*.pc`）、`share/`（示例代码，用不到）。

检查编出来的是不是 ARM 的：

```bash
file /Users/dev/rk-toolchain/ffmpeg6-rk3588/lib/libavcodec.so.60.3.100
# ELF 64-bit LSB shared object, ARM aarch64, ..., with debug_info, not stripped
```

## 5. 在 MppPlayer 的 CMake 里用

见 [CMakeLists.txt](../CMakeLists.txt)。有三个要注意的地方。

### 5.1 find_library 要加 `NO_CMAKE_FIND_ROOT_PATH`

```cmake
find_library(FFMPEG_avcodec_LIBRARY NAMES avcodec
        PATHS "${FFMPEG_PREFIX}/lib" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
```

工具链文件 `rk3588-toolchain.cmake` 里有 `CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY`：所有 `find_library` 的搜索路径都会被加上 sysroot 前缀，变成 `sysroot/Users/dev/rk-toolchain/ffmpeg6-rk3588/lib`，当然找不到。我们的库不在 sysroot 里，所以加 `NO_CMAKE_FIND_ROOT_PATH` 关掉这个改写。`NO_DEFAULT_PATH` 则保证不会找到 sysroot 里的 4.4.1。

### 5.2 fftools 的 config.h 换成 aarch64 版

ffplay 的 `cmdutils.c`、`opt_common.c` 要包含 FFmpeg 编译时生成的 `config.h` 和 `config_components.h`（里面是 `ARCH_AARCH64`、`CONFIG_AVFILTER`、`HAVE_xxx` 这些开关）。SimpleFFPlay6.0 里那份是 macOS 编译生成的（`CONFIG_AVFOUNDATION 1`、`CONFIG_APPKIT 1`……），换成 `build-aarch64/` 里生成的：

```bash
cp /Users/dev/rk-toolchain/ffmpeg6-src/build-aarch64/config.h \
   /Users/dev/rk-toolchain/ffmpeg6-src/build-aarch64/config_components.h \
   src/fftools/
```

**重新 configure 了 FFmpeg（改了参数）之后，这两个文件要重新拷一次。**

### 5.3 rpath：只写板子上的路径

```cmake
set_target_properties(MppPlayer PROPERTIES SKIP_BUILD_RPATH ON)
target_link_options(MppPlayer PRIVATE "-Wl,-rpath,/userdata/ffmpeg6/lib:/userdata/mpp_build/lib")
```

- `-Wl,-rpath,目录`：把“运行时先去这些目录找 `.so`”写进程序文件，效果和 `LD_LIBRARY_PATH` 一样，但从桌面图标启动时也有效
- 程序需要 `libavcodec.so.60`，板子 `/usr/lib` 里是 `libavcodec.so.58`。soname 不同，不会加载错；就算 `/usr/lib` 里有同名文件，rpath 里的目录也会先被找到
- `SKIP_BUILD_RPATH ON`：不加的话，CMake 会自动把链接用到的库目录（Mac 上的 `/Users/dev/rk-toolchain/ffmpeg6-rk3588/lib`）也写进 rpath。板子上没有这个路径，虽然不会出错，但看着让人疑惑。加上以后：

```bash
aarch64-buildroot-linux-gnu-readelf -d cmake-build-rk3588-debug/MppPlayer | grep RPATH
#  (RPATH)  Library rpath: [/userdata/ffmpeg6/lib:/userdata/mpp_build/lib]
```

## 6. 推到板子

```bash
./push_ffmpeg6_to_board.sh   # run_on_board_with_mac.sh、install_app_on_board_with_mac.sh 会自动调用
```

- 只推 8 个 `libxxx.so.<主版本号>`（soname）。本地它们是软链接，`adb push` 推的是软链接指向的文件内容，板子上得到的是普通文件，名字正好是程序要找的 soname。`libavcodec.so`（不带版本号的，编译链接时用）和 `libavcodec.so.60.3.100` 不用推
- 先比较本地和板子上 `libavcodec.so.60` 的 md5，一样就跳过，所以每次运行都调用也不浪费时间
- 放 `/userdata`：重启不会清空（`/tmp` 会）
- 板子的 adbd 不把命令的退出码传回 Mac（day8 实测），所以脚本让板子 `echo` 出 md5，再在 Mac 上比较文字

## 7. 验证

| 检查 | 命令 | 结果 |
|---|---|---|
| 库是 aarch64 的 | `file .../libavcodec.so.60.3.100` | ✅ ARM aarch64，带 debug_info（2026-11-19） |
| 依赖只有 libc / libm / libz | `readelf -d` 看 NEEDED | ✅（2026-11-19） |
| MppPlayer 链接的是 6.0 | `readelf -d MppPlayer` 看 NEEDED | ✅ `libavcodec.so.60`、`libavformat.so.60`……（2026-11-19） |
| MppPlayer 的 rpath | `readelf -d MppPlayer` 看 RPATH | ✅ 只有 `/userdata/ffmpeg6/lib:/userdata/mpp_build/lib`（2026-11-19） |
| 没有板子 glibc 2.37 里没有的符号 | 脚本最后自动 `objdump -T ... \| grep GLIBC_2.38` | ✅ 加了 `glibc237_fmod_compat.h` 以后没有（2026-11-19） |
| 板子上能加载 | 板子上 `/tmp/CLion/run/MppPlayer -version` | ✅ 打印 `MppPlayer version 6.0.2`、`libavutil 58. 2.100`（2026-11-19） |

## 8. 换电脑 / 重新编

1. 装好正点原子工具链、OrbStack 的 rk-dev（见 `rk_code/RK3588开发流程_Mac+Ubuntu.md`）
2. 有 FFmpeg-6.0 源码仓库；路径不一样就改 `build_ffmpeg6_for_board.sh` 开头的 `FFMPEG_SOURCE_REPO`
3. `./build_ffmpeg6_for_board.sh`
4. 把 `build-aarch64/config.h`、`config_components.h` 拷到 `src/fftools/`（见 5.2）。忘了拷也能编过，只是 `-version` 打印的 configuration 还是旧参数
5. 改了 configure 参数要从头编：`rm -rf /Users/dev/rk-toolchain/ffmpeg6-src/build-aarch64` 再跑脚本

## 9. 踩过的坑

| 现象 | 原因 | 办法 |
|---|---|---|
| 板子上运行报 `/lib/libm.so.6: version 'GLIBC_2.38' not found (required by /userdata/ffmpeg6/lib/libavutil.so.58)` | 板子 rootfs 是 glibc **2.37**，工具链 sysroot 是 2.38。glibc 2.38 给 `fmod`、`fmodf` 加了新的符号版本 `@GLIBC_2.38`，链接时默认绑到最新的。sysroot 的 libm 里老版本 `@GLIBC_2.17` 也在（`objdump -T` 看到两个） | [glibc237_fmod_compat.h](../glibc237_fmod_compat.h) 里用 `__asm__(".symver fmod,fmod@GLIBC_2.17")` 把引用绑到老版本，`--extra-cflags="-include ..."` 加给每个文件；脚本最后检查不能再有 `GLIBC_2.38`。工具链目录里的 `glibc237_compat.h` 管的是 `sscanf`/`strtol` 那一类，管不到 `fmod` |
| out-of-tree configure 报 `Out of tree builds are impossible with config.h in source dir` | 原源码目录给 Mac 编过，里面有 `config.h` | `git archive` 导出干净副本（4.1） |
| rk-dev 里没有 `pkg-config` | rk-dev 只装了编 C 的基本工具 | `--disable-autodetect` + 只 `--enable-zlib`，zlib 靠 `--sysroot` 找头文件和库，不需要 pkg-config |
| CMake 找不到 `${FFMPEG_PREFIX}/lib` 里的库 | 工具链文件让 `find_library` 只在 sysroot 里找 | `NO_CMAKE_FIND_ROOT_PATH`（5.1） |
| `readelf` 看到 rpath 里有 Mac 路径 | CMake 自动加了构建时的 rpath | `SKIP_BUILD_RPATH ON`（5.3） |
