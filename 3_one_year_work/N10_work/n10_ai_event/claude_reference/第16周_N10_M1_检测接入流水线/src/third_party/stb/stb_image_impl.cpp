// stb_image 是单头文件库：整个工程里只能有一个 .cpp 定义这个宏，生成函数实现
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include "third_party/stb/stb_image.h"
