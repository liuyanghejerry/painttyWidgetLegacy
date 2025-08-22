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
#include "../common/network/sse-clientsocket.h"

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
    // id, name and count, where count can be use to sort
    typedef std::tuple<QString, QString, quint64, QPoint, qint64> MemberSection;
    enum MemberSectionIndex {
        Id = 0,
        Name,
        Count,
        Footprint,
        LastActiveStamp
    };

    explicit CanvasBackend(QObject *parent = nullptr);
    ~CanvasBackend();
public slots:
    void initInThread(SSEClientSocket *clientSocket);
    void onDataBlock(const QVariantMap d);
    void onIncomingData(const QJsonObject &d);
    void requestMembers(MemberSectionIndex index);
    void clearMembers();
    void pauseParse();
    void resumeParse();
    void onNewClientId(const QString& clientId);
    void initializeArchiveFile(const QString &roomName);
    void onArchiveData(const QJsonObject &archiveData);
    void onArchiveEnd();
    void onCanvasLayersSaved(const QList<QImage> &layerImages);
    
    // 新增：ArchiveFile管理方法
    void processLocalArchive(const QString &roomName, const QString &remoteArchiveSign);
    void startRemoteArchiveProcessing();
    void finishRemoteArchiveProcessing();
    
    // 新增：数据保存方法
    void saveDrawData(const QJsonObject& data);
    void saveArchiveData(const QJsonObject& data);
    
    // 新增：ArchiveFile获取方法
    QSharedPointer<ArchiveFile> getCurrentArchiveFile() const;
    QString getArchiveSignature() const;
    quint64 getArchiveLineCount() const;
    
    // 新增：房间管理方法
    void setCurrentRoomName(const QString& roomName);
    QString getCurrentRoomName() const;
    
    // 新增：画布快照管理方法
    void saveCanvasSnapshot(const QList<QImage> &layerImages);
    QList<QImage> loadCanvasSnapshot() const;
    bool hasCanvasSnapshot() const;

signals:
    void newDataGroup(const QByteArray& d);
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
    void membersSorted(QList<MemberSection> list);
    void archiveRenderFinished();
    void archiveLoadingStarted();
    void localArchiveRenderFinished(); // 新增：本地archive渲染完成
    void remoteArchiveRenderFinished(); // 新增：远程archive渲染完成
    void localArchiveLoadingStarted(); // 新增：本地archive加载开始
    void remoteArchiveLoadingStarted(); // 新增：远程archive加载开始
    void sendDataPack(const QByteArray& d);
    void cachedCanvasLoaded();
    void canvasSaveCompleted();
    void localHistoryProcessed(const QString &roomName, const QString &remoteArchiveSign);
    
    // 新增：ArchiveFile状态变化信号
    void archiveSignatureChanged(const QString& signature);
    void archiveLineCountChanged(quint64 lineCount);
    
    // 新增：画布快照恢复信号
    void canvasSnapshotRestored();
    
    // 新增：请求建立SSE连接信号
    void requestEstablishSSEConnection(const QString& roomName, const QString& nickname, const QString& remoteArchiveSign);

private:
    // Warning, access memberHistory_ across thread
    // via member functions is not thread-safe
    QHash<QString, MemberSection> memberHistory_;
    QString cached_clientid_;
    bool archive_loaded_;
    bool is_parsed_signal_sent;
    bool pause_;
    bool archive_loading_;
    bool archive_data_fully_processed_; // 新增：标记archive数据是否已完全处理
    bool sse_archive_in_progress_; // 新增：标记SSE archive数据是否正在处理
    QString current_room_name_;
    bool data_sync_in_progress_;
    bool sse_connection_established_; // 新增：标记SSE连接是否已建立
    QTimer* parse_timer_;
    SSEClientSocket* clientSocket_ = nullptr;
    
    // 新增：ArchiveFile管理
    QSharedPointer<ArchiveFile> currentArchiveFile_;
    
    void upsertFootprint(const QString& id, const QString& name, const QPoint &point);
    void upsertFootprint(const QString& id, const QString& name);
    QByteArray toJson(const QVariant &m);
    QVariant fromJson(const QByteArray &d);
    void processIncomingData(const QJsonObject& data);
    void processBlockData(const QVariantMap& m);
    
    // 新增：ArchiveFile管理私有方法
    void initializeArchiveFileInternal(const QString& roomName);
    void connectArchiveFileSignals();
    void disconnectArchiveFileSignals();
};

typedef CanvasBackend::MemberSection MS;
typedef CanvasBackend::MemberSectionIndex MSI;

#endif // CANVASBACKEND_H
