#include <QApplication>
#include <QProcess>
#include <QTranslator>
#include <QSettings>
#include <QFontDatabase>
#include <QVariant>
#include <QDir>
#include <QMessageBox>
#include <QThread>
#include <QDebug>
#include <QDateTime>
#include <QInputDialog>
#include <QLineEdit>
#include "common/common.h"
#include "widgets/mainwindow.h"
#include "widgets/roomlistdialog.h"
#include "widgets/gradualbox.h"
#include "widgets/waitupdaterdialog.h"
#include "common/room-info-manager.h"

namespace mainOnly
{
QPalette& rePalette(QPalette &p)
{
    p.setColor(QPalette::ToolTipText, QColor::fromRgb(38, 38, 53));
    p.setColor(QPalette::WindowText, QColor::fromRgb(38, 38, 53));
    p.setColor(QPalette::Text, QColor::fromRgb(38, 38, 53));
    p.setColor(QPalette::Button, QColor::fromRgb(249, 249, 249));
    p.setColor(QPalette::ButtonText, QColor::fromRgb(38, 38, 53));
    p.setColor(QPalette::BrightText, QColor::fromRgb(249, 249, 249));
    p.setColor(QPalette::Highlight, QColor::fromRgb(255, 68, 0));
    p.setColor(QPalette::HighlightedText, QColor::fromRgb(249, 249, 249));
    p.setColor(QPalette::Window, QColor::fromRgb(249, 249, 249));
    p.setColor(QPalette::Base, QColor::fromRgb(249, 249, 249));
    p.setColor(QPalette::Link, QColor::fromRgb(255, 68, 0));
    p.setColor(QPalette::LinkVisited, QColor::fromRgb(255, 68, 0));
    return p;
}

void initStyle()
{
    QApplication::setStyle("Fusion");
    auto p = QApplication::palette();
    p = mainOnly::rePalette(p);
    QApplication::setPalette(p);
}

void initSettings()
{
    QSettings::setDefaultFormat(QSettings::IniFormat);

    QSettings settings(GlobalDef::SETTINGS_NAME,
                       QSettings::defaultFormat(),
                       qApp);
    QString clientVersion = settings.value("global/version/client",
                                           GlobalDef::CLIENT_VER)
            .toString();
    settings.setValue("global/version/client", clientVersion);
    settings.sync();
}

void initTranslation()
{
    QSettings settings(GlobalDef::SETTINGS_NAME,
                       QSettings::defaultFormat(),
                       qApp);

    QTranslator *qtTranslator = new QTranslator(qApp);
    QTranslator *myappTranslator = new QTranslator(qApp);

    QString locale = settings.value("global/language", "")
            .toString();
    if(locale.isEmpty())
        locale = QLocale(QLocale::system().uiLanguages().at(0)).name();

    qtTranslator->load(QString("qt_%1").arg(locale), ":/translation", "_", ".qm");
    myappTranslator->load(QString("paintty_%1").arg(locale), ":/translation", "_", ".qm");
    QCoreApplication::installTranslator(qtTranslator);
    QCoreApplication::installTranslator(myappTranslator);
}
void initFonts()
{
    QSettings settings(GlobalDef::SETTINGS_NAME,
                       QSettings::defaultFormat(),
                       qApp);
    bool use_droid_font = settings.value("global/use_droid_font", false)
            .toBool();
    if(!use_droid_font){
        return;
    }

    int  ret = QFontDatabase::addApplicationFont(":/fonts/DroidSansFallback.ttf");
    if(ret < 0){
        qDebug()<<"Cannot load fonts!";
    }
    QStringList strList(QFontDatabase::applicationFontFamilies(ret));
    if (strList.count() > 0){
        QFont fontThis(strList.at(0));
        //        fontThis.setPointSize(9);
        if(qApp->font().pointSize() == -1){
            fontThis.setPixelSize(qApp->font().pixelSize());
        }else{
            fontThis.setPointSize(qApp->font().pointSize());
        }

        qApp->setFont(fontThis);
    }
}

bool runUpdater()
{
    QStringList args;
    args<<"-v"<< QString::number(GlobalDef::CLIENT_VER, 10)
       <<"-p"<< QString::number(qApp->applicationPid(), 10);

    qDebug()<<"try to start updater: "<<args;

    // TODO: considering using detached way,
    // which won't kill updater when Mr.Paint is get killed or be closed
    //    QProcess::startDetached(QDir::current().filePath("updater"), args, QDir::currentPath());

    QProcess *process = new QProcess(qApp);
    process->setWorkingDirectory(QDir::currentPath());
    process->start(QDir::current().filePath("updater"), args);
    if (!process->waitForStarted()){
        GradualBox::showText(QObject::tr("We cannot find updater.\n"
                                         "You may need to check update yourself."));
        return false;
    }
    WaitUpdaterDialog *dialog = new WaitUpdaterDialog;
    WaitUpdaterDialog::connect(dialog, &WaitUpdaterDialog::rejected,
                               process, &QProcess::terminate);
    WaitUpdaterDialog::connect(process,
                               static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                               dialog, &WaitUpdaterDialog::close);
    dialog->exec();
    dialog->deleteLater();
    return true;
}

void customMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QThread *currentThread = QThread::currentThread();
    QString threadName = currentThread->objectName();
    if (threadName.isEmpty()) {
        // 为主线程设置特殊名称
        if (currentThread == QApplication::instance()->thread()) {
            threadName = "MainThread";
        } else {
            threadName = QString("Thread-%1").arg((quintptr)currentThread->currentThreadId());
        }
    }
    
