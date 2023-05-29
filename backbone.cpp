extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
}

int main() {
    // Initialize libavformat and register all codecs and formats
    av_register_all();

    AVFormatContext* formatContext = NULL;
    if (avformat_open_input(&formatContext, "input.mp4", NULL, NULL) < 0) {
        printf("Could not open the video file\n");
        return -1;
    }

    // Find the first video stream
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

    // Get a pointer to the codec context for the video stream
    AVCodecContext* codecContext = formatContext->streams[videoStreamIndex]->codec;

    // Find the decoder for the video stream
    AVCodec* codec = avcodec_find_decoder(codecContext->codec_id);
    if (!codec) {
        printf("Codec not found\n");
        return -1;
    }

    // Open codec
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
            if (gotPicture) {
                // Here you can process the frame, e.g., convert it to an image, display it, etc.
                printf("Decoded a video frame\n");
            }
        }
        // Free the packet that was allocated by av_read_frame
        av_packet_unref(&packet);
    }

    // Close the codec
    avcodec_close(codecContext);
    // Close the video file
    avformat_close_input(&formatContext);
    // Free the frame
    av_frame_free(&frame);

    return 0;
}


