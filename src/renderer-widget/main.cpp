#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSlider>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QTabWidget>
#include <QMouseEvent>
#include <QTabletEvent>
#include <QPainter>
#include <QPainterPath>
#include <QDebug>
#include <QGroupBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QRandomGenerator>
#include <QResizeEvent>

#ifdef PAINTTY_USE_SIMD
#include "../painttyDesktop/paintingTools/brush/basicbrushv3-simd.h"
using MyBrushV3 = BasicBrushV3SIMD;
#else
#include "../painttyDesktop/paintingTools/brush/basicbrushv3.h"
using MyBrushV3 = BasicBrushV3;
#endif

// 绘制区域类
class CanvasWidget : public QWidget
{
    Q_OBJECT

public:
    CanvasWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(400, 400);
        setStyleSheet("CanvasWidget { background-color: white; border: 2px solid #ccc; }");
    }

    void setBrush(MyBrushV3* brush) { brush_ = brush; }
    void setStatusLabel(QLabel* label) { statusLabel_ = label; }

    // 清空画布
    void clearCanvas()
    {
        canvasImage_ = QImage();
        if (brush_) {
            brush_->clearAllPaths();
        }
        update();
    }

    // 清空历史路径
    void clearHistoryPaths()
    {
        if (brush_) {
            brush_->clearHistoryPaths();
            // 清空缓存图像，重新创建
            canvasImage_ = QImage();
            updateCanvasImage();
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        if (event->rect().isEmpty()) {
            return;
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        // 绘制缓存的画布内容（历史路径）
        if (!canvasImage_.isNull()) {
            painter.drawImage(event->rect(), canvasImage_, event->rect());
        } else {
            // 绘制背景
            painter.fillRect(rect(), Qt::white);
        }

        // 绘制当前正在绘制的路径（实时显示）
        if (brush_) {
            brush_->drawCurrentPath(&painter);
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && brush_) {
            brush_->clearCurrentPath();
            PressurePoint pt(event->pos(), 0.5);
            brush_->addPointToCurrentPath(pt);
            // 只更新显示，不更新缓存图像
            update();
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if ((event->buttons() & Qt::LeftButton) && brush_) {
            PressurePoint pt(event->pos(), 0.8);
            brush_->addPointToCurrentPath(pt);
            // 只更新显示，不更新缓存图像
            update();
        }
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && brush_) {
            brush_->endStroke();
            // 结束笔画时更新缓存图像
            updateCanvasImageOnStrokeEnd();
            update();
        }
    }

    // Tablet事件处理
    void tabletEvent(QTabletEvent* event) override
    {
        // qDebug() << "tabletEvent" << event->type() << event->pressure();
        switch (event->type()) {
            case QEvent::TabletPress:
                if (event->button() == Qt::LeftButton) {
                    brush_->clearCurrentPath();
                    PressurePoint pt(
                        event->position(),
                        event->pressure(),
                        event->xTilt() / 60.0,
                        event->yTilt() / 60.0
                    );
                    brush_->addPointToCurrentPath(pt);
                    // 只更新显示，不更新缓存图像
                    updateStatus(event);
                    update();
                }
                break;

            case QEvent::TabletMove:
                if (event->buttons() & Qt::LeftButton) {
                    PressurePoint pt(
                        event->position(),
                        event->pressure(),
                        event->xTilt() / 60.0,
                        event->yTilt() / 60.0
                    );
                    brush_->addPointToCurrentPath(pt);
                    // 只更新显示，不更新缓存图像
                    updateStatus(event);
                    update();
                }
                break;

            case QEvent::TabletRelease:
                if (event->button() == Qt::LeftButton) {
                    brush_->endStroke();
                    // 结束笔画时更新缓存图像
                    updateCanvasImageOnStrokeEnd();
                    if (statusLabel_) {
                        statusLabel_->setText("状态: 等待输入");
                    }
                    update();
                }
                break;

            default:
                break;
        }

        event->accept();
    }

    // 处理窗口大小变化
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        // 窗口大小变化时，重新创建缓存图像
        if (!canvasImage_.isNull() && canvasImage_.size() != size()) {
            updateCanvasImage();
        }
    }

private:
    void updateStatus(QTabletEvent* event)
    {
        if (statusLabel_) {
            QString status = QString("压感: %1 倾斜: (%2, %3)")
                .arg(event->pressure(), 0, 'f', 2)
                .arg(event->xTilt())
                .arg(event->yTilt());
            statusLabel_->setText("状态: " + status);
        }
    }

    void updateCanvasImage()
    {
        // 如果缓存图像为空或大小不匹配，重新创建并绘制所有路径
        if (canvasImage_.isNull() || canvasImage_.size() != size()) {
            canvasImage_ = QImage(size(), QImage::Format_ARGB32);
            canvasImage_.fill(Qt::white);
            
            // 重新创建时绘制所有路径
            QPainter imagePainter(&canvasImage_);
            imagePainter.setRenderHint(QPainter::Antialiasing);

            if (brush_) {
                brush_->drawAllPaths(&imagePainter);
                brush_->drawCurrentPath(&imagePainter);
            }
        }
        // 如果缓存图像存在且大小匹配，不做任何操作（避免重复绘制）
    }

    // 只在笔画结束时更新缓存图像，避免重复绘制
    void updateCanvasImageOnStrokeEnd()
    {
        // 如果缓存图像为空或大小不匹配，重新创建
        if (canvasImage_.isNull() || canvasImage_.size() != size()) {
            canvasImage_ = QImage(size(), QImage::Format_ARGB32);
            canvasImage_.fill(Qt::white);
        }

        // 在缓存图像上绘制所有历史路径（包括刚完成的笔画）
        QPainter imagePainter(&canvasImage_);
        imagePainter.setRenderHint(QPainter::Antialiasing);

        if (brush_) {
            // 绘制所有历史路径（endStroke已经将当前路径添加到历史路径中）
            brush_->drawAllPaths(&imagePainter);
            brush_->clearHistoryPaths();
        }
    }

private:
    MyBrushV3* brush_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QImage canvasImage_; // 缓存画布内容
};

