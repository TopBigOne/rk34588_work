
* 建立demo code 软连
```shell
ln -s "/Users/dev/Documents/AV/openedv/ATK-DLRK3588开发板网盘A盘_基础资料/11、AV_demo_源码" demo_code
```
--

* 建立 mmp demo code 软连
```shell
ln -s "/Users/dev/Documents/AV/rk_work/rk_code/external/mpp" demo_code_mmp
```

* 建立doc 软连
```shell
ln -s "/Users/dev/Documents/AV/openedv/Tone_rk358/doc" rk3588_doc
```


-----
### day 01 学习计划

# Day 1 · MPP 硬件编码：`day1_yuv2h264`

> **一句话目标**：读一个 NV12 文件 → 用 RK3588 的 MPP 硬件编码成 H.264（改一个参数就能出 H.265）→ 在 Mac 上用 VLC 能流畅播放
> **总时间**：约 8～9 小时（准备 0.5h + 上午读代码 3h + 下午写代码 4h + 晚上实验 1.5h）
> **相关文件**：
> - 本工程：`/Users/dev/Documents/AV/rk_work/day1_yuv2h264`
> - demo 源码：`demo_code/mpp/`（软链接，指向 SDK 里的 MPP 官方源码）
> - MPP 笔记：`/Users/dev/Documents/AV/openedv/Tone_rk358/doc/MPP开发指南_学习笔记.md`
> - 总计划：`/Users/dev/Documents/AV/rk_work/国庆5天的学习计划/README.md`

---

## 0. 准备工作（约 30 分钟）

### 0.1 CLion 配置（参考 `day_01/rk_test_env/doc/1_在mac上搭建调试环境.md`）
- [ ] CMake：新建 `RK3588-Debug`，CMake options 填 `-DCMAKE_TOOLCHAIN_FILE=/Users/dev/rk-toolchain/rk3588-toolchain.cmake`；**删掉 Mac 本机的 `Debug` 配置**
- [ ] `demo_code` 目录：右键 → **Mark Directory as → Excluded**（不参与本工程编译；想在里面跳转代码的话，就用 CLion 单独打开 `demo_code/mpp`）
- [ ] Remote GDB Server（调试）：Target/Executable 选 `day1_yuv2h264`，GDB Server args 填 `:1234 /tmp/CLion/debug/day1_yuv2h264 <程序参数>`
- [ ] Shell Script（一键运行）：Script path 填 `/Users/dev/Documents/AV/rk_work/day1_yuv2h264/run_on_board_with_mac.sh`，**Script options 里填程序参数**（见 2.4 节）

### 0.2 测试素材
测试文件比较大，**放在 git 仓库外面**，避免误提交：`/Users/dev/Documents/AV/rk_test_data/`
```bash
mkdir -p /Users/dev/Documents/AV/rk_test_data && cd /Users/dev/Documents/AV/rk_test_data
cp "/Users/dev/Documents/AV/openedv/ATK-DLRK3588开发板网盘A盘_基础资料/01、程序源码/03、live555测试文件/aaa.264" .

FF=/usr/local/ffmpeg/4.4/bin
$FF/ffmpeg -i aaa.264 -frames:v 60 -pix_fmt nv12 -f rawvideo in_1080p_60f.nv12
ls -l in_1080p_60f.nv12          # 应该是 60 × 3,110,400 = 186,624,000 字节
```

### 0.3 板子上的工作目录
```bash
IP=$(sh /Users/dev/Documents/AV/rk_work/day1_yuv2h264/run_get_board_ip.sh | awk '/板子 IP/{print $NF}')
ssh root@$IP 'mkdir -p /userdata/av && df -h /userdata'
scp /Users/dev/Documents/AV/rk_test_data/in_1080p_60f.nv12 root@$IP:/userdata/av/
```

> 这 3 行都在 **Mac 终端**里执行，做了 3 件事：① 查到板子的 IP 存进变量 `IP`；② 远程登录板子，建好工作目录 `/userdata/av` 并看看剩余空间；③ 把测试素材拷到板子的这个目录里。

#### 0.3.1 第 1 行：拿到板子 IP，存进变量 `IP`
```bash
IP=$(sh .../run_get_board_ip.sh | awk '/板子 IP/{print $NF}')
```
从里往外拆成 4 块来看：

