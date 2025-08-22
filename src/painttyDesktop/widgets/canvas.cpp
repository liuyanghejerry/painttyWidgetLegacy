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
#include "../common/network/sse-clientsocket.h"
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

/*!
     \class Canvas

     \brief The widget that used for rendering painting results.

     Canvas uses several \l{QPixmap} to render contents. Each layer
     opaque one \l{QPixmap} and painted together when paintEvent happens.

     Canvas can be used for mouse painting, tablet painting and remote
     painting via json messages from network.

     \l{Brush} is used for painting. There're 4 kind of brush
     currently and will be more. When changing color of
     a \l{Brush} using on Canvas now,
     you can use either setBrushColor()
     or \l{Brush::} {setColor()} .

     \sa Brush
*/

/*!
    \fn Canvas::Canvas(QWidget *parent)

    Construct a canvas with width of 720px, height of 480px and a \a parent.
*/

Canvas::Canvas(QWidget *parent) :
    QWidget(parent),
    m_tabletEnabled(false),
    control_mode_(UNKNOWN),
    canvasSize(QSize(720, 480)), // 默认尺寸，稍后会被设置
    layers(canvasSize),
    image(canvasSize, QImage::Format_ARGB32_Premultiplied),
    layerNameCounter(0),
    useV3Brush_(false), // 默认使用V1笔刷
    shareColor_(true),
    jitterCorrection_(true),
    jitterCorrectionLevel_(10),
    backend_(new CanvasBackend()),
    worker_(new QThread(this)),
    archive_loading_(false),
    clientSocket_(nullptr)
{
    setAttribute(Qt::WA_StaticContents);
    brush_ = BrushPointer(new BasicBrush);
    brush_->setSettings(brush_->defaultSettings());  // 设置默认设置
    updateCursor();

    setMouseTracking(true);
    setFocusPolicy(Qt::WheelFocus); // necessary for IME control
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
    
    // v3还不太成熟，先注释掉
    // // 根据平台支持情况注册SIMD优化的V3笔刷
    // #if defined(PAINTTY_USE_SIMD)
    //     // 在 x86 平台上注册 SIMD 版本
    //     BrushPointerV3 pv3_simd(new BasicBrushV3SIMD);
    //     pv3_simd->setSettings(pv3_simd->defaultSettings());
    //     brush_manager.addBrushV3(pv3_simd);
    //     qDebug() << "[Canvas] 注册 SIMD 优化的 V3 笔刷 (x86 平台)";
    // #else
    //     qDebug() << "[Canvas] 跳过 SIMD 优化的 V3 笔刷注册 (ARM64 平台)";
    //     // 注册V3笔刷
    //     BrushPointerV3 pv3(new BasicBrushV3);
    //     pv3->setSettings(pv3->defaultSettings());
    //     brush_manager.addBrushV3(pv3);
    // #endif

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

    // TODO: proper use of signal requestSortedMembers,
    // instead of using QTimer
    QTimer *t = new QTimer(this);

    qRegisterMetaType<MSI>("CanvasBackend::MemberSectionIndex");
    qRegisterMetaType< QList<MS> >("QList<MemberSection>");

    connect(backend_, &CanvasBackend::membersSorted,
            this, &Canvas::onMembersSorted, Qt::QueuedConnection);
    connect(this, &Canvas::requestSortedMembers,
            backend_, &CanvasBackend::requestMembers, Qt::QueuedConnection);
    connect(this, &Canvas::requestClearMembers,
            backend_, &CanvasBackend::clearMembers, Qt::QueuedConnection);
    connect(backend_, &CanvasBackend::archiveLoadingStarted,
            this, &Canvas::onArchiveLoadingStarted, Qt::QueuedConnection);
    connect(backend_, &CanvasBackend::archiveRenderFinished,
            this, &Canvas::onArchiveRenderFinished, Qt::QueuedConnection);

    // 新增：连接本地和远程archive信号
    connect(backend_, &CanvasBackend::localArchiveLoadingStarted,
            this, &Canvas::onLocalArchiveLoadingStarted, Qt::QueuedConnection);
    connect(backend_, &CanvasBackend::localArchiveRenderFinished,
            this, &Canvas::onLocalArchiveRenderFinished, Qt::QueuedConnection);
    connect(backend_, &CanvasBackend::remoteArchiveLoadingStarted,
            this, &Canvas::onRemoteArchiveLoadingStarted, Qt::QueuedConnection);
    connect(backend_, &CanvasBackend::remoteArchiveRenderFinished,
            this, &Canvas::onRemoteArchiveRenderFinished, Qt::QueuedConnection);

    // 新增：数据同步相关信号连接
    connect(backend_, &CanvasBackend::cachedCanvasLoaded,
            this, &Canvas::onCachedCanvasLoaded, Qt::QueuedConnection);
    connect(backend_, &CanvasBackend::canvasSaveCompleted,
            this, &Canvas::onCanvasSaveCompleted, Qt::QueuedConnection);

    // 新增：连接画布快照恢复信号
    connect(backend_, &CanvasBackend::canvasSnapshotRestored,
            this, &Canvas::restoreCanvasFromSnapshot, Qt::QueuedConnection);


    connect(t, &QTimer::timeout,
            this, &Canvas::onRequestSortedMembers);
    t->start(500);
}

