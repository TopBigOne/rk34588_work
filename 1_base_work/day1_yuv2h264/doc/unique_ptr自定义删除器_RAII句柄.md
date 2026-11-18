# unique_ptr + 自定义删除器：mpp_utils.h 里的 RAII 句柄

> 目标：看懂 `src/common/mpp_utils.h` 里 `XxxDeleter` + `using XxxPtr = std::unique_ptr<...>` 这种写法，知道它在项目里怎么用、有哪些坑。
> 相关文件：`src/common/mpp_utils.h`（定义）、`src/encoder/mpp_encoder.cpp`（`MppFramePtr`、`MppPacketPtr` 的用法）、`src/source/read_yuv.h`（`FilePtr`、`MppBufferGroupPtr`、`MppBufferPtr` 当成员变量用）

---

## 1. 一句话说清楚

```cpp
struct MppFrameDeleter {
    void operator()(MppFrame frame) const { mpp_frame_deinit(&frame); }
};
using MppFramePtr = std::unique_ptr<void, MppFrameDeleter>;
```

这三行只做一件事：**`MppFrame` 离开作用域时，自动调用 `mpp_frame_deinit` 释放它。**

它用到 C++ 的三个知识点，下面一个一个拆开看：

| 写法 | 知识点 | 作用 |
|---|---|---|
| `struct ... { void operator()(...) }` | 函数对象（仿函数） | 定义"怎么释放" |
| `std::unique_ptr<void, MppFrameDeleter>` | 智能指针 + 自定义删除器 | 保管指针，销毁时调用删除器 |
| `using MppFramePtr = ...` | 类型别名 | 给长类型起个短名字 |

---

## 2. 第 1 步：`struct` + `operator()` = 函数对象（仿函数）

```cpp
struct MppFrameDeleter {
    void operator()(MppFrame frame) const { mpp_frame_deinit(&frame); }
};
```

`operator()` 是重载**函数调用运算符**，也就是圆括号 `()`。重载之后，这个结构体的对象就能像函数一样调用：

```cpp
MppFrameDeleter d;   // 创建一个对象
d(someFrame);        // 像调函数一样调用 → 实际执行 mpp_frame_deinit(&someFrame)
```

可以把 `MppFrameDeleter` 理解成**一个名字叫 MppFrameDeleter、作用是释放 MppFrame 的函数**，只不过是用结构体的形式写的。

- 末尾的 `const`：表示调用它不会修改这个对象自己（它本来也没有成员变量），可以忽略
- **为什么不直接写普通函数？** 下一步的 `unique_ptr` 要求把"怎么释放"写成一个**类型**，填进尖括号 `<>` 里。普通函数是一个值，不是类型，填不进去；结构体是类型，可以

---

## 3. 第 2 步：`std::unique_ptr<T, Deleter>`：自动释放的智能指针

```cpp
std::unique_ptr<void, MppFrameDeleter>
```

`unique_ptr` 是标准库（`<memory>`）里的一个类，它做两件事：

1. **保存一个指针**（类型是 `T*`）
2. **它自己被销毁时**（离开作用域、函数 `return`、出错提前 `return`），如果保存的指针**不为空**，就调用 `Deleter` 释放它

尖括号里的两个参数：

| 参数 | 这里填的 | 为什么 |
|---|---|---|
| `T` | `void` | 保存的指针类型是 `T*` = `void*`。MPP 头文件里 `typedef void* MppFrame;`，`MppFrame` 本身就是 `void*` |
| `Deleter` | `MppFrameDeleter` | 释放时调用第 1 步的函数对象。不写的话默认用 `delete`，但 `MppFrame` 不是 `new` 出来的，必须用 `mpp_frame_deinit` 释放 |

"unique"（独占）的意思是：**一个指针同一时间只有一个 `unique_ptr` 管**。所以它**不能拷贝**（拷贝了就有两个主人，会释放两次），只能用 `std::move` 把所有权转交出去。

