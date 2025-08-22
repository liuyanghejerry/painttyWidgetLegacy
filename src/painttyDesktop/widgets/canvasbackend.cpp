#include "canvasbackend.h"
#include "../common/common.h"
#include "../common/network/sse-clientsocket.h"
#include "../common/room-info-manager.h"
#include "../misc/singleton.h"

#include <algorithm>
#include <QTimerEvent>
#include <QDateTime>
#include <QJsonDocument>
#include <QSettings>
#include <QThread>


CanvasBackend::CanvasBackend(QObject *parent)
    : QObject(parent),
      archive_loaded_(false),
      is_parsed_signal_sent(false),
      pause_(false),
      archive_loading_(false),
      archive_data_fully_processed_(false),
      sse_archive_in_progress_(false),
      data_sync_in_progress_(false),
      sse_connection_established_(false),
      parse_timer_(nullptr),
      clientSocket_(nullptr)
{
    // 初始化将在initInThread方法中进行
}

void CanvasBackend::initInThread(SSEClientSocket *clientSocket) {
    clientSocket_ = clientSocket;
    sse_connection_established_ = false; // 重置SSE连接标志
    connect(clientSocket_, &SSEClientSocket::newClientId, this,
            &CanvasBackend::onNewClientId, Qt::QueuedConnection);
    connect(clientSocket_, &SSEClientSocket::newDrawData, this,
            &CanvasBackend::onIncomingData, Qt::QueuedConnection);
    connect(clientSocket_, &SSEClientSocket::archiveData, this,
            &CanvasBackend::onArchiveData, Qt::QueuedConnection);
    connect(clientSocket_, &SSEClientSocket::archiveEnd, this,
            &CanvasBackend::onArchiveEnd, Qt::QueuedConnection);
    connect(this, &CanvasBackend::newDataGroup, clientSocket_,
            static_cast<void (SSEClientSocket::*)(const QByteArray &)>(
                &SSEClientSocket::sendDataPack), Qt::QueuedConnection);
    
    // 新增：连接SSE连接请求信号
    connect(this, &CanvasBackend::requestEstablishSSEConnection, clientSocket_,
            &SSEClientSocket::establishSSEConnection, Qt::QueuedConnection);
    
    // 新增：连接数据保存请求信号
    connect(clientSocket_, &SSEClientSocket::requestSaveDrawData, this,
            &CanvasBackend::saveDrawData, Qt::QueuedConnection);
    connect(clientSocket_, &SSEClientSocket::requestSaveArchiveData, this,
            &CanvasBackend::saveArchiveData, Qt::QueuedConnection);
}

CanvasBackend::~CanvasBackend()
{
    this->disconnect();
    
    if(parse_timer_) {
        parse_timer_->stop();
        delete parse_timer_;
        parse_timer_ = nullptr;
    }
    
    // 断开ArchiveFile信号连接
    disconnectArchiveFileSignals();
    if (archive_loaded_ && currentArchiveFile_) {
        currentArchiveFile_->flush();
    }
}

void CanvasBackend::pauseParse()
{
    pause_ = true;
}

void CanvasBackend::resumeParse()
{
    pause_ = false;
}

// 新增：设置当前房间名称
void CanvasBackend::setCurrentRoomName(const QString& roomName)
{
    current_room_name_ = roomName;
}

// 新增：获取当前房间名称
QString CanvasBackend::getCurrentRoomName() const
{
    return current_room_name_;
}

// 新增：获取当前ArchiveFile
QSharedPointer<ArchiveFile> CanvasBackend::getCurrentArchiveFile() const
{
    return currentArchiveFile_;
}

// 新增：获取Archive签名
QString CanvasBackend::getArchiveSignature() const
{
    if (currentArchiveFile_) {
        return currentArchiveFile_->signature();
    }
    return QString();
}

// 新增：获取Archive行数
quint64 CanvasBackend::getArchiveLineCount() const
{
    if (currentArchiveFile_) {
        return currentArchiveFile_->lineCount();
    }
    return 0;
}

