# v4l2-ctl 和 media-ctl 用法

> 板子 `/usr/bin` 下的两个摄像头调试命令。**不用写代码，就能查摄像头、设格式、抓帧、看整条链路。**
> 本文所有输出都是 2026-10-17 在板子上实测的（IMX415 已装在 MIPI CSI3 接口）。
> 相关文档：[guide/04_摄像头和V4L2知识.md](../guide/04_摄像头和V4L2知识.md)（V4L2、plane、mainpath 这些概念）

---

## 0. 一句话

| 命令 | 一句话 | 操作的对象 |
|---|---|---|
| **`v4l2-ctl`** | **在命令行里调用 V4L2 ioctl**：查能力、设格式、抓帧 | 一个 `/dev/videoN` 或 `/dev/v4l-subdevN` |
| **`media-ctl`** | **看摄像头整条链路的接线图**：有哪些部件、谁连着谁、每一站是什么格式 | 一个 `/dev/mediaN` |

两个命令来自同一个软件包 **v4l-utils**（板子上是 1.22.1 版，实测 `--version`），是 Linux 摄像头调试的"官方工具套装"。名字里的 `ctl` 是 control（控制）的缩写。

**和我们代码的关系**：我们 M2 里写的每一个 `ioctl(fd, VIDIOC_xxx, ...)`，v4l2-ctl 都有一个参数能做同样的事。所以它可以当成"标准答案"：v4l2-ctl 能抓到画面，说明摄像头没问题；自己的程序抓不到，就是代码的问题。

### 最常用的 5 条（先记住这几个）

```bash
v4l2-ctl -d 22 --info                     # 这个节点是谁
v4l2-ctl -d 22 --get-fmt-video            # 现在设的是什么格式
v4l2-ctl -d 22 --set-fmt-video=width=1920,height=1080,pixelformat=NV12 \
         --stream-mmap=4 --stream-skip=30 --stream-count=1 --stream-to=/userdata/av/cam_1frame.nv12   # 抓一帧
media-ctl -d /dev/media2 -e rkisp_mainpath # 按名字查节点号
media-ctl -d /dev/media1 -p               # 看整条链路
```

---

## 1. v4l2-ctl

### 1.1 用 `-d` 指定设备

```bash
v4l2-ctl -d /dev/video22 ...     # 完整写法
v4l2-ctl -d 22 ...               # 简写：数字开头就当成 /dev/video22
v4l2-ctl -d /dev/v4l-subdev2 ... # 也可以操作子设备（比如摄像头本身，见 1.6）
```

不写 `-d` 默认是 `/dev/video0`。在这块板子上 video0 是 CIF 的节点，不是我们要的，**所以每条命令都要写 `-d`**。

### 1.2 `--list-devices`：列出所有设备

```bash
v4l2-ctl --list-devices
```

实测输出（截取）：
```
Failed to open /dev/video0: No such device     ← 这一行可以忽略（那组 CIF 没接东西）
rkcif (platform:rkcif-mipi-lvds2):             ← 一组：bus info 相同的节点放在一起
	/dev/video11
	...
	/dev/video21
rkisp_mainpath (platform:rkisp0-vir0):          ← ISP0 这一组
	/dev/video22                                ← ISP0 的 mainpath ← 摄像头装在 CSI3 时用它
	/dev/video23
```

⚠️ 它是**按 bus info 分组**的，每组的标题用的是组里第一个节点的名字。所以看起来有点乱：标题写着 `rkisp_mainpath`，下面的 video23 其实是 selfpath。**要看每个节点到底是谁，用下面两种方法更准：**

```bash
v4l2-ctl -d 23 --info | grep "Card type"                          # 问单个节点
for d in /sys/class/video4linux/video*; do echo "$(basename $d) $(cat $d/name)"; done   # 一次列出所有节点的名字
```

### 1.3 `--info`：问"你是谁、能干什么"（= `VIDIOC_QUERYCAP`）

```bash
v4l2-ctl -d 22 --info
```

实测输出（截取），逐行解释：
```
Driver Info:
	Driver name      : rkisp_v6              ← 哪个驱动：Rockchip ISP
	Card type        : rkisp_mainpath        ← 节点名字：ISP 的主出口
	Bus info         : platform:rkisp0-vir0  ← 属于哪个 ISP：rkisp0 = ISP0
	Capabilities     : 0x84201000
		Video Capture Multiplanar            ← 能采集，而且是多平面写法（guide/04 第 4 节）
		Streaming                            ← 能用 QBUF / DQBUF 取帧
Entity Info:
	Name             : rkisp_mainpath
	  Link ...: from remote pad ... of entity 'rkisp-isp-subdev' ...: Data, Enabled   ← 它的数据来自 ISP
```

