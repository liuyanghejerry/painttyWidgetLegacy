#include "sse-clientsocket.h"
#include "../common.h"

#include <QApplication>
#include <QAtomicInt>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimer>
#include <QUrlQuery>
#include "QtCore/qmetatype.h"
#include "known-error.h"

SSEClientSocket::SSEClientSocket(RoomInfoManager* roomInfoManager, QObject *parent)
    : QObject(parent)
    , sseThread_(nullptr)
    , sseConnection_(nullptr)
    , roomInfoManager_(roomInfoManager)
    , state_(INIT)
    , roomDelay_(0)
    , networkManager_(nullptr)
    , heartbeatTimer_(nullptr)
    , heartbeatInterval_(30000)  // 30秒心跳间隔
    , heartbeatEnabled_(false)
    , isReconnecting_(false)
{
    qDebug() << "[SSEClientSocket] Constructor called";
    
    // 验证RoomInfoManager参数
    if (!roomInfoManager_) {
        qWarning() << "[SSEClientSocket] RoomInfoManager is null!";
    }
    
    // 在主线程中初始化基本数据
    initializeClientIdAndNickname();
    initializeNetworkManager();
    
    // 初始化心跳定时器（主线程）
    heartbeatTimer_ = new QTimer(this);
    heartbeatTimer_->setSingleShot(false);
    connect(heartbeatTimer_, &QTimer::timeout, this, &SSEClientSocket::sendHeartbeat);
    
    // 创建 SSE 连接线程
    sseThread_ = new QThread(this);
    sseThread_->setObjectName("SSEConnectionThread");
    
    // 创建 SSE 连接对象并移动到子线程
    sseConnection_ = new SSEConnection();
    sseConnection_->moveToThread(sseThread_);
    
    // 连接 SSE 信号到主线程的槽函数
    connect(sseConnection_, &SSEConnection::connected, this, &SSEClientSocket::onSSEConnected, Qt::QueuedConnection);
    connect(sseConnection_, &SSEConnection::disconnected, this, &SSEClientSocket::onSSEDisconnected, Qt::QueuedConnection);
    connect(sseConnection_, &SSEConnection::error, this, &SSEClientSocket::onSSEError, Qt::QueuedConnection);
    
    // 连接 SSE 事件信号
    connect(sseConnection_, &SSEConnection::message, this, &SSEClientSocket::onSSEMessage, Qt::QueuedConnection);
    connect(sseConnection_, &SSEConnection::draw, this, &SSEClientSocket::onSSEDraw, Qt::QueuedConnection);
    connect(sseConnection_, &SSEConnection::chat, this, &SSEClientSocket::onSSEChat, Qt::QueuedConnection);
    connect(sseConnection_, &SSEConnection::archive, this, &SSEClientSocket::onSSEArchive, Qt::QueuedConnection);
    connect(sseConnection_, &SSEConnection::archiveEnd, this, &SSEClientSocket::onSSEArchiveEnd, Qt::QueuedConnection);
    connect(sseConnection_, &SSEConnection::heartbeat, this, &SSEClientSocket::onSSEHeartbeat, Qt::QueuedConnection);
    connect(sseConnection_, &SSEConnection::notify, this, &SSEClientSocket::onSSENotify, Qt::QueuedConnection);
    connect(sseConnection_, &SSEConnection::kick, this, &SSEClientSocket::onSSEKick, Qt::QueuedConnection);
    connect(sseConnection_, &SSEConnection::clearAll, this, &SSEClientSocket::onSSEClearAll, Qt::QueuedConnection);
    connect(sseConnection_, &SSEConnection::closeRoom, this, &SSEClientSocket::onSSECloseRoom, Qt::QueuedConnection);
    connect(sseConnection_, &SSEConnection::sseCustomEvent, this, &SSEClientSocket::onSSECustomEvent, Qt::QueuedConnection);
    
    // 启动 SSE 线程
    sseThread_->start();
    qDebug() << "[SSEClientSocket] Constructor completed, SSE thread started";
}

SSEClientSocket::~SSEClientSocket()
{
    qDebug() << "[SSEClientSocket] Destructor called";
    
    // 先停止心跳定时器
    if (heartbeatTimer_) {
        heartbeatTimer_->stop();
        QObject::disconnect(heartbeatTimer_, nullptr, this, nullptr);
        heartbeatTimer_->deleteLater();
        heartbeatTimer_ = nullptr;
    }
    
    // 停止 SSE 线程
    if (sseThread_) {
        // 断开 SSE 连接，但不触发错误处理
        if (sseConnection_) {
            // 断开信号连接，避免在对象销毁过程中触发回调
            QObject::disconnect(sseConnection_, nullptr, this, nullptr);
            
            // 通过信号槽机制断开连接
            QMetaObject::invokeMethod(sseConnection_, "disconnect", Qt::QueuedConnection);
            
            // 等待一小段时间确保操作完成
            QThread::msleep(100);
            
            sseConnection_->deleteLater();
            sseConnection_ = nullptr;
        }
        
        // 停止线程
        sseThread_->quit();
        
        // 等待线程结束（最多等待5秒）
        if (!sseThread_->wait(5000)) {
            qWarning() << "[SSEClientSocket] SSE thread did not stop gracefully, terminating";
            sseThread_->terminate();
            sseThread_->wait();
        }
        
        // 清理线程
        sseThread_->deleteLater();
        sseThread_ = nullptr;
    }
    
    // 清理网络管理器
    if (networkManager_) {
        // 断开所有网络请求的信号连接
        QObject::disconnect(networkManager_, nullptr, this, nullptr);
        networkManager_->deleteLater();
        networkManager_ = nullptr;
    }
    
    // 重置状态，避免在销毁过程中触发状态变更
    state_ = INIT;
    
    qDebug() << "[SSEClientSocket] Destructor completed";
}

void SSEClientSocket::initializeNetworkManager()
{
    if (!networkManager_) {
        networkManager_ = new QNetworkAccessManager(this);
    }
}

void SSEClientSocket::initializeClientIdAndNickname() {
    QSettings settings(GlobalDef::SETTINGS_NAME,
                       QSettings::defaultFormat(),
                       qApp);
    QByteArray data = settings.value("global/personal/nick")
            .toByteArray();
    nickname_ = QString::fromUtf8(data);
}

SSEClientSocket::State SSEClientSocket::currentState() const
{
    QMutexLocker locker(&stateMutex_);
    return state_;
}

bool SSEClientSocket::isConnected() const
{
    QMutexLocker locker(&stateMutex_);
    return state_ >= ROOM_JOINED;
}

bool SSEClientSocket::isInRoom() const
{
    QMutexLocker locker(&stateMutex_);
    return state_ >= ROOM_JOINED && state_ <= ROOM_EXITED;
}

bool SSEClientSocket::isRoomOwner() const
{
    QMutexLocker locker(&dataMutex_);
    auto roomInfo = roomInfoManager_->getRoomInfo(roomName_);
    if (roomInfo) {
        return !roomInfo->roomKey.isEmpty();
    }
    return false;
}

QString SSEClientSocket::getClientIdOfCurrentRoom() const
{
    // 从 RoomInfoManager 获取当前房间的 clientId
    auto roomInfo = roomInfoManager_->getRoomInfo(roomName_);
    if (roomInfo) {
        return roomInfo->clientId;
    }
    return roomInfoManager_->getGlobalClientId();
}

