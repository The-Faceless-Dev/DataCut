#pragma once

#include <QSplashScreen>
#include <QString>

class QPainter;

namespace datacut {

class StartupSplash final : public QSplashScreen {
public:
    explicit StartupSplash(const QPixmap &artwork);
    void setStage(const QString &stage);

protected:
    void drawContents(QPainter *painter) override;

private:
    QString m_stage;
};

} // namespace datacut