    QString level;
    switch (type) {
        case QtDebugMsg:
            level = "D";
            break;
        case QtWarningMsg:
            level = "W";
            break;
        case QtCriticalMsg:
            level = "C";
            break;
        case QtFatalMsg:
            level = "F";
            break;
        default:
            level = "?";
            break;
    }
    
    // 提取文件名（去掉路径）
    QString fileName = QString(context.file);
    int lastSlash = fileName.lastIndexOf('/');
    if (lastSlash != -1) {
        fileName = fileName.mid(lastSlash + 1);
    }
    
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
    QString logMessage = QString("[%1][%2][%3] %4:%5 - %6")
                        .arg(level)
                        .arg(threadName)
                        .arg(timestamp)
                        .arg(fileName)
                        .arg(context.line)
                        .arg(msg);
    
    fprintf(stderr, "%s\n", qPrintable(logMessage));
    
    if (type == QtFatalMsg) {
        abort();
    }
}

void adjustLog()
{
    qInstallMessageHandler(customMessageHandler);
}

} // namespace mainOnly

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    mainOnly::adjustLog();

#ifdef Q_OS_MACOS
    QDir::setCurrent(a.applicationDirPath());
#endif
    mainOnly::initStyle();
    mainOnly::initSettings();
    mainOnly::initTranslation();
    mainOnly::initFonts();

    // 初始化全局房间信息管理器单例
    RoomInfoManager& roomInfoManager = RoomInfoManager::instance();
    roomInfoManager.loadRoomInfoFromSettings();

    // 创建 RoomListDialog，使用全局单例的 roomInfoManager
    RoomListDialog* dialog = new RoomListDialog(nullptr);
    
    int exitCode = 0;
#ifndef Q_OS_MACOS
    dialog->show();
#endif
#ifndef Q_OS_LINUX
    mainOnly::runUpdater();
#endif
    
    while (!exitCode && dialog->exec()) {
        dialog->hide();

        // 获取选中的房间信息
        RoomListDialog::SelectedRoomInfo selectedRoom = dialog->getSelectedRoomInfo();

        qDebug() << "selectedRoom: " << selectedRoom.roomName << selectedRoom.webAddress << selectedRoom.password << selectedRoom.nickname << selectedRoom.roomData;
        
        // 如果有选中的房间信息，处理密码输入
        if (!selectedRoom.roomName.isEmpty() && !selectedRoom.webAddress.isEmpty()) {
            // 如果是私有房间，需要用户输入密码
            if (selectedRoom.roomData.value("private").toBool()) {
                bool isOk = false;
                QString password = QInputDialog::getText(dialog,
                                                       QObject::tr("Password"),
                                                       QObject::tr("This is a private room, please input password:"),
                                                       QLineEdit::PasswordEchoOnEdit,
                                                       QString(),
                                                       &isOk);
                if (!isOk) {
                    // 用户取消了密码输入，跳过这个房间
                    continue;
                }
                selectedRoom.password = password;
                selectedRoom.password.truncate(16);
            }
            
            RoomConnectionInfo roomInfo;
            roomInfo.roomName = selectedRoom.roomName;
            roomInfo.webAddress = selectedRoom.webAddress;
            roomInfo.password = selectedRoom.password;
            roomInfo.nickname = selectedRoom.nickname;
            roomInfo.roomData = selectedRoom.roomData;
            
            // 创建新的 MainWindow 实例并连接到房间
            MainWindow* roomWindow = new MainWindow();
            roomWindow->showMaximized();
            roomWindow->tryJoinRoom(roomInfo);
            exitCode = a.exec();
            delete roomWindow;
        }
    }

    delete dialog;
    a.quit();

    return 0;
}
