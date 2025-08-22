#include "roomlistdialog.h"
#include "ui_roomlistdialog.h"

#include <QDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QTimer>
#include <QTableWidgetItem>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QCloseEvent>
#include <QSettings>
#include <QDateTime>
#include <QCryptographicHash>
#include <QShortcut>
#include <QKeySequence>
#include <QApplication>

#include "../common/common.h"
#include "../common/network/sse-clientsocket.h"
#include "newroomwindow.h"
#include "configuredialog.h"
#include "../common/room-info-manager.h"
#include "../common/crypto.h"

RoomListDialog::RoomListDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::RoomListDialog),
    clientSocket_(nullptr),
    timer(new QTimer(this)),
    newRoomWindow(new NewRoomWindow(this)),
    state_(Init),
    closeShortcut(nullptr),
    quitShortcut(nullptr)
{
    ui->setupUi(this);
    resize(width() * logicalDpiX() / 96, height() * logicalDpiY() / 96);

    timer->setSingleShot(true);
    
    // 使用全局单例的房间信息管理器
    RoomInfoManager& roomInfoManager = RoomInfoManager::instance();
    
    // 创建SSE客户端套接字
    clientSocket_ = new SSEClientSocket(&roomInfoManager, this);
    
    connect(ui->pushButton_6, &QPushButton::clicked,
            [this](){
        bool ok;
        QString text = QInputDialog::getText(this, tr("Room Url"),
                                             tr("Input room url:"), QLineEdit::Normal,
                                             "paintty://", &ok);
        if (ok && !text.isEmpty()){
            connectRoomByUrl(text);
        }
    });
    connect(ui->pushButton_5, &QPushButton::clicked,
            this, &RoomListDialog::openConfigure);
    connect(ui->pushButton_4,&QPushButton::clicked,
            this, &RoomListDialog::reject);
    connect(ui->pushButton_4,&QPushButton::clicked,
            this, &RoomListDialog::saveNick);
    connect(ui->pushButton_3,&QPushButton::clicked,
            this, &RoomListDialog::requestRoomList);
    connect(ui->pushButton_2,&QPushButton::clicked,
            this, &RoomListDialog::tryJoinRoomManually);
    connect(ui->tableWidget,&QTableWidget::cellDoubleClicked,
            this, &RoomListDialog::tryJoinRoomManually);
    connect(ui->checkBox, &QCheckBox::clicked,
            this, &RoomListDialog::updateRoomListTable);
    connect(ui->search_box, &QLineEdit::textChanged,
            this, &RoomListDialog::updateRoomListTable);

    connect(ui->pushButton, &QPushButton::clicked,
            newRoomWindow, &NewRoomWindow::show);
    connect(ui->pushButton, &QPushButton::clicked,
            [this]() { state_ = AboutToRequestNewRoom; });
    connect(newRoomWindow, &NewRoomWindow::newRoom,
            this,&RoomListDialog::requestNewRoom);
    connect(newRoomWindow, &NewRoomWindow::finished,
            this,&RoomListDialog::requestRoomList);

    connect(timer,&QTimer::timeout,
            this,&RoomListDialog::requestRoomList);

    connect(clientSocket_, &SSEClientSocket::roomCreated,
            this, &RoomListDialog::onNewRoomCreated);
    connect(clientSocket_, &SSEClientSocket::roomListFetched,
            this, &RoomListDialog::onRoomlist);
    connect(clientSocket_, &SSEClientSocket::roomJoined,
            this, &RoomListDialog::accept);
    connect(clientSocket_, &SSEClientSocket::managerUrlInvalid,
            this, &RoomListDialog::onManagerConnectFailed);
    connect(clientSocket_, &SSEClientSocket::managerAddressResolved,
            this, &RoomListDialog::onManagerAddressResolved);
    connect(clientSocket_, &SSEClientSocket::requestRoomListFailed, this,
            &RoomListDialog::onRequestRoomListFailed);
    connect(clientSocket_, &SSEClientSocket::requestNewRoomFailed, this,
            &RoomListDialog::onRequestNewRoomFailed);

    ui->counter_label->setText(tr("Rooms: %1, Members: %2")
                               .arg("?")
                               .arg("?"));
    
    // 设置快捷键
    setupShortcuts();
    
    tableInit();
    state_ = Ready;
    loadNick();
    
    // 房间信息管理将在外部处理
}

