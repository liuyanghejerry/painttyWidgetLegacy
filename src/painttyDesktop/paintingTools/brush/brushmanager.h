#ifndef BRUSHMANAGER_H
#define BRUSHMANAGER_H

#include <QSharedPointer>
#include <QMap>
#include <QList>

class AbstractBrush;

typedef QSharedPointer<AbstractBrush> BrushPointer;

class BrushManager
{
public:
    BrushManager();
    ~BrushManager() = default;
    
    bool addBrush(BrushPointer brush);

    QList<BrushPointer> allBrushes();

    BrushPointer getBrush(const QString &name);

    BrushPointer makeBrush(const QString &name);

    
private:
    QMap<QString, BrushPointer> registeredBrushes_;
};

#endif // BRUSHMANAGER_H
