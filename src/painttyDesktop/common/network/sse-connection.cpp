#include "sse-connection.h"
#include "sse-event-parser.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDebug>
#include <QElapsedTimer>
#include <QDateTime>
#include <QTimer>
#include <QThread>
#include <QUrlQuery>

SSEConnection::SSEConnection(QObject *parent)
    : QObject(parent)
    , networkManager_(nullptr)
    , currentReply_(nullptr)
    , state_(Disconnected)
    , reconnectTimer_(nullptr)
    , reconnectInterval_(5000)  // 默认5秒重连间隔
    , maxReconnectAttempts_(5)
    , currentReconnectAttempts_(0)
    , reconnectEnabled_(true)
    , eventParser_(nullptr)
    , receiveRateTimer_(nullptr)
    , receiveRateLimit_(100)  // 默认每秒100个事件
    , currentReceiveCount_(0)
    , receiveRateLimited_(false)  // 默认不启用速率限制
    , reconnectLineStart_(-1)  // 初始化重连时的 lineStart 值
    , waitingForLineStart_(false)  // 初始化等待标志
{
    // 注意：所有组件将在相应的初始化方法中创建，以确保在正确的线程中
}

SSEConnection::~SSEConnection()
{
    qDebug() << "[SSEConnection] Destructor called";
    
    // 在析构函数中，先停止所有定时器，避免在对象销毁过程中触发回调
    if (reconnectTimer_) {
        reconnectTimer_->stop();
        QObject::disconnect(reconnectTimer_, nullptr, this, nullptr);
        reconnectTimer_->deleteLater();
        reconnectTimer_ = nullptr;
    }
    
    // 清理接收速率控制定时器
    if (receiveRateTimer_) {
        receiveRateTimer_->stop();
        QObject::disconnect(receiveRateTimer_, nullptr, this, nullptr);
        receiveRateTimer_->deleteLater();
        receiveRateTimer_ = nullptr;
    }
    
    // 断开网络连接，但不触发错误处理
    if (currentReply_) {
        // 断开信号连接，避免在对象销毁过程中触发回调
        QObject::disconnect(currentReply_, nullptr, this, nullptr);
        currentReply_->abort();
        currentReply_->deleteLater();
        currentReply_ = nullptr;
    }
    
    // 清理网络管理器
    if (networkManager_) {
        // 断开所有网络请求的信号连接
        QObject::disconnect(networkManager_, nullptr, this, nullptr);
        networkManager_->deleteLater();
        networkManager_ = nullptr;
    }
    
    // 清理事件解析器
    if (eventParser_) {
        eventParser_->deleteLater();
        eventParser_ = nullptr;
    }
    
    // 清理缓冲区
    dataBuffer_.clear();
    lastEventId_.clear();
    pendingEvents_.clear();
    
    // 重置状态，避免在销毁过程中触发状态变更
    state_ = Disconnected;
    currentReconnectAttempts_ = 0;
    currentReceiveCount_ = 0;
    
    qDebug() << "[SSEConnection] Destructor completed";
}

void SSEConnection::setReconnectLineStart(int lineStart)
{
    reconnectLineStart_ = lineStart;
    waitingForLineStart_ = false;  // 标记 lineStart 已设置
    qDebug() << "[SSEConnection] Reconnect lineStart set:" << lineStart;
    
    // 如果正在等待重连，立即开始重连
    if (state_ == Reconnecting) {
        qDebug() << "[SSEConnection] LineStart received, starting reconnection";
        setState(Connecting);
        createConnection();
    }
}

void SSEConnection::connectToUrl(const QUrl& url)
{
    if (state_ == Connected || state_ == Connecting) {
        qWarning() << "[SSEConnection] Connection already exists, disconnecting current connection first";
        disconnect();
        // 等待断开完成
        QThread::msleep(50);
    }
    
    // 确保所有组件已经初始化
    if (!networkManager_ || !eventParser_ || !reconnectTimer_) {
        qDebug() << "[SSEConnection] Components not initialized, initializing now";
        initInThread();
    }
    
    // 重置连接状态
    currentReconnectAttempts_ = 0;
    currentReceiveCount_ = 0;
    connectionUrl_ = url;
    
    setState(Connecting);
    
    qDebug() << "[SSEConnection] Starting connection to:" << url.toString();
    createConnection();
}

void SSEConnection::disconnect()
{
    qDebug() << "[SSEConnection] Disconnecting";
    
    stopReconnectTimer();
    
    if (currentReply_) {
        // 断开信号连接，避免在对象销毁过程中触发回调
        QObject::disconnect(currentReply_, nullptr, this, nullptr);
        currentReply_->abort();
        currentReply_->deleteLater();
        currentReply_ = nullptr;
    }
    
    setState(Disconnected);
    emit disconnected();
}