| 片段 | 含义 | 类比 Android / Kotlin |
|---|---|---|
| `sh .../run_get_board_ip.sh` | 运行脚本。脚本内部用 `adb shell ip -4 addr show wlan0`（再试 eth0/eth1）在板子上查 IP，然后打印两行文字 | 调用一个函数，它往 stdout 打印结果 |
| `\|`（管道） | 把左边命令**打印的内容**，当作右边命令的**输入** | `result.let { 下一步(it) }` |
| `awk '/板子 IP/{print $NF}'` | 逐行处理输入：只挑**包含"板子 IP"的那一行**，打印它的**最后一个字段**（`$NF`：NF = 字段数，`$NF` = 最后一个字段，字段默认按空格切分） | `lines.filter { "板子 IP" in it }.map { it.split(" ").last() }` |
| `IP=$( ... )` | `$( )` 叫"命令替换"：执行括号里的命令，把它打印的内容变成一个字符串，赋值给变量 `IP`。**注意 `=` 两边不能有空格** | `val IP = runCommand(...)` |

**demo：一步一步看效果**
```bash
# 1) 只运行脚本，看它打印了什么
$ sh /Users/dev/Documents/AV/rk_work/day1_yuv2h264/run_get_board_ip.sh
板子 IP  ： 192.168.1.105
登录板子 ： ssh root@192.168.1.105

# 2) 加上 awk 过滤：只剩包含"板子 IP"那一行的最后一个字段
$ sh .../run_get_board_ip.sh | awk '/板子 IP/{print $NF}'
192.168.1.105

# 3) 存进变量，再用 $IP 取出来
$ IP=$(sh .../run_get_board_ip.sh | awk '/板子 IP/{print $NF}')
$ echo $IP
192.168.1.105
```

**awk 小练习**（不需要板子，直接在 Mac 上就能跑）：
```bash
$ echo "板子 IP  ： 192.168.1.105" | awk '{print $1}'      # 第 1 个字段
板子
$ echo "板子 IP  ： 192.168.1.105" | awk '{print NF}'      # 一共几个字段
4
$ echo "板子 IP  ： 192.168.1.105" | awk '{print $NF}'     # 最后一个字段
192.168.1.105
$ printf "苹果 3\n香蕉 5\n" | awk '/香蕉/{print $2}'        # 只处理匹配"香蕉"的行
5
```

> ⚠️ 变量只在**当前终端窗口**里有效。新开一个终端、或者板子重启后 IP 变了，都要重新执行第 1 行。如果 `echo $IP` 输出是空的，说明脚本没找到板子（看脚本打印的报错：adb 没连上 / 板子没连 Wi-Fi）。
>
> 不想用变量的话，也可以直接运行 `./run_get_board_ip.sh` 看到 IP，然后手动把后面命令里的 `$IP` 换成真实 IP。

#### 0.3.2 第 2 行：远程在板子上建目录、看空间
```bash
ssh root@$IP 'mkdir -p /userdata/av && df -h /userdata'
```

| 片段 | 含义 |
|---|---|
| `ssh root@$IP` | 用 root 用户登录板子（`$IP` 会被替换成 `192.168.1.105`） |
| `'...'` | ssh 后面跟一个命令时：**登录 → 在板子上执行这个命令 → 自动退出**，不会停在板子的 shell 里。单引号保证整段命令原样发到板子上执行 |
| `mkdir -p /userdata/av` | 在板子上建目录。`-p`：父目录不存在就一起建；目录已经存在也**不报错**（所以这行可以反复执行） |
| `&&` | 前一个命令成功了，才执行后一个 |
| `df -h /userdata` | 查看 `/userdata` 所在分区的空间。`-h` = human readable，用 G/M 显示，而不是一长串字节数 |

为什么放 `/userdata`？它是板子上专门存用户数据的大分区（类比 Android 的 `/data`），空间大、重启不丢；`/tmp` 一般是内存盘，重启就清空，而且放不下几百 MB 的视频。

**demo：**
```bash
$ ssh root@$IP 'mkdir -p /userdata/av && df -h /userdata'
Filesystem      Size  Used Avail Use% Mounted on
/dev/mmcblk0p8   20G  1.2G   18G   7% /userdata
#                               ↑ 看 Avail（可用空间）：60 帧素材 178MB，晚上的 300 帧素材约 890MB，要够放

# 对比：不带命令的 ssh 会进入板子的交互 shell，要手动 exit 退出
$ ssh root@$IP
root@ATK-DLRK3588:~# ls /userdata/av
root@ATK-DLRK3588:~# exit
```
（`Size/Avail` 的数字以你的板子实际输出为准）

