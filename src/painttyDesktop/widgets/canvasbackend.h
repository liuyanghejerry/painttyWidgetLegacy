#ifndef CANVASBACKEND_H
#define CANVASBACKEND_H

#include <QObject>
#include <QHash>
#include <QQueue>
#include <QJsonObject>
#include <QImage>
#include <QVariantList>
#include <QByteArray>
#include <QPoint>
#include <QTimer>
#include <QSharedPointer>
#include "../misc/archivefile.h"

// 定义缓冲区结构体
struct DrawEvent {
    enum Type { Point, Line, Block };
    Type type;
    QPoint startPoint;
    QPoint endPoint;
    QVariantMap brushInfo;
    QString layer;
    QString clientid;
    qreal pressure;
    QVariantList blockData; // 新增：用于存储block数据
    
    DrawEvent(Type t, const QPoint &start, const QPoint &end,
              const QVariantMap &brush, const QString &l,
              const QString &cid, qreal p = 1.0)
        : type(t), startPoint(start), endPoint(end),
          brushInfo(brush), layer(l), clientid(cid), pressure(p) {}
    
    // 新增：用于创建Block类型事件的构造函数
    DrawEvent(const QVariantList &block, const QVariantMap &brush, 
              const QString &l, const QString &cid)
        : type(Block), startPoint(), endPoint(),
          brushInfo(brush), layer(l), clientid(cid), pressure(1.0), 
          blockData(block) {}
};

class CanvasBackend : public QObject
{
    Q_OBJECT
public:
    explicit CanvasBackend(QObject *parent = nullptr);
    ~CanvasBackend();

    // 客户端身份管理
    void setLocalClientId(const QString& clientId);
    QString localClientId() const;

    // ArchiveFile管理方法
    void initializeArchiveFile(const QString &roomName);
    QSharedPointer<ArchiveFile> getCurrentArchiveFile() const;
    QString getArchiveSignature() const;
    quint64 getArchiveLineCount() const;
    
    // 数据保存方法
    void saveDrawData(const QJsonObject& data);
    void saveArchiveData(const QJsonObject& data);
    
    // 画布快照管理方法
    void saveCanvasSnapshot(const QList<QImage> &layerImages);
    QList<QImage> loadCanvasSnapshot() const;
    bool hasCanvasSnapshot() const;
    
    void onCanvasLayersSaved(const QList<QImage> &layerImages);

public slots:
    void onDataBlock(const QVariantMap info);
    void onIncomingData(const QJsonObject &d);
    void pauseParse();
    void resumeParse();

signals:
    void remoteDrawPoint(const QPoint &point,
                         const QVariantMap &brushInfo,
                         const QString &layer,
                         const QString clientid,
                         const qreal pressure=1.0);
    void remoteDrawLine(const QPoint &start,
                        const QPoint &end,
                        const QVariantMap &brushInfo,
                        const QString &layer,
                        const QString clientid,
                        const qreal pressure=1.0);
    void remoteDrawBlock(const QVariantList &block,
                        const QVariantMap &brushInfo,
                        const QString &layer,
                        const QString clientid);
    void repaintHint();
    void archiveRenderFinished();
    void archiveLoadingStarted();
    void cachedCanvasLoaded();
    void canvasSaveCompleted();
    void canvasSnapshotRestored();
    
    // ArchiveFile状态变化信号
    void archiveSignatureChanged(const QString& signature);
    void archiveLineCountChanged(quint64 lineCount);

private:
    QString cached_clientid_;
    bool archive_loaded_;
    bool is_parsed_signal_sent;
    bool pause_;
    bool archive_loading_;
    QString current_room_name_;
    QTimer* parse_timer_;
    
    // ArchiveFile管理
    QSharedPointer<ArchiveFile> currentArchiveFile_;
    
    QByteArray toJson(const QVariant &m);
    QVariant fromJson(const QByteArray &d);
    void processIncomingData(const QJsonObject& data);
    void processBlockData(const QVariantMap& m);
    
    // ArchiveFile管理私有方法
    void initializeArchiveFileInternal(const QString& roomName);
    void connectArchiveFileSignals();
    void disconnectArchiveFileSignals();
};

#endif // CANVASBACKEND_H
