#include <QPainter>
#include <QHash>
#include <QSharedPointer>
#include <QStyleOption>
#include <QMouseEvent>
#include <QTabletEvent>
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
#include "../paintingTools/brush/basicbrushv3.h"
#include "../paintingTools/brush/basicbrushv3-simd.h"
#include "../misc/platformextend.h"
#include "../misc/singleton.h"

#include "canvas.h"

#define brush_manager (Singleton<BrushManager>::instance())

Canvas::Canvas(QWidget *parent) :
    QWidget(parent),
    m_tabletEnabled(false),
    control_mode_(UNKNOWN),
    canvasSize_(QSize(720, 480)),
    layers(canvasSize_),
    image(canvasSize_, QImage::Format_ARGB32_Premultiplied),
    layerNameCounter(0),
    useV3Brush_(false),
    shareColor_(true),
    jitterCorrection_(true),
    jitterCorrectionLevel_(10),
    jitterCorrectionLevel_internal_(0)
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
    BrushPointerV3 pressureBrush(new BasicBrushV3);
    pressureBrush->setSettings(pressureBrush->defaultSettings());
    brush_manager.addBrushV3(pressureBrush);

    setJitterCorrectionLevel(5);
    QSettings settings(GlobalDef::settingsPath(), QSettings::IniFormat);
    m_tabletEnabled = settings.value("canvas/enable_tablet", true).toBool();
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

int Canvas::jitterCorrectionLevel() const
{
    return jitterCorrectionLevel_;
}

bool Canvas::isJitterCorrectionEnabled() const
{
    return jitterCorrection_;
}

void Canvas::setJitterCorrectionEnabled(bool correct)
{
    jitterCorrection_ = correct;
}

void Canvas::setJitterCorrectionLevel(int value)
{
    jitterCorrectionLevel_ = qBound(0, value, 10);
    jitterCorrectionLevel_internal_ = jitterCorrectionLevel_ * 0.5;
}

void Canvas::tryJitterCorrection()
{
    if(stackPoints.length() < qBound(3, jitterCorrectionLevel_, 10) )
        return;

    int amount = stackPoints.length();
    int redudent = amount;

    auto should_correct = [this](const QPoint& p1,
            const QPoint& p2,
            const QPoint& p3) -> bool
    {
        QLine l1(p1, p2);
        QLine l2(p2, p3);
        QLine l3(p3, p1);

        qreal A = 1.0;
        if(l3.dx() != l1.dx()){
            A = (l1.dy() - l3.dy()) / (l3.dx() - l1.dx());
        }
        qreal B = 1.0;
        qreal C = l1.dx() * (-A) - l1.dy();

        qreal distance_up = A*l2.dx() + B*l2.dy() + C;
        qreal distance_down = qSqrt(A*A+B*B);
        if(qFuzzyCompare(distance_down, 0.0)){
            return false;
        }

        qreal distance = qAbs(distance_up / distance_down);

        if(distance <= jitterCorrectionLevel_internal_ ){
            return true;
        }else{
            return false;
        }

    };

    int basePos = 0;
    while((stackPoints.length() >= 3) && (basePos < stackPoints.length() - 3)){
        if(should_correct(stackPoints[basePos],
                          stackPoints[basePos+1],
                          stackPoints[basePos+2])){
            stackPoints.removeAt(basePos+1);
            redudent--;
        }else{
            basePos++;
        }
    }
}

