# void* 能转成任何对象指针：为什么 `mpp_packet_get_pos` 的返回值能转成 `uint8_t*`

> 出处：[src/encoder/mpp_encoder.cpp](../src/encoder/mpp_encoder.cpp) 里 `MppEncoder::encode()` 的最后几行。
> 相关文档：[unique_ptr自定义删除器_RAII句柄.md](unique_ptr自定义删除器_RAII句柄.md)、[MppPacket和MppFrame的用法和区别.md](../MppPacket和MppFrame的用法和区别.md)

---

## 1. 问题从哪来

```cpp
const size_t packetLength = mpp_packet_get_length(outputPacket);
...
// 把码流拷出来：outputPacketPtr 在函数返回时就释放了，调用方拿到的是一份独立的副本
const auto* pos = static_cast<const uint8_t*>(mpp_packet_get_pos(outputPacket));
encodedPacket.data.assign(pos, pos + packetLength);
```

`mpp_packet_get_pos` 的原型是：


```c
void* mpp_packet_get_pos(const MppPacket packet);   // 返回 void*
```

疑问：
1. 返回的是 `void*`，为什么能转成 `uint8_t*`？
2. 是不是只要是 `void*`，就都能转成 `uint8_t*`？
3. 这个转换的依据是什么？

答案分两层：**编译器为什么允许**（语法），和**转完之后为什么能放心用**（正确性）。

> ⭐ **先记住转换的目的**：`void*` 只是一个地址，**不能做指针运算**，也不能解引用；
> 转成 `uint8_t*` 之后，**可以做指针运算**（`pos + packetLength` = 往后 packetLength 个字节），也能按字节读。
> 下一行 `assign(pos, pos + packetLength)` 要算码流结尾地址，所以必须先转。详见第 4 节。

---

## 2. 第一层：语法上，任何 `void*` 都能转成任何对象指针

C++ 标准规定：**`void*` 可以用 `static_cast` 转成指向任意对象类型的指针 `T*`。**

```cpp
void* p = ...;
static_cast<uint8_t*>(p);    // ✅
static_cast<int*>(p);        // ✅
static_cast<MyStruct*>(p);   // ✅ 全都能编译通过
```

**编译器只检查语法，不检查内存里实际放的是什么。** `void*` 本来就是"只有地址、没有类型"的指针，编译器也无从知道那里是什么 —— 转成什么类型，由写代码的人负责。

### 为什么会有这条规则

`void*` 是 C 语言的"通用指针"：`malloc`、`memcpy`、`fread`，以及 MPP 这种 C 接口，都用它传"一块我不关心类型的内存"，由使用者自己转回具体类型。

| 语言 | `void*` → `T*` 的写法 |
|---|---|
| C | 自动转换，不用写 cast：`uint8_t* p = malloc(100);` |
| C++ | 必须显式写出来：`auto* p = static_cast<uint8_t*>(malloc(100));`（C++ 更严格，要你明确表态） |

### `void*` 是专门开放的转换通道

| 转换 | `static_cast` 能不能编译 |
|---|---|
| `void*` → `uint8_t*` | ✅ |
| `uint8_t*` → `void*` | ✅（任何对象指针都能隐式转成 `void*`，不写 cast 也行） |
| `int*` → `uint8_t*`（两种具体类型之间直接转） | ❌ 编译报错，必须用 `reinterpret_cast` |
| `const void*` → `uint8_t*`（去掉 const） | ❌ 编译报错，`static_cast` 不能去掉 const |

还有一条配套规则：**`T*` → `void*` → `T*` 转一圈，得到的还是原来那个地址**（往返保证）。

---

## 3. 第二层：能编译 ≠ 用起来正确，正确性靠两点保证

### 依据 1：MPP 的接口约定 —— `pos` 指向的就是码流字节

`mpp_packet_get_pos` 返回的是**有效码流的起始地址**（`pos`，见 `mpp_packet.h` 里四个属性的说明），而 H.264 / H.265 码流本来就是一串字节（`00 00 00 01 67 64 ...`）。

MPP 内部（`mpp/base/inc/mpp_packet_impl.h` 58 行）也只是用 `void *pos;` 存了这个地址，没有类型 —— 它就是一块字节内存。所以**按字节去看，和实际内容一致**。