/*!
    \fn Canvas::~Canvas()

    Destroys the canvas. The internal \l Brush will also be deleted.
*/

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
    return appendAuthorSignature(pmp);
}

QImage Canvas::allCanvas()
{
    QImage exp(canvasSize, QImage::Format_ARGB32_Premultiplied);
    exp.fill(Qt::white);
    QPainter painter(&exp);
    int count = layers.count();
    QImage * im = 0;
    for(int i=0;i<count;++i){
        LayerPointer l = layers.layerFrom(i);
        im = l->imagePtr();
        painter.drawImage(0, 0, *im);
    }
    return appendAuthorSignature(exp);
}

QImage Canvas::appendAuthorSignature(QImage target)
{

    QHash<QString, MS> list;
    for(const MS &elem: author_list_){
        QString author = std::get<MSI::Name>(elem);
        if(list.contains(author)){
            std::get<MSI::Count>(list[author]) += std::get<MSI::Count>(elem);
        }else{
            list.insert(author, elem);
        }
    }

    // TODO: make all numbers configurable
    int textSize = qMin(target.size().height(), target.width());
    textSize = qBound(10, int(textSize * 0.02), 100);

    QPainter painter(&target);
    QPen textPen;
    textPen.setColor(Qt::black);
    painter.setOpacity(0.4);
    painter.setPen(textPen);

    QStaticText text;
    QString authors;
    text.setTextFormat(Qt::RichText);
    QTextOption textOp;
    textOp.setAlignment(Qt::AlignRight);
    text.setTextOption(textOp);

    for(auto& item: list.values()){
        QString name = std::get<MSI::Name>(item);
        authors += QString("BY ") + name + "<br/>";
    }

    text.setText(authors);

    QFont textFont(QGuiApplication::font());
    textFont.setPointSize(textSize);
    painter.setFont(textFont);
    text.prepare(QTransform(), textFont);

    QPoint rightCorner = target.rect().bottomRight();
    QPoint padding(20, 20);
    rightCorner -= QPoint(text.size().width(),
                          text.size().height());
    rightCorner -= padding;

    painter.drawStaticText(rightCorner, text);
    return target;
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
        //        qDebug()<<"distance"<<distance;

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
    // you can see the correction rate.
    //    qDebug()<<"correction rate: "<<qreal(amount-redudent)/amount *100<<"%";
}

/*!
    \fn QVariantMap Canvas::brushInfo()

    Returns the brush info painting now.
    \sa Brush::brushInfo()
*/

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

/*!
    \fn void Canvas::setBrushColor(const QColor &newColor)

    Sets a \a newColor to current brush.
    \sa Brush::setColor()
*/

void Canvas::setBrushColor(const QColor &newColor)
{
    if(brush_->color() == newColor) return;
    brush_->setColor(newColor);
}

/*!
    \fn void Canvas::setBrushWidth(int newWidth)

    Sets a \a newWidth to current brush.
    \sa Brush::setWidth()
*/

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

