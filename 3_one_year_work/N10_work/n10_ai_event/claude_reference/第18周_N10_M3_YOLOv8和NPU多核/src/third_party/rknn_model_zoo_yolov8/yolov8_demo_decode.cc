// 来源：正点原子 ATK-DLRK3588 网盘 A 盘 01、AI例程/01、linux6.1AI例程/01、源码/10_yolov8/yolov8.zip 的 cpp/postprocess.cc
//（就是 rknn_model_zoo examples/yolov8/cpp/postprocess.cc）。
// 原样复制：__clip、qnt_f32_to_affine、deqnt_affine_to_f32、compute_dfl、process_i8；OBJ_CLASS_NUM 改成本文件的宏。
// 文件末尾的 runDemoYolov8Decode 是自己写的包装，用来和 npu/yolov8_post_process 的解码结果对比。
#include "third_party/rknn_model_zoo_yolov8/yolov8_demo_decode.h"

#include <math.h>
#include <stdint.h>
#include <vector>

#define OBJ_CLASS_NUM 80


inline static int32_t __clip(float val, float min, float max)
{
    float f = val <= min ? min : (val >= max ? max : val);
    return f;
}

static int8_t qnt_f32_to_affine(float f32, int32_t zp, float scale)
{
    float dst_val = (f32 / scale) + zp;
    int8_t res = (int8_t)__clip(dst_val, -128, 127);
    return res;
}

static float deqnt_affine_to_f32(int8_t qnt, int32_t zp, float scale) { return ((float)qnt - (float)zp) * scale; }

static void compute_dfl(float* tensor, int dfl_len, float* box){
    for (int b=0; b<4; b++){
        float exp_t[dfl_len];
        float exp_sum=0;
        float acc_sum=0;
        for (int i=0; i< dfl_len; i++){
            exp_t[i] = exp(tensor[i+b*dfl_len]);
            exp_sum += exp_t[i];
        }
        
        for (int i=0; i< dfl_len; i++){
            acc_sum += exp_t[i]/exp_sum *i;
        }
        box[b] = acc_sum;
    }
}

static int process_i8(int8_t *box_tensor, int32_t box_zp, float box_scale,
                      int8_t *score_tensor, int32_t score_zp, float score_scale,
                      int8_t *score_sum_tensor, int32_t score_sum_zp, float score_sum_scale,
                      int grid_h, int grid_w, int stride, int dfl_len,
                      std::vector<float> &boxes, 
                      std::vector<float> &objProbs, 
                      std::vector<int> &classId, 
                      float threshold)
{
    int validCount = 0;
    int grid_len = grid_h * grid_w;
    int8_t score_thres_i8 = qnt_f32_to_affine(threshold, score_zp, score_scale);
    int8_t score_sum_thres_i8 = qnt_f32_to_affine(threshold, score_sum_zp, score_sum_scale);

    for (int i = 0; i < grid_h; i++)
    {
        for (int j = 0; j < grid_w; j++)
        {
            int offset = i* grid_w + j;
            int max_class_id = -1;

            // 通过 score sum 起到快速过滤的作用
            if (score_sum_tensor != nullptr){
                if (score_sum_tensor[offset] < score_sum_thres_i8){
                    continue;
                }
            }

            int8_t max_score = -score_zp;
            for (int c= 0; c< OBJ_CLASS_NUM; c++){
                if ((score_tensor[offset] > score_thres_i8) && (score_tensor[offset] > max_score))
                {
                    max_score = score_tensor[offset];
                    max_class_id = c;
                }
                offset += grid_len;
            }

            // compute box
            if (max_score> score_thres_i8){
                offset = i* grid_w + j;
                float box[4];
                float before_dfl[dfl_len*4];
                for (int k=0; k< dfl_len*4; k++){
                    before_dfl[k] = deqnt_affine_to_f32(box_tensor[offset], box_zp, box_scale);
                    offset += grid_len;
                }
                compute_dfl(before_dfl, dfl_len, box);

                float x1,y1,x2,y2,w,h;
                x1 = (-box[0] + j + 0.5)*stride;
                y1 = (-box[1] + i + 0.5)*stride;
                x2 = (box[2] + j + 0.5)*stride;
                y2 = (box[3] + i + 0.5)*stride;
                w = x2 - x1;
                h = y2 - y1;
                boxes.push_back(x1);
                boxes.push_back(y1);
                boxes.push_back(w);
                boxes.push_back(h);

                objProbs.push_back(deqnt_affine_to_f32(max_score, score_zp, score_scale));
                classId.push_back(max_class_id);
                validCount ++;
            }
        }
    }
    return validCount;
}

// ---- 以下自己写 ----
bool runDemoYolov8Decode(const std::vector<Yolov8OutputBranch> &outputBranches, float boxThreshold,
                         std::vector<CandidateObject> &candidateObjects) {
    candidateObjects.clear();
    std::vector<float> boxes;  // demo 的格式：每个框 x1, y1, w, h
    std::vector<float> objProbs;
    std::vector<int> classId;
    for (const Yolov8OutputBranch &branch : outputBranches) {
        if (branch.boxData == nullptr || branch.classScoreData == nullptr) {
            return false;
        }
        process_i8(const_cast<int8_t *>(branch.boxData), branch.boxQuantInfo.zeroPoint, branch.boxQuantInfo.scale,
                   const_cast<int8_t *>(branch.classScoreData), branch.classScoreQuantInfo.zeroPoint,
                   branch.classScoreQuantInfo.scale, const_cast<int8_t *>(branch.scoreSumData),
                   branch.scoreSumQuantInfo.zeroPoint, branch.scoreSumQuantInfo.scale, branch.gridHeight,
                   branch.gridWidth, branch.stridePixels, branch.dflBinCount, boxes, objProbs, classId, boxThreshold);
    }
    for (size_t objectIndex = 0; objectIndex < objProbs.size(); objectIndex++) {
        CandidateObject candidateObject;
        candidateObject.classIndex = classId[objectIndex];
        candidateObject.confidence = objProbs[objectIndex];
        candidateObject.left = boxes[objectIndex * 4];
        candidateObject.top = boxes[objectIndex * 4 + 1];
        candidateObject.right = candidateObject.left + boxes[objectIndex * 4 + 2];
        candidateObject.bottom = candidateObject.top + boxes[objectIndex * 4 + 3];
        candidateObjects.push_back(candidateObject);
    }
    return true;
}
