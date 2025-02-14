/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <sstream>
#include <typeinfo>
#include <vector>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavcodec/cabac.h"
#include "libavcodec/coding_hooks.h"
#include "libavformat/avformat.h"
#include "libavformat/avio.h"
#include "libavutil/error.h"
#include "libavutil/file.h"
}

#include "arithmetic_code.h"
#include "cabac_code.h"
#include "recode.pb.h"
#include "framebuffer.h"

// CABAC blocks smaller than this will be skipped.
const int SURROGATE_MARKER_BYTES = 8;
//#define DO_NEIGHBOR_LOGGING
#ifdef DO_NEIGHBOR_LOGGING
#define LOG_NEIGHBORS printf
#else
#define LOG_NEIGHBORS(...)
#endif
template <typename T>
std::unique_ptr<T, std::function<void(T*&)>> av_unique_ptr(T* p, const std::function<void(T*&)>& deleter) {
  if (p == nullptr) {
    throw std::bad_alloc();
  }
  return std::unique_ptr<T, std::function<void(T*&)>>(p, deleter);
}
'''
这段代码定义了一个名为 `av_unique_ptr` 的模板函数。
该函数接收两个参数：一个类型为 T 的原始指针 `p`，和一个接收类型为 T 的指针的 std::function 对象 `deleter`。
函数返回一个 `std::unique_ptr`，拥有类型为 T 的对象的所有权，并使用提供的 `deleter` 来在必要时删除该对象。

`std::unique_ptr` 是一种智能指针，它管理一个动态分配（堆上）的对象。
当 `std::unique_ptr` 被销毁（例如离开其作用域）时，它将删除其所拥有的对象。
`std::unique_ptr` 的一个重要特性是它拥有其对象的唯一所有权，不能被复制，但可以被移动。
这可以防止由于忘记删除对象或多次删除同一个对象而引发的内存泄漏和错误。

这段代码为 `std::unique_ptr` 提供了一个自定义的删除器 `deleter`。
默认情况下，`std::unique_ptr` 使用 `delete` 来删除其所拥有的对象。
但有时，我们需要使用特殊的方式来删除对象，例如调用一个特殊的函数，或在一个特殊的环境（如特殊的内存池）中删除对象。
在这种情况下，我们可以提供一个自定义的删除器。

在这个 `av_unique_ptr` 函数中，如果传入的原始指针 `p` 是 `nullptr`，函数会抛出 `std::bad_alloc` 异常。
这可能是因为在尝试分配内存时发生了错误。
'''
template <typename T>
std::unique_ptr<T, std::function<void(T*&)>> av_unique_ptr(T* p, void (*deleter)(T**)) {
  return av_unique_ptr<T>(p, [deleter](T*& to_delete){ deleter(&to_delete); });
}
'''
这段代码是一个 `av_unique_ptr` 函数的重载版本，
它接收一个函数指针 `deleter` 作为其第二个参数，而不是接收一个 `std::function` 对象。
这个函数指针指向一个接收类型为 `T*` 的指针的函数，并返回 `void`。

在这个函数中，我们创建了一个 lambda 函数，该 lambda 函数接收一个 `T*&` 参数，
然后调用 `deleter` 函数指针，传入一个指向这个参数的指针。
然后，我们将这个 lambda 函数传递给另一个 `av_unique_ptr` 函数版本，以创建一个 `std::unique_ptr`。

这个函数允许我们传递一个 C 风格的函数指针作为删除器，
而不需要直接创建一个 `std::function` 对象。
这可以让我们更方便地使用像是 FFmpeg 这样的 C 库，
这些库通常提供了一些函数，用于正确删除其动态分配的对象。
'''
template <typename T>
std::unique_ptr<T, std::function<void(T*&)>> av_unique_ptr(T* p, void (*deleter)(void*) = av_free) {
  return av_unique_ptr<T>(p, [deleter](T*& to_delete){ deleter(to_delete); });
}
'''
这段代码提供了 `av_unique_ptr` 函数的另一种模板重载，它同样接收一个函数指针 `deleter` 作为第二个参数，
但这次，这个函数接收一个 `void*` 参数，并返回 `void`。这个函数还为 `deleter` 提供了一个默认值 `av_free`，
如果调用者没有提供第二个参数，那么就会使用这个默认值。

在这个函数中，我们创建了一个 lambda 函数，该 lambda 函数接收一个 `T*&` 参数，
然后调用 `deleter` 函数指针，传入这个参数。
然后，我们将这个 lambda 函数传递给另一个 `av_unique_ptr` 函数版本，以创建一个 `std::unique_ptr`。

`av_free` 是 FFmpeg 库中的一个函数，它可以释放由 FFmpeg 的函数（如 `av_malloc`）分配的内存。
这个函数版本允许我们方便地创建一个 `std::unique_ptr`，用于管理由 FFmpeg 的函数分配的内存，而不需要手动提供一个删除器。
'''

template <typename T = std::function<void()>>
struct defer {
  T to_defer;
  explicit defer(const T& to_defer) : to_defer(to_defer) {}
  defer(const defer&) = delete;
  ~defer() { to_defer(); }
};


int av_check(int return_value, int expected_error = 0, const std::string& message = "") {
  if (return_value >= 0 || return_value == expected_error) {
    return return_value;
  } else {
    char err[AV_ERROR_MAX_STRING_SIZE];
    av_make_error_string(err, AV_ERROR_MAX_STRING_SIZE, return_value);
    throw std::runtime_error(message + ": " + err);
  }
}
bool av_check(int return_value, const std::string& message = "") {
  return av_check(return_value, 0, message);
}


// Sets up a libavcodec decoder with I/O and decoding hooks.
template <typename Driver>

class av_decoder {
 public:
  av_decoder(Driver *driver, const std::string& input_filename) : driver(driver) {
    const size_t avio_ctx_buffer_size = 1024*1024;
    uint8_t *avio_ctx_buffer = static_cast<uint8_t*>( av_malloc(avio_ctx_buffer_size) );

    format_ctx = avformat_alloc_context();
    if (avio_ctx_buffer == nullptr || format_ctx == nullptr) throw std::bad_alloc();
    format_ctx->pb = avio_alloc_context(
        avio_ctx_buffer,                      // input buffer
        avio_ctx_buffer_size,                 // input buffer size
        false,                                // stream is not writable
        this,                                 // first argument for read_packet()
        read_packet,                          // read callback
        nullptr,                              // write_packet()
        nullptr);                             // seek()

    if (avformat_open_input(&format_ctx, input_filename.c_str(), nullptr, nullptr) < 0) {
      throw std::invalid_argument("Failed to initialize decoding context: " + input_filename);
    }
  }
  ~av_decoder() {
    for (size_t i = 0; i < format_ctx->nb_streams; i++) {
      avcodec_close(format_ctx->streams[i]->codec);
    }
    av_freep(&format_ctx->pb->buffer);  // May no longer be the same buffer we initially malloced.
    av_freep(&format_ctx->pb);
    avformat_close_input(&format_ctx);
  }

  // Read enough frames to display stream diagnostics. Only used by compressor,
  // because hooks are not yet set. Reads from already in-memory blocks.
  void dump_stream_info() {
    av_check( avformat_find_stream_info(format_ctx, nullptr),
        "Invalid input stream information" );
    av_dump_format(format_ctx, 0, format_ctx->filename, 0);
  }

