#include <QPainter>
#include <QHash>
#include <QSharedPointer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStyleOption>
#include <QMouseEvent>
#include <QTabletEvent>
#include <QSettings>
#include <QApplication>
#include <QTimer>
#include <QDir>
#include <QStaticText>
#include <QDateTime>
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
#include "../misc/archivefile.h"

#include "canvas.h"

#define brush_manager (Singleton<BrushManager>::instance())

Canvas::Canvas(QWidget *parent) :
    QWidget(parent),
    m_tabletEnabled(false),
    control_mode_(UNKNOWN),
    canvasSize(QSize(720, 480)),
    layers(canvasSize),
    image(canvasSize, QImage::Format_ARGB32_Premultiplied),
    layerNameCounter(0),
    useV3Brush_(false),
    shareColor_(true),
    jitterCorrection_(true),
    jitterCorrectionLevel_(10),
    backend_(new CanvasBackend()),
    worker_(new QThread(this)),
    archive_loading_(false)
{
    setAttribute(Qt::WA_StaticContents);
    brush_ = BrushPointer(new BasicBrush);
    brush_->setSettings(brush_->defaultSettings());
    updateCursor();

    setMouseTracking(true);
    setFocusPolicy(Qt::WheelFocus);
    resize(canvasSize);

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

    setJitterCorrectionLevel(5);

    worker_->setObjectName("CanvasWorkerThread");
    worker_->start();
    backend_->moveToThread(worker_);

    connect(backend_, &CanvasBackend::remoteDrawLine,
            this, &Canvas::remoteDrawLine);
    connect(backend_, &CanvasBackend::remoteDrawPoint,
            this, &Canvas::remoteDrawPoint);
    connect(backend_, &CanvasBackend::remoteDrawBlock,
            this, &Canvas::remoteDrawBlock);
    connect(backend_, &CanvasBackend::repaintHint,
            this, static_cast<void (Canvas::*)()>(&Canvas::update));
    connect(worker_, &QThread::finished,
            backend_, &CanvasBackend::deleteLater);
    connect(this, &Canvas::newPaintAction,
            backend_, &CanvasBackend::onDataBlock);
    connect(this, &Canvas::parsePaused,
            backend_, &CanvasBackend::pauseParse);

    connect(backend_, &CanvasBackend::archiveLoadingStarted,
            this, &Canvas::onArchiveLoadingStarted, Qt::QueuedConnection);
    connect(backend_, &CanvasBackend::archiveRenderFinished,
            this, &Canvas::onArchiveRenderFinished, Qt::QueuedConnection);

    connect(backend_, &CanvasBackend::canvasSnapshotRestored,
            this, &Canvas::restoreCanvasFromSnapshot, Qt::QueuedConnection);
}

Canvas::~Canvas()
{
    pause();
    if(worker_){
        worker_->quit();
        worker_->wait();
    }
    this->disconnect();
}

QImage Canvas::currentCanvas()
{
    QImage pmp = image;
    layers.combineLayers(&pmp);
    return pmp;
}

QImage Canvas::allCanvas()
{
    QImage exp(canvasSize, QImage::Format_ARGB32_Premultiplied);
    exp.fill(Qt::white);
    QPainter painter(&exp);
    int count = layers.count();
    for(int i=0;i<count;++i){
        LayerPointer l = layers.layerFrom(i);
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
    if(brush_->color() == newColor) return;
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
        if(!layers.layerFrom(i)->isTouched()){
            continue;
        }
        lists.append(*(layers.layerFrom(i)->imageConstPtr()));
    }
    return lists;
}

void Canvas::pause()
{
    emit parsePaused();
}

void Canvas::initCanvasWithArchive(const QString &roomName)
{
    qDebug() << "[Canvas] 初始化画布，房间:" << roomName;

    if (backend_) {
        QMetaObject::invokeMethod(backend_, "initializeArchiveFile",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, roomName));
    }
}

void Canvas::saveCanvasToCache()
{
    if(backend_) {
        QList<QImage> images = layerImages();
        QMetaObject::invokeMethod(backend_, "saveCurrentCanvas",
                                  Qt::QueuedConnection,
                                  Q_ARG(QList<QImage>, images));
    }
}

void Canvas::loadCanvasFromCache()
{
    if(backend_) {
        QMetaObject::invokeMethod(backend_, "loadCachedCanvas",
                                  Qt::QueuedConnection);
    }
}

void Canvas::setArchiveLoading(bool loading)
{
    if(archive_loading_ != loading) {
        archive_loading_ = loading;
        if(loading) {
            setEnabled(false);
            qDebug() << "[Canvas] 手动设置loading状态为true";
        } else {
            setEnabled(true);
            qDebug() << "[Canvas] 手动设置loading状态为false";
        }
        update();
    }
}

