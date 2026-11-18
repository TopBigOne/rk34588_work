# M3 RTSP 播放
* 板子上跑 live555MediaServer 把转码结果"发"出去，Mac 上用 ffplay 通过 `rtsp://` 地址看

> 本文没有 C++ 代码，只有一个运行脚本。2026-11-17 在板子上实测过：live555 是板子自带的、端口 554、`.265` 能播而 `.h265` 返回 404、必须走 TCP。数字和日志都是当天抓的。

---

## 1. 干了什么

把转码程序输出的 H.265 文件 `out_720p.h265` 留在板子上，用一个现成的 RTSP 服务器（live555MediaServer）把它通过网络协议发出去；Mac 上的播放器打开 `rtsp://localhost:8554/out_720p.265`，就能看到画面。

这是第一次让码流**走网络协议**到另一台机器上播放。前面都是"文件进、文件出"，要看结果得把文件拷到 Mac 上。

数据流：

```
板子                                                          Mac
/userdata/av/out_720p.265 ──→ live555MediaServer ──(端口 554)
（指向 out_720p.h265 的链接）   读文件、切成 RTP 包          │
                                                            │ adb forward（USB 线）
                                                            ↓
                                     ffplay ←── localhost:8554
```

| 步骤 | 谁做        | 做什么                                                                  |
|------|-------------|-------------------------------------------------------------------------|
| 1    | 脚本第 2 步 | 在板子上建 `out_720p.265` 链接，让 live555 认出这是 H.265               |
| 2    | 脚本第 3 步 | 停掉上次没停的 live555，空出端口                                        |
| 3    | 脚本第 4 步 | `adb forward tcp:8554 tcp:554`：Mac 的 8554 端口通过 USB 接到板子的 554 |
| 4    | 脚本第 5 步 | 在 `/userdata/av` 目录里启动 live555MediaServer                         |
| 5    | Mac 终端 2  | `ffplay -rtsp_transport tcp rtsp://localhost:8554/out_720p.265`         |

## 2. 输入

|        | 内容                                                                                                                                              |
|--------|---------------------------------------------------------------------------------------------------------------------------------------------------|
| 文件   | 板子上 `/userdata/av/out_720p.h265`：转码程序的输出，H.265 裸码流，1280×720，1803 帧，15028188 字节                                               |
| 要求   | **文件开头必须有码流头（VPS、SPS、PPS）**。live555 从这里读出参数集，放进 SDP 告诉播放器（见 4.3 节）。转码程序已经在文件开头写了 83 字节的码流头 |
| 也能播 | 同目录的 `out_720p_m2.h265`（同样要建 `.265` 链接）、`aaa.264`（H.264，1080p，扩展名本来就是 `.264`）                                             |

## 3. 输出

没有文件。Mac 上的播放器窗口里出现画面。

2026-11-17 实测：用 ffprobe 当播放器，通过 `adb forward` 连 live555，读到的流信息：

| URL                                   | 结果                                                                  |
|---------------------------------------|-----------------------------------------------------------------------|
| `rtsp://localhost:8554/out_720p.265`  | `codec_name=hevc`，`1280×720`，`r_frame_rate=30/1`                    |
| `rtsp://localhost:8554/aaa.264`       | `codec_name=h264`，`1920×1080`，`r_frame_rate=10000/333`（约 30 fps） |
| `rtsp://localhost:8554/out_720p.h265` | `404 Stream Not Found`：live555 不认识 `.h265` 这个扩展名             |

## 4. 技术点

### 4.1 RTSP、RTP、点播

- **RTSP**（Real Time Streaming Protocol，实时流协议）：播放器和服务器之间"说话"用的协议，只管**控制命令**："我要看哪个""开始播""停"。它本身不装视频数据。
- **RTP**（Real-time Transport Protocol，实时传输协议）：真正装视频数据的包。一帧 H.265 切成若干个 RTP 包发出去，每个包的包头带序号（发现丢包、乱序）和时间戳（播放器据此控制播放速度）。
- **点播**：服务器上存着文件，播放器连上来才从头开始发；和"直播"（摄像头一直在出画面，谁连上来就从当前画面开始看）相对。