// 新增：处理登录完成后的数据同步
void Canvas::onLoginCompleted(const QString &roomName,
                              const QString &remoteArchiveSign,
                              SSEClientSocket *clientSocket) {
    clientSocket_ = clientSocket;
    qDebug() << "[Canvas] 登录完成，开始处理数据同步，房间:" << roomName
            << "远程签名:" << remoteArchiveSign;

    if (backend_) {
      // 跨线程调用setClientSocket
      QMetaObject::invokeMethod(backend_, "initInThread",
                                Qt::QueuedConnection,
                                Q_ARG(SSEClientSocket*, clientSocket_));
    }

    auto roomInfoManager = clientSocket_->roomInfoManager();
    auto roomInfo = roomInfoManager->getRoomInfo(roomName);
    if (roomInfo) {
      canvasSize = roomInfo->canvasSize;
      resize(canvasSize);
      qDebug() << "[Canvas] 设置画布尺寸:" << canvasSize;
    }

    // 跨线程调用processLocalArchive
    QMetaObject::invokeMethod(backend_, "processLocalArchive",
                              Qt::QueuedConnection, Q_ARG(QString, roomName),
                              Q_ARG(QString, remoteArchiveSign));
    qDebug() << "[Canvas] 初始化数据同步（改进流程），房间:" << roomName
             << "远程签名:" << remoteArchiveSign;
}

void Canvas::saveCanvasToCache()
{
    if(backend_) {
        QList<QImage> images = layerImages();
        // 跨线程调用saveCurrentCanvas
        QMetaObject::invokeMethod(backend_, "saveCurrentCanvas",
                                  Qt::QueuedConnection,
                                  Q_ARG(QList<QImage>, images));
    }
}

void Canvas::loadCanvasFromCache()
{
    if(backend_) {
        // 跨线程调用loadCachedCanvas
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
        update(); // 触发重绘
    }
}

void Canvas::onArchiveLoadingStarted()
{
    archive_loading_ = true;
    setEnabled(false);
    update(); // 触发重绘以显示loading状态
    qDebug() << "[Canvas] 开始加载archive数据，设置loading状态";
}

void Canvas::onArchiveRenderFinished()
{
    archive_loading_ = false;
    setEnabled(true);
    update(); // 触发重绘以移除loading状态
    qDebug() << "[Canvas] archive数据加载完成，移除loading状态";
}

// 新增：本地和远程archive处理槽函数
void Canvas::onLocalArchiveLoadingStarted()
{
    archive_loading_ = true;
    setEnabled(false);
    update(); // 触发重绘以显示loading状态
    qDebug() << "[Canvas] 开始加载本地archive数据，设置loading状态";
}

void Canvas::onLocalArchiveRenderFinished()
{
    qDebug() << "[Canvas] 本地archive数据加载完成";
    // 本地archive完成后，启动远程archive处理
    if (backend_) {
        QMetaObject::invokeMethod(backend_, "startRemoteArchiveProcessing",
                                  Qt::QueuedConnection);
    }
}

void Canvas::onRemoteArchiveLoadingStarted()
{
    archive_loading_ = true;
    setEnabled(false);
    update(); // 触发重绘以显示loading状态
    qDebug() << "[Canvas] 开始加载远程archive数据，设置loading状态";
}

void Canvas::onRemoteArchiveRenderFinished()
{
    archive_loading_ = false;
    setEnabled(true);
    update(); // 触发重绘以移除loading状态
    qDebug() << "[Canvas] 远程archive数据加载完成，移除loading状态";
}

void Canvas::onCanvasSaveCompleted()
{
    qDebug() << "[Canvas] 画布保存完成";
}

void Canvas::onRequestSortedMembers()
{
    emit requestSortedMembers(MSI::Count);
}



void Canvas::onCachedCanvasLoaded()
{
    qDebug() << "[Canvas] 缓存画布加载完成";

    // 通过CanvasBackend获取缓存的图层图片
    if(backend_) {
        QList<QImage> layerImages;
        // 跨线程调用getCachedLayerImages
        QMetaObject::invokeMethod(backend_, "getCachedLayerImages",
                                  Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(QList<QImage>, layerImages));

        if(!layerImages.isEmpty()) {
            // 恢复图层内容
            for(int i = 0; i < qMin(layerImages.size(), layers.count()); ++i) {
                LayerPointer layer = layers.layerFrom(i);
                if(!layer.isNull()) {
                    QPainter painter(layer->imagePtr());
                    painter.drawImage(0, 0, layerImages[i]);
                }
            }

            setEnabled(true);
            // 触发重绘
            update();
            qDebug() << "[Canvas] 成功恢复" << layerImages.size() << "个图层";
        }
    }
}

