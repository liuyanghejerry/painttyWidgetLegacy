#include "brushmanager.h"
#include "abstractbrush.h"
#include "basicbrush.h"
#include "basiceraser.h"
#include "binarybrush.h"
#include "sketchbrush.h"
#include "maskbased.h"

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

QList<BrushPointer> BrushManager::allBrushes()
{
    return registeredBrushes_.values();
}

BrushPointer BrushManager::getBrush(const QString &name)
{
    return registeredBrushes_.value(name.toLower());
}

BrushPointer BrushManager::makeBrush(const QString &name)
{
    auto brush = getBrush(name);
    if (brush) {
        return BrushPointer(brush->createBrush());
    }

    return BrushPointer(new BasicBrush);
}