打个比方：RTSP 是你打电话给录像店"我要看第 3 号片子，现在开始放"；RTP 是录像店顺着管子一格一格送过来的画面。

播放器和服务器之间的对话：

| 命令     | 播放器问什么     | 服务器答什么                                              |
|----------|------------------|-----------------------------------------------------------|
| OPTIONS  | 你支持哪些命令？ | DESCRIBE、SETUP、PLAY……                                   |
| DESCRIBE | 这个文件是什么？ | 一段 SDP（4.3 节）：编码格式、RTP 负载类型号、VPS/SPS/PPS |
| SETUP    | 数据怎么传给我？ | 同意的传输方式（这里是 TCP，4.2 节），以及一个会话号      |
| PLAY     | 开始播           | 好，然后开始发 RTP 包                                     |

每条命令的原始文本在 [m4流程.md](m4流程.md) 里用 Wireshark 抓包看。

### 4.2 为什么必须"RTP over TCP"

按项目规定，Mac 和板子只走 USB（adb），不用板子 IP。办法是 `adb forward`：

```
Mac 上的播放器 → localhost:8554 ──USB──→ 板子上 live555 的 554 端口
```

`adb forward tcp:8554 tcp:554`：Mac 上谁连 `localhost:8554`，adb 就通过 USB 线把这条 TCP 连接接到板子的 554 端口上。**它只能转 TCP**。

RTP 默认走 UDP：SETUP 时播放器告诉服务器"往我的某某 UDP 端口发"，服务器就往播放器的 IP 发 UDP 包。这条路在 USB 转发上是不通的，包根本到不了 Mac。

解决办法是在 SETUP 时要求 **RTP over TCP**（也叫 interleaved，交错传输）：RTP 包不另开 UDP 端口，而是夹在 RTSP 的那条 TCP 连接里一起传，而这条连接正好是 adb 转发的那条。

- ffplay / ffprobe：加参数 `-rtsp_transport tcp`
- VLC：命令行加 `--rtsp-tcp`，或者在偏好设置里勾选"RTP over RTSP (TCP)"（VLC 这条没实测）

2026-11-17 实测：同一个 URL，加 `-rtsp_transport tcp` 很快就读到了流信息（没计时）；不加的时候，ffprobe 等了 90 秒以上还没有任何输出，最后被手动停掉。

### 4.3 SDP 里的 VPS/SPS/PPS：码流头为什么必须在文件开头

DESCRIBE 的回答是一段 SDP（Session Description Protocol，会话描述协议），告诉播放器"这路流是什么"。2026-11-17 实测 live555 对 `out_720p.265` 回的 SDP（摘出和视频有关的几行）：

```
m=video 0 RTP/AVP 96
a=rtpmap:96 H265/90000
a=fmtp:96 profile-space=0;profile-id=1;tier-flag=0;level-id=120;...;sprop-vps=QAEMAf//AWAAAAMAgAAAAwAAAwB4vAk=;sprop-sps=QgEBAWAAAAMAgAAAAwAAAwB4oAKAgC0WNvkkymAQAAADABAAAAMB4IA=;sprop-pps=RAHA88BNkA==
a=control:track1
```

逐行看：

| 行                                      | 字面意思                              | 在这里起什么作用                                          |
|-----------------------------------------|---------------------------------------|-----------------------------------------------------------|
| `m=video 0 RTP/AVP 96`                  | 一路视频，用 RTP 传，负载类型号 96    | 96 是动态类型号（96～127 自己定），下一行说明 96 代表什么 |
| `a=rtpmap:96 H265/90000`                | 类型 96 是 H.265，时间戳时钟 90000 Hz | RTP 包头里的时间戳每秒加 90000；30 fps 时相邻两帧差 3000  |
| `profile-id=1`                          | H.265 的 Main profile                 | 和编码器设置的 profile 一致                               |
| `level-id=120`                          | level 4（level × 30 = 120）           |                                                           |
| `sprop-vps` / `sprop-sps` / `sprop-pps` | 三个参数集，base64 编码               | 播放器**还没收到第一个 RTP 包**就能初始化解码器           |

