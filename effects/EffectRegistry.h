#pragma once

#include <QString>
#include <QVector>
#include <QMap>

#include <functional>

namespace datacut {

struct EffectParameterDefinition {
    QString id;
    QString name;
    int minimum = 0;
    int maximum = 100;
    int initial = 0;
};

struct EffectDefinition {
    QString id;
    QString name;
    QString category;
    std::function<QString(int)> ffmpegFilter;
    QVector<EffectParameterDefinition> parameters;
    QString expressionTimeSymbol = QStringLiteral("t");
};

const QVector<EffectDefinition> &builtinEffects();
QString effectFilter(const QString &id, int strength);
bool effectExists(const QString &id);
const EffectDefinition *videoEffectDefinition(const QString &id);
QString parameterizedEffectFilter(const QString &id, const QMap<QString, QString> &values);
const QVector<EffectDefinition> &builtinAudioEffects();
QString audioEffectFilter(const QString &id, int strength);
bool audioEffectExists(const QString &id);

} // namespace datacut