#### 0.3.3 第 3 行：把素材从 Mac 拷到板子上
```bash
scp /Users/dev/Documents/AV/rk_test_data/in_1080p_60f.nv12 root@$IP:/userdata/av/
```
`scp` = 基于 ssh 的文件拷贝，用法和 `cp 源 目标` 一样，只是远程的一端写成 `用户@IP:路径`：

| 方向 | 写法 |
|---|---|
| Mac → 板子（上传） | `scp 本地文件 root@$IP:/userdata/av/` |
| 板子 → Mac（下载，2.4 节会用到） | `scp root@$IP:/userdata/av/out.h264 /Users/dev/Documents/AV/rk_test_data/` |
| 拷贝整个目录 | 加 `-r`：`scp -r 本地目录 root@$IP:/userdata/av/` |

类比 Android：相当于 `adb push` / `adb pull`，只是走的是网络（ssh），不是 USB。

**demo：拷完以后确认一下**
```bash
$ scp /Users/dev/Documents/AV/rk_test_data/in_1080p_60f.nv12 root@$IP:/userdata/av/
in_1080p_60f.nv12                     100%  178MB  11.2MB/s   00:15

$ ssh root@$IP 'ls -l /userdata/av'
-rw-r--r-- 1 root root 186624000 Oct  2 10:00 in_1080p_60f.nv12
#                      ↑ 字节数必须和 Mac 上 ls -l 看到的一样（60 × 3,110,400 = 186,624,000），说明传完整了
```

#### 0.3.4 常见问题
| 现象 | 原因 / 解决办法 |
|---|---|
| `ssh: Could not resolve hostname root@:` 之类 | `$IP` 是空的，第 1 行没成功。先 `echo $IP` 检查 |
| 第一次 ssh 提示 `Are you sure you want to continue connecting (yes/no)?` | 第一次连这台机器的正常提示，输入 `yes` |
| `WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!` | 板子重刷了系统，指纹变了。执行 `ssh-keygen -R $IP` 删掉旧记录再连 |
| 每次都要输密码 | 可以配置免密登录：`ssh-copy-id root@$IP`（只需做一次） |
| `No space left on device` | 板子空间不够，`df -h /userdata` 看一下，删掉旧的输出文件 |

---

## 1. 上午：读代码（约 3 小时）

### 1.1 先复习笔记（30 分钟）
`MPP开发指南_学习笔记.md` 的这几节：

| 章节 | 内容 |
|---|---|
| 第 3 节 | MppCtx / MppApi / MppFrame / MppPacket / MppBuffer 分别是什么 |
| 第 4 节 | MppApi 的函数，以及 `mpp_create` → `mpp_init` → `mpp_destroy` 的生命周期 |
| ⭐ 第 6 节 | **编码主循环的代码框架**、stride、码率控制参数 |
| 第 8 节 | `-t`、`-f` 参数的数值（H.264 = 7，NV12 = 0） |

### 1.2 读 `demo_code/mpp/test/mpi_enc_test.c`（2 小时）
在 CLion 里用 `Cmd + L` 跳到指定行号。**按下面的顺序读**：

| 顺序 | 函数 / 位置 | 行号 | 要看懂什么 |
|:---:|---|:---:|---|
| 1 | `main` | 1077 | 入口：解析命令行参数 → 调用 `enc_test_multi` |
| 2 | `enc_test_multi` | 1020 | 按 `-s` 参数开多个线程（多实例），每个线程跑一个 `enc_test`。**只看一路就行** |
| 3 | ⭐ `enc_test` | 869 | **一路编码的总流程**，是整个文件的骨架，见下表 |
| 4 | ⭐ `test_ctx_init` | 146 | **stride 和缓冲区大小怎么算** |
| 5 | ⭐ `test_mpp_enc_cfg_setup` | 290 | **编码参数怎么设置** |
| 6 | ⭐ `test_mpp_run` | 547 | **编码主循环** |

**`enc_test`（第 869 行）的骨架**，你的程序就照这个顺序来写：

