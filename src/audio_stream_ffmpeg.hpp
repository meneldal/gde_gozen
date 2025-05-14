#pragma once

#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/audio_stream_playback_resampled.hpp>
#include <godot_cpp/classes/audio_stream_playback.hpp>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavcodec/codec_id.h>
#include <libavcodec/packet.h>

#include <libavdevice/avdevice.h>

#include <libavformat/avformat.h>

#include <libavutil/avassert.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/display.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/rational.h>
#include <libavutil/timestamp.h>

#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include "gozen_error.hpp"

using namespace godot;

class AudioStreamFFmpeg : public AudioStream
{
    GDCLASS(AudioStreamFFmpeg, AudioStream);

public:
    double _get_length() const override { return 0; }
    bool _is_monophonic() const override { return true; }
    Ref<AudioStreamPlayback> _instantiate_playback() const override;
    static Ref<AudioStreamFFmpeg> load_from_file(String path);
    virtual ~AudioStreamFFmpeg() {} // todo free ffmpeg stuff
    int error = 0;
    friend class AudioStreamFFmpegPlayback;

protected:
    static inline void _bind_methods()
    {
        ClassDB::bind_static_method("AudioStreamFFmpeg", D_METHOD("load_from_file", "a_file_path"), &AudioStreamFFmpeg::load_from_file);
    }

private:
    AVFormatContext *m_format_ctx = nullptr;
    AVStream *m_stream = nullptr;
    AVCodecContext *l_codec_ctx_audio = nullptr;
    int m_bytes_per_samples = 0;
    int m_stream_idx = 0;
    bool l_stereo = true;
    AVChannelLayout l_ch_layout;
    struct SwrContext *l_swr_ctx = nullptr;
};

class AudioStreamFFmpegPlayback : public AudioStreamPlaybackResampled
{
    GDCLASS(AudioStreamFFmpegPlayback, AudioStreamPlaybackResampled);

public:
    void _start(double p_from_pos) override;
    void _stop() override;
    bool _is_playing() const override;
    int32_t _get_loop_count() const override { return 0; }
    double _get_playback_position() const override;
    void _seek(double p_position) override;
    int32_t _mix_resampled(AudioFrame *p_buffer, int32_t p_frames) override;
    float _get_stream_sampling_rate() const override { return mix_rate; }
    AudioStreamFFmpegPlayback()
    {
        buffer = new sint16_stereo[buffer_len];
    }
    virtual ~AudioStreamFFmpegPlayback()
    {
        delete[] buffer;
    } // todo free ffmpeg stuff
    friend class AudioStreamFFmpeg;

protected:
    static inline void _bind_methods()
    {
    }

private:
    const AudioStreamFFmpeg *m_stream;
    struct sint16_stereo
    {
        int16_t l;
        int16_t r;
    };
    sint16_stereo *buffer;
    size_t buffer_len = 4410000; // 10s
    size_t buffer_fill = 0;      // number of samples current in buffer
    bool is_playing = false;
    AVFrame *l_frame = av_frame_alloc();
    AVFrame *l_decoded_frame = av_frame_alloc();
    AVPacket *l_packet = av_packet_alloc();
    bool l_stereo = true;
    uint32_t mixed = 0;
    uint32_t mix_rate = 44100;

    bool fill_buffer();
};
