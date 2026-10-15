// 【扫一眼】fread 一块，M3 里自己写过
#include "source/read_stream.h"

#include <cstdio> // fopen / fread / ferror / printf

/**
 * @brief 打开码流文件
 * @param path       文件路径
 * @param chunkSize  每次读多少字节
 * @return 成功返回 true；打开失败打印原因，返回 false
 * @note 【扫一眼】
 */
bool ReadStream::open(const char* path, size_t chunkSize) {
    // 为什么用 reset：
    // file_ 是成员变量，ReadStream 构造时就已经存在了（里面是空的）。fopen 返回的是裸指针 FILE*，
    // 而 unique_ptr 不能用 = 直接接收裸指针（file_ = fopen(...) 编译不过），
    // 要用 reset 明确写出"把这个指针交给 file_ 管"，也可以写成 file_ = FilePtr(fopen(...))
    // reset(新指针) 做两件事：
    //   ① file_ 里原来已经有文件（同一个 reader 第二次调用 open）→ 先调用 FileCloser，fclose 掉旧文件
    //   ② 接管新指针，以后由 file_ 负责 fclose（ReadStream 析构时自动关闭）
    // fopen 失败时返回 nullptr，reset(nullptr) 后 file_ 是空的，下面的 if (!file_) 就能判断出来；
    // unique_ptr 为空时析构不会调用 FileCloser，所以不会出现 fclose(NULL)
    file_.reset(fopen(path, "rb"));
    if (!file_) {
        printf("open %s failed\n", path);
        return false;
    }
    chunkSize_ = chunkSize;
    return true;
}

/**
 * @brief fread 一块；读不满 chunkSize 就是文件读完了
 * @param data    输出参数：这一块的数据，size() 就是真正读到的字节数
 * @param isLast  输出参数：true = 这是最后一块
 * @return 成功返回 true；读出错返回 false
 * @note 【扫一眼】
 */
bool ReadStream::read(std::vector<uint8_t>& data, bool& isLast) {
    data.resize(chunkSize_);
    const size_t readSize = fread(data.data(), 1, chunkSize_, file_.get());
    if (ferror(file_.get())) {
        perror("    fread");
        return false;
    }
    data.resize(readSize); // 最后一块可能读不满，size() 就是真正读到的字节数
    isLast = readSize < chunkSize_; // 读不满，就是文件读完了
    return true;
}
