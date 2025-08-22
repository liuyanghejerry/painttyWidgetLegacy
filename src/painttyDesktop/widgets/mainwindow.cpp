#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QScrollBar>
#include <QActionGroup>
#include <QSettings>
#include <QShortcut>
#include <QTableWidgetItem>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QtConcurrent>
#include <QThread>
#include <QApplication>
#include <QTextStream>
#include <QTextCursor>
#include <QTextEdit>
#include <QPushButton>

#include "../common/common.h"
#include "../common/network/sse-clientsocket.h"
#include "../misc/archivefile.h"
#include "../common/network/known-error.h"
#include "../misc/platformextend.h"
#include "../misc/psdexport.h"
#include "../misc/shortcutmanager.h"
#include "../misc/singleshortcut.h"
#include "../misc/singleton.h"
#include "../paintingTools/brush/brushmanager.h"
#include "canvas.h"
#include "canvascontainer.h"
#include "panoramawidget.h"
#include "aboutdialog.h"
#include "brushsettingswidget.h"
#include "colorgrid.h"
#include "configuredialog.h"
#include "gradualbox.h"
#include "layeritem.h"
#include "layerwidget.h"
#include "networkindicator.h"
#include "roomsharebar.h"
#include "colorbox.h"
#include "memberlistwidget.h"
#include "../common/room-info-manager.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    roomInfoManager_(nullptr),
    clientSocket_(nullptr),
    lastBrushAction(nullptr),
    brushSettingControl_(nullptr),
    toolbar_(nullptr),
    brushActionGroup_(nullptr),
    colorPickerButton_(nullptr),
    moveToolButton_(nullptr),
    networkIndicator_(nullptr),
    onlineListTimer_(nullptr)
{
    ui->setupUi(this);

    // 使用全局单例的房间信息管理器
    roomInfoManager_ = &RoomInfoManager::instance();
    init();
}

MainWindow::~MainWindow()
{
    qDebug() << "MainWindow::~MainWindow";

    // 停止定时器
    if (onlineListTimer_) {
        onlineListTimer_->stop();
    }

    // 先断开网络连接，避免在对象销毁过程中产生错误
    if (clientSocket_) {
        clientSocket_->disconnect();
    }

    // 等待一小段时间，确保网络操作完成
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    delete ui;
}

void MainWindow::stylize()
{
    QFile stylesheet("./iconset/style.qss",this);
    stylesheet.open(QIODevice::ReadOnly);
    QTextStream stream(&stylesheet);
    QString string;
    string = stream.readAll();
    this->setStyleSheet(string);
    stylesheet.close();
}

void MainWindow::init()
{
    // 设置初始窗口标题，房间名将在加入房间后更新
    setWindowTitle(tr("Mr.Paint"));

    // 创建快捷键管理器
    shortcutManager_ = new ShortcutManager(this);

    ui->centralWidget->setBackgroundRole(QPalette::Dark);

    connect(ui->canvas, &Canvas::contentMovedBy,
            [this](const QPoint& p){
        ui->centralWidget->moveBy(p * ui->centralWidget->currentScaleFactor());
    });

    connect(ui->panorama, &PanoramaWidget::scaled,
            ui->centralWidget, &CanvasContainer::setScaleFactor);
    connect(ui->centralWidget, &CanvasContainer::scaled,
            ui->panorama, &PanoramaWidget::setScaled);
    connect(ui->panorama, &PanoramaWidget::rotated,
            ui->centralWidget, &CanvasContainer::setRotation);
    connect(ui->centralWidget, &CanvasContainer::rotated,
            ui->panorama, &PanoramaWidget::setRotation);

    connect(ui->lineEdit,&QLineEdit::returnPressed,
            this,&MainWindow::onSendPressed);
    connect(ui->pushButton,&QPushButton::clicked,
            this,&MainWindow::onSendPressed);

    connect(ui->canvas, &Canvas::newBrushSettings,
            this, &MainWindow::onBrushSettingsChanged);

    connect(ui->layerWidget,&LayerWidget::itemHide,
            ui->canvas, &Canvas::hideLayer);
    connect(ui->layerWidget,&LayerWidget::itemShow,
            ui->canvas, &Canvas::showLayer);
    connect(ui->layerWidget,&LayerWidget::itemLock,
            ui->canvas, &Canvas::lockLayer);
    connect(ui->layerWidget,&LayerWidget::itemUnlock,
            ui->canvas,  &Canvas::unlockLayer);
    connect(ui->layerWidget,&LayerWidget::itemSelected,
            ui->canvas, &Canvas::layerSelected);

    connect(ui->colorBox, &ColorBox::colorChanged,
            this, &MainWindow::brushColorChange);
    connect(this, &MainWindow::brushColorChange,
            ui->canvas, &Canvas::setBrushColor);
    connect(ui->canvas, &Canvas::canvasToolComplete,
            this, &MainWindow::onCanvasToolComplete);

    connect(ui->colorGrid,
            static_cast<void (ColorGrid::*)(const int&)>
            (&ColorGrid::colorDroped),
            this, &MainWindow::onColorGridDroped);
    connect(ui->colorGrid, &ColorGrid::colorPicked,
            this, &MainWindow::onColorGridPicked);
    connect(ui->panorama, &PanoramaWidget::refresh,
            this, &MainWindow::onPanoramaRefresh);
    connect(ui->centralWidget, &CanvasContainer::rectChanged,
            ui->panorama, &PanoramaWidget::onRectChange);
    connect(ui->panorama, &PanoramaWidget::moveTo,
            ui->centralWidget,
            static_cast<void (CanvasContainer::*)(const QPointF&)>
            (&CanvasContainer::centerOn));

    connect(ui->memberList, &MemberListWidget::memberGetKicked,
            this, &MainWindow::requestKickUser);

    layerWidgetInit();
    colorGridInit();
    statusBarInit();
    toolbarInit();
    viewInit();
    shortcutInit();
    socketInit();

    // 初始化在线列表定时器
    onlineListTimer_ = new QTimer(this);
    connect(onlineListTimer_, &QTimer::timeout, this, &MainWindow::onOnlineListTimer);
}

void MainWindow::layerWidgetInit()
{
    for(int i=0;i<10;++i){
        addLayer();
    }
    ui->layerWidget->itemAt(0)->setSelect(true);
}

