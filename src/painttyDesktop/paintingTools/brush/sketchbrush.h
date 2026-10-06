#ifndef SKETCHBRUSH_H
#define SKETCHBRUSH_H

#include "abstractbrush.h"
#include <QList>
#include <QPen>

class SketchBrush : public AbstractBrush
{
public:
    SketchBrush();
    void setColor(const QColor& c) Q_DECL_OVERRIDE;
    void drawPoint(const QPoint& p) Q_DECL_OVERRIDE;
    void drawLineTo(const QPoint& end) Q_DECL_OVERRIDE;
    AbstractBrush* createBrush() Q_DECL_OVERRIDE;
    void setSettings(const BrushSettings &settings) Q_DECL_OVERRIDE;
    BrushSettings defaultSettings() const Q_DECL_OVERRIDE;
protected:
    void preparePen();
    QPen sketchPen;
    QList<QPoint> points;
    void sketch();
};

#endif // SKETCHBRUSH_H
