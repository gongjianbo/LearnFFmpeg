#include "MainWindow.h"
#include "ui_MainWindow.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
}
#include <QFileDialog>
#include <QDebug>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    fprintf(stderr, "ffmpeg version:%s\n", av_version_info());

    connect(ui->pushButton, &QPushButton::clicked,
            this, [this]{
        const QString &filepath = QFileDialog::getOpenFileName(this);
        qDebug()<<"select filepath:"<<filepath;
        if(filepath.isEmpty())
            return;
        ui->label->setPixmap(QPixmap::fromImage(getFrameImage(filepath, 100)));
    });
}

MainWindow::~MainWindow()
{
    delete ui;
}

//用于资源释放
struct AVGuard {
    //格式化I/O上下文
    AVFormatContext *formatCtx = NULL;
    //解码器对应的流
    int streamIndex = -1;
    //解码器
    AVCodec *codec = NULL;
    //解码器上下文
    AVCodecContext *codecCtx = NULL;
    //参数信息
    AVCodecParameters *codecParam = NULL;

    //AVPacket存储压缩数据
    //视频通常包含一个压缩帧，音频可能包含多个压缩帧
    AVPacket *packet = NULL;
    //AVFrame存储原始数据，YUV、RGB、PCM等
    //转换前图像
    AVFrame *frameIn = NULL;
    //转换后图像
    AVFrame *frameOut = NULL;
    uint8_t *outBuf = NULL;
    //转换
    SwsContext *swsCtx = NULL;

    ~AVGuard() {
        qDebug()<<"free";
        if(codecCtx){
            //avcodec_close(codecCtx);
            avcodec_free_context(&codecCtx);
            codecCtx = NULL;
        }
        if(formatCtx){
            avformat_close_input(&formatCtx);
            avformat_free_context(formatCtx);
            formatCtx = NULL;
        }
        codec = NULL;
        codecParam = NULL;
        if(frameIn){
            av_frame_unref(frameIn);
            av_frame_free(&frameIn);
            frameIn = NULL;
        }
        if(frameOut){
            av_frame_unref(frameOut);
            av_frame_free(&frameOut);
            frameOut = NULL;
        }
        if(outBuf){
            av_free(outBuf);
            outBuf = NULL;
        }
        if(packet){
            av_packet_unref(packet);
            av_packet_free(&packet);
            packet = NULL;
        }
        if(swsCtx){
            sws_freeContext(swsCtx);
            swsCtx = NULL;
        }
    }
};

