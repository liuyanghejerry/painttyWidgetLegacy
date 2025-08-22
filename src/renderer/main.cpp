#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QDebug>
#include <QDir>
#include <QColor>

// 包含笔刷相关头文件
#include "../painttyDesktop/paintingTools/brush/basicbrushv3.h"

class SimpleTabletRenderer
{
public:
    explicit SimpleTabletRenderer() {}
    
    bool renderFromEventsJson(const QString &jsonFile, const QString &outputFile, 
                             int width = 2880, int height = 1920,
                             const QVariantMap &brushSettings = QVariantMap())
    {
        // 读取JSON文件
        QFile file(jsonFile);
        if (!file.open(QIODevice::ReadOnly)) {
            qCritical() << "无法打开JSON文件:" << jsonFile;
            return false;
        }
        
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError) {
            qCritical() << "JSON解析错误:" << error.errorString();
            return false;
        }
        
        if (!doc.isObject()) {
            qCritical() << "JSON文件格式错误：根元素必须是对象";
            return false;
        }
        
        QJsonObject root = doc.object();
        
        // 创建图像
        QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        
        // 创建Painter
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        
        // 创建BasicBrushV3实例
        BasicBrushV3 brush;
        brush.setColor(Qt::black);
        brush.setWidth(12);  // 增加笔刷宽度，确保笔迹更明显
        
        // 配置V3笔刷系统的参数
        QVariantMap settings = brush.defaultSettings();
        
        // 使用命令行参数或默认值
        settings["outline"] = brushSettings.value("outline", "circle");
        settings["texture"] = brushSettings.value("texture", "solid");
        settings["spacing"] = brushSettings.value("spacing", 0.3).toReal();
        settings["smoothing"] = brushSettings.value("smoothing", 0.4).toReal();
        settings["taperLength"] = brushSettings.value("taperLength", 0.1).toReal();
        settings["colorVariation"] = brushSettings.value("colorVariation", 0.05).toReal();
        
        brush.setSettings(settings);
        
        qDebug() << "笔刷配置:";
        qDebug() << "  轮廓:" << settings["outline"].toString();
        qDebug() << "  纹理:" << settings["texture"].toString();
        qDebug() << "  间距:" << settings["spacing"].toReal();
        qDebug() << "  平滑:" << settings["smoothing"].toReal();
        qDebug() << "  锥度:" << settings["taperLength"].toReal();
        qDebug() << "  颜色变化:" << settings["colorVariation"].toReal();
        
        // 处理strokes数据
        if (root.contains("strokes")) {
            QJsonArray strokesArray = root["strokes"].toArray();
            qDebug() << "找到" << strokesArray.size() << "个笔画";
            
            for (const QJsonValue &strokeValue : strokesArray) {
                QJsonObject strokeObj = strokeValue.toObject();
                qDebug() << "处理笔画" << strokeObj["stroke_id"].toInt();
                processStroke(brush, painter, strokeObj);
            }
        }
        
        painter.end();
        
        // 保存到PNG文件
        if (!image.save(outputFile, "PNG")) {
            qCritical() << "无法保存PNG文件:" << outputFile;
            return false;
        }
        
        qDebug() << "成功渲染图像到:" << outputFile;
        qDebug() << "图像尺寸:" << image.size();
        
        return true;
    }
    