// 预定义绘制区类
class PredefinedCanvasWidget : public QWidget
{
    Q_OBJECT

public:
    PredefinedCanvasWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(400, 300);
        setStyleSheet("QWidget { background-color: white; border: 1px solid #ccc; }");
    }

    void setBrush(MyBrushV3* brush) { brush_ = brush; }
    void setStatusLabel(QLabel* label) { statusLabel_ = label; }

    // 添加重绘预定义内容的方法
    void redrawPredefinedContent()
    {
        update();
        if (statusLabel_) {
            statusLabel_->setText("状态: 预定义内容已重绘");
        }
    }

    bool loadEventsFromJson(const QString& jsonFile)
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

        // 清空笔刷中的所有路径
        if (brush_) {
            brush_->clearAllPaths();
        }

        // 处理strokes数据
        if (root.contains("strokes")) {
            QJsonArray strokesArray = root["strokes"].toArray();
            qDebug() << "找到" << strokesArray.size() << "个笔画";

            calculateBoundingBox(strokesArray);
            calculateTransform();

            for (const QJsonValue &strokeValue : strokesArray) {
                QJsonObject strokeObj = strokeValue.toObject();
                processStroke(strokeObj);
            }
        }

        update();
        return true;
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event)

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        // 绘制白色背景
        painter.fillRect(rect(), Qt::white);

        painter.save();
        painter.translate(centerOffset_);
        painter.scale(scale_, scale_);

        // 绘制预定义的路径
        if (brush_) {
            brush_->drawAllPaths(&painter);
            brush_->drawCurrentPath(&painter);
        }

        painter.restore();
    }