void MainWindow::colorGridInit()
{
    QSettings settings(GlobalDef::SETTINGS_NAME,
                       QSettings::defaultFormat(),
                       qApp);
    QByteArray data = settings.value("colorgrid/pal")
            .toByteArray();
    if(data.isEmpty()){
        return;
    }else{
        ui->colorGrid->dataImport(data);
    }
}

void MainWindow::viewInit()
{
    QSettings settings(GlobalDef::SETTINGS_NAME,
                       QSettings::defaultFormat(),
                       qApp);
    QByteArray data = settings.value("mainwindow/view")
            .toByteArray();
    if(data.isEmpty()){
        return;
    }else{
        restoreState(data);
    }
}

void MainWindow::toolbarInit()
{
    toolbar_ = new QToolBar(tr("Brushes"), this);
    toolbar_->setObjectName("BrushToolbar");
    this->addToolBar(Qt::TopToolBarArea, toolbar_);
    brushActionGroup_ = new QActionGroup(this);

    // always remember last action
    auto restoreAction =  [this](){
        if(lastBrushAction){
            lastBrushAction->trigger();
        }
    };

    auto brushes = Singleton<BrushManager>::instance().allBrushes();
    auto brushesV3 = Singleton<BrushManager>::instance().allBrushesV3();

    // 添加v1笔刷
    for(auto &item: brushes){
        // create action on tool bar
        QAction * action = toolbar_->addAction(item->icon(),
                                               item->displayName());
        action->setObjectName(item->name());
        connect(action, &QAction::triggered,
                this, &MainWindow::onBrushTypeChange);
        action->setCheckable(true);
        action->setAutoRepeat(false);
        brushActionGroup_->addAction(action);

        // set shortcut for the brush
        regShortcut<>(item->shortcut(),
                      [this, action](){
            lastBrushAction = brushActionGroup_->checkedAction();
            action->trigger();
        },
        restoreAction);

        action->setToolTip(
                    tr("%1\n"
                       "Shortcut: %2")
                    .arg(item->displayName())
                    .arg(item->shortcut().toString()));
        if(toolbar_->actions().count() < 2){
            action->trigger();
        }
    }

    // 添加v3笔刷
    for(auto &item: brushesV3){
        // create action on tool bar
        QAction * action = toolbar_->addAction(item->icon(),
                                               item->displayName());
        action->setObjectName(item->name());
        connect(action, &QAction::triggered,
                this, &MainWindow::onBrushTypeChange);
        action->setCheckable(true);
        action->setAutoRepeat(false);
        brushActionGroup_->addAction(action);

        // set shortcut for the brush
        regShortcut<>(item->shortcut(),
                      [this, action](){
            lastBrushAction = brushActionGroup_->checkedAction();
            action->trigger();
        },
        restoreAction);

        action->setToolTip(
                    tr("%1\n"
                       "Shortcut: %2")
                    .arg(item->displayName())
                    .arg(item->shortcut().toString()));
    }


    // doing hacking to color picker
    QIcon colorpickerIcon(":/iconset/ui/brush/colorpicker.png");
    QAction *colorpicker = new QAction(colorpickerIcon, tr("Color Picker"), this);
    colorpicker->setCheckable(true);
    colorpicker->setAutoRepeat(false);
    
    // 手动构造 QToolButton 并添加到工具栏
    colorPickerButton_ = new QToolButton(this);
    colorPickerButton_->setDefaultAction(colorpicker);
    colorPickerButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar_->addWidget(colorPickerButton_);
    
    // 连接信号槽
    connect(colorPickerButton_, &QToolButton::clicked,
            this, &MainWindow::onColorPickerPressed);

    auto colorpicker_key = Singleton<ShortcutManager>::instance()
            .shortcut("colorpicker")["key"].toString();
    SingleShortcut *pickerShortcut = new SingleShortcut(this);
    pickerShortcut->setKey(colorpicker_key);
    connect(pickerShortcut, &SingleShortcut::activated,
            colorPickerButton_, &QToolButton::click);
    connect(pickerShortcut, &SingleShortcut::inactivated,
            colorPickerButton_, &QToolButton::click);
    colorpicker->setToolTip(
                tr("%1\n"
                   "Shortcut: %2")
                .arg(colorpicker->text())
                .arg(colorpicker_key));

    // doing hacking for move tool
    QIcon moveIcon(":/iconset/ui/brush/move.png");
    QAction *moveTool = new QAction(moveIcon, tr("Move Tool"), this);
    moveTool->setCheckable(true);
    moveTool->setAutoRepeat(false);
    
    // 手动构造 QToolButton 并添加到工具栏
    moveToolButton_ = new QToolButton(this);
    moveToolButton_->setDefaultAction(moveTool);
    moveToolButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar_->addWidget(moveToolButton_);
    
    // 连接信号槽
    connect(moveToolButton_, &QToolButton::clicked,
            this, &MainWindow::onMoveToolPressed);
    
    auto movetool_key = Singleton<ShortcutManager>::instance()
            .shortcut("movetool")["key"].toString();
    SingleShortcut *moveToolShortcut = new SingleShortcut(this);
    moveToolShortcut->setKey(movetool_key);
    connect(moveToolShortcut, &SingleShortcut::activated,
            moveToolButton_, &QToolButton::click);
    connect(moveToolShortcut, &SingleShortcut::inactivated,
            moveToolButton_, &QToolButton::click);
    moveTool->setToolTip(
                tr("%1\n"
                   "Shortcut: %2")
                .arg(moveTool->text())
                .arg(movetool_key));

    // // TODO: v3笔刷成熟后可以放开
    // 从 settings 读取 tablet 默认状态，与 configuredialog.cpp 保持一致
    // QSettings settings(GlobalDef::SETTINGS_NAME, QSettings::defaultFormat(), qApp);
    // bool enable_tablet = settings.value("canvas/enable_tablet", false).toBool();

    // QToolBar *tabletEnableToolbar = new QToolBar(tr("Tablet"), this);
    // tabletEnableToolbar->setObjectName("TabletEnableToolbar");
    // QAction *tabletAction = tabletEnableToolbar->addAction(QIcon(":/iconset/ui/tablet.png"), tr("Draw with Tablet"));
    // tabletAction->setCheckable(true);
    // tabletAction->setChecked(enable_tablet);
    // ui->canvas->setTabletEnabled(enable_tablet);
    // connect(tabletAction, &QAction::toggled, ui->canvas, &Canvas::setTabletEnabled);
    // addToolBar(Qt::TopToolBarArea, tabletEnableToolbar);

    // for brush width
    QToolBar *brushSettingToolbar = new QToolBar(tr("Brush Settings"), this);
    brushSettingToolbar->setObjectName("BrushSettingToolbar");
    this->addToolBar(Qt::TopToolBarArea, brushSettingToolbar);
    BrushSettingsWidget * brushSettingWidget = new BrushSettingsWidget(this);
    connect(brushSettingWidget, &BrushSettingsWidget::widthChanged,
            ui->canvas, &Canvas::setBrushWidth);
    connect(brushSettingWidget, &BrushSettingsWidget::hardnessChanged,
            ui->canvas, &Canvas::setBrushHardness);
    connect(brushSettingWidget, &BrushSettingsWidget::thicknessChanged,
            ui->canvas, &Canvas::setBrushThickness);
    connect(brushSettingWidget, &BrushSettingsWidget::waterChanged,
            ui->canvas, &Canvas::setBrushWater);
    connect(brushSettingWidget, &BrushSettingsWidget::extendChanged,
            ui->canvas, &Canvas::setBrushExtend);
    connect(brushSettingWidget, &BrushSettingsWidget::mixinChanged,
            ui->canvas, &Canvas::setBrushMixin);
    connect(brushSettingToolbar, &QToolBar::orientationChanged,
            brushSettingWidget, &BrushSettingsWidget::setOrientation);


    //    ShortcutManager &stctmgr = Singleton<ShortcutManager>::instance();
    regShortcut<>("subwidth",
                  std::bind(&BrushSettingsWidget::widthDown, brushSettingWidget));
    regShortcut<>("addwidth",
                  std::bind(&BrushSettingsWidget::widthUp, brushSettingWidget));

    regShortcut<>("subhardness",
                  std::bind(&BrushSettingsWidget::hardnessDown, brushSettingWidget));
    regShortcut<>("addhardness",
                  std::bind(&BrushSettingsWidget::hardnessUp, brushSettingWidget));

    regShortcut<>("subthickness",
                  std::bind(&BrushSettingsWidget::thicknessDown, brushSettingWidget));
    regShortcut<>("addthickness",
                  std::bind(&BrushSettingsWidget::thicknessUp, brushSettingWidget));

    brushSettingControl_ = brushSettingWidget;
    brushSettingToolbar->addWidget(brushSettingWidget);

    changeToBrush("BasicBrush");

    // for room share - 修改为使用 SSE 客户端的 URL 生成
    QToolBar* roomShareToolbar = new QToolBar(tr("Room Share"), this);
    roomShareToolbar->setObjectName("RoomShareToolbar");
    this->addToolBar(Qt::TopToolBarArea, roomShareToolbar);
    RoomShareBar* rsb = new RoomShareBar(this);
    // SSE 客户端暂时不提供 URL 生成功能，使用占位符
    rsb->setAddress("SSE Client - URL generation not implemented");
    roomShareToolbar->addWidget(rsb);
}

