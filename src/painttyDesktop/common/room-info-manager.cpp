#include "room-info-manager.h"
#include <QDebug>
#include <QMutexLocker>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "../common/common.h"
#include "crypto.h"

// 静态成员初始化
RoomInfoManager* RoomInfoManager::instance_ = nullptr;
QMutex RoomInfoManager::instanceMutex_;

RoomInfoManager::RoomInfoManager(QObject *parent)
    : QObject(parent)
{
    qDebug() << "[RoomInfoManager] 初始化房间信息管理器";
}

RoomInfoManager::~RoomInfoManager()
{
    qDebug() << "[RoomInfoManager] 销毁房间信息管理器";
    clearAllRoomInfo();
}

RoomInfoManager& RoomInfoManager::instance()
{
    QMutexLocker locker(&instanceMutex_);
    if (!instance_) {
        instance_ = new RoomInfoManager();
    }
    return *instance_;
}

QSharedPointer<RoomInfo> RoomInfoManager::getOrCreateRoomInfo(const QString& roomName, 
                                                             const QString& roomKey,
                                                             const QString& clientId,
                                                             const QString& signature)
{
    QMutexLocker locker(&roomInfoMapMutex_);
    
    // 检查是否已存在
    if (roomInfoMap_.contains(roomName)) {
        auto& existingInfo = roomInfoMap_[roomName];
        // 更新现有信息
        existingInfo->roomKey = roomKey;
        existingInfo->clientId = clientId;
        existingInfo->signature = signature;
        qDebug() << "[RoomInfoManager] 更新房间信息:" << roomName;
        return existingInfo;
    }
    
    // 创建新的房间信息
    auto roomInfo = QSharedPointer<RoomInfo>::create(roomName, roomKey, clientId, signature);
    
    // 创建对应的 ArchiveFile（使用默认构造函数，稍后设置名称）
    roomInfo->archiveFile = QSharedPointer<ArchiveFile>::create();
    roomInfo->archiveFile->setName(roomName);
    
    // 存储到映射中
    roomInfoMap_[roomName] = roomInfo;
    
    qDebug() << "[RoomInfoManager] 创建新房间信息:" << roomName;
    emit roomInfoCreated(roomName);
    
    return roomInfo;
}

QSharedPointer<RoomInfo> RoomInfoManager::getRoomInfo(const QString& roomName) const
{
    QMutexLocker locker(&roomInfoMapMutex_);
    
    auto it = roomInfoMap_.find(roomName);
    if (it != roomInfoMap_.end()) {
        return it.value();
    }
    
    return QSharedPointer<RoomInfo>();
}

QSharedPointer<ArchiveFile> RoomInfoManager::getCurrentArchiveFile(const QString& roomName) const
{
    auto roomInfo = getRoomInfo(roomName);
    if (roomInfo && roomInfo->archiveFile) {
        return roomInfo->archiveFile;
    }
    
    return QSharedPointer<ArchiveFile>();
}

void RoomInfoManager::updateRoomInfo(const QString& roomName, const QSharedPointer<RoomInfo>& roomInfo)
{
    QMutexLocker locker(&roomInfoMapMutex_);
    
    if (roomInfo) {
        roomInfoMap_[roomName] = roomInfo;
        qDebug() << "[RoomInfoManager] 更新房间信息:" << roomName;
    }
}

void RoomInfoManager::removeRoomInfo(const QString& roomName)
{
    QMutexLocker locker(&roomInfoMapMutex_);
    
    if (roomInfoMap_.remove(roomName) > 0) {
        qDebug() << "[RoomInfoManager] 移除房间信息:" << roomName;
        emit roomInfoRemoved(roomName);
    }
}

void RoomInfoManager::clearAllRoomInfo()
{
    QMutexLocker locker(&roomInfoMapMutex_);
    
    QStringList roomNames = roomInfoMap_.keys();
    roomInfoMap_.clear();
    
    for (const QString& roomName : roomNames) {
        emit roomInfoRemoved(roomName);
    }
    
    qDebug() << "[RoomInfoManager] 清理所有房间信息，共清理" << roomNames.size() << "个房间";
}