private:
    void processStroke(BasicBrushV3 &brush, QPainter &painter, const QJsonObject &strokeObj)
    {
        if (!strokeObj.contains("events")) {
            qDebug() << "笔画没有事件数据";
            return;
        }
        
        QJsonArray eventsArray = strokeObj["events"].toArray();
        qDebug() << "笔画包含" << eventsArray.size() << "个事件";
        
        int pressCount = 0, moveCount = 0, releaseCount = 0;
        
        for (const QJsonValue &eventValue : eventsArray) {
            QJsonObject eventObj = eventValue.toObject();
            QString eventType = eventObj["event_type"].toString();
            
            if (eventType == "TabletPress" || eventType == "TabletMove") {
                // 处理按下和移动事件
                QJsonObject positionObj = eventObj["position"].toObject();
                int x = positionObj["x"].toInt();
                int y = positionObj["y"].toInt();
                qreal pressure = eventObj["pressure"].toDouble();
                
                // 获取倾斜角度
                QJsonObject tiltObj = eventObj["tilt"].toObject();
                qreal tiltX = tiltObj["x"].toDouble();
                qreal tiltY = tiltObj["y"].toDouble();
                
                // 创建压感点并添加到笔刷路径
                PressurePoint point(QPointF(x, y), pressure, tiltX, tiltY);
                brush.addPointToCurrentPath(point);
                
                if (eventType == "TabletPress") pressCount++;
                else moveCount++;
                
            } else if (eventType == "TabletRelease") {
                // 处理释放事件，结束笔画并渲染
                brush.endStroke();
                brush.drawAllPaths(&painter);
                brush.clearAllPaths();
                releaseCount++;
            }
        }
        
        qDebug() << "事件统计: Press=" << pressCount << "Move=" << moveCount << "Release=" << releaseCount;
        
        // 如果还有未处理的路径，也进行渲染
        if (!brush.currentPath().isEmpty()) {
            qDebug() << "渲染未完成的路径，点数:" << brush.currentPath().size();
            brush.endStroke();
            brush.drawAllPaths(&painter);
            brush.clearAllPaths();
        }
    }
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("SimpleTabletRenderer");
    app.setApplicationVersion("1.0");
    
    QCommandLineParser parser;
    parser.setApplicationDescription("使用AbstractBrushV3系统从events.json文件渲染tablet事件到PNG图片");
    parser.addHelpOption();
    parser.addVersionOption();
    
    QCommandLineOption inputOption(QStringList() << "i" << "input",
                                  "输入events.json文件路径", "file");
    parser.addOption(inputOption);
    
    QCommandLineOption outputOption(QStringList() << "o" << "output",
                                   "输出PNG文件路径", "file");
    parser.addOption(outputOption);
    
    QCommandLineOption widthOption("width",
                                  "画布宽度 (默认: 720)", "width");
    parser.addOption(widthOption);
    
    QCommandLineOption heightOption("height",
                                   "画布高度 (默认: 480)", "height");
    parser.addOption(heightOption);
    
    // V3笔刷系统参数选项
    QCommandLineOption outlineOption("outline",
                                    "印轮廓类型 (circle/square/star, 默认: circle)", "type");
    parser.addOption(outlineOption);
    
    QCommandLineOption textureOption("texture",
                                    "印纹理类型 (solid/noise/crayon, 默认: solid)", "type");
    parser.addOption(textureOption);
    
    QCommandLineOption spacingOption("spacing",
                                    "轨迹间距 (0.1-2.0, 默认: 0.3)", "value");
    parser.addOption(spacingOption);
    
    QCommandLineOption smoothingOption("smoothing",
                                      "轨迹平滑 (0.0-1.0, 默认: 0.4)", "value");
    parser.addOption(smoothingOption);
    
    QCommandLineOption taperOption("taper",
                                  "锥度长度 (0.0-1.0, 默认: 0.1)", "value");
    parser.addOption(taperOption);
    
    QCommandLineOption colorVarOption("color-variation",
                                     "颜色变化 (0.0-1.0, 默认: 0.05)", "value");
    parser.addOption(colorVarOption);
    
    parser.process(app);
    
    if (!parser.isSet(inputOption) || !parser.isSet(outputOption)) {
        qCritical() << "必须指定输入和输出文件路径";
        qCritical() << "用法:" << app.applicationName() << "-i events.json -o output.png";
        qCritical() << "";
        qCritical() << "V3笔刷系统参数示例:";
        qCritical() << "  --outline star --texture noise --spacing 0.5 --smoothing 0.6";
        qCritical() << "  --taper 0.2 --color-variation 0.1";
        qCritical() << "";
        qCritical() << "轮廓类型: circle, square, star";
        qCritical() << "纹理类型: solid, noise, crayon";
        return 1;
    }
    
    QString inputFile = parser.value(inputOption);
    QString outputFile = parser.value(outputOption);
    int width = parser.value(widthOption).toInt();
    int height = parser.value(heightOption).toInt();
    
    if (width <= 0) width = 720;
    if (height <= 0) height = 480;
    
    // 收集笔刷设置参数
    QVariantMap brushSettings;
    if (parser.isSet(outlineOption)) {
        brushSettings["outline"] = parser.value(outlineOption);
    }
    if (parser.isSet(textureOption)) {
        brushSettings["texture"] = parser.value(textureOption);
    }
    if (parser.isSet(spacingOption)) {
        brushSettings["spacing"] = parser.value(spacingOption).toDouble();
    }
    if (parser.isSet(smoothingOption)) {
        brushSettings["smoothing"] = parser.value(smoothingOption).toDouble();
    }
    if (parser.isSet(taperOption)) {
        brushSettings["taperLength"] = parser.value(taperOption).toDouble();
    }
    if (parser.isSet(colorVarOption)) {
        brushSettings["colorVariation"] = parser.value(colorVarOption).toDouble();
    }
    
    SimpleTabletRenderer renderer;
    if (renderer.renderFromEventsJson(inputFile, outputFile, width, height, brushSettings)) {
        qDebug() << "渲染完成！";
        return 0;
    } else {
        qCritical() << "渲染失败！";
        return 1;
    }
} 