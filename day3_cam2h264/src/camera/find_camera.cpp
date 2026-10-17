#include "camera/find_camera.h"

#include "common/log.h"  // LOGD / LOGI / LOGE：带颜色的日志
#include <cstdio>        // printf / fopen / fgets
#include <cstring>       // strncmp
#include <fcntl.h>       // open
#include <linux/media.h> // Media Controller API：media-ctl 底层用的就是它
#include <sys/ioctl.h>   // ioctl
#include <unistd.h>      // close
#include <utility>       // std::move

namespace {

    // 文件描述符的 RAII 包装：离开作用域自动 close，中途 return 也不会漏关
    struct FdGuard {
        int fd = -1;
        explicit FdGuard(int f) : fd(f) {}
        ~FdGuard() {
            if (fd >= 0) {
                close(fd);
            }
        }
        FdGuard(const FdGuard&)            = delete;
        FdGuard& operator=(const FdGuard&) = delete;
    };

    // Rockchip ISP 那张图里，我们要找的三个实体的名字（media-ctl -p 实测）
    const char* const kIspEntity      = "rkisp-isp-subdev"; // ISP 本身
    const char* const kMainPathEntity = "rkisp_mainpath";   // 主出口
    const char* const kSelfPathEntity = "rkisp_selfpath";   // 副出口

    const char* const kTag = "FindCamera"; // 日志标签（LOGD / LOGI / LOGE 的第一个参数）

} // namespace

/**
 * @brief 找出所有"已经接通到 ISP"的摄像头
 * @param[out] cameras  传出找到的摄像头，每个摄像头一项；调用前的内容会被清空
 * @param[in]  verbose  true = 打印查找过程
 * @return 至少找到一个摄像头返回 true；一个都没找到返回 false
 * @note 【精读】①～④ 四步和手动用 media-ctl 排查的顺序一样，对照 doc/v4l2-ctl和media-ctl用法.md 第 3 节看
 */
bool FindCamera::find(std::vector<CameraNode>& cameras, bool verbose) {
    cameras.clear();
    // ① 读出所有的图：/dev/media0、media1 …… 不存在的号码直接跳过
    std::vector<Graph> graphs;
    for (int n = 0; n < 64; n++) {
        Graph graph;
        if (load_graph("/dev/media" + std::to_string(n), graph)) {
            if (verbose) {
                LOGD(kTag, "%-12s model=%-18s %zu 个实体", graph.path.c_str(), graph.model.c_str(),
                    graph.entities.size());
            }
            graphs.push_back(std::move(graph));
        }
    }

    // ② 找摄像头：类型是 MEDIA_ENT_F_CAM_SENSOR 的实体（media-ctl -p 里显示 "subtype Sensor"）
    for (const Graph& cifGraph : graphs) {
        for (const Entity& sensor : cifGraph.entities) {
            if (sensor.type != MEDIA_ENT_F_CAM_SENSOR) {
                continue;
            }
            if (verbose) {
                LOGD(kTag, "摄像头 \"%s\" 在 %s（model=%s）", sensor.name.c_str(), cifGraph.path.c_str(),
                    cifGraph.model.c_str());
            }

            // ③ 找处理它的 ISP：ISP 图里有一个和 CIF 图 model 同名的实体（两张图之间的"桥"），
            //    而且这个"桥"连到 ISP 的线是接通的。两个 ISP 图的 model 都叫 rkisp0（实测），不能靠 model 区分
            for (const Graph& ispGraph : graphs) {
                const Entity* bridge   = find_entity(ispGraph, cifGraph.model);
                const Entity* isp      = find_entity(ispGraph, kIspEntity);
                const Entity* mainPath = find_entity(ispGraph, kMainPathEntity);
                const Entity* selfPath = find_entity(ispGraph, kSelfPathEntity);
                if (!bridge || !isp || !mainPath || !has_enabled_link(ispGraph, *bridge, *isp)) {
                    continue;
                }

                // ④ 设备号 → 设备文件名
                CameraNode node;
                node.sensorName   = sensor.name;
                node.sensorSubdev = dev_path(sensor.major, sensor.minor);
                node.cifMedia     = cifGraph.path;
                node.ispMedia     = ispGraph.path;
                node.mainPath     = dev_path(mainPath->major, mainPath->minor);
                node.selfPath     = selfPath ? dev_path(selfPath->major, selfPath->minor) : "";
                if (verbose) {
                    LOGD(kTag, "  → 接到 %s，mainpath %s，selfpath %s", node.ispMedia.c_str(), node.mainPath.c_str(),
                        node.selfPath.c_str());
                }
                cameras.push_back(node);
            }
        }
    }
    return !cameras.empty();
}

/**
 * @brief 查找并打印所有摄像头的信息
 * @note 【扫一眼】只是给人看的；程序里要拿节点去 open，用 find()
 */
