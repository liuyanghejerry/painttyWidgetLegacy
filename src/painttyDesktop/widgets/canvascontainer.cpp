#include <QGraphicsScene>
#include "canvascontainer.h"
#include "canvas.h"
#include <QGraphicsProxyWidget>
#include <QApplication>
#include <QScrollBar>
#include <QSlider>
#include <QLabel>
#include <QHBoxLayout>
#include <QSettings>
#include <qmath.h>
#include <QDebug>

#include "../common/common.h"

using GlobalDef::MAX_SCALE_FACTOR;
using GlobalDef::MIN_SCALE_FACTOR;

CanvasContainer::CanvasContainer(QWidget *parent) :
    QGraphicsView(parent), proxy(0),
    smoothScaleFlag(true)
{
    setCacheMode(QGraphicsView::CacheBackground);
    setBackgroundBrush(QColor(82, 82, 82));
    setAlignment(Qt::AlignCenter);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setDragMode(QGraphicsView::ScrollHandDrag);
    scene = new QGraphicsScene(this);
    setScene(scene);
    setSceneRect(0, 0, 720, 480); // default canvas size

    auto rc = [&](){
        emit rectChanged(visualRect().toRect());
    };
    connect(horizontalScrollBar(), &QScrollBar::valueChanged,
            rc);
    connect(verticalScrollBar(), &QScrollBar::valueChanged,
            rc);
}
CanvasContainer::~CanvasContainer()
{
}

void CanvasContainer::setCanvas(QWidget *canvas)
{
    if (!canvas) {
        qWarning() << "CanvasContainer::setCanvas: canvas is null";
        return;
    }
    
    if (canvas->parent())
    {
        canvas->setParent(0);
        canvas->setWindowFlags(Qt::Widget);
    }
    proxy = scene->addWidget(canvas);
    proxy->setMinimumSize(1, 1);
    proxy->setMaximumSize(10000, 10000);
    
    if (proxy && proxy->widget()) {
        canvas->installEventFilter(this);
        if (viewport()) {
            viewport()->installEventFilter(this);
        }
    } else {
        qWarning() << "CanvasContainer::setCanvas: failed to create proxy widget";
    }

    // Force scene rect update after proxy is created
    QTimer::singleShot(0, this, [this]() {
        if (proxy) {
            setSceneRect(scene->itemsBoundingRect());
        }
    });
}

void CanvasContainer::setScaleFactor(qreal factor)
{
    //new signal and slot syntax has some trouble
    //dealing with slots having default parameter
    setScaleFactorInternal(factor);
}

void CanvasContainer::resetView()
{
    if (!proxy)
        return;
    proxy->setScale(1.0);
    proxy->setRotation(0);
    setSceneRect(scene->itemsBoundingRect());
    QGraphicsView::centerOn(proxy);
    emit scaled(1.0);
    emit rotated(0);
    emit rectChanged(visualRect().toRect());
}

qreal CanvasContainer::currentScaleFactor() const
{
    if (proxy)
        return proxy->scale();
    return 0;
}

bool CanvasContainer::smoothScale() const
{
    return smoothScaleFlag;
}

void CanvasContainer::setSmoothScale(bool smooth)
{
    smoothScaleFlag = smooth;
    if (!smoothScaleFlag)
    {
        setRenderHint(QPainter::Antialiasing, false);
        setRenderHint(QPainter::SmoothPixmapTransform, false);
    }
}

void CanvasContainer::scaleBy(qreal factor)
{
    factor *= currentScaleFactor();
    setScaleFactorInternal(factor);
}
void CanvasContainer::setRotation(int degree)
{
    if (!proxy) {
        return;
    }
    
    degree = qBound(-180, degree, 180);
    if(int(proxy->rotation()) == degree){
        return;
    }
    QPointF position = proxy->mapFromScene(
                mapToScene(viewport()->rect().center()));
    if(proxy->rect().contains(position))
        proxy->setTransformOriginPoint(position);
    proxy->setRotation(degree);
    setSceneRect(scene->itemsBoundingRect());
    emit rotated(degree);
}

void CanvasContainer::rotateBy(int deg)
{
    if (!proxy) {
        return;
    }
    setRotation(int(proxy->rotation())+deg);
}

QRectF CanvasContainer::visualRect() const
{
    if (!proxy) {
        return QRectF();
    }
    return proxy->mapFromScene(
                mapToScene(viewport()->rect()))
            .boundingRect().intersected(proxy->rect());
}

