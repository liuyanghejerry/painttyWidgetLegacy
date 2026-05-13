#include <QApplication>
#include <QTranslator>
#include <QSettings>
#include <QFontDatabase>
#include <QVariant>
#include <QDir>
#include <QThread>
#include <QDebug>
#include <QDateTime>
#include "common/common.h"
#include "widgets/mainwindow.h"

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

    MainWindow w;
    w.showMaximized();
    return a.exec();
}