QString SSEClientSocket::nickname() const
{
    QMutexLocker locker(&dataMutex_);
    return nickname_;
}

QString SSEClientSocket::roomName() const
{
    QMutexLocker locker(&dataMutex_);
    return roomName_;
}

QSize SSEClientSocket::canvasSize() const
{
    // 从 RoomInfoManager 获取当前房间的画布尺寸
    auto roomInfo = roomInfoManager_->getRoomInfo(roomName_);
    if (roomInfo) {
        return roomInfo->canvasSize;
    }
    return QSize();
}

int SSEClientSocket::getDelay() const
{
    return roomDelay_.loadRelaxed();
}

QString SSEClientSocket::toUrl() const
{
    if (roomBaseUrl_.isEmpty() || roomName_.isEmpty()) {
        return QString();
    }
    return QString("%1/room/%2").arg(roomBaseUrl_).arg(roomName_);
}

QString SSEClientSocket::archiveSignature() const
{
    // 从 RoomInfoManager 获取当前房间的 archive signature
    if (!roomName_.isEmpty()) {
      return roomInfoManager_->getArchiveSignature(roomName_);
    }
    return QString();
}

RoomInfoManager* SSEClientSocket::roomInfoManager() const
{
    return roomInfoManager_;
}

void SSEClientSocket::setUserName(const QString& name)
{
    QMutexLocker locker(&dataMutex_);
    nickname_ = name;
}

void SSEClientSocket::setState(State newState)
{
    QMutexLocker locker(&stateMutex_);
    if (state_ != newState) {
        qDebug() << "[SSEClientSocket] State changed:" << state_ << "->" << newState;
        state_ = newState;
    }
}

void SSEClientSocket::tryJoinRoom(const QString& urlBase, const QString& roomName, const QString& password)
{
    QUrl url(urlBase);
    if (!url.isValid()) {
        qWarning() << "[SSEClientSocket] Invalid room URL:" << urlBase;
        return;
    }
    
    // 获取主机名
    QString host = url.host();
    if (host.isEmpty()) {
        qWarning() << "[SSEClientSocket] Empty host in room URL:" << urlBase;
        return;
    }
    
    // 检查主机名是否已经是IP地址
    QHostAddress testAddress(host);
    if (!testAddress.isNull() && testAddress.protocol() == QAbstractSocket::IPv4Protocol) {
        // 已经是IPv4地址，直接使用
        qDebug() << "[SSEClientSocket] Room URL already contains IPv4 address:" << host;
        roomBaseUrl_ = urlBase;
        roomName_ = roomName;
        connectToRoom(roomName_, nickname_, password);
        return;
    }
    
    // 使用Qt的DNS解析能力解析IPv4地址
    QHostInfo::lookupHost(host, this, [this, url, roomName, password](const QHostInfo &hostInfo) {
        if (hostInfo.error() != QHostInfo::NoError) {
            qWarning() << "[SSEClientSocket] DNS lookup failed for room host:" << hostInfo.hostName() 
                       << "Error:" << hostInfo.errorString();
            return;
        }
        
        // 查找IPv4地址
        QHostAddress ipv4Address;
        for (const QHostAddress &address : hostInfo.addresses()) {
            if (address.protocol() == QAbstractSocket::IPv4Protocol) {
                ipv4Address = address;
                break;
            }
        }
        
        if (ipv4Address.isNull()) {
            qWarning() << "[SSEClientSocket] No IPv4 address found for room host:" << hostInfo.hostName();
            return;
        }
        
        // 构建新的URL，用IPv4地址替换主机名
        QUrl newUrl = url;
        newUrl.setHost(ipv4Address.toString());
        roomBaseUrl_ = newUrl.toString();
        roomName_ = roomName;
        
        qDebug() << "[SSEClientSocket] Room DNS resolved" << hostInfo.hostName() 
                 << "to" << ipv4Address.toString();
        qDebug() << "[SSEClientSocket] Connecting to room with resolved URL:" << roomBaseUrl_;
        
        connectToRoom(roomName_, nickname_, password);
    });
}

void SSEClientSocket::requestRoomList()
{
    qDebug() << "[SSEClientSocket] Requesting room list";
    // setState(REQUESTING_ROOMLIST);
    
    QUrl url(managerBaseUrl_ + "/api/rooms");
    sendGetRoomListRequest(url);
}

void SSEClientSocket::requestNewRoom(const QJsonObject& roomInfo)
{
    qDebug() << "[SSEClientSocket] Creating new room:" << roomInfo["name"].toString() << "data:" << roomInfo;
    setState(CONNECTING_ROOM);
    
    QJsonObject request;
    request["info"] = roomInfo;
    roomName_ = roomInfo["name"].toString();
    
    QUrl url(managerBaseUrl_ + "/api/rooms");
    sendCreateRoomRequest(url, request);
}

void SSEClientSocket::connectToRoom(const QString& roomName, const QString& nickname, 
                                   const QString& password, int lineStart)
{
    qDebug() << "[SSEClientSocket] Connecting to room:" << roomName << "username:" << nickname;
    
    // 检查当前状态，如果还在连接中，先完全断开
    if (state_ >= CONNECTING_ROOM) {
        qDebug() << "[SSEClientSocket] Previous connection still active, disconnecting first";
        disconnect();
        // 等待断开完成
        QThread::msleep(100);
    }
    
    setState(CONNECTING_ROOM);
    
    roomName_ = roomName;
    nickname_ = nickname;
    
    // ArchiveFile初始化现在由CanvasBackend负责
    
    // 如果还没有 workerBaseUrl，需要先获取房间信息
    if (roomBaseUrl_.isEmpty()) {
        qWarning() << "[SSEClientSocket] RoomBaseUrl is empty";
        return;
    }
    
    // 先登录获取 clientId
    loginToRoom(roomName, nickname, password);
}

void SSEClientSocket::loginToRoom(const QString& roomName, const QString& nickname, const QString& password)
{
    if (roomBaseUrl_.isEmpty()) {
        qWarning() << "[SSEClientSocket] 未获取到房间信息";
        return;
    }
    
    qDebug() << "[SSEClientSocket] Logging in to room, username:" << nickname;
    setState(JOINING_ROOM);

    QString currentRoomClientId = getClientIdOfCurrentRoom();
    QString globalClientId = roomInfoManager_->getGlobalClientId();
    if (currentRoomClientId.isEmpty()) {
        currentRoomClientId = globalClientId;
    }
    
    QJsonObject request;
    request["name"] = nickname;
    request["password"] = password;
    request["clientid"] = currentRoomClientId;
    
    QUrl url(roomBaseUrl_ + QString("/api/room/%1/login").arg(roomName));
    sendLoginRequest(url, request);
}

