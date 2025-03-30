#include "audio_stream_ffmpeg.hpp"
#include "ffmpeg.hpp"


Ref<AudioStreamPlayback> AudioStreamFFmpeg::_instantiate_playback() const
{
    auto myplayback = memnew(AudioStreamFFmpegPlayback);
    myplayback->m_stream=this;
    myplayback->l_stereo=l_stereo;
    myplayback->fill_buffer();
    return myplayback;
}

Ref<AudioStreamFFmpeg> AudioStreamFFmpeg::load_from_file(String a_path)
{
    UtilityFunctions::print("start reading from file\n");
    auto mystream = memnew(AudioStreamFFmpeg);
    mystream->m_format_ctx = avformat_alloc_context();

	if (!mystream->m_format_ctx) {
        mystream->error = GoZenError::ERR_CREATING_AV_FORMAT_FAILED;
		return mystream;
	}

	if (avformat_open_input(&mystream->m_format_ctx, a_path.utf8(), NULL, NULL)) {
        mystream->error = GoZenError::ERR_OPENING_AUDIO;
		return mystream;
	}

	if (avformat_find_stream_info(mystream->m_format_ctx, NULL)) {
		mystream->error = GoZenError::ERR_NO_STREAM_INFO_FOUND;
		return nullptr;
	}

	for (int i = 0; i < mystream->m_format_ctx->nb_streams; i++) {
		AVCodecParameters *av_codec_params = mystream->m_format_ctx->streams[i]->codecpar;

		if (!avcodec_find_decoder(av_codec_params->codec_id)) {
			mystream->m_format_ctx->streams[i]->discard = AVDISCARD_ALL;
			continue;
		} else if (av_codec_params->codec_type == AVMEDIA_TYPE_AUDIO) {
            //we got the right track with audio
			mystream->m_stream = mystream->m_format_ctx->streams[i];
			break;
		}
	}
    if (!mystream->m_format_ctx->streams){
        //no audio stream found
        mystream->error = GoZenError::ERR_OPENING_AUDIO;
    }

    const AVCodec *l_codec_audio = avcodec_find_decoder(mystream->m_stream->codecpar->codec_id);
	if (!l_codec_audio) {
		UtilityFunctions::printerr("Couldn't find any codec decoder for audio!");
		return mystream;
	}

	mystream->l_codec_ctx_audio = avcodec_alloc_context3(l_codec_audio);
	if (mystream->l_codec_ctx_audio == NULL) {
		UtilityFunctions::printerr("Couldn't allocate codec context for audio!");
		return mystream;
	} else if (avcodec_parameters_to_context(mystream->l_codec_ctx_audio, mystream->m_stream->codecpar)) {
		UtilityFunctions::printerr("Couldn't initialize audio codec context!");
		return mystream;
	}

	// FFmpeg::enable_multithreading(mystream->l_codec_ctx_audio, l_codec_audio);
	mystream->l_codec_ctx_audio->request_sample_fmt = AV_SAMPLE_FMT_S16;

	// Open codec - Audio
	if (avcodec_open2(mystream->l_codec_ctx_audio, l_codec_audio, NULL)) {
		UtilityFunctions::printerr("Couldn't open audio codec!");
		return mystream;
	}

	
	if (mystream->l_codec_ctx_audio->ch_layout.nb_channels <= 3) 
    mystream->l_ch_layout = mystream->l_codec_ctx_audio->ch_layout;
	else
    mystream->l_ch_layout = AV_CHANNEL_LAYOUT_STEREO;

	auto response = swr_alloc_set_opts2(
		&mystream->l_swr_ctx, &mystream->l_ch_layout, AV_SAMPLE_FMT_S16,
		mystream->l_codec_ctx_audio->sample_rate, &mystream->l_codec_ctx_audio->ch_layout,
		mystream->l_codec_ctx_audio->sample_fmt, mystream->l_codec_ctx_audio->sample_rate, 0,
		nullptr);

	if (response < 0) {
		FFmpeg::print_av_error("Failed to obtain SWR context!", response);
		avcodec_flush_buffers(mystream->l_codec_ctx_audio);
		avcodec_free_context(&mystream->l_codec_ctx_audio);
		return mystream;
	}

	response = swr_init(mystream->l_swr_ctx);
	if (response < 0) {
		FFmpeg::print_av_error("Couldn't initialize SWR!", response);
		avcodec_flush_buffers(mystream->l_codec_ctx_audio);
		avcodec_free_context(&mystream->l_codec_ctx_audio);
		return mystream;
	}

	// if (!l_frame || !l_decoded_frame || !l_packet) {
	// 	UtilityFunctions::printerr("Couldn't allocate frames or packet for audio!");
	// 	avcodec_flush_buffers(l_codec_ctx_audio);
	// 	avcodec_free_context(&l_codec_ctx_audio);
	// 	swr_free(&l_swr_ctx);
	// 	return mystream;
	// }

	mystream->m_bytes_per_samples = av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
	mystream->l_stereo = mystream->l_codec_ctx_audio->ch_layout.nb_channels >= 2;
    return mystream;
}