QString RoomInfoManager::getArchiveSignature(const QString& roomName) const
{
    // 优先从RoomInfo的archiveSignature字段获取
    auto roomInfo = getRoomInfo(roomName);
    if (roomInfo && !roomInfo->archiveSignature.isEmpty()) {
        qDebug() << "[RoomInfoManager] 从RoomInfo获取房间" << roomName << "的archive signature:" << roomInfo->archiveSignature;
        return roomInfo->archiveSignature;
    }
    
    // 如果RoomInfo中没有，则从ArchiveFile获取
    auto archiveFile = getCurrentArchiveFile(roomName);
    if (archiveFile) {
        QString signature = archiveFile->signature();
        qDebug() << "[RoomInfoManager] 从ArchiveFile获取房间" << roomName << "的archive signature:" << signature;
        return signature;
    }
    
    qDebug() << "[RoomInfoManager] 房间" << roomName << "没有找到archive signature";
    return QString();
}

quint64 RoomInfoManager::getArchiveLineCount(const QString& roomName) const
{
    auto archiveFile = getCurrentArchiveFile(roomName);
    if (archiveFile) {
        quint64 lineCount = archiveFile->lineCount();
        qDebug() << "[RoomInfoManager] 获取房间" << roomName << "的archive行数:" << lineCount;
        return lineCount;
    }
    
    qDebug() << "[RoomInfoManager] 房间" << roomName << "没有找到ArchiveFile";
    return 0;
}

void RoomInfoManager::updateArchiveSignature(const QString& roomName, const QString& archiveSignature)
{
    QMutexLocker locker(&roomInfoMapMutex_);
    
    auto roomInfo = getRoomInfo(roomName);
    if (roomInfo) {
        roomInfo->archiveSignature = archiveSignature;
        qDebug() << "[RoomInfoManager] 更新房间" << roomName << "的archive signature:" << archiveSignature;
        emit archiveSignatureChanged(roomName, archiveSignature);
    } else {
        qWarning() << "[RoomInfoManager] 尝试更新不存在的房间" << roomName << "的archive signature";
    }
}

void RoomInfoManager::loadRoomInfoFromSettings()
{
    QMutexLocker locker(&roomInfoMapMutex_);
    
    QSettings settings(GlobalDef::SETTINGS_NAME, QSettings::defaultFormat());
    
    // 读取房间信息列表
    QStringList roomNames = settings.value("rooms/room_list").toStringList();
    
    qDebug() << "[RoomInfoManager] 从Settings加载房间信息，共" << roomNames.size() << "个房间";
    
    for (const QString& roomName : roomNames) {
        QString groupKey = QString("rooms/%1").arg(roomName);
        
        // 读取房间基本信息
        QString roomKey = settings.value(QString("%1/room_key").arg(groupKey)).toString();
        QString clientId = settings.value(QString("%1/client_id").arg(groupKey)).toString();
        QString signature = settings.value(QString("%1/signature").arg(groupKey)).toString();
        
        // 只恢复有有效签名的房间信息（表示有Archive数据）
        if (!signature.isEmpty()) {
            // 创建房间信息
            auto roomInfo = QSharedPointer<RoomInfo>::create(roomName, roomKey, clientId, signature);
            
            // 创建ArchiveFile并设置名称
            roomInfo->archiveFile = QSharedPointer<ArchiveFile>::create();
            roomInfo->archiveFile->setName(roomName);
            
            // 存储到映射中
            roomInfoMap_[roomName] = roomInfo;
            
            qDebug() << "[RoomInfoManager] 恢复房间信息:" << roomName << "签名:" << signature;
        } else {
            qDebug() << "[RoomInfoManager] 跳过无效房间信息:" << roomName;
        }
    }

    QMutexLocker locker2(&globalClientIdMutex_);
    QString clientId = settings.value("global/personal/clientid").toString();
    if (!clientId.isEmpty()) {
        globalClientId_ = clientId;
    } else {
        QString nickName = settings.value("global/personal/nick").toString();
        globalClientId_ = generateClientId(nickName);
    }
}