void SSEClientSocket::setupSSEConnection(const QString& roomName, const QString& nickname, int lineStart, const QString& archiveSign)
{
    // 从 RoomInfoManager 获取房间信息
    auto roomInfo = roomInfoManager_->getRoomInfo(roomName);
    if (!roomInfo || roomInfo->clientId.isEmpty() || roomInfo->signature.isEmpty()) {
        qWarning() << "[SSEClientSocket] SSE connection setup failed: missing necessary information";
        return;
    }
    
    if (roomBaseUrl_.isEmpty()) {
        qWarning() << "[SSEClientSocket] SSE connection setup failed: roomBaseUrl is empty";
        return;
    }
    
    // 构建 SSE 连接 URL
    QUrl url(roomBaseUrl_ + QString("/api/room/%1/data").arg(roomName));
    QUrlQuery query;
    query.addQueryItem("clientId", roomInfo->clientId);
    query.addQueryItem("signature", roomInfo->signature); // 客户端身份验证签名
    query.addQueryItem("nickname", nickname);
    
    // 添加 archiveSign 参数（优先使用传入的参数，否则使用本地archive签名）
    QString finalArchiveSign = archiveSign;
    if (finalArchiveSign.isEmpty()) {
        finalArchiveSign = this->archiveSignature();
    }
    if (!finalArchiveSign.isEmpty()) {
        query.addQueryItem("archiveSign", finalArchiveSign);
        qDebug() << "[SSEClientSocket] 使用archiveSign参数:" << finalArchiveSign;
    }
    
    // 如果lineStart为-1，使用本地archive的行数
    if (lineStart == -1) {
        quint64 localLineCount = roomInfoManager_->getArchiveLineCount(roomName);
        if (localLineCount > 0) {
            lineStart = static_cast<int>(localLineCount);
            qDebug() << "[SSEClientSocket] 使用本地archive行数作为lineStart:" << lineStart;
        } else {
            lineStart = 0;
        }
    }
    
    if (lineStart >= 0) {
        query.addQueryItem("lineStart", QString::number(lineStart));
    }
    url.setQuery(query);
    
    qDebug() << "[SSEClientSocket] Establishing SSE connection:" << url.toString();
    
    // 配置 SSE 连接
    if (sseConnection_) {
        // 确保所有组件已经在正确的线程中初始化
        QMetaObject::invokeMethod(sseConnection_, "initInThread", Qt::QueuedConnection);
        
        // 设置重连参数
        sseConnection_->setReconnectInterval(5000);  // 5秒重连间隔
        sseConnection_->setMaxReconnectAttempts(10);  // 最大重连10次
        sseConnection_->setReconnectEnabled(true);
        
        // 新增：连接信号槽，用于在重连时获取最新的 lineStart 值
        connect(sseConnection_, &SSEConnection::requestLineStart, this, &SSEClientSocket::onRequestLineStart, Qt::QueuedConnection);
        
        // 配置接收速率限制 - 防止archive数据过快导致内存问题
        // 跨线程调用设置接收速率限制
        QMetaObject::invokeMethod(sseConnection_, "setReceiveRateLimit", 
                                  Qt::QueuedConnection, Q_ARG(int, 50));
        QMetaObject::invokeMethod(sseConnection_, "enableReceiveRateLimit", 
                                  Qt::QueuedConnection, Q_ARG(bool, true));
        
        // 在子线程中连接到URL
        QMetaObject::invokeMethod(sseConnection_, "connectToUrlInThread", Qt::QueuedConnection, Q_ARG(QUrl, url));
    } else {
        qWarning() << "[SSEClientSocket] SSE connection object not initialized";
    }
}

void SSEClientSocket::exitFromRoom()
{
    qDebug() << "[SSEClientSocket] Exiting room";
    
    setState(ROOM_EXITED);
    emit roomExited();
}

void SSEClientSocket::disconnect()
{
    qDebug() << "[SSEClientSocket] Disconnecting";
    exitFromRoom();
    
    if (sseConnection_) {
        // 使用阻塞方式断开连接，确保完全断开
        QMetaObject::invokeMethod(sseConnection_, "disconnect", Qt::BlockingQueuedConnection);
        
        // 等待一小段时间确保断开操作完成
        QThread::msleep(100);
        
        // 重置 SSE 连接的状态
        QMetaObject::invokeMethod(sseConnection_, "resetState", Qt::QueuedConnection);
    }
    
    // 停止心跳
    stopHeartbeat();
    
    // 重置连接相关状态
    roomName_.clear();
    roomBaseUrl_.clear();
    // setState(MANAGER_DISCONNECTED);
    
    qDebug() << "[SSEClientSocket] Disconnect completed, state reset";
}

void SSEClientSocket::close()
{
    disconnect();
}

void SSEClientSocket::sendMessage(const QString& content)
{
    sendChatMessage(content);
}

void SSEClientSocket::sendChatMessage(const QString& content)
{
    // 从 RoomInfoManager 获取房间信息
    auto roomInfo = roomInfoManager_->getRoomInfo(roomName_);
    if (!roomInfo || roomInfo->signature.isEmpty()) {
        qWarning() << "[SSEClientSocket] 无法发送聊天消息：未连接到房间或缺少签名";
        return;
    }
    
    if (roomBaseUrl_.isEmpty() || roomName_.isEmpty()) {
        qWarning() << "[SSEClientSocket] 无法发送聊天消息：缺少房间信息";
        return;
    }
    
    qDebug() << "[SSEClientSocket] Sending chat message:" << content;
    
    QJsonObject request;
    request["content"] = nickname() + ": " + content + "\n";
    appendCommonData(request);
    
    QUrl url(roomBaseUrl_ + QString("/api/room/%1/chat").arg(roomName_));
    sendChatMessageRequest(url, request);
}

void SSEClientSocket::sendDataPack(const QByteArray& content)
{
    qDebug() << "[SSEClientSocket] sendDataPack, content size:" << content.size() << "bytes";
    // 将 QByteArray 转换为 QJsonObject 并发送
    QJsonDocument doc = QJsonDocument::fromJson(content);
    if (doc.isObject()) {
        sendDataPack(doc.object());
    }
}

void SSEClientSocket::sendDataPack(const QJsonObject& content)
{
    // 发送绘画数据
    if (content.contains("action") && content["action"].toString() == "block") {
        DrawBlockAction action;
        action.action = content["action"].toString();
        action.layer = content["layer"].toString();
        action.clientid = content["clientid"].toString();
        action.name = content["name"].toString();
        
        // 解析 block 数据
        QJsonArray blockArray = content["block"].toArray();
        for (const QJsonValue& pointValue : blockArray) {
            QJsonObject pointObj = pointValue.toObject();
            DrawPoint point;
            point.x = pointObj["x"].toDouble();
            point.y = pointObj["y"].toDouble();
            point.pressure = pointObj["pressure"].toDouble();
            action.block.append(point);
        }
        
        // 解析 brush 数据
        QJsonObject brushObj = content["brush"].toObject();
        action.brush.width = brushObj["width"].toDouble();
        action.brush.name = brushObj["name"].toString();
        action.brush.hardness = brushObj["hardness"].toDouble();
        action.brush.thickness = brushObj["thickness"].toDouble();
        
        QJsonObject colorObj = brushObj["color"].toObject();
        action.brush.color.red = colorObj["red"].toInt();
        action.brush.color.green = colorObj["green"].toInt();
        action.brush.color.blue = colorObj["blue"].toInt();
        
        sendDrawData(action);
    }
}

void SSEClientSocket::sendCmdPack(const QJsonObject& content)
{
    // 处理命令包
    QString cmd = content["cmd"].toString();
    if (cmd == "clearall") {
        clearCanvas();
    } else if (cmd == "close") {
        requestCloseRoom();
    } else if (cmd == "checkout") {
        requestCheckout();
    } else if (cmd == "kick") {
        QString clientId = content["clientid"].toString();
        requestKickUser(clientId);
    }
}

