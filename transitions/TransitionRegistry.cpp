#include "transitions/TransitionRegistry.h"

#include <algorithm>
#include <utility>

namespace datacut {

TransitionRegistry::TransitionRegistry()
{
    registerRenderer(QStringLiteral("cross_dissolve"), [](const TransitionFrameInput &input) {
        TransitionSample result;
        if (input.duration <= 0) return result;
        if (!input.paired) {
            const double progress = std::clamp((input.frame - input.start + 0.5)
                / double(input.duration), 0.0, 1.0);
            result.mainOpacity = input.startEdge ? progress : 1.0 - progress;
            result.blackOpacity = 1.0 - result.mainOpacity;
            return result;
        }
        const double blend = std::clamp(input.blend / 100.0, 0.0, 1.0);
        if (input.frame < input.cut) {
            const double weight = blend * (input.frame - input.start + 0.5)
                / std::max<qint64>(1, input.cut - input.start);
            result.incomingOpacity = std::clamp(weight, 0.0, 1.0);
            result.mainOpacity = 1.0 - result.incomingOpacity;
        } else {
            const double weight = (1.0 - blend) * (1.0 -
                (input.frame - input.cut + 0.5) /
                std::max<qint64>(1, input.start + input.duration - input.cut));
            result.outgoingOpacity = std::clamp(weight, 0.0, 1.0);
            result.mainOpacity = 1.0 - result.outgoingOpacity;
        }
        return result;
    }, true);
    registerRenderer(QStringLiteral("dip_black"), [](const TransitionFrameInput &input) {
        TransitionSample result;
        if (input.duration <= 0) return result;
        if (!input.paired) {
            const double progress = std::clamp((input.frame - input.start + 0.5)
                / double(input.duration), 0.0, 1.0);
            result.mainOpacity = input.startEdge ? progress : 1.0 - progress;
            result.blackOpacity = 1.0 - result.mainOpacity;
            return result;
        }
        const double weight = input.frame < input.cut
            ? 0.5 * (input.frame - input.start + 0.5)
                / std::max<qint64>(1, input.cut - input.start)
            : 0.5 * (1.0 - (input.frame - input.cut + 0.5)
                / std::max<qint64>(1, input.start + input.duration - input.cut));
        result.mainOpacity = 1.0 - std::clamp(weight, 0.0, 1.0);
        result.blackOpacity = std::clamp(2.0 * weight, 0.0, 1.0);
        return result;
    });
    const auto spatial = [](const TransitionFrameInput &input) {
        TransitionSample result;
        if (input.paired) {
            if (input.frame < input.cut) result.incomingOpacity = 1.0;
            else result.outgoingOpacity = 1.0;
        }
        return result;
    };
    const QVector<std::pair<QString, QString>> masks{
        {QStringLiteral("wipe_right"), QStringLiteral("Wipe Right")},
        {QStringLiteral("wipe_left"), QStringLiteral("Wipe Left")},
        {QStringLiteral("wipe_down"), QStringLiteral("Wipe Down")},
        {QStringLiteral("wipe_up"), QStringLiteral("Wipe Up")},
        {QStringLiteral("diagonal_wipe"), QStringLiteral("Diagonal Wipe")},
        {QStringLiteral("split_reveal"), QStringLiteral("Split Reveal")},
        {QStringLiteral("circle_iris"), QStringLiteral("Circle Iris")},
        {QStringLiteral("diamond_iris"), QStringLiteral("Diamond Iris")},
        {QStringLiteral("venetian_blinds"), QStringLiteral("Venetian Blinds")},
        {QStringLiteral("checker_reveal"), QStringLiteral("Checker Reveal")},
        {QStringLiteral("liquid_wave"), QStringLiteral("Liquid Wave")},
        {QStringLiteral("ripple_rings"), QStringLiteral("Ripple Rings")},
        {QStringLiteral("prism_sweep"), QStringLiteral("Prism Sweep")},
        {QStringLiteral("shattered_glass"), QStringLiteral("Shattered Glass")},
        {QStringLiteral("hex_mosaic"), QStringLiteral("Hex Mosaic")},
        {QStringLiteral("radial_burst"), QStringLiteral("Radial Burst")},
        {QStringLiteral("pixel_storm"), QStringLiteral("Pixel Storm")},
        {QStringLiteral("ribbon_twist"), QStringLiteral("Ribbon Twist")},
        {QStringLiteral("ink_bloom"), QStringLiteral("Ink Bloom")},
        {QStringLiteral("crosshatch"), QStringLiteral("Crosshatch")}};
    int maskIndex = 0;
    for (const auto &[renderer, name] : masks) {
        const bool procedural = maskIndex++ >= 10;
        registerRenderer(renderer, spatial, true);
        QVector<TransitionParameterDefinition> parameters{
            {QStringLiteral("speed"), QStringLiteral("Speed"), 25, 200, 100},
            {QStringLiteral("softness"), QStringLiteral("Edge softness"), 0, 30, 3}};
        if (renderer == QStringLiteral("venetian_blinds")
            || renderer == QStringLiteral("checker_reveal")
            || procedural)
            parameters.append({QStringLiteral("complexity"), QStringLiteral("Complexity"), 2, 16, 6});
        registerDefinition({QStringLiteral("datacut.transition.") + renderer,
            name, procedural ? QStringLiteral("Procedural")
                : renderer.contains(QStringLiteral("split"))
                    ? QStringLiteral("Split") : QStringLiteral("Mask"),
            renderer, parameters});
    }
    registerDefinition({QStringLiteral("datacut.transition.cross_dissolve"),
        QStringLiteral("Cross Dissolve"), QStringLiteral("Dissolve"),
        QStringLiteral("cross_dissolve"), {{QStringLiteral("balance"),
            QStringLiteral("Balance"), 20, 80, 50},
            {QStringLiteral("blend"), QStringLiteral("Blend at cut"), 20, 80, 50}}});
    registerDefinition({QStringLiteral("datacut.transition.dip_black"),
        QStringLiteral("Dip to Black"), QStringLiteral("Dissolve"),
        QStringLiteral("dip_black"), {{QStringLiteral("balance"),
            QStringLiteral("Balance"), 20, 80, 50}}});
}

bool TransitionRegistry::registerDefinition(TransitionDefinition definition)
{
    if (definition.id.isEmpty() || definition.name.isEmpty() || definition.renderer.isEmpty()
        || !supportsPreview(definition.renderer) || find(definition.id)) return false;
    m_definitions.append(std::move(definition));
    return true;
}

bool TransitionRegistry::registerRenderer(const QString &id, TransitionEvaluator evaluator,
                                          bool usesHeldEdges)
{
    if (id.isEmpty() || !evaluator || m_renderers.contains(id)) return false;
    m_renderers.insert(id, Renderer{std::move(evaluator), usesHeldEdges});
    return true;
}

TransitionSample TransitionRegistry::sample(const QString &renderer,
                                            const TransitionFrameInput &input) const
{
    const auto it = m_renderers.constFind(renderer);
    return it == m_renderers.cend() ? TransitionSample{} : it->evaluate(input);
}

bool TransitionRegistry::supportsPreview(const QString &renderer) const
{
    return m_renderers.contains(renderer);
}

bool TransitionRegistry::usesHeldEdges(const QString &renderer) const
{
    const auto it = m_renderers.constFind(renderer);
    return it != m_renderers.cend() && it->heldEdges;
}

const TransitionDefinition *TransitionRegistry::find(const QString &id) const
{
    const auto it = std::find_if(m_definitions.cbegin(), m_definitions.cend(),
        [&](const TransitionDefinition &definition) { return definition.id == id; });
    return it == m_definitions.cend() ? nullptr : &*it;
}

} // namespace datacut