private:
    void processStroke(const QJsonObject& strokeObj)
    {
        if (!strokeObj.contains("events") || !brush_) {
            return;
        }

        QJsonArray eventsArray = strokeObj["events"].toArray();

        // 开始新的笔画
        brush_->clearCurrentPath();

        for (const QJsonValue &eventValue : eventsArray) {
            QJsonObject eventObj = eventValue.toObject();
            QString eventType = eventObj["event_type"].toString();

            if (eventType == "TabletPress" || eventType == "TabletMove") {
                QJsonObject positionObj = eventObj["position"].toObject();
                int x = positionObj["x"].toInt();
                int y = positionObj["y"].toInt();
                qreal pressure = eventObj["pressure"].toDouble();

                QJsonObject tiltObj = eventObj["tilt"].toObject();
                qreal tiltX = tiltObj["x"].toDouble() / 60.0;
                qreal tiltY = tiltObj["y"].toDouble() / 60.0;

                PressurePoint point(QPointF(x, y), pressure, tiltX, tiltY);
                brush_->addPointToCurrentPath(point);
            }
        }

        // 结束当前笔画
        brush_->endStroke();
    }

    void calculateBoundingBox(const QJsonArray& strokesArray)
    {
        qreal minX = -1, minY = -1, maxX = -1, maxY = -1;

        for (const QJsonValue &strokeValue : strokesArray) {
            QJsonObject strokeObj = strokeValue.toObject();
            if (!strokeObj.contains("events")) {
                continue;
            }
            QJsonArray eventsArray = strokeObj["events"].toArray();

            for (const QJsonValue &eventValue : eventsArray) {
                QJsonObject eventObj = eventValue.toObject();
                QString eventType = eventObj["event_type"].toString();

                if (eventType == "TabletPress" || eventType == "TabletMove") {
                    QJsonObject positionObj = eventObj["position"].toObject();
                    int x = positionObj["x"].toInt();
                    int y = positionObj["y"].toInt();
                    if (minX == -1) {
                        minX = maxX = x;
                        minY = maxY = y;
                    } else {
                        minX = qMin(minX, (qreal)x);
                        minY = qMin(minY, (qreal)y);
                        maxX = qMax(maxX, (qreal)x);
                        maxY = qMax(maxY, (qreal)y);
                    }
                }
            }
        }
        if (minX != -1) {
            contentBoundingBox_.setRect(minX, minY, maxX - minX, maxY - minY);
        } else {
            contentBoundingBox_ = QRectF();
        }
    }

    void calculateTransform()
    {
        if (contentBoundingBox_.isNull() || contentBoundingBox_.isEmpty() || contentBoundingBox_.width() == 0 || contentBoundingBox_.height() == 0) {
            scale_ = 1.0;
            centerOffset_ = QPointF(0, 0);
            return;
        }

        qreal scaleX = width() / contentBoundingBox_.width();
        qreal scaleY = height() / contentBoundingBox_.height();
        scale_ = qMin(scaleX, scaleY) * 0.95; // 0.95 for some padding

        QPointF boxCenter = contentBoundingBox_.center();
        QPointF widgetCenter = rect().center();

        centerOffset_ = widgetCenter - boxCenter * scale_;
    }

private:
    MyBrushV3* brush_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QRectF contentBoundingBox_;
    qreal scale_ = 1.0;
    QPointF centerOffset_;
};

// 主窗口类
class BrushTestWidget : public QWidget
{
    Q_OBJECT

public:
    BrushTestWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setupUI();
        setupBrush();
    }

