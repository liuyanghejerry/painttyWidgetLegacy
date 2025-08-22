#ifndef ROOMLISTDIALOG_H
#define ROOMLISTDIALOG_H

#include <QDialog>
#include <QWidget>
#include <QTimer>
#include <QHash>
#include <QJsonObject>
#include <QShortcut>
#include <QString>
#include <QByteArray>

#include "../misc/router.h"

class NewRoomWindow;
class SSEClientSocket;

class Ui_RoomListDialog;

// TODO: re-arrange APIs

class RoomListDialog : public QDialog
{
    Q_OBJECT
    
public:
    explicit RoomListDialog(QWidget *parent = 0);
    ~RoomListDialog();
    
    // 获取选中的房间信息
    struct SelectedRoomInfo {
        QString roomName;
        QString webAddress;
        QString password;
        QString nickname;
        QJsonObject roomData;
    };
    
    SelectedRoomInfo getSelectedRoomInfo() const;

public slots:
    void requestNewRoom(const QJsonObject &m);
    void requestRoomList();
    void updateRoomListTable();
    void connectRoomByUrl(const QString& url);
private slots:
    void onRoomlist(const QHash<QString, QJsonObject> &obj);
    void onNewRoomCreated(const QJsonObject& roomInfo);
    void loadNick();
    void saveNick();
    void openConfigure();
    void closeWindow();
    void quitApplication();
    void onManagerConnectFailed(const QString& errorMessage);
    void onManagerAddressResolved(const QString& originalHost, const QString& resolvedIp, const QString& finalUrl);
    void onRequestRoomListFailed(const QString& errorMessage);
    void onRequestNewRoomFailed(const QString& errorMessage);

protected:
    void hideEvent(QHideEvent *e);
    void showEvent(QShowEvent *e);
    void closeEvent(QCloseEvent *e);
    
    bool collectUserInfo();
    void tryJoinRoomManually();
private:
    enum State{
        Error = -999,
        ConnectFailed,
        Init,
        Ready = 0,
        RequestingList,
        RequestingListFailed,
        AboutToRequestNewRoom,
        RequestingNewRoom,
        RequestingNewRoomFailed,
        NewRoomCreated,
        RoomConnecting
    };

    Ui_RoomListDialog *ui;
    SSEClientSocket* clientSocket_;
    static const int REFRESH_TIME = 10000;
    QString nickName_;
    QTimer *timer;
    NewRoomWindow *newRoomWindow;
    QHash<QString, QJsonObject> roomsInfo;
    State state_;
    QShortcut *closeShortcut;
    QShortcut *quitShortcut;
    SelectedRoomInfo createdRoomInfo_;
    void tableInit();
    void connectToManager();
    void changeState(State state);
    void setupShortcuts();
};

#endif // ROOMLISTDIALOG_H