void SSEClientSocket::sendManagerPack(const QJsonObject& content)
{
    // 处理管理器包
    emit managerPack(content);
}

void SSEClientSocket::sendDrawData(const DrawBlockAction& data)
{
    // 从 RoomInfoManager 获取房间信息
    auto roomInfo = roomInfoManager_->getRoomInfo(roomName_);
    if (!roomInfo || roomInfo->signature.isEmpty()) {
        qWarning() << "[SSEClientSocket] 无法发送绘画数据：未连接到房间或缺少签名";
        return;
    }
    
    if (roomBaseUrl_.isEmpty() || roomName_.isEmpty()) {
        qWarning() << "[SSEClientSocket] 无法发送绘画数据：缺少房间信息";
        return;
    }
    
    qDebug() << "[SSEClientSocket] Sending drawing data";
    
    QJsonObject request = drawActionToJson(data);
    appendCommonData(request); // 添加clientId和签名
    
    QUrl url(roomBaseUrl_ + QString("/api/room/%1/draw").arg(roomName_));
    sendDrawDataRequest(url, request);
}

void SSEClientSocket::clearCanvas()
{
    auto roomKey = roomInfoManager_->getRoomInfo(roomName_)->roomKey;
    if (roomBaseUrl_.isEmpty() || roomName_.isEmpty() || roomKey.isEmpty()) {
        qWarning() << "[SSEClientSocket] 无法清空画布：缺少必要信息" << roomBaseUrl_ << roomName_ << roomKey;
        return;
    }
    
    qDebug() << "[SSEClientSocket] Clearing canvas";
    
    QJsonObject request;
    request["key"] = roomKey;
    appendCommonData(request);
    
    QUrl url(roomBaseUrl_ + QString("/api/room/%1/clearall").arg(roomName_));
    sendClearCanvasRequest(url, request);
}

void SSEClientSocket::requestOnlinelist()
{
    if (roomBaseUrl_.isEmpty() || roomName_.isEmpty()) {
        qWarning() << "[SSEClientSocket] 无法获取在线列表：未连接到房间";
        return;
    }
    
    qDebug() << "[SSEClientSocket] Requesting online list";
    
    QUrl url(roomBaseUrl_ + QString("/api/room/%1/onlinelist").arg(roomName_));
    sendGetOnlineListRequest(url);
}

bool SSEClientSocket::requestCheckout()
{
    auto roomKey = roomInfoManager_->getRoomInfo(roomName_)->roomKey;
    if (roomBaseUrl_.isEmpty() || roomName_.isEmpty() || roomKey.isEmpty()) {
        qWarning() << "[SSEClientSocket] 无法续期房间：缺少必要信息";
        return false;
    }
    
    qDebug() << "[SSEClientSocket] Requesting room renewal";
    
    QJsonObject request;
    request["key"] = roomKey;
    
    QUrl url(roomBaseUrl_ + QString("/api/room/%1/checkout").arg(roomName_));
    sendCheckoutRequest(url, request);
    return true;
}

bool SSEClientSocket::requestCloseRoom()
{
    auto roomKey = roomInfoManager_->getRoomInfo(roomName_)->roomKey;
    if (roomBaseUrl_.isEmpty() || roomName_.isEmpty() || roomKey.isEmpty()) {
        qWarning() << "[SSEClientSocket] 无法关闭房间：缺少必要信息";
        return false;
    }
    
    qDebug() << "[SSEClientSocket] Requesting close room";
    
    QJsonObject request;
    request["key"] = roomKey;
    
    QUrl url(roomBaseUrl_ + QString("/api/room/%1/close").arg(roomName_));
    sendCloseRoomRequest(url, request);
    return true;
}

bool SSEClientSocket::requestKickUser(const QString& id)
{
    auto roomKey = roomInfoManager_->getRoomInfo(roomName_)->roomKey;
    if (roomBaseUrl_.isEmpty() || roomName_.isEmpty() || roomKey.isEmpty()) {
        qWarning() << "[SSEClientSocket] 无法踢出用户：缺少必要信息";
        return false;
    }
    
    qDebug() << "[SSEClientSocket] Requesting to kick user:" << id;
    
    QJsonObject request;
    request["key"] = roomKey;
    request["clientid"] = id;
    
    QUrl url(roomBaseUrl_ + QString("/api/room/%1/kick").arg(roomName_));
    sendKickUserRequest(url, request);
    return true;
}

void SSEClientSocket::requestArchive()
{
    // TODO: 实现存档请求
    qDebug() << "[SSEClientSocket] Requesting archive";
}

void SSEClientSocket::requestArchiveSign()
{
    // TODO: 实现存档签名请求
    qDebug() << "[SSEClientSocket] Requesting archive signature";
}

void SSEClientSocket::resetConnection()
{
    sseBuffer_.clear();
}

QNetworkRequest SSEClientSocket::createRequest(const QUrl& url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "application/json");
    return request;
}

