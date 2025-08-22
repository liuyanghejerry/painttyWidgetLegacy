#ifndef SSE_CONNECTION_H
#define SSE_CONNECTION_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>
#include <QByteArray>
#include <QTimer>
#include <QJsonObject>
#include <QQueue>
#include "sse-event-parser.h"

/**
 * @brief SSE 连接类
 * 
 * 负责管理与服务器的 SSE 连接，包括连接建立、数据接收、重连等功能
 */
class SSEConnection : public QObject
{
    Q_OBJECT

public:
    enum ConnectionState {
        Disconnected,
        Connecting,
        Connected,
        Reconnecting,
        Error
    };

    // 使用解析器中的类型定义
    using EventType = SSEEventParser::EventType;
    using SSEEvent = SSEEventParser::SSEEvent;

    explicit SSEConnection(QObject *parent = nullptr);
    ~SSEConnection();

    // 连接管理
    void connectToUrl(const QUrl& url);
    bool isConnected() const;
    ConnectionState state() const;

    // 配置选项
    void setReconnectInterval(int interval);
    void setMaxReconnectAttempts(int attempts);
    void setReconnectEnabled(bool enabled);
    
    // 接收速率控制
    int getReceiveRateLimit() const;
    bool isReceiveRateLimited() const;

    // 新增：检查是否可以开始重连
    bool canStartReconnect() const;

public slots:
    // 新增：设置重连时的 lineStart 值
    void setReconnectLineStart(int lineStart);

    // 接收速率控制
    void setReceiveRateLimit(int eventsPerSecond);
    void enableReceiveRateLimit(bool enabled);
    // 线程安全的组件初始化（在目标线程中调用）
    void initInThread();
    // 线程安全的连接方法（在目标线程中调用）
    void connectToUrlInThread(const QUrl& url);
    void disconnect();
    // 重置连接状态
    void resetState();

  signals:
    void connected();
    void disconnected();
    void error(const QString& errorMessage);
    void message(const SSEEvent& event);
    void draw(const SSEEvent& event);
    void chat(const SSEEvent& event);
    void archive(const SSEEvent& event);
    void archiveEnd(const SSEEvent& event);
    void heartbeat(const SSEEvent& event);
    void notify(const SSEEvent& event);
    void kick(const SSEEvent& event);
    void clearAll(const SSEEvent& event);
    void closeRoom(const SSEEvent& event);
    void sseCustomEvent(const QString& eventName, const SSEEvent& event);

    // 新增：请求获取 lineStart 值的信号
    void requestLineStart(const QString& roomName);

private slots:
    void onReadyRead();
    void onError(QNetworkReply::NetworkError error);
    void onFinished();
    void onReconnectTimeout();

private:
    // 网络组件
    QNetworkAccessManager* networkManager_;
    QNetworkReply* currentReply_;
    
    // 连接状态
    ConnectionState state_;
    QUrl connectionUrl_;
    
    // 重连机制
    QTimer* reconnectTimer_;
    int reconnectInterval_;
    int maxReconnectAttempts_;
    int currentReconnectAttempts_;
    bool reconnectEnabled_;
    
    // 数据缓冲区
    QByteArray dataBuffer_;
    QString lastEventId_;
    
    // 事件解析器
    SSEEventParser* eventParser_;
    
    // 接收速率控制
    QTimer* receiveRateTimer_;
    int receiveRateLimit_;  // 每秒最大接收事件数
    int currentReceiveCount_;  // 当前周期内已接收的事件数
    QQueue<QByteArray> pendingEvents_;  // 待处理事件队列
    bool receiveRateLimited_;  // 是否启用接收速率限制
    
    // 新增：重连时的 lineStart 值
    int reconnectLineStart_;
    
    // 新增：是否正在等待 lineStart 设置
    bool waitingForLineStart_;
    
    // 私有方法
    void setState(ConnectionState newState);
    void createConnection();
    void processBuffer();
    void processEvent(const QByteArray& eventData);
    SSEEvent parseEvent(const QByteArray& eventData);
    void handleEvent(const SSEEvent& event);
    void startReconnectTimer();
    void stopReconnectTimer();
    void emitEvent(const SSEEvent& event);
    void emitError(const QString& errorMessage);
    void cleanup();
    
    // 错误处理辅助方法
    bool isErrorRecoverable(QNetworkReply::NetworkError error) const;
    
    // 接收速率控制相关方法
    void onReceiveRateTimeout();
    void processPendingEvents();
    bool canProcessEvent();
};

#endif // SSE_CONNECTION_H 