// 新增：保存绘图数据
void CanvasBackend::saveDrawData(const QJsonObject& data)
{
    if (!currentArchiveFile_) {
        qWarning() << "[CanvasBackend] ArchiveFile未初始化，无法保存绘图数据";
        return;
    }
    
    QByteArray jsonData = QJsonDocument(data).toJson(QJsonDocument::Compact);
    currentArchiveFile_->appendData(jsonData);
    qDebug() << "[CanvasBackend] 保存绘图数据，当前行数:" << currentArchiveFile_->lineCount();
}

// 新增：保存Archive数据
void CanvasBackend::saveArchiveData(const QJsonObject& data)
{
    if (!currentArchiveFile_) {
        qWarning() << "[CanvasBackend] ArchiveFile未初始化，无法保存archive数据";
        return;
    }
    
    QByteArray jsonData = QJsonDocument(data).toJson(QJsonDocument::Compact);
    currentArchiveFile_->appendArchiveData(jsonData);
    qDebug() << "[CanvasBackend] 保存archive数据，当前行数:" << currentArchiveFile_->lineCount();
}

// 新增：初始化ArchiveFile内部方法
void CanvasBackend::initializeArchiveFileInternal(const QString& roomName)
{
    qDebug() << "[CanvasBackend] 初始化ArchiveFile，房间:" << roomName;
    
    // 断开之前的ArchiveFile信号连接
    disconnectArchiveFileSignals();
    
    // 从RoomInfoManager获取ArchiveFile
    auto archiveFile = RoomInfoManager::instance().getCurrentArchiveFile(roomName);
    if (archiveFile) {
        currentArchiveFile_ = archiveFile;
        qDebug() << "[CanvasBackend] 从RoomInfoManager获取ArchiveFile成功，房间:" << roomName;
    } else {
        qWarning() << "[CanvasBackend] 从RoomInfoManager获取ArchiveFile失败，创建新的，房间:" << roomName;
        
        // 如果RoomInfoManager中没有，创建新的ArchiveFile
        currentArchiveFile_ = QSharedPointer<ArchiveFile>::create(roomName, QString());
        
        // 将新创建的ArchiveFile添加到RoomInfoManager
        auto roomInfo = RoomInfoManager::instance().getRoomInfo(roomName);
        if (roomInfo) {
            roomInfo->archiveFile = currentArchiveFile_;
            RoomInfoManager::instance().updateRoomInfo(roomName, roomInfo);
        }
    }
    
    // 连接ArchiveFile的信号
    connectArchiveFileSignals();
}

// 新增：连接ArchiveFile信号
void CanvasBackend::connectArchiveFileSignals()
{
    if (currentArchiveFile_) {
        connect(currentArchiveFile_.data(), &ArchiveFile::newSignature,
                [this](const QString &signature) {
            qDebug() << "[CanvasBackend] ArchiveFile签名更新:" << signature;
            emit archiveSignatureChanged(signature);
        });
        
        connect(currentArchiveFile_.data(), &ArchiveFile::lineCountChanged,
                [this](quint64 lineCount) {
            qDebug() << "[CanvasBackend] ArchiveFile行数更新:" << lineCount;
            emit archiveLineCountChanged(lineCount);
        });
    }
}

// 新增：断开ArchiveFile信号连接
void CanvasBackend::disconnectArchiveFileSignals()
{
    if (currentArchiveFile_) {
        disconnect(currentArchiveFile_.data(), nullptr, this, nullptr);
    }
}

