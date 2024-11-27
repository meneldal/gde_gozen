#include "renderer.hpp"

// TODO: Only show encoders to people which support S16

Renderer::~Renderer() {
	close();
}

PackedStringArray Renderer::get_available_codecs(int a_codec_id) {
	PackedStringArray l_data = PackedStringArray();

	const AVCodec *l_codec = nullptr;
	void *l_i = nullptr;

	while ((l_codec = av_codec_iterate(&l_i)))
		if (l_codec->id == a_codec_id and av_codec_is_encoder(l_codec))
			l_data.append(l_codec->name);

	return l_data;
}

int Renderer::open() {
	if (renderer_open) {
		UtilityFunctions::printerr("Render already open!");
		return -1;
	} else {
		if (path == "") {
			UtilityFunctions::printerr("Path is not set!");
			return -2;
		} else if (video_codec_id == AV_CODEC_ID_NONE) {
			UtilityFunctions::printerr("Video codec not set!");
			return -3;
		} else if (audio_codec_id == AV_CODEC_ID_NONE) {
			_print_debug("Audio codec not set, not rendering audio!");
		} else if (audio_codec_id != AV_CODEC_ID_NONE && sample_rate == -1) {
			UtilityFunctions::printerr("A sample rate needs to be set for audio exporting!");
			audio_codec_id = AV_CODEC_ID_NONE;
		}
	}

	// Allocating output media context
	avformat_alloc_output_context2(&av_format_ctx, NULL, NULL, path.utf8());
	if (!av_format_ctx) {
		UtilityFunctions::printerr("Couldn't allocate av format context by looking at path extension, using MPEG!");
		avformat_alloc_output_context2(&av_format_ctx, NULL, "mpeg", path.utf8());
	} if (!av_format_ctx)
		return -4;
	
	av_output_format = av_format_ctx->oformat;

	// Setting up video stream
	const AVCodec *av_codec_video = avcodec_find_encoder(video_codec_id);
	if (!av_codec_video) {
		UtilityFunctions::printerr("Video codec '", avcodec_get_name(video_codec_id), "' not found!");
		return -5;
	}

	av_packet_video = av_packet_alloc();
	if (!av_packet_video) {
		UtilityFunctions::printerr("Couldn't allocate packet!");
		return -8;
	}

	av_stream_video = avformat_new_stream(av_format_ctx, NULL);
	if (!av_stream_video) {
		UtilityFunctions::printerr("Couldn't create stream!");
		return -6;
	}
	av_stream_video->id = av_format_ctx->nb_streams-1;

	av_codec_ctx_video = avcodec_alloc_context3(av_codec_video);
	if (!av_codec_ctx_video) {
		UtilityFunctions::printerr("Couldn't allocate video codec context!");
		return -5;
	}

	FFmpeg::enable_multithreading(av_codec_ctx_video, av_codec_video);

	av_codec_ctx_video->codec_id = video_codec_id;
	av_codec_ctx_video->bit_rate = bit_rate;
	av_codec_ctx_video->pix_fmt = AV_PIX_FMT_YUV420P;
	av_codec_ctx_video->width = resolution.x; // Resolution must be a multiple of two
	av_codec_ctx_video->height = resolution.y;
	av_stream_video->time_base = (AVRational){1, (int)framerate};
	av_codec_ctx_video->time_base = av_stream_video->time_base;
	av_codec_ctx_video->framerate = (AVRational){(int)framerate, 1};
	av_codec_ctx_video->gop_size = gop_size;

	if (av_codec_ctx_video->codec_id == AV_CODEC_ID_MPEG2VIDEO)
		av_codec_ctx_video->max_b_frames = 2;
	else av_codec_ctx_video->max_b_frames = 1;

	if (av_codec_ctx_video->codec_id == AV_CODEC_ID_MPEG1VIDEO)
		av_codec_ctx_video->mb_decision = 2;

	// Some formats want stream headers separated
	if (av_output_format->flags & AVFMT_GLOBALHEADER)
		av_codec_ctx_video->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	// Encoding options for different codecs
	if (av_codec_video->id == AV_CODEC_ID_H264)
		av_opt_set(av_codec_ctx_video->priv_data, "preset", h264_preset.c_str(), 0);

	// Opening the video encoder codec
	response = avcodec_open2(av_codec_ctx_video, av_codec_video, NULL);
	if (response < 0) {
		FFmpeg::print_av_error("Couldn't open video codec context!", response);
		return -7;
	}

	av_frame_video = av_frame_alloc();
	if (!av_frame_video) {
		UtilityFunctions::printerr("Couldn't allocate frame!");
		return -100;
	}

	av_frame_video->format = av_codec_ctx_video->pix_fmt;
	av_frame_video->width = resolution.x;
	av_frame_video->height = resolution.y;
	if (av_frame_get_buffer(av_frame_video, 0)) {
		UtilityFunctions::printerr("Couldn't allocate frame data!");
		return -8;
	}

	// Copy video stream params to muxer
	if (avcodec_parameters_from_context(av_stream_video->codecpar, av_codec_ctx_video) < 0) {
		UtilityFunctions::printerr("Couldn't copy video stream params!");
		return -9;
	}

	if (audio_codec_id != AV_CODEC_ID_NONE) {
		const AVCodec *av_codec_audio = avcodec_find_encoder(audio_codec_id);
		if (!av_codec_audio) {
			UtilityFunctions::printerr("Audio codec '", avcodec_get_name(audio_codec_id), "' not found!");
			return -4;
		}

		av_packet_audio = av_packet_alloc();
		if (!av_packet_audio) {
			UtilityFunctions::printerr("Couldn't allocate packet!");
			return -540;
		}

		av_stream_audio = avformat_new_stream(av_format_ctx, NULL);
		if (!av_stream_audio) {
			UtilityFunctions::printerr("Couldn't create new stream!");
			return -3;
		}
		av_stream_audio->id = av_format_ctx->nb_streams-1;

		av_codec_ctx_audio = avcodec_alloc_context3(av_codec_audio);
		if (!av_codec_ctx_audio) {
			UtilityFunctions::printerr("Couldn't allocate audio codec context!");
			return -5;
		}

		FFmpeg::enable_multithreading(av_codec_ctx_audio, av_codec_audio);

		av_codec_ctx_audio->bit_rate = 128000;
		av_codec_ctx_audio->sample_fmt = av_codec_audio->sample_fmts[0]; // AV_SAMPLE_FMT_S16;
		av_codec_ctx_audio->sample_rate = 44100;
		if (av_codec_audio->supported_samplerates) {
			for (int i = 0; av_codec_audio->supported_samplerates[i]; i++) {
				if (av_codec_audio->supported_samplerates[i] == 48000) {
					av_codec_ctx_audio->sample_rate = 48000;
					break;
				}
			}
		}
		av_codec_ctx_audio->time_base = (AVRational){1, av_codec_ctx_audio->sample_rate};
		av_stream_audio->time_base = av_codec_ctx_audio->time_base;

		AVChannelLayout l_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
		av_channel_layout_copy(&av_codec_ctx_audio->ch_layout, &(l_ch_layout));

		// Opening the audio encoder codec
		response = avcodec_open2(av_codec_ctx_audio, av_codec_audio, NULL);
		if (response < 0) {
			FFmpeg::print_av_error("Couldn't open audio codec!", response);
			return -4;
		}

		// Copy audio stream params to muxer
		if (avcodec_parameters_from_context(av_stream_audio->codecpar, av_codec_ctx_audio)) {
			UtilityFunctions::printerr("Couldn't copy audio stream params!");
			return -4;
		}

		if (av_output_format->flags & AVFMT_GLOBALHEADER)
			av_codec_ctx_audio->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}

	av_dump_format(av_format_ctx, 0, path.utf8(), 1);

	// Open output file if needed
	if (!(av_output_format->flags & AVFMT_NOFILE)) {
		response = avio_open(&av_format_ctx->pb, path.utf8(), AVIO_FLAG_WRITE);
		if (response < 0) {
			FFmpeg::print_av_error("Couldn't open output file!", response);
			return -10;
		}
	}

	// Write stream header - if any
	response = avformat_write_header(av_format_ctx, NULL);
	if (response < 0) {
		FFmpeg::print_av_error("Error when writing header!", response);
		return -11;
	}

	// Setting up SWS
	sws_ctx = sws_getContext(
		av_frame_video->width, av_frame_video->height, AV_PIX_FMT_RGBA,
		av_frame_video->width, av_frame_video->height, AV_PIX_FMT_YUV420P,
		SWS_BILINEAR, NULL, NULL, NULL); // TODO: Future option to change SWS_BILINEAR
	if (!sws_ctx) {
		UtilityFunctions::printerr("Couldn't get sws context!");
		return -12;
	}

	frame_nr = 0;
	renderer_open = true;
	return OK;
}

