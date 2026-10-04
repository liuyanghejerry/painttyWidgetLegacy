#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QActionGroup>
#include <QSettings>
#include <QShortcut>
#include <QToolBar>
#include <QToolButton>
#include <QApplication>
#include <QTextStream>
#include <QStatusBar>
#include <QInputDialog>
#include <QImageReader>
#include <QImageWriter>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QMenu>
#include <QVBoxLayout>
#include <QDockWidget>
#include <algorithm>
#include <QUndoStack>
#include <QUndoCommand>

#include "../common/common.h"
#include "../misc/projectfile.h"
#include "../misc/psdexport.h"
#include "../misc/shortcutmanager.h"
#include "../misc/singleshortcut.h"
#include "../misc/singleton.h"
#include "../paintingTools/brush/brushmanager.h"
#include "canvas.h"
#include "canvascontainer.h"
#include "newprojectdialog.h"
#include "panoramawidget.h"
#include "aboutdialog.h"
#include "brushsettingswidget.h"
#include "colorgrid.h"
#include "configuredialog.h"
#include "layeritem.h"
#include "layerwidget.h"
#include "colorbox.h"

namespace {
class DocumentChange : public QUndoCommand
{
public:
    DocumentChange(const PaintingProject &before, const PaintingProject &after,
                   std::function<void(const PaintingProject &)> apply)
        : before_(before), after_(after), apply_(std::move(apply)) {}
    void undo() override { apply_(before_); }
    void redo() override {
        if (firstRedo_) firstRedo_ = false;
        else apply_(after_);
    }
private:
    PaintingProject before_, after_;
    std::function<void(const PaintingProject &)> apply_;
    bool firstRedo_ = true;
};
}

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    lastBrushAction(nullptr),
    brushSettingControl_(nullptr),
    toolbar_(nullptr),
    brushActionGroup_(nullptr),
    colorPickerButton_(nullptr),
    moveToolButton_(nullptr)
{
    ui->setupUi(this);
    init();
}

MainWindow::~MainWindow()
{
    qDebug() << "MainWindow::~MainWindow";

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
    undoStack_ = new QUndoStack(this);
    undoStack_->setUndoLimit(30);
    connect(undoStack_, &QUndoStack::cleanChanged, this, [this](bool clean) { setWindowModified(!clean); });
    // 设置初始窗口标题
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

    colorGridInit();
    statusBarInit();
    toolbarInit();
    shortcutInit();
    // Restore only after every toolbar has been created. Qt can reapply a
    // maximized window's restored layout while the window is being shown.
    viewInit();

    // 将 Canvas 注册到 CanvasContainer 的 scene 中，启用滚动条和视图管理
    ui->centralWidget->setCanvas(ui->canvas);
    ui->centralWidget->resetView();
    connect(ui->canvas, &Canvas::documentChanged, this, [this]() {
        if (!resettingDocument_) {
            setWindowModified(true);
            if (!ui->canvas->isDrawing()) recordDocumentChange();
        }
    });
    newProject(1280, 720);
}

