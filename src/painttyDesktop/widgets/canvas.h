#ifndef CANVAS_H
#define CANVAS_H

#include <QWidget>
#include "../paintingTools/brush/abstractbrush.h"
#include "../misc/layermanager.h"
#include "../misc/projectfile.h"

typedef QSharedPointer<AbstractBrush> BrushPointer;

class Canvas : public QWidget
{
    Q_OBJECT
public:
    explicit Canvas(QWidget *parent = 0);
    ~Canvas();

    QVariantMap brushSettings() const;
    BrushFeature brushFeatures() const;
    QString currentLayer();
    int count() const{return layers.count();}
    int layerNum() const{return layerNameCounter;}
    QImage currentCanvas();
    QImage allCanvas();
    QSize canvasSize() const { return canvasSize_; }
    void setCanvasSize(const QSize &size);
    PaintingProject projectState() const;
    void setProjectState(const PaintingProject &project);
    bool renameLayer(const QString &oldName, const QString &newName);
    bool canDraw() const;
    bool isDrawing() const { return control_mode_ == DRAWING; }

    virtual QSize sizeHint () const;
    virtual QSize minimumSizeHint () const;

public slots:
    void setShareColor(bool b);
    void finishStroke();
    void setBrushColor(const QColor &newColor);
    void setBrushWidth(int newWidth);
    void setBrushHardness(int h);
    void setBrushThickness(int t);
    void setBrushWater(int w);
    void setBrushExtend(int e);
    void setBrushMixin(int e);
    void setBrushSettings(const QVariantMap& settings);
    void addLayer(const QString &name);
    bool deleteLayer(const QString &name);
    void clearLayer(const QString &name);
    void clearAllLayer();
    void lockLayer(const QString &name);
    void unlockLayer(const QString &name);
    void hideLayer(const QString &name);
    void showLayer(const QString &name);
    void moveLayerUp(const QString &name);
    void moveLayerDown(const QString &name);
    void layerSelected(const QString &name);
    void setLayerContent(int index, const QImage &image);
    void changeBrush(const QString &name);
    void onColorPicker(bool in);
    void onMoveTool(bool in);
    QList<QImage> layerImages() const;

signals:
    void newBrushSettings(const QVariantMap &map);
    void documentChanged();
protected:
    void mousePressEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);
    void paintEvent(QPaintEvent *event);
    void resizeEvent(QResizeEvent *event);
    void focusInEvent(QFocusEvent * event);
    void focusOutEvent(QFocusEvent * event);

private:
    void drawLineTo(const QPoint &endPoint);
    void drawPoint(const QPoint &point);
    void pickColor(const QPoint &point);
    void updateCursor();
    BrushPointer brushFactory(const QString &name);
    void setBrushFeature(const QString& key, const QVariant& value);

    enum CONTROL_MODE {
        UNKNOWN = -1,
        NONE = 0,
        PICKING,
        DRAWING,
        MOVING
    };

    CONTROL_MODE control_mode_;
    QSize canvasSize_;
    LayerManager layers;
    QImage image;
    int layerNameCounter;
    BrushPointer brush_;
    bool shareColor_;
    QHash<QString, BrushPointer> localBrush;

};

#endif // CANVAS_H
