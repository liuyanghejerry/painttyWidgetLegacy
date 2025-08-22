#ifndef BRUSHMANAGER_H
#define BRUSHMANAGER_H

#include <QSharedPointer>
#include <QMap>
#include <QList>

class AbstractBrush;
class AbstractBrushV3;

typedef QSharedPointer<AbstractBrush> BrushPointer;
typedef QSharedPointer<AbstractBrushV3> BrushPointerV3;

class BrushManager
{
public:
    BrushManager();
    ~BrushManager() = default;
    
    bool addBrush(BrushPointer brush);
    bool addBrushV3(BrushPointerV3 brush);

    QList<BrushPointer> allBrushes();
    QList<BrushPointerV3> allBrushesV3();

    BrushPointer getBrush(const QString &name);
    BrushPointerV3 getBrushV3(const QString &name);

    BrushPointer makeBrush(const QString &name);
    BrushPointerV3 makeBrushV3(const QString &name);

    // 判断笔刷类型
    bool isV3Brush(const QString &name) const;
    
private:
    QMap<QString, BrushPointer> registeredBrushes_;
    QMap<QString, BrushPointerV3> registeredBrushesV3_;
};

#endif // BRUSHMANAGER_H
