#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPair>

#include "../misc/shortcutmanager.h"
#include "../misc/projectfile.h"

class QToolButton;
class SingleShortcut;
class BrushSettingsWidget;
class QActionGroup;
class QMenu;
class QUndoStack;

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

    template<typename T>
    bool regShortcut(const QString& name, T func, bool autoRepeat = true);
    template<typename T>
    bool regShortcut(const QKeySequence& k, T func, bool autoRepeat = true);

public slots:
    void exportAllToFile();
    void exportVisibleToFile();
    void exportAllToClipboard();
    void exportVisibleToClipboard();
    void exportToPSD();
    void resetView();
    void about();
    void changeToBrush(const QString& brushName);

    /* project management */
    void newProject(int width, int height);
    bool openProject(const QString &filePath);
    bool saveProject();
    bool saveProjectAs();
    bool saveProjectTo(const QString &path);

    /* layer operations */
    void addLayer(const QString &name = QString());
    void deleteLayer();
    void deleteLayer(const QString &name);
    void clearLayer(const QString &name);
    void clearAllLayer();

signals:
    void brushColorChange(const QColor &color);
protected:
    bool event(QEvent *event) override;
    void closeEvent( QCloseEvent * event ) ;
private:
    void init();
    void stylize();
    void rebuildLayerList();
    void updateProjectTitle();
    QString lastProjectDirectory() const;
    QString defaultExportPath(const QString &extension) const;
    void rememberProject(const QString &path);
    void refreshRecentProjects();
    void importImage();
    void recordDocumentChange();
    void updateHistoryActions();
    void restoreDocument(const PaintingProject &project);
    void colorGridInit();
    void viewInit();
    void statusBarInit();
    void toolbarInit();
    void shortcutInit();

    Ui::MainWindow *ui;

    // 快捷键管理器
    ShortcutManager* shortcutManager_;

    QByteArray defaultView;
    BrushSettingsWidget *brushSettingControl_;
    QToolBar *toolbar_;
    QActionGroup *brushActionGroup_;
    QToolButton *colorPickerButton_;
    QToolButton *moveToolButton_;
    QToolButton *persistentTool_ = nullptr;
    QList<QPair<SingleShortcut *, QToolButton *>> heldTools_;
    void updateActiveTool();
    void registerTemporaryTool(const QKeySequence &key, QToolButton *button);
    QHash<QString, bool> keyMap_;
    QString currentProjectPath_;

    bool resettingDocument_ = false;
    QMenu *recentProjectsMenu_ = nullptr;
    QUndoStack *undoStack_ = nullptr;
    PaintingProject lastProjectState_;
    qint64 historyBytes_ = 0;
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
