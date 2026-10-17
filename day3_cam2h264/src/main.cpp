// day3_cam2h264：先用 FindCamera 找出摄像头节点（M2 之前的准备）
// 写 M2 时把这个文件整个替换成"具体编码day3_cam2h264的流程.md"里的 M2 代码，
// 可以把下面的查找逻辑留下来，当作 kDevice 的默认值
#include "camera/find_camera.h"

int main() {
    FindCamera find_camera;
    find_camera.printAllCameraInfo();
    return 0;
}