void AudioStreamFFmpegPlayback::_start(double p_from_pos)
{
    is_playing=true;
}

void AudioStreamFFmpegPlayback::_stop()
{
    is_playing=false;
}

bool AudioStreamFFmpegPlayback::_is_playing() const
{
    return is_playing;
}

double AudioStreamFFmpegPlayback::_get_playback_position() const
{
    return mixed / mix_rate;
}

void AudioStreamFFmpegPlayback::_seek(double p_position)
{
}

int32_t AudioStreamFFmpegPlayback::_mix_resampled(AudioFrame *p_buffer, int32_t p_frames)
{
    // UtilityFunctions::print("start mix\n");
    while(buffer_fill<p_frames)
        fill_buffer();
    //we have a buffer with buffer_fill values and need to provide p_frames
    if(p_frames<=buffer_fill)
    {
        for(int i=0;i<p_frames;++i)
            p_buffer[i] = AudioFrame{static_cast<float>(buffer[i].l)/32767.0f,static_cast<float>(buffer[i].r)/32767.0f};
        buffer_fill-=p_frames;
        std::memmove(buffer,buffer+p_frames*4,buffer_fill*4); //move memory back to beginning of buffer 4 bytes per sample
        
        mixed+=p_frames;
        return p_frames;
    }
    return 0;
}

void AudioStreamFFmpegPlayback::fill_buffer()
{
    // UtilityFunctions::print("fill buffer\n");
    if (FFmpeg::get_frame(m_stream->m_format_ctx, m_stream->l_codec_ctx_audio, m_stream->m_stream->index, l_frame, l_packet))
			//error
            return;
        
		// Copy decoded data to new frame
		l_decoded_frame->format = AV_SAMPLE_FMT_S16;
		l_decoded_frame->ch_layout = m_stream->l_ch_layout;
		l_decoded_frame->sample_rate = l_frame->sample_rate;
		l_decoded_frame->nb_samples = swr_get_out_samples(m_stream->l_swr_ctx, l_frame->nb_samples);

		if (auto resp =(av_frame_get_buffer(l_decoded_frame, 0)) < 0) {
			FFmpeg::print_av_error("Couldn't create new frame for swr!", resp);
			av_frame_unref(l_frame);
			av_frame_unref(l_decoded_frame);
			return;
		}

		if (auto resp = swr_config_frame(m_stream->l_swr_ctx, l_decoded_frame, l_frame) < 0) {
			FFmpeg::print_av_error("Couldn't config the audio frame!", resp);
			av_frame_unref(l_frame);
			av_frame_unref(l_decoded_frame);
			return;
		}

		if (auto resp = swr_convert_frame(m_stream->l_swr_ctx, l_decoded_frame, l_frame) < 0) {
			FFmpeg::print_av_error("Couldn't convert the audio frame!", resp);
			av_frame_unref(l_frame);
			av_frame_unref(l_decoded_frame);
			return;
		}

		size_t l_byte_size = l_decoded_frame->nb_samples * m_stream->m_bytes_per_samples;
		if (m_stream->l_codec_ctx_audio->ch_layout.nb_channels >= 2)
			l_byte_size *= 2;

        //UtilityFunctions::print("buffer size, ffmpeg buff",buffer_len," ",l_decoded_frame->nb_samples);
        // std::memcpy(buffer+buffer_fill*4,l_decoded_frame->extended_data[0],l_byte_size);
        std::memcpy(buffer+(buffer_fill)*4,l_decoded_frame->extended_data[0],l_byte_size);

		buffer_fill+=l_decoded_frame->nb_samples;
        mix_rate = l_frame->sample_rate;
		av_frame_unref(l_frame);
		av_frame_unref(l_decoded_frame);
}
