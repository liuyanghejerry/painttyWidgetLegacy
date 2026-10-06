#include <QPainter>
#include <QHash>
#include <QSharedPointer>
#include <QStyleOption>
#include <QMouseEvent>
#include <QSettings>
#include <QApplication>
#include <QTimer>
#include <QtCore/qmath.h>

#include "../common/common.h"
#include "../paintingTools/brush/brushmanager.h"
#include "../paintingTools/brush/basicbrush.h"
#include "../paintingTools/brush/sketchbrush.h"
#include "../paintingTools/brush/basiceraser.h"
#include "../paintingTools/brush/binarybrush.h"
#include "../paintingTools/brush/maskbased.h"
#include "../misc/platformextend.h"
#include "../misc/singleton.h"

#include "canvas.h"

#define brush_manager (Singleton<BrushManager>::instance())

Canvas::Canvas(QWidget *parent) :
    QWidget(parent),
    control_mode_(UNKNOWN),
    canvasSize_(QSize(720, 480)),
    layers(canvasSize_),
    image(canvasSize_, QImage::Format_ARGB32_Premultiplied),
    layerNameCounter(0),
    shareColor_(true)
{
    brush_ = BrushPointer(new BasicBrush);
    brush_->setSettings(brush_->defaultSettings());
    updateCursor();

    setMouseTracking(true);
    setFocusPolicy(Qt::WheelFocus);
    setFixedSize(canvasSize_);

    BrushPointer p1(new BasicBrush);
    p1->setSettings(p1->defaultSettings());
    BrushPointer p2(new BinaryBrush);
    p2->setSettings(p1->defaultSettings());
    BrushPointer p3(new SketchBrush);
    p3->setSettings(p1->defaultSettings());
    BrushPointer p4(new BasicEraser);
    p4->setSettings(p1->defaultSettings());
    BrushPointer p5(new MaskBased);
    p5->setSettings(p1->defaultSettings());

    brush_manager.addBrush(p1);
    brush_manager.addBrush(p2);
    brush_manager.addBrush(p3);
    brush_manager.addBrush(p4);
    brush_manager.addBrush(p5);

}

Canvas::~Canvas() = default;

QImage Canvas::currentCanvas()
{
    QImage pmp = image;
    layers.combineLayers(&pmp);
    return pmp;
}

QImage Canvas::allCanvas()
{
    QImage exp(canvasSize_, QImage::Format_ARGB32_Premultiplied);
    exp.fill(Qt::white);
    QPainter painter(&exp);
    int count = layers.count();
    for(int i=0;i<count;++i){
        LayerPointer l = layers.layerFrom(i);
        if (!l->isTouched()) continue;
        QImage * im = l->imagePtr();
        painter.drawImage(0, 0, *im);
    }
    return exp;
}

QVariantMap Canvas::brushSettings() const
{
    auto m = brush_->settings();
    m.insert("name", brush_->name().toLower());
    return m;
}

BrushFeature Canvas::brushFeatures() const
{
    return brush_->features();
}

void Canvas::setShareColor(bool b)
{
    shareColor_ = b;
}

void Canvas::setBrushColor(const QColor &newColor)
{
    brush_->setColor(newColor);
}

void Canvas::setBrushWidth(int newWidth)
{
    setBrushFeature("width", newWidth);
    updateCursor();
}

void Canvas::setBrushHardness(int h)
{
    setBrushFeature("hardness", h);
}

void Canvas::setBrushThickness(int t)
{
    setBrushFeature("thickness", t);
}

void Canvas::setBrushWater(int w)
{
    setBrushFeature("water", w);
}

void Canvas::setBrushExtend(int e)
{
    setBrushFeature("extend", e);
}

void Canvas::setBrushMixin(int e)
{
    setBrushFeature("mixin", e);
}

void Canvas::setBrushSettings(const QVariantMap &settings)
{
    brush_->setSettings(settings);
}

BrushPointer Canvas::brushFactory(const QString &name)
{
    return Singleton<BrushManager>::instance().makeBrush(name);
}

void Canvas::setBrushFeature(const QString &key, const QVariant &value)
{
    auto&& settings = brushSettings();
    settings.insert(key, value);
    setBrushSettings(settings);
}

QList<QImage> Canvas::layerImages() const
{
    QList<QImage> lists;
    for(int i=0;i<layers.count();++i){
        lists.append(*(layers.layerFrom(i)->imageConstPtr()));
    }
    return lists;
}

PaintingProject Canvas::projectState() const
{
    PaintingProject project;
    project.size = canvasSize_;
    for (int i = 0; i < layers.count(); ++i) {
        const auto layer = layers.layerFrom(i);
        project.layers.append({layer->name(), layer->isTouched() ? *layer->imageConstPtr() : QImage(),
                               !layer->isHided(), layer->isLocked()});
        if (layer == layers.selectedLayer()) project.selectedLayer = i;
    }
    return project;
}

