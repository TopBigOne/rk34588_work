# 链接错误：undefined reference

> 2026-10-17 写 `FindCamera` 时真实遇到的报错。看懂它要先知道：**C++ 程序是分两步做出来的——先编译、再链接**。

---

## 1. 报错长什么样

```
[4/4] Linking CXX executable day3_cam2h264          ← 注意是 Linking（链接）这一步失败的
FAILED: [code=1] day3_cam2h264
... aarch64-buildroot-linux-gnu-g++ ... main.cpp.o mpp_encoder.cpp.o find_camera.cpp.o -o day3_cam2h264 ...
.../ld: CMakeFiles/day3_cam2h264.dir/src/main.cpp.o: in function `main':
.../src/main.cpp:10: undefined reference to `FindCamera::FindCamera()'
.../src/main.cpp:10: undefined reference to `FindCamera::~FindCamera()'
collect2: error: ld returned 1 exit status
```

⚠️ **真正的错误在 `FAILED` 那一行的后面**。只看到 `FAILED` 和一长串命令时，往下翻，找 `undefined reference` 或者 `error:`。

## 2. 先懂：程序是分两步做出来的

```
 main.cpp ──编译──→ main.cpp.o ─┐
 find_camera.cpp ──编译──→ find_camera.cpp.o ─┼──链接（ld）──→ day3_cam2h264（可执行程序）
 mpp_encoder.cpp ──编译──→ mpp_encoder.cpp.o ─┘        + librockchip_mpp.so
```

| 步骤 | 谁干 | 干什么 | 只看得到什么 |
|---|---|---|---|
| ① 编译 | 编译器（g++） | **每个 .cpp 单独**翻译成机器码（.o） | 这一个 .cpp 和它 include 的头文件 |
| ② 链接 | 链接器（ld） | 把所有 .o 和库拼成一个程序，把"调用"和"实现"对上 | 所有 .o 和库 |

关键在于第 ① 步：**编译 main.cpp 时，编译器只看得到头文件里的声明**，看到 `FindCamera();` 这一行，它就相信"实现在别的文件里"，先在 main.cpp.o 里留一个"这里要调用 FindCamera::FindCamera()"的记号，编译就通过了。

到第 ② 步，链接器拿着这个记号，去所有 .o 和库里找 `FindCamera::FindCamera()` 的实现。**哪里都找不到**，就报 `undefined reference`（没有定义）。

## 3. 这次的原因

`find_camera.h` 里**声明**了：
```cpp
FindCamera();
~FindCamera();
```
但 `find_camera.cpp` 里**没有实现**（没有 `FindCamera::FindCamera() { ... }`）。

为什么平时不写构造函数也没事？**一个都不声明时，编译器会自动生成默认的**；可一旦自己声明了，编译器就认为"你要自己写"，不再自动生成。

## 4. 怎么改（三选一）

| 改法 | 写法 | 什么时候用 |
|---|---|---|
| ⭐ 让编译器生成 | 头文件里：`FindCamera() = default;` `~FindCamera() = default;` | 没有要特别初始化 / 释放的东西（这次用的就是这个） |
| 删掉声明 | 头文件里两行都删掉 | 效果一样，编译器自动生成 |
| 自己写实现 | .cpp 里：`FindCamera::FindCamera() { ... }` | 构造时要做事（打开文件、申请内存……） |

## 5. 其他常见的 undefined reference

| 报错里的名字 | 原因 | 怎么改 |
|---|---|---|
| 自己写的类的函数（`FindCamera::xxx`） | ① 头文件声明了、.cpp 没实现；② .cpp 写了但**没加进 CMakeLists.txt 的 `add_executable`**；③ .cpp 里函数名、参数和声明对不上（比如少了 `const`） | 对照声明检查；确认 .cpp 在 CMakeLists.txt 里 |
| 库的函数（`mpp_create`、`mpp_buffer_get_with_tag`……） | 没链接库：CMakeLists.txt 里少了 `target_link_libraries(... rockchip_mpp)` | 加上要链接的库 |
| 实现写在 .cpp 里的模板函数 | 模板要在头文件里实现 | 把实现搬进 .h |

## 6. 一眼区分：编译错误还是链接错误

| | 编译错误 | 链接错误 |
|---|---|---|
| 出现在哪一步 | `Building CXX object xxx.cpp.o` | `Linking CXX executable xxx` |
| 报错里有 | `error:` + 文件名:行号 | `undefined reference to`、`ld returned 1 exit status` |
| 典型原因 | 语法错、类型不对、找不到头文件 | 有声明没实现、少链接了库、.cpp 没加进工程 |
| 先查哪里 | 报错指的那一行代码 | 头文件声明和 .cpp 实现是否对得上；CMakeLists.txt |

## 7. 自测

- [ ] C++ 程序是哪两步做出来的？编译 main.cpp 时，编译器看得到 find_camera.cpp 吗？
- [ ] 为什么头文件里只有声明、没有实现，main.cpp 也能编译通过？
- [ ] 为什么不写构造函数没事，写了声明不写实现就报错？
- [ ] 报 `undefined reference to mpp_create`，最可能是什么原因？
- [ ] 怎么一眼看出是编译错误还是链接错误？