  // Decode all video frames in the file in single-threaded mode, calling the driver's hooks.
  void decode_video() {
    auto frame = av_unique_ptr(av_frame_alloc(), av_frame_free);
    AVPacket packet;
    // TODO(ctl) add better diagnostics to error results.
    while (!av_check( av_read_frame(format_ctx, &packet), AVERROR_EOF, "Failed to read frame" )) {
      AVCodecContext *codec = format_ctx->streams[packet.stream_index]->codec;
      if (codec->codec_type == AVMEDIA_TYPE_VIDEO) {
        if (!avcodec_is_open(codec)) {
          codec->thread_count = 1;
          codec->hooks = &hooks;
          av_check( avcodec_open2(codec, avcodec_find_decoder(codec->codec_id), nullptr),
            "Failed to open decoder for stream " + std::to_string(packet.stream_index) );
        }

        int got_frame = 0;

        //利用ffmpeg的 lib库
        //libavcodec->utils.c->avcodec_decode_video
        av_check( avcodec_decode_video2(codec, frame.get(), &got_frame, &packet),
            "Failed to decode video frame" );
      }
      av_packet_unref(&packet);
    }
  }

 private:
  // Hook stubs - wrap driver into opaque pointers.
  static int read_packet(void *opaque, uint8_t *buffer_out, int size) {
    av_decoder *self = static_cast<av_decoder*>(opaque);
    return self->driver->read_packet(buffer_out, size);
  }
  struct cabac {    //cabac in av_decoder
    static void* init_decoder(void *opaque, CABACContext *ctx, const uint8_t *buf, int size) {
      av_decoder *self = static_cast<av_decoder*>(opaque);
      auto *cabac_decoder = new typename Driver::cabac_decoder(self->driver, ctx, buf, size);
      self->cabac_contexts[ctx].reset(cabac_decoder);
      return cabac_decoder;
    }
    static int get(void *opaque, uint8_t *state) {
      auto *self = static_cast<typename Driver::cabac_decoder*>(opaque);
      return self->get(state);
    }
    static int get_bypass(void *opaque) {
      auto *self = static_cast<typename Driver::cabac_decoder*>(opaque);
      return self->get_bypass();
    }
    static int get_terminate(void *opaque) {
      auto *self = static_cast<typename Driver::cabac_decoder*>(opaque);
      return self->get_terminate();
    }
    static const uint8_t* skip_bytes(void *opaque, int n) {
      throw std::runtime_error("Not implemented: CABAC decoder doesn't use skip_bytes.");
    }
  };
  struct model_hooks {
    static void frame_spec(void *opaque, int frame_num, int mb_width, int mb_height) {
      auto *self = static_cast<av_decoder*>(opaque)->driver->get_model();
      self->update_frame_spec(frame_num, mb_width, mb_height);
    }
    static void mb_xy(void *opaque, int x, int y) {
      auto *self = static_cast<av_decoder*>(opaque)->driver->get_model();
      self->mb_coord.mb_x = x;
      self->mb_coord.mb_y = y;
    }
    static void begin_sub_mb(void *opaque, int cat, int scan8index, int max_coeff, int is_dc, int chroma422) {
      auto *self = static_cast<av_decoder*>(opaque)->driver->get_model();
      self->sub_mb_cat = cat;
      self->mb_coord.scan8_index = scan8index;
      self->sub_mb_size = max_coeff;
      self->sub_mb_is_dc = is_dc;
      self->sub_mb_chroma422 = chroma422;
    }
    static void end_sub_mb(void *opaque, int cat, int scan8index, int max_coeff, int is_dc, int chroma422) {
      auto *self = static_cast<av_decoder*>(opaque)->driver->get_model();
      assert(self->sub_mb_cat == cat);
      assert(self->mb_coord.scan8_index == scan8index);
      assert(self->sub_mb_size == max_coeff);
      assert(self->sub_mb_is_dc == is_dc);
      assert(self->sub_mb_chroma422 == chroma422);
      self->sub_mb_cat = -1;
      self->mb_coord.scan8_index = -1;
      self->sub_mb_size = -1;
      self->sub_mb_is_dc = 0;
      self->sub_mb_chroma422 = 0;
    }
    static void begin_coding_type(void *opaque, CodingType ct,
                                    int zigzag_index, int param0, int param1) {
      auto &cabac_contexts = static_cast<av_decoder*>(opaque)->cabac_contexts;
      assert(cabac_contexts.size() == 1);
      typename Driver::cabac_decoder*self = cabac_contexts.begin()->second.get();
      self->begin_coding_type(ct, zigzag_index, param0, param1);
    }
    static void end_coding_type(void *opaque, CodingType ct) {
      auto &cabac_contexts = static_cast<av_decoder*>(opaque)->cabac_contexts;
      assert(cabac_contexts.size() == 1);
      typename Driver::cabac_decoder*self = cabac_contexts.begin()->second.get();
      self->end_coding_type(ct);
    }
  };
  Driver *driver;
  AVFormatContext *format_ctx;
  AVCodecHooks hooks = { this, {
      cabac::init_decoder,
      cabac::get,
      cabac::get_bypass,
      cabac::get_terminate,
      cabac::skip_bytes,
    },
    {
      model_hooks::frame_spec,
      model_hooks::mb_xy,
      model_hooks::begin_sub_mb,
      model_hooks::end_sub_mb,
      model_hooks::begin_coding_type,
      model_hooks::end_coding_type,

    },
  };
  std::map<CABACContext*, std::unique_ptr<typename Driver::cabac_decoder>> cabac_contexts;
};


struct r_scan8 {
    uint16_t scan8_index;
    bool neighbor_left;
    bool neighbor_up;
    bool is_invalid() const {
        return scan8_index == 0 && neighbor_left && neighbor_up;
    }
    static constexpr r_scan8 inv() {
        return {0, true, true};
    }
};
/* Scan8 organization:
 *    0 1 2 3 4 5 6 7
 * 0  DY    y y y y y
 * 1        y Y Y Y Y
 * 2        y Y Y Y Y
 * 3        y Y Y Y Y
 * 4  du    y Y Y Y Y
 * 5  DU    u u u u u
 * 6        u U U U U
 * 7        u U U U U
 * 8        u U U U U
 * 9  dv    u U U U U
 * 10 DV    v v v v v
 * 11       v V V V V
 * 12       v V V V V
 * 13       v V V V V
 * 14       v V V V V
 * DY/DU/DV are for luma/chroma DC.
 */
constexpr uint8_t scan_8[16 * 3 + 3] = {
    4 +  1 * 8, 5 +  1 * 8, 4 +  2 * 8, 5 +  2 * 8,
    6 +  1 * 8, 7 +  1 * 8, 6 +  2 * 8, 7 +  2 * 8,
    4 +  3 * 8, 5 +  3 * 8, 4 +  4 * 8, 5 +  4 * 8,
    6 +  3 * 8, 7 +  3 * 8, 6 +  4 * 8, 7 +  4 * 8,
    4 +  6 * 8, 5 +  6 * 8, 4 +  7 * 8, 5 +  7 * 8,
    6 +  6 * 8, 7 +  6 * 8, 6 +  7 * 8, 7 +  7 * 8,
    4 +  8 * 8, 5 +  8 * 8, 4 +  9 * 8, 5 +  9 * 8,
    6 +  8 * 8, 7 +  8 * 8, 6 +  9 * 8, 7 +  9 * 8,
    4 + 11 * 8, 5 + 11 * 8, 4 + 12 * 8, 5 + 12 * 8,
    6 + 11 * 8, 7 + 11 * 8, 6 + 12 * 8, 7 + 12 * 8,
    4 + 13 * 8, 5 + 13 * 8, 4 + 14 * 8, 5 + 14 * 8,
    6 + 13 * 8, 7 + 13 * 8, 6 + 14 * 8, 7 + 14 * 8,
    0 +  0 * 8, 0 +  5 * 8, 0 + 10 * 8
};

