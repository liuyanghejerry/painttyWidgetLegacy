#ifndef BASICBRUSHV3_H
#define BASICBRUSHV3_H

#include "abstractbrushv3.h"
#include "basic-stamp.h"
#include "basic-trail.h"
#include "basic-color-system.h"

class BasicBrushV3 : public AbstractBrushV3
{
public:
    explicit BasicBrushV3();
    ~BasicBrushV3() override;
    
    QString name() const override { return "BasicBrushV3"; }
    QPixmap cursor() const override;

protected:
    void drawPathToPainter(const QList<PressurePoint>& points, QPainter* painter) override;
    
    // 创建子系统实例
    Stamp* createStamp() override;
    Trail* createTrail() override;
    ColorSystem* createColorSystem() override;

private:
    // 渲染辅助方法
    void drawStampsAlongPath(const QList<PressurePoint>& points, QPainter* painter);
    qreal calculateStampSize(const PressurePoint& point);
};

#endif // BASICBRUSHV3_H 