void Canvas::onArchiveLoadingStarted()
{
    archive_loading_ = true;
    setEnabled(false);
    update();
    qDebug() << "[Canvas] 开始加载archive数据，设置loading状态";
}

void Canvas::onArchiveRenderFinished()
{
    archive_loading_ = false;
    setEnabled(true);
    update();
    qDebug() << "[Canvas] archive数据加载完成，移除loading状态";
}

void Canvas::exportCanvasSnapshot()
{
    qDebug() << "[Canvas] 开始导出画布快照";

    if (backend_) {
        QList<QImage> layerImages;
        for(int i = 0; i < layers.count(); ++i) {
            LayerPointer layer = layers.layerFrom(i);
            if(!layer.isNull()) {
                layerImages.append(*(layer->imageConstPtr()));
            }
        }

        if (!layerImages.isEmpty()) {
            QMetaObject::invokeMethod(backend_, "saveCanvasSnapshot",
                                      Qt::QueuedConnection,
                                      Q_ARG(QList<QImage>, layerImages));
            qDebug() << "[Canvas] 画布快照导出完成，共" << layerImages.size() << "个图层";
        } else {
            qDebug() << "[Canvas] 没有图层内容，跳过快照导出";
        }
    }
}

void Canvas::restoreCanvasFromSnapshot()
{
    qDebug() << "[Canvas] 开始从快照恢复画布";

    if (backend_) {
        bool hasSnapshot = false;
        QMetaObject::invokeMethod(backend_, "hasCanvasSnapshot",
                                  Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(bool, hasSnapshot));

        if (hasSnapshot) {
            QList<QImage> layerImages;
            QMetaObject::invokeMethod(backend_, "loadCanvasSnapshot",
                                      Qt::BlockingQueuedConnection,
                                      Q_RETURN_ARG(QList<QImage>, layerImages));

            if (!layerImages.isEmpty()) {
                for(int i = 0; i < qMin(layerImages.size(), layers.count()); ++i) {
                    LayerPointer layer = layers.layerFrom(i);
                    if(!layer.isNull()) {
                        QPainter painter(layer->imagePtr());
                        painter.drawImage(0, 0, layerImages[i]);
                    }
                }

                setEnabled(true);
                update();
                qDebug() << "[Canvas] 成功从快照恢复" << layerImages.size() << "个图层";
            }
        } else {
            qDebug() << "[Canvas] 没有可用的画布快照";
        }
    }
}

void Canvas::changeBrush(const QString &name)
{
    useV3Brush_ = false;
    QVariantMap currentSettings;
    LayerPointer sur = brush_->surface();
    QVariantMap colorMap = brush_->settings()
            .value("color").toMap();

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

    brush_->setSurface(sur);
    updateCursor();

    emit newBrushSettings(currentSettings);
}