`sprop-*` 是从哪来的？live555 打开文件时，从码流里找出 VPS、SPS、PPS 这三个 NAL 单元，转成 base64 填进来。可以自己验证：`sprop-vps` 的开头 `QAEM` 解码成字节是 `40 01 0C`，`40 01` 正是 H.265 VPS 的 NAL 头（类型 32）。板子上 `out_720p.h265` 的前 8 个字节是 `00 00 00 01 40 01 0c 01`：起始码 `00 00 00 01` 后面就是同样的 `40 01 0c 01`（2026-11-17 实测）。

所以转码程序必须把码流头写进文件开头：

- 转码程序调用了 MPP 的 `MPP_ENC_GET_HDR_SYNC`，单独取出码流头。MPP 认为码流头已经交给调用方了，第一个 IDR 帧前面就不再带 VPS/SPS/PPS。调用方不把它写进文件，文件开头就没有参数集
- 没有参数集，live555 填不出 `sprop-*`，播放器第一批帧也解不出来

以后自己做 RTSP 服务器（`cam_rtsp`）时，SDP 里的 `sprop-*` 就要自己填：用的正是 `MPP_ENC_GET_HDR_SYNC` 取出来的那段码流头。

### 4.4 live555 怎么知道文件是 H.265：看扩展名

live555MediaServer 启动时打印的帮助里写着（2026-11-17 板子上实测，摘录）：

```
Each file's type is inferred from its name suffix:
	".264" => a H.264 Video Elementary Stream file
	".265" => a H.265 Video Elementary Stream file
```

它只看文件名最后一个点后面的部分。`out_720p.h265` 的扩展名是 `.h265`，不在表里，所以返回 404。办法是建一个 `.265` 结尾的符号链接：

```bash
ln -sf out_720p.h265 out_720p.265   # -s 符号链接（相当于快捷方式），-f 已经有同名的就覆盖
```

它会把**启动时的当前目录**里的文件当作点播源，URL 里的路径就是文件名，所以脚本要先 `cd /userdata/av` 再启动。

### 4.5 端口：板子 554，Mac 8554

- **板子上 554**：RTSP 的标准端口。live555 先试 554，拿不到才换 8554（学习计划原文这么写，没看源码）。adb shell 是 root（实测 `uid=0`），能拿到 554。判断方法：启动日志里的地址是 `rtsp://192.168.1.63/<filename>`，**没写端口**，就是默认的 554；拿不到时会打印成 `rtsp://...:8554/<filename>`
- **Mac 上 8554**：Mac 上普通用户不能监听 1024 以下的端口，所以 `adb forward` 在 Mac 这边用 8554
- 启动日志里的 `192.168.1.63` 是板子当前的 IP，**不要用**：IP 会变，按项目约定只走 USB

### 4.6 live555MediaServer 从哪来

板子系统自带（2026-11-17 实测）：

- 程序：`/usr/bin/live555MediaServer`，版本 1.09（LIVE555 库 2021.05.03）
- 库：`/usr/lib` 下的 `libliveMedia.so.94`、`libgroupsock.so.30`、`libBasicUsageEnvironment.so.1`、`libUsageEnvironment.so.3`

不用交叉编译，也不用从 A 盘传。

## 5. 实际业务场景

- **网络摄像头（IPC）**：几乎所有网络摄像头都提供 `rtsp://IP/...` 地址，录像机（NVR）、VLC、安防平台都用 RTSP 拉流。以后的 `cam_rtsp` 就是把这里的"文件"换成"摄像头实时编出来的码流"
- **NVR / 视频平台点播录像**：录好的文件按需拉流回放，就是本文的点播模式
- **转码服务器**：拉一路 1080p H.264，转成 720p H.265 再用 RTSP 发出去，省带宽。day5_transcoder 加上本文，就是这个流程的"文件版"

## 6. 脚本：`run_rtsp_server_via_usb.sh`

