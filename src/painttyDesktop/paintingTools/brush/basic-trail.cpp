#include "basic-trail.h"
#include <QRandomGenerator>
#include <QtMath>
#include <QDebug> // Added for debugging output

BasicTrail::BasicTrail()
    : spacing_(0.5)
    , smoothing_(0.3)
    , randomPoints_(0.0)
    , colorDecay_(0.0)
    , taperLength_(0.0)
    , taperSharpness_(0.5)
    , taperFade_(0.0)
{
}

void BasicTrail::setSpacing(qreal spacing)
{
    spacing_ = qBound(0.01, spacing, 2.0);
}

qreal BasicTrail::spacing() const
{
    return spacing_;
}

void BasicTrail::setSmoothing(qreal smoothing)
{
    smoothing_ = qBound(0.0, smoothing, 1.0);
}

qreal BasicTrail::smoothing() const
{
    return smoothing_;
}

void BasicTrail::setRandomPoints(qreal intensity)
{
    randomPoints_ = qBound(0.0, intensity, 1.0);
}

qreal BasicTrail::randomPoints() const
{
    return randomPoints_;
}

void BasicTrail::setColorDecay(qreal decay)
{
    colorDecay_ = qBound(0.0, decay, 1.0);
}

qreal BasicTrail::colorDecay() const
{
    return colorDecay_;
}

void BasicTrail::setTaperLength(qreal length)
{
    taperLength_ = qBound(0.0, length, 1.0);
}

qreal BasicTrail::taperLength() const
{
    return taperLength_;
}

void BasicTrail::setTaperSharpness(qreal sharpness)
{
    taperSharpness_ = qBound(0.0, sharpness, 1.0);
}

qreal BasicTrail::taperSharpness() const
{
    return taperSharpness_;
}

void BasicTrail::setTaperFade(qreal fade)
{
    taperFade_ = qBound(0.0, fade, 1.0);
}

qreal BasicTrail::taperFade() const
{
    return taperFade_;
}

QList<PressurePoint> BasicTrail::processTrail(const QList<PressurePoint>& input)
{
    if (input.isEmpty()) return input;

    QList<PressurePoint> result = input;

    // 应用各种轨迹处理
    // TODO: 暂时用不上随机点，先注释掉简化实现
    // result = makePointsRandom(result);
    result = applySmoothing(result);
    result = applySpacing(result);
    // result = applyTapering(result);

    return result;
}

QList<PressurePoint> BasicTrail::applySpacing(const QList<PressurePoint>& points)
{
    if (points.size() < 2) return points;

    // 间距参数解释：
    // spacing_ = 0.01: 印记几乎重合（超密集）
    // spacing_ = 0.1: 较多重合（高密度）
    // spacing_ = 0.5: 中等间距（原0.1的效果）
    // spacing_ = 1.0: 较大间距
    // spacing_ = 2.0: 非常大的间距（印很稀疏）

    // 计算路径总长度
    qreal totalLength = 0.0;
    for (int i = 1; i < points.size(); ++i) {
        totalLength += QLineF(points[i-1].pos, points[i].pos).length();
    }

    // 根据间距参数计算印的密度
    qreal stampDensity;
    if (spacing_ <= 0.5) {
        // 将0.01-0.5区间映射到从几乎完全重合到中等间距
        qreal normalizedSpacing = (spacing_ - 0.01) / 0.49; // 0.01-0.5 映射到 0-1
        stampDensity = 20.0 * qPow(0.05, normalizedSpacing); // 使用指数衰减
    } else {
        // 正常范围内的密度计算
        stampDensity = 1.0 - (spacing_ - 0.5) / 1.5; // 0.5-2.0 映射到 1.0-0.0
        stampDensity = qMax(0.05, stampDensity); // 最小密度限制
    }


    // 计算目标印的数量
    int targetStamps = qMax(1, qRound(points.size() * stampDensity));

    // 如果原始点数>=2，确保目标点数也>=2，以保证起点和终点都被包括在内，并避免除零错误
    if (points.size() >= 2 && targetStamps < 2) {
        targetStamps = 2;
    }

    QList<PressurePoint> resampled;
    // 如果路径长度为0，无法进行插值，根据目标数量返回端点
    if (totalLength <= 0) {
        resampled.append(points.first());
        if (targetStamps > 1) {
            resampled.append(points.last());
        }
        return resampled;
    }

    resampled.append(points.first());

    if (targetStamps > 1) {
        qreal stepLength = totalLength / (targetStamps - 1);
        qreal accumulatedDistance = stepLength;
        qreal currentPathLength = 0.0;

        for (int i = 1; i < points.size(); ++i) {
            qreal segmentLength = QLineF(points[i-1].pos, points[i].pos).length();

            // 在当前线段上生成所有需要的点
            while (currentPathLength + segmentLength >= accumulatedDistance) {
                // -1 是为了给最后的终点留出位置
                if (resampled.size() >= targetStamps - 1) break;

                qreal ratio = (accumulatedDistance - currentPathLength) / segmentLength;
                ratio = qBound(0.0, ratio, 1.0); // 防止浮点误差

                PressurePoint interpolated;
                interpolated.pos = points[i-1].pos + (points[i].pos - points[i-1].pos) * ratio;
                interpolated.pressure = qBound(0.0, points[i-1].pressure + (points[i].pressure - points[i-1].pressure) * ratio, 1.0);
                interpolated.tiltX = points[i-1].tiltX + (points[i].tiltX - points[i-1].tiltX) * ratio;
                interpolated.tiltY = points[i-1].tiltY + (points[i].tiltY - points[i-1].tiltY) * ratio;
                interpolated.timestamp = points[i-1].timestamp + (points[i].timestamp - points[i-1].timestamp) * ratio;

                resampled.append(interpolated);
                accumulatedDistance += stepLength;
            }

            if (resampled.size() >= targetStamps - 1) break;
            currentPathLength += segmentLength;
        }
    }

    // 确保包含最后一个点，并且总数不超过目标值
    while (resampled.size() >= targetStamps) {
        resampled.removeLast();
    }
    resampled.prepend(points.first());
    resampled.append(points.last());

    return resampled;
}