QImage MainWindow::getFrameImage(const QString &filepath, int pos)
{
    //参考：https://blog.csdn.net/qq_40946921/article/details/115794514
    //参考：https://blog.csdn.net/kenfan1647/article/details/123687910
    //参考：https://zhuanlan.zhihu.com/p/346010443
    QImage image;

    //借助析构函数来释放
    AVGuard guard;

    //打开输入流并读取头
    //流要使用avformat_close_input关闭，成功时返回=0
    int result = avformat_open_input(&guard.formatCtx, filepath.toUtf8().constData(), NULL, NULL);
    if (result != 0 || guard.formatCtx == NULL)
        return image;

    //读取文件获取流信息，把它存入AVFormatContext中
    //正常时返回>=0
    if (avformat_find_stream_info(guard.formatCtx, NULL) < 0)
        return image;

    //时长，duration/AV_TIME_BASE单位为秒
    qDebug()<<"时长"<<guard.formatCtx->duration/double(AV_TIME_BASE)<<"s";
    qDebug()<<"格式"<<guard.formatCtx->iformat->name;

    //找到视频流，获取对应解码器
    //for (unsigned int i = 0; i < guard.formatCtx->nb_streams; i++) {
    //    if(guard.formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
    guard.streamIndex = av_find_best_stream(guard.formatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (guard.streamIndex < 0)
        return image;
    //参数信息
    guard.codecParam = guard.formatCtx->streams[guard.streamIndex]->codecpar;
    //查找具有匹配编解码器ID的已注册解码器
    //失败返回NULL
    guard.codec = avcodec_find_decoder(guard.codecParam->codec_id);
    if (!guard.codec)
        return image;

    //AVStream.codec属性已弃用，这里使用avcodec_alloc_context3创见一个编解码上下文
    guard.codecCtx = avcodec_alloc_context3(guard.codec);
    //流参数复制到CodecCtx
    avcodec_parameters_to_context(guard.codecCtx, guard.codecParam);
    //打开解码器
    //正常时返回0
    if (!guard.codecCtx || avcodec_open2(guard.codecCtx, guard.codec, NULL) != 0)
        return image;

    qDebug()<<"解码器"<<guard.codec->name;
    qDebug()<<"宽*高"<<guard.codecParam->width<<guard.codecParam->height;

    //跳转到某一帧
    //参数一: 上下文;
    //参数二: 流索引, 如果stream_index是-1，会选择一个默认流，时间戳会从以AV_TIME_BASE为单位向具体流的时间基自动转换。
    //参数三: 将要定位处的时间戳，time_base单位或者如果没有流是指定的就用av_time_base单位。
    //参数四: seek功能flag；
    //AVSEEK_FLAG_BACKWARD  是seek到请求的timestamp之前最近的关键帧
    //AVSEEK_FLAG_BYTE 是基于字节位置的查找
    //AVSEEK_FLAG_ANY 是可以seek到任意帧，注意不一定是关键帧，因此使用时可能会导致花屏
    //AVSEEK_FLAG_FRAME 是基于帧数量快进
    //返回值：成功返回>=0
    if (av_seek_frame(guard.formatCtx, guard.streamIndex, pos * AV_TIME_BASE, AVSEEK_FLAG_BACKWARD) < 0)
        return image;
    //跳转后把解码器的数据清空下
    if (guard.codecCtx) {
        avcodec_flush_buffers(guard.codecCtx);
    }

    //AVPacket存储压缩数据
    guard.packet = av_packet_alloc();
    //AVFrame存储原始数据，YUV、RGB、PCM等
    //存放输入
    guard.frameIn = av_frame_alloc();
    //存放输出
    guard.frameOut = av_frame_alloc();

    //返回存储给定参数的图像数据所需数据量的大小（以字节为单位）
    int size = av_image_get_buffer_size(AV_PIX_FMT_RGB24,
                                        guard.codecParam->width,
                                        guard.codecParam->height, 1);
    guard.outBuf = static_cast<uint8_t *>(av_malloc(static_cast<size_t>(size)));
    //也可以不用frame放输出，直接用buffer+linesize
    //int outLineSize[3];                                                                         //构造AVFrame到QImage所需要的数据
    //av_image_fill_linesizes(outLineSize, AV_PIX_FMT_RGB24, guard.codecParam->width);
    //根据后5个参数的内容填充前两个参数，成功返回源图像的大小，失败返回一个负值
    if (av_image_fill_arrays(guard.frameOut->data,
                             guard.frameOut->linesize,
                             guard.outBuf,
                             AV_PIX_FMT_RGB24,
                             guard.codecParam->width,
                             guard.codecParam->height,
                             1) < 0)
        return image;

    //指定sws_scale上下文
    guard.swsCtx = sws_getContext(guard.codecParam->width,
                                  guard.codecParam->height,
                                  guard.codecCtx->pix_fmt,
                                  guard.codecParam->width,
                                  guard.codecParam->height,
                                  AV_PIX_FMT_RGB24,
                                  SWS_BICUBIC, NULL, NULL, NULL);

    if (!guard.swsCtx)
        return image;

    while (true) {
        //读取码流中的一帧视频，或者若干帧音频
        //注：av_read_frame 每次循环后必须执行av_packet_unref(packet)进行释放
        //frame同理
        if(av_read_frame(guard.formatCtx, guard.packet) != 0)
            return image;

        if (guard.packet->stream_index == guard.streamIndex) {
            //发送编码数据包，将一个packet放入到队列中等待解码
            //注：ffmpeg内部会缓冲几帧，要想取出来就需要传递空的AVPacket进去
            result = avcodec_send_packet(guard.codecCtx, guard.packet);
            //当前状态下不接受输入-用户必须使用avcodec_receive_frame（）读取输出
            if (result == AVERROR(EAGAIN)) {
                avcodec_receive_frame(guard.codecCtx, guard.frameIn);
                av_packet_unref(guard.packet);
                av_frame_unref(guard.frameIn);
                continue;
            } else if (result != 0) {
                return image;
            }
            //接收解码后数据，将解码后的数据拷贝给avframe
            //avcodec_send_packet和avcodec_receive_frame调用关系并不一定是一一对应
            //如一些音频数据调用一次avcodec_send_packet之后，
            //可能需要调用多次avcodec_receive_frame才能获取全部的解码音频数据
            result = avcodec_receive_frame(guard.codecCtx, guard.frameIn);
            //输出在此状态下不可用-用户必须尝试发送新输入
            if (result == AVERROR(EAGAIN)) {
                av_packet_unref(guard.packet);
                av_frame_unref(guard.frameIn);
                continue;
            } else if (result != 0) {
                return image;
            }

            //视频像素格式和分辨率的转换
            //函数功能：1.图像色彩空间转换；2.分辨率缩放；3.前后图像滤波处理。
            //效率相对较低，不如libyuv或shader
            sws_scale(guard.swsCtx,
                      static_cast<const uint8_t* const*>(guard.frameIn->data),
                      guard.frameIn->linesize,
                      0,
                      guard.codecParam->height,
                      guard.frameOut->data,
                      guard.frameOut->linesize);

            //guard.frameOut->data就是指向的guard.outBuf，可替换
            image = QImage(static_cast<uchar*>(guard.frameOut->data[0]),
                    guard.codecParam->width,
                    guard.codecParam->height,
                    QImage::Format_RGB888);
            //构造用的buf的内存，这里拷贝一份
            image = image.copy();

            qDebug()<<"finish"<<image.size();
            return image;
        }
    }

    return image;
}
