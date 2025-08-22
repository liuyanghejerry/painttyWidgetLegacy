#ifndef ABSTRACTBRUSHV3_H
#define ABSTRACTBRUSHV3_H

#include <QPointF>
#include <QColor>
#include <QPainter>
#include <QList>
#include <QVariantMap>
#include <QDateTime>
#include <QIcon>
#include <QKeySequence>
#include <QImage>
#include <QRandomGenerator>

// 压感点数据结构
struct PressurePoint {
    QPointF pos;
    qreal pressure;
    qreal tiltX;
    qreal tiltY;
    qint64 timestamp;

    PressurePoint() : pressure(0.5), tiltX(0), tiltY(0), timestamp(0) {}
    PressurePoint(const QPointF& p, qreal pr = 0.5, qreal tx = 0, qreal ty = 0)
        : pos(p), pressure(pr), tiltX(tx), tiltY(ty), timestamp(QDateTime::currentMSecsSinceEpoch()) {}
};

// 印系统 - 单个点的绘制
class Stamp {
public:
    virtual ~Stamp() = default;

    // 轮廓相关
    virtual void setOutline(const QString& outlineType) = 0; // "circle", "square", "star", etc.
    virtual QString outlineType() const = 0;
    virtual QPainterPath getOutlinePath(qreal size) const = 0;

    // 纹理相关
    virtual void setTexture(const QString& textureType) = 0; // "solid", "noise", "crayon", etc.
    virtual QString textureType() const = 0;
    virtual void applyTexture(QPainter* painter, const QColor& color, qreal size, const QPointF& pos) = 0;

    // 绘制单个印
    virtual void drawStamp(QPainter* painter, const QPointF& pos, const QColor& color, qreal size, qreal pressure = 1.0) = 0;
};

// 轨迹系统 - 印与印之间的关系
class Trail {
public:
    virtual ~Trail() = default;

    // 间距控制
    virtual void setSpacing(qreal spacing) = 0; // 0.1-2.0, 越小越连续
    virtual qreal spacing() const = 0;

    // 线条修正（减缓曲线）
    virtual void setSmoothing(qreal smoothing) = 0; // 0.0-1.0, 越大角度效果越弱
    virtual qreal smoothing() const = 0;

    // 随机增点
    virtual void setRandomPoints(qreal intensity) = 0; // 0.0-1.0, 随机点强度
    virtual qreal randomPoints() const = 0;

    // 颜色衰减
    virtual void setColorDecay(qreal decay) = 0; // 0.0-1.0, 颜色衰减程度
    virtual qreal colorDecay() const = 0;

    // 锥度控制
    virtual void setTaperLength(qreal length) = 0; // 0.0-1.0, 锥度长度
    virtual qreal taperLength() const = 0;
    virtual void setTaperSharpness(qreal sharpness) = 0; // 0.0-1.0, 锥度锐度
    virtual qreal taperSharpness() const = 0;
    virtual void setTaperFade(qreal fade) = 0; // 0.0-1.0, 淡出效果
    virtual qreal taperFade() const = 0;

    // 轨迹处理
    virtual QList<PressurePoint> processTrail(const QList<PressurePoint>& input) = 0;
};

// 颜色系统
class ColorSystem {
public:
    virtual ~ColorSystem() = default;

    // 基础颜色
    virtual void setBaseColor(const QColor& color) = 0;
    virtual QColor baseColor() const = 0;

    // 颜色变化
    virtual void setColorVariation(qreal variation) = 0; // 0.0-1.0, 颜色变化程度
    virtual qreal colorVariation() const = 0;

    // 透明度控制
    virtual void setOpacity(qreal opacity) = 0; // 0.0-1.0
    virtual qreal opacity() const = 0;

    // 获取处理后的颜色
    virtual QColor getProcessedColor(qreal pressure = 1.0, int pointIndex = 0, int totalPoints = 1) = 0;
};

class AbstractBrushV3
{
public:
    explicit AbstractBrushV3();
    virtual ~AbstractBrushV3();

    // 基础属性设置
    virtual void setWidth(int width);
    virtual void setColor(const QColor &color);
    virtual void setThickness(int thickness);

    // 路径管理接口
    virtual void addPointToCurrentPath(const PressurePoint& pt);
    virtual void clearCurrentPath();
    virtual void endStroke();
    virtual void clearAllPaths();
    virtual void clearHistoryPaths(); // 清空历史路径，保留当前路径

    // 渲染接口
    virtual void drawAllPaths(QPainter* painter);
    virtual void drawCurrentPath(QPainter* painter);

    // 设置管理
    virtual void setSettings(const QVariantMap &settings);
    virtual QVariantMap settings() const;
    virtual QVariantMap defaultSettings() const;

    // 笔刷信息
    virtual QString name() const = 0;
    virtual QString displayName() const;
    virtual QIcon icon() const;
    virtual QPixmap cursor() const;
    virtual QKeySequence shortcut() const;
    virtual void setShortcut(const QKeySequence &key);

    // 获取当前状态
    const QList<PressurePoint>& currentPath() const { return currentPath_; }
    const QList<QList<PressurePoint>>& allPaths() const { return paths_; }

    // 获取子系统
    Stamp* stamp() const { return stamp_; }
    Trail* trail() const { return trail_; }
    ColorSystem* colorSystem() const { return colorSystem_; }

    // 初始化子系统（子类构造函数中调用）
    void initializeSubsystems();

protected:
    // 子类需要实现的渲染方法
    virtual void drawPathToPainter(const QList<PressurePoint>& points, QPainter* painter) = 0;

    // 创建子系统实例（子类需要实现）
    virtual Stamp* createStamp() = 0;
    virtual Trail* createTrail() = 0;
    virtual ColorSystem* createColorSystem() = 0;

    // 通用属性
    QColor color_;
    int width_;
    int thickness_;
    QVariantMap settings_;

    // UI相关属性
    QString displayName_;
    QIcon icon_;
    QKeySequence shortcut_;

    // 路径数据
    QList<PressurePoint> currentPath_;
    QList<QList<PressurePoint>> paths_;

    // 三个核心子系统
    Stamp* stamp_;
    Trail* trail_;
    ColorSystem* colorSystem_;
};

#endif // ABSTRACTBRUSHV3_H
