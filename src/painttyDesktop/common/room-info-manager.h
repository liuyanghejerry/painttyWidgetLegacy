#ifndef ROOM_INFO_MANAGER_H
#define ROOM_INFO_MANAGER_H

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QRecursiveMutex>
#include <QSharedPointer>
#include <QSize>
#include <QString>
#include "../misc/archivefile.h"

/**
 * @brief 单个房间的信息结构
 */
struct RoomInfo {
    QString roomName;
    QString roomKey;
    QString clientId;
    QString signature; // 客户端身份验证签名
    QString archiveSignature; // Archive数据签名，用于标识archive版本
    QString password;
    QString webAddress;
    QSharedPointer<ArchiveFile> archiveFile;
    QSize canvasSize;
    
    RoomInfo() = default;
    RoomInfo(const QString& name, const QString& key, const QString& cid, const QString& sig)
        : roomName(name), roomKey(key), clientId(cid), signature(sig) {}
};

/**
 * @brief 房间信息管理器
 * 
 * 负责管理所有房间的信息，包括每个房间的 ArchiveFile
 * 使用单例模式，确保全局只有一个实例
 */
class RoomInfoManager : public QObject
{
    Q_OBJECT

public:
    explicit RoomInfoManager(QObject *parent = nullptr);
    ~RoomInfoManager();
    
    /**
     * @brief 获取单例实例
     * @return RoomInfoManager实例的引用
     */
    static RoomInfoManager& instance();
    
    /**
     * @brief 创建或获取房间信息
     * @param roomName 房间名
     * @param roomKey 房间密钥
     * @param clientId 客户端ID
     * @param signature 客户端签名
     * @return 房间信息指针
     */
    QSharedPointer<RoomInfo> getOrCreateRoomInfo(const QString& roomName, 
                                                 const QString& roomKey,
                                                 const QString& clientId,
                                                 const QString& signature);
    
    /**
     * @brief 获取当前房间信息
     * @param roomName 房间名
     * @return 房间信息指针，如果不存在则返回空指针
     */
    QSharedPointer<RoomInfo> getRoomInfo(const QString& roomName) const;
    
    /**
     * @brief 获取当前房间的 ArchiveFile
     * @param roomName 房间名
     * @return ArchiveFile 指针，如果不存在则返回空指针
     */
    QSharedPointer<ArchiveFile> getCurrentArchiveFile(const QString& roomName) const;
    
    /**
     * @brief 更新房间信息
     * @param roomName 房间名
     * @param roomInfo 新的房间信息
     */
    void updateRoomInfo(const QString& roomName, const QSharedPointer<RoomInfo>& roomInfo);
    
    /**
     * @brief 移除房间信息
     * @param roomName 房间名
     */
    void removeRoomInfo(const QString& roomName);
    
    /**
     * @brief 清理所有房间信息
     */
    void clearAllRoomInfo();
    
    /**
     * @brief 获取当前房间的 archive signature
     * @param roomName 房间名
     * @return archive signature，如果不存在则返回空字符串
     */
    QString getArchiveSignature(const QString& roomName) const;
    
    /**
     * @brief 获取当前房间的 archive 行数
     * @param roomName 房间名
     * @return archive 行数，如果不存在则返回 0
     */
    quint64 getArchiveLineCount(const QString& roomName) const;

    /**
     * @brief 更新房间的 archive signature
     * @param roomName 房间名
     * @param archiveSignature 新的 archive signature
     */
    void updateArchiveSignature(const QString& roomName, const QString& archiveSignature);

    /**
     * @brief 从Settings加载房间信息
     * 在程序启动时调用，用于恢复上次的房间Archive信息
     */
    void loadRoomInfoFromSettings();
    
    /**
     * @brief 将房间信息保存到Settings
     * 在程序关闭时调用，用于保存房间Archive信息供下次启动使用
     */
    void saveRoomInfoToSettings() const;
    
    /**
     * @brief 清理Settings中的房间信息
     * 用于清理过期的房间信息
     */
    void clearRoomInfoFromSettings() const;

    /**
     * @brief 根据房间列表清理Settings中的房间信息
     * 只保留在roomNames列表中的房间，删除其他所有房间记录
     * @param roomNames 要保留的房间名列表
     */
    void clearRoomInfoFromSettingsByList(const QStringList& roomNames) const;
    
    /**
     * @brief 清理过期的房间信息
     * 清理超过指定天数的房间信息
     * @param maxDays 最大保留天数，默认30天
     */
    void cleanupExpiredRoomInfo(int maxDays = 30) const;
    
    /**
     * @brief 处理登录响应，解析并存储房间信息
     * @param roomName 房间名
     * @param response 登录响应JSON对象
     * @return 更新后的房间信息指针
     */
    QSharedPointer<RoomInfo> registerRoomBasicInfo(const QString& roomName, const QJsonObject& response);
    
    /**
     * @brief 列出当前具有房主身份的房间
     * @return 房间信息指针列表
     */
    QList<QSharedPointer<RoomInfo>> listMyRooms() const;


    QString getGlobalClientId() const;
    void updateClientIdOfRoom(const QString& roomName, const QString& clientId) const;

  signals:
    void roomInfoCreated(const QString& roomName);
    void roomInfoRemoved(const QString& roomName);
    void archiveSignatureChanged(const QString& roomName, const QString& signature);

private:
    QHash<QString, QSharedPointer<RoomInfo>> roomInfoMap_;
    mutable QRecursiveMutex roomInfoMapMutex_; // 线程安全

    QString globalClientId_;
    mutable QRecursiveMutex globalClientIdMutex_;

    // 单例相关
    static RoomInfoManager* instance_;
    static QMutex instanceMutex_;
};

#endif // ROOM_INFO_MANAGER_H 