void MainWindow::statusBarInit()
{
    networkIndicator_ = new NetworkIndicator(this);
    this->statusBar()->addPermanentWidget(networkIndicator_);
}

void MainWindow::requestCloseRoom()
{
    if (!clientSocket_->isRoomOwner()) {
        QMessageBox::warning(this,
                         tr("Warning"),
                         tr("You are not the room owner, you can't close the room."),
                         QMessageBox::Close);
        return;
    }
    QMessageBox::StandardButton reply;
    reply = QMessageBox::warning(this,
                         tr("Warning"),
                         tr("You are closing the room.\n"
                            "Your paintings will be lost if you don't save them.\n"
                            "Are you sure you want to close the room?"),
                         QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        clientSocket_->requestCloseRoom();
    }
}

void MainWindow::requestKickUser(const QString& id)
{
    if (!clientSocket_->isRoomOwner()) {
      QMessageBox::warning(
          this, tr("Warning"),
          tr("You are not the room owner, you can't kick user."),
          QMessageBox::Close);
      return;
    }
    QMessageBox::StandardButton reply;
    reply = QMessageBox::warning(this,
                         tr("Warning"),
                         tr("You are kicking someone.\n"
                            "He/She may never be allowed to join the room again.\n"
                            "Are you sure you want to kick user?"),
                         QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        clientSocket_->requestKickUser(id);
    }
}

void MainWindow::shortcutInit()
{
    connect(ui->action_Quit, &QAction::triggered,
            this, &MainWindow::close);
    connect(ui->actionExport_All, &QAction::triggered,
            this, &MainWindow::exportAllToFile);
    connect(ui->actionExport_Visiable, &QAction::triggered,
            this, &MainWindow::exportVisibleToFile);
    connect(ui->actionExport_All_To_Clipboard, &QAction::triggered,
            this, &MainWindow::exportAllToClipboard);
    connect(ui->actionExport_Visible_To_ClipBorad, &QAction::triggered,
            this, &MainWindow::exportVisibleToClipboard);
    connect(ui->actionReset_View, &QAction::triggered,
            this, &MainWindow::resetView);
    connect(ui->action_About_Mr_Paint, &QAction::triggered,
            this, &MainWindow::about);
    connect(ui->actionAbout_Qt, &QAction::triggered,
            &QApplication::aboutQt);
    connect(ui->actionExport_to_PSD, &QAction::triggered,
            this, &MainWindow::exportToPSD);
    connect(ui->actionClose_Room, &QAction::triggered,
            this, &MainWindow::requestCloseRoom);
    connect(ui->actionAll_Layers, &QAction::triggered,
            this, &MainWindow::clearAllLayer);
    connect(ui->actionConfiguration, &QAction::triggered,
            [](){
        ConfigureDialog conf_dialog;
        conf_dialog.exec();
    });

    regShortcut<>("zoomin", [this](){
        this->ui->centralWidget->scaleBy(1.2);
    });
    regShortcut<>("zoomout", [this](){
        this->ui->centralWidget->scaleBy(0.8);
    });
    regShortcut<>("rotateclock", [this](){
        this->ui->centralWidget->rotateBy(10);
    });
    regShortcut<>("rotateanticlock", [this](){
        this->ui->centralWidget->rotateBy(-10);
    });
    regShortcut<>("canvasreset", [this](){
        this->ui->centralWidget->setRotation(0);
        this->ui->centralWidget->setScaleFactor(1);
    });
}