| 行号 | 调用 | 作用 |
|:---:|---|---|
| 883 | `test_ctx_init(info)` | 计算 stride、frame_size，打开输入和输出文件 |
| 889 | `mpp_buffer_group_get_internal(&buf_grp, MPP_BUFFER_TYPE_DRM \| MPP_BUFFER_FLAGS_CACHABLE)` | 创建 DRM 类型的内存池（**硬件能访问的内存**） |
| 895 | `mpp_buffer_get(buf_grp, &frm_buf, frame_size + header_size)` | 申请一块输入图像缓冲区 |
| 901、907 | `mpp_buffer_get(... pkt_buf / md_info ...)` | 输出码流缓冲区、运动信息缓冲区（**进阶用法，今天可以不用**） |
| 914 | `mpp_create(&ctx, &mpi)` | 创建编码器实例 |
| 923 | `mpi->control(ctx, MPP_SET_OUTPUT_TIMEOUT, &timeout)` | 让 `encode_get_packet` 阻塞等待结果 |
| 929 | `mpp_init(ctx, MPP_CTX_ENC, type)` | 初始化为编码器，指定 H.264 / H.265 |
| 935、941 | `mpp_enc_cfg_init` + `MPP_ENC_GET_CFG` | 拿到默认配置 |
| 947 | `test_mpp_enc_cfg_setup(info)` | 设置所有编码参数 |
| 954 | `test_mpp_run(info)` | 编码主循环 |
| 961 | `mpi->reset(ctx)` | 重置 |
| 976～1006 | `mpp_destroy`、`mpp_buffer_put`、`mpp_buffer_group_put` | 释放资源 |

**`test_ctx_init`（第 146 行）要看懂的**：
- 第 155～158 行：`hor_stride = MPP_ALIGN(width, 16)`，`ver_stride = MPP_ALIGN(height, 16)` → 1920×1080 得到 **1920 × 1088**
- 第 218～222 行：NV12（`MPP_FMT_YUV420SP`）的 `frame_size = MPP_ALIGN(hor_stride, 64) × MPP_ALIGN(ver_stride, 64) × 3 / 2`

**`test_mpp_enc_cfg_setup`（第 290 行）要看懂的**：

| 行号 | 设置的参数 |
|:---:|---|
| 320～324 | `prep:width/height/hor_stride/ver_stride/format`：**输入图像的描述，必须和内存里的实际排布一致** |
| 326 | `rc:mode`：码率控制模式 |
| 330～361 | 帧率、`rc:bps_target/max/min`（注意不同模式下 max/min 的默认算法） |
| 423 | `codec:type` |
| 434～444 | H.264 的 profile（100 = High）、level（40）、cabac |
| 486 | `rc:gop`：默认是**输出帧率 × 2** |
| 501 | `mpi->control(ctx, MPP_ENC_SET_CFG, cfg)`：参数真正生效 |
| 525 | `MPP_ENC_SET_HEADER_MODE`：每个 IDR 帧前面都带 SPS/PPS |

**`test_mpp_run`（第 547 行）要看懂的**：

| 行号 | 做什么 |
|:---:|---|
| 574～588 | `MPP_ENC_GET_HDR_SYNC`：**先取 SPS/PPS 头信息，写到文件开头** |
| 594 | `mpp_buffer_get_ptr(frm_buf)`：拿到硬件缓冲区的 CPU 地址 |
| 601 | `read_image(...)`：从文件读一帧到缓冲区（**按 stride**） |
| 616 | ⭐ `mpp_buffer_sync_end(frm_buf)`：**CPU 写完以后刷新缓存，硬件才能看到最新数据** |
| 640～658 | `mpp_frame_init` + 设置宽高、stride、格式 + `mpp_frame_set_buffer` + `mpp_frame_set_eos` |
| 660～757 | 设置 meta、OSD、ROI 等**进阶功能，今天跳过** |
| 758 | ⭐ `mpi->encode_put_frame(ctx, frame)`：送一帧进去（阻塞，要等硬件读完） |
| 768 | ⭐ `mpi->encode_get_packet(ctx, &packet)`：取出码流 |
| 778～790 | `mpp_packet_get_pos` / `mpp_packet_get_length` → `fwrite` 写到文件 |
| 802～803 | `is_partition` / `is_eoi`：slice 分片输出（今天不涉及，一帧就是一个 packet） |

### 1.3 读 `demo_code_mmp/utils/utils.c` 的 `read_image`（第 479 行，30 分钟）
源码：`rk_code/external/mpp/utils/utils.c`（`demo_code_mmp` 是它的软链接）