反过来，如果转成 `int*` 去读，照样能编译，但含义就错了：码流不是一串 int。

### 依据 2：C++ 对"字节类型"的特殊豁免 —— 这是 `uint8_t` 特殊的地方

一般情况下，**用错误的类型去读一块内存是未定义行为**（比如把一块 `float` 当成 `int` 读，这叫违反"严格别名规则 strict aliasing"）。

但标准对 `char`、`unsigned char`、`std::byte` 开了特例：**它们可以用来读取任何对象的底层字节** —— 因为任何对象在内存里最终都是一串字节。

而 `uint8_t` 在这个工具链里就是 `unsigned char`：

```c
// sysroot/usr/include/bits/types.h 38 行
typedef unsigned char __uint8_t;
// sysroot/usr/include/bits/stdint-uintn.h 24 行
typedef __uint8_t uint8_t;
```

所以：

| 转成 | 编译 | 读取是否保证合法 |
|---|---|---|
| `uint8_t*`（字节） | ✅ | ✅ **永远可以**：任何内存都能按字节看 |
| `int*`、`float*`、结构体指针 | ✅ | ⚠️ 只有那里**真的放着这个类型的对象**才行，还要满足**对齐**要求（比如 `int` 地址要是 4 的倍数），否则是未定义行为 |

这就是为什么处理原始数据（码流、图像、文件内容、网络包）时，大家都用 `uint8_t*`。

---

## 4. 为什么非转不可：`void*` 不能做两件事

```cpp
void* p = mpp_packet_get_pos(outputPacket);
p + packetLength;   // ❌ 编译错误：不知道每个元素多大，没法"往后数 packetLength 个"
*p;                 // ❌ 编译错误：不知道该按什么类型读
```

后面一句要算 **`pos + packetLength`（码流结尾的地址）**，必须先告诉编译器"这里是一个一个字节"。转成 `uint8_t*` 之后，这两件事就都能做了：

```cpp
const auto* pos = static_cast<const uint8_t*>(mpp_packet_get_pos(outputPacket));
pos + packetLength;   // ✅ 指针运算：uint8_t 是 1 字节，往后数 74674 个元素 = 往后 74674 个字节 → 码流结尾
pos[0];               // ✅ 解引用：读出第 0 个字节（H.264 码流开头是 0x00）
pos[4];               // ✅ 读出第 4 个字节（起始码 00 00 00 01 后面的 NAL 头：文件头里是 0x67 = SPS，实测第 0 帧是 0x06 = SEI，见 6.1 节）
```

| 指针类型 | 指针运算 `p + n` | 解引用 `*p` / `p[i]` |
|---|---|---|
| `void*` | ❌ 不知道元素多大 | ❌ 不知道按什么类型读 |
| `uint8_t*` | ✅ 地址 + n 字节 | ✅ 读 1 个字节 |

所以这一行转换的目的，就是把"只有地址"的 `void*` 变成"能按字节运算、能按字节读"的 `uint8_t*`。

> 注意：指针加法的单位是"元素个数"，不是字节：
> `uint8_t* p; p + 1` → 地址 +1；`int* q; q + 1` → 地址 +4。
> 这也是要转成 `uint8_t*` 的原因 —— 只有它的"1 个元素 = 1 字节"。

---

## 5. 这一行里的两个 const

```cpp
const auto* pos = static_cast<const uint8_t*>(mpp_packet_get_pos(outputPacket));
```

| 写法 | 含义 |
|---|---|
| `static_cast<const uint8_t*>` | 转成"指向只读字节"的指针：我们只是把码流读出来，加 const 防止误写 MPP 的内存。`void*` → `const uint8_t*` 是合法的（加 const 可以，去掉 const 不行） |
| `const auto* pos` | `auto` 自动推导成 `uint8_t`，等于 `const uint8_t* pos`，省得把类型名写两遍 |

⚠️ 不要和 `const MppPacket` 混淆：`MppPacket` 本身是 `void*` 的 typedef，`const MppPacket` 等于 `void* const`（锁住的是**指针本身**），不是 `const void*`（锁住的是**指向的内容**）。所以 `mpp_packet_get_pos(const MppPacket packet)` 参数里的 const 并不代表 packet 内容只读。

---

## 6. 下一行：`assign` 就是内存拷贝