void SSEClientSocket::sendPostRequest(const QUrl& url, const QJsonObject& data, 
                                     std::function<void(QNetworkReply*, bool success, const QString& errorMsg)> callback)
{
    qDebug() << "[SSEClientSocket] sendPostRequest called, URL:" << url.toString();
    
    if (!networkManager_) {
        qWarning() << "[SSEClientSocket] Network manager not initialized";
        callback(nullptr, false, "Network manager not initialized");
        return;
    }
    
    QNetworkRequest request = createRequest(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QJsonDocument doc(data);
    QByteArray postData = doc.toJson(QJsonDocument::Compact);
    
    QNetworkReply* reply = networkManager_->post(request, postData);
    if (!reply) {
        qWarning() << "[SSEClientSocket] Failed to create network request";
        callback(nullptr, false, "Failed to create network request");
        return;
    }
    
    qDebug() << "[SSEClientSocket] Network request created, reply:" << reply;
    
    // 统一处理完成和错误场景
    connect(reply, &QNetworkReply::finished, [this, reply, callback]() {
        qDebug() << "[SSEClientSocket] POST request completed";
        
        bool success = (reply->error() == QNetworkReply::NoError);
        QString errorMsg = success ? "" : reply->errorString();
        
        // 调用回调函数，传递结果
        callback(reply, success, errorMsg);
        
        // 确保reply对象在回调完成后被正确删除
        reply->deleteLater();
    });
}

void SSEClientSocket::sendGetRequest(const QUrl& url, 
                                    std::function<void(QNetworkReply*, bool success, const QString& errorMsg)> callback)
{
    qDebug() << "[SSEClientSocket] sendGetRequest called, URL:" << url.toString();
    
    if (!networkManager_) {
        qWarning() << "[SSEClientSocket] Network manager not initialized";
        callback(nullptr, false, "Network manager not initialized");
        return;
    }
    
    QNetworkRequest request = createRequest(url);
    
    QNetworkReply* reply = networkManager_->get(request);
    if (!reply) {
        qWarning() << "[SSEClientSocket] Failed to create network request";
        callback(nullptr, false, "Failed to create network request");
        return;
    }
    
    qDebug() << "[SSEClientSocket] Network request created, reply:" << reply;
    
    // 统一处理完成和错误场景
    connect(reply, &QNetworkReply::finished, [this, reply, callback]() {
        qDebug() << "[SSEClientSocket] GET request completed"<<reply->error();
        
        bool success = (reply->error() == QNetworkReply::NoError);
        QString errorMsg = success ? "" : reply->errorString();
        
        // 调用回调函数，传递结果
        callback(reply, success, errorMsg);
        
        // 确保reply对象在回调完成后被正确删除
        reply->deleteLater();
    });
}

QJsonObject SSEClientSocket::drawActionToJson(const DrawBlockAction& action) const
{
    QJsonObject obj;
    obj["action"] = action.action;
    obj["layer"] = action.layer;
    obj["clientid"] = action.clientid;
    obj["name"] = action.name;
    
    // 转换 block 数据
    QJsonArray blockArray;
    for (const DrawPoint& point : action.block) {
        QJsonObject pointObj;
        pointObj["x"] = point.x;
        pointObj["y"] = point.y;
        pointObj["pressure"] = point.pressure;
        blockArray.append(pointObj);
    }
    obj["block"] = blockArray;
    
    // 转换 brush 数据
    QJsonObject brushObj;
    brushObj["width"] = action.brush.width;
    brushObj["name"] = action.brush.name;
    brushObj["hardness"] = action.brush.hardness;
    brushObj["thickness"] = action.brush.thickness;
    
    QJsonObject colorObj;
    colorObj["red"] = action.brush.color.red;
    colorObj["green"] = action.brush.color.green;
    colorObj["blue"] = action.brush.color.blue;
    brushObj["color"] = colorObj;
    
    obj["brush"] = brushObj;
    
    return obj;
}

void SSEClientSocket::appendCommonData(QJsonObject& data) const
{
    // 从 RoomInfoManager 获取房间信息
    auto roomInfo = roomInfoManager_->getRoomInfo(roomName_);
    if (roomInfo) {
        data["clientid"] = roomInfo->clientId;
        data["signature"] = roomInfo->signature; // 添加签名
    }
    data["name"] = nickname();
}

void SSEClientSocket::onResponseRoomList(const QJsonObject& response)
{
    auto [isError, errorMessage] = getErrorMessageIfError(response);
    
    if (!isError) {
        QHash<QString, QJsonObject> rooms;
        QJsonArray roomList = response["roomlist"].toArray();
        
        for (const QJsonValue& roomValue : roomList) {
            QJsonObject roomObj = roomValue.toObject();
            QString roomName = roomObj["name"].toString();
            rooms[roomName] = roomObj;
        }
        
        emit roomListFetched(rooms);
        // setState(MANAGER_CONNECTED);
    } else {
        qWarning() << "[SSEClientSocket] Failed to get room list:" << response;
        emit requestRoomListFailed(errorMessage);
        // 失败时重置状态
        // setState(MANAGER_CONNECTED);
    }
}

void SSEClientSocket::onResponseNewRoom(const QJsonObject& response)
{
    qDebug() << "[SSEClientSocket] New room response:" << response;
    
    auto [isError, errorMessage] = getErrorMessageIfError(response);
    
    if (!isError) {
        QJsonObject info = response["info"].toObject();
        roomInfoManager_->registerRoomBasicInfo(roomName_, info);

        // 发出房间创建成功的信号，包含房间信息
        emit roomCreated(info);
    } else {
        qWarning() << "[SSEClientSocket] Failed to create room:" << response;
        emit requestNewRoomFailed(errorMessage);
    }
}

void SSEClientSocket::onResponseLogin(const QJsonObject& response)
{
    qDebug() << "[SSEClientSocket] Login response:" << response;

    auto [isError, errorMessage] = getErrorMessageIfError(response);

    if (!isError) {
        // 使用 RoomInfoManager 处理登录响应
        auto roomInfo = roomInfoManager_->registerRoomBasicInfo(roomName_, response["info"].toObject());

        if (roomInfo) {
            // 发射新客户端ID信号
            emit newClientId(roomInfo->clientId);
            
            // 启动心跳
            startHeartbeat();
            
            // 改进的流程：从登录响应中获取archiveSign
            QString remoteArchiveSign;
            if (response["info"].isObject()) {
                QJsonObject info = response["info"].toObject();
                remoteArchiveSign = info["archivesign"].toString();
                qDebug() << "[SSEClientSocket] 从登录响应获取远程archive签名:" << remoteArchiveSign;
            }
            
            // 发出登录完成信号，包含房间名和远程archive签名
            // 注意：此时不立即建立SSE连接，等待本地历史数据处理完成后再建立
            emit loginCompleted(roomName_, remoteArchiveSign);
        } else {
            qWarning() << "[SSEClientSocket] Login failed: RoomInfoManager returned null" << roomName_ << response;
            emit requestLoginFailed(tr("Failed to process login response"));
        }
    } else {
        qWarning() << "[SSEClientSocket] Login failed:" << roomName_ << response;
        emit requestLoginFailed(errorMessage);
    }
}

void SSEClientSocket::onCommandActionClose(const QJsonObject& data)
{
    emit roomAboutToClose();
}

void SSEClientSocket::onCommandResponseClose(const QJsonObject& data)
{
    // 处理关闭房间响应
}

void SSEClientSocket::onCommandResponseClearAll(const QJsonObject& data)
{
    emit layerAllCleared();
}

void SSEClientSocket::onResponseCheckout(const QJsonObject& data)
{
    // 处理续期响应
}

void SSEClientSocket::onCommandActionClearAll(const QJsonObject& data)
{
    emit layerAllCleared();
}

void SSEClientSocket::onResponseOnlineList(const QJsonObject& data)
{
    QHash<QString, QVariantList> members;
    QJsonArray userList = data["onlinelist"].toArray();
    
    for (const QJsonValue& userValue : userList) {
        QJsonObject userObj = userValue.toObject();
        QString clientId = userObj["clientid"].toString();
        QVariantList userInfo;
        userInfo.append(userObj["name"].toString());
        userInfo.append(userObj["clientid"].toString());
        members[clientId] = userInfo;
    }
    
    emit memberListFetched(members);
}

void SSEClientSocket::onActionNotify(const QJsonObject& data)
{
    QString content = data["content"].toString();
    emit getNotified(content);
}

void SSEClientSocket::onActionKick(const QJsonObject& data)
{
    emit getKicked();
}

void SSEClientSocket::onResponseArchiveSign(const QJsonObject& data)
{
    // 处理存档签名响应
}

void SSEClientSocket::onResponseArchive(const QJsonObject& data)
{
    // 处理存档响应
}

// ==================== 专门的请求方法实现 ====================

void SSEClientSocket::sendGetRoomListRequest(const QUrl& url)
{
    qDebug() << "[SSEClientSocket] Sending get room list request:" << url.toString();
    sendGetRequest(url, [this](QNetworkReply* reply, bool success, const QString& errorMsg) {
        if (!success) {
            qWarning() << "[SSEClientSocket] Failed to get room list:" << errorMsg;
            emit requestRoomListFailed(errorMsg);
            return;
        }
        
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            qWarning() << "[SSEClientSocket] Invalid JSON response";
            return;
        }
        
        QJsonObject response = doc.object();
        onResponseRoomList(response);
    });
}

