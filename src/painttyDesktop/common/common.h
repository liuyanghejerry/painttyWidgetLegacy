#ifndef COMMON_H
#define COMMON_H

#include <QtGlobal>
#include <QString>
#include <QTimer>
#include <QDir>
#include <QStandardPaths>

namespace GlobalDef
{

inline QString settingsPath()
{
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(directory);
    return QDir(directory).filePath("mrpaint.ini");
}

const qreal MAX_SCALE_FACTOR = 5.0;
const qreal MIN_SCALE_FACTOR = 0.125;

template<typename Func>
void delayJob(Func f, int ms=2000)
{
    QTimer* t = new QTimer;
    t->setSingleShot(true);
    QObject::connect(t, &QTimer::timeout,
            [&f, t](){
        f();
        t->deleteLater();
    });
    t->start(ms);
}

} // namespace GlobalDef

#endif // COMMON_H
