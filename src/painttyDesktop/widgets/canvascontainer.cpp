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
    setAlignment(Qt::AlignTop | Qt::AlignLeft);
    scene = new QGraphicsScene(this);
    setScene(scene);

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
        canvas->setWindowFlags(canvas->windowFlags() | Qt::Window);
    }
    proxy = scene->addWidget(canvas);
    
    // 确保 proxy 创建成功后再安装事件过滤器
    if (proxy && proxy->widget()) {
        canvas->installEventFilter(this);
        if (viewport()) {
            viewport()->installEventFilter(this); //at this time, viewport is available, so we intall event filter to send tablet event
        }
    } else {
        qWarning() << "CanvasContainer::setCanvas: failed to create proxy widget";
    }
}

void CanvasContainer::setScaleFactor(qreal factor)
{
    //new signal and slot syntax has some trouble
    //dealing with slots having default parameter
    setScaleFactorInternal(factor);
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

qreal CanvasContainer::calculateFactor(qreal current, bool zoomIn)
{
    if (current <= MIN_SCALE_FACTOR && !zoomIn)
        return MIN_SCALE_FACTOR;
    if (current >= MAX_SCALE_FACTOR && zoomIn)
        return MAX_SCALE_FACTOR;
    if (current < 1)
    {
        qreal power = qLn(current) / qLn(0.5);
        int newPower = qFloor(power);
        if (zoomIn)
        {
            if (qFuzzyCompare(qreal(newPower), power))
                newPower--;
            return qBound(MIN_SCALE_FACTOR, qPow(0.5, newPower), MAX_SCALE_FACTOR);
        }
        else
        {
            return qBound(MIN_SCALE_FACTOR, qPow(0.5, newPower + 1), MAX_SCALE_FACTOR);
        }
    }
    else
    {
        qreal newScale = qFloor(current);
        if (zoomIn)
        {
            return qBound(MIN_SCALE_FACTOR, newScale + 1, MAX_SCALE_FACTOR);
        }
        else
        {
            if (qFuzzyCompare(newScale, current))
                newScale--;
            if (qFuzzyCompare(newScale, 0.0))
                newScale = 0.5;
            return qBound(MIN_SCALE_FACTOR, newScale, MAX_SCALE_FACTOR);
        }

    }
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
    if (event->modifiers() & Qt::ControlModifier && proxy) //tablet pinch is ctrl+scrolling
    {
        setScaleFactorInternal(calculateFactor(proxy->scale(), event->angleDelta().y() > 0), event->position().toPoint());
        return;
    }
    if (event->modifiers() & Qt::AltModifier && proxy) //tablet pinch is alt+scrolling
    {
        QWheelEvent *event2 = new QWheelEvent(event->position(), event->globalPosition(), event->pixelDelta(),
                        event->angleDelta(), event->buttons(),
                        event->modifiers(), event->phase(), event->inverted());
        QGraphicsView::wheelEvent(event);
        delete(event2);
        return;
    }
    if (proxy && proxy->widget() && qobject_cast<Canvas*>(proxy->widget())->tabletEnabled()) //it seems that tablet pen scrolling is conflict with drawing, we disable it
        return;
    QGraphicsView::wheelEvent(event);
}

void CanvasContainer::mousePressEvent(QMouseEvent *event)
{
    if (proxy && proxy->widget() && qobject_cast<Canvas*>(proxy->widget())->tabletEnabled()) //it seems that tablet pen right click is conflict with drawing, we disable it
        return;
    if (event->button() == Qt::RightButton)
    {
        moveStartPoint = event->pos();
    }
    QGraphicsView::mousePressEvent(event);
}

void CanvasContainer::mouseMoveEvent(QMouseEvent *event)
{
    if (proxy && proxy->widget() && qobject_cast<Canvas*>(proxy->widget())->tabletEnabled())
        return;
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
    if (object == viewport()) //process tablet event
    {
        if (event->type() == QEvent::TabletPress
                || event->type() == QEvent::TabletMove
                || event->type() == QEvent::TabletRelease)
        {
            QTabletEvent *e = static_cast<QTabletEvent*>(event);
            QTabletEvent newEvent(e->type(),
                                  e->pointingDevice(),
                                  proxy->mapFromScene(mapToScene(e->position().toPoint())),
                                  e->globalPosition(),
                                  e->pressure(),
                                  e->xTilt(),
                                  e->yTilt(),
                                  e->tangentialPressure(),
                                  e->rotation(),
                                  e->z(),
                                  e->modifiers(),
                                  e->button(), e->buttons());
            QApplication::sendEvent(proxy->widget(), &newEvent);
            return true;
        }
    }
    return QGraphicsView::eventFilter(object, event);
}
