# decode_get_frame 拿到的几种帧

> 一句话：`decode_get_frame` 交给你的 `MppFrame` **不一定是一帧画面**。`outputFrame != nullptr` 只说明"拿到了一个 MppFrame"，它到底是"格式通知"、"能用的图像"、"坏图像"还是"结束信号"，要靠它身上的几个标志来分。

---

## 1. 为什么会这样：MppFrame 是解码器唯一的"出口"

解码器往外交东西只有一个接口：`decode_get_frame`。除了解好的图像，它还要告诉你别的事情，比如"视频的宽高我知道了，请给我准备内存"、"这帧有错"、"码流结束了"。MPP 没有为这些事情另开接口，而是**全部塞进 MppFrame，打上不同的标志**。

官方《MPP 开发参考》3.1.2 节（`rk_code/external/mpp/doc/Rockchip_Developer_Guide_MPP_CN.md` 第 352 行）原话：

> MPP解码输出的图像是通过MppFrame结构来描述的，同时**MppFrame结构也是MPP实例输出信息的管道**，图像的错误信息，以及变宽高信息（info change）也是带在MppFrame结构进行输出的。

可以把 `decode_get_frame` 想成一个**快递柜**：取出来的都是同一种盒子（MppFrame），但盒子上贴的标签不一样。有的盒子里是货（图像），有的只装了一张通知单（info change），有的写着"最后一件"（eos）。拿到盒子要先看标签，再决定怎么处理。

---

## 2. 四个标志，分出四种情况

| 标志 | 读取函数 | 为 1 时表示 | 有没有图像 | 拿到后要做什么 |
|---|---|---|---|---|
| `info_change` | `mpp_frame_get_info_change` | **格式通知**：解码器解析完 SPS，知道了宽高、stride、需要多大内存（`buf_size`）；或者码流中途变了分辨率、位深 | ❌ `buffer` 为 NULL | 建内存池 → `SET_EXT_BUF_GROUP` → **`SET_INFO_CHANGE_READY`**。不能写文件，不计数 |
| `errinfo` | `mpp_frame_get_errinfo` | 这帧**解码出错**，画面内容有问题 | 有，但内容不可信 | 跳过（官方："可以做丢弃处理"） |
| `discard` | `mpp_frame_get_discard` | 这帧的**参考帧关系不满足**（比如从中间开始解、丢了参考帧），应该丢弃，不显示 | 有，但不该用 | 跳过 |
| `eos` | `mpp_frame_get_eos` | 这是**最后一个** MppFrame，解码器不会再输出了 | **可能有，也可能没有**（见第 4 节） | 处理完这一帧后退出取帧循环 |

`eos` 和前三个**不互斥**：它可能贴在最后一个正常图像上，也可能单独贴在一个空帧上。所以 `eos` 要**单独判断**，不能放进 `if / else if` 链里。

官方文档第 227–231 行对这几个字段的定义：

| 字段 | 官方说明 |
|---|---|
| `eos` | 表示图像的结束标志（End Of Stream） |
| `errinfo` | 表示图像的错误标志，是否图像内有解码错误 |
| `discard` | 表示图像的丢弃标志，如果图像解码时的参考关系不满足要求，则这帧图像会被标记为需要丢弃，不被显示 |
| `info_change` | 如果为真，表示当前MppFrame是一个**用于标记码流信息变化的描述结构**，说明了新的宽高，stride，以及图像格式。可能的原因有：图像序列宽高变化；图像序列格式变化，如8bit变为10bit。一旦info_change产生，需要重新分析解码器使用的内存池 |

---

## 3. 实测：aaa.264 一共拿到 1804 个 MppFrame

在板子上写了一个探针程序，解完整个 `aaa.264`（不写文件），把每个 MppFrame 按标志分类：

```
#1    info_change=1 buffer=NULL 1920x1080 stride 1920x1088 buf_size 4177920
#1804 eos=1 info_change=0 errinfo=0 discard=0 buffer=有
total=1804 info_change=1 image=1803 err_or_discard=0 no_buffer=0
```

| 第几个 MppFrame | 是什么 | 个数 |
|---|---|---|
| #1 | **info change**（没有 buffer，只带格式信息） | 1 |
| #2 ～ #1804 | 正常图像（有 buffer） | **1803**，和 `aaa.264` 的帧数（39 I + 1764 P）一致 |
| #1804 | 最后一个图像，**同时**带着 `eos=1` | （包含在上面 1803 里） |
| — | `errinfo` / `discard` 帧 | 0（码流完整，从头开始解） |

所以对这个文件来说：**取到的第一个 MppFrame 一定不是图像**，而是 info change；不处理它，解码器就停在那里，后面 1803 帧一帧都出不来。

---

## 4. 源码依据

### 4.1 每个输出帧都会被设置 info_change 标志

`rk_code/external/mpp/mpp/codec/mpp_dec.cpp` 第 316 行，解码器每往外交一帧，都会执行：

```cpp
mpp_frame_set_info_change(frame, change);   // change = 1：格式通知；change = 0：正常图像
```

字段本身的注释在 `mpp/base/inc/mpp_frame_impl.h` 第 128 行：

```c
 * info_change - set when buffer resized or frame infomation changed
```