void SSEConnection::resetState()
{
    qDebug() << "[SSEConnection] Resetting state";
    
    // 停止所有定时器
    stopReconnectTimer();
    if (receiveRateTimer_) {
        receiveRateTimer_->stop();
    }
    
    // 清理网络连接
    if (currentReply_) {
        QObject::disconnect(currentReply_, nullptr, this, nullptr);
        currentReply_->abort();
        currentReply_->deleteLater();
        currentReply_ = nullptr;
    }
    
    // 重置所有状态变量
    state_ = Disconnected;
    currentReconnectAttempts_ = 0;
    currentReceiveCount_ = 0;
    connectionUrl_.clear();
    lastEventId_.clear();
    
    // 清理缓冲区
    dataBuffer_.clear();
    pendingEvents_.clear();
    
    qDebug() << "[SSEConnection] State reset completed";
}

bool SSEConnection::isConnected() const
{
    return state_ == Connected;
}

SSEConnection::ConnectionState SSEConnection::state() const
{
    return state_;
}

void SSEConnection::setReconnectInterval(int interval)
{
    reconnectInterval_ = interval;
}

void SSEConnection::setMaxReconnectAttempts(int attempts)
{
    maxReconnectAttempts_ = attempts;
}

void SSEConnection::setReconnectEnabled(bool enabled)
{
    reconnectEnabled_ = enabled;
}



void SSEConnection::setState(ConnectionState newState)
{
    if (state_ != newState) {
        qDebug() << "[SSEConnection] State changed:" << state_ << "->" << newState;
        state_ = newState;
    }
}

void SSEConnection::createConnection()
{
    if (!networkManager_) {
        emitError("Network manager not initialized");
        return;
    }
    
    // 创建网络请求
    QNetworkRequest request(connectionUrl_);
    request.setRawHeader("Accept", "text/event-stream");
    request.setRawHeader("Cache-Control", "no-cache");
    request.setRawHeader("Connection", "keep-alive");
    
    // 如果有上次的事件ID，添加到请求头
    if (!lastEventId_.isEmpty()) {
        request.setRawHeader("Last-Event-ID", lastEventId_.toUtf8());
    }
    
    // 新增：如果是重连且设置了 lineStart 值，动态更新 URL 中的 lineStart 参数
    if (state_ == Reconnecting && reconnectLineStart_ >= 0) {
        QUrl url = request.url();
        QUrlQuery query(url.query());
        query.removeQueryItem("lineStart");
        query.addQueryItem("lineStart", QString::number(reconnectLineStart_));
        url.setQuery(query);
        request.setUrl(url);
        qDebug() << "[SSEConnection] 重连时更新 lineStart 参数为:" << reconnectLineStart_;
        
        // 重置 lineStart 值，避免下次重连时重复使用
        reconnectLineStart_ = -1;
    }
    
    // 发送GET请求
    qDebug() << "[SSEConnection] Sending GET request to:" << request.url().toString() << "with Last-Event-ID:" << lastEventId_;
    currentReply_ = networkManager_->get(request);
    if (!currentReply_) {
        emitError("Failed to create network request");
        return;
    }
    
    // 连接信号
    connect(currentReply_, &QNetworkReply::readyRead, this, &SSEConnection::onReadyRead);
    connect(currentReply_, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::errorOccurred),
            this, &SSEConnection::onError);
    connect(currentReply_, &QNetworkReply::finished, this, &SSEConnection::onFinished);
    
    // 连接建立成功
    setState(Connected);
    emit connected();
    
    // 如果启用了接收速率限制，启动定时器
    if (receiveRateLimited_ && receiveRateTimer_) {
        receiveRateTimer_->start();
        qDebug() << "[SSEConnection] 启动接收速率控制定时器";
    }
    
    qDebug() << "[SSEConnection] Network request created, connection established";
}

void SSEConnection::onReadyRead()
{
    if (!currentReply_ || currentReply_->isFinished()) {
        return;
    }
    
    // 读取新数据并添加到缓冲区
    QByteArray newData = currentReply_->readAll();
    dataBuffer_.append(newData);
    
    qDebug() << "[SSEConnection] Received data, buffer size:" << dataBuffer_.size();
    
    // 处理缓冲区中的完整事件
    processBuffer();
}

