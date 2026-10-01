// 测试 RK3588 开发环境：
//   1. C++17 能交叉编译
//   2. 能链接 MPP 库，并在板子上创建 H.264 硬件编码器
//   3. 能在 CLion 里远程断点调试（在下面的 mpp_init 那一行打断点试试）
#include <iostream>
#include <memory>
#include <string_view>
#include <sys/utsname.h>

#ifdef RK_TARGET
#include <rockchip/rk_mpi.h>

// 用 RAII 管理 MppCtx：离开作用域时自动调用 mpp_destroy
struct MppCtxDeleter {
    void operator()(void *ctx) const { mpp_destroy(static_cast<MppCtx>(ctx)); }
};
using MppCtxPtr = std::unique_ptr<void, MppCtxDeleter>;

static bool test_mpp_encoder()
{
    MppCtx raw = nullptr;
    MppApi *mpi = nullptr;
    if (mpp_create(&raw, &mpi) != MPP_OK) {
        std::cerr << "mpp_create failed\n";
        return false;
    }
    MppCtxPtr ctx(raw);

    if (mpp_init(ctx.get(), MPP_CTX_ENC, MPP_VIDEO_CodingAVC) != MPP_OK) {
        std::cerr << "mpp_init H.264 encoder failed\n";
        return false;
    }
    std::cout << "MPP: H.264 hardware encoder created OK\n";
    return true;
}
#endif

int main()
{
    utsname u{};
    uname(&u);
    std::string_view machine = u.machine;

    std::cout << "Hello from " << u.nodename << " (" << machine << ", kernel "
              << u.release << ")\n";
    std::cout << "C++ standard: " << __cplusplus << "\n";

#ifdef RK_TARGET
    return test_mpp_encoder() ? 0 : 1;
#else
    std::cout << "Built for host, MPP test skipped\n";
    return 0;
#endif
}
