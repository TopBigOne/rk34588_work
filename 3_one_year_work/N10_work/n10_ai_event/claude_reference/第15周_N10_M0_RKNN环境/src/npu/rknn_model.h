#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <rknn/rknn_api.h>

struct TensorQuantInfo {
    int32_t zeroPoint = 0;  // rknn_tensor_attr.zp
    float scale = 0.0f;     // rknn_tensor_attr.scale；float 值 = (int8 值 - zeroPoint) * scale
};

// 一个 rknn_context 的 RAII 封装。输入固定按 UINT8 + NHWC（RGB 排好的一整块）送进去，输出拿原始 int8（want_float = 0）。
// 不可复制：context 只能 rknn_destroy 一次。
class RknnModel {
public:
    RknnModel() = default;
    ~RknnModel();
    RknnModel(const RknnModel &) = delete;
    RknnModel &operator=(const RknnModel &) = delete;

    bool load(const char *requestedModelPath);
    bool runWithRgbInput(const uint8_t *rgbData, size_t sizeBytes);
    void releaseOutputs();  // 每次 runWithRgbInput 成功后、下一次 run 之前必须调

    int getInputWidth() const { return inputWidth; }
    int getInputHeight() const { return inputHeight; }
    int getInputChannelCount() const { return inputChannelCount; }
    int getOutputCount() const { return static_cast<int>(outputAttrs.size()); }
    const int8_t *getOutputData(int outputIndex) const;
    TensorQuantInfo getOutputQuantInfo(int outputIndex) const;
    std::string getRuntimeVersion() const;  // RKNN_QUERY_SDK_VERSION 的 api_version
    std::string getDriverVersion() const;   // 同一次查询的 drv_version
    void printTensorAttrs() const;

private:
    bool queryTensorAttrs();
    void destroyContext();

    rknn_context context = 0;
    bool hasContext = false;
    std::vector<uint8_t> modelFileBytes;  // rknn_init 之后官方 demo 仍然留着，这里也留到析构（推断：init 后可以释放，没核对文档）
    std::vector<rknn_tensor_attr> inputAttrs;
    std::vector<rknn_tensor_attr> outputAttrs;
    std::vector<rknn_output> outputs;
    bool hasPendingOutputs = false;  // rknn_outputs_get 成功、还没 rknn_outputs_release
    int inputWidth = 0;
    int inputHeight = 0;
    int inputChannelCount = 0;
    std::string runtimeVersion;
    std::string driverVersion;
};