QList<PressurePoint> BasicTrail::applySmoothing(const QList<PressurePoint>& points)
{
    if (points.size() < 3 || smoothing_ <= 0.0) return points;

    // 迭代次数与 smoothing_ 参数关联，以实现更强的平滑效果
    // smoothing_ = 0.1 -> 1次, smoothing_ = 1.0 -> 16次
    int iterations = qMax(1, int(smoothing_ * smoothing_ * 15 + 1));

    QList<PressurePoint> smoothedPoints = points;

    for (int iter = 0; iter < iterations; ++iter) {
        if (smoothedPoints.size() < 3) break;

        QList<PressurePoint> tempPoints;
        tempPoints.append(smoothedPoints.first()); // 保留第一个点

        for (int i = 1; i < smoothedPoints.size() - 1; ++i) {
            const PressurePoint& prev = smoothedPoints[i-1];
            const PressurePoint& curr = smoothedPoints[i];
            const PressurePoint& next = smoothedPoints[i+1];

            PressurePoint newPoint;

            // 使用更强的平滑权重，并进行多次迭代
            qreal weight = 0.25; // 每次迭代的平滑权重

            newPoint.pos = prev.pos * weight + curr.pos * (1.0 - 2 * weight) + next.pos * weight;

            newPoint.pressure = qBound(0.0,
                                       prev.pressure * weight + curr.pressure * (1.0 - 2 * weight) + next.pressure * weight,
                                       1.0);
            newPoint.tiltX = prev.tiltX * weight + curr.tiltX * (1.0 - 2 * weight) + next.tiltX * weight;
            newPoint.tiltY = prev.tiltY * weight + curr.tiltY * (1.0 - 2 * weight) + next.tiltY * weight;
            newPoint.timestamp = curr.timestamp; // 时间戳保持不变

            tempPoints.append(newPoint);
        }

        tempPoints.append(smoothedPoints.last()); // 保留最后一个点
        smoothedPoints = tempPoints;
    }

    return smoothedPoints;
}


QList<PressurePoint> BasicTrail::makePointsRandom(const QList<PressurePoint>& points)
{
    if (randomPoints_ <= 0.0) return points;

    QList<PressurePoint> result;
    result.reserve(points.size());

    // 根据输入点的位置和压力生成一个确定的种子，以确保随机性是可复现的
    quint32 seed = points.size();
    for (const auto& p : points) {
        seed = (seed * 31) + static_cast<quint32>(p.pos.x());
        seed = (seed * 31) + static_cast<quint32>(p.pos.y());
        seed = (seed * 31) + static_cast<quint32>(p.pressure * 1000);
    }
    QRandomGenerator rng(seed);

    for (const PressurePoint& p : points) {
        PressurePoint newPoint = p;

        // 最大偏移量受 randomPoints_ 和压力共同影响
        qreal maxOffset = 40.0 * randomPoints_; // 最大偏移半径
        qreal offsetDistance = rng.generateDouble() * maxOffset * p.pressure;
        qreal offsetAngle = rng.generateDouble() * 2.0 * M_PI;

        QPointF offset(offsetDistance * qCos(offsetAngle), offsetDistance * qSin(offsetAngle));

        newPoint.pos += offset;
        result.append(newPoint);
    }

    return result;
}

QList<PressurePoint> BasicTrail::applyTapering(const QList<PressurePoint>& points)
{
    if (points.size() < 2 || taperLength_ <= 0.0) return points;

    QList<PressurePoint> result = points;
    int totalPoints = points.size();
    int taperPoints = qMax(1, int(totalPoints * taperLength_));

    for (int i = 0; i < totalPoints; ++i) {
        PressurePoint& point = result[i];

        // 计算起笔和落笔的锥度
        qreal startTaper = calculateTaperMultiplier(i, taperPoints, true);
        qreal endTaper = calculateTaperMultiplier(totalPoints - 1 - i, taperPoints, false);

        // 应用锥度效果
        qreal taperMultiplier = qMin(startTaper, endTaper);
        // 修复：确保压感值在有效范围内
        point.pressure = qBound(0.0, point.pressure * taperMultiplier, 1.0);

        // 应用淡出效果
        if (taperFade_ > 0.0) {
            qreal fadeMultiplier = 1.0 - (taperFade_ * (1.0 - taperMultiplier));
            // 修复：确保压感值在有效范围内
            point.pressure = qBound(0.0, point.pressure * fadeMultiplier, 1.0);
        }
    }

    return result;
}

qreal BasicTrail::calculateTaperMultiplier(int index, int total, bool isStart)
{
    if (index >= total) return 1.0;

    qreal ratio = qreal(index) / total;
    qreal sharpness = taperSharpness_;

    if (isStart) {
        // 起笔锥度
        return qPow(ratio, sharpness);
    } else {
        // 落笔锥度
        return qPow(1.0 - ratio, sharpness);
    }
}