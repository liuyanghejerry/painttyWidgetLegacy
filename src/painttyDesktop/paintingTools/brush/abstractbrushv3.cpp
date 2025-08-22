#include "abstractbrushv3.h"
#include <QDateTime>
#include <QCursor>

AbstractBrushV3::AbstractBrushV3()
    : color_(Qt::black)
    , width_(10)
    , thickness_(50)
    , stamp_(nullptr)
    , trail_(nullptr)
    , colorSystem_(nullptr)
{
    // 注意：子类需要在构造函数中调用 initializeSubsystems()
}

AbstractBrushV3::~AbstractBrushV3()
{
    delete stamp_;
    delete trail_;
    delete colorSystem_;
}

void AbstractBrushV3::initializeSubsystems()
{
    // 创建子系统实例
    stamp_ = createStamp();
    trail_ = createTrail();
    colorSystem_ = createColorSystem();

    // 初始化默认设置
    if (colorSystem_) {
        colorSystem_->setBaseColor(color_);
    }
}

void AbstractBrushV3::setWidth(int width)
{
    width_ = width;
    settings_["width"] = width;
}

void AbstractBrushV3::setColor(const QColor &color)
{
    color_ = color;
    settings_["color"] = color;

    if (colorSystem_) {
        colorSystem_->setBaseColor(color);
    }
}

void AbstractBrushV3::setThickness(int thickness)
{
    thickness_ = thickness;
    settings_["thickness"] = thickness;
}

void AbstractBrushV3::addPointToCurrentPath(const PressurePoint& pt)
{
    currentPath_.append(pt);
}

void AbstractBrushV3::clearCurrentPath()
{
    currentPath_.clear();
}

void AbstractBrushV3::endStroke()
{
    if (!currentPath_.isEmpty()) {
        paths_.append(currentPath_);
        currentPath_.clear();
    }
}

void AbstractBrushV3::clearAllPaths()
{
    paths_.clear();
    currentPath_.clear();
}

void AbstractBrushV3::clearHistoryPaths()
{
    paths_.clear();
    // 注意：不清空 currentPath_，保留当前正在绘制的路径
}

void AbstractBrushV3::drawAllPaths(QPainter* painter)
{
    if (!painter) return;

    for (const auto& path : paths_) {
        drawPathToPainter(path, painter);
    }
}

void AbstractBrushV3::drawCurrentPath(QPainter* painter)
{
    if (!currentPath_.isEmpty() && painter) {
        drawPathToPainter(currentPath_, painter);
    }
}

void AbstractBrushV3::setSettings(const QVariantMap &settings)
{
    qDebug() << "AbstractBrushV3::setSettings" << settings;
    settings_ = settings;

    if (settings.contains("color")) {
        setColor(settings["color"].value<QColor>());
    }
    if (settings.contains("width")) {
        setWidth(settings["width"].toInt());
    }
    if (settings.contains("thickness")) {
        setThickness(settings["thickness"].toInt());
    }

    // 设置子系统参数
    if (stamp_) {
        if (settings.contains("outline")) {
            stamp_->setOutline(settings["outline"].toString());
        }
        if (settings.contains("texture")) {
            stamp_->setTexture(settings["texture"].toString());
        }
    }

    if (trail_) {
        if (settings.contains("spacing")) {
            trail_->setSpacing(settings["spacing"].toReal());
        }
        if (settings.contains("smoothing")) {
            trail_->setSmoothing(settings["smoothing"].toReal());
        }
        if (settings.contains("randomPoints")) {
            trail_->setRandomPoints(settings["randomPoints"].toReal());
        }
        if (settings.contains("colorDecay")) {
            trail_->setColorDecay(settings["colorDecay"].toReal());
        }
        if (settings.contains("taperLength")) {
            trail_->setTaperLength(settings["taperLength"].toReal());
        }
        if (settings.contains("taperSharpness")) {
            trail_->setTaperSharpness(settings["taperSharpness"].toReal());
        }
        if (settings.contains("taperFade")) {
            trail_->setTaperFade(settings["taperFade"].toReal());
        }
    }

    if (colorSystem_) {
        if (settings.contains("colorVariation")) {
            colorSystem_->setColorVariation(settings["colorVariation"].toReal());
        }
        if (settings.contains("opacity")) {
            colorSystem_->setOpacity(settings["opacity"].toReal());
        }
    }
}

QVariantMap AbstractBrushV3::settings() const
{
    QVariantMap result = settings_;

    // 添加子系统设置
    if (stamp_) {
        result["outline"] = stamp_->outlineType();
        result["texture"] = stamp_->textureType();
    }

    if (trail_) {
        result["spacing"] = trail_->spacing();
        result["smoothing"] = trail_->smoothing();
        result["randomPoints"] = trail_->randomPoints();
        result["colorDecay"] = trail_->colorDecay();
        result["taperLength"] = trail_->taperLength();
        result["taperSharpness"] = trail_->taperSharpness();
        result["taperFade"] = trail_->taperFade();
    }

    if (colorSystem_) {
        result["colorVariation"] = colorSystem_->colorVariation();
        result["opacity"] = colorSystem_->opacity();
    }

    return result;
}

QVariantMap AbstractBrushV3::defaultSettings() const
{
    QVariantMap settings;
    settings["color"] = QColor(Qt::black);
    settings["width"] = 10;
    settings["thickness"] = 50;

    // 默认子系统设置
    settings["outline"] = "circle";
    settings["texture"] = "solid";
    settings["spacing"] = 0.5;
    settings["smoothing"] = 0.3;
    settings["randomPoints"] = 0.0;
    settings["colorDecay"] = 0.0;
    settings["taperLength"] = 0.0;
    settings["taperSharpness"] = 0.5;
    settings["taperFade"] = 0.0;
    settings["colorVariation"] = 0.0;
    settings["opacity"] = 1.0;

    return settings;
}

QString AbstractBrushV3::displayName() const
{
    return displayName_.isEmpty() ? name() : displayName_;
}

QIcon AbstractBrushV3::icon() const
{
    return icon_;
}

QPixmap AbstractBrushV3::cursor() const
{
    // 默认圆形光标
    QPixmap cursor(32, 32);
    cursor.fill(Qt::transparent);
    QPainter painter(&cursor);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(Qt::black, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(16 - width_/2, 16 - width_/2, width_, width_);
    return cursor;
}

QKeySequence AbstractBrushV3::shortcut() const
{
    return shortcut_;
}

void AbstractBrushV3::setShortcut(const QKeySequence &key)
{
    shortcut_ = key;
}