constexpr r_scan8 reverse_scan_8[15][8] = {
    //Y
    {{16 * 3, false, false}, r_scan8::inv(), r_scan8::inv(), {15, true, true},
     {10, false, true}, {11, false, true}, {14, false, true}, {15, false, true}},
    {r_scan8::inv(), r_scan8::inv(), r_scan8::inv(), {5, true, false},
     {0, false, false}, {1, false, false}, {4, false, false}, {5, false, false}},
    {r_scan8::inv(), r_scan8::inv(), r_scan8::inv(), {7, true, false},
     {2, false, false}, {3, false, false}, {6, false, false}, {7, false, false}},
    {r_scan8::inv(), r_scan8::inv(), r_scan8::inv(), {13, true, false},
     {8, false, false}, {9, false, false}, {12, false, false}, {13, false, false}},
    {{16 * 3 + 1,false, true}, r_scan8::inv(), r_scan8::inv(), {15, true, false},
     {10, false, false}, {11, false, false}, {14, false, false}, {15, false, false}},
    // U
    {{16 * 3 + 1,false, false}, r_scan8::inv(), r_scan8::inv(), {16 + 15, true, true},
     {16 + 10, false, true}, {16 + 11, false, true}, {16 + 14, false, true}, {16 + 15, false, true}},
    {r_scan8::inv(), r_scan8::inv(), r_scan8::inv(), {16 + 5, true, false},
     {16 + 0, false, false}, {16 + 1, false, false}, {16 + 4, false, false}, {16 + 5, false, false}},
    {r_scan8::inv(), r_scan8::inv(), r_scan8::inv(), {16 + 7, true, false},
     {16 + 2, false, false}, {16 + 3, false, false}, {16 + 6, false, false}, {16 + 7, false, false}},
    {r_scan8::inv(), r_scan8::inv(), r_scan8::inv(), {16 + 13, true, false},
     {16 + 8, false, false}, {16 + 9, false, false}, {16 + 12, false, false}, {16 + 13, false, false}},
    {{16 * 3 + 2,false, true}, r_scan8::inv(), r_scan8::inv(), {16 + 15, true, false},
     {16 + 10, false, false}, {16 + 11, false, false}, {16 + 14, false, false}, {16 + 15, false, false}},
    // V
    {{16 * 3 + 2,false, false}, r_scan8::inv(), r_scan8::inv(), {32 + 15, true, true},
     {32 + 10, false, true}, {32 + 11, false, true}, {32 + 14, false, true}, {32 + 15, false, true}},
    {r_scan8::inv(), r_scan8::inv(), r_scan8::inv(), {32 + 5, true, false},
     {32 + 0, false, false}, {32 + 1, false, false}, {32 + 4, false, false}, {32 + 5, false, false}},
    {r_scan8::inv(), r_scan8::inv(), r_scan8::inv(), {32 + 7, true, false},
     {32 + 2, false, false}, {32 + 3, false, false}, {32 + 6, false, false}, {32 + 7, false, false}},
    {r_scan8::inv(), r_scan8::inv(), r_scan8::inv(), {32 + 13, true, false},
     {32 + 8, false, false}, {32 + 9, false, false}, {32 + 12, false, false}, {32 + 13, false, false}},
    {{32 + 16 * 3 + 1,false, true}, r_scan8::inv(), r_scan8::inv(), {32 + 15, true, false},
     {32 + 10, false, false}, {32 + 11, false, false}, {32 + 14, false, false}, {32 + 15, false, false}}};

// Encoder / decoder for recoded CABAC blocks.
typedef uint64_t range_t;
typedef arithmetic_code<range_t, uint8_t> recoded_code;

typedef std::tuple<const void*, int, int> model_key;
/*
not sure these tables are the ones we want to use
constexpr uint8_t unzigzag16[16] = {
    0 + 0 * 4, 0 + 1 * 4, 1 + 0 * 4, 0 + 2 * 4,
    0 + 3 * 4, 1 + 1 * 4, 1 + 2 * 4, 1 + 3 * 4,
    2 + 0 * 4, 2 + 1 * 4, 2 + 2 * 4, 2 + 3 * 4,
    3 + 0 * 4, 3 + 1 * 4, 3 + 2 * 4, 3 + 3 * 4,
};
constexpr uint8_t zigzag16[16] = {
    0, 2, 8, 12,
    1, 5, 9, 13,
    3, 6, 10, 14,
    4, 7, 11, 15
};

constexpr uint8_t zigzag_field64[64] = {
    0 + 0 * 8, 0 + 1 * 8, 0 + 2 * 8, 1 + 0 * 8,
    1 + 1 * 8, 0 + 3 * 8, 0 + 4 * 8, 1 + 2 * 8,
    2 + 0 * 8, 1 + 3 * 8, 0 + 5 * 8, 0 + 6 * 8,
    0 + 7 * 8, 1 + 4 * 8, 2 + 1 * 8, 3 + 0 * 8,
    2 + 2 * 8, 1 + 5 * 8, 1 + 6 * 8, 1 + 7 * 8,
    2 + 3 * 8, 3 + 1 * 8, 4 + 0 * 8, 3 + 2 * 8,
    2 + 4 * 8, 2 + 5 * 8, 2 + 6 * 8, 2 + 7 * 8,
    3 + 3 * 8, 4 + 1 * 8, 5 + 0 * 8, 4 + 2 * 8,
    3 + 4 * 8, 3 + 5 * 8, 3 + 6 * 8, 3 + 7 * 8,
    4 + 3 * 8, 5 + 1 * 8, 6 + 0 * 8, 5 + 2 * 8,
    4 + 4 * 8, 4 + 5 * 8, 4 + 6 * 8, 4 + 7 * 8,
    5 + 3 * 8, 6 + 1 * 8, 6 + 2 * 8, 5 + 4 * 8,
    5 + 5 * 8, 5 + 6 * 8, 5 + 7 * 8, 6 + 3 * 8,
    7 + 0 * 8, 7 + 1 * 8, 6 + 4 * 8, 6 + 5 * 8,
    6 + 6 * 8, 6 + 7 * 8, 7 + 2 * 8, 7 + 3 * 8,
    7 + 4 * 8, 7 + 5 * 8, 7 + 6 * 8, 7 + 7 * 8,
};

*/
constexpr uint8_t zigzag4[4] = {
    0, 1, 2, 3
};
constexpr uint8_t unzigzag4[4] = {
    0, 1, 2, 3
};

