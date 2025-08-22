#ifndef CRYPTO_H
#define CRYPTO_H

#include <QByteArray>
#include <QDateTime>
#include <QCryptographicHash>

inline QString generateClientId(const QString& nickName)
{
    QString info =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODate) + nickName;
    QCryptographicHash hashedInfo(QCryptographicHash::Sha1);
    hashedInfo.addData(info.toUtf8());
    return hashedInfo.result().toHex();
}

#endif // CRYPTO_H