QVariantMap Canvas::brushSettings() const
{
    auto m = isCurrentBrushV3() ? brushV3_->settings() : brush_->settings();
    m.insert("name", isCurrentBrushV3() ? brushV3_->name().toLower() : brush_->name().toLower());
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
    if (isCurrentBrushV3()) brushV3_->setColor(newColor);
    else brush_->setColor(newColor);
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
    if (isCurrentBrushV3()) brushV3_->setSettings(settings);
    else brush_->setSettings(settings);
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
    stackPoints.clear();
    if (brushV3_) brushV3_->clearAllPaths();
    v3StrokeBase_ = QImage();
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
    const auto color = isCurrentBrushV3() ? brushV3_->settings().value("color").value<QColor>() : brush_->color();
    useV3Brush_ = false;
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

void Canvas::changeBrushV3(const QString &name)
{
    QVariantMap currentSettings;

    const auto legacyColor = brush_->settings().value("color").toMap();
    const QColor color = isCurrentBrushV3() ? brushV3_->settings().value("color").value<QColor>()
        : QColor(legacyColor.value("red").toInt(), legacyColor.value("green").toInt(), legacyColor.value("blue").toInt());
    useV3Brush_ = true;

    QString brushName = name;
    brushV3_ = brushV3Factory(brushName);

    if (brushV3_) {
        currentSettings = brushV3_->defaultSettings();
        if (shareColor_) currentSettings["color"] = color;
        brushV3_->setSettings(currentSettings);
    }

    updateCursor();
    emit newBrushSettings(currentSettings);
}

BrushPointerV3 Canvas::brushV3Factory(const QString &name)
{
    return Singleton<BrushManager>::instance().makeBrushV3(name);
}

bool Canvas::isCurrentBrushV3() const
{
    return useV3Brush_ && brushV3_;
}

void Canvas::onColorPicker(bool in)
{
    if(in){
        control_mode_ = PICKING;
        QPixmap icon = QPixmap(":/iconset/ui/picker-cursor.png");
        setCursor(QCursor(icon, 11, 20));
    }else{
        control_mode_ = NONE;
        updateCursor();
        emit canvasToolComplete();
    }
}

void Canvas::onMoveTool(bool in)
{
    if(in){
        control_mode_ = MOVING;
        QPixmap icon = QPixmap(":/iconset/ui/brush/move-cursor.png");
        setCursor(QCursor(icon, 15, 15));
    }else{
        control_mode_ = NONE;
        updateCursor();
        emit canvasToolComplete();
    }
}

void Canvas::drawLineTo(const QPoint &endPoint, qreal pressure)
{
    LayerPointer l = layers.selectedLayer();
    if(l.isNull() || l->isLocked() || l->isHided()){
        setCursor(Qt::ForbiddenCursor);
        return;
    }
    updateCursor();
    brush_->setSurface(l);
    brush_->drawLineTo(endPoint, pressure);

    update();

    emit documentChanged();
}

void Canvas::drawPoint(const QPoint &point, qreal pressure)
{
    LayerPointer l = layers.selectedLayer();
    if(l.isNull() || l->isLocked() || l->isHided()){
        setCursor(Qt::ForbiddenCursor);
        return;
    }
    updateCursor();
    brush_->setSurface(l);
    brush_->drawPoint(point, pressure);

    int rad = (brush_->width() / 2) + 2;
    update(QRect(lastPoint, point).normalized()
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

void Canvas::updateCursor()
{
    if (isCurrentBrushV3()) {
        this->setCursor(QCursor(brushV3_->cursor()));
    } else {
        this->setCursor(brush_->cursor());
    }
}

QString Canvas::currentLayer()
{
    const auto selected = layers.selectedLayer();
    return selected ? selected->name() : QString();
}

void Canvas::addLayer(const QString &name)
{
    if (layers.appendLayer(name)) {
        layerNameCounter++;
        layers.select(name);
        emit documentChanged();
    }
}

bool Canvas::deleteLayer(const QString &name)
{
    if (!layers.exists(name) || layers.count() <= 1 || layers.layerFrom(name)->isLocked())
        return false;

    layers.removeLayer(name);
    emit documentChanged();
    update();
    return true;
}

void Canvas::clearLayer(const QString &name)
{
    if (!layers.exists(name) || layers.layerFrom(name)->isLocked()) return;
    layers.clearLayer(name);
    emit documentChanged();
    update();
}

void Canvas::clearAllLayer()
{
    layers.clearAllLayer();
    emit documentChanged();
    update();
}

void Canvas::setLayerContent(int index, const QImage &image)
{
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
    if (!layers.exists(name)) return;
    layers.layerFrom(name)->lock();
    emit documentChanged();
}

void Canvas::unlockLayer(const QString &name)
{
    if (!layers.exists(name)) return;
    layers.layerFrom(name)->unlock();
    emit documentChanged();
}

void Canvas::hideLayer(const QString &name)
{
    if (!layers.exists(name)) return;
    layers.layerFrom(name)->hide();
    emit documentChanged();
    update();
}

void Canvas::showLayer(const QString &name)
{
    if (!layers.exists(name)) return;
    layers.layerFrom(name)->show();
    emit documentChanged();
    update();
}

void Canvas::moveLayerUp(const QString &name)
{
    layers.moveUp(name);
    emit documentChanged();
    update();
}

void Canvas::moveLayerDown(const QString &name)
{
    layers.moveDown(name);
    emit documentChanged();
    update();
}

void Canvas::layerSelected(const QString &name)
{
    layers.select(name);
}

/* Event control */
void Canvas::tabletEvent(QTabletEvent *event)
{
    if (!m_tabletEnabled)
        return;

    if (event->type() == QEvent::TabletPress && control_mode_ != PICKING && control_mode_ != MOVING
            && !canDraw()) { event->accept(); return; }

    if (isCurrentBrushV3()) {
        switch(event->type()){
        case QEvent::TabletPress:
            if (event->deviceType() != QInputDevice::DeviceType::Stylus || qFuzzyCompare(event->pressure(), 0.0))
                break;
            lastPoint = event->position().toPoint();
            switch(control_mode_) {
            case PICKING:
                pickColor(event->position().toPoint());
                break;
            case MOVING:
                break;
            default:
            case NONE:
                control_mode_ = DRAWING;
                brushV3_->clearAllPaths();
                v3StrokeBase_ = *layers.selectedLayer()->imageConstPtr();
            case DRAWING:
                PressurePoint pt(
                    event->position(),
                    event->pressure(),
                    event->xTilt() / 60.0,
                    event->yTilt() / 60.0
                );
                brushV3_->addPointToCurrentPath(pt);
                updateBrushV3StrokesOnGoing();
                update();
            }
            break;
        case QEvent::TabletMove:
            if (event->deviceType() != QInputDevice::DeviceType::Stylus)
                break;
            switch(control_mode_) {
            case PICKING:
                pickColor(event->position().toPoint());
                break;
            case MOVING:
            {
                auto p(lastPoint - event->position().toPoint());
                if(p.manhattanLength() > 10){
                    emit contentMovedBy(p);
                }
            }
                break;
            case DRAWING:
            {
                PressurePoint pt(
                    event->position(),
                    event->pressure(),
                    event->xTilt() / 60.0,
                    event->yTilt() / 60.0
                );
                brushV3_->addPointToCurrentPath(pt);
                updateBrushV3StrokesOnGoing();
                update();
            }
                break;
            default:
                break;
            }
            break;
        case QEvent::TabletRelease:
            switch(control_mode_) {
            case PICKING:
                break;
            case MOVING:
                break;
            default:
                break;
            case DRAWING:
                updateBrushV3StrokesOnGoing();
                brushV3_->endStroke();
                updateBrushV3StrokesOnDone();
                update();
                control_mode_ = NONE;
                emit documentChanged();
            }
            break;
        default:
            break;
        }
        event->accept();
        return;
    }

    switch(event->type()){
    case QEvent::TabletPress:
        if (event->deviceType() != QInputDevice::DeviceType::Stylus || qFuzzyCompare(event->pressure(), 0.0))
            break;
        lastPoint = event->position().toPoint();
        switch(control_mode_) {
        case PICKING:
            pickColor(event->position().toPoint());
            break;
        case MOVING:
            break;
        default:
        case NONE:
            control_mode_ = DRAWING;
        case DRAWING:
            stackPoints.push_back(lastPoint);
            drawPoint(lastPoint, event->pressure());
        }
        break;
    case QEvent::TabletMove:
        if (event->deviceType() != QInputDevice::DeviceType::Stylus)
            break;
        switch(control_mode_) {
        case PICKING:
            pickColor(event->position().toPoint());
            break;
        case MOVING:
        {
            auto p(lastPoint - event->position().toPoint());
            if(p.manhattanLength() > 10){
                emit contentMovedBy(p);
            }
        }
            break;
        case DRAWING:
            if(jitterCorrection_){
                if(stackPoints.length() < qBound(3, jitterCorrectionLevel_, 10)){
                    stackPoints.push_back(event->position().toPoint());
                }else{
                    tryJitterCorrection();
                    for(auto &p: stackPoints){
                        drawLineTo(p, event->pressure());
                        lastPoint = p;
                    }
                    stackPoints.clear();
                }
                            }else{
                drawLineTo(event->position().toPoint(), event->pressure());
                lastPoint = event->position().toPoint();
            }
            break;
        default:
            break;
        }
        break;
    case QEvent::TabletRelease:
        switch(control_mode_) {
        case PICKING:
            break;
        case MOVING:
            break;
        default:
            break;
        case DRAWING:
            for (const auto &point : stackPoints) drawLineTo(point, event->pressure());
            stackPoints.clear();
            updateCursor();
            control_mode_ = NONE;
            if (brush_) brush_->endStroke();
            emit documentChanged();
        }
        break;
    default:
        break;
    }
    event->accept();
}

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
    QSettings settings(GlobalDef::settingsPath(),
                       QSettings::defaultFormat(),
                       qApp);
    bool disable_ime = settings.value("canvas/auto_disable_ime", true).toBool();
    if(disable_ime)
        PlatformExtend::setIMEState(this, true);
}

void Canvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && (!m_tabletEnabled || event->source() == Qt::MouseEventNotSynthesized)) {
        if (control_mode_ != PICKING && control_mode_ != MOVING && !canDraw()) return;
        lastPoint = event->position().toPoint();
        switch(control_mode_) {
        case PICKING:
            pickColor(event->position().toPoint());
            break;
        case MOVING:
            break;
        default:
        case NONE:
            control_mode_ = DRAWING;
            if (isCurrentBrushV3()) {
                brushV3_->clearAllPaths();
                v3StrokeBase_ = *layers.selectedLayer()->imageConstPtr();
            }
        case DRAWING:
            if(isCurrentBrushV3()) {
                PressurePoint pt(
                    event->position()
                );
                brushV3_->addPointToCurrentPath(pt);
                updateBrushV3StrokesOnGoing();
            } else {
                stackPoints.push_back(lastPoint);
                drawPoint(lastPoint);
            }
        }
    }
}