void CanvasContainer::centerOn(const QPointF &pos)
{
    if (!proxy) {
        return;
    }
    QPointF point = proxy->mapToScene(pos);
    QGraphicsView::centerOn(point);
}

void CanvasContainer::centerOn(qreal x, qreal y)
{
    centerOn(QPoint(x, y));
}

void CanvasContainer::moveBy(const QPoint &p)
{
    auto v = qBound(horizontalScrollBar()->minimum(), horizontalScrollBar()->value() + p.x(), horizontalScrollBar()->maximum());
    horizontalScrollBar()->setValue(v);
    v = qBound(verticalScrollBar()->minimum(), verticalScrollBar()->value() + p.y(), verticalScrollBar()->maximum());
    verticalScrollBar()->setValue(v);
}

void CanvasContainer::setScaleFactorInternal(qreal factor, const QPoint scaleCenter)
{
    factor = qBound(MIN_SCALE_FACTOR, factor, MAX_SCALE_FACTOR);
    if(proxy){
        if (qFuzzyCompare(factor, proxy->scale()))
            return;
        if(smoothScaleFlag){
            if(factor < 1)
                setRenderHints(QPainter::Antialiasing
                               | QPainter::SmoothPixmapTransform);
            else{
                setRenderHint(QPainter::Antialiasing, false);
                setRenderHint(QPainter::SmoothPixmapTransform,
                              false);
            }
        }

        QPointF position = proxy->mapFromScene(
                    mapToScene(scaleCenter.isNull()? viewport()->rect().center() : scaleCenter));
        if(proxy->rect().contains(position))
            proxy->setTransformOriginPoint(position);
        proxy->setScale(factor);
        setSceneRect(scene->itemsBoundingRect());

        emit scaled(factor);
        emit rectChanged(visualRect().toRect());
    }
}

void CanvasContainer::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier && proxy)
    {
        const int delta = event->angleDelta().y() != 0 ? event->angleDelta().y() : event->pixelDelta().y();
        if (delta != 0)
            setScaleFactorInternal(proxy->scale() * (delta > 0 ? 1.2 : 1.0 / 1.2), event->position().toPoint());
        event->accept();
        return;
    }
    QGraphicsView::wheelEvent(event);
}

void CanvasContainer::mousePressEvent(QMouseEvent *event)
{
    if (panning_ && event->button() == Qt::LeftButton) {
        moveStartPoint = event->position().toPoint();
        panPressed_ = true;
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if (event->button() == Qt::RightButton) moveStartPoint = event->position().toPoint();
    QGraphicsView::mousePressEvent(event);
}

void CanvasContainer::mouseMoveEvent(QMouseEvent *event)
{
    if (panning_ && (event->buttons() & Qt::LeftButton)) {
        const QPoint position = event->position().toPoint();
        if (panPressed_) moveBy(moveStartPoint - position);
        moveStartPoint = position;
        panPressed_ = true;
        event->accept();
        return;
    }
    if (event->buttons() & Qt::RightButton)
    {
        moveBy(moveStartPoint - event->pos());
        moveStartPoint = event->pos();
    }
    QGraphicsView::mouseMoveEvent(event);
}

bool CanvasContainer::eventFilter(QObject *object, QEvent *event)
{
    // 添加空指针检查，防止段错误
    if (!proxy || !proxy->widget()) {
        return QGraphicsView::eventFilter(object, event);
    }
    
    if (object == proxy->widget()
            && event->type() == QEvent::CursorChange)
        proxy->setCursor(proxy->widget()->cursor());
    if (object == proxy->widget()
            && event->type() == QEvent::Resize) {
        setSceneRect(scene->itemsBoundingRect());
    }
    return QGraphicsView::eventFilter(object, event);
}

void CanvasContainer::setPanning(bool enabled)
{
    if (panning_ == enabled) return;
    panning_ = enabled;
    panPressed_ = false;
    if (enabled) viewport()->setCursor(Qt::OpenHandCursor);
    else viewport()->unsetCursor();
}

void CanvasContainer::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        panPressed_ = false;
        if (panning_) viewport()->setCursor(Qt::OpenHandCursor);
    }
    // Also release any scene grab left by a stroke that changed to the move tool.
    QGraphicsView::mouseReleaseEvent(event);
}