void CanvasBackend::initializeArchiveFile(const QString &roomName)
{
    current_room_name_ = roomName;
    qDebug() << "[CanvasBackend] 初始化房间名称:" << roomName;
    
    // 重置信号发送标志
    is_parsed_signal_sent = false;

    // 初始化ArchiveFile
    initializeArchiveFileInternal(roomName);

    if (currentArchiveFile_ && currentArchiveFile_->lineCount() > 0) {
      // 设置archive加载状态
      archive_loading_ = true;
      emit archiveLoadingStarted();
      qDebug() << "[CanvasBackend] 开始本地archive重放，行数:"
               << currentArchiveFile_->lineCount();
      qDebug() << "[CanvasBackend] 文件大小:" << currentArchiveFile_->size() << "字节";

      auto lines = currentArchiveFile_->readAllLines();
      qDebug() << "[CanvasBackend] 实际读取的行数:" << lines.size();

      for (const auto &line : lines) {
        qDebug() << "[CanvasBackend] 处理行数据，长度:" << line.size()
                 << "字节";
        QJsonDocument doc = QJsonDocument::fromJson(line);
        if (doc.isObject()) {
          QJsonObject obj = doc.object();
          processIncomingData(obj);
        } else {
          qWarning() << "[CanvasBackend] 解析JSON失败，行数据:" << line;
        }
      }

      // 重置archive加载状态
      archive_loading_ = false;

      // 只有在没有SSE archive处理时才立即发送完成信号
      if (!sse_archive_in_progress_) {
        emit archiveRenderFinished();
        qDebug() << "[CanvasBackend] 本地archive重放完成，共" << lines.size()
                 << "条";
      } else {
        qDebug() << "[CanvasBackend] 本地archive重放完成，但SSE "
                    "archive正在处理中，等待SSE完成";
      }

      // 发出本地历史数据处理完成信号
      emit localHistoryProcessed(current_room_name_, QString());
    }
    else {
      qDebug() << "[CanvasBackend] 没有找到本地archive文件或文件为空";
      // 只有在没有SSE archive处理时才立即发送完成信号
      if (!sse_archive_in_progress_) {
        emit archiveRenderFinished();
      } else {
        qDebug() << "[CanvasBackend] 本地archive为空，但SSE "
                    "archive正在处理中，等待SSE完成";
      }
    }
}