RoomListDialog::~RoomListDialog()
{
    qDebug() << "[RoomListDialog] Destructor called";
    
    // 先断开所有信号槽连接，避免在对象销毁过程中触发回调
    if (clientSocket_) {
        QObject::disconnect(clientSocket_, nullptr, this, nullptr);
    }
    
    // 清理快捷键
    if (closeShortcut) {
        delete closeShortcut;
        closeShortcut = nullptr;
    }
    if (quitShortcut) {
        delete quitShortcut;
        quitShortcut = nullptr;
    }
    
    // 最后再断开客户端连接
    if (clientSocket_) {
        clientSocket_->disconnect();
        clientSocket_->deleteLater();
    }
    
    delete ui;
}

void RoomListDialog::tableInit()
{
    ui->tableWidget->setColumnCount(4);
    QStringList list;
    list << QString(tr("Room Name"))
         << QString(tr("Privacy"))
         << QString(tr("Current Member"))
         << QString(tr("Max Member"));
    ui->tableWidget->setHorizontalHeaderLabels(list);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->tableWidget->setSortingEnabled(true);
    ui->tableWidget->sortByColumn(2, Qt::DescendingOrder);
}

void RoomListDialog::connectToManager()
{
    QSettings settings(GlobalDef::SETTINGS_NAME,
                       QSettings::defaultFormat(),
                       qApp);
    bool use_default_server = settings.value("global/server/use_default", true).toBool();
    QString addrStr = settings.value("global/server/addr", QString()).toString();

    QString addr;

    if(!use_default_server){
        qDebug()<<"using address in settings";
        addr = QString(addrStr);
    }else{
        qDebug()<<"using default address";
        addr = QString(GlobalDef::HOST_ADDR);
    }

    qDebug()<<"using address to connect server" << addr;
    if (clientSocket_) {
        clientSocket_->resolveManagerAddress(addr);
        // requestRoomList() 将在 onManagerAddressResolved 中调用
    }
}

bool RoomListDialog::collectUserInfo()
{
    ui->lineEdit->setText(ui->lineEdit->text().trimmed());
    nickName_ = ui->lineEdit->text();
    if(nickName_.isEmpty()){
        QMessageBox::warning( this,
                              tr("Warning"),
                              tr("You must have a valid nick name."),
                              QMessageBox::Close);
        return false;
    }
    return true;
}

void RoomListDialog::requestRoomList()
{
    if (newRoomWindow->isVisible() || state_ == RequestingList) {
        return;
    }
    if (clientSocket_) {
        clientSocket_->requestRoomList();
        changeState(RequestingList);
    }
}

void RoomListDialog::requestNewRoom(const QJsonObject &m)
{
    if (state_ == RequestingNewRoom) {
        return;
    }
    if (!collectUserInfo()) {
        return;
    }
    if (clientSocket_) {
        changeState(RequestingNewRoom);
        clientSocket_->setUserName(nickName_);
        clientSocket_->requestNewRoom(m);
    }
}

void RoomListDialog::tryJoinRoomManually()
{
    qDebug() << "[RoomListDialog] tryJoinRoomManually";
    if(!collectUserInfo()){
      qDebug() << "Unexpected State in tryJoinRoomManually" << state_
               << collectUserInfo();
      return;
    }
    
    // 获取选中的房间信息
    SelectedRoomInfo selectedRoom = getSelectedRoomInfo();


    if (selectedRoom.roomName.isEmpty()) {
        QMessageBox::warning(this, tr("Warning"),
                            tr("You didn't choose any room."),
                            QMessageBox::Close);
      return;
    }

    int current = selectedRoom.roomData.value("currentload").toDouble();
    int max = selectedRoom.roomData.value("maxload").toDouble();

    if(current >= max){
        QMessageBox::critical(this,
                              tr("Full loaded"),
                              tr("Cannot join a full loaded room."),
                              QMessageBox::Close);
        return;
    }

    changeState(RoomConnecting);
    this->accept();
}

void RoomListDialog::connectRoomByUrl(const QString& url)
{
    Q_UNUSED(url)  // 暂时未实现URL解析功能
    changeState(RoomConnecting);
    if(!collectUserInfo()) {
        return;
    }
    if (clientSocket_) {
        clientSocket_->setUserName(nickName_);
        // TODO: 这里需要解析url，获取password和roomName
        // clientSocket_->tryJoinRoom(url, roomName_);
    }
}

void RoomListDialog::onRoomlist(const QHash<QString, QJsonObject> &obj)
{
    qDebug()<<"onRoomlist";
    changeState(Ready);
    roomsInfo = obj;
    updateRoomListTable();
    timer->start(REFRESH_TIME);
    RoomInfoManager &roomInfoManager = RoomInfoManager::instance();
    roomInfoManager.clearRoomInfoFromSettingsByList(obj.keys());
}