void RoomInfoManager::saveRoomInfoToSettings() const
{
    QMutexLocker locker(&roomInfoMapMutex_);
    
    QSettings settings(GlobalDef::SETTINGS_NAME, QSettings::defaultFormat());
    
    QStringList roomNames;
    
    // 遍历所有房间信息
    for (auto it = roomInfoMap_.begin(); it != roomInfoMap_.end(); ++it) {
        const QString& roomName = it.key();
        const QSharedPointer<RoomInfo>& roomInfo = it.value();
        
        QString groupKey = QString("rooms/%1").arg(roomName);
        
        // 保存房间基本信息
        settings.setValue(QString("%1/room_key").arg(groupKey), roomInfo->roomKey);
        settings.setValue(QString("%1/client_id").arg(groupKey), roomInfo->clientId);
        settings.setValue(QString("%1/signature").arg(groupKey), roomInfo->signature);
        settings.setValue(QString("%1/last_modified").arg(groupKey), QDateTime::currentMSecsSinceEpoch());
        
        roomNames.append(roomName);
        
        qDebug() << "[RoomInfoManager] 保存房间信息到Settings:" << roomName;
    }
    
    // 保存房间列表
    settings.setValue("rooms/room_list", roomNames);
    
    qDebug() << "[RoomInfoManager] 保存房间信息完成，共" << roomNames.size() << "个房间";

    QMutexLocker locker2(&globalClientIdMutex_);
    settings.setValue("global/personal/clientid", globalClientId_);
    qDebug() << "[RoomInfoManager] 保存globalClientId:" << globalClientId_;
    settings.sync();
}

void RoomInfoManager::clearRoomInfoFromSettings() const
{
    QSettings settings(GlobalDef::SETTINGS_NAME, QSettings::defaultFormat());
    
    // 读取房间信息列表
    QStringList roomNames = settings.value("rooms/room_list").toStringList();
    QStringList validRoomNames;
    int cleanedCount = 0;
    
    // 检查每个房间的配置
    for (const QString& roomName : roomNames) {
        QString groupKey = QString("rooms/%1").arg(roomName);
        QString roomKey = settings.value(QString("%1/room_key").arg(groupKey)).toString();
        
        // 如果有room_key，保留该房间信息
        if (!roomKey.isEmpty()) {
            validRoomNames.append(roomName);
            qDebug() << "[RoomInfoManager] 保留有room_key的房间:" << roomName;
        } else {
            // 删除没有room_key的房间配置
            settings.remove(groupKey);
            cleanedCount++;
            qDebug() << "[RoomInfoManager] 清理无room_key的房间:" << roomName;
        }
    }
    
    // 更新房间列表，只保留有room_key的房间
    settings.setValue("rooms/room_list", validRoomNames);
    settings.sync();
    
    qDebug() << "[RoomInfoManager] 清理Settings中的房间信息完成，保留" << validRoomNames.size() << "个房间，清理" << cleanedCount << "个房间";
}

void RoomInfoManager::clearRoomInfoFromSettingsByList(const QStringList &validRoomNames) const {
    QSettings settings(GlobalDef::SETTINGS_NAME, QSettings::defaultFormat());

    QStringList existingRoomNames =
        settings.value("rooms/room_list").toStringList();

    QStringList roomsToBeRemoved;

    for (const QString &roomName : existingRoomNames) {
        if (!validRoomNames.contains(roomName)) {
            roomsToBeRemoved.append(roomName);
        }
    }

    for (const QString &roomName : roomsToBeRemoved) {
        settings.remove(QString("rooms/%1").arg(roomName));
        qDebug() << "[RoomInfoManager] 清理房间:" << roomName;
    }

    settings.sync();
}