| 行号 | 内容 | 要看懂什么 |
|:---:|---|---|
| 484～486 | `buf_y = buf`<br>`buf_u = buf_y + hor_stride * ver_stride`<br>`buf_v = buf_u + hor_stride * ver_stride / 4` | 三个分量的起始地址。**UV 从第 `ver_stride`（1088）行开始，不是第 1080 行**。`buf_v` 只有 YUV420P（I420）用得到，NV12 用不到 |
| 488～541 | `if (MPP_FRAME_FMT_IS_FBC(fmt))` | FBC 压缩格式分支：**跳过** |
| 543 | `switch (fmt & MPP_FRAME_FMT_MASK)` | 按像素格式分别读 |
| ⭐ 544～564 | `case MPP_FMT_YUV420SP_VU:`<br>`case MPP_FMT_YUV420SP:` | **NV12 分支**（NV21 也走这里，两者只是 U/V 顺序相反，读法一样） |
| 546～552 | Y 分量：`for row in [0, height)`：`fread(buf_y + row * hor_stride, 1, width, fp)` | **每行只从文件读 `width` 个字节，但写到按 `hor_stride` 对齐的位置**；每行末尾 `hor_stride - width` 个字节是填充，不读 |
| 554～555 | `height = MPP_ALIGN(height, 2)`<br>`width = MPP_ALIGN(width, 2)` | 宽高是奇数时向上取偶，保证 UV 能按 2×2 采样 |
| 556～562 | UV 分量：`for row in [0, height/2)`：`fread(buf_u + row * hor_stride, 1, width, fp)` | UV 只有 `height/2` 行，但**每行仍然读 `width` 个字节**：NV12 的 U、V 交错存放（UVUV…），`width/2` 个 U 加上 `width/2` 个 V，正好是 `width` 个字节 |
| 548～551 / 558～561 | `if (read_size != width) { ret = MPP_NOK; goto err; }` | 读不满一行就返回 `MPP_NOK`，读到文件末尾时也走这里，调用方据此判断是否结束 |
| 565～592 | `case MPP_FMT_YUV420P:` | 对比着看：I420 的 U、V 是分开的两个平面，每行读 `width/2` 字节，行距 `hor_stride/2` |

### 1.4 读完应该能回答的问题
- [ ] 1920×1080 的 NV12，为什么 `ver_stride` 是 1088？UV 分量的起始地址在哪里？
- [ ] `frame_size` 为什么再按 64 对齐一次？
- [ ] 为什么输入缓冲区必须用 `mpp_buffer_get` 申请，不能用 `malloc`？
- [ ] `mpp_buffer_sync_end` 是做什么的？不调用会怎样？
- [ ] SPS/PPS 是在哪一步拿到的？`MPP_ENC_HEADER_MODE_EACH_IDR` 起什么作用？
- [ ] `MPP_SET_OUTPUT_TIMEOUT` 设成阻塞，和不设置有什么区别？
- [ ] 最后一帧是怎么告诉编码器"结束了"的？

---

## 2. 下午：写代码（约 4 小时）

### 2.1 建议的文件结构
```
day1_yuv2h264/
├── CMakeLists.txt          ← add_executable 里加上 src/mpp_encoder.cpp
├── main.cpp                ← 参数解析、读文件、主循环、统计
├── src/
│   ├── mpp_encoder.h       ← MppEncoder 类的声明
│   └── mpp_encoder.cpp     ← MppEncoder 类的实现
└── demo_code -> ...        ← 软链接（只读参考）
```

### 2.2 `MppEncoder` 类的设计（C++17，用 RAII 管理资源）
```cpp
struct EncoderConfig {
    int width = 1920, height = 1080;
    MppCodingType type = MPP_VIDEO_CodingAVC;    // H.265: MPP_VIDEO_CodingHEVC
    MppEncRcMode rc_mode = MPP_ENC_RC_MODE_CBR;
    int bps = 4'000'000;
    int fps = 30;
    int gop = 60;
};

class MppEncoder {
public:
    bool init(const EncoderConfig& cfg);                    // create → timeout → init → cfg → SET_CFG → header mode
    int  hor_stride() const;                                // 给外面按 stride 填数据用
    int  ver_stride() const;
    bool get_header(std::vector<uint8_t>& out);             // MPP_ENC_GET_HDR_SYNC
    bool encode(MppBuffer frame_buf, bool eos,
                std::vector<uint8_t>& out, bool& is_key);   // put_frame + get_packet
    ~MppEncoder();                                          // mpp_destroy、mpp_enc_cfg_deinit
private:
    MppCtx ctx_ = nullptr;
    MppApi* mpi_ = nullptr;
    MppEncCfg cfg_ = nullptr;
    int hor_stride_ = 0, ver_stride_ = 0;
};
```
> 输入缓冲区（MppBufferGroup + MppBuffer）可以放在 `main` 里管理，也可以再封装一个 `MppBufferPool` 类。Day 3 接摄像头时，输入来自摄像头的 DMA-BUF，所以**不要把"申请输入缓冲区"写死在编码器里面**。

