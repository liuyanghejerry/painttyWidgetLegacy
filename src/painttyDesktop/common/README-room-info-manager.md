# RoomInfoManager 房间信息管理器

## 概述

RoomInfoManager 是一个全局单例类，负责管理所有房间的信息，包括每个房间的 ArchiveFile。它提供了从 Settings 读取和写入房间信息的功能，使得用户重新进入程序时能够复用上一次的 Archive 信息。

## 主要功能

### 1. 房间信息管理
- 创建或获取房间信息
- 更新房间信息
- 移除房间信息
- 清理所有房间信息

### 2. Settings 持久化
- `loadRoomInfoFromSettings()`: 从 Settings 加载房间信息
- `saveRoomInfoToSettings()`: 将房间信息保存到 Settings
- `clearRoomInfoFromSettings()`: 清理 Settings 中的房间信息
- `cleanupExpiredRoomInfo()`: 清理过期的房间信息

### 3. Archive 信息查询
- `getCurrentArchiveFile()`: 获取当前房间的 ArchiveFile
- `getArchiveSignature()`: 获取当前房间的 archive signature
- `getArchiveLineCount()`: 获取当前房间的 archive 行数

## 数据结构

### RoomInfo 结构
```cpp
struct RoomInfo {
    QString roomName;        // 房间名
    QString roomKey;         // 房间密钥
    QString clientId;        // 客户端ID
    QString signature;       // 客户端身份验证签名
    QSharedPointer<ArchiveFile> archiveFile;  // Archive文件
    QSize canvasSize;        // 画布尺寸（不持久化）
};
```

## 使用场景

### 1. 程序启动时
在 `RoomListDialog` 构造函数中：
```cpp
// 清理过期的房间信息（保留30天）
RoomInfoManager::instance().cleanupExpiredRoomInfo(30);

// 加载房间信息（用于程序重启时复用Archive信息）
RoomInfoManager::instance().loadRoomInfoFromSettings();
```

### 2. 房间加入时
在 `MainWindow::onRoomJoined()` 中：
```cpp
// 更新RoomInfoManager中的房间信息
QString roomName = client_socket.roomName();
QString roomKey = client_socket.roomKey();
QString clientId = client_socket.clientId();
QString signature = client_socket.archiveSignature();

if (!roomName.isEmpty() && !clientId.isEmpty()) {
    RoomInfoManager::instance().getOrCreateRoomInfo(roomName, roomKey, clientId, signature);
}
```

### 3. Archive 签名更新时
在 `CanvasBackend` 中：
```cpp
connect(archive_file_, &ArchiveFile::newSignature,
        [this](const QString &signature) {
    // 更新RoomInfoManager中的Archive签名
    updateRoomInfoManagerSignature(signature);
});
```

### 4. 程序关闭时
在 `MainWindow::closeEvent()` 中：
```cpp
// 保存房间信息（用于程序重启时复用Archive信息）
RoomInfoManager::instance().saveRoomInfoToSettings();
```

## Settings 存储结构

房间信息在 Settings 中的存储结构如下：
```
rooms/
├── room_list                    # 房间名列表
├── {roomName1}/
│   ├── room_key                 # 房间密钥
│   ├── client_id                # 客户端ID
│   ├── signature                # Archive签名
│   └── last_modified            # 最后修改时间
└── {roomName2}/
    ├── room_key
    ├── client_id
    ├── signature
    └── last_modified
```

## 注意事项

1. **只复用 Archive 信息**：根据需求，只复用 Archive 信息，画布尺寸不需要复用
2. **线程安全**：所有方法都是线程安全的，使用 QMutex 保护
3. **自动清理**：程序启动时自动清理超过30天的过期房间信息
4. **签名验证**：只保存有有效签名的房间信息（表示有 Archive 数据）

## 信号

- `roomInfoCreated(const QString& roomName)`: 房间信息创建时发出
- `roomInfoRemoved(const QString& roomName)`: 房间信息移除时发出
- `archiveSignatureChanged(const QString& roomName, const QString& signature)`: Archive 签名改变时发出 