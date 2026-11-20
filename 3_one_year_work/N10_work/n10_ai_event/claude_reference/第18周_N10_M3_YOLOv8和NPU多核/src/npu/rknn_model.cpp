#include "npu/rknn_model.h"

#include <cstdio>
#include <cstring>

#include "common/log.h"

static const char *const kTag = "RknnModel";

static bool readWholeFile(const char *filePath, std::vector<uint8_t> &fileBytes) {
    FILE *file = fopen(filePath, "rb");
    if (file == nullptr) {
        return false;
    }
    fseek(file, 0, SEEK_END);
    long fileSizeBytes = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (fileSizeBytes <= 0) {
        fclose(file);
        return false;
    }
    fileBytes.resize(static_cast<size_t>(fileSizeBytes));
    size_t readBytes = fread(fileBytes.data(), 1, fileBytes.size(), file);
    fclose(file);
    return readBytes == fileBytes.size();
}

RknnModel::~RknnModel() {
    destroyContext();
}

void RknnModel::destroyContext() {
    if (!hasContext) {
        return;
    }
    releaseOutputs();
    rknn_destroy(context);
    context = 0;
    hasContext = false;
}

bool RknnModel::load(const char *requestedModelPath) {
    destroyContext();
    if (!readWholeFile(requestedModelPath, modelFileBytes)) {
        LOGE(kTag, "读模型文件失败：%s", requestedModelPath);
        return false;
    }
    // rknn_init 的 model 参数是 void*（不是 const），所以传 data()
    int ret = rknn_init(&context, modelFileBytes.data(), static_cast<uint32_t>(modelFileBytes.size()), 0, nullptr);
    if (ret != RKNN_SUCC) {
        LOGE(kTag, "rknn_init 失败 ret=%d（模型和 librknnrt 版本不配、或不是 RK3588 的模型时也会失败）", ret);
        return false;
    }
    hasContext = true;

    rknn_sdk_version sdkVersion;
    memset(&sdkVersion, 0, sizeof(sdkVersion));
    ret = rknn_query(context, RKNN_QUERY_SDK_VERSION, &sdkVersion, sizeof(sdkVersion));
    if (ret != RKNN_SUCC) {
        LOGE(kTag, "rknn_query(SDK_VERSION) 失败 ret=%d", ret);
        destroyContext();
        return false;
    }
    runtimeVersion = sdkVersion.api_version;
    driverVersion = sdkVersion.drv_version;

    if (!queryTensorAttrs()) {
        destroyContext();
        return false;
    }
    LOGI(kTag, "加载 %s：%zu 字节，输入 %dx%dx%d，输出 %zu 个；runtime %s，驱动 %s", requestedModelPath,
         modelFileBytes.size(), inputWidth, inputHeight, inputChannelCount, outputAttrs.size(), runtimeVersion.c_str(),
         driverVersion.c_str());
    return true;
}

bool RknnModel::duplicateFrom(RknnModel &sourceModel) {
    destroyContext();
    if (!sourceModel.hasContext) {
        LOGE(kTag, "源模型还没 load");
        return false;
    }
    int ret = rknn_dup_context(&sourceModel.context, &context);
    if (ret != RKNN_SUCC) {
        LOGE(kTag, "rknn_dup_context 失败 ret=%d", ret);
        return false;
    }
    hasContext = true;
    runtimeVersion = sourceModel.runtimeVersion;
    driverVersion = sourceModel.driverVersion;
    if (!queryTensorAttrs()) {
        destroyContext();
        return false;
    }
    return true;
}

bool RknnModel::setCoreMask(rknn_core_mask coreMask) {
    if (!hasContext) {
        LOGE(kTag, "还没 load");
        return false;
    }
    int ret = rknn_set_core_mask(context, coreMask);
    if (ret != RKNN_SUCC) {
        LOGE(kTag, "rknn_set_core_mask(0x%x) 失败 ret=%d", static_cast<unsigned>(coreMask), ret);
        return false;
    }
    return true;
}

bool RknnModel::queryTensorAttrs() {
    rknn_input_output_num ioCount;
    memset(&ioCount, 0, sizeof(ioCount));
    int ret = rknn_query(context, RKNN_QUERY_IN_OUT_NUM, &ioCount, sizeof(ioCount));
    if (ret != RKNN_SUCC) {
        LOGE(kTag, "rknn_query(IN_OUT_NUM) 失败 ret=%d", ret);
        return false;
    }
    if (ioCount.n_input != 1) {
        LOGE(kTag, "只支持 1 个输入，这个模型有 %u 个", ioCount.n_input);
        return false;
    }

    inputAttrs.assign(ioCount.n_input, rknn_tensor_attr{});
    for (uint32_t inputIndex = 0; inputIndex < ioCount.n_input; inputIndex++) {
        inputAttrs[inputIndex].index = inputIndex;
        ret = rknn_query(context, RKNN_QUERY_INPUT_ATTR, &inputAttrs[inputIndex], sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC) {
            LOGE(kTag, "rknn_query(INPUT_ATTR %u) 失败 ret=%d", inputIndex, ret);
            return false;
        }
    }
    outputAttrs.assign(ioCount.n_output, rknn_tensor_attr{});
    for (uint32_t outputIndex = 0; outputIndex < ioCount.n_output; outputIndex++) {
        outputAttrs[outputIndex].index = outputIndex;
        ret = rknn_query(context, RKNN_QUERY_OUTPUT_ATTR, &outputAttrs[outputIndex], sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC) {
            LOGE(kTag, "rknn_query(OUTPUT_ATTR %u) 失败 ret=%d", outputIndex, ret);
            return false;
        }
    }

    // 查到的输入属性是模型内部的排布；demo 同样按 fmt 区分 NCHW / NHWC 取宽高
    const rknn_tensor_attr &inputAttr = inputAttrs[0];
    if (inputAttr.fmt == RKNN_TENSOR_NCHW) {
        inputChannelCount = static_cast<int>(inputAttr.dims[1]);
        inputHeight = static_cast<int>(inputAttr.dims[2]);
        inputWidth = static_cast<int>(inputAttr.dims[3]);
    } else {
        inputHeight = static_cast<int>(inputAttr.dims[1]);
        inputWidth = static_cast<int>(inputAttr.dims[2]);
        inputChannelCount = static_cast<int>(inputAttr.dims[3]);
    }
    outputs.assign(ioCount.n_output, rknn_output{});
    return true;
}

