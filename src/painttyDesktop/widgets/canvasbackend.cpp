#include "canvasbackend.h"
#include "../common/common.h"
#include "../misc/singleton.h"

#include <algorithm>
#include <QTimerEvent>
#include <QDateTime>
#include <QJsonDocument>
#include <QSettings>
#include <QThread>
#include <QUuid>


CanvasBackend::CanvasBackend(QObject *parent)
    : QObject(parent),
      archive_loaded_(false),
      is_parsed_signal_sent(false),
      pause_(false),
      archive_loading_(false),
      parse_timer_(nullptr)
{
    // 生成稳定的本地客户端ID（基于QSettings持久化）
    QSettings settings(GlobalDef::SETTINGS_NAME, QSettings::defaultFormat());
    cached_clientid_ = settings.value("local/clientId").toString();
    if (cached_clientid_.isEmpty()) {
        cached_clientid_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
        settings.setValue("local/clientId", cached_clientid_);
    }
}

void CanvasBackend::setLocalClientId(const QString& clientId)
{
    cached_clientid_ = clientId;
    QSettings settings(GlobalDef::SETTINGS_NAME, QSettings::defaultFormat());
    settings.setValue("local/clientId", clientId);
}

QString CanvasBackend::localClientId() const
{
    return cached_clientid_;
}

CanvasBackend::~CanvasBackend()
{
    this->disconnect();
    
    if(parse_timer_) {
        parse_timer_->stop();
        delete parse_timer_;
        parse_timer_ = nullptr;
    }
    
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

QSharedPointer<ArchiveFile> CanvasBackend::getCurrentArchiveFile() const
{
    return currentArchiveFile_;
}

QString CanvasBackend::getArchiveSignature() const
{
    if (currentArchiveFile_) {
        return currentArchiveFile_->signature();
    }
    return QString();
}

quint64 CanvasBackend::getArchiveLineCount() const
{
    if (currentArchiveFile_) {
        return currentArchiveFile_->lineCount();
    }
    return 0;
}

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

void CanvasBackend::initializeArchiveFileInternal(const QString& roomName)
{
    qDebug() << "[CanvasBackend] 初始化ArchiveFile，房间:" << roomName;
    
    disconnectArchiveFileSignals();
    
    // 直接创建ArchiveFile，不再依赖RoomInfoManager
    currentArchiveFile_ = QSharedPointer<ArchiveFile>::create(roomName, QString());
    qDebug() << "[CanvasBackend] 创建本地ArchiveFile成功，房间:" << roomName;
    
    connectArchiveFileSignals();
}

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
    
    is_parsed_signal_sent = false;

    initializeArchiveFileInternal(roomName);

    if (currentArchiveFile_ && currentArchiveFile_->lineCount() > 0) {
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

      archive_loading_ = false;
      emit archiveRenderFinished();
      qDebug() << "[CanvasBackend] 本地archive重放完成，共" << lines.size()
               << "条";
    }
    else {
      qDebug() << "[CanvasBackend] 没有找到本地archive文件或文件为空";
      emit archiveRenderFinished();
    }
}

void CanvasBackend::onDataBlock(const QVariantMap info)
{
    auto data = toJson(QVariant(info));
    qDebug() << "[CanvasBackend] onDataBlock, data size:" << data.size() << "bytes";
    
    // 保存到本地ArchiveFile
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isObject()) {
        saveDrawData(doc.object());
    }
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
    
    // 处理实时数据时过滤掉自己的数据（自己的绘图由Canvas直接绘制）
    if (!archive_loading_ && clientid == cached_clientid_){
        return;
    }
    
    QVariantList list(m["block"].toList());
    if(list.length() < 1) {
        return;
    }

    QString layerName(m["layer"].toString());
    QVariantMap brushInfo(m["brush"].toMap());

    emit remoteDrawBlock(list, brushInfo, layerName, clientid);
    emit repaintHint();
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

void CanvasBackend::onCanvasLayersSaved(const QList<QImage> &layerImages)
{
    qDebug() << "[CanvasBackend] 画布快照保存完成";
    if (currentArchiveFile_) {
        QMetaObject::invokeMethod(currentArchiveFile_.data(), "saveSnapshotImages",
                                  Qt::QueuedConnection,
                                  Q_ARG(QList<QImage>, layerImages));
    }
}

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

bool CanvasBackend::hasCanvasSnapshot() const
{
    if (!currentArchiveFile_) {
        return false;
    }
    
    return currentArchiveFile_->hasCachedImages();
}