void RoomInfoManager::cleanupExpiredRoomInfo(int maxDays) const
{
    QSettings settings(GlobalDef::SETTINGS_NAME, QSettings::defaultFormat());
    
    // 读取房间信息列表
    QStringList roomNames = settings.value("rooms/room_list").toStringList();
    QStringList validRoomNames;
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    qint64 maxAge = maxDays * 24 * 60 * 60 * 1000; // 转换为毫秒
    
    for (const QString& roomName : roomNames) {
        QString groupKey = QString("rooms/%1").arg(roomName);
        qint64 lastModified =
            settings
                .value(QString("%1/last_modified").arg(groupKey), currentTime)
                .toLongLong();
        
        // 检查是否过期
        if (currentTime - lastModified < maxAge) {
            validRoomNames.append(roomName);
        } else {
            // 删除过期的房间信息
            settings.remove(groupKey);
            qDebug() << "[RoomInfoManager] 清理过期房间信息:" << roomName;
        }
    }
    
    // 更新房间列表
    settings.setValue("rooms/room_list", validRoomNames);
    settings.sync();
    
    qDebug() << "[RoomInfoManager] 清理过期房间信息完成，保留" << validRoomNames.size() << "个房间";
}

QSharedPointer<RoomInfo> RoomInfoManager::registerRoomBasicInfo(const QString& roomName, const QJsonObject& info)
{
    QMutexLocker locker(&roomInfoMapMutex_);
    qDebug() << "[RoomInfoManager] 注册房间基本信息:" << roomName << "info:" << info;
    QSharedPointer<RoomInfo> roomInfo;
    const bool isNewRoom = !info.value("key").toString(QString()).isEmpty();
    if(isNewRoom) {
        // 1. 创建新房间时
        // 注意，创建新房间时没有clientId和signature
        roomInfo = QSharedPointer<RoomInfo>::create(
            roomName, info.value("key").toString(QString()), QString(),
            QString());
        roomInfo->password = info.value("password").toString(QString());
        roomInfo->roomKey = info.value("key").toString(QString());

        // 创建对应的 ArchiveFile
        roomInfo->archiveFile = QSharedPointer<ArchiveFile>::create();
        roomInfo->archiveFile->setName(roomName);

        // 存储到映射中
        roomInfoMap_[roomName] = roomInfo;
        emit roomInfoCreated(roomName);
    } else {
        // 2. 登录房间时
        QString clientId = info["clientid"].toString();
        QString signature = info["signature"].toString();
        
        // 解析画布尺寸
        QSize canvasSize;
        if (info.contains("size")) {
            QJsonObject sizeObj = info["size"].toObject();
            canvasSize = QSize(sizeObj["width"].toInt(), sizeObj["height"].toInt());
        } else {
            qWarning() << "[RoomInfoManager] 登录响应中没有画布尺寸";
        }
        // 获取或创建房间信息
        if (roomInfoMap_.contains(roomName)) {
          roomInfo = roomInfoMap_[roomName];
          roomInfo->clientId = clientId;
          roomInfo->signature = signature;
        } else {
            roomInfo = QSharedPointer<RoomInfo>::create(roomName, QString(), clientId, signature);
            // 创建对应的 ArchiveFile
            roomInfo->archiveFile = QSharedPointer<ArchiveFile>::create();
            roomInfo->archiveFile->setName(roomName);
        }
        roomInfo->canvasSize = canvasSize;

        // 存储到映射中
        roomInfoMap_[roomName] = roomInfo;
        emit roomInfoCreated(roomName);
    }

    saveRoomInfoToSettings();

    return roomInfo;
}

QList<QSharedPointer<RoomInfo>> RoomInfoManager::listMyRooms() const
{
    QMutexLocker locker(&roomInfoMapMutex_);
    QList<QSharedPointer<RoomInfo>> myRooms;
    for (auto it = roomInfoMap_.begin(); it != roomInfoMap_.end(); ++it) {
        const QSharedPointer<RoomInfo>& roomInfo = it.value();
        if (roomInfo->roomKey.length() != 0) {
            myRooms.append(roomInfo);
        }
    }
    return myRooms;
}

QString RoomInfoManager::getGlobalClientId() const
{
    QMutexLocker locker(&globalClientIdMutex_);
    return globalClientId_;
}

void RoomInfoManager::updateClientIdOfRoom(const QString &roomName,
                                           const QString &clientId) const
{
    QMutexLocker locker(&roomInfoMapMutex_);
    if (roomInfoMap_.contains(roomName)) {
        roomInfoMap_[roomName]->clientId = clientId;
        saveRoomInfoToSettings();
    }
}