#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QJsonObject>

#include "../misc/shortcutmanager.h"

class QToolButton;
class BrushSettingsWidget;
class QActionGroup;
class NetworkIndicator;
class QTimer;
class RoomInfoManager;
class SSEClientSocket;

typedef ShortcutManager::ShortcutType ShT;

namespace Ui {
class MainWindow;
}

// 接受房间信息的构造函数
struct RoomConnectionInfo {
    QString roomName;
    QString webAddress;
    QString password;
    QString nickname;
    QJsonObject roomData;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
    
public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    template<typename T, typename U>
    bool regShortcut(const QString& name, T func, U func2);
    template<typename T>
    bool regShortcut(const QString& name, T func);
    template<typename T, typename U>
    bool regShortcut(const QKeySequence& k, T func, U func2);
    template<typename T>
    bool regShortcut(const QKeySequence& k, T func);

    void tryJoinRoom(const RoomConnectionInfo& roomInfo);

public slots:
    void exportAllToFile();
    void exportVisibleToFile();
    void exportAllToClipboard();
    void exportVisibleToClipboard();
    void exportToPSD();
    void resetView();
    void about();
    void onCanvasToolComplete();
    void changeToBrush(const QString& brushName);

    /* layer operations */
    void remoteAddLayer(const QString &layerName);
    void addLayer(const QString &name = QString());
    void deleteLayer();
    void deleteLayer(const QString &name);
    void clearLayer(const QString &name);
    void clearAllLayer();

    /* script - removed QtScript functionality */
signals:
    void sendMessage(QString);
    void brushColorChange(const QColor &color);
protected:
    void closeEvent( QCloseEvent * event ) ;
private:
    void init();
    void stylize();
    void layerWidgetInit();
    void colorGridInit();
    void viewInit();
    void statusBarInit();
    void toolbarInit();
    void shortcutInit();
    void socketInit();
    void requestCloseRoom();
    void requestKickUser(const QString& id);
    void startOnlineListTimer();
    void stopOnlineListTimer();

    Ui::MainWindow *ui;

    // 房间信息管理器和客户端套接字
    RoomInfoManager* roomInfoManager_;  // 指向单例实例的指针
    SSEClientSocket* clientSocket_;
    
    // 快捷键管理器
    ShortcutManager* shortcutManager_;

    QByteArray defaultView;
    QAction *lastBrushAction;
    BrushSettingsWidget *brushSettingControl_;
    QToolBar *toolbar_;
    QActionGroup *brushActionGroup_;
    QToolButton *colorPickerButton_;
    QToolButton *moveToolButton_;
    NetworkIndicator* networkIndicator_;
    QHash<QString, bool> keyMap_;
    QTimer* onlineListTimer_;

private slots:
    void onServerDisconnected();
    void onNewMessage(const QString &content);
    void onSendPressed();
    void onColorGridDroped(int);
    void onColorGridPicked(int, const QColor &);
    void onBrushTypeChange();
    void onBrushSettingsChanged(const QVariantMap &m);
    void onColorPickerPressed(bool c);
    void onMoveToolPressed(bool c);
    void onPanoramaRefresh();

    void onAboutToClose();
    void onAllLayerCleared();
    void onMemberlistFetched(const QHash<QString, QVariantList> &list);
    void onNotify(const QString &content);
    void onKicked();
    void onDelayGet(const int delay);
//    void onResponseHeartbeat(const QJsonObject &o);
    // 用户主动操作错误处理（带重试按钮的对话框）
    void onRequestLoginFailed(const QString &errorMessage);
    void onRequestChatMessageFailed(const QString &errorMessage);
    void onRequestDrawDataFailed(const QString &errorMessage);
    void onRequestClearCanvasFailed(const QString &errorMessage);
    void onRequestCheckoutFailed(const QString &errorMessage);
    void onRequestCloseRoomFailed(const QString &errorMessage);
    void onRequestKickUserFailed(const QString &errorMessage);
    
    // 自动操作错误处理（GradualBox提示）
    void onRequestOnlineListFailed(const QString &errorMessage);
    void onOnlineListTimer();
    void onNewClientId(const QString &clientId);
    void onRoomJoined();
    void onRoomExited();
    void onLoginCompleted(const QString &roomName, const QString &remoteArchiveSign);
    
    // 新增：重连相关槽函数
    void onReconnectionStarted();
    void onReconnectionSucceeded();
    void onReconnectionFailed(const QString &reason);
    void onReconnectionCompleted();
};

#endif // MAINWINDOW_H
