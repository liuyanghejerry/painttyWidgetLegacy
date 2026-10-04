#include <QtTest>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QDrag>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QTranslator>
#include <QUndoStack>
#include <QGraphicsScene>
#include <QGraphicsProxyWidget>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

#include "common/common.h"
#include "misc/projectfile.h"
#include "widgets/canvas.h"
#include "widgets/canvascontainer.h"
#include "widgets/configuredialog.h"
#include "widgets/layerwidget.h"
#include "widgets/mainwindow.h"
#include "widgets/panoramaview.h"

namespace {
Canvas *canvasOf(MainWindow &window)
{
    // The view embeds the canvas in a scene, rather than QObject-parenting it to the window.
    const auto *scene = window.findChild<QGraphicsScene *>();
    if (!scene) return nullptr;
    for (auto *item : scene->items()) {
        if (auto *proxy = qgraphicsitem_cast<QGraphicsProxyWidget *>(item))
            if (auto *canvas = qobject_cast<Canvas *>(proxy->widget())) return canvas;
    }
    return nullptr;
}
QImage image(const QSize &size, const QColor &color)
{
    QImage result(size, QImage::Format_ARGB32_Premultiplied);
    result.fill(color);
    return result;
}

PaintingProject sample()
{
    PaintingProject project;
    project.size = QSize(32, 24);
    for (int i = 0; i < 12; ++i)
        project.layers.append({QString("绘画 %1").arg(i), i == 5 ? QImage() : image(project.size, QColor(i * 20, 100, 80)),
                               i != 7, i == 8});
    project.selectedLayer = 8;
    return project;
}

void answerMessage(QMessageBox::StandardButton button)
{
    QTimer::singleShot(0, [button]() {
        for (auto *widget : QApplication::topLevelWidgets()) {
            if (auto *box = qobject_cast<QMessageBox *>(widget)) {
                if (auto *answer = box->button(button)) answer->click();
            }
        }
    });
}
}

class LocalTests : public QObject
{
    Q_OBJECT
private slots:
    void panoramaCentersAcrossPixelRatios_data()
    {
        QTest::addColumn<qreal>("pixelRatio");
        QTest::newRow("standard") << qreal(1);
        QTest::newRow("fractional") << qreal(1.5);
        QTest::newRow("retina") << qreal(2);
    }

