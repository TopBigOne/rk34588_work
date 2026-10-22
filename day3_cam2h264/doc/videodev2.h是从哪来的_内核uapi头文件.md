# `<linux/videodev2.h>` 是从哪来的：内核 uapi 头文件

> 问题：`#include <linux/videodev2.h>` 是正点原子独有的，还是 Linux 系统自带的？
> 答案：**Linux 系统自带的**（Linux 内核提供的标准头文件），不是正点原子、也不是 Rockchip 独有的。
> 本文的证据都是 2026-10-18 在工具链 sysroot 里查的。

---

## 1. 证据

### 1.1 文件开头写的就是 Linux 内核

工具链 sysroot 里的 `/usr/include/linux/videodev2.h` 开头：

```c
/* SPDX-License-Identifier: ((GPL-2.0+ WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 *  Video for Linux Two header file
 *  Copyright (C) 1999-2012 the contributors
 *  ...
 *	Author: Bill Dirks <bill@thedirks.org>
```

| 看到的 | 说明 |
|---|---|
| Video for Linux Two | 就是 V4L2 的全称 |
| Copyright 1999-2012 the contributors，Author: Bill Dirks | V4L2 最早的作者和贡献者，不是 Rockchip / 正点原子 |
| `Linux-syscall-note` | 内核里"给用户程序用的头文件"专门带的许可声明：用户程序 include 它不受 GPL 影响 |

### 1.2 文件里没有任何 Rockchip 的内容

```bash
grep -i -n "rockchip\|RKISP\|rk_" .../sysroot/usr/include/linux/videodev2.h     # 没有输出
```

### 1.3 来自 Linux 5.10.209 内核

同目录的 `version.h`：`LINUX_VERSION_CODE = 330449`，换算：`330449 = 5×65536 + 10×256 + 209` → **5.10.209**。

### 1.4 Mac 上没有

`/usr/include/linux/` 和 Xcode 的 SDK 里都没有 `linux/videodev2.h`：macOS 不是 Linux，没有 V4L2。
这就是**用 Mac 本地编译器编 day3 会报"找不到 linux/videodev2.h"**的原因，要用交叉编译工具链（CLion 的 `RK3588-Debug` profile）。

---

## 2. 它是怎么到 sysroot 里来的

```
Linux 内核源码   include/uapi/linux/videodev2.h      ← uapi = user API，专门给用户程序用的接口
       │   做工具链时执行 make headers_install，把 uapi 下的头文件导出来
       ▼
工具链 sysroot   /usr/include/linux/videodev2.h      ← #include <linux/videodev2.h> 找到的就是它
```

- 内核源码里的头文件分两类：
  - `include/linux/...`：**内核自己用的**，用户程序看不到
  - `include/uapi/linux/...`：**给用户程序用的**，定义系统调用和 ioctl 要用的结构体、常量。`videodev2.h`（V4L2）、`media.h`（Media Controller）都在这里
- 任何 Linux 系统都有它们：Ubuntu 上在 `/usr/include/linux/videodev2.h`，由 `linux-libc-dev` 包提供（未在本机实测）
- 所以我们写的 V4L2 代码，**换到 Ubuntu 电脑上接一个 USB 摄像头也能编、也能跑**，只是节点、格式不一样

---

## 3. Rockchip 自己加的头文件在哪

同一个目录下，有一批 `rk-` 开头的头文件，是 Rockchip 驱动的**私有扩展**（实测）：

```
rk-camera-module.h   rk-isp1-config.h   rk-isp2-config.h   rk-isp21-config.h
rk-isp3-config.h     rk-isp32-config.h  rk-ispp-config.h   rk-dma-heap.h ...
```

开头写着 `Copyright (C) 2018-2019 Rockchip Electronics Co., Ltd.`。里面是 ISP 3A 参数等 Rockchip 驱动特有的结构体，`rkaiq_3A_server` 这类程序会用。

| 头文件 | 谁的 | 我们用不用 |
|---|---|---|
| `linux/videodev2.h` | Linux 内核（标准 V4L2） | ✅ M2 代码 |
| `linux/media.h` | Linux 内核（标准 Media Controller） | ✅ CameraFinder |
| `linux/rk-*.h` | Rockchip 私有扩展 | ❌ 不用 |
| `rockchip/rk_mpi.h`、`rockchip/mpp_buffer.h` 等 | Rockchip MPP 库（不是内核的） | ✅ 编码器 |

**我们的摄像头代码只用标准接口，不依赖 Rockchip 私有头文件。**

---

## 4. 头文件版本和板子内核版本不一样，有关系吗

| | 版本 |
|---|---|
| sysroot 里的内核头文件 | 5.10.209（`version.h`） |
| 板子上运行的内核 | 5.10.160（10/15 `v4l2-ctl --all` 里的 Media version） |

没关系：**内核保证 uapi 向后兼容**——已经有的结构体、ioctl 编号不会改，只会加新的。头文件比内核新时，只要不用新版本才加的功能就行（真用了，ioctl 会返回 `EINVAL` / `ENOTTY`，不会出错乱）。

---

## 5. 怎么自己判断一个头文件是谁的

1. **看文件开头**的版权和许可：`Linux-syscall-note` → 内核 uapi；`Rockchip Electronics` → Rockchip
2. **看它在哪**：`<linux/xxx.h>` 一般是内核 uapi；`<rockchip/xxx.h>` 是 MPP；`<xxx.h>` 是 C 库或第三方库
3. **到别的 Linux 系统上找一找**：Ubuntu 上也有，就是标准的；只有 Rockchip 的 SDK 里有，就是私有的

---

## 6. 自测

- [ ] `linux/videodev2.h` 是谁提供的？凭什么这么判断？
- [ ] 内核源码里 `include/linux` 和 `include/uapi/linux` 有什么区别？
- [ ] 为什么 Mac 本地编译器找不到这个头文件？
- [ ] sysroot 里 `rk-` 开头的头文件是什么？我们的代码用不用？
- [ ] 头文件是 5.10.209、板子内核是 5.10.160，会不会有问题？
