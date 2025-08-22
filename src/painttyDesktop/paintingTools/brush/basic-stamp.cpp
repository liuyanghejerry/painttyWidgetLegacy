#include "basic-stamp.h"
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>
#include <QtMath>
#include <QDebug> // Added for qDebug

BasicStamp::BasicStamp()
    : outlineType_("circle")
    , textureType_("noise")
{
}

void BasicStamp::setOutline(const QString& outlineType)
{
    outlineType_ = outlineType;
}

QString BasicStamp::outlineType() const
{
    return outlineType_;
}

QPainterPath BasicStamp::getOutlinePath(qreal size) const
{
    QPainterPath path;
    
    if (outlineType_ == "circle") {
        path.addEllipse(QPointF(0, 0), size/2, size/2);
    } else if (outlineType_ == "square") {
        path.addRect(-size/2, -size/2, size, size);
    } else if (outlineType_ == "star") {
        // 创建五角星路径
        const int points = 5;
        const qreal outerRadius = size/2;
        const qreal innerRadius = outerRadius * 0.4;
        
        for (int i = 0; i < points * 2; ++i) {
            qreal angle = i * M_PI / points;
            qreal radius = (i % 2 == 0) ? outerRadius : innerRadius;
            QPointF point(radius * qCos(angle), radius * qSin(angle));
            
            if (i == 0) {
                path.moveTo(point);
            } else {
                path.lineTo(point);
            }
        }
        path.closeSubpath();
    }
    
    return path;
}

void BasicStamp::setTexture(const QString& textureType)
{
    textureType_ = textureType;
}

QString BasicStamp::textureType() const
{
    return textureType_;
}

void BasicStamp::applyTexture(QPainter* painter, const QColor& color, qreal size, const QPointF& pos)
{
    if (!painter) return;
    
    if (textureType_ == "solid") {
        applySolidTexture(painter, color, size, pos);
    } else if (textureType_ == "noise") {
        applyNoiseTexture(painter, color, size, pos);
    } else if (textureType_ == "crayon") {
        applyCrayonTexture(painter, color, size, pos);
    }
}

void BasicStamp::drawStamp(QPainter* painter, const QPointF& pos, const QColor& color, qreal size, qreal pressure)
{
    if (!painter) return;
    
    // size 参数已经考虑了压感，不需要再次调整
    if (outlineType_ == "circle") {
        drawCircleStamp(painter, pos, color, size, pressure);
    } else if (outlineType_ == "square") {
        drawSquareStamp(painter, pos, color, size, pressure);
    } else if (outlineType_ == "star") {
        drawStarStamp(painter, pos, color, size, pressure);
    }
}

void BasicStamp::drawCircleStamp(QPainter* painter, const QPointF& pos, const QColor& color, qreal size, qreal pressure)
{
    painter->setRenderHint(QPainter::Antialiasing);
    
    // 设置颜色和透明度
    QColor adjustedColor = color;
    // 修复：确保压感值在有效范围内，避免传递给 setAlphaF 负值
    qreal validPressure = qBound(0.0, pressure, 1.0);
    adjustedColor.setAlphaF(qBound(0.0, color.alphaF() * validPressure, 1.0));
    painter->setPen(Qt::NoPen);
    painter->setBrush(adjustedColor);
    
    // 绘制主轮廓
    painter->drawEllipse(pos, size/2, size/2);
    
    // 应用纹理（在主印记之上绘制）
    applyTexture(painter, adjustedColor, size, pos);
}

void BasicStamp::drawSquareStamp(QPainter* painter, const QPointF& pos, const QColor& color, qreal size, qreal pressure)
{
    painter->setRenderHint(QPainter::Antialiasing);
    
    // 设置颜色和透明度
    QColor adjustedColor = color;
    // 修复：确保压感值在有效范围内，避免传递给 setAlphaF 负值
    qreal validPressure = qBound(0.0, pressure, 1.0);
    adjustedColor.setAlphaF(qBound(0.0, color.alphaF() * validPressure, 1.0));
    painter->setPen(Qt::NoPen);
    painter->setBrush(adjustedColor);
    
    // 绘制主轮廓
    painter->drawRect(QRectF(pos.x() - size/2, pos.y() - size/2, size, size));
    
    // 应用纹理（在主印记之上绘制）
    applyTexture(painter, adjustedColor, size, pos);
}