constexpr uint8_t unzigzag16[16] = {
    0, 1, 4, 8,
    5, 2, 3, 6,
    9, 12, 13, 10,
    7, 11, 14, 15
};
constexpr uint8_t zigzag16[16] = {
    0, 1, 5, 6,
    2, 4, 7, 12,
    3, 8, 11, 13,
    9, 10, 14, 15
};
constexpr uint8_t unzigzag64[64] = {
    0,   1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

constexpr uint8_t zigzag64[64] = {
    0, 1, 5, 6, 14, 15, 27, 28,
    2, 4, 7, 13, 16, 26, 29, 42,
    3, 8, 12, 17, 25, 30, 41, 43,
    9, 11, 18, 24, 31, 40, 44, 53,
    10, 19, 23, 32, 39, 45, 52, 54,
    20, 22, 33, 38, 46, 51, 55, 60,
    21, 34, 37, 47, 50, 56, 59, 61,
    35, 36, 48, 49, 57, 58, 62, 63
};


int test_reverse_scan8() {
    for (size_t i = 0; i < sizeof(scan_8)/ sizeof(scan_8[0]); ++i) {
        auto a = reverse_scan_8[scan_8[i] >> 3][scan_8[i] & 7];
        assert(a.neighbor_left == false && a.neighbor_up == false);
        assert(a.scan8_index == i);
        if (a.scan8_index != i) {
            return 1;
        }
    }
    for (int i = 0;i < 16; ++i) {
        assert(zigzag16[unzigzag16[i]] == i);
        assert(unzigzag16[zigzag16[i]] == i);
    }
    return 0;
}
int make_sure_reverse_scan8 = test_reverse_scan8();
struct CoefficientCoord {
    int mb_x;
    int mb_y;
    int scan8_index;
    int zigzag_index;
};

bool get_neighbor_sub_mb(bool above, int sub_mb_size,
                  CoefficientCoord input,
                  CoefficientCoord *output) {
    int mb_x = input.mb_x;
    int mb_y = input.mb_y;
    int scan8_index = input.scan8_index;
    output->scan8_index = scan8_index;
    output->mb_x = mb_x;
    output->mb_y = mb_y;
    output->zigzag_index = input.zigzag_index;
    if (scan8_index >= 16 * 3) {
        if (above) {
            if (mb_y > 0) {
                output->mb_y -= 1;
                return true;
            }
            return false;
        } else {
            if (mb_x > 0) {
                output->mb_x -= 1;
                return true;
            }
            return false;
        }
    }
    int scan8 = scan_8[scan8_index];
    int left_shift = (above ? 0 : -1);
    int above_shift = (above ? -1 : 0);
    auto neighbor = reverse_scan_8[(scan8 >> 3) + above_shift][(scan8 & 7) + left_shift];
    if (neighbor.neighbor_left) {
        if (mb_x == 0){
            return false;
        } else {
            --mb_x;
        }
    }
    if (neighbor.neighbor_up) {
        if (mb_y == 0) {
            return false;
        } else {
            --mb_y;
        }
    }
    output->scan8_index = neighbor.scan8_index;
    if (sub_mb_size >= 32) {
        output->scan8_index /= 4;
        output->scan8_index *= 4; // round down to the nearest multiple of 4
    }
    output->zigzag_index = input.zigzag_index;
    output->mb_x = mb_x;
    output->mb_y = mb_y;
    return true;
}
int log2(int y) {
    int x = -1;
    while (y) {
        y/=2;
        x++;
    }
    return x;
}
bool get_neighbor(bool above, int sub_mb_size,
                  CoefficientCoord input,
                  CoefficientCoord *output) {
    int mb_x = input.mb_x;
    int mb_y = input.mb_y;
    int scan8_index = input.scan8_index;
    unsigned int zigzag_index = input.zigzag_index;
    int dimension = 2;
    if (sub_mb_size > 15) {
        dimension = 4;
    }
    if (sub_mb_size > 32) {
        dimension = 8;
    }
    if (scan8_index >= 16 * 3) {
        // we are DC...
        int linear_index = unzigzag4[zigzag_index & 0x3];
        if (sub_mb_size == 16) {
            linear_index = unzigzag16[zigzag_index & 0xf];
        } else {
            assert(sub_mb_size <= 4);
        }
        if ((above && linear_index >= dimension) // if is inner
            || ((linear_index & (dimension - 1)) && !above)) {
            if (above) {
                linear_index -= dimension;
            } else {
                -- linear_index;
            }
            if (sub_mb_size == 16) {
                output->zigzag_index = zigzag16[linear_index];
            } else {
                output->zigzag_index = zigzag4[linear_index];
            }
            output->mb_x = mb_x;
            output->mb_y = mb_y;
            output->scan8_index = scan8_index;
            return true;
        }
        if (above) {
            if (mb_y == 0) {
                return false;
            }
            linear_index += dimension * (dimension - 1);//go to bottom
            --mb_y;
        } else {
            if (mb_x == 0) {
                return false;
            }
            linear_index += dimension - 1;//go to end of row
            --mb_x;
        }
        if (sub_mb_size == 16) {
            output->zigzag_index = zigzag16[linear_index];
        } else {
            output->zigzag_index = linear_index;
        }
        output->mb_x = mb_x;
        output->mb_y = mb_y;
        output->scan8_index = scan8_index;
        return true;
    }
    int scan8 = scan_8[scan8_index];
    int left_shift = (above ? 0 : -1);
    int above_shift = (above ? -1 : 0);
    auto neighbor = reverse_scan_8[(scan8 >> 3) + above_shift][(scan8 & 7) + left_shift];
    if (neighbor.neighbor_left) {
        if (mb_x == 0){
            return false;
        } else {
            --mb_x;
        }
    }
    if (neighbor.neighbor_up) {
        if (mb_y == 0) {
            return false;
        } else {
            --mb_y;
        }
    }
    output->scan8_index = neighbor.scan8_index;
    if (sub_mb_size >= 32) {
        output->scan8_index /= 4;
        output->scan8_index *= 4; // round down to the nearest multiple of 4
    }
    output->zigzag_index = zigzag_index;
    output->mb_x = mb_x;
    output->mb_y = mb_y;
    return true;
}

bool get_neighbor_coefficient(bool above,
                              int sub_mb_size,
                              CoefficientCoord input,
                              CoefficientCoord *output) {
    if (input.scan8_index >= 16 * 3) {
        return get_neighbor(above, sub_mb_size, input, output);
    }
    int zigzag_addition = 0;

    if ((sub_mb_size & (sub_mb_size - 1)) != 0) {
        zigzag_addition = 1;// the DC is not included
    }
    const uint8_t *zigzag_to_raster = unzigzag16;
    const uint8_t *raster_to_zigzag = zigzag16;
    int dim = 4;
    if (sub_mb_size <= 4) {
        dim = 2;
        zigzag_to_raster = zigzag4;
        raster_to_zigzag = unzigzag4;
    }
    if (sub_mb_size > 16) {
        dim = 16;
        zigzag_to_raster = zigzag64;
        raster_to_zigzag = unzigzag64;
    }
    int raster_coord = zigzag_to_raster[input.zigzag_index + zigzag_addition];
    //fprintf(stderr, "%d %d   %d -> %d\n", sub_mb_size, zigzag_addition, input.zigzag_index, raster_coord);
    if (above) {
        if (raster_coord >= dim) {
            raster_coord -= dim;
        } else {
            return false;
        }
    } else {
        if (raster_coord & (dim - 1)) {
            raster_coord -= 1;
        } else {
            return false;
        }
    }
    *output = input;
    output->zigzag_index = raster_to_zigzag[raster_coord] - zigzag_addition;
    return true;
}
#define STRINGIFY_COMMA(s) #s ,
const char * billing_names [] = {EACH_PIP_CODING_TYPE(STRINGIFY_COMMA)};
#undef STRINGIFY_COMMA
class h264_model {
  public:
  CodingType coding_type = PIP_UNKNOWN;
  size_t bill[sizeof(billing_names)/sizeof(billing_names[0])];
  size_t cabac_bill[sizeof(billing_names)/sizeof(billing_names[0])];
  FrameBuffer frames[2];
  int cur_frame = 0;
  uint8_t STATE_FOR_NUM_NONZERO_BIT[6];
  bool do_print;
 public:
  h264_model() { reset(); do_print = false; memset(bill, 0, sizeof(bill)); memset(cabac_bill, 0, sizeof(cabac_bill));}
  void enable_debug() {
    do_print = true;
  }
  void disable_debug() {
    do_print = false;
  }
  ~h264_model() {
      bool first = true;
      for (size_t i = 0; i < sizeof(billing_names)/sizeof(billing_names[i]); ++i) {
          if (bill[i]) {
              if (first) {
                  fprintf(stderr, "Avrecode Bill\n=============\n");
              }
              first = false;
              fprintf(stderr, "%s : %ld\n", billing_names[i], bill[i]);
          }
      }
      for (size_t i = 0; i < sizeof(billing_names)/sizeof(billing_names[i]); ++i) {
          if (cabac_bill[i]) {
              if (first) {
                  fprintf(stderr, "CABAC Bill\n=============\n");
              }
              first = false;
              fprintf(stderr, "%s : %ld\n", billing_names[i], cabac_bill[i]);
          }
      }
  }
  void billable_bytes(size_t num_bytes_emitted) {
      bill[coding_type] += num_bytes_emitted;
  }
  void billable_cabac_bytes(size_t num_bytes_emitted) {
      cabac_bill[coding_type] += num_bytes_emitted;
  }
  void reset() {
      // reset should do nothing as we wish to remember what we've learned
    memset(STATE_FOR_NUM_NONZERO_BIT, 0, sizeof(STATE_FOR_NUM_NONZERO_BIT));
  }
  bool fetch(bool previous, bool match_type, CoefficientCoord coord, int16_t*output) const{
      if (match_type && (previous || coord.mb_x != mb_coord.mb_x || coord.mb_y != mb_coord.mb_y)) {
          BlockMeta meta = frames[previous ? !cur_frame : cur_frame].meta_at(coord.mb_x, coord.mb_y);
          if (!meta.coded) { // when we populate mb_type in the metadata, then we can use it here
              return false;
          }
      }
      *output = frames[previous ? !cur_frame : cur_frame].at(coord.mb_x, coord.mb_y).residual[coord.scan8_index * 16 + coord.zigzag_index];
      return true;
  }
  model_key get_model_key(const void *context)const {
      switch(coding_type) {
        case PIP_SIGNIFICANCE_NZ:
          return model_key(context, 0, 0);
        case PIP_UNKNOWN:
        case PIP_UNREACHABLE:
        case PIP_RESIDUALS:
          return model_key(context, 0, 0);
        case PIP_SIGNIFICANCE_MAP:
          {
              static const uint8_t sig_coeff_flag_offset_8x8[2][63] = {
                  { 0, 1, 2, 3, 4, 5, 5, 4, 4, 3, 3, 4, 4, 4, 5, 5,
                    4, 4, 4, 4, 3, 3, 6, 7, 7, 7, 8, 9,10, 9, 8, 7,
                    7, 6,11,12,13,11, 6, 7, 8, 9,14,10, 9, 8, 6,11,
                    12,13,11, 6, 9,14,10, 9,11,12,13,11,14,10,12 },
                  { 0, 1, 1, 2, 2, 3, 3, 4, 5, 6, 7, 7, 7, 8, 4, 5,
                    6, 9,10,10, 8,11,12,11, 9, 9,10,10, 8,11,12,11,
                    9, 9,10,10, 8,11,12,11, 9, 9,10,10, 8,13,13, 9,
                    9,10,10, 8,13,13, 9, 9,10,10,14,14,14,14,14 }
              };
              int cat_lookup[14] = { 105+0, 105+15, 105+29, 105+44, 105+47, 402, 484+0, 484+15, 484+29, 660, 528+0, 528+15, 528+29, 718 };
              static const uint8_t sig_coeff_offset_dc[7] = { 0, 0, 1, 1, 2, 2, 2 };
              int zigzag_offset = mb_coord.zigzag_index;
              if (sub_mb_is_dc && sub_mb_chroma422) {
                  assert(mb_coord.zigzag_index < 7);
                  zigzag_offset = sig_coeff_offset_dc[mb_coord.zigzag_index];
              } else {
                  if (sub_mb_size > 32) {                      assert(mb_coord.zigzag_index < 63);
                      zigzag_offset = sig_coeff_flag_offset_8x8[0][mb_coord.zigzag_index];
                  }
              }
              assert(sub_mb_cat < (int)(sizeof(cat_lookup)/sizeof(cat_lookup[0])));
              int neighbor_above = 2;
              int neighbor_left = 2;
              int coeff_neighbor_above = 2;
              int coeff_neighbor_left = 2;
              if (do_print) {
                  LOG_NEIGHBORS("[");
              }
              {
                  CoefficientCoord neighbor_left_coord = {0, 0, 0, 0};
                  if (get_neighbor(false, sub_mb_size, mb_coord, &neighbor_left_coord)) {
                      int16_t tmp = 0;
                      if (fetch(false, true, neighbor_left_coord, &tmp)){
                          neighbor_left = !!tmp;
                          if (do_print) {
                              LOG_NEIGHBORS("%d,", tmp);
                          }
                      } else {
                          neighbor_left = 3;
                          if (do_print) {
                              LOG_NEIGHBORS("_,");
                          }
                      }
                  } else {
                      if (do_print) {
                          LOG_NEIGHBORS("x,");
                      }
                  }
              }
              {
                  CoefficientCoord neighbor_above_coord = {0, 0, 0, 0};
                  if (get_neighbor(true, sub_mb_size, mb_coord, &neighbor_above_coord)) {
                      int16_t tmp = 0;
                      if (fetch(false, true, neighbor_above_coord, &tmp)){
                          neighbor_above = !!tmp;
                          if (do_print) {
                              LOG_NEIGHBORS("%d,", tmp);
                          }
                      } else {
                          neighbor_above = 3;
                          if (do_print) {
                              LOG_NEIGHBORS("_,");
                          }
                      }
                  } else {
                      if (do_print) {
                          LOG_NEIGHBORS("x,");
                      }
                  }

              }
              {
                  CoefficientCoord neighbor_left_coord = {0, 0, 0, 0};
                  if (get_neighbor_coefficient(false, sub_mb_size, mb_coord, &neighbor_left_coord)) {
                      int16_t tmp = 0;
                      if (fetch(false, true, neighbor_left_coord, &tmp)){
                          coeff_neighbor_left = !!tmp;
                      } else {
                          coeff_neighbor_left = 3;
                      }
                  } else {
                  }
              }
              {
                  CoefficientCoord neighbor_above_coord = {0, 0, 0, 0};
                  if (get_neighbor_coefficient(true, sub_mb_size, mb_coord, &neighbor_above_coord)) {
                      int16_t tmp = 0;
                      if (fetch(false, true, neighbor_above_coord, &tmp)){
                          coeff_neighbor_above = !!tmp;
                      } else {
                          coeff_neighbor_above = 3;
                      }
                  } else {
                  }
              }
              
              // FIXM: why doesn't this prior help at all
              {
                  int16_t output = 0;
                  if (fetch(true, true, mb_coord, &output)) {
                      if (do_print) LOG_NEIGHBORS("%d] ", output);
                  } else {
                      if (do_print) LOG_NEIGHBORS("x] ");
                  }
              }
              //const BlockMeta &meta = frames[!cur_frame].meta_at(mb_x, mb_y);
              int num_nonzeros = frames[cur_frame].meta_at(mb_coord.mb_x, mb_coord.mb_y).num_nonzeros[mb_coord.scan8_index];
              (void)neighbor_above;
              (void)neighbor_left;
              (void)coeff_neighbor_above;
              (void)coeff_neighbor_left;//haven't found a good way to utilize these priors to make the results better
              return model_key(&significance_context,
                               64 * num_nonzeros + nonzeros_observed,
                               sub_mb_is_dc + zigzag_offset * 2 + 16 * 2 * cat_lookup[sub_mb_cat]);
          }
        case PIP_SIGNIFICANCE_EOB:
          {
            // FIXME: why doesn't this prior help at all
            static int fake_context = 0;
            int num_nonzeros = frames[cur_frame].meta_at(mb_coord.mb_x, mb_coord.mb_y).num_nonzeros[mb_coord.scan8_index];
            
            return model_key(&fake_context, num_nonzeros == nonzeros_observed, 0);
          }
        default:
          break;
      }
      assert(false && "Unreachable");
      abort();
  }
  range_t probability_for_model_key(range_t range, model_key key) {
    auto* e = &estimators[key];
    int total = e->pos + e->neg;
    return (range/total) * e->pos;
  }
  range_t probability_for_state(range_t range, const void *context) {
    return probability_for_model_key(range, get_model_key(context));
  }
  void update_frame_spec(int frame_num, int mb_width, int mb_height) {
    if (frames[cur_frame].width() != (uint32_t)mb_width
        || frames[cur_frame].height() != (uint32_t)mb_height
        || !frames[cur_frame].is_same_frame(frame_num)) {
      cur_frame = !cur_frame;
      if (frames[cur_frame].width() != (uint32_t)mb_width
          || frames[cur_frame].height() != (uint32_t)mb_height) {
        frames[cur_frame].init(mb_width, mb_height, mb_width * mb_height);
        if (frames[!cur_frame].width() != (uint32_t)mb_width
            || frames[!cur_frame].height() != (uint32_t)mb_height) {
            frames[!cur_frame].init(mb_width, mb_height, mb_width * mb_height);
        }
        //fprintf(stderr, "Init(%d=%d) %d x %d\n", frame_num, cur_frame, mb_width, mb_height);
      } else {
        frames[cur_frame].bzero();
        //fprintf(stderr, "Clear (%d=%d)\n", frame_num, cur_frame);
      }
      frames[cur_frame].set_frame_num(frame_num);
    }
  }
  template <class Functor>
  void finished_queueing(CodingType ct, const Functor &put_or_get) {

    if (ct == PIP_SIGNIFICANCE_MAP) {
      bool block_of_interest = (sub_mb_cat == 1 || sub_mb_cat == 2);
      CodingType last = coding_type;
      coding_type = PIP_SIGNIFICANCE_NZ;
      BlockMeta &meta = frames[cur_frame].meta_at(mb_coord.mb_x, mb_coord.mb_y);
      int nonzero_bits[6] = {};
      for (int i= 0; i < 6; ++i) {
          nonzero_bits[i] = (meta.num_nonzeros[mb_coord.scan8_index] & (1 << i)) >> i;
      }
#define QUEUE_MODE
#ifdef QUEUE_MODE
      const uint32_t serialized_bits = sub_mb_size > 16 ? 6 : sub_mb_size > 4 ? 4 : 2;
      {
          uint32_t i = 0;
          uint32_t serialized_so_far = 0;
          CoefficientCoord neighbor;
          uint32_t left_nonzero = 0;
          uint32_t above_nonzero = 0;
          bool has_left = get_neighbor_sub_mb(false, sub_mb_size, mb_coord, &neighbor);
          if (has_left) {
              left_nonzero = frames[cur_frame].meta_at(neighbor.mb_x, neighbor.mb_y).num_nonzeros[neighbor.scan8_index];
          }
          bool has_above = get_neighbor_sub_mb(true, sub_mb_size, mb_coord, &neighbor);
          if (has_above) {
              above_nonzero = frames[cur_frame].meta_at(neighbor.mb_x, neighbor.mb_y).num_nonzeros[neighbor.scan8_index];
          }
          
          do {
              uint32_t cur_bit = (1<<i);
              int left_nonzero_bit = 2;
              if (has_left) {
                  left_nonzero_bit = (left_nonzero >= cur_bit);
              }
              int above_nonzero_bit = 2;
              if (above_nonzero) {
                  above_nonzero_bit = (above_nonzero >= cur_bit);
              }
              put_or_get(model_key(&(STATE_FOR_NUM_NONZERO_BIT[i]), serialized_so_far + 64 * (frames[!cur_frame].meta_at(mb_coord.mb_x, mb_coord.mb_y).num_nonzeros[mb_coord.scan8_index] >= cur_bit) + 128 * left_nonzero_bit + 384 * above_nonzero_bit, meta.is_8x8 + sub_mb_is_dc * 2 + sub_mb_chroma422 + sub_mb_cat * 4), &nonzero_bits[i]);
              if (nonzero_bits[i]) {
                  serialized_so_far |= cur_bit;
              }
          } while (++i < serialized_bits);
          if (block_of_interest) {
              LOG_NEIGHBORS("<{");
          }
          if (has_left) {
              if (block_of_interest) {
                  LOG_NEIGHBORS("%d,", left_nonzero);
              }
          } else {
              if (block_of_interest) {
                  LOG_NEIGHBORS("X,");
              }
          }
          if (has_above) {
              if (block_of_interest) {
                  LOG_NEIGHBORS("%d,", above_nonzero);
              }
          } else {
              if (block_of_interest) {
                  LOG_NEIGHBORS("X,");
              }
          }
          if (frames[!cur_frame].meta_at(mb_coord.mb_x, mb_coord.mb_y).coded) {
              if (block_of_interest) {
                  LOG_NEIGHBORS("%d",frames[!cur_frame].meta_at(mb_coord.mb_x, mb_coord.mb_y).num_nonzeros[mb_coord.scan8_index]);
              }
          } else {
              if (block_of_interest) {
                  LOG_NEIGHBORS("X");
              }
          }
      }
#endif
      meta.num_nonzeros[mb_coord.scan8_index] = 0;
      for (int i= 0; i < 6; ++i) {
          meta.num_nonzeros[mb_coord.scan8_index] |= nonzero_bits[i] << i;
      }
      if (block_of_interest) {
          LOG_NEIGHBORS("} %d> ",meta.num_nonzeros[mb_coord.scan8_index]);
      }
      coding_type = last;
    }
  }
  void end_coding_type(CodingType ct) {
      if (ct == PIP_SIGNIFICANCE_MAP) {
        assert(coding_type == PIP_UNREACHABLE
               || (coding_type == PIP_SIGNIFICANCE_MAP && mb_coord.zigzag_index == 0));
        uint8_t num_nonzeros = 0;
        for (int i = 0; i < sub_mb_size; ++i) {
            int16_t res = frames[cur_frame].at(mb_coord.mb_x, mb_coord.mb_y).residual[mb_coord.scan8_index * 16 + i];
            assert(res == 1 || res == 0);
            if (res != 0) {
                num_nonzeros += 1;
            }
        }
        BlockMeta &meta = frames[cur_frame].meta_at(mb_coord.mb_x, mb_coord.mb_y);
        meta.is_8x8 = meta.is_8x8 || (sub_mb_size > 32); // 8x8 will have DC be 2x2
        meta.coded = true;
        assert(meta.num_nonzeros[mb_coord.scan8_index] == 0 || meta.num_nonzeros[mb_coord.scan8_index] == num_nonzeros);
        meta.num_nonzeros[mb_coord.scan8_index] = num_nonzeros;
      }
      coding_type = PIP_UNKNOWN;
  }
  bool begin_coding_type(CodingType ct, int zz_index, int param0, int param1) {

    bool begin_queueing = false;
    coding_type = ct;
    switch (ct) {
    case PIP_SIGNIFICANCE_MAP:
      {
          BlockMeta &meta = frames[cur_frame].meta_at(mb_coord.mb_x, mb_coord.mb_y);
          meta.num_nonzeros[mb_coord.scan8_index] = 0;
      }
      assert(!zz_index);
      nonzeros_observed = 0;
      if (sub_mb_is_dc) {
        mb_coord.zigzag_index = 0;
      } else {
        mb_coord.zigzag_index = 0;
      }
      begin_queueing = true;
      break;
    default:
      break;
    }
    return begin_queueing;
  }
  void reset_mb_significance_state_tracking() {
      mb_coord.zigzag_index = 0;
      nonzeros_observed = 0;
      coding_type = PIP_SIGNIFICANCE_MAP;
  }
  void update_state_tracking(int symbol) {
    switch (coding_type) {
    case PIP_SIGNIFICANCE_NZ:
      break;
    case PIP_SIGNIFICANCE_MAP:
      frames[cur_frame].at(mb_coord.mb_x, mb_coord.mb_y).residual[mb_coord.scan8_index * 16 + mb_coord.zigzag_index] = symbol;
      nonzeros_observed += symbol;
      if (mb_coord.zigzag_index + 1 == sub_mb_size) {
        coding_type = PIP_UNREACHABLE;
        mb_coord.zigzag_index = 0;
      } else {
        if (symbol) {
          coding_type = PIP_SIGNIFICANCE_EOB;
        } else {
          ++mb_coord.zigzag_index;
          if (mb_coord.zigzag_index + 1 == sub_mb_size) {
              // if we were a zero and we haven't eob'd then the
              // next and last must be a one
              frames[cur_frame].at(mb_coord.mb_x, mb_coord.mb_y).residual[mb_coord.scan8_index * 16 + mb_coord.zigzag_index] = 1;
              ++nonzeros_observed;
              coding_type = PIP_UNREACHABLE;
              mb_coord.zigzag_index = 0;
          }
        }
      }
      break;
    case PIP_SIGNIFICANCE_EOB:
      if (symbol) {
        mb_coord.zigzag_index = 0;
        coding_type = PIP_UNREACHABLE;
      } else if (mb_coord.zigzag_index + 2 == sub_mb_size) {
        frames[cur_frame].at(mb_coord.mb_x, mb_coord.mb_y).residual[mb_coord.scan8_index * 16 + mb_coord.zigzag_index + 1] = 1;
        coding_type = PIP_UNREACHABLE;  
      } else {
        coding_type = PIP_SIGNIFICANCE_MAP;
        ++mb_coord.zigzag_index;
      }
      break;
    case PIP_RESIDUALS:
    case PIP_UNKNOWN:
      break;
    case PIP_UNREACHABLE:
      assert(false);
    default:
      assert(false);
    }
  }
  void update_state(int symbol, const void *context) {
      update_state_for_model_key(symbol, get_model_key(context));
  }
  void update_state_for_model_key(int symbol, model_key key) {
    if (coding_type == PIP_SIGNIFICANCE_EOB) {
        int num_nonzeros = frames[cur_frame].meta_at(mb_coord.mb_x, mb_coord.mb_y).num_nonzeros[mb_coord.scan8_index];
        assert(symbol == (num_nonzeros == nonzeros_observed));
    }
    auto* e = &estimators[key];
    if (symbol) {
      e->pos++;
    } else {
      e->neg++;
    }
    if ((coding_type != PIP_SIGNIFICANCE_MAP && e->pos + e->neg > 0x60)
        || (coding_type == PIP_SIGNIFICANCE_MAP && e->pos + e->neg > 0x50)) {
      e->pos = (e->pos + 1) / 2;
      e->neg = (e->neg + 1) / 2;
    }
    update_state_tracking(symbol);
  }

  const uint8_t bypass_context = 0, terminate_context = 0, significance_context = 0;
  CoefficientCoord mb_coord;
  int nonzeros_observed = 0;
  int sub_mb_cat = -1;
  int sub_mb_size = -1;
  int sub_mb_is_dc = 0;
  int sub_mb_chroma422 = 0;
 private:
  struct estimator { int pos = 1, neg = 1; };
  std::map<model_key, estimator> estimators;
};

class h264_symbol {
public:
  h264_symbol(int symbol, const void*state)
    : symbol(symbol), state(state) {
  }

  template <class T>
  void execute(T &encoder, h264_model *model,
      Recoded::Block *out, std::vector<uint8_t> &encoder_out) {
    bool in_significance_map = (model->coding_type == PIP_SIGNIFICANCE_MAP);
    bool block_of_interest = (model->sub_mb_cat == 1 || model->sub_mb_cat == 2);
    bool print_priors = in_significance_map && block_of_interest;
    if (model->coding_type != PIP_SIGNIFICANCE_EOB) {
      size_t billable_bytes = encoder.put(symbol, [&](range_t range){
          return model->probability_for_state(range, state); });
      if (billable_bytes) {
        model->billable_bytes(billable_bytes);
      }
    }else if (block_of_interest) {
        if (symbol) {
            LOG_NEIGHBORS("\n");
        }
    }
    if (print_priors) {
        model->enable_debug();
    }
    model->update_state(symbol, state);
    if (print_priors) {
        LOG_NEIGHBORS("%d ", symbol);
        model->disable_debug();
    }
    if (state == &model->terminate_context && symbol) {
      encoder.finish();
      out->set_cabac(&encoder_out[0], encoder_out.size());
    }
  }
private:
  int symbol;
  const void* state;
};


//这个类用于对输入文件进行压缩，并将压缩后的数据写入到指定的输出流。
class compressor {
 public:
  //公共成员函数： 接受文件名，输出数据流
  compressor(const std::string& input_filename, std::ostream& out_stream)
    : input_filename(input_filename), out_stream(out_stream) {
    if (av_file_map(input_filename.c_str(), &original_bytes, &original_size, 0, NULL) < 0) {
      throw std::invalid_argument("Failed to open file: " + input_filename);
    }
  }
  //析构函数，清零变量
  ~compressor() {
    av_file_unmap(original_bytes, original_size);
  }

//run方法：负责整个压缩过程
  void run() {
    // Run through all the frames in the file, building the output using our hooks.
    av_decoder<compressor> d(this, input_filename);
    d.dump_stream_info(); //输出流信息，这个方法会读取音视频文件的流信息并显示在控制台上
    d.decode_video();     //解码视频，这个方法会逐帧解码输入文件中的所有视频帧。

    // Flush the final block to the output and write to stdout.
    out.add_block()->set_literal(
        &original_bytes[prev_coded_block_end], original_size - prev_coded_block_end);
    out_stream << out.SerializeAsString();
  }

  //read_packet 方法：从原始字节中读取数据包。
  int read_packet(uint8_t *buffer_out, int size) {
    size = std::min(size, int(original_size - read_offset));
    memcpy(buffer_out, &original_bytes[read_offset], size);
    read_offset += size;
    return size;
  }

  //内部类 cabac_decoder：这个类是一个 CABAC 解码器，用于处理 CABAC 编码的数据。
  class cabac_decoder {
   public:
    cabac_decoder(compressor *c, CABACContext *ctx_in, const uint8_t *buf, int size) {
      out = c->find_next_coded_block_and_emit_literal(buf, size);
      model = nullptr;
      if (out == nullptr) {
        // We're skipping this block, so disable calls to our hooks.
        ctx_in->coding_hooks = nullptr;
        ctx_in->coding_hooks_opaque = nullptr;
        ::ff_reset_cabac_decoder(ctx_in, buf, size);
        return;
      }

      out->set_size(size);

      ctx = *ctx_in;
      ctx.coding_hooks = nullptr;
      ctx.coding_hooks_opaque = nullptr;
      ::ff_reset_cabac_decoder(&ctx, buf, size);

      this->c = c;
      model = &c->model;
      model->reset();
    }
    ~cabac_decoder() { assert(out == nullptr || out->has_cabac()); }

    void execute_symbol(int symbol, const void* state) {
      h264_symbol sym(symbol, state);
#define QUEUE_MODE
#ifdef QUEUE_MODE
      if (queueing_symbols == PIP_SIGNIFICANCE_MAP || queueing_symbols == PIP_SIGNIFICANCE_EOB || !symbol_buffer.empty()) {
        symbol_buffer.push_back(sym);
        model->update_state_tracking(symbol);
      } else {
#endif
        sym.execute(encoder, model, out, encoder_out);
#ifdef QUEUE_MODE
      }
#endif
    }

    int get(uint8_t *state) {
      int symbol = ::ff_get_cabac(&ctx, state);
      execute_symbol(symbol, state);
      return symbol;
    }

    int get_bypass() {
      int symbol = ::ff_get_cabac_bypass(&ctx);
      execute_symbol(symbol, &model->bypass_context);
      return symbol;
    }

    int get_terminate() {
      int n = ::ff_get_cabac_terminate(&ctx);
      int symbol = (n != 0);
      execute_symbol(symbol, &model->terminate_context);
      return symbol;
    }

    void begin_coding_type(
        CodingType ct, int zigzag_index, int param0, int param1) {
      if (!model) {
          return;
      }
      bool begin_queue = model->begin_coding_type(ct, zigzag_index, param0, param1);
      if (begin_queue && (ct == PIP_SIGNIFICANCE_MAP || ct == PIP_SIGNIFICANCE_EOB)) {
        push_queueing_symbols(ct);
      }
    }
    void end_coding_type(CodingType ct) {
      if (!model) {
          return;
      }
      model->end_coding_type(ct);

      if ((ct == PIP_SIGNIFICANCE_MAP || ct == PIP_SIGNIFICANCE_EOB)) {
        stop_queueing_symbols();
        model->finished_queueing(ct,
               [&](model_key key, int*symbol) {
               size_t billable_bytes = encoder.put(*symbol, [&](range_t range){
                   return model->probability_for_model_key(range, key);
               });
               model->update_state_for_model_key(*symbol, key);
               if (billable_bytes) {
                   model->billable_bytes(billable_bytes);
               }
            });
        static int i = 0;
        if (i++ < 10) {
        std::cerr << "FINISHED QUEUING DECODE: " << (int)(model->frames[model->cur_frame].meta_at(model->mb_coord.mb_x, model->mb_coord.mb_y).num_nonzeros[model->mb_coord.scan8_index]) << std::endl;
        }
        pop_queueing_symbols(ct);
        model->coding_type = PIP_UNKNOWN;
      }
    }

   private:
    void push_queueing_symbols(CodingType ct) {
      // Does not currently support nested queues.
      assert (queueing_symbols == PIP_UNKNOWN);
      assert (symbol_buffer.empty());
      queueing_symbols = ct;
    }

    void stop_queueing_symbols() {
      assert (queueing_symbols != PIP_UNKNOWN);
      queueing_symbols = PIP_UNKNOWN;
    }

    void pop_queueing_symbols(CodingType ct) {
        //std::cerr<< "FINISHED QUEUEING "<< symbol_buffer.size()<<std::endl;
      if (ct == PIP_SIGNIFICANCE_MAP || ct == PIP_SIGNIFICANCE_EOB) {
        if (model) {
          model->reset_mb_significance_state_tracking();
        }
      }
      for (auto &sym : symbol_buffer) {
        sym.execute(encoder, model, out, encoder_out);
      }
      symbol_buffer.clear();
    }

    Recoded::Block *out;
    CABACContext ctx;

    compressor *c;
    h264_model *model;
    std::vector<uint8_t> encoder_out;
    recoded_code::encoder<std::back_insert_iterator<std::vector<uint8_t>>, uint8_t> encoder{
      std::back_inserter(encoder_out)};

    CodingType queueing_symbols = PIP_UNKNOWN;
    std::vector<h264_symbol> symbol_buffer;
  };
  //get_model 方法：返回指向 h264_model 实例的指针。
  h264_model *get_model() {
    return &model;
  }

  private:
  //私有方法和成员变量，只能被该类的成员函数访问
  //包含find_next_coded_block_and_emit_literal方法
  //input_filename、out_stream、original_bytes等私有成员变量
  Recoded::Block* find_next_coded_block_and_emit_literal(const uint8_t *buf, int size) {
    uint8_t *found = static_cast<uint8_t*>( memmem(
        &original_bytes[prev_coded_block_end], read_offset - prev_coded_block_end,
        buf, size) );
    if (found && size >= SURROGATE_MARKER_BYTES) {
      size_t gap = found - &original_bytes[prev_coded_block_end];
      out.add_block()->set_literal(&original_bytes[prev_coded_block_end], gap);
      prev_coded_block_end += gap + size;
      Recoded::Block *newBlock = out.add_block();
      newBlock->set_length_parity(size & 1);
      if (size > 1) {
        newBlock->set_last_byte(&(buf[size - 1]), 1);
      }
      return newBlock;  // Return a block for the recoder to fill.
    } else {
      // Can't recode this block, probably because it was NAL-escaped. Place
      // a skip marker in the block list.
      Recoded::Block* block = out.add_block();
      block->set_skip_coded(true);
      block->set_size(size);
      return nullptr;  // Tell the recoder to ignore this block.
    }
  }

  std::string input_filename;
  std::ostream& out_stream;

  uint8_t *original_bytes = nullptr;
  size_t original_size = 0;
  int read_offset = 0;
  int prev_coded_block_end = 0;

  h264_model model;
  Recoded out;
};

int
main(int argc, char **argv) {
  av_register_all();

  if (argc < 3 || argc > 4) {
    std::cerr << "Usage: " << argv[0] << " [compress|decompress|roundtrip] <input> [output]" << std::endl;
    return 1;
  }
  std::string command = argv[1];
  std::string input_filename = argv[2];
  std::ofstream out_file;
  if (argc > 3) {
    out_file.open(argv[3]);
  }

  try {
    if (command == "compress") {
      compressor c(input_filename, out_file.is_open() ? out_file : std::cout);
      c.run();
    } 
  } catch (const std::exception& e) {
    std::cerr << "Exception (" << typeid(e).name() << "): " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