void Canvas::changeBrushV3(const QString &name)
{
    useV3Brush_ = true;

    QVariantMap currentSettings;
    QVariantMap colorMap;

    if (brushV3_) {
        colorMap = brushV3_->settings().value("color").toMap();
    }

    QString brushName = name;
    brushV3_ = brushV3Factory(brushName);

    if (brushV3_) {
        currentSettings = brushV3_->settings();
        if(shareColor_ && !colorMap.isEmpty()){
            currentSettings["color"] = colorMap;
        }
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

    QVariantMap point;
    point.insert("x", endPoint.x());
    point.insert("y", endPoint.y());
    point.insert("pressure", double(pressure));

    storeAction(point);
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

    QVariantMap point_j;
    point_j.insert("x", point.x());
    point_j.insert("y", point.y());
    point_j.insert("pressure", double(pressure));

    storeAction(point_j);
}

void Canvas::storeAction(const QVariantMap &map)
{
    action_buffer_.push_back(map);
}

void Canvas::sendAction()
{
    QVariantMap store;
    store.insert("layer", currentLayer());
    store.insert("clientid", backend_->localClientId());
    store.insert("type", "data");
    store.insert("brush", brushSettings());
    store.insert("action", "block");
    store.insert("block", action_buffer_);

    qDebug() << "[Canvas] sendAction:" << store;
    emit newPaintAction(store);
    action_buffer_.clear();
}

void Canvas::pickColor(const QPoint &point)
{
    brush_->setColor(image.pixel(point));
    newBrushSettings(brush_->settings());
}

void Canvas::updateCursor()
{
    if (isCurrentBrushV3()) {
        this->setCursor(QCursor(brushV3_->cursor()));
    } else {
        this->setCursor(brush_->cursor());
    }
}

void Canvas::remoteDrawPoint(const QPoint &point,
                             const QVariantMap &brushInfo,
                             const QString &layer,
                             const QString clientid,
                             const qreal pressure)
{
    if(!layers.exists(layer)) return;
    LayerPointer l = layers.layerFrom(layer);

    QVariantMap cpd_brushInfo = brushInfo;
    QString brushName = cpd_brushInfo["name"].toString().toLower();

    cpd_brushInfo.remove("name");

    if(remoteBrush.contains(clientid)){
        BrushPointer t = remoteBrush[clientid];
        if(brushName != t->name().toLower()){
            BrushPointer newOne = brushFactory(brushName);
            newOne->setSurface(l);
            newOne->setSettings(cpd_brushInfo);
            newOne->drawPoint(point, pressure);
            remoteBrush[clientid] = newOne;
        }else{
            BrushPointer original = remoteBrush[clientid];
            original->setSurface(l);
            original->setSettings(cpd_brushInfo);
            original->drawPoint(point, pressure);
        }
    }else{
        BrushPointer newOne = brushFactory(brushName);
        newOne->setSurface(l);
        newOne->setSettings(cpd_brushInfo);
        newOne->drawPoint(point, pressure);
        remoteBrush[clientid] = newOne;
    }

    update();
}

void Canvas::remoteDrawLine(const QPoint &, const QPoint &end,
                            const QVariantMap &brushInfo,
                            const QString &layer,
                            const QString clientid,
                            const qreal pressure)
{
    if(!layers.exists(layer)){
        return;
    }
    LayerPointer l = layers.layerFrom(layer);

    QVariantMap cpd_brushInfo = brushInfo;
    QString brushName = cpd_brushInfo["name"].toString().toLower();

    if(remoteBrush.contains(clientid)){
        BrushPointer t = remoteBrush[clientid];
        if(brushName != t->name().toLower()){
            BrushPointer newOne = brushFactory(brushName);
            newOne->setSurface(l);
            newOne->setSettings(cpd_brushInfo);
            newOne->drawLineTo(end, pressure);
            remoteBrush[clientid] = newOne;
        }else{
            BrushPointer original = remoteBrush[clientid];
            original->setSurface(l);
            original->setSettings(cpd_brushInfo);
            original->drawLineTo(end, pressure);
        }
    }else{
        BrushPointer newOne = brushFactory(brushName);
        newOne->setSurface(l);
        newOne->setSettings(cpd_brushInfo);
        qDebug()<<"warning, remote drawing starts with line drawing";
        newOne->drawLineTo(end, pressure);
        remoteBrush[clientid] = newOne;
    }
    update();
}

void Canvas::remoteDrawBlock(const QVariantList &block,
                            const QVariantMap &brushInfo,
                            const QString &layer,
                            const QString clientid)
{
    if(!layers.exists(layer)){
        return;
    }
    LayerPointer l = layers.layerFrom(layer);

    QVariantMap cpd_brushInfo = brushInfo;
    QString brushName = cpd_brushInfo["name"].toString().toLower();
    cpd_brushInfo.remove("name");

    BrushPointer remoteBrushPtr;
    if(remoteBrush.contains(clientid)){
        remoteBrushPtr = remoteBrush[clientid];
        if(brushName != remoteBrushPtr->name().toLower()){
            remoteBrushPtr = brushFactory(brushName);
            remoteBrush[clientid] = remoteBrushPtr;
        }
    }else{
        remoteBrushPtr = brushFactory(brushName);
        remoteBrush[clientid] = remoteBrushPtr;
    }

    remoteBrushPtr->setSurface(l);
    remoteBrushPtr->setSettings(cpd_brushInfo);

    if(block.length() >= 1) {
        QVariantMap firstPoint = block.first().toMap();
        QPoint point(firstPoint.value("x", 0).toInt(), firstPoint.value("y", 0).toInt());
        qreal pressure = firstPoint.contains("pressure") ? firstPoint.value("pressure").toDouble() : 1.0;

        remoteBrushPtr->drawPoint(point, pressure);

        QPoint startPoint = point;
        for(int i = 1; i < block.length(); ++i) {
            QVariantMap pointData = block[i].toMap();
            QPoint endPoint(pointData.value("x", 0).toInt(), pointData.value("y", 0).toInt());
            qreal pressure = pointData.contains("pressure") ? pointData.value("pressure").toDouble() : 1.0;

            remoteBrushPtr->drawLineTo(endPoint, pressure);
            startPoint = endPoint;
        }
    }

    update();
}

/* Layer */

QString Canvas::currentLayer()
{
    return layers.selectedLayer()->name();
}

void Canvas::addLayer(const QString &name)
{
    layers.appendLayer(name);
    layerNameCounter++;
}

bool Canvas::deleteLayer(const QString &name)
{
    if(layers.layerFrom(name)->isLocked())
        return false;

    layers.removeLayer(name);
    update();
    return true;
}

void Canvas::clearLayer(const QString &name)
{
    layers.clearLayer(name);
    update();
}

void Canvas::clearAllLayer()
{
    layers.clearAllLayer();
    update();
}

void Canvas::lockLayer(const QString &name)
{
    layers.layerFrom(name)->lock();
}

void Canvas::unlockLayer(const QString &name)
{
    layers.layerFrom(name)->unlock();
}

void Canvas::hideLayer(const QString &name)
{
    layers.layerFrom(name)->hide();
    update();
}

void Canvas::showLayer(const QString &name)
{
    layers.layerFrom(name)->show();
    update();
}

void Canvas::moveLayerUp(const QString &name)
{
    layers.moveUp(name);
    update();
}

void Canvas::moveLayerDown(const QString &name)
{
    layers.moveDown(name);
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
            case DRAWING:
                PressurePoint pt(
                    event->position(),
                    event->pressure(),
                    event->xTilt() / 60.0,
                    event->yTilt() / 60.0
                );
                brushV3_->addPointToCurrentPath(pt);
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
            case DRAWING:
                brushV3_->endStroke();
                updateBrushV3StrokesOnDone();
                update();
                control_mode_ = NONE;
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
        case DRAWING:
            stackPoints.clear();
            updateCursor();
            sendAction();
            control_mode_ = NONE;
            if (brush_) brush_->endStroke();
        }
        break;
    default:
        break;
    }
    event->accept();
}