---

## 4. 第 3 步：`using` 起别名

```cpp
using MppFramePtr = std::unique_ptr<void, MppFrameDeleter>;
```

就是给那个很长的类型起个短名字，作用和 C 语言的 `typedef` 一样：

```cpp
typedef std::unique_ptr<void, MppFrameDeleter> MppFramePtr;   // C 风格，效果完全相同
```

之后写 `MppFramePtr` 就等于写 `std::unique_ptr<void, MppFrameDeleter>`。

---

## 5. 合起来：在项目里怎么用

`src/encoder/mpp_encoder.cpp` 的 `MppEncoder::encode()`：

```cpp
MppFrame rawFrame = nullptr;
CHECK(mpp_frame_init(&rawFrame));              // ① 申请
MppFramePtr inputFrame(rawFrame);              // ② 交给 unique_ptr 保管

mpp_frame_set_width(inputFrame.get(), ...);    // ③ .get() 取出里面的原始指针（MppFrame）传给 MPP 函数
...
CHECK(encoderApi_->encode_put_frame(encoderCtx_, inputFrame.get()));
inputFrame.reset();                            // ④ 用完马上释放（不写也行，函数返回时会自动释放）
```

`CHECK` 失败时会直接 `return false`。**不管从哪里 return，`inputFrame` 都会被销毁，自动调用 `mpp_frame_deinit`。**

### 5.1 不用 unique_ptr 时要怎么写

```cpp
MppFrame frame = nullptr;
mpp_frame_init(&frame);
mpp_frame_set_width(frame, ...);
if (出错) {
    mpp_frame_deinit(&frame);   // 每个提前 return 的地方都要记得释放
    return false;
}
...
if (又出错) {
    mpp_frame_deinit(&frame);   // 又要写一遍
    return false;
}
mpp_frame_deinit(&frame);       // 正常结束也要释放
return true;
```

出口一多，就容易漏掉某一处释放（内存泄漏），或者释放了两次（崩溃）。原来单文件版 `main.cpp` 用 `goto CLEANUP` 就是为了解决这个问题。

### 5.2 RAII 是什么

**RAII**（Resource Acquisition Is Initialization，资源获取即初始化）：

- 对象**创建**时拿到资源
- 对象**销毁**时自动释放资源

"怎么释放"写在**类型**里，编译器在每个出口自动帮你调用释放代码，不会漏、也不会重复。

---

## 6. 常用操作

| 写法 | 作用 | 项目里的例子 |
|---|---|---|
| `XxxPtr p(raw);` | 把原始指针交给 `p` 保管 | `MppFramePtr inputFrame(rawFrame);` |
| `p.get()` | 取出原始指针，传给 MPP 的 C 函数。**所有权还在 `p` 手里** | `mpp_frame_set_width(inputFrame.get(), ...)` |
| `p.reset()` | 立刻释放（调用 Deleter），`p` 变成空 | `inputFrame.reset();` |
| `p.reset(raw)` | 先释放旧的，再保管新的 | `drmBufferGroup_.reset(rawGroup);` |
| `if (p)` / `if (!p)` | 判断是不是空 | |
| `p.release()` | 交出所有权：返回原始指针，`p` 变成空，**不释放**。之后要自己负责释放 | 项目里没用到 |
| `std::move(p)` | 把所有权转给另一个 `unique_ptr` | 项目里没用到 |

---

## 7. mpp_utils.h 里的 5 个句柄

套路完全一样，只是"释放函数"不同：

