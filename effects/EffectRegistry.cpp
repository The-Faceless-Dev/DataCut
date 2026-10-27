#include "effects/EffectRegistry.h"

#include <algorithm>

namespace datacut {

const QVector<EffectDefinition> &builtinEffects()
{
    static const QVector<EffectDefinition> effects{
        {QStringLiteral("datacut.video.grayscale"), QStringLiteral("Grayscale"),
         QStringLiteral("Color"), [](int strength) {
             return QStringLiteral("hue=s=%1")
                 .arg(QString::number((100 - strength) / 100.0, 'f', 3));
         }},
        {QStringLiteral("datacut.video.saturation"), QStringLiteral("Color Boost"),
         QStringLiteral("Color"), [](int strength) {
             return QStringLiteral("eq=saturation=%1")
                 .arg(QString::number(1.0 + strength / 50.0, 'f', 3));
         }},
        {QStringLiteral("datacut.video.brightness"), QStringLiteral("Brightness"),
         QStringLiteral("Color"), [](int strength) {
             return QStringLiteral("eq=brightness=%1")
                 .arg(QString::number(strength / 300.0, 'f', 3));
         }},
        {QStringLiteral("datacut.video.blur"), QStringLiteral("Gaussian Blur"),
         QStringLiteral("Blur"), [](int strength) {
             return QStringLiteral("gblur=sigma=%1")
                 .arg(QString::number(std::max(0.1, strength / 10.0), 'f', 2));
         }},
        {QStringLiteral("datacut.video.vignette"), QStringLiteral("Vignette"),
         QStringLiteral("Style"), [](int strength) {
             return QStringLiteral("vignette=angle=%1")
                 .arg(QString::number(strength / 200.0, 'f', 3));
         }},
        {QStringLiteral("datacut.video.contrast"), QStringLiteral("Contrast"),
         QStringLiteral("Color & Lighting"), {}, {{"amount", "Contrast", 0, 200, 130},
                                                    {"pivot", "Pivot", 0, 100, 50}}},
        {QStringLiteral("datacut.video.hue_shift"), QStringLiteral("Hue Shift"),
         QStringLiteral("Color & Lighting"), {}, {{"angle", "Angle", -180, 180, 45},
                                                    {"saturation", "Saturation", 0, 200, 100}}},
        {QStringLiteral("datacut.video.temperature"), QStringLiteral("Temperature & Tint"),
         QStringLiteral("Color & Lighting"), {}, {{"temperature", "Temperature", -100, 100, 30},
                                                    {"tint", "Tint", -100, 100, 0}}, QStringLiteral("T")},
        {QStringLiteral("datacut.video.gamma"), QStringLiteral("Gamma"),
         QStringLiteral("Color & Lighting"), {}, {{"gamma", "Gamma", 50, 300, 130},
                                                    {"weight", "Highlight weight", 0, 100, 100}}},
        {QStringLiteral("datacut.video.sepia"), QStringLiteral("Sepia"),
         QStringLiteral("Retro & Stylized"), {}, {{"amount", "Amount", 0, 100, 100},
                                                    {"warmth", "Warmth", -100, 100, 0}}, QStringLiteral("T")},
        {QStringLiteral("datacut.video.invert"), QStringLiteral("Color Invert"),
         QStringLiteral("Retro & Stylized"), {}, {{"amount", "Amount", 0, 100, 100}}, QStringLiteral("T")},
        {QStringLiteral("datacut.video.posterize"), QStringLiteral("Posterize"),
         QStringLiteral("Retro & Stylized"), {}, {{"levels", "Levels", 2, 32, 8},
                                                    {"amount", "Amount", 0, 100, 100}}, QStringLiteral("T")},
        {QStringLiteral("datacut.video.threshold"), QStringLiteral("Threshold / Black & White"),
         QStringLiteral("Retro & Stylized"), {}, {{"threshold", "Threshold", 0, 255, 128},
                                                    {"softness", "Softness", 0, 100, 0}}, QStringLiteral("T")},
        {QStringLiteral("datacut.video.pixelate"), QStringLiteral("Pixelate / Mosaic"),
         QStringLiteral("Blur & Sharpness"), {}, {{"size", "Cell size", 1, 64, 12},
                                                   {"amount", "Amount", 0, 100, 100}}, QStringLiteral("T")},
        {QStringLiteral("datacut.video.sharpen"), QStringLiteral("Sharpen"),
         QStringLiteral("Blur & Sharpness"), {}, {{"amount", "Amount", 0, 200, 100},
                                                   {"radius", "Radius", 1, 4, 1}}, QStringLiteral("T")},
        {QStringLiteral("datacut.video.chromatic_shift"), QStringLiteral("Chromatic Shift"),
         QStringLiteral("Optical"), {}, {{"offset", "Channel offset", 0, 30, 10},
                                          {"angle", "Direction", 0, 360, 0}}, QStringLiteral("T")},
        {QStringLiteral("datacut.video.ripple_glass"), QStringLiteral("Ripple Glass"),
         QStringLiteral("Distortion"), {}, {{"amplitude", "Refraction", 0, 30, 10},
                                             {"frequency", "Ripple density", 2, 24, 9}}, QStringLiteral("T")},
        {QStringLiteral("datacut.video.digital_tear"), QStringLiteral("Digital Tear"),
         QStringLiteral("Glitch"), {}, {{"amount", "Displacement", 0, 40, 14},
                                        {"bands", "Band count", 3, 40, 12}}, QStringLiteral("T")},
        {QStringLiteral("datacut.video.kaleidoscope"), QStringLiteral("Kaleidoscope"),
         QStringLiteral("Distortion"), {}, {{"segments", "Facets", 2, 16, 6},
                                             {"mix", "Mix", 0, 100, 100}}, QStringLiteral("T")},
        {QStringLiteral("datacut.video.halation"), QStringLiteral("Halation"),
         QStringLiteral("Optical"), {}, {{"radius", "Bloom radius", 1, 30, 12},
                                        {"amount", "Warm glow", 0, 100, 65}}, QStringLiteral("T")},
        {QStringLiteral("datacut.video.duotone"), QStringLiteral("Duotone"),
         QStringLiteral("Color & Lighting"), {}, {{"hue", "Shadow hue", 0, 360, 220},
                                                  {"amount", "Mix", 0, 100, 100}}, QStringLiteral("T")},
        {QStringLiteral("datacut.video.scanline_glow"), QStringLiteral("Scanline Glow"),
         QStringLiteral("Retro & Stylized"), {}, {{"spacing", "Line spacing", 2, 24, 6},
                                                  {"amount", "Glow", 0, 100, 55}}, QStringLiteral("T")},
        {QStringLiteral("datacut.video.solarize"), QStringLiteral("Solarize"),
         QStringLiteral("Color & Lighting"), {}, {{"threshold", "Threshold", 0, 255, 145},
                                                  {"amount", "Mix", 0, 100, 100}}, QStringLiteral("T")},
        {QStringLiteral("datacut.video.emboss_light"), QStringLiteral("Embossed Light"),
         QStringLiteral("Stylize"), {}, {{"depth", "Relief", 0, 100, 55},
                                         {"distance", "Light distance", 1, 12, 3}}, QStringLiteral("T")},
        {QStringLiteral("datacut.video.edge_neon"), QStringLiteral("Edge Neon"),
         QStringLiteral("Stylize"), {}, {{"threshold", "Edge threshold", 0, 100, 25},
                                         {"amount", "Glow", 0, 100, 80}}, QStringLiteral("T")}
    };
    return effects;
}

QString effectFilter(const QString &id, int strength)
{
    if (id.isEmpty() || strength <= 0) return {};
    const auto &effects = builtinEffects();
    const auto it = std::find_if(effects.cbegin(), effects.cend(),
        [&](const EffectDefinition &effect) { return effect.id == id; });
    if (it == effects.cend()) return {};
    if (it->ffmpegFilter) return it->ffmpegFilter(std::clamp(strength, 0, 100));
    QMap<QString, QString> defaults;
    for (const auto &parameter : it->parameters)
        defaults.insert(parameter.id, QString::number(parameter.initial));
    return parameterizedEffectFilter(id, defaults);
}

const EffectDefinition *videoEffectDefinition(const QString &id)
{
    const auto &effects = builtinEffects();
    const auto it = std::find_if(effects.cbegin(), effects.cend(),
        [&](const EffectDefinition &effect) { return effect.id == id; });
    return it == effects.cend() ? nullptr : &*it;
}

QString parameterizedEffectFilter(const QString &id, const QMap<QString, QString> &values)
{
    const auto value = [&](const QString &name) { return values.value(name, QStringLiteral("0")); };
    const auto channel = [](QChar letter) {
        return QString(letter) + QStringLiteral("(X\\,Y)");
    };
    const auto bounded = [](const QString &expression) {
        return QStringLiteral("clip((%1)\\,0\\,255)").arg(expression);
    };
    const auto rgb = [](const QString &red, const QString &green, const QString &blue) {
        return QStringLiteral("format=gbrp,geq=r='%1':g='%2':b='%3'")
            .arg(red, green, blue);
    };
    const QString red = channel(QLatin1Char('r'));
    const QString green = channel(QLatin1Char('g'));
    const QString blue = channel(QLatin1Char('b'));
    const QString suffix = id.section(QLatin1Char('.'), -1);
    // The advanced processors operate on straight RGB and preserve the alpha
    // plane. This is essential for titles and transparent image clips.
    const auto advancedRgb = [](const QString &r, const QString &g,
                                const QString &b, const QString &a = QStringLiteral("alpha(X\\,Y)")) {
        return QStringLiteral("format=gbrap,geq=r='%1':g='%2':b='%3':a='%4',format=rgba")
            .arg(r, g, b, a);
    };
    const auto sample = [](QChar c, const QString &x, const QString &y = QStringLiteral("Y")) {
        return QString(c) + QStringLiteral("(%1\\,%2)").arg(x, y);
    };
    if (suffix == QStringLiteral("chromatic_shift")) {
        const QString dx = QStringLiteral("(%1)*cos((%2)*PI/180)")
            .arg(value(QStringLiteral("offset")), value(QStringLiteral("angle")));
        const QString dy = QStringLiteral("(%1)*sin((%2)*PI/180)")
            .arg(value(QStringLiteral("offset")), value(QStringLiteral("angle")));
        return advancedRgb(sample('r', QStringLiteral("X-(%1)").arg(dx), QStringLiteral("Y-(%1)").arg(dy)),
                           green, sample('b', QStringLiteral("X+(%1)").arg(dx), QStringLiteral("Y+(%1)").arg(dy)));
    }
    if (suffix == QStringLiteral("ripple_glass")) {
        const QString dx = QStringLiteral("(%1)*sin(2*PI*(%2)*Y/H)")
            .arg(value(QStringLiteral("amplitude")), value(QStringLiteral("frequency")));
        const QString dy = QStringLiteral("(%1)*sin(2*PI*(%2)*X/W)/2")
            .arg(value(QStringLiteral("amplitude")), value(QStringLiteral("frequency")));
        const QString x = QStringLiteral("X+(%1)").arg(dx);
        const QString y = QStringLiteral("Y+(%1)").arg(dy);
        return advancedRgb(sample('r', x, y), sample('g', x, y),
                           sample('b', x, y), QStringLiteral("alpha(%1\\,%2)").arg(x, y));
    }
    if (suffix == QStringLiteral("digital_tear")) {
        const QString band = QStringLiteral("floor(Y*(%1)/H)").arg(value(QStringLiteral("bands")));
        const QString shift = QStringLiteral("(%1)*sin((%2)*78.233)*sin((%2)*12.9898)")
            .arg(value(QStringLiteral("amount")), band);
        const QString x = QStringLiteral("X+(%1)").arg(shift);
        return advancedRgb(sample('r', x), sample('g', x), sample('b', x),
                           QStringLiteral("alpha(%1\\,Y)").arg(x));
    }
    if (suffix == QStringLiteral("kaleidoscope")) {
        const QString facets = value(QStringLiteral("segments"));
        const QString reflected = QStringLiteral("abs(mod(X*(%1)/W\\,2)-1)*W").arg(facets);
        const QString x = QStringLiteral("X*(1-(%1)/100)+(%2)*(%1)/100")
            .arg(value(QStringLiteral("mix")), reflected);
        return advancedRgb(sample('r', x), sample('g', x), sample('b', x),
                           QStringLiteral("alpha(%1\\,Y)").arg(x));
    }
    if (suffix == QStringLiteral("halation")) {
        const QString radius = value(QStringLiteral("radius"));
        const QString bloom = QStringLiteral("max(0\\,(r(X-(%1)\\,Y)+r(X+(%1)\\,Y)"
            "+r(X\\,Y-(%1))+r(X\\,Y+(%1)))/4-135)").arg(radius);
        const QString amount = value(QStringLiteral("amount"));
        return advancedRgb(bounded(QStringLiteral("%1+(%2)*(%3)/100").arg(red, bloom, amount)),
            bounded(QStringLiteral("%1+(%2)*(%3)/220").arg(green, bloom, amount)), blue);
    }
    if (suffix == QStringLiteral("duotone")) {
        const QString lum = QStringLiteral("(0.299*%1+0.587*%2+0.114*%3)/255")
            .arg(red, green, blue);
        const QString angle = QStringLiteral("(%1)*PI/180").arg(value(QStringLiteral("hue")));
        const QString mix = value(QStringLiteral("amount"));
        const QString shadowR = QStringLiteral("(0.5+0.5*cos(%1))*110").arg(angle);
        const QString shadowG = QStringLiteral("(0.5+0.5*cos((%1)-2.094395))*110").arg(angle);
        const QString shadowB = QStringLiteral("(0.5+0.5*cos((%1)+2.094395))*110").arg(angle);
        const auto tone = [&](const QString &original, const QString &shadow) {
            return bounded(QStringLiteral("(%1)*(1-(%2)/100)+((%3)*(1-(%4))+255*(%4))*(%2)/100")
                .arg(original, mix, shadow, lum));
        };
        return advancedRgb(tone(red, shadowR), tone(green, shadowG), tone(blue, shadowB));
    }
    if (suffix == QStringLiteral("scanline_glow")) {
        const QString line = QStringLiteral("(0.7+0.3*cos(2*PI*Y/(%1)))")
            .arg(value(QStringLiteral("spacing")));
        const QString amount = value(QStringLiteral("amount"));
        const auto scan = [&](const QString &original) {
            return bounded(QStringLiteral("(%1)*((1-(%2)/100)+(%2)/100*(%3))")
                .arg(original, amount, line));
        };
        return advancedRgb(scan(red), scan(green), scan(blue));
    }
    if (suffix == QStringLiteral("solarize")) {
        const auto solar = [&](const QString &original) {
            return bounded(QStringLiteral("(%1)*(1-(%2)/100)+if(gt(%1\\,(%3))\\,255-%1\\,%1)*(%2)/100")
                .arg(original, value(QStringLiteral("amount")), value(QStringLiteral("threshold"))));
        };
        return advancedRgb(solar(red), solar(green), solar(blue));
    }
    if (suffix == QStringLiteral("emboss_light")) {
        const QString distance = value(QStringLiteral("distance"));
        const QString relief = QStringLiteral("(0.299*(r(X-(%1)\\,Y-(%1))-r(X+(%1)\\,Y+(%1)))"
            "+0.587*(g(X-(%1)\\,Y-(%1))-g(X+(%1)\\,Y+(%1)))"
            "+0.114*(b(X-(%1)\\,Y-(%1))-b(X+(%1)\\,Y+(%1))))")
            .arg(distance);
        const auto emboss = [&](const QString &original) {
            return bounded(QStringLiteral("(%1)*(1-(%2)/100)+(128+(%3))*(%2)/100")
                .arg(original, value(QStringLiteral("depth")), relief));
        };
        return advancedRgb(emboss(red), emboss(green), emboss(blue));
    }
    if (suffix == QStringLiteral("edge_neon")) {
        const QString edge = QStringLiteral("max(0\\,abs(0.299*(r(X+2\\,Y)-r(X-2\\,Y))"
            "+0.587*(g(X+2\\,Y)-g(X-2\\,Y))+0.114*(b(X+2\\,Y)-b(X-2\\,Y)))"
            "-(%1))").arg(value(QStringLiteral("threshold")));
        const QString amount = value(QStringLiteral("amount"));
        return advancedRgb(bounded(QStringLiteral("%1+(%2)*(%3)/250").arg(red, edge, amount)),
            bounded(QStringLiteral("%1+(%2)*(%3)/120").arg(green, edge, amount)),
            bounded(QStringLiteral("%1+(%2)*(%3)/80").arg(blue, edge, amount)));
    }
    if (suffix == QStringLiteral("contrast")) {
        const auto adjust = [&](const QString &source) {
            return bounded(QStringLiteral("((%1)-(%2)*2.55)*(%3)/100+(%2)*2.55")
                .arg(source, value(QStringLiteral("pivot")), value(QStringLiteral("amount"))));
        };
        return rgb(adjust(red), adjust(green), adjust(blue));
    }
    if (suffix == QStringLiteral("hue_shift"))
        return QStringLiteral("hue=h='%1':s='(%2)/100'")
            .arg(value(QStringLiteral("angle")), value(QStringLiteral("saturation")));
    if (suffix == QStringLiteral("temperature"))
        return rgb(
            bounded(QStringLiteral("%1+(%2)*0.6+(%3)*0.15")
                .arg(red, value(QStringLiteral("temperature")), value(QStringLiteral("tint")))),
            bounded(QStringLiteral("%1-(%2)*0.45")
                .arg(green, value(QStringLiteral("tint")))),
            bounded(QStringLiteral("%1-(%2)*0.6+(%3)*0.15")
                .arg(blue, value(QStringLiteral("temperature")), value(QStringLiteral("tint")))));
    if (suffix == QStringLiteral("gamma"))
        return QStringLiteral("eq=gamma='(%1)/100':gamma_weight='(%2)/100':eval=frame")
            .arg(value(QStringLiteral("gamma")), value(QStringLiteral("weight")));
    if (suffix == QStringLiteral("sepia")) {
        const auto sepia = [&](const QString &source, const QString &matrix, const QString &warmth) {
            return bounded(QStringLiteral("(%1)*(1-(%2)/100)+((%3)+(%4))*(%2)/100")
                .arg(source, value(QStringLiteral("amount")), matrix, warmth));
        };
        return rgb(
            sepia(red, QStringLiteral("0.393*%1+0.769*%2+0.189*%3").arg(red, green, blue),
                  QStringLiteral("(%1)*0.3").arg(value(QStringLiteral("warmth")))),
            sepia(green, QStringLiteral("0.349*%1+0.686*%2+0.168*%3").arg(red, green, blue),
                  QStringLiteral("0")),
            sepia(blue, QStringLiteral("0.272*%1+0.534*%2+0.131*%3").arg(red, green, blue),
                  QStringLiteral("-(%1)*0.3").arg(value(QStringLiteral("warmth")))));
    }
    if (suffix == QStringLiteral("invert")) {
        const auto invert = [&](const QString &source) {
            return bounded(QStringLiteral("%1*(1-(%2)/100)+(255-%1)*(%2)/100")
                .arg(source, value(QStringLiteral("amount"))));
        };
        return rgb(invert(red), invert(green), invert(blue));
    }
    if (suffix == QStringLiteral("posterize")) {
        const auto posterize = [&](const QString &source) {
            return bounded(QStringLiteral("%1*(1-(%2)/100)+floor(%1/255*((%3)-1)+0.5)*255/((%3)-1)*(%2)/100")
                .arg(source, value(QStringLiteral("amount")), value(QStringLiteral("levels"))));
        };
        return rgb(posterize(red), posterize(green), posterize(blue));
    }
    if (suffix == QStringLiteral("threshold")) {
        const QString luminance = QStringLiteral("0.299*%1+0.587*%2+0.114*%3")
            .arg(red, green, blue);
        const QString output = bounded(QStringLiteral("((%1)-(%2)+max(1\\,(%3))/2)*255/max(1\\,(%3))")
            .arg(luminance, value(QStringLiteral("threshold")), value(QStringLiteral("softness"))));
        return rgb(output, output, output);
    }
    if (suffix == QStringLiteral("pixelate")) {
        const auto pixelate = [&](QChar letter, const QString &source) {
            const QString sampled = QString(letter) + QStringLiteral("(floor(X/(%1))*(%1)\\,floor(Y/(%1))*(%1))")
                .arg(value(QStringLiteral("size")));
            return bounded(QStringLiteral("%1*(1-(%2)/100)+(%3)*(%2)/100")
                .arg(source, value(QStringLiteral("amount")), sampled));
        };
        return rgb(pixelate(QLatin1Char('r'), red), pixelate(QLatin1Char('g'), green),
                   pixelate(QLatin1Char('b'), blue));
    }
    if (suffix == QStringLiteral("sharpen")) {
        const auto sharpen = [&](QChar letter, const QString &source) {
            const QString sample = QString(letter);
            const QString radius = value(QStringLiteral("radius"));
            const QString neighbors = QStringLiteral("%1(X-(%2)\\,Y)+%1(X+(%2)\\,Y)+%1(X\\,Y-(%2))+%1(X\\,Y+(%2))")
                .arg(sample, radius);
            return bounded(QStringLiteral("%1+((%2)/100)*(%1*4-(%3))")
                .arg(source, value(QStringLiteral("amount")), neighbors));
        };
        return rgb(sharpen(QLatin1Char('r'), red), sharpen(QLatin1Char('g'), green),
                   sharpen(QLatin1Char('b'), blue));
    }
    return {};
}

bool effectExists(const QString &id)
{
    if (id.isEmpty()) return true;
    const auto &effects = builtinEffects();
    return std::any_of(effects.cbegin(), effects.cend(),
        [&](const EffectDefinition &effect) { return effect.id == id; });
}

const QVector<EffectDefinition> &builtinAudioEffects()
{
    static const QVector<EffectDefinition> effects{
        {QStringLiteral("datacut.audio.highpass"), QStringLiteral("High-pass"),
         QStringLiteral("Filter"), [](int strength) {
             return QStringLiteral("highpass=f=100:mix=%1")
                 .arg(QString::number(strength / 100.0, 'f', 3));
         }},
        {QStringLiteral("datacut.audio.lowpass"), QStringLiteral("Low-pass"),
         QStringLiteral("Filter"), [](int strength) {
             return QStringLiteral("lowpass=f=7000:mix=%1")
                 .arg(QString::number(strength / 100.0, 'f', 3));
         }},
        {QStringLiteral("datacut.audio.bass"), QStringLiteral("Bass Boost"),
         QStringLiteral("EQ"), [](int strength) {
             return QStringLiteral("bass=g=%1")
                 .arg(QString::number(strength / 10.0, 'f', 2));
         }},
        {QStringLiteral("datacut.audio.treble"), QStringLiteral("Treble Boost"),
         QStringLiteral("EQ"), [](int strength) {
             return QStringLiteral("treble=g=%1")
                 .arg(QString::number(strength / 10.0, 'f', 2));
         }}
    };
    return effects;
}

QString audioEffectFilter(const QString &id, int strength)
{
    if (id.isEmpty() || strength <= 0) return {};
    const auto &effects = builtinAudioEffects();
    const auto it = std::find_if(effects.cbegin(), effects.cend(),
        [&](const EffectDefinition &effect) { return effect.id == id; });
    return it == effects.cend() ? QString{} : it->ffmpegFilter(std::clamp(strength, 0, 100));
}

bool audioEffectExists(const QString &id)
{
    if (id.isEmpty()) return true;
    const auto &effects = builtinAudioEffects();
    return std::any_of(effects.cbegin(), effects.cend(),
        [&](const EffectDefinition &effect) { return effect.id == id; });
}

} // namespace datacut