void SSEClientSocket::sendCreateRoomRequest(const QUrl& url, const QJsonObject& data)
{
    qDebug() << "[SSEClientSocket] Sending create room request:" << url.toString();
    sendPostRequest(url, data, [this](QNetworkReply* reply, bool success, const QString& errorMsg) {
        if (!success) {
            qWarning() << "[SSEClientSocket] Failed to create room:" << errorMsg;
            emit requestNewRoomFailed(errorMsg);
            return;
        }
        
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            qWarning() << "[SSEClientSocket] Invalid JSON response";
            return;
        }
        
        QJsonObject response = doc.object();
        onResponseNewRoom(response);
    });
}

void SSEClientSocket::sendLoginRequest(const QUrl& url, const QJsonObject& data)
{
    qDebug() << "[SSEClientSocket] Sending login request:" << url.toString() << "data:" << data;
    sendPostRequest(url, data, [this](QNetworkReply* reply, bool success, const QString& errorMsg) {
        if (!success) {
            qWarning() << "[SSEClientSocket] Login failed:" << reply->rawHeaderPairs() << errorMsg;
            emit requestLoginFailed(errorMsg);
            return;
        }
        
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            qWarning() << "[SSEClientSocket] Invalid JSON response";
            emit requestLoginFailed(tr("Invalid JSON response"));
            return;
        }
        
        QJsonObject response = doc.object();
        onResponseLogin(response);
    });
}

void SSEClientSocket::sendChatMessageRequest(const QUrl& url, const QJsonObject& data)
{
    qDebug() << "[SSEClientSocket] Sending chat message request:" << url.toString() << "data size:" << data.size() << "keys";
    sendPostRequest(url, data, [this](QNetworkReply* reply, bool success, const QString& errorMsg) {
        if (!success) {
            qWarning() << "[SSEClientSocket] Failed to send chat message:" << errorMsg;
            emit requestChatMessageFailed(errorMsg);
            return;
        }
        
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            qWarning() << "[SSEClientSocket] Invalid JSON response";
            emit requestChatMessageFailed(tr("Invalid JSON response"));
            return;
        }
        
        QJsonObject response = doc.object();
        auto [isError, errorMessage] = getErrorMessageIfError(response);
        
        if (!isError) {
            qDebug() << "[SSEClientSocket] Chat message sent successfully";
        } else {
            qWarning() << "[SSEClientSocket] Failed to send chat message:" << response;
            emit requestChatMessageFailed(errorMessage);
        }
    });
}

void SSEClientSocket::sendDrawDataRequest(const QUrl& url, const QJsonObject& data)
{
    qDebug() << "[SSEClientSocket] Sending drawing data request:" << url.toString();
    sendPostRequest(url, data, [this](QNetworkReply* reply, bool success, const QString& errorMsg) {
        if (!success) {
            qWarning() << "[SSEClientSocket] Failed to send drawing data:" << errorMsg;
            emit requestDrawDataFailed(errorMsg);
            return;
        }
        
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            qWarning() << "[SSEClientSocket] Invalid JSON response";
            emit requestDrawDataFailed(tr("Invalid JSON response"));
            return;
        }
        
        QJsonObject response = doc.object();
        auto [isError, errorMessage] = getErrorMessageIfError(response);
        
        if (!isError) {
            qDebug() << "[SSEClientSocket] Drawing data sent successfully";
        } else {
            qWarning() << "[SSEClientSocket] Failed to send drawing data:" << response;
            emit requestDrawDataFailed(errorMessage);
        }
    });
}

void SSEClientSocket::sendClearCanvasRequest(const QUrl& url, const QJsonObject& data)
{
    qDebug() << "[SSEClientSocket] Sending clear canvas request:" << url.toString();
    sendPostRequest(url, data, [this](QNetworkReply* reply, bool success, const QString& errorMsg) {
        if (!success) {
            qWarning() << "[SSEClientSocket] Failed to clear canvas:" << errorMsg;
            emit requestClearCanvasFailed(errorMsg);
            return;
        }
        
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            qWarning() << "[SSEClientSocket] Invalid JSON response";
            emit requestClearCanvasFailed(tr("Invalid JSON response"));
            return;
        }
        
        QJsonObject response = doc.object();
        auto [isError, errorMessage] = getErrorMessageIfError(response);
        
        if (!isError) {
            qDebug() << "[SSEClientSocket] Canvas cleared successfully";
            emit layerAllCleared();
        } else {
            qWarning() << "[SSEClientSocket] Failed to clear canvas:" << response;
            emit requestClearCanvasFailed(errorMessage);
        }
    });
}

void SSEClientSocket::sendGetOnlineListRequest(const QUrl& url)
{
    // 从 RoomInfoManager 获取房间信息
    auto roomInfo = roomInfoManager_->getRoomInfo(roomName_);
    if (!roomInfo || roomInfo->signature.isEmpty()) {
        qWarning() << "[SSEClientSocket] 无法获取在线用户列表：缺少签名";
        return;
    }
    
    qDebug() << "[SSEClientSocket] Sending get online list request:" << url.toString();
    QJsonObject data;
    appendCommonData(data);
    
    sendPostRequest(
        url, data,
    [this](QNetworkReply *reply, bool success, const QString &errorMsg) {
            if (!success) {
                qWarning() << "[SSEClientSocket] Failed to get online list:" << errorMsg; 
                emit requestOnlineListFailed(errorMsg); 
                return;
            }

            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (!doc.isObject()) {
                qWarning() << "[SSEClientSocket] Invalid JSON response";
                emit requestOnlineListFailed(tr("Invalid JSON response"));
                return;
            }

            QJsonObject response = doc.object();
            auto [isError, errorMessage] = getErrorMessageIfError(response);
            
            if (!isError) {
                onResponseOnlineList(response);
            } else {
                qWarning() << "[SSEClientSocket] Failed to get online list:" << response;
                emit requestOnlineListFailed(errorMessage);
            }
    });
}

void SSEClientSocket::sendCheckoutRequest(const QUrl& url, const QJsonObject& data)
{
    qDebug() << "[SSEClientSocket] Sending room renewal request:" << url.toString();
    sendPostRequest(url, data, [this](QNetworkReply* reply, bool success, const QString& errorMsg) {
        if (!success) {
            qWarning() << "[SSEClientSocket] Failed to renew room:" << errorMsg;
            emit requestCheckoutFailed(errorMsg);
            return;
        }
        
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            qWarning() << "[SSEClientSocket] Invalid JSON response";
            emit requestCheckoutFailed(tr("Invalid JSON response"));
            return;
        }
        
        QJsonObject response = doc.object();
        auto [isError, errorMessage] = getErrorMessageIfError(response);
        
        if (!isError) {
            qDebug() << "[SSEClientSocket] Room renewed successfully";
        } else {
            qWarning() << "[SSEClientSocket] Failed to renew room:" << response;
            emit requestCheckoutFailed(errorMessage);
        }
    });
}