void Canvas::setProjectState(const PaintingProject &project)
{
    control_mode_ = NONE;
    for (auto &brush : localBrush) brush->setSurface(LayerPointer());
    brush_->setSurface(LayerPointer());
    layers.reset();
    setCanvasSize(project.size);
    layerNameCounter = 0;
    for (const auto &entry : project.layers) {
        auto layer = layers.appendLayer(entry.name);
        if (!entry.image.isNull()) *layer->imagePtr() = entry.image;
        if (entry.locked) layer->lock();
        if (!entry.visible) layer->hide();
        ++layerNameCounter;
    }
    layers.select(project.layers.at(project.selectedLayer).name);
    updateCursor();
    update();
}

bool Canvas::renameLayer(const QString &oldName, const QString &newName)
{
    finishStroke();
    if (newName.trimmed().isEmpty() || newName.size() > 256 || !layers.exists(oldName)
            || layers.exists(newName)) return false;
    layers.rename(oldName, newName);
    emit documentChanged();
    return true;
}

bool Canvas::canDraw() const
{
    const auto selected = layers.selectedLayer();
    return selected && !selected->isLocked() && !selected->isHided();
}

void Canvas::changeBrush(const QString &name)
{
    finishStroke();
    const auto color = brush_->color();
    QVariantMap currentSettings;
    LayerPointer sur = brush_->surface();
    QVariantMap colorMap{{"red", color.red()}, {"green", color.green()}, {"blue", color.blue()}};

    QString brushName = name;
    if(localBrush.contains(brushName)){
        brush_ = localBrush[brushName];
        currentSettings = brush_->settings();
    }else{
        brush_ = brushFactory(brushName);
        brush_->setSettings(brush_->defaultSettings());
        localBrush.insert(brushName, brush_);
        currentSettings = brush_->settings();
    }
    if(shareColor_){
        currentSettings["color"] = colorMap;
    }

    brush_->setSettings(currentSettings);
    brush_->setSurface(sur);
    updateCursor();

    emit newBrushSettings(currentSettings);
}

void Canvas::onColorPicker(bool in)
{
    finishStroke();
    if(in){
        control_mode_ = PICKING;
        QPixmap icon = QPixmap(":/iconset/ui/picker-cursor.png");
        setCursor(QCursor(icon, 11, 20));
    }else{
        control_mode_ = NONE;
        updateCursor();
    }
}

void Canvas::onMoveTool(bool in)
{
    finishStroke();
    if(in){
        control_mode_ = MOVING;
        QPixmap icon = QPixmap(":/iconset/ui/brush/move-cursor.png");
        setCursor(QCursor(icon, 15, 15));
    }else{
        control_mode_ = NONE;
        updateCursor();
    }
}

void Canvas::drawLineTo(const QPoint &endPoint)
{
    LayerPointer l = layers.selectedLayer();
    if(l.isNull() || l->isLocked() || l->isHided()){
        setCursor(Qt::ForbiddenCursor);
        return;
    }
    updateCursor();
    brush_->setSurface(l);
    brush_->drawLineTo(endPoint);

    update();

    emit documentChanged();
}

void Canvas::drawPoint(const QPoint &point)
{
    LayerPointer l = layers.selectedLayer();
    if(l.isNull() || l->isLocked() || l->isHided()){
        setCursor(Qt::ForbiddenCursor);
        return;
    }
    updateCursor();
    brush_->setSurface(l);
    brush_->drawPoint(point);

    int rad = (brush_->width() / 2) + 2;
    update(QRect(point, point).normalized()
           .adjusted(-rad, -rad, +rad, +rad));

    emit documentChanged();
}

void Canvas::pickColor(const QPoint &point)
{
    if (!rect().contains(point)) return;
    const QColor color = currentCanvas().pixelColor(point);
    setBrushColor(color);
    emit newBrushSettings(brushSettings());
}

QString Canvas::currentLayer()
{
    const auto selected = layers.selectedLayer();
    return selected ? selected->name() : QString();
}

void Canvas::addLayer(const QString &name)
{
    finishStroke();
    if (layers.appendLayer(name)) {
        layerNameCounter++;
        layers.select(name);
        emit documentChanged();
    }
}

bool Canvas::deleteLayer(const QString &name)
{
    finishStroke();
    if (!layers.exists(name) || layers.count() <= 1 || layers.layerFrom(name)->isLocked())
        return false;

    layers.removeLayer(name);
    emit documentChanged();
    update();
    return true;
}

void Canvas::clearLayer(const QString &name)
{
    finishStroke();
    if (!layers.exists(name) || layers.layerFrom(name)->isLocked()) return;
    layers.clearLayer(name);
    emit documentChanged();
    update();
}

void Canvas::clearAllLayer()
{
    finishStroke();
    layers.clearAllLayer();
    emit documentChanged();
    update();
}

void Canvas::setLayerContent(int index, const QImage &image)
{
    finishStroke();
    LayerPointer layer = layers.layerFrom(index);
    if (layer.isNull() || image.isNull())
        return;
    layer->clear();
    {
        QPainter painter(layer->imagePtr());
        painter.drawImage(0, 0, image);
    }
    emit documentChanged();
    update();
}