### 2.3 分 6 个小里程碑，每一步都能单独验证
| # | 里程碑 | 怎么验证 |
|:---:|---|---|
| **M1** | `MppEncoder::init()`：create → init → 设置参数 → SET_CFG | 运行后打印 `encoder ready: 1920x1080 stride 1920x1088`，没有报错 |
| **M2** | `get_header()`：取 SPS/PPS 写入 `out.h264` | 用 `xxd out.h264 \| head -3` 看开头：`00 00 00 01 67`（SPS）……`00 00 00 01 68`（PPS） |
| **M3** | 申请 DRM 缓冲区 → **按 stride 逐行读 1 帧** → `sync_end` → 编码 1 帧 | `ffprobe -show_frames out_1f.h264` 能看到 1 帧，`pict_type=I` |
| **M4** | 循环读完 60 帧，最后一帧设置 EOS，取完所有 packet | Mac 上用 VLC 能播放 2 秒的画面，**没有花屏、没有绿条** |
| **M5** | 加命令行参数：`-i -o -w -h -t h264\|h265 -rc cbr\|vbr\|avbr -bps -g` | `-t h265` 输出的 `out.h265` 也能播放 |
| **M6** | 统计：总帧数、总用时、平均 fps、实际码率、I 帧数量 | 程序结束时打印一行汇总 |

**M3 的关键代码思路（stride 拷贝）**：
```
uint8_t* dst = (uint8_t*)mpp_buffer_get_ptr(frm_buf);
// Y：1080 行，每行从文件读 1920 字节，写到 dst + row * hor_stride
// UV：540 行，每行从文件读 1920 字节，写到 dst + hor_stride * ver_stride + row * hor_stride
mpp_buffer_sync_end(frm_buf);      // 缓冲区是 CACHABLE 的，CPU 写完一定要刷缓存
```

### 2.4 在板子上运行
```bash
# Mac 终端里执行（参数会原样传给板子上的程序）
./run_on_board_with_mac.sh -i /userdata/av/in_1080p_60f.nv12 -o /userdata/av/out.h264 -w 1920 -h 1080 -t h264

# 把结果拉回 Mac 查看
IP=<板子IP>
scp root@$IP:/userdata/av/out.h264 /Users/dev/Documents/AV/rk_test_data/
open -a VLC /Users/dev/Documents/AV/rk_test_data/out.h264
```
- CLion 一键运行：把 `-i ... -o ...` 这些参数填到 Shell Script 配置的 **Script options** 里
- CLion 调试：把参数加在 GDB Server args 的程序路径后面：`:1234 /tmp/CLion/debug/day1_yuv2h264 -i /userdata/av/in_1080p_60f.nv12 -o /userdata/av/out.h264`

---

## 3. 晚上：实验（约 1.5 小时）

功能调通以后，生成 300 帧（10 秒）的输入文件来做实验（约 890MB，先确认板子空间够用）：
```bash
cd /Users/dev/Documents/AV/rk_test_data
/usr/local/ffmpeg/4.4/bin/ffmpeg -i aaa.264 -frames:v 300 -pix_fmt nv12 -f rawvideo in_1080p.nv12
scp in_1080p.nv12 root@<板子IP>:/userdata/av/
```

