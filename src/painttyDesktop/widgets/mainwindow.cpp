#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QProgressDialog>
#include <QActionGroup>
#include <QSettings>
#include <QShortcut>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QtConcurrent>
#include <QThread>
#include <QApplication>
#include <QTextStream>
#include <QPushButton>

#include "../common/common.h"
#include "../misc/archivefile.h"
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

    layerWidgetInit();
    colorGridInit();
    statusBarInit();
    toolbarInit();
    viewInit();
    shortcutInit();

    // 将 Canvas 注册到 CanvasContainer 的 scene 中，启用滚动条和视图管理
    ui->centralWidget->setCanvas(ui->canvas);
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
}

void MainWindow::statusBarInit()
{
}

void MainWindow::shortcutInit()
{
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
    }
}

void MainWindow::clearAllLayer()
{
    auto result = QMessageBox::question(this,
                                        tr("OMG"),
                                        tr("You're going to clear ALL LAYERS"
                                           ". All of work on the canvas"
                                           "will be deleted and CANNOT be undone.\n"
                                           "Do you really want to do so?"),
                                        QMessageBox::Yes|QMessageBox::No);
    if(result == QMessageBox::Yes){
        ui->canvas->clearAllLayer();
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

    // 在关闭前保存画布快照
    if (ui->canvas) {
        qDebug() << "[MainWindow] 关闭窗口前保存画布快照";
        ui->canvas->exportCanvasSnapshot();

        // 等待一小段时间确保快照保存完成
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        QThread::msleep(100);
    }

    QProgressDialog dialog(tr("Saving, please wait..."),
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

void MainWindow::onNewProject()
{
    if (!promptSaveIfDirty())
        return;

    NewProjectDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        newProject(dialog.canvasWidth(), dialog.canvasHeight());
    }
}

void MainWindow::newProject(int width, int height)
{
    currentProjectPath_.clear();
    ui->canvas->setCanvasSize(QSize(width, height));
    ui->canvas->clearAllLayer();
    setWindowTitle(tr("Mr.Paint - Untitled"));
    onPanoramaRefresh();
}

void MainWindow::onOpenProject()
{
    if (!promptSaveIfDirty())
        return;

    QString filePath = QFileDialog::getExistingDirectory(
        this,
        tr("Open Project"),
        QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );

    if (filePath.isEmpty())
        return;

    QFileInfo fi(filePath);
    QString metadataPath = filePath + "/metadata.json";
    if (fi.suffix() == "paintty" || QFile::exists(metadataPath)) {
        openProject(filePath);
    } else {
        QMessageBox::warning(this, tr("Invalid Project"),
                             tr("The selected directory is not a valid Mr.Paint project."));
    }
}

bool MainWindow::openProject(const QString &filePath)
{
    if (loadFromFile(filePath)) {
        currentProjectPath_ = filePath;
        setWindowTitle(tr("Mr.Paint - %1").arg(QFileInfo(filePath).fileName()));
        onPanoramaRefresh();
        return true;
    }
    return false;
}

void MainWindow::onSaveProject()
{
    if (currentProjectPath_.isEmpty()) {
        onSaveProjectAs();
    } else {
        saveToFile(currentProjectPath_);
    }
}

bool MainWindow::saveProject()
{
    if (currentProjectPath_.isEmpty())
        return false;
    return saveToFile(currentProjectPath_);
}

void MainWindow::onSaveProjectAs()
{
    QString filePath = QFileDialog::getSaveFileName(
        this,
        tr("Save Project As"),
        QString(),
        tr("Mr.Paint Projects (*.paintty)")
    );

    if (filePath.isEmpty())
        return;

    if (filePath.endsWith(".paintty", Qt::CaseInsensitive))
        filePath.chop(8);
    filePath += ".paintty";

    if (saveToFile(filePath)) {
        currentProjectPath_ = filePath;
        setWindowTitle(tr("Mr.Paint - %1").arg(QFileInfo(filePath).fileName()));
    }
}

bool MainWindow::saveProjectAs()
{
    if (currentProjectPath_.isEmpty())
        return false;
    onSaveProjectAs();
    return !currentProjectPath_.isEmpty();
}

bool MainWindow::saveToFile(const QString &filePath)
{
    QDir projectDir(filePath);

    if (projectDir.exists()) {
        if (!projectDir.removeRecursively()) {
            QMessageBox::critical(this, tr("Save Failed"),
                                  tr("Could not overwrite existing project directory."));
            return false;
        }
    }
    if (!projectDir.mkpath(".")) {
        QMessageBox::critical(this, tr("Save Failed"),
                              tr("Could not create project directory:\n%1").arg(filePath));
        return false;
    }

    QSize size = ui->canvas->canvasSize();
    QJsonObject metadata;
    metadata["version"] = 1;
    metadata["canvasWidth"] = size.width();
    metadata["canvasHeight"] = size.height();
    metadata["created"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QFile metadataFile(filePath + "/metadata.json");
    if (!metadataFile.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, tr("Save Failed"),
                              tr("Could not write project metadata."));
        return false;
    }
    metadataFile.write(QJsonDocument(metadata).toJson());
    metadataFile.close();

    QDir imagesDir(filePath + "/images");
    imagesDir.mkpath(".");

    QList<QImage> images = ui->canvas->layerImages();
    for (int i = 0; i < images.size(); ++i) {
        QString imagePath = QString("%1/images/layer_%2.png").arg(filePath).arg(i);
        if (!images[i].save(imagePath, "PNG")) {
            qWarning() << "[MainWindow] Failed to save layer image:" << imagePath;
        }
    }

    qDebug() << "[MainWindow] Project saved to:" << filePath
             << "canvas:" << size << "layers:" << images.size();
    return true;
}

bool MainWindow::loadFromFile(const QString &filePath)
{
    QFile metadataFile(filePath + "/metadata.json");
    if (!metadataFile.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Load Failed"),
                              tr("Could not read project metadata:\n%1").arg(filePath));
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(metadataFile.readAll(), &parseError);
    metadataFile.close();

    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::critical(this, tr("Load Failed"),
                              tr("Invalid project metadata: %1").arg(parseError.errorString()));
        return false;
    }

    QJsonObject metadata = doc.object();
    int width = metadata.value("canvasWidth").toInt(720);
    int height = metadata.value("canvasHeight").toInt(480);

    if (width < 1 || height < 1 || width > 10000 || height > 10000) {
        QMessageBox::critical(this, tr("Load Failed"),
                              tr("Invalid canvas dimensions in project file."));
        return false;
    }

    ui->canvas->setCanvasSize(QSize(width, height));
    ui->canvas->clearAllLayer();

    QString imagesPath = filePath + "/images";
    QDir imagesDir(imagesPath);

    if (imagesDir.exists()) {
        QStringList filters;
        filters << "layer_*.png";
        QStringList imageFiles = imagesDir.entryList(filters, QDir::Files, QDir::Name);

        int canvasCount = ui->canvas->count();

        for (int i = 0; i < imageFiles.size(); ++i) {
            QString imagePath = imagesDir.filePath(imageFiles[i]);
            QImage img(imagePath);
            if (img.isNull()) {
                qWarning() << "[MainWindow] Failed to load layer image:" << imagePath;
                continue;
            }

            if (i >= canvasCount) {
                addLayer();
            }

            ui->canvas->setLayerContent(i, img);
        }
    }

    qDebug() << "[MainWindow] Project loaded from:" << filePath
             << "canvas:" << width << "x" << height;
    return true;
}

bool MainWindow::promptSaveIfDirty()
{
    return true;
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