int Renderer::send_frame(Ref<Image> a_image) {
	if (!renderer_open) {
		UtilityFunctions::printerr("Renderer isn't open!");
		return -6;
	} else if (audio_codec_id != AV_CODEC_ID_NONE && !audio_added) {
		UtilityFunctions::printerr("Audio codec set but not added yet!");
		return -1;
	} else if (!av_codec_ctx_video) {
		UtilityFunctions::printerr("Video codec isn't open!");
		return -2;
	}

	if (av_frame_make_writable(av_frame_video) < 0) {
		UtilityFunctions::printerr("Video frame is not writeable!");
		return -3;
	}

	uint8_t *l_src_data[4] = { a_image->get_data().ptrw(), NULL, NULL, NULL };
	int l_src_linesize[4] = { av_frame_video->width * 4, 0, 0, 0 };
	response = sws_scale(
			sws_ctx,
			l_src_data, l_src_linesize, 0, av_frame_video->height,
			av_frame_video->data, av_frame_video->linesize);
	if (response < 0) {
		FFmpeg::print_av_error("Scaling frame data failed!", response);
		return -4;
	}

	av_frame_video->pts = frame_nr;
	frame_nr++;

	// Adding frame
	response = avcodec_send_frame(av_codec_ctx_video, av_frame_video);
	if (response < 0) {
		FFmpeg::print_av_error("Error sending video frame!", response);
		return -5;
	}

	av_packet_video = av_packet_alloc();

	while (response >= 0) {
		response = avcodec_receive_packet(av_codec_ctx_video, av_packet_video);
		if (response == AVERROR(EAGAIN) || response == AVERROR_EOF)
			break;
		else if (response < 0) {
			FFmpeg::print_av_error("Error encoding video frame!", response);
			response = -1;
			av_packet_free(&av_packet_video);
			return response;
		}

		// Rescale output packet timestamp values from codec to stream timebase
		av_packet_video->stream_index = av_stream_video->index;
		av_packet_rescale_ts(av_packet_video, av_codec_ctx_video->time_base, av_stream_video->time_base);

		// Write the frame to file
		response = av_interleaved_write_frame(av_format_ctx, av_packet_video);
		if (response < 0) {
			FFmpeg::print_av_error("Error whilst writing output packet!", response);
			response = -1;
			av_packet_free(&av_packet_video);
			return response;
		}

		av_packet_unref(av_packet_video);
	}

	av_packet_free(&av_packet_video);
	return 0;
}

