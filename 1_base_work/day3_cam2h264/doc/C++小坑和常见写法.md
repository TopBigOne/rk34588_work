# C++ 小坑和常见写法

> 写 day3 时**真实踩过的坑**，每一条都配当时的代码。很多坑**编译器不报错**，程序也能跑，只是结果碰巧对——最危险。
> 本文的数值和行为都是 2026-10-18 实际查头文件、编译运行验证过的。

| # | 坑 | 编译器会提醒吗 | 后果 |
|:---:|---|:---:|---|
| 1 | 系统调用的返回值用 `if (fd)` 判断 | ❌ | open 失败也当成功 |
| 2 | 宏定义末尾多写分号 | ✅ 报错 | 编译不过 |
| 3 | 名字很像的常量用错（`CAP_` 和 `BUF_TYPE_`） | ❌ | 判断永远失败 |
| 4 | 数组名当指针用：`g_bufs->start` | ❌ | 永远操作第 0 个元素 |
| 5 | range-for 不加 `&`，改的是副本 | ❌ | 改动没生效 |
| 6 | 函数参数放错位置（mmap 的 `MAP_SHARED`） | ❌ | 碰巧能跑 |
| 7 | 返回 int 的函数没写 return | ⚠️ 警告 | 返回随机值 |
| 8 | `reinterpret_cast` 把数字当指针 | ❌ | 打印错乱、可能崩溃 |
| 9 | 大括号初始化 `vector{0}` 有歧义 | ❌ | 元素个数和想的不一样 |
| — | 常见写法：输出参数（用指针带回第二个结果） | | |
| — | 声明了函数不写实现 → 链接错误 | ✅ | 见 [链接错误undefined_reference.md](链接错误undefined_reference.md) |

---

## 1. 系统调用的返回值：`if (fd)` 是错的

**当时的代码：**
```cpp
g_videoFd = open(kDevice, O_RDWR | O_CLOEXEC);
if (g_videoFd) {                                  // ❌
    LOGI(kTag, "open camera in success.");
```

**为什么错：** `open` 成功返回 **≥ 0** 的 fd（0 也合法），失败返回 **-1**。C/C++ 里 `if (整数)` 是"不等于 0 就为真"，**-1 也是真**（实测：`if (-1)` 为 true）。所以打开失败也会打印 success。

**正确写法：**
```cpp
if (g_videoFd < 0) {
    LOGE(kTag, "open %s failed: %s", kDevice, strerror(errno));   // 原因在 errno 里
    return -1;
}
```

**规律**：Linux 系统调用（`open`、`ioctl`、`read`……）失败都返回 **-1**、原因在 `errno`；`mmap` 失败返回 **`MAP_FAILED`**。判断失败要和这两个比，不要用 `if (返回值)`。

---

## 2. 宏定义末尾多写分号

**当时的代码：**
```cpp
#define CHECK_CAMERA_INFO  (1);      // ❌ 末尾的分号
if (CHECK_CAMERA_INFO) { ... }
```

**为什么错：** 宏是**原样替换文本**，展开后变成 `if ((1);) {`，括号里多了分号。编译报 `expected primary-expression before ')' token`。

**正确写法：** 去掉分号；能不用宏就不用，改成常量更安全：
```cpp
static const bool kCheckCameraInfo = true;
if (kCheckCameraInfo) { ... }
```

---

## 3. 名字很像的常量用错：`V4L2_CAP_` 和 `V4L2_BUF_TYPE_`

**当时的代码：**
```cpp
__u32 caseOne = cap.capabilities & V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;   // ❌
```

| 常量 | 值 | 用途 |
|---|---|---|
| `V4L2_CAP_VIDEO_CAPTURE_MPLANE` | `0x1000` | 能力标志位，和 `cap.capabilities` 做 `&` |
| `V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE` | `9` | 缓冲区类型，填进 `xxx.type` |

`0x84201000 & 9 = 0`，于是程序以为"不支持多平面"退出了（实测）。**都是整数，编译器不会报错。** 详见 [V4L2这个名字是什么意思.md](V4L2这个名字是什么意思.md) 第 3.1 节。