### 3.1 实验记录表（边做边填）
| # | 实验 | 参数 | 文件大小 | 实际码率 | I 帧数 | 编码 fps | 主观画质 |
|:---:|---|---|---|---|---|---|---|
| 1 | 基准 | H.264 CBR 4Mbps gop=60 | | | | | |
| 2 | 码率模式 | H.264 **VBR** 4Mbps | | | | | |
| 3 | 码率模式 | H.264 **AVBR** 4Mbps | | | | | |
| 4 | GOP | gop = **30** | | | | | |
| 5 | GOP | gop = **300** | | | | | |
| 6 | 编码格式 | **H.265** CBR 4Mbps | | | | | |
| 7 | 低码率 | H.264 vs H.265，都是 **1Mbps** | | | | | |
| 8 | 官方工具 | `mpi_enc_test -w 1920 -h 1080 -t 7 -i /userdata/av/in_1080p.nv12 -o /userdata/av/ref.h264 -n 300` | | | | | |

查看码流信息的命令（在 Mac 上执行）：
```bash
FF=/usr/local/ffmpeg/4.4/bin
$FF/ffprobe -v error -show_entries format=bit_rate,duration -of default=nw=1 out.h264      # 码率
$FF/ffprobe -v error -show_frames out.h264 | grep pict_type | sort | uniq -c                # I/P 帧数量
```
板子上看 CPU 占用：编码时另开一个终端执行 `ssh root@<板子IP> top`，**CPU 占用应该很低**，因为编码是硬件在做。

### 3.2 观察要点
- 实验 7：低码率下，H.265 的画质应该明显比 H.264 好
- 实验 5：gop = 300 时文件更小，但播放器中途拖动进度条会变慢（I 帧少了）
- 实验 1～3：输入是同一段画面，CBR 的码率曲线最平，VBR/AVBR 会随画面复杂度上下浮动

---

## 4. 验收清单
- [ ] `out.h264` 在 Mac 上用 VLC 能流畅播放，画面没有花屏、没有绿边，颜色正常
- [ ] ffprobe 能看到正确的分辨率（1920x1080）、帧率和 GOP
- [ ] `-t h265` 输出的文件也能正常播放
- [ ] 能用断点在 `encode_put_frame` 停下来，查看 `frame` 的宽、高、stride
- [ ] 实验记录表填完了
- [ ] **能用自己的话讲清楚**：stride、SPS/PPS、GOP、I/P 帧、CBR/VBR/AVBR

---

## 5. 容易踩的坑

| 现象 | 原因 | 解决办法 |
|---|---|---|
| CMake 报 `Cannot specify link libraries for target "xxx"` | `target_link_libraries` 里写的目标名和 `add_executable` 里的不一致 | 保持一致（本工程是 `day1_yuv2h264`） |
| `encode_put_frame` 返回错误 | 输入缓冲区是 `malloc` 出来的 | 必须用 `mpp_buffer_get` 从 DRM 内存池申请 |
| 画面斜着错位 | 整块 `fread` 进缓冲区，没有按 stride 逐行拷贝 | 参考 `read_image` 逐行拷贝 |
| 画面底部有绿条，或者颜色不对 | UV 的起始位置算成了 `width × height`，应该是 `hor_stride × ver_stride` | `buf_uv = buf_y + hor_stride * ver_stride` |
| 画面花屏、有残影，像是上一帧的数据 | 写完数据**没有调用 `mpp_buffer_sync_end`**，硬件读到的是缓存刷新之前的旧数据 | CPU 写完后调用 `mpp_buffer_sync_end(frm_buf)` |
| VLC 打不开，或者开头几秒是黑的 | 文件开头没有 SPS/PPS | 先写 `GET_HDR_SYNC` 拿到的头信息，或者设置 `EACH_IDR` |
| 程序最后卡住不退出 | 最后一帧没有设置 EOS，或者没有把剩下的 packet 取完 | `mpp_frame_set_eos(frame, 1)`，循环取到 `mpp_packet_get_eos(packet)` 为真 |
| 内存一直涨 | `mpp_packet_deinit` / `mpp_frame_deinit` 没有调用 | 每个 frame 和 packet 用完都要 deinit |
| 改了 `prep:*` 参数不生效 | 改完没有再调用 `MPP_ENC_SET_CFG` | 每次修改参数都要 SET_CFG |
| 找不到板子 | 板子 IP 变了 | 运行 `./run_get_board_ip.sh` 查看，调试配置里的 IP 要手动改 |

---

## 6. 今天的笔记（写在这里，或者 `doc/` 目录下）
- **今天学会了什么**（用自己的话写）：
  - stride 就是……
  - SPS/PPS 是……
  - GOP 是……
- **踩了哪些坑，怎么解决的**：
- **实验数据和结论**：
- **明天（Day 2 解码）要带着的问题**：
