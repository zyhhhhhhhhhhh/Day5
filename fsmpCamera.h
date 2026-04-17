#ifndef FSMP__H_
#define FSMP__H_

/***************************************************************************
 *
 * 类名：fsmpCamera
 * 功能说明：摄像头 板载：video0  USB： video1
 * 公有函数：
 *      fsmpCamera()  fsmpCamera("/dev/video0", 640, 480)
 *      void setPixDelay(int us)     设置帧间隔us
 *      bool start(void)             启动摄像头线程
 *      bool stop(void)              停止摄像头线程
 *
 * 信号：
 *      void pixReady(QImage pix)    pix：已拍好的图片
 *
 * *************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include<stdio.h>      /*标准输入输出定义*/
#include<stdlib.h>     /*标准函数库定义*/
#include<unistd.h>     /*Unix 标准函数定义*/
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>      /*文件控制定义*/
#include<termios.h>    /*PPSIX 终端控制定义*/
#include<errno.h>      /*错误号定义*/
#include<string.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <QPixmap>
#include <QImage>
#include <QThread>
#include <QDebug>

struct cam_buf {
    void *start;
    size_t length;
};

#define	REQBUFS_COUNT	4

class fsmpCamera:public QThread{
    Q_OBJECT
signals:
    void pixReady(QImage pix);
public:

