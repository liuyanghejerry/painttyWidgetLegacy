#ifndef SSE_CLIENTSOCKET_H
#define SSE_CLIENTSOCKET_H

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QSize>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>
#include <QHostAddress>
#include <QHostInfo>
#include <QAbstractSocket>
#include <QHash>
#include <QVariantList>
#include <QThread>
#include <QMutex>
#include <QMutexLocker>
#include <functional>
#include "sse-connection.h"
#include "../room-info-manager.h"

/**
 * @brief SSE-based client socket that communicates with paintty server via HTTP/SSE
 * 
 * This class provides a Server-Sent Events based implementation for connecting to
 * paintty rooms, similar to the original socket-based ClientSocket but using web APIs.
 * 
 * SSEConnection runs in a dedicated worker thread to avoid blocking the main UI thread.
 */
class SSEClientSocket : public QObject
{
    Q_OBJECT

public:
    // 状态枚举
    enum State {
        INIT,
        REQUESTING_ROOMLIST,
        REQUESTING_NEWROOM,
        CONNECTING_ROOM,
        JOINING_ROOM,
        ROOM_JOINED,
        ROOM_JOINED_SSE,
        ROOM_EXITED,
        ROOM_OFFLINED
    };

    /**
     * @brief 绘画动作数据结构
     */
    struct DrawPoint {
        double x;
        double y;
        double pressure;
    };

    struct BrushColor {
        int red;
        int green;
        int blue;
    };

    struct BrushInfo {
        double width;
        BrushColor color;
        QString name;
        double hardness;
        double thickness;
    };

    struct DrawBlockAction {
        QString action;
        QString layer;
        QString clientid;
        QString name;
        QList<DrawPoint> block;
        BrushInfo brush;
    };

    explicit SSEClientSocket(RoomInfoManager* roomInfoManager, QObject *parent = nullptr);
    ~SSEClientSocket();

    // 连接管理
    void resolveManagerAddress(const QString& addr);
    void disconnect();
    void connectToRoom(const QString& roomName, const QString& nickname, 
                       const QString& password = QString(), int lineStart = -1);
    void exitRoom();
    
    // 房间操作
    void tryJoinRoom(const QString& urlBase, const QString& roomName, const QString& password);
    void requestRoomList();
    void requestNewRoom(const QJsonObject& roomInfo);
    void exitFromRoom();
    void close();
    
    // 房间内操作
    void sendMessage(const QString& content);
    void sendChatMessage(const QString& content);
    void sendDataPack(const QByteArray& content);
    void sendDataPack(const QJsonObject& content);
    void sendCmdPack(const QJsonObject& content);
    void sendManagerPack(const QJsonObject& content);
    void sendDrawData(const DrawBlockAction& data);
    void clearCanvas();
    void requestOnlinelist();
    bool requestCheckout();
    bool requestCloseRoom();
    bool requestKickUser(const QString& id);
    void requestArchive();
    void requestArchiveSign();
    
    // 心跳控制
    void setHeartbeatInterval(int intervalMs);
    int getHeartbeatInterval() const;
    void enableHeartbeat(bool enabled);
    bool isHeartbeatEnabled() const;

    // 状态查询
    State currentState() const;
    bool isConnected() const;
    bool isInRoom() const;
    bool isRoomOwner() const;

    // 数据获取
    QString getClientIdOfCurrentRoom() const;
    QString nickname() const;
    QString roomName() const;
    QSize canvasSize() const;
    int getDelay() const;
    QString toUrl() const;
    QString archiveSignature() const;
    
    // 获取房间信息管理器
    RoomInfoManager* roomInfoManager() const;

    // 配置设置
    void setUserName(const QString& name);
    QString address() const;

    // 心跳相关方法
    void startHeartbeat();
    void stopHeartbeat();
    void sendHeartbeat();
    void sendHeartbeatRequest(const QUrl& url, const QJsonObject& data);
    void onHeartbeatResponse(const QJsonObject& response);
    
    // 新增：公开的SSE连接建立方法
    void establishSSEConnection(const QString& roomName, const QString& nickname, const QString& remoteArchiveSign);

signals:
    // 连接状态信号
    void managerConnected();
    void roomListFetched(const QHash<QString, QJsonObject>& rooms);
    void roomCreated(const QJsonObject& roomInfo);
    void roomJoined();
    void roomOfflined();
    void roomExited();
    void roomAboutToClose();
    
    // 数据信号
    void dataPack(const QJsonObject& data);
    void msgPack(const QJsonObject& data);
    void cmdPack(const QJsonObject& data);
    void managerPack(const QJsonObject& data);
    void newDrawData(const QJsonObject& data);
    void newMessage(const QString& message);
    void memberListFetched(const QHash<QString, QVariantList>& members);
    
    // 错误信号
    void managerUrlInvalid(const QString &errorMessage);
    void managerAddressResolved(const QString &originalHost, const QString &resolvedIp, const QString &finalUrl);
    void requestRoomListFailed(const QString &errorMessage);
    void requestNewRoomFailed(const QString &errorMessage);
    void requestLoginFailed(const QString &errorMessage);
    void requestChatMessageFailed(const QString &errorMessage);
    void requestDrawDataFailed(const QString &errorMessage);
    void requestClearCanvasFailed(const QString &errorMessage);
    void requestOnlineListFailed(const QString &errorMessage);
    void requestCheckoutFailed(const QString &errorMessage);
    void requestCloseRoomFailed(const QString &errorMessage);
    void requestKickUserFailed(const QString &errorMessage);
    void requestUnauthed();
    
    // 心跳信号
    void heartbeatSent();
    void heartbeatReceived();
    void heartbeatFailed(const QString& error);
    
