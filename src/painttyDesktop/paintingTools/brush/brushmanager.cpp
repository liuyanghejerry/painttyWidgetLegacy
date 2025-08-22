#include "brushmanager.h"
#include "abstractbrush.h"
#include "abstractbrushv3.h"
#include "basicbrush.h"
#include "basiceraser.h"
#include "binarybrush.h"
#include "sketchbrush.h"
#include "maskbased.h"

// 根据平台支持情况包含 SIMD 头文件
#ifdef PAINTTY_USE_SIMD
#include "basicbrushv3-simd.h"
#else
#include "basicbrushv3.h"
#endif
#include <QDebug>

BrushManager::BrushManager()
{
    
}

bool BrushManager::addBrush(BrushPointer brush)
{
    if (!brush) return false;
    registeredBrushes_[brush->name().toLower()] = brush;
    return true;
}

bool BrushManager::addBrushV3(BrushPointerV3 brush)
{
    if (!brush) return false;
    registeredBrushesV3_[brush->name().toLower()] = brush;
    return true;
}

QList<BrushPointer> BrushManager::allBrushes()
{
    return registeredBrushes_.values();
}

QList<BrushPointerV3> BrushManager::allBrushesV3()
{
    return registeredBrushesV3_.values();
}

BrushPointer BrushManager::getBrush(const QString &name)
{
    return registeredBrushes_.value(name.toLower());
}

BrushPointerV3 BrushManager::getBrushV3(const QString &name)
{
    return registeredBrushesV3_.value(name.toLower());
}

BrushPointer BrushManager::makeBrush(const QString &name)
{
    auto brush = getBrush(name);
    if (brush) {
        return brush;
    }

    return BrushPointer(new BasicBrush);
}

BrushPointerV3 BrushManager::makeBrushV3(const QString &name)
{
    QString brushName = name.toLower();
    
    if (brushName == "basicbrushv3") {
        return BrushPointerV3(new BasicBrushV3);
    } else if (brushName == "basicbrushv3simd" || brushName == "basicbrushv3-simd") {
        #ifdef PAINTTY_USE_SIMD
            return BrushPointerV3(new BasicBrushV3SIMD);
        #else
            qDebug() << "[BrushManager] SIMD 笔刷在 ARM64 平台上不可用，回退到基础 V3 笔刷";
            return BrushPointerV3(new BasicBrushV3);
        #endif
    }
    
    return BrushPointerV3();
}

bool BrushManager::isV3Brush(const QString &name) const
{
    return registeredBrushesV3_.contains(name.toLower());
}
