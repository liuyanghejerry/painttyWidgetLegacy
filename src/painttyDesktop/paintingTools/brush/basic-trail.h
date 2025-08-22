#ifndef BASIC_TRAIL_H
#define BASIC_TRAIL_H

#include "abstractbrushv3.h"
#include <QList>

class BasicTrail : public Trail
{
public:
    explicit BasicTrail();
    ~BasicTrail() override = default;
    
    // 间距控制
    void setSpacing(qreal spacing) override;
    qreal spacing() const override;
    
    // 线条修正（减缓曲线）
    void setSmoothing(qreal smoothing) override;
    qreal smoothing() const override;
    
    // 随机增点
    void setRandomPoints(qreal intensity) override;
    qreal randomPoints() const override;
    
    // 颜色衰减
    void setColorDecay(qreal decay) override;
    qreal colorDecay() const override;
    
    // 锥度控制
    void setTaperLength(qreal length) override;
    qreal taperLength() const override;
    void setTaperSharpness(qreal sharpness) override;
    qreal taperSharpness() const override;
    void setTaperFade(qreal fade) override;
    qreal taperFade() const override;
    
    // 轨迹处理
    QList<PressurePoint> processTrail(const QList<PressurePoint>& input) override;

private:
    qreal spacing_;
    qreal smoothing_;
    qreal randomPoints_;
    qreal colorDecay_;
    qreal taperLength_;
    qreal taperSharpness_;
    qreal taperFade_;
    
    // 辅助方法
    QList<PressurePoint> applySpacing(const QList<PressurePoint>& points);
    QList<PressurePoint> applySmoothing(const QList<PressurePoint>& points);
    QList<PressurePoint> makePointsRandom(const QList<PressurePoint>& points);
    QList<PressurePoint> applyTapering(const QList<PressurePoint>& points);
    qreal calculateTaperMultiplier(int index, int total, bool isStart);
};

#endif // BASIC_TRAIL_H 