放在 `day5_transcoder/` 下，`chmod +x`。做 6 件事：确认板子连上 → 建 `.265` 链接 → 停掉旧的 live555 → `adb forward` → 列出能播的文件、打印 ffplay 命令 → 启动 live555。

```sh
#!/bin/sh
# M3：在板子上启动 live555MediaServer（RTSP 点播服务器），并用 adb forward 把它的端口转到 Mac。
# 全程走 USB（adb），不用板子 IP。
#
# 用法：./run_rtsp_server_via_usb.sh
# 然后在 Mac 的另一个终端播放：ffplay -rtsp_transport tcp rtsp://localhost:8554/out_720p.265
#
# 停止：Ctrl+C 只能停掉 Mac 这边的 adb，板子上的 live555 可能还在跑。
#      下次运行本脚本时会先把旧的停掉；也可以手动 adb shell "killall live555MediaServer"

MEDIA_DIR=/userdata/av   # live555 把"启动时的当前目录"里的文件当点播源，转码结果就在这里
LOCAL_RTSP_PORT=8554     # Mac 这边的端口：Mac 普通用户不能监听 1024 以下的端口
BOARD_RTSP_PORT=554      # 板子上 live555 监听的端口：RTSP 标准端口，adb shell 是 root 能拿到（2026-11-17 实测）

# 第 1 步 确认板子连上了
# adb get-state：有设备时打印 device 并返回 0，没有时返回非 0
if ! adb get-state >/dev/null 2>&1; then
    echo "adb 没找到板子：检查板子开机了吗、OTG 线（TypeC0 口）插好了吗，执行 adb devices 看看" >&2
    exit 1
fi

# 第 2 步 建 .265 结尾的链接：live555 按扩展名认格式，认 .265 不认 .h265（.h265 会返回 404）
# 所有 adb shell 都加 < /dev/null：不让 adb 读终端输入，否则一次粘贴多行命令时，后面几行会被 adb 吃掉
adb shell "cd $MEDIA_DIR && ln -sf out_720p.h265 out_720p.265 && ln -sf out_720p_m2.h265 out_720p_m2.265" < /dev/null || exit 1

# 第 3 步 把上次没停掉的 live555 停掉，否则 554 端口被占着，新的起不来
# 没有在跑的 live555 时 killall 会报错，丢掉输出、不当成失败
adb shell "killall live555MediaServer" < /dev/null >/dev/null 2>&1

# 第 4 步 端口转发：Mac 的 localhost:8554 ←USB→ 板子的 554
# adb forward 只转 TCP，所以播放器要用"RTP over TCP"（RTSP 命令和视频数据走同一条 TCP 连接）
adb forward tcp:$LOCAL_RTSP_PORT tcp:$BOARD_RTSP_PORT >/dev/null || exit 1
# 变量写成 ${变量名}：Mac 的 /bin/sh（bash 3.2）在 UTF-8 环境下，$MEDIA_DIR 后面紧跟中文"）"时，会把"）"的前几个字节当成变量名的一部分，打印出空值加乱码（2026-11-17 实测）
echo "端口转发：Mac localhost:${LOCAL_RTSP_PORT} → 板子 ${BOARD_RTSP_PORT}"

# 第 5 步 列出能播的文件，直接打印成完整的 ffplay 命令，复制到另一个终端就能用
# live555 启动日志里的 rtsp://<板子IP>/<filename> 只是占位符：它不会列出目录里有哪些文件，IP 也不能用（只走 USB）
# 能播的条件：在 MEDIA_DIR 里，并且扩展名是 live555 认识的 .264 / .265（.h264、.h265 不认，会 404）
# ls 2>/dev/null：某种扩展名一个文件都没有时，ls 会报"No such file"，丢掉不看
# tr -d '\r'：adb shell 输出的每行末尾可能带 \r（回车符），不去掉的话拼出来的地址末尾多一个看不见的字符
PLAYABLE_FILE_NAMES=$(adb shell "cd $MEDIA_DIR && ls *.264 *.265 2>/dev/null" < /dev/null | tr -d '\r')
echo ""
echo "=== 能播的文件（在另一个终端执行下面的命令）==="
for fileName in $PLAYABLE_FILE_NAMES; do
    echo "    ffplay -rtsp_transport tcp rtsp://localhost:${LOCAL_RTSP_PORT}/${fileName}"
done
echo ""

# 第 6 步 在 MEDIA_DIR 里启动 live555（板子自带，在 /usr/bin 下）；前台运行，日志直接显示在这个终端
# exec：用 adb 这个进程替换掉脚本本身，Ctrl+C 直接作用在 adb 上
echo "=== 板子上启动 live555MediaServer（目录 ${MEDIA_DIR}）==="
exec adb shell "cd $MEDIA_DIR && live555MediaServer" < /dev/null
```

