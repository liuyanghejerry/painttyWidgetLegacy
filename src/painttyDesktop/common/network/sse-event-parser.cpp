#include "sse-event-parser.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

SSEEventParser::SSEEventParser(QObject *parent)
    : QObject(parent)
{
}

SSEEventParser::~SSEEventParser()
{
}

SSEEventParser::SSEEvent SSEEventParser::parseEvent(const QByteArray& eventData)
{
    SSEEvent event;
    QString eventStr = QString::fromUtf8(eventData);
    QStringList lines = eventStr.split('\n', Qt::SkipEmptyParts);
    
    // 解析每一行事件数据
    for (const QString& line : lines) {
        parseEventLine(line, event);
    }
    
    // 解析JSON格式的数据
    parseJsonData(event);
    
    return event;
}

SSEEventParser::EventType SSEEventParser::parseEventType(const QString& eventName)
{
    // 使用映射表来简化事件类型判断
    static const QHash<QString, EventType> eventTypeMap = {
        {"draw", Draw},
        {"chat", Chat},
        {"archive", Archive},
        {"archive-end", ArchiveEnd},
        {"heartbeat", Heartbeat},
        {"notify", Notify},
        {"kick", Kick},
        {"clearall", ClearAll},
        {"close", CloseRoom}
    };
    
    return eventTypeMap.value(eventName, Custom);
}

void SSEEventParser::parseEventLine(const QString& line, SSEEvent& event)
{
    if (line.startsWith("event: ")) {
        event.eventName = line.mid(7).trimmed();
        event.type = parseEventType(event.eventName);
    } else if (line.startsWith("data: ")) {
        event.data = line.mid(6).trimmed();
    } else if (line.startsWith("id: ")) {
        QString eventId = line.mid(4).trimmed();
        event.id = eventId;
        
            // 对于archive事件，从id中解析archiveSign
    if (event.type == SSEEventParser::Archive && !eventId.isEmpty()) {
            event.archiveSign = extractArchiveSign(eventId);
            event.id = extractLineIndex(eventId);
            
            if (!event.archiveSign.isEmpty()) {
                qDebug() << "[SSEEventParser] Extracted archiveSign:" << event.archiveSign << "and lineIndex:" << event.id;
            } else {
                qDebug() << "[SSEEventParser] Event id is not in the correct format:" << eventId;
            }
        }
    } else if (line.startsWith("retry: ")) {
        bool isOk = false;
        event.retry = line.mid(7).trimmed().toInt(&isOk);
        if (!isOk || event.retry <= 0) {
            event.retry = -1; // 无效值
        }
    }
}

void SSEEventParser::parseJsonData(SSEEvent& event)
{
    if (event.data.isEmpty() || !event.data.startsWith('{')) {
        // 如果 notify 事件的数据不是 JSON 格式，直接使用数据作为内容
        if (event.type == SSEEventParser::Notify && !event.data.isEmpty()) {
            qDebug() << "[SSEEventParser] Notify event data is not JSON format, using directly:" << event.data;
            event.content = event.data;
        }
        return;
    }
    
    // qDebug() << "[SSEEventParser] Parsing JSON format data:" << event.data;
    
    QJsonDocument doc = QJsonDocument::fromJson(event.data.toUtf8());
    if (!doc.isObject()) {
        qWarning() << "[SSEEventParser] Failed to parse JSON data";
        return;
    }
    
    QJsonObject obj = doc.object();
    
    // 根据事件类型解析不同的JSON字段
    switch (event.type) {
        case SSEEventParser::Notify:
            if (obj.contains("content")) {
                event.content = obj["content"].toString();
                qDebug() << "[SSEEventParser] Extracting content from JSON:" << event.content;
            }
            break;
            
        case SSEEventParser::ClearAll:
            if (obj.contains("signature")) {
                event.signature = obj["signature"].toString();
            }
            break;
            
        case SSEEventParser::CloseRoom:
            if (obj.contains("data")) {
                event.reason = obj["data"].toObject()["reason"].toInt();
            }
            break;
            
        default:
            // 其他事件类型暂时不需要特殊处理
            break;
    }
}

QString SSEEventParser::extractArchiveSign(const QString& eventId)
{
    // 根据服务端实现，id格式为 "archiveSign-lineIndex"
    int dashIndex = eventId.indexOf('-');
    if (dashIndex > 0) {
        return eventId.left(dashIndex);
    }
    return QString();
}

QString SSEEventParser::extractLineIndex(const QString& eventId)
{
    // 根据服务端实现，id格式为 "archiveSign-lineIndex"
    int dashIndex = eventId.indexOf('-');
    if (dashIndex > 0) {
        return eventId.right(eventId.length() - dashIndex - 1);
    }
    return eventId; // 如果没有分隔符，返回原始ID
} 