---

## 4. 数组名当指针用：`g_bufs->start` 永远是第 0 个

**当时的代码：**
```cpp
static CamBuffer g_bufs[4];
...
g_bufs[i].start = mmap(...);
if (g_bufs->start == MAP_FAILED) { ... }          // ❌ 检查的永远是 g_bufs[0]
...
int index = dequeue_buffer(&bytesUsed);           // 实测拿到 buffer 2
fwrite(g_bufs->start, 1, bytesUsed, g_out);       // ❌ 写的却是 buffer 0
```

**为什么错：** 数组名在表达式里会**自动变成指向第 0 个元素的指针**（叫"数组退化"），所以：
```
g_bufs->start   ≡   (*g_bufs).start   ≡   g_bufs[0].start
```
和 `i`、`index` 一点关系都没有。实测：`int arr[3] = {10, 20, 30};` 时 `*arr` 就是 `10`。

**后果：**
- mmap 检查：第 1、2、3 块失败了发现不了
- 存文件：写的是 buffer 0，不是刚取到的 buffer 2。buffer 0 这时在驱动手里，摄像头随时可能往里写 → 画面一动就可能撕裂。**拍静止的桌面时看起来完全正常**（实测），所以很难发现

**正确写法：**
```cpp
if (g_bufs[i].start == MAP_FAILED) { ... }
fwrite(g_bufs[index].start, 1, bytesUsed, g_out);
```

**规律**：对数组元素，永远写 `数组[下标].成员`；`->` 留给真正的指针（比如 `CamBuffer* p = &g_bufs[i]; p->start`）。

---

## 5. range-for 不加 `&`，改的是副本

**当时的代码：**
```cpp
for (auto g_buf : g_bufs) {          // ❌ auto 是拷贝
    if (g_buf.start != MAP_FAILED) {
        munmap(g_buf.start, g_buf.length);
        g_buf.start = MAP_FAILED;    // 改的是副本，全局数组里没变
    }
}
```

**为什么错：** `for (auto x : 数组)` 每次把元素**拷贝**一份给 `x`；改 `x` 不影响数组。munmap 本身执行了，但"标记已释放"没生效，cleanup 被调两次就会对同一块内存 munmap 两次。

**正确写法：**
```cpp
for (CamBuffer& b : g_bufs) {        // & = 引用，b 就是数组元素本身
```

**规律**：要修改元素用 `auto&`；只读、元素又大用 `const auto&`；只有元素很小（int）又不改时才用 `auto`。

---

## 6. 函数参数放错位置：mmap 里的 `MAP_SHARED`

**当时的代码：**
```cpp
mmap(nullptr, length, PROT_READ | PROT_WRITE | MAP_SHARED, MAP_SHARED, fd, offset);
//                    └──────── 第 3 个参数：保护方式 ────────┘  └ 第 4 个：映射方式 ┘
```

mmap 的参数：

| 位置 | 参数 | 能放什么 |
|:---:|---|---|
| 3 | `prot` 保护方式 | `PROT_READ`、`PROT_WRITE`、`PROT_EXEC` |
| 4 | `flags` 映射方式 | `MAP_SHARED`、`MAP_PRIVATE`…… |

`MAP_SHARED` 放进了第 3 个参数。**为什么还能正常运行**：查头文件（`bits/mman-linux.h`）：`PROT_READ = 0x1`、`MAP_SHARED = 0x01`——**值碰巧相同**，或进去等于没变。换个平台或者换个标志（比如 `MAP_PRIVATE = 0x02`，正好等于 `PROT_WRITE`）就会出问题。

**正确写法：**
```cpp
mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
```

**规律**：这类"一堆整数标志位"的参数，编译器分不出哪个放哪，要对照函数原型（`man mmap`，或者在 CLion 里按住 Cmd 点函数名看声明）。

---

## 7. 返回 int 的函数没写 return

**当时的代码：**
```cpp
int run() {}       // 还没写完，先占个位
```

编译器报 warning：`no return statement in function returning non-void`。**一旦被调用，返回值是随机的**（C++ 里叫"未定义行为"），`main` 拿它当退出码就会乱。

