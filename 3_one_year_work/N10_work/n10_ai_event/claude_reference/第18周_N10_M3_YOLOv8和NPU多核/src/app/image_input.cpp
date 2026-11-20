#include "app/image_input.h"

#include <memory>

#include "app/app_config.h"
#include "common/log.h"
#include "third_party/stb/stb_image.h"

static const char *const kTag = "ImageInput";

struct StbImageDeleter {
    void operator()(uint8_t *pixels) const { stbi_image_free(pixels); }
};
using StbImagePtr = std::unique_ptr<uint8_t, StbImageDeleter>;

bool loadImageAsModelInput(const char *imagePath, int modelWidth, int modelHeight, ImageModelInput &imageInput) {
    int fileChannelCount = 0;
    // 最后一个参数 3：不管文件几个通道，都转成 RGB 3 通道（OpenCV imread 是 BGR，demo 才要 cvtColor）
    StbImagePtr sourceRgbPtr(
            stbi_load(imagePath, &imageInput.sourceWidth, &imageInput.sourceHeight, &fileChannelCount, 3));
    if (!sourceRgbPtr) {
        LOGE(kTag, "读图片失败：%s（%s）", imagePath, stbi_failure_reason());
        return false;
    }
    imageInput.letterboxInfo =
            computeLetterboxInfo(imageInput.sourceWidth, imageInput.sourceHeight, modelWidth, modelHeight, true);
    letterboxRgbOnCpu(sourceRgbPtr.get(), imageInput.sourceWidth, imageInput.sourceHeight, imageInput.letterboxInfo,
                      modelWidth, modelHeight, kLetterboxPadValue, imageInput.modelInputRgb);
    LOGI(kTag, "图片 %s：%dx%d → 缩放到 %dx%d，补边 左 %d 上 %d", imagePath, imageInput.sourceWidth,
         imageInput.sourceHeight, imageInput.letterboxInfo.resizedWidth, imageInput.letterboxInfo.resizedHeight,
         imageInput.letterboxInfo.padLeftPixels, imageInput.letterboxInfo.padTopPixels);
    return true;
}
