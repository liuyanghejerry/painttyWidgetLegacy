#include "basic-color-system.h"
#include <QRandomGenerator>
#include <QtMath>

BasicColorSystem::BasicColorSystem()
    : baseColor_(Qt::black)
    , colorVariation_(0.0)
    , opacity_(1.0)
{
}

void BasicColorSystem::setBaseColor(const QColor& color)
{
    baseColor_ = color;
}

QColor BasicColorSystem::baseColor() const
{
    return baseColor_;
}

void BasicColorSystem::setColorVariation(qreal variation)
{
    colorVariation_ = qBound(0.0, variation, 1.0);
}

qreal BasicColorSystem::colorVariation() const
{
    return colorVariation_;
}

void BasicColorSystem::setOpacity(qreal opacity)
{
    opacity_ = qBound(0.0, opacity, 1.0);
}

qreal BasicColorSystem::opacity() const
{
    return opacity_;
}

QColor BasicColorSystem::getProcessedColor(qreal pressure, int pointIndex, int totalPoints)
{
    QColor result = baseColor_;
    
    // 应用颜色变化
    if (colorVariation_ > 0.0) {
        result = applyColorVariation(result);
    }
    
    // 应用压感效果
    result = applyPressureEffect(result, pressure);
    
    // 应用透明度效果
    result = applyOpacityEffect(result);
    
    return result;
}

QColor BasicColorSystem::applyColorVariation(const QColor& color)
{
    if (colorVariation_ <= 0.0) return color;
    
    QRandomGenerator* rng = QRandomGenerator::global();
    
    // 随机调整色相、饱和度和亮度
    qreal hueVariation = ((rng->generate() / (double)RAND_MAX) - 0.5) * colorVariation_ * 30.0; // ±15度色相变化
    qreal saturationVariation = ((rng->generate() / (double)RAND_MAX) - 0.5) * colorVariation_ * 0.3; // ±15%饱和度变化
    qreal valueVariation = ((rng->generate() / (double)RAND_MAX) - 0.5) * colorVariation_ * 0.2; // ±10%亮度变化
    
    QColor hsvColor = color.toHsv();
    int hue = (hsvColor.hue() + int(hueVariation)) % 360;
    if (hue < 0) hue += 360;
    
    qreal saturation = qBound(0.0, hsvColor.saturationF() + saturationVariation, 1.0);
    qreal value = qBound(0.0, hsvColor.valueF() + valueVariation, 1.0);
    
    QColor result = QColor::fromHsv(hue, int(saturation * 255), int(value * 255));
    result.setAlpha(color.alpha());
    
    return result;
}

QColor BasicColorSystem::applyPressureEffect(const QColor& color, qreal pressure)
{
    if (pressure >= 1.0) return color;
    
    // 压感影响亮度和饱和度
    QColor hsvColor = color.toHsv();
    qreal saturation = hsvColor.saturationF() * (0.5 + pressure * 0.5);
    qreal value = hsvColor.valueF() * (0.7 + pressure * 0.3);
    
    QColor result = QColor::fromHsv(hsvColor.hue(), int(saturation * 255), int(value * 255));
    result.setAlpha(color.alpha());
    
    return result;
}

QColor BasicColorSystem::applyOpacityEffect(const QColor& color)
{
    if (opacity_ >= 1.0) return color;
    
    QColor result = color;
    result.setAlphaF(color.alphaF() * opacity_);
    return result;
} 