void Canvas::lockLayer(const QString &name)
{
    finishStroke();
    if (!layers.exists(name)) return;
    layers.layerFrom(name)->lock();
    emit documentChanged();
}

void Canvas::unlockLayer(const QString &name)
{
    finishStroke();
    if (!layers.exists(name)) return;
    layers.layerFrom(name)->unlock();
    emit documentChanged();
}

void Canvas::hideLayer(const QString &name)
{
    finishStroke();
    if (!layers.exists(name)) return;
    layers.layerFrom(name)->hide();
    emit documentChanged();
    update();
}

void Canvas::showLayer(const QString &name)
{
    finishStroke();
    if (!layers.exists(name)) return;
    layers.layerFrom(name)->show();
    emit documentChanged();
    update();
}

void Canvas::moveLayerUp(const QString &name)
{
    finishStroke();
    layers.moveUp(name);
    emit documentChanged();
    update();
}

void Canvas::moveLayerDown(const QString &name)
{
    finishStroke();
    layers.moveDown(name);
    emit documentChanged();
    update();
}

void Canvas::layerSelected(const QString &name)
{
    finishStroke();
    layers.select(name);
}

/* Event control */
void Canvas::focusInEvent(QFocusEvent *)
{
    QSettings settings(GlobalDef::settingsPath(),
                       QSettings::defaultFormat(),
                       qApp);
    bool disable_ime = settings.value("canvas/auto_disable_ime", true).toBool();
    if(disable_ime)
        PlatformExtend::setIMEState(this, false);
}

void Canvas::focusOutEvent(QFocusEvent *)
{
    finishStroke();
    QSettings settings(GlobalDef::settingsPath(),
                       QSettings::defaultFormat(),
                       qApp);
    bool disable_ime = settings.value("canvas/auto_disable_ime", true).toBool();
    if(disable_ime)
        PlatformExtend::setIMEState(this, true);
}

void Canvas::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    QRect dirtyRect = event->rect();
    if(dirtyRect.isEmpty()) return;

    layers.combineLayers(&image, dirtyRect);
    painter.drawImage(dirtyRect, image, dirtyRect);

    QStyleOption opt;
    opt.initFrom(this);
    style()->drawPrimitive(QStyle::PE_Widget,
                           &opt, &painter, this);
}

void Canvas::resizeEvent(QResizeEvent *event)
{
    if(event->size() != canvasSize_)
        return;
    QSize newSize = event->size();
    canvasSize_ = newSize;
    layers.resizeLayers(newSize);
    QImage newImage(newSize, QImage::Format_ARGB32_Premultiplied);
    newImage.fill(Qt::transparent);
    QPainter painter(&newImage);
    painter.drawImage(QPoint(0, 0), image);
    image = newImage;

    update();
    QWidget::resizeEvent(event);
}

void Canvas::setCanvasSize(const QSize &size)
{
    if (size == canvasSize_ || !size.isValid())
        return;

    canvasSize_ = size;
    layers.resizeLayers(size);
    emit documentChanged();

    QImage newImage(size, QImage::Format_ARGB32_Premultiplied);
    newImage.fill(Qt::transparent);
    {
        QPainter painter(&newImage);
        painter.drawImage(QPoint(0, 0), image);
    }
    image = newImage;

    setFixedSize(size);
    update();
}

QSize Canvas::sizeHint() const
{
    return canvasSize_;
}

QSize Canvas::minimumSizeHint() const
{
    return canvasSize_;
}

void Canvas::updateCursor()
{
    setCursor(brush_->cursor());
}

void Canvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || event->source() != Qt::MouseEventNotSynthesized)
        return;
    if (control_mode_ != PICKING && control_mode_ != MOVING && !canDraw()) {
        setCursor(Qt::ForbiddenCursor);
        return;
    }
    switch (control_mode_) {
    case PICKING:
        pickColor(event->position().toPoint());
        break;
    case MOVING:
        break;
    default:
        control_mode_ = DRAWING;
        drawPoint(event->position().toPoint());
        break;
    }
}

void Canvas::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton) || event->source() != Qt::MouseEventNotSynthesized)
        return;
    switch (control_mode_) {
    case PICKING:
        pickColor(event->position().toPoint());
        break;
    case MOVING:
        break;
    case DRAWING:
        // Render every sample immediately; batching used to drop the flush sample.
        drawLineTo(event->position().toPoint());
        break;
    default:
        break;
    }
}

void Canvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || event->source() != Qt::MouseEventNotSynthesized)
        return;
    if (control_mode_ != DRAWING) return;
    drawLineTo(event->position().toPoint());
    finishStroke();
}

void Canvas::finishStroke()
{
    if (control_mode_ != DRAWING) return;
    brush_->endStroke();
    control_mode_ = NONE;
    updateCursor();
    emit documentChanged();
}