**正确写法：** 占位时也写上 `return 0;`。

---

## 8. `reinterpret_cast` 把数字当指针

**当时的代码：**
```cpp
LOGI(kTag, "format: %ux%u ...", reinterpret_cast<const char*>(fmt.fmt.pix_mp.width), ...);   // ❌
```

`width` 是数字 1920，`%u` 要的就是数字。`reinterpret_cast<const char*>` 把 1920 **当成一个内存地址**，类型和格式串对不上，打印结果是错的。

**为什么会这么写**：上一次的编译错误是"漏写 kTag"，被误以为是 width 的类型问题。**看编译错误要看它指的是哪一行、哪个符号**，不要猜着改。

**规律**：`reinterpret_cast` 是"强行换个解读方式"，几乎只在和硬件、内存地址打交道时才用；格式化打印用不到它。

---

## 9. 大括号初始化 `vector{0}`：元素个数和想的不一样

**当时的代码：**
```cpp
std::vector<CameraNode> cameras{0};
```

实测（Mac 上编译运行）：

| 写法 | 元素个数 | 为什么 |
|---|:---:|---|
| `std::vector<int>{0}` | **1**（一个 0） | 大括号优先当成"元素列表"：0 能当 int，就是一个元素 |
| `std::vector<CameraNode>{0}` | **0** | 0 变不成 CameraNode，退回去当"个数"：0 个 |
| `std::vector<int>(5)` | 5 | 小括号：5 个元素 |
| `std::vector<int>{5}` | 1 | 大括号：一个元素，值是 5 |

同样写 `{0}`，换个元素类型结果就不同，读代码的人很难一眼看出来。

**正确写法：** 要空的就什么都不写：
```cpp
std::vector<CameraNode> cameras;
```

---

## 常见写法：输出参数（用指针带回第二个结果）

**代码：**
```cpp
uint32_t bytesUsed = 0;                       // 调用方准备一个"空盒子"
int index = dequeue_buffer(&bytesUsed);       // 把盒子的地址传进去

int dequeue_buffer(uint32_t* bytesUsed) {
    ...
    if (bytesUsed) {                          // 调用方传了 nullptr 就不写
        *bytesUsed = planes[0].bytesused;     // 通过地址把值写回盒子
    }
    return static_cast<int>(buf.index);       // 第一个结果用 return
}
```

**为什么这么写：** 一个函数只能 `return` 一个值，DQBUF 要交回两样东西：
- **哪块缓冲区**（index，实测 2）→ 用 `return`
- **这一帧用了多少字节**（bytesUsed，实测 3,110,400）→ 用输出参数

`bytesUsed` **不存帧数据**，只是一个数字。帧数据在驱动写好的 `g_bufs[index].start` 里。需要它是因为缓冲区（3,133,440）比一帧（3,110,400）大，存文件要按实际字节数写。

**可选的输出参数**：跳帧时不关心字节数，传 `nullptr`，函数里先判断 `if (bytesUsed)` 再写，避免往空地址写崩溃。

**以前见过的同一种写法**：`mpp_create(&ctx, &mpi)`、`mpp_buffer_get(group, &buf, size)`、`mpp_frame_init(&frame)`——都是"传地址进去，函数把结果写回来"。

**另一种写法**：结果多时，返回一个结构体也行（比如 `FindCamera::find` 用 `std::vector<CameraNode>&` 引用带回）。

---

## 自测

- [ ] `open` 失败返回什么？为什么 `if (fd)` 判断不对？
- [ ] `#define X (1);` 用在 `if (X)` 里展开成什么？
- [ ] `g_bufs->start` 等于什么？为什么拍静止画面时看不出这个 bug？
- [ ] `for (auto b : arr)` 和 `for (auto& b : arr)` 有什么区别？
- [ ] mmap 第 3、4 个参数分别放什么？`MAP_SHARED` 放错位置为什么还能跑？
- [ ] `std::vector<int>{0}` 和 `std::vector<CameraNode>{0}` 各有几个元素？
- [ ] `dequeue_buffer` 为什么要用指针带回 `bytesUsed`？它存的是帧数据吗？