我们代码里 ① 那一步（`VIDIOC_QUERYCAP`）读的就是这些。

### 1.4 格式：列出、读取、试探、设置

| 想干什么 | 命令 | 对应 ioctl |
|---|---|---|
| 支持哪些格式 | `--list-formats` | `VIDIOC_ENUM_FMT` |
| 支持哪些格式 + 尺寸范围 | `--list-formats-ext` | `VIDIOC_ENUM_FMT` + `ENUM_FRAMESIZES` |
| 现在设的是什么 | `--get-fmt-video` | `VIDIOC_G_FMT` |
| **只问不改**：如果设成这样，驱动会怎么排布 | `--try-fmt-video=...` | `VIDIOC_TRY_FMT` |
| 真的设置 | `--set-fmt-video=...` | `VIDIOC_S_FMT` |

**`--list-formats-ext`**（实测，截取）：
```
	[4]: 'NV12' (Y/CbCr 4:2:0)
		Size: Stepwise 32x32 - 3840x2160 with step 8/8     ← 宽高 32～3840 / 32～2160，按 8 的倍数
```
> 没装摄像头时这里显示的是 `32x32 - 800x600`（10/15 实测），装上以后变成 3840x2160。所以**没装摄像头时查到的尺寸不可信**。

**`--get-fmt-video`**（实测，相机 App 刚用过，所以是 1280x720）：
```
Format Video Capture Multiplanar:
	Width/Height      : 1280/720
	Pixel Format      : 'NV12' (Y/CbCr 4:2:0)
	Number of planes  : 1                  ← NV12 只有 1 个 plane
	Plane 0           :
	   Bytes per Line : 1280               ← 每行字节数 = hor_stride
	   Size Image     : 1382400            ← 一帧多少字节 = 1280 × 720 × 3/2
```

**`--try-fmt-video`**（实测）：
```bash
v4l2-ctl -d 22 --try-fmt-video=width=3840,height=2160,pixelformat=NV12
#	Width/Height      : 3840/2160
#	   Bytes per Line : 3840
#	   Size Image     : 12441600         ← 4K 一帧 12MB
```

`try` 和 `set` 的区别：`try` 只是问一问，**不改设备的设置**，别的程序正在用这个节点时也能问；`set` 会真的改掉。写代码前想知道"1080p 的 bytesperline 是多少"，用 `try` 最安全。

### 1.5 抓帧：`--stream-mmap` 这一组

```bash
v4l2-ctl -d 22 --set-fmt-video=width=1920,height=1080,pixelformat=NV12 \
    --stream-mmap=4 --stream-skip=30 --stream-count=1 --stream-to=/userdata/av/cam_1frame.nv12
```

| 参数 | 意思 | 对应我们 M2 代码里的 |
|---|---|---|
| `--set-fmt-video=...` | 先设格式 | ② S_FMT |
| `--stream-mmap=4` | 用 MMAP 方式、要 4 块缓冲区，开始采集 | ③ REQBUFS(4) → ④ QUERYBUF + mmap → ⑤ QBUF × 4 → STREAMON |
| `--stream-skip=30` | 先丢掉 30 帧（等自动曝光稳定） | ⑥ 跳过前 30 帧 |
| `--stream-count=1` | 一共存 1 帧（不含跳过的） | ⑦ DQBUF 一次 |
| `--stream-to=文件` | 存到哪 | ⑦ fwrite |

**这一行命令做的事，就是 M2 那 200 多行代码做的事。**

实测结果：
```
<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< 29.98 fps      ← 每取到一帧打一个 <，最后打印帧率
-rw-r--r-- 1 root root 3110400 /userdata/av/cam_1frame.nv12    ← 正好 1920 × 1080 × 3/2
```

拉回 Mac 看画面：
```bash
cd day3_cam2h264
KEEP=1 ./run_move_file_from_board_via_usb.sh /userdata/av/cam_1frame.nv12
ffplay -f rawvideo -pixel_format nv12 -video_size 1920x1080 result_from_board/cam_1frame.nv12
```
实测画面颜色正常、亮度正常 → **说明 UV 从第 1080 行开始（`verStride` 填 1080 是对的），3A 也在工作**。

**测帧率**（不存文件，只看速度）：
```bash
v4l2-ctl -d 22 --stream-mmap=4 --stream-count=150     # 实测：29.99 / 29.99 / 30.00 fps
```