    void panoramaCentersAcrossPixelRatios()
    {
        QFETCH(qreal, pixelRatio);
        PanoramaView view;
        QPixmap preview(400, 200);
        preview.fill(Qt::red);
        preview.setDevicePixelRatio(pixelRatio);
        view.resize(200, 160);
        view.show();
        QTest::qWait(20);
        view.onImageChange(preview, QRect());
        QSignalSpy navigation(&view, &PanoramaView::moveTo);
        for (const auto &size : {QSize(200, 160), QSize(160, 240)}) {
            view.resize(size);
            QTest::qWait(20);
            const auto rendered = view.grab().toImage().scaled(size, Qt::IgnoreAspectRatio, Qt::FastTransformation);
            QRect colored;
            for (int y = 0; y < rendered.height(); ++y)
                for (int x = 0; x < rendered.width(); ++x)
                    if (rendered.pixelColor(x, y) == QColor(Qt::red))
                        colored = colored.united(QRect(x, y, 1, 1));
            const QSize expected(size.width(), size.width() / 2);
            QCOMPARE(colored.size(), expected);
            const QRect centered(QPoint(0, (size.height() - expected.height()) / 2), expected);
            QCOMPARE(colored, centered);
            navigation.clear();
            QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, QPoint(size.width() / 2, size.height() / 2));
            QCOMPARE(navigation.size(), 1);
            const auto point = navigation.first().first().toPointF();
            const auto logicalSize = preview.deviceIndependentSize();
            QVERIFY(qAbs(point.x() - logicalSize.width() / 2) < 0.01);
            QVERIFY(qAbs(point.y() - logicalSize.height() / 2) < 0.01);
        }
    }

    void toolbarsSurviveRestoredMaximizedLayout()
    {
        QSettings settings(GlobalDef::settingsPath(), QSettings::IniFormat);
        const auto oldGeometry = settings.value("mainwindow/geometry");
        const auto oldView = settings.value("mainwindow/view");
        const auto restoreSettings = qScopeGuard([&]() {
            for (const auto &entry : {qMakePair(QString("mainwindow/geometry"), oldGeometry),
                                      qMakePair(QString("mainwindow/view"), oldView)}) {
                if (entry.second.isValid()) settings.setValue(entry.first, entry.second);
                else settings.remove(entry.first);
            }
        });
        settings.remove("mainwindow/geometry");
        settings.remove("mainwindow/view");
        settings.sync();

        QTranslator translation;
        QVERIFY(translation.load(":/translation/paintty_zh_CN.qm"));
        QApplication::installTranslator(&translation);
        const auto removeTranslation = qScopeGuard([&]() { QApplication::removeTranslator(&translation); });

        // Older versions saved a FileToolbar and dock sizes for a large window.
        // Restore that layout into a smaller maximized window, then let Qt resize it.
        {
            MainWindow previous;
            if (!previous.findChild<QToolBar *>("FileToolbar")) {
                auto *legacy = previous.addToolBar("File");
                legacy->setObjectName("FileToolbar");
                legacy->addAction(previous.findChild<QAction *>("actionNew"));
            }
            previous.resize(2400, 1400);
            previous.show();
            QTest::qWait(50);
            settings.setValue("mainwindow/view", previous.saveState());
            previous.hide();
            previous.resize(1100, 570);
            previous.setWindowState(Qt::WindowMaximized);
            settings.setValue("mainwindow/geometry", previous.saveGeometry());
            settings.sync();
        }

        MainWindow window;
        window.showMaximized();
        QTest::qWait(200);
        const auto toolbars = window.findChildren<QToolBar *>(QString(), Qt::FindDirectChildrenOnly);
        QVERIFY(!toolbars.isEmpty());
        for (auto *toolbar : toolbars) {
            QVERIFY2(window.toolBarArea(toolbar) != Qt::NoToolBarArea, qPrintable(toolbar->objectName()));
        }
        for (auto *toolbar : toolbars) {
            const QPoint start(5, toolbar->height() / 2);
            const QPoint end(5, toolbar->height() + 120);
            QMouseEvent press(QEvent::MouseButtonPress, start, toolbar->mapToGlobal(start),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(toolbar, &press);
            // Wayland uses a nested platform drag loop. Cancel the synthetic drag
            // without requiring a real pointer grab from the desktop compositor.
            QTimer cancelDrag;
            cancelDrag.setSingleShot(true);
            connect(&cancelDrag, &QTimer::timeout, this, []() { QDrag::cancel(); });
            if (QApplication::platformName().startsWith("wayland")) cancelDrag.start(500);
            QMouseEvent move(QEvent::MouseMove, end, toolbar->mapToGlobal(end),
                             Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(toolbar, &move);
            cancelDrag.stop();
            QMouseEvent release(QEvent::MouseButtonRelease, end, toolbar->mapToGlobal(end),
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(toolbar, &release);
            window.resetView();
            QTest::qWait(50);
            QVERIFY2(window.toolBarArea(toolbar) != Qt::NoToolBarArea, qPrintable(toolbar->objectName()));
        }
        window.resetView();
        QTest::qWait(50);
        for (auto *toolbar : toolbars) {
            QVERIFY2(window.toolBarArea(toolbar) != Qt::NoToolBarArea, qPrintable(toolbar->objectName()));
        }
    }

    void roundTrip()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("layers.paintty");
        auto project = sample();
        QString error;
        QVERIFY2(ProjectFile::save(path, project, &error), qPrintable(error));
        QVERIFY(QFileInfo(path).isFile());
        PaintingProject loaded;
        QVERIFY2(ProjectFile::load(path, &loaded, &error), qPrintable(error));
        QCOMPARE(loaded.size, project.size);
        QCOMPARE(loaded.selectedLayer, project.selectedLayer);
        QCOMPARE(loaded.layers.size(), 12);
        for (int i = 0; i < 12; ++i) {
            QCOMPARE(loaded.layers[i].name, project.layers[i].name);
            QCOMPARE(loaded.layers[i].image, project.layers[i].image);
            QCOMPARE(loaded.layers[i].visible, project.layers[i].visible);
            QCOMPARE(loaded.layers[i].locked, project.layers[i].locked);
        }
    }

    void failedSavePreservesExistingProject()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("safe.paintty");
        auto project = sample();
        QString error;
        QVERIFY(ProjectFile::save(path, project, &error));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto before = file.readAll();
        file.close();
        project.layers[0].image = image(QSize(1, 1), Qt::red);
        QVERIFY(!ProjectFile::save(path, project, &error));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), before);
        file.close();
#ifdef Q_OS_UNIX
        // Windows directory ACLs and privileged Unix users do not follow these mode bits.
        if (geteuid() != 0) {
            const auto permissions = QFile::permissions(directory.path());
            QVERIFY(QFile::setPermissions(directory.path(), QFile::ReadOwner | QFile::ExeOwner));
            const bool saved = ProjectFile::save(path, sample(), &error);
            QVERIFY(QFile::setPermissions(directory.path(), permissions));
            QVERIFY(!saved);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), before);
            file.close();
        }
