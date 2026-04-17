#include <linux/videodev2.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 辅助函数：检查是否支持某种像素格式
int check_pixel_format(int fd, __u32 format) {
    struct v4l2_fmtdesc fmtdesc;
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmtdesc.index = 0;
    
    while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
        if (fmtdesc.pixelformat == format) {
            return 1;  // 支持该格式
        }
        fmtdesc.index++;
    }
    return 0;  // 不支持该格式
}

int main(int argc, char *argv[]) {
    const char *device_name = "/dev/video0";
    if (argc > 1) {
        device_name = argv[1];  // 可以通过命令行参数指定设备
    }

    printf("Opening video device: %s\n", device_name);
    
    int fd = open(device_name, O_RDWR);
    if (fd == -1) {
        perror("Opening video device");
        return 1;
    }

    // 查询设备能力
    struct v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == -1) {
        perror("Querying Capabilities");
        close(fd);
        return 1;
    }
    
    printf("Driver: %s\n", cap.driver);
    printf("Card: %s\n", cap.card);
    printf("Bus info: %s\n", cap.bus_info);
    printf("Capabilities: 0x%08X\n", cap.capabilities);

    // 检查是否支持视频捕获
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "Device does not support video capture\n");
        close(fd);
        return 1;
    }

    // 检查是否支持内存映射
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "Device does not support streaming I/O\n");
        close(fd);
        return 1;
    }

    // 设置像素格式
    struct v4l2_format fmt;
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = 640;
    fmt.fmt.pix.height = 480;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    // 检查是否支持所选格式
    if (!check_pixel_format(fd, V4L2_PIX_FMT_MJPEG)) {
        printf("MJPEG format not supported, trying YUYV...\n");
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        if (!check_pixel_format(fd, V4L2_PIX_FMT_YUYV)) {
            fprintf(stderr, "No suitable pixel format found\n");
            close(fd);
            return 1;
        }
    }

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
        perror("Setting Pixel Format");
        close(fd);
        return 1;
    }

    printf("Set format: %dx%d, format: 0x%08X\n", 
           fmt.fmt.pix.width, fmt.fmt.pix.height, fmt.fmt.pix.pixelformat);

    // 请求缓冲区
    struct v4l2_requestbuffers req;
    req.count = 1;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    
    if (ioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
        perror("Requesting Buffer");
        close(fd);
        return 1;
    }

    // 查询缓冲区信息
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = 0;
    
    if (ioctl(fd, VIDIOC_QUERYBUF, &buf) == -1) {
        perror("Querying Buffer");
        close(fd);
        return 1;
    }

    // 内存映射
    void* buffer = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
    if (buffer == MAP_FAILED) {
        perror("Mapping Buffer");
        close(fd);
        return 1;
    }

    // 将缓冲区加入队列
    if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
        perror("Queue Buffer");
        munmap(buffer, buf.length);
        close(fd);
        return 1;
    }

    // 启动视频流
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) == -1) {
        perror("Start Capture");
        munmap(buffer, buf.length);
        close(fd);
        return 1;
    }

    printf("Capturing frame...\n");
    
    // 获取帧数据
    if (ioctl(fd, VIDIOC_DQBUF, &buf) == -1) {
        perror("Retrieving Frame");
        ioctl(fd, VIDIOC_STREAMOFF, &type);
        munmap(buffer, buf.length);
        close(fd);
        return 1;
    }

    // 保存到文件
    FILE* file = fopen("frame.jpg", "wb");
    if (file) {
        size_t written = fwrite(buffer, buf.bytesused, 1, file);
        if (written != 1) {
            perror("Writing frame to file");
        } else {
            printf("Frame saved successfully (%d bytes)\n", buf.bytesused);
        }
        fclose(file);
    } else {
        perror("Opening output file");
    }

    // 停止视频流
    ioctl(fd, VIDIOC_STREAMOFF, &type);
    
    // 清理资源
    munmap(buffer, buf.length);
    close(fd);

    printf("Program completed successfully\n");
    return 0;
}
