#include "media/ThumbnailGenerator.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QStandardPaths>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <memory>

namespace datacut {

QString thumbnailCachePath(const QString &mediaPath)
{
    const QFileInfo info(mediaPath);
    const QByteArray identity = info.absoluteFilePath().toUtf8() + '|'
        + QByteArray::number(info.size()) + '|'
        + QByteArray::number(info.lastModified().toMSecsSinceEpoch());
    const auto key = QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex();
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .filePath(QStringLiteral("thumbnails/") + QString::fromLatin1(key) + QStringLiteral(".jpg"));
}

QString generateThumbnail(const QString &mediaPath)
{
    const QString outputPath = thumbnailCachePath(mediaPath);
    if (QFileInfo::exists(outputPath)) return outputPath;

    AVFormatContext *rawFormat = nullptr;
    const QByteArray pathBytes = mediaPath.toUtf8();
    if (avformat_open_input(&rawFormat, pathBytes.constData(), nullptr, nullptr) < 0)
        return {};
    const auto closeFormat = [](AVFormatContext *context) { avformat_close_input(&context); };
    const auto format = std::unique_ptr<AVFormatContext, decltype(closeFormat)>(rawFormat, closeFormat);
    if (avformat_find_stream_info(format.get(), nullptr) < 0) return {};

    const int streamIndex = av_find_best_stream(format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (streamIndex < 0) return {};
    AVStream *stream = format->streams[streamIndex];
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) return {};
    const auto closeCodec = [](AVCodecContext *codecContext) { avcodec_free_context(&codecContext); };
    const auto context = std::unique_ptr<AVCodecContext, decltype(closeCodec)>(
        avcodec_alloc_context3(codec), closeCodec);
    if (!context || avcodec_parameters_to_context(context.get(), stream->codecpar) < 0
        || avcodec_open2(context.get(), codec, nullptr) < 0) return {};

    if (stream->duration > 0)
        av_seek_frame(format.get(), streamIndex, stream->start_time + stream->duration / 8,
                      AVSEEK_FLAG_BACKWARD);

    const auto closePacket = [](AVPacket *value) { av_packet_free(&value); };
    const auto closeFrame = [](AVFrame *value) { av_frame_free(&value); };
    const auto packet = std::unique_ptr<AVPacket, decltype(closePacket)>(av_packet_alloc(), closePacket);
    const auto frame = std::unique_ptr<AVFrame, decltype(closeFrame)>(av_frame_alloc(), closeFrame);
    if (!packet || !frame) return {};

    bool decoded = false;
    for (int packetsRead = 0; packetsRead < 120 && av_read_frame(format.get(), packet.get()) >= 0;
         ++packetsRead) {
        if (packet->stream_index == streamIndex
            && avcodec_send_packet(context.get(), packet.get()) >= 0
            && avcodec_receive_frame(context.get(), frame.get()) >= 0) {
            decoded = true;
            av_packet_unref(packet.get());
            break;
        }
        av_packet_unref(packet.get());
    }
    if (!decoded || frame->width <= 0 || frame->height <= 0) return {};

    const int width = 320;
    const int height = std::clamp(frame->height * width / frame->width, 1, 320);
    QImage image(width, height, QImage::Format_RGB888);
    if (image.isNull()) return {};
    SwsContext *rawScale = sws_getContext(frame->width, frame->height,
        static_cast<AVPixelFormat>(frame->format), width, height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    const auto scale = std::unique_ptr<SwsContext, decltype(&sws_freeContext)>(rawScale, &sws_freeContext);
    if (!scale) return {};
    uint8_t *destination[] = {image.bits(), nullptr, nullptr, nullptr};
    int stride[] = {static_cast<int>(image.bytesPerLine()), 0, 0, 0};
    if (sws_scale(scale.get(), frame->data, frame->linesize, 0, frame->height,
                  destination, stride) <= 0) return {};
    QDir().mkpath(QFileInfo(outputPath).absolutePath());
    return image.save(outputPath, "JPEG", 84) ? outputPath : QString{};
}

QString transitionFrameCachePath(const QString &mediaPath, qint64 sourceFrame,
                                 FrameRate sourceRate)
{
    const QFileInfo info(mediaPath);
    const QByteArray identity = info.absoluteFilePath().toUtf8() + '|'
        + QByteArray::number(info.size()) + '|'
        + QByteArray::number(info.lastModified().toMSecsSinceEpoch()) + '|'
        + QByteArray::number(sourceFrame) + '|'
        + QByteArray::number(sourceRate.numerator) + '/'
        + QByteArray::number(sourceRate.denominator);
    const auto key = QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex();
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .filePath(QStringLiteral("transition-frames/") + QString::fromLatin1(key)
                  + QStringLiteral(".jpg"));
}

QString generateTransitionFrame(const QString &mediaPath, qint64 sourceFrame,
                                FrameRate sourceRate)
{
    if (sourceFrame < 0 || !sourceRate.valid()) return {};
    const QString outputPath = transitionFrameCachePath(mediaPath, sourceFrame, sourceRate);
    if (QFileInfo::exists(outputPath)) return outputPath;
    AVFormatContext *rawFormat = nullptr;
    const QByteArray pathBytes = mediaPath.toUtf8();
    if (avformat_open_input(&rawFormat, pathBytes.constData(), nullptr, nullptr) < 0)
        return {};
    const auto closeFormat = [](AVFormatContext *context) { avformat_close_input(&context); };
    const auto format = std::unique_ptr<AVFormatContext, decltype(closeFormat)>(rawFormat, closeFormat);
    if (avformat_find_stream_info(format.get(), nullptr) < 0) return {};
    const int streamIndex = av_find_best_stream(format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (streamIndex < 0) return {};
    AVStream *stream = format->streams[streamIndex];
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) return {};
    const auto closeCodec = [](AVCodecContext *context) { avcodec_free_context(&context); };
    const auto context = std::unique_ptr<AVCodecContext, decltype(closeCodec)>(
        avcodec_alloc_context3(codec), closeCodec);
    if (!context || avcodec_parameters_to_context(context.get(), stream->codecpar) < 0
        || avcodec_open2(context.get(), codec, nullptr) < 0) return {};

    const AVRational frameBase{static_cast<int>(sourceRate.denominator),
                               static_cast<int>(sourceRate.numerator)};
    const int64_t firstTimestamp = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
    const int64_t targetTimestamp = firstTimestamp
        + av_rescale_q(sourceFrame, frameBase, stream->time_base);
    if (av_seek_frame(format.get(), streamIndex, targetTimestamp, AVSEEK_FLAG_BACKWARD) >= 0)
        avcodec_flush_buffers(context.get());

    const auto closePacket = [](AVPacket *packet) { av_packet_free(&packet); };
    const auto closeFrame = [](AVFrame *frame) { av_frame_free(&frame); };
    const auto packet = std::unique_ptr<AVPacket, decltype(closePacket)>(av_packet_alloc(), closePacket);
    const auto decoded = std::unique_ptr<AVFrame, decltype(closeFrame)>(av_frame_alloc(), closeFrame);
    const auto chosen = std::unique_ptr<AVFrame, decltype(closeFrame)>(av_frame_alloc(), closeFrame);
    if (!packet || !decoded || !chosen) return {};
    bool found = false;
    for (int packetsRead = 0; packetsRead < 2400 && !found
         && av_read_frame(format.get(), packet.get()) >= 0; ++packetsRead) {
        if (packet->stream_index == streamIndex
            && avcodec_send_packet(context.get(), packet.get()) >= 0) {
            while (avcodec_receive_frame(context.get(), decoded.get()) >= 0) {
                const int64_t timestamp = decoded->best_effort_timestamp;
                av_frame_unref(chosen.get());
                if (av_frame_ref(chosen.get(), decoded.get()) < 0) return {};
                found = timestamp == AV_NOPTS_VALUE || timestamp >= targetTimestamp;
                av_frame_unref(decoded.get());
                if (found) break;
            }
        }
        av_packet_unref(packet.get());
    }
    if (chosen->width <= 0 || chosen->height <= 0) return {};
    const int width = std::min(chosen->width, 960);
    const int height = std::max(1, chosen->height * width / chosen->width);
    QImage image(width, height, QImage::Format_RGB888);
    if (image.isNull()) return {};
    const auto scale = std::unique_ptr<SwsContext, decltype(&sws_freeContext)>(
        sws_getContext(chosen->width, chosen->height,
                       static_cast<AVPixelFormat>(chosen->format), width, height,
                       AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr),
        &sws_freeContext);
    if (!scale) return {};
    uint8_t *destination[] = {image.bits(), nullptr, nullptr, nullptr};
    int stride[] = {static_cast<int>(image.bytesPerLine()), 0, 0, 0};
    if (sws_scale(scale.get(), chosen->data, chosen->linesize, 0,
                  chosen->height, destination, stride) <= 0) return {};
    QDir().mkpath(QFileInfo(outputPath).absolutePath());
    return image.save(outputPath, "JPEG", 92) ? outputPath : QString{};
}

} // namespace datacut