private slots:
    void onOutlineChanged(int index)
    {
        QString outline = "circle";
        if (index == 1) outline = "square";
        else if (index == 2) outline = "star";

        if (brush_ && brush_->stamp()) {
            brush_->stamp()->setOutline(outline);
        }
        canvas_->update();
        // 重绘预定义绘制区的内容
        predefinedCanvas_->redrawPredefinedContent();
    }

    void onTextureChanged(int index)
    {
        QString texture = "solid";
        if (index == 1) texture = "noise";
        else if (index == 2) texture = "crayon";

        if (brush_ && brush_->stamp()) {
            brush_->stamp()->setTexture(texture);
        }
        canvas_->update();
        // 重绘预定义绘制区的内容
        predefinedCanvas_->redrawPredefinedContent();
    }

    void onSpacingChanged(int value)
    {
        if (brush_ && brush_->trail()) {
            brush_->trail()->setSpacing(value / 100.0);
        }
        // 更新标签显示的值
        if (spacingValueLabel_) {
            spacingValueLabel_->setText(QString::number(value));
        }
        canvas_->update();
        // 重绘预定义绘制区的内容
        predefinedCanvas_->redrawPredefinedContent();
    }

    void onSmoothingChanged(int value)
    {
        if (brush_ && brush_->trail()) {
            brush_->trail()->setSmoothing(value / 100.0);
        }
        // 更新标签显示的值
        if (smoothingValueLabel_) {
            smoothingValueLabel_->setText(QString::number(value));
        }
        canvas_->update();
        // 重绘预定义绘制区的内容
        predefinedCanvas_->redrawPredefinedContent();
    }

    void onWidthChanged(int value)
    {
        if (brush_) {
            brush_->setWidth(value);
        }
        // 更新标签显示的值
        if (widthValueLabel_) {
            widthValueLabel_->setText(QString::number(value));
        }
        canvas_->update();
        // 重绘预定义绘制区的内容
        predefinedCanvas_->redrawPredefinedContent();
    }

    void onRandomPointsChanged(int value)
    {
        if (brush_) {
            brush_->trail()->setRandomPoints(value / 100.0);
        }
        canvas_->update();
        // 重绘预定义绘制区的内容
        predefinedCanvas_->redrawPredefinedContent();
    }

    void onClearCanvas()
    {
        canvas_->clearCanvas();
        statusLabel_->setText("状态: 画布已清空");
    }

    void onClearHistoryPaths()
    {
        canvas_->clearHistoryPaths();
        statusLabel_->setText("状态: 历史路径已清空");
    }

    void onTabChanged(int index)
    {
        if (index == 1) { // 预定义绘制区标签页
            // 当切换到预定义绘制区时，自动加载events.json
            QString jsonFile = "events.json";
            if (predefinedCanvas_->loadEventsFromJson(jsonFile)) {
                statusLabel_->setText("状态: 已加载预定义绘制内容");
            } else {
                statusLabel_->setText("状态: 加载预定义内容失败");
            }
        } else {
            statusLabel_->setText("状态: 等待输入");
            canvas_->clearCanvas();
            canvas_->setBrush(brush_);
        }
    }

    void setupUI()
    {
        // 创建主布局（左右分栏）
        QHBoxLayout* mainLayout = new QHBoxLayout(this);

        // 左侧控制面板
        QGroupBox* controlPanel = new QGroupBox("笔刷参数");
        controlPanel->setMaximumWidth(250);
        QVBoxLayout* controlLayout = new QVBoxLayout(controlPanel);

        // 轮廓选择
        QLabel* outlineLabel = new QLabel("轮廓:");
        outlineCombo_ = new QComboBox();
        outlineCombo_->addItems({"圆形", "方形", "星形"});
        connect(outlineCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &BrushTestWidget::onOutlineChanged);

        // 纹理选择
        QLabel* textureLabel = new QLabel("纹理:");
        textureCombo_ = new QComboBox();
        textureCombo_->addItems({"实心", "噪声", "蜡笔"});
        connect(textureCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &BrushTestWidget::onTextureChanged);

        // 间距控制
        QHBoxLayout* spacingLayout = new QHBoxLayout();
        QLabel* spacingLabel = new QLabel("间距:");
        spacingValueLabel_ = new QLabel("50");
        spacingValueLabel_->setMinimumWidth(30);
        spacingValueLabel_->setAlignment(Qt::AlignRight);
        spacingValueLabel_->setStyleSheet("QLabel { background-color: #e0e0e0; padding: 2px; border: 1px solid #ccc; }");
        spacingLayout->addWidget(spacingLabel);
        spacingLayout->addWidget(spacingValueLabel_);
        
        spacingSlider_ = new QSlider(Qt::Horizontal);
        spacingSlider_->setRange(1, 200);
        spacingSlider_->setValue(50);
        connect(spacingSlider_, &QSlider::valueChanged,
                this, &BrushTestWidget::onSpacingChanged);

        // 平滑控制
        QHBoxLayout* smoothingLayout = new QHBoxLayout();
        QLabel* smoothingLabel = new QLabel("平滑:");
        smoothingValueLabel_ = new QLabel("30");
        smoothingValueLabel_->setMinimumWidth(30);
        smoothingValueLabel_->setAlignment(Qt::AlignRight);
        smoothingValueLabel_->setStyleSheet("QLabel { background-color: #e0e0e0; padding: 2px; border: 1px solid #ccc; }");
        smoothingLayout->addWidget(smoothingLabel);
        smoothingLayout->addWidget(smoothingValueLabel_);
        
        smoothingSlider_ = new QSlider(Qt::Horizontal);
        smoothingSlider_->setRange(0, 100);
        smoothingSlider_->setValue(30);
        connect(smoothingSlider_, &QSlider::valueChanged,
                this, &BrushTestWidget::onSmoothingChanged);

        // 随机点控制
        QLabel* randomPointsLabel = new QLabel("随机点:");
        randomPointsSlider_ = new QSlider(Qt::Horizontal);
        randomPointsSlider_->setRange(0, 100);
        randomPointsSlider_->setValue(0);
        connect(randomPointsSlider_, &QSlider::valueChanged,
                this, &BrushTestWidget::onRandomPointsChanged);



        // 粗细控制
        QHBoxLayout* widthLayout = new QHBoxLayout();
        QLabel* widthLabel = new QLabel("粗细:");
        widthValueLabel_ = new QLabel("15");
        widthValueLabel_->setMinimumWidth(30);
        widthValueLabel_->setAlignment(Qt::AlignRight);
        widthValueLabel_->setStyleSheet("QLabel { background-color: #e0e0e0; padding: 2px; border: 1px solid #ccc; }");
        widthLayout->addWidget(widthLabel);
        widthLayout->addWidget(widthValueLabel_);
        
        widthSlider_ = new QSlider(Qt::Horizontal);
        widthSlider_->setRange(1, 50);
        widthSlider_->setValue(15);
        connect(widthSlider_, &QSlider::valueChanged,
                this, &BrushTestWidget::onWidthChanged);

        // 清除按钮
        clearButton_ = new QPushButton("清除画布");
        connect(clearButton_, &QPushButton::clicked, this, &BrushTestWidget::onClearCanvas);
        clearButton_->setStyleSheet(
            "QPushButton { "
            "    background-color: #ff6b6b; "
            "    color: white; "
            "    border: none; "
            "    padding: 8px; "
            "    border-radius: 4px; "
            "    font-weight: bold; "
            "} "
            "QPushButton:hover { "
            "    background-color: #ff5252; "
            "} "
            "QPushButton:pressed { "
            "    background-color: #d32f2f; "
            "}"
        );
        connect(clearButton_, &QPushButton::clicked,
                this, &BrushTestWidget::onClearCanvas);

        // 清空历史路径按钮
        clearHistoryButton_ = new QPushButton("清空历史");
        clearHistoryButton_->setStyleSheet(
            "QPushButton { "
            "    background-color: #ffa726; "
            "    color: white; "
            "    border: none; "
            "    padding: 8px; "
            "    border-radius: 4px; "
            "    font-weight: bold; "
            "} "
            "QPushButton:hover { "
            "    background-color: #ff9800; "
            "} "
            "QPushButton:pressed { "
            "    background-color: #f57c00; "
            "}"
        );
        connect(clearHistoryButton_, &QPushButton::clicked,
                this, &BrushTestWidget::onClearHistoryPaths);


        // 添加到控制面板
        controlLayout->addWidget(outlineLabel);
        controlLayout->addWidget(outlineCombo_);
        controlLayout->addWidget(textureLabel);
        controlLayout->addWidget(textureCombo_);
        controlLayout->addLayout(widthLayout);
        controlLayout->addWidget(widthSlider_);
        controlLayout->addLayout(spacingLayout);
        controlLayout->addWidget(spacingSlider_);
        controlLayout->addLayout(smoothingLayout);
        controlLayout->addWidget(smoothingSlider_);
        controlLayout->addWidget(randomPointsLabel);
        controlLayout->addWidget(randomPointsSlider_);
        controlLayout->addWidget(clearButton_);
        controlLayout->addWidget(clearHistoryButton_);
        controlLayout->addStretch(); // 添加弹性空间

        // 右侧绘制区域
        QGroupBox* canvasPanel = new QGroupBox("绘制区域");
        QVBoxLayout* canvasLayout = new QVBoxLayout(canvasPanel);

        // 状态显示 - 先创建状态标签
        statusLabel_ = new QLabel("状态: 等待输入");
        statusLabel_->setStyleSheet("QLabel { background-color: #f0f0f0; padding: 5px; border: 1px solid #ccc; }");

        // 创建标签页
        tabWidget_ = new QTabWidget();

        // 第一个标签页：实时绘制区
        canvas_ = new CanvasWidget();
        canvas_->setStatusLabel(statusLabel_);

        QLabel* infoLabel = new QLabel("在此区域绘制测试笔迹（支持压感笔）");
        infoLabel->setAlignment(Qt::AlignCenter);
        infoLabel->setMaximumHeight(50);
        infoLabel->setStyleSheet("QLabel { color: #666; font-style: italic; }");

        QWidget* realtimeTab = new QWidget();
        QVBoxLayout* realtimeLayout = new QVBoxLayout(realtimeTab);
        realtimeLayout->addWidget(canvas_);
        realtimeLayout->addWidget(infoLabel);

        // 第二个标签页：预定义绘制区
        predefinedCanvas_ = new PredefinedCanvasWidget();
        predefinedCanvas_->setBrush(brush_);
        predefinedCanvas_->setStatusLabel(statusLabel_);

        QLabel* predefinedInfoLabel = new QLabel("显示从events.json解析的预定义绘制内容");
        predefinedInfoLabel->setAlignment(Qt::AlignCenter);
        predefinedInfoLabel->setMaximumHeight(50);
        predefinedInfoLabel->setStyleSheet("QLabel { color: #666; font-style: italic; }");

        QWidget* predefinedTab = new QWidget();
        QVBoxLayout* predefinedLayout = new QVBoxLayout(predefinedTab);
        predefinedLayout->addWidget(predefinedCanvas_);
        predefinedLayout->addWidget(predefinedInfoLabel);

        // 添加标签页
        tabWidget_->addTab(realtimeTab, "实时绘制");
        tabWidget_->addTab(predefinedTab, "预定义绘制");

        // 连接标签页切换信号
        connect(tabWidget_, &QTabWidget::currentChanged,
                this, &BrushTestWidget::onTabChanged);

        canvasLayout->addWidget(tabWidget_);
        canvasLayout->addWidget(statusLabel_);

        // 添加到主布局
        mainLayout->addWidget(controlPanel);
        mainLayout->addWidget(canvasPanel);

        setLayout(mainLayout);
        resize(800, 600);
    }

    void setupBrush()
    {
        #ifdef PAINTTY_USE_SIMD
            brush_ = new BasicBrushV3SIMD();
        #else
            brush_ = new BasicBrushV3();
        #endif
        brush_->setWidth(15);
        brush_->setColor(Qt::blue);
        brush_->stamp()->setTexture("solid");

        // 确保颜色系统使用正确的颜色
        if (brush_->colorSystem()) {
            brush_->colorSystem()->setBaseColor(Qt::blue);
        }

        canvas_->setBrush(brush_);
        predefinedCanvas_->setBrush(brush_);
    }

private:
    MyBrushV3* brush_ = nullptr;
    CanvasWidget* canvas_ = nullptr;
    PredefinedCanvasWidget* predefinedCanvas_ = nullptr;
    QTabWidget* tabWidget_ = nullptr;
    QComboBox* outlineCombo_ = nullptr;
    QComboBox* textureCombo_ = nullptr;
    QSlider* widthSlider_ = nullptr;
    QSlider* spacingSlider_ = nullptr;
    QSlider* smoothingSlider_ = nullptr;
    QSlider* randomPointsSlider_ = nullptr;
    QPushButton* clearButton_ = nullptr;
    QPushButton* clearHistoryButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* widthValueLabel_ = nullptr;
    QLabel* spacingValueLabel_ = nullptr;
    QLabel* smoothingValueLabel_ = nullptr;
};

// 主函数
int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    BrushTestWidget widget;
    widget.show();

    return app.exec();
}

#include "main.moc"
