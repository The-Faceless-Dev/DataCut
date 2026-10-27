#include "app/EditorController.h"

#include "core/FrameTime.h"
#include "media/ThumbnailGenerator.h"
#include "media/WaveformGenerator.h"
#include "media/FilmstripGenerator.h"
#include "effects/EffectRegistry.h"
#include "media/ProxyCache.h"

#include <QDir>
#include <QCoreApplication>
#include <QColor>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QHash>
#include <QFutureWatcher>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QPainter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTextLayout>
#include <QTextOption>
#include <QTimer>
#include <QUuid>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <functional>
#include <limits>

namespace datacut {

namespace {
QString freshId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

int spatialMaskKind(const QString &renderer)
{
    static const QStringList names{
        QStringLiteral("wipe_right"), QStringLiteral("wipe_left"),
        QStringLiteral("wipe_down"), QStringLiteral("wipe_up"),
        QStringLiteral("diagonal_wipe"), QStringLiteral("split_reveal"),
        QStringLiteral("circle_iris"), QStringLiteral("diamond_iris"),
        QStringLiteral("venetian_blinds"), QStringLiteral("checker_reveal"),
        QStringLiteral("liquid_wave"), QStringLiteral("ripple_rings"),
        QStringLiteral("prism_sweep"), QStringLiteral("shattered_glass"),
        QStringLiteral("hex_mosaic"), QStringLiteral("radial_burst"),
        QStringLiteral("pixel_storm"), QStringLiteral("ribbon_twist"),
        QStringLiteral("ink_bloom"), QStringLiteral("crosshatch")};
    return names.indexOf(renderer) + 1;
}

QString spatialMaskFilter(int kind, int softness, int complexity,
                          const QString &progress, bool reveal,
                          bool colorEnabled, int colorRgb,
                          const QString &condition = {})
{
    if (kind <= 0) return {};
    const QString u = QStringLiteral("((X+0.5)/W)");
    const QString v = QStringLiteral("((Y+0.5)/H)");
    const QString count = QString::number(std::clamp(complexity, 2, 16));
    QString coordinate;
    switch (kind) {
    case 1: coordinate = u; break;
    case 2: coordinate = QStringLiteral("(1-%1)").arg(u); break;
    case 3: coordinate = v; break;
    case 4: coordinate = QStringLiteral("(1-%1)").arg(v); break;
    case 5: coordinate = QStringLiteral("((%1+%2)/2)").arg(u, v); break;
    case 6: coordinate = QStringLiteral("(2*abs(%1-0.5))").arg(u); break;
    case 7: coordinate = QStringLiteral("(sqrt((%1-0.5)^2+(%2-0.5)^2)/0.70710678)").arg(u, v); break;
    case 8: coordinate = QStringLiteral("(abs(%1-0.5)+abs(%2-0.5))").arg(u, v); break;
    case 9: coordinate = QStringLiteral("mod(%1*%2,1)").arg(u, count); break;
    case 10: coordinate = QStringLiteral("(mod(floor(%1*%3)+floor(%2*%3),2)*0.5+mod(%1*%3,1)*0.5)")
        .arg(u, v, count); break;
    case 11: coordinate = QStringLiteral("(%1+0.16*sin(%2*18.849556+%1*6.283185))").arg(u, v); break;
    case 12: coordinate = QStringLiteral("mod(sqrt((%1-0.5)^2+(%2-0.5)^2)*%3,1)").arg(u, v, count); break;
    case 13: coordinate = QStringLiteral("(%1+0.13*sin(%2*%3*3.141593))").arg(u, v, count); break;
    case 14: coordinate = QStringLiteral("(mod(floor(%1*%3)+floor(%2*%3)*3,5)/5"
        "+mod((%1+%2)*%3,1)/5)").arg(u, v, count); break;
    case 15: coordinate = QStringLiteral("(mod(floor(%1*%3)+floor(%2*%3/1.732)*2,7)/7"
        "+mod(%1*%3+mod(floor(%2*%3/1.732),2)*0.5,1)/7)").arg(u, v, count); break;
    case 16: coordinate = QStringLiteral("(abs(%1-0.5)+abs(%2-0.5)"
        "+0.12*sin((%1-%2)*%3*3.141593))").arg(u, v, count); break;
    case 17: coordinate = QStringLiteral("(mod(floor(%1*%3)*7+floor(%2*%3)*11,13)/13"
        "+mod(%1*%3,1)/13)").arg(u, v, count); break;
    case 18: coordinate = QStringLiteral("(mod(%2*%3,1)*0.55+%1*0.45"
        "+0.08*sin(%2*%3*6.283185))").arg(u, v, count); break;
    case 19: coordinate = QStringLiteral("(sqrt((%1-0.5)^2+(%2-0.5)^2)/0.70710678"
        "+0.13*sin(%1*%3*3.141593)*sin(%2*%3*3.141593))").arg(u, v, count); break;
    case 20: coordinate = QStringLiteral("min(mod(%1*%3,1),mod(%2*%3,1))").arg(u, v, count); break;
    default: return {};
    }
    const QString width = QString::number(std::max(0.000001, softness / 100.0), 'f', 6);
    // Procedural coordinates can overshoot at the canvas edges. Bound them so
    // every pixel is fully revealed by the end of a transition.
    const QString normalized = QStringLiteral("max(0,min(1,%1))").arg(coordinate);
    const QString boundedDelta = reveal ? QStringLiteral("(%1)-(%2)").arg(progress, normalized)
                                        : QStringLiteral("(%1)-(%2)").arg(normalized, progress);
    QString mask = QStringLiteral("max(0,min(1,(%1)/%2+0.5))").arg(boundedDelta, width);
    if (!condition.isEmpty())
        mask = QStringLiteral("if(%1,%2,1)").arg(condition, mask);
    if (kind < 11 || !colorEnabled)
        return QStringLiteral("format=gbrap,geq=r='r(X,Y)':g='g(X,Y)':b='b(X,Y)':"
                              "a='alpha(X,Y)*%1',format=rgba").arg(mask);
    const QString phase = QStringLiteral("sin(PI*max(0,min(1,%1)))").arg(progress);
    const QString edge = QStringLiteral("max(0,1-abs((%1)-(%2))/0.075)*(%3)")
        .arg(normalized, progress, phase);
    const auto lit = [&](QChar channel, double accent) {
        return QStringLiteral("clip(%1(X,Y)+255*(%2)*%3,0,255)")
            .arg(QString(channel), edge, QString::number(accent * 0.85, 'f', 4));
    };
    const double red = ((colorRgb >> 16) & 0xFF) / 255.0;
    const double green = ((colorRgb >> 8) & 0xFF) / 255.0;
    const double blue = (colorRgb & 0xFF) / 255.0;
    return QStringLiteral("format=gbrap,geq=r='%1':g='%2':b='%3':"
                          "a='alpha(X,Y)*%4',format=rgba")
        .arg(lit('r', red), lit('g', green), lit('b', blue), mask);
}

QString spatialProgress(const QString &linear, int speed, int softness)
{
    const double margin = std::max(0.000001, softness / 100.0) / 2.0;
    return QStringLiteral("(-%1+(1+%2)*pow(max(0,min(1,%3)),%4))")
        .arg(QString::number(margin, 'f', 6),
             QString::number(margin * 2.0, 'f', 6), linear,
             QString::number(100.0 / std::clamp(speed, 25, 200), 'f', 6));
}
struct CaptionStyleDefinition {
    const char *id;
    const char *name;
    const char *category;
    const char *description;
    const char *treatment;
};

constexpr CaptionStyleDefinition captionStyles[] = {
    {"sentence", "Full sentence", "Basic", "Keep the full sentence visible", "plain"},
    {"sentence_clear", "Clean sentence", "Basic", "Sentence without a background", "clear"},
    {"sentence_card", "Caption card", "Frame", "Sentence on a solid card", "card"},
    {"word", "One word", "Word", "Show only the spoken word", "plain"},
    {"word_accent", "Accent word", "Word", "Spoken word in the accent color", "accent"},
    {"word_pair", "Word pair", "Word", "Current word with the next word", "plain"},
    {"word_prev", "Trailing pair", "Word", "Previous word with the current word", "plain"},
    {"word_next", "Lead in", "Word", "Current word with the upcoming word emphasized", "lead"},
    {"phrase2", "Two word beats", "Phrase", "Reveal two words at a time", "plain"},
    {"phrase3", "Three word beats", "Phrase", "Reveal three words at a time", "plain"},
    {"rolling3", "Rolling trio", "Phrase", "Three word window follows speech", "plain"},
    {"rolling5", "Rolling line", "Phrase", "Five word window follows speech", "plain"},
    {"build", "Build up", "Reveal", "Accumulate words as they are spoken", "plain"},
    {"build_accent", "Build and highlight", "Reveal", "Build the sentence with the active word accented", "color"},
    {"karaoke", "Spoken highlight", "Highlight", "Highlight the active word in the sentence", "color"},
    {"karaoke_box", "Highlight block", "Highlight", "Put a colored block behind the active word", "block"},
    {"karaoke_underline", "Underline", "Highlight", "Underline the active word", "underline"},
    {"karaoke_next", "Upcoming word", "Highlight", "Highlight the word that is coming next", "lead"},
    {"fade_sentence", "Sentence fade", "Motion", "Fade the sentence in over six frames", "plain"},
    {"fade_word", "Word fade", "Motion", "Fade each spoken word in over six frames", "plain"},
    {"typewriter", "Typewriter", "Motion", "Reveal the sentence character by character", "plain"},
    {"typewriter_word", "Word typewriter", "Motion", "Reveal each spoken word character by character", "plain"},
};

const CaptionStyleDefinition *captionStyle(const QString &id)
{
    for (const auto &style : captionStyles)
        if (id == QLatin1String(style.id)) return &style;
    return nullptr;
}

struct CaptionFrame {
    QStringList words;
    int highlight = -1;
    QString treatment = QStringLiteral("plain");
    int opacity = 100;
};

CaptionFrame captionFrame(const CaptionRow &row, const CaptionCue &cue,
                          qint64 frame, const QString &overrideMode = {})
{
    if (cue.words.isEmpty() || frame < cue.words.first().firstFrame
        || frame >= cue.words.last().endFrame) return {};
    const QString mode = overrideMode.isEmpty() ? row.displayMode : overrideMode;
    const auto *style = captionStyle(mode);
    CaptionFrame result;
    result.treatment = style ? QLatin1String(style->treatment) : QStringLiteral("plain");
    int active = -1;
    int lastStarted = -1;
    for (int i = 0; i < cue.words.size(); ++i)
    {
        if (frame >= cue.words[i].firstFrame) lastStarted = i;
        if (frame >= cue.words[i].firstFrame && frame < cue.words[i].endFrame) active = i;
    }
    if (active < 0 && mode != QStringLiteral("sentence")
        && mode != QStringLiteral("sentence_clear")
        && mode != QStringLiteral("sentence_card")
        && mode != QStringLiteral("fade_sentence")
        && mode != QStringLiteral("typewriter")) {
        if (mode.startsWith(QStringLiteral("karaoke"))) {
            // The sentence remains on screen through short pauses between words.
        } else if (mode == QStringLiteral("build") || mode == QStringLiteral("build_accent")) {
            active = lastStarted;
        } else return {};
    }
    int first = 0;
    int last = cue.words.size();
    if (mode == QStringLiteral("word") || mode == QStringLiteral("word_accent")
        || mode == QStringLiteral("fade_word") || mode == QStringLiteral("typewriter_word")) {
        first = active; last = active + 1;
        if (mode == QStringLiteral("word_accent")) result.highlight = 0;
    } else if (mode == QStringLiteral("word_pair") || mode == QStringLiteral("word_next")) {
        first = active; last = std::min(active + 2, last);
        if (mode == QStringLiteral("word_next")) result.highlight = last - first - 1;
    } else if (mode == QStringLiteral("word_prev")) {
        first = std::max(0, active - 1); last = active + 1;
    } else if (mode == QStringLiteral("phrase2") || mode == QStringLiteral("phrase3")) {
        const int size = mode == QStringLiteral("phrase2") ? 2 : 3;
        first = (active / size) * size; last = std::min(first + size, last);
    } else if (mode == QStringLiteral("rolling3") || mode == QStringLiteral("rolling5")) {
        const int size = mode == QStringLiteral("rolling3") ? 3 : 5;
        first = std::clamp(active - size / 2, 0, std::max(0, last - size));
        last = std::min(first + size, last);
    } else if (mode == QStringLiteral("build") || mode == QStringLiteral("build_accent")) {
        last = active + 1;
        if (mode == QStringLiteral("build_accent")) result.highlight = active;
    } else if (mode == QStringLiteral("karaoke_next")) {
        result.highlight = active >= 0 ? std::min(active + 1, last - 1) : -1;
    } else if (mode.startsWith(QStringLiteral("karaoke"))) {
        result.highlight = active;
    }
    for (int i = first; i < last; ++i) result.words.append(cue.words[i].text);
    if (mode == QStringLiteral("fade_sentence") || mode == QStringLiteral("fade_word")) {
        const qint64 start = mode == QStringLiteral("fade_word")
            ? cue.words[active].firstFrame : cue.words.first().firstFrame;
        result.opacity = static_cast<int>(std::clamp<qint64>((frame - start + 1) * 100 / 6, 0, 100));
    } else if (mode == QStringLiteral("typewriter") || mode == QStringLiteral("typewriter_word")) {
        const qint64 start = mode == QStringLiteral("typewriter_word")
            ? cue.words[active].firstFrame : cue.words.first().firstFrame;
        const QString full = result.words.join(QLatin1Char(' '));
        const int phase = static_cast<int>(std::clamp<qint64>(frame - start + 1, 1, 12));
        const int chars = static_cast<int>(std::max<qsizetype>(1,
            (full.size() * phase + 11) / 12));
        result.words = {full.left(chars)};
    }
    return result;
}

void resegmentCaptionRow(CaptionRow &row)
{
    QVector<CaptionCue> rebuilt;
    rebuilt.reserve(row.cues.size());
    for (qsizetype firstCue = 0; firstCue < row.cues.size();) {
        const QString groupId = row.cues[firstCue].groupId.isEmpty()
            ? row.cues[firstCue].id : row.cues[firstCue].groupId;
        QVector<CaptionWord> words;
        QHash<QString, QString> existingIds;
        qsizetype nextCue = firstCue;
        while (nextCue < row.cues.size()) {
            const auto &cue = row.cues[nextCue];
            const QString currentGroup = cue.groupId.isEmpty() ? cue.id : cue.groupId;
            if (currentGroup != groupId) break;
            if (!cue.words.isEmpty()) existingIds.insert(cue.words.first().id, cue.id);
            words += cue.words;
            ++nextCue;
        }
        const qsizetype limit = row.maxWordsPerCue < 1 ? words.size() : row.maxWordsPerCue;
        for (qsizetype offset = 0; offset < words.size(); offset += limit) {
            CaptionCue fragment;
            fragment.id = existingIds.value(words[offset].id);
            if (fragment.id.isEmpty()) fragment.id = freshId();
            fragment.groupId = groupId;
            fragment.words = words.mid(offset, limit);
            rebuilt.append(std::move(fragment));
        }
        firstCue = nextCue;
    }
    row.cues = std::move(rebuilt);
}
struct VisualEdge {
    QString id;
    int track = -1;
    qint64 start = 0;
    qint64 duration = 0;
};

QVector<VisualEdge> visualEdges(const QVector<Clip> &clips,
                                const QVector<TitleClip> &titles)
{
    QVector<VisualEdge> result;
    result.reserve(clips.size() + titles.size());
    for (const auto &clip : clips)
        if (clip.includeVideo)
            result.append({clip.id, clip.track, clip.startFrame, clip.durationFrames});
    for (const auto &title : titles)
        result.append({title.id, title.track, title.startFrame, title.durationFrames});
    return result;
}

const VisualEdge *findVisualEdge(const QVector<VisualEdge> &items, const QString &id)
{
    const auto it = std::find_if(items.cbegin(), items.cend(),
        [&](const VisualEdge &item) { return item.id == id; });
    return it == items.cend() ? nullptr : &*it;
}
constexpr qint64 maximumRangeFrames = 2147483647;

bool mp3CoverArtWasClassifiedAsVideo(const MediaInfo &media)
{
    return media.hasVideo && media.hasAudio && media.audioSampleRate > 0
        && media.durationUs > 0
        && QFileInfo(media.path).suffix().compare(QStringLiteral("mp3"), Qt::CaseInsensitive) == 0;
}

bool normalizeMp3CoverArt(MediaInfo &media)
{
    if (!mp3CoverArtWasClassifiedAsVideo(media)) return false;
    media.hasVideo = false;
    media.width = 0;
    media.height = 0;
    media.frameRate = {media.audioSampleRate, 1};
    media.frameCount = rescaleFrames(media.durationUs, FrameRate{1000000, 1}, media.frameRate);
    return true;
}

struct RangeIncrement {
    qint64 step = 1;
    qint64 offset = 0;
};

std::optional<RangeIncrement> parseRangeIncrement(const QString &rule)
{
    static const QRegularExpression plain(QStringLiteral("^([1-9][0-9]*)$"));
    static const QRegularExpression formula(QStringLiteral("^([1-9][0-9]*)?n(?:([+-])([0-9]+))?$"));
    const QString compact = rule.trimmed().remove(QLatin1Char(' ')).toLower();
    const auto plainMatch = plain.match(compact);
    if (plainMatch.hasMatch()) {
        bool valid = false;
        const qint64 step = plainMatch.captured(1).toLongLong(&valid);
        if (valid && step <= maximumRangeFrames) return RangeIncrement{step, 0};
        return std::nullopt;
    }
    const auto match = formula.match(compact);
    if (!match.hasMatch()) return std::nullopt;
    bool validStep = true;
    bool validOffset = true;
    const qint64 step = match.captured(1).isEmpty() ? 1 : match.captured(1).toLongLong(&validStep);
    qint64 offset = match.captured(3).isEmpty() ? 0 : match.captured(3).toLongLong(&validOffset);
    if (match.captured(2) == QStringLiteral("-")) offset = -offset;
    if (!validStep || !validOffset || step < 1 || step > maximumRangeFrames
        || offset < -maximumRangeFrames || offset > maximumRangeFrames) return std::nullopt;
    return RangeIncrement{step, offset};
}

QString normalizedRangeIncrement(const QString &rule)
{
    return rule.trimmed().remove(QLatin1Char(' ')).toLower();
}

qint64 firstRangeSize(RangeIncrement rule)
{
    if (rule.offset > 0) return rule.offset;
    return rule.offset + ((-rule.offset) / rule.step + 1) * rule.step;
}

qint64 nearestRangeSize(qint64 size, RangeIncrement rule)
{
    const qint64 first = firstRangeSize(rule);
    if (size <= first) return first;
    const qint64 lower = first + (size - first) / rule.step * rule.step;
    const qint64 upper = lower + rule.step;
    return upper <= maximumRangeFrames && upper - size <= size - lower ? upper : lower;
}

QSettings rangeSettings()
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(directory);
    return QSettings(directory + QStringLiteral("/settings.ini"), QSettings::IniFormat);
}
QString rateText(FrameRate rate)
{
    return QString::number(rate.numerator) + QLatin1Char('/') + QString::number(rate.denominator);
}
int valueAt(const QVector<ValueKeyframe> &points, qint64 frame, int baseline)
{
    if (points.isEmpty()) return baseline;
    if (frame <= points.first().frame) {
        if (points.first().frame == 0) return points.first().value;
        return baseline + static_cast<int>(std::llround(
            (points.first().value - baseline) * static_cast<double>(frame)
            / points.first().frame));
    }
    for (qsizetype i = 1; i < points.size(); ++i) {
        if (frame <= points[i].frame) {
            const auto &a = points[i - 1];
            const auto &b = points[i];
            return a.value + static_cast<int>(std::llround(
                (b.value - a.value) * static_cast<double>(frame - a.frame)
                / (b.frame - a.frame)));
        }
    }
    return points.last().value;
}

QJsonArray keyframesJson(const QVector<ValueKeyframe> &points)
{
    QJsonArray result;
    for (const auto &point : points)
        result.append(QJsonObject{{"frame", QString::number(point.frame)}, {"value", point.value}});
    return result;
}

void upsertKeyframe(QVector<ValueKeyframe> &points, qint64 frame, int value)
{
    const auto it = std::lower_bound(points.begin(), points.end(), frame,
        [](const ValueKeyframe &point, qint64 target) { return point.frame < target; });
    if (it != points.end() && it->frame == frame) it->value = value;
    else points.insert(it, {frame, value});
}

void eraseKeyframe(QVector<ValueKeyframe> &points, qint64 frame)
{
    const auto it = std::lower_bound(points.begin(), points.end(), frame,
        [](const ValueKeyframe &point, qint64 target) { return point.frame < target; });
    if (it != points.end() && it->frame == frame) points.erase(it);
}

struct VisualPropertyChannel {
    int *baseline = nullptr;
    QVector<ValueKeyframe> *points = nullptr;
    int minimum = 0;
    int maximum = 0;
};

VisualPropertyChannel propertyChannel(Clip &clip, const QString &property)
{
    if (property == QStringLiteral("x")) return {&clip.positionX, &clip.positionXKeyframes, -4000, 4000};
    if (property == QStringLiteral("y")) return {&clip.positionY, &clip.positionYKeyframes, -4000, 4000};
    if (property == QStringLiteral("scale")) return {&clip.scalePercent, &clip.scaleKeyframes, 25, 400};
    if (property == QStringLiteral("rotation")) return {&clip.rotationDegrees, &clip.rotationKeyframes, -180, 180};
    if (property == QStringLiteral("opacity")) return {&clip.opacityPercent, &clip.opacityKeyframes, 0, 100};
    if (property == QStringLiteral("gain") && clip.includeAudio)
        return {&clip.gainPercent, &clip.gainKeyframes, 0, 300};
    return {};
}

VisualPropertyChannel propertyChannel(TitleClip &title, const QString &property)
{
    if (property == QStringLiteral("x")) return {&title.positionX, &title.positionXKeyframes, -200, 300};
    if (property == QStringLiteral("y")) return {&title.positionY, &title.positionYKeyframes, -200, 300};
    if (property == QStringLiteral("scale")) return {&title.scalePercent, &title.scaleKeyframes, 25, 400};
    return {};
}

VisualPropertyChannel propertyChannel(CaptionRow &row, const QString &property)
{
    if (property == QStringLiteral("x")) return {&row.positionX, &row.positionXKeyframes, -200, 300};
    if (property == QStringLiteral("y")) return {&row.positionY, &row.positionYKeyframes, -200, 300};
    if (property == QStringLiteral("scale")) return {&row.scalePercent, &row.scaleKeyframes, 25, 400};
    return {};
}

bool changeVisualChannel(VisualPropertyChannel channel, qint64 frame,
                         qint64 destination, int value, int operation)
{
    if (!channel.baseline || !channel.points) return false;
    auto &points = *channel.points;
    auto point = std::lower_bound(points.begin(), points.end(), frame,
        [](const ValueKeyframe &entry, qint64 target) { return entry.frame < target; });
    const bool exists = point != points.end() && point->frame == frame;
    if (operation == 0) {
        if (value < channel.minimum || value > channel.maximum) return false;
        if (exists) {
            if (point->value == value) return false;
            point->value = value;
        } else {
            if (*channel.baseline == value) return false;
            *channel.baseline = value;
        }
    } else if (operation == 1) {
        if (exists) return false;
        points.insert(point, {frame, valueAt(points, frame, *channel.baseline)});
    } else if (operation == 2) {
        if (!exists) return false;
        points.erase(point);
    } else if (operation == 3) {
        if (!exists || destination == frame
            || std::any_of(points.cbegin(), points.cend(), [destination](const ValueKeyframe &entry) {
                return entry.frame == destination;
            })) return false;
        const int movedValue = point->value;
        points.erase(point);
        upsertKeyframe(points, destination, movedValue);
    } else return false;
    return true;
}

bool readKeyframes(const QJsonValue &json, qint64 maximumFrame, int maximumValue,
                   QVector<ValueKeyframe> &result, int minimumValue = 0)
{
    if (!json.isUndefined() && !json.isArray()) return false;
    qint64 previous = -1;
    for (const auto &entry : json.toArray()) {
        const auto object = entry.toObject();
        const qint64 frame = object.value("frame").toString().toLongLong();
        const int value = object.value("value").toInt(-1);
        if (frame <= previous || frame < 0 || frame > maximumFrame
            || value < minimumValue || value > maximumValue) return false;
        result.append({frame, value});
        previous = frame;
    }
    return true;
}

void rescaleKeyframeList(QVector<ValueKeyframe> &points, FrameRate before,
                         FrameRate after, qint64 maximum)
{
    for (auto &point : points)
        point.frame = std::clamp<qint64>(rescaleFrames(point.frame, before, after), 0, maximum);
    points.erase(std::unique(points.begin(), points.end(),
        [](const ValueKeyframe &a, const ValueKeyframe &b) { return a.frame == b.frame; }),
        points.end());
}

QString decimal(double value) { return QString::number(value, 'f', 9); }

// The expression is evaluated in the clip's output timebase. Commas are escaped
// for FFmpeg's filtergraph parser, so keyframes remain sample/frame accurate.
QString envelopeExpression(const QVector<ValueKeyframe> &points, int baseline,
                            qint64 duration, FrameRate rate, double scale, double offset = 0,
                            const QString &timeSymbol = QStringLiteral("t"))
{
    QVector<ValueKeyframe> knots{{0, valueAt(points, 0, baseline)}};
    for (const auto &point : points)
        if (point.frame > 0 && point.frame < duration) knots.append(point);
    if (duration > 0) knots.append({duration, valueAt(points, duration, baseline)});
    if (knots.size() == 1) return decimal(offset + knots.first().value * scale);
    QString expression = decimal(offset + knots.last().value * scale);
    for (qsizetype i = knots.size() - 1; i > 0; --i) {
        const auto &a = knots[i - 1];
        const auto &b = knots[i];
        const double firstTime = static_cast<double>(a.frame) * rate.denominator / rate.numerator;
        const double lastTime = static_cast<double>(b.frame) * rate.denominator / rate.numerator;
        const double firstValue = offset + a.value * scale;
        const double slope = (b.value - a.value) * scale / (lastTime - firstTime);
        const QString segment = QStringLiteral("(%1+(%2-%3)*%4)")
            .arg(decimal(firstValue), timeSymbol, decimal(firstTime), decimal(slope));
        expression = QStringLiteral("if(lt(%1\\,%2)\\,%3\\,%4)")
            .arg(timeSymbol, decimal(lastTime), segment, expression);
    }
    return expression;
}

// Preview samples transform keyframes as integer values on each project frame.
// Keep export on that same grid before converting percent/position to pixels.
QString visualEnvelopeExpression(const QVector<ValueKeyframe> &points, int baseline,
                                 qint64 duration, FrameRate rate, double scale,
                                 const QString &timeSymbol)
{
    const QString value = envelopeExpression(points, baseline, duration, rate,
                                             1, 0, timeSymbol);
    return QStringLiteral("(round(%1)*%2)").arg(value, decimal(scale));
}

QString gainFilter(const Clip &clip, FrameRate rate)
{
    if (clip.gainKeyframes.isEmpty())
        return QStringLiteral("volume=%1").arg(decimal(clip.gainPercent / 100.0));
    return QStringLiteral("volume='%1':eval=frame")
        .arg(envelopeExpression(clip.gainKeyframes, clip.gainPercent,
                                clip.durationFrames, rate, 0.01));
}

bool keyframeableEffect(const QString &id)
{
    return id == QStringLiteral("datacut.video.grayscale")
        || id == QStringLiteral("datacut.video.saturation")
        || id == QStringLiteral("datacut.video.brightness")
        || id == QStringLiteral("datacut.video.vignette")
        || id == QStringLiteral("datacut.video.blur");
}

QString blurMixExpression(const TrackEffect &effect, const Clip &clip,
                          FrameRate rate, bool timelineTime)
{
    QVector<ValueKeyframe> local{{0, valueAt(effect.keyframes, clip.startFrame,
                                             effect.strength)}};
    for (const auto &point : effect.keyframes)
        if (point.frame > clip.startFrame
            && point.frame < clip.startFrame + clip.durationFrames)
            local.append({point.frame - clip.startFrame, point.value});
    local.append({clip.durationFrames,
        valueAt(effect.keyframes, clip.startFrame + clip.durationFrames, effect.strength)});
    const QString timeSymbol = timelineTime
        ? QStringLiteral("(T-%1)").arg(decimal(static_cast<double>(clip.startFrame)
            * rate.denominator / rate.numerator)) : QStringLiteral("T");
    return envelopeExpression(local, effect.strength, clip.durationFrames,
                              rate, 0.01, 0, timeSymbol);
}

QString trackEffectFilter(const TrackEffect &effect, const Clip &clip, FrameRate rate)
{
    if (const auto *definition = videoEffectDefinition(effect.id);
        definition && !definition->parameters.isEmpty()) {
        QMap<QString, QString> values;
        for (const auto &parameter : effect.parameters) {
            if (parameter.keyframes.isEmpty()) {
                values.insert(parameter.id, QString::number(parameter.value));
                continue;
            }
            QVector<ValueKeyframe> local{{0, valueAt(parameter.keyframes, clip.startFrame,
                                                    parameter.value)}};
            for (const auto &point : parameter.keyframes)
                if (point.frame > clip.startFrame
                    && point.frame < clip.startFrame + clip.durationFrames)
                    local.append({point.frame - clip.startFrame, point.value});
            local.append({clip.durationFrames,
                valueAt(parameter.keyframes, clip.startFrame + clip.durationFrames,
                        parameter.value)});
            values.insert(parameter.id, envelopeExpression(local, parameter.value,
                clip.durationFrames, rate, 1.0, 0, definition->expressionTimeSymbol));
        }
        return parameterizedEffectFilter(effect.id, values);
    }
    if (effect.keyframes.isEmpty()) return effectFilter(effect.id, effect.strength);
    QVector<ValueKeyframe> local;
    local.append({0, valueAt(effect.keyframes, clip.startFrame, effect.strength)});
    for (const auto &point : effect.keyframes)
        if (point.frame > clip.startFrame
            && point.frame < clip.startFrame + clip.durationFrames)
            local.append({point.frame - clip.startFrame, point.value});
    local.append({clip.durationFrames,
        valueAt(effect.keyframes, clip.startFrame + clip.durationFrames, effect.strength)});
    QString expression;
    if (effect.id == QStringLiteral("datacut.video.grayscale"))
        expression = envelopeExpression(local, effect.strength, clip.durationFrames,
                                        rate, -0.01, 1.0);
    else if (effect.id == QStringLiteral("datacut.video.saturation"))
        expression = envelopeExpression(local, effect.strength, clip.durationFrames,
                                        rate, 0.02, 1.0);
    else if (effect.id == QStringLiteral("datacut.video.brightness"))
        expression = envelopeExpression(local, effect.strength, clip.durationFrames,
                                        rate, 1.0 / 300.0);
    else if (effect.id == QStringLiteral("datacut.video.vignette"))
        expression = envelopeExpression(local, effect.strength, clip.durationFrames,
                                        rate, 0.00499, 0.001);
    else return effectFilter(effect.id, valueAt(effect.keyframes, clip.startFrame, effect.strength));
    if (effect.id == QStringLiteral("datacut.video.vignette"))
        return QStringLiteral("vignette=angle='%1':eval=frame").arg(expression);
    return QStringLiteral("eq=%1='%2':eval=frame")
        .arg(effect.id == QStringLiteral("datacut.video.brightness")
                 ? QStringLiteral("brightness") : QStringLiteral("saturation"), expression);
}

void trimGainStart(Clip &changed, const Clip &original)
{
    if (original.gainKeyframes.isEmpty()) return;
    const qint64 removed = changed.startFrame - original.startFrame;
    QVector<ValueKeyframe> points;
    if (removed > 0) points.append({0, valueAt(original.gainKeyframes, removed,
                                               original.gainPercent)});
    for (const auto &point : original.gainKeyframes)
        if (point.frame >= removed && point.frame - removed <= changed.durationFrames) {
            const qint64 frame = point.frame - removed;
            if (!points.isEmpty() && points.last().frame == frame) points.last().value = point.value;
            else points.append({frame, point.value});
        }
    changed.gainKeyframes = points;
}

void trimGainEnd(Clip &changed, const Clip &original)
{
    if (original.gainKeyframes.isEmpty()) return;
    changed.gainKeyframes.erase(std::remove_if(changed.gainKeyframes.begin(),
        changed.gainKeyframes.end(), [&](const ValueKeyframe &point) {
            return point.frame > changed.durationFrames;
        }), changed.gainKeyframes.end());
    const int ending = valueAt(original.gainKeyframes, changed.durationFrames,
                               original.gainPercent);
    if (changed.gainKeyframes.isEmpty()
        || changed.gainKeyframes.last().frame < changed.durationFrames)
        changed.gainKeyframes.append({changed.durationFrames, ending});
}

void trimVisualStart(Clip &changed, const Clip &original)
{
    const qint64 removed = changed.startFrame - original.startFrame;
    const auto trim = [removed, &changed](const QVector<ValueKeyframe> &source,
                                          int baseline, QVector<ValueKeyframe> &target) {
        if (source.isEmpty()) return;
        target.clear();
        target.append({0, valueAt(source, removed, baseline)});
        for (const auto &point : source)
            if (point.frame > removed && point.frame - removed < changed.durationFrames)
                target.append({point.frame - removed, point.value});
    };
    trim(original.positionXKeyframes, original.positionX, changed.positionXKeyframes);
    trim(original.positionYKeyframes, original.positionY, changed.positionYKeyframes);
    trim(original.scaleKeyframes, original.scalePercent, changed.scaleKeyframes);
    trim(original.rotationKeyframes, original.rotationDegrees, changed.rotationKeyframes);
    trim(original.opacityKeyframes, original.opacityPercent, changed.opacityKeyframes);
}

void trimVisualEnd(Clip &changed)
{
    for (auto *points : {&changed.positionXKeyframes, &changed.positionYKeyframes,
                         &changed.scaleKeyframes, &changed.rotationKeyframes,
                         &changed.opacityKeyframes})
        points->erase(std::remove_if(points->begin(), points->end(),
            [&](const ValueKeyframe &point) { return point.frame >= changed.durationFrames; }),
            points->end());
}
QJsonObject mediaJson(const Asset &asset, const QString &projectDirectory)
{
    const auto &m = asset.media;
    const QString relative = QDir(projectDirectory).relativeFilePath(m.path);
    const QString storedPath = relative.startsWith(QStringLiteral("..")) ? m.path : relative;
    return {{"id", asset.id}, {"path", storedPath}, {"name", m.name},
            {"rateNumerator", QString::number(m.frameRate.numerator)},
            {"rateDenominator", QString::number(m.frameRate.denominator)},
            {"frameCount", QString::number(m.frameCount)},
            {"durationUs", QString::number(m.durationUs)},
            {"width", m.width}, {"height", m.height},
            {"hasVideo", m.hasVideo}, {"hasAudio", m.hasAudio},
            {"isStillImage", m.isStillImage},
            {"audioSampleRate", m.audioSampleRate}};
}
int normalizedTextWidth(int canvasWidth, int canvasHeight)
{
    // Text assets are rasterized in a 1080-high coordinate system. Export
    // scales them by outputHeight/1080, just as QML scales fonts in the viewer.
    const double width = 1080.0 * std::max(1, canvasWidth)
        / std::max(1, canvasHeight);
    return std::max(1, qRound(width * 0.86));
}

void layoutWrappedText(QTextLayout &layout, int textWidth, qreal &height,
                       qreal &naturalWidth, QStringList *lines = nullptr,
                       QVector<int> *lineStarts = nullptr)
{
    height = 0;
    naturalWidth = 0;
    layout.beginLayout();
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(textWidth);
        line.setPosition(QPointF(0, height));
        height += line.height();
        naturalWidth = std::max(naturalWidth, line.naturalTextWidth());
        if (lines)
            lines->append(layout.text().mid(line.textStart(), line.textLength()).trimmed());
        if (lineStarts) lineStarts->append(line.textStart());
    }
    layout.endLayout();
}

QStringList wrappedVisualLines(const QString &text, const QFont &font,
                               int canvasWidth, int canvasHeight,
                               QVector<int> *lineStarts = nullptr)
{
    QTextLayout layout(text, font);
    QTextOption option;
    option.setWrapMode(QTextOption::WordWrap);
    option.setAlignment(Qt::AlignHCenter);
    layout.setTextOption(option);
    QStringList lines;
    qreal height = 0;
    qreal naturalWidth = 0;
    layoutWrappedText(layout, normalizedTextWidth(canvasWidth, canvasHeight),
                      height, naturalWidth, &lines, lineStarts);
    return lines;
}

QString renderWrappedTextImage(const QString &path, const QString &text,
    const QFont &font, const QColor &normalColor, const QColor &highlightColor,
    int highlightStart, int highlightLength, const QColor &backgroundColor,
    int backgroundOpacity, int canvasWidth, int canvasHeight,
    const QString &treatment = {}, int opacity = 100)
{
    const int textWidth = normalizedTextWidth(canvasWidth, canvasHeight);
    const int boxWidth = std::max(2, qRound(textWidth * 0.9 / 0.86));
    QTextLayout layout(text, font);
    QTextOption option;
    option.setWrapMode(QTextOption::WordWrap);
    option.setAlignment(Qt::AlignHCenter);
    layout.setTextOption(option);
    QList<QTextLayout::FormatRange> colors;
    QTextLayout::FormatRange normal;
    normal.start = 0;
    normal.length = text.size();
    normal.format.setForeground(normalColor);
    colors.append(normal);
    if (highlightStart >= 0 && highlightLength > 0) {
        QTextLayout::FormatRange highlight;
        highlight.start = highlightStart;
        highlight.length = highlightLength;
        highlight.format.setForeground(treatment == QStringLiteral("block")
            ? QColor(QStringLiteral("#101826")) : highlightColor);
        if (treatment == QStringLiteral("block"))
            highlight.format.setBackground(highlightColor);
        if (treatment == QStringLiteral("underline"))
            highlight.format.setFontUnderline(true);
        colors.append(highlight);
    }
    layout.setFormats(colors);
    qreal height = 0;
    qreal naturalWidth = 0;
    layoutWrappedText(layout, textWidth, height, naturalWidth);
    const int imageWidth = std::max(2, std::min(boxWidth, qCeil(naturalWidth) + 28));
    const int imageHeight = std::max(2, qCeil(height) + 28);
    QImage image(imageWidth, imageHeight, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return {};
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setOpacity(std::clamp(opacity, 0, 100) / 100.0);
    QColor background(backgroundColor);
    background.setAlphaF(backgroundOpacity / 100.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawRoundedRect(QRect(0, 0, imageWidth, imageHeight), 8, 8);
    layout.draw(&painter, QPointF((imageWidth - textWidth) / 2.0, 14));
    painter.end();
    QDir().mkpath(QFileInfo(path).absolutePath());
    return image.save(path, "PNG") ? path : QString{};
}

QString renderTitleImage(const TitleClip &title, int canvasWidth, int canvasHeight)
{
    const QByteArray identity = QByteArray("layout-v2|")
        + QByteArray::number(normalizedTextWidth(canvasWidth, canvasHeight)) + '|'
        + title.text.toUtf8() + '|'
        + QByteArray::number(title.fontPixels) + '|' + title.color.toUtf8() + '|'
        + title.fontFamily.toUtf8() + '|' + QByteArray::number(title.bold) + '|'
        + QByteArray::number(title.italic) + '|' + title.backgroundColor.toUtf8() + '|'
        + QByteArray::number(title.backgroundOpacity) + '|'
        + QByteArray::number(title.anchorX) + '|' + QByteArray::number(title.anchorY);
    const QString key = QString::fromLatin1(
        QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
    const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .filePath(QStringLiteral("titles/") + key + QStringLiteral(".png"));
    if (QFileInfo::exists(path)) return path;
    QFont font(title.fontFamily);
    font.setPixelSize(title.fontPixels);
    font.setBold(title.bold);
    font.setItalic(title.italic);
    return renderWrappedTextImage(path, title.text, font, QColor(title.color),
        QColor(title.color), -1, 0, QColor(title.backgroundColor),
        title.backgroundOpacity, canvasWidth, canvasHeight);
}

QString renderCaptionImage(const CaptionRow &row, const CaptionFrame &state,
                           int canvasWidth, int canvasHeight)
{
    const QByteArray identity = QByteArray("layout-v3|")
        + QByteArray::number(normalizedTextWidth(canvasWidth, canvasHeight)) + '|'
        + row.fontFamily.toUtf8() + '|'
        + QByteArray::number(row.fontPixels) + '|' + QByteArray::number(row.bold) + '|'
        + QByteArray::number(row.italic) + '|' + row.color.toUtf8() + '|'
        + row.highlightColor.toUtf8() + '|' + row.backgroundColor.toUtf8() + '|'
        + QByteArray::number(row.backgroundOpacity) + '|'
        + state.words.join(QLatin1Char(' ')).toUtf8() + '|'
        + QByteArray::number(state.highlight) + '|' + state.treatment.toUtf8() + '|'
        + QByteArray::number(state.opacity);
    const QString key = QString::fromLatin1(
        QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
    const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .filePath(QStringLiteral("captions/") + key + QStringLiteral(".png"));
    if (QFileInfo::exists(path)) return path;
    QFont font(row.fontFamily);
    font.setPixelSize(row.fontPixels);
    font.setBold(row.bold);
    font.setItalic(row.italic);
    const QString all = state.words.join(QLatin1Char(' '));
    int highlightStart = -1;
    int highlightLength = 0;
    if (state.highlight >= 0 && state.highlight < state.words.size()) {
        highlightStart = 0;
        for (int i = 0; i < state.highlight; ++i)
            highlightStart += state.words[i].size() + 1;
        highlightLength = state.words[state.highlight].size();
    }
    const int backgroundOpacity = state.treatment == QStringLiteral("clear") ? 0
        : state.treatment == QStringLiteral("card") ? 92 : row.backgroundOpacity;
    return renderWrappedTextImage(path, all, font, QColor(row.color),
        QColor(row.highlightColor), highlightStart, highlightLength,
        QColor(row.backgroundColor), backgroundOpacity, canvasWidth, canvasHeight,
        state.treatment, state.opacity);
}
} // namespace

EditorController::EditorController(QObject *parent) : QObject(parent)
{
    m_transitionFramePool.setMaxThreadCount(2);
    auto settings = rangeSettings();
    for (const auto &stored : settings.value(QStringLiteral("timeline/customRangeIncrements")).toStringList()) {
        const QString rule = normalizedRangeIncrement(stored);
        if (parseRangeIncrement(rule) && !m_customRangeIncrements.contains(rule))
            m_customRangeIncrements.append(rule);
    }
    const QString savedRule = normalizedRangeIncrement(
        settings.value(QStringLiteral("timeline/rangeIncrementRule"), QStringLiteral("1")).toString());
    if (rangeIncrementOptions().contains(savedRule)) m_rangeIncrementRule = savedRule;
    m_playbackTick.setInterval(16);
    m_playbackTick.setTimerType(Qt::PreciseTimer);
    connect(&m_playbackTick, &QTimer::timeout, this,
            &EditorController::advanceTimelinePlayback);
    m_exportTick.setInterval(1000);
    connect(&m_exportTick, &QTimer::timeout, this, &EditorController::exportChanged);
    m_effectPreviewTimer.setSingleShot(true);
    m_effectPreviewTimer.setInterval(2200);
    connect(&m_effectPreviewTimer, &QTimer::timeout, this, [this] {
        pauseTimelinePlayback();
    });
    connect(this, &EditorController::projectChanged, this, [this] {
        m_playheadFrame = std::clamp<qint64>(m_playheadFrame, 0,
            std::max<qint64>(0, timelineDurationFrames() - 1));
        if (m_dirty) {
            if (m_demoProject) {
                m_demoProject = false;
                m_projectDirectory.clear();
                m_projectFilePath.clear();
                m_projectName = QStringLiteral("Untitled Project");
            }
            ++m_editRevision;
            m_autosaveDebounce.start();
        }
        emit playheadChanged();
    });
    connect(this, &EditorController::projectChanged,
            this, &EditorController::queueTransitionFrames);
    connect(this, &EditorController::proxyChanged,
            this, &EditorController::playheadChanged);
    m_autosaveTimer.setInterval(30000);
    connect(&m_autosaveTimer, &QTimer::timeout, this, &EditorController::autosave);
    m_autosaveTimer.start();
    m_autosaveDebounce.setSingleShot(true);
    m_autosaveDebounce.setInterval(1200);
    connect(&m_autosaveDebounce, &QTimer::timeout, this, &EditorController::autosave);
}

QStringList EditorController::recentProjects() const
{
    return rangeSettings().value(QStringLiteral("projects/recent")).toStringList();
}

void EditorController::recordRecentProject(const QString &path)
{
    if (path.isEmpty()) return;
    QStringList recent = recentProjects();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > 10) recent.removeLast();
    auto settings = rangeSettings();
    settings.setValue(QStringLiteral("projects/recent"), recent);
}

QStringList EditorController::rangeIncrementOptions() const
{
    QStringList result{QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"),
                       QStringLiteral("4"), QStringLiteral("4n+1"), QStringLiteral("8n+1")};
    for (const auto &rule : m_customRangeIncrements)
        if (!result.contains(rule)) result.append(rule);
    return result;
}

QString EditorController::projectRate() const
{
    return rateText(m_projectRate) + QStringLiteral(" FPS");
}

qint64 EditorController::framesForSeconds(qint64 seconds) const
{
    try {
        return rescaleFrames(seconds, FrameRate{1, 1}, m_projectRate);
    } catch (const std::exception &) {
        return 0;
    }
}

qint64 EditorController::projectFramesToMs(qint64 frames) const
{
    try {
        return rescaleFrames(std::max<qint64>(0, frames), m_projectRate, FrameRate{1000, 1});
    } catch (const std::exception &) {
        return 0;
    }
}

QString EditorController::projectFrameTimestamp(qint64 frame) const
{
    const qint64 milliseconds = projectFramesToMs(frame);
    const qint64 hours = milliseconds / 3600000;
    const qint64 minutes = (milliseconds / 60000) % 60;
    const qint64 seconds = (milliseconds / 1000) % 60;
    const qint64 remainder = milliseconds % 1000;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(remainder, 3, 10, QLatin1Char('0'));
}

qint64 EditorController::projectMsToFrames(qint64 milliseconds) const
{
    try {
        return rescaleFrames(std::max<qint64>(0, milliseconds),
                             FrameRate{1000, 1}, m_projectRate);
    } catch (const std::exception &) {
        return 0;
    }
}

qint64 EditorController::timelineDurationFrames() const
{
    qint64 end = 0;
    for (const auto &clip : m_clips)
        end = std::max(end, clip.startFrame + clip.durationFrames);
    for (const auto &title : m_titles)
        end = std::max(end, title.startFrame + title.durationFrames);
    for (const auto &row : m_captionRows)
        for (const auto &cue : row.cues)
            if (!cue.words.isEmpty()) end = std::max(end, cue.words.last().endFrame);
    return end;
}

const Clip *EditorController::clipAtFrame(qint64 frame, int track) const
{
    const Clip *chosen = nullptr;
    for (const auto &clip : m_clips) {
        if (clip.track != track || frame < clip.startFrame
            || frame >= clip.startFrame + clip.durationFrames) continue;
        const auto *asset = findAsset(clip.assetId);
        if (!asset || asset->media.path.isEmpty()) continue;
        if (isVideoTrack(track)) {
            if (!clip.includeVideo || !asset->media.hasVideo) continue;
        } else if (!clip.includeAudio || !asset->media.hasAudio) {
            continue;
        }
        // Later clips on a track cover earlier clips, matching export order.
        chosen = &clip;
    }
    return chosen;
}

QVariantMap EditorController::timelineMedia(int track) const
{
    const qint64 previewFrame = m_effectPreviewFrame >= 0 ? m_effectPreviewFrame : m_playheadFrame;
    const Clip *clip = clipAtFrame(previewFrame, track);
    if (!clip) return {};
    const auto *asset = findAsset(clip->assetId);
    if (!asset) return {};
    try {
        const qint64 offset = previewFrame - clip->startFrame;
        if (offset > std::numeric_limits<qint64>::max() / clip->speedPercent) return {};
        const qint64 sourceOffset = rescaleFrames(
            offset * clip->speedPercent,
            FrameRate{m_projectRate.numerator * 100, m_projectRate.denominator},
            asset->media.frameRate);
        const qint64 sourceFrame = std::min(clip->sourceFirstFrame + sourceOffset,
            clip->sourceFirstFrame + std::max<qint64>(1, clip->sourceFrameCount) - 1);
        const QString path = m_previewQuality == QStringLiteral("Proxy")
            && !asset->proxyPath.isEmpty() && QFileInfo::exists(asset->proxyPath)
            ? asset->proxyPath : asset->media.path;
        const bool changedVoice = clip->voiceChangeEnabled
            && QFileInfo::exists(clip->voiceChangePath)
            && clip->sourceFirstFrame >= clip->voiceSourceFirstFrame
            && clip->sourceFirstFrame + clip->sourceFrameCount
                <= clip->voiceSourceFirstFrame + clip->voiceSourceFrameCount;
        const qint64 voiceMs = changedVoice
            ? rescaleFrames(sourceFrame - clip->voiceSourceFirstFrame,
                            asset->media.frameRate, FrameRate{1000, 1}) : 0;
        return {{"id", clip->id},
                {"url", QUrl::fromLocalFile(changedVoice && !isVideoTrack(track)
                    ? clip->voiceChangePath : path)},
                {"voiceUrl", changedVoice && isVideoTrack(track)
                    ? QUrl::fromLocalFile(clip->voiceChangePath) : QUrl{}},
                {"voiceMs", voiceMs}, {"voiceActive", changedVoice},
                {"track", clip->track},
                {"sourceWidth", asset->media.width}, {"sourceHeight", asset->media.height},
                {"muted", m_mutedTracks.contains(track)},
                {"sourceMs", changedVoice && !isVideoTrack(track) ? voiceMs
                    : rescaleFrames(sourceFrame, asset->media.frameRate,
                                    FrameRate{1000, 1})},
                {"speed", clip->speedPercent / 100.0},
                {"gain", valueAt(clip->gainKeyframes, offset, clip->gainPercent) / 100.0},
                {"positionX", valueAt(clip->positionXKeyframes, offset, clip->positionX)},
                {"positionY", valueAt(clip->positionYKeyframes, offset, clip->positionY)},
                {"scalePercent", valueAt(clip->scaleKeyframes, offset, clip->scalePercent)},
                {"rotationDegrees", valueAt(clip->rotationKeyframes, offset, clip->rotationDegrees)},
                {"opacityPercent", valueAt(clip->opacityKeyframes, offset, clip->opacityPercent)},
                {"still", asset->media.isStillImage}};
    } catch (const std::exception &) {
        return {};
    }
}

QVariantMap EditorController::timelineVideo() const
{
    const qint64 previewFrame = m_effectPreviewFrame >= 0 ? m_effectPreviewFrame : m_playheadFrame;
    for (auto it = m_videoTracks.crbegin(); it != m_videoTracks.crend(); ++it)
        if (!m_hiddenVideoTracks.contains(*it) && clipAtFrame(previewFrame, *it))
            return timelineMedia(*it);
    return {};
}

QVariantMap EditorController::timelineAudio1() const { return timelineMedia(1); }
QVariantMap EditorController::timelineAudio2() const { return timelineMedia(2); }
QVariantMap EditorController::timelineMediaForTrack(int track) const
{
    return isVideoTrack(track) || isAudioTrack(track) ? timelineMedia(track) : QVariantMap{};
}

void EditorController::advanceTimelinePlayback()
{
    if (!m_timelinePlaying) return;
    // Project time drives the playhead. Decoder position is never used as the clock.
    const qint64 elapsedFrames = projectMsToFrames(m_playbackClock.elapsed());
    const qint64 end = m_rangePreviewPlaying ? m_playbackStopFrame : timelineDurationFrames();
    const qint64 next = std::min(end, m_playbackAnchorFrame + elapsedFrames);
    qint64 &currentFrame = m_effectPreviewFrame >= 0 ? m_effectPreviewFrame : m_playheadFrame;
    const qint64 displayFrame = std::min(next, end - 1);
    if (displayFrame != currentFrame) {
        currentFrame = displayFrame;
        emit playheadChanged();
    }
    if (next >= end) pauseTimelinePlayback();
}

void EditorController::pauseTimelinePlayback()
{
    if (!m_timelinePlaying && m_effectPreviewFrame < 0 && !m_rangePreviewPlaying) return;
    m_timelinePlaying = false;
    m_rangePreviewPlaying = false;
    m_playbackStopFrame = -1;
    m_playbackTick.stop();
    if (m_effectPreviewFrame >= 0) {
        m_effectPreviewTimer.stop();
        m_effectPreviewFrame = -1;
        m_previewEffectId.clear();
        m_previewEffectTrack = -1;
        m_previewCaptionRowId.clear();
        m_previewCaptionMode.clear();
        emit playheadChanged();
    }
    emit playbackChanged();
}

void EditorController::cancelEffectPreview()
{
    if (m_effectPreviewFrame >= 0) pauseTimelinePlayback();
}

void EditorController::toggleTimelinePlayback()
{
    if (m_timelinePlaying) {
        pauseTimelinePlayback();
        return;
    }
    const qint64 end = timelineDurationFrames();
    if (end <= 0) return;
    m_timelinePreviewEnabled = true;
    m_rangePreviewPlaying = false;
    m_playbackStopFrame = -1;
    if (m_playheadFrame >= end) m_playheadFrame = 0;
    m_playbackAnchorFrame = m_playheadFrame;
    m_playbackClock.restart();
    m_timelinePlaying = true;
    emit playbackChanged();
    emit playheadChanged();
    m_playbackTick.start();
}

void EditorController::playTimelineRange()
{
    if (!m_timelineRange.valid()) return;
    if (m_timelinePlaying || m_effectPreviewFrame >= 0) pauseTimelinePlayback();
    m_timelinePreviewEnabled = true;
    m_playheadFrame = m_timelineRange.first;
    m_playbackAnchorFrame = m_timelineRange.first;
    m_playbackStopFrame = m_timelineRange.end;
    m_rangePreviewPlaying = true;
    m_playbackClock.restart();
    m_timelinePlaying = true;
    emit playbackChanged();
    emit playheadChanged();
    m_playbackTick.start();
}

QString EditorController::timelineFrameDetails(const QString &clipId,
                                                qint64 projectFrame) const
{
    const auto clip = std::find_if(m_clips.cbegin(), m_clips.cend(),
        [&](const Clip &entry) { return entry.id == clipId; });
    if (clip == m_clips.cend() || clip->durationFrames <= 0) return {};
    const auto *asset = findAsset(clip->assetId);
    if (!asset) return {};
    const qint64 offset = std::clamp(projectFrame - clip->startFrame,
                                     qint64{0}, clip->durationFrames - 1);
    try {
        if (offset > std::numeric_limits<qint64>::max() / clip->speedPercent)
            return {};
        const qint64 sourceOffset = rescaleFrames(
            offset * clip->speedPercent,
            FrameRate{m_projectRate.numerator * 100, m_projectRate.denominator},
            asset->media.frameRate);
        const qint64 sourceUnit = std::min(
            clip->sourceFirstFrame + sourceOffset,
            clip->sourceFirstFrame + clip->sourceFrameCount - 1);
        return QStringLiteral("Project frame %1 · Source %2 %3 · Source rate %4")
            .arg(projectFrame)
            .arg(asset->media.hasVideo ? QStringLiteral("frame")
                                       : QStringLiteral("sample"))
            .arg(sourceUnit)
            .arg(rateText(asset->media.frameRate));
    } catch (const std::exception &) {
        return {};
    }
}

void EditorController::setProjectFrameRate(qint64 numerator, qint64 denominator)
{
    pauseTimelinePlayback();
    const FrameRate changed{numerator, denominator};
    if (!changed.valid() || numerator > 240000 || denominator > 1001) {
        setStatus(QStringLiteral("Choose a supported positive rational frame rate"));
        return;
    }
    if (changed.numerator == m_projectRate.numerator
        && changed.denominator == m_projectRate.denominator) return;
    try {
        Edit edit{Edit::ChangeProjectRate, {}, {}, {}, 0};
        edit.previousRate = m_projectRate;
        edit.resultingRate = changed;
        edit.previousPlayhead = m_playheadFrame;
        edit.resultingPlayhead = rescaleFrames(m_playheadFrame, m_projectRate, changed);
        edit.affectedClips = m_clips;
        edit.resultingClips = m_clips;
        edit.previousTitles = m_titles;
        edit.resultingTitles = m_titles;
        edit.previousTrackEffects = m_trackEffects;
        edit.resultingTrackEffects = m_trackEffects;
        edit.previousTransitions = m_transitions;
        edit.resultingTransitions = m_transitions;
        edit.previousCaptionRows = m_captionRows;
        edit.resultingCaptionRows = m_captionRows;
        for (auto &clip : edit.resultingClips) {
            clip.startFrame = rescaleFrames(clip.startFrame, m_projectRate, changed);
            clip.durationFrames = std::max<qint64>(1,
                rescaleFrames(clip.durationFrames, m_projectRate, changed));
            clip.fadeInFrames = rescaleFrames(clip.fadeInFrames, m_projectRate, changed);
            clip.fadeOutFrames = rescaleFrames(clip.fadeOutFrames, m_projectRate, changed);
            for (auto &point : clip.gainKeyframes)
                point.frame = rescaleFrames(point.frame, m_projectRate, changed);
            clip.gainKeyframes.erase(std::unique(clip.gainKeyframes.begin(),
                clip.gainKeyframes.end(), [](const ValueKeyframe &a, const ValueKeyframe &b) {
                    return a.frame == b.frame;
                }), clip.gainKeyframes.end());
            for (auto *points : {&clip.positionXKeyframes, &clip.positionYKeyframes,
                                 &clip.scaleKeyframes, &clip.rotationKeyframes,
                                 &clip.opacityKeyframes})
                rescaleKeyframeList(*points, m_projectRate, changed, clip.durationFrames - 1);
        }
        for (auto &effect : edit.resultingTrackEffects) {
            for (auto &point : effect.keyframes)
                point.frame = rescaleFrames(point.frame, m_projectRate, changed);
            effect.keyframes.erase(std::unique(effect.keyframes.begin(),
                effect.keyframes.end(), [](const ValueKeyframe &a, const ValueKeyframe &b) {
                    return a.frame == b.frame;
                }), effect.keyframes.end());
            for (auto &parameter : effect.parameters) {
                for (auto &point : parameter.keyframes)
                    point.frame = rescaleFrames(point.frame, m_projectRate, changed);
                parameter.keyframes.erase(std::unique(parameter.keyframes.begin(),
                    parameter.keyframes.end(), [](const ValueKeyframe &a, const ValueKeyframe &b) {
                        return a.frame == b.frame;
                    }), parameter.keyframes.end());
            }
        }
        for (auto &transition : edit.resultingTransitions)
            transition.durationFrames = std::max<qint64>(1,
                rescaleFrames(transition.durationFrames, m_projectRate, changed));
        for (auto &title : edit.resultingTitles) {
            title.startFrame = rescaleFrames(title.startFrame, m_projectRate, changed);
            title.durationFrames = std::max<qint64>(1,
                rescaleFrames(title.durationFrames, m_projectRate, changed));
            for (auto *points : {&title.positionXKeyframes, &title.positionYKeyframes,
                                 &title.scaleKeyframes})
                rescaleKeyframeList(*points, m_projectRate, changed, title.durationFrames - 1);
        }
        for (auto &row : edit.resultingCaptionRows) {
            for (auto *points : {&row.positionXKeyframes, &row.positionYKeyframes,
                                 &row.scaleKeyframes})
                rescaleKeyframeList(*points, m_projectRate, changed,
                    std::numeric_limits<qint64>::max());
            for (auto &cue : row.cues)
                for (auto &word : cue.words) {
                    word.firstFrame = rescaleFrames(word.firstFrame, m_projectRate, changed);
                    word.endFrame = std::max(word.firstFrame + 1,
                        rescaleFrames(word.endFrame, m_projectRate, changed));
                }
        }
        m_undo.append(edit);
        m_redo.clear();
        m_clips = edit.resultingClips;
        m_titles = edit.resultingTitles;
        m_trackEffects = edit.resultingTrackEffects;
        m_transitions = edit.resultingTransitions;
        m_captionRows = edit.resultingCaptionRows;
        m_projectRate = changed;
        m_playheadFrame = edit.resultingPlayhead;
        m_dirty = true;
        emit projectChanged();
        emit playheadChanged();
        setStatus(QStringLiteral("Project rate changed; clip timing preserved"));
    } catch (const std::exception &error) {
        setStatus(QStringLiteral("Frame rate change failed: %1").arg(QString::fromUtf8(error.what())));
    }
}

QVariantList EditorController::assets() const
{
    QVariantList result;
    result.reserve(m_assets.size());
    for (const auto &asset : m_assets) {
        const auto &m = asset.media;
        result.append(QVariantMap{{"id", asset.id}, {"name", m.name},
                                  {"url", QUrl::fromLocalFile(m.path)},
                                  {"frames", m.frameCount}, {"rate", rateText(m.frameRate)},
                                  {"durationUs", m.durationUs},
                                  {"thumbnail", asset.thumbnailPath.isEmpty()
                                      ? QUrl{} : QUrl::fromLocalFile(asset.thumbnailPath)},
                                  {"waveform", asset.waveformPath.isEmpty()
                                      ? QUrl{} : QUrl::fromLocalFile(asset.waveformPath)},
                                  {"hasVideo", m.hasVideo && !mp3CoverArtWasClassifiedAsVideo(m)},
                                  {"hasAudio", m.hasAudio},
                                  {"isStillImage", m.isStillImage},
                                  {"proxyReady", !asset.proxyPath.isEmpty()
                                      && QFileInfo::exists(asset.proxyPath)},
                                  {"audioSampleRate", m.audioSampleRate},
                                  {"missing", !QFileInfo::exists(m.path)}});
    }
    return result;
}

QVariantList EditorController::clips() const
{
    QVariantList result;
    result.reserve(m_clips.size());
    for (const auto &clip : m_clips) {
        const auto *asset = findAsset(clip.assetId);
        QVariantMap item{{"id", clip.id}, {"assetId", clip.assetId},
                         {"name", asset ? asset->media.name : QStringLiteral("Missing asset")},
                         {"start", clip.startFrame}, {"duration", clip.durationFrames},
                         {"sourceFirst", clip.sourceFirstFrame},
                         {"sourceFrames", clip.sourceFrameCount},
                         {"assetFrames", asset ? asset->media.frameCount : 0},
                         {"track", clip.track},
                         {"positionX", clip.positionX}, {"positionY", clip.positionY},
                         {"scalePercent", clip.scalePercent},
                         {"positionXKeyframes", keyframesJson(clip.positionXKeyframes).toVariantList()},
                         {"positionYKeyframes", keyframesJson(clip.positionYKeyframes).toVariantList()},
                         {"scaleKeyframes", keyframesJson(clip.scaleKeyframes).toVariantList()},
                         {"rotationKeyframes", keyframesJson(clip.rotationKeyframes).toVariantList()},
                         {"opacityKeyframes", keyframesJson(clip.opacityKeyframes).toVariantList()},
                         {"rotationDegrees", clip.rotationDegrees},
                         {"opacityPercent", clip.opacityPercent},
                         {"gainPercent", clip.gainPercent},
                         {"gainKeyframes", QVariantList{}},
                         {"fadeInFrames", clip.fadeInFrames},
                         {"fadeOutFrames", clip.fadeOutFrames},
                         {"speedPercent", clip.speedPercent},
                         {"effectId", clip.effectId},
                         {"effectStrength", clip.effectStrength},
                         {"audioEffectId", clip.audioEffectId},
                         {"audioEffectStrength", clip.audioEffectStrength},
                         {"voiceChangePath", clip.voiceChangePath},
                         {"voiceName", clip.voiceName},
                         {"voiceMode", clip.voiceMode},
                         {"voiceChangeEnabled", clip.voiceChangeEnabled},
                         {"hasVideo", asset && asset->media.hasVideo && clip.includeVideo},
                         {"hasAudio", asset && asset->media.hasAudio && clip.includeAudio},
                         {"isStillImage", asset && asset->media.isStillImage}};
        QVariantList gainPoints;
        for (const auto &point : clip.gainKeyframes)
            gainPoints.append(QVariantMap{{"frame", point.frame}, {"value", point.value}});
        item.insert("gainKeyframes", gainPoints);
        if (asset && !asset->thumbnailPath.isEmpty())
            item.insert("thumbnail", QUrl::fromLocalFile(asset->thumbnailPath));
        if (asset && !asset->waveformPath.isEmpty())
            item.insert("waveform", QUrl::fromLocalFile(asset->waveformPath));
        if (asset && !asset->filmstripPaths.isEmpty()) {
            QVariantList images;
            for (const auto &path : asset->filmstripPaths)
                images.append(QUrl::fromLocalFile(path));
            item.insert("filmstrip", images);
        }
        result.append(item);
    }
    return result;
}

QVariantList EditorController::trackEffects() const
{
    QVariantList result;
    for (const auto &effect : m_trackEffects) {
        QVariantList points;
        for (const auto &point : effect.keyframes)
            points.append(QVariantMap{{"frame", point.frame}, {"value", point.value}});
        QVariantList parameters;
        for (const auto &parameter : effect.parameters) {
            QVariantList parameterPoints;
            for (const auto &point : parameter.keyframes)
                parameterPoints.append(QVariantMap{{"frame", point.frame}, {"value", point.value}});
            parameters.append(QVariantMap{{"id", parameter.id}, {"value", parameter.value},
                {"currentValue", valueAt(parameter.keyframes, m_playheadFrame, parameter.value)},
                {"keyframes", parameterPoints}});
        }
        result.append(QVariantMap{{"track", effect.track}, {"id", effect.id},
            {"strength", effect.strength},
            {"currentStrength", valueAt(effect.keyframes, m_playheadFrame, effect.strength)},
            {"keyframes", points}, {"parameters", parameters}});
    }
    return result;
}

QVariantList EditorController::tracks() const
{
    QVariantList result;
    // Audio has no picture to cover; put its generated captions where they are visible by default.
    for (const auto &row : m_captionRows)
        if (isAudioTrack(row.targetTrack))
            result.append(QVariantMap{{"id", row.trackId}, {"kind", "caption"},
                {"name", "Captions"}, {"captionRowId", row.id},
                {"visible", row.visible}, {"muted", false}});
    for (qsizetype i = m_videoTracks.size(); i > 0; --i) {
        for (const auto &row : m_captionRows)
            if (row.targetTrack == m_videoTracks[i - 1])
                result.append(QVariantMap{{"id", row.trackId}, {"kind", "caption"},
                    {"name", "Captions"}, {"captionRowId", row.id},
                    {"visible", row.visible}, {"muted", false}});
        result.append(QVariantMap{{"id", m_videoTracks[i - 1]}, {"kind", "video"},
            {"name", QStringLiteral("V%1").arg(i)},
            {"visible", !m_hiddenVideoTracks.contains(m_videoTracks[i - 1])},
            {"muted", m_mutedTracks.contains(m_videoTracks[i - 1])}});
    }
    for (qsizetype i = 0; i < m_audioTracks.size(); ++i) {
        result.append(QVariantMap{{"id", m_audioTracks[i]}, {"kind", "audio"},
            {"name", QStringLiteral("A%1").arg(i + 1)},
            {"visible", true}, {"muted", m_mutedTracks.contains(m_audioTracks[i])}});
    }
    if (m_trackOrder.isEmpty()) return result;
    QVariantList ordered;
    for (const int id : m_trackOrder) {
        const auto it = std::find_if(result.cbegin(), result.cend(), [id](const QVariant &row) {
            return row.toMap().value(QStringLiteral("id")).toInt() == id;
        });
        if (it != result.cend()) ordered.append(*it);
    }
    for (const auto &row : result) {
        const auto data = row.toMap();
        const int id = data.value(QStringLiteral("id")).toInt();
        if (std::any_of(ordered.cbegin(), ordered.cend(), [id](const QVariant &item) {
            return item.toMap().value(QStringLiteral("id")).toInt() == id;
        })) continue;
        const QString kind = data.value(QStringLiteral("kind")).toString();
        if (kind == QStringLiteral("caption")) {
            const auto caption = std::find_if(m_captionRows.cbegin(), m_captionRows.cend(),
                [id](const CaptionRow &item) { return item.trackId == id; });
            const int target = caption == m_captionRows.cend() ? -1 : caption->targetTrack;
            if (isAudioTrack(target)) {
                ordered.prepend(row);
                continue;
            }
            auto position = std::find_if(ordered.begin(), ordered.end(), [target](const QVariant &item) {
                return item.toMap().value(QStringLiteral("id")).toInt() == target;
            });
            ordered.insert(position, row);
        } else if (kind == QStringLiteral("video")) {
            auto position = std::find_if(ordered.begin(), ordered.end(), [](const QVariant &item) {
                return item.toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("video");
            });
            ordered.insert(position, row);
        } else ordered.append(row);
    }
    return ordered;
}

void EditorController::toggleTrackVisibility(int track)
{
    if (!isVideoTrack(track)) return;
    const bool before = !m_hiddenVideoTracks.contains(track);
    Edit edit{Edit::SetTrackVisibility};
    edit.stateTrack = track;
    edit.previousTrackState = before;
    edit.resultingTrackState = !before;
    m_undo.append(edit);
    m_redo.clear();
    if (before) m_hiddenVideoTracks.insert(track);
    else m_hiddenVideoTracks.remove(track);
    m_dirty = true;
    emit projectChanged();
}

void EditorController::toggleTrackMute(int track)
{
    if (!isVideoTrack(track) && !isAudioTrack(track)) return;
    const bool before = m_mutedTracks.contains(track);
    Edit edit{Edit::SetTrackMute};
    edit.stateTrack = track;
    edit.previousTrackState = before;
    edit.resultingTrackState = !before;
    m_undo.append(edit);
    m_redo.clear();
    if (before) m_mutedTracks.remove(track);
    else m_mutedTracks.insert(track);
    m_dirty = true;
    emit projectChanged();
}

bool EditorController::isVideoTrack(int track) const
{
    return m_videoTracks.contains(track);
}

bool EditorController::isAudioTrack(int track) const
{
    return m_audioTracks.contains(track);
}

int EditorController::trackRow(int track) const
{
    const auto rows = tracks();
    for (qsizetype i = 0; i < rows.size(); ++i)
        if (rows[i].toMap().value(QStringLiteral("id")).toInt() == track) return int(i);
    return -1;
}

void EditorController::moveTrackRow(int track, int destinationRow)
{
    const auto rows = tracks();
    const int from = trackRow(track);
    if (from < 0 || destinationRow < 0 || destinationRow >= rows.size()
        || from == destinationRow) return;
    Edit edit{Edit::ReorderTracks};
    edit.previousTrackOrder = m_trackOrder;
    QVector<int> order;
    order.reserve(rows.size());
    for (const auto &row : rows)
        order.append(row.toMap().value(QStringLiteral("id")).toInt());
    order.move(from, destinationRow);
    m_trackOrder = order;
    edit.resultingTrackOrder = order;
    m_undo.append(edit);
    m_redo.clear();
    m_dirty = true;
    emit projectChanged();
}

int EditorController::addTrack(const QString &kind)
{
    const bool video = kind == QStringLiteral("video");
    if (!video && kind != QStringLiteral("audio")) return -1;
    auto &list = video ? m_videoTracks : m_audioTracks;
    if (list.size() >= 30) {
        setStatus(QStringLiteral("A project can have at most 30 %1 tracks").arg(kind));
        return -1;
    }
    int id = -1;
    const int first = video ? 4 : 32;
    const int last = video ? 31 : 59;
    for (int candidate = first; candidate <= last; ++candidate)
        if (!list.contains(candidate)) { id = candidate; break; }
    if (id < 0) return -1;
    Edit edit;
    edit.kind = video ? Edit::AddVideoTrack : Edit::AddAudioTrack;
    edit.addedTrack = id;
    m_undo.append(edit);
    m_redo.clear();
    list.append(id);
    m_dirty = true;
    emit projectChanged();
    setStatus(QStringLiteral("%1 track added").arg(video ? "Video" : "Audio"));
    return id;
}

int EditorController::resolveNewTrack(int track)
{
    if (track == -1) return addTrack(QStringLiteral("video"));
    if (track == -2) return addTrack(QStringLiteral("audio"));
    return track;
}

QVariantMap EditorController::previewEffects() const
{
    const Clip *clip = nullptr;
    const qint64 previewFrame = m_effectPreviewFrame >= 0 ? m_effectPreviewFrame : m_playheadFrame;
    for (auto it = m_videoTracks.crbegin(); it != m_videoTracks.crend(); ++it)
        if ((clip = clipAtFrame(previewFrame, *it))) break;
    if (!clip) return {};
    return previewEffectsForTrack(clip->track);
}

QVariantMap EditorController::previewEffectsForTrack(int track) const
{
    const qint64 previewFrame = m_effectPreviewFrame >= 0 ? m_effectPreviewFrame : m_playheadFrame;
    const Clip *clip = isVideoTrack(track) ? clipAtFrame(previewFrame, track) : nullptr;
    const bool hasTitle = std::any_of(m_titles.cbegin(), m_titles.cend(),
        [track, previewFrame](const TitleClip &title) {
            return title.track == track && previewFrame >= title.startFrame
                && previewFrame < title.startFrame + title.durationFrames;
        });
    const bool hasCaption = std::any_of(m_captionRows.cbegin(), m_captionRows.cend(),
        [track, previewFrame](const CaptionRow &row) {
            if (row.trackId != track || !row.visible) return false;
            return std::any_of(row.cues.cbegin(), row.cues.cend(),
                [previewFrame](const CaptionCue &cue) {
                    return !cue.words.isEmpty()
                        && previewFrame >= cue.words.first().firstFrame
                        && previewFrame < cue.words.last().endFrame;
                });
        });
    if (!clip && !hasTitle && !hasCaption) return {};
    QVariantMap result = effectTilePreviewSettings({});
    const auto accumulate = [&](const QString &id, int strength,
                                const QVector<EffectParameterState> *parameters = nullptr) {
        const auto *definition = videoEffectDefinition(id);
        if (definition && !definition->parameters.isEmpty()) {
            const QString prefix = id.section(QLatin1Char('.'), -1) + QLatin1Char('_');
            result.insert(prefix + QStringLiteral("enabled"), 1.0);
            for (const auto &parameter : definition->parameters) {
                int value = parameter.initial;
                if (parameters) {
                    const auto state = std::find_if(parameters->cbegin(), parameters->cend(),
                        [&](const EffectParameterState &item) { return item.id == parameter.id; });
                    if (state != parameters->cend())
                        value = valueAt(state->keyframes, previewFrame, state->value);
                }
                result.insert(prefix + parameter.id, value);
            }
        } else if (id == QStringLiteral("datacut.video.grayscale"))
            result.insert("saturation", result.value("saturation").toDouble() - strength / 100.0);
        else if (id == QStringLiteral("datacut.video.saturation"))
            result.insert("saturation", result.value("saturation").toDouble() + strength / 100.0);
        else if (id == QStringLiteral("datacut.video.brightness"))
            result.insert("brightness", result.value("brightness").toDouble() + strength / 300.0);
        else if (id == QStringLiteral("datacut.video.blur"))
            result.insert("blur", std::max(result.value("blur").toDouble(), strength / 100.0));
        else if (id == QStringLiteral("datacut.video.vignette"))
            result.insert("vignette", std::max(result.value("vignette").toDouble(), strength / 100.0));
    };
    for (const auto &effect : m_trackEffects)
        if (effect.track == track)
            accumulate(effect.id, valueAt(effect.keyframes, previewFrame, effect.strength),
                       &effect.parameters);
    if (clip) accumulate(clip->effectId, clip->effectStrength);
    if (!m_previewEffectId.isEmpty() && m_previewEffectTrack == track)
        accumulate(m_previewEffectId, 100);
    result.insert("saturation", std::clamp(result.value("saturation").toDouble(), -1.0, 2.0));
    result.insert("brightness", std::clamp(result.value("brightness").toDouble(), -1.0, 1.0));
    bool active = result.value("saturation").toDouble() != 0
        || result.value("brightness").toDouble() != 0
        || result.value("blur").toDouble() != 0
        || result.value("vignette").toDouble() != 0;
    for (const auto &definition : builtinEffects())
        if (!definition.parameters.isEmpty()
            && result.value(definition.id.section(QLatin1Char('.'), -1)
                + QStringLiteral("_enabled")).toDouble() > 0) active = true;
    result.insert("active", active);
    return result;
}

QVariantMap EditorController::effectTilePreviewSettings(const QString &effectId) const
{
    QVariantMap result{{"saturation", 0.0}, {"brightness", 0.0}, {"blur", 0.0},
                       {"vignette", 0.0}, {"active", !effectId.isEmpty()}};
    for (const auto &definition : builtinEffects()) {
        if (definition.parameters.isEmpty()) continue;
        const QString prefix = definition.id.section(QLatin1Char('.'), -1) + QLatin1Char('_');
        result.insert(prefix + QStringLiteral("enabled"), definition.id == effectId ? 1.0 : 0.0);
        for (const auto &parameter : definition.parameters)
            result.insert(prefix + parameter.id, parameter.initial);
    }
    if (effectId == QStringLiteral("datacut.video.grayscale")) result.insert("saturation", -1.0);
    else if (effectId == QStringLiteral("datacut.video.saturation")) result.insert("saturation", 1.0);
    else if (effectId == QStringLiteral("datacut.video.brightness")) result.insert("brightness", 0.33);
    else if (effectId == QStringLiteral("datacut.video.blur")) result.insert("blur", 0.75);
    else if (effectId == QStringLiteral("datacut.video.vignette")) result.insert("vignette", 1.0);
    return result;
}

QVariantList EditorController::transitionDefinitions() const
{
    QVariantList result;
    for (const auto &definition : m_transitionRegistry.definitions()) {
        QVariantList parameters;
        for (const auto &parameter : definition.parameters)
            parameters.append(QVariantMap{{"id", parameter.id}, {"name", parameter.name},
                {"minimum", parameter.minimum}, {"maximum", parameter.maximum},
                {"initial", parameter.initial}});
        result.append(QVariantMap{{"id", definition.id}, {"name", definition.name},
            {"category", definition.category}, {"renderer", definition.renderer},
            {"parameters", parameters}});
    }
    return result;
}

QVariantMap EditorController::transitionTargetAt(int track, qint64 frame,
                                                   qint64 toleranceFrames) const
{
    if (!isVideoTrack(track)) return {};
    const auto visuals = visualEdges(m_clips, m_titles);
    qint64 nearest = std::numeric_limits<qint64>::max();
    qint64 cut = -1;
    for (const auto &visual : visuals) {
        if (visual.track != track) continue;
        for (const qint64 edge : {visual.start, visual.start + visual.duration}) {
            const qint64 distance = std::llabs(frame - edge);
            if (distance <= toleranceFrames && distance < nearest) {
                nearest = distance;
                cut = edge;
            }
        }
    }
    if (cut < 0) return {};
    QString outgoing, incoming;
    const VisualEdge *outgoingClip = nullptr;
    const VisualEdge *incomingClip = nullptr;
    for (const auto &visual : visuals) {
        if (visual.track != track) continue;
        if (visual.start + visual.duration == cut) {
            outgoing = visual.id;
            outgoingClip = &visual;
        }
        if (visual.start == cut) {
            incoming = visual.id;
            incomingClip = &visual;
        }
    }
    const bool paired = outgoingClip && incomingClip;
    qint64 maximum = paired ? std::min(outgoingClip->duration,
        incomingClip->duration) * 2
        : outgoingClip ? outgoingClip->duration
        : incomingClip ? incomingClip->duration : 0;
    const qint64 requested = std::max<qint64>(1, m_projectRate.numerator
        / m_projectRate.denominator);
    const qint64 duration = std::max<qint64>(paired ? 2 : 1,
        std::min(maximum, requested));
    const qint64 left = paired ? std::min(outgoingClip->duration,
        std::max<qint64>(1, duration / 2)) : outgoingClip ? duration : 0;
    const qint64 right = paired ? std::min(incomingClip->duration,
        std::max<qint64>(1, duration - left)) : incomingClip ? duration : 0;
    return {{"valid", !outgoing.isEmpty() || !incoming.isEmpty()}, {"cut", cut},
        {"track", track}, {"outgoing", outgoing}, {"incoming", incoming},
        {"paired", paired}, {"start", cut - left}, {"duration", left + right}};
}

QVariantList EditorController::transitions() const
{
    QVariantList result;
    const auto visuals = visualEdges(m_clips, m_titles);
    for (const auto &transition : m_transitions) {
        const VisualEdge *outgoing = findVisualEdge(visuals, transition.outgoingClipId);
        const VisualEdge *incoming = findVisualEdge(visuals, transition.incomingClipId);
        const bool paired = outgoing && incoming && outgoing->track == incoming->track
            && outgoing->start + outgoing->duration == incoming->start;
        const auto *definition = m_transitionRegistry.find(transition.definitionId);
        const QString name = definition ? definition->name
            : QStringLiteral("Unavailable transition: %1").arg(transition.definitionId);
        const QString renderer = definition ? definition->renderer : QStringLiteral("missing");
        const auto append = [&](const VisualEdge &clip, qint64 start, qint64 duration,
                                qint64 cut, bool startEdge, bool joined) {
            QVariantMap values;
            for (auto it = transition.parameters.cbegin(); it != transition.parameters.cend(); ++it)
                values.insert(it.key(), it.value());
            result.append(QVariantMap{{"id", transition.id}, {"definitionId", transition.definitionId},
                {"name", name}, {"renderer", renderer}, {"parameters", values},
                {"track", clip.track}, {"start", start}, {"duration", duration},
                {"cut", cut}, {"startEdge", startEdge}, {"paired", joined},
                {"outgoingId", transition.outgoingClipId},
                {"incomingId", transition.incomingClipId},
                {"balance", transition.balance}});
        };
        if (paired) {
            const qint64 left = std::min(outgoing->duration,
                std::max<qint64>(1, transition.durationFrames * transition.balance / 100));
            const qint64 right = std::min(incoming->duration,
                std::max<qint64>(1, transition.durationFrames - left));
            const qint64 cut = incoming->start;
            append(*outgoing, cut - left, left + right, cut, false, true);
        } else {
            if (outgoing) {
                const qint64 duration = std::min(transition.durationFrames, outgoing->duration);
                const qint64 cut = outgoing->start + outgoing->duration;
                append(*outgoing, cut - duration, duration, cut, false, false);
            }
            if (incoming) {
                const qint64 duration = std::min(transition.durationFrames, incoming->duration);
                append(*incoming, incoming->start, duration, incoming->start,
                       true, false);
            }
        }
    }
    return result;
}

void EditorController::addTransition(const QString &definitionId, int track, qint64 frame)
{
    if (!m_transitionRegistry.find(definitionId)) return;
    const QVariantMap target = transitionTargetAt(track, frame, 0);
    if (!target.value("valid").toBool()) return;
    const QString outgoing = target.value("outgoing").toString();
    const QString incoming = target.value("incoming").toString();
    const auto sameEdge = [&](const ClipTransition &transition) {
        return (!outgoing.isEmpty() && transition.outgoingClipId == outgoing)
            || (!incoming.isEmpty() && transition.incomingClipId == incoming);
    };
    Edit edit; edit.kind = Edit::ReplaceTransitions;
    edit.previousTransitions = m_transitions;
    m_transitions.erase(std::remove_if(m_transitions.begin(), m_transitions.end(), sameEdge),
                        m_transitions.end());
    const qint64 duration = target.value("duration").toLongLong();
    ClipTransition added{freshId(), definitionId, outgoing, incoming, duration, 50};
    for (const auto &parameter : m_transitionRegistry.find(definitionId)->parameters)
        if (parameter.id != QStringLiteral("balance"))
            added.parameters.insert(parameter.id, parameter.initial);
    m_transitions.append(added);
    qInfo().noquote() << "transition_added" << definitionId
                      << "track" << track << "frame" << frame
                      << "outgoing" << outgoing << "incoming" << incoming;
    edit.resultingTransitions = m_transitions;
    m_undo.append(edit); m_redo.clear(); m_dirty = true;
    m_selectedTransitionId = added.id;
    m_selectedCaptionId.clear();
    m_selectedCaptionRowId.clear();
    emit projectChanged(); emit selectionChanged();
    setStatus(QStringLiteral("Transition added at %1").arg(projectFrameTimestamp(frame)));
}

void EditorController::selectTransition(const QString &id)
{
    if (std::none_of(m_transitions.cbegin(), m_transitions.cend(),
        [&](const ClipTransition &transition) { return transition.id == id; })) return;
    m_selectedTransitionId = id;
    m_selectedClipId.clear();
    m_selectedTitleId.clear();
    m_selectedCaptionId.clear();
    m_selectedCaptionRowId.clear();
    emit selectionChanged();
}

void EditorController::removeTransition(const QString &id)
{
    const auto it = std::find_if(m_transitions.cbegin(), m_transitions.cend(),
        [&](const ClipTransition &transition) { return transition.id == id; });
    if (it == m_transitions.cend()) return;
    Edit edit; edit.kind = Edit::ReplaceTransitions;
    edit.previousTransitions = m_transitions;
    m_transitions.erase(it);
    edit.resultingTransitions = m_transitions;
    m_undo.append(edit); m_redo.clear(); m_dirty = true;
    if (m_selectedTransitionId == id) m_selectedTransitionId.clear();
    emit projectChanged(); emit selectionChanged();
}

void EditorController::setTransitionDuration(const QString &id, qint64 frames)
{
    auto it = std::find_if(m_transitions.begin(), m_transitions.end(),
        [&](const ClipTransition &transition) { return transition.id == id; });
    if (it == m_transitions.end() || frames < 1) return;
    qint64 maximum = std::numeric_limits<qint64>::max();
    const auto visuals = visualEdges(m_clips, m_titles);
    const auto *outgoing = findVisualEdge(visuals, it->outgoingClipId);
    const auto *incoming = findVisualEdge(visuals, it->incomingClipId);
    const bool paired = outgoing && incoming && outgoing->track == incoming->track
        && outgoing->start + outgoing->duration == incoming->start;
    if (outgoing)
        maximum = std::min(maximum, outgoing->duration * 100
                           / (paired ? it->balance : 100));
    if (incoming)
        maximum = std::min(maximum, incoming->duration * 100
                           / (paired ? 100 - it->balance : 100));
    if (maximum == std::numeric_limits<qint64>::max()) return;
    frames = std::max<qint64>(paired ? 2 : 1, std::min(frames, maximum));
    if (frames == it->durationFrames) return;
    Edit edit; edit.kind = Edit::ReplaceTransitions;
    edit.previousTransitions = m_transitions;
    it->durationFrames = frames;
    edit.resultingTransitions = m_transitions;
    m_undo.append(edit); m_redo.clear(); m_dirty = true;
    emit projectChanged();
}

void EditorController::setTransitionBalance(const QString &id, int balance)
{
    auto it = std::find_if(m_transitions.begin(), m_transitions.end(),
        [&](const ClipTransition &transition) { return transition.id == id; });
    if (it == m_transitions.end() || balance < 20 || balance > 80
        || balance == it->balance) return;
    Edit edit; edit.kind = Edit::ReplaceTransitions;
    edit.previousTransitions = m_transitions;
    it->balance = balance;
    if (!it->outgoingClipId.isEmpty() && !it->incomingClipId.isEmpty()) {
        qint64 maximum = std::numeric_limits<qint64>::max();
        const auto visuals = visualEdges(m_clips, m_titles);
        if (const auto *outgoing = findVisualEdge(visuals, it->outgoingClipId))
            maximum = std::min(maximum, outgoing->duration * 100 / balance);
        if (const auto *incoming = findVisualEdge(visuals, it->incomingClipId))
            maximum = std::min(maximum, incoming->duration * 100 / (100 - balance));
        if (maximum != std::numeric_limits<qint64>::max())
            it->durationFrames = std::min(it->durationFrames, maximum);
    }
    edit.resultingTransitions = m_transitions;
    m_undo.append(edit); m_redo.clear(); m_dirty = true;
    emit projectChanged();
}

void EditorController::setTransitionParameter(const QString &id,
                                               const QString &parameterId, int value)
{
    auto it = std::find_if(m_transitions.begin(), m_transitions.end(),
        [&](const ClipTransition &transition) { return transition.id == id; });
    if (it == m_transitions.end()) return;
    const auto *definition = m_transitionRegistry.find(it->definitionId);
    if (!definition) return;
    const auto metadata = std::find_if(definition->parameters.cbegin(), definition->parameters.cend(),
        [&](const TransitionParameterDefinition &item) { return item.id == parameterId; });
    if (metadata == definition->parameters.cend() || value < metadata->minimum
        || value > metadata->maximum) return;
    if (parameterId == QStringLiteral("balance")) {
        setTransitionBalance(id, value);
        return;
    }
    if (it->parameters.value(parameterId, metadata->initial) == value) return;
    Edit edit; edit.kind = Edit::ReplaceTransitions;
    edit.previousTransitions = m_transitions;
    it->parameters.insert(parameterId, value);
    edit.resultingTransitions = m_transitions;
    m_undo.append(edit); m_redo.clear(); m_dirty = true;
    emit projectChanged();
}

QVariantMap EditorController::transitionPreviewForTrack(int track) const
{
    const qint64 frame = m_effectPreviewFrame >= 0 ? m_effectPreviewFrame : m_playheadFrame;
    for (const auto &view : transitions()) {
        const auto item = view.toMap();
        if (item.value("track").toInt() != track) continue;
        const qint64 start = item.value("start").toLongLong();
        const qint64 duration = item.value("duration").toLongLong();
        const QString renderer = item.value("renderer").toString();
        const bool usesHeldEdges = m_transitionRegistry.usesHeldEdges(renderer);
        const bool preloading = frame < start
            && frame >= start - std::max<qint64>(1, m_projectRate.numerator * 2
                / m_projectRate.denominator)
            && item.value("paired").toBool() && usesHeldEdges;
        if ((!preloading && frame < start) || frame >= start + duration) continue;
        const auto transition = std::find_if(m_transitions.cbegin(), m_transitions.cend(),
            [&](const ClipTransition &value) { return value.id == item.value("id").toString(); });
        if (transition == m_transitions.cend()) continue;
        if (!m_transitionRegistry.supportsPreview(renderer)) return {};
        const bool paired = item.value("paired").toBool();
        const qint64 cut = item.value("cut").toLongLong();
        const TransitionFrameInput sampleInput{frame, start, cut, duration, paired,
            item.value("startEdge").toBool(),
            transition->parameters.value(QStringLiteral("blend"), 50)};
        const TransitionSample sample = preloading ? TransitionSample{}
            : m_transitionRegistry.sample(renderer, sampleInput);
        QVariantMap result{{"id", transition->id}, {"renderer", item.value("renderer")},
            {"paired", paired}, {"usesHeldEdges", usesHeldEdges},
            {"outgoingId", transition->outgoingClipId},
            {"incomingId", transition->incomingClipId},
            {"progress", preloading ? 0.0 : (frame - start + 0.5) / duration},
            {"cut", cut}, {"mainOpacity", sample.mainOpacity},
            {"blackOpacity", sample.blackOpacity},
            {"otherOpacity", 0.0}};
        const int maskKind = spatialMaskKind(renderer);
        if (maskKind > 0) {
            const double linear = duration <= 1 ? 1.0 : std::clamp(
                (frame - start) / double(duration - 1), 0.0, 1.0);
            const double margin = std::max(0.000001,
                transition->parameters.value(QStringLiteral("softness"), 3) / 100.0) / 2.0;
            const double progress = -margin + (1.0 + 2.0 * margin) * std::pow(linear,
                100.0 / std::clamp(transition->parameters.value(QStringLiteral("speed"), 100), 25, 200));
            const QVariantMap mask{{"kind", maskKind}, {"progress", progress},
                {"softness", transition->parameters.value(QStringLiteral("softness"), 3)},
                {"complexity", transition->parameters.value(QStringLiteral("complexity"), 6)},
                {"edgeEnabled", transition->parameters.value(QStringLiteral("edge_enabled"), 0)},
                {"edgeColor", transition->parameters.value(QStringLiteral("edge_color"), 0xFFFFFF)}};
            if (paired) {
                QVariantMap incomingMask = mask;
                incomingMask.insert(QStringLiteral("reveal"), true);
                QVariantMap outgoingMask = mask;
                outgoingMask.insert(QStringLiteral("reveal"), false);
                result.insert(QStringLiteral("incomingMask"), incomingMask);
                result.insert(QStringLiteral("outgoingMask"), outgoingMask);
            } else {
                QVariantMap mainMask = mask;
                mainMask.insert(QStringLiteral("reveal"), item.value("startEdge").toBool());
                result.insert(QStringLiteral("mainMask"), mainMask);
            }
        }
        if (!usesHeldEdges || !paired) return result;
        const auto edgeSource = [&](const QString &clipId, bool first, double opacity) {
            const auto edge = std::find_if(m_clips.cbegin(), m_clips.cend(),
                [&](const Clip &clip) { return clip.id == clipId; });
            if (edge == m_clips.cend()) {
                const auto title = std::find_if(m_titles.cbegin(), m_titles.cend(),
                    [&](const TitleClip &item) { return item.id == clipId; });
                if (title == m_titles.cend()) return QVariantMap{};
                const qint64 localFrame = first ? 0 : title->durationFrames - 1;
                const QString imagePath = renderTitleImage(*title, m_exportWidth,
                                                           m_exportHeight);
                const QSize size = QImageReader(imagePath).size();
                if (!size.isValid()) return QVariantMap{};
                const QUrl imageUrl = QUrl::fromLocalFile(imagePath);
                return QVariantMap{{"url", imageUrl}, {"imageUrl", imageUrl},
                    {"textObject", true}, {"imageWidth", size.width()},
                    {"imageHeight", size.height()},
                    {"positionX", valueAt(title->positionXKeyframes,
                                           localFrame, title->positionX)},
                    {"positionY", valueAt(title->positionYKeyframes,
                                           localFrame, title->positionY)},
                    {"scalePercent", valueAt(title->scaleKeyframes,
                                               localFrame, title->scalePercent)},
                    {"anchorX", title->anchorX}, {"anchorY", title->anchorY},
                    {"opacity", opacity}};
            }
            const Asset *asset = findAsset(edge->assetId);
            if (!asset) return QVariantMap{};
            const qint64 edgeFrame = first ? 0 : std::max<qint64>(0, edge->durationFrames - 1);
            const qint64 sourceFrame = first ? edge->sourceFirstFrame
                : edge->sourceFirstFrame + std::max<qint64>(1, edge->sourceFrameCount) - 1;
            const QString cachedFrame = asset->media.isStillImage ? asset->media.path
                : transitionFrameCachePath(asset->media.path, sourceFrame,
                                           asset->media.frameRate);
            const QString imageUrl = QFileInfo::exists(cachedFrame)
                ? QUrl::fromLocalFile(cachedFrame).toString() : QString{};
            return QVariantMap{{"url", QUrl::fromLocalFile(asset->media.path)},
                {"imageUrl", imageUrl},
                {"sourceWidth", asset->media.width},
                {"sourceHeight", asset->media.height},
                {"sourceMs", rescaleFrames(sourceFrame, asset->media.frameRate,
                                             FrameRate{1000, 1})},
                {"still", asset->media.isStillImage},
                {"scalePercent", valueAt(edge->scaleKeyframes, edgeFrame, edge->scalePercent)},
                {"positionX", valueAt(edge->positionXKeyframes, edgeFrame, edge->positionX)},
                {"positionY", valueAt(edge->positionYKeyframes, edgeFrame, edge->positionY)},
                {"rotationDegrees", valueAt(edge->rotationKeyframes,
                    edgeFrame, edge->rotationDegrees)},
                {"clipOpacity", valueAt(edge->opacityKeyframes,
                    edgeFrame, edge->opacityPercent) / 100.0},
                {"opacity", opacity}};
        };
        result.insert("incoming", edgeSource(transition->incomingClipId, true,
            sample.incomingOpacity));
        result.insert("outgoing", edgeSource(transition->outgoingClipId, false,
            sample.outgoingOpacity));
        return result;
    }
    return {};
}

void EditorController::queueThumbnail(const QString &assetId, const QString &mediaPath)
{
    const auto generation = m_projectGeneration;
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this,
        [this, watcher, generation, assetId] {
            if (generation == m_projectGeneration) {
                const QString thumbnail = watcher->result();
                for (auto &asset : m_assets) {
                    if (asset.id == assetId) {
                        asset.thumbnailPath = thumbnail;
                        emit projectChanged();
                        break;
                    }
                }
            }
            watcher->deleteLater();
        });
    watcher->setFuture(QtConcurrent::run([mediaPath] { return generateThumbnail(mediaPath); }));
}

void EditorController::queueTransitionFrames()
{
    const auto clipById = [&](const QString &id) -> const Clip * {
        const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
            [&](const Clip &clip) { return clip.id == id; });
        return it == m_clips.cend() ? nullptr : &*it;
    };
    for (const auto &transition : m_transitions) {
        const auto *definition = m_transitionRegistry.find(transition.definitionId);
        if (!definition || !m_transitionRegistry.usesHeldEdges(definition->renderer)) continue;
        const Clip *outgoing = clipById(transition.outgoingClipId);
        const Clip *incoming = clipById(transition.incomingClipId);
        const auto visuals = visualEdges(m_clips, m_titles);
        const auto *outgoingVisual = findVisualEdge(visuals, transition.outgoingClipId);
        const auto *incomingVisual = findVisualEdge(visuals, transition.incomingClipId);
        if (!outgoingVisual || !incomingVisual
            || outgoingVisual->track != incomingVisual->track
            || outgoingVisual->start + outgoingVisual->duration != incomingVisual->start)
            continue;
        for (const auto &edge : {std::pair<const Clip *, bool>{incoming, true},
                                 std::pair<const Clip *, bool>{outgoing, false}}) {
            if (!edge.first) continue;
            const Asset *asset = findAsset(edge.first->assetId);
            if (!asset || asset->media.isStillImage) continue;
            const qint64 sourceFrame = edge.second ? edge.first->sourceFirstFrame
                : edge.first->sourceFirstFrame
                    + std::max<qint64>(1, edge.first->sourceFrameCount) - 1;
            const QString cachePath = transitionFrameCachePath(asset->media.path,
                sourceFrame, asset->media.frameRate);
            if (QFileInfo::exists(cachePath) || m_pendingTransitionFrames.contains(cachePath))
                continue;
            m_pendingTransitionFrames.insert(cachePath);
            auto *watcher = new QFutureWatcher<QString>(this);
            const auto generation = m_projectGeneration;
            connect(watcher, &QFutureWatcher<QString>::finished, this,
                [this, watcher, cachePath, generation] {
                    m_pendingTransitionFrames.remove(cachePath);
                    if (generation == m_projectGeneration && !watcher->result().isEmpty())
                        emit playheadChanged();
                    watcher->deleteLater();
                });
            const QString path = asset->media.path;
            const FrameRate rate = asset->media.frameRate;
            watcher->setFuture(QtConcurrent::run(&m_transitionFramePool,
                [path, sourceFrame, rate] {
                    return generateTransitionFrame(path, sourceFrame, rate);
                }));
        }
    }
}

void EditorController::queueWaveform(const QString &assetId, const QString &mediaPath)
{
    const auto generation = m_projectGeneration;
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this,
        [this, watcher, generation, assetId] {
            if (generation == m_projectGeneration) {
                const QString waveform = watcher->result();
                for (auto &asset : m_assets) {
                    if (asset.id == assetId) {
                        asset.waveformPath = waveform;
                        emit projectChanged();
                        break;
                    }
                }
            }
            watcher->deleteLater();
        });
    watcher->setFuture(QtConcurrent::run([mediaPath] { return generateWaveform(mediaPath); }));
}

void EditorController::queueFilmstrip(const QString &assetId,
                                      const QString &mediaPath, qint64 durationUs)
{
    const auto generation = m_projectGeneration;
    auto *watcher = new QFutureWatcher<QStringList>(this);
    connect(watcher, &QFutureWatcher<QStringList>::finished, this,
        [this, watcher, generation, assetId] {
            if (generation == m_projectGeneration) {
                const QStringList frames = watcher->result();
                for (auto &asset : m_assets) {
                    if (asset.id == assetId) {
                        asset.filmstripPaths = frames;
                        emit projectChanged();
                        break;
                    }
                }
            }
            watcher->deleteLater();
        });
    watcher->setFuture(QtConcurrent::run([mediaPath, durationUs] {
        return generateFilmstrip(mediaPath, durationUs);
    }));
}

QVariantList EditorController::segments() const
{
    QVariantList result;
    result.reserve(m_segments.size());
    for (const auto &segment : m_segments) {
        const auto *asset = findAsset(segment.source.assetId);
        result.append(QVariantMap{{"id", segment.id}, {"label", segment.label},
                                  {"collectionId", segment.collectionId},
                                  {"tags", segment.tags.join(QStringLiteral(", "))},
                                  {"notes", segment.notes}, {"rating", segment.rating},
                                  {"caption", segment.caption},
                                  {"sourceName", asset ? asset->media.name : QStringLiteral("Missing asset")},
                                  {"firstFrame", segment.source.frames.first},
                                  {"endFrame", segment.source.frames.end},
                                  {"frameCount", segment.source.frames.length()},
                                  {"timelineFrames", segment.timelineFrames},
                                  {"hasVideo", segment.source.includeVideo},
                                  {"hasAudio", segment.source.includeAudio},
                                  {"durationMs", segment.timelineFrames > 0 && segment.timelineRate.valid()
                                      ? rescaleFrames(segment.timelineFrames, segment.timelineRate,
                                                      FrameRate{1000, 1})
                                      : asset ? rescaleFrames(segment.source.frames.length(),
                                          asset->media.frameRate, FrameRate{1000, 1}) : 0},
                                  {"thumbnail", asset && !asset->thumbnailPath.isEmpty()
                                      ? QUrl::fromLocalFile(asset->thumbnailPath) : QUrl{}},
                                  {"waveform", asset && !asset->waveformPath.isEmpty()
                                      ? QUrl::fromLocalFile(asset->waveformPath) : QUrl{}}});
    }
    return result;
}

QVariantList EditorController::collections() const
{
    QVariantList result;
    for (const auto &collection : m_collections) {
        int count = 0;
        for (const auto &segment : m_segments)
            if (segment.collectionId == collection.id) ++count;
        result.append(QVariantMap{{"id", collection.id}, {"name", collection.name},
                                  {"count", count}});
    }
    return result;
}

QVariantList EditorController::titles() const
{
    QVariantList result;
    for (const auto &title : m_titles) {
        QFont font(title.fontFamily);
        font.setPixelSize(title.fontPixels);
        font.setBold(title.bold);
        font.setItalic(title.italic);
        const QString wrappedText = wrappedVisualLines(title.text, font,
            m_exportWidth, m_exportHeight).join(QLatin1Char('\n'));
        result.append(QVariantMap{{"id", title.id}, {"text", title.text},
                                  {"wrappedText", wrappedText},
                                  {"start", title.startFrame},
                                  {"duration", title.durationFrames},
                                  {"fontPixels", title.fontPixels},
                                  {"color", title.color}, {"fontFamily", title.fontFamily},
                                  {"bold", title.bold}, {"italic", title.italic},
                                  {"backgroundColor", title.backgroundColor},
                                  {"backgroundOpacity", title.backgroundOpacity},
                                  {"positionX", title.positionX}, {"positionY", title.positionY},
                                  {"anchorX", title.anchorX}, {"anchorY", title.anchorY},
                                  {"track", title.track}, {"scalePercent", title.scalePercent},
                                  {"positionXKeyframes", keyframesJson(title.positionXKeyframes).toVariantList()},
                                  {"positionYKeyframes", keyframesJson(title.positionYKeyframes).toVariantList()},
                                  {"scaleKeyframes", keyframesJson(title.scaleKeyframes).toVariantList()}});
    }
    return result;
}

QVariantList EditorController::captionRows() const
{
    QVariantList result;
    for (const auto &row : m_captionRows)
        result.append(QVariantMap{{"id", row.id}, {"trackId", row.trackId},
            {"targetTrack", row.targetTrack}, {"sourceClipId", row.sourceClipId},
            {"displayMode", row.displayMode}, {"maxWordsPerCue", row.maxWordsPerCue},
            {"fontFamily", row.fontFamily},
            {"fontPixels", row.fontPixels}, {"bold", row.bold}, {"italic", row.italic},
            {"color", row.color}, {"highlightColor", row.highlightColor},
            {"backgroundColor", row.backgroundColor},
            {"backgroundOpacity", row.backgroundOpacity},
            {"positionX", row.positionX}, {"positionY", row.positionY},
            {"scalePercent", row.scalePercent},
            {"positionXKeyframes", keyframesJson(row.positionXKeyframes).toVariantList()},
            {"positionYKeyframes", keyframesJson(row.positionYKeyframes).toVariantList()},
            {"scaleKeyframes", keyframesJson(row.scaleKeyframes).toVariantList()},
            {"visible", row.visible}});
    return result;
}

QVariantList EditorController::captionCues() const
{
    QVariantList result;
    for (const auto &row : m_captionRows)
        for (const auto &cue : row.cues) {
            if (cue.words.isEmpty()) continue;
            QVariantList words;
            QStringList text;
            for (const auto &word : cue.words) {
                words.append(QVariantMap{{"id", word.id}, {"text", word.text},
                    {"start", word.firstFrame}, {"end", word.endFrame}});
                text.append(word.text);
            }
            result.append(QVariantMap{{"id", cue.id}, {"rowId", row.id},
                {"track", row.trackId}, {"start", cue.words.first().firstFrame},
                {"duration", cue.words.last().endFrame - cue.words.first().firstFrame},
                {"text", text.join(QLatin1Char(' '))}, {"words", words}});
        }
    return result;
}

QStringList EditorController::fontFamilies() const
{
    return QFontDatabase::families();
}

QVariantList EditorController::effects() const
{
    QVariantList result;
    for (const auto &effect : builtinEffects())
    {
        QVariantList parameters;
        for (const auto &parameter : effect.parameters)
            parameters.append(QVariantMap{{"id", parameter.id}, {"name", parameter.name},
                {"minimum", parameter.minimum}, {"maximum", parameter.maximum},
                {"initial", parameter.initial}});
        result.append(QVariantMap{{"id", effect.id}, {"name", effect.name},
                                  {"category", effect.category}, {"parameters", parameters}});
    }
    return result;
}

QVariantList EditorController::audioEffects() const
{
    QVariantList result;
    for (const auto &effect : builtinAudioEffects())
        result.append(QVariantMap{{"id", effect.id}, {"name", effect.name},
                                  {"category", effect.category}});
    return result;
}

bool EditorController::canSaveRange() const
{
    const auto *asset = findAsset(m_selectedAssetId);
    return asset && m_currentRange.valid()
        && m_currentRange.end <= asset->media.frameCount;
}

QString EditorController::rangeUnit() const
{
    const auto *asset = findAsset(m_selectedAssetId);
    return asset && !asset->media.hasVideo ? QStringLiteral("samples")
                                           : QStringLiteral("frames");
}

bool EditorController::frameRuleSatisfied() const
{
    if (!canSaveRange()) return false;
    if (m_frameRule == QStringLiteral("any")) return true;
    const qint64 count = m_currentRange.length();
    const auto match = QRegularExpression(QStringLiteral("^(\\d*)n([+-]\\d+)?$"))
        .match(m_frameRule);
    if (match.hasMatch()) {
        const qint64 step = match.captured(1).isEmpty() ? 1 : match.captured(1).toLongLong();
        const qint64 offset = match.captured(2).isEmpty() ? 0 : match.captured(2).toLongLong();
        return step > 0 && count >= offset && (count - offset) % step == 0;
    }
    return count == m_frameRule.toLongLong();
}

void EditorController::setFrameRule(const QString &rule)
{
    QString cleaned = rule.trimmed().toLower();
    cleaned.remove(QLatin1Char(' '));
    const bool valid = cleaned == QStringLiteral("any")
        || QRegularExpression(QStringLiteral("^[1-9]\\d*$")).match(cleaned).hasMatch()
        || QRegularExpression(QStringLiteral("^(?:[1-9]\\d*)?n(?:[+-]\\d+)?$"))
               .match(cleaned).hasMatch();
    if (!valid) {
        setStatus(QStringLiteral("Use 'any', an exact frame count, or a rule such as 8n+1"));
        return;
    }
    if (m_frameRule == cleaned) return;
    m_frameRule = cleaned;
    m_dirty = true;
    emit projectChanged();
    emit selectionChanged();
}

const Asset *EditorController::findAsset(const QString &id) const
{
    const auto it = std::find_if(m_assets.cbegin(), m_assets.cend(),
                                 [&](const Asset &asset) { return asset.id == id; });
    return it == m_assets.cend() ? nullptr : &*it;
}

QUrl EditorController::previewUrl() const
{
    const auto *asset = findAsset(m_selectedAssetId);
    if (!asset) return {};
    if (m_previewQuality == QStringLiteral("Proxy") && !asset->proxyPath.isEmpty()
        && QFileInfo::exists(asset->proxyPath))
        return QUrl::fromLocalFile(asset->proxyPath);
    return QUrl::fromLocalFile(asset->media.path);
}

qint64 EditorController::previewPositionMs() const
{
    const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
        [&](const Clip &clip) { return clip.id == m_selectedClipId; });
    if (it == m_clips.cend()) return 1000;
    const auto *asset = findAsset(it->assetId);
    if (!asset) return 0;
    const FrameRate sourceRate = asset->media.frameRate;
    try {
        const auto offset = std::clamp<std::int64_t>(m_playheadFrame - it->startFrame, 0,
                                                     it->durationFrames - 1);
        const auto sourceFrame = it->sourceFirstFrame
            + rescaleFrames(offset * it->speedPercent,
                            FrameRate{m_projectRate.numerator * 100,
                                      m_projectRate.denominator}, sourceRate);
        return rescaleFrames(sourceFrame, sourceRate, FrameRate{1000, 1});
    } catch (const std::exception &) {
        return 0;
    }
}

QString EditorController::sourceDetails() const
{
    const auto *asset = findAsset(m_selectedAssetId);
    if (!asset)
        return QStringLiteral("Select media or a timeline clip");
    const auto &m = asset->media;
    if (m.isStillImage)
        return QStringLiteral("Still image\n%1 × %2\n%3")
            .arg(m.width).arg(m.height).arg(m.path);
    if (!m.hasVideo)
        return QStringLiteral("Audio source\n%1 Hz · %2 samples\nDuration: %3 ms\n%4")
            .arg(m.audioSampleRate).arg(m.frameCount).arg(m.durationUs / 1000).arg(m.path);
    return QStringLiteral("%1 × %2\n%3 FPS · %4 source frames\n%5")
        .arg(m.width).arg(m.height).arg(rateText(m.frameRate))
        .arg(m.frameCount).arg(m.path);
}

void EditorController::setStatus(const QString &message)
{
    m_status = message;
    emit statusChanged();
}

void EditorController::importMedia(const QUrl &fileUrl)
{
    importMediaAt(fileUrl, -1, -1, false, 0);
}

void EditorController::importMediaAt(const QUrl &fileUrl, qint64 startFrame,
                                     int track, bool snap, qint64 snapToleranceFrames)
{
    if (!fileUrl.isLocalFile()) {
        setStatus(QStringLiteral("Choose a local media file"));
        return;
    }
    const QString path = fileUrl.toLocalFile();
    const auto generation = m_projectGeneration;
    setStatus(QStringLiteral("Reading %1…").arg(QFileInfo(path).fileName()));
    auto *watcher = new QFutureWatcher<MediaInfo>(this);
    connect(watcher, &QFutureWatcher<MediaInfo>::finished, this,
            [this, watcher, generation, startFrame, track, snap, snapToleranceFrames] {
        if (generation != m_projectGeneration) { watcher->deleteLater(); return; }
        try {
            MediaInfo info = watcher->result();
            const auto existing = std::find_if(m_assets.begin(), m_assets.end(),
                [&](const Asset &asset) { return asset.media.path == info.path; });
            if (existing == m_assets.end()) {
                const QString id = freshId();
                m_assets.append({id, std::move(info)});
                const QString cachedProxy = proxyCachePath(m_assets.last().media.path);
                if (QFileInfo::exists(cachedProxy))
                    m_assets.last().proxyPath = cachedProxy;
                m_dirty = true;
                emit projectChanged();
                selectAsset(id);
                if (m_assets.last().media.hasVideo)
                    queueThumbnail(id, m_assets.last().media.path);
                if (m_assets.last().media.hasVideo)
                    queueFilmstrip(id, m_assets.last().media.path,
                                   m_assets.last().media.durationUs);
                if (m_assets.last().media.hasAudio)
                    queueWaveform(id, m_assets.last().media.path);
                if (startFrame >= 0)
                    placeAssetOnTimeline(id, startFrame, track, snap,
                                         snapToleranceFrames);
                else
                    setStatus(QStringLiteral("Media imported"));
            } else {
                if (std::none_of(m_clips.cbegin(), m_clips.cend(), [&](const Clip &clip) {
                    return clip.assetId == existing->id;
                }) && normalizeMp3CoverArt(existing->media)) {
                    m_dirty = true;
                    emit projectChanged();
                }
                selectAsset(existing->id);
                if (startFrame >= 0)
                    placeAssetOnTimeline(existing->id, startFrame, track, snap,
                                         snapToleranceFrames);
                else
                    setStatus(QStringLiteral("Media already in project"));
            }
        } catch (const std::exception &error) {
            setStatus(QStringLiteral("Import failed: %1").arg(QString::fromUtf8(error.what())));
        }
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([path] { return probeMedia(path); }));
}

void EditorController::relinkAsset(const QString &id, const QUrl &fileUrl)
{
    if (!fileUrl.isLocalFile() || !findAsset(id)) return;
    const QString path = fileUrl.toLocalFile();
    const auto generation = m_projectGeneration;
    setStatus(QStringLiteral("Checking replacement media…"));
    auto *watcher = new QFutureWatcher<MediaInfo>(this);
    connect(watcher, &QFutureWatcher<MediaInfo>::finished, this,
        [this, watcher, generation, id] {
            if (generation != m_projectGeneration) { watcher->deleteLater(); return; }
            try {
                const MediaInfo replacement = watcher->result();
                for (auto &asset : m_assets) {
                    if (asset.id != id) continue;
                    const auto &old = asset.media;
                    if (replacement.hasVideo != old.hasVideo
                        || replacement.hasAudio != old.hasAudio
                        || replacement.frameRate.numerator != old.frameRate.numerator
                        || replacement.frameRate.denominator != old.frameRate.denominator
                        || replacement.frameCount != old.frameCount) {
                        setStatus(QStringLiteral("Replacement does not match the saved streams, frame rate, and frame count"));
                        break;
                    }
                    asset.media = replacement;
                    asset.thumbnailPath.clear();
                    asset.waveformPath.clear();
                    asset.filmstripPaths.clear();
                    asset.proxyPath.clear();
                    const QString cachedProxy = proxyCachePath(replacement.path);
                    if (QFileInfo::exists(cachedProxy)) asset.proxyPath = cachedProxy;
                    m_dirty = true;
                    emit projectChanged();
                    emit selectionChanged();
                    if (replacement.hasVideo) queueThumbnail(id, replacement.path);
                    if (replacement.hasVideo)
                        queueFilmstrip(id, replacement.path, replacement.durationUs);
                    if (replacement.hasAudio) queueWaveform(id, replacement.path);
                    setStatus(QStringLiteral("Media relinked; save the project to keep the new path"));
                    break;
                }
            } catch (const std::exception &error) {
                setStatus(QStringLiteral("Relink failed: %1").arg(QString::fromUtf8(error.what())));
            }
            watcher->deleteLater();
        });
    watcher->setFuture(QtConcurrent::run([path] { return probeMedia(path); }));
}

void EditorController::relinkFolder(const QUrl &folderUrl)
{
    if (!folderUrl.isLocalFile()) return;
    const QDir folder(folderUrl.toLocalFile());
    int matches = 0;
    for (const auto &asset : m_assets) {
        if (QFileInfo::exists(asset.media.path)) continue;
        const QString candidate = folder.filePath(QFileInfo(asset.media.path).fileName());
        if (QFileInfo::exists(candidate)) {
            relinkAsset(asset.id, QUrl::fromLocalFile(candidate));
            ++matches;
        }
    }
    if (matches == 0) setStatus(QStringLiteral("No missing media filenames found in that folder"));
}

void EditorController::selectAsset(const QString &id)
{
    const auto *asset = findAsset(id);
    if (!asset) return;
    pauseTimelinePlayback();
    m_timelinePreviewEnabled = false;
    m_effectPreviewTimer.stop();
    m_previewEffectId.clear();
    emit playbackChanged();
    m_selectedAssetId = id;
    m_selectedClipId.clear();
    m_selectedTitleId.clear();
    m_selectedCaptionId.clear();
    m_selectedCaptionRowId.clear();
    m_selectedTransitionId.clear();
    m_currentRange = {0, asset->media.frameCount};
    emit selectionChanged();
}

void EditorController::clearSelection()
{
    if (m_selectedAssetId.isEmpty() && m_selectedClipId.isEmpty()
        && m_selectedTitleId.isEmpty() && m_selectedCaptionId.isEmpty()
        && m_selectedCaptionRowId.isEmpty() && m_selectedTransitionId.isEmpty()) return;
    m_selectedAssetId.clear();
    m_selectedClipId.clear();
    m_selectedTitleId.clear();
    m_selectedCaptionId.clear();
    m_selectedCaptionRowId.clear();
    m_selectedTransitionId.clear();
    m_currentRange = {};
    emit selectionChanged();
}

void EditorController::selectClip(const QString &id)
{
    selectClipWithoutSeek(id);
    const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
                                 [&](const Clip &clip) { return clip.id == id; });
    if (it != m_clips.cend() && (!m_timelinePreviewEnabled || m_playheadFrame < it->startFrame
        || m_playheadFrame >= it->startFrame + it->durationFrames))
        seekTimelineFrame(it->startFrame);
}

void EditorController::selectClipWithoutSeek(const QString &id)
{
    const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
                                 [&](const Clip &clip) { return clip.id == id; });
    if (it == m_clips.cend()) return;
    m_selectedClipId = id;
    m_selectedTitleId.clear();
    m_selectedCaptionId.clear();
    m_selectedCaptionRowId.clear();
    m_selectedTransitionId.clear();
    m_selectedAssetId = it->assetId;
    const auto *asset = findAsset(m_selectedAssetId);
    m_currentRange = {0, asset ? asset->media.frameCount : 0};
    emit selectionChanged();
}

QVariantMap EditorController::selectedVoiceSource() const
{
    const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
        [&](const Clip &clip) { return clip.id == m_selectedClipId; });
    if (it == m_clips.cend() || !it->includeAudio) return {};
    const auto *asset = findAsset(it->assetId);
    if (!asset || !asset->media.hasAudio || !QFileInfo::exists(asset->media.path)) return {};
    const double rate = double(asset->media.frameRate.numerator)
        / double(asset->media.frameRate.denominator);
    return {{"clipId", it->id}, {"path", asset->media.path},
            {"startSeconds", it->sourceFirstFrame / rate},
            {"durationSeconds", it->sourceFrameCount / rate}};
}

void EditorController::applyVoiceChange(const QString &clipId, const QString &path,
                                        const QString &voiceName, const QString &mode)
{
    const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
        [&](const Clip &clip) { return clip.id == clipId; });
    if (it == m_clips.cend() || !it->includeAudio || !QFileInfo::exists(path)) return;
    Clip changed = *it;
    changed.voiceChangePath = QFileInfo(path).absoluteFilePath();
    changed.voiceName = voiceName;
    changed.voiceMode = mode;
    changed.voiceSourceFirstFrame = it->sourceFirstFrame;
    changed.voiceSourceFrameCount = it->sourceFrameCount;
    changed.voiceChangeEnabled = true;
    replaceClip(*it, changed, QStringLiteral("Voice change applied"));
}

void EditorController::setVoiceChangeEnabled(const QString &clipId, bool enabled)
{
    const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
        [&](const Clip &clip) { return clip.id == clipId; });
    if (it == m_clips.cend() || it->voiceChangePath.isEmpty()
        || !QFileInfo::exists(it->voiceChangePath) || it->voiceChangeEnabled == enabled) return;
    if (enabled && (it->sourceFirstFrame < it->voiceSourceFirstFrame
        || it->sourceFirstFrame + it->sourceFrameCount
            > it->voiceSourceFirstFrame + it->voiceSourceFrameCount)) {
        setStatus(QStringLiteral("This clip extends outside the changed voice audio; generate it again"));
        return;
    }
    Clip changed = *it;
    changed.voiceChangeEnabled = enabled;
    replaceClip(*it, changed, enabled ? QStringLiteral("Voice change enabled")
                                       : QStringLiteral("Voice change disabled"));
}

void EditorController::startAutoCaption()
{
    const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
        [&](const Clip &clip) { return clip.id == m_selectedClipId; });
    if (it == m_clips.cend()) return;
    const auto *asset = findAsset(it->assetId);
    if (!asset || !asset->media.hasAudio || !it->includeAudio || !QFileInfo::exists(asset->media.path)) {
        setStatus(QStringLiteral("Select a clip with available audio first"));
        return;
    }
    const double sourceRate = double(asset->media.frameRate.numerator)
        / asset->media.frameRate.denominator;
    emit autoCaptionRequested(it->id, asset->media.path,
        it->sourceFirstFrame / sourceRate, it->sourceFrameCount / sourceRate);
}

void EditorController::acceptGeneratedCaptions(const QString &clipId, const QVariantList &words)
{
    const auto clip = std::find_if(m_clips.cbegin(), m_clips.cend(),
        [&](const Clip &item) { return item.id == clipId; });
    if (clip == m_clips.cend() || words.isEmpty()) return;
    CaptionRow row;
    row.id = freshId();
    row.sourceClipId = clipId;
    row.targetTrack = clip->track;
    row.trackId = 1000;
    for (const auto &existing : m_captionRows) row.trackId = std::max(row.trackId, existing.trackId + 1);
    const double fps = double(m_projectRate.numerator) / m_projectRate.denominator;
    const double timelineFramesPerSourceSecond = fps * 100.0 / clip->speedPercent;
    const qint64 clipEnd = clip->startFrame + clip->durationFrames;
    CaptionCue cue{freshId(), {}};
    for (const auto &entry : words) {
        const auto map = entry.toMap();
        const QString value = map.value(QStringLiteral("text")).toString().trimmed();
        if (value.isEmpty()) continue;
        const qint64 first = std::clamp(clip->startFrame
            + qint64(std::floor(map.value(QStringLiteral("start")).toDouble()
                * timelineFramesPerSourceSecond)),
            clip->startFrame, clipEnd - 1);
        const qint64 end = std::clamp(clip->startFrame
            + qint64(std::ceil(map.value(QStringLiteral("end")).toDouble()
                * timelineFramesPerSourceSecond)),
            first + 1, clipEnd);
        if (!cue.words.isEmpty() && (first - cue.words.last().endFrame > qint64(fps * 0.45)
            || cue.words.size() >= 10 || cue.words.last().text.endsWith(QLatin1Char('.'))
            || cue.words.last().text.endsWith(QLatin1Char('?'))
            || cue.words.last().text.endsWith(QLatin1Char('!')))) {
            cue.groupId = cue.id;
            row.cues.append(cue);
            cue = {freshId(), {}};
        }
        cue.words.append({freshId(), value, first, end});
    }
    if (!cue.words.isEmpty()) {
        cue.groupId = cue.id;
        row.cues.append(cue);
    }
    if (row.cues.isEmpty()) return;
    Edit edit{Edit::ReplaceCaptions};
    edit.previousCaptionRows = m_captionRows;
    m_captionRows.erase(std::remove_if(m_captionRows.begin(), m_captionRows.end(),
        [&](const CaptionRow &existing) { return existing.sourceClipId == clipId; }), m_captionRows.end());
    m_captionRows.append(row);
    edit.resultingCaptionRows = m_captionRows;
    m_undo.append(edit);
    m_redo.clear();
    m_selectedCaptionRowId = row.id;
    m_selectedCaptionId = row.cues.first().id;
    m_selectedClipId.clear();
    m_selectedTitleId.clear();
    m_selectedTransitionId.clear();
    m_dirty = true;
    emit projectChanged();
    emit selectionChanged();
    setStatus(QStringLiteral("Captions added on a new row"));
}

void EditorController::selectCaption(const QString &cueId)
{
    for (const auto &row : m_captionRows)
        for (const auto &cue : row.cues)
            if (cue.id == cueId) {
                  m_selectedCaptionId = cueId;
                  m_selectedCaptionRowId = row.id;
                  m_selectedClipId.clear();
                  m_selectedTitleId.clear();
                  m_selectedTransitionId.clear();
                emit selectionChanged();
                return;
            }
}

void EditorController::selectCaptionRow(const QString &rowId)
{
    if (std::none_of(m_captionRows.cbegin(), m_captionRows.cend(),
        [&](const CaptionRow &row) { return row.id == rowId; })) return;
    m_selectedCaptionRowId = rowId;
    m_selectedCaptionId.clear();
    m_selectedClipId.clear();
    m_selectedTitleId.clear();
    m_selectedTransitionId.clear();
    emit selectionChanged();
}

void EditorController::selectTrackRow(int track)
{
    for (const auto &row : m_captionRows)
        if (row.trackId == track) {
            selectCaptionRow(row.id);
            return;
        }
    if (!isVideoTrack(track) && !isAudioTrack(track)) return;
    if (m_selectedCaptionId.isEmpty() && m_selectedCaptionRowId.isEmpty()) return;
    m_selectedCaptionId.clear();
    m_selectedCaptionRowId.clear();
    emit selectionChanged();
}

void EditorController::setCaptionMode(const QString &rowId, const QString &mode)
{
    if (!captionStyle(mode)) return;
    auto it = std::find_if(m_captionRows.begin(), m_captionRows.end(),
        [&](const CaptionRow &row) { return row.id == rowId; });
    if (it == m_captionRows.end() || it->displayMode == mode) return;
    Edit edit{Edit::ReplaceCaptions};
    edit.previousCaptionRows = m_captionRows;
    it->displayMode = mode;
    edit.resultingCaptionRows = m_captionRows;
    m_undo.append(edit); m_redo.clear(); m_dirty = true;
    emit projectChanged();
}

void EditorController::setCaptionMaxWords(const QString &rowId, int maxWords)
{
    if (maxWords != -1 && (maxWords < 1 || maxWords > 1000)) return;
    auto it = std::find_if(m_captionRows.begin(), m_captionRows.end(),
        [&](const CaptionRow &row) { return row.id == rowId; });
    if (it == m_captionRows.end() || it->maxWordsPerCue == maxWords) return;
    QString selectedWordId;
    if (m_selectedCaptionRowId == rowId)
        for (const auto &cue : it->cues)
            if (cue.id == m_selectedCaptionId && !cue.words.isEmpty()) {
                selectedWordId = cue.words.first().id;
                break;
            }
    Edit edit{Edit::ReplaceCaptions};
    edit.previousCaptionRows = m_captionRows;
    it->maxWordsPerCue = maxWords;
    resegmentCaptionRow(*it);
    if (!selectedWordId.isEmpty())
        for (const auto &cue : it->cues)
            if (std::any_of(cue.words.cbegin(), cue.words.cend(),
                [&](const CaptionWord &word) { return word.id == selectedWordId; })) {
                m_selectedCaptionId = cue.id;
                break;
            }
    edit.resultingCaptionRows = m_captionRows;
    m_undo.append(edit); m_redo.clear(); m_dirty = true;
    emit projectChanged();
    emit selectionChanged();
    setStatus(maxWords < 0 ? QStringLiteral("Original caption grouping restored")
        : QStringLiteral("Captions grouped in up to %1 words").arg(maxWords));
}

QVariantList EditorController::captionDisplayStyles() const
{
    QVariantList result;
    for (const auto &style : captionStyles)
        result.append(QVariantMap{{"id", QLatin1String(style.id)},
            {"name", QLatin1String(style.name)},
            {"category", QLatin1String(style.category)},
            {"description", QLatin1String(style.description)},
            {"treatment", QLatin1String(style.treatment)}});
    return result;
}

void EditorController::previewCaptionStyle(const QString &rowId, const QString &mode)
{
    if (!captionStyle(mode)) return;
    const auto row = std::find_if(m_captionRows.cbegin(), m_captionRows.cend(),
        [&](const CaptionRow &item) { return item.id == rowId; });
    if (row == m_captionRows.cend() || row->cues.isEmpty()) return;
    pauseTimelinePlayback();
    qint64 startFrame = -1;
    qint64 nearest = std::numeric_limits<qint64>::max();
    for (const auto &cue : row->cues) {
        if (cue.words.isEmpty()) continue;
        const qint64 first = cue.words.first().firstFrame;
        const qint64 end = cue.words.last().endFrame;
        if (m_playheadFrame >= first && m_playheadFrame < end) {
            startFrame = m_playheadFrame;
            break;
        }
        const qint64 distance = m_playheadFrame < first
            ? first - m_playheadFrame : m_playheadFrame - end;
        if (distance < nearest) { nearest = distance; startFrame = first; }
    }
    if (startFrame < 0) return;
    m_timelinePreviewEnabled = true;
    m_effectPreviewFrame = startFrame;
    m_previewCaptionRowId = rowId;
    m_previewCaptionMode = mode;
    m_previewEffectId.clear();
    m_previewEffectTrack = -1;
    m_playbackAnchorFrame = startFrame;
    m_playbackClock.restart();
    m_timelinePlaying = true;
    m_playbackTick.start();
    emit playbackChanged();
    emit playheadChanged();
    m_effectPreviewTimer.start();
    setStatus(QStringLiteral("Previewing caption style"));
}

void EditorController::toggleCaptionVisibility(const QString &rowId)
{
    auto it = std::find_if(m_captionRows.begin(), m_captionRows.end(),
        [&](const CaptionRow &row) { return row.id == rowId; });
    if (it == m_captionRows.end()) return;
    Edit edit{Edit::ReplaceCaptions}; edit.previousCaptionRows = m_captionRows;
    it->visible = !it->visible;
    edit.resultingCaptionRows = m_captionRows;
    m_undo.append(edit); m_redo.clear(); m_dirty = true;
    emit projectChanged();
}

void EditorController::updateCaptionStyle(const QString &rowId, const QString &fontFamily,
    int fontPixels, bool bold, bool italic, const QString &color,
    const QString &highlightColor, const QString &backgroundColor,
    int backgroundOpacity, int positionX, int positionY)
{
    auto it = std::find_if(m_captionRows.begin(), m_captionRows.end(),
        [&](const CaptionRow &row) { return row.id == rowId; });
    if (it == m_captionRows.end() || fontPixels < 12 || fontPixels > 300
        || !QColor(color).isValid() || !QColor(highlightColor).isValid()
        || !QColor(backgroundColor).isValid()) return;
    Edit edit{Edit::ReplaceCaptions}; edit.previousCaptionRows = m_captionRows;
    it->fontFamily = fontFamily.trimmed().isEmpty() ? QStringLiteral("Segoe UI") : fontFamily.trimmed();
    it->fontPixels = fontPixels; it->bold = bold; it->italic = italic;
    it->color = color; it->highlightColor = highlightColor;
    it->backgroundColor = backgroundColor;
    it->backgroundOpacity = std::clamp(backgroundOpacity, 0, 100);
    it->positionX = std::clamp(positionX, -200, 300);
    it->positionY = std::clamp(positionY, -200, 300);
    edit.resultingCaptionRows = m_captionRows;
    m_undo.append(edit); m_redo.clear(); m_dirty = true;
    emit projectChanged();
}

QVariantMap EditorController::captionVisualAt(const QString &rowId, qint64 frame) const
{
    const auto it = std::find_if(m_captionRows.cbegin(), m_captionRows.cend(),
        [&](const CaptionRow &row) { return row.id == rowId; });
    if (it == m_captionRows.cend()) return {};
    return {{"x", valueAt(it->positionXKeyframes, frame, it->positionX)},
            {"y", valueAt(it->positionYKeyframes, frame, it->positionY)},
            {"scale", valueAt(it->scaleKeyframes, frame, it->scalePercent)},
            {"keyframe", std::any_of(it->scaleKeyframes.cbegin(), it->scaleKeyframes.cend(),
                [frame](const ValueKeyframe &point) { return point.frame == frame; })}};
}

void EditorController::setCaptionVisual(const QString &rowId, int x, int y, int scale)
{
    auto it = std::find_if(m_captionRows.begin(), m_captionRows.end(),
        [&](const CaptionRow &row) { return row.id == rowId; });
    if (it == m_captionRows.end() || scale < 25 || scale > 400) return;
    Edit edit{Edit::ReplaceCaptions}; edit.previousCaptionRows = m_captionRows;
    it->positionX = std::clamp(x, -200, 300);
    it->positionY = std::clamp(y, -200, 300);
    it->scalePercent = scale;
    edit.resultingCaptionRows = m_captionRows;
    m_undo.append(edit); m_redo.clear(); m_dirty = true;
    emit projectChanged();
}

void EditorController::setCaptionVisualKeyframe(const QString &rowId, qint64 frame,
                                                 int x, int y, int scale)
{
    auto it = std::find_if(m_captionRows.begin(), m_captionRows.end(),
        [&](const CaptionRow &row) { return row.id == rowId; });
    if (it == m_captionRows.end() || frame < 0 || scale < 25 || scale > 400) return;
    Edit edit{Edit::ReplaceCaptions}; edit.previousCaptionRows = m_captionRows;
    upsertKeyframe(it->positionXKeyframes, frame, std::clamp(x, -200, 300));
    upsertKeyframe(it->positionYKeyframes, frame, std::clamp(y, -200, 300));
    upsertKeyframe(it->scaleKeyframes, frame, scale);
    edit.resultingCaptionRows = m_captionRows;
    m_undo.append(edit); m_redo.clear(); m_dirty = true;
    emit projectChanged();
}

void EditorController::removeCaptionVisualKeyframe(const QString &rowId, qint64 frame)
{
    auto it = std::find_if(m_captionRows.begin(), m_captionRows.end(),
        [&](const CaptionRow &row) { return row.id == rowId; });
    if (it == m_captionRows.end()) return;
    Edit edit{Edit::ReplaceCaptions}; edit.previousCaptionRows = m_captionRows;
    eraseKeyframe(it->positionXKeyframes, frame);
    eraseKeyframe(it->positionYKeyframes, frame);
    eraseKeyframe(it->scaleKeyframes, frame);
    edit.resultingCaptionRows = m_captionRows;
    m_undo.append(edit); m_redo.clear(); m_dirty = true;
    emit projectChanged();
}

void EditorController::updateCaptionWord(const QString &cueId, const QString &wordId,
    const QString &text, qint64 firstFrame, qint64 endFrame)
{
    const QString cleaned = text.trimmed();
    if (cleaned.isEmpty() || firstFrame < 0 || endFrame <= firstFrame) return;
    for (auto &row : m_captionRows) {
        for (auto &cue : row.cues) {
            if (cue.id != cueId) continue;
            const auto found = std::find_if(cue.words.cbegin(), cue.words.cend(),
                [&](const CaptionWord &word) { return word.id == wordId; });
            if (found == cue.words.cend()) return;
            if (found->text == cleaned && found->firstFrame == firstFrame
                && found->endFrame == endFrame) return;
            const auto clip = std::find_if(m_clips.cbegin(), m_clips.cend(),
                [&](const Clip &item) { return item.id == row.sourceClipId; });
            if (clip != m_clips.cend()
                && (firstFrame < clip->startFrame
                    || endFrame > clip->startFrame + clip->durationFrames)) {
                setStatus(QStringLiteral("Word timing must stay inside the source clip"));
                return;
            }
            if (firstFrame != found->firstFrame || endFrame != found->endFrame) {
                for (const auto &other : cue.words) {
                    if (other.id == wordId) continue;
                    if (firstFrame < other.endFrame && endFrame > other.firstFrame) {
                        setStatus(QStringLiteral("Word timing overlaps another word"));
                        return;
                    }
                }
            }
            Edit edit{Edit::ReplaceCaptions};
            edit.previousCaptionRows = m_captionRows;
            auto &word = cue.words[std::distance(cue.words.cbegin(), found)];
            word.text = cleaned;
            word.firstFrame = firstFrame;
            word.endFrame = endFrame;
            std::stable_sort(cue.words.begin(), cue.words.end(),
                [](const CaptionWord &a, const CaptionWord &b) { return a.firstFrame < b.firstFrame; });
            edit.resultingCaptionRows = m_captionRows;
            m_undo.append(edit);
            m_redo.clear();
            m_dirty = true;
            emit projectChanged();
            return;
        }
    }
}

void EditorController::insertCaptionWord(const QString &cueId, const QString &afterWordId,
                                         const QString &text)
{
    const QString cleaned = text.trimmed();
    if (cleaned.isEmpty()) return;
    for (auto &row : m_captionRows) {
        for (auto &cue : row.cues) {
            if (cue.id != cueId || cue.words.isEmpty()) continue;
            qsizetype position = 0;
            if (!afterWordId.isEmpty()) {
                const auto previous = std::find_if(cue.words.cbegin(), cue.words.cend(),
                    [&](const CaptionWord &word) { return word.id == afterWordId; });
                if (previous == cue.words.cend()) return;
                position = std::distance(cue.words.cbegin(), previous) + 1;
            }
            const auto clip = std::find_if(m_clips.cbegin(), m_clips.cend(),
                [&](const Clip &item) { return item.id == row.sourceClipId; });
            const qint64 clipFirst = clip == m_clips.cend() ? 0 : clip->startFrame;
            const qint64 clipEnd = clip == m_clips.cend()
                ? std::max<qint64>(cue.words.last().endFrame + 1, timelineDurationFrames())
                : clip->startFrame + clip->durationFrames;
            qint64 first = position == 0 ? clipFirst : cue.words[position - 1].endFrame;
            qint64 end = position == cue.words.size() ? clipEnd : cue.words[position].firstFrame;
            Edit edit{Edit::ReplaceCaptions};
            edit.previousCaptionRows = m_captionRows;
            if (end > first) {
                const qint64 length = std::max<qint64>(1, framesForSeconds(0.35));
                if (position == 0) first = std::max(first, end - length);
                else end = std::min(end, first + length);
            } else if (position > 0
                       && cue.words[position - 1].endFrame - cue.words[position - 1].firstFrame >= 2) {
                auto &previous = cue.words[position - 1];
                first = previous.firstFrame + (previous.endFrame - previous.firstFrame) / 2;
                end = previous.endFrame;
                previous.endFrame = first;
            } else if (position < cue.words.size()
                       && cue.words[position].endFrame - cue.words[position].firstFrame >= 2) {
                auto &next = cue.words[position];
                first = next.firstFrame;
                end = first + (next.endFrame - first) / 2;
                next.firstFrame = end;
            } else if (cue.words.last().endFrame < clipEnd) {
                for (qsizetype i = cue.words.size(); i > position; --i) {
                    cue.words[i - 1].firstFrame++;
                    cue.words[i - 1].endFrame++;
                }
                end = first + 1;
            } else {
                setStatus(QStringLiteral("No frame is available here; extend the clip or adjust nearby word timing"));
                return;
            }
            const QString insertedWordId = freshId();
            cue.words.insert(position, {insertedWordId, cleaned, first, end});
            if (row.maxWordsPerCue > 0) {
                resegmentCaptionRow(row);
                for (const auto &fragment : row.cues)
                    if (std::any_of(fragment.words.cbegin(), fragment.words.cend(),
                        [&](const CaptionWord &word) { return word.id == insertedWordId; })) {
                        m_selectedCaptionId = fragment.id;
                        break;
                    }
            }
            edit.resultingCaptionRows = m_captionRows;
            m_undo.append(edit);
            m_redo.clear();
            m_dirty = true;
            emit projectChanged();
            emit selectionChanged();
            setStatus(QStringLiteral("Caption word inserted"));
            return;
        }
    }
}

void EditorController::removeCaptionWord(const QString &cueId, const QString &wordId)
{
    for (auto &row : m_captionRows) {
        for (qsizetype cueIndex = 0; cueIndex < row.cues.size(); ++cueIndex) {
            auto &cue = row.cues[cueIndex];
            if (cue.id != cueId) continue;
            const auto word = std::find_if(cue.words.cbegin(), cue.words.cend(),
                [&](const CaptionWord &item) { return item.id == wordId; });
            if (word == cue.words.cend()) return;
            const QString groupId = cue.groupId.isEmpty() ? cue.id : cue.groupId;
            Edit edit{Edit::ReplaceCaptions};
            edit.previousCaptionRows = m_captionRows;
            cue.words.erase(word);
            if (cue.words.isEmpty()) {
                row.cues.removeAt(cueIndex);
                m_selectedCaptionId.clear();
                emit selectionChanged();
            }
            if (row.maxWordsPerCue > 0) {
                resegmentCaptionRow(row);
                if (m_selectedCaptionRowId == row.id && !m_selectedCaptionId.isEmpty()
                    && std::none_of(row.cues.cbegin(), row.cues.cend(),
                        [&](const CaptionCue &fragment) { return fragment.id == m_selectedCaptionId; })) {
                    const auto replacement = std::find_if(row.cues.cbegin(), row.cues.cend(),
                        [&](const CaptionCue &fragment) { return fragment.groupId == groupId; });
                    m_selectedCaptionId = replacement == row.cues.cend() ? QString{} : replacement->id;
                    emit selectionChanged();
                }
            }
            edit.resultingCaptionRows = m_captionRows;
            m_undo.append(edit);
            m_redo.clear();
            m_dirty = true;
            emit projectChanged();
            return;
        }
    }
}

void EditorController::removeSelectedCaption()
{
    if (m_selectedCaptionId.isEmpty() && m_selectedCaptionRowId.isEmpty()) return;
    Edit edit{Edit::ReplaceCaptions}; edit.previousCaptionRows = m_captionRows;
    if (!m_selectedCaptionId.isEmpty()) {
        for (auto &row : m_captionRows)
            row.cues.erase(std::remove_if(row.cues.begin(), row.cues.end(),
                [&](const CaptionCue &cue) { return cue.id == m_selectedCaptionId; }), row.cues.end());
    } else m_captionRows.erase(std::remove_if(m_captionRows.begin(), m_captionRows.end(),
        [&](const CaptionRow &row) { return row.id == m_selectedCaptionRowId; }), m_captionRows.end());
    edit.resultingCaptionRows = m_captionRows;
    m_undo.append(edit); m_redo.clear(); m_dirty = true;
    m_selectedCaptionId.clear(); m_selectedCaptionRowId.clear();
    emit projectChanged(); emit selectionChanged();
}

QVariantMap EditorController::captionDisplayForRow(const QString &rowId, qint64 frame) const
{
    for (const auto &row : m_captionRows) {
        if (row.id != rowId || !row.visible) continue;
        for (const auto &cue : row.cues) {
            const QString mode = rowId == m_previewCaptionRowId && m_effectPreviewFrame >= 0
                ? m_previewCaptionMode : row.displayMode;
            const CaptionFrame state = captionFrame(row, cue, frame, mode);
            if (state.words.isEmpty()) continue;
            const QStringList &text = state.words;
            const QString joined = text.join(QLatin1Char(' '));
            QFont font(row.fontFamily);
            font.setPixelSize(row.fontPixels);
            font.setBold(row.bold);
            font.setItalic(row.italic);
            QVector<int> lineStarts;
            const QString wrapped = wrappedVisualLines(joined, font,
                m_exportWidth, m_exportHeight, &lineStarts).join(QLatin1Char('\n'));
            QVariantList breaks;
            int charOffset = 0;
            int nextLine = 1;
            for (int i = 0; i < text.size(); ++i) {
                if (nextLine < lineStarts.size() && charOffset >= lineStarts[nextLine]) {
                    breaks.append(i);
                    ++nextLine;
                }
                charOffset += text[i].size() + 1;
            }
            return {{"text", joined}, {"wrappedText", wrapped},
                {"lineBreakBefore", breaks}, {"highlight", state.highlight},
                {"words", text}, {"mode", mode}, {"treatment", state.treatment},
                {"opacity", state.opacity}};
        }
    }
    return {};
}

void EditorController::addTitle(const QString &text)
{
    addTitleAt(text, m_playheadFrame, 3);
}

void EditorController::addTitleAt(const QString &text, qint64 frame, int track)
{
    placeTitleAt(text, frame, track, false, 0);
}

void EditorController::placeTitleAt(const QString &text, qint64 frame, int track,
                                    bool snap, qint64 snapToleranceFrames)
{
    const QString cleaned = text.trimmed();
    if (cleaned.isEmpty() || frame < 0 || (track != -1 && !isVideoTrack(track))) return;
    TitleClip title{freshId(), cleaned, frame,
                    std::max<qint64>(1, framesForSeconds(5)), 88, QStringLiteral("#ffffff")};
    track = resolveNewTrack(track);
    if (track < 0) return;
    if (snap) frame = snappedStartFrame(frame, title.durationFrames, track,
                                        snapToleranceFrames);
    const auto placement = timelinePlacement(frame, title.durationFrames, track, {}, frame);
    title.startFrame = placement.startFrame;
    title.track = track;
    Edit edit; edit.kind = Edit::PlaceTimelineItems;
    applyTimelinePlacement(placement, track, {}, edit);
    m_titles.append(title);
    edit.resultingTitles.append(title);
    m_selectedTitleId = title.id;
    m_selectedClipId.clear();
    finishTimelinePlacement(std::move(edit));
    emit selectionChanged();
    setStatus(QStringLiteral("Text added to timeline"));
}

QVariantMap EditorController::previewTitleSnap(const QString &id, qint64 frame,
                                               int track, bool snap,
                                               qint64 snapToleranceFrames,
                                               qint64 dropFrame) const
{
    const auto it = std::find_if(m_titles.cbegin(), m_titles.cend(),
        [&](const TitleClip &title) { return title.id == id; });
    if (it == m_titles.cend()) return {{"start", std::max<qint64>(0, frame)}, {"guide", -1}};
    const auto snapped = snap ? snapPlacement(std::max<qint64>(0, frame),
        it->durationFrames, track, snapToleranceFrames, id)
        : SnapPlacement{std::max<qint64>(0, frame), -1};
    const auto placement = timelinePlacement(snapped.startFrame,
        it->durationFrames, track, id, dropFrame);
    return {{"start", placement.startFrame},
            {"guide", placement.startFrame == snapped.startFrame ? snapped.guideFrame : -1}};
}

QVariantMap EditorController::previewNewTitleSnap(qint64 frame, int track,
                                                   bool snap,
                                                   qint64 snapToleranceFrames) const
{
    const qint64 duration = std::max<qint64>(1, framesForSeconds(5));
    const auto snapped = snap ? snapPlacement(std::max<qint64>(0, frame),
        duration, track, snapToleranceFrames)
        : SnapPlacement{std::max<qint64>(0, frame), -1};
    const auto placement = timelinePlacement(snapped.startFrame,
        duration, track, {}, frame);
    return {{"start", placement.startFrame},
            {"guide", placement.startFrame == snapped.startFrame ? snapped.guideFrame : -1}};
}

QVariantMap EditorController::previewTitleTrimSnap(const QString &id, qint64 edgeFrame,
                                                    bool startEdge, qint64 toleranceFrames) const
{
    const auto result = snapTrimEdge(id, edgeFrame, startEdge,
                                     std::max<qint64>(0, toleranceFrames));
    return {{"frame", result.startFrame}, {"guide", result.guideFrame}};
}

void EditorController::trimTitleStartSnapped(const QString &id, qint64 startFrame,
                                             bool snap, qint64 toleranceFrames)
{
    const auto it = std::find_if(m_titles.begin(), m_titles.end(),
        [&](const TitleClip &title) { return title.id == id; });
    if (it == m_titles.end()) return;
    if (snap) startFrame = snapTrimEdge(id, startFrame, true,
        std::max<qint64>(0, toleranceFrames)).startFrame;
    const qint64 end = it->startFrame + it->durationFrames;
    startFrame = std::clamp<qint64>(startFrame, 0, end - 1);
    if (startFrame < it->startFrame) {
        for (const auto &clip : m_clips)
            if (clip.track == it->track && clip.startFrame + clip.durationFrames <= it->startFrame)
                startFrame = std::max(startFrame, clip.startFrame + clip.durationFrames);
        for (const auto &title : m_titles)
            if (title.id != id && title.track == it->track
                && title.startFrame + title.durationFrames <= it->startFrame)
                startFrame = std::max(startFrame, title.startFrame + title.durationFrames);
    }
    if (startFrame == it->startFrame) return;
    TitleClip changed = *it;
    changed.startFrame = startFrame;
    changed.durationFrames = end - startFrame;
    const qint64 removed = startFrame - it->startFrame;
    const auto trimPoints = [removed, &changed](const QVector<ValueKeyframe> &source,
                                                int baseline, QVector<ValueKeyframe> &target) {
        if (source.isEmpty()) return;
        target.clear();
        target.append({0, removed > 0 ? valueAt(source, removed, baseline) : baseline});
        if (removed < 0) target.append({-removed, valueAt(source, 0, baseline)});
        for (const auto &point : source)
            if (point.frame > removed && point.frame - removed < changed.durationFrames)
                if (target.isEmpty() || target.last().frame != point.frame - removed)
                    target.append({point.frame - removed, point.value});
    };
    trimPoints(it->positionXKeyframes, it->positionX, changed.positionXKeyframes);
    trimPoints(it->positionYKeyframes, it->positionY, changed.positionYKeyframes);
    trimPoints(it->scaleKeyframes, it->scalePercent, changed.scaleKeyframes);
    Edit edit{Edit::ReplaceTitle, {}, {}, {}, it - m_titles.begin()};
    edit.title = *it; edit.extraTitle = changed;
    m_undo.append(edit); m_redo.clear(); *it = changed; m_dirty = true;
    emit projectChanged();
    setStatus(QStringLiteral("Text start trimmed"));
}

void EditorController::trimTitleEndSnapped(const QString &id, qint64 endFrame,
                                           bool snap, qint64 toleranceFrames)
{
    const auto it = std::find_if(m_titles.begin(), m_titles.end(),
        [&](const TitleClip &title) { return title.id == id; });
    if (it == m_titles.end()) return;
    if (snap) endFrame = snapTrimEdge(id, endFrame, false,
        std::max<qint64>(0, toleranceFrames)).startFrame;
    endFrame = std::max(it->startFrame + 1, endFrame);
    const qint64 originalEnd = it->startFrame + it->durationFrames;
    if (endFrame > originalEnd) {
        for (const auto &clip : m_clips)
            if (clip.track == it->track && clip.startFrame >= originalEnd)
                endFrame = std::min(endFrame, clip.startFrame);
        for (const auto &title : m_titles)
            if (title.id != id && title.track == it->track && title.startFrame >= originalEnd)
                endFrame = std::min(endFrame, title.startFrame);
    }
    if (endFrame == originalEnd) return;
    TitleClip changed = *it;
    changed.durationFrames = endFrame - it->startFrame;
    for (auto *points : {&changed.positionXKeyframes, &changed.positionYKeyframes,
                         &changed.scaleKeyframes})
        points->erase(std::remove_if(points->begin(), points->end(),
            [&changed](const ValueKeyframe &point) {
                return point.frame >= changed.durationFrames;
            }), points->end());
    Edit edit{Edit::ReplaceTitle, {}, {}, {}, it - m_titles.begin()};
    edit.title = *it; edit.extraTitle = changed;
    m_undo.append(edit); m_redo.clear(); *it = changed; m_dirty = true;
    emit projectChanged();
    setStatus(QStringLiteral("Text end trimmed"));
}

void EditorController::moveTitle(const QString &id, qint64 frame, int track,
                                 bool snap, qint64 snapToleranceFrames,
                                 qint64 dropFrame)
{
    const auto it = std::find_if(m_titles.begin(), m_titles.end(),
        [&](const TitleClip &title) { return title.id == id; });
    if (it == m_titles.end() || frame < 0
        || (track != -1 && !isVideoTrack(track))) return;
    track = resolveNewTrack(track);
    if (track < 0) return;
    if (snap) frame = snappedStartFrame(frame, it->durationFrames, track,
                                       snapToleranceFrames, id);
    const auto placement = timelinePlacement(frame, it->durationFrames,
        track, id, dropFrame);
    if (it->startFrame == placement.startFrame && it->track == track
        && placement.shiftFrom < 0) return;
    Edit edit; edit.kind = Edit::PlaceTimelineItems;
    edit.previousTitles.append(*it);
    applyTimelinePlacement(placement, track, id, edit);
    it->startFrame = placement.startFrame;
    it->track = track;
    edit.resultingTitles.append(*it);
    finishTimelinePlacement(std::move(edit));
    emit selectionChanged();
    setStatus(QStringLiteral("Text moved"));
}

void EditorController::selectTitle(const QString &id)
{
    if (std::none_of(m_titles.cbegin(), m_titles.cend(),
        [&](const TitleClip &title) { return title.id == id; })) return;
    m_selectedTitleId = id;
    m_selectedClipId.clear();
    m_selectedCaptionId.clear();
    m_selectedCaptionRowId.clear();
    emit selectionChanged();
}

void EditorController::updateTitle(const QString &id, const QString &text,
                                   qint64 startFrame, qint64 durationFrames,
                                   int fontPixels, const QString &color)
{
    const auto it = std::find_if(m_titles.begin(), m_titles.end(),
        [&](const TitleClip &title) { return title.id == id; });
    const QColor parsed(color);
    if (it == m_titles.end() || text.trimmed().isEmpty() || startFrame < 0
        || durationFrames <= 0 || fontPixels < 12 || fontPixels > 300 || !parsed.isValid()) return;
    TitleClip changed = *it;
    changed.text = text.trimmed();
    changed.startFrame = startFrame;
    changed.durationFrames = durationFrames;
    for (auto *points : {&changed.positionXKeyframes, &changed.positionYKeyframes,
                         &changed.scaleKeyframes})
        points->erase(std::remove_if(points->begin(), points->end(),
            [durationFrames](const ValueKeyframe &point) {
                return point.frame >= durationFrames;
            }), points->end());
    changed.fontPixels = fontPixels;
    changed.color = parsed.name(QColor::HexRgb);
    if (changed.startFrame != it->startFrame
        || changed.durationFrames != it->durationFrames) {
        const auto placement = timelinePlacement(changed.startFrame,
            changed.durationFrames, changed.track, id, changed.startFrame);
        changed.startFrame = placement.startFrame;
        Edit edit; edit.kind = Edit::PlaceTimelineItems;
        edit.previousTitles.append(*it);
        applyTimelinePlacement(placement, changed.track, id, edit);
        *it = changed;
        edit.resultingTitles.append(changed);
        finishTimelinePlacement(std::move(edit));
        return;
    }
    Edit edit{Edit::ReplaceTitle, {}, {}, {}, it - m_titles.begin()};
    edit.title = *it;
    edit.extraTitle = changed;
    m_undo.append(edit);
    m_redo.clear();
    *it = changed;
    m_dirty = true;
    emit projectChanged();
}

void EditorController::updateTitleStyle(const QString &id, const QString &fontFamily,
    bool bold, bool italic, const QString &backgroundColor, int backgroundOpacity,
    int positionX, int positionY, int anchorX, int anchorY)
{
    const auto it = std::find_if(m_titles.begin(), m_titles.end(),
        [&](const TitleClip &title) { return title.id == id; });
    if (it == m_titles.end() || !QColor(backgroundColor).isValid()) return;
    TitleClip changed = *it;
    changed.fontFamily = fontFamily.trimmed().isEmpty() ? QStringLiteral("Segoe UI") : fontFamily.trimmed();
    changed.bold = bold; changed.italic = italic;
    changed.backgroundColor = backgroundColor;
    changed.backgroundOpacity = std::clamp(backgroundOpacity, 0, 100);
    changed.positionX = std::clamp(positionX, -200, 300);
    changed.positionY = std::clamp(positionY, -200, 300);
    changed.anchorX = std::clamp(anchorX, 0, 100);
    changed.anchorY = std::clamp(anchorY, 0, 100);
    Edit edit{Edit::ReplaceTitle, {}, {}, {}, it - m_titles.begin()};
    edit.title = *it; edit.extraTitle = changed;
    m_undo.append(edit); m_redo.clear(); *it = changed; m_dirty = true;
    emit projectChanged();
}

void EditorController::setTitleVisual(const QString &id, int x, int y, int scale)
{
    const auto it = std::find_if(m_titles.begin(), m_titles.end(),
        [&](const TitleClip &title) { return title.id == id; });
    if (it == m_titles.end() || scale < 25 || scale > 400) return;
    TitleClip changed = *it;
    changed.positionX = std::clamp(x, -200, 300);
    changed.positionY = std::clamp(y, -200, 300);
    changed.scalePercent = scale;
    if (changed.positionX == it->positionX && changed.positionY == it->positionY
        && changed.scalePercent == it->scalePercent) return;
    Edit edit{Edit::ReplaceTitle, {}, {}, {}, it - m_titles.begin()};
    edit.title = *it; edit.extraTitle = changed;
    m_undo.append(edit); m_redo.clear(); *it = changed; m_dirty = true;
    emit projectChanged();
}

void EditorController::setTitleVisualKeyframe(const QString &id, qint64 frame,
                                               int x, int y, int scale)
{
    const auto it = std::find_if(m_titles.begin(), m_titles.end(),
        [&](const TitleClip &title) { return title.id == id; });
    if (it == m_titles.end() || frame < it->startFrame
        || frame >= it->startFrame + it->durationFrames || scale < 25 || scale > 400) return;
    TitleClip changed = *it;
    const qint64 local = frame - it->startFrame;
    upsertKeyframe(changed.positionXKeyframes, local, std::clamp(x, -200, 300));
    upsertKeyframe(changed.positionYKeyframes, local, std::clamp(y, -200, 300));
    upsertKeyframe(changed.scaleKeyframes, local, scale);
    Edit edit{Edit::ReplaceTitle, {}, {}, {}, it - m_titles.begin()};
    edit.title = *it; edit.extraTitle = changed;
    m_undo.append(edit); m_redo.clear(); *it = changed; m_dirty = true;
    emit projectChanged();
}

void EditorController::removeTitleVisualKeyframe(const QString &id, qint64 frame)
{
    const auto it = std::find_if(m_titles.begin(), m_titles.end(),
        [&](const TitleClip &title) { return title.id == id; });
    if (it == m_titles.end()) return;
    TitleClip changed = *it;
    const qint64 local = frame - it->startFrame;
    eraseKeyframe(changed.positionXKeyframes, local);
    eraseKeyframe(changed.positionYKeyframes, local);
    eraseKeyframe(changed.scaleKeyframes, local);
    Edit edit{Edit::ReplaceTitle, {}, {}, {}, it - m_titles.begin()};
    edit.title = *it; edit.extraTitle = changed;
    m_undo.append(edit); m_redo.clear(); *it = changed; m_dirty = true;
    emit projectChanged();
}

QVariantMap EditorController::titleVisualAt(const QString &id, qint64 frame) const
{
    const auto it = std::find_if(m_titles.cbegin(), m_titles.cend(),
        [&](const TitleClip &title) { return title.id == id; });
    if (it == m_titles.cend()) return {};
    const qint64 local = std::clamp<qint64>(frame - it->startFrame, 0, it->durationFrames - 1);
    return {{"x", valueAt(it->positionXKeyframes, local, it->positionX)},
            {"y", valueAt(it->positionYKeyframes, local, it->positionY)},
            {"scale", valueAt(it->scaleKeyframes, local, it->scalePercent)},
            {"keyframe", std::any_of(it->scaleKeyframes.cbegin(), it->scaleKeyframes.cend(),
                [local](const ValueKeyframe &point) { return point.frame == local; })}};
}

void EditorController::removeSelectedTitle()
{
    const auto it = std::find_if(m_titles.cbegin(), m_titles.cend(),
        [&](const TitleClip &title) { return title.id == m_selectedTitleId; });
    if (it == m_titles.cend()) return;
    Edit edit{Edit::RemoveTitle, {}, {}, {}, it - m_titles.cbegin()};
    edit.title = *it;
    edit.previousTransitions = m_transitions;
    for (auto &transition : m_transitions) {
        if (transition.outgoingClipId == it->id) transition.outgoingClipId.clear();
        if (transition.incomingClipId == it->id) transition.incomingClipId.clear();
    }
    m_transitions.erase(std::remove_if(m_transitions.begin(), m_transitions.end(),
        [](const ClipTransition &transition) {
            return transition.outgoingClipId.isEmpty() && transition.incomingClipId.isEmpty();
        }), m_transitions.end());
    edit.resultingTransitions = m_transitions;
    m_undo.append(edit);
    m_redo.clear();
    m_titles.erase(it);
    m_selectedTitleId.clear();
    m_dirty = true;
    emit projectChanged();
    emit selectionChanged();
}

void EditorController::addToTimeline(const QString &assetId)
{
    const auto *asset = findAsset(assetId);
    if (!asset) return;
    const int track = asset->media.hasVideo ? 0 : 1;
    qint64 end = 0;
    for (const auto &clip : m_clips)
        if (clip.track == track)
            end = std::max(end, clip.startFrame + clip.durationFrames);
    placeAssetOnTimeline(assetId, end, track, false, 0);
}

EditorController::SnapPlacement EditorController::snapPlacement(
    qint64 startFrame, qint64 duration, int track, qint64 toleranceFrames,
    const QString &excludeId) const
{
    SnapPlacement result{std::max<qint64>(0, startFrame), -1};
    qint64 bestDistance = std::max<qint64>(0, toleranceFrames) + 1;
    int bestPriority = std::numeric_limits<int>::max();
    auto consider = [&](qint64 targetStart, qint64 guideFrame, int priority) {
        if (targetStart < 0) return;
        const qint64 distance = std::abs(targetStart - startFrame);
        if (distance > toleranceFrames) return;
        if (distance < bestDistance || (distance == bestDistance && priority < bestPriority)) {
            result = {targetStart, guideFrame};
            bestDistance = distance;
            bestPriority = priority;
        }
    };

    // Both ends of a moving clip can meet existing clip edges or the playhead.
    // The playhead names a visible frame, so aligning a clip's end includes it.
    for (const auto &other : m_clips) {
        if (other.id == excludeId) continue;
        const qint64 end = other.startFrame + other.durationFrames;
        const int crossTrackPenalty = other.track == track ? 1 : 3;
        consider(end, end, crossTrackPenalty);
        consider(other.startFrame - duration, other.startFrame, crossTrackPenalty);
        consider(other.startFrame, other.startFrame, crossTrackPenalty + 1);
        consider(end - duration, end, crossTrackPenalty + 1);
    }
    for (const auto &other : m_titles) {
        if (other.id == excludeId) continue;
        const qint64 end = other.startFrame + other.durationFrames;
        const int crossTrackPenalty = other.track == track ? 1 : 3;
        consider(end, end, crossTrackPenalty);
        consider(other.startFrame - duration, other.startFrame, crossTrackPenalty);
        consider(other.startFrame, other.startFrame, crossTrackPenalty + 1);
        consider(end - duration, end, crossTrackPenalty + 1);
    }
    consider(m_playheadFrame, m_playheadFrame, 0);
    consider(m_playheadFrame + 1 - duration, m_playheadFrame, 0);
    consider(0, 0, 4);
    return result;
}

EditorController::SnapPlacement EditorController::snapTrimEdge(
    const QString &id, qint64 edgeFrame, bool startEdge, qint64 toleranceFrames) const
{
    const auto clip = std::find_if(m_clips.cbegin(), m_clips.cend(),
        [&](const Clip &item) { return item.id == id; });
    const auto title = std::find_if(m_titles.cbegin(), m_titles.cend(),
        [&](const TitleClip &item) { return item.id == id; });
    if (clip == m_clips.cend() && title == m_titles.cend()) return {edgeFrame, -1};
    const qint64 start = clip != m_clips.cend() ? clip->startFrame : title->startFrame;
    const qint64 duration = clip != m_clips.cend() ? clip->durationFrames : title->durationFrames;
    const int track = clip != m_clips.cend() ? clip->track : title->track;
    const qint64 minimum = startEdge ? 0 : start + 1;
    const qint64 maximum = startEdge
        ? start + duration - 1
        : std::numeric_limits<qint64>::max();
    SnapPlacement result{edgeFrame, -1};
    qint64 bestDistance = std::max<qint64>(0, toleranceFrames) + 1;
    int bestPriority = std::numeric_limits<int>::max();
    const auto consider = [&](qint64 candidate, qint64 guide, int priority) {
        if (candidate < minimum || candidate > maximum) return;
        const qint64 distance = std::abs(candidate - edgeFrame);
        if (distance > toleranceFrames) return;
        if (distance < bestDistance || (distance == bestDistance && priority < bestPriority)) {
            result = {candidate, guide};
            bestDistance = distance;
            bestPriority = priority;
        }
    };
    // A trim end is exclusive internally. Include the frame under the
    // playhead when the right edge is snapped to it.
    consider(m_playheadFrame + (startEdge ? 0 : 1), m_playheadFrame, 0);
    for (const auto &other : m_clips) {
        if (other.id == id) continue;
        const int priority = other.track == track ? 1 : 2;
        consider(other.startFrame, other.startFrame, priority);
        consider(other.startFrame + other.durationFrames,
                 other.startFrame + other.durationFrames, priority);
    }
    for (const auto &other : m_titles) {
        if (other.id == id) continue;
        const int priority = other.track == track ? 1 : 2;
        consider(other.startFrame, other.startFrame, priority);
        consider(other.startFrame + other.durationFrames,
                 other.startFrame + other.durationFrames, priority);
    }
    consider(0, 0, 3);
    return result;
}

QVariantMap EditorController::previewClipTrimSnap(const QString &id, qint64 edgeFrame,
                                                   bool startEdge, qint64 toleranceFrames) const
{
    const auto result = snapTrimEdge(id, edgeFrame, startEdge,
                                      std::max<qint64>(0, toleranceFrames));
    return {{"frame", result.startFrame}, {"guide", result.guideFrame}};
}

void EditorController::trimClipStartSnapped(const QString &id, qint64 startFrame,
                                            bool snap, qint64 toleranceFrames)
{
    trimClipStart(id, snap ? snapTrimEdge(id, startFrame, true,
        std::max<qint64>(0, toleranceFrames)).startFrame : startFrame);
}

void EditorController::trimClipEndSnapped(const QString &id, qint64 endFrame,
                                          bool snap, qint64 toleranceFrames)
{
    trimClipEnd(id, snap ? snapTrimEdge(id, endFrame, false,
        std::max<qint64>(0, toleranceFrames)).startFrame : endFrame);
}

qint64 EditorController::snappedStartFrame(qint64 startFrame, qint64 duration,
                                            int track, qint64 toleranceFrames,
                                            const QString &excludeId) const
{
    return snapPlacement(startFrame, duration, track, toleranceFrames, excludeId).startFrame;
}

EditorController::TimelinePlacement EditorController::timelinePlacement(
    qint64 startFrame, qint64 duration, int track, const QString &excludeId,
    qint64 dropFrame) const
{
    TimelinePlacement placement{std::max<qint64>(0, startFrame), -1, 0};
    if (duration <= 0) return placement;
    QVector<VisualEdge> row;
    for (const auto &clip : m_clips)
        if (clip.track == track && clip.id != excludeId)
            row.append({clip.id, track, clip.startFrame, clip.durationFrames});
    for (const auto &title : m_titles)
        if (title.track == track && title.id != excludeId)
            row.append({title.id, track, title.startFrame, title.durationFrames});
    std::sort(row.begin(), row.end(), [](const VisualEdge &left, const VisualEdge &right) {
        return left.start < right.start;
    });
    const auto overlaps = [&](const VisualEdge &item) {
        return item.start < placement.startFrame + duration
            && item.start + item.duration > placement.startFrame;
    };
    const auto collision = std::find_if(row.cbegin(), row.cend(), overlaps);
    if (collision == row.cend()) return placement;

    const qint64 pointer = dropFrame >= 0 ? dropFrame : placement.startFrame + duration / 2;
    const auto hovered = std::find_if(row.cbegin(), row.cend(), [&](const VisualEdge &item) {
        return pointer >= item.start && pointer < item.start + item.duration;
    });
    const VisualEdge &target = hovered == row.cend() ? *collision : *hovered;
    if (pointer < target.start + target.duration / 2) {
        qint64 previousEnd = 0;
        for (const auto &item : row)
            if (item.start + item.duration <= target.start)
                previousEnd = std::max(previousEnd, item.start + item.duration);
        placement.startFrame = std::max(previousEnd,
            std::max<qint64>(0, target.start - duration));
    } else placement.startFrame = target.start + target.duration;

    const auto firstBlocked = std::find_if(row.cbegin(), row.cend(), overlaps);
    if (firstBlocked != row.cend()) {
        placement.shiftFrom = firstBlocked->start;
        placement.shiftBy = duration;
    }
    return placement;
}

void EditorController::applyTimelinePlacement(const TimelinePlacement &placement,
                                               int track, const QString &excludeId,
                                               Edit &edit)
{
    if (placement.shiftFrom < 0) return;
    for (auto &clip : m_clips) {
        if (clip.track != track || clip.id == excludeId
            || clip.startFrame < placement.shiftFrom) continue;
        edit.affectedClips.append(clip);
        clip.startFrame += placement.shiftBy;
        edit.resultingClips.append(clip);
        shiftCaptionsWithClip(clip.id, placement.shiftBy, track, edit);
    }
    for (auto &title : m_titles) {
        if (title.track != track || title.id == excludeId
            || title.startFrame < placement.shiftFrom) continue;
        edit.previousTitles.append(title);
        title.startFrame += placement.shiftBy;
        edit.resultingTitles.append(title);
    }
}

void EditorController::shiftCaptionsWithClip(const QString &clipId, qint64 frames,
                                             int track, Edit &edit)
{
    if (frames == 0 && std::none_of(m_captionRows.cbegin(), m_captionRows.cend(),
        [&](const CaptionRow &row) { return row.sourceClipId == clipId
            && row.targetTrack != track; })) return;
    for (auto &row : m_captionRows) {
        if (row.sourceClipId != clipId) continue;
        if (edit.previousCaptionRows.isEmpty()) edit.previousCaptionRows = m_captionRows;
        row.targetTrack = track;
        for (auto *points : {&row.positionXKeyframes, &row.positionYKeyframes,
                             &row.scaleKeyframes})
            for (auto &point : *points) point.frame += frames;
        for (auto &cue : row.cues)
            for (auto &word : cue.words) {
                word.firstFrame += frames;
                word.endFrame += frames;
            }
    }
    if (!edit.previousCaptionRows.isEmpty()) edit.resultingCaptionRows = m_captionRows;
}

void EditorController::finishTimelinePlacement(Edit edit)
{
    m_undo.append(std::move(edit));
    m_redo.clear();
    m_dirty = true;
    emit projectChanged();
}

QVariantMap EditorController::previewClipSnap(const QString &id, qint64 startFrame,
                                                int track, bool snap,
                                                qint64 snapToleranceFrames,
                                                qint64 dropFrame) const
{
    const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
        [&](const Clip &clip) { return clip.id == id; });
    if (it == m_clips.cend())
        return {{"start", std::max<qint64>(0, startFrame)}, {"guide", -1}};
    const auto snapped = snap ? snapPlacement(std::max<qint64>(0, startFrame),
        it->durationFrames, track, std::max<qint64>(0, snapToleranceFrames), id)
        : SnapPlacement{std::max<qint64>(0, startFrame), -1};
    const auto placement = timelinePlacement(snapped.startFrame,
        it->durationFrames, track, id, dropFrame);
    return {{"start", placement.startFrame},
            {"guide", placement.startFrame == snapped.startFrame ? snapped.guideFrame : -1}};
}

QVariantMap EditorController::previewNewClipSnap(const QString &id, bool savedSegment,
                                                  qint64 startFrame, int track,
                                                  qint64 snapToleranceFrames) const
{
    const qint64 rawStart = std::max<qint64>(0, startFrame);
    const Asset *asset = nullptr;
    qint64 sourceFrames = 0;
    if (savedSegment) {
        const auto segment = std::find_if(m_segments.cbegin(), m_segments.cend(),
            [&](const Segment &item) { return item.id == id; });
        if (segment == m_segments.cend()) return {{"start", rawStart}, {"guide", -1}};
        asset = findAsset(segment->source.assetId);
        sourceFrames = segment->source.frames.length();
    } else {
        asset = findAsset(id);
        if (asset) sourceFrames = asset->media.frameCount;
    }
    if (!asset || sourceFrames <= 0) return {{"start", rawStart}, {"guide", -1}};
    try {
        MediaInfo media = asset->media;
        if (!savedSegment && normalizeMp3CoverArt(media)) sourceFrames = media.frameCount;
        const qint64 duration = media.isStillImage ? framesForSeconds(5)
            : rescaleFrames(sourceFrames, media.frameRate, m_projectRate);
        const auto result = snapPlacement(rawStart, duration, track,
                                           std::max<qint64>(0, snapToleranceFrames));
        const auto placement = timelinePlacement(result.startFrame, duration,
            track, {}, rawStart);
        return {{"start", placement.startFrame},
                {"guide", placement.startFrame == result.startFrame ? result.guideFrame : -1}};
    } catch (const std::exception &) {
        return {{"start", rawStart}, {"guide", -1}};
    }
}

void EditorController::placeAssetOnTimeline(const QString &assetId,
                                             qint64 startFrame, int track, bool snap,
                                             qint64 snapToleranceFrames)
{
    const auto assetIt = std::find_if(m_assets.begin(), m_assets.end(),
        [&](const Asset &item) { return item.id == assetId; });
    auto *asset = assetIt == m_assets.end() ? nullptr : &*assetIt;
    if (!asset || startFrame < 0) return;
    QString placementAssetId = assetId;
    if (mp3CoverArtWasClassifiedAsVideo(asset->media)) {
        const bool inUse = std::any_of(m_clips.cbegin(), m_clips.cend(), [&](const Clip &clip) {
            return clip.assetId == assetId;
        });
        if (!inUse) {
            normalizeMp3CoverArt(asset->media);
        } else {
            const QString path = asset->media.path;
            auto corrected = std::find_if(m_assets.begin(), m_assets.end(),
                [&](const Asset &item) { return item.media.path == path && !item.media.hasVideo; });
            if (corrected == m_assets.end()) {
                MediaInfo media = asset->media;
                normalizeMp3CoverArt(media);
                m_assets.append({freshId(), std::move(media)});
                corrected = std::prev(m_assets.end());
            }
            asset = &*corrected;
            placementAssetId = asset->id;
        }
        m_dirty = true;
        emit projectChanged();
    }
    if (asset->media.hasVideo ? (track != -1 && !isVideoTrack(track))
                              : (track != -2 && !isAudioTrack(track))) {
        setStatus(QStringLiteral("Drop video on a video row or audio on an audio row"));
        return;
    }
    try {
        const auto sourceFrames = asset->media.frameCount;
        const auto duration = asset->media.isStillImage ? framesForSeconds(5)
            : rescaleFrames(sourceFrames, asset->media.frameRate, m_projectRate);
        if (duration <= 0) {
            setStatus(QStringLiteral("Cannot place media with unknown duration"));
            return;
        }
        track = resolveNewTrack(track);
        if (track < 0) return;
        if (snap)
            startFrame = snappedStartFrame(startFrame, duration, track,
                                          snapToleranceFrames);
        const auto placement = timelinePlacement(startFrame, duration, track, {}, startFrame);
        startFrame = placement.startFrame;
        const QString id = freshId();
        Clip clip{id, placementAssetId, startFrame, duration, 0, sourceFrames, track};
        clip.includeVideo = asset->media.hasVideo;
        clip.includeAudio = asset->media.hasAudio;
        Edit edit; edit.kind = Edit::PlaceTimelineItems;
        applyTimelinePlacement(placement, track, {}, edit);
        m_clips.append(clip);
        edit.resultingClips.append(clip);
        finishTimelinePlacement(std::move(edit));
        selectClip(id);
        setStatus(QStringLiteral("Placed on timeline at project frame %1")
            .arg(startFrame));
    } catch (const std::exception &error) {
        setStatus(QStringLiteral("Cannot place media: %1").arg(QString::fromUtf8(error.what())));
    }
}

void EditorController::removeSelectedClip()
{
    if (m_selectedClipId.isEmpty()) return;
    const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
        [&](const Clip &clip) { return clip.id == m_selectedClipId; });
    if (it == m_clips.cend()) return;
    const auto index = it - m_clips.cbegin();
    Edit edit{Edit::Remove, *it, {}, {}, index};
    edit.previousCaptionRows = m_captionRows;
    m_captionRows.erase(std::remove_if(m_captionRows.begin(), m_captionRows.end(),
        [&](const CaptionRow &row) { return row.sourceClipId == it->id; }), m_captionRows.end());
    edit.resultingCaptionRows = m_captionRows;
    edit.previousTransitions = m_transitions;
    for (auto &transition : m_transitions) {
        if (transition.outgoingClipId == it->id) transition.outgoingClipId.clear();
        if (transition.incomingClipId == it->id) transition.incomingClipId.clear();
    }
    m_transitions.erase(std::remove_if(m_transitions.begin(), m_transitions.end(),
        [](const ClipTransition &transition) {
            return transition.outgoingClipId.isEmpty() && transition.incomingClipId.isEmpty();
        }), m_transitions.end());
    edit.resultingTransitions = m_transitions;
    m_undo.append(edit);
    m_redo.clear();
    m_clips.removeAt(index);
    m_dirty = true;
    m_selectedClipId.clear();
    emit projectChanged();
    emit selectionChanged();
    setStatus(QStringLiteral("Clip removed"));
}

void EditorController::rippleDeleteSelectedClip()
{
    const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
        [&](const Clip &clip) { return clip.id == m_selectedClipId; });
    if (it == m_clips.cend()) return;
    Edit edit{Edit::RippleDelete, *it, {}, {}, it - m_clips.cbegin()};
    edit.previousTransitions = m_transitions;
    const qint64 end = it->startFrame + it->durationFrames;
    for (const auto &clip : m_clips)
        if (clip.id != it->id && clip.track == it->track && clip.startFrame >= end)
            edit.affectedClips.append(clip);
    m_clips.erase(it);
    for (auto &transition : m_transitions) {
        if (transition.outgoingClipId == edit.clip.id) transition.outgoingClipId.clear();
        if (transition.incomingClipId == edit.clip.id) transition.incomingClipId.clear();
    }
    m_transitions.erase(std::remove_if(m_transitions.begin(), m_transitions.end(),
        [](const ClipTransition &transition) {
            return transition.outgoingClipId.isEmpty() && transition.incomingClipId.isEmpty();
        }), m_transitions.end());
    edit.resultingTransitions = m_transitions;
    for (auto &clip : m_clips)
        if (std::any_of(edit.affectedClips.cbegin(), edit.affectedClips.cend(),
            [&](const Clip &affected) { return affected.id == clip.id; }))
            clip.startFrame -= edit.clip.durationFrames;
    m_undo.append(edit);
    m_redo.clear();
    m_selectedClipId.clear();
    m_dirty = true;
    emit projectChanged();
    emit selectionChanged();
    setStatus(QStringLiteral("Ripple deleted clip on its track"));
}

void EditorController::copySelectedClip()
{
    const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
        [&](const Clip &clip) { return clip.id == m_selectedClipId; });
    if (it == m_clips.cend()) return;
    m_clipboard = *it;
    emit clipboardChanged();
    setStatus(QStringLiteral("Clip copied"));
}

void EditorController::cutSelectedClip()
{
    if (m_selectedClipId.isEmpty()) return;
    copySelectedClip();
    if (m_clipboard) removeSelectedClip();
    setStatus(QStringLiteral("Clip cut"));
}

void EditorController::pasteClip()
{
    if (!m_clipboard) return;
    pasteClipToTrack(m_clipboard->track);
}

bool EditorController::canPasteClipToTrack(int track) const
{
    if (!m_clipboard) return false;
    const Asset *asset = findAsset(m_clipboard->assetId);
    if (!asset) return false;
    return m_clipboard->includeVideo ? isVideoTrack(track) : isAudioTrack(track);
}

void EditorController::pasteClipToTrack(int track)
{
    if (!canPasteClipToTrack(track)) {
        setStatus(QStringLiteral("Copied clip cannot be pasted on this track"));
        return;
    }
    Clip pasted = *m_clipboard;
    pasted.id = freshId();
    pasted.startFrame = m_playheadFrame;
    pasted.track = track;
    const auto placement = timelinePlacement(pasted.startFrame,
        pasted.durationFrames, track, {}, m_playheadFrame);
    pasted.startFrame = placement.startFrame;
    Edit edit; edit.kind = Edit::PlaceTimelineItems;
    applyTimelinePlacement(placement, track, {}, edit);
    m_clips.append(pasted);
    edit.resultingClips.append(pasted);
    finishTimelinePlacement(std::move(edit));
    selectClip(pasted.id);
    setStatus(QStringLiteral("Clip pasted at playhead"));
}

qint64 EditorController::seekTimelineFrame(qint64 frame)
{
    if (m_effectPreviewFrame >= 0 || m_rangePreviewPlaying) pauseTimelinePlayback();
    m_timelinePreviewEnabled = true;
    m_playheadFrame = std::clamp<qint64>(frame, 0,
        std::max<qint64>(0, timelineDurationFrames() - 1));
    if (m_timelinePlaying) {
        m_playbackAnchorFrame = m_playheadFrame;
        m_playbackClock.restart();
    }
    emit playbackChanged();
    emit playheadChanged();
    return timelineVideo().value(QStringLiteral("sourceMs"), -1).toLongLong();
}

void EditorController::scrubTimelineFrame(qint64 frame, bool snap, qint64 toleranceFrames)
{
    frame = std::max<qint64>(0, frame);
    if (snap) {
        const qint64 requestedFrame = frame;
        qint64 bestDistance = std::max<qint64>(0, toleranceFrames) + 1;
        for (const auto &clip : m_clips) {
            for (const qint64 edge : {clip.startFrame,
                                      clip.startFrame + clip.durationFrames - 1}) {
                const qint64 distance = std::abs(edge - requestedFrame);
                if (distance < bestDistance) {
                    frame = edge;
                    bestDistance = distance;
                }
            }
        }
    }
    seekTimelineFrame(frame);
}

void EditorController::markTimelineIn()
{
    if (m_rangePreviewPlaying) pauseTimelinePlayback();
    m_timelineRange.first = m_playheadFrame;
    if (m_timelineRange.end <= m_timelineRange.first)
        m_timelineRange.end = m_timelineRange.first + 1;
    emit timelineRangeChanged();
    setStatus(QStringLiteral("Timeline in: project frame %1").arg(m_timelineRange.first));
}

void EditorController::markTimelineOut()
{
    if (m_rangePreviewPlaying) pauseTimelinePlayback();
    if (m_playheadFrame < m_timelineRange.first) {
        setStatus(QStringLiteral("Move the playhead after the timeline in mark"));
        return;
    }
    m_timelineRange.end = m_playheadFrame + 1;
    emit timelineRangeChanged();
    setStatus(QStringLiteral("Timeline out: project frame %1")
        .arg(m_timelineRange.end - 1));
}

void EditorController::clearTimelineRange()
{
    if (m_rangePreviewPlaying) pauseTimelinePlayback();
    m_timelineRange = {};
    emit timelineRangeChanged();
    setStatus(QStringLiteral("Timeline range cleared"));
}

void EditorController::setTimelineRange(qint64 firstFrame, qint64 endFrame)
{
    if (firstFrame < 0 || endFrame <= firstFrame) return;
    if (m_rangePreviewPlaying) pauseTimelinePlayback();
    m_timelineRange = {firstFrame, endFrame};
    emit timelineRangeChanged();
}

void EditorController::moveTimelineRange(qint64 firstFrame)
{
    if (!m_timelineRange.valid()) return;
    if (m_rangePreviewPlaying) pauseTimelinePlayback();
    const qint64 length = m_timelineRange.length();
    const qint64 lastStart = std::max<qint64>(0,
        std::max(timelineDurationFrames(), m_timelineRange.end) - length);
    const qint64 movedFirst = std::clamp<qint64>(firstFrame, 0, lastStart);
    if (movedFirst == m_timelineRange.first) return;
    m_timelineRange = {movedFirst, movedFirst + length};
    emit timelineRangeChanged();
}

void EditorController::setTimelineRangeLength(qint64 frames)
{
    if (m_rangePreviewPlaying) pauseTimelinePlayback();
    if (frames == -1) {
        if (m_timelineRange.valid()) clearTimelineRange();
        return;
    }
    if (frames < 1 || frames > maximumRangeFrames) {
        setStatus(QStringLiteral("Range size must be between 1 and %1 project frames, or -1 to clear")
            .arg(maximumRangeFrames));
        return;
    }
    const qint64 first = m_timelineRange.valid() ? m_timelineRange.first : m_playheadFrame;
    if (first > std::numeric_limits<qint64>::max() - frames) return;
    if (m_timelineRange.valid() && m_timelineRange.length() == frames) return;
    m_timelineRange = {first, first + frames};
    emit timelineRangeChanged();
}

void EditorController::adjustTimelineRangeLength(int direction)
{
    if (direction == 0) return;
    const auto parsed = parseRangeIncrement(m_rangeIncrementRule);
    if (!parsed) return;
    const RangeIncrement rule = *parsed;
    const qint64 first = firstRangeSize(rule);
    const qint64 current = timelineRangeLength();
    if (current < 1) {
        if (direction > 0) setTimelineRangeLength(first);
        return;
    }
    if (direction > 0) {
        const qint64 next = current < first ? first
            : first + ((current - first) / rule.step + 1) * rule.step;
        if (next <= maximumRangeFrames) setTimelineRangeLength(next);
    } else {
        if (current <= first) { clearTimelineRange(); return; }
        const qint64 previous = first + (current - first - 1) / rule.step * rule.step;
        setTimelineRangeLength(previous);
    }
}

void EditorController::setRangeIncrementRule(const QString &input)
{
    const QString rule = normalizedRangeIncrement(input);
    if (!rangeIncrementOptions().contains(rule) || rule == m_rangeIncrementRule) return;
    m_rangeIncrementRule = rule;
    auto settings = rangeSettings();
    settings.setValue(QStringLiteral("timeline/rangeIncrementRule"), rule);
    emit rangeIncrementOptionsChanged();
    if (m_timelineRange.valid())
        setTimelineRangeLength(nearestRangeSize(m_timelineRange.length(), *parseRangeIncrement(rule)));
}

bool EditorController::addCustomRangeIncrement(const QString &input)
{
    const QString rule = normalizedRangeIncrement(input);
    if (!parseRangeIncrement(rule)) {
        setStatus(QStringLiteral("Use a positive step such as 6, 6n+1, or n+4"));
        return false;
    }
    if (!rangeIncrementOptions().contains(rule)) {
        m_customRangeIncrements.append(rule);
        auto settings = rangeSettings();
        settings.setValue(QStringLiteral("timeline/customRangeIncrements"), m_customRangeIncrements);
        emit rangeIncrementOptionsChanged();
    }
    setRangeIncrementRule(rule);
    return true;
}

void EditorController::removeCustomRangeIncrement(const QString &input)
{
    const QString rule = normalizedRangeIncrement(input);
    if (!m_customRangeIncrements.removeOne(rule)) return;
    auto settings = rangeSettings();
    settings.setValue(QStringLiteral("timeline/customRangeIncrements"), m_customRangeIncrements);
    if (m_rangeIncrementRule == rule) setRangeIncrementRule(QStringLiteral("1"));
    else emit rangeIncrementOptionsChanged();
}

void EditorController::replaceClip(const Clip &before, const Clip &after,
                                   const QString &message)
{
    const auto it = std::find_if(m_clips.begin(), m_clips.end(),
        [&](const Clip &clip) { return clip.id == before.id; });
    if (it == m_clips.end()) return;
    const auto index = it - m_clips.begin();
    Edit edit{Edit::ReplaceClip, before, after, {}, index};
    edit.previousCaptionRows = m_captionRows;
    for (auto &row : m_captionRows) {
        if (row.sourceClipId != before.id) continue;
        row.targetTrack = after.track;
        for (auto *points : {&row.positionXKeyframes, &row.positionYKeyframes,
                             &row.scaleKeyframes}) {
            for (auto &point : *points) {
                if (after.sourceFirstFrame == before.sourceFirstFrame
                    && after.durationFrames != before.durationFrames)
                    point.frame = after.startFrame + qint64(std::llround(
                        double(point.frame - before.startFrame)
                        * after.durationFrames / before.durationFrames));
                else if (after.sourceFirstFrame == before.sourceFirstFrame)
                    point.frame += after.startFrame - before.startFrame;
            }
            points->erase(std::remove_if(points->begin(), points->end(),
                [&](const ValueKeyframe &point) {
                    return point.frame < after.startFrame
                        || point.frame >= after.startFrame + after.durationFrames;
                }), points->end());
            points->erase(std::unique(points->begin(), points->end(),
                [](const ValueKeyframe &left, const ValueKeyframe &right) {
                    return left.frame == right.frame;
                }), points->end());
        }
        for (auto &cue : row.cues) {
            for (auto &word : cue.words) {
                if (after.sourceFirstFrame == before.sourceFirstFrame
                    && after.durationFrames != before.durationFrames) {
                    const qint64 firstOffset = word.firstFrame - before.startFrame;
                    const qint64 endOffset = word.endFrame - before.startFrame;
                    word.firstFrame = after.startFrame + qint64(std::llround(
                        double(firstOffset) * after.durationFrames / before.durationFrames));
                    word.endFrame = after.startFrame + qint64(std::llround(
                        double(endOffset) * after.durationFrames / before.durationFrames));
                } else if (after.sourceFirstFrame == before.sourceFirstFrame) {
                    word.firstFrame += after.startFrame - before.startFrame;
                    word.endFrame += after.startFrame - before.startFrame;
                }
            }
            cue.words.erase(std::remove_if(cue.words.begin(), cue.words.end(),
                [&](const CaptionWord &word) {
                    return word.endFrame <= after.startFrame
                        || word.firstFrame >= after.startFrame + after.durationFrames;
                }), cue.words.end());
            for (auto &word : cue.words) {
                word.firstFrame = std::max(word.firstFrame, after.startFrame);
                word.endFrame = std::min(word.endFrame, after.startFrame + after.durationFrames);
            }
        }
        row.cues.erase(std::remove_if(row.cues.begin(), row.cues.end(),
            [](const CaptionCue &cue) { return cue.words.isEmpty(); }), row.cues.end());
    }
    edit.resultingCaptionRows = m_captionRows;
    m_undo.append(edit);
    m_redo.clear();
    *it = after;
    m_dirty = true;
    emit projectChanged();
    emit playheadChanged();
    setStatus(message);
}

void EditorController::moveClip(const QString &id, qint64 startFrame, int track,
                                bool snap, qint64 snapToleranceFrames,
                                qint64 dropFrame)
{
    const auto it = std::find_if(m_clips.begin(), m_clips.end(),
        [&](const Clip &clip) { return clip.id == id; });
    if (it == m_clips.end() || startFrame < 0) return;
    const auto *asset = findAsset(it->assetId);
    if (!asset || (it->includeVideo ? (track != -1 && !isVideoTrack(track))
                                    : (track != -2 && !isAudioTrack(track)))) return;
    track = resolveNewTrack(track);
    if (track < 0) return;
    if (snap)
        startFrame = snappedStartFrame(startFrame, it->durationFrames, track,
                                      snapToleranceFrames, id);
    const auto placement = timelinePlacement(startFrame, it->durationFrames,
        track, id, dropFrame);
    if (it->startFrame == placement.startFrame && it->track == track
        && placement.shiftFrom < 0) return;
    Edit edit; edit.kind = Edit::PlaceTimelineItems;
    edit.affectedClips.append(*it);
    applyTimelinePlacement(placement, track, id, edit);
    Clip moved = *it;
    moved.startFrame = placement.startFrame;
    moved.track = track;
    *it = moved;
    edit.resultingClips.append(moved);
    shiftCaptionsWithClip(id, moved.startFrame - edit.affectedClips.first().startFrame,
                          track, edit);
    finishTimelinePlacement(std::move(edit));
    emit selectionChanged();
    setStatus(QStringLiteral("Clip moved"));
}

void EditorController::trimClipStart(const QString &id, qint64 startFrame)
{
    const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
        [&](const Clip &clip) { return clip.id == id; });
    if (it == m_clips.cend() || startFrame < 0 || startFrame == it->startFrame
        || startFrame >= it->startFrame + it->durationFrames) return;
    if (startFrame < it->startFrame) {
        for (const auto &other : m_clips)
            if (other.id != id && other.track == it->track
                && other.startFrame + other.durationFrames <= it->startFrame)
                startFrame = std::max(startFrame,
                    other.startFrame + other.durationFrames);
        for (const auto &other : m_titles)
            if (other.track == it->track
                && other.startFrame + other.durationFrames <= it->startFrame)
                startFrame = std::max(startFrame,
                    other.startFrame + other.durationFrames);
        if (startFrame == it->startFrame) return;
    }
    const auto *asset = findAsset(it->assetId);
    if (!asset) return;
    if (asset->media.isStillImage) {
        Clip trimmed = *it;
        trimmed.durationFrames -= startFrame - it->startFrame;
        trimmed.startFrame = startFrame;
        trimGainStart(trimmed, *it);
        trimVisualStart(trimmed, *it);
        trimmed.fadeInFrames = std::min(trimmed.fadeInFrames, trimmed.durationFrames);
        trimmed.fadeOutFrames = std::min(trimmed.fadeOutFrames,
                                         trimmed.durationFrames - trimmed.fadeInFrames);
        replaceClip(*it, trimmed, QStringLiteral("Still image start trimmed"));
        return;
    }
    try {
        const FrameRate sourceRate = asset->media.frameRate;
        const qint64 delta = startFrame - it->startFrame;
        const auto sourceDelta = rescaleFrames(std::abs(delta) * it->speedPercent,
            FrameRate{m_projectRate.numerator * 100, m_projectRate.denominator}, sourceRate);
        const qint64 offset = delta > 0 ? sourceDelta : -sourceDelta;
        if (it->sourceFirstFrame + offset < 0
            || it->sourceFrameCount - offset <= 0) return;
        Clip trimmed = *it;
        trimmed.startFrame = startFrame;
        trimmed.durationFrames -= startFrame - it->startFrame;
        trimmed.sourceFirstFrame += offset;
        trimmed.sourceFrameCount -= offset;
        const bool voiceOutOfRange = trimmed.voiceChangeEnabled
            && (trimmed.sourceFirstFrame < trimmed.voiceSourceFirstFrame
                || trimmed.sourceFirstFrame + trimmed.sourceFrameCount
                    > trimmed.voiceSourceFirstFrame + trimmed.voiceSourceFrameCount);
        if (voiceOutOfRange) trimmed.voiceChangeEnabled = false;
        trimGainStart(trimmed, *it);
        trimVisualStart(trimmed, *it);
        trimmed.fadeInFrames = std::min(trimmed.fadeInFrames, trimmed.durationFrames);
        trimmed.fadeOutFrames = std::min(trimmed.fadeOutFrames,
                                         trimmed.durationFrames - trimmed.fadeInFrames);
        replaceClip(*it, trimmed, voiceOutOfRange
            ? QStringLiteral("Clip trimmed; voice change disabled outside its generated range")
            : QStringLiteral("Clip start trimmed"));
    } catch (const std::exception &error) {
        setStatus(QString::fromUtf8(error.what()));
    }
}

void EditorController::trimClipEnd(const QString &id, qint64 endFrame)
{
    const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
        [&](const Clip &clip) { return clip.id == id; });
    if (it == m_clips.cend() || endFrame <= it->startFrame
        || endFrame == it->startFrame + it->durationFrames) return;
    if (endFrame > it->startFrame + it->durationFrames) {
        for (const auto &other : m_clips)
            if (other.id != id && other.track == it->track
                && other.startFrame >= it->startFrame + it->durationFrames)
                endFrame = std::min(endFrame, other.startFrame);
        for (const auto &other : m_titles)
            if (other.track == it->track
                && other.startFrame >= it->startFrame + it->durationFrames)
                endFrame = std::min(endFrame, other.startFrame);
        if (endFrame == it->startFrame + it->durationFrames) return;
    }
    const auto *asset = findAsset(it->assetId);
    if (!asset) return;
    if (asset->media.isStillImage) {
        Clip trimmed = *it;
        trimmed.durationFrames = endFrame - it->startFrame;
        trimGainEnd(trimmed, *it);
        trimVisualEnd(trimmed);
        trimmed.fadeInFrames = std::min(trimmed.fadeInFrames, trimmed.durationFrames);
        trimmed.fadeOutFrames = std::min(trimmed.fadeOutFrames,
                                         trimmed.durationFrames - trimmed.fadeInFrames);
        replaceClip(*it, trimmed, QStringLiteral("Still image end trimmed"));
        return;
    }
    try {
        const FrameRate sourceRate = asset->media.frameRate;
        const auto count = rescaleFrames((endFrame - it->startFrame) * it->speedPercent,
            FrameRate{m_projectRate.numerator * 100, m_projectRate.denominator}, sourceRate);
        if (count <= 0 || it->sourceFirstFrame + count > asset->media.frameCount) return;
        Clip trimmed = *it;
        trimmed.durationFrames = endFrame - it->startFrame;
        trimmed.sourceFrameCount = count;
        const bool voiceOutOfRange = trimmed.voiceChangeEnabled
            && (trimmed.sourceFirstFrame < trimmed.voiceSourceFirstFrame
                || trimmed.sourceFirstFrame + trimmed.sourceFrameCount
                    > trimmed.voiceSourceFirstFrame + trimmed.voiceSourceFrameCount);
        if (voiceOutOfRange) trimmed.voiceChangeEnabled = false;
        trimGainEnd(trimmed, *it);
        trimVisualEnd(trimmed);
        trimmed.fadeInFrames = std::min(trimmed.fadeInFrames, trimmed.durationFrames);
        trimmed.fadeOutFrames = std::min(trimmed.fadeOutFrames,
                                         trimmed.durationFrames - trimmed.fadeInFrames);
        replaceClip(*it, trimmed, voiceOutOfRange
            ? QStringLiteral("Clip trimmed; voice change disabled outside its generated range")
            : QStringLiteral("Clip end trimmed"));
    } catch (const std::exception &error) {
        setStatus(QString::fromUtf8(error.what()));
    }
}

void EditorController::setClipVisual(const QString &id, int x, int y, int scale,
                                     int rotation, int opacity)
{
    const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
        [&](const Clip &clip) { return clip.id == id; });
    if (it == m_clips.cend() || scale < 25 || scale > 400 || rotation < -180
        || rotation > 180 || opacity < 0 || opacity > 100) return;
    Clip changed = *it;
    changed.positionX = x;
    changed.positionY = y;
    changed.scalePercent = scale;
    changed.rotationDegrees = rotation;
    changed.opacityPercent = opacity;
    if (changed.positionX == it->positionX && changed.positionY == it->positionY
        && changed.scalePercent == it->scalePercent
        && changed.rotationDegrees == it->rotationDegrees
        && changed.opacityPercent == it->opacityPercent) return;
    replaceClip(*it, changed, QStringLiteral("Clip transform updated"));
}

void EditorController::setClipVisualKeyframe(const QString &id, qint64 frame,
                                              int x, int y, int scale)
{
    const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
        [&](const Clip &clip) { return clip.id == id; });
    if (it == m_clips.cend() || frame < it->startFrame
        || frame >= it->startFrame + it->durationFrames || scale < 25 || scale > 400) return;
    Clip changed = *it;
    const qint64 local = frame - it->startFrame;
    upsertKeyframe(changed.positionXKeyframes, local, x);
    upsertKeyframe(changed.positionYKeyframes, local, y);
    upsertKeyframe(changed.scaleKeyframes, local, scale);
    replaceClip(*it, changed, QStringLiteral("Clip transform keyframe updated"));
}

void EditorController::removeClipVisualKeyframe(const QString &id, qint64 frame)
{
    const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
        [&](const Clip &clip) { return clip.id == id; });
    if (it == m_clips.cend()) return;
    Clip changed = *it;
    const qint64 local = frame - it->startFrame;
    eraseKeyframe(changed.positionXKeyframes, local);
    eraseKeyframe(changed.positionYKeyframes, local);
    eraseKeyframe(changed.scaleKeyframes, local);
    replaceClip(*it, changed, QStringLiteral("Clip transform keyframe removed"));
}

QVariantMap EditorController::clipVisualAt(const QString &id, qint64 frame) const
{
    const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
        [&](const Clip &clip) { return clip.id == id; });
    if (it == m_clips.cend()) return {};
    const qint64 local = std::clamp<qint64>(frame - it->startFrame, 0, it->durationFrames - 1);
    return {{"x", valueAt(it->positionXKeyframes, local, it->positionX)},
            {"y", valueAt(it->positionYKeyframes, local, it->positionY)},
            {"scale", valueAt(it->scaleKeyframes, local, it->scalePercent)},
            {"rotation", valueAt(it->rotationKeyframes, local, it->rotationDegrees)},
            {"opacity", valueAt(it->opacityKeyframes, local, it->opacityPercent)},
            {"keyframe", std::any_of(it->scaleKeyframes.cbegin(), it->scaleKeyframes.cend(),
                [local](const ValueKeyframe &point) { return point.frame == local; })}};
}

void EditorController::setVisualPropertyValue(const QString &kind, const QString &id,
    const QString &property, qint64 frame, int value)
{
    editVisualProperty(kind, id, property, frame, frame, value, 0);
}

void EditorController::setVisualPropertyKeyframe(const QString &kind, const QString &id,
    const QString &property, qint64 frame)
{
    editVisualProperty(kind, id, property, frame, frame, 0, 1);
}

void EditorController::removeVisualPropertyKeyframe(const QString &kind, const QString &id,
    const QString &property, qint64 frame)
{
    editVisualProperty(kind, id, property, frame, frame, 0, 2);
}

void EditorController::moveVisualPropertyKeyframe(const QString &kind, const QString &id,
    const QString &property, qint64 oldFrame, qint64 newFrame)
{
    editVisualProperty(kind, id, property, oldFrame, newFrame, 0, 3);
}

void EditorController::editVisualProperty(const QString &kind, const QString &id,
    const QString &property, qint64 frame, qint64 destinationFrame, int value, int operation)
{
    if (kind == QStringLiteral("clip")) {
        const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
            [&](const Clip &clip) { return clip.id == id; });
        if (it == m_clips.cend() || frame < it->startFrame
            || frame >= it->startFrame + it->durationFrames
            || destinationFrame < it->startFrame
            || destinationFrame >= it->startFrame + it->durationFrames) return;
        Clip changed = *it;
        if (!changeVisualChannel(propertyChannel(changed, property),
            frame - it->startFrame, destinationFrame - it->startFrame, value, operation)) return;
        replaceClip(*it, changed, QStringLiteral("Clip %1 keyframe updated").arg(property));
    } else if (kind == QStringLiteral("title")) {
        const auto it = std::find_if(m_titles.begin(), m_titles.end(),
            [&](const TitleClip &title) { return title.id == id; });
        if (it == m_titles.end() || frame < it->startFrame
            || frame >= it->startFrame + it->durationFrames
            || destinationFrame < it->startFrame
            || destinationFrame >= it->startFrame + it->durationFrames) return;
        TitleClip changed = *it;
        if (!changeVisualChannel(propertyChannel(changed, property),
            frame - it->startFrame, destinationFrame - it->startFrame, value, operation)) return;
        Edit edit{Edit::ReplaceTitle, {}, {}, {}, it - m_titles.begin()};
        edit.title = *it; edit.extraTitle = changed;
        m_undo.append(edit); m_redo.clear(); *it = changed; m_dirty = true;
        emit projectChanged();
    } else if (kind == QStringLiteral("caption")) {
        auto it = std::find_if(m_captionRows.begin(), m_captionRows.end(),
            [&](const CaptionRow &row) { return row.id == id; });
        if (it == m_captionRows.end() || frame < 0 || destinationFrame < 0
            || frame >= timelineDurationFrames() || destinationFrame >= timelineDurationFrames()) return;
        Edit edit{Edit::ReplaceCaptions};
        edit.previousCaptionRows = m_captionRows;
        if (!changeVisualChannel(propertyChannel(*it, property),
            frame, destinationFrame, value, operation)) return;
        edit.resultingCaptionRows = m_captionRows;
        m_undo.append(edit); m_redo.clear(); m_dirty = true;
        emit projectChanged();
    }
}

void EditorController::setClipAudio(const QString &id, int gainPercent,
                                    qint64 fadeInFrames, qint64 fadeOutFrames)
{
    const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
        [&](const Clip &clip) { return clip.id == id; });
    if (it == m_clips.cend() || !it->includeAudio || gainPercent < 0
        || gainPercent > 300 || fadeInFrames < 0 || fadeOutFrames < 0
        || fadeInFrames + fadeOutFrames > it->durationFrames) return;
    Clip changed = *it;
    changed.gainPercent = gainPercent;
    changed.fadeInFrames = fadeInFrames;
    changed.fadeOutFrames = fadeOutFrames;
    if (changed.gainPercent == it->gainPercent
        && changed.fadeInFrames == it->fadeInFrames
        && changed.fadeOutFrames == it->fadeOutFrames) return;
    replaceClip(*it, changed, QStringLiteral("Clip audio updated"));
}

int EditorController::clipGainAt(const QString &id, qint64 timelineFrame) const
{
    const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
        [&](const Clip &clip) { return clip.id == id; });
    if (it == m_clips.cend() || !it->includeAudio) return 100;
    const qint64 localFrame = std::clamp<qint64>(timelineFrame - it->startFrame,
                                                0, it->durationFrames);
    return valueAt(it->gainKeyframes, localFrame, it->gainPercent);
}

void EditorController::setClipGainAt(const QString &id, qint64 timelineFrame, int gainPercent)
{
    const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
        [&](const Clip &clip) { return clip.id == id; });
    if (it == m_clips.cend() || !it->includeAudio || gainPercent < 0
        || gainPercent > 300) return;
    if (it->gainKeyframes.isEmpty()) {
        setClipAudio(id, gainPercent, it->fadeInFrames, it->fadeOutFrames);
        return;
    }
    const qint64 localFrame = std::clamp<qint64>(timelineFrame - it->startFrame,
                                                0, it->durationFrames);
    setAudioGainPoint(id, localFrame, gainPercent);
}

void EditorController::resetClipGain(const QString &id)
{
    const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
        [&](const Clip &clip) { return clip.id == id; });
    if (it == m_clips.cend() || !it->includeAudio
        || (it->gainPercent == 100 && it->gainKeyframes.isEmpty())) return;
    Clip changed = *it;
    changed.gainPercent = 100;
    changed.gainKeyframes.clear();
    replaceClip(*it, changed, QStringLiteral("Clip gain reset to 100%"));
}

void EditorController::setClipSpeed(const QString &id, int speedPercent)
{
    const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
        [&](const Clip &clip) { return clip.id == id; });
    if (it == m_clips.cend() || speedPercent < 50 || speedPercent > 200
        || speedPercent == it->speedPercent) return;
    const auto *asset = findAsset(it->assetId);
    if (!asset || asset->media.isStillImage) return;
    try {
        const auto normalDuration = rescaleFrames(it->sourceFrameCount,
                                                  asset->media.frameRate, m_projectRate);
        Clip changed = *it;
        changed.speedPercent = speedPercent;
        changed.durationFrames = std::max<qint64>(1,
            (normalDuration * 100 + speedPercent / 2) / speedPercent);
        for (auto &point : changed.gainKeyframes)
            point.frame = std::clamp<qint64>(
                static_cast<qint64>(std::llround(static_cast<double>(point.frame)
                    * changed.durationFrames / it->durationFrames)), 0, changed.durationFrames);
        changed.gainKeyframes.erase(std::unique(changed.gainKeyframes.begin(),
            changed.gainKeyframes.end(), [](const ValueKeyframe &a, const ValueKeyframe &b) {
                return a.frame == b.frame;
            }), changed.gainKeyframes.end());
        for (auto *points : {&changed.positionXKeyframes, &changed.positionYKeyframes,
                             &changed.scaleKeyframes}) {
            for (auto &point : *points)
                point.frame = std::clamp<qint64>(qint64(std::llround(double(point.frame)
                    * changed.durationFrames / it->durationFrames)), 0, changed.durationFrames - 1);
            points->erase(std::unique(points->begin(), points->end(),
                [](const ValueKeyframe &a, const ValueKeyframe &b) {
                    return a.frame == b.frame;
                }), points->end());
        }
        changed.fadeInFrames = std::min(changed.fadeInFrames, changed.durationFrames);
        changed.fadeOutFrames = std::min(changed.fadeOutFrames,
                                         changed.durationFrames - changed.fadeInFrames);
        replaceClip(*it, changed, QStringLiteral("Clip speed changed"));
    } catch (const std::exception &error) {
        setStatus(QStringLiteral("Speed change failed: %1").arg(QString::fromUtf8(error.what())));
    }
}

void EditorController::setClipEffect(const QString &id, const QString &effectId, int strength)
{
    const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
        [&](const Clip &clip) { return clip.id == id; });
    if (it == m_clips.cend() || !it->includeVideo || !effectExists(effectId)
        || strength < 0 || strength > 100) return;
    Clip changed = *it;
    changed.effectId = effectId;
    changed.effectStrength = strength;
    if (changed.effectId == it->effectId && changed.effectStrength == it->effectStrength) return;
    replaceClip(*it, changed, effectId.isEmpty()
        ? QStringLiteral("Effect removed") : QStringLiteral("Effect applied"));
}

void EditorController::addTrackEffect(int track, const QString &effectId)
{
    const bool visualTrack = isVideoTrack(track)
        || std::any_of(m_captionRows.cbegin(), m_captionRows.cend(),
            [track](const CaptionRow &row) { return row.trackId == track; });
    if (!visualTrack || effectId.isEmpty() || !effectExists(effectId)) return;
    if (std::any_of(m_trackEffects.cbegin(), m_trackEffects.cend(),
        [&](const TrackEffect &effect) { return effect.track == track && effect.id == effectId; }))
        return;
    Edit edit;
    edit.kind = Edit::ReplaceTrackEffects;
    edit.previousTrackEffects = m_trackEffects;
    TrackEffect added{track, effectId, 100, {}};
    if (const auto *definition = videoEffectDefinition(effectId))
        for (const auto &parameter : definition->parameters)
            added.parameters.append({parameter.id, parameter.initial, {}});
    m_trackEffects.append(added);
    qInfo().noquote() << "track_effect_added" << effectId << "track" << track;
    edit.resultingTrackEffects = m_trackEffects;
    m_undo.append(edit);
    m_redo.clear();
    m_dirty = true;
    emit projectChanged();
    setStatus(QStringLiteral("Effect added to visual row %1").arg(track));
}

void EditorController::previewEffect(const QString &effectId, int track)
{
    if (!effectExists(effectId) || effectId.isEmpty()) return;
    pauseTimelinePlayback();
    qint64 startFrame = m_playheadFrame;
    const bool hasActiveVisual = clipAtFrame(startFrame, track)
        || std::any_of(m_titles.cbegin(), m_titles.cend(), [&](const TitleClip &title) {
            return title.track == track && startFrame >= title.startFrame
                && startFrame < title.startFrame + title.durationFrames;
        })
        || std::any_of(m_captionRows.cbegin(), m_captionRows.cend(), [&](const CaptionRow &row) {
            if (row.trackId != track || !row.visible) return false;
            return std::any_of(row.cues.cbegin(), row.cues.cend(), [&](const CaptionCue &cue) {
                return !cue.words.isEmpty() && startFrame >= cue.words.first().firstFrame
                    && startFrame < cue.words.last().endFrame;
            });
        });
    if (!hasActiveVisual) {
        qint64 next = std::numeric_limits<qint64>::max();
        for (const auto &clip : m_clips)
            if (clip.track == track && clip.includeVideo)
                next = std::min(next, clip.startFrame);
        for (const auto &title : m_titles)
            if (title.track == track) next = std::min(next, title.startFrame);
        for (const auto &row : m_captionRows)
            if (row.trackId == track && row.visible)
                for (const auto &cue : row.cues)
                    if (!cue.words.isEmpty())
                        next = std::min(next, cue.words.first().firstFrame);
        if (next == std::numeric_limits<qint64>::max()) {
            setStatus(QStringLiteral("Place a visual object on this row to preview effects"));
            return;
        }
        startFrame = next;
    }
    m_timelinePreviewEnabled = true;
    m_effectPreviewFrame = startFrame;
    m_previewEffectId = effectId;
    m_previewEffectTrack = track;
    m_playbackAnchorFrame = startFrame;
    m_playbackClock.restart();
    m_timelinePlaying = true;
    m_playbackTick.start();
    emit playbackChanged();
    emit playheadChanged();
    m_effectPreviewTimer.start();
    setStatus(QStringLiteral("Previewing effect at the playhead"));
}

void EditorController::removeTrackEffect(int track, const QString &effectId)
{
    const auto it = std::find_if(m_trackEffects.cbegin(), m_trackEffects.cend(),
        [&](const TrackEffect &effect) { return effect.track == track && effect.id == effectId; });
    if (it == m_trackEffects.cend()) return;
    Edit edit;
    edit.kind = Edit::ReplaceTrackEffects;
    edit.previousTrackEffects = m_trackEffects;
    m_trackEffects.erase(it);
    qInfo().noquote() << "track_effect_removed" << effectId << "track" << track;
    edit.resultingTrackEffects = m_trackEffects;
    m_undo.append(edit);
    m_redo.clear();
    m_dirty = true;
    emit projectChanged();
    setStatus(QStringLiteral("Track effect removed"));
}

void EditorController::setTrackEffectStrength(int track, const QString &effectId, int strength)
{
    if (strength < 0 || strength > 100) return;
    const auto it = std::find_if(m_trackEffects.begin(), m_trackEffects.end(),
        [&](const TrackEffect &effect) { return effect.track == track && effect.id == effectId; });
    if (it == m_trackEffects.end()) return;
    Edit edit;
    edit.kind = Edit::ReplaceTrackEffects;
    edit.previousTrackEffects = m_trackEffects;
    auto &effect = *it;
    if (effect.keyframes.isEmpty()) {
        if (effect.strength == strength) return;
        effect.strength = strength;
    } else {
        const auto point = std::lower_bound(effect.keyframes.begin(), effect.keyframes.end(),
            m_playheadFrame, [](const ValueKeyframe &item, qint64 frame) {
                return item.frame < frame;
            });
        if (point != effect.keyframes.end() && point->frame == m_playheadFrame)
            point->value = strength;
        else effect.keyframes.insert(point, {m_playheadFrame, strength});
    }
    edit.resultingTrackEffects = m_trackEffects;
    m_undo.append(edit);
    m_redo.clear();
    m_dirty = true;
    emit projectChanged();
    setStatus(QStringLiteral("Track effect strength updated"));
}

int EditorController::trackEffectStrengthAt(int track, const QString &effectId,
                                            qint64 frame) const
{
    const auto it = std::find_if(m_trackEffects.cbegin(), m_trackEffects.cend(),
        [&](const TrackEffect &effect) { return effect.track == track && effect.id == effectId; });
    return it == m_trackEffects.cend() ? 0 : valueAt(it->keyframes, frame, it->strength);
}

void EditorController::setTrackEffectKeyframe(int track, const QString &effectId,
                                               qint64 frame, int strength)
{
    if (frame < 0 || strength < 0 || strength > 100 || !keyframeableEffect(effectId)) return;
    const auto it = std::find_if(m_trackEffects.begin(), m_trackEffects.end(),
        [&](const TrackEffect &effect) { return effect.track == track && effect.id == effectId; });
    if (it == m_trackEffects.end()) return;
    Edit edit;
    edit.kind = Edit::ReplaceTrackEffects;
    edit.previousTrackEffects = m_trackEffects;
    const auto point = std::lower_bound(it->keyframes.begin(), it->keyframes.end(),
        frame, [](const ValueKeyframe &item, qint64 position) { return item.frame < position; });
    if (point != it->keyframes.end() && point->frame == frame) point->value = strength;
    else it->keyframes.insert(point, {frame, strength});
    edit.resultingTrackEffects = m_trackEffects;
    m_undo.append(edit);
    m_redo.clear();
    m_dirty = true;
    emit projectChanged();
    setStatus(QStringLiteral("Effect keyframe set at %1").arg(projectFrameTimestamp(frame)));
}

void EditorController::removeTrackEffectKeyframe(int track, const QString &effectId, qint64 frame)
{
    const auto it = std::find_if(m_trackEffects.begin(), m_trackEffects.end(),
        [&](const TrackEffect &effect) { return effect.track == track && effect.id == effectId; });
    if (it == m_trackEffects.end()) return;
    const auto point = std::find_if(it->keyframes.cbegin(), it->keyframes.cend(),
        [&](const ValueKeyframe &keyframe) { return keyframe.frame == frame; });
    if (point == it->keyframes.cend()) return;
    Edit edit;
    edit.kind = Edit::ReplaceTrackEffects;
    edit.previousTrackEffects = m_trackEffects;
    it->keyframes.erase(point);
    edit.resultingTrackEffects = m_trackEffects;
    m_undo.append(edit);
    m_redo.clear();
    m_dirty = true;
    emit projectChanged();
}

void EditorController::moveTrackEffectKeyframe(int track, const QString &effectId,
                                                qint64 oldFrame, qint64 newFrame)
{
    if (newFrame < 0 || newFrame >= timelineDurationFrames() || oldFrame == newFrame) return;
    const auto effect = std::find_if(m_trackEffects.begin(), m_trackEffects.end(),
        [&](const TrackEffect &item) { return item.track == track && item.id == effectId; });
    if (effect == m_trackEffects.end()) return;
    const auto old = std::find_if(effect->keyframes.begin(), effect->keyframes.end(),
        [&](const ValueKeyframe &point) { return point.frame == oldFrame; });
    if (old == effect->keyframes.end()) return;
    if (std::any_of(effect->keyframes.cbegin(), effect->keyframes.cend(),
        [&](const ValueKeyframe &point) { return point.frame == newFrame; })) {
        setStatus(QStringLiteral("Another effect keyframe is already at that frame"));
        return;
    }
    Edit edit;
    edit.kind = Edit::ReplaceTrackEffects;
    edit.previousTrackEffects = m_trackEffects;
    const int strength = old->value;
    effect->keyframes.erase(old);
    const auto position = std::lower_bound(effect->keyframes.begin(), effect->keyframes.end(),
        newFrame, [](const ValueKeyframe &point, qint64 frame) {
            return point.frame < frame;
        });
    effect->keyframes.insert(position, {newFrame, strength});
    edit.resultingTrackEffects = m_trackEffects;
    m_undo.append(edit);
    m_redo.clear();
    m_dirty = true;
    emit projectChanged();
    setStatus(QStringLiteral("Effect keyframe moved to %1")
        .arg(projectFrameTimestamp(newFrame)));
}

int EditorController::trackEffectParameterAt(int track, const QString &effectId,
                                              const QString &parameterId, qint64 frame) const
{
    const auto effect = std::find_if(m_trackEffects.cbegin(), m_trackEffects.cend(),
        [&](const TrackEffect &item) { return item.track == track && item.id == effectId; });
    if (effect == m_trackEffects.cend()) return 0;
    const auto parameter = std::find_if(effect->parameters.cbegin(), effect->parameters.cend(),
        [&](const EffectParameterState &item) { return item.id == parameterId; });
    return parameter == effect->parameters.cend() ? 0
        : valueAt(parameter->keyframes, frame, parameter->value);
}

void EditorController::resetTrackEffectParameter(int track, const QString &effectId,
                                                 const QString &parameterId)
{
    const auto *definition = videoEffectDefinition(effectId);
    if (!definition) return;
    const auto metadata = std::find_if(definition->parameters.cbegin(), definition->parameters.cend(),
        [&](const EffectParameterDefinition &item) { return item.id == parameterId; });
    if (metadata == definition->parameters.cend()) return;
    auto effect = std::find_if(m_trackEffects.begin(), m_trackEffects.end(),
        [&](const TrackEffect &item) { return item.track == track && item.id == effectId; });
    if (effect == m_trackEffects.end()) return;
    auto parameter = std::find_if(effect->parameters.begin(), effect->parameters.end(),
        [&](const EffectParameterState &item) { return item.id == parameterId; });
    if (parameter == effect->parameters.end()
        || (parameter->value == metadata->initial && parameter->keyframes.isEmpty())) return;
    Edit edit; edit.kind = Edit::ReplaceTrackEffects;
    edit.previousTrackEffects = m_trackEffects;
    parameter->value = metadata->initial;
    parameter->keyframes.clear();
    edit.resultingTrackEffects = m_trackEffects;
    m_undo.append(edit); m_redo.clear(); m_dirty = true;
    emit projectChanged();
}

void EditorController::setTrackEffectParameter(int track, const QString &effectId,
                                                const QString &parameterId, int value)
{
    const auto *definition = videoEffectDefinition(effectId);
    if (!definition) return;
    const auto metadata = std::find_if(definition->parameters.cbegin(), definition->parameters.cend(),
        [&](const EffectParameterDefinition &item) { return item.id == parameterId; });
    if (metadata == definition->parameters.cend()
        || value < metadata->minimum || value > metadata->maximum) return;
    auto effect = std::find_if(m_trackEffects.begin(), m_trackEffects.end(),
        [&](const TrackEffect &item) { return item.track == track && item.id == effectId; });
    if (effect == m_trackEffects.end()) return;
    auto parameter = std::find_if(effect->parameters.begin(), effect->parameters.end(),
        [&](const EffectParameterState &item) { return item.id == parameterId; });
    if (parameter == effect->parameters.end()) return;
    if (valueAt(parameter->keyframes, m_playheadFrame, parameter->value) == value) return;
    Edit edit; edit.kind = Edit::ReplaceTrackEffects;
    edit.previousTrackEffects = m_trackEffects;
    if (parameter->keyframes.isEmpty()) parameter->value = value;
    else {
        const auto point = std::lower_bound(parameter->keyframes.begin(), parameter->keyframes.end(),
            m_playheadFrame, [](const ValueKeyframe &item, qint64 frame) {
                return item.frame < frame;
            });
        if (point != parameter->keyframes.end() && point->frame == m_playheadFrame)
            point->value = value;
        else parameter->keyframes.insert(point, {m_playheadFrame, value});
    }
    edit.resultingTrackEffects = m_trackEffects;
    m_undo.append(edit); m_redo.clear(); m_dirty = true;
    emit projectChanged();
}

void EditorController::setTrackEffectParameterKeyframe(int track, const QString &effectId,
                                                        const QString &parameterId, qint64 frame,
                                                        int value)
{
    if (frame < 0 || frame >= timelineDurationFrames()) return;
    const auto *definition = videoEffectDefinition(effectId);
    if (!definition) return;
    const auto metadata = std::find_if(definition->parameters.cbegin(), definition->parameters.cend(),
        [&](const EffectParameterDefinition &item) { return item.id == parameterId; });
    if (metadata == definition->parameters.cend()
        || value < metadata->minimum || value > metadata->maximum) return;
    auto effect = std::find_if(m_trackEffects.begin(), m_trackEffects.end(),
        [&](const TrackEffect &item) { return item.track == track && item.id == effectId; });
    if (effect == m_trackEffects.end()) return;
    auto parameter = std::find_if(effect->parameters.begin(), effect->parameters.end(),
        [&](const EffectParameterState &item) { return item.id == parameterId; });
    if (parameter == effect->parameters.end()) return;
    if (std::any_of(parameter->keyframes.cbegin(), parameter->keyframes.cend(),
        [&](const ValueKeyframe &point) { return point.frame == frame && point.value == value; }))
        return;
    Edit edit; edit.kind = Edit::ReplaceTrackEffects;
    edit.previousTrackEffects = m_trackEffects;
    const auto point = std::lower_bound(parameter->keyframes.begin(), parameter->keyframes.end(),
        frame, [](const ValueKeyframe &item, qint64 position) { return item.frame < position; });
    if (point != parameter->keyframes.end() && point->frame == frame) point->value = value;
    else parameter->keyframes.insert(point, {frame, value});
    edit.resultingTrackEffects = m_trackEffects;
    m_undo.append(edit); m_redo.clear(); m_dirty = true;
    emit projectChanged();
}

void EditorController::moveTrackEffectParameterKeyframe(int track, const QString &effectId,
                                                         const QString &parameterId,
                                                         qint64 oldFrame, qint64 newFrame)
{
    if (newFrame < 0 || newFrame >= timelineDurationFrames() || oldFrame == newFrame) return;
    auto effect = std::find_if(m_trackEffects.begin(), m_trackEffects.end(),
        [&](const TrackEffect &item) { return item.track == track && item.id == effectId; });
    if (effect == m_trackEffects.end()) return;
    auto parameter = std::find_if(effect->parameters.begin(), effect->parameters.end(),
        [&](const EffectParameterState &item) { return item.id == parameterId; });
    if (parameter == effect->parameters.end()) return;
    const auto old = std::find_if(parameter->keyframes.begin(), parameter->keyframes.end(),
        [&](const ValueKeyframe &point) { return point.frame == oldFrame; });
    if (old == parameter->keyframes.end() || std::any_of(parameter->keyframes.cbegin(),
        parameter->keyframes.cend(), [&](const ValueKeyframe &point) {
            return point.frame == newFrame;
        })) return;
    Edit edit; edit.kind = Edit::ReplaceTrackEffects;
    edit.previousTrackEffects = m_trackEffects;
    const int value = old->value;
    parameter->keyframes.erase(old);
    const auto position = std::lower_bound(parameter->keyframes.begin(), parameter->keyframes.end(),
        newFrame, [](const ValueKeyframe &point, qint64 frame) { return point.frame < frame; });
    parameter->keyframes.insert(position, {newFrame, value});
    edit.resultingTrackEffects = m_trackEffects;
    m_undo.append(edit); m_redo.clear(); m_dirty = true;
    emit projectChanged();
}

void EditorController::removeTrackEffectParameterKeyframe(int track, const QString &effectId,
                                                           const QString &parameterId, qint64 frame)
{
    auto effect = std::find_if(m_trackEffects.begin(), m_trackEffects.end(),
        [&](const TrackEffect &item) { return item.track == track && item.id == effectId; });
    if (effect == m_trackEffects.end()) return;
    auto parameter = std::find_if(effect->parameters.begin(), effect->parameters.end(),
        [&](const EffectParameterState &item) { return item.id == parameterId; });
    if (parameter == effect->parameters.end()) return;
    const auto point = std::find_if(parameter->keyframes.begin(), parameter->keyframes.end(),
        [&](const ValueKeyframe &item) { return item.frame == frame; });
    if (point == parameter->keyframes.end()) return;
    Edit edit; edit.kind = Edit::ReplaceTrackEffects;
    edit.previousTrackEffects = m_trackEffects;
    parameter->keyframes.erase(point);
    edit.resultingTrackEffects = m_trackEffects;
    m_undo.append(edit); m_redo.clear(); m_dirty = true;
    emit projectChanged();
}

void EditorController::setAudioGainPoint(const QString &clipId, qint64 frame, int gain)
{
    const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
        [&](const Clip &clip) { return clip.id == clipId; });
    if (it == m_clips.cend() || !it->includeAudio || frame < 0
        || frame > it->durationFrames || gain < 0 || gain > 300) return;
    Clip changed = *it;
    const auto point = std::lower_bound(changed.gainKeyframes.begin(), changed.gainKeyframes.end(),
        frame, [](const ValueKeyframe &item, qint64 position) { return item.frame < position; });
    if (point != changed.gainKeyframes.end() && point->frame == frame) point->value = gain;
    else changed.gainKeyframes.insert(point, {frame, gain});
    replaceClip(*it, changed, QStringLiteral("Audio gain point set"));
}

void EditorController::moveAudioGainPoint(const QString &clipId, qint64 oldFrame,
                                           qint64 newFrame, int gain)
{
    const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
        [&](const Clip &clip) { return clip.id == clipId; });
    if (it == m_clips.cend() || newFrame < 0 || newFrame > it->durationFrames
        || gain < 0 || gain > 300) return;
    Clip changed = *it;
    const auto old = std::find_if(changed.gainKeyframes.cbegin(), changed.gainKeyframes.cend(),
        [&](const ValueKeyframe &point) { return point.frame == oldFrame; });
    if (old == changed.gainKeyframes.cend()) return;
    changed.gainKeyframes.erase(old);
    const auto point = std::lower_bound(changed.gainKeyframes.begin(), changed.gainKeyframes.end(),
        newFrame, [](const ValueKeyframe &item, qint64 position) { return item.frame < position; });
    if (point != changed.gainKeyframes.end() && point->frame == newFrame) point->value = gain;
    else changed.gainKeyframes.insert(point, {newFrame, gain});
    replaceClip(*it, changed, QStringLiteral("Audio gain point moved"));
}

void EditorController::removeAudioGainPoint(const QString &clipId, qint64 frame)
{
    const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
        [&](const Clip &clip) { return clip.id == clipId; });
    if (it == m_clips.cend()) return;
    Clip changed = *it;
    const auto point = std::find_if(changed.gainKeyframes.cbegin(), changed.gainKeyframes.cend(),
        [&](const ValueKeyframe &item) { return item.frame == frame; });
    if (point == changed.gainKeyframes.cend()) return;
    changed.gainKeyframes.erase(point);
    replaceClip(*it, changed, QStringLiteral("Audio gain point removed"));
}

void EditorController::setClipAudioEffect(const QString &id, const QString &effectId,
                                          int strength)
{
    const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
        [&](const Clip &clip) { return clip.id == id; });
    if (it == m_clips.cend() || !it->includeAudio || !audioEffectExists(effectId)
        || strength < 0 || strength > 100) return;
    Clip changed = *it;
    changed.audioEffectId = effectId;
    changed.audioEffectStrength = strength;
    if (changed.audioEffectId == it->audioEffectId
        && changed.audioEffectStrength == it->audioEffectStrength) return;
    replaceClip(*it, changed, effectId.isEmpty()
        ? QStringLiteral("Audio effect removed")
        : QStringLiteral("Audio effect applied"));
}

void EditorController::splitSelectedClip()
{
    const auto it = std::find_if(m_clips.begin(), m_clips.end(),
        [&](const Clip &clip) { return clip.id == m_selectedClipId; });
    if (it == m_clips.end() || m_playheadFrame <= it->startFrame
        || m_playheadFrame >= it->startFrame + it->durationFrames) {
        setStatus(QStringLiteral("Place the playhead inside the selected clip"));
        return;
    }
    const auto *asset = findAsset(it->assetId);
    if (!asset) return;
    if (asset->media.isStillImage) {
        const auto index = it - m_clips.begin();
        const Clip original = *it;
        Clip right = original;
        right.id = freshId();
        right.startFrame = m_playheadFrame;
        right.durationFrames = original.durationFrames - (m_playheadFrame - original.startFrame);
        trimGainStart(right, original);
        trimVisualStart(right, original);
        it->durationFrames = m_playheadFrame - original.startFrame;
        trimGainEnd(*it, original);
        trimVisualEnd(*it);
        m_clips.insert(index + 1, right);
        m_undo.append({Edit::SplitClip, original, right, {}, index});
        m_redo.clear();
        m_dirty = true;
        emit projectChanged();
        return;
    }
    try {
        const FrameRate sourceRate = asset->media.frameRate;
        const auto sourceOffset = rescaleFrames(
            (m_playheadFrame - it->startFrame) * it->speedPercent,
            FrameRate{m_projectRate.numerator * 100, m_projectRate.denominator}, sourceRate);
        if (sourceOffset <= 0 || sourceOffset >= it->sourceFrameCount) return;
        const auto index = it - m_clips.begin();
        const Clip original = *it;
        Clip right = original;
        right.id = freshId();
        right.startFrame = m_playheadFrame;
        right.durationFrames = original.durationFrames - (m_playheadFrame - original.startFrame);
        right.sourceFirstFrame += sourceOffset;
        right.sourceFrameCount -= sourceOffset;
        trimGainStart(right, original);
        trimVisualStart(right, original);
        it->durationFrames = m_playheadFrame - original.startFrame;
        it->sourceFrameCount = sourceOffset;
        trimGainEnd(*it, original);
        trimVisualEnd(*it);
        m_clips.insert(index + 1, right);
        m_undo.append({Edit::SplitClip, original, right, {}, index});
        m_redo.clear();
        m_dirty = true;
        emit projectChanged();
        setStatus(QStringLiteral("Clip split at project frame %1").arg(m_playheadFrame));
    } catch (const std::exception &error) {
        setStatus(QString::fromUtf8(error.what()));
    }
}

void EditorController::markRangeStart(qint64 positionMs)
{
    const auto *asset = findAsset(m_selectedAssetId);
    if (!asset || asset->media.frameCount <= 0) return;
    try {
        const auto frame = std::clamp(rescaleFrames(std::max<qint64>(0, positionMs),
            FrameRate{1000, 1}, asset->media.frameRate),
            std::int64_t{0}, asset->media.frameCount - 1);
        m_currentRange.first = frame;
        if (m_currentRange.end <= frame) m_currentRange.end = frame + 1;
        emit selectionChanged();
        setStatus(QStringLiteral("Range starts at source frame %1").arg(frame));
    } catch (const std::exception &error) {
        setStatus(QString::fromUtf8(error.what()));
    }
}

void EditorController::markRangeEnd(qint64 positionMs)
{
    const auto *asset = findAsset(m_selectedAssetId);
    if (!asset || asset->media.frameCount <= 0) return;
    try {
        const auto frame = std::clamp(rescaleFrames(std::max<qint64>(0, positionMs),
            FrameRate{1000, 1}, asset->media.frameRate),
            std::int64_t{1}, asset->media.frameCount);
        m_currentRange.end = frame;
        if (m_currentRange.first >= frame) m_currentRange.first = frame - 1;
        emit selectionChanged();
        setStatus(QStringLiteral("Range ends before source frame %1").arg(frame));
    } catch (const std::exception &error) {
        setStatus(QString::fromUtf8(error.what()));
    }
}

void EditorController::setRange(qint64 firstFrame, qint64 endFrame)
{
    const auto *asset = findAsset(m_selectedAssetId);
    if (!asset || firstFrame < 0 || endFrame <= firstFrame
        || endFrame > asset->media.frameCount) {
        setStatus(QStringLiteral("Range must fit within the selected source"));
        return;
    }
    m_currentRange = {firstFrame, endFrame};
    emit selectionChanged();
    setStatus(QStringLiteral("Exact source range selected"));
}

void EditorController::saveRangeToCollection(const QString &collectionId,
                                             bool includeVideo, bool includeAudio)
{
    if (!canSaveRange()) {
        setStatus(QStringLiteral("Select a valid source range first"));
        return;
    }
    const auto *asset = findAsset(m_selectedAssetId);
    if ((!includeVideo && !includeAudio)
        || (includeVideo && !asset->media.hasVideo)
        || (includeAudio && !asset->media.hasAudio)) {
        setStatus(QStringLiteral("Choose available video or audio streams for the range"));
        return;
    }
    QString target = collectionId;
    if (std::none_of(m_collections.cbegin(), m_collections.cend(),
        [&](const Collection &collection) { return collection.id == target; }))
        target = m_collections.isEmpty() ? QString{} : m_collections.first().id;
    if (target.isEmpty()) {
        m_collections.append({freshId(), QStringLiteral("Selections")});
        target = m_collections.last().id;
    }
    Segment segment{freshId(),
        QStringLiteral("%1 · %2–%3").arg(asset->media.name)
            .arg(m_currentRange.first).arg(m_currentRange.end - 1),
        MediaRange{asset->id, m_currentRange, includeVideo, includeAudio},
        target};
    m_undo.append({Edit::AddSegment, {}, {}, segment, m_segments.size()});
    m_redo.clear();
    m_segments.append(segment);
    m_dirty = true;
    emit projectChanged();
    setStatus(QStringLiteral("Range saved to collection without changing the timeline"));
}

QString EditorController::saveTimelineRangeToCollection(const QString &collectionId, int track)
{
    if (!m_timelineRange.valid()) {
        setStatus(QStringLiteral("Drag a range on the timeline ruler first"));
        return {};
    }
    if (!isVideoTrack(track) && !isAudioTrack(track)) {
        setStatus(QStringLiteral("Select a timeline row for the marked range"));
        return {};
    }
    const auto coversRange = [&](const Clip &item) {
        return item.track == track && item.startFrame <= m_timelineRange.first
            && item.startFrame + item.durationFrames >= m_timelineRange.end;
    };
    const Clip *clip = nullptr;
    for (const auto &item : m_clips)
        if (item.id == m_selectedClipId && coversRange(item)) clip = &item;
    if (!clip)
        for (const auto &item : m_clips)
            if (coversRange(item)) clip = &item;
    if (!clip) {
        setStatus(QStringLiteral("The selected row needs one clip covering the full marked range"));
        return {};
    }
    const auto *asset = findAsset(clip->assetId);
    if (!asset) return {};
    const qint64 first = m_timelineRange.first;
    const qint64 end = m_timelineRange.end;
    const auto sourceFrame = [&](qint64 timelineFrame) {
        return clip->sourceFirstFrame + rescaleFrames(
            (timelineFrame - clip->startFrame) * clip->speedPercent,
            FrameRate{m_projectRate.numerator * 100, m_projectRate.denominator},
            asset->media.frameRate);
    };
    try {
        const qint64 sourceLimit = clip->sourceFirstFrame + clip->sourceFrameCount;
        const qint64 sourceFirst = std::clamp(sourceFrame(first), clip->sourceFirstFrame,
                                               sourceLimit - 1);
        const FrameRange source{sourceFirst,
            std::clamp(std::max(sourceFirst + 1, sourceFrame(end)),
                       sourceFirst + 1, sourceLimit)};
        if (!source.valid()) {
            setStatus(QStringLiteral("This range is shorter than one source frame"));
            return {};
        }
        QString target = collectionId;
        if (std::none_of(m_collections.cbegin(), m_collections.cend(),
            [&](const Collection &collection) { return collection.id == target; }))
            target = m_collections.isEmpty() ? QString{} : m_collections.first().id;
        if (target.isEmpty()) {
            createCollection(QStringLiteral("Selections"));
            target = m_collections.last().id;
        }
        Segment segment{freshId(),
            QStringLiteral("%1 · %2–%3").arg(asset->media.name)
                .arg(source.first).arg(source.end - 1),
            MediaRange{asset->id, source, clip->includeVideo, clip->includeAudio}, target};
        segment.timelineFrames = m_timelineRange.length();
        segment.timelineRate = m_projectRate;
        segment.speedPercent = clip->speedPercent;
        m_undo.append({Edit::AddSegment, {}, {}, segment, m_segments.size()});
        m_redo.clear();
        m_segments.append(segment);
        m_dirty = true;
        emit projectChanged();
        setStatus(QStringLiteral("Marked clip range saved to collection"));
        return target;
    } catch (const std::exception &error) {
        setStatus(QStringLiteral("Could not map range to source frames: %1")
            .arg(QString::fromUtf8(error.what())));
        return {};
    }
}

void EditorController::createCollection(const QString &name)
{
    const QString cleaned = name.trimmed();
    if (cleaned.isEmpty()) return;
    const Collection collection{freshId(), cleaned};
    Edit edit{Edit::AddCollection, {}, {}, {}, m_collections.size()};
    edit.collection = collection;
    m_undo.append(edit);
    m_redo.clear();
    m_collections.append(collection);
    m_dirty = true;
    emit projectChanged();
    setStatus(QStringLiteral("Collection created"));
}

void EditorController::renameCollection(const QString &id, const QString &name)
{
    const QString cleaned = name.trimmed();
    const auto it = std::find_if(m_collections.begin(), m_collections.end(),
        [&](const Collection &collection) { return collection.id == id; });
    if (it == m_collections.end() || cleaned.isEmpty() || it->name == cleaned) return;
    Edit edit{Edit::RenameCollection, {}, {}, {}, it - m_collections.begin()};
    edit.collection = *it;
    edit.extraCollection = {id, cleaned};
    m_undo.append(edit);
    m_redo.clear();
    it->name = cleaned;
    m_dirty = true;
    emit projectChanged();
}

void EditorController::removeCollection(const QString &id)
{
    const auto it = std::find_if(m_collections.cbegin(), m_collections.cend(),
        [&](const Collection &collection) { return collection.id == id; });
    if (it == m_collections.cend()) return;
    if (std::any_of(m_segments.cbegin(), m_segments.cend(),
        [&](const Segment &segment) { return segment.collectionId == id; })) {
        setStatus(QStringLiteral("Remove saved segments before deleting this collection"));
        return;
    }
    Edit edit{Edit::RemoveCollection, {}, {}, {}, it - m_collections.cbegin()};
    edit.collection = *it;
    m_undo.append(edit);
    m_redo.clear();
    m_collections.erase(it);
    m_dirty = true;
    emit projectChanged();
}

void EditorController::removeCollectionWithSegments(const QString &id)
{
    const auto collection = std::find_if(m_collections.cbegin(), m_collections.cend(),
        [&](const Collection &item) { return item.id == id; });
    if (collection == m_collections.cend()) return;
    Edit edit;
    edit.kind = Edit::RemoveCollectionWithSegments;
    edit.collection = *collection;
    edit.index = collection - m_collections.cbegin();
    for (qsizetype i = 0; i < m_segments.size(); ++i)
        if (m_segments[i].collectionId == id)
            edit.previousSegments.append({m_segments[i], i});
    m_segments.erase(std::remove_if(m_segments.begin(), m_segments.end(),
        [&](const Segment &item) { return item.collectionId == id; }), m_segments.end());
    m_collections.erase(collection);
    m_undo.append(edit);
    m_redo.clear();
    m_dirty = true;
    emit projectChanged();
    setStatus(QStringLiteral("Collection and %1 saved references removed")
        .arg(edit.previousSegments.size()));
}

void EditorController::moveSegments(const QStringList &ids, const QString &collectionId)
{
    if (std::none_of(m_collections.cbegin(), m_collections.cend(),
        [&](const Collection &item) { return item.id == collectionId; })) return;
    Edit edit;
    edit.kind = Edit::BatchSegments;
    for (qsizetype i = 0; i < m_segments.size(); ++i) {
        auto &segment = m_segments[i];
        if (!ids.contains(segment.id) || segment.collectionId == collectionId) continue;
        edit.previousSegments.append({segment, i});
        segment.collectionId = collectionId;
        edit.resultingSegments.append({segment, i});
    }
    if (edit.previousSegments.isEmpty()) return;
    m_undo.append(edit);
    m_redo.clear();
    m_dirty = true;
    emit projectChanged();
    setStatus(QStringLiteral("Moved %1 saved segments").arg(edit.previousSegments.size()));
}

void EditorController::copySegments(const QStringList &ids, const QString &collectionId)
{
    if (std::none_of(m_collections.cbegin(), m_collections.cend(),
        [&](const Collection &item) { return item.id == collectionId; })) return;
    Edit edit;
    edit.kind = Edit::BatchSegments;
    for (const auto &segment : m_segments) {
        if (!ids.contains(segment.id)) continue;
        Segment copy = segment;
        copy.id = freshId();
        copy.collectionId = collectionId;
        edit.resultingSegments.append({copy, m_segments.size() + edit.resultingSegments.size()});
    }
    if (edit.resultingSegments.isEmpty()) return;
    for (const auto &item : edit.resultingSegments) m_segments.append(item.segment);
    m_undo.append(edit);
    m_redo.clear();
    m_dirty = true;
    emit projectChanged();
    setStatus(QStringLiteral("Copied %1 saved segments").arg(edit.resultingSegments.size()));
}

void EditorController::removeSegments(const QStringList &ids)
{
    Edit edit;
    edit.kind = Edit::BatchSegments;
    for (qsizetype i = 0; i < m_segments.size(); ++i)
        if (ids.contains(m_segments[i].id)) edit.previousSegments.append({m_segments[i], i});
    if (edit.previousSegments.isEmpty()) return;
    m_segments.erase(std::remove_if(m_segments.begin(), m_segments.end(),
        [&](const Segment &item) { return ids.contains(item.id); }), m_segments.end());
    m_undo.append(edit);
    m_redo.clear();
    m_dirty = true;
    emit projectChanged();
    setStatus(QStringLiteral("Removed %1 saved segments").arg(edit.previousSegments.size()));
}

void EditorController::updateSegment(const QString &id, const QString &label,
                                     const QString &tags, const QString &notes, int rating)
{
    const auto it = std::find_if(m_segments.cbegin(), m_segments.cend(),
        [&](const Segment &segment) { return segment.id == id; });
    if (it != m_segments.cend())
        updateSegmentDetails(id, label, tags, notes, rating, it->caption);
}

void EditorController::updateSegmentDetails(const QString &id, const QString &label,
                                             const QString &tags, const QString &notes,
                                             int rating, const QString &caption)
{
    const auto it = std::find_if(m_segments.begin(), m_segments.end(),
        [&](const Segment &segment) { return segment.id == id; });
    if (it == m_segments.end() || label.trimmed().isEmpty() || rating < 0 || rating > 5) return;
    Segment changed = *it;
    changed.label = label.trimmed();
    changed.tags.clear();
    for (const auto &tag : tags.split(QLatin1Char(','), Qt::SkipEmptyParts))
        if (!tag.trimmed().isEmpty()) changed.tags.append(tag.trimmed());
    changed.notes = notes;
    changed.rating = rating;
    changed.caption = caption;
    if (changed.label == it->label && changed.tags == it->tags
        && changed.notes == it->notes && changed.rating == it->rating
        && changed.caption == it->caption) return;
    Edit edit{Edit::ReplaceSegment, {}, {}, *it, it - m_segments.begin()};
    edit.extraSegment = changed;
    m_undo.append(edit);
    m_redo.clear();
    *it = changed;
    m_dirty = true;
    emit projectChanged();
}

void EditorController::selectSegment(const QString &id)
{
    const auto it = std::find_if(m_segments.cbegin(), m_segments.cend(),
        [&](const Segment &segment) { return segment.id == id; });
    if (it == m_segments.cend() || !findAsset(it->source.assetId)) return;
    m_selectedAssetId = it->source.assetId;
    m_selectedClipId.clear();
    m_selectedTitleId.clear();
    m_currentRange = it->source.frames;
    emit selectionChanged();
}

void EditorController::addSegmentToTimeline(const QString &id)
{
    const auto segment = std::find_if(m_segments.cbegin(), m_segments.cend(),
        [&](const Segment &candidate) { return candidate.id == id; });
    if (segment == m_segments.cend()) return;
    const int track = segment->source.includeVideo ? 0 : 1;
    qint64 end = 0;
    for (const auto &clip : m_clips)
        if (clip.track == track)
            end = std::max(end, clip.startFrame + clip.durationFrames);
    placeSegmentOnTimeline(id, end, track, false, 0);
}

void EditorController::placeSegmentOnTimeline(const QString &id, qint64 startFrame,
                                               int track, bool snap,
                                               qint64 snapToleranceFrames)
{
    const auto segment = std::find_if(m_segments.cbegin(), m_segments.cend(),
        [&](const Segment &candidate) { return candidate.id == id; });
    if (segment == m_segments.cend() || startFrame < 0) return;
    const auto *asset = findAsset(segment->source.assetId);
    if (!asset || !QFileInfo::exists(asset->media.path)) {
        setStatus(QStringLiteral("Saved segment source media is missing"));
        return;
    }
    if (segment->source.includeVideo ? (track != -1 && !isVideoTrack(track))
                                     : (track != -2 && !isAudioTrack(track))) {
        setStatus(QStringLiteral("Drop video on a video row or audio on an audio row"));
        return;
    }
    try {
        const qint64 duration = asset->media.isStillImage ? framesForSeconds(5)
            : rescaleFrames(segment->source.frames.length(),
                            asset->media.frameRate, m_projectRate);
        if (duration <= 0) return;
        track = resolveNewTrack(track);
        if (track < 0) return;
        if (snap)
            startFrame = snappedStartFrame(startFrame, duration, track,
                                          snapToleranceFrames);
        const auto placement = timelinePlacement(startFrame, duration, track, {}, startFrame);
        startFrame = placement.startFrame;
        Clip clip{freshId(), asset->id, startFrame, duration,
                  segment->source.frames.first, segment->source.frames.length(), track};
        clip.includeVideo = segment->source.includeVideo;
        clip.includeAudio = segment->source.includeAudio;
        Edit edit; edit.kind = Edit::PlaceTimelineItems;
        applyTimelinePlacement(placement, track, {}, edit);
        m_clips.append(clip);
        edit.resultingClips.append(clip);
        finishTimelinePlacement(std::move(edit));
        selectClip(clip.id);
        setStatus(QStringLiteral("Saved segment placed at project frame %1")
            .arg(startFrame));
    } catch (const std::exception &error) {
        setStatus(QStringLiteral("Cannot add segment: %1").arg(QString::fromUtf8(error.what())));
    }
}

void EditorController::extractSelectedAudio()
{
    const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
        [&](const Clip &clip) { return clip.id == m_selectedClipId; });
    if (it == m_clips.cend()) return;
    const auto *asset = findAsset(it->assetId);
    if (!asset || !asset->media.hasAudio || !it->includeVideo || !it->includeAudio) {
        setStatus(QStringLiteral("Selected video clip has no audio to extract"));
        return;
    }
    Clip audio = *it;
    audio.id = freshId();
    audio.track = 1;
    audio.includeVideo = false;
    audio.includeAudio = true;
    const Clip original = *it;
    const qsizetype originalIndex = it - m_clips.cbegin();
    m_undo.append({Edit::ExtractAudio, original, audio, {}, originalIndex});
    m_redo.clear();
    m_clips[originalIndex].includeAudio = false;
    m_clips.append(audio);
    m_dirty = true;
    emit projectChanged();
    selectClip(audio.id);
    setStatus(QStringLiteral("Audio extracted to A1; source video clip is unchanged"));
}

void EditorController::removeSegment(const QString &id)
{
    const auto it = std::find_if(m_segments.cbegin(), m_segments.cend(),
        [&](const Segment &segment) { return segment.id == id; });
    if (it == m_segments.cend()) return;
    const auto index = it - m_segments.cbegin();
    m_undo.append({Edit::RemoveSegment, {}, {}, *it, index});
    m_redo.clear();
    m_segments.removeAt(index);
    m_dirty = true;
    emit projectChanged();
    setStatus(QStringLiteral("Segment removed"));
}

void EditorController::applyEdit(const Edit &edit, bool forward)
{
    const bool segmentEdit = edit.kind == Edit::AddSegment || edit.kind == Edit::RemoveSegment;
    if (edit.kind == Edit::SetTrackVisibility || edit.kind == Edit::SetTrackMute) {
        const bool state = forward ? edit.resultingTrackState : edit.previousTrackState;
        auto &set = edit.kind == Edit::SetTrackVisibility ? m_hiddenVideoTracks : m_mutedTracks;
        const bool enabled = edit.kind == Edit::SetTrackVisibility ? !state : state;
        if (enabled) set.insert(edit.stateTrack);
        else set.remove(edit.stateTrack);
    } else if (edit.kind == Edit::PlaceTimelineItems) {
        const auto &desiredClips = forward ? edit.resultingClips : edit.affectedClips;
        const auto &otherClips = forward ? edit.affectedClips : edit.resultingClips;
        for (const auto &other : otherClips) {
            if (std::none_of(desiredClips.cbegin(), desiredClips.cend(),
                [&](const Clip &clip) { return clip.id == other.id; })) {
                m_clips.erase(std::remove_if(m_clips.begin(), m_clips.end(),
                    [&](const Clip &clip) { return clip.id == other.id; }), m_clips.end());
            }
        }
        for (const auto &desired : desiredClips) {
            const auto it = std::find_if(m_clips.begin(), m_clips.end(),
                [&](const Clip &clip) { return clip.id == desired.id; });
            if (it == m_clips.end()) m_clips.append(desired);
            else *it = desired;
        }
        const auto &desiredTitles = forward ? edit.resultingTitles : edit.previousTitles;
        const auto &otherTitles = forward ? edit.previousTitles : edit.resultingTitles;
        for (const auto &other : otherTitles) {
            if (std::none_of(desiredTitles.cbegin(), desiredTitles.cend(),
                [&](const TitleClip &title) { return title.id == other.id; })) {
                m_titles.erase(std::remove_if(m_titles.begin(), m_titles.end(),
                    [&](const TitleClip &title) { return title.id == other.id; }), m_titles.end());
            }
        }
        for (const auto &desired : desiredTitles) {
            const auto it = std::find_if(m_titles.begin(), m_titles.end(),
                [&](const TitleClip &title) { return title.id == desired.id; });
            if (it == m_titles.end()) m_titles.append(desired);
            else *it = desired;
        }
        if (!edit.previousCaptionRows.isEmpty() || !edit.resultingCaptionRows.isEmpty())
            m_captionRows = forward ? edit.resultingCaptionRows : edit.previousCaptionRows;
    } else if (edit.kind == Edit::AddTitle || edit.kind == Edit::RemoveTitle) {
        const bool insert = (edit.kind == Edit::AddTitle) == forward;
        if (insert) m_titles.insert(std::min(edit.index, m_titles.size()), edit.title);
        else {
            const auto it = std::find_if(m_titles.cbegin(), m_titles.cend(),
                [&](const TitleClip &title) { return title.id == edit.title.id; });
            if (it != m_titles.cend()) m_titles.erase(it);
        }
        if (edit.kind == Edit::RemoveTitle)
            m_transitions = forward ? edit.resultingTransitions : edit.previousTransitions;
    } else if (edit.kind == Edit::ReplaceTitle) {
        const auto it = std::find_if(m_titles.begin(), m_titles.end(),
            [&](const TitleClip &title) { return title.id == edit.title.id; });
        if (it != m_titles.end()) *it = forward ? edit.extraTitle : edit.title;
    } else if (edit.kind == Edit::AddVideoTrack || edit.kind == Edit::AddAudioTrack) {
        auto &tracks = edit.kind == Edit::AddVideoTrack ? m_videoTracks : m_audioTracks;
        if (forward) {
            if (!tracks.contains(edit.addedTrack)) tracks.append(edit.addedTrack);
        }
        else if (std::none_of(m_clips.cbegin(), m_clips.cend(),
            [&](const Clip &clip) { return clip.track == edit.addedTrack; })
            && std::none_of(m_titles.cbegin(), m_titles.cend(),
                [&](const TitleClip &title) { return title.track == edit.addedTrack; }))
            tracks.removeAll(edit.addedTrack);
    } else if (edit.kind == Edit::ReplaceTrackEffects) {
        m_trackEffects = forward ? edit.resultingTrackEffects : edit.previousTrackEffects;
    } else if (edit.kind == Edit::ReorderTracks) {
        m_trackOrder = forward ? edit.resultingTrackOrder : edit.previousTrackOrder;
    } else if (edit.kind == Edit::ReplaceCaptions) {
        m_captionRows = forward ? edit.resultingCaptionRows : edit.previousCaptionRows;
        m_selectedCaptionId.clear();
        m_selectedCaptionRowId.clear();
    } else if (edit.kind == Edit::ReplaceTransitions) {
        m_transitions = forward ? edit.resultingTransitions : edit.previousTransitions;
        if (std::none_of(m_transitions.cbegin(), m_transitions.cend(),
            [&](const ClipTransition &transition) { return transition.id == m_selectedTransitionId; }))
            m_selectedTransitionId.clear();
        emit selectionChanged();
    } else if (edit.kind == Edit::ChangeProjectRate) {
        m_projectRate = forward ? edit.resultingRate : edit.previousRate;
        m_clips = forward ? edit.resultingClips : edit.affectedClips;
        m_titles = forward ? edit.resultingTitles : edit.previousTitles;
        m_trackEffects = forward ? edit.resultingTrackEffects : edit.previousTrackEffects;
        m_transitions = forward ? edit.resultingTransitions : edit.previousTransitions;
        m_captionRows = forward ? edit.resultingCaptionRows : edit.previousCaptionRows;
        m_playheadFrame = forward ? edit.resultingPlayhead : edit.previousPlayhead;
        emit playheadChanged();
    } else if (edit.kind == Edit::RippleDelete) {
        m_transitions = forward ? edit.resultingTransitions : edit.previousTransitions;
        if (forward) {
            const auto removed = std::find_if(m_clips.cbegin(), m_clips.cend(),
                [&](const Clip &clip) { return clip.id == edit.clip.id; });
            if (removed != m_clips.cend()) m_clips.erase(removed);
        } else {
            m_clips.insert(std::min(edit.index, m_clips.size()), edit.clip);
        }
        for (auto &clip : m_clips) {
            const auto original = std::find_if(edit.affectedClips.cbegin(),
                edit.affectedClips.cend(),
                [&](const Clip &affected) { return affected.id == clip.id; });
            if (original != edit.affectedClips.cend())
                clip.startFrame = original->startFrame
                    - (forward ? edit.clip.durationFrames : 0);
        }
    } else if (edit.kind == Edit::ExtractAudio) {
        const auto original = std::find_if(m_clips.begin(), m_clips.end(),
            [&](const Clip &clip) { return clip.id == edit.clip.id; });
        if (original != m_clips.end()) original->includeAudio = !forward;
        if (forward) m_clips.append(edit.extraClip);
        else {
            const auto added = std::find_if(m_clips.cbegin(), m_clips.cend(),
                [&](const Clip &clip) { return clip.id == edit.extraClip.id; });
            if (added != m_clips.cend()) m_clips.erase(added);
        }
    } else if (edit.kind == Edit::BatchSegments
               || edit.kind == Edit::RemoveCollectionWithSegments) {
        const auto &desired = forward ? edit.resultingSegments : edit.previousSegments;
        const auto affected = [&](const QString &id) {
            return std::any_of(edit.previousSegments.cbegin(), edit.previousSegments.cend(),
                [&](const Edit::IndexedSegment &item) { return item.segment.id == id; })
                || std::any_of(edit.resultingSegments.cbegin(), edit.resultingSegments.cend(),
                [&](const Edit::IndexedSegment &item) { return item.segment.id == id; });
        };
        m_segments.erase(std::remove_if(m_segments.begin(), m_segments.end(),
            [&](const Segment &item) { return affected(item.id); }), m_segments.end());
        for (const auto &item : desired)
            m_segments.insert(std::min(item.index, m_segments.size()), item.segment);
        if (edit.kind == Edit::RemoveCollectionWithSegments) {
            if (forward) {
                const auto collection = std::find_if(m_collections.cbegin(), m_collections.cend(),
                    [&](const Collection &item) { return item.id == edit.collection.id; });
                if (collection != m_collections.cend()) m_collections.erase(collection);
            } else m_collections.insert(std::min(edit.index, m_collections.size()), edit.collection);
        }
    } else if (edit.kind == Edit::RenameCollection) {
        const auto it = std::find_if(m_collections.begin(), m_collections.end(),
            [&](const Collection &collection) { return collection.id == edit.collection.id; });
        if (it != m_collections.end()) *it = forward ? edit.extraCollection : edit.collection;
    } else if (edit.kind == Edit::AddCollection || edit.kind == Edit::RemoveCollection) {
        const bool insert = (edit.kind == Edit::AddCollection) == forward;
        if (insert)
            m_collections.insert(std::min(edit.index, m_collections.size()), edit.collection);
        else {
            const auto it = std::find_if(m_collections.cbegin(), m_collections.cend(),
                [&](const Collection &collection) { return collection.id == edit.collection.id; });
            if (it != m_collections.cend()) m_collections.erase(it);
        }
    } else if (edit.kind == Edit::ReplaceSegment) {
        const auto it = std::find_if(m_segments.begin(), m_segments.end(),
            [&](const Segment &segment) { return segment.id == edit.segment.id; });
        if (it != m_segments.end()) *it = forward ? edit.extraSegment : edit.segment;
    } else if (edit.kind == Edit::ReplaceClip) {
        const auto it = std::find_if(m_clips.begin(), m_clips.end(),
            [&](const Clip &clip) { return clip.id == edit.clip.id; });
        if (it != m_clips.end()) *it = forward ? edit.extraClip : edit.clip;
        m_captionRows = forward ? edit.resultingCaptionRows : edit.previousCaptionRows;
    } else if (edit.kind == Edit::SplitClip) {
        const auto it = std::find_if(m_clips.begin(), m_clips.end(),
            [&](const Clip &clip) { return clip.id == edit.clip.id; });
        if (it != m_clips.end()) {
            if (forward) {
                it->durationFrames = edit.extraClip.startFrame - edit.clip.startFrame;
                trimGainEnd(*it, edit.clip);
                if (edit.extraClip.sourceFirstFrame != edit.clip.sourceFirstFrame)
                    it->sourceFrameCount = edit.extraClip.sourceFirstFrame
                        - edit.clip.sourceFirstFrame;
                m_clips.insert((it - m_clips.begin()) + 1, edit.extraClip);
            } else {
                const auto right = std::find_if(m_clips.begin(), m_clips.end(),
                    [&](const Clip &clip) { return clip.id == edit.extraClip.id; });
                if (right != m_clips.end()) m_clips.erase(right);
                const auto left = std::find_if(m_clips.begin(), m_clips.end(),
                    [&](const Clip &clip) { return clip.id == edit.clip.id; });
                if (left != m_clips.end()) *left = edit.clip;
            }
        }
    } else {
    const bool insert = (edit.kind == Edit::Add || edit.kind == Edit::AddSegment) == forward;
    if (segmentEdit) {
        if (insert)
            m_segments.insert(std::min(edit.index, m_segments.size()), edit.segment);
        else {
            const auto it = std::find_if(m_segments.cbegin(), m_segments.cend(),
                [&](const Segment &segment) { return segment.id == edit.segment.id; });
            if (it != m_segments.cend()) m_segments.removeAt(it - m_segments.cbegin());
        }
    } else if (insert) {
        m_clips.insert(std::min(edit.index, m_clips.size()), edit.clip);
    } else {
        const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
            [&](const Clip &clip) { return clip.id == edit.clip.id; });
        if (it != m_clips.cend()) m_clips.removeAt(it - m_clips.cbegin());
    }
    if (edit.kind == Edit::Remove)
        m_transitions = forward ? edit.resultingTransitions : edit.previousTransitions;
    if (edit.kind == Edit::Remove)
        m_captionRows = forward ? edit.resultingCaptionRows : edit.previousCaptionRows;
    }
    if (edit.kind != Edit::ReplaceClip && edit.kind != Edit::ReplaceTrackEffects
        && edit.kind != Edit::ChangeProjectRate) {
        m_selectedClipId.clear();
        m_selectedTitleId.clear();
    }
    m_dirty = true;
    emit projectChanged();
    emit selectionChanged();
}

void EditorController::undo()
{
    if (m_undo.isEmpty()) return;
    const auto &last = m_undo.last();
    if ((last.kind == Edit::AddVideoTrack || last.kind == Edit::AddAudioTrack)
        && (std::any_of(m_clips.cbegin(), m_clips.cend(),
            [&](const Clip &clip) { return clip.track == last.addedTrack; })
            || std::any_of(m_titles.cbegin(), m_titles.cend(),
                [&](const TitleClip &title) { return title.track == last.addedTrack; }))) {
        setStatus(QStringLiteral("Remove clips from this row before undoing its creation"));
        return;
    }
    const auto edit = m_undo.takeLast();
    applyEdit(edit, false);
    m_redo.append(edit);
    emit projectChanged();
    setStatus(QStringLiteral("Undo"));
}

void EditorController::redo()
{
    if (m_redo.isEmpty()) return;
    const auto edit = m_redo.takeLast();
    applyEdit(edit, true);
    m_undo.append(edit);
    emit projectChanged();
    setStatus(QStringLiteral("Redo"));
}

QByteArray EditorController::projectContents(const QString &directory) const
{
    QJsonArray assetsArray;
    for (const auto &asset : m_assets) assetsArray.append(mediaJson(asset, directory));
    QJsonArray clipsArray;
    for (const auto &clip : m_clips)
        clipsArray.append(QJsonObject{{"id", clip.id}, {"assetId", clip.assetId},
            {"startFrame", QString::number(clip.startFrame)},
            {"durationFrames", QString::number(clip.durationFrames)},
            {"sourceFirstFrame", QString::number(clip.sourceFirstFrame)},
            {"sourceFrameCount", QString::number(clip.sourceFrameCount)},
            {"track", clip.track}, {"positionX", clip.positionX},
            {"positionY", clip.positionY}, {"scalePercent", clip.scalePercent},
            {"positionXKeyframes", keyframesJson(clip.positionXKeyframes)},
            {"positionYKeyframes", keyframesJson(clip.positionYKeyframes)},
            {"scaleKeyframes", keyframesJson(clip.scaleKeyframes)},
            {"rotationKeyframes", keyframesJson(clip.rotationKeyframes)},
            {"opacityKeyframes", keyframesJson(clip.opacityKeyframes)},
            {"rotationDegrees", clip.rotationDegrees},
            {"opacityPercent", clip.opacityPercent},
            {"includeVideo", clip.includeVideo}, {"includeAudio", clip.includeAudio},
            {"gainPercent", clip.gainPercent},
            {"gainKeyframes", keyframesJson(clip.gainKeyframes)},
            {"fadeInFrames", QString::number(clip.fadeInFrames)},
            {"fadeOutFrames", QString::number(clip.fadeOutFrames)},
            {"speedPercent", clip.speedPercent},
            {"effectId", clip.effectId}, {"effectStrength", clip.effectStrength},
            {"audioEffectId", clip.audioEffectId},
            {"audioEffectStrength", clip.audioEffectStrength},
            {"voiceChangePath", clip.voiceChangePath},
            {"voiceName", clip.voiceName}, {"voiceMode", clip.voiceMode},
            {"voiceSourceFirstFrame", QString::number(clip.voiceSourceFirstFrame)},
            {"voiceSourceFrameCount", QString::number(clip.voiceSourceFrameCount)},
            {"voiceChangeEnabled", clip.voiceChangeEnabled}});
    QJsonArray trackEffectsArray;
    for (const auto &effect : m_trackEffects) {
        QJsonArray parameters;
        for (const auto &parameter : effect.parameters)
            parameters.append(QJsonObject{{"id", parameter.id}, {"value", parameter.value},
                {"keyframes", keyframesJson(parameter.keyframes)}});
        trackEffectsArray.append(QJsonObject{{"track", effect.track},
            {"id", effect.id}, {"strength", effect.strength},
            {"keyframes", keyframesJson(effect.keyframes)}, {"parameters", parameters}});
    }
    QJsonArray transitionsArray;
    for (const auto &transition : m_transitions) {
        QJsonObject parameters;
        for (auto it = transition.parameters.cbegin(); it != transition.parameters.cend(); ++it)
            parameters.insert(it.key(), it.value());
        transitionsArray.append(QJsonObject{{"id", transition.id},
            {"definitionId", transition.definitionId},
            {"outgoingClipId", transition.outgoingClipId},
            {"incomingClipId", transition.incomingClipId},
            {"durationFrames", QString::number(transition.durationFrames)},
            {"balance", transition.balance}, {"parameters", parameters}});
    }
    QJsonArray videoTracksArray;
    for (int track : m_videoTracks) videoTracksArray.append(track);
    QJsonArray audioTracksArray;
    for (int track : m_audioTracks) audioTracksArray.append(track);
    QJsonArray trackOrderArray;
    for (int track : m_trackOrder) trackOrderArray.append(track);
    QJsonArray hiddenVideoTracksArray;
    for (int track : m_videoTracks)
        if (m_hiddenVideoTracks.contains(track)) hiddenVideoTracksArray.append(track);
    QJsonArray mutedTracksArray;
    for (const auto track : m_videoTracks + m_audioTracks)
        if (m_mutedTracks.contains(track)) mutedTracksArray.append(track);
    QJsonArray segmentsArray;
    for (const auto &segment : m_segments) {
        QJsonArray tags;
        for (const auto &tag : segment.tags) tags.append(tag);
        segmentsArray.append(QJsonObject{{"id", segment.id}, {"label", segment.label},
            {"assetId", segment.source.assetId},
            {"firstFrame", QString::number(segment.source.frames.first)},
            {"endFrame", QString::number(segment.source.frames.end)},
            {"includeVideo", segment.source.includeVideo},
            {"includeAudio", segment.source.includeAudio},
            {"collectionId", segment.collectionId}, {"tags", tags},
            {"notes", segment.notes}, {"rating", segment.rating},
            {"caption", segment.caption},
            {"timelineFrames", QString::number(segment.timelineFrames)},
            {"timelineRateNumerator", QString::number(segment.timelineRate.numerator)},
            {"timelineRateDenominator", QString::number(segment.timelineRate.denominator)},
            {"speedPercent", segment.speedPercent}});
    }
    QJsonArray collectionsArray;
    for (const auto &collection : m_collections)
        collectionsArray.append(QJsonObject{{"id", collection.id}, {"name", collection.name}});
    QJsonArray titlesArray;
    for (const auto &title : m_titles)
        titlesArray.append(QJsonObject{{"id", title.id}, {"text", title.text},
            {"startFrame", QString::number(title.startFrame)},
            {"durationFrames", QString::number(title.durationFrames)},
            {"fontPixels", title.fontPixels}, {"color", title.color},
            {"fontFamily", title.fontFamily}, {"bold", title.bold},
            {"italic", title.italic}, {"backgroundColor", title.backgroundColor},
            {"backgroundOpacity", title.backgroundOpacity},
            {"positionX", title.positionX}, {"positionY", title.positionY},
            {"anchorX", title.anchorX}, {"anchorY", title.anchorY},
            {"track", title.track}, {"scalePercent", title.scalePercent},
            {"positionXKeyframes", keyframesJson(title.positionXKeyframes)},
            {"positionYKeyframes", keyframesJson(title.positionYKeyframes)},
            {"scaleKeyframes", keyframesJson(title.scaleKeyframes)}});
    QJsonArray captionRowsArray;
    for (const auto &row : m_captionRows) {
        QJsonArray cues;
        for (const auto &cue : row.cues) {
            QJsonArray words;
            for (const auto &word : cue.words)
                words.append(QJsonObject{{"id", word.id}, {"text", word.text},
                    {"firstFrame", QString::number(word.firstFrame)},
                    {"endFrame", QString::number(word.endFrame)}});
            cues.append(QJsonObject{{"id", cue.id}, {"groupId", cue.groupId},
                {"words", words}});
        }
        captionRowsArray.append(QJsonObject{{"id", row.id}, {"sourceClipId", row.sourceClipId},
            {"trackId", row.trackId}, {"targetTrack", row.targetTrack},
            {"displayMode", row.displayMode}, {"maxWordsPerCue", row.maxWordsPerCue},
            {"fontFamily", row.fontFamily},
            {"fontPixels", row.fontPixels}, {"bold", row.bold}, {"italic", row.italic},
            {"color", row.color}, {"highlightColor", row.highlightColor},
            {"backgroundColor", row.backgroundColor}, {"backgroundOpacity", row.backgroundOpacity},
            {"positionX", row.positionX}, {"positionY", row.positionY},
            {"scalePercent", row.scalePercent},
            {"positionXKeyframes", keyframesJson(row.positionXKeyframes)},
            {"positionYKeyframes", keyframesJson(row.positionYKeyframes)},
            {"scaleKeyframes", keyframesJson(row.scaleKeyframes)},
            {"visible", row.visible}, {"cues", cues}});
    }
    QJsonObject root{{"schemaVersion", 1}, {"projectName", m_projectName},
                     {"projectFrameRate", QJsonObject{{"numerator", m_projectRate.numerator},
                                                      {"denominator", m_projectRate.denominator}}},
                     {"assets", assetsArray}, {"clips", clipsArray},
                     {"projectAspectRatio", m_projectAspectRatio},
                     {"exportResolution", m_exportResolution},
                     {"trackEffects", trackEffectsArray},
                     {"transitions", transitionsArray},
                     {"videoTracks", videoTracksArray}, {"audioTracks", audioTracksArray},
                     {"trackOrder", trackOrderArray},
                     {"hiddenVideoTracks", hiddenVideoTracksArray}, {"mutedTracks", mutedTracksArray},
                     {"segments", segmentsArray}, {"collections", collectionsArray},
                     {"datasetFrameRule", m_frameRule}, {"titles", titlesArray},
                     {"captionRows", captionRowsArray}};
    return QJsonDocument(root).toJson();
}

void EditorController::saveProject(const QUrl &directoryUrl)
{
    const QString previousPath = m_projectFilePath;
    QString path = directoryUrl.isLocalFile() ? directoryUrl.toLocalFile()
        : (m_demoProject ? QString{} : m_projectFilePath);
    if (path.isEmpty()) {
        setStatus(QStringLiteral("Choose a project file"));
        return;
    }
    const bool legacyFolder = QFileInfo(path).isDir();
    if (legacyFolder) path = QDir(path).filePath(QStringLiteral("project.json"));
    else if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".datacutproject");
    const QString directory = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(directory)) {
        setStatus(QStringLiteral("Could not create project location"));
        return;
    }
    QSaveFile file(path);
    const QString previousName = m_projectName;
    if (!legacyFolder && QFileInfo(path).fileName() != QStringLiteral("project.json"))
        m_projectName = QFileInfo(path).completeBaseName();
    const QByteArray contents = projectContents(directory);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit()) {
        m_projectName = previousName;
        setStatus(QStringLiteral("Project save failed: %1").arg(file.errorString()));
        return;
    }
    m_projectDirectory = directory;
    m_projectFilePath = QFileInfo(path).absoluteFilePath();
    m_demoProject = false;
    m_dirty = false;
    ++m_manualSaveSerial;
    m_autosaveDebounce.stop();
    if (m_autoNamedProject && !previousPath.isEmpty() && previousPath != m_projectFilePath)
        QFile::remove(previousPath);
    m_autoNamedProject = false;
    recordRecentProject(m_projectFilePath);
    if (m_recoveryAvailable) {
        m_recoveryAvailable = false;
        emit recoveryChanged();
    }
    emit projectChanged();
    setStatus(QStringLiteral("Project saved"));
}

void EditorController::autosave()
{
    if (!m_dirty || m_autosavePending || m_demoProject) return;
    if (m_projectFilePath.isEmpty()) {
        const QString base = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        const QString directory = QDir(base).filePath(QStringLiteral("DataCut/Projects"));
        if (!QDir().mkpath(directory)) {
            setStatus(QStringLiteral("Autosave failed: could not create Documents/DataCut/Projects"));
            return;
        }
        const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
        QString path = QDir(directory).filePath(QStringLiteral("Project-%1.datacutproject").arg(stamp));
        for (int suffix = 2; QFileInfo::exists(path); ++suffix)
            path = QDir(directory).filePath(QStringLiteral("Project-%1-%2.datacutproject").arg(stamp).arg(suffix));
        m_projectDirectory = directory;
        m_projectFilePath = path;
        m_projectName = QFileInfo(path).completeBaseName();
        m_autoNamedProject = true;
        emit projectChanged();
    }
    const QString directory = m_projectDirectory;
    const QString path = m_projectFilePath;
    const QByteArray contents = projectContents(directory);
    const auto generation = m_projectGeneration;
    const auto revision = m_editRevision;
    const auto manualSaveSerial = m_manualSaveSerial;
    const bool autoNamed = m_autoNamedProject;
    m_autosavePending = true;
    auto *watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this,
        [this, watcher, generation, revision, manualSaveSerial, autoNamed, path] {
            m_autosavePending = false;
            if (generation == m_projectGeneration && path != m_projectFilePath && autoNamed)
                QFile::remove(path);
            if (generation == m_projectGeneration && path == m_projectFilePath) {
                if (watcher->result()) {
                    if (manualSaveSerial != m_manualSaveSerial) {
                        saveProject(QUrl::fromLocalFile(path));
                        watcher->deleteLater();
                        return;
                    }
                    recordRecentProject(path);
                    if (revision == m_editRevision) {
                        m_dirty = false;
                        emit projectChanged();
                    } else m_autosaveDebounce.start();
                } else setStatus(QStringLiteral("Autosave failed: could not write %1").arg(path));
            }
            watcher->deleteLater();
        });
    watcher->setFuture(QtConcurrent::run([path, contents] {
        QSaveFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size()
            && file.commit();
    }));
}

void EditorController::openProject(const QUrl &directoryUrl)
{
    if (!directoryUrl.isLocalFile()) return;
    const QFileInfo target(directoryUrl.toLocalFile());
    openProjectFile(target.isDir() ? target.absoluteFilePath() : target.absolutePath(),
                    target.isDir() ? QStringLiteral("project.json") : target.fileName(), false);
}

void EditorController::openProjectLocation() const
{
    if (!m_projectFilePath.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_projectFilePath).absolutePath()));
}

void EditorController::recoverProject()
{
    if (!m_recoveryAvailable || m_projectDirectory.isEmpty()) return;
    openProjectFile(m_projectDirectory, QStringLiteral("recovery/autosave.json"), true);
}

void EditorController::discardRecovery()
{
    if (m_projectDirectory.isEmpty()) return;
    QFile::remove(QDir(m_projectDirectory).filePath(QStringLiteral("recovery/autosave.json")));
    m_recoveryAvailable = false;
    emit recoveryChanged();
}

void EditorController::openProjectFile(const QString &directory,
                                       const QString &fileName, bool recovered)
{
    QFile file(QDir(directory).filePath(fileName));
    if (!file.open(QIODevice::ReadOnly)) {
        setStatus(QStringLiteral("Could not open project file: %1").arg(file.fileName()));
        return;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || document.object().value("schemaVersion").toInt() != 1) {
        setStatus(QStringLiteral("Unsupported or invalid project file"));
        return;
    }
    const auto root = document.object();
    const auto rate = root.value("projectFrameRate").toObject();
    const FrameRate loadedRate{rate.value("numerator").toInteger(),
                               rate.value("denominator").toInteger()};
    if (!root.value("assets").isArray() || !root.value("clips").isArray()
        || !loadedRate.valid() || loadedRate.numerator > 240000
        || loadedRate.denominator > 1001) {
        setStatus(QStringLiteral("Project has unsupported timeline metadata"));
        return;
    }
    QVector<Asset> loadedAssets;
    QVector<Clip> loadedClips;
    QVector<TrackEffect> loadedTrackEffects;
    QVector<ClipTransition> loadedTransitions;
    QVector<int> loadedVideoTracks{0, 3};
    QVector<int> loadedAudioTracks{1, 2};
    QVector<int> loadedTrackOrder;
    if (root.contains("videoTracks") || root.contains("audioTracks")) {
        if (!root.value("videoTracks").isArray() || !root.value("audioTracks").isArray()) {
            setStatus(QStringLiteral("Project contains invalid track metadata"));
            return;
        }
        loadedVideoTracks.clear();
        loadedAudioTracks.clear();
        for (const auto &entry : root.value("videoTracks").toArray())
            loadedVideoTracks.append(entry.toInt(-1));
        for (const auto &entry : root.value("audioTracks").toArray())
            loadedAudioTracks.append(entry.toInt(-1));
        const auto validList = [](const QVector<int> &tracks, bool video) {
            if (tracks.isEmpty() || tracks.size() > 30) return false;
            QVector<int> seen;
            for (int id : tracks) {
                if ((video ? (id != 0 && id != 3 && (id < 4 || id > 31))
                           : (id != 1 && id != 2 && (id < 32 || id > 59)))
                    || seen.contains(id)) return false;
                seen.append(id);
            }
            return true;
        };
        if (!validList(loadedVideoTracks, true) || !validList(loadedAudioTracks, false)) {
            setStatus(QStringLiteral("Project contains invalid track metadata"));
            return;
        }
    }
    QSet<int> loadedHiddenVideoTracks;
    QSet<int> loadedMutedTracks;
    if ((root.contains("hiddenVideoTracks") && !root.value("hiddenVideoTracks").isArray())
        || (root.contains("mutedTracks") && !root.value("mutedTracks").isArray())) {
        setStatus(QStringLiteral("Project contains invalid track state"));
        return;
    }
    for (const auto &entry : root.value("hiddenVideoTracks").toArray()) {
        const int track = entry.toInt(-1);
        if (!loadedVideoTracks.contains(track)) {
            setStatus(QStringLiteral("Project contains invalid hidden video track"));
            return;
        }
        loadedHiddenVideoTracks.insert(track);
    }
    for (const auto &entry : root.value("mutedTracks").toArray()) {
        const int track = entry.toInt(-1);
        if (!loadedVideoTracks.contains(track) && !loadedAudioTracks.contains(track)) {
            setStatus(QStringLiteral("Project contains invalid muted track"));
            return;
        }
        loadedMutedTracks.insert(track);
    }
    QVector<Segment> loadedSegments;
    QVector<Collection> loadedCollections;
    QVector<TitleClip> loadedTitles;
    QVector<CaptionRow> loadedCaptionRows;
    for (const auto &value : root.value("captionRows").toArray()) {
        const auto item = value.toObject();
        CaptionRow row;
        row.id = item.value("id").toString();
        row.sourceClipId = item.value("sourceClipId").toString();
        row.trackId = item.value("trackId").toInt(1000);
        row.targetTrack = item.value("targetTrack").toInt();
        row.displayMode = item.value("displayMode").toString(QStringLiteral("sentence"));
        row.maxWordsPerCue = item.value("maxWordsPerCue").toInt(-1);
        if (row.maxWordsPerCue != -1 && (row.maxWordsPerCue < 1 || row.maxWordsPerCue > 1000)) {
            setStatus(QStringLiteral("Project contains an invalid caption word limit"));
            return;
        }
        row.fontFamily = item.value("fontFamily").toString(QStringLiteral("Segoe UI"));
        row.fontPixels = item.value("fontPixels").toInt(72);
        row.bold = item.value("bold").toBool(true);
        row.italic = item.value("italic").toBool();
        row.color = item.value("color").toString(QStringLiteral("#ffffff"));
        row.highlightColor = item.value("highlightColor").toString(QStringLiteral("#ffd34d"));
        row.backgroundColor = item.value("backgroundColor").toString(QStringLiteral("#000000"));
        row.backgroundOpacity = item.value("backgroundOpacity").toInt(65);
        row.positionX = item.value("positionX").toInt(50);
        row.positionY = item.value("positionY").toInt(84);
        row.scalePercent = item.value("scalePercent").toInt(100);
        if (row.scalePercent < 25 || row.scalePercent > 400
            || !readKeyframes(item.value("positionXKeyframes"), std::numeric_limits<qint64>::max(),
                300, row.positionXKeyframes, -200)
            || !readKeyframes(item.value("positionYKeyframes"), std::numeric_limits<qint64>::max(),
                300, row.positionYKeyframes, -200)
            || !readKeyframes(item.value("scaleKeyframes"), std::numeric_limits<qint64>::max(),
                400, row.scaleKeyframes, 25)) {
            setStatus(QStringLiteral("Project contains invalid caption transforms"));
            return;
        }
        row.visible = item.value("visible").toBool(true);
        for (const auto &cueValue : item.value("cues").toArray()) {
            const auto cueObject = cueValue.toObject();
            CaptionCue cue{cueObject.value("id").toString(), {},
                cueObject.value("groupId").toString()};
            if (cue.groupId.isEmpty()) cue.groupId = cue.id;
            for (const auto &wordValue : cueObject.value("words").toArray()) {
                const auto word = wordValue.toObject();
                cue.words.append({word.value("id").toString(), word.value("text").toString(),
                    word.value("firstFrame").toString().toLongLong(),
                    word.value("endFrame").toString().toLongLong()});
            }
            if (!cue.words.isEmpty()) row.cues.append(cue);
        }
        if (row.id.isEmpty() || (!loadedVideoTracks.contains(row.targetTrack)
            && !loadedAudioTracks.contains(row.targetTrack))
            || row.trackId < 1000 || row.cues.isEmpty()) {
            setStatus(QStringLiteral("Project contains an invalid caption row"));
            return;
        }
        loadedCaptionRows.append(row);
    }
    if (root.contains("trackOrder")) {
        if (!root.value("trackOrder").isArray()) {
            setStatus(QStringLiteral("Project contains invalid track order"));
            return;
        }
        for (const auto &entry : root.value("trackOrder").toArray()) {
            const int id = entry.toInt(-1);
            const bool known = loadedVideoTracks.contains(id) || loadedAudioTracks.contains(id)
                || std::any_of(loadedCaptionRows.cbegin(), loadedCaptionRows.cend(),
                    [id](const CaptionRow &row) { return row.trackId == id; });
            if (!known || loadedTrackOrder.contains(id)) {
                setStatus(QStringLiteral("Project contains invalid track order"));
                return;
            }
            loadedTrackOrder.append(id);
        }
    }
    for (const auto &value : root.value("titles").toArray()) {
        const auto item = value.toObject();
        TitleClip title{item.value("id").toString(), item.value("text").toString(),
            item.value("startFrame").toString().toLongLong(),
            item.value("durationFrames").toString().toLongLong(),
            item.value("fontPixels").toInt(88),
            item.value("color").toString(QStringLiteral("#ffffff"))};
        title.fontFamily = item.value("fontFamily").toString(QStringLiteral("Segoe UI"));
        title.bold = item.value("bold").toBool(true);
        title.italic = item.value("italic").toBool();
        title.backgroundColor = item.value("backgroundColor").toString(QStringLiteral("#000000"));
        title.backgroundOpacity = item.value("backgroundOpacity").toInt();
        title.positionX = item.value("positionX").toInt(50);
        title.positionY = item.value("positionY").toInt(50);
        title.anchorX = item.value("anchorX").toInt(50);
        title.anchorY = item.value("anchorY").toInt(50);
        title.track = item.value("track").toInt(3);
        title.scalePercent = item.value("scalePercent").toInt(100);
        if (!readKeyframes(item.value("positionXKeyframes"), title.durationFrames - 1,
                300, title.positionXKeyframes, -200)
            || !readKeyframes(item.value("positionYKeyframes"), title.durationFrames - 1,
                300, title.positionYKeyframes, -200)
            || !readKeyframes(item.value("scaleKeyframes"), title.durationFrames - 1,
                400, title.scaleKeyframes, 25)) {
            setStatus(QStringLiteral("Project contains invalid text keyframes"));
            return;
        }
        if (title.id.isEmpty() || title.text.trimmed().isEmpty() || title.startFrame < 0
            || title.durationFrames <= 0 || title.fontPixels < 12 || title.fontPixels > 300
            || !QColor(title.color).isValid() || !QColor(title.backgroundColor).isValid()
            || title.backgroundOpacity < 0 || title.backgroundOpacity > 100
            || title.positionX < -200 || title.positionX > 300
            || title.positionY < -200 || title.positionY > 300
            || title.scalePercent < 25 || title.scalePercent > 400
            || !loadedVideoTracks.contains(title.track)
            || title.anchorX < 0 || title.anchorX > 100
            || title.anchorY < 0 || title.anchorY > 100) {
            setStatus(QStringLiteral("Project contains an invalid text title"));
            return;
        }
        loadedTitles.append(title);
    }
    for (const auto &value : root.value("collections").toArray()) {
        const auto item = value.toObject();
        const Collection collection{item.value("id").toString(), item.value("name").toString()};
        if (collection.id.isEmpty() || collection.name.trimmed().isEmpty()) {
            setStatus(QStringLiteral("Project contains an invalid collection"));
            return;
        }
        loadedCollections.append(collection);
    }
    if (loadedCollections.isEmpty())
        loadedCollections.append({QStringLiteral("selections"), QStringLiteral("Selections")});
    for (const auto &value : root.value("assets").toArray()) {
        const auto item = value.toObject();
        MediaInfo m;
        const QString storedPath = item.value("path").toString();
        m.path = QFileInfo(storedPath).isRelative()
            ? QDir(directory).absoluteFilePath(storedPath) : storedPath;
        m.name = item.value("name").toString();
        m.frameRate = {item.value("rateNumerator").toString().toLongLong(),
                       item.value("rateDenominator").toString().toLongLong()};
        m.frameCount = item.value("frameCount").toString().toLongLong();
        m.durationUs = item.value("durationUs").toString().toLongLong();
        m.width = item.value("width").toInt();
        m.height = item.value("height").toInt();
        m.hasVideo = item.value("hasVideo").toBool();
        m.hasAudio = item.value("hasAudio").toBool();
        m.isStillImage = item.value("isStillImage").toBool();
        m.audioSampleRate = item.value("audioSampleRate").toInt();
        if (!m.hasVideo && m.hasAudio && m.audioSampleRate == 0)
            m.audioSampleRate = static_cast<int>(m.frameRate.numerator);
        if (item.value("id").toString().isEmpty() || m.path.isEmpty() || !m.frameRate.valid()) {
            setStatus(QStringLiteral("Project contains invalid media metadata"));
            return;
        }
        loadedAssets.append({item.value("id").toString(), m, {}});
        const QString cachedProxy = proxyCachePath(m.path);
        if (QFileInfo::exists(cachedProxy))
            loadedAssets.last().proxyPath = cachedProxy;
    }
    for (const auto &value : root.value("clips").toArray()) {
        const auto item = value.toObject();
        Clip clip{item.value("id").toString(), item.value("assetId").toString(),
                  item.value("startFrame").toString().toLongLong(),
                  item.value("durationFrames").toString().toLongLong(),
                  item.value("sourceFirstFrame").toString().toLongLong(),
                  item.value("sourceFrameCount").toString().toLongLong(),
                  item.value("track").toInt()};
        clip.positionX = item.value("positionX").toInt();
        clip.positionY = item.value("positionY").toInt();
        clip.scalePercent = item.value("scalePercent").toInt(100);
        clip.rotationDegrees = item.value("rotationDegrees").toInt();
        clip.opacityPercent = item.value("opacityPercent").toInt(100);
        if (!readKeyframes(item.value("positionXKeyframes"), clip.durationFrames - 1,
                4000, clip.positionXKeyframes, -4000)
            || !readKeyframes(item.value("positionYKeyframes"), clip.durationFrames - 1,
                4000, clip.positionYKeyframes, -4000)
                || !readKeyframes(item.value("scaleKeyframes"), clip.durationFrames - 1,
                    400, clip.scaleKeyframes, 25)
                || !readKeyframes(item.value("rotationKeyframes"), clip.durationFrames - 1,
                    180, clip.rotationKeyframes, -180)
                || !readKeyframes(item.value("opacityKeyframes"), clip.durationFrames - 1,
                    100, clip.opacityKeyframes)) {
            setStatus(QStringLiteral("Project contains invalid clip transform keyframes"));
            return;
        }
        const auto loadedAsset = std::find_if(loadedAssets.cbegin(), loadedAssets.cend(),
            [&](const Asset &asset) { return asset.id == clip.assetId; });
        clip.includeVideo = item.contains("includeVideo") ? item.value("includeVideo").toBool()
            : (loadedAsset != loadedAssets.cend() && loadedAsset->media.hasVideo);
        clip.includeAudio = item.contains("includeAudio") ? item.value("includeAudio").toBool()
            : (loadedAsset != loadedAssets.cend() && loadedAsset->media.hasAudio);
        clip.gainPercent = item.value("gainPercent").toInt(100);
        if (!readKeyframes(item.value("gainKeyframes"), clip.durationFrames, 300,
                           clip.gainKeyframes)) {
            setStatus(QStringLiteral("Project contains invalid audio gain points"));
            return;
        }
        clip.fadeInFrames = item.value("fadeInFrames").toString().toLongLong();
        clip.fadeOutFrames = item.value("fadeOutFrames").toString().toLongLong();
        clip.speedPercent = item.value("speedPercent").toInt(100);
        clip.effectId = item.value("effectId").toString();
        clip.effectStrength = item.value("effectStrength").toInt(100);
        clip.audioEffectId = item.value("audioEffectId").toString();
        clip.audioEffectStrength = item.value("audioEffectStrength").toInt(100);
        clip.voiceChangePath = item.value("voiceChangePath").toString();
        clip.voiceName = item.value("voiceName").toString();
        clip.voiceMode = item.value("voiceMode").toString();
        clip.voiceSourceFirstFrame = item.value("voiceSourceFirstFrame").toString().toLongLong();
        clip.voiceSourceFrameCount = item.value("voiceSourceFrameCount").toString().toLongLong();
        clip.voiceChangeEnabled = item.value("voiceChangeEnabled").toBool();
        if (clip.sourceFrameCount == 0) {
            const auto asset = std::find_if(loadedAssets.cbegin(), loadedAssets.cend(),
                [&](const Asset &candidate) { return candidate.id == clip.assetId; });
            if (asset != loadedAssets.cend())
                clip.sourceFrameCount = asset->media.frameCount;
        }
        if (clip.id.isEmpty() || clip.startFrame < 0 || clip.durationFrames <= 0 ||
            clip.sourceFirstFrame < 0 || clip.sourceFrameCount <= 0 ||
            clip.scalePercent < 25 || clip.scalePercent > 400 ||
            clip.rotationDegrees < -180 || clip.rotationDegrees > 180 ||
            clip.opacityPercent < 0 || clip.opacityPercent > 100 ||
            clip.gainPercent < 0 || clip.gainPercent > 300 ||
            clip.fadeInFrames < 0 || clip.fadeOutFrames < 0 ||
            clip.fadeInFrames + clip.fadeOutFrames > clip.durationFrames ||
            clip.speedPercent < 50 || clip.speedPercent > 200 ||
            !effectExists(clip.effectId) || clip.effectStrength < 0
            || clip.effectStrength > 100 ||
            !audioEffectExists(clip.audioEffectId)
            || clip.audioEffectStrength < 0 || clip.audioEffectStrength > 100 ||
            std::none_of(loadedAssets.cbegin(), loadedAssets.cend(),
                [&](const Asset &asset) { return asset.id == clip.assetId &&
                    (!clip.includeVideo || asset.media.hasVideo) &&
                    (!clip.includeAudio || asset.media.hasAudio) &&
                    (clip.includeVideo || clip.includeAudio) &&
                    (clip.includeVideo ? loadedVideoTracks.contains(clip.track)
                                       : loadedAudioTracks.contains(clip.track)) &&
                    clip.sourceFirstFrame + clip.sourceFrameCount <= asset.media.frameCount; })) {
            setStatus(QStringLiteral("Project contains an invalid timeline clip"));
            return;
        }
        loadedClips.append(clip);
    }
    for (auto &asset : loadedAssets)
        if (std::none_of(loadedClips.cbegin(), loadedClips.cend(), [&](const Clip &clip) {
            return clip.assetId == asset.id;
        })) normalizeMp3CoverArt(asset.media);
    for (const auto &entry : root.value("trackEffects").toArray()) {
        const auto item = entry.toObject();
        TrackEffect effect{item.value("track").toInt(-1),
            item.value("id").toString(), item.value("strength").toInt(-1), {}};
        const bool visualTrack = loadedVideoTracks.contains(effect.track)
            || std::any_of(loadedCaptionRows.cbegin(), loadedCaptionRows.cend(),
                [&](const CaptionRow &row) { return row.trackId == effect.track; });
        if (!visualTrack || effect.id.isEmpty()
            || !effectExists(effect.id) || effect.strength < 0 || effect.strength > 100
            || !readKeyframes(item.value("keyframes"), std::numeric_limits<qint64>::max(),
                              100, effect.keyframes)
            || std::any_of(loadedTrackEffects.cbegin(), loadedTrackEffects.cend(),
                [&](const TrackEffect &other) {
                    return other.track == effect.track && other.id == effect.id;
                })) {
            setStatus(QStringLiteral("Project contains an invalid track effect"));
            return;
        }
        const auto *definition = videoEffectDefinition(effect.id);
        if (!definition || (item.contains("parameters") && !item.value("parameters").isArray())) {
            setStatus(QStringLiteral("Project contains invalid effect parameters"));
            return;
        }
        const QJsonArray storedParameters = item.value("parameters").toArray();
        for (const auto &metadata : definition->parameters) {
            const auto stored = std::find_if(storedParameters.cbegin(), storedParameters.cend(),
                [&](const QJsonValue &value) {
                    return value.toObject().value("id").toString() == metadata.id;
                });
            EffectParameterState parameter{metadata.id, metadata.initial, {}};
            if (stored != storedParameters.cend()) {
                const QJsonObject state = stored->toObject();
                parameter.value = state.value("value").toInt(metadata.initial);
                if (parameter.value < metadata.minimum || parameter.value > metadata.maximum
                    || !readKeyframes(state.value("keyframes"),
                                      std::numeric_limits<qint64>::max(), metadata.maximum,
                                      parameter.keyframes, metadata.minimum)) {
                    setStatus(QStringLiteral("Project contains an invalid effect parameter"));
                    return;
                }
            }
            effect.parameters.append(parameter);
        }
        loadedTrackEffects.append(effect);
    }
    if (root.contains("transitions") && !root.value("transitions").isArray()) {
        setStatus(QStringLiteral("Project contains invalid transition data"));
        return;
    }
    for (const auto &entry : root.value("transitions").toArray()) {
        const QJsonObject item = entry.toObject();
        bool validDuration = false;
        ClipTransition transition{item.value("id").toString(),
            item.value("definitionId").toString(),
            item.value("outgoingClipId").toString(),
            item.value("incomingClipId").toString(),
            item.value("durationFrames").toString().toLongLong(&validDuration),
            item.value("balance").toInt(-1)};
        const auto validClip = [&](const QString &id) {
            return id.isEmpty()
                || std::any_of(loadedClips.cbegin(), loadedClips.cend(),
                    [&](const Clip &clip) { return clip.id == id && clip.includeVideo; })
                || std::any_of(loadedTitles.cbegin(), loadedTitles.cend(),
                    [&](const TitleClip &title) { return title.id == id; });
        };
        const auto *definition = m_transitionRegistry.find(transition.definitionId);
        if (transition.id.isEmpty() || transition.definitionId.isEmpty()
            || (transition.outgoingClipId.isEmpty() && transition.incomingClipId.isEmpty())
            || !validClip(transition.outgoingClipId) || !validClip(transition.incomingClipId)
            || !validDuration || transition.durationFrames < 1
            || transition.balance < 20 || transition.balance > 80
            || std::any_of(loadedTransitions.cbegin(), loadedTransitions.cend(),
                [&](const ClipTransition &other) { return other.id == transition.id; })) {
            setStatus(QStringLiteral("Project contains an invalid transition"));
            return;
        }
        const QJsonObject storedParameters = item.value("parameters").toObject();
        for (auto it = storedParameters.begin(); it != storedParameters.end(); ++it) {
            if (!it.value().isDouble()) {
                setStatus(QStringLiteral("Project contains an invalid transition parameter"));
                return;
            }
            const int value = it.value().toInt();
            if (definition) {
                const auto metadata = std::find_if(definition->parameters.cbegin(),
                    definition->parameters.cend(), [&](const TransitionParameterDefinition &parameter) {
                        return parameter.id == it.key();
                    });
                if (metadata != definition->parameters.cend()
                    && (value < metadata->minimum || value > metadata->maximum)) {
                    setStatus(QStringLiteral("Project contains an invalid transition parameter"));
                    return;
                }
            }
            transition.parameters.insert(it.key(), value);
        }
        loadedTransitions.append(transition);
    }
    for (const auto &value : root.value("segments").toArray()) {
        const auto item = value.toObject();
        Segment segment{item.value("id").toString(), item.value("label").toString(),
            MediaRange{item.value("assetId").toString(),
                FrameRange{item.value("firstFrame").toString().toLongLong(),
                           item.value("endFrame").toString().toLongLong()},
                item.value("includeVideo").toBool(), item.value("includeAudio").toBool()},
            item.value("collectionId").toString(loadedCollections.first().id)};
        for (const auto &tag : item.value("tags").toArray())
            if (tag.isString()) segment.tags.append(tag.toString());
        segment.notes = item.value("notes").toString();
        segment.rating = item.value("rating").toInt();
        segment.caption = item.value("caption").toString();
        segment.timelineFrames = item.value("timelineFrames").toString().toLongLong();
        if (segment.timelineFrames > 0) {
            segment.timelineRate = {
                item.value("timelineRateNumerator").toString().toLongLong(),
                item.value("timelineRateDenominator").toString().toLongLong()};
            segment.speedPercent = item.value("speedPercent").toInt(100);
        }
        const auto asset = std::find_if(loadedAssets.cbegin(), loadedAssets.cend(),
            [&](const Asset &candidate) { return candidate.id == segment.source.assetId; });
        if (segment.id.isEmpty() || asset == loadedAssets.cend() ||
            !segment.source.frames.valid() || segment.source.frames.end > asset->media.frameCount
            || segment.rating < 0 || segment.rating > 5
            || segment.timelineFrames < 0
            || (segment.timelineFrames > 0 && (!segment.timelineRate.valid()
                || segment.speedPercent < 50 || segment.speedPercent > 200))
            || std::none_of(loadedCollections.cbegin(), loadedCollections.cend(),
                [&](const Collection &collection) { return collection.id == segment.collectionId; })) {
            setStatus(QStringLiteral("Project contains an invalid saved segment"));
            return;
        }
        loadedSegments.append(segment);
    }
    pauseTimelinePlayback();
    m_timelinePreviewEnabled = false;
    m_assets = std::move(loadedAssets);
    m_clips = std::move(loadedClips);
    m_videoTracks = std::move(loadedVideoTracks);
    m_audioTracks = std::move(loadedAudioTracks);
    m_trackOrder = std::move(loadedTrackOrder);
    m_hiddenVideoTracks = std::move(loadedHiddenVideoTracks);
    m_mutedTracks = std::move(loadedMutedTracks);
    m_trackEffects = std::move(loadedTrackEffects);
    m_transitions = std::move(loadedTransitions);
    m_timelinePreviewEnabled = !m_clips.isEmpty();
    m_segments = std::move(loadedSegments);
    m_collections = std::move(loadedCollections);
    m_titles = std::move(loadedTitles);
    m_captionRows = std::move(loadedCaptionRows);
    m_projectRate = loadedRate;
    m_exportResolution = root.value("exportResolution").toString(QStringLiteral("1080p"));
    if (m_exportResolution != QStringLiteral("720p")) m_exportResolution = QStringLiteral("1080p");
    m_projectAspectRatio = root.value("projectAspectRatio").toString(QStringLiteral("16:9"));
    setExportResolution(m_exportResolution);
    ++m_projectGeneration;
    m_undo.clear();
    m_redo.clear();
    m_clipboard.reset();
    emit clipboardChanged();
    m_projectDirectory = directory;
    if (!recovered) m_projectFilePath = QFileInfo(file.fileName()).absoluteFilePath();
    m_autoNamedProject = false;
    m_demoProject = false;
    m_projectName = root.value("projectName").toString(QFileInfo(m_projectFilePath).completeBaseName());
    m_frameRule = root.value("datasetFrameRule").toString(QStringLiteral("any"));
    m_selectedClipId = m_clips.isEmpty() ? QString{} : m_clips.first().id;
    m_selectedTitleId.clear();
    m_selectedCaptionId.clear();
    m_selectedCaptionRowId.clear();
    m_selectedTransitionId.clear();
    m_selectedAssetId = m_assets.isEmpty() ? QString{} : m_assets.first().id;
    m_currentRange = m_assets.isEmpty() ? FrameRange{} : FrameRange{0, m_assets.first().media.frameCount};
    m_timelineRange = {};
    m_playheadFrame = 0;
    m_dirty = recovered;
    const QFileInfo projectFile(m_projectFilePath);
    const QFileInfo recoveryFile(QDir(directory).filePath(QStringLiteral("recovery/autosave.json")));
    m_recoveryAvailable = !recovered && recoveryFile.exists()
        && (!projectFile.exists() || recoveryFile.lastModified() > projectFile.lastModified());
    if (!recovered && !m_demoProject) recordRecentProject(m_projectFilePath);
    emit projectChanged();
    emit playbackChanged();
    emit recoveryChanged();
    emit selectionChanged();
    emit playheadChanged();
    emit timelineRangeChanged();
    for (const auto &asset : m_assets) {
        if (asset.media.hasVideo && QFileInfo::exists(asset.media.path))
            queueThumbnail(asset.id, asset.media.path);
        if (asset.media.hasVideo && QFileInfo::exists(asset.media.path))
            queueFilmstrip(asset.id, asset.media.path, asset.media.durationUs);
        if (asset.media.hasAudio && QFileInfo::exists(asset.media.path))
            queueWaveform(asset.id, asset.media.path);
    }
    setStatus(recovered ? QStringLiteral("Recovered autosave; save the project to keep it")
                        : QStringLiteral("Project opened"));
}

void EditorController::newProject()
{
    pauseTimelinePlayback();
    m_timelinePreviewEnabled = false;
    ++m_projectGeneration;
    m_assets.clear();
    m_clips.clear();
    m_trackEffects.clear();
    m_transitions.clear();
    m_videoTracks = {0, 3};
    m_audioTracks = {1, 2};
    m_trackOrder.clear();
    m_hiddenVideoTracks.clear();
    m_mutedTracks.clear();
    m_segments.clear();
    m_collections = {{QStringLiteral("selections"), QStringLiteral("Selections")}};
    m_titles.clear();
    m_captionRows.clear();
    m_undo.clear();
    m_redo.clear();
    m_clipboard.reset();
    emit clipboardChanged();
    m_projectDirectory.clear();
    m_projectFilePath.clear();
    m_autoNamedProject = false;
    m_autosaveDebounce.stop();
    m_demoProject = false;
    m_projectName = QStringLiteral("Untitled Project");
    m_frameRule = QStringLiteral("any");
    m_projectRate = {30, 1};
    m_projectAspectRatio = QStringLiteral("16:9");
    m_exportResolution = QStringLiteral("1080p");
    m_exportWidth = 1920;
    m_exportHeight = 1080;
    m_selectedAssetId.clear();
    m_selectedClipId.clear();
    m_selectedTitleId.clear();
    m_selectedTransitionId.clear();
    m_currentRange = {};
    m_timelineRange = {};
    m_playheadFrame = 0;
    m_dirty = false;
    m_recoveryAvailable = false;
    emit projectChanged();
    emit playbackChanged();
    emit recoveryChanged();
    emit selectionChanged();
    emit playheadChanged();
    emit timelineRangeChanged();
    setStatus(QStringLiteral("New project"));
}

void EditorController::openDemoProject()
{
    const QString name = QStringLiteral("Mountain Adventure.datacut");
    QString directory = QDir(QCoreApplication::applicationDirPath()).filePath(
        QStringLiteral("demo/") + name);
    if (!QFileInfo::exists(QDir(directory).filePath(QStringLiteral("project.json"))))
        directory = QDir::current().filePath(QStringLiteral("demo/") + name);
    openProject(QUrl::fromLocalFile(directory));
    if (m_projectDirectory == directory) {
        m_demoProject = true;
        emit projectChanged();
    }
}

void EditorController::readExportProgress()
{
    if (!m_exportProcess) return;
    m_exportReadBuffer += m_exportProcess->readAllStandardOutput();
    while (true) {
        const auto newline = m_exportReadBuffer.indexOf('\n');
        if (newline < 0) break;
        const QByteArray line = m_exportReadBuffer.left(newline).trimmed();
        m_exportReadBuffer.remove(0, newline + 1);
        if (line.startsWith("out_time_us=")) {
            bool valid = false;
            const auto elapsed = line.mid(12).toLongLong(&valid);
            if (valid && m_exportExpectedUs > 0) {
                const qint64 completed = m_collectionExportJobs.isEmpty()
                    ? 0 : m_collectionExportCompletedUs;
                const qint64 total = m_collectionExportJobs.isEmpty()
                    ? m_exportExpectedUs : m_collectionExportTotalUs;
                m_exportProgress = std::clamp(
                    static_cast<double>(completed + std::clamp<qint64>(elapsed, 0, m_exportExpectedUs))
                        / static_cast<double>(std::max<qint64>(1, total)),
                    0.0, 0.99);
                emit exportChanged();
            }
        }
    }
}

int EditorController::exportElapsedSeconds() const
{
    return m_exportClock.isValid() ? int(m_exportClock.elapsed() / 1000) : 0;
}

int EditorController::exportRemainingSeconds() const
{
    if (!exportRunning() || m_exportProgress < 0.05 || m_exportProgress >= 0.99)
        return -1;
    const double elapsed = m_exportClock.elapsed() / 1000.0;
    if (elapsed < 3) return -1;
    return std::max(0, int(std::ceil(elapsed * (1.0 - m_exportProgress)
        / m_exportProgress)));
}

void EditorController::setExportResolution(const QString &resolution)
{
    if (resolution != QStringLiteral("720p") && resolution != QStringLiteral("1080p")) return;
    const bool changed = m_exportResolution != resolution;
    const auto parts = m_projectAspectRatio.split(QLatin1Char(':'));
    bool numeratorOkay = false, denominatorOkay = false;
    const int numerator = parts.size() == 2 ? parts[0].toInt(&numeratorOkay) : 0;
    const int denominator = parts.size() == 2 ? parts[1].toInt(&denominatorOkay) : 0;
    if (!numeratorOkay || !denominatorOkay || numerator < 1 || denominator < 1
        || numerator > 100 || denominator > 100) return;
    const int base = resolution == QStringLiteral("720p") ? 720 : 1080;
    const auto even = [](double value) { return std::max(2, int(std::round(value / 2.0)) * 2); };
    if (numerator >= denominator) {
        m_exportHeight = base;
        m_exportWidth = even(double(base) * numerator / denominator);
    } else {
        m_exportWidth = base;
        m_exportHeight = even(double(base) * denominator / numerator);
    }
    m_exportResolution = resolution;
    if (changed) m_dirty = true;
    emit exportChanged();
    emit projectChanged();
}

void EditorController::setProjectAspectRatio(const QString &ratio)
{
    if (ratio == m_projectAspectRatio) return;
    const auto parts = ratio.split(QLatin1Char(':'));
    bool firstOkay = false, secondOkay = false;
    const int first = parts.size() == 2 ? parts[0].toInt(&firstOkay) : 0;
    const int second = parts.size() == 2 ? parts[1].toInt(&secondOkay) : 0;
    if (!firstOkay || !secondOkay || first < 1 || second < 1
        || first > 100 || second > 100) return;
    m_projectAspectRatio = ratio;
    setExportResolution(m_exportResolution);
    m_dirty = true;
}

void EditorController::setPreviewQuality(const QString &quality)
{
    if (quality != QStringLiteral("Full") && quality != QStringLiteral("Proxy")) return;
    if (m_previewQuality == quality) return;
    m_previewQuality = quality;
    emit proxyChanged();
    emit selectionChanged();
    if (quality == QStringLiteral("Proxy")) startProxyForSelected();
}

void EditorController::startProxyForSelected()
{
    if (m_proxyProcess) return;
    const auto *asset = findAsset(m_selectedAssetId);
    if (!asset || !asset->media.hasVideo || asset->media.isStillImage) return;
    const QString output = proxyCachePath(asset->media.path);
    if (QFileInfo::exists(output)) {
        for (auto &item : m_assets)
            if (item.id == asset->id) item.proxyPath = output;
        m_proxyStage = QStringLiteral("Ready");
        m_proxyProgress = 1.0;
        emit projectChanged();
        emit selectionChanged();
        emit proxyChanged();
        return;
    }
    const QString ffmpegInApp = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("ffmpeg.exe"));
    const QString ffmpeg = QFileInfo::exists(ffmpegInApp)
        ? ffmpegInApp : QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty() || !QDir().mkpath(QFileInfo(output).absolutePath())) {
        setStatus(QStringLiteral("Cannot start proxy generation: FFmpeg or cache path unavailable"));
        return;
    }
    m_proxyAssetId = asset->id;
    m_proxyOutput = output;
    m_proxyTempOutput = output + QStringLiteral(".partial.mp4");
    m_proxyReadBuffer.clear();
    m_proxyProgress = 0.0;
    m_proxyStage = QStringLiteral("Generating");
    const qint64 expectedUs = asset->media.durationUs;
    const auto generation = m_projectGeneration;
    auto *process = new QProcess(this);
    m_proxyProcess = process;
    process->setProgram(ffmpeg);
    process->setArguments({QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"),
        QStringLiteral("-y"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-progress"), QStringLiteral("pipe:1"),
        QStringLiteral("-i"), asset->media.path,
        QStringLiteral("-map"), QStringLiteral("0:v:0"),
        QStringLiteral("-map"), QStringLiteral("0:a:0?"),
        QStringLiteral("-vf"), QStringLiteral("scale=960:-2"),
        QStringLiteral("-c:v"), QStringLiteral("libx264"),
        QStringLiteral("-preset"), QStringLiteral("veryfast"),
        QStringLiteral("-crf"), QStringLiteral("28"),
        QStringLiteral("-c:a"), QStringLiteral("aac"),
        QStringLiteral("-b:a"), QStringLiteral("96k"),
        QStringLiteral("-movflags"), QStringLiteral("+faststart"), m_proxyTempOutput});
    connect(process, &QProcess::readyReadStandardOutput, this,
        [this, process, expectedUs] {
            if (m_proxyProcess != process) return;
            m_proxyReadBuffer += process->readAllStandardOutput();
            while (true) {
                const auto newline = m_proxyReadBuffer.indexOf('\n');
                if (newline < 0) break;
                const QByteArray line = m_proxyReadBuffer.left(newline).trimmed();
                m_proxyReadBuffer.remove(0, newline + 1);
                if (!line.startsWith("out_time_us=") || expectedUs <= 0) continue;
                bool valid = false;
                const qint64 elapsed = line.mid(12).toLongLong(&valid);
                if (valid) {
                    m_proxyProgress = std::clamp(double(elapsed) / double(expectedUs), 0.0, 0.99);
                    emit proxyChanged();
                }
            }
        });
    connect(process, &QProcess::finished, this,
        [this, process, generation](int exitCode, QProcess::ExitStatus exitStatus) {
            if (m_proxyProcess != process) return;
            const bool canceled = m_proxyStage == QStringLiteral("Canceling");
            const bool encoded = exitStatus == QProcess::NormalExit && exitCode == 0
                && QFileInfo::exists(m_proxyTempOutput);
            const bool success = encoded && QFile::rename(m_proxyTempOutput, m_proxyOutput);
            if (!success) QFile::remove(m_proxyTempOutput);
            if (success && generation == m_projectGeneration) {
                for (auto &item : m_assets)
                    if (item.id == m_proxyAssetId) item.proxyPath = m_proxyOutput;
                emit projectChanged();
                emit selectionChanged();
            }
            m_proxyStage = success ? QStringLiteral("Ready")
                : canceled ? QStringLiteral("Canceled") : QStringLiteral("Failed");
            m_proxyProgress = success ? 1.0 : 0.0;
            m_proxyProcess = nullptr;
            process->deleteLater();
            emit proxyChanged();
            if (!success && !canceled) setStatus(QStringLiteral("Proxy generation failed"));
        });
    connect(process, &QProcess::errorOccurred, this,
        [this, process](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart || m_proxyProcess != process) return;
            m_proxyProcess = nullptr;
            m_proxyStage = QStringLiteral("Failed");
            process->deleteLater();
            emit proxyChanged();
            setStatus(QStringLiteral("Could not launch FFmpeg for proxy generation"));
        });
    process->start();
    emit proxyChanged();
    setStatus(QStringLiteral("Proxy generation started in background"));
}

void EditorController::cancelProxy()
{
    if (!m_proxyProcess) return;
    m_proxyStage = QStringLiteral("Canceling");
    m_proxyProcess->terminate();
    QPointer<QProcess> process = m_proxyProcess;
    QTimer::singleShot(2000, this, [process] {
        if (process && process->state() != QProcess::NotRunning) process->kill();
    });
    emit proxyChanged();
}

void EditorController::startExport(const QString &kind, const QUrl &outputUrl)
{
    if (kind == QStringLiteral("timelineClipVideo")) {
        const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
            [&](const Clip &clip) { return clip.id == m_selectedClipId; });
        if (it == m_clips.cend() || !it->includeVideo) {
            setStatus(QStringLiteral("Select a video clip to export"));
            return;
        }
        startProjectExport(outputUrl, false,
            FrameRange{it->startFrame, it->startFrame + it->durationFrames});
        return;
    }
    if (kind == QStringLiteral("imageSequence")) {
        startImageSequenceExport(outputUrl);
        return;
    }
    if (kind == QStringLiteral("projectVideo")) {
        startProjectExport(outputUrl);
        return;
    }
    if (kind == QStringLiteral("timelineRangeVideo")) {
        startProjectExport(outputUrl, true);
        return;
    }
    if (kind == QStringLiteral("projectAudio")) {
        startProjectAudioExport(outputUrl);
        return;
    }
    if (m_exportProcess) {
        setStatus(QStringLiteral("An export is already running"));
        return;
    }
    if (!outputUrl.isLocalFile()) {
        setStatus(QStringLiteral("Choose a local output file"));
        return;
    }
    const QString output = outputUrl.toLocalFile();
    if (QFileInfo::exists(output)) {
        setStatus(QStringLiteral("Output already exists. Choose a new filename"));
        return;
    }
    const bool audioOnly = kind.endsWith(QStringLiteral("Audio"));
    const bool stillFrame = kind == QStringLiteral("stillFrame");
    const bool selectedClip = kind.startsWith(QStringLiteral("clip"));
    if (kind != QStringLiteral("rangeVideo") && kind != QStringLiteral("rangeAudio")
        && kind != QStringLiteral("clipVideo") && kind != QStringLiteral("clipAudio")
        && !stillFrame) {
        setStatus(QStringLiteral("Unsupported export choice"));
        return;
    }
    const QString extension = QFileInfo(output).suffix().toLower();
    if ((stillFrame && extension != QStringLiteral("png"))
        || (!audioOnly && !stillFrame && extension != QStringLiteral("mp4"))
        || (audioOnly && extension != QStringLiteral("m4a")
            && extension != QStringLiteral("wav") && extension != QStringLiteral("mp3")
            && extension != QStringLiteral("flac"))) {
        setStatus(QStringLiteral("Choose PNG for a frame, MP4 for video, or M4A/WAV/MP3/FLAC for audio"));
        return;
    }

    QString assetId = m_selectedAssetId;
    FrameRange range = m_currentRange;
    bool selectedVideoEnabled = true;
    bool selectedAudioEnabled = true;
    int selectedSpeed = 100;
    qint64 selectedDurationFrames = 0;
    QString selectedEffectId;
    int selectedEffectStrength = 100;
    QString selectedAudioEffectId;
    int selectedAudioEffectStrength = 100;
    int selectedGain = 100;
    const Clip *selectedClipData = nullptr;
    qint64 selectedFadeIn = 0;
    qint64 selectedFadeOut = 0;
    if (selectedClip) {
        const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
            [&](const Clip &clip) { return clip.id == m_selectedClipId; });
        if (it == m_clips.cend()) {
            setStatus(QStringLiteral("Select a timeline clip first"));
            return;
        }
        assetId = it->assetId;
        range = {it->sourceFirstFrame, it->sourceFirstFrame + it->sourceFrameCount};
        selectedVideoEnabled = it->includeVideo;
        selectedAudioEnabled = it->includeAudio;
        selectedSpeed = it->speedPercent;
        selectedDurationFrames = it->durationFrames;
        selectedEffectId = it->effectId;
        selectedEffectStrength = it->effectStrength;
        selectedAudioEffectId = it->audioEffectId;
        selectedAudioEffectStrength = it->audioEffectStrength;
        selectedGain = it->gainPercent;
        selectedClipData = &*it;
        selectedFadeIn = it->fadeInFrames;
        selectedFadeOut = it->fadeOutFrames;
    }
    const auto *asset = findAsset(assetId);
    if (!asset || !range.valid() || range.end > asset->media.frameCount
        || (audioOnly && (!asset->media.hasAudio || !selectedAudioEnabled))
        || (!audioOnly && (!asset->media.hasVideo || !selectedVideoEnabled))) {
        setStatus(QStringLiteral("The selected source does not support this export"));
        return;
    }
    if (QFileInfo(output).absoluteFilePath() == QFileInfo(asset->media.path).absoluteFilePath()) {
        setStatus(QStringLiteral("Output must differ from the source file"));
        return;
    }
    try {
        const auto rate = asset->media.frameRate;
        const auto normalUs = rescaleFrames(range.length(), rate, FrameRate{1000000, 1});
        const auto expectedUs = asset->media.isStillImage && !stillFrame
            ? projectFramesToMs(selectedDurationFrames > 0 ? selectedDurationFrames
                                                       : framesForSeconds(5)) * 1000
            : normalUs * 100 / selectedSpeed;
        const auto seconds = [rate](qint64 unit) {
            const long double value = static_cast<long double>(unit)
                * static_cast<long double>(rate.denominator)
                / static_cast<long double>(rate.numerator);
            return QString::number(static_cast<double>(value), 'f', 9);
        };
        const QString first = QString::number(range.first);
        const QString end = QString::number(range.end);
        QString audioProcessing = QStringLiteral("atempo=%1,asetpts=PTS-STARTPTS,%2")
            .arg(QString::number(selectedSpeed / 100.0, 'f', 3),
                 selectedClipData ? gainFilter(*selectedClipData, m_projectRate)
                                  : QStringLiteral("volume=%1").arg(decimal(selectedGain / 100.0)));
        const QString selectedAudioEffect = audioEffectFilter(
            selectedAudioEffectId, selectedAudioEffectStrength);
        if (!selectedAudioEffect.isEmpty())
            audioProcessing += QLatin1Char(',') + selectedAudioEffect;
        const auto projectSeconds = [rate = m_projectRate](qint64 frames) {
            return QString::number(static_cast<double>(frames)
                * static_cast<double>(rate.denominator)
                / static_cast<double>(rate.numerator), 'f', 9);
        };
        if (selectedFadeIn > 0)
            audioProcessing += QStringLiteral(",afade=t=in:st=0:d=%1")
                .arg(projectSeconds(selectedFadeIn));
        if (selectedFadeOut > 0 && selectedDurationFrames >= selectedFadeOut)
            audioProcessing += QStringLiteral(",afade=t=out:st=%1:d=%2")
                .arg(projectSeconds(selectedDurationFrames - selectedFadeOut),
                     projectSeconds(selectedFadeOut));
        QStringList args{QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"),
                         QStringLiteral("-n"), QStringLiteral("-loglevel"),
                         QStringLiteral("error"), QStringLiteral("-progress"),
                         QStringLiteral("pipe:1")};
        if (asset->media.isStillImage && !stillFrame)
            args << QStringLiteral("-loop") << QStringLiteral("1")
                 << QStringLiteral("-framerate") << rateText(m_projectRate);
        args << QStringLiteral("-i") << asset->media.path;
        const bool useChangedVoice = selectedClipData && selectedClipData->voiceChangeEnabled
            && !selectedClipData->voiceChangePath.isEmpty() && !stillFrame;
        if (useChangedVoice) {
            if (!QFileInfo::exists(selectedClipData->voiceChangePath)
                || range.first < selectedClipData->voiceSourceFirstFrame
                || range.end > selectedClipData->voiceSourceFirstFrame
                    + selectedClipData->voiceSourceFrameCount) {
                setStatus(QStringLiteral("Changed voice audio is missing or does not cover this clip"));
                return;
            }
            args << QStringLiteral("-i") << selectedClipData->voiceChangePath;
        }
        const QString voiceTrim = useChangedVoice
            ? QStringLiteral("atrim=start=%1:end=%2")
                .arg(seconds(range.first - selectedClipData->voiceSourceFirstFrame),
                     seconds(range.end - selectedClipData->voiceSourceFirstFrame)) : QString{};
        if (stillFrame) {
            args << QStringLiteral("-filter_complex")
                 << QStringLiteral("[0:v:0]trim=start_frame=%1:end_frame=%2,"
                                   "setpts=PTS-STARTPTS[v]")
                       .arg(range.first).arg(range.first + 1)
                 << QStringLiteral("-map") << QStringLiteral("[v]")
                 << QStringLiteral("-frames:v") << QStringLiteral("1")
                 << QStringLiteral("-c:v") << QStringLiteral("png");
        } else if (audioOnly) {
            const QString trim = useChangedVoice ? voiceTrim : asset->media.hasVideo
                ? QStringLiteral("atrim=start=%1:end=%2")
                    .arg(seconds(range.first), seconds(range.end))
                : QStringLiteral("atrim=start_sample=%1:end_sample=%2")
                    .arg(first, end);
            args << QStringLiteral("-filter_complex")
                 << QStringLiteral("[%1:a:0]%2,asetpts=PTS-STARTPTS,%3[a]")
                       .arg(useChangedVoice ? 1 : 0).arg(trim, audioProcessing)
                 << QStringLiteral("-map") << QStringLiteral("[a]")
                 << QStringLiteral("-vn") << QStringLiteral("-c:a");
            if (extension == QStringLiteral("wav")) args << QStringLiteral("pcm_s16le");
            else if (extension == QStringLiteral("flac")) args << QStringLiteral("flac");
            else if (extension == QStringLiteral("mp3")) args << QStringLiteral("libmp3lame");
            else args << QStringLiteral("aac");
        } else {
            const QString timing = asset->media.isStillImage
                ? QStringLiteral("trim=duration=%1,setpts=PTS-STARTPTS")
                      .arg(QString::number(expectedUs / 1000000.0, 'f', 9))
                : QStringLiteral("trim=start_frame=%1:end_frame=%2,"
                                 "setpts=(PTS-STARTPTS)*100/%3")
                      .arg(first, end, QString::number(selectedSpeed));
            QString filter = QStringLiteral("[0:v:0]%1,"
                                            "scale=%2:%3:force_original_aspect_ratio=decrease,"
                                            "pad=%2:%3:(ow-iw)/2:(oh-ih)/2")
                                 .arg(timing, QString::number(m_exportWidth),
                                      QString::number(m_exportHeight));
            const QString selectedEffect = effectFilter(selectedEffectId, selectedEffectStrength);
            if (!selectedEffect.isEmpty()) filter += QLatin1Char(',') + selectedEffect;
            const TrackEffect *animatedBlur = nullptr;
            if (selectedClipData)
                for (const auto &trackEffect : m_trackEffects)
                    if (trackEffect.track == selectedClipData->track) {
                        if (trackEffect.id == QStringLiteral("datacut.video.blur")
                            && !trackEffect.keyframes.isEmpty()) {
                            animatedBlur = &trackEffect;
                            continue;
                        }
                        const QString effect = trackEffectFilter(trackEffect, *selectedClipData,
                                                                 m_projectRate);
                        if (!effect.isEmpty()) filter += QLatin1Char(',') + effect;
                    }
            if (animatedBlur) {
                filter += QStringLiteral("[vraw];[vraw]split=2[vorig][vblurin];"
                                         "[vblurin]gblur=sigma=10[vblur];"
                                         "[vorig][vblur]blend=all_expr='A*(1-(%1))+B*(%1)'[v]")
                    .arg(blurMixExpression(*animatedBlur, *selectedClipData,
                                           m_projectRate, false));
            } else filter += QStringLiteral("[v]");
            if (asset->media.hasAudio && selectedAudioEnabled)
                filter += useChangedVoice
                    ? QStringLiteral(";[1:a:0]%1,asetpts=PTS-STARTPTS,%2[a]")
                        .arg(voiceTrim, audioProcessing)
                    : QStringLiteral(";[0:a:0]atrim=start=%1:end=%2,"
                                     "asetpts=PTS-STARTPTS,%3[a]")
                        .arg(seconds(range.first), seconds(range.end), audioProcessing);
            args << QStringLiteral("-filter_complex") << filter
                 << QStringLiteral("-map") << QStringLiteral("[v]");
            if (asset->media.hasAudio && selectedAudioEnabled)
                args << QStringLiteral("-map") << QStringLiteral("[a]")
                     << QStringLiteral("-c:a") << QStringLiteral("aac");
            else args << QStringLiteral("-an");
            args << QStringLiteral("-c:v") << QStringLiteral("libx264")
                 << QStringLiteral("-preset") << QStringLiteral("medium")
                 << QStringLiteral("-crf") << QStringLiteral("18")
                 << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
                 << QStringLiteral("-movflags") << QStringLiteral("+faststart");
        }
        launchExport(args, output, stillFrame ? 33333 : expectedUs);
    } catch (const std::exception &error) {
        setStatus(QStringLiteral("Export failed: %1").arg(QString::fromUtf8(error.what())));
    }
}

void EditorController::startFrameExport(qint64 projectFrame, const QUrl &outputUrl)
{
    startProjectExport(outputUrl, false, FrameRange{projectFrame, projectFrame + 1}, true);
}

void EditorController::startImageSequenceExport(const QUrl &destinationFolder)
{
    if (m_exportProcess) {
        setStatus(QStringLiteral("An export is already running"));
        return;
    }
    if (!destinationFolder.isLocalFile()) {
        setStatus(QStringLiteral("Choose a local destination folder"));
        return;
    }
    const QString parent = destinationFolder.toLocalFile();
    if (!QFileInfo(parent).isDir()) {
        setStatus(QStringLiteral("The destination folder does not exist"));
        return;
    }
    const auto *asset = findAsset(m_selectedAssetId);
    if (!asset || !asset->media.hasVideo || !canSaveRange()
        || !QFileInfo::exists(asset->media.path)) {
        setStatus(QStringLiteral("Select a valid video source range first"));
        return;
    }
    const QString name = QStringLiteral("DataCut-frames-%1-%2")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")),
             freshId().left(8));
    const QString output = QDir(parent).filePath(name);
    const auto rate = asset->media.frameRate;
    QJsonObject manifest{
        {"schemaVersion", 1}, {"type", QStringLiteral("imageSequence")},
        {"sourceName", asset->media.name}, {"sourcePath", asset->media.path},
        {"sourceFrameRate", QJsonObject{{"numerator", rate.numerator},
                                         {"denominator", rate.denominator}}},
        {"firstFrame", QString::number(m_currentRange.first)},
        {"endFrameExclusive", QString::number(m_currentRange.end)},
        {"frameCount", QString::number(m_currentRange.length())},
        {"frameRule", m_frameRule}, {"projectName", m_projectName},
        {"filePattern", QStringLiteral("frame_%06d.png")}
    };
    m_exportManifest = QJsonDocument(manifest).toJson();
    QStringList args{QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"),
        QStringLiteral("-n"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-progress"), QStringLiteral("pipe:1"),
        QStringLiteral("-i"), asset->media.path,
        QStringLiteral("-filter_complex"),
        QStringLiteral("[0:v:0]trim=start_frame=%1:end_frame=%2,"
                       "setpts=PTS-STARTPTS[v]")
            .arg(m_currentRange.first).arg(m_currentRange.end),
        QStringLiteral("-map"), QStringLiteral("[v]"),
        QStringLiteral("-frames:v"), QString::number(m_currentRange.length()),
        QStringLiteral("-fps_mode"), QStringLiteral("passthrough"),
        QStringLiteral("-c:v"), QStringLiteral("png"),
        QStringLiteral("-f"), QStringLiteral("image2")};
    try {
        launchExport(args, output,
            rescaleFrames(m_currentRange.length(), rate, FrameRate{1000000, 1}), true);
    } catch (const std::exception &error) {
        setStatus(QStringLiteral("Frame export failed: %1").arg(QString::fromUtf8(error.what())));
    }
}

void EditorController::exportCollection(const QString &collectionId,
                                         const QUrl &destinationFolder)
{
    if (m_exportProcess || !m_collectionExportJobs.isEmpty()) {
        setStatus(QStringLiteral("An export is already running"));
        return;
    }
    if (!destinationFolder.isLocalFile()
        || !QFileInfo(destinationFolder.toLocalFile()).isDir()) {
        setStatus(QStringLiteral("Choose an existing local destination folder"));
        return;
    }
    const auto collection = std::find_if(m_collections.cbegin(), m_collections.cend(),
        [&](const Collection &entry) { return entry.id == collectionId; });
    if (collection == m_collections.cend()) {
        setStatus(QStringLiteral("Select a collection first"));
        return;
    }

    QVector<CollectionExportJob> jobs;
    QJsonArray manifestItems;
    int number = 0;
    for (const auto &segment : m_segments) {
        if (segment.collectionId != collectionId) continue;
        const auto *asset = findAsset(segment.source.assetId);
        if (!asset || !QFileInfo::exists(asset->media.path)
            || !segment.source.frames.valid()
            || segment.source.frames.end > asset->media.frameCount) {
            setStatus(QStringLiteral("Collection contains missing or invalid source media"));
            return;
        }
        const bool video = segment.source.includeVideo && asset->media.hasVideo;
        const bool audio = segment.source.includeAudio && asset->media.hasAudio;
        if (!video && !audio) {
            setStatus(QStringLiteral("Collection contains a segment with no available streams"));
            return;
        }
        ++number;
        QString safeLabel = segment.label.trimmed().left(64);
        safeLabel.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1F]")),
                          QStringLiteral("_"));
        safeLabel.replace(QRegularExpression(QStringLiteral("[. ]+$")), QString{});
        if (safeLabel.isEmpty()) safeLabel = QStringLiteral("segment");
        const QString fileName = QStringLiteral("%1-%2.%3")
            .arg(number, 3, 10, QLatin1Char('0')).arg(safeLabel,
                                                   video ? QStringLiteral("mp4")
                                                         : QStringLiteral("m4a"));
        const auto rate = asset->media.frameRate;
        const auto seconds = [rate](qint64 frame) {
            return QString::number(static_cast<double>(frame)
                * static_cast<double>(rate.denominator)
                / static_cast<double>(rate.numerator), 'f', 9);
        };
        QStringList args{QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"),
                         QStringLiteral("-n"), QStringLiteral("-loglevel"),
                         QStringLiteral("error"), QStringLiteral("-progress"),
                         QStringLiteral("pipe:1")};
        const bool exactTimelineLength = segment.timelineFrames > 0
            && segment.timelineRate.valid();
        const QString targetSeconds = exactTimelineLength
            ? QString::number(static_cast<double>(segment.timelineFrames)
                * segment.timelineRate.denominator / segment.timelineRate.numerator, 'f', 9)
            : QString{};
        if (asset->media.isStillImage && video)
            args << QStringLiteral("-loop") << QStringLiteral("1")
                 << QStringLiteral("-framerate")
                 << rateText(exactTimelineLength ? segment.timelineRate : m_projectRate);
        args << QStringLiteral("-i") << asset->media.path;
        const auto first = segment.source.frames.first;
        const auto end = segment.source.frames.end;
        const qint64 expectedUs = exactTimelineLength
            ? rescaleFrames(segment.timelineFrames, segment.timelineRate,
                            FrameRate{1000000, 1})
            : asset->media.isStillImage ? 5000000
                : rescaleFrames(segment.source.frames.length(), rate,
                                FrameRate{1000000, 1});
        if (video) {
            QString filter = asset->media.isStillImage
                ? QStringLiteral("[0:v:0]trim=duration=%1,setpts=PTS-STARTPTS")
                    .arg(exactTimelineLength ? targetSeconds : QStringLiteral("5"))
                : QStringLiteral("[0:v:0]trim=start_frame=%1:end_frame=%2,"
                                 "setpts=PTS-STARTPTS").arg(first).arg(end);
            if (exactTimelineLength) {
                if (!asset->media.isStillImage)
                    filter += QStringLiteral(",setpts=PTS*%1")
                        .arg(decimal(100.0 / segment.speedPercent));
                filter += QStringLiteral(",fps=%1,tpad=stop_mode=clone:stop_duration=%2,"
                                         "trim=end_frame=%3,setpts=PTS-STARTPTS")
                    .arg(rateText(segment.timelineRate), targetSeconds)
                    .arg(segment.timelineFrames);
            }
            filter += QStringLiteral(",scale=%1:%2:force_original_aspect_ratio=decrease,"
                                     "pad=%1:%2:(ow-iw)/2:(oh-ih)/2[v]")
                          .arg(m_exportWidth).arg(m_exportHeight);
            if (audio) {
                filter += QStringLiteral(";[0:a:0]atrim=start=%1:end=%2,"
                                         "asetpts=PTS-STARTPTS")
                              .arg(seconds(first), seconds(end));
                if (exactTimelineLength)
                    filter += QStringLiteral(",atempo=%1,apad,atrim=duration=%2,"
                                             "asetpts=PTS-STARTPTS")
                        .arg(decimal(segment.speedPercent / 100.0), targetSeconds);
                filter += QStringLiteral("[a]");
            }
            args << QStringLiteral("-filter_complex") << filter
                 << QStringLiteral("-map") << QStringLiteral("[v]");
            if (audio)
                args << QStringLiteral("-map") << QStringLiteral("[a]")
                     << QStringLiteral("-c:a") << QStringLiteral("aac");
            else args << QStringLiteral("-an");
            args << QStringLiteral("-c:v") << QStringLiteral("libx264")
                 << QStringLiteral("-preset") << QStringLiteral("medium")
                 << QStringLiteral("-crf") << QStringLiteral("18")
                 << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
                 << QStringLiteral("-movflags") << QStringLiteral("+faststart");
            if (exactTimelineLength)
                args << QStringLiteral("-frames:v") << QString::number(segment.timelineFrames);
        } else {
            const QString trim = asset->media.hasVideo
                ? QStringLiteral("atrim=start=%1:end=%2")
                    .arg(seconds(first), seconds(end))
                : QStringLiteral("atrim=start_sample=%1:end_sample=%2")
                    .arg(first).arg(end);
            QString filter = QStringLiteral("[0:a:0]%1,asetpts=PTS-STARTPTS").arg(trim);
            if (exactTimelineLength)
                filter += QStringLiteral(",atempo=%1,apad,atrim=duration=%2,"
                                         "asetpts=PTS-STARTPTS")
                    .arg(decimal(segment.speedPercent / 100.0), targetSeconds);
            args << QStringLiteral("-filter_complex")
                 << filter + QStringLiteral("[a]")
                 << QStringLiteral("-map") << QStringLiteral("[a]")
                 << QStringLiteral("-vn") << QStringLiteral("-c:a")
                 << QStringLiteral("aac");
        }
        jobs.append({args, fileName, expectedUs});
        manifestItems.append(QJsonObject{
            {"file", fileName}, {"label", segment.label},
            {"captionFile", segment.caption.isEmpty() ? QString{}
                : QFileInfo(fileName).completeBaseName() + QStringLiteral(".txt")},
            {"sourcePath", asset->media.path},
            {"firstFrame", QString::number(first)},
            {"endFrameExclusive", QString::number(end)},
            {"frameCount", QString::number(end - first)},
            {"timelineFrames", QString::number(segment.timelineFrames)},
            {"timelineRate", exactTimelineLength
                ? rateText(segment.timelineRate) : QString{}},
            {"speedPercent", segment.speedPercent},
            {"sourceRate", rateText(rate)},
            {"includeVideo", video}, {"includeAudio", audio},
            {"tags", QJsonArray::fromStringList(segment.tags)},
            {"notes", segment.notes}, {"rating", segment.rating},
            {"caption", segment.caption}
        });
    }
    if (jobs.isEmpty()) {
        setStatus(QStringLiteral("The selected collection is empty"));
        return;
    }
    QString safeName = collection->name.left(64);
    safeName.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1F]")),
                     QStringLiteral("_"));
    const QString folder = QDir(destinationFolder.toLocalFile()).filePath(
        safeName + QStringLiteral("-DataCut-")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))
        + QLatin1Char('-') + freshId().left(8));
    if (!QDir().mkpath(folder)) {
        setStatus(QStringLiteral("Could not create collection export folder"));
        return;
    }
    m_collectionExportFolder = folder;
    m_collectionExportJobs = std::move(jobs);
    m_collectionExportIndex = 0;
    m_collectionExportCompletedUs = 0;
    m_collectionExportTotalUs = 0;
    for (const auto &job : std::as_const(m_collectionExportJobs))
        m_collectionExportTotalUs += std::max<qint64>(1, job.expectedUs);
    m_collectionExportManifest = QJsonDocument(QJsonObject{
        {"schemaVersion", 1}, {"type", QStringLiteral("collectionExport")},
        {"collection", collection->name}, {"project", m_projectName},
        {"segments", manifestItems}
    }).toJson();
    launchNextCollectionExport();
}

void EditorController::launchNextCollectionExport()
{
    if (m_collectionExportIndex >= m_collectionExportJobs.size()) {
        bool captionsSaved = true;
        const auto manifestObject = QJsonDocument::fromJson(m_collectionExportManifest).object();
        for (const auto &value : manifestObject.value(QStringLiteral("segments")).toArray()) {
            const auto item = value.toObject();
            const QString name = item.value(QStringLiteral("captionFile")).toString();
            if (name.isEmpty()) continue;
            const QByteArray contents = item.value(QStringLiteral("caption")).toString().toUtf8();
            QSaveFile sidecar(QDir(m_collectionExportFolder).filePath(name));
            if (!sidecar.open(QIODevice::WriteOnly)
                || sidecar.write(contents) != contents.size() || !sidecar.commit())
                captionsSaved = false;
        }
        QSaveFile manifest(QDir(m_collectionExportFolder).filePath(QStringLiteral("manifest.json")));
        const bool saved = manifest.open(QIODevice::WriteOnly)
            && manifest.write(m_collectionExportManifest) == m_collectionExportManifest.size()
            && manifest.commit() && captionsSaved;
        m_exportOutput = m_collectionExportFolder;
        m_exportStage = saved ? QStringLiteral("Completed") : QStringLiteral("Failed");
        m_exportProgress = saved ? 1.0 : 0.0;
        m_exportTick.stop();
        m_collectionExportTotalUs = 0;
        m_collectionExportCompletedUs = 0;
        m_collectionExportJobs.clear();
        m_collectionExportManifest.clear();
        emit exportChanged();
        setStatus(saved ? QStringLiteral("Collection exported: %1").arg(m_collectionExportFolder)
                        : QStringLiteral("Media exported, but the collection manifest could not be saved"));
        return;
    }
    const auto &job = m_collectionExportJobs[m_collectionExportIndex];
    launchExport(job.args, QDir(m_collectionExportFolder).filePath(job.output),
                 job.expectedUs);
    if (!m_exportProcess) {
        m_collectionExportJobs.clear();
        m_collectionExportManifest.clear();
        return;
    }
    m_exportStage = QStringLiteral("Encoding %1 of %2")
        .arg(m_collectionExportIndex + 1).arg(m_collectionExportJobs.size());
    emit exportChanged();
}

void EditorController::launchExport(QStringList args, const QString &output,
                                    qint64 expectedUs, bool directoryOutput)
{
    const QString ffmpegInApp = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("ffmpeg.exe"));
    const QString ffmpeg = QFileInfo::exists(ffmpegInApp)
        ? ffmpegInApp : QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        QFile::remove(m_exportGraphPath);
        m_exportGraphPath.clear();
        for (const auto &path : std::as_const(m_exportAuxiliaryPaths)) QFile::remove(path);
        m_exportAuxiliaryPaths.clear();
        setStatus(QStringLiteral("FFmpeg encoder is missing from this installation"));
        return;
    }
    const QFileInfo outputInfo(output);
    const QString tempName = (directoryOutput ? outputInfo.fileName()
        : outputInfo.completeBaseName())
        + QStringLiteral(".datacut-partial-") + freshId()
        + (directoryOutput ? QString{} : QLatin1Char('.') + outputInfo.suffix().toLower());
    m_exportTempOutput = QDir(outputInfo.absolutePath()).filePath(tempName);
    if (directoryOutput) {
        if (!QDir().mkpath(m_exportTempOutput)) {
            QFile::remove(m_exportGraphPath);
            m_exportGraphPath.clear();
            setStatus(QStringLiteral("Could not create temporary frame folder"));
            return;
        }
        QSaveFile manifest(QDir(m_exportTempOutput).filePath(QStringLiteral("manifest.json")));
        if (!manifest.open(QIODevice::WriteOnly)
            || manifest.write(m_exportManifest) != m_exportManifest.size()
            || !manifest.commit()) {
            QFile::remove(m_exportGraphPath);
            m_exportGraphPath.clear();
            setStatus(QStringLiteral("Could not write frame manifest"));
            return;
        }
        args << QDir(m_exportTempOutput).filePath(QStringLiteral("frame_%06d.png"));
    } else {
        args << m_exportTempOutput;
        m_exportManifest.clear();
    }

    m_exportExpectedUs = expectedUs;
    m_exportDirectory = directoryOutput;
    m_exportOutput = output;
    if (m_collectionExportJobs.isEmpty() || m_collectionExportIndex == 0)
        m_exportClock.start();
    m_exportProgress = m_collectionExportJobs.isEmpty() ? 0.0
        : double(m_collectionExportCompletedUs)
            / double(std::max<qint64>(1, m_collectionExportTotalUs));
    m_exportStage = QStringLiteral("Encoding");
    m_exportError.clear();
    m_exportReadBuffer.clear();
    auto *process = new QProcess(this);
    m_exportProcess = process;
    process->setProgram(ffmpeg);
    process->setArguments(args);
    connect(process, &QProcess::readyReadStandardOutput, this,
            &EditorController::readExportProgress);
    connect(process, &QProcess::readyReadStandardError, this, [this, process] {
        m_exportError += QString::fromUtf8(process->readAllStandardError());
        if (m_exportError.size() > 12000) m_exportError = m_exportError.right(12000);
    });
    connect(process, &QProcess::finished, this,
            [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
        if (m_exportProcess != process) return;
        QFile::remove(m_exportGraphPath);
        m_exportGraphPath.clear();
        for (const auto &path : std::as_const(m_exportAuxiliaryPaths)) QFile::remove(path);
        m_exportAuxiliaryPaths.clear();
        readExportProgress();
        m_exportError += QString::fromUtf8(process->readAllStandardError());
        const bool canceled = m_exportStage == QStringLiteral("Canceling");
        const bool encoded = exitStatus == QProcess::NormalExit && exitCode == 0
            && (m_exportDirectory
                ? !QDir(m_exportTempOutput).entryList({QStringLiteral("frame_*.png")},
                                                   QDir::Files).isEmpty()
                : QFileInfo::exists(m_exportTempOutput));
        const bool success = encoded && !QFileInfo::exists(m_exportOutput)
            && (m_exportDirectory
                ? QDir().rename(m_exportTempOutput, m_exportOutput)
                : QFile::rename(m_exportTempOutput, m_exportOutput));
        if (!success && !m_exportDirectory) QFile::remove(m_exportTempOutput);
        if (encoded && !success)
            m_exportError += QStringLiteral(" Could not safely place the completed file at the chosen path.");
        if (success && !m_collectionExportJobs.isEmpty()) {
            m_collectionExportCompletedUs += std::max<qint64>(1, m_exportExpectedUs);
            m_exportProgress = double(m_collectionExportCompletedUs)
                / double(std::max<qint64>(1, m_collectionExportTotalUs));
        } else {
            m_exportProgress = success ? 1.0 : 0.0;
        }
        m_exportStage = success ? QStringLiteral("Completed")
            : canceled ? QStringLiteral("Canceled") : QStringLiteral("Failed");
        if (!success && !canceled) {
            if (m_exportError.trimmed().isEmpty())
                m_exportError = QStringLiteral("FFmpeg exited with code %1 (%2).")
                    .arg(exitCode).arg(exitStatus == QProcess::NormalExit
                        ? QStringLiteral("normal exit") : QStringLiteral("process crash"));
            qWarning().noquote() << "export_failed" << "exit_code=" << exitCode
                                 << "output=" << m_exportOutput
                                 << "details=" << m_exportError.trimmed();
        }
        m_exportProcess = nullptr;
        m_exportDirectory = false;
        process->deleteLater();
        emit exportChanged();
        if (!m_collectionExportJobs.isEmpty()) {
            if (success) {
                ++m_collectionExportIndex;
                launchNextCollectionExport();
            } else {
                m_exportTick.stop();
                m_collectionExportJobs.clear();
                m_collectionExportManifest.clear();
                setStatus(canceled ? QStringLiteral("Collection export canceled; completed files remain in %1")
                                       .arg(m_collectionExportFolder)
                                   : QStringLiteral("Collection export failed; completed files remain in %1: %2")
                                       .arg(m_collectionExportFolder, m_exportError.trimmed()));
            }
            return;
        }
        m_exportTick.stop();
        setStatus(success ? QStringLiteral("Export completed: %1").arg(m_exportOutput)
            : canceled ? QStringLiteral("Export canceled")
            : QStringLiteral("Export failed: %1").arg(m_exportError.trimmed()));
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_exportProcess != process) return;
        QFile::remove(m_exportGraphPath);
        m_exportGraphPath.clear();
        for (const auto &path : std::as_const(m_exportAuxiliaryPaths)) QFile::remove(path);
        m_exportAuxiliaryPaths.clear();
        m_exportStage = QStringLiteral("Failed");
        m_exportTick.stop();
        m_exportProcess = nullptr;
        m_collectionExportJobs.clear();
        m_collectionExportManifest.clear();
        process->deleteLater();
        emit exportChanged();
        setStatus(QStringLiteral("FFmpeg encoder could not start: %1").arg(process->errorString()));
    });
    process->start();
    m_exportTick.start();
    emit exportChanged();
    setStatus(QStringLiteral("Export started"));
}

void EditorController::startProjectExport(const QUrl &outputUrl, bool selectedRange,
                                          std::optional<FrameRange> explicitRange,
                                          bool stillFrame)
{
    if (m_exportProcess) {
        setStatus(QStringLiteral("An export is already running"));
        return;
    }
    const QString requiredExtension = stillFrame ? QStringLiteral("png") : QStringLiteral("mp4");
    if (!outputUrl.isLocalFile() || QFileInfo(outputUrl.toLocalFile()).suffix().toLower()
        != requiredExtension) {
        setStatus(stillFrame ? QStringLiteral("Choose a PNG output file")
                             : QStringLiteral("Choose an MP4 output file"));
        return;
    }
    const QString output = outputUrl.toLocalFile();
    if (QFileInfo::exists(output)) {
        setStatus(QStringLiteral("Output already exists. Choose a new filename"));
        return;
    }
    for (const auto &path : std::as_const(m_exportAuxiliaryPaths)) QFile::remove(path);
    m_exportAuxiliaryPaths.clear();
    const bool hasVisibleCaptions = std::any_of(m_captionRows.cbegin(), m_captionRows.cend(),
        [](const CaptionRow &row) { return row.visible && !row.cues.isEmpty(); });
    if (m_clips.isEmpty() && m_titles.isEmpty() && !hasVisibleCaptions) {
        setStatus(QStringLiteral("The timeline is empty"));
        return;
    }
    for (const auto &transition : m_transitions) {
        const auto *definition = m_transitionRegistry.find(transition.definitionId);
        if (!definition || (definition->renderer != QStringLiteral("cross_dissolve")
            && definition->renderer != QStringLiteral("dip_black")
            && spatialMaskKind(definition->renderer) == 0)) {
            setStatus(QStringLiteral("Cannot export: transition renderer for %1 is unavailable")
                .arg(transition.definitionId));
            return;
        }
    }

    qint64 totalFrames = 0;
    QStringList args{QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"),
                     QStringLiteral("-n"), QStringLiteral("-loglevel"),
                     QStringLiteral("error"), QStringLiteral("-progress"),
                     QStringLiteral("pipe:1"), QStringLiteral("-filter_complex_threads"),
                     QStringLiteral("2")};
    for (const auto &clip : m_clips) {
        const auto *asset = findAsset(clip.assetId);
        if (!asset || !QFileInfo::exists(asset->media.path) || clip.durationFrames <= 0) {
            setStatus(QStringLiteral("A timeline clip has missing media or invalid duration"));
            return;
        }
        if (QFileInfo(output).absoluteFilePath() == QFileInfo(asset->media.path).absoluteFilePath()) {
            setStatus(QStringLiteral("Output must differ from every source file"));
            return;
        }
        totalFrames = std::max(totalFrames, clip.startFrame + clip.durationFrames);
        if (asset->media.isStillImage)
            args << QStringLiteral("-loop") << QStringLiteral("1")
                 << QStringLiteral("-framerate") << rateText(m_projectRate);
        args << QStringLiteral("-i") << asset->media.path;
    }
    QMap<QString, int> voiceInputs;
    for (const auto &clip : m_clips) {
        if (!clip.includeAudio || !clip.voiceChangeEnabled || clip.voiceChangePath.isEmpty()) continue;
        if (!QFileInfo::exists(clip.voiceChangePath)) {
            setStatus(QStringLiteral("Changed voice audio is missing for %1").arg(clip.voiceName));
            return;
        }
        if (clip.sourceFirstFrame < clip.voiceSourceFirstFrame
            || clip.sourceFirstFrame + clip.sourceFrameCount
                > clip.voiceSourceFirstFrame + clip.voiceSourceFrameCount) {
            setStatus(QStringLiteral("Changed voice does not cover the extended clip. Regenerate it."));
            return;
        }
        voiceInputs.insert(clip.id, m_clips.size() + voiceInputs.size());
        args << QStringLiteral("-i") << clip.voiceChangePath;
    }
    // Voice replacements are real FFmpeg inputs. Every subsequent visual input
    // must start after them, or transitions and text refer to audio streams.
    const int visualInputBase = int(m_clips.size() + voiceInputs.size());
    struct CrossRender {
        const ClipTransition *transition = nullptr;
        const Clip *outgoing = nullptr;
        const Clip *incoming = nullptr;
        const TitleClip *outgoingTitle = nullptr;
        const TitleClip *incomingTitle = nullptr;
        qint64 leftFrames = 0;
        qint64 rightFrames = 0;
        double blend = 0.5;
        int outgoingInput = -1;
        int incomingInput = -1;
    };
    QVector<CrossRender> crossRenders;
    QHash<QString, VisualEdge> visualById;
    visualById.reserve(m_clips.size() + m_titles.size());
    for (const auto &visual : visualEdges(m_clips, m_titles))
        visualById.insert(visual.id, visual);
    const auto isPairedTransition = [&](const ClipTransition &transition) {
        const auto outgoing = visualById.constFind(transition.outgoingClipId);
        const auto incoming = visualById.constFind(transition.incomingClipId);
        return outgoing != visualById.cend() && incoming != visualById.cend()
            && outgoing->track == incoming->track
            && outgoing->start + outgoing->duration == incoming->start;
    };
    const auto singleEdgeMask = [&](const ClipTransition &transition,
                                    qint64 objectFrames, bool startEdge) {
        const auto *definition = m_transitionRegistry.find(transition.definitionId);
        const int kind = definition ? spatialMaskKind(definition->renderer) : 0;
        if (!kind || isPairedTransition(transition)) return QString{};
        const qint64 frames = std::min(objectFrames, transition.durationFrames);
        if (frames <= 0) return QString{};
        const QString local = startEdge ? QStringLiteral("N")
            : QStringLiteral("(N-%1)").arg(objectFrames - frames);
        const QString progress = spatialProgress(
            frames <= 1 ? QStringLiteral("1")
                : QStringLiteral("(%1/%2)").arg(local).arg(frames - 1),
            transition.parameters.value(QStringLiteral("speed"), 100),
            transition.parameters.value(QStringLiteral("softness"), 3));
        const QString condition = startEdge
            ? QStringLiteral("lt(N,%1)").arg(frames)
            : QStringLiteral("gte(N,%1)").arg(objectFrames - frames);
        return spatialMaskFilter(kind,
            transition.parameters.value(QStringLiteral("softness"), 3),
            transition.parameters.value(QStringLiteral("complexity"), 6),
            progress, startEdge,
            transition.parameters.value(QStringLiteral("edge_enabled"), 0) != 0,
            transition.parameters.value(QStringLiteral("edge_color"), 0xFFFFFF), condition);
    };
    const auto frozenEdgeMask = [&](const CrossRender &cross, bool incoming) {
        const auto *definition = m_transitionRegistry.find(cross.transition->definitionId);
        const int kind = definition ? spatialMaskKind(definition->renderer) : 0;
        if (!kind) return QString{};
        const qint64 total = cross.leftFrames + cross.rightFrames;
        const QString linear = total <= 1 ? QStringLiteral("1") : incoming
            ? QStringLiteral("(N/%1)").arg(total - 1)
            : QStringLiteral("((%1+N)/%2)").arg(cross.leftFrames).arg(total - 1);
        const QString progress = spatialProgress(linear,
            cross.transition->parameters.value(QStringLiteral("speed"), 100),
            cross.transition->parameters.value(QStringLiteral("softness"), 3));
        return spatialMaskFilter(kind,
            cross.transition->parameters.value(QStringLiteral("softness"), 3),
            cross.transition->parameters.value(QStringLiteral("complexity"), 6),
            progress, incoming,
            cross.transition->parameters.value(QStringLiteral("edge_enabled"), 0) != 0,
            cross.transition->parameters.value(QStringLiteral("edge_color"), 0xFFFFFF));
    };
    QHash<QString, QVector<const ClipTransition *>> transitionsByVisual;
    for (const auto &transition : m_transitions) {
        if (!transition.outgoingClipId.isEmpty())
            transitionsByVisual[transition.outgoingClipId].append(&transition);
        if (!transition.incomingClipId.isEmpty())
            transitionsByVisual[transition.incomingClipId].append(&transition);
    }
    const auto clipById = [&](const QString &id) -> const Clip * {
        const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(),
            [&](const Clip &clip) { return clip.id == id; });
        return it == m_clips.cend() ? nullptr : &*it;
    };
    const auto titleById = [&](const QString &id) -> const TitleClip * {
        const auto it = std::find_if(m_titles.cbegin(), m_titles.cend(),
            [&](const TitleClip &title) { return title.id == id; });
        return it == m_titles.cend() ? nullptr : &*it;
    };
    for (const auto &transition : m_transitions) {
        const Clip *outgoing = clipById(transition.outgoingClipId);
        const Clip *incoming = clipById(transition.incomingClipId);
        const TitleClip *outgoingTitle = titleById(transition.outgoingClipId);
        const TitleClip *incomingTitle = titleById(transition.incomingClipId);
        const auto *definition = m_transitionRegistry.find(transition.definitionId);
        const int outgoingTrack = outgoing ? outgoing->track
            : outgoingTitle ? outgoingTitle->track : -1;
        const int incomingTrack = incoming ? incoming->track
            : incomingTitle ? incomingTitle->track : -1;
        const qint64 outgoingEnd = outgoing ? outgoing->startFrame + outgoing->durationFrames
            : outgoingTitle ? outgoingTitle->startFrame + outgoingTitle->durationFrames : -1;
        const qint64 incomingStart = incoming ? incoming->startFrame
            : incomingTitle ? incomingTitle->startFrame : -2;
        if ((!outgoing && !outgoingTitle) || (!incoming && !incomingTitle)
            || !definition || m_hiddenVideoTracks.contains(outgoingTrack)
            || outgoingTrack != incomingTrack || outgoingEnd != incomingStart
            || (definition->renderer != QStringLiteral("cross_dissolve")
                && spatialMaskKind(definition->renderer) == 0)) continue;
        const qint64 left = std::min(outgoing ? outgoing->durationFrames
            : outgoingTitle->durationFrames,
            std::max<qint64>(1, transition.durationFrames * transition.balance / 100));
        const qint64 right = std::min(incoming ? incoming->durationFrames
            : incomingTitle->durationFrames,
            std::max<qint64>(1, transition.durationFrames - left));
        const Asset *outgoingAsset = outgoing ? findAsset(outgoing->assetId) : nullptr;
        const Asset *incomingAsset = incoming ? findAsset(incoming->assetId) : nullptr;
        CrossRender render{&transition, outgoing, incoming, outgoingTitle, incomingTitle, left, right,
            transition.parameters.value(QStringLiteral("blend"), 50) / 100.0,
            visualInputBase + static_cast<int>(crossRenders.size() * 2),
            visualInputBase + static_cast<int>(crossRenders.size() * 2 + 1)};
        for (const auto edge : {std::pair{outgoingAsset, outgoingTitle},
                                std::pair{incomingAsset, incomingTitle}}) {
            if ((edge.first && edge.first->media.isStillImage) || edge.second)
                args << QStringLiteral("-loop") << QStringLiteral("1")
                     << QStringLiteral("-framerate") << rateText(m_projectRate);
            const QString path = edge.second ? renderTitleImage(*edge.second,
                m_exportWidth, m_exportHeight)
                : edge.first ? edge.first->media.path : QString{};
            if (path.isEmpty()) {
                setStatus(QStringLiteral("Could not prepare a transition source for export"));
                return;
            }
            args << QStringLiteral("-i") << path;
        }
        crossRenders.append(render);
    }
    QVector<int> titleInputIndices;
    for (const auto &title : m_titles) {
        const QString image = renderTitleImage(title, m_exportWidth, m_exportHeight);
        if (image.isEmpty()) {
            setStatus(QStringLiteral("Could not render a text title for export"));
            return;
        }
        titleInputIndices.append(visualInputBase + crossRenders.size() * 2
                                 + titleInputIndices.size());
        args << QStringLiteral("-loop") << QStringLiteral("1")
             << QStringLiteral("-framerate") << rateText(m_projectRate)
             << QStringLiteral("-i") << image;
        totalFrames = std::max(totalFrames, title.startFrame + title.durationFrames);
    }
    struct CaptionRender { QString image; int trackId = -1; qint64 first = 0; qint64 end = 0; };
    QVector<CaptionRender> captionSegments;
    const auto appendCaption = [&](const CaptionRow &row, const CaptionFrame &state,
                                   qint64 first, qint64 end) -> bool {
        if (first >= end || state.words.isEmpty()) return true;
        const QString image = renderCaptionImage(row, state,
                                                 m_exportWidth, m_exportHeight);
        if (image.isEmpty()) return false;
        if (!captionSegments.isEmpty() && captionSegments.last().trackId == row.trackId
            && captionSegments.last().image == image && captionSegments.last().end == first) {
            captionSegments.last().end = end;
            totalFrames = std::max(totalFrames, end);
            return true;
        }
        captionSegments.append({image, row.trackId, first, end});
        totalFrames = std::max(totalFrames, end);
        return true;
    };
    for (const auto &row : m_captionRows) {
        if (!row.visible) continue;
        for (const auto &cue : row.cues) {
            if (cue.words.isEmpty()) continue;
            QVector<qint64> boundaries;
            boundaries.reserve(cue.words.size() * 2);
            for (const auto &word : cue.words) {
                boundaries.append(word.firstFrame);
                boundaries.append(word.endFrame);
            }
            const bool eachWordMotion = row.displayMode == QStringLiteral("fade_word")
                || row.displayMode == QStringLiteral("typewriter_word");
            const bool cueMotion = row.displayMode == QStringLiteral("fade_sentence")
                || row.displayMode == QStringLiteral("typewriter");
            if (eachWordMotion || cueMotion) {
                const int phaseCount = row.displayMode.startsWith(QStringLiteral("fade")) ? 6 : 12;
                const int count = cueMotion ? 1 : cue.words.size();
                for (int wordIndex = 0; wordIndex < count; ++wordIndex) {
                    const qint64 start = cue.words[wordIndex].firstFrame;
                    const qint64 end = cueMotion ? cue.words.last().endFrame
                        : cue.words[wordIndex].endFrame;
                    for (int phase = 1; phase < phaseCount && start + phase < end; ++phase)
                        boundaries.append(start + phase);
                }
            }
            std::sort(boundaries.begin(), boundaries.end());
            boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
            for (qsizetype i = 0; i + 1 < boundaries.size(); ++i)
                if (!appendCaption(row, captionFrame(row, cue, boundaries[i]),
                                   boundaries[i], boundaries[i + 1])) {
                    setStatus(QStringLiteral("Could not render captions for export")); return;
                }
        }
    }
    struct CaptionRowRender { int input = -1; int trackId = -1; };
    QVector<CaptionRowRender> captionRenders;
    const QString captionDirectory = QDir(QStandardPaths::writableLocation(
        QStandardPaths::CacheLocation)).filePath(QStringLiteral("caption-sequences"));
    if (!captionSegments.isEmpty() && !QDir().mkpath(captionDirectory)) {
        setStatus(QStringLiteral("Could not create the caption export cache"));
        return;
    }
    for (const auto &row : m_captionRows) {
        if (!row.visible) continue;
        QVector<const CaptionRender *> segments;
        int imageWidth = 2;
        int imageHeight = 2;
        for (const auto &segment : captionSegments) {
            if (segment.trackId != row.trackId) continue;
            segments.append(&segment);
            const QImageReader reader(segment.image);
            const QSize size = reader.size();
            if (!size.isValid()) {
                setStatus(QStringLiteral("Could not read a rendered caption image"));
                return;
            }
            imageWidth = std::max(imageWidth, size.width());
            imageHeight = std::max(imageHeight, size.height());
        }
        if (segments.isEmpty()) continue;
        imageWidth += imageWidth % 2;
        imageHeight += imageHeight % 2;
        const QString sequenceDirectory = QDir(captionDirectory).filePath(
            QStringLiteral("%1-%2x%3").arg(row.id).arg(imageWidth).arg(imageHeight));
        if (!QDir().mkpath(sequenceDirectory)) {
            setStatus(QStringLiteral("Could not prepare caption images"));
            return;
        }
        const QString blankPath = QDir(sequenceDirectory).filePath(QStringLiteral("blank.png"));
        if (!QFileInfo::exists(blankPath)) {
            QImage blank(imageWidth, imageHeight, QImage::Format_ARGB32_Premultiplied);
            blank.fill(Qt::transparent);
            if (!blank.save(blankPath, "PNG")) {
                setStatus(QStringLiteral("Could not prepare a transparent caption frame"));
                return;
            }
        }
        QHash<QString, QString> paddedPaths;
        for (const auto *segment : segments) {
            if (paddedPaths.contains(segment->image)) continue;
            const QString paddedPath = QDir(sequenceDirectory).filePath(
                QFileInfo(segment->image).completeBaseName() + QStringLiteral(".png"));
            if (!QFileInfo::exists(paddedPath)) {
                const QImage source(segment->image);
                if (source.isNull()) {
                    setStatus(QStringLiteral("Could not read a caption frame"));
                    return;
                }
                QImage padded(imageWidth, imageHeight, QImage::Format_ARGB32_Premultiplied);
                padded.fill(Qt::transparent);
                QPainter painter(&padded);
                painter.drawImage((imageWidth - source.width()) / 2,
                    (imageHeight - source.height()) / 2, source);
                painter.end();
                if (!padded.save(paddedPath, "PNG")) {
                    setStatus(QStringLiteral("Could not prepare a caption frame"));
                    return;
                }
            }
            paddedPaths.insert(segment->image, paddedPath);
        }
        QVector<qint64> boundaries{0, totalFrames};
        for (const auto *segment : segments) {
            boundaries.append(segment->first);
            boundaries.append(segment->end);
        }
        std::sort(boundaries.begin(), boundaries.end());
        boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
        const QString listPath = QDir(sequenceDirectory).filePath(freshId()
            + QStringLiteral(".ffconcat"));
        QSaveFile list(listPath);
        if (!list.open(QIODevice::WriteOnly)) {
            setStatus(QStringLiteral("Could not write the caption sequence"));
            return;
        }
        QByteArray listData("ffconcat version 1.0\n");
        const auto appendFile = [&](const QString &path) {
            const QString escaped = QDir::fromNativeSeparators(path)
                .replace(QLatin1Char('\''), QStringLiteral("'\\''"));
            listData += "file '" + escaped.toUtf8() + "'\n";
        };
        QString lastPath = blankPath;
        for (qsizetype i = 0; i + 1 < boundaries.size(); ++i) {
            const qint64 first = boundaries[i];
            const qint64 end = boundaries[i + 1];
            if (end <= first) continue;
            lastPath = blankPath;
            for (const auto *segment : segments)
                if (segment->first <= first && first < segment->end)
                    lastPath = paddedPaths.value(segment->image);
            appendFile(lastPath);
            listData += "duration " + QString::number(
                static_cast<double>(end - first) * m_projectRate.denominator
                    / m_projectRate.numerator, 'f', 9).toUtf8() + '\n';
        }
        appendFile(lastPath);
        if (list.write(listData) != listData.size() || !list.commit()) {
            setStatus(QStringLiteral("Could not save the caption sequence"));
            return;
        }
        m_exportAuxiliaryPaths.append(listPath);
        const int input = int(visualInputBase + crossRenders.size() * 2
            + titleInputIndices.size() + captionRenders.size());
        args << QStringLiteral("-safe") << QStringLiteral("0")
             << QStringLiteral("-f") << QStringLiteral("concat")
             << QStringLiteral("-i") << listPath;
        captionRenders.append({input, row.trackId});
    }
    if (totalFrames <= 0) return;
    const qint64 exportFirst = explicitRange ? explicitRange->first
        : selectedRange ? m_timelineRange.first : 0;
    const qint64 exportEnd = explicitRange ? explicitRange->end
        : selectedRange ? m_timelineRange.end : totalFrames;
    if (exportFirst < 0 || exportEnd <= exportFirst || exportEnd > totalFrames) {
        setStatus(QStringLiteral("Choose a frame or range inside the project timeline"));
        return;
    }
    const auto seconds = [rate = m_projectRate](qint64 frames) {
        const long double value = static_cast<long double>(frames)
            * static_cast<long double>(rate.denominator)
            / static_cast<long double>(rate.numerator);
        return QString::number(static_cast<double>(value), 'f', 9);
    };
    const auto offsetExpr = [rate = m_projectRate](qint64 frames) {
        return QStringLiteral("%1*%2/(%3*TB)")
            .arg(frames).arg(rate.denominator).arg(rate.numerator);
    };
    const QString outputFps = rateText(m_projectRate);
    QStringList filters;
    filters << QStringLiteral("color=c=black:s=%1x%2:r=%3:d=%4[vbase]")
                   .arg(m_exportWidth).arg(m_exportHeight)
                   .arg(outputFps, seconds(totalFrames));
    filters << QStringLiteral("anullsrc=channel_layout=stereo:sample_rate=48000:d=%1[abase]")
                   .arg(seconds(totalFrames));
    QString videoPrevious = QStringLiteral("vbase");
    QStringList audioInputs{QStringLiteral("[abase]")};
    int videoIndex = 0;
    int audioIndex = 0;
    QVector<qsizetype> renderOrder;
    renderOrder.reserve(m_clips.size());
    for (qsizetype i = 0; i < m_clips.size(); ++i) renderOrder.append(i);
    std::stable_sort(renderOrder.begin(), renderOrder.end(), [this](qsizetype left, qsizetype right) {
        const int leftLayer = trackRow(m_clips[left].track);
        const int rightLayer = trackRow(m_clips[right].track);
        return leftLayer == rightLayer
            ? m_clips[left].startFrame < m_clips[right].startFrame
            : leftLayer > rightLayer;
    });
    struct VisualOverlay { int row = -1; bool title = false; qsizetype index = 0; };
    QVector<VisualOverlay> visualOverlays;
    for (qsizetype i = 0; i < m_titles.size(); ++i)
        if (!m_hiddenVideoTracks.contains(m_titles[i].track))
            visualOverlays.append({trackRow(m_titles[i].track), true, i});
    for (qsizetype i = 0; i < captionRenders.size(); ++i)
        visualOverlays.append({trackRow(captionRenders[i].trackId), false, i});
    std::stable_sort(visualOverlays.begin(), visualOverlays.end(),
        [](const VisualOverlay &left, const VisualOverlay &right) {
            return left.row > right.row;
        });
    qsizetype nextVisualOverlay = 0;
    std::function<void(const Clip &, int, qint64, qint64, qint64, bool, double,
                       const CrossRender *)>
        renderFrozenClipEdge;
    int frozenTitleEdgeIndex = 0;
    const auto renderFrozenTitleEdge = [&](const TitleClip &title, int input,
        qint64 timelineStart, qint64 frames, bool fadeIn, double blend,
        const CrossRender *cross) {
        if (frames <= 0) return;
        const qint64 localFrame = fadeIn ? 0 : title.durationFrames - 1;
        const int visualScale = valueAt(title.scaleKeyframes, localFrame, title.scalePercent);
        const int visualX = valueAt(title.positionXKeyframes, localFrame, title.positionX);
        const int visualY = valueAt(title.positionYKeyframes, localFrame, title.positionY);
        const QString edgeName = QStringLiteral("tedge%1").arg(frozenTitleEdgeIndex);
        const QString stepName = QStringLiteral("testep%1").arg(frozenTitleEdgeIndex++);
        Clip effectWindow;
        effectWindow.startFrame = timelineStart;
        effectWindow.durationFrames = frames;
        QStringList objectEffects;
        for (const auto &effect : m_trackEffects)
            if (effect.track == title.track) {
                const QString filter = trackEffectFilter(effect, effectWindow, m_projectRate);
                if (!filter.isEmpty()) objectEffects << filter;
            }
        const QString effectChain = objectEffects.isEmpty() ? QString{}
            : objectEffects.join(QLatin1Char(',')) + QLatin1Char(',');
        const QString mask = cross ? frozenEdgeMask(*cross, fadeIn) : QString{};
        const QString transitionFilter = mask.isEmpty()
            ? QStringLiteral("fade=t=%1:s=0:n=%2:alpha=1").arg(
                fadeIn ? QStringLiteral("in") : QStringLiteral("out")).arg(frames)
            : mask;
        filters << QStringLiteral("[%1:v:0]trim=duration=%2,setpts=PTS-STARTPTS,"
                                  "fps=%3,trim=end_frame=%4,"
                                  "scale=w='trunc(iw*%5/2)*2':h=-2,"
                                  "%6format=rgba,colorchannelmixer=aa=%7,"
                                  "%8,setpts=PTS+%9[%10]")
            .arg(input).arg(seconds(frames)).arg(outputFps).arg(frames)
            .arg(QString::number(visualScale * m_exportHeight / 108000.0, 'f', 9))
            .arg(effectChain).arg(QString::number(mask.isEmpty() ? blend : 1.0, 'f', 6))
            .arg(transitionFilter).arg(offsetExpr(timelineStart), edgeName);
        filters << QStringLiteral("[%1][%2]overlay=x='W*%3/100-w*%4/100':"
                                  "y='H*%5/100-h*%6/100':"
                                  "eof_action=pass:repeatlast=0:shortest=0[%7]")
            .arg(videoPrevious, edgeName).arg(visualX).arg(title.anchorX)
            .arg(visualY).arg(title.anchorY).arg(stepName);
        videoPrevious = stepName;
    };
    QSet<int> renderedTitleCrosses;
    const auto insertVisualOverlaysBelow = [&](int row) {
        while (nextVisualOverlay < visualOverlays.size()
            && visualOverlays[nextVisualOverlay].row > row) {
            const auto overlay = visualOverlays[nextVisualOverlay++];
            if (overlay.title) {
                const auto &title = m_titles[overlay.index];
                const QString titleName = QStringLiteral("title%1").arg(overlay.index);
                const QString outputName = QStringLiteral("tstep%1").arg(overlay.index);
                const QString localTime = QStringLiteral("(t-%1)").arg(seconds(title.startFrame));
                const QString localFrameTime = QStringLiteral("(n*%1/%2)")
                    .arg(m_projectRate.denominator).arg(m_projectRate.numerator);
                const QString titleScale = envelopeExpression(title.scaleKeyframes,
                    title.scalePercent, title.durationFrames, m_projectRate,
                    m_exportHeight / 108000.0, 0, localFrameTime);
                const QString x = envelopeExpression(title.positionXKeyframes,
                    title.positionX, title.durationFrames, m_projectRate, 0.01, 0, localTime);
                const QString y = envelopeExpression(title.positionYKeyframes,
                    title.positionY, title.durationFrames, m_projectRate, 0.01, 0, localTime);
                Clip effectWindow;
                effectWindow.startFrame = title.startFrame;
                effectWindow.durationFrames = title.durationFrames;
                QStringList objectEffects;
                for (const auto &effect : m_trackEffects)
                    if (effect.track == title.track) {
                        const QString filter = trackEffectFilter(effect, effectWindow,
                                                                 m_projectRate);
                        if (!filter.isEmpty()) objectEffects << filter;
                    }
                const QString effectChain = objectEffects.isEmpty() ? QString{}
                    : objectEffects.join(QLatin1Char(',')) + QLatin1Char(',');
                QStringList transitionFades;
                for (const auto *transition : transitionsByVisual.value(title.id)) {
                    const auto *definition = m_transitionRegistry.find(transition->definitionId);
                    if (!definition) continue;
                    const bool paired = isPairedTransition(*transition);
                    if (spatialMaskKind(definition->renderer) > 0) {
                        if (!paired) {
                            const QString mask = singleEdgeMask(*transition,
                                title.durationFrames, transition->incomingClipId == title.id);
                            if (!mask.isEmpty()) transitionFades << mask;
                        }
                        continue;
                    }
                    if (paired && definition->renderer == QStringLiteral("cross_dissolve"))
                        continue;
                    if (transition->outgoingClipId == title.id) {
                        const qint64 frames = std::min(title.durationFrames,
                            paired ? std::max<qint64>(1, transition->durationFrames
                                * transition->balance / 100) : transition->durationFrames);
                        transitionFades << QStringLiteral("fade=t=out:s=%1:n=%2:alpha=1")
                            .arg(title.durationFrames - frames).arg(frames);
                    }
                    if (transition->incomingClipId == title.id) {
                        const qint64 left = paired ? std::max<qint64>(1,
                            transition->durationFrames * transition->balance / 100) : 0;
                        const qint64 frames = std::min(title.durationFrames,
                            paired ? std::max<qint64>(1, transition->durationFrames - left)
                                   : transition->durationFrames);
                        transitionFades << QStringLiteral("fade=t=in:s=0:n=%1:alpha=1")
                            .arg(frames);
                    }
                }
                const QString fadeChain = transitionFades.isEmpty() ? QString{}
                    : QLatin1Char(',') + transitionFades.join(QLatin1Char(','));
                filters << QStringLiteral("[%1:v:0]trim=duration=%2,setpts=PTS-STARTPTS+%3,"
                                          "scale=w='trunc(iw*%4/2)*2':h=-2:eval=frame,"
                                          "%5format=rgba%6[%7]")
                               .arg(titleInputIndices[overlay.index]).arg(seconds(title.durationFrames))
                               .arg(offsetExpr(title.startFrame))
                               .arg(titleScale, effectChain, fadeChain, titleName);
                filters << QStringLiteral("[%1][%2]overlay=x='W*%3-w*%4/100':"
                                          "y='H*%5-h*%6/100':eval=frame:eof_action=pass:"
                                          "repeatlast=0:shortest=0[%7]")
                               .arg(videoPrevious, titleName, x)
                               .arg(title.anchorX).arg(y).arg(title.anchorY).arg(outputName);
                videoPrevious = outputName;
                ++videoIndex;
                for (qsizetype crossIndex = 0; crossIndex < crossRenders.size(); ++crossIndex) {
                    const auto &cross = crossRenders[crossIndex];
                    if ((!cross.outgoingTitle && !cross.incomingTitle)
                        || renderedTitleCrosses.contains(int(crossIndex))) continue;
                    if (cross.outgoingTitle != &title && cross.incomingTitle != &title) continue;
                    if (cross.outgoingTitle && cross.outgoingTitle > &title) continue;
                    if (cross.incomingTitle && cross.incomingTitle > &title) continue;
                    renderedTitleCrosses.insert(int(crossIndex));
                    const qint64 cut = cross.incoming ? cross.incoming->startFrame
                        : cross.incomingTitle->startFrame;
                    if (cross.incomingTitle)
                        renderFrozenTitleEdge(*cross.incomingTitle, cross.incomingInput,
                            cut - cross.leftFrames, cross.leftFrames, true, cross.blend, &cross);
                    else
                        renderFrozenClipEdge(*cross.incoming, cross.incomingInput,
                            cross.incoming->sourceFirstFrame, cut - cross.leftFrames,
                            cross.leftFrames, true, cross.blend, &cross);
                    if (cross.outgoingTitle)
                        renderFrozenTitleEdge(*cross.outgoingTitle, cross.outgoingInput,
                            cut, cross.rightFrames, false, 1.0 - cross.blend, &cross);
                    else
                        renderFrozenClipEdge(*cross.outgoing, cross.outgoingInput,
                            cross.outgoing->sourceFirstFrame
                                + std::max<qint64>(1, cross.outgoing->sourceFrameCount) - 1,
                            cut, cross.rightFrames, false, 1.0 - cross.blend, &cross);
                }
            } else {
                const auto &caption = captionRenders[overlay.index];
                const auto rowIt = std::find_if(m_captionRows.cbegin(), m_captionRows.cend(),
                    [&](const CaptionRow &row) { return row.trackId == caption.trackId; });
                if (rowIt == m_captionRows.cend()) continue;
                const CaptionRow &row = *rowIt;
                const QString overlayName = QStringLiteral("caption%1").arg(overlay.index);
                const QString outputName = QStringLiteral("cstep%1").arg(overlay.index);
                const QString frameTime = QStringLiteral("(n*%1/%2)")
                    .arg(m_projectRate.denominator).arg(m_projectRate.numerator);
                const QString size = envelopeExpression(row.scaleKeyframes,
                    row.scalePercent, totalFrames, m_projectRate,
                    m_exportHeight / 108000.0, 0, frameTime);
                const QString x = envelopeExpression(row.positionXKeyframes,
                    row.positionX, totalFrames, m_projectRate, 0.01, 0,
                    QStringLiteral("t"));
                const QString y = envelopeExpression(row.positionYKeyframes,
                    row.positionY, totalFrames, m_projectRate, 0.01, 0,
                    QStringLiteral("t"));
                Clip effectWindow;
                effectWindow.durationFrames = totalFrames;
                QStringList objectEffects;
                for (const auto &effect : m_trackEffects)
                    if (effect.track == row.trackId) {
                        const QString filter = trackEffectFilter(effect, effectWindow,
                                                                 m_projectRate);
                        if (!filter.isEmpty()) objectEffects << filter;
                    }
                const QString effectChain = objectEffects.isEmpty() ? QString{}
                    : objectEffects.join(QLatin1Char(',')) + QLatin1Char(',');
                filters << QStringLiteral("[%1:v:0]fps=%2,trim=end_frame=%3,"
                                          "setpts=PTS-STARTPTS,"
                                          "scale=w='trunc(iw*%4/2)*2':h=-2:eval=frame,"
                                          "%5format=rgba[%6]")
                    .arg(caption.input).arg(outputFps).arg(totalFrames)
                    .arg(size, effectChain, overlayName);
                filters << QStringLiteral("[%1][%2]overlay=x='W*%3-w/2':y='H*%4-h/2':"
                                          "eval=frame:eof_action=pass:repeatlast=0:shortest=0[%5]")
                    .arg(videoPrevious, overlayName, x, y, outputName);
                videoPrevious = outputName;
            }
        }
    };
    int frozenEdgeIndex = 0;
    renderFrozenClipEdge = [&](const Clip &clip, int inputIndex,
                                      qint64 sourceFrame, qint64 timelineStart,
                                      qint64 frames, bool fadeIn, double blend,
                                      const CrossRender *cross) {
        const QString edgeName = QStringLiteral("edge%1").arg(frozenEdgeIndex);
        const QString stepName = QStringLiteral("estep%1").arg(frozenEdgeIndex++);
        const qint64 visualFrame = fadeIn ? 0 : clip.durationFrames - 1;
        const int visualScale = valueAt(clip.scaleKeyframes, visualFrame, clip.scalePercent);
        const int visualX = valueAt(clip.positionXKeyframes, visualFrame, clip.positionX);
        const int visualY = valueAt(clip.positionYKeyframes, visualFrame, clip.positionY);
        const int visualRotation = valueAt(clip.rotationKeyframes, visualFrame, clip.rotationDegrees);
        const int visualOpacity = valueAt(clip.opacityKeyframes, visualFrame, clip.opacityPercent);
        const int scaledWidth = std::max(2, (m_exportWidth * visualScale / 100) / 2 * 2);
        const int scaledHeight = std::max(2, (m_exportHeight * visualScale / 100) / 2 * 2);
        Clip snapshot = clip;
        snapshot.startFrame = timelineStart;
        snapshot.durationFrames = frames;
        QStringList applied;
        for (const auto &effect : m_trackEffects)
            if (effect.track == clip.track) {
                const QString filter = trackEffectFilter(effect, snapshot, m_projectRate);
                if (!filter.isEmpty()) applied << filter;
            }
        const QString clipEffect = effectFilter(clip.effectId, clip.effectStrength);
        if (!clipEffect.isEmpty()) applied << clipEffect;
        const QString effects = applied.isEmpty() ? QString{}
            : applied.join(QLatin1Char(',')) + QLatin1Char(',');
        const QString mask = cross ? frozenEdgeMask(*cross, fadeIn) : QString{};
        const QString transitionFilter = mask.isEmpty()
            ? QStringLiteral("fade=t=%1:s=0:n=%2:alpha=1").arg(
                fadeIn ? QStringLiteral("in") : QStringLiteral("out")).arg(frames)
            : mask;
        filters << QStringLiteral("[%1:v:0]trim=start_frame=%2:end_frame=%3,"
                                  "setpts=PTS-STARTPTS,tpad=stop_mode=clone:stop_duration=%4,"
                                  "fps=%5,trim=end_frame=%6,"
                                  "scale=%7:%8:force_original_aspect_ratio=decrease,"
                                  "%9format=rgba,colorchannelmixer=aa=%10,"
                                  "%11,"
                                  "rotate=%12*PI/180:ow=rotw(iw):oh=roth(ih):c=none,"
                                  "setpts=PTS+%13[%14]")
                       .arg(inputIndex).arg(sourceFrame).arg(sourceFrame + 1)
                       .arg(seconds(frames + 1)).arg(outputFps).arg(frames)
                       .arg(scaledWidth).arg(scaledHeight).arg(effects)
                        .arg(QString::number(visualOpacity / 100.0
                            * (mask.isEmpty() ? blend : 1.0), 'f', 6))
                       .arg(transitionFilter).arg(visualRotation)
                       .arg(offsetExpr(timelineStart), edgeName);
        filters << QStringLiteral("[%1][%2]overlay=x=(W-w)/2+%3:y=(H-h)/2+%4:"
                                  "eof_action=pass:repeatlast=0:shortest=0[%5]")
                       .arg(videoPrevious, edgeName,
                            QString::number(visualX),
                            QString::number(visualY), stepName);
        videoPrevious = stepName;
    };
    for (const qsizetype i : renderOrder) {
        const auto &clip = m_clips[i];
        if (isVideoTrack(clip.track)) insertVisualOverlaysBelow(trackRow(clip.track));
        const auto *asset = findAsset(clip.assetId);
        const QString input = QString::number(i);
        const QString sourceFirst = QString::number(clip.sourceFirstFrame);
        const QString sourceEnd = QString::number(clip.sourceFirstFrame + clip.sourceFrameCount);
        if (asset->media.hasVideo && clip.includeVideo && isVideoTrack(clip.track)
            && !m_hiddenVideoTracks.contains(clip.track)) {
            const CrossRender *crossAtStart = nullptr;
            for (const auto &render : crossRenders)
                if (render.incoming == &clip && !render.outgoingTitle
                    && !render.incomingTitle) { crossAtStart = &render; break; }
            if (crossAtStart) {
                renderFrozenClipEdge(clip, crossAtStart->incomingInput,
                    clip.sourceFirstFrame,
                    clip.startFrame - crossAtStart->leftFrames,
                    crossAtStart->leftFrames, true, crossAtStart->blend, crossAtStart);
            }
            const QString videoName = QStringLiteral("vclip%1").arg(videoIndex);
            const QString rawVideoName = QStringLiteral("vraw%1").arg(videoIndex);
            const QString outputName = QStringLiteral("vstep%1").arg(videoIndex);
            const TrackEffect *animatedBlur = nullptr;
            const QString localTime = QStringLiteral("(t-%1)").arg(seconds(clip.startFrame));
            // Rotate the source at its fixed native size, before the animated scale.
            // rotate's output dimensions are fixed at filter configuration time;
            // placing it after scale clips later, larger keyframed frames.
            const double sourceRatio = asset->media.width > 0 && asset->media.height > 0
                ? double(asset->media.width) / asset->media.height
                : double(m_exportWidth) / m_exportHeight;
            const double fitWidth = std::min(double(m_exportWidth), m_exportHeight * sourceRatio);
            const double fitHeight = fitWidth / sourceRatio;
            const double angle = clip.rotationDegrees * std::acos(-1.0) / 180.0;
            const double cosine = std::abs(std::cos(angle));
            const double sine = std::abs(std::sin(angle));
            const bool animatedRotation = !clip.rotationKeyframes.isEmpty();
            const double rotatedFitWidth = animatedRotation ? std::hypot(fitWidth, fitHeight)
                : fitWidth * cosine + fitHeight * sine;
            const double rotatedFitHeight = animatedRotation ? rotatedFitWidth
                : fitWidth * sine + fitHeight * cosine;
            int maximumScale = clip.scalePercent;
            for (const auto &keyframe : clip.scaleKeyframes)
                maximumScale = std::max(maximumScale, keyframe.value);
            int maximumX = std::abs(clip.positionX);
            int maximumY = std::abs(clip.positionY);
            for (const auto &keyframe : clip.positionXKeyframes)
                maximumX = std::max(maximumX, std::abs(keyframe.value));
            for (const auto &keyframe : clip.positionYKeyframes)
                maximumY = std::max(maximumY, std::abs(keyframe.value));
            // overlay keeps the dimensions of its second input from the first
            // frame. Wrap the animated image in a constant, transparent surface
            // so every frame has the same geometry at the compositor boundary.
            const auto wrapperSize = [maximumScale](double fittedSize,
                                                   int canvasSize, int maximumPosition) {
                const int padded = static_cast<int>(std::ceil(
                    std::max(fittedSize * maximumScale / 100.0,
                             double(canvasSize)))) + maximumPosition * 2 + 16;
                return std::max(2, ((padded + 1) / 2) * 2);
            };
            const int wrapperWidth = wrapperSize(rotatedFitWidth, m_exportWidth, maximumX);
            const int wrapperHeight = wrapperSize(rotatedFitHeight, m_exportHeight, maximumY);
            const QString sourceRotation = animatedRotation
                ? QStringLiteral("format=rgba,rotate='%1':ow=hypot(iw\\,ih):oh=ow:c=none,")
                    .arg(visualEnvelopeExpression(clip.rotationKeyframes,
                        clip.rotationDegrees, clip.durationFrames, m_projectRate,
                        std::acos(-1.0) / 180.0, QStringLiteral("t")))
                : clip.rotationDegrees == 0 ? QString{}
                : QStringLiteral("format=rgba,rotate=%1*PI/180:ow=rotw(iw):oh=roth(ih):c=none,")
                    .arg(clip.rotationDegrees);
            const QString dynamicWidth = visualEnvelopeExpression(clip.scaleKeyframes,
                clip.scalePercent, clip.durationFrames, m_projectRate,
                rotatedFitWidth / 100.0, QStringLiteral("t"));
            const QString dynamicX = visualEnvelopeExpression(clip.positionXKeyframes,
                clip.positionX, clip.durationFrames, m_projectRate, 1, localTime);
            const QString dynamicY = visualEnvelopeExpression(clip.positionYKeyframes,
                clip.positionY, clip.durationFrames, m_projectRate, 1, localTime);
            const QString padX = visualEnvelopeExpression(clip.positionXKeyframes,
                clip.positionX, clip.durationFrames, m_projectRate, 1,
                QStringLiteral("t"));
            const QString padY = visualEnvelopeExpression(clip.positionYKeyframes,
                clip.positionY, clip.durationFrames, m_projectRate, 1,
                QStringLiteral("t"));
            const QString timing = asset->media.isStillImage
                ? QStringLiteral("trim=duration=%1,setpts=PTS-STARTPTS")
                      .arg(seconds(clip.durationFrames))
                : QStringLiteral("trim=start_frame=%1:end_frame=%2,"
                                 "setpts=(PTS-STARTPTS)*100/%3")
                      .arg(sourceFirst, sourceEnd, QString::number(clip.speedPercent));
            QString videoFilter = QStringLiteral("[%1:v:0]%2,fps=%3,%4"
                                      "scale=w='round(%5/2)*2':h=-2:eval=frame,"
                                      "EFFECT_TOKENformat=rgba,"
                                      "pad=w=%6:h=%7:x=(ow-iw)/2:y=(oh-ih)/2:"
                                      "color=black@0:eval=frame,"
                                      "crop=w=%10:h=%11:x='(iw-ow)/2-(%8)':"
                                      "y='(ih-oh)/2-(%9)':exact=1,"
                                       "OPACITY_TOKENsetpts=PTS+%12")
                            .arg(input, timing, outputFps,
                                 sourceRotation, dynamicWidth,
                                 QString::number(wrapperWidth), QString::number(wrapperHeight),
                                 padX, padY, QString::number(m_exportWidth),
                                  QString::number(m_exportHeight),
                                  offsetExpr(clip.startFrame));
            QString opacityFilter = QStringLiteral("colorchannelmixer=aa=%1,")
                .arg(QString::number(valueAt(clip.opacityKeyframes, 0,
                    clip.opacityPercent) / 100.0, 'f', 3));
            if (!clip.opacityKeyframes.isEmpty()) {
                const QString filterName = QStringLiteral("clipopacity%1").arg(videoIndex);
                QStringList commands;
                int previous = valueAt(clip.opacityKeyframes, 0, clip.opacityPercent);
                for (qint64 frame = 1; frame < clip.durationFrames; ++frame) {
                    const int opacity = valueAt(clip.opacityKeyframes, frame, clip.opacityPercent);
                    if (opacity == previous) continue;
                    commands << QStringLiteral("%1 colorchannelmixer@%2 aa %3")
                        .arg(seconds(frame), filterName,
                            QString::number(opacity / 100.0, 'f', 3));
                    previous = opacity;
                }
                opacityFilter = commands.isEmpty() ? opacityFilter
                    : QStringLiteral("sendcmd=c='%1',colorchannelmixer@%2=aa=%3,")
                        .arg(commands.join(QLatin1Char(';')), filterName,
                            QString::number(valueAt(clip.opacityKeyframes, 0,
                                clip.opacityPercent) / 100.0, 'f', 3));
            }
            videoFilter.replace(QStringLiteral("OPACITY_TOKEN"), opacityFilter);
            QStringList clipEffects;
            for (const auto &trackEffect : m_trackEffects)
                if (trackEffect.track == clip.track) {
                    if (trackEffect.id == QStringLiteral("datacut.video.blur")
                        && !trackEffect.keyframes.isEmpty()) {
                        animatedBlur = &trackEffect;
                        continue;
                    }
                    const QString filter = trackEffectFilter(trackEffect, clip, m_projectRate);
                    if (!filter.isEmpty()) clipEffects << filter;
                }
            const QString clipEffect = effectFilter(clip.effectId, clip.effectStrength);
            if (!clipEffect.isEmpty()) clipEffects << clipEffect;
            for (const auto *transition : transitionsByVisual.value(clip.id)) {
                const auto *definition = m_transitionRegistry.find(transition->definitionId);
                if (!definition) continue;
                const bool paired = isPairedTransition(*transition);
                if (spatialMaskKind(definition->renderer) > 0) {
                    if (!paired) {
                        const QString mask = singleEdgeMask(*transition,
                            clip.durationFrames, transition->incomingClipId == clip.id);
                        if (!mask.isEmpty()) clipEffects << mask;
                    }
                    continue;
                }
                if (paired && definition->renderer == QStringLiteral("cross_dissolve")) continue;
                if (transition->outgoingClipId == clip.id) {
                    const qint64 frames = std::min(clip.durationFrames,
                        paired ? std::max<qint64>(1, transition->durationFrames
                            * transition->balance / 100) : transition->durationFrames);
                    clipEffects << QStringLiteral("fade=t=out:s=%1:n=%2:c=black")
                                       .arg(clip.durationFrames - frames).arg(frames);
                }
                if (transition->incomingClipId == clip.id) {
                    const qint64 left = paired ? std::max<qint64>(1, transition->durationFrames
                        * transition->balance / 100) : 0;
                    const qint64 frames = std::min(clip.durationFrames,
                        paired ? std::max<qint64>(1, transition->durationFrames - left)
                               : transition->durationFrames);
                    clipEffects << QStringLiteral("fade=t=in:s=0:n=%1:c=black").arg(frames);
                }
            }
            const QString effect = clipEffects.join(QLatin1Char(','));
            videoFilter.replace(QStringLiteral("EFFECT_TOKEN"),
                                effect.isEmpty() ? QString{} : effect + QLatin1Char(','));
            videoFilter += QStringLiteral("[%1]")
                .arg(animatedBlur ? rawVideoName : videoName);
            filters << videoFilter;
            if (animatedBlur) {
                const QString originalName = QStringLiteral("vorig%1").arg(videoIndex);
                const QString blurInputName = QStringLiteral("vblurin%1").arg(videoIndex);
                const QString blurredName = QStringLiteral("vblur%1").arg(videoIndex);
                filters << QStringLiteral("[%1]split=2[%2][%3]")
                    .arg(rawVideoName, originalName, blurInputName);
                filters << QStringLiteral("[%1]gblur=sigma=10[%2]")
                    .arg(blurInputName, blurredName);
                const QString amount = blurMixExpression(*animatedBlur, clip, m_projectRate, true);
                filters << QStringLiteral("[%1][%2]blend=all_expr='A*(1-(%3))+B*(%3)'[%4]")
                    .arg(originalName, blurredName, amount, videoName);
            }
            filters << QStringLiteral("[%1][%2]overlay=x=0:y=0:"
                                      "eof_action=pass:repeatlast=0:shortest=0[%3]")
                            .arg(videoPrevious, videoName, outputName);
            videoPrevious = outputName;
            ++videoIndex;
            if (crossAtStart) {
                const Clip &outgoing = *crossAtStart->outgoing;
                renderFrozenClipEdge(outgoing, crossAtStart->outgoingInput,
                    outgoing.sourceFirstFrame + std::max<qint64>(1, outgoing.sourceFrameCount) - 1,
                    clip.startFrame, crossAtStart->rightFrames, false,
                    1.0 - crossAtStart->blend, crossAtStart);
            }
        }
        if (asset->media.hasAudio && clip.includeAudio
            && !m_mutedTracks.contains(clip.track)) {
            const QString audioName = QStringLiteral("aclip%1").arg(audioIndex++);
            QString trim;
            const bool useVoice = voiceInputs.contains(clip.id);
            const QString audioInput = useVoice ? QString::number(voiceInputs.value(clip.id)) : input;
            if (useVoice) {
                const double rate = double(asset->media.frameRate.numerator)
                    / double(asset->media.frameRate.denominator);
                trim = QStringLiteral("atrim=start=%1:end=%2")
                    .arg(QString::number((clip.sourceFirstFrame - clip.voiceSourceFirstFrame) / rate, 'f', 9),
                         QString::number((clip.sourceFirstFrame - clip.voiceSourceFirstFrame
                             + clip.sourceFrameCount) / rate, 'f', 9));
            } else
            if (asset->media.hasVideo) {
                const auto sourceSeconds = [rate = asset->media.frameRate](qint64 frame) {
                    const long double value = static_cast<long double>(frame)
                        * static_cast<long double>(rate.denominator)
                        / static_cast<long double>(rate.numerator);
                    return QString::number(static_cast<double>(value), 'f', 9);
                };
                trim = QStringLiteral("atrim=start=%1:end=%2")
                    .arg(sourceSeconds(clip.sourceFirstFrame),
                         sourceSeconds(clip.sourceFirstFrame + clip.sourceFrameCount));
            } else {
                trim = QStringLiteral("atrim=start_sample=%1:end_sample=%2")
                    .arg(sourceFirst, sourceEnd);
            }
            QString audioFilter = QStringLiteral("[%1:a:0]%2,asetpts=PTS-STARTPTS,"
                                                 "atempo=%3,aresample=48000,"
                                                 "aformat=channel_layouts=stereo,asetpts=PTS-STARTPTS,%4")
                                      .arg(audioInput, trim,
                                           QString::number(clip.speedPercent / 100.0, 'f', 3),
                                           gainFilter(clip, m_projectRate));
            const QString clipAudioEffect = audioEffectFilter(
                clip.audioEffectId, clip.audioEffectStrength);
            if (!clipAudioEffect.isEmpty())
                audioFilter += QLatin1Char(',') + clipAudioEffect;
            if (clip.fadeInFrames > 0)
                audioFilter += QStringLiteral(",afade=t=in:st=0:d=%1")
                    .arg(seconds(clip.fadeInFrames));
            if (clip.fadeOutFrames > 0)
                audioFilter += QStringLiteral(",afade=t=out:st=%1:d=%2")
                    .arg(seconds(clip.durationFrames - clip.fadeOutFrames),
                         seconds(clip.fadeOutFrames));
            // amix consumes samples from each input immediately; shifting PTS
            // alone does not leave silence before a later timeline clip.
            const qint64 delaySamples = rescaleFrames(
                clip.startFrame, m_projectRate, FrameRate{48000, 1});
            audioFilter += QStringLiteral(",adelay=%1S:all=1[%2]")
                .arg(delaySamples).arg(audioName);
            filters << audioFilter;
            audioInputs << QStringLiteral("[%1]").arg(audioName);
        }
    }
    insertVisualOverlaysBelow(-1);
    if (videoIndex == 0 && m_titles.isEmpty() && !hasVisibleCaptions) {
        setStatus(QStringLiteral("Project video export needs video, text, or visible captions"));
        return;
    }
    filters << QStringLiteral("[%1]format=%2[vout]")
                   .arg(videoPrevious, stillFrame ? QStringLiteral("rgb24")
                                                   : QStringLiteral("yuv420p"));
    if (audioInputs.size() == 1)
        filters << QStringLiteral("[abase]anull[aout]");
    else
        filters << audioInputs.join(QString{})
            + QStringLiteral("amix=inputs=%1:duration=first:dropout_transition=0:normalize=0,"
                             "alimiter=limit=0.95[aout]")
                  .arg(audioInputs.size());
    QString videoMap = QStringLiteral("[vout]");
    QString audioMap = QStringLiteral("[aout]");
    if (selectedRange || explicitRange) {
        filters << QStringLiteral("[vout]trim=start_frame=%1:end_frame=%2,"
                                  "setpts=PTS-STARTPTS[vselected]")
                       .arg(exportFirst).arg(exportEnd);
        filters << QStringLiteral("[aout]atrim=start=%1:end=%2,"
                                  "asetpts=PTS-STARTPTS[aselected]")
                       .arg(seconds(exportFirst), seconds(exportEnd));
        videoMap = QStringLiteral("[vselected]");
        audioMap = QStringLiteral("[aselected]");
    }
    if (stillFrame) filters << QStringLiteral("[aout]anullsink");
    const QString graph = filters.join(QLatin1Char(';'));
    const QString graphPath = qEnvironmentVariable("DATACUT_DIAGNOSTIC_EXPORT_GRAPH");
    if (!graphPath.isEmpty()) {
        QFile graphFile(graphPath);
        if (graphFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
            graphFile.write(graph.toUtf8());
    }
    const QString graphDirectory = QDir(QStandardPaths::writableLocation(
        QStandardPaths::CacheLocation)).filePath(QStringLiteral("export-graphs"));
    if (!QDir().mkpath(graphDirectory)) {
        setStatus(QStringLiteral("Could not create the export graph folder"));
        return;
    }
    const QString graphFile = QDir(graphDirectory).filePath(freshId() + QStringLiteral(".txt"));
    QSaveFile savedGraph(graphFile);
    const QByteArray graphBytes = graph.toUtf8();
    if (!savedGraph.open(QIODevice::WriteOnly)
        || savedGraph.write(graphBytes) != graphBytes.size() || !savedGraph.commit()) {
        setStatus(QStringLiteral("Could not write the export filter graph"));
        return;
    }
    m_exportGraphPath = graphFile;
    args << QStringLiteral("-/filter_complex") << graphFile
         << QStringLiteral("-map") << videoMap
         << QStringLiteral("-frames:v") << QString::number(exportEnd - exportFirst);
    if (stillFrame) {
        args << QStringLiteral("-c:v") << QStringLiteral("png")
             << QStringLiteral("-pix_fmt") << QStringLiteral("rgb24");
    } else {
        args << QStringLiteral("-map") << audioMap
             << QStringLiteral("-t") << seconds(exportEnd - exportFirst)
             << QStringLiteral("-c:v") << QStringLiteral("libx264")
             << QStringLiteral("-preset") << QStringLiteral("medium")
             << QStringLiteral("-crf") << QStringLiteral("18")
             << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
             << QStringLiteral("-c:a") << QStringLiteral("aac")
             << QStringLiteral("-movflags") << QStringLiteral("+faststart");
    }
    try {
        launchExport(args, output,
                     rescaleFrames(exportEnd - exportFirst, m_projectRate,
                                   FrameRate{1000000, 1}));
    } catch (const std::exception &error) {
        setStatus(QStringLiteral("Project export failed: %1").arg(QString::fromUtf8(error.what())));
    }
}

void EditorController::startProjectAudioExport(const QUrl &outputUrl)
{
    if (m_exportProcess) {
        setStatus(QStringLiteral("An export is already running"));
        return;
    }
    if (!outputUrl.isLocalFile()) {
        setStatus(QStringLiteral("Choose a local audio output file"));
        return;
    }
    const QString output = outputUrl.toLocalFile();
    const QString extension = QFileInfo(output).suffix().toLower();
    if (extension != QStringLiteral("m4a") && extension != QStringLiteral("wav")
        && extension != QStringLiteral("mp3") && extension != QStringLiteral("flac")) {
        setStatus(QStringLiteral("Choose M4A, WAV, MP3, or FLAC for project audio"));
        return;
    }
    if (QFileInfo::exists(output)) {
        setStatus(QStringLiteral("Output already exists. Choose a new filename"));
        return;
    }
    const auto seconds = [rate = m_projectRate](qint64 frames) {
        const long double value = static_cast<long double>(frames)
            * static_cast<long double>(rate.denominator)
            / static_cast<long double>(rate.numerator);
        return QString::number(static_cast<double>(value), 'f', 9);
    };
    qint64 totalFrames = 0;
    bool hasAudioClip = false;
    QStringList args{QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"),
                     QStringLiteral("-n"), QStringLiteral("-loglevel"),
                     QStringLiteral("error"), QStringLiteral("-progress"),
                     QStringLiteral("pipe:1")};
    QVector<const Clip *> audioClips;
    for (const auto &clip : m_clips) {
        if (!clip.includeAudio) continue;
        hasAudioClip = true;
        totalFrames = std::max(totalFrames, clip.startFrame + clip.durationFrames);
        if (m_mutedTracks.contains(clip.track)) continue;
        const auto *asset = findAsset(clip.assetId);
        if (!asset || !asset->media.hasAudio || !QFileInfo::exists(asset->media.path)) {
            setStatus(QStringLiteral("A timeline audio clip has missing media"));
            return;
        }
        if (QFileInfo(output).absoluteFilePath() == QFileInfo(asset->media.path).absoluteFilePath()) {
            setStatus(QStringLiteral("Output must differ from every source file"));
            return;
        }
        const bool useVoice = clip.voiceChangeEnabled && !clip.voiceChangePath.isEmpty();
        if (useVoice && (!QFileInfo::exists(clip.voiceChangePath)
            || clip.sourceFirstFrame < clip.voiceSourceFirstFrame
            || clip.sourceFirstFrame + clip.sourceFrameCount
                > clip.voiceSourceFirstFrame + clip.voiceSourceFrameCount)) {
            setStatus(QStringLiteral("Changed voice audio is missing or does not cover this clip"));
            return;
        }
        args << QStringLiteral("-i") << (useVoice ? clip.voiceChangePath : asset->media.path);
        audioClips.append(&clip);
    }
    if (!hasAudioClip || totalFrames <= 0) {
        setStatus(QStringLiteral("The timeline has no audio to export"));
        return;
    }
    QStringList filters;
    filters << QStringLiteral("anullsrc=channel_layout=stereo:sample_rate=48000:d=%1[base]")
                   .arg(seconds(totalFrames));
    QStringList inputs{QStringLiteral("[base]")};
    for (qsizetype i = 0; i < audioClips.size(); ++i) {
        const Clip &clip = *audioClips[i];
        const auto *asset = findAsset(clip.assetId);
        const auto sourceSeconds = [rate = asset->media.frameRate](qint64 frame) {
            const long double value = static_cast<long double>(frame)
                * static_cast<long double>(rate.denominator)
                / static_cast<long double>(rate.numerator);
            return QString::number(static_cast<double>(value), 'f', 9);
        };
        const bool useVoice = clip.voiceChangeEnabled && !clip.voiceChangePath.isEmpty();
        const QString trim = useVoice
            ? QStringLiteral("atrim=start=%1:end=%2")
                  .arg(sourceSeconds(clip.sourceFirstFrame - clip.voiceSourceFirstFrame),
                       sourceSeconds(clip.sourceFirstFrame - clip.voiceSourceFirstFrame
                           + clip.sourceFrameCount))
            : asset->media.hasVideo
            ? QStringLiteral("atrim=start=%1:end=%2")
                  .arg(sourceSeconds(clip.sourceFirstFrame),
                       sourceSeconds(clip.sourceFirstFrame + clip.sourceFrameCount))
            : QStringLiteral("atrim=start_sample=%1:end_sample=%2")
                  .arg(clip.sourceFirstFrame)
                  .arg(clip.sourceFirstFrame + clip.sourceFrameCount);
        QString filter = QStringLiteral("[%1:a:0]%2,asetpts=PTS-STARTPTS,"
                                        "atempo=%3,aresample=48000,"
                                        "aformat=channel_layouts=stereo,asetpts=PTS-STARTPTS,%4")
                             .arg(i).arg(trim)
                             .arg(QString::number(clip.speedPercent / 100.0, 'f', 3))
                             .arg(gainFilter(clip, m_projectRate));
        const QString clipAudioEffect = audioEffectFilter(
            clip.audioEffectId, clip.audioEffectStrength);
        if (!clipAudioEffect.isEmpty())
            filter += QLatin1Char(',') + clipAudioEffect;
        if (clip.fadeInFrames > 0)
            filter += QStringLiteral(",afade=t=in:st=0:d=%1")
                .arg(seconds(clip.fadeInFrames));
        if (clip.fadeOutFrames > 0)
            filter += QStringLiteral(",afade=t=out:st=%1:d=%2")
                .arg(seconds(clip.durationFrames - clip.fadeOutFrames),
                     seconds(clip.fadeOutFrames));
        const qint64 delaySamples = rescaleFrames(
            clip.startFrame, m_projectRate, FrameRate{48000, 1});
        filter += QStringLiteral(",adelay=%1S:all=1[a%2]")
            .arg(delaySamples).arg(i);
        filters << filter;
        inputs << QStringLiteral("[a%1]").arg(i);
    }
    if (inputs.size() == 1)
        filters << QStringLiteral("[base]anull[out]");
    else
        filters << inputs.join(QString{})
            + QStringLiteral("amix=inputs=%1:duration=first:dropout_transition=0:normalize=0,"
                             "alimiter=limit=0.95[out]").arg(inputs.size());
    args << QStringLiteral("-filter_complex") << filters.join(QLatin1Char(';'))
         << QStringLiteral("-map") << QStringLiteral("[out]")
         << QStringLiteral("-vn") << QStringLiteral("-t") << seconds(totalFrames)
         << QStringLiteral("-c:a");
    if (extension == QStringLiteral("wav")) args << QStringLiteral("pcm_s16le");
    else if (extension == QStringLiteral("flac")) args << QStringLiteral("flac");
    else if (extension == QStringLiteral("mp3")) args << QStringLiteral("libmp3lame");
    else args << QStringLiteral("aac");
    try {
        launchExport(args, output,
                     rescaleFrames(totalFrames, m_projectRate, FrameRate{1000000, 1}));
    } catch (const std::exception &error) {
        setStatus(QStringLiteral("Project audio export failed: %1")
            .arg(QString::fromUtf8(error.what())));
    }
}

void EditorController::cancelExport()
{
    if (!m_exportProcess) return;
    m_exportStage = QStringLiteral("Canceling");
    m_exportProcess->terminate();
    QPointer<QProcess> process = m_exportProcess;
    QTimer::singleShot(2000, this, [process] {
        if (process && process->state() != QProcess::NotRunning) process->kill();
    });
    emit exportChanged();
}

} // namespace datacut