### 1.6 控制项：`--list-ctrls`、`-C`（读）、`-c`（改）

控制项就是设备的"旋钮"：曝光、增益、翻转等。**大部分旋钮在摄像头本身（子设备）上，不在 video 节点上**：

```bash
v4l2-ctl -d 22 --list-ctrls                  # video 节点：实测只有一个只读的 pixel_rate
v4l2-ctl -d /dev/v4l-subdev2 --list-ctrls    # 摄像头本身（subdev2 是怎么查到的，见 2.3）
```

摄像头的控制项（实测，截取）：
```
                       exposure (int)    : min=4 max=2242 ... value=2025      ← 曝光
                horizontal_flip (bool)   : default=0 value=0                  ← 水平翻转
                  vertical_flip (bool)   : default=0 value=0                  ← 垂直翻转
                  analogue_gain (int)    : min=0 max=240 ... value=77         ← 模拟增益
```

```bash
v4l2-ctl -d /dev/v4l-subdev2 -C exposure,analogue_gain    # 读几个旋钮的当前值（未实测）
v4l2-ctl -d /dev/v4l-subdev2 -c vertical_flip=1           # 改一个旋钮（未实测）
```

⚠️ **曝光和增益不要手动改**：3A 服务每一帧都在调它们（`dmesg` 里那一长串 `set analog gain`、`set exposure` 就是），手动改了马上会被 3A 改回去。翻转（flip）可以做实验：画面倒了，除了在编码器里旋转（M5 的 `-r`），也可以让摄像头直接翻转输出（未实测）。

### 1.7 参数和 ioctl 对照表

`v4l2-ctl --help` 里，很多参数后面直接写着对应的 ioctl，比如 `-D, --info  show driver info [VIDIOC_QUERYCAP]`。整理一下我们用到的：

| v4l2-ctl 参数 | ioctl | 我们代码里的哪一步 |
|---|---|---|
| `--info` | `VIDIOC_QUERYCAP` | ① 查能力 |
| `--list-formats` | `VIDIOC_ENUM_FMT` | （camera_source.c 第 175 行打印格式） |
| `--set-fmt-video` / `--get-fmt-video` | `VIDIOC_S_FMT` / `VIDIOC_G_FMT` | ② 设格式、读回 |
| `--try-fmt-video` | `VIDIOC_TRY_FMT` | （写代码前查排布） |
| `--stream-mmap=N` | `REQBUFS` + `QUERYBUF` + `mmap` + `QBUF` + `STREAMON` | ③～⑤ |
| `--stream-count` / `--stream-to` | `DQBUF` → 写文件 → `QBUF` | ⑦ |
| `--list-ctrls` / `-C` / `-c` | `VIDIOC_QUERY_EXT_CTRL` / `G_EXT_CTRLS` / `S_EXT_CTRLS` | （我们没用） |

想看全部参数：`v4l2-ctl --help-all`；只看某一类：`--help-streaming`、`--help-subdev` 等。

### 1.8 在这块板子上不能用的：`-z`

`--help` 里说可以用 `-z 总线信息 -d 实体名字` 按名字找节点，但实测不行：
```bash
v4l2-ctl -z platform:rkisp0-vir0 -d rkisp_mainpath --info
# Cannot open device rkisp_mainpath, exiting.
```
原因：`-z` 要按 media 设备的 bus info 去找，而这块板子上 media 设备的 bus info 是空的（`media-ctl -p` 开头那几行能看到 `bus info` 后面什么都没有）。**按名字找节点，用 `media-ctl -e`（2.3 节）**。

---

## 2. media-ctl

### 2.1 先回忆三个词

| 词 | 意思 | 比方 |
|---|---|---|
| **entity（实体）** | 链路里的一个部件：摄像头、CSI、ISP、出口…… | 接线图上的一个盒子 |
| **pad（端口）** | 部件的输入口（Sink）或输出口（Source） | 盒子上的插孔 |
| **link（连线）** | 一个输出口连到另一个输入口；`[ENABLED]` 表示接通 | 插孔之间的线 |

板子上有 4 张接线图（实测）：

| 文件 | 是哪张图 | 里面有什么 |
|---|---|---|
| `/dev/media0` | 第一组 CIF（摄像头数据接收） | 现在没有摄像头（它对应哪个丝印接口还没测） |
| **`/dev/media1`** | 第二组 CIF（`rkcif-mipi-lvds2`） | **摄像头 imx415 就在这张图里** |
| **`/dev/media2`** | ISP0 | **mainpath = video22** |
| `/dev/media3` | ISP1 | mainpath = video31（现在没有摄像头接到它） |