void MainWindow::socketInit()
{
    // 创建SSE客户端套接字
    clientSocket_ = new SSEClientSocket(roomInfoManager_, this);
    qDebug() << "[MainWindow] 创建SSE客户端套接字"<<clientSocket_;

    connect(clientSocket_, &SSEClientSocket::newMessage, this,
            &MainWindow::onNewMessage);
    
    // 连接用户主动操作的错误信号（带重试按钮的对话框）
    connect(clientSocket_, &SSEClientSocket::requestLoginFailed, this,
            &MainWindow::onRequestLoginFailed);
    connect(clientSocket_, &SSEClientSocket::requestChatMessageFailed, this,
            &MainWindow::onRequestChatMessageFailed);
    connect(clientSocket_, &SSEClientSocket::requestDrawDataFailed, this,
            &MainWindow::onRequestDrawDataFailed);
    connect(clientSocket_, &SSEClientSocket::requestClearCanvasFailed, this,
            &MainWindow::onRequestClearCanvasFailed);
    connect(clientSocket_, &SSEClientSocket::requestCheckoutFailed, this,
            &MainWindow::onRequestCheckoutFailed);
    connect(clientSocket_, &SSEClientSocket::requestCloseRoomFailed, this,
            &MainWindow::onRequestCloseRoomFailed);
    connect(clientSocket_, &SSEClientSocket::requestKickUserFailed, this,
            &MainWindow::onRequestKickUserFailed);
    
    // 连接自动操作的错误信号（GradualBox提示）
    connect(clientSocket_, &SSEClientSocket::requestOnlineListFailed, this,
            &MainWindow::onRequestOnlineListFailed);

    // 添加对 notify 事件的连接
    connect(clientSocket_, &SSEClientSocket::getNotified, this,
            &MainWindow::onNotify);

    // 添加其他重要的信号连接
    connect(clientSocket_, &SSEClientSocket::getKicked, this,
            &MainWindow::onKicked);
    connect(clientSocket_, &SSEClientSocket::delayGet, this,
            &MainWindow::onDelayGet);
    connect(clientSocket_, &SSEClientSocket::layerAllCleared, this,
            &MainWindow::onAllLayerCleared);
    connect(clientSocket_, &SSEClientSocket::memberListFetched, this,
            &MainWindow::onMemberlistFetched);
    connect(clientSocket_, &SSEClientSocket::roomAboutToClose, this,
            &MainWindow::onAboutToClose);

    // 添加对 newClientId 信号的连接
    connect(clientSocket_, &SSEClientSocket::newClientId, this,
            &MainWindow::onNewClientId);

    connect(clientSocket_, &SSEClientSocket::roomJoined, this,
            &MainWindow::onRoomJoined);

    connect(clientSocket_, &SSEClientSocket::roomOfflined, this,
            &MainWindow::onServerDisconnected);

    connect(clientSocket_, &SSEClientSocket::roomExited, this,
            &MainWindow::onRoomExited);

    connect(clientSocket_, &SSEClientSocket::loginCompleted, this,
            &MainWindow::onLoginCompleted);

    // 重连相关信号连接
    connect(clientSocket_, &SSEClientSocket::reconnectionStarted, this,
            &MainWindow::onReconnectionStarted);
    connect(clientSocket_, &SSEClientSocket::reconnectionSucceeded, this,
            &MainWindow::onReconnectionSucceeded);
    connect(clientSocket_, &SSEClientSocket::reconnectionFailed, this,
            &MainWindow::onReconnectionFailed);
    connect(clientSocket_, &SSEClientSocket::reconnectionCompleted, this,
            &MainWindow::onReconnectionCompleted);
}

void MainWindow::tryJoinRoom(const RoomConnectionInfo& roomInfo)
{
  // 如果有房间信息，自动连接到房间
  if (!roomInfo.roomName.isEmpty() && !roomInfo.webAddress.isEmpty()) {
    // 设置用户名
    clientSocket_->setUserName(roomInfo.nickname);

    // 连接到房间
    qDebug() << "[MainWindow] 连接到房间" << clientSocket_;
    clientSocket_->tryJoinRoom(roomInfo.webAddress, roomInfo.roomName,
                               roomInfo.password);

    // 更新窗口标题
    setWindowTitle(roomInfo.roomName + tr(" - Mr.Paint"));
  } else {
    qDebug() << "房间信息不完整，无法连接";
  }
}

void MainWindow::onRoomJoined()
{
    qDebug() << "成功加入房间";

    // 更新窗口标题
    QString roomName = clientSocket_->roomName();
    if (!roomName.isEmpty()) {
        setWindowTitle(roomName + tr(" - Mr.Paint"));
    }

    ui->centralWidget->setCanvas(ui->canvas);

    // 注意：画布尺寸设置已移至登录完成后，因为此时房间信息更完整

    // 房间加入时启动定时器
    startOnlineListTimer();
}

void MainWindow::onRoomExited()
{
    qDebug() << "已退出房间";
    // 房间退出时停止定时器
    stopOnlineListTimer();
}

void MainWindow::onLoginCompleted(const QString &roomName, const QString &remoteArchiveSign)
{
    qDebug() << "[MainWindow] 登录完成，房间:" << roomName << "远程archive签名:" << remoteArchiveSign;

    // 通知 Canvas 登录完成，开始数据同步
    if (ui->canvas) {
        ui->canvas->onLoginCompleted(roomName, remoteArchiveSign, clientSocket_);
    }
}

void MainWindow::onServerDisconnected()
{
    GradualBox::showText(tr("Server Connection Failed."));
    ui->canvas->setEnabled(false);

    // 停止周期性在线列表获取定时器
    stopOnlineListTimer();
    clientSocket_->stopHeartbeat();
}