void Canvas::focusInEvent(QFocusEvent *)
{
    QSettings settings(GlobalDef::SETTINGS_NAME,
                       QSettings::defaultFormat(),
                       qApp);
    bool disable_ime = settings.value("canvas/auto_disable_ime", true).toBool();
    if(disable_ime)
        PlatformExtend::setIMEState(this, false);
}

void Canvas::focusOutEvent(QFocusEvent *)
{
    QSettings settings(GlobalDef::SETTINGS_NAME,
                       QSettings::defaultFormat(),
                       qApp);
    bool disable_ime = settings.value("canvas/auto_disable_ime", true).toBool();
    if(disable_ime)
        PlatformExtend::setIMEState(this, true);
}

void Canvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !m_tabletEnabled) {
        qDebug() << "Drawing with v3?" << useV3Brush_;
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
            if(isCurrentBrushV3()) {
                PressurePoint pt(
                    event->position()
                );
                brushV3_->addPointToCurrentPath(pt);
            } else {
                stackPoints.push_back(lastPoint);
                drawPoint(lastPoint);
            }
        }
    }
}

void Canvas::mouseMoveEvent(QMouseEvent *event)
{
    if ((event->buttons() & Qt::LeftButton && !m_tabletEnabled)){
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
    if (event->button() == Qt::LeftButton && !m_tabletEnabled) {
        switch(control_mode_) {
        case PICKING:
            break;
        case MOVING:
            break;
        default:
        case DRAWING:
            if (isCurrentBrushV3() && brushV3_) {
                brushV3_->endStroke();
                updateBrushV3StrokesOnDone();
                updateCursor();
                control_mode_ = NONE;
            } else {
                stackPoints.clear();
                updateCursor();
                sendAction();
                control_mode_ = NONE;
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
}

void Canvas::updateBrushV3StrokesOnGoing()
{
    if(!isCurrentBrushV3()){
        return;
    }

    LayerPointer currentLayer = layers.selectedLayer();
    QPainter imagePainter(currentLayer->imagePtr());

    brushV3_->drawCurrentPath(&imagePainter);
    brushV3_->clearCurrentPath();
}

void Canvas::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    QRect dirtyRect = event->rect();
    if(dirtyRect.isEmpty()) return;

    layers.combineLayers(&image, dirtyRect);
    painter.drawImage(dirtyRect, image, dirtyRect);

    if(archive_loading_){
        QBrush brush;
        brush.setStyle(Qt::BDiagPattern);
        brush.setColor(Qt::lightGray);
        painter.setBrush(brush);
        QRect rect = this->rect();
        rect.setWidth(rect.width());
        rect.setHeight(rect.height());
        painter.drawRect(rect);
    }

    QStyleOption opt;
    opt.initFrom(this);
    style()->drawPrimitive(QStyle::PE_Widget,
                           &opt, &painter, this);
}

void Canvas::resizeEvent(QResizeEvent *event)
{
    if(event->size() != canvasSize)
        return;
    QSize newSize = event->size();
    canvasSize = newSize;
    layers.resizeLayers(newSize);
    QImage newImage(newSize, QImage::Format_ARGB32_Premultiplied);
    newImage.fill(Qt::transparent);
    QPainter painter(&newImage);
    painter.drawImage(QPoint(0, 0), image);
    image = newImage;

    update();
    QWidget::resizeEvent(event);
}

QSize Canvas::sizeHint() const
{
    return canvasSize;
}

QSize Canvas::minimumSizeHint() const
{
    return canvasSize;
}
