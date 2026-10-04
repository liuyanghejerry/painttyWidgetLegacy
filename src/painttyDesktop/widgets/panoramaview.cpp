#include "panoramaview.h"

#include <QPixmap>
#include <QResizeEvent>
#include <QPainter>
#include <QDebug>

PanoramaView::PanoramaView(QWidget *parent) :
    QWidget(parent),
    preferSize_(144, 96),
    image_( preferSize_ ),
    full_img_(preferSize_),
    sized_img_(preferSize_)
{
    QPalette p = this->palette();
    p.setColor(QPalette::Window, Qt::gray);
    this->setPalette(p);
    sized_img_.fill(Qt::white);
    full_img_.fill(Qt::white);
    image_.fill(Qt::white);
    timer_.setInterval(2*1000);
    connect(&timer_, &QTimer::timeout,
            this, &PanoramaView::refresh);
    timer_.start();
    emit refresh();
}

QSize PanoramaView::sizeHint() const
{
    return image_.deviceIndependentSize().toSize();
}

QSize PanoramaView::minimumSizeHint() const
{
    return QSize(144, 96);
}

void PanoramaView::onImageChange(const QPixmap &p,
                                   const QRect &r)
{
    full_img_ = p;
    viewport_ = r;
    thumbnail();
    image_ = drawViewport();
    update();
}

void PanoramaView::onRectChange(const QRect &r)
{
    viewport_ = r;
    image_ = drawViewport();
    update();
}

QPixmap PanoramaView::drawViewport()
{
    QPixmap p = sized_img_;
    if(p.isNull() || full_img_.isNull()){
        return p;
    }
    QPainter painter(&p);
    QPen pen;
    pen.setColor(this->palette().color(QPalette::Text));
    pen.setWidth(1);
    painter.setPen(pen);
    const QSizeF fullSize = full_img_.deviceIndependentSize();
    const QSizeF thumbnailSize = p.deviceIndependentSize();
    const qreal scaleX = thumbnailSize.width() / fullSize.width();
    const qreal scaleY = thumbnailSize.height() / fullSize.height();
    QRectF thumbRect(viewport_.x() * scaleX, viewport_.y() * scaleY,
                     viewport_.width() * scaleX, viewport_.height() * scaleY);
    // Keep the outline inside the thumbnail, in logical widget coordinates.
    thumbRect = thumbRect.intersected(QRectF(QPointF(), thumbnailSize).adjusted(0.5, 0.5, -0.5, -0.5));
    if (!thumbRect.isEmpty()) painter.drawRect(thumbRect);
    return p;
}

void PanoramaView::thumbnail()
{
    if(full_img_.isNull()){
        sized_img_ = QPixmap();
        return;
    }
    // QPixmap::scaled takes physical pixels; the widget size is logical.
    sized_img_ = full_img_.scaled(preferSize_ * full_img_.devicePixelRatio(),
                                  Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QRectF PanoramaView::thumbnailRect() const
{
    const QSizeF thumbnailSize = sized_img_.deviceIndependentSize();
    return QRectF(QPointF((width() - thumbnailSize.width()) / 2,
                         (height() - thumbnailSize.height()) / 2), thumbnailSize);
}

void PanoramaView::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.drawPixmap(thumbnailRect().topLeft(), image_);
}

void PanoramaView::resizeEvent(QResizeEvent * event)
{
    preferSize_ = event->size();
    if(image_.isNull()){
        return;
    }
    thumbnail();
    image_ = drawViewport();
    if(this->isVisible()){
        update();
    }
}

void PanoramaView::navigateTo(const QPoint &p)
{
    if (full_img_.isNull() || sized_img_.isNull()) return;
    const QRectF thumbnail = thumbnailRect();
    const QPointF miniPoint = QPointF(p) - thumbnail.topLeft();
    const QSizeF fullSize = full_img_.deviceIndependentSize();
    const QPointF realPoint(miniPoint.x() * fullSize.width() / thumbnail.width(),
                            miniPoint.y() * fullSize.height() / thumbnail.height());

    emit moveTo(realPoint);
}

void PanoramaView::mouseMoveEvent(QMouseEvent * event)
{
    if(event->buttons()){
        navigateTo(event->pos());
    }
}

void PanoramaView::mousePressEvent(QMouseEvent * event)
{
    navigateTo(event->pos());
}