void RoomListDialog::onNewRoomCreated(const QJsonObject& roomInfo)
{
    // 自动连接到新创建的房间
    if (roomInfo.isEmpty()) {
        qDebug() << "[RoomListDialog] 新创建的房间信息为空";
        return;
    }
    newRoomWindow->complete();
    changeState(NewRoomCreated);

    QString webAddress = QString("%1://%2:%3")
        .arg(roomInfo.value("webProtocol").toString())
        .arg(roomInfo.value("webHost").toString())
        .arg(roomInfo.value("webPort").toInt());

    QString roomName = newRoomWindow->roomName();

    qDebug() << "[RoomListDialog] 自动连接到新创建的房间:" << roomName
             << "地址:" << webAddress;

    SelectedRoomInfo info;
    info.roomName = roomName;
    info.roomData = roomInfo;
    info.webAddress = webAddress;
    info.password = newRoomWindow->password();
    info.nickname = nickName_;
    createdRoomInfo_ = info;
    this->accept();
}

void RoomListDialog::onManagerConnectFailed(const QString& errorMessage)
{
    qDebug() << "[RoomListDialog] Manager connect failed:" << errorMessage;
    changeState(Error);
    QMessageBox::critical(this, tr("Error"), errorMessage);
}

void RoomListDialog::onManagerAddressResolved(const QString& originalHost, const QString& resolvedIp, const QString& finalUrl)
{
    qDebug() << "[RoomListDialog] Manager address resolved:" << originalHost << "->" << resolvedIp;
    qDebug() << "[RoomListDialog] Final URL:" << finalUrl;

    requestRoomList();
}

void RoomListDialog::onRequestRoomListFailed(const QString &errorMessage) {
    qDebug() << "[RoomListDialog] Request room list failed:" << errorMessage;
    changeState(RequestingListFailed);
    auto result = QMessageBox::critical(this, tr("Error"), errorMessage, QMessageBox::Retry | QMessageBox::No);
    if (result == QMessageBox::Retry) {
        requestRoomList();
    }
}

void RoomListDialog::onRequestNewRoomFailed(const QString &errorMessage) {
    qDebug() << "[RoomListDialog] Request new room failed:" << errorMessage;
    changeState(RequestingNewRoomFailed);
    QMessageBox::critical(this, tr("Error"), errorMessage);
    newRoomWindow->failed();
}

void RoomListDialog::updateRoomListTable()
{
    QString&& searchText = ui->search_box->text();
    QTableWidgetItem *item = 0;
    int row = 0;
    int column = 0;
    int members = 0;
    ui->progressBar->setMaximum(100);
    ui->tableWidget->clearContents();
    ui->tableWidget->setRowCount(0);
    ui->tableWidget->setSortingEnabled(false);
    for(auto& info: roomsInfo){
        QString name = info["name"].toString();
        int currentLoad = info["currentload"].toDouble();
        int maxLoad = info["maxload"].toDouble();
        if(ui->checkBox->isChecked()){
            //Don't show it if room is full
            if(currentLoad >= maxLoad){
                continue;
            }
        }

        if(!name.contains(searchText, Qt::CaseInsensitive)){
            continue;
        }

        if(row >= ui->tableWidget->rowCount())
            ui->tableWidget->insertRow(0);
        column = 0;

        item = new QTableWidgetItem(name);
        item->setTextAlignment(Qt::AlignCenter);
        ui->tableWidget->setItem(0, column++, item);

        bool isPrivate = info["private"].toBool();
        item = new QTableWidgetItem(
                    isPrivate?tr("Private"):tr("Public"));
        item->setTextAlignment(Qt::AlignCenter);
        ui->tableWidget->setItem(0, column++, item);

        item = new QTableWidgetItem;
        item->setTextAlignment(Qt::AlignCenter);
        // if current > max, there must be people joining via url
        if(currentLoad > maxLoad) {
            item->setData(Qt::DisplayRole, QString("%1+%2").arg(maxLoad).arg(currentLoad - maxLoad));
        } else {
            item->setData(Qt::DisplayRole, currentLoad);
        }
        ui->tableWidget->setItem(0, column++, item);
        members += currentLoad;

        item = new QTableWidgetItem;
        item->setTextAlignment(Qt::AlignCenter);
        item->setData(Qt::DisplayRole, maxLoad);
        ui->tableWidget->setItem(0, column++, item);

        row++;
    }
    ui->counter_label->setText(tr("Rooms: %1, Members: %2")
                               .arg(row)
                               .arg(members));
    ui->tableWidget->setSortingEnabled(true);
}

