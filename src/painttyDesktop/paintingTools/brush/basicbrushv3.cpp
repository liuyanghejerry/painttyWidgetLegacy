#include "basicbrushv3.h"
#include <QPainter>
#include <QCursor>

BasicBrushV3::BasicBrushV3()
    : AbstractBrushV3()
{
    displayName_ = "基础笔刷 V3";
    initializeSubsystems();
}

BasicBrushV3::~BasicBrushV3()
{
    // 基类会自动清理子系统
}

QPixmap BasicBrushV3::cursor() const
{
    // 创建基于当前设置的圆形光标
    QPixmap cursor(50, 50);
    cursor.fill(Qt::transparent);
    QPainter painter(&cursor);
    painter.setRenderHint(QPainter::Antialiasing);

    // 绘制外圈
    painter.setPen(QPen(Qt::black, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(25 - width_/2, 25 - width_/2, width_, width_);

    // 绘制内圈（压感指示）
    painter.setPen(QPen(Qt::gray, 1));
    painter.drawEllipse(25 - width_/4, 25 - width_/4, width_/2, width_/2);

    return cursor;
}

void BasicBrushV3::drawPathToPainter(const QList<PressurePoint>& points, QPainter* painter)
{
    if (!painter || points.isEmpty()) return;

    // 使用轨迹系统处理点序列
    QList<PressurePoint> processedPoints = points;
    if (trail_) {
        processedPoints = trail_->processTrail(points);
    }

    // 沿着路径绘制印
    drawStampsAlongPath(processedPoints, painter);
}

Stamp* BasicBrushV3::createStamp()
{
    return new BasicStamp();
}

Trail* BasicBrushV3::createTrail()
{
    return new BasicTrail();
}

ColorSystem* BasicBrushV3::createColorSystem()
{
    return new BasicColorSystem();
}

void BasicBrushV3::drawStampsAlongPath(const QList<PressurePoint>& points, QPainter* painter)
{
    qDebug() << "Drawing stamps along path" << points.size();
    if (!painter || !stamp_ || !colorSystem_ || points.isEmpty()) return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    // 检查是否包含透明色，如果是则设置合适的合成模式
    bool hasTransparentColors = false;
    for (int i = 0; i < points.size(); ++i) {
        const PressurePoint& point = points[i];
        QColor color = colorSystem_->getProcessedColor(point.pressure, i, points.size());
        if (color.alphaF() < 1.0) {
            hasTransparentColors = true;
            break;
        }
    }

    // 如果包含透明色，设置Plus合成模式
    if (hasTransparentColors) {
        painter->setCompositionMode(QPainter::CompositionMode_Plus);
    }

    for (int i = 0; i < points.size(); ++i) {
        const PressurePoint& point = points[i];

        // 计算印的大小
        qreal stampSize = calculateStampSize(point);

        // 获取处理后的颜色
        QColor color = colorSystem_->getProcessedColor(point.pressure, i, points.size());

        // 绘制印（不保存/恢复painter状态，保持合成模式）
        stamp_->drawStamp(painter, point.pos, color, stampSize, point.pressure);
    }

    painter->restore();
}

qreal BasicBrushV3::calculateStampSize(const PressurePoint& point)
{
    // 基础大小
    qreal baseSize = width_;

    // 压感影响大小
    qreal pressureMultiplier = 0.5 + point.pressure * 0.5; // 0.5-1.0

    // 倾斜影响大小
    qreal tiltMultiplier = 1.0;
    if (qAbs(point.tiltX) > 0.1 || qAbs(point.tiltY) > 0.1) {
        qreal tiltAngle = qAtan2(point.tiltY, point.tiltX);
        qreal tiltIntensity = qSqrt(point.tiltX * point.tiltX + point.tiltY * point.tiltY);
        tiltMultiplier = 1.0 - tiltIntensity * 0.3; // 倾斜时稍微减小
    }

    return baseSize * pressureMultiplier * tiltMultiplier;
}
