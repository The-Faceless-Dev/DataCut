#pragma once

#include <QString>
#include <QVector>
#include <QHash>

#include <functional>

namespace datacut {

struct TransitionParameterDefinition {
    QString id;
    QString name;
    int minimum = 0;
    int maximum = 100;
    int initial = 50;
};

// Renderers consume the identifier and parameter metadata. A future package
// loader can register definitions without changing timeline/project semantics.
struct TransitionDefinition {
    QString id;
    QString name;
    QString category;
    QString renderer;
    QVector<TransitionParameterDefinition> parameters;
};

// Processor input is timeline geometry only. The caller supplies visual
// sources, so the same renderer works for video, still images, and text.
struct TransitionFrameInput {
    qint64 frame = 0;
    qint64 start = 0;
    qint64 cut = 0;
    qint64 duration = 0;
    bool paired = false;
    bool startEdge = false;
    int blend = 50;
};

struct TransitionSample {
    double mainOpacity = 1.0;
    double incomingOpacity = 0.0;
    double outgoingOpacity = 0.0;
    double blackOpacity = 0.0;
};

using TransitionEvaluator = std::function<TransitionSample(const TransitionFrameInput &)>;

class TransitionRegistry {
public:
    TransitionRegistry();
    bool registerDefinition(TransitionDefinition definition);
    bool registerRenderer(const QString &id, TransitionEvaluator evaluator,
                          bool usesHeldEdges = false);
    [[nodiscard]] TransitionSample sample(const QString &renderer,
                                          const TransitionFrameInput &input) const;
    [[nodiscard]] bool supportsPreview(const QString &renderer) const;
    [[nodiscard]] bool usesHeldEdges(const QString &renderer) const;
    [[nodiscard]] const TransitionDefinition *find(const QString &id) const;
    [[nodiscard]] const QVector<TransitionDefinition> &definitions() const { return m_definitions; }

private:
    QVector<TransitionDefinition> m_definitions;
    struct Renderer {
        TransitionEvaluator evaluate;
        bool heldEdges = false;
    };
    QHash<QString, Renderer> m_renderers;
};

} // namespace datacut