### 4.2 EOS 可能贴在最后一帧上，也可能是一个空帧

同一个文件里，码流结束时有两条路：

- **还有图像没输出**（第 419 行）：输出最后一帧时顺手把 `eos` 置 1。`aaa.264` 走的就是这条路，所以实测里 #1804 既是图像又带 eos。

  ```cpp
  if (eos && mpp_slots_is_empty(frame_slots, QUEUE_DISPLAY))
      tmp.eos = 1;              // 显示队列里只剩这一帧了，给它打上 eos
  ```

- **没有图像可输出了**（第 285 行附近）：解码器**新建一个空的 MppFrame**，只为了把 `eos` 带出来。这个帧没有 buffer。

  ```cpp
  mpp_frame_init(&frame);
  fake_frame = 1;               // 假帧：没有像素，只用来通知"结束了"
  ```

这就是为什么写文件前要多判断一次 `mpp_frame_get_buffer(frame)`：换一个码流，最后拿到的可能是一个没有 buffer 的空 EOS 帧，直接拿它的 buffer 去读像素会出错。

---

## 5. 正确的处理顺序

```cpp
while (true) {
    CHECK(api->decode_get_frame(ctx, &frame));
    if (!frame) {
        break;                                  // ① 暂时没有输出：回外层继续送码流
    }

    if (mpp_frame_get_info_change(frame)) {     // ② 格式通知：先处理，否则解码器一直暂停
        // 读 width / height / stride / buf_size → 建内存池
        // SET_EXT_BUF_GROUP → SET_INFO_CHANGE_READY
    } else if (mpp_frame_get_errinfo(frame) || mpp_frame_get_discard(frame)) {
        // ③ 坏帧 / 该丢的帧：跳过
    } else if (mpp_frame_get_buffer(frame)) {
        // ④ 正常图像：写文件、计数
    }
    // （剩下的情况：没有 buffer 的空 EOS 帧，什么都不用做）

    bool eos = mpp_frame_get_eos(frame);        // ⑤ eos 单独判断：它可能贴在 ②③④ 任何一种上
    mpp_frame_deinit(&frame);                   // ⑥ 不管哪种，都要释放（图像帧会把 buffer 还给内存池）
    if (eos) {
        break;
    }
}
```

为什么是这个顺序：

| 步骤 | 为什么放在这里 |
|---|---|
| ① 先判 NULL | 非阻塞模式下，没有输出时返回 `MPP_OK` + `nullptr`。后面所有 `mpp_frame_get_xxx` 都不能拿 NULL 去调 |
| ② info change 放最前 | 它没有 buffer，不能当图像处理；而且不回 `SET_INFO_CHANGE_READY`，解码器就一直等 |
| ③ 先看错误再写文件 | 错帧也有 buffer，只判断 buffer 会把坏画面写进文件 |
| ④ 写之前判 buffer | 防住"没有 buffer 的空 EOS 帧" |
| ⑤ eos 在链外判断 | 它和 ②③④ 不互斥，放进 `else if` 里会漏掉 |
| ⑥ 每种都 deinit | 漏了图像帧的 deinit，buffer 不还给内存池；24 块用完后解码器就卡住 |

---

## 6. 常见错误

| 写法 | 后果 |
|---|---|
| 拿到 frame 就当图像写文件，不看 `info_change` | 第一个 frame 没有 buffer，写文件出错；更严重的是没调 `SET_INFO_CHANGE_READY`，**解码器停住，后面一帧都出不来** |
| 处理了 info change，但忘了 `SET_INFO_CHANGE_READY` | 同上，解码器一直暂停 |
| 不检查 `errinfo` / `discard` | 花屏、绿块之类的坏画面被写进文件 |
| 写文件前不判断 `buffer` | 遇到空 EOS 帧时访问 NULL buffer |
| 把 `eos` 放进 `else if` 链 | eos 贴在最后一个正常图像上时，走了"写文件"分支，eos 被漏掉，循环停不下来 |
| info change 帧不 `mpp_frame_deinit` | 内存泄漏（info change 帧也是一个要释放的 MppFrame） |

---

## 7. 对照我们的代码

`src/mpi_dec_most_light.cpp` 的取帧循环就是第 5 节的结构：

| 步骤 | 代码位置 |
|---|---|
| `decode_get_frame` | 第 131 行 |
| ① `if (!outputFrame) break;` | 第 133 行 |
| ② `mpp_frame_get_info_change` | 第 137 行（前面的 `outputFrame &&` 在有了 ① 之后已经多余） |
| ③ `errinfo` / `discard` | 第 154–157 行 |
| ④ `mpp_frame_get_buffer` → `write_nv12_frame` | 第 158–163 行 |
| ⑤ `mpp_frame_get_eos` | 第 171 行 |
| ⑥ `mpp_frame_deinit` | 第 172 行 |

相关文档：

- info change 之后建内存池、`buf_size = 4177920` 怎么来的：[具体写day2_h2642yuv的流程.md](../具体写day2_h2642yuv的流程.md) 的 M2
- MppFrame / MppPacket 的基本用法：[../../day1_yuv2h264/MppPacket和MppFrame的用法和区别.md](../../day1_yuv2h264/MppPacket和MppFrame的用法和区别.md)