void SSEConnection::onError(QNetworkReply::NetworkError error)
{
    QString errorMsg = currentReply_ ? currentReply_->errorString() : "Unknown network error";
    
    // 对于操作取消错误，不记录为错误，因为这是正常的程序关闭行为
    if (error == QNetworkReply::OperationCanceledError) {
        qDebug() << "[SSEConnection] Network connection canceled (program closed)";
        return;
    }
    
    qWarning() << "[SSEConnection] Network error:" << error << errorMsg;

    // 判断是否为可重连的错误
    bool isRecoverableError = isErrorRecoverable(error);

    emitError(errorMsg);

    if (isRecoverableError && reconnectEnabled_ && currentReconnectAttempts_ < maxReconnectAttempts_) {
        qDebug() << "[SSEConnection] Recoverable error detected, attempting to reconnect";
        setState(Reconnecting);
        startReconnectTimer();
    } else {
        if (!isRecoverableError) {
            qWarning() << "[SSEConnection] Non-recoverable error detected, will not attempt reconnection";
        } else {
            qWarning() << "[SSEConnection] Reconnect attempts exhausted or reconnection disabled";
        }
        setState(Error);
    }
}

void SSEConnection::onFinished()
{
    qDebug() << "[SSEConnection] Network request completed";
    
    if (currentReply_) {
        currentReply_->deleteLater();
        currentReply_ = nullptr;
    }
    
    // 如果不是主动断开，则尝试重连
    if (state_ == Connected && reconnectEnabled_) {
        setState(Reconnecting);
        startReconnectTimer();
    }
}

void SSEConnection::onReconnectTimeout()
{
    if (state_ != Reconnecting) {
        return;
    }
    
    currentReconnectAttempts_++;
    qDebug() << "[SSEConnection] Attempting to reconnect, attempt" << currentReconnectAttempts_ << "of" << maxReconnectAttempts_;
    
    if (currentReconnectAttempts_ <= maxReconnectAttempts_) {
        // 新增：在重连前发出请求获取最新 lineStart 的信号
        qDebug() << "[SSEConnection] Requesting lineStart for reconnection";
        waitingForLineStart_ = true;  // 标记正在等待 lineStart
        emit requestLineStart(QString()); // 发送空字符串，由接收方处理
        
        // 使用定时器检查是否收到 lineStart，避免无限等待
        QTimer::singleShot(2000, this, [this]() {
            if (waitingForLineStart_ && state_ == Reconnecting) {
                qWarning() << "[SSEConnection] LineStart request timeout, proceeding with default value";
                waitingForLineStart_ = false;
                reconnectLineStart_ = -1;  // 使用默认值
                setState(Connecting);
                createConnection();
            }
        });
    } else {
        qWarning() << "[SSEConnection] Reconnect failed, reached maximum reconnect attempts";
        setState(Error);
        emitError("Reconnect failed, reached maximum reconnect attempts");
    }
}

void SSEConnection::processBuffer()
{
    // 查找完整的事件（以双换行符结尾）
    int eventEnd = dataBuffer_.indexOf("\n\n");
    while (eventEnd != -1) {
        // 提取一个完整的事件
        QByteArray eventData = dataBuffer_.left(eventEnd);
        dataBuffer_.remove(0, eventEnd + 2); // 移除事件和双换行符
        
        // 检查是否可以处理事件（速率控制）
        if (receiveRateLimited_ && !canProcessEvent()) {
            // 如果启用了速率限制且当前不能处理，将事件加入待处理队列
            pendingEvents_.enqueue(eventData);
            qDebug() << "[SSEConnection] 事件加入待处理队列，当前队列长度:" << pendingEvents_.size();
        } else {
            // 直接处理事件
            processEvent(eventData);
            if (receiveRateLimited_) {
                currentReceiveCount_++;
            }
        }
        
        // 查找下一个事件
        eventEnd = dataBuffer_.indexOf("\n\n");
    }
    
    // 如果缓冲区太大，可能是数据格式有问题，清空缓冲区
    if (dataBuffer_.size() > 1024 * 1024) { // 1MB 限制
        qWarning() << "[SSEConnection] SSE buffer too large, clearing buffer";
        // dataBuffer_.clear();
    }
}

void SSEConnection::processEvent(const QByteArray& eventData)
{
    SSEEvent event = parseEvent(eventData);
    handleEvent(event);
}

SSEConnection::SSEEvent SSEConnection::parseEvent(const QByteArray& eventData)
{
    if (!eventParser_) {
        qWarning() << "[SSEConnection] Event parser not initialized";
        return SSEEvent();
    }
    
    SSEEvent event = eventParser_->parseEvent(eventData);
    
    // 更新 lastEventId_ 用于重连
    if (!event.id.isEmpty()) {
        lastEventId_ = event.id;
    }
    
    // 如果解析器返回了有效的重连间隔，更新重连配置
    if (event.retry > 0) {
        reconnectInterval_ = event.retry;
    }
    
    return event;
}