用 `-d` 指定看哪一张，不写默认 `/dev/media0`。

### 2.2 `-p`：打印整张接线图

```bash
media-ctl -d /dev/media1 -p
```

开头是这张图的基本信息（实测）：
```
driver          rkcif
model           rkcif-mipi-lvds2
bus info                             ← 空的，所以 v4l2-ctl -z 用不了（1.8 节）
```

然后是所有实体（实测，只列名字）：
```bash
media-ctl -d /dev/media1 -p | grep "^- entity"
```
```
- entity 1: stream_cif_mipi_id0 ...        ← CIF 的几路输出（video11～21）
  ...
- entity 45: rockchip-mipi-csi2 ...         ← MIPI CSI-2 接收
- entity 58: rockchip-csi2-dphy0 ...        ← MIPI 物理层（电信号 → 数据）
- entity 63: m00_b_imx415 3-001a ...        ← ⭐ 摄像头：I2C 总线 3、地址 0x1a
```

摄像头那一段（实测）：
```
- entity 63: m00_b_imx415 3-001a (1 pad, 1 link)
             type V4L2 subdev subtype Sensor        ← 子设备，类型是 Sensor
             device node name /dev/v4l-subdev2      ← 它的设备文件（1.6 节改旋钮用的就是它）
	pad0: Source
		[fmt:SGBRG10_1X10/3864x2192@10000/300000 field:none   ← 输出 RAW10，3864x2192，每帧 10000/300000 秒 = 30fps
		 crop.bounds:(12,16)/3840x2160]
		-> "rockchip-csi2-dphy0":0 [ENABLED]          ← 连到 MIPI 物理层
```

**判断摄像头在不在、接在哪张图里，就靠 `-p`：**
```bash
for m in /dev/media*; do media-ctl -d $m -p 2>/dev/null | grep -q imx415 && echo "摄像头在 $m"; done
# 实测：摄像头在 /dev/media1
```

### 2.3 `-e`：按名字查设备文件

```bash
media-ctl -d /dev/media2 -e rkisp_mainpath           # 实测：/dev/video22
media-ctl -d /dev/media1 -e "m00_b_imx415 3-001a"    # 实测：/dev/v4l-subdev2（名字带空格要加引号）
```

**这是"不记号码、按名字找"的方法**：号码会随系统版本变，名字不会。注意要先知道它在哪张图里（`-d` 写对），在别的图里查会报 `Entity 'rkisp_mainpath' not found`。

### 2.4 `--get-v4l2`：看某个端口上的格式

想看数据在每一站变成了什么样，就查那一站端口上的格式：

```bash
media-ctl -d /dev/media1 --get-v4l2 '"m00_b_imx415 3-001a":0'   # 摄像头输出口
media-ctl -d /dev/media2 --get-v4l2 '"rkisp-isp-subdev":0'      # ISP 输入口
media-ctl -d /dev/media2 --get-v4l2 '"rkisp-isp-subdev":2'      # ISP 输出口
```
写法：`'"实体名字":端口号'`，外面单引号、里面双引号（名字里可能有空格）。

实测结果，串起来就是一帧画面的变化过程：

| 哪一站 | 格式 | 说明 |
|---|---|---|
| 摄像头输出 | `SGBRG10` 3864×2192 | RAW Bayer（G B R G 排列），每像素 10 bit，30fps |
| ISP 输入 | `SGBRG10` 3840×2160 | 裁掉边缘一点（`crop.bounds:(12,16)`），还是 RAW |
| ISP 输出 | `YUYV8` 3840×2160 | ⭐ **已经变成 YUV 了**：去马赛克、3A 都在这一步 |
| mainpath（video22） | NV12 1920×1080 | 我们 `--set-fmt-video` 要的格式，mainpath 负责缩放、转成 NV12 |

### 2.5 完整链路（实测拼出来的）

把 `media1` 和 `media2` 的输出连起来，摄像头装在 MIPI CSI3 时，一帧画面走的路是：

```
media1（CIF 那张图）                                                   media2（ISP0 那张图）
m00_b_imx415 3-001a ──→ rockchip-csi2-dphy0 ──→ rockchip-mipi-csi2 ──→ rkcif-mipi-lvds2 ──→ rkisp-isp-subdev ──→ rkisp_mainpath
 （摄像头，subdev2）      （MIPI 物理层）          （CSI-2 接收）           （CIF）              （ISP0）              （/dev/video22）
   RAW10 3864x2192                                                                         RAW → YUV 3840x2160     NV12 1920x1080
```