// 新增：改进的archive处理流程
void CanvasBackend::processLocalArchive(const QString &roomName, const QString &remoteArchiveSign)
{
    current_room_name_ = roomName;
    qDebug() << "[CanvasBackend] 处理本地archive，房间:" << roomName << "远程签名:" << remoteArchiveSign;
    
    // 重置信号发送标志
    is_parsed_signal_sent = false;

    // 初始化ArchiveFile
    initializeArchiveFileInternal(roomName);
    
    // 设置远程archive签名（即使没有本地数据，也要设置签名）
    if (currentArchiveFile_ && !remoteArchiveSign.isEmpty()) {
        currentArchiveFile_->setSignature(remoteArchiveSign);
        qDebug() << "[CanvasBackend] 设置远程archive签名:" << remoteArchiveSign;
    }
    
    QString localArchiveSign;
    if (currentArchiveFile_) {
        localArchiveSign = currentArchiveFile_->signature();
        qDebug() << "[CanvasBackend] 本地archive签名:" << localArchiveSign;
    }

    // 检查是否有可用的PNG快照
    bool hasSnapshot = hasCanvasSnapshot();
    qDebug() << "[CanvasBackend] 检查PNG快照可用性:" << hasSnapshot;

    // 比较本地和远程的archive签名
    if (currentArchiveFile_ && currentArchiveFile_->lineCount() > 0 && localArchiveSign == remoteArchiveSign) {
        // 签名一致，优先使用PNG快照
        if (hasSnapshot) {
            qDebug() << "[CanvasBackend] 本地和远程archive签名一致，且有PNG快照，使用快照恢复";
            
            // 发送本地archive加载开始信号
            emit localArchiveLoadingStarted();
            
            // 发送快照恢复信号（Canvas会处理快照恢复）
            emit canvasSnapshotRestored();
            
            // 发送本地archive渲染完成信号
            emit localArchiveRenderFinished();
            qDebug() << "[CanvasBackend] PNG快照恢复完成";
            
            // 发出本地历史数据处理完成信号
            emit localHistoryProcessed(roomName, remoteArchiveSign);
            
            // 直接建立SSE连接（仅在SSE连接未建立时）
            if (clientSocket_ && !sse_connection_established_) {
                QString nickname = clientSocket_->nickname();
                emit requestEstablishSSEConnection(roomName, nickname, remoteArchiveSign);
                sse_connection_established_ = true;
            }
        } else {
            // 没有PNG快照，使用传统的数据回放方式
            qDebug() << "[CanvasBackend] 本地和远程archive签名一致，但没有PNG快照，使用数据回放";
            
            // 设置archive加载状态，避免客户端ID过滤
            archive_loading_ = true;
            
            // 发送本地archive加载开始信号
            emit localArchiveLoadingStarted();
            
            auto lines = currentArchiveFile_->readAllLines();
            qDebug() << "[CanvasBackend] 开始本地archive重放，行数:" << lines.size();
            
            for (const auto& line : lines) {
                QJsonDocument doc = QJsonDocument::fromJson(line);
                if (doc.isObject()) {
                    QJsonObject obj = doc.object();
                    processIncomingData(obj);
                } else {
                    qWarning() << "[CanvasBackend] 解析JSON失败，行数据:" << line;
                }
            }
            
            // 重置archive加载状态
            archive_loading_ = false;
            
            // 发送本地archive渲染完成信号
            emit localArchiveRenderFinished();
            qDebug() << "[CanvasBackend] 本地archive重放完成，共" << lines.size() << "条";
            
            // 发出本地历史数据处理完成信号
            emit localHistoryProcessed(roomName, remoteArchiveSign);
            
            // 直接建立SSE连接（仅在SSE连接未建立时）
            if (clientSocket_ && !sse_connection_established_) {
                QString nickname = clientSocket_->nickname();
                emit requestEstablishSSEConnection(roomName, nickname, remoteArchiveSign);
                sse_connection_established_ = true;
            }
        }
    } else {
        // 签名不一致或本地archive为空，需要重建
        qDebug() << "[CanvasBackend] 本地和远程archive签名不一致或本地archive为空，需要重建";
        
        if (currentArchiveFile_) {
            // 清除本地archive文件和PNG快照
            currentArchiveFile_->remove();
            qDebug() << "[CanvasBackend] 已清除本地archive文件和PNG快照";
        }
        
        // 发送本地archive渲染完成信号（表示本地处理完成，等待远程数据）
        emit localArchiveRenderFinished();
        
        // 发出本地历史数据处理完成信号（即使没有本地数据，也表示本地处理阶段完成）
        emit localHistoryProcessed(roomName, remoteArchiveSign);
        
        // 直接建立SSE连接（仅在SSE连接未建立时）
        if (clientSocket_ && !sse_connection_established_) {
            QString nickname = clientSocket_->nickname();
            emit requestEstablishSSEConnection(roomName, nickname, remoteArchiveSign);
            sse_connection_established_ = true;
        }
    }
}

void CanvasBackend::startRemoteArchiveProcessing()
{
    qDebug() << "[CanvasBackend] 开始远程archive处理";
    
    // 设置SSE archive处理标志
    sse_archive_in_progress_ = true;
    
    // 发送远程archive加载开始信号
    emit remoteArchiveLoadingStarted();
}

void CanvasBackend::finishRemoteArchiveProcessing()
{
    qDebug() << "[CanvasBackend] 完成远程archive处理";
    
    archive_loaded_ = true;
    archive_loading_ = false;
    sse_archive_in_progress_ = false; // 重置SSE archive处理标志
    
    // 标记archive数据已完全处理
    archive_data_fully_processed_ = true;
    
    // 发送远程archive渲染完成信号
    if (!is_parsed_signal_sent) {
        emit remoteArchiveRenderFinished();
        is_parsed_signal_sent = true;
        qDebug() << "[CanvasBackend] 发送remoteArchiveRenderFinished信号";
        
        // 重连完成信号现在由SSEClientSocket在onSSEArchiveEnd中发射
    }
}