void SSEConnection::handleEvent(const SSEEvent& event)
{
    qDebug() << "[SSEConnection] Processing event:" << event.eventName;
    
    // 发送对应信号
    emitEvent(event);
}

void SSEConnection::startReconnectTimer()
{
    if (reconnectTimer_) {
        reconnectTimer_->start(reconnectInterval_);
    }
}

void SSEConnection::stopReconnectTimer()
{
    if (reconnectTimer_) {
        reconnectTimer_->stop();
    }
}



void SSEConnection::emitEvent(const SSEEvent& event)
{
    // 能够正确发送事件说明重连很可能已经成功，此时重置重连次数，以便下次断线后不受历史重连次数影响
    currentReconnectAttempts_ = 0;
    // 发送对应的事件信号
    switch (event.type) {
        case SSEEventParser::Message:
            emit message(event);
            break;
        case SSEEventParser::Draw:
            emit draw(event);
            break;
        case SSEEventParser::Chat:
            emit chat(event);
            break;
        case SSEEventParser::Archive:
            emit archive(event);
            break;
        case SSEEventParser::ArchiveEnd:
            emit archiveEnd(event);
            break;
        case SSEEventParser::Heartbeat:
            emit heartbeat(event);
            break;
        case SSEEventParser::Notify:
            emit notify(event);
            break;
        case SSEEventParser::Kick:
            emit kick(event);
            break;
        case SSEEventParser::ClearAll:
            emit clearAll(event);
            break;
        case SSEEventParser::CloseRoom:
            emit closeRoom(event);
            break;
        case SSEEventParser::Custom:
            emit sseCustomEvent(event.eventName, event);
            break;
    }
}

void SSEConnection::emitError(const QString& errorMessage)
{
    qWarning() << "[SSEConnection] Error:" << errorMessage;
    
    // 发送错误信号
    emit error(errorMessage);
}

void SSEConnection::initInThread()
{
    qDebug() << "[SSEConnection] Initializing all components in thread:" << QThread::currentThread();
    
    // 初始化网络管理器
    if (networkManager_) {
        QObject::disconnect(networkManager_, nullptr, this, nullptr);
        networkManager_->deleteLater();
        networkManager_ = nullptr;
    }
    networkManager_ = new QNetworkAccessManager(this);
    
    // 初始化事件解析器
    if (eventParser_) {
        eventParser_->deleteLater();
        eventParser_ = nullptr;
    }
    eventParser_ = new SSEEventParser(this);
    
    // 初始化重连定时器
    if (reconnectTimer_) {
        reconnectTimer_->stop();
        QObject::disconnect(reconnectTimer_, nullptr, this, nullptr);
        reconnectTimer_->deleteLater();
        reconnectTimer_ = nullptr;
    }
    reconnectTimer_ = new QTimer(this);
    reconnectTimer_->setSingleShot(true);
    connect(reconnectTimer_, &QTimer::timeout, this, &SSEConnection::onReconnectTimeout);
    
    // 初始化接收速率控制定时器
    if (receiveRateTimer_) {
        receiveRateTimer_->stop();
        QObject::disconnect(receiveRateTimer_, nullptr, this, nullptr);
        receiveRateTimer_->deleteLater();
        receiveRateTimer_ = nullptr;
    }
    receiveRateTimer_ = new QTimer(this);
    receiveRateTimer_->setInterval(1000); // 1秒周期
    connect(receiveRateTimer_, &QTimer::timeout, this, &SSEConnection::onReceiveRateTimeout);
    
    // 如果启用了接收速率限制，启动定时器
    if (receiveRateLimited_) {
        receiveRateTimer_->start();
        qDebug() << "[SSEConnection] 初始化时启动接收速率控制定时器";
    }
    
    qDebug() << "[SSEConnection] All components initialized successfully";
}

void SSEConnection::connectToUrlInThread(const QUrl& url)
{
    qDebug() << "[SSEConnection] Connecting to URL in thread:" << QThread::currentThread();
    connectToUrl(url);
}

// 接收速率控制相关方法实现
void SSEConnection::setReceiveRateLimit(int eventsPerSecond)
{
    if (eventsPerSecond > 0) {
        receiveRateLimit_ = eventsPerSecond;
        qDebug() << "[SSEConnection] 设置接收速率限制为每秒" << eventsPerSecond << "个事件";
    }
}