    // 房间事件信号
    void layerAllCleared();
    void getNotified(const QString& content);
    void getKicked();
    void delayGet(int delay);

    void newClientId(const QString&);
    void archiveEnd();
    void archiveData(const QJsonObject& data);
    void loginCompleted(const QString& roomName, const QString& remoteArchiveSign);
    
    // 新增：重连相关信号
    void reconnectionStarted();
    void reconnectionSucceeded(); 
    void reconnectionFailed(const QString& reason);
    void reconnectionCompleted(); // 重连后数据同步完成
    
    // 新增：数据保存请求信号
    void requestSaveDrawData(const QJsonObject& data);
    void requestSaveArchiveData(const QJsonObject& data);

private:
    // SSEConnection 线程相关
    QThread* sseThread_;
    SSEConnection* sseConnection_;
    
    // 线程安全保护
    mutable QMutex stateMutex_;      // 保护状态相关数据
    mutable QMutex configMutex_;     // 保护配置相关数据
    mutable QMutex dataMutex_;       // 保护其他数据
    
    // 房间信息管理器
    RoomInfoManager* roomInfoManager_;
    
    // 连接配置
    QString managerHost_;
    QString managerBaseUrl_;
    QString roomBaseUrl_;
    
    // 房间状态
    QString nickname_;
    QString roomName_;
    State state_;
    QAtomicInt roomDelay_;
    
    // 网络组件（主线程）
    QNetworkAccessManager* networkManager_;
    
    // SSE 数据缓冲区
    QByteArray sseBuffer_;
    
    // 心跳相关（主线程）
    QTimer* heartbeatTimer_;
    int heartbeatInterval_;  // 心跳间隔（毫秒）
    bool heartbeatEnabled_;
    
    // 重连状态跟踪
    bool isReconnecting_;
    
    // 常量
    static const int WAIT_TIME = 1000;
    
    // 私有方法
    void initializeNetworkManager();
    void initializeClientIdAndNickname();
    void loginToRoom(const QString& roomName, const QString& nickname, const QString& password);
    void setupSSEConnection(const QString& roomName, const QString& nickname, int lineStart, const QString& archiveSign = QString());
    void setState(State newState);
    void resetConnection();
    
    // 网络请求方法
    QNetworkRequest createRequest(const QUrl& url);
    void sendPostRequest(const QUrl& url, const QJsonObject& data, 
                         std::function<void(QNetworkReply*, bool success, const QString& errorMsg)> callback);
    void sendGetRequest(const QUrl& url, 
                        std::function<void(QNetworkReply*, bool success, const QString& errorMsg)> callback);
    
    // 专门的请求方法实现
    void sendGetRoomListRequest(const QUrl& url);
    void sendCreateRoomRequest(const QUrl& url, const QJsonObject& data);
    void sendLoginRequest(const QUrl& url, const QJsonObject& data);
    void sendChatMessageRequest(const QUrl& url, const QJsonObject& data);
    void sendDrawDataRequest(const QUrl& url, const QJsonObject& data);
    void sendClearCanvasRequest(const QUrl& url, const QJsonObject& data);
    void sendGetOnlineListRequest(const QUrl& url);
    void sendCheckoutRequest(const QUrl& url, const QJsonObject& data);
    void sendCloseRoomRequest(const QUrl& url, const QJsonObject& data);
    void sendKickUserRequest(const QUrl& url, const QJsonObject& data);
    
    // 数据转换方法
    QJsonObject drawActionToJson(const DrawBlockAction& action) const;
    void appendCommonData(QJsonObject& data) const;
    
    // 响应处理方法
    void onResponseRoomList(const QJsonObject& response);
    void onResponseNewRoom(const QJsonObject& response);
    void onResponseLogin(const QJsonObject& response);
    void onResponseOnlineList(const QJsonObject& response);
    void onResponseCheckout(const QJsonObject& response);
    void onResponseArchiveSign(const QJsonObject& response);
    void onResponseArchive(const QJsonObject& response);
    
    // 命令处理方法
    void onCommandActionClose(const QJsonObject& data);
    void onCommandResponseClose(const QJsonObject& data);
    void onCommandResponseClearAll(const QJsonObject& data);
    void onCommandResponseCheckout(const QJsonObject& data);
    void onCommandResponseOnlinelist(const QJsonObject& data);
    void onCommandActionClearAll(const QJsonObject& data);
    
    // 动作处理方法
    void onActionNotify(const QJsonObject& data);
    void onActionKick(const QJsonObject& data);
    
    // SSE 事件处理方法
    void onSSEMessage(const SSEConnection::SSEEvent& event);
    void onSSEDraw(const SSEConnection::SSEEvent& event);
    void onSSEChat(const SSEConnection::SSEEvent& event);
    void onSSEArchive(const SSEConnection::SSEEvent& event);
    void onSSEArchiveEnd(const SSEConnection::SSEEvent& event);
    void onSSEHeartbeat(const SSEConnection::SSEEvent& event);
    void onSSENotify(const SSEConnection::SSEEvent& event);
    void onSSEKick(const SSEConnection::SSEEvent& event);
    void onSSEClearAll(const SSEConnection::SSEEvent& event);
    void onSSECloseRoom(const SSEConnection::SSEEvent& event);
    void onSSECustomEvent(const QString& eventName, const SSEConnection::SSEEvent& event);
    
    // SSE 连接状态处理方法
    void onSSEConnected();
    void onSSEDisconnected();
    void onSSEError(const QString& errorMessage);

private slots:
    // 新增：处理 lineStart 请求的槽函数
    void onRequestLineStart(const QString& roomName);
};

#endif // SSE_CLIENTSOCKET_H 