// 新增：重连相关槽函数实现
void MainWindow::onReconnectionStarted()
{
    qDebug() << "[MainWindow] 开始重连";

    // 设置Canvas为loading状态
    if (ui->canvas) {
        ui->canvas->setArchiveLoading(true);
    }

    // 在聊天框显示重连提示
    if (ui->textEdit) {
        QTextCursor c = ui->textEdit->textCursor();
        c.movePosition(QTextCursor::End);
        ui->textEdit->setTextCursor(c);
        ui->textEdit->insertHtml(QString("<span style='color: orange;'>%1</span>")
                                .arg(tr("Connection interrupted, reconnecting...")));
        ui->textEdit->verticalScrollBar()->setValue(ui->textEdit->verticalScrollBar()->maximum());
        ui->textEdit->insertPlainText("\n");
    }

    // 更新网络指示器
    if (networkIndicator_) {
        networkIndicator_->setLevel(NetworkIndicator::LEVEL::UNKNOWN);
    }
}

void MainWindow::onReconnectionSucceeded()
{
    qDebug() << "[MainWindow] 重连成功";

    // 在聊天框显示重连成功提示
    if (ui->textEdit) {
        QTextCursor c = ui->textEdit->textCursor();
        c.movePosition(QTextCursor::End);
        ui->textEdit->setTextCursor(c);
        ui->textEdit->insertHtml(QString("<span style='color: green;'>%1</span>")
                                .arg(tr("Reconnection successful, synchronizing data...")));
        ui->textEdit->verticalScrollBar()->setValue(ui->textEdit->verticalScrollBar()->maximum());
        ui->textEdit->insertPlainText("\n");
    }

    // 注意：Canvas的loading状态将在数据同步完成后解除
}

void MainWindow::onReconnectionFailed(const QString &reason)
{
    qDebug() << "[MainWindow] 重连失败:" << reason;

    // 在聊天框显示重连失败提示
    if (ui->textEdit) {
        QTextCursor c = ui->textEdit->textCursor();
        c.movePosition(QTextCursor::End);
        ui->textEdit->setTextCursor(c);
        ui->textEdit->insertHtml(QString("<span style='color: red;'>%1</span>")
                                .arg(tr("Reconnection failed: %1").arg(reason)));
        ui->textEdit->verticalScrollBar()->setValue(ui->textEdit->verticalScrollBar()->maximum());
        ui->textEdit->insertPlainText("\n");
    }

    // 解除Canvas loading状态（重连失败）
    if (ui->canvas) {
        ui->canvas->setArchiveLoading(false);
    }

    // 更新网络指示器
    if (networkIndicator_) {
        networkIndicator_->setLevel(NetworkIndicator::LEVEL::NONE);
    }
}

void MainWindow::onReconnectionCompleted()
{
    qDebug() << "[MainWindow] 重连完成，数据同步结束";

    // 在聊天框显示重连完成提示
    if (ui->textEdit) {
        QTextCursor c = ui->textEdit->textCursor();
        c.movePosition(QTextCursor::End);
        ui->textEdit->setTextCursor(c);
        ui->textEdit->insertHtml(QString("<span style='color: green;'>%1</span>")
                                .arg(tr("Reconnection completed, you can continue drawing")));
        ui->textEdit->verticalScrollBar()->setValue(ui->textEdit->verticalScrollBar()->maximum());
        ui->textEdit->insertPlainText("\n");
    }

    // 解除Canvas loading状态（重连完成）
    if (ui->canvas) {
        ui->canvas->setArchiveLoading(false);
    }

    // 重新启动在线列表定时器
    startOnlineListTimer();

    // 重新启动心跳
    if (clientSocket_) {
        clientSocket_->enableHeartbeat(true);
    }
}

void MainWindow::onAboutToClose()
{
    QMessageBox::warning(this,
                         tr("Closing"),
                         tr("Warning, the room owner has "
                            "closed the room. This room will close"
                            " when everyone leaves.\n"
                            "Save your work if you like it!"));
}

void MainWindow::onAllLayerCleared()
{
    ui->canvas->clearAllLayer();
}

void MainWindow::onMemberlistFetched(const QHash<QString, QVariantList> &list)
{
    ui->memberList->setMemberList(list);
//    ui->statusBar->showMessage(tr("Online List Refreshed."),
//                               2000);
}

void MainWindow::onNotify(const QString &content)
{
    if (content.isEmpty()) {
        return;
    }

    // 安全检查
    if (!ui || !ui->textEdit) {
        qWarning() << "[MainWindow] onNotify: ui or textEdit is null";
        return;
    }

    QTextCursor c = ui->textEdit->textCursor();
    c.movePosition(QTextCursor::End);
    ui->textEdit->setTextCursor(c);
    ui->textEdit->insertHtml(content);
    ui->textEdit->verticalScrollBar()
            ->setValue(ui->textEdit->verticalScrollBar()
                       ->maximum());
    ui->textEdit->insertPlainText("\n");

    // 收到通知后，重新获取在线列表
    if (clientSocket_) {
        clientSocket_->requestOnlinelist();
    }
}

void MainWindow::onKicked()
{
    GradualBox::showText(tr("You've been kicked by room owner."), true, 3000);
}

void MainWindow::onDelayGet(const int delay)
{
    typedef NetworkIndicator::LEVEL NL;
    if(delay < 0){
        networkIndicator_->setLevel(NL::UNKNOWN);
        return;
    }
    if(delay > 60){
        networkIndicator_->setLevel(NL::NONE);
        return;
    }
    if(delay > 20){
        networkIndicator_->setLevel(NL::LOW);
        return;
    }
    if(delay > 10){
        networkIndicator_->setLevel(NL::MEDIUM);
        return;
    }
    if(delay < 10){
        networkIndicator_->setLevel(NL::GOOD);
        return;
    }
}



void MainWindow::onNewMessage(const QString &content)
{
    QTextCursor c = ui->textEdit->textCursor();
    c.movePosition(QTextCursor::End);
    ui->textEdit->setTextCursor(c);
    ui->textEdit->insertPlainText(content);
    ui->textEdit->verticalScrollBar()
            ->setValue(ui->textEdit->verticalScrollBar()
                       ->maximum());

    QSettings settings(GlobalDef::SETTINGS_NAME,
                       QSettings::defaultFormat(),
                       qApp);
    bool msg_notify = settings.value("chat/msg_notify", true).toBool();
    if(!this->isActiveWindow() && msg_notify)
        PlatformExtend::notify(this);
}

void MainWindow::onSendPressed()
{
    QString string(ui->lineEdit->text());
    if(string.isEmpty() || string.length()>256){
        qDebug()<<"Warnning: text too long or empty.";
        return;
    }
    // 修改消息发送方式，SSE 客户端使用 sendChatMessage
    QString messageContent = string;
    if (clientSocket_) {
        clientSocket_->sendChatMessage(messageContent);
    }
    ui->lineEdit->clear();
}

