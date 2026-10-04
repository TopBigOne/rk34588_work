# vector 的 assign() 的用法

> 出处：[src/encoder/mpp_encoder.cpp](../src/encoder/mpp_encoder.cpp) 里 `MppEncoder::encode()` 把码流从 MppPacket 拷出来的那一行：
> ```cpp
> encodedPacket.data.assign(pos, pos + packetLength);
> ```
> 相关文档：[void* 能转成任何对象指针.md](%20void*%20能转成任何对象指针.md)（`pos` 为什么要转成 `uint8_t*`）、[fwrite和write的区别.md](fwrite和write的区别.md)（拷进 vector 之后怎么写文件）

---

## 1. 一句话

`assign()` 是 `std::vector` 的成员函数：**用一批新元素替换 vector 原来的全部内容**。名字就是"赋值"的意思 —— 清空旧的，换上新的。

---

## 2. 三种用法

```cpp
std::vector<uint8_t> v = {1, 2, 3};

v.assign(5, 0xFF);        // ① n 个相同的值        → v = {FF, FF, FF, FF, FF}
v.assign({7, 8});         // ② 初始化列表           → v = {7, 8}
v.assign(first, last);    // ③ 一段范围 [first, last) → v = 这段范围里的所有元素
```

| 用法 | 参数 | 执行后 `size()` |
|---|---|---|
| ① `assign(n, value)` | 个数 + 值 | `n` |
| ② `assign({a, b, ...})` | 花括号列表 | 列表里的元素个数 |
| ③ `assign(first, last)` | 两个迭代器（起点、终点） | `last - first` |

本项目用的是第 ③ 种。不管原来 vector 里有多少元素，执行完都只剩新内容。

---

## 3. 本项目这一行在做什么

```cpp
const auto* pos = static_cast<const uint8_t*>(mpp_packet_get_pos(outputPacket));
encodedPacket.data.assign(pos, pos + packetLength);
//                        ↑起点  ↑终点（不包含）
```

意思是：把 MPP 内存里**从 `pos` 开始、到 `pos + packetLength` 之前**的这 `packetLength` 个字节，作为 `encodedPacket.data` 的新内容。

```
MPP 的内存（outputPacket 里）              encodedPacket.data（std::vector<uint8_t>）
┌─────┬──────────────────┬─────┐          ┌──────────────────┐
│     │  码流 74674 字节  │     │  assign→  │  码流 74674 字节  │
└─────┴──────────────────┴─────┘          └──────────────────┘
      ↑pos               ↑pos + packetLength（不包含这个位置）
```

执行完：
- `encodedPacket.data.size() == packetLength`
- 内容和 MPP 内存里那段码流一模一样（实测第 0 帧开头是 `00 00 00 01 06 ...`）
- 这是一份**独立的副本**：`encode()` 返回时 `outputPacketPtr` 会释放 MppPacket，`pos` 随之失效，但 vector 里的数据不受影响

### 区间是"左闭右开" `[first, last)`

`last` 指向的是**最后一个元素的下一个位置**，它本身不被复制。所以元素个数正好是 `last - first`：

```
pos[0]  pos[1]  pos[2]  ...  pos[packetLength-1]   pos[packetLength]
  ✅      ✅      ✅    ...          ✅                ❌ ← last 指在这里，不复制
```

C++ 标准库里所有"范围"都是这个规则（`std::sort(first, last)`、`std::copy(first, last, ...)`……），好处是：个数 = `last - first`，空范围就是 `first == last`，不用 +1 / -1。

---

## 4. 为什么传两个指针就行

第 ③ 种用法要的是两个**迭代器**。普通指针天生就满足迭代器的要求：

| 迭代器要能做的事 | 指针能不能做 |
|---|---|
| `*it` 取出当前元素 | ✅ `*pos` 读出一个字节 |
| `++it` 走到下一个 | ✅ `pos + 1` 往后 1 字节 |
| `it1 != it2` 判断有没有走到头 | ✅ 比较两个地址 |
| `last - first` 算距离 | ✅ 指针相减 |

所以 `pos` 和 `pos + packetLength` 可以直接传进去。

这也是前面必须把 `void*` 转成 `uint8_t*` 的原因之一：`void*` 不能做指针运算，**算不出 `pos + packetLength`**，也不能解引用，当不了迭代器。

---

## 5. assign 内部做了什么

1. **丢掉原来的内容**：不管之前有多少元素，全部清掉。
2. **确保空间够**：
   - 容量（`capacity()`）不够 → 申请一块更大的内存，旧内存释放
   - 容量够 → **直接用原来那块内存**，不重新申请
3. **复制元素**：把 `[first, last)` 里的元素复制进来。对 `uint8_t` 这种简单类型，标准库内部会用 `memcpy` / `memmove` 一次拷完，不会一个字节一个字节地循环。
4. **设置大小**：`size()` 变成 `last - first`。

效果等价于手写的：

```cpp
encodedPacket.data.resize(packetLength);
memcpy(encodedPacket.data.data(), pos, packetLength);
```

### size 和 capacity 的区别

| | 含义 | 例子 |
|---|---|---|
| `size()` | 现在**装了**多少个元素 | 这一帧 27520 字节 |
| `capacity()` | 已经申请的内存**能装**多少个 | 之前申请过 74674 字节的空间 |

