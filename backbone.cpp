extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
}

/*
此代码的主要目的是对H.264编码的视频文件进行解码，并非直接转码为其他格式的文件。
解码后，得到的是原始的未压缩的视频帧，它们被存储在 `AVFrame` 结构中。

`AVFrame` 结构包含解码后的音频/视频帧数据，以及关于帧的元数据（如分辨率、像素格式、时间戳等）。
此代码片段中，每当成功解码一个视频帧，就会打印一条消息说“Decoded a video frame”。
然而，它并没有将这些帧保存为任何特定格式的文件。

如果你想将解码后的帧保存为另一种编码格式（如MPEG-4、VP9、AV1等），你需要再加入编码的步骤。
FFmpeg库提供了`avcodec_encode_video2`函数，可以用来将`AVFrame`结构编码为特定的编码格式。

如果你想将解码后的帧保存为图像文件（如JPEG或PNG格式），你需要使用FFmpeg库中的格式转换函数（如`sws_scale`）将帧的格式转换为RGB或YUV，并使用相应的图像编码库来保存为图像文件。

请注意，这都需要编写额外的代码，因此具体实现取决于你的需求。*/
int main() {
    // 初始化编码器
    av_register_all();
    AVFormatContext* formatContext = NULL;
    if (avformat_open_input(&formatContext, "test.264", NULL, NULL) < 0) {
        printf("Could not open the video file\n");
        return -1;
    }
    // 查找视频流，AVMEDIA_TYPE_VIDEO表示视频
    int videoStreamIndex = -1;
    for (int i = 0; i < formatContext->nb_streams; ++i) {
        if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;
            break;
        }
    }
    if (videoStreamIndex == -1) {
        printf("Could not find a video stream\n");
        return -1;
    }
    // 获得指向上下文的指针，codecContext 指向了视频流的编解码器上下文
    //可以使用它来访问编解码器的各种信息，例如编解码器的类型、编解码器的参数、以及编解码器用于处理数据的函数等。
    AVCodecContext* codecContext = formatContext->streams[videoStreamIndex]->codec;
    // 查找编解码器
    AVCodec* codec = avcodec_find_decoder(codecContext->codec_id);
    if (!codec) {
        printf("Codec not found\n");
        return -1;
    }
    // 打开编码器
    if (avcodec_open2(codecContext, codec, NULL) < 0) {
        printf("Could not open codec\n");
        return -1;
    }
    AVFrame* frame = av_frame_alloc();
    AVPacket packet;
    while (av_read_frame(formatContext, &packet) >= 0) {
        if (packet.stream_index == videoStreamIndex) {
            int gotPicture;
            avcodec_decode_video2(codecContext, frame, &gotPicture, &packet);
            if (gotPicture) printf("Decoded a video frame\n"); 
        }
        av_packet_unref(&packet);           // 释放AVPacket结构体的空间
    }
    avcodec_close(codecContext);            // 关闭编码器
    avformat_close_input(&formatContext);   // 关闭视频文件
    av_frame_free(&frame);                  // 释放空间
    return 0;
}