void MainWindow::onColorGridDroped(int id)
{
    QColor c = ui->colorBox->color();
    ui->colorGrid->setColor(id, c);
}

void MainWindow::onColorGridPicked(int, const QColor &c)
{
    ui->colorBox->setColor(c);
}

void MainWindow::onBrushTypeChange()
{
    changeToBrush(sender()->objectName());
}

void MainWindow::onBrushSettingsChanged(const QVariantMap &m)
{
    qDebug() << "[MainWindow] onBrushSettingsChanged" << m;
    int width = m["width"].toInt();
    int hardness = m["hardness"].toInt();
    int thickness = m["thickness"].toInt();
    int water = m["water"].toInt();
    int extend = m["extend"].toInt();
    int mixin = m["mixin"].toInt();
    QVariantMap colorMap = m["color"].toMap();
    QColor c(colorMap["red"].toInt(),
            colorMap["green"].toInt(),
            colorMap["blue"].toInt());

    // INFO: to prevent scaled to 1px, should always
    // change width first
    if(brushSettingControl_){
        if(brushSettingControl_->width() != width)
            brushSettingControl_->setWidth(width);
        if(brushSettingControl_->hardness() != hardness)
            brushSettingControl_->setHardness(hardness);
        if(brushSettingControl_->thickness() != thickness)
            brushSettingControl_->setThickness(thickness);
        if(brushSettingControl_->water() != water)
            brushSettingControl_->setWater(water);
        if(brushSettingControl_->extend() != extend)
            brushSettingControl_->setExtend(extend);
        if(brushSettingControl_->mixin() != mixin)
            brushSettingControl_->setMixin(mixin);
    }
    if(ui->colorBox->color() != c)
        ui->colorBox->setColor(c);

}

void MainWindow::onPanoramaRefresh()
{
    ui->panorama->onImageChange(ui->canvas->grab(),
                                ui->centralWidget->visualRect().toRect());
}

void MainWindow::onMoveToolPressed(bool c)
{
    ui->canvas->onMoveTool(c);
    if(brushActionGroup_){
        brushActionGroup_->setDisabled(c);
    }
    if(colorPickerButton_){
        colorPickerButton_->setDisabled(c);
    }
}

void MainWindow::onColorPickerPressed(bool c)
{
    ui->canvas->onColorPicker(c);
    if(brushActionGroup_){
        brushActionGroup_->setDisabled(c);
    }
    if(moveToolButton_){
        moveToolButton_->setDisabled(c);
    }
}

void MainWindow::onCanvasToolComplete()
{
    if(brushActionGroup_){
        brushActionGroup_->setDisabled(false);
    }
    if(colorPickerButton_){
        colorPickerButton_->setChecked(false);
    }
    if(moveToolButton_){
        moveToolButton_->setChecked(false);
    }
}



void MainWindow::changeToBrush(const QString &brushName)
{
    // 检查是否为v3笔刷
    if (Singleton<BrushManager>::instance().isV3Brush(brushName)) {
        ui->canvas->changeBrushV3(brushName);
        // v3笔刷暂时不支持所有设置，禁用相关控件
        if (this->brushSettingControl_) {
            this->brushSettingControl_->setHardnessEnabled(false);
            this->brushSettingControl_->setThicknessEnabled(false);
            this->brushSettingControl_->setWaterEnabled(false);
            this->brushSettingControl_->setExtendEnabled(false);
            this->brushSettingControl_->setMixinEnabled(false);
        }
    } else {
        ui->canvas->changeBrush(brushName);
        auto f = ui->canvas->brushFeatures();
        if(!this->brushSettingControl_){
            return;
        }
        this->brushSettingControl_->setHardnessEnabled(f.support(BrushFeature::HARDNESS));
        this->brushSettingControl_->setThicknessEnabled(f.support(BrushFeature::THICKNESS));
        this->brushSettingControl_->setWaterEnabled(f.support(BrushFeature::WATER));
        this->brushSettingControl_->setExtendEnabled(f.support(BrushFeature::EXTEND));
        this->brushSettingControl_->setMixinEnabled(f.support(BrushFeature::MIXIN));
    }

    // onBrushSettingsChanged(ui->canvas->brushSettings());
}

void MainWindow::remoteAddLayer(const QString &layerName)
{
    if( layerName.isEmpty() ){
        return;
    }

    LayerItem *item = new LayerItem;
    QIcon visibility(":/iconset/ui/visibility-on.png");
    visibility.addFile(":/iconset/ui/visibility-off.png",
                       QSize(),
                       QIcon::Selected,
                       QIcon::On);
    item->setVisibleIcon(visibility);
    QIcon lock(":/iconset/ui/lock.png");
    lock.addFile(":/iconset/ui/unlock.png",
                 QSize(),
                 QIcon::Selected,
                 QIcon::On);
    item->setLockIcon(lock);
    item->setLabel(layerName);
    ui->layerWidget->addItem(item);
}

void MainWindow::addLayer(const QString &layerName)
{
    QString name = layerName;
    if(name.isNull() || name.isEmpty())
        name = QString::number(ui->canvas->layerNum());

    LayerItem *item = new LayerItem;
    QIcon visibility(":/iconset/ui/visibility-on.png");
    visibility.addFile(":/iconset/ui/visibility-off.png",
                       QSize(),
                       QIcon::Selected,
                       QIcon::On);
    item->setVisibleIcon(visibility);
    QIcon lock(":/iconset/ui/lock.png");
    lock.addFile(":/iconset/ui/unlock.png",
                 QSize(),
                 QIcon::Selected,
                 QIcon::On);
    item->setLockIcon(lock);
    item->setLabel(name);
    ui->layerWidget->addItem(item);
    ui->canvas->addLayer(name);

    // NOTICE: disable single layer clear due to lack of
    // a way to store this action in server history
    //    QAction *clearOne = new QAction(this);
    //    ui->menuClear_Canvas->insertAction(ui->actionAll_Layers,
    //                                       clearOne);
    //    clearOne->setText(tr("Layer ")+name);
    //    connect(clearOne, &QAction::triggered,
    //            [this, name, clearOne](){
    //        this->clearLayer(name);
    //    });
}