media2 里 ISP 输入口的连线是 `<- "rkcif-mipi-lvds2":0 [ENABLED]`（实测），这就是两张图接起来的地方。

### 2.6 不要乱用的参数

| 参数 | 干什么 | 为什么别用 |
|---|---|---|
| `-l` | 改连线（接通 / 断开） | 链路由驱动和 3A 服务配好了，改了可能出不了图 |
| `-V` | 改某个端口的格式 | 同上 |
| `-r` | 把所有连线都断开 | 同上，而且会影响正在运行的 3A |
| `-i` | 交互式改连线 | 同上 |

**只用 `-p`、`-e`、`--get-v4l2` 这三个"只看不改"的就够了。**

---

## 3. 实战：装好摄像头以后，用这两个命令查一遍

这是 2026-10-17 装好摄像头后实际走的流程：

| 步 | 命令 | 实测结果 |
|:---:|---|---|
| 1 | `dmesg \| grep imx415` | `imx415 3-001a: Detected imx415 id 0000e0` → 驱动认到了，在 I2C 3 上 |
| 2 | `for m in /dev/media*; do media-ctl -d $m -p \| grep -q imx415 && echo $m; done` | `/dev/media1` |
| 3 | `media-ctl -d /dev/media2 -p`，看 ISP 输入口有没有连着 CIF | ISP0（media2）连着 `rkcif-mipi-lvds2`，输入是 3840x2160 RAW；ISP1（media3）还是 800x600 默认值 → 摄像头接到了 **ISP0** |
| 4 | `media-ctl -d /dev/media2 -e rkisp_mainpath` | `/dev/video22` |
| 5 | `v4l2-ctl -d 22 --info` | `Card type: rkisp_mainpath`，`Bus info: platform:rkisp0-vir0` ✅ |
| 6 | `v4l2-ctl -d 22 --set-fmt-video=... --stream-mmap=4 --stream-skip=30 --stream-count=1 --stream-to=...` | 3,110,400 字节，30 fps，Mac 上画面正常 |
| 7 | `pidof rkaiq_3A_server` | 有 pid → 3A 在运行 |

结论（实测）：

| 丝印 | I2C | 接到哪个 ISP | 用哪个节点 |
|---|---|---|---|
| **MIPI CSI3**（J20） | I2C3 | **ISP0** | **`/dev/video22`** |
| MIPI CSI1（J18） | I2C7 | （没测） | （没测） |

---

## 4. 常见报错

| 报错 | 原因 | 怎么办 |
|---|---|---|
| `VIDIOC_REQBUFS returned -1 (Device or resource busy)` | **别的程序正在用这个节点**（10/17 实测：正点原子自带的相机 App 占着 video22） | 查是谁：`for p in /proc/[0-9]*; do ls -l $p/fd 2>/dev/null \| grep -q /dev/video22 && cat $p/cmdline; echo; done`，把它关掉 |
| `VIDIOC_STREAMON returned -1 (Operation not permitted)` | 这个 ISP 上**没有接摄像头**（10/17 实测：video31 就是这样） | 用 2.2 节查摄像头接到了哪个 ISP，换成对应的节点 |
| `Failed to open /dev/video0: No such device` | `--list-devices` 时打开某个没接东西的 CIF 节点失败 | 可以忽略 |
| `Entity 'xxx' not found` | `media-ctl -e` 的 `-d` 指错了图 | 先用 `-p` 找到这个名字在哪张图里 |
| `Cannot open device rkisp_mainpath` | 用了 `v4l2-ctl -z`，这块板子的 media bus info 是空的 | 改用 `media-ctl -e` |

---

## 5. 自测

- [ ] v4l2-ctl 和 media-ctl 分别操作什么文件？各自用来干什么？
- [ ] 不写 `-d` 时它们默认操作哪个设备？为什么在这块板子上每次都要写？
- [ ] `--try-fmt-video` 和 `--set-fmt-video` 有什么区别？什么时候用 try？
- [ ] `--stream-mmap=4 --stream-skip=30 --stream-count=1` 对应 M2 代码里的哪几步？
- [ ] 曝光、翻转这些旋钮在哪个设备文件上？为什么不要手动改曝光？
- [ ] 怎么用 media-ctl 判断摄像头在不在、接到了哪个 ISP？
- [ ] `media-ctl -e` 解决了什么问题？为什么比记 `/dev/video22` 这个号码可靠？
- [ ] 一帧画面从摄像头到 video22，格式依次变成了什么？
- [ ] 遇到 `Device or resource busy`，怎么找出是谁占着设备？
