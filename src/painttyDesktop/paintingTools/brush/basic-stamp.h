#ifndef BASIC_STAMP_H
#define BASIC_STAMP_H

#include "abstractbrushv3.h"
#include <QPainterPath>

class BasicStamp : public Stamp
{
public:
    explicit BasicStamp();
    ~BasicStamp() override = default;
    
    // 轮廓相关
    void setOutline(const QString& outlineType) override;
    QString outlineType() const override;
    QPainterPath getOutlinePath(qreal size) const override;
    
    // 纹理相关
    void setTexture(const QString& textureType) override;
    QString textureType() const override;
    void applyTexture(QPainter* painter, const QColor& color, qreal size, const QPointF& pos) override;
    
    // 绘制单个印
    void drawStamp(QPainter* painter, const QPointF& pos, const QColor& color, qreal size, qreal pressure = 1.0) override;

private:
    QString outlineType_;
    QString textureType_;
    
    // 辅助方法
    void drawCircleStamp(QPainter* painter, const QPointF& pos, const QColor& color, qreal size, qreal pressure);
    void drawSquareStamp(QPainter* painter, const QPointF& pos, const QColor& color, qreal size, qreal pressure);
    void drawStarStamp(QPainter* painter, const QPointF& pos, const QColor& color, qreal size, qreal pressure);
    
    void applySolidTexture(QPainter* painter, const QColor& color, qreal size, const QPointF& pos);
    void applyNoiseTexture(QPainter* painter, const QColor& color, qreal size, const QPointF& pos);
    void applyCrayonTexture(QPainter* painter, const QColor& color, qreal size, const QPointF& pos);
};

#endif // BASIC_STAMP_H 