void CanvasBackend::onDataBlock(const QVariantMap info)
{
    QString author = info["name"].toString();
    QString clientid = info["clientid"].toString();
    upsertFootprint(clientid, author);

    auto data = toJson(QVariant(info));
    qDebug() << "[CanvasBackend] onDataBlock, data size:" << data.size() << "bytes";
    
    // 不在这里保存数据，让网络回显统一保存，避免重复
    // QJsonDocument doc = QJsonDocument::fromJson(data);
    // if (doc.isObject()) {
    //     saveDrawData(doc.object());
    // }
    
    emit newDataGroup(data);
}

void CanvasBackend::onIncomingData(const QJsonObject& obj)
{
    qDebug() << "[CanvasBackend] onIncomingData, object size:" << obj.size() << "keys";

    if(!pause_){
        processIncomingData(obj);
    }
}

void CanvasBackend::processIncomingData(const QJsonObject& data)
{
    // 添加线程安全检查
    if (QThread::currentThread() != this->thread()) {
        qWarning() << "[CanvasBackend] processIncomingData called from wrong thread:" << QThread::currentThread() << "expected:" << this->thread();
        return;
    }
    
    QString action = data.value("action").toString().toLower();
    if(action == "block"){
        processBlockData(data.toVariantMap());
    }
}

void CanvasBackend::processBlockData(const QVariantMap& m)
{
    QString clientid(m["clientid"].toString());
    
    // 对于archive数据，不进行客户端ID过滤，因为archive数据是历史数据
    // 只有在处理实时数据时才过滤自己的数据
    if (!archive_loading_ && clientid == cached_clientid_){
        return;
    }
    
    QVariantList list(m["block"].toList());
    if(list.length() < 1) {
        return;
    }

    QString layerName(m["layer"].toString());
    QVariantMap brushInfo(m["brush"].toMap());

    QString author;
    bool has_author = m.contains("name");
    if(has_author){
        author = m["name"].toString();
        // 获取最后一个点的位置用于footprint
        QVariantMap last_set(list.last().toMap());
        QPoint point(last_set.value("x", 0).toInt(), last_set.value("y", 0).toInt());
        upsertFootprint(clientid, author, point);
    }

    // 直接发送绘图信号，不使用缓冲区
    emit remoteDrawBlock(list, brushInfo, layerName, clientid);
    emit repaintHint();
}

void CanvasBackend::requestMembers(MSI index)
{
    //    qDebug()<<"Members requested!";
    typedef QList<MS> MSL;

    MSL&& list = memberHistory_.values();
    std::sort(list.begin(), list.end(), [index](const MS &e1,
          const MS &e2) {
        // Note, std::get<> never receive dynamic index.
        // And that's why we have to use switch :(
        switch (index){
        case Name:
            return std::get<Name>(e1) < std::get<Name>(e2);
            break;
        default: // fall-through
        case Count:
            return std::get<Count>(e1) < std::get<Count>(e2);
            break;
        }
    });

    emit membersSorted(list);
}

void CanvasBackend::clearMembers()
{
    memberHistory_.clear();
}

void CanvasBackend::upsertFootprint(const QString& id,
                                    const QString& name,
                                    const QPoint& point)
{
    qint64 stamp = QDateTime::currentMSecsSinceEpoch();
    if( memberHistory_.contains(id) ) {
        auto& member = memberHistory_[id];
        std::get<MSI::Count>( member )++;
        std::get<MSI::Footprint>( member ) = point;
        std::get<MSI::Name>( member ) = name;
        std::get<MSI::LastActiveStamp>( member ) = stamp;
    }else{
        memberHistory_.insert(id, MemberSection(id,
                                                name,
                                                1,
                                                point,
                                                stamp));
    }
}

void CanvasBackend::upsertFootprint(const QString& id,
                                    const QString& name)
{
    if( memberHistory_.contains(id) ) {
        auto& member = memberHistory_[id];
        std::get<MSI::Count>( member )++;
        std::get<MSI::Name>( member ) = name;
    }else{
        memberHistory_.insert(id, MemberSection(id,
                                                name,
                                                1,
                                                QPoint(),
                                                QDateTime::currentMSecsSinceEpoch()));
    }
}

