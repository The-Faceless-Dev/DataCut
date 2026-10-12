#include "media/MediaProbe.h"

#include <QFileInfo>

#include <stdexcept>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
}

namespace datacut {

MediaInfo probeMedia(const QString &path)
{
    AVFormatContext *raw = nullptr;
    const auto utf8Path = path.toUtf8();
    if (avformat_open_input(&raw, utf8Path.constData(), nullptr, nullptr) < 0)
        throw std::runtime_error("FFmpeg could not open this media file");
    struct FormatGuard {
        AVFormatContext *context;
        ~FormatGuard() { avformat_close_input(&context); }
    } guard{raw};
    if (avformat_find_stream_info(raw, nullptr) < 0)
        throw std::runtime_error("FFmpeg could not read stream information");

    MediaInfo info;
    info.path = QFileInfo(path).absoluteFilePath();
    info.name = QFileInfo(path).fileName();
    const QString extension = QFileInfo(path).suffix().toLower();
    info.isStillImage = extension == QStringLiteral("png")
        || extension == QStringLiteral("jpg") || extension == QStringLiteral("jpeg")
        || extension == QStringLiteral("webp") || extension == QStringLiteral("bmp");
    if (raw->duration != AV_NOPTS_VALUE)
        info.durationUs = raw->duration;

    for (unsigned i = 0; i < raw->nb_streams; ++i) {
        const auto *stream = raw->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            info.hasAudio = true;
            if (info.audioSampleRate == 0)
                info.audioSampleRate = stream->codecpar->sample_rate;
        }
        if (stream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO || info.hasVideo
            || extension == QStringLiteral("mp3")
            || (stream->disposition & AV_DISPOSITION_ATTACHED_PIC))
            continue;

        info.hasVideo = true;
        info.width = stream->codecpar->width;
        info.height = stream->codecpar->height;
        AVRational rate = stream->avg_frame_rate;
        if (rate.num <= 0 || rate.den <= 0)
            rate = stream->r_frame_rate;
        if (rate.num > 0 && rate.den > 0)
            info.frameRate = {rate.num, rate.den};
        if (stream->nb_frames > 0)
            info.frameCount = stream->nb_frames;
        else if (stream->duration > 0)
            info.frameCount = av_rescale_q_rnd(stream->duration, stream->time_base,
                                                AVRational{static_cast<int>(info.frameRate.denominator),
                                                           static_cast<int>(info.frameRate.numerator)},
                                                AV_ROUND_NEAR_INF);
    }
    if (!info.hasVideo && !info.hasAudio)
        throw std::runtime_error("This file has no supported audio or video streams");
    if (info.frameCount == 0 && info.hasVideo && info.durationUs > 0)
        info.frameCount = av_rescale_q_rnd(info.durationUs, AV_TIME_BASE_Q,
                                            AVRational{static_cast<int>(info.frameRate.denominator),
                                                       static_cast<int>(info.frameRate.numerator)},
                                            AV_ROUND_NEAR_INF);
    if (!info.hasVideo && info.hasAudio && info.audioSampleRate > 0) {
        info.frameRate = {info.audioSampleRate, 1};
        if (info.durationUs > 0)
            info.frameCount = av_rescale_q_rnd(info.durationUs, AV_TIME_BASE_Q,
                                                AVRational{1, info.audioSampleRate},
                                                AV_ROUND_NEAR_INF);
    }
    if (info.isStillImage && info.hasVideo) {
        info.frameCount = 1;
        info.durationUs = 0;
        info.frameRate = {1, 1};
    }
    return info;
}

} // namespace datacut