void SSEClientSocket::sendCloseRoomRequest(const QUrl& url, const QJsonObject& data)
{
    qDebug() << "[SSEClientSocket] Sending close room request:" << url.toString();
    sendPostRequest(url, data, [this](QNetworkReply* reply, bool success, const QString& errorMsg) {
        if (!success) {
            qWarning() << "[SSEClientSocket] Failed to close room:" << errorMsg;
            emit requestCloseRoomFailed(errorMsg);
            return;
        }
        
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            qWarning() << "[SSEClientSocket] Invalid JSON response";
            emit requestCloseRoomFailed(tr("Invalid JSON response"));
            return;
        }
        
        QJsonObject response = doc.object();
        auto [isError, errorMessage] = getErrorMessageIfError(response);
        
        if (!isError) {
            qDebug() << "[SSEClientSocket] Room closed successfully";
        } else {
            qWarning() << "[SSEClientSocket] Failed to close room:" << response;
            emit requestCloseRoomFailed(errorMessage);
        }
    });
}

void SSEClientSocket::sendKickUserRequest(const QUrl& url, const QJsonObject& data)
{
    qDebug() << "[SSEClientSocket] Sending kick user request:" << url.toString();
    sendPostRequest(url, data, [this](QNetworkReply* reply, bool success, const QString& errorMsg) {
        if (!success) {
            qWarning() << "[SSEClientSocket] Failed to kick user:" << errorMsg;
            emit requestKickUserFailed(errorMsg);
            return;
        }
        
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            qWarning() << "[SSEClientSocket] Invalid JSON response";
            emit requestKickUserFailed(tr("Invalid JSON response"));
            return;
        }
        
        QJsonObject response = doc.object();
        auto [isError, errorMessage] = getErrorMessageIfError(response);
        
        if (!isError) {
            qDebug() << "[SSEClientSocket] User kicked successfully";
        } else {
            qWarning() << "[SSEClientSocket] Failed to kick user:" << response;
            emit requestKickUserFailed(errorMessage);
        }
    });
}

// ==================== 心跳相关方法实现 ====================

void SSEClientSocket::startHeartbeat()
{
    if (!heartbeatEnabled_ && heartbeatTimer_) {
        qDebug() << "[SSEClientSocket] Starting heartbeat, interval:" << heartbeatInterval_ << "ms";
        heartbeatEnabled_ = true;
        heartbeatTimer_->start(heartbeatInterval_);
    }
}

void SSEClientSocket::stopHeartbeat()
{
    if (heartbeatEnabled_ && heartbeatTimer_) {
        qDebug() << "[SSEClientSocket] Stopping heartbeat";
        heartbeatEnabled_ = false;
        heartbeatTimer_->stop();
    }
}

void SSEClientSocket::sendHeartbeat()
{
    // 从 RoomInfoManager 获取房间信息
    auto roomInfo = roomInfoManager_->getRoomInfo(roomName_);
    if (!heartbeatEnabled_ || roomBaseUrl_.isEmpty() || roomName_.isEmpty() || 
        !roomInfo || roomInfo->clientId.isEmpty() || roomInfo->signature.isEmpty()) {
        return;
    }
    
    qDebug() << "[SSEClientSocket] Sending heartbeat";
    emit heartbeatSent();
    
    QJsonObject request;
    request["clientid"] = roomInfo->clientId;
    request["signature"] = roomInfo->signature; // 添加签名
    
    QUrl url(roomBaseUrl_ + QString("/api/room/%1/heartbeat").arg(roomName_));
    sendHeartbeatRequest(url, request);
}

void SSEClientSocket::sendHeartbeatRequest(const QUrl& url, const QJsonObject& data)
{
    qDebug() << "[SSEClientSocket] Sending heartbeat request:" << url.toString();
    sendPostRequest(url, data, [this](QNetworkReply* reply, bool success, const QString& errorMsg) {
        if (!success) {
            qWarning() << "[SSEClientSocket] Failed to send heartbeat:" << errorMsg;
            emit heartbeatFailed(errorMsg);
            // 心跳失败不触发错误信号，避免影响正常连接
            return;
        }
        
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            qWarning() << "[SSEClientSocket] Invalid heartbeat response";
            return;
        }
        
        QJsonObject response = doc.object();
        onHeartbeatResponse(response);
    });
}

void SSEClientSocket::onHeartbeatResponse(const QJsonObject& response)
{
    if (response["result"].toBool()) {
        qDebug() << "[SSEClientSocket] Heartbeat response successful";
        emit heartbeatReceived();
    } else {
        qWarning() << "[SSEClientSocket] Failed to send heartbeat:" << response;
        emit heartbeatFailed("Server returned failure");
    }
}

// ==================== 心跳控制方法 ====================

void SSEClientSocket::setHeartbeatInterval(int intervalMs)
{
    if (intervalMs > 0) {
        heartbeatInterval_ = intervalMs;
        if (heartbeatEnabled_ && heartbeatTimer_) {
            heartbeatTimer_->setInterval(heartbeatInterval_);
            qDebug() << "[SSEClientSocket] Heartbeat interval updated to:" << heartbeatInterval_ << "ms";
        }
    }
}

int SSEClientSocket::getHeartbeatInterval() const
{
    return heartbeatInterval_;
}

void SSEClientSocket::enableHeartbeat(bool enabled)
{
    if (enabled && !heartbeatEnabled_) {
        startHeartbeat();
    } else if (!enabled && heartbeatEnabled_) {
        stopHeartbeat();
    }
}

bool SSEClientSocket::isHeartbeatEnabled() const
{
    return heartbeatEnabled_;
}

// SSE 事件处理方法实现
void SSEClientSocket::onSSEConnected()
{
    qDebug() << "[SSEClientSocket] SSE connection established";
    
    setState(ROOM_JOINED_SSE);
    
    if (isReconnecting_) {
        qDebug() << "[SSEClientSocket] SSE reconnection detected, starting data synchronization";
        emit reconnectionSucceeded();
        
        // 重连成功后不发射loginCompleted信号，避免重复建立连接
        // 数据同步将在onSSEArchiveEnd中完成
        qDebug() << "[SSEClientSocket] 重连后等待数据同步完成";
    } else {
        // 正常首次连接
        emit roomJoined();
    }
}

void SSEClientSocket::onSSEDisconnected()
{
    qDebug() << "[SSEClientSocket] SSE connection disconnected";
    setState(ROOM_EXITED);
    emit roomExited();
}

void SSEClientSocket::onSSEError(const QString& errorMessage)
{
    qWarning() << "[SSEClientSocket] SSE connection error:" << errorMessage;
    
    // 检查是否是重连失败
    if (errorMessage.contains("maximum reconnect attempts")) {
        isReconnecting_ = false;  // 重连失败，重置标志
        emit reconnectionFailed(errorMessage);
    } else {
        // 设置重连标志并发射重连开始信号
        isReconnecting_ = true;
        emit reconnectionStarted();
    }
    
    emit roomOfflined();
}

void SSEClientSocket::onSSEMessage(const SSEConnection::SSEEvent& event)
{
    QJsonDocument doc = QJsonDocument::fromJson(event.data.toUtf8());
    if (doc.isObject()) {
        QJsonObject data = doc.object();
        emit dataPack(data);
    }
}

void SSEClientSocket::onSSEDraw(const SSEConnection::SSEEvent& event)
{
    QJsonDocument doc = QJsonDocument::fromJson(event.data.toUtf8());
    if (doc.isObject()) {
        QJsonObject data = doc.object();
        
        // 发射数据保存请求信号，由CanvasBackend处理
        emit requestSaveDrawData(data);
        
        emit newDrawData(data);
    }
}