| 句柄 | 保管的东西 | Deleter 里调用 | 用在哪 |
|---|---|---|---|
| `FilePtr` | `FILE*` | `fclose(fp)` | `ReadYUV` 的输入文件、`WriteStream` 的输出文件 |
| `MppBufferGroupPtr` | `MppBufferGroup`（内存池） | `mpp_buffer_group_put(group)` | `ReadYUV::drmBufferGroup_` |
| `MppBufferPtr` | `MppBuffer`（一块硬件内存） | `mpp_buffer_put(buffer)` | `ReadYUV::nv12FrameBuffer_` |
| `MppFramePtr` | `MppFrame` | `mpp_frame_deinit(&frame)` | `MppEncoder::encode()` 的 `inputFrame` |
| `MppPacketPtr` | `MppPacket` | `mpp_packet_deinit(&packet)` | `MppEncoder::encode()` 的 `outputPacket` |

注意 `FilePtr` 的 `T` 写的是 `FILE`（保存 `FILE*`），其他 4 个写的是 `void`（保存 `void*`），因为 MPP 的这几种类型都是 `typedef void* Xxx;`。

---

## 8. 几个细节

### 8.1 为什么 Deleter 里是 `&frame`

```cpp
void operator()(MppFrame frame) const { mpp_frame_deinit(&frame); }
```

`mpp_frame_deinit` 的参数是 `MppFrame*`（变量的地址），因为它释放完还要把你的变量置成 `NULL`。

这里的 `frame` 是 Deleter 自己的参数，`unique_ptr` 把指针**拷贝**了一份传进来，所以取的是这份拷贝的地址：

- 释放的仍然是**同一个** frame（指针值一样）
- 置 `NULL` 只影响这份拷贝，没关系，因为 `unique_ptr` 销毁后也不会再用这个指针了

`MppPacketDeleter` 同理。`mpp_buffer_put`、`mpp_buffer_group_put`、`fclose` 的参数本来就是指针本身，所以不用 `&`。

### 8.2 为什么 MppBuffer 也要包一层结构体

`mpp_buffer_put` 不是函数，是一个**宏**（展开成 `mpp_buffer_put_with_caller(buffer, __FUNCTION__)`）。宏没有地址，不能当函数指针传给 `unique_ptr`，所以只能包在 `operator()` 里调用。

### 8.3 Deleter 里不用判断空指针

`unique_ptr` 只有在保存的指针**不为空**时才调用 Deleter，所以 Deleter 里不用写 `if (frame)`。

### 8.4 成员变量的声明顺序 = 析构顺序的反过来

`src/source/read_yuv.h`：

```cpp
FilePtr nv12InputFile_;            // 先声明 → 最后析构
MppBufferGroupPtr drmBufferGroup_; // 内存池
MppBufferPtr nv12FrameBuffer_;     // 后声明 → 最先析构
```

C++ 规定：**成员变量和局部变量都按声明的相反顺序析构**。

`MppBuffer` 是从 `MppBufferGroup` 里申请的，必须**先还 buffer，再销毁内存池**，所以 `drmBufferGroup_` 要声明在 `nv12FrameBuffer_` 前面。顺序写反了，析构时会先销毁内存池，再去还一块已经不存在的内存池里的 buffer。

### 8.5 `.get()` 拿到的指针不要自己释放

```cpp
MppFramePtr inputFrame(rawFrame);
mpp_frame_deinit(&rawFrame);        // ❌ 自己释放了一次
// 函数返回时 inputFrame 又释放一次 → 重复释放，崩溃
```

交给 `unique_ptr` 以后，释放就是它的事了。`.get()` 只是"借来用一下"，用完不要 deinit / put / fclose。真的要提前释放，用 `p.reset()`。

---

## 9. 小结

```
struct XxxDeleter { void operator()(Xxx x) const { 释放函数(x); } };   // 怎么释放
using XxxPtr = std::unique_ptr<void, XxxDeleter>;                     // 起个短名字

XxxPtr p(raw);       // 交给它保管
p.get()              // 借出原始指针给 C 函数用
p.reset()            // 提前释放；不写的话离开作用域自动释放
```

**记住一句话：把"怎么释放"写进类型里，从此不用再手写释放代码。**
