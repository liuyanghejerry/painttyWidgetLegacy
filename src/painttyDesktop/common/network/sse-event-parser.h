#ifndef SSE_EVENT_PARSER_H
#define SSE_EVENT_PARSER_H

#include <QObject>
#include <QByteArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDebug>

/**
 * @brief SSE事件解析器类
 * 
 * 负责解析SSE事件数据，包括：
 * - 事件类型映射
 * - 数据字段解析
 * - JSON数据解析
 * - 特殊字段提取（如archiveSign）
 */
class SSEEventParser : public QObject
{
    Q_OBJECT

public:
    enum EventType {
        Message,    // 默认消息事件
        Draw,       // 绘画数据事件
        Chat,       // 聊天消息事件
        Archive,    // 存档数据事件
        ArchiveEnd, // 存档数据结束事件
        Heartbeat,  // 心跳事件
        Notify,     // 通知消息事件
        Kick,       // 踢出用户事件
        ClearAll,   // 清空画布事件
        CloseRoom,  // 关闭房间事件
        Custom      // 自定义事件
    };

    struct SSEEvent {
        EventType type;
        QString eventName;
        QString data;
        QString id;
        QString archiveSign;
        int retry;
        
        // 通知相关字段
        QString content;      // 通知内容
        QString signature;    // 清空画布签名
        int reason;           // 关闭房间原因 (500 | 501)
        
        SSEEvent() : type(Message), retry(-1), reason(0) {}
    };

    explicit SSEEventParser(QObject *parent = nullptr);
    ~SSEEventParser();

    /**
     * @brief 解析SSE事件数据
     * @param eventData 原始事件数据
     * @return 解析后的事件对象
     */
    SSEEvent parseEvent(const QByteArray& eventData);

private:
    /**
     * @brief 解析事件类型
     * @param eventName 事件名称
     * @return 事件类型枚举值
     */
    EventType parseEventType(const QString& eventName);

    /**
     * @brief 解析事件行
     * @param line 单行事件数据
     * @param event 事件对象引用
     */
    void parseEventLine(const QString& line, SSEEvent& event);

    /**
     * @brief 解析JSON格式的数据
     * @param event 事件对象引用
     */
    void parseJsonData(SSEEvent& event);

    /**
     * @brief 从事件ID中提取archiveSign
     * @param eventId 事件ID
     * @return archiveSign字符串
     */
    QString extractArchiveSign(const QString& eventId);

    /**
     * @brief 从事件ID中提取行索引
     * @param eventId 事件ID
     * @return 行索引字符串
     */
    QString extractLineIndex(const QString& eventId);
};

#endif // SSE_EVENT_PARSER_H 