void FindCamera::printAllCameraInfo() {
    std::vector<CameraNode> cameras{0};
    if (!find(cameras, true)) { // true：把查找过程打印出来
        LOGE(kTag, "没有找到接通到 ISP 的摄像头（先看 dmesg | grep imx415）");
        return;
    }

    LOGI(kTag, "找到 %zu 个摄像头：", cameras.size());
    for (const CameraNode& c : cameras) {
        LOGI(kTag, "  摄像头  : %s（子设备 %s）", c.sensorName.c_str(), c.sensorSubdev.c_str());
        LOGI(kTag, "  链路    : %s → %s", c.cifMedia.c_str(), c.ispMedia.c_str());
        LOGI(kTag, "  主出口  : %s   ← 录像用这个", c.mainPath.c_str());
        LOGI(kTag, "  副出口  : %s", c.selfPath.c_str());
    }
}

/**
 * @brief 读一张图：基本信息 + 所有实体
 * @param[in]  path   media 设备文件，例如 "/dev/media1"
 * @param[out] graph  传出读到的图
 * @return 成功返回 true；文件不存在或者不是 media 设备返回 false
 * @note 【扫一眼】遍历实体的写法：id 填"上一个 id | MEDIA_ENT_ID_FLAG_NEXT"，内核就返回下一个，直到返回失败
 */
bool FindCamera::load_graph(const std::string& path, Graph& graph) {
    FdGuard fd(open(path.c_str(), O_RDONLY | O_CLOEXEC));
    if (fd.fd < 0) {
        return false;
    }
    media_device_info info = {};
    if (ioctl(fd.fd, MEDIA_IOC_DEVICE_INFO, &info) != 0) {
        return false;
    }
    graph.path  = path;
    graph.model = info.model;

    unsigned lastId = 0;
    while (true) {
        media_entity_desc desc = {};
        desc.id                = lastId | MEDIA_ENT_ID_FLAG_NEXT;
        if (ioctl(fd.fd, MEDIA_IOC_ENUM_ENTITIES, &desc) != 0) {
            break; // 没有下一个了
        }
        Entity e;
        e.id    = desc.id;
        e.name  = desc.name;
        e.type  = desc.type;
        e.major = desc.dev.major;
        e.minor = desc.dev.minor;
        e.pads  = desc.pads;
        e.links = desc.links;
        graph.entities.push_back(e);
        lastId = desc.id;
    }
    return true;
}

/**
 * @brief 在图里按名字找实体
 * @param[in] graph  在哪张图里找
 * @param[in] name   实体名字（完全相同才算）
 * @return 找到返回指向它的指针；找不到返回 nullptr
 * @note 【扫一眼】
 */
const FindCamera::Entity* FindCamera::find_entity(const Graph& graph, const std::string& name) {
    for (const Entity& e : graph.entities) {
        if (e.name == name) {
            return &e;
        }
    }
    return nullptr;
}

/**
 * @brief 判断实体 from 有没有一条"已接通"的连线连到实体 to
 * @param[in] graph  两个实体所在的图
 * @param[in] from   起点实体
 * @param[in] to     终点实体
 * @return 有接通的连线返回 true；没有、或者查询失败返回 false
 * @note 【扫一眼】要先按 from.pads / from.links 准备好数组，内核往里填
 */
bool FindCamera::has_enabled_link(const Graph& graph, const Entity& from, const Entity& to) {
    if (from.links == 0) {
        return false;
    }
    FdGuard fd(open(graph.path.c_str(), O_RDONLY | O_CLOEXEC));
    if (fd.fd < 0) {
        return false;
    }
    std::vector<media_pad_desc> pads(from.pads);
    std::vector<media_link_desc> links(from.links);
    media_links_enum req = {};
    req.entity           = from.id;
    req.pads             = pads.data();
    req.links            = links.data(); // 内核只返回从 from "出去"的连线
    if (ioctl(fd.fd, MEDIA_IOC_ENUM_LINKS, &req) != 0) {
        return false;
    }
    for (const media_link_desc& link : links) {
        if (link.sink.entity == to.id && (link.flags & MEDIA_LNK_FL_ENABLED)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief 设备号 → 设备文件名
 * @param[in] major  主设备号，例如 81
 * @param[in] minor  次设备号，例如 22
 * @return 例如 "/dev/video22"；查不到返回空字符串
 * @note 【扫一眼】/sys/dev/char/81:22/uevent 里有一行 DEVNAME=video22（实测）
 */
std::string FindCamera::dev_path(unsigned major, unsigned minor) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/dev/char/%u:%u/uevent", major, minor);
    FILE* fp = fopen(path, "r");
    if (!fp) {
        return "";
    }
    char line[128];
    std::string name;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "DEVNAME=", 8) == 0) {
            name = std::string("/dev/") + (line + 8);
            name.erase(name.find_last_not_of('\n') + 1); // 去掉行尾换行
        }
    }
    fclose(fp);
    return name;
}
