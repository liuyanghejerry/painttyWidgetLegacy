#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "../misc/shortcutmanager.h"

class QToolButton;
class BrushSettingsWidget;
class QActionGroup;

typedef ShortcutManager::ShortcutType ShT;

namespace Ui {
class MainWindow;
}

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

    /* project management */
    void newProject(int width, int height);
    bool openProject(const QString &filePath);
    bool saveProject();
    bool saveProjectAs();

    /* layer operations */
    void addLayer(const QString &name = QString());
    void deleteLayer();
    void deleteLayer(const QString &name);
    void clearLayer(const QString &name);
    void clearAllLayer();

    /* script - removed QtScript functionality */
signals:
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

    Ui::MainWindow *ui;

    // 快捷键管理器
    ShortcutManager* shortcutManager_;

    QByteArray defaultView;
    QAction *lastBrushAction;
    BrushSettingsWidget *brushSettingControl_;
    QToolBar *toolbar_;
    QActionGroup *brushActionGroup_;
    QToolButton *colorPickerButton_;
    QToolButton *moveToolButton_;
    QHash<QString, bool> keyMap_;
    QString currentProjectPath_;

    bool saveToFile(const QString &filePath);
    bool loadFromFile(const QString &filePath);
    bool promptSaveIfDirty();

private slots:
    void onColorGridDroped(int);
    void onColorGridPicked(int, const QColor &);
    void onBrushTypeChange();
    void onBrushSettingsChanged(const QVariantMap &m);
    void onColorPickerPressed(bool c);
    void onMoveToolPressed(bool c);
    void onPanoramaRefresh();
    void onNewProject();
    void onOpenProject();
    void onSaveProject();
    void onSaveProjectAs();
};

#endif // MAINWINDOW_H
