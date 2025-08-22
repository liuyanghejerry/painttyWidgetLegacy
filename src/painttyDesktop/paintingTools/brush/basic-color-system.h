#ifndef BASIC_COLOR_SYSTEM_H
#define BASIC_COLOR_SYSTEM_H

#include "abstractbrushv3.h"
#include <QColor>

class BasicColorSystem : public ColorSystem
{
public:
    explicit BasicColorSystem();
    ~BasicColorSystem() override = default;
    
    // 基础颜色
    void setBaseColor(const QColor& color) override;
    QColor baseColor() const override;
    
    // 颜色变化
    void setColorVariation(qreal variation) override;
    qreal colorVariation() const override;
    
    // 透明度控制
    void setOpacity(qreal opacity) override;
    qreal opacity() const override;
    
    // 获取处理后的颜色
    QColor getProcessedColor(qreal pressure = 1.0, int pointIndex = 0, int totalPoints = 1) override;

private:
    QColor baseColor_;
    qreal colorVariation_;
    qreal opacity_;
    
    // 辅助方法
    QColor applyColorVariation(const QColor& color);
    QColor applyPressureEffect(const QColor& color, qreal pressure);
    QColor applyOpacityEffect(const QColor& color);
};

#endif // BASIC_COLOR_SYSTEM_H 