QByteArray CanvasBackend::toJson(const QVariant &m)
{
#if (QT_VERSION >= QT_VERSION_CHECK(5, 1, 0))
    return QJsonDocument::fromVariant(m).toJson(QJsonDocument::Compact);
#else
    return QJsonDocument::fromVariant(m).toJson();
#endif
}

QVariant CanvasBackend::fromJson(const QByteArray &d)
{
    return QJsonDocument::fromJson(d).toVariant();
}

void CanvasBackend::onArchiveData(const QJsonObject& archiveData)
{
    qDebug() << "[CanvasBackend] onArchiveData:" << archiveData.size();
    
    // 设置archive加载状态 - 确保在第一次收到数据时就设置loading状态
    if (!archive_loading_) {
        archive_loading_ = true;
        is_parsed_signal_sent = false; // 重置信号发送标志
        emit archiveLoadingStarted();
        qDebug() << "[CanvasBackend] 开始加载archive数据，发送archiveLoadingStarted信号";
    }
    
    // 处理archiveSign（如果存在）
    if (archiveData.contains("archiveSign")) {
        QString archiveSign = archiveData["archiveSign"].toString();
        if (!archiveSign.isEmpty() && currentArchiveFile_) {
            currentArchiveFile_->setSignature(archiveSign);
            qDebug() << "[CanvasBackend] 设置ArchiveFile签名:" << archiveSign;
        }
    }
    
    // 保存archive数据到ArchiveFile
    saveArchiveData(archiveData);
    
    // 简化：直接处理数据，不使用缓冲区
    if (!pause_) {
        processIncomingData(archiveData);
    }
}

void CanvasBackend::onArchiveEnd()
{
    qDebug() << "[CanvasBackend] Archive data loading completed in thread:" << QThread::currentThread();
    
    // 调用新的完成方法
    finishRemoteArchiveProcessing();
}

void CanvasBackend::onNewClientId(const QString& clientId)
{
    cached_clientid_ = clientId;
}

void CanvasBackend::onCanvasLayersSaved(const QList<QImage> &layerImages)
{
    qDebug() << "[CanvasBackend] 画布快照保存完成";
    if (currentArchiveFile_) {
        QMetaObject::invokeMethod(currentArchiveFile_.data(), "saveSnapshotImages",
                                  Qt::QueuedConnection,
                                  Q_ARG(QList<QImage>, layerImages));
    }
}

// 新增：保存画布快照
void CanvasBackend::saveCanvasSnapshot(const QList<QImage> &layerImages)
{
    qDebug() << "[CanvasBackend] 开始保存画布快照";
    
    if (!currentArchiveFile_) {
        qWarning() << "[CanvasBackend] ArchiveFile未初始化，无法保存画布快照";
        return;
    }
    
    bool success = currentArchiveFile_->saveSnapshotImages(layerImages);
    if (success) {
        qDebug() << "[CanvasBackend] 画布快照保存成功，共" << layerImages.size() << "个图层";
    } else {
        qWarning() << "[CanvasBackend] 画布快照保存失败";
    }
}

// 新增：加载画布快照
QList<QImage> CanvasBackend::loadCanvasSnapshot() const
{
    qDebug() << "[CanvasBackend] 开始加载画布快照";
    
    if (!currentArchiveFile_) {
        qWarning() << "[CanvasBackend] ArchiveFile未初始化，无法加载画布快照";
        return QList<QImage>();
    }
    
    QList<QImage> images = currentArchiveFile_->loadSnapshotImages();
    qDebug() << "[CanvasBackend] 画布快照加载完成，共" << images.size() << "个图层";
    return images;
}

// 新增：检查是否有画布快照
bool CanvasBackend::hasCanvasSnapshot() const
{
    if (!currentArchiveFile_) {
        return false;
    }
    
    return currentArchiveFile_->hasCachedImages();
}