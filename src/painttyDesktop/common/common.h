#ifndef COMMON_H
#define COMMON_H

#include <QtGlobal>
#include <QString>
#include <QTimer>

namespace GlobalDef
{

const static char CLIENT_TYPE[] = "beta";
const static int CLIENT_VER = 60;

const static char SETTINGS_NAME[] = "mrpaint.ini";

#ifndef PAINTTY_DEV
const static QString HOST_ADDR("http://115.29.229.23:8080");
#else
const static QString HOST_ADDR("http://127.0.0.1:8080");
#endif

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