void SSEConnection::enableReceiveRateLimit(bool enabled)
{
    receiveRateLimited_ = enabled;
    if (enabled) {
        // 确保接收速率定时器存在并启动
        if (!receiveRateTimer_) {
            // 创建接收速率定时器
            receiveRateTimer_ = new QTimer(this);
            receiveRateTimer_->setInterval(1000); // 1秒周期
            connect(receiveRateTimer_, &QTimer::timeout, this, &SSEConnection::onReceiveRateTimeout);
        }
        receiveRateTimer_->start();
        qDebug() << "[SSEConnection] 启用接收速率限制";
    } else if (receiveRateTimer_) {
        // 停止定时器
        receiveRateTimer_->stop();
        qDebug() << "[SSEConnection] 禁用接收速率限制";
    }
}

int SSEConnection::getReceiveRateLimit() const
{
    return receiveRateLimit_;
}

bool SSEConnection::isReceiveRateLimited() const
{
    return receiveRateLimited_;
}

void SSEConnection::onReceiveRateTimeout()
{
    qDebug() << "[SSEConnection] 接收速率定时器超时，重置计数器";
    
    // 重置计数器
    currentReceiveCount_ = 0;
    
    // 处理待处理队列中的事件
    processPendingEvents();
}

void SSEConnection::processPendingEvents()
{
    if (pendingEvents_.isEmpty()) {
        return;
    }
    
    qDebug() << "[SSEConnection] 处理待处理队列中的" << pendingEvents_.size() << "个事件";
    
    int processedCount = 0;
    // 处理待处理队列中的事件，但不超过速率限制
    while (!pendingEvents_.isEmpty() && canProcessEvent()) {
        QByteArray eventData = pendingEvents_.dequeue();
        processEvent(eventData);
        currentReceiveCount_++;
        processedCount++;
    }
    
    qDebug() << "[SSEConnection] 本次处理了" << processedCount << "个事件，当前接收计数:" << currentReceiveCount_;
    
    if (!pendingEvents_.isEmpty()) {
        qDebug() << "[SSEConnection] 待处理队列中还有" << pendingEvents_.size() << "个事件等待处理";
    }
}

bool SSEConnection::canProcessEvent()
{
    return !receiveRateLimited_ || currentReceiveCount_ < receiveRateLimit_;
}

bool SSEConnection::isErrorRecoverable(QNetworkReply::NetworkError error) const
{
    // 可重连的错误类型 - 通常是临时的网络问题
    switch (error) {
        // 连接相关的临时错误
        case QNetworkReply::ConnectionRefusedError:
        case QNetworkReply::RemoteHostClosedError:
        case QNetworkReply::HostNotFoundError:
        case QNetworkReply::TimeoutError:
        case QNetworkReply::TemporaryNetworkFailureError:
        case QNetworkReply::NetworkSessionFailedError:
        case QNetworkReply::BackgroundRequestNotAllowedError:
            return true;
            
        // 代理相关的临时错误
        case QNetworkReply::ProxyConnectionRefusedError:
        case QNetworkReply::ProxyConnectionClosedError:
        case QNetworkReply::ProxyNotFoundError:
        case QNetworkReply::ProxyTimeoutError:
            return true;
            
        // 重定向相关的可能临时错误
        case QNetworkReply::TooManyRedirectsError:
        case QNetworkReply::InsecureRedirectError:
            return true;
            
        // 不可重连的错误类型 - 通常是配置或权限问题
        case QNetworkReply::AuthenticationRequiredError:
        case QNetworkReply::ContentAccessDenied:
        case QNetworkReply::ContentOperationNotPermittedError:
        case QNetworkReply::ContentNotFoundError:
        case QNetworkReply::ContentReSendError:
        case QNetworkReply::ContentConflictError:
        case QNetworkReply::ContentGoneError:
        case QNetworkReply::InternalServerError:
        case QNetworkReply::OperationNotImplementedError:
        case QNetworkReply::ServiceUnavailableError:
        case QNetworkReply::ProtocolUnknownError:
        case QNetworkReply::ProtocolInvalidOperationError:
        case QNetworkReply::UnknownNetworkError:
        case QNetworkReply::UnknownProxyError:
        case QNetworkReply::UnknownContentError:
        case QNetworkReply::ProtocolFailure:
        case QNetworkReply::UnknownServerError:
        case QNetworkReply::ProxyAuthenticationRequiredError:
            return false;
            
        // 默认情况下，对于未知错误，不尝试重连以避免无限循环
        default:
            qWarning() << "[SSEConnection] Unknown network error encountered:" << error;
            return false;
    }
} 