void MainWindow::deleteLayer()
{
    LayerItem * item = ui->layerWidget->selected();
    QString text = item->label();
    bool sucess = ui->canvas->deleteLayer(text);
    if(sucess) ui->layerWidget->removeItem(item);
}

void MainWindow::clearLayer(const QString &name)
{
    auto result = QMessageBox::question(this,
                                        tr("OMG"),
                                        tr("You're going to clear layer %1. "
                                           "All the work of that layer"
                                           "will be deleted and CANNOT be undone.\n"
                                           "Do you really want to do so?").arg(name),
                                        QMessageBox::Yes|QMessageBox::No);
    if(result == QMessageBox::Yes){
        ui->canvas->clearLayer(name);
        // SSE 客户端暂时不支持单层清空，显示提示信息
        QMessageBox::information(this,
                                 tr("Notice"),
                                 tr("Layer clearing command sent to canvas.\n"
                                    "Server-side layer clearing is not yet implemented "
                                    "in the SSE client version."));
    }
}

void MainWindow::clearAllLayer()
{
    if (!clientSocket_->isRoomOwner()) {
        QMessageBox::warning(this,
                         tr("Warning"),
                         tr("You are not the room owner, you can't close the room."),
                         QMessageBox::Close);
        return;
    }
    auto result = QMessageBox::question(this,
                                        tr("OMG"),
                                        tr("You're going to clear ALL LAYERS"
                                           ". All of work in this room"
                                           "will be deleted and CANNOT be undone.\n"
                                           "Do you really want to do so?"),
                                        QMessageBox::Yes|QMessageBox::No);
    if(result == QMessageBox::Yes){
        if (clientSocket_) {
            clientSocket_->clearCanvas();
        } else {
            qDebug() << "[MainWindow] 未连接到房间，无法清空画布";
        }
    }
}

void MainWindow::deleteLayer(const QString &name)
{
    bool sucess = ui->canvas->deleteLayer(name);
    if(sucess) ui->layerWidget->removeItem(name);
}

void MainWindow::closeEvent( QCloseEvent * event )
{
    ui->canvas->pause();

    // 停止定时器
    if (onlineListTimer_) {
        onlineListTimer_->stop();
    }

    // 新增：在关闭前保存画布快照
    if (ui->canvas) {
        qDebug() << "[MainWindow] 关闭窗口前保存画布快照";
        ui->canvas->exportCanvasSnapshot();

        // 等待一小段时间确保快照保存完成
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        QThread::msleep(100);
    }

    // 先断开网络连接，避免在对象销毁过程中产生错误
    if (clientSocket_) {
        clientSocket_->disconnect();
    }

    // 等待一小段时间，确保网络操作完成
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    QProgressDialog dialog(tr("Waiting for sync, please do not close.\n"\
                              "This will cost you 1 minute at most."),
                           QString(),
                           0, 0, this);
    dialog.setWindowModality(Qt::ApplicationModal);
    dialog.show();

    // This is a workaround to make msgBox text shown
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    QSettings settings(GlobalDef::SETTINGS_NAME,
                       QSettings::defaultFormat(),
                       qApp);
    settings.setValue("colorgrid/pal",
                      ui->colorGrid->dataExport());
    settings.setValue("mainwindow/view",
                      saveState());

    // 保存房间信息（用于程序重启时复用Archive信息）
    roomInfoManager_->saveRoomInfoToSettings();

    settings.sync();

    dialog.close();

    event->accept();
}

void MainWindow::exportAllToFile()
{
    QString fileName =
            QFileDialog::getSaveFileName(this,
                                         tr("Export all to file"),
                                         this->windowTitle(),
                                         tr("Images (*.png)"));
    fileName = fileName.trimmed();
    if(fileName.isEmpty()){
        return;
    }
    if(!fileName.endsWith(".png", Qt::CaseInsensitive)){
        fileName = fileName + ".png";
    }
    QImage image = ui->canvas->allCanvas();
    image.save(fileName, "PNG");
}

void MainWindow::exportVisibleToFile()
{
    QString fileName =
            QFileDialog::getSaveFileName(this,
                                         tr("Export visible part to file"),
                                         this->windowTitle(),
                                         tr("Images (*.png)"));
    fileName = fileName.trimmed();
    if(fileName.isEmpty()){
        return;
    }
    if(!fileName.endsWith(".png", Qt::CaseInsensitive)){
        fileName = fileName + ".png";
    }
    QImage image = ui->canvas->currentCanvas();
    image.save(fileName, "PNG");
}

void MainWindow::exportToPSD()
{
    QString fileName =
            QFileDialog::getSaveFileName(this,
                                         tr("Export contents to psd file"),
                                         this->windowTitle(),
                                         tr("Photoshop Images (*.psd)"));
    fileName = fileName.trimmed();
    if(fileName.isEmpty()){
        return;
    }
    if(!fileName.endsWith(".psd", Qt::CaseInsensitive)){
        fileName = fileName + ".psd";
    }

    // save all layers into psd

    QProgressDialog *dialog = new QProgressDialog(tr("Exporting..."), QString(), 0, 0, this);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->show();
    QFutureWatcher<QByteArray> *watcher = new QFutureWatcher<QByteArray>;
    QFuture<QByteArray> *future = new QFuture<QByteArray>(QtConcurrent::run(imagesToPSD,
                                                                            ui->canvas->layerImages(),
                                                                            ui->canvas->allCanvas()));
    watcher->setFuture(*future);
    connect(watcher, &QFutureWatcher<QByteArray>::finished, [watcher, dialog, future, fileName](){
        QByteArray data = future->result();
        QFile file(fileName);
        if(!file.open(QIODevice::Truncate|QIODevice::WriteOnly)) {
            return;
        }
        qDebug()<<data.length();
        file.write(data);
        file.close();
        dialog->close();
        dialog->deleteLater();
        watcher->deleteLater();
    });
}

void MainWindow::exportAllToClipboard()
{
    QClipboard *cb = qApp->clipboard();
    QImage image = ui->canvas->allCanvas();
    cb->setImage(image);
}

void MainWindow::exportVisibleToClipboard()
{
    QClipboard *cb = qApp->clipboard();
    QImage image = ui->canvas->currentCanvas();
    cb->setImage(image);
}