void BasicStamp::drawStarStamp(QPainter* painter, const QPointF& pos, const QColor& color, qreal size, qreal pressure)
{
    painter->setRenderHint(QPainter::Antialiasing);
    
    // 设置颜色和透明度
    QColor adjustedColor = color;
    // 修复：确保压感值在有效范围内，避免传递给 setAlphaF 负值
    qreal validPressure = qBound(0.0, pressure, 1.0);
    adjustedColor.setAlphaF(qBound(0.0, color.alphaF() * validPressure, 1.0));
    painter->setPen(Qt::NoPen);
    painter->setBrush(adjustedColor);
    
    // 绘制主轮廓
    QPainterPath path = getOutlinePath(size);
    path.translate(pos);
    painter->drawPath(path);
    
    // 应用纹理（在主印记之上绘制）
    applyTexture(painter, adjustedColor, size, pos);
}

void BasicStamp::applySolidTexture(QPainter* painter, const QColor& color, qreal size, const QPointF& pos)
{
    // 纯色填充，无需额外处理
    Q_UNUSED(painter)
    Q_UNUSED(color)
    Q_UNUSED(size)
    Q_UNUSED(pos)
}

void BasicStamp::applyNoiseTexture(QPainter* painter, const QColor& color, qreal size, const QPointF& pos)
{
    // 添加噪声纹理
    QRandomGenerator* rng = QRandomGenerator::global();
    
    // 减少噪点数量，使效果更自然
    int noisePoints = qMax(1, int(size * 0.15)); // 至少1个噪点，最多为印记大小的15%
    
    // 保存当前的合成模式
    QPainter::CompositionMode originalMode = painter->compositionMode();
    
    // 使用SourceOver模式确保噪点可见
    painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
    
    for (int i = 0; i < noisePoints; ++i) {
        // 噪点位置：在印记范围内随机分布（相对于印记中心）
        qreal relativeX = (rng->generate() / (double)RAND_MAX - 0.5) * size * 0.6; // 限制在印记的60%范围内
        qreal relativeY = (rng->generate() / (double)RAND_MAX - 0.5) * size * 0.6;
        
        // 转换为绝对坐标
        qreal absoluteX = pos.x() + relativeX;
        qreal absoluteY = pos.y() + relativeY;
        
        // 减小噪点尺寸，使效果更微妙
        qreal noiseSize = (rng->generate() / (double)RAND_MAX) * size * 0.15; // 最大为印记大小的15%
        noiseSize = qMax(noiseSize, size * 0.03); // 最小为印记大小的3%
        
        // 创建更微妙的噪点颜色
        QColor noiseColor;
        if (color.lightness() > 128) {
            // 如果原色较亮，使用较暗的噪点
            noiseColor = QColor(color.red() * 0.8, color.green() * 0.8, color.blue() * 0.8, 120); // 半透明暗色
        } else {
            // 如果原色较暗，使用较亮的噪点
            noiseColor = QColor(qMin(255, int(color.red() * 1.2)), 
                               qMin(255, int(color.green() * 1.2)), 
                               qMin(255, int(color.blue() * 1.2)), 120); // 半透明亮色
        }
        
        painter->setBrush(noiseColor);
        painter->drawEllipse(QPointF(absoluteX, absoluteY), noiseSize, noiseSize);
    }
    
    // 恢复原始合成模式
    painter->setCompositionMode(originalMode);
}

void BasicStamp::applyCrayonTexture(QPainter* painter, const QColor& color, qreal size, const QPointF& pos)
{
    // 蜡笔纹理 - 留白效果
    QRandomGenerator* rng = QRandomGenerator::global();
    
    // 增加留白数量，使效果更明显
    int gaps = qMax(2, int(size * 0.15)); // 至少2个留白，最多为印记大小的15%
    
    for (int i = 0; i < gaps; ++i) {
        // 留白位置：在印记范围内随机分布（相对于印记中心）
        qreal relativeX = (rng->generate() / (double)RAND_MAX - 0.5) * size * 0.7; // 限制在印记的70%范围内
        qreal relativeY = (rng->generate() / (double)RAND_MAX - 0.5) * size * 0.7;
        
        // 转换为绝对坐标
        qreal absoluteX = pos.x() + relativeX;
        qreal absoluteY = pos.y() + relativeY;
        
        // 调整留白大小，使其更明显
        qreal gapSize = (rng->generate() / (double)RAND_MAX) * size * 0.3; // 最大为印记大小的30%
        gapSize = qMax(gapSize, size * 0.08); // 最小为印记大小的8%
        
        painter->setBrush(Qt::transparent);
        painter->drawEllipse(QPointF(absoluteX, absoluteY), gapSize, gapSize);
    }
}