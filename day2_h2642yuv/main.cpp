#include <iostream>
#include <memory>

#include <rockchip/rk_mpi.h>

struct MppCtxDeleter {
    void operator()(void *ctx) { mpp_destroy(ctx); }
};
using MppCtxPtr = std::unique_ptr<void, MppCtxDeleter>;

static bool testMppDecoder() {
    MppCtx raw = nullptr;
    MppApi *mpi = nullptr;
    if (mpp_create(&raw, &mpi) != MPP_OK) {
        std::cerr << "Failed to create MppApi" << std::endl;
        return false;
    }
    MppCtxPtr mppCtxPtr(raw);
    if (mpp_init(mppCtxPtr.get(), MPP_CTX_DEC, MPP_VIDEO_CodingAVC) != MPP_OK) {
        std::cerr << "Failed to init h264 decoder" << std::endl;
        return false;
    }
    std::cout << "h264 hardware decoder created OK " << std::endl;
    // 销毁解码器
    mpp_destroy(mppCtxPtr.get());
    return true;
}

int main() {
    testMppDecoder();
    return 0;
}