void MainWindow::resetView()
{
    restoreState(defaultView);
}

void MainWindow::about()
{
    AboutDialog dialog(this);
    dialog.exec();
}

template<typename T, typename U>
bool MainWindow::regShortcut(const QString& name, T func, U func2)
{
    //    auto shortcut_type = (ShT)config["type"].toInt();
    return regShortcut<>(QKeySequence(shortcutManager_->shortcut(name)["key"].toString()),
            func, func2);
}

template<typename T>
bool MainWindow::regShortcut(const QString& name, T func)
{
    return regShortcut<>(QKeySequence(shortcutManager_->shortcut(name)["key"].toString()), func);
}

template<typename T, typename U>
bool MainWindow::regShortcut(const QKeySequence& k, T func, U func2)
{
    if(keyMap_.contains(k.toString())){
        return false;
    }
    keyMap_.insert(k.toString(), true);

    SingleShortcut *shortcut = new SingleShortcut(this);
    shortcut->setKey(k);
    connect(shortcut, &SingleShortcut::activated,
            func);
    connect(shortcut, &SingleShortcut::inactivated,
            func2);
    return true;
}

template<typename T>
bool MainWindow::regShortcut(const QKeySequence& k, T func)
{
    if(keyMap_.contains(k.toString())){
        return false;
    }
    keyMap_.insert(k.toString(), true);
    QShortcut* shortcut = new QShortcut(k, this);
    connect(shortcut, &QShortcut::activated,
            func);
    return true;
}

void MainWindow::onOnlineListTimer()
{
    // 检查是否仍然连接到房间
    if (!clientSocket_ || clientSocket_->roomName().isEmpty() || clientSocket_->getClientIdOfCurrentRoom().isEmpty()) {
        qDebug() << "[MainWindow] 定时器触发但未连接到房间，停止定时器";
        stopOnlineListTimer();
        return;
    }

    // 周期性获取在线列表
    qDebug() << "[MainWindow] 定时器触发：获取在线列表，房间:" << clientSocket_->roomName();
    clientSocket_->requestOnlinelist();
}

void MainWindow::startOnlineListTimer()
{
    if (onlineListTimer_ && !onlineListTimer_->isActive()) {
        // 检查是否已经连接到房间
        if (clientSocket_ && !clientSocket_->roomName().isEmpty() && !clientSocket_->getClientIdOfCurrentRoom().isEmpty()) {
            onlineListTimer_->start(10000); // 每10秒执行一次
            qDebug() << "[MainWindow] 启动周期性在线列表获取定时器，房间:" << clientSocket_->roomName();
        } else {
            qDebug() << "[MainWindow] 未连接到房间，跳过启动定时器";
        }
    }
}

void MainWindow::stopOnlineListTimer()
{
    if (onlineListTimer_ && onlineListTimer_->isActive()) {
        onlineListTimer_->stop();
        qDebug() << "[MainWindow] 停止周期性在线列表获取定时器";
    }
}

void MainWindow::onNewClientId(const QString &clientId)
{
    roomInfoManager_->updateClientIdOfRoom(clientSocket_->roomName(), clientId);
}

// ==================== 用户主动操作错误处理（带重试按钮的对话框）====================

void MainWindow::onRequestLoginFailed(const QString &errorMessage)
{
    QMessageBox::StandardButton reply = QMessageBox::critical(this, tr("Login Failed"),
        tr("An error occurred while logging into the room:\n%1\n\nWould you like to retry?").arg(errorMessage),
        QMessageBox::Retry | QMessageBox::Cancel);
    
    if (reply == QMessageBox::Retry) {
        // 重新尝试登录
        QString roomName = clientSocket_->roomName();
        QString nickname = clientSocket_->nickname();
        if (!roomName.isEmpty() && !nickname.isEmpty()) {
            clientSocket_->connectToRoom(roomName, nickname);
        }
    }
}

void MainWindow::onRequestChatMessageFailed(const QString &errorMessage)
{
    QMessageBox::critical(this, tr("Send Message Failed"),
        tr("An error occurred while sending chat message:\n%1\n\nPlease try again later.").arg(errorMessage),
        QMessageBox::Ok);
}

void MainWindow::onRequestDrawDataFailed(const QString &errorMessage)
{
    QMessageBox::critical(this, tr("Send Drawing Data Failed"),
        tr("An error occurred while sending drawing data:\n%1\n\nPlease try again later.").arg(errorMessage),
        QMessageBox::Ok);
}

void MainWindow::onRequestClearCanvasFailed(const QString &errorMessage)
{
    QMessageBox::StandardButton reply = QMessageBox::critical(this, tr("Clear Canvas Failed"),
        tr("An error occurred while clearing canvas:\n%1\n\nWould you like to retry?").arg(errorMessage),
        QMessageBox::Retry | QMessageBox::Cancel);
    
    if (reply == QMessageBox::Retry) {
        clientSocket_->clearCanvas();
    }
}

void MainWindow::onRequestCheckoutFailed(const QString &errorMessage)
{
    QMessageBox::StandardButton reply = QMessageBox::critical(this, tr("Renew Room Failed"),
        tr("An error occurred while renewing room:\n%1\n\nWould you like to retry?").arg(errorMessage),
        QMessageBox::Retry | QMessageBox::Cancel);
    
    if (reply == QMessageBox::Retry) {
        clientSocket_->requestCheckout();
    }
}

void MainWindow::onRequestCloseRoomFailed(const QString &errorMessage)
{
    QMessageBox::StandardButton reply = QMessageBox::critical(this, tr("Close Room Failed"),
        tr("An error occurred while closing room:\n%1\n\nWould you like to retry?").arg(errorMessage),
        QMessageBox::Retry | QMessageBox::Cancel);
    
    if (reply == QMessageBox::Retry) {
        clientSocket_->requestCloseRoom();
    }
}

void MainWindow::onRequestKickUserFailed(const QString &errorMessage)
{
    QMessageBox::critical(this, tr("Kick User Failed"),
        tr("An error occurred while kicking user:\n%1\n\nPlease try again later").arg(errorMessage),
        QMessageBox::Ok);
}

// ==================== 自动操作错误处理（GradualBox提示）====================

void MainWindow::onRequestOnlineListFailed(const QString &errorMessage)
{
    GradualBox::showText(tr("Failed to get online list: %1").arg(errorMessage), true, 3000);
}
