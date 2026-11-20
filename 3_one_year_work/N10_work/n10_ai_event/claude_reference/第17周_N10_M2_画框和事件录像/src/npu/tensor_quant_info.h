#pragma once

#include <cstdint>

// 单独放一个头文件：后处理（Mac 单测也编）要用它，但不能 include rknn_api.h（Mac 上没有）
struct TensorQuantInfo {
    int32_t zeroPoint = 0;  // rknn_tensor_attr.zp
    float scale = 0.0f;     // rknn_tensor_attr.scale；float 值 = (int8 值 - zeroPoint) * scale
};
