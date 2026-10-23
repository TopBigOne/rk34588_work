// 实验 1：mmap 一个普通文件，用指针改内容，文件真的变了
//
// 看点：mmap 以后，"读写一段内存" = "读写 fd 背后的东西"（这里是文件），中间没有 read / write，没有拷贝。
// 讲解见 ../doc/open_ioctl_mmap是什么.md 第 5 节。
//
// Mac 上编译运行：  cc -o /tmp/mmap_file_demo mmap_file_demo.c && cd /tmp && ./mmap_file_demo && cat hello.txt
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

int main(void) {
    const char* filePath = "hello.txt";
    const size_t kFileSizeBytes = 12;

    // 第 1 步 open：拿到文件的"把手"（fd）。O_CREAT | O_TRUNC：没有就创建，有就清空
    int fileFd = open(filePath, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fileFd < 0) {
        perror("open");
        return 1;
    }
    printf("step 1 open:  fileFd = %d\n", fileFd);

    // 先用老办法 write 写 12 个字节进去
    write(fileFd, "hello world\n", kFileSizeBytes);

    // 第 2 步 mmap：把这个文件放进本进程的地址空间，返回一个指针
    //   MAP_SHARED：通过指针改的内容，会同步回文件（MAP_PRIVATE 则只改自己的副本）
    char* fileAddr = mmap(NULL, kFileSizeBytes, PROT_READ | PROT_WRITE, MAP_SHARED, fileFd, 0);
    if (fileAddr == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    printf("step 2 mmap:  fileAddr = %p\n", (void*)fileAddr);

    // 第 3 步：不用 read()，直接用指针读文件内容
    printf("step 3 read through pointer: \"%.5s\"\n", fileAddr);

    // 第 4 步：用指针"写内存"，其实就是在写文件
    memcpy(fileAddr, "HELLO", 5);
    printf("step 4 memcpy(fileAddr, \"HELLO\", 5)\n");

    // 第 5 步：解除映射、关 fd（和 open / mmap 的顺序相反）
    munmap(fileAddr, kFileSizeBytes);
    close(fileFd);
    printf("step 5 munmap + close. now run: cat %s\n", filePath);
    return 0;
}