    explicit  fsmpCamera(const char* cameraDev="/dev/video0", unsigned int w = 320, unsigned int h=240, QObject *parent = nullptr)
        :QThread(parent),width(w), height(h),device(cameraDev)
    {
        m_stop.store(false);
        delayus = 100000;
#if 1
        fd =  _init(device, &width, &height, &size);
        if (fd == -1)
            exit(EXIT_FAILURE);
        ret = _start();
        if (ret == -1)
            exit(EXIT_FAILURE);
        // 采集几张图片丢弃

        for (i = 0; i < 8; i++) {
            ret =  dqbuf((void **)&jpeg_ptr, &size, &index);
            if (ret == -1)
                exit(EXIT_FAILURE);

            ret =  eqbuf(index);
            if (ret == -1)
                exit(EXIT_FAILURE);
        }

        qDebug()<<"init camera success\n";
        signal(SIGPIPE, SIG_IGN);

#endif
    }
    ~fsmpCamera(){
        stop();
        this->quit();         // 请求线程退出
        this->wait();         // 等待线程真正结束
        cstop();
        cexit();
        //close(fd);
    }

public slots:
    void setPixDelay(int us)
    {
        delayus = us;
    }
    void stop()  // 停止线程的接口
    {
        m_stop.store(true);            // 停止循环标志
    }
protected:
    void run(void)
    {
        QImage pix;
        m_stop.store(false);
        while(!m_stop.load())
        {
            //拍照放入内存的jpeg_ptr指针指向的位置而且长度为size
            ret =  dqbuf((void **)&jpeg_ptr, &size, &index); //tack picture
            if (ret == -1)
                return ;

            if (size == 0 || jpeg_ptr == nullptr) {
                qDebug() << "Invalid image data";
                eqbuf(index); //prepare for next tacking
                usleep(delayus);
                continue;
            }

            if (!pix.loadFromData((uchar *)jpeg_ptr, size, "jpg")) {
                qDebug() << "Failed to load image from data";
                qDebug() << "First 4 bytes:" << QString::asprintf("%02X %02X %02X %02X",
                    ((uchar*)jpeg_ptr)[0], ((uchar*)jpeg_ptr)[1],
                    ((uchar*)jpeg_ptr)[2], ((uchar*)jpeg_ptr)[3]);
                eqbuf(index); //prepare for next tacking
                usleep(delayus);
                continue;
            }

            //qDebug() << size << pix.size();
            if (pix.size().width() != 0 && pix.size().height() != 0)
                emit pixReady(pix);

            //告诉系统已经取走
            ret =  eqbuf(index); //prepare for next tacking
            if (ret == -1)
                return ;

           usleep(delayus);

        }
    }
private:
    int _init(const char *devpath, unsigned int *width, unsigned int *height, unsigned int *size)
    {
        __u32 i;
        //int fd = -1;;
        int ret;
        struct v4l2_buffer vbuf;
        struct v4l2_format format;
        struct v4l2_capability capability;
        /*open 打开设备文件*/
        if((fd = open(devpath, O_RDWR| O_NONBLOCK)) == -1){
            perror("open:");
            return -1;
        }
        /*ioctl 查看支持的驱动*/
        ret = ioctl(fd, VIDIOC_QUERYCAP, &capability);
        if (ret == -1) {
            perror("camera->init");
            return -1;
        }
        /*判断设备是否支持视频采集*/
        if(!(capability.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
            fprintf(stderr, "camera->init: device can not support V4L2_CAP_VIDEO_CAPTURE\n");
            close(fd);
            return -1;
        }
        /*判断设备是否支持视频流采集*/
        if(!(capability.capabilities & V4L2_CAP_STREAMING)) {
            fprintf(stderr, "camera->init: device can not support V4L2_CAP_STREAMING\n");
            close(fd);
            return -1;
        }

        /*设置捕获的视频格式 MYJPEG*/
        memset(&format, 0, sizeof(format));
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        format.fmt.pix.pixelformat = V4L2_PIX_FMT_JPEG;
        format.fmt.pix.width = *width;
        format.fmt.pix.height = *height;
        format.fmt.pix.field = V4L2_FIELD_ANY;
        ret = ioctl(fd, VIDIOC_S_FMT, &format);
        if(ret == -1) {
            perror("camera set format");
            return -1;
        }

        ret = ioctl(fd, VIDIOC_G_FMT, &format);
        if (ret == -1) {
            perror("camera init");
            return -1;
        }

        if(format.fmt.pix.pixelformat !=  V4L2_PIX_FMT_JPEG)
        {

            memset(&format, 0, sizeof(format));
            format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;			//永远都是这个类型
            format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;		//设置采集图片的格式
            format.fmt.pix.width = *width;
            format.fmt.pix.height = *height;
            format.fmt.pix.field = V4L2_FIELD_ANY;				//设置图片一行一行的采集
            ret = ioctl(fd, VIDIOC_S_FMT, &format);				//ioctl	是设置生效
            if(ret == -1)
            {
                perror("camera init");
                return -1;
            }

        }

        /*向驱动申请缓存*/
        memset(&reqbufs_, 0, sizeof(struct v4l2_requestbuffers));
        reqbufs_.count	= REQBUFS_COUNT;					//缓存区个数
        reqbufs_.type	= V4L2_BUF_TYPE_VIDEO_CAPTURE;
        reqbufs_.memory	= V4L2_MEMORY_MMAP;					//设置操作申请缓存的方式:映射 MMAP
        ret = ioctl(fd, VIDIOC_REQBUFS, &reqbufs_);
        if (ret == -1) {
            perror("camera init");
            close(fd);
            return -1;
        }
        /*循环映射并入队*/
        for (i = 0; i < reqbufs_.count; i++){
            /*真正获取缓存的地址大小*/
            memset(&vbuf, 0, sizeof(struct v4l2_buffer));
            vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            vbuf.memory = V4L2_MEMORY_MMAP;
            vbuf.index = i;
            ret = ioctl(fd, VIDIOC_QUERYBUF, &vbuf);
            if (ret == -1) {
                perror("camera init");
                close(fd);
                return -1;
            }
            /*映射缓存到用户空间,通过mmap讲内核的缓存地址映射到用户空间,并切和文件描述符fd相关联*/
            bufs[i].length = vbuf.length;
            bufs[i].start = mmap(NULL, vbuf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, vbuf.m.offset);
            if (bufs[i].start == MAP_FAILED) {
                perror("camera init");
                close(fd);
                return -1;
            }
            /*每次映射都会入队,放入缓冲队列*/
            vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            vbuf.memory = V4L2_MEMORY_MMAP;
            ret = ioctl(fd, VIDIOC_QBUF, &vbuf);
            if (ret == -1) {
                perror("camera init");
                close(fd);
                return -1;
            }
        }
        /*返回真正设置成功的宽.高.大小*/
        *width = format.fmt.pix.width;
        *height = format.fmt.pix.height;
        *size = bufs[0].length;

        return fd;
    }

    int _start()
    {
        int ret;

        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        /*ioctl控制摄像头开始采集*/
        ret = ioctl(fd, VIDIOC_STREAMON, &type);
        if (ret == -1) {
            perror("camera->start");
            return -1;
        }
        fprintf(stdout, "camera->start: start capture\n");

        return 0;
    }

    int dqbuf(void **buf, unsigned int *size, unsigned int *index)
    {
        int ret;
        fd_set fds;
        struct timeval timeout;
        struct v4l2_buffer vbuf;

        while (1) {
            FD_ZERO(&fds);
            FD_SET(fd, &fds);
            timeout.tv_sec = 2;
            timeout.tv_usec = 0;
            ret = select(fd + 1, &fds, NULL, NULL, &timeout);	//使用select机制,保证fd有图片的时候才能出对
            if (ret == -1) {
                perror("camera->dbytesusedqbuf");
                if (errno == EINTR)
                    continue;
                else
                    return -1;
            } else if (ret == 0) {
                fprintf(stderr, "camera->dqbuf: dequeue buffer timeout\n");
                return -1;
            } else {
                memset(&vbuf, 0, sizeof(vbuf));
                vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                vbuf.memory = V4L2_MEMORY_MMAP;
                ret = ioctl(fd, VIDIOC_DQBUF, &vbuf);	//出队,也就是从用户空间取出图片
                if (errno == EAGAIN) {
                        // 没有新帧，可以跳过或等待
                        continue;
                }
                else if (ret < 0) {
                    perror("camera->dqbuf");
                    return -1;
                }

                // 检查 bytesused 是否合理
                if (vbuf.bytesused <= 0) {
                    fprintf(stderr, "camera->dqbuf: invalid bytesused: %d\n", vbuf.bytesused);
                    return -1;
                }

                *buf = bufs[vbuf.index].start;
                *size = vbuf.bytesused;
                *index = vbuf.index;

                return 0;
            }
        }
    }

    int eqbuf(unsigned int index)
    {
        int ret;
        struct v4l2_buffer vbuf;
        memset(&vbuf, 0, sizeof(vbuf));
        vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        vbuf.memory = V4L2_MEMORY_MMAP;
        vbuf.index = index;
        ret = ioctl(fd, VIDIOC_QBUF, &vbuf);		//入队
        if (ret == -1) {
            qDebug()<<"lyx---:"<<index;
            perror("camera->eqbuf");
            return -1;
        }

        return 0;
    }

    int cstop()
    {
        int ret;
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        ret = ioctl(fd, VIDIOC_STREAMOFF, &type);
        if (ret == -1) {
            perror("camera->stop");
            return -1;
        }
        fprintf(stdout, "camera->stop: stop capture\n");

        return 0;
    }

    int cexit()
    {
        __u32 i;
        int ret;
        struct v4l2_buffer vbuf;
        vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        vbuf.memory = V4L2_MEMORY_MMAP;
        for (i = 0; i < reqbufs_.count; i++) {
            ret = ioctl(fd, VIDIOC_DQBUF, &vbuf);
            if (ret == -1)
                break;
        }
        for (i = 0; i < reqbufs_.count; i++)
            munmap(bufs[i].start, bufs[i].length);
        fprintf(stdout, "camera->exit: camera exit\n");
        return close(fd);
    }

    unsigned int width;
    unsigned int height;
    const char * device;
    int delayus;
    int i;
    int fd;
    int ret;
    unsigned int size;
    unsigned int index;
    char *jpeg_ptr;
    struct v4l2_requestbuffers reqbufs_;
    struct cam_buf bufs[REQBUFS_COUNT];
    std::atomic<bool> m_stop;         // 控制是否停止采集
};

#endif // FSMP H