int Renderer::send_audio(PackedByteArray a_wav_data, int a_mix_rate) {
	if (!renderer_open) {
		UtilityFunctions::printerr("Renderer isn't open!");
		return -6;
	} else if (audio_codec_id == AV_CODEC_ID_NONE) {
		UtilityFunctions::printerr("Audio not enabled for this renderer!");
		return -1;
	} else if (audio_added) {
		UtilityFunctions::printerr("Audio already added!");
		return -2;
	}

	SwrContext *l_swr_ctx = nullptr;

	// Allocate and setup SWR
	AVChannelLayout l_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
	swr_alloc_set_opts2(
		&l_swr_ctx, 
		&l_ch_layout, av_codec_ctx_audio->sample_fmt, av_codec_ctx_audio->sample_rate,
		&l_ch_layout, AV_SAMPLE_FMT_S16, a_mix_rate,
		0, NULL);
	if (!l_swr_ctx) {
		UtilityFunctions::printerr("Failed to allocate SWR!");
		return -1;
	}

	if (swr_init(l_swr_ctx) < 0) {
		UtilityFunctions::printerr("Failed to initialize SWR!");
		swr_free(&l_swr_ctx);
		return -1;
	}
	
	const uint8_t *l_input_data = a_wav_data.ptr();
	int l_input_size = a_wav_data.size();

	AVFrame *l_frame_in = av_frame_alloc();
	if (!l_frame_in) {
		UtilityFunctions::printerr("Couldn't allocate av frame in!");
		swr_free(&l_swr_ctx);
		return -1;
	}

	l_frame_in->ch_layout = l_ch_layout;
	l_frame_in->format = AV_SAMPLE_FMT_S16;
	l_frame_in->sample_rate = a_mix_rate;

	int l_total_samples = l_input_size / (av_get_bytes_per_sample(AV_SAMPLE_FMT_S16) * 2); // Stereo
	const uint8_t ** l_data = &l_input_data;	

	// Allocate a buffer for the output in the target format
	AVFrame *l_frame_out = av_frame_alloc();
	if (!l_frame_out) {
		UtilityFunctions::printerr("Couldn't allocate av frame out!");
		av_frame_free(&l_frame_in);
		swr_free(&l_swr_ctx);
		return -1;
	}

	l_frame_out->ch_layout = av_codec_ctx_audio->ch_layout;
	l_frame_out->format = av_codec_ctx_audio->sample_fmt;
	l_frame_out->sample_rate = av_codec_ctx_audio->sample_rate;
	l_frame_out->nb_samples = av_codec_ctx_audio->frame_size;

	av_frame_get_buffer(l_frame_out, 0);

	av_packet_audio = av_packet_alloc();
	if (!av_packet_audio) {
		UtilityFunctions::printerr("Couldn't allocate av packet!");
		av_frame_free(&l_frame_in);
		av_frame_free(&l_frame_out);
		swr_free(&l_swr_ctx);
		return -1;
	}

	av_packet_audio->pts = l_frame_out->pts; // PTS from the frame (if available)
	av_packet_audio->dts = l_frame_out->pts; // DTS (can be same as PTS for audio)

	int l_remaining_samples = l_total_samples;
	while (l_remaining_samples > 0) {
		int l_samples_to_convert = FFMIN(l_remaining_samples, av_codec_ctx_audio->frame_size);
		
		// Resample the data 
		int l_converted_samples = swr_convert(
			l_swr_ctx, l_frame_out->data, l_samples_to_convert,
			l_data, l_samples_to_convert);
		if (l_converted_samples < 0) {
			UtilityFunctions::printerr("Error during resampling!");
			av_frame_free(&l_frame_in);
			av_frame_free(&l_frame_out);
			swr_free(&l_swr_ctx);
			return -12340;
		}

		// Send audio frame to the encoder
		response = avcodec_send_frame(av_codec_ctx_audio, l_frame_out);
		if (response < 0) {
			FFmpeg::print_av_error("Error sending audio frame!", response);
			av_frame_free(&l_frame_in);
			av_frame_free(&l_frame_out);
			swr_free(&l_swr_ctx);
			return -10;
		}

		while ((response = avcodec_receive_packet(av_codec_ctx_audio, av_packet_audio)) >= 0) {
			// Rescale packet timestamp if necessary
			av_packet_audio->stream_index = av_stream_audio->index;
			av_packet_rescale_ts(av_packet_audio, av_codec_ctx_audio->time_base, av_stream_audio->time_base);

			// Write audio frame
			response = av_interleaved_write_frame(av_format_ctx, av_packet_audio);
			if (response < 0) {
				FFmpeg::print_av_error("Error writing audio packet!", response);
				av_frame_free(&l_frame_in);
				av_frame_free(&l_frame_out);
				swr_free(&l_swr_ctx);
				return -320;
			}
			av_packet_unref(av_packet_audio);
		}

		l_remaining_samples -= l_samples_to_convert;
		l_input_data += l_samples_to_convert * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16) * 2;
	}

	// Signal the encoder to flush remaining frames
	int response = avcodec_send_frame(av_codec_ctx_audio, nullptr);
	if (response < 0) {
		FFmpeg::print_av_error("Error flushing audio encoder!", response);
		return -1;
	}

	// Drain the encoder
	while ((response = avcodec_receive_packet(av_codec_ctx_audio, av_packet_audio)) >= 0) {
		av_packet_audio->stream_index = av_stream_audio->index;
		av_packet_rescale_ts(av_packet_audio, av_codec_ctx_audio->time_base, av_stream_audio->time_base);

		response = av_interleaved_write_frame(av_format_ctx, av_packet_audio);
		if (response < 0) {
			FFmpeg::print_av_error("Error writing flushed audio packet!", response);
			av_packet_free(&av_packet_audio);
			return -320;
		}

		av_packet_unref(av_packet_audio);
	}

	if (response != AVERROR(EAGAIN) && response != AVERROR_EOF) {
		FFmpeg::print_av_error("Error during audio encoder flush!", response);
	}

	av_frame_free(&l_frame_in);
	av_frame_free(&l_frame_out);
	av_packet_free(&av_packet_audio);
	swr_free(&l_swr_ctx);

	audio_added = true;
	return OK;
}

int Renderer::close() {
	if (av_codec_ctx_video == nullptr)
		return -1;

	av_write_trailer(av_format_ctx);

	avcodec_free_context(&av_codec_ctx_video);
	if (av_codec_ctx_audio) avcodec_free_context(&av_codec_ctx_audio);

	if (av_frame_video) av_frame_free(&av_frame_video);
	if (av_packet_video) av_packet_free(&av_packet_video);

	if (av_packet_audio) av_packet_free(&av_packet_audio);

	if (sws_ctx) sws_freeContext(sws_ctx);

	if (!(av_output_format->flags & AVFMT_NOFILE))
		avio_closep(&av_format_ctx->pb);

	avformat_free_context(av_format_ctx);

	return OK;
}

void Renderer::_print_debug(std::string a_text) {
	if (debug)
		UtilityFunctions::print(a_text.c_str());
}

void Renderer::_printerr_debug(std::string a_text) {
	if (debug)
		UtilityFunctions::print(a_text.c_str());
}