## 7. 运行步骤

### 7.1 准备

板子上要有转码结果。没有的话先跑一次转码：`./run_on_board_with_mac.sh m2`（或 `m1`）。

```bash
adb shell ls -l /userdata/av/out_720p.h265   # 应该是 15028188 字节
```

### 7.2 终端 1：启动服务器

```bash
cd day5_transcoder
./run_rtsp_server_via_usb.sh
```

看到 `LIVE555 Media Server` 和 `rtsp://192.168.1.63/<filename>`（没写端口 = 554）就是起来了。这个终端一直开着。

### 7.3 终端 2：Mac 上播放
* ffplay：出问题时日志清楚，先用它
```bash
ffplay -rtsp_transport tcp rtsp://localhost:8554/out_720p.265
```
* 只看流信息、不弹窗口（2026-11-17 实测用的就是这条）
```bash
ffprobe -v error -rtsp_transport tcp -show_entries stream=codec_name,width,height,r_frame_rate -of compact rtsp://localhost:8554/out_720p.265
```
* VLC（没实测）
```bash
/Applications/VLC.app/Contents/MacOS/VLC --rtsp-tcp rtsp://localhost:8554/out_720p.265
```
### 7.4 停止

关掉播放器；终端 1 按 Ctrl+C；再执行：

```bash
adb shell "killall live555MediaServer"   # 确认板子上的也停了
adb forward --remove tcp:8554            # 去掉端口转发
```

## 8. 验收

- [ ] ffplay 窗口里有画面，1280×720，画面正常、没有花屏
- [ ] ffplay 的日志里能看到 `hevc`、`1280x720`
- [ ] 换成 `aaa.264`（`rtsp://localhost:8554/aaa.264`）也能播，说明 H.264、H.265 都能点播
- [ ] 故意打开 `out_720p.h265`，看到 404，理解为什么要建 `.265` 链接

## 9. 可能遇到的问题

| 现象                                                      | 原因                                                                                | 怎么办                                                            |
|-----------------------------------------------------------|-------------------------------------------------------------------------------------|-------------------------------------------------------------------|
| `404 Stream Not Found`                                    | 文件名不对，或扩展名 live555 不认识（实测 `.h265` 就是 404）                        | 用 `.265` 结尾的链接（4.4 节）                                    |
| 连上了但一直没画面、命令卡住不动                          | 播放器在用 UDP 收 RTP，包到不了 Mac（实测 90 秒以上没输出）                         | 加 `-rtsp_transport tcp` / `--rtsp-tcp`（4.2 节）                 |
| `Connection refused`                                      | live555 没起来，或者 `adb forward` 没做                                             | 看终端 1 的日志；`adb forward --list` 看有没有 `tcp:8554 tcp:554` |
| ffprobe 加了 `-timeout` 以后一直卡住                      | ffmpeg 4.4 的 RTSP 里 `-timeout` 的意思是"自己当服务器、等别人连进来"，不是连接超时 | 要设连接超时用 `-stimeout 10000000`（单位微秒，这里是 10 秒）     |
| 启动 live555 时报端口被占用（推断，没实测过具体报错文字） | 上次的 live555 还在跑，占着 554                                                     | `adb shell "killall live555MediaServer"` 后重跑脚本               |