void MainWindow::colorGridInit()
{
    QSettings settings(GlobalDef::settingsPath(),
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
    QSettings settings(GlobalDef::settingsPath(), QSettings::IniFormat);
    defaultView = saveState();
    restoreGeometry(settings.value("mainwindow/geometry").toByteArray());
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
    std::sort(brushes.begin(), brushes.end(), [](const auto &a, const auto &b) { return a->name() < b->name(); });

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
}

void MainWindow::statusBarInit()
{
    ui->statusBar->setSizeGripEnabled(true);
}

void MainWindow::shortcutInit()
{
    auto *undo = undoStack_->createUndoAction(this, tr("Undo"));
    undo->setObjectName("actionUndo");
    undo->setShortcut(QKeySequence::Undo);
    auto *redo = undoStack_->createRedoAction(this, tr("Redo"));
    redo->setObjectName("actionRedo");
    redo->setShortcuts(QKeySequence::Redo);
    ui->menu_Edit->insertAction(ui->actionClear_All_Layers, undo);
    ui->menu_Edit->insertAction(ui->actionClear_All_Layers, redo);
    ui->actionNew->setShortcut(QKeySequence::New);
    ui->actionOpen->setShortcut(QKeySequence::Open);
    ui->actionSave->setShortcut(QKeySequence::Save);
    ui->actionSave_As->setShortcut(QKeySequence::SaveAs);
    ui->action_Quit->setShortcut(QKeySequence::Quit);
    recentProjectsMenu_ = new QMenu(tr("Open Recent"), this);
    ui->menu_File->insertMenu(ui->actionSave, recentProjectsMenu_);
    refreshRecentProjects();
    auto *import = new QAction(tr("Import Image as Layer…"), this);
    import->setObjectName("actionImportImage");
    ui->menu_File->insertAction(ui->action_Quit, import);
    connect(import, &QAction::triggered, this, &MainWindow::importImage);
    auto *legacy = new QAction(tr("Open Legacy Project Folder…"), this);
    ui->menu_File->insertAction(ui->action_Quit, legacy);
    connect(legacy, &QAction::triggered, this, [this]() {
        const QString path = QFileDialog::getExistingDirectory(this, tr("Open Legacy Project"), lastProjectDirectory());
        if (!path.isEmpty()) openProject(path);
    });
    auto *layerMenu = new QMenu(tr("&Layer"), this);
    ui->menuBar->insertMenu(ui->menu_View->menuAction(), layerMenu);
    auto *layerToolbar = new QToolBar(tr("Layers"), ui->layerWidget->parentWidget());
    layerToolbar->setObjectName("LayerToolbar");
    // This toolbar belongs to the layer panel, not QMainWindow's docking layout.
    layerToolbar->setMovable(false);
    layerToolbar->setFloatable(false);
    ui->layerWidget->parentWidget()->layout()->addWidget(layerToolbar);
    auto addLayerAction = [this, layerMenu, layerToolbar](const QString &text, const QString &name, auto callback) {
        auto *action = layerMenu->addAction(text);
        action->setObjectName(name);
        layerToolbar->addAction(action);
        connect(action, &QAction::triggered, this, callback);
        return action;
    };
    addLayerAction(tr("Add"), "actionAddLayer", [this]() { addLayer(); })->setShortcut(QKeySequence("Ctrl+Shift+N"));
    addLayerAction(tr("Delete"), "actionDeleteLayer", [this]() { deleteLayer(); });
    addLayerAction(tr("Up"), "actionRaiseLayer", [this]() {
        ui->canvas->moveLayerUp(ui->canvas->currentLayer()); rebuildLayerList();
    });
    addLayerAction(tr("Down"), "actionLowerLayer", [this]() {
        ui->canvas->moveLayerDown(ui->canvas->currentLayer()); rebuildLayerList();
    });
    layerMenu->addAction(tr("Clear Selected Layer"), this, [this]() { clearLayer(ui->canvas->currentLayer()); });
    connect(ui->layerWidget, &LayerWidget::renameRequested, this, [this](const QString &oldName, const QString &newName) {
        if (ui->canvas->renameLayer(oldName, newName)) rebuildLayerList();
        else QMessageBox::warning(this, tr("Rename Layer"), tr("Choose a unique layer name of 1–256 characters."));
    });
    ui->menu_View->addSeparator();
    for (auto *dock : findChildren<QDockWidget *>()) ui->menu_View->addAction(dock->toggleViewAction());
    connect(ui->action_Quit, &QAction::triggered,
            this, &MainWindow::close);
    connect(ui->actionNew, &QAction::triggered,
            this, &MainWindow::onNewProject);
    connect(ui->actionOpen, &QAction::triggered,
            this, &MainWindow::onOpenProject);
    connect(ui->actionSave, &QAction::triggered,
            this, &MainWindow::onSaveProject);
    connect(ui->actionSave_As, &QAction::triggered,
            this, &MainWindow::onSaveProjectAs);
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
    connect(ui->actionClear_All_Layers, &QAction::triggered,
            this, &MainWindow::clearAllLayer);
    connect(ui->actionConfiguration, &QAction::triggered,
            [this](){
        ConfigureDialog conf_dialog(this);
        if (conf_dialog.exec() == QDialog::Accepted) {
            QSettings settings(GlobalDef::settingsPath(), QSettings::IniFormat);
            ui->canvas->setTabletEnabled(settings.value("canvas/enable_tablet", true).toBool());
        }
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
    QColor c = m["color"].canConvert<QColor>() ? m["color"].value<QColor>()
        : QColor(colorMap["red"].toInt(), colorMap["green"].toInt(), colorMap["blue"].toInt());

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
    for (auto *action : brushActionGroup_->actions()) {
        if (action->objectName().compare(brushName, Qt::CaseInsensitive) == 0) action->setChecked(true);
    }
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

    onBrushSettingsChanged(ui->canvas->brushSettings());
}

void MainWindow::rebuildLayerList()
{
    ui->layerWidget->clear();
    const auto project = ui->canvas->projectState();
    for (const auto &layer : project.layers) {
        auto *item = new LayerItem;
        QIcon visibility(":/iconset/ui/visibility-on.png");
        visibility.addFile(":/iconset/ui/visibility-off.png", QSize(), QIcon::Selected, QIcon::On);
        item->setVisibleIcon(visibility);
        QIcon lock(":/iconset/ui/lock.png");
        lock.addFile(":/iconset/ui/unlock.png", QSize(), QIcon::Selected, QIcon::On);
        item->setLockIcon(lock);
        item->setLabel(layer.name);
        item->setHidden(!layer.visible);
        item->setLocked(layer.locked);
        ui->layerWidget->addItem(item);
        if (layer.name == project.layers.at(project.selectedLayer).name) item->setSelect(true);
    }
}

void MainWindow::addLayer(const QString &layerName)
{
    const auto project = ui->canvas->projectState();
    if (project.layers.size() >= ProjectFile::MaxLayers) {
        QMessageBox::warning(this, tr("Layers"), tr("The maximum number of layers is 256."));
        return;
    }
    QString name = layerName;
    int number = ui->canvas->layerNum() + 1;
    auto exists = [&project](const QString &candidate) {
        for (const auto &layer : project.layers) if (layer.name == candidate) return true;
        return false;
    };
    if (name.isEmpty()) {
        do { name = tr("Layer %1").arg(number++); } while (exists(name));
    } else if (exists(name)) return;
    ui->canvas->addLayer(name);
    ui->canvas->layerSelected(name);
    rebuildLayerList();
}

void MainWindow::deleteLayer()
{
    deleteLayer(ui->canvas->currentLayer());
}

void MainWindow::deleteLayer(const QString &name)
{
    if (ui->canvas->count() <= 1) {
        ui->statusBar->showMessage(tr("Keep at least one layer in the project."), 4000);
        return;
    }
    for (const auto &layer : ui->canvas->projectState().layers) {
        if (layer.name == name && layer.locked) {
            ui->statusBar->showMessage(tr("Unlock the layer before deleting it."), 4000);
            return;
        }
    }
    if (QMessageBox::question(this, tr("Delete Layer"), tr("Delete layer “%1”?").arg(name),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    if (ui->canvas->deleteLayer(name)) rebuildLayerList();
}

void MainWindow::clearLayer(const QString &name)
{
    if (QMessageBox::question(this, tr("Clear Layer"), tr("Clear layer “%1”?").arg(name),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
        ui->canvas->clearLayer(name);
}

void MainWindow::clearAllLayer()
{
    if (QMessageBox::question(this, tr("Clear Canvas"), tr("Clear all unlocked layers?"),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
        ui->canvas->clearAllLayer();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!promptSaveIfDirty()) { event->ignore(); return; }
    QSettings settings(GlobalDef::settingsPath(), QSettings::IniFormat);
    settings.setValue("colorgrid/pal", ui->colorGrid->dataExport());
    settings.setValue("mainwindow/view", saveState());
    settings.setValue("mainwindow/geometry", saveGeometry());
    settings.sync();
    event->accept();
}

void MainWindow::exportAllToFile()
{
    QString fileName =
            QFileDialog::getSaveFileName(this,
                                         tr("Export all to file"),
                                         defaultExportPath("png"),
                                         tr("Images (*.png)"));
    fileName = fileName.trimmed();
    if(fileName.isEmpty()){
        return;
    }
    if(!fileName.endsWith(".png", Qt::CaseInsensitive)){
        fileName = fileName + ".png";
    }
    QImage image = ui->canvas->allCanvas();
    QSaveFile file(fileName);
    QImageWriter writer(&file, "PNG");
    if (!file.open(QIODevice::WriteOnly) || !writer.write(image) || !file.commit())
        QMessageBox::critical(this, tr("Export Failed"), tr("Could not write the image:\n%1").arg(file.errorString()));
}

void MainWindow::exportVisibleToFile()
{
    QString fileName =
            QFileDialog::getSaveFileName(this,
                                         tr("Export visible part to file"),
                                         defaultExportPath("png"),
                                         tr("Images (*.png)"));
    fileName = fileName.trimmed();
    if(fileName.isEmpty()){
        return;
    }
    if(!fileName.endsWith(".png", Qt::CaseInsensitive)){
        fileName = fileName + ".png";
    }
    QImage image = ui->canvas->currentCanvas();
    QSaveFile file(fileName);
    QImageWriter writer(&file, "PNG");
    if (!file.open(QIODevice::WriteOnly) || !writer.write(image) || !file.commit())
        QMessageBox::critical(this, tr("Export Failed"), tr("Could not write the image:\n%1").arg(file.errorString()));
}

void MainWindow::exportToPSD()
{
    QString fileName =
            QFileDialog::getSaveFileName(this,
                                         tr("Export contents to psd file"),
                                         defaultExportPath("psd"),
                                         tr("Photoshop Images (*.psd)"));
    fileName = fileName.trimmed();
    if(fileName.isEmpty()){
        return;
    }
    if(!fileName.endsWith(".psd", Qt::CaseInsensitive)){
        fileName = fileName + ".psd";
    }

    // save all layers into psd

    const QByteArray data = imagesToPSD(ui->canvas->layerImages(), ui->canvas->allCanvas());
    QSaveFile file(fileName);
    if (data.isEmpty() || !file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        QMessageBox::critical(this, tr("Export Failed"), tr("Could not write the PSD file:\n%1").arg(file.errorString()));
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

void MainWindow::onNewProject()
{
    NewProjectDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) newProject(dialog.canvasWidth(), dialog.canvasHeight());
}

void MainWindow::newProject(int width, int height)
{
    if (!ProjectFile::validSize(QSize(width, height)) || !promptSaveIfDirty()) return;
    QScopedValueRollback<bool> resetting(resettingDocument_, true);
    PaintingProject project;
    project.size = QSize(width, height);
    project.layers.append({tr("Layer 1"), QImage(), true, false});
    ui->canvas->setProjectState(project);
    rebuildLayerList();
    undoStack_->clear();
    historyBytes_ = 0;
    lastProjectState_ = ui->canvas->projectState();
    currentProjectPath_.clear();
    setWindowModified(false);
    updateProjectTitle();
    ui->centralWidget->resetView();
    onPanoramaRefresh();
}

void MainWindow::onOpenProject()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Project"), lastProjectDirectory(),
                                                     tr("Mr.Paint Projects (*.paintty)"));
    if (!path.isEmpty()) openProject(path);
}

bool MainWindow::openProject(const QString &filePath)
{
    PaintingProject project;
    QString error;
    if (!ProjectFile::load(filePath, &project, &error)) {
        QMessageBox::critical(this, tr("Open Failed"), tr("Could not open the project:\n%1").arg(error));
        return false;
    }
    if (!promptSaveIfDirty()) return false;
    QScopedValueRollback<bool> resetting(resettingDocument_, true);
    ui->canvas->setProjectState(project);
    rebuildLayerList();
    undoStack_->clear();
    historyBytes_ = 0;
    lastProjectState_ = ui->canvas->projectState();
    currentProjectPath_ = QFileInfo(filePath).absoluteFilePath();
    setWindowModified(false);
    updateProjectTitle();
    rememberProject(currentProjectPath_);
    ui->centralWidget->resetView();
    onPanoramaRefresh();
    ui->statusBar->showMessage(tr("Project opened"), 3000);
    return true;
}

void MainWindow::onSaveProject() { saveProject(); }
void MainWindow::onSaveProjectAs() { saveProjectAs(); }

bool MainWindow::saveProject()
{
    if (currentProjectPath_.isEmpty() || QFileInfo(currentProjectPath_).isDir()) return saveProjectAs();
    return saveProjectTo(currentProjectPath_);
}

bool MainWindow::saveProjectAs()
{
    QString initialPath = currentProjectPath_;
    if (initialPath.isEmpty()) initialPath = QDir(lastProjectDirectory()).filePath(tr("Untitled.paintty"));
    if (QFileInfo(initialPath).isDir()) initialPath += ".paintty";
    QFileDialog dialog(this, tr("Save Project As"), initialPath, tr("Mr.Paint Projects (*.paintty)"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDefaultSuffix("paintty");
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return false;
    return saveProjectTo(dialog.selectedFiles().first());
}

bool MainWindow::saveProjectTo(const QString &path)
{
    QString error;
    if (!ProjectFile::save(path, ui->canvas->projectState(), &error)) {
        QMessageBox::critical(this, tr("Save Failed"), tr("Could not save the project:\n%1").arg(error));
        return false;
    }
    currentProjectPath_ = QFileInfo(path).absoluteFilePath();
    undoStack_->setClean();
    setWindowModified(false);
    updateProjectTitle();
    rememberProject(currentProjectPath_);
    ui->statusBar->showMessage(tr("Project saved"), 3000);
    return true;
}

bool MainWindow::promptSaveIfDirty()
{
    if (!isWindowModified()) return true;
    const auto choice = QMessageBox::warning(this, tr("Unsaved Changes"),
        tr("Save changes to %1 before continuing?").arg(currentProjectPath_.isEmpty() ? tr("Untitled")
                                                        : QFileInfo(currentProjectPath_).fileName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (choice == QMessageBox::Save) return saveProject();
    return choice == QMessageBox::Discard;
}

void MainWindow::updateProjectTitle()
{
    setWindowTitle(tr("%1[*] — Mr.Paint").arg(currentProjectPath_.isEmpty() ? tr("Untitled")
                                                                         : QFileInfo(currentProjectPath_).fileName()));
    setWindowFilePath(QFileInfo(currentProjectPath_).isFile() ? currentProjectPath_ : QString());
    const QSize size = ui->canvas->canvasSize();
    ui->statusBar->showMessage(tr("%1 × %2 px · %3 layers").arg(size.width()).arg(size.height()).arg(ui->canvas->count()));
}

QString MainWindow::lastProjectDirectory() const
{
    QSettings settings(GlobalDef::settingsPath(), QSettings::IniFormat);
    return settings.value("projects/lastDirectory", QDir::homePath()).toString();
}

QString MainWindow::defaultExportPath(const QString &extension) const
{
    const QString name = currentProjectPath_.isEmpty() ? tr("Untitled") : QFileInfo(currentProjectPath_).completeBaseName();
    return QDir(lastProjectDirectory()).filePath(name + "." + extension);
}

void MainWindow::rememberProject(const QString &path)
{
    QSettings settings(GlobalDef::settingsPath(), QSettings::IniFormat);
    auto recent = settings.value("projects/recent").toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > 10) recent.removeLast();
    settings.setValue("projects/recent", recent);
    settings.setValue("projects/lastDirectory", QFileInfo(path).absolutePath());
    refreshRecentProjects();
}

void MainWindow::refreshRecentProjects()
{
    recentProjectsMenu_->clear();
    QSettings settings(GlobalDef::settingsPath(), QSettings::IniFormat);
    for (const auto &path : settings.value("projects/recent").toStringList()) {
        if (!QFileInfo::exists(path)) continue;
        auto *action = recentProjectsMenu_->addAction(QFileInfo(path).fileName());
        action->setToolTip(path);
        connect(action, &QAction::triggered, this, [this, path]() { openProject(path); });
    }
    recentProjectsMenu_->setEnabled(!recentProjectsMenu_->isEmpty());
}

void MainWindow::importImage()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import Image as Layer"), lastProjectDirectory(),
                                                     tr("Images (*.png *.jpg *.jpeg *.bmp *.webp)"));
    if (path.isEmpty()) return;
    if (ui->canvas->count() >= ProjectFile::MaxLayers) return;
    QImageReader::setAllocationLimit(256);
    QImageReader reader(path);
    reader.setAutoTransform(true);
    if (!ProjectFile::validSize(reader.size())) {
        QMessageBox::warning(this, tr("Import Failed"), tr("The image dimensions are unsupported."));
        return;
    }
    const QImage image = reader.read();
    if (image.isNull()) { QMessageBox::warning(this, tr("Import Failed"), reader.errorString()); return; }
    addLayer();
    ui->canvas->setLayerContent(ui->canvas->count() - 1, image);
    onPanoramaRefresh();
}

void MainWindow::recordDocumentChange()
{
    const auto after = ui->canvas->projectState();
    bool equal = after.size == lastProjectState_.size && after.layers.size() == lastProjectState_.layers.size();
    if (equal) {
        for (int i = 0; i < after.layers.size(); ++i) {
            const auto &a = after.layers[i];
            const auto &b = lastProjectState_.layers[i];
            if (a.name != b.name || a.visible != b.visible || a.locked != b.locked || a.image != b.image) {
                equal = false;
                break;
            }
        }
    }
    if (equal) {
        lastProjectState_ = after;
        setWindowModified(!undoStack_->isClean());
        return;
    }
    qint64 bytes = 0;
    for (const auto &layer : lastProjectState_.layers) bytes += layer.image.sizeInBytes();
    for (const auto &layer : after.layers) bytes += layer.image.sizeInBytes();
    // Images share storage until edited. Bound retained history conservatively.
    constexpr qint64 budget = 256LL * 1024 * 1024;
    if (historyBytes_ + bytes > budget) {
        undoStack_->clear();
        undoStack_->resetClean();
        historyBytes_ = 0;
    }
    if (bytes <= budget) {
        undoStack_->push(new DocumentChange(lastProjectState_, after,
            [this](const PaintingProject &project) { restoreDocument(project); }));
        historyBytes_ += bytes;
    }
    lastProjectState_ = after;
    setWindowModified(!undoStack_->isClean());
}

void MainWindow::restoreDocument(const PaintingProject &project)
{
    QScopedValueRollback<bool> resetting(resettingDocument_, true);
    ui->canvas->setProjectState(project);
    rebuildLayerList();
    lastProjectState_ = project;
    onPanoramaRefresh();
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