bool RknnModel::runWithRgbInput(const uint8_t *rgbData, size_t sizeBytes) {
    if (!hasContext) {
        LOGE(kTag, "还没 load");
        return false;
    }
    if (hasPendingOutputs) {
        LOGE(kTag, "上一次的输出还没 releaseOutputs");
        return false;
    }
    size_t expectedSizeBytes = static_cast<size_t>(inputWidth) * inputHeight * inputChannelCount;
    if (sizeBytes != expectedSizeBytes) {
        LOGE(kTag, "输入 %zu 字节，模型要 %zu 字节", sizeBytes, expectedSizeBytes);
        return false;
    }

    rknn_input input;
    memset(&input, 0, sizeof(input));
    input.index = 0;
    input.buf = const_cast<uint8_t *>(rgbData);  // API 的 buf 是 void*，内部只读（会拷贝或转换一份）
    input.size = static_cast<uint32_t>(sizeBytes);
    input.pass_through = 0;  // 0 = 让 runtime 按模型配置做格式转换和归一化（mean/std 在转换时写进了模型）
    input.type = RKNN_TENSOR_UINT8;
    input.fmt = RKNN_TENSOR_NHWC;
    int ret = rknn_inputs_set(context, 1, &input);
    if (ret != RKNN_SUCC) {
        LOGE(kTag, "rknn_inputs_set 失败 ret=%d", ret);
        return false;
    }

    ret = rknn_run(context, nullptr);
    if (ret != RKNN_SUCC) {
        LOGE(kTag, "rknn_run 失败 ret=%d", ret);
        return false;
    }

    for (size_t outputIndex = 0; outputIndex < outputs.size(); outputIndex++) {
        memset(&outputs[outputIndex], 0, sizeof(rknn_output));
        outputs[outputIndex].index = static_cast<uint32_t>(outputIndex);
        outputs[outputIndex].want_float = 0;  // 要原始 int8，后处理自己用 zp/scale 还原
    }
    ret = rknn_outputs_get(context, static_cast<uint32_t>(outputs.size()), outputs.data(), nullptr);
    if (ret != RKNN_SUCC) {
        LOGE(kTag, "rknn_outputs_get 失败 ret=%d", ret);
        return false;
    }
    hasPendingOutputs = true;
    return true;
}

void RknnModel::releaseOutputs() {
    if (!hasPendingOutputs) {
        return;
    }
    rknn_outputs_release(context, static_cast<uint32_t>(outputs.size()), outputs.data());
    hasPendingOutputs = false;
}

const int8_t *RknnModel::getOutputData(int outputIndex) const {
    if (!hasPendingOutputs || outputIndex < 0 || outputIndex >= getOutputCount()) {
        return nullptr;
    }
    return static_cast<const int8_t *>(outputs[outputIndex].buf);
}

TensorQuantInfo RknnModel::getOutputQuantInfo(int outputIndex) const {
    TensorQuantInfo quantInfo;
    if (outputIndex < 0 || outputIndex >= getOutputCount()) {
        return quantInfo;
    }
    quantInfo.zeroPoint = outputAttrs[outputIndex].zp;
    quantInfo.scale = outputAttrs[outputIndex].scale;
    return quantInfo;
}

std::vector<int> RknnModel::getOutputDims(int outputIndex) const {
    std::vector<int> dims;
    if (outputIndex < 0 || outputIndex >= getOutputCount()) {
        return dims;
    }
    const rknn_tensor_attr &attr = outputAttrs[outputIndex];
    for (uint32_t dimIndex = 0; dimIndex < attr.n_dims; dimIndex++) {
        dims.push_back(static_cast<int>(attr.dims[dimIndex]));
    }
    return dims;
}

std::string RknnModel::getRuntimeVersion() const {
    return runtimeVersion;
}

std::string RknnModel::getDriverVersion() const {
    return driverVersion;
}

static void printOneTensorAttr(const char *directionName, const rknn_tensor_attr &attr) {
    std::string dimsText;
    for (uint32_t dimIndex = 0; dimIndex < attr.n_dims; dimIndex++) {
        if (dimIndex > 0) {
            dimsText += ", ";
        }
        dimsText += std::to_string(attr.dims[dimIndex]);
    }
    LOGI(kTag, "%s[%u] name=%s dims=[%s] size=%u fmt=%s type=%s qnt=%s zp=%d scale=%f", directionName, attr.index,
         attr.name, dimsText.c_str(), attr.size, get_format_string(attr.fmt), get_type_string(attr.type),
         get_qnt_type_string(attr.qnt_type), attr.zp, attr.scale);
}

void RknnModel::printTensorAttrs() const {
    for (const rknn_tensor_attr &attr : inputAttrs) {
        printOneTensorAttr("输入", attr);
    }
    for (const rknn_tensor_attr &attr : outputAttrs) {
        printOneTensorAttr("输出", attr);
    }
}