void SSEClientSocket::onSSEChat(const SSEConnection::SSEEvent& event)
{
    QJsonDocument doc = QJsonDocument::fromJson(event.data.toUtf8());
    if (doc.isObject()) {
        QJsonObject data = doc.object();
        emit msgPack(data);
        if (data.contains("content")) {
            QString message = data["content"].toString();
            emit newMessage(message);
        }
    }
}

void SSEClientSocket::onSSEArchive(const SSEConnection::SSEEvent& event)
{
    QJsonDocument doc = QJsonDocument::fromJson(event.data.toUtf8());
    if (doc.isObject()) {
        QJsonObject data = doc.object();
        
        // 从event.id中提取archiveSign
        // 格式: archiveSign-lineIndex
        QString eventId = event.id;
        if (!eventId.isEmpty()) {
            QStringList parts = eventId.split('-');
            if (parts.size() >= 2) {
                QString archiveSign = parts[0];
                if (!archiveSign.isEmpty()) {
                    qDebug() << "[SSEClientSocket] 从archive事件ID中提取archiveSign:" << archiveSign;
                    // 将archiveSign添加到数据中，供CanvasBackend使用
                    data["archiveSign"] = archiveSign;
                }
            }
        }
        
        // 发射archive数据保存请求信号，由CanvasBackend处理
        emit requestSaveArchiveData(data);
        
        emit archiveData(data);
    }
}

void SSEClientSocket::onSSEArchiveEnd(const SSEConnection::SSEEvent& event)
{
    qDebug() << "[SSEClientSocket] Received archive-end event";
    
    // Archive数据保存现在由CanvasBackend负责
    qDebug() << "[SSEClientSocket] Archive数据保存完成";
    
    emit archiveEnd(); // 新增，发射Qt信号
    
    // 如果是重连状态，发射重连完成信号
    qDebug() << "[SSEClientSocket] 检查重连状态，isReconnecting_:" << isReconnecting_;
    if (isReconnecting_) {
        qDebug() << "[SSEClientSocket] 重连数据同步完成，发射reconnectionCompleted信号";
        isReconnecting_ = false;  // 重置重连标志
        emit reconnectionCompleted();
    } else {
        qDebug() << "[SSEClientSocket] 不是重连状态，不发射reconnectionCompleted信号";
    }
}

void SSEClientSocket::onSSEHeartbeat(const SSEConnection::SSEEvent& event)
{
    qDebug() << "[SSEClientSocket] Received heartbeat event";
    emit heartbeatReceived();
}

void SSEClientSocket::onSSENotify(const SSEConnection::SSEEvent& event)
{
    qDebug() << "[SSEClientSocket] Received notification message, original data:" << event.data;
    qDebug() << "[SSEClientSocket] Received notification message, parsed content:" << event.content;
    emit getNotified(event.content);
}

void SSEClientSocket::onSSEKick(const SSEConnection::SSEEvent& event)
{
    qDebug() << "[SSEClientSocket] Received kick notification";
    emit getKicked();
}

void SSEClientSocket::onSSEClearAll(const SSEConnection::SSEEvent& event)
{
    qDebug() << "[SSEClientSocket] Received clear canvas notification, signature:" << event.signature;
    emit layerAllCleared();
}

void SSEClientSocket::onSSECloseRoom(const SSEConnection::SSEEvent& event)
{
    qDebug() << "[SSEClientSocket] Received close room notification, reason:" << event.reason;
    emit roomAboutToClose();
}

void SSEClientSocket::onSSECustomEvent(const QString& eventName, const SSEConnection::SSEEvent& event)
{
    qDebug() << "[SSEClientSocket] Received custom event:" << eventName;
    // 可以根据需要处理自定义事件
}

void SSEClientSocket::resolveManagerAddress(const QString& address)
{
    QMutexLocker locker(&configMutex_);
    QUrl url(address);
    if (!url.isValid()) {
        qWarning() << "[SSEClientSocket] Invalid manager address:" << address;
        emit managerUrlInvalid(tr("Invalid manager address: %1. \nUse default manager address in settings please.").arg(address));
        return;
    }
    
    // 获取主机名
    QString host = url.host();
    if (host.isEmpty()) {
        qWarning() << "[SSEClientSocket] Empty host in manager address:" << address;
        emit managerUrlInvalid(tr("Empty host in manager address: %1").arg(address));
        return;
    }
    
    // 使用Qt的DNS解析能力解析IPv4地址
    QHostInfo::lookupHost(host, this, [this, url](const QHostInfo &hostInfo) {
        if (hostInfo.error() != QHostInfo::NoError) {
            qWarning() << "[SSEClientSocket] DNS lookup failed for host:" << hostInfo.hostName() 
                       << "Error:" << hostInfo.errorString();
            emit managerUrlInvalid(tr("DNS lookup failed for host: %1. Error: %2").arg(hostInfo.hostName(), hostInfo.errorString()));
            return;
        }
        
        // 查找IPv4地址
        QHostAddress ipv4Address;
        for (const QHostAddress &address : hostInfo.addresses()) {
            if (address.protocol() == QAbstractSocket::IPv4Protocol) {
                ipv4Address = address;
                break;
            }
        }
        
        if (ipv4Address.isNull()) {
            qWarning() << "[SSEClientSocket] No IPv4 address found for host:" << hostInfo.hostName();
            emit managerUrlInvalid(tr("No IPv4 address found for host: %1").arg(hostInfo.hostName()));
            return;
        }
        
        // 构建新的URL，用IPv4地址替换主机名
        QUrl newUrl = url;
        newUrl.setHost(ipv4Address.toString());
        managerBaseUrl_ = newUrl.toString();
        
        qDebug() << "[SSEClientSocket] DNS resolved" << hostInfo.hostName() 
                 << "to" << ipv4Address.toString();
        qDebug() << "[SSEClientSocket] Connecting to manager:" << managerBaseUrl_;
        
        // 发送DNS解析完成信号
        emit managerAddressResolved(hostInfo.hostName(), ipv4Address.toString(), managerBaseUrl_);
    });
}

void SSEClientSocket::establishSSEConnection(const QString& roomName, const QString& nickname, const QString& remoteArchiveSign)
{
    qDebug() << "[SSEClientSocket] 建立SSE连接，房间:" << roomName << "昵称:" << nickname << "远程archive签名:" << remoteArchiveSign;
    
    // 建立 SSE 连接（传递archiveSign和lineStart参数）
    setupSSEConnection(roomName, nickname, -1, remoteArchiveSign);
}

void SSEClientSocket::onRequestLineStart(const QString& roomName)
{
    // 如果传入的房间名为空，使用当前房间名
    QString targetRoomName = roomName.isEmpty() ? roomName_ : roomName;
    
    qDebug() << "[SSEClientSocket] Received requestLineStart signal for room:" << targetRoomName;
    
    // 在主线程中安全地获取本地 archive 行数
    quint64 localLineCount = roomInfoManager_->getArchiveLineCount(targetRoomName);
    int lineStart = static_cast<int>(localLineCount);
    
    qDebug() << "[SSEClientSocket] Local archive line count:" << localLineCount << "setting lineStart to:" << lineStart;
    
    // 通过信号槽机制设置 SSE 连接的 lineStart 值
    if (sseConnection_) {
        QMetaObject::invokeMethod(sseConnection_, "setReconnectLineStart", Qt::QueuedConnection, Q_ARG(int, lineStart));
    }
}