// 新增：导出画布快照
void Canvas::exportCanvasSnapshot()
{
    qDebug() << "[Canvas] 开始导出画布快照";

    if (backend_) {
        // 获取当前所有图层的图片（包括未修改的图层）
        QList<QImage> layerImages;
        for(int i = 0; i < layers.count(); ++i) {
            LayerPointer layer = layers.layerFrom(i);
            if(!layer.isNull()) {
                layerImages.append(*(layer->imageConstPtr()));
            }
        }

        if (!layerImages.isEmpty()) {
            // 跨线程调用保存画布快照
            QMetaObject::invokeMethod(backend_, "saveCanvasSnapshot",
                                      Qt::QueuedConnection,
                                      Q_ARG(QList<QImage>, layerImages));
            qDebug() << "[Canvas] 画布快照导出完成，共" << layerImages.size() << "个图层";
        } else {
            qDebug() << "[Canvas] 没有图层内容，跳过快照导出";
        }
    }
}

// 新增：从快照恢复画布
void Canvas::restoreCanvasFromSnapshot()
{
    qDebug() << "[Canvas] 开始从快照恢复画布";

    if (backend_) {
        // 检查是否有可用的快照
        bool hasSnapshot = false;
        QMetaObject::invokeMethod(backend_, "hasCanvasSnapshot",
                                  Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(bool, hasSnapshot));

        if (hasSnapshot) {
            // 跨线程调用加载画布快照
            QList<QImage> layerImages;
            QMetaObject::invokeMethod(backend_, "loadCanvasSnapshot",
                                      Qt::BlockingQueuedConnection,
                                      Q_RETURN_ARG(QList<QImage>, layerImages));

            if (!layerImages.isEmpty()) {
                // 恢复图层内容
                for(int i = 0; i < qMin(layerImages.size(), layers.count()); ++i) {
                    LayerPointer layer = layers.layerFrom(i);
                    if(!layer.isNull()) {
                        QPainter painter(layer->imagePtr());
                        painter.drawImage(0, 0, layerImages[i]);
                    }
                }

                setEnabled(true);
                // 触发重绘
                update();
                qDebug() << "[Canvas] 成功从快照恢复" << layerImages.size() << "个图层";
            }
        } else {
            qDebug() << "[Canvas] 没有可用的画布快照";
        }
    }
}

/*!
    \fn void Canvas::changeBrush(const QString &name)

    Changes current brush to \a name .
    At this time, we have only brushButton for Brush, pencilButton for Pencil, sketchButton for Sketch, and eraserButton for Eraser.
    \sa Brush, SketchBrush, Pencil, Eraser
*/

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
        // 为新创建的笔刷设置默认设置
        brush_->setSettings(brush_->defaultSettings());
        localBrush.insert(brushName, brush_);
        currentSettings = brush_->settings();
    }
    // share same color between brushes
    if(shareColor_){
        currentSettings["color"] = colorMap;
    }

    //    brush_->setLastPoint(lp);
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
        // share same color between brushes
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

/*!
    \fn void Canvas::drawLineTo(const QPoint &endPoint)

    Draws a line from last point to \a endPoint .
    The last point can be either determined by drawPoint() or by function itself.
    After drawing, it emits linePainted()

    \sa drawPoint() , linePainted()
*/

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
    // guarantee that pressure is double
    point.insert("pressure", double(pressure));

    storeAction(point);
}

/*!
    \fn void Canvas::drawPoint(const QPoint &point)

    Draws a point at \a endPoint.
    After drawing, it emits pointPainted()
    \sa drawPoint() , pointPainted()
*/

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
    // guarantee that pressure is double
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
    store.insert("clientid", clientSocket_->getClientIdOfCurrentRoom());
    store.insert("name", clientSocket_->nickname());
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