void Canvas::mouseMoveEvent(QMouseEvent *event)
{
    if ((event->buttons() & Qt::LeftButton && (!m_tabletEnabled || event->source() == Qt::MouseEventNotSynthesized))){
        switch(control_mode_) {
        case PICKING:
            pickColor(event->position().toPoint());
            break;
        case MOVING:
        {
            auto p(lastPoint - event->position().toPoint());
            if(p.manhattanLength() > 10){
                emit contentMovedBy(p);
            }
        }
            break;
        case DRAWING:
            if(isCurrentBrushV3()) {
                PressurePoint pt(
                    event->position()
                );
                brushV3_->addPointToCurrentPath(pt);
                updateBrushV3StrokesOnGoing();
                update();
            } else {
                if(jitterCorrection_){
                    if(stackPoints.length() < qBound(3, jitterCorrectionLevel_, 10)){
                        stackPoints.push_back(event->position().toPoint());
                    }else{
                        tryJitterCorrection();
                        for(auto &p: stackPoints){
                            drawLineTo(p);
                            lastPoint = p;
                        }
                        stackPoints.clear();
                    }
                }else{
                    drawLineTo(event->position().toPoint());
                    lastPoint = event->position().toPoint();
                }
            }
            break;
        default:
            break;
        }
    }
}

void Canvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && (!m_tabletEnabled || event->source() == Qt::MouseEventNotSynthesized)) {
        switch(control_mode_) {
        case PICKING:
            break;
        case MOVING:
            break;
        default:
            break;
        case DRAWING:
            if (isCurrentBrushV3() && brushV3_) {
                updateBrushV3StrokesOnGoing();
                brushV3_->endStroke();
                updateBrushV3StrokesOnDone();
                updateCursor();
                control_mode_ = NONE;
                emit documentChanged();
            } else {
                for (const auto &point : stackPoints) drawLineTo(point);
                drawLineTo(event->position().toPoint());
                stackPoints.clear();
                if (brush_) brush_->endStroke();
                updateCursor();
                control_mode_ = NONE;
                emit documentChanged();
            }
        }
    }

}

void Canvas::updateBrushV3StrokesOnDone()
{
    if(!isCurrentBrushV3()){
        return;
    }
    brushV3_->clearAllPaths();
    v3StrokeBase_ = QImage();
}

void Canvas::updateBrushV3StrokesOnGoing()
{
    if(!isCurrentBrushV3()){
        return;
    }

    LayerPointer currentLayer = layers.selectedLayer();
    if (!canDraw()) { brushV3_->clearAllPaths(); return; }
    QImage preview = v3StrokeBase_;
    if (preview.isNull()) return;
    {
        QPainter imagePainter(&preview);
        brushV3_->drawCurrentPath(&imagePainter);
    }
    *currentLayer->imagePtr() = preview;
    emit documentChanged();
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
