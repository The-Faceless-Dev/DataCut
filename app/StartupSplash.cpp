#include "app/StartupSplash.h"

#include <QFont>
#include <QPainter>
#include <QPixmap>

namespace datacut {

StartupSplash::StartupSplash(const QPixmap &artwork)
    : QSplashScreen(artwork), m_stage(QStringLiteral("Starting DataCut"))
{
    setWindowFlag(Qt::WindowStaysOnTopHint);
}

void StartupSplash::setStage(const QString &stage)
{
    if (m_stage == stage) return;
    m_stage = stage;
    update();
}

void StartupSplash::drawContents(QPainter *painter)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const int barHeight = 48;
    const QRect bar(0, height() - barHeight, width(), barHeight);
    painter->fillRect(bar, QColor(5, 15, 30, 218));
    painter->fillRect(QRect(0, bar.top(), width(), 1), QColor(40, 137, 245, 210));

    QFont statusFont(QStringLiteral("Segoe UI"));
    statusFont.setPixelSize(13);
    painter->setFont(statusFont);
    painter->setPen(QColor(QStringLiteral("#e8eff7")));
    painter->drawText(bar.adjusted(22, 0, -22, 0),
                      Qt::AlignVCenter | Qt::AlignLeft, m_stage);

    QFont versionFont(QStringLiteral("Segoe UI"));
    versionFont.setPixelSize(11);
    painter->setFont(versionFont);
    painter->setPen(QColor(QStringLiteral("#a0b3c7")));
    painter->drawText(bar.adjusted(22, 0, -22, 0),
                      Qt::AlignVCenter | Qt::AlignRight,
                      QStringLiteral("DataCut 0.7-beta.1"));
    painter->restore();
}

} // namespace datacut
