/*
 * FindCamera：自动找出摄像头对应的 /dev/videoN，不用在代码里写死号码
 *
 * 为什么需要它：
 *   板子上有两个 ISP，同名的 rkisp_mainpath 有两个（video22、video31），只有接了摄像头的那个能出图；
 *   号码是开机时按注册顺序编的，换系统版本、换摄像头接口都会变。
 *
 * 怎么找（和手动用 media-ctl 排查的步骤一样，改成在程序里调 Media Controller API）：
 *   ① 打开每一个 /dev/mediaN，列出所有实体（MEDIA_IOC_DEVICE_INFO + MEDIA_IOC_ENUM_ENTITIES）
 *   ② 找到类型是摄像头（MEDIA_ENT_F_CAM_SENSOR）的实体，记下它所在那张图的 model（例如 rkcif-mipi-lvds2）
 *   ③ 找到含有"同名实体"的 ISP 图，并确认这个实体连到 ISP 的线是接通的（MEDIA_IOC_ENUM_LINKS）
 *   ④ 把 rkisp_mainpath 的设备号（81:22）换成文件名：读 /sys/dev/char/81:22/uevent 里的 DEVNAME
 * 详细说明见 doc/程序里怎么找到摄像头节点.md
 *
 * 用法：
 *   FindCamera finder;
 *   finder.printAllCameraInfo();                     // 只想看一眼：打印找到的所有摄像头
 *
 *   std::vector<CameraNode> cameras;
 *   if (!finder.find(cameras)) return -1;            // 一个摄像头都没找到
 *   int fd = open(cameras[0].mainPath.c_str(), O_RDWR | O_CLOEXEC);   // "/dev/video22"
 */
#pragma once

#include <string>
#include <vector>

// 找到的一个摄像头：从摄像头本身到 ISP 出口，整条链路上的设备文件
struct CameraNode {
    std::string sensorName;   // 摄像头实体的名字，例如 "m00_b_imx415 3-001a"（I2C 总线 3、地址 0x1a）
    std::string sensorSubdev; // 摄像头的子设备文件，例如 "/dev/v4l-subdev2"（调曝光、翻转等控制项用）
    std::string cifMedia;     // 摄像头所在的那张图（CIF），例如 "/dev/media1"
    std::string ispMedia;     // 处理它的 ISP 那张图，例如 "/dev/media2"
    std::string mainPath;     // ⭐ ISP 主出口（大图），例如 "/dev/video22"，录像用这个
    std::string selfPath;     // ISP 副出口（小图，最宽 1920），例如 "/dev/video23"
};

class FindCamera {
public:
    // 没有要特别初始化或释放的资源，让编译器自动生成。
    // 只写 FindCamera(); 不写实现的话，链接时会报 undefined reference to `FindCamera::FindCamera()'
    FindCamera()  = default;
    ~FindCamera() = default;
    /**
     * @brief 找出所有"已经接通到 ISP"的摄像头
     *
     * 遍历 /dev/media0 ～ /dev/media63，按头文件开头说的 ①～④ 步查找。
     * 只读查询，不会改动任何设备的设置，别的程序正在用摄像头时也能调用。
     *
     * @param[out] cameras  传出找到的摄像头，每个摄像头一项；调用前的内容会被清空
     * @param[in]  verbose  true = 打印查找过程（每张图的 model、实体数，找到的链路），调试时用
     * @return 至少找到一个摄像头返回 true；一个都没找到返回 false
     *
     * @note 找不到时先查硬件：dmesg | grep imx415，看驱动有没有认到摄像头。
     * @note 只认 Rockchip ISP 的实体名字（rkisp-isp-subdev、rkisp_mainpath、rkisp_selfpath），
     *       换成别家芯片要改这几个名字，思路不变。
     */
    bool find(std::vector<CameraNode>& cameras, bool verbose = false);

    /**
     * @brief 查找并打印所有摄像头的信息（摄像头名字、子设备、链路、mainpath、selfpath）
     *
     * 内部调用 find(cameras, true)，所以会先打印查找过程，再打印汇总。
     * 找不到时打印一行 [FAIL] 提示。
     *
     * @note 只是给人看的，程序里要拿到节点去 open，请用 find()。
     */
    void printAllCameraInfo();

private:
    // 图里的一个实体（只留我们用得到的字段）
    struct Entity {
        unsigned id = 0;    // 实体编号（media-ctl -p 里 "entity 63:" 的 63）
        std::string name;   // 实体名字，例如 "rkisp_mainpath"
        unsigned type  = 0; // 实体类型，例如 MEDIA_ENT_F_CAM_SENSOR
        unsigned major = 0; // 设备号：有设备文件的实体（video 节点、子设备）才有
        unsigned minor = 0;
        unsigned pads  = 0; // 端口个数，查连线时要用
        unsigned links = 0; // 连线个数，查连线时要用
    };

    // 一张图（一个 /dev/mediaN）
    struct Graph {
        std::string path;  // "/dev/media2"
        std::string model; // MEDIA_IOC_DEVICE_INFO 里的 model，例如 "rkcif-mipi-lvds2"、"rkisp0"
        std::vector<Entity> entities;
    };

    /**
     * @brief 读一张图：基本信息 + 所有实体
     * @param[in]  path   media 设备文件，例如 "/dev/media1"
     * @param[out] graph  传出读到的图
     * @return 成功返回 true；文件不存在或者不是 media 设备返回 false
     */
    static bool load_graph(const std::string& path, Graph& graph);

    /**
     * @brief 在图里按名字找实体
     * @param[in] graph  在哪张图里找
     * @param[in] name   实体名字（完全相同才算）
     * @return 找到返回指向它的指针（指向 graph 里的元素，graph 不能先销毁）；找不到返回 nullptr
     */
    static const Entity* find_entity(const Graph& graph, const std::string& name);

    /**
     * @brief 判断实体 from 有没有一条"已接通"的连线连到实体 to
     * @param[in] graph  两个实体所在的图
     * @param[in] from   起点实体（连线从它的输出口出去）
     * @param[in] to     终点实体
     * @return 有接通的连线返回 true；没有、或者查询失败返回 false
     * @note MEDIA_IOC_ENUM_LINKS 只返回从实体"出去"的连线，所以要从起点查。
     */
    static bool has_enabled_link(const Graph& graph, const Entity& from, const Entity& to);

    /**
     * @brief 设备号 → 设备文件名
     * @param[in] major  主设备号，例如 81（V4L2）
     * @param[in] minor  次设备号，例如 22
     * @return 例如 "/dev/video22"；查不到返回空字符串
     * @note 读的是 /sys/dev/char/主:次/uevent 里的 DEVNAME=xxx 那一行。
     */
    static std::string dev_path(unsigned major, unsigned minor);
};