```cpp
encodedPacket.data.assign(pos, pos + packetLength);
//                        起点  终点（不含）
```

`std::vector::assign(first, last)`：清掉 vector 原来的内容，把 `[first, last)` 这段内存里的每个元素复制进来。指针可以当迭代器用，所以传两个地址就行。效果等价于：

```cpp
encodedPacket.data.resize(packetLength);
memcpy(encodedPacket.data.data(), pos, packetLength);
```

对 `uint8_t` 这种简单类型，标准库内部会用 `memcpy` / `memmove` 一次性拷完。

```
MPP 的内存（outputPacket 里）              我们的 vector（encodedPacket.data）
┌─────┬──────────────────┬─────┐          ┌──────────────────┐
│     │  码流 74674 字节  │     │  ──拷贝──→ │  码流 74674 字节  │
└─────┴──────────────────┴─────┘          └──────────────────┘
      ↑pos               ↑pos + packetLength
```

**为什么必须拷**：`outputPacketPtr` 是 RAII 句柄，`encode()` 一返回就 `mpp_packet_deinit`，`pos` 指向的内存随之失效。只把 `pos` 交出去，调用方拿到的是野指针；拷进 vector，数据就归调用方自己所有。代价是每帧多拷贝约 75KB，和硬件编码一帧的耗时比可以忽略。

### 6.1 `assign` 执行完，`encodedPacket.data` 里是什么

**是一串 `uint8_t`，个数正好是 `packetLength`**：

| | 说明 |
|---|---|
| 类型 | `data` 是 `std::vector<uint8_t>`，每个元素是 1 个 `uint8_t`（1 字节） |
| 个数 | `data.size() == packetLength`。这一行在 `if (packetLength > 0)` 里，所以至少 1 个；实际一帧是几千到几万个（实测 1080p：第 0 帧 74674、第 1 帧 27520、第 2 帧 16253） |
| 空的情况 | `packetLength == 0`（EOS 时的空包）不会走到这一行；`data` 在 `encode()` 开头已经 `clear()`，保持 `size() == 0` |
| 内容 | 这一帧的 H.264 码流字节，和 MPP 内存里的一模一样，只是换了个地方存 |

实测第 0 帧的开头（板子上 `hexdump -C -s 40 out.h264`，跳过文件开头 40 字节的 SPS/PPS）：

```
data[0] data[1] data[2] data[3] data[4] data[5] ...                         data[74673]
 0x00    0x00    0x00    0x01    0x06    0x05   ...                         （最后一个字节）
└──────── 起始码 ────────┘        ↑ NAL 头：0x06 = SEI（MPP 在这里写了自己的版本信息，
                                    hexdump 里能看到 "unknown mpp version for ..." 字样），
                                    后面才是真正的 I 帧图像数据
```

**内存布局**：`std::vector` 保证所有元素在内存里**连续存放**（`data[0]`、`data[1]`……一个挨一个，中间没有空隙），`data.data()` 返回指向第 0 个元素的 `uint8_t*`。这一点是下一节 `fwrite` 能一次写完的前提。

### 6.2 写文件：`fwrite` 一次写完整个 vector，不需要 for 循环

[src/sink/write_stream.cpp](../src/sink/write_stream.cpp) 里只有一行：

```cpp
bool WriteStream::write(const std::vector<uint8_t>& data) {
    return fwrite(data.data(), 1, data.size(), streamOutputFile_.get()) == data.size();
}
```

**可能的疑问**：不是应该用 for 循环遍历 `data`，在循环里一个字节一个字节地 `fwrite` 吗？

**答案**：不需要。`fwrite` 本来就是"从一个地址开始，连续写一大块内存"的函数，循环已经在它内部做了。

#### `fwrite` 的四个参数

```c
size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* fp);
//            从哪开始写      每个元素多大  写多少个元素   写到哪个文件
```

| 参数 | 本项目传的 | 含义 |
|---|---|---|
| `ptr` | `data.data()` | 第 0 个字节的地址（`uint8_t*` 自动转成 `const void*`，不用写 cast） |
| `size` | `1` | 每个元素 1 字节（`uint8_t`） |
| `nmemb` | `data.size()` | 一共写多少个元素，比如 74674 |
| `fp` | `streamOutputFile_.get()` | 输出文件 |