void RoomListDialog::changeState(RoomListDialog::State state)
{
    switch(state) {
    case RequestingListFailed:
    case RequestingNewRoomFailed:
    case Error:
        ui->progressBar->setRange(0,0);
        break;
    case Ready:
        ui->progressBar->setRange(0, 100);
        ui->progressBar->setValue(100);
        break;
    case RequestingList:
        ui->progressBar->setRange(0,0);
        break;
    case RequestingNewRoom:
        ui->progressBar->setRange(0,0);
        break;
    case NewRoomCreated:
        ui->progressBar->setRange(0, 100);
        ui->progressBar->setValue(100);
        break;
    case RoomConnecting:
        ui->progressBar->setRange(0,0);
        break;
    default:
        break;
    }
    state_ = state;
}

void RoomListDialog::loadNick()
{
    QSettings settings(GlobalDef::SETTINGS_NAME,
                       QSettings::defaultFormat(),
                       qApp);
    QByteArray data = settings.value("global/personal/nick")
            .toByteArray();
    QString name = QString::fromUtf8(data);
    if(name.isEmpty()
            || name.length() > 16){
        QMessageBox::information(this,
                                 tr("Notice"),
                                 tr("We're still in alpha test. "
                                    "This means the program may crash at any time "
                                    "in any condition.\nUse this software "
                                    "only when you accept it."));
    }else{
        ui->lineEdit->setText(name);
    }
}

void RoomListDialog::saveNick()
{
    collectUserInfo();
    if(nickName_.length() > 16){
        return;
    }
    QSettings settings(GlobalDef::SETTINGS_NAME,
                       QSettings::defaultFormat(),
                       qApp);
    settings.setValue("global/personal/nick",
                      nickName_.toUtf8());
    settings.sync();
}

void RoomListDialog::openConfigure()
{
    ConfigureDialog w;
    w.exec();
}

void RoomListDialog::hideEvent(QHideEvent *e)
{
    saveNick();
    QDialog::hideEvent(e);
}

void RoomListDialog::showEvent(QShowEvent *e)
{
    connectToManager();
    QDialog::showEvent(e);
}

void RoomListDialog::closeEvent(QCloseEvent *e)
{
    saveNick();
    e->accept();
}

void RoomListDialog::setupShortcuts()
{
    // 根据操作系统设置不同的快捷键
#ifdef Q_OS_MAC
    // macOS: Cmd+W 关闭窗口, Cmd+Q 退出应用 (Qt中Ctrl会自动映射到Command键)
    closeShortcut = new QShortcut(QKeySequence("Ctrl+W"), this);
    quitShortcut = new QShortcut(QKeySequence("Ctrl+Q"), this);
#elif defined(Q_OS_WIN)
    // Windows: Alt+F4 关闭窗口, Ctrl+Q 退出应用
    closeShortcut = new QShortcut(QKeySequence("Alt+F4"), this);
    quitShortcut = new QShortcut(QKeySequence("Ctrl+Q"), this);
#else
    // Linux/Unix: Ctrl+W 关闭窗口, Ctrl+Q 退出应用
    closeShortcut = new QShortcut(QKeySequence("Ctrl+W"), this);
    quitShortcut = new QShortcut(QKeySequence("Ctrl+Q"), this);
#endif

    // 连接快捷键信号到槽函数
    connect(closeShortcut, &QShortcut::activated, this, &RoomListDialog::closeWindow);
    connect(quitShortcut, &QShortcut::activated, this,
            &RoomListDialog::closeWindow);
}

void RoomListDialog::closeWindow()
{
    // 关闭当前窗口
    close();
}

void RoomListDialog::quitApplication()
{
    // 退出整个应用程序
    QApplication::quit();
}

RoomListDialog::SelectedRoomInfo RoomListDialog::getSelectedRoomInfo() const
{
    if (state_ == NewRoomCreated) {
        return createdRoomInfo_;
    }

    SelectedRoomInfo info;
    
    // 获取当前昵称
    info.nickname = nickName_;
    
    // 获取选中的房间信息
    QList<QTableWidgetItem *> list = ui->tableWidget->selectedItems();
    if (!list.isEmpty()) {
        QString roomName = ui->tableWidget->item(list.at(0)->row(), 0)->text();
        if (roomsInfo.contains(roomName)) {
            info.roomName = roomName;
            info.roomData = roomsInfo[roomName];
            
            // 构建web地址
            info.webAddress = QString("%1://%2:%3")
                .arg(roomsInfo[roomName].value("webProtocol").toString())
                .arg(roomsInfo[roomName].value("webHost").toString())
                .arg(roomsInfo[roomName].value("webPort").toInt());
        }
    }
    
    return info;
}

