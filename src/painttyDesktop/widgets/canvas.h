#ifndef CANVAS_H
#define CANVAS_H

#include <QWidget>
#include "../paintingTools/brush/abstractbrush.h"
#include "../paintingTools/brush/abstractbrushv3.h"
#include "../misc/layermanager.h"
#include "canvasbackend.h"

typedef QSharedPointer<AbstractBrush> BrushPointer;
typedef QSharedPointer<AbstractBrushV3> BrushPointerV3;

// 辅助函数：判断当前是否为V3笔刷
inline bool isV3Brush(const BrushPointerV3 &brush) {
    return brush != nullptr;
}

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
    int jitterCorrectionLevel() const;
    bool isJitterCorrectionEnabled() const;
    bool tabletEnabled() const {return m_tabletEnabled; }
    bool isArchiveLoading() const { return archive_loading_; }

    virtual QSize sizeHint () const;
    virtual QSize minimumSizeHint () const;

public slots:
    void setTabletEnabled(bool enabled) { m_tabletEnabled = enabled; }
    void setJitterCorrectionEnabled(bool correct);
    void setJitterCorrectionLevel(int value);
    void setShareColor(bool b);
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
    void changeBrush(const QString &name);
    void onColorPicker(bool in);
    void onMoveTool(bool in);
    QList<QImage> layerImages() const;
    void pause();

    void initCanvasWithArchive(const QString &roomName);
    void saveCanvasToCache();
    void loadCanvasFromCache();
    void setArchiveLoading(bool loading);

    void exportCanvasSnapshot();
    void restoreCanvasFromSnapshot();

    void changeBrushV3(const QString &name);
    BrushPointerV3 brushV3Factory(const QString &name);
    bool isCurrentBrushV3() const;

signals:
    void contentMovedBy(const QPoint&);
    void canvasToolComplete();
    void newBrushSettings(const QVariantMap &map);
    void historyComplete();
    void newPaintAction(const QVariantMap m);
    void canvasExported(const QPixmap& pic);
    void parsePaused();
protected:
    void mousePressEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);
    void paintEvent(QPaintEvent *event);
    void resizeEvent(QResizeEvent *event);
    void tabletEvent(QTabletEvent *event);
    void focusInEvent(QFocusEvent * event);
    void focusOutEvent(QFocusEvent * event);

private slots:
    void onArchiveLoadingStarted();
    void onArchiveRenderFinished();
    void remoteDrawPoint(const QPoint &point,
                         const QVariantMap &brushSettings,
                         const QString &layer,
                         const QString clientid,
                         const qreal pressure=1.0);
    void remoteDrawLine(const QPoint &start,
                        const QPoint &end,
                        const QVariantMap &brushSettings,
                        const QString &layer,
                        const QString clientid,
                        const qreal pressure=1.0);
    void remoteDrawBlock(const QVariantList &block,
                         const QVariantMap &brushSettings,
                         const QString &layer,
                         const QString clientid);

private:
    void drawLineTo(const QPoint &endPoint, qreal pressure=1.0);
    void drawPoint(const QPoint &point, qreal pressure=1.0);
    void storeAction(const QVariantMap& map);
    void sendAction();
    void pickColor(const QPoint &point);
    void updateCursor();
    void tryJitterCorrection();
    QImage appendAuthorSignature(QImage target);
    BrushPointer brushFactory(const QString &name);
    void setBrushFeature(const QString& key, const QVariant& value);
    void updateBrushV3StrokesOnGoing();
    void updateBrushV3StrokesOnDone();

    enum CONTROL_MODE {
        UNKNOWN = -1,
        NONE = 0,
        PICKING,
        DRAWING,
        MOVING
    };

    bool m_tabletEnabled;
    CONTROL_MODE control_mode_;
    QSize canvasSize;
    LayerManager layers;
    QImage image;
    QImage *currentImage;
    QPoint lastPoint;
    QList<QPoint> stackPoints;
    int layerNameCounter;
    BrushPointer brush_;
    BrushPointerV3 brushV3_;
    bool useV3Brush_;
    bool shareColor_;
    bool jitterCorrection_;
    int jitterCorrectionLevel_;
    qreal jitterCorrectionLevel_internal_;
    QHash<QString, BrushPointer> remoteBrush;
    QHash<QString, BrushPointer> localBrush;
    CanvasBackend* backend_;
    QThread *worker_;
    QVariantList action_buffer_;
    bool archive_loading_;
};

#endif // CANVAS_H