`fwrite` 会从 `ptr` 开始，连续写 `size × nmemb = 1 × 74674 = 74674` 个字节。因为 vector 的元素是**连续存放**的（6.1 节），这 74674 个字节正好就是整个 vector 的内容。

```
data.data()                                      data.data() + data.size()
    ↓                                                   ↓
    ┌────┬────┬────┬────┬────┬─────────────────────────┐
    │0x00│0x00│0x00│0x01│0x06│  ……  共 74674 个字节    │  ──fwrite 一次──→  文件末尾追加 74674 字节
    └────┴────┴────┴────┴────┴─────────────────────────┘
```

#### 返回值：写成功了多少个元素

`fwrite` 返回实际写成功的**元素个数**（不是字节数，只是这里 `size = 1`，两者正好相等）。和 `data.size()` 比较：相等说明全部写完；不相等说明出错了（比如板子磁盘满了），调用方用 `perror` 打印原因。

#### 两种写法对比

```cpp
// ✅ 本项目的写法：一次调用写完
fwrite(data.data(), 1, data.size(), fp);

// ❌ for 循环一个字节一个字节写：结果一样，但没必要
for (uint8_t byte : data) {
    fwrite(&byte, 1, 1, fp);       // 每个字节调用一次 fwrite，一帧要调 74674 次
}
```

| | 一次 `fwrite` | for 循环逐字节 `fwrite` |
|---|---|---|
| 结果 | 一样 | 一样 |
| 函数调用次数 | 1 次 | 74674 次（每次都要加锁、检查、拷贝） |
| 错误检查 | 比较一次返回值 | 每次都要检查，写到一半出错还要处理"写了一部分"的情况 |
| 速度 | 快 | 慢很多（虽然 stdio 有缓冲，不会每个字节都进内核，但几万次函数调用本身的开销就很可观） |

**什么时候才需要循环**：内存**不连续**的时候。比如 `read_nv12_rows` 读 NV12：缓冲区每行末尾有 stride 填充，文件里一行的数据要放到缓冲区里不连续的位置，才需要一行一行地 `fread`（见 [nv12中yuv分布效果图和读取方式.md](../nv12中yuv分布效果图和读取方式.md)）。码流在 vector 里是连续的，所以一次就够了。

> 更多 `fread` / `fwrite` 的细节（返回值、缓冲、常见错误）见 [fread和fwrite 的详细用法.md](fread和fwrite%20的详细用法.md)。

---

## 7. 对比：MPP 里另一种 `void*` —— 句柄，我们不要去转

MPP 里有两种 `void*`，用法完全不同：

| | 例子 | 指向什么 | 我们能不能自己 cast |
|---|---|---|---|
| **数据指针** | `mpp_packet_get_pos`、`mpp_buffer_get_ptr` 的返回值 | 一块字节内存（码流、图像） | ✅ 转成 `uint8_t*` 按字节读写 |
| **不透明句柄** | `MppCtx`、`MppPacket`、`MppFrame`、`MppBuffer` | MPP 内部的结构体（`MppPacketImpl` 等），故意不公开 | ❌ 不要转，只能原样传回给 `mpp_xxx` 函数 |

句柄做成 `void*` 是为了**隐藏内部结构**：MPP 升级时内部字段随便改，我们的代码不用重新编译。我们手里的 `MppPacket` 只是一张"取件凭证"，所有操作都要交给 `mpp_packet_get_xxx` 去做。

---

## 8. 一句话总结

- **为什么要转**：`void*` 不能做指针运算；**`uint8_t*` 可以做指针运算**，而且 1 个元素 = 1 个字节，`pos + n` 就是往后 n 个字节 —— 算码流结尾 `pos + packetLength` 全靠它。
- **语法依据**：C++ 规定 `void*` 可以 `static_cast` 成任何对象指针，编译器不检查内存里实际是什么 —— 所以**任何 `void*` 都能转成 `uint8_t*`**。
- **正确性依据**：① MPP 约定 `pos` 指向码流字节；② C++ 允许用 `unsigned char`（= `uint8_t`）读取任何内存的字节。两者都满足，读出来的结果才正确。
- **打个比方**：`void*` 是一个没贴标签的箱子，`static_cast` 是你给它贴上标签。编译器相信你贴的标签；贴得对不对，要看箱子里实际装的是什么。