/*!
    \fn void Canvas::remoteDrawPoint(const QPoint &point, const QVariantMap &brushInfo,
                             const QString &layer,
                             const quint64 userid)

    Draws a remote point at \a point at \a layer with \a brushInfo.
    To identical user, \a userid must provided.
    \sa Canvas::remoteDrawLine()
*/

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

    cpd_brushInfo.remove("name"); // remove useless info

    if(remoteBrush.contains(clientid)){
        BrushPointer t = remoteBrush[clientid];
        if(brushName != t->name().toLower()){
            BrushPointer newOne = brushFactory(brushName);
            newOne->setSurface(l);
            newOne->setSettings(cpd_brushInfo);
            newOne->drawPoint(point, pressure);
            remoteBrush[clientid] = newOne;
            //            t.clear();
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

/*!
    \fn void Canvas::remoteDrawLine(const QPoint &start, const QPoint &end,
                            const QVariantMap &brushInfo,
                            const QString &layer,
                            const quint64 userid)

    Draws a remote line from \a start to \a end at \a layer with \a brushInfo .
    To identical user, \a userid must provided.
    \sa Canvas::remoteDrawLine()
*/

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
            //            t.clear();
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

/*!
    \fn void Canvas::remoteDrawBlock(const QVariantList &block,
                            const QVariantMap &brushInfo,
                            const QString &layer,
                            const QString clientid)

    Draws a remote block at \a layer with \a brushInfo and \a block data.
    To identical user, \a clientid must provided.
    \sa Canvas::remoteDrawPoint(), Canvas::remoteDrawLine()
*/

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
    cpd_brushInfo.remove("name"); // remove useless info

    // 获取或创建远程画笔
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

    // 设置画笔参数
    remoteBrushPtr->setSurface(l);
    remoteBrushPtr->setSettings(cpd_brushInfo);

    // 处理block数据
    if(block.length() >= 1) {
        // 处理第一个点
        QVariantMap firstPoint = block.first().toMap();
        QPoint point(firstPoint.value("x", 0).toInt(), firstPoint.value("y", 0).toInt());
        qreal pressure = firstPoint.contains("pressure") ? firstPoint.value("pressure").toDouble() : 1.0;

        remoteBrushPtr->drawPoint(point, pressure);

        // 处理后续的点，绘制线条
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

void Canvas::onMembersSorted(const QList<MS>& list)
{
    author_list_ = list;
    update();
}

/* Layer */

/*!
    \fn QString Canvas::currentLayer()

    Returns current drawing layer.
    In detail, it always returns the layer selected by user, even it is hided or locked.
*/

QString Canvas::currentLayer()
{
    return layers.selectedLayer()->name();
}

/*!
    \fn void Canvas::addLayer(const QString &name)

    Adds a new layer named \a name. The layer will append on the top.
    \sa deleteLayer()
*/

void Canvas::addLayer(const QString &name)
{
    layers.appendLayer(name);
    layerNameCounter++;
}

/*!
    \fn bool Canvas::deleteLayer(const QString &name)

    Deletes the layer named \a name. Returns false if failed.
    A locked layer cannot be deleted, while a hided one can.
    \sa addLayer()
*/

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
    emit requestClearMembers();
    layers.clearAllLayer();
    update();
}

/*!
    \fn void Canvas::lockLayer(const QString &name)

    Locks one layer named \a name.
    \sa unlockLayer()
*/

void Canvas::lockLayer(const QString &name)
{
    layers.layerFrom(name)->lock();
}

/*!
    \fn void Canvas::unlockLayer(const QString &name)

    Unlocks one layer named \a name.
    \sa lockLayer()
*/

void Canvas::unlockLayer(const QString &name)
{
    layers.layerFrom(name)->unlock();
}

/*!
    \fn void Canvas::hideLayer(const QString &name)

    Hides one layer named \a name.
    \sa showLayer()
*/

void Canvas::hideLayer(const QString &name)
{
    layers.layerFrom(name)->hide();
    update();
}

/*!
    \fn void Canvas::showLayer(const QString &name)

    Show one layer named \a name.
    \sa hideLayer()
*/

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

/*!
    \fn void Canvas::layerSelected(const QString &name)

    Selects the layer named \a name.

    This function is used when render on the screen.
*/

void Canvas::layerSelected(const QString &name)
{
    layers.select(name);
}

/* Event control */
void Canvas::tabletEvent(QTabletEvent *event)
{
    // qDebug() << "[Canvas] tabletEvent" << event->type() << event->deviceType() << event->pressure() << event->position() << event->buttons() << event->modifiers() << event->pointerType() << event->xTilt() << event->yTilt() << event->rotation() << event->tangentialPressure() << event->z() << event->xTilt() << event->yTilt() << event->rotation() << event->tangentialPressure() << event->z();
    if (!m_tabletEnabled)
        return;

    // 处理V3笔刷
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
                // V3笔刷：添加点到当前路径
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
                // V3笔刷：添加点到当前路径
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
                // V3笔刷：结束笔画
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

    // 处理V1笔刷（原有逻辑）
    switch(event->type()){
    case QEvent::TabletPress:
        if (event->deviceType() != QInputDevice::DeviceType::Stylus || qFuzzyCompare(event->pressure(), 0.0)) //sometimes press event will raise when pen is actually not pressed (such as a pen button is pressed), we don't need it.
            break;
        lastPoint = event->position().toPoint();
        switch(control_mode_) {
        case PICKING:
            pickColor(event->position().toPoint());
            break;
        case MOVING:
            break;
        default:
            // fall-through
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
            // FIXME: if we don't limit move events here, stack may overflow
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
            // fall-through
        case DRAWING:
            stackPoints.clear();
            updateCursor();
            sendAction();
            control_mode_ = NONE;
            // 新增：抬笔清空路径
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
            // fall-through
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
            // FIXME: if we don't limit move events here, stack may overflow
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
            // fall-through
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

static inline qint64 find_newest_active(const QList<CanvasBackend::MemberSection>& al)
{
    qint64 longest = 0;
    for(auto &item: al) {
        qint64 stamp = std::get<MSI::LastActiveStamp>(item);
        if(stamp > longest){
            longest = stamp;
        }
    }
    if(!longest){
        longest = QDateTime::currentMSecsSinceEpoch();
    }
    return longest;
}

void Canvas::drawAuthorTips(QPainter& painter,
                            const QPoint& pos,
                            const QString& name)
{
    QFontMetrics fm(this->font());
    int t_width = fm.horizontalAdvance(name)+15;
    int t_height = fm.height()+15;
    QRect box_rect(pos.x(), pos.y(), t_width, t_height);

    painter.save();
    QPen pen(Qt::transparent);
    QBrush brush(Qt::black);
    painter.setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing, true);
    painter.setOpacity(0.6);
    painter.setPen(pen);
    painter.setBrush(brush);
    painter.drawRoundedRect(box_rect, 10, 10);

    pen.setColor(Qt::white);
    painter.setPen(pen);
    painter.drawText(box_rect, Qt::AlignCenter, name);
    painter.restore();
}

// 画笔抬起时将内容完整绘制到图层上
void Canvas::updateBrushV3StrokesOnDone()
{
    if(!isCurrentBrushV3()){
        return;
    }
    brushV3_->clearAllPaths();
}

// 画笔拖动过程中将未画完的轨迹画到临时层上
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

    // 先渲染所有图层
    layers.combineLayers(&image, dirtyRect);
    painter.drawImage(dirtyRect, image, dirtyRect);

    // filter outdated names.
    // Considering using another QImage instead of direct draw
    qint64 longest = find_newest_active(author_list_);
    for(auto& item: author_list_){
        QPoint point = std::get<MSI::Footprint>(item);
        QString name = std::get<MSI::Name>(item);
        qint64 stamp = std::get<MSI::LastActiveStamp>(item);
        if(name.isEmpty()){
            name = std::get<MSI::Id>(item);
        }
        if(longest - stamp > 1000*30){
            continue;
        }
        if(point.isNull()){
            break;
        }

        drawAuthorTips(painter, point, name);
    }

    if(archive_loading_){
        // 绘制loading遮罩
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
    // NOTE: only to stop unexpected resize
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

/*!
    \fn QSize Canvas::sizeHint() const

    Returns a pre-defined size for canvas.

    \sa QWidget::minimumSizeHint()
*/

QSize Canvas::sizeHint() const
{
    return canvasSize;
}

/*!
    \fn QSize Canvas::minimumSizeHint() const

    Returns a pre-defined minimal size for canvas.

    \sa sizeHint()
*/

QSize Canvas::minimumSizeHint() const
{
    return canvasSize;
}