`size() <= capacity()` 永远成立。`clear()` 只把 `size` 变成 0，**不释放内存**，`capacity` 保持不变。

---

## 6. 本项目里的隐藏好处：每帧复用同一块内存

[src/pipeline/encode_pipeline.cpp](../src/pipeline/encode_pipeline.cpp) 里，`EncodedPacket` 是在**循环外面**声明的，每一帧都复用同一个对象：

```cpp
EncodedPacket packet;              // 循环外：只构造一次
while (!outputEos) {
    ...
    encoder_.encode(frameBuffer, inputEos, packet);   // 每帧都往同一个 packet 里填
    ...
}
```

而 `encode()` 一开头先 `clear()`，再 `assign()`：

```cpp
bool MppEncoder::encode(MppBuffer frameBuffer, bool inputEos, EncodedPacket& encodedPacket) {
    encodedPacket.data.clear();    // size = 0，内存还在
    ...
    encodedPacket.data.assign(pos, pos + packetLength);
```

用板子实测的前三帧大小，在 Mac 上模拟一遍（测试程序见第 9 节）：

```
assign  74674 字节 → size= 74674 capacity= 74674  重新申请了内存
assign  27520 字节 → size= 27520 capacity= 74674  复用原来的内存
assign  16253 字节 → size= 16253 capacity= 74674  复用原来的内存
```

- 第 0 帧是 I 帧，最大，申请一块 74674 字节的内存
- 后面的 P 帧都比它小，`assign` 直接复用这块内存，**不再申请**，只做一次拷贝
- 只有遇到比之前都大的帧（比如下一个 GOP 的 I 帧更大），才会再申请一次

如果把 `EncodedPacket` 声明在循环**里面**，每帧结束都会析构、释放内存，下一帧再重新申请。60 帧就是 60 次申请 + 60 次释放 —— 结果一样，只是多了没必要的开销。

---

## 7. 和其他写法的对比

| 写法 | 结果 | 说明 |
|---|---|---|
| `data.assign(pos, pos + len)` | ✅ 替换为这段字节 | 本项目的写法，一行搞定，能复用内存 |
| `data = std::vector<uint8_t>(pos, pos + len)` | ✅ 一样 | 先构造一个临时 vector 再赋值，原来的内存不能复用 |
| `data.insert(data.end(), pos, pos + len)` | ⚠️ **追加**到末尾 | 原内容还在，不是替换（除非先 `clear()`） |
| `for (...) data.push_back(pos[i])` | ✅ 一样，但慢 | 逐个添加，74674 次函数调用，容量不够时还会多次重新分配 |
| `data.resize(len); memcpy(data.data(), pos, len)` | ✅ 一样 | 手写版的 assign，要自己保证长度对 |

实测 `insert` 是追加：

```
assign({7, 8})  → size=2, v[0]=7 v[1]=8
insert 3 个     → size=5（原来的 2 个还在）
```

---

## 8. 一句话总结

- `assign(first, last)` = **清空 vector，把 `[first, last)` 这段元素复制进来**，执行完 `size() == last - first`。
- 指针可以当迭代器，所以 `assign(pos, pos + packetLength)` 能直接把一段原始内存拷进 vector —— 前提是 `pos` 是 `uint8_t*`，能做指针运算。
- `clear()` 不释放内存，配合循环外声明的 `EncodedPacket`，后面每帧 `assign` 都复用第一帧申请的那块内存。

---

## 9. 附：第 6、7 节的测试程序

```cpp
// assign_demo.cpp —— Mac：c++ -std=c++17 -O2 -o assign_demo assign_demo.cpp && ./assign_demo
#include <cstdint>
#include <cstdio>
#include <vector>

int main() {
    // 模拟 MPP 里三帧码流的长度（板子实测）
    const size_t frameLengths[] = {74674, 27520, 16253};
    static uint8_t mppMemory[100000]; // 假装这是 MPP 的内存
    for (size_t i = 0; i < sizeof(mppMemory); i++) mppMemory[i] = static_cast<uint8_t>(i);

    std::vector<uint8_t> data; // 和项目里一样：循环外声明，每帧复用
    for (size_t len : frameLengths) {
        data.clear();
        const uint8_t* before = data.data();
        const uint8_t* pos    = mppMemory;
        data.assign(pos, pos + len);
        printf("assign %6zu 字节 → size=%6zu capacity=%6zu  %s\n", len, data.size(), data.capacity(),
            data.data() == before ? "复用原来的内存" : "重新申请了内存");
    }

    // 三种用法
    std::vector<uint8_t> v = {1, 2, 3};
    v.assign(5, 0xFF);
    printf("assign(5, 0xFF) → size=%zu, v[0]=0x%02X\n", v.size(), v[0]);
    v.assign({7, 8});
    printf("assign({7, 8})  → size=%zu, v[0]=%d v[1]=%d\n", v.size(), v[0], v[1]);
    // insert 是追加，不是替换
    v.insert(v.end(), mppMemory, mppMemory + 3);
    printf("insert 3 个     → size=%zu（原来的 2 个还在）\n", v.size());
    return 0;
}
```