#endif
        QVERIFY(!ProjectFile::save(directory.path(), sample(), &error));
        QVERIFY(QFileInfo(directory.path()).isDir());
        QVERIFY(ProjectFile::save(path, sample(), &error));
        PaintingProject loaded;
        QVERIFY(ProjectFile::load(path, &loaded, &error));
    }

    void corruptLoadPreservesDocument()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("broken.paintty");
        QString error;
        QVERIFY(ProjectFile::save(path, sample(), &error));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.resize(file.size() - 10));
        file.close();
        PaintingProject loaded = sample();
        QVERIFY(!ProjectFile::load(path, &loaded, &error));
        QCOMPARE(loaded.layers.size(), 12);
        QCOMPARE(loaded.layers[0].image, sample().layers[0].image);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("PAINTTY\n\xff\xff\xff\xff");
        file.close();
        QVERIFY(!ProjectFile::load(path, &loaded, &error));
        QCOMPARE(loaded.layers.size(), 12);
    }

    void legacyNumericOrder()
    {
        QTemporaryDir directory;
        QVERIFY(QDir(directory.path()).mkdir("images"));
        QFile metadata(directory.filePath("metadata.json"));
        QVERIFY(metadata.open(QIODevice::WriteOnly));
        metadata.write(QJsonDocument(QJsonObject{{"version", 1}, {"canvasWidth", 32}, {"canvasHeight", 24}}).toJson());
        metadata.close();
        for (int i = 0; i < 12; ++i)
            QVERIFY(image(QSize(32, 24), QColor(i * 20, 0, 0)).save(directory.filePath(QString("images/layer_%1.png").arg(i))));
        PaintingProject project;
        QString error;
        QVERIFY2(ProjectFile::load(directory.path(), &project, &error), qPrintable(error));
        QCOMPARE(project.layers.size(), 12);
        QCOMPARE(project.layers[2].image.pixelColor(0, 0), QColor(40, 0, 0));
        QCOMPARE(project.layers[10].image.pixelColor(0, 0), QColor(200, 0, 0));
        QVERIFY(ProjectFile::save(directory.filePath("converted.paintty"), project, &error));
        QVERIFY(QFile::exists(directory.filePath("images/layer_0.png")));
    }

    void windowRoundTripAndHistory()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("window.paintty");
        MainWindow window;
        auto *canvas = canvasOf(window);
        auto *list = window.findChild<LayerWidget *>();
        QVERIFY(canvas);
        QVERIFY(list);
        QCOMPARE(canvas->count(), 1);
        QVERIFY(!window.isWindowModified());
        window.newProject(32, 24);
        canvas->setLayerContent(0, image(QSize(32, 24), Qt::red));
        window.addLayer("Ink");
        canvas->setLayerContent(1, image(QSize(32, 24), Qt::blue));
        canvas->hideLayer("Ink");
        canvas->lockLayer("Ink");
        QVERIFY(window.isWindowModified());
        QVERIFY(window.saveProjectTo(path));
        QVERIFY(!window.isWindowModified());
        window.addLayer("Extra");
        QCOMPARE(canvas->count(), 3);
        window.findChild<QAction *>("actionUndo")->trigger();
        QCOMPARE(canvas->count(), 2);
        QVERIFY(!window.isWindowModified());
        window.findChild<QAction *>("actionRedo")->trigger();
        QCOMPARE(canvas->count(), 3);
        QVERIFY(window.isWindowModified());
        answerMessage(QMessageBox::Discard);
        QVERIFY(window.openProject(path));
        QCOMPARE(canvas->count(), 2);
        QCOMPARE(list->itemCount(), 2);
        const auto project = canvas->projectState();
        QCOMPARE(project.layers[1].name, QString("Ink"));
        QCOMPARE(project.layers[1].visible, false);
        QCOMPARE(project.layers[1].locked, true);
        QCOMPARE(project.layers[0].image.pixelColor(0, 0), QColor(Qt::red));
        QCOMPARE(project.layers[1].image.pixelColor(0, 0), QColor(Qt::blue));
        QVERIFY(!window.isWindowModified());
        QSettings settings(GlobalDef::settingsPath(), QSettings::IniFormat);
        QVERIFY(settings.value("projects/recent").toStringList().contains(path));
        if (!qgetenv("PAINTTY_TEST_SCREENSHOT").isEmpty()) {
            window.resize(1280, 800);
            window.show();
            QTest::qWait(100);
            QVERIFY(window.grab().save(QString::fromUtf8(qgetenv("PAINTTY_TEST_SCREENSHOT"))));
        }
    }

    void unsavedCloseAndCancelledSaveAs()
    {
        MainWindow window;
        window.addLayer();
        QVERIFY(window.isWindowModified());
        answerMessage(QMessageBox::Cancel);
        QCloseEvent cancel;
        QApplication::sendEvent(&window, &cancel);
        QVERIFY(!cancel.isAccepted());
        // Save on an untitled document must open Save As, and Cancel must propagate.
        QTimer::singleShot(0, []() {
            for (auto *widget : QApplication::topLevelWidgets()) {
                if (auto *dialog = qobject_cast<QFileDialog *>(widget)) dialog->reject();
            }
        });
        QVERIFY(!window.saveProjectAs());
        QVERIFY(window.isWindowModified());
        answerMessage(QMessageBox::Discard);
        QCloseEvent discard;
        QApplication::sendEvent(&window, &discard);
        QVERIFY(discard.isAccepted());
    }

    void brushStrokesAndLockedLayers()
    {
        MainWindow window;
        window.newProject(128, 128);
        auto *canvas = canvasOf(window);
        QVERIFY(canvas);
        canvas->setTabletEnabled(false);
        canvas->setJitterCorrectionEnabled(false);
        canvas->setBrushColor(Qt::red);
        canvas->show();
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
        QVERIFY(window.isWindowModified());
        QVERIFY(!canvas->projectState().layers[0].image.isNull());
        window.findChild<QAction *>("actionUndo")->trigger();
        QVERIFY(canvas->projectState().layers[0].image.isNull());
        QVERIFY(!window.isWindowModified());
        window.findChild<QAction *>("actionRedo")->trigger();
        QVERIFY(!canvas->projectState().layers[0].image.isNull());
        canvas->lockLayer(canvas->currentLayer());
        const auto before = canvas->projectState().layers[0].image;
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(70, 70));
        QCOMPARE(canvas->projectState().layers[0].image, before);
        canvas->unlockLayer(canvas->currentLayer());
        window.changeToBrush("BasicBrushV3");
        canvas->setBrushColor(Qt::blue);
        canvas->setBrushWidth(15);
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(80, 80));
        QVERIFY(canvas->projectState().layers[0].image.pixelColor(80, 80).blue() > 0);
        // A sparse stream of pointer events must still create a continuous V3 stroke.
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(15, 100));
        QMouseEvent move(QEvent::MouseMove, QPointF(110, 100), QPointF(110, 100),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &move);
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(110, 100));
        QVERIFY(canvas->projectState().layers[0].image.pixelColor(60, 100).blue() > 0);
    }

    void layerOrderRenameAndDeletion()
    {
        Canvas canvas;
        auto project = sample();
        canvas.setProjectState(project);
        QVERIFY(canvas.renameLayer(project.layers[0].name, "Background"));
        QVERIFY(!canvas.renameLayer("Background", project.layers[1].name));
        canvas.moveLayerUp("Background");
        QCOMPARE(canvas.projectState().layers[1].name, QString("Background"));
        canvas.moveLayerDown("Background");
        QCOMPARE(canvas.projectState().layers[0].name, QString("Background"));
        canvas.layerSelected("Background");
        QVERIFY(canvas.deleteLayer("Background"));
        QCOMPARE(canvas.count(), 11);
        QVERIFY(!canvas.currentLayer().isEmpty());
        QVERIFY(!canvas.deleteLayer(project.layers[8].name));
        QCOMPARE(canvas.count(), 11);
    }

    void mouseThroughCanvasViewWithTabletEnabled()
    {
        MainWindow window;
        window.newProject(256, 256);
        window.resize(1100, 800);
        window.show();
        QTest::qWait(50);
        auto *canvas = canvasOf(window);
        auto *view = window.findChild<CanvasContainer *>();
        QVERIFY(canvas);
        QVERIFY(view);
        canvas->setTabletEnabled(true);
        canvas->setBrushColor(Qt::green);
        auto *proxy = canvas->graphicsProxyWidget();
        QVERIFY(proxy);
        const QPoint point = view->mapFromScene(proxy->mapToScene(QPointF(100, 100)));
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QVERIFY(window.isWindowModified());
        QVERIFY(canvas->projectState().layers[0].image.pixelColor(100, 100).green() > 0);
    }

    void offlinePreferences()
    {
        ConfigureDialog dialog;
        QVERIFY(!dialog.findChild<QWidget *>("serverTab"));
        QVERIFY(!dialog.findChild<QWidget *>("skip_replay"));
        QVERIFY(!dialog.findChild<QWidget *>("clearCache"));
        QVERIFY(dialog.findChild<QWidget *>("enable_tablet")->isEnabled());
    }
};

int main(int argc, char **argv)
{
    QTemporaryDir settings;
    qputenv("XDG_CONFIG_HOME", settings.path().toUtf8());
    QApplication app(argc, argv);
    app.setOrganizationName("PainttyTest");
    app.setApplicationName("LocalTests");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    LocalTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "local-tests.moc"
