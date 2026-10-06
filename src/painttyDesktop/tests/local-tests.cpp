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
#include <QLineEdit>
#include <QToolButton>
#include <QTabletEvent>
#include <QPointingDevice>
#include <QScrollBar>
#include "misc/singleshortcut.h"
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

QToolButton *toolButton(MainWindow &window, const QString &text)
{
    for (auto *button : window.findChildren<QToolButton *>())
        if (button->defaultAction() && button->defaultAction()->text() == text) return button;
    return nullptr;
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
        window.changeToBrush("BasicBrush");
        canvas->setBrushColor(Qt::blue);
        canvas->setBrushWidth(15);
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(80, 80));
        QVERIFY(canvas->projectState().layers[0].image.pixelColor(80, 80).blue() > 0);
        // A sparse stream of pointer events must still create a continuous mouse stroke.
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

    void mouseThroughCanvasView()
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
        canvas->setBrushColor(Qt::green);
        auto *proxy = canvas->graphicsProxyWidget();
        QVERIFY(proxy);
        const QPoint point = view->mapFromScene(proxy->mapToScene(QPointF(100, 100)));
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QVERIFY(window.isWindowModified());
        QVERIFY(canvas->projectState().layers[0].image.pixelColor(100, 100).green() > 0);
    }

    void mouseOnlyIgnoresCompatibilityEvents_data()
    {
        QTest::addColumn<int>("source");
        QTest::newRow("system") << int(Qt::MouseEventSynthesizedBySystem);
        QTest::newRow("qt") << int(Qt::MouseEventSynthesizedByQt);
        QTest::newRow("application") << int(Qt::MouseEventSynthesizedByApplication);
    }

    void mouseOnlyIgnoresCompatibilityEvents()
    {
        QFETCH(int, source);
        MainWindow window;
        window.newProject(128, 128);
        auto *canvas = canvasOf(window);
        auto send = [&](QEvent::Type type, QPoint point, Qt::MouseButton button, Qt::MouseButtons buttons) {
            QMouseEvent event(type, point, point, point, button, buttons, Qt::NoModifier,
                              Qt::MouseEventSource(source));
            QApplication::sendEvent(canvas, &event);
        };
        send(QEvent::MouseButtonPress, {20, 20}, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseMove, {80, 20}, Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, {80, 20}, Qt::LeftButton, Qt::NoButton);
        QVERIFY(canvas->projectState().layers[0].image.isNull());
        QVERIFY(!window.isWindowModified());
        window.show(); QTest::qWait(20);
        auto *view = window.findChild<CanvasContainer *>();
        const auto *proxy = canvas->graphicsProxyWidget();
        const QPoint point = view->mapFromScene(proxy->mapToScene(QPointF(50, 50)));
        for (auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
            QMouseEvent event(type, point, point, view->viewport()->mapToGlobal(point),
                              Qt::LeftButton, type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,
                              Qt::NoModifier, Qt::MouseEventSource(source));
            QApplication::sendEvent(view->viewport(), &event);
        }
        QVERIFY(canvas->projectState().layers[0].image.isNull());
        canvas->setBrushColor(Qt::red);
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(30, 30));
        QVERIFY(canvas->projectState().layers[0].image.pixelColor(30, 30).red() > 0);
        // Compatibility events must not terminate a real mouse stroke either.
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(40, 30));
        send(QEvent::MouseButtonRelease, {80, 20}, Qt::LeftButton, Qt::NoButton);
        QVERIFY(canvas->isDrawing());
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(70, 30));
        QVERIFY(!canvas->isDrawing());
    }

    void penEventsDoNotDraw()
    {
        MainWindow window;
        window.newProject(128, 128); window.show(); QTest::qWait(20);
        auto *view = window.findChild<CanvasContainer *>();
        QPointingDevice pen("Test Pen", 1, QInputDevice::DeviceType::Stylus,
            QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3);
        QTabletEvent press(QEvent::TabletPress, &pen, {50, 50}, {50, 50},
                           0.8, 0, 0, 0, 0, 0, Qt::NoModifier, Qt::LeftButton, Qt::LeftButton);
        QApplication::sendEvent(view->viewport(), &press);
        QTabletEvent release(QEvent::TabletRelease, &pen, {50, 50}, {50, 50},
                             0, 0, 0, 0, 0, 0, Qt::NoModifier, Qt::LeftButton, Qt::NoButton);
        QApplication::sendEvent(view->viewport(), &release);
        QVERIFY(canvasOf(window)->projectState().layers[0].image.isNull());
        QVERIFY(!window.isWindowModified());
    }

    void brushesSelectPersistentlyAndRememberWidth()
    {
        MainWindow window;
        window.show(); window.activateWindow(); QTest::qWait(30);
        auto *view = window.findChild<CanvasContainer *>();
        auto *canvas = canvasOf(window);
        view->setFocus();
        canvas->setBrushWidth(37);
        QTest::keyClick(view, Qt::Key_N);
        QCOMPARE(canvas->brushSettings()["name"].toString(), QString("basiceraser"));
        QTest::keyPress(view, Qt::Key_N);
        QTest::keyPress(view, Qt::Key_M);
        QTest::keyRelease(view, Qt::Key_M);
        QTest::keyRelease(view, Qt::Key_N);
        QCOMPARE(canvas->brushSettings()["name"].toString(), QString("binarybrush"));
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(80, 80));
        QKeyEvent repeated(QEvent::KeyPress, Qt::Key_M, Qt::NoModifier, QString(), true);
        QApplication::sendEvent(view, &repeated);
        QVERIFY(canvas->isDrawing());
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(90, 80));
        QTest::keyClick(view, Qt::Key_B);
        QCOMPARE(canvas->brushSettings()["name"].toString(), QString("basicbrush"));
        QCOMPARE(canvas->brushSettings()["width"].toInt(), 37);
        QTest::keyClick(view, Qt::Key_E);
        QCOMPARE(canvas->brushSettings()["name"].toString(), QString("basiceraser"));
        for (auto *action : window.findChildren<QAction *>())
            QVERIFY(!action->objectName().contains("V3"));
    }

    void temporaryToolsRestoreModifiersFocusAndOverlaps()
    {
        MainWindow window;
        window.show(); window.activateWindow(); QTest::qWait(30);
        auto *view = window.findChild<CanvasContainer *>();
        view->setFocus();
        auto *picker = toolButton(window, "Color Picker");
        auto *move = toolButton(window, "Move Tool");
        QVERIFY(picker); QVERIFY(move);
        QTest::keyPress(view, Qt::Key_V);
        QVERIFY(picker->isChecked());
        QTest::keyRelease(view, Qt::Key_V, Qt::ShiftModifier);
        QVERIFY(!picker->isChecked());
        QVERIFY(window.findChild<QAction *>("BasicBrush")->isEnabled());
        QTest::keyPress(view, Qt::Key_V);
        QEvent deactivated(QEvent::WindowDeactivate);
        QApplication::sendEvent(&window, &deactivated);
        QVERIFY(!picker->isChecked());
        QTest::keyRelease(view, Qt::Key_V);
        window.activateWindow(); view->setFocus(); QTest::qWait(10);
        // A toolbar selection remains the underlying tool during temporary holds.
        picker->click(); QVERIFY(picker->isChecked());
        QTest::keyPress(view, Qt::Key_Space);
        QVERIFY(move->isChecked()); QVERIFY(!picker->isChecked());
        QTest::keyPress(view, Qt::Key_V);
        QVERIFY(picker->isChecked()); QVERIFY(!move->isChecked());
        QTest::keyRelease(view, Qt::Key_Space);
        QVERIFY(picker->isChecked());
        QTest::keyRelease(view, Qt::Key_V);
        QVERIFY(picker->isChecked());
        // C and Space refer to the same tool but must keep distinct hold identities.
        QTest::keyPress(view, Qt::Key_C);
        QTest::keyPress(view, Qt::Key_V);
        QTest::keyPress(view, Qt::Key_Space);
        QVERIFY(move->isChecked());
        QTest::keyRelease(view, Qt::Key_Space);
        QVERIFY(picker->isChecked());
        QTest::keyRelease(view, Qt::Key_V);
        QVERIFY(move->isChecked());
        QTest::keyRelease(view, Qt::Key_C);
        QVERIFY(picker->isChecked());
        QTest::keyClick(view, Qt::Key_B);
        QVERIFY(!picker->isChecked()); QVERIFY(!move->isChecked());
        // Shortcut matching must let text fields handle ordinary typing.
        auto *input = new QLineEdit(&window);
        input->show(); input->setFocus();
        QTest::keyClicks(input, "bnvcs");
        QCOMPARE(input->text(), QString("bnvcs"));
        QVERIFY(!picker->isChecked()); QVERIFY(!move->isChecked());
        QCOMPARE(canvasOf(window)->brushSettings()["name"].toString(), QString("basicbrush"));
    }

    void destroyingWindowWithHeldTool()
    {
        {
            MainWindow window;
            window.show(); window.activateWindow(); QTest::qWait(20);
            auto *view = window.findChild<CanvasContainer *>(); view->setFocus();
            QTest::keyPress(view, Qt::Key_V);
            QVERIFY(toolButton(window, "Color Picker")->isChecked());
            // Window teardown can emit deactivation and undo-stack signals.
        }
        QCoreApplication::processEvents();
    }

    void modifiedTemporaryShortcutAndDisabledState()
    {
        QWidget window;
        window.setFocusPolicy(Qt::StrongFocus);
        SingleShortcut shortcut(&window);
        shortcut.setKey(QKeySequence("Ctrl+E"));
        QSignalSpy activated(&shortcut, &SingleShortcut::activated);
        QSignalSpy released(&shortcut, &SingleShortcut::inactivated);
        window.show(); window.activateWindow(); window.setFocus(); QTest::qWait(30);
        QTest::keyPress(&window, Qt::Key_E, Qt::ControlModifier);
        QCOMPARE(activated.count(), 1);
        QTest::keyRelease(&window, Qt::Key_E, Qt::ShiftModifier);
        QCOMPARE(released.count(), 1);
        shortcut.setEnabled(false);
        QTest::keyClick(&window, Qt::Key_E, Qt::ControlModifier);
        QCOMPARE(activated.count(), 1);
        shortcut.setEnabled(true);
        QTest::keyPress(&window, Qt::Key_E, Qt::ControlModifier);
        QCOMPARE(activated.count(), 2);
        QEvent deactivated(QEvent::WindowDeactivate);
        QApplication::sendEvent(&window, &deactivated);
        QCOMPARE(released.count(), 2);
        QTest::keyRelease(&window, Qt::Key_E);
        QCOMPARE(released.count(), 2);
    }

    void toolChangesCommitStrokeHistory_data()
    {
        QTest::addColumn<int>("key");
        QTest::newRow("picker") << int(Qt::Key_V);
        QTest::newRow("move") << int(Qt::Key_C);
        QTest::newRow("brush") << int(Qt::Key_N);
    }

    void toolChangesCommitStrokeHistory()
    {
        QFETCH(int, key);
        MainWindow window;
        window.newProject(128, 128);
        window.show(); window.activateWindow(); QTest::qWait(30);
        auto *canvas = canvasOf(window);
        auto *view = window.findChild<CanvasContainer *>(); view->setFocus();
        canvas->setBrushColor(Qt::red);
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(20, 20));
        QMouseEvent move(QEvent::MouseMove, QPointF(40, 20), QPointF(40, 20),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &move);
        QVERIFY(canvas->isDrawing());
        QTest::keyPress(view, Qt::Key(key));
        QVERIFY(!canvas->isDrawing());
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(80, 20));
        QTest::keyRelease(view, Qt::Key(key));
        const auto stroke = canvas->projectState().layers[0].image;
        QVERIFY(!stroke.isNull());
        auto *undo = window.findChild<QAction *>("actionUndo");
        QVERIFY(undo->isEnabled()); undo->trigger();
        QVERIFY(canvas->projectState().layers[0].image.isNull());
        window.findChild<QAction *>("actionRedo")->trigger();
        QCOMPARE(canvas->projectState().layers[0].image, stroke);
    }

    void undoDuringStrokeAndFocusLoss()
    {
        MainWindow window;
        window.newProject(128, 128);
        auto *canvas = canvasOf(window);
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(20, 20));
        QVERIFY(window.findChild<QAction *>("actionUndo")->isEnabled());
        window.findChild<QAction *>("actionUndo")->trigger();
        QVERIFY(!canvas->isDrawing());
        QVERIFY(canvas->projectState().layers[0].image.isNull());
        window.findChild<QAction *>("actionRedo")->trigger();
        QVERIFY(!canvas->projectState().layers[0].image.isNull());
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(40, 40));
        QEvent deactivated(QEvent::WindowDeactivate);
        QApplication::sendEvent(&window, &deactivated);
        QVERIFY(!canvas->isDrawing());
        window.findChild<QAction *>("actionUndo")->trigger();
        QCOMPARE(canvas->projectState().layers[0].image.pixelColor(40, 40).alpha(), 0);
    }

    void mouseSamplesRenderImmediatelyAndKeepCorners()
    {
        MainWindow window;
        window.newProject(160, 160);
        auto *canvas = canvasOf(window);
        canvas->setBrushColor(Qt::blue);
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(20, 20));
        for (const QPoint point : {QPoint(40, 20), QPoint(60, 20), QPoint(80, 20),
                                   QPoint(100, 20), QPoint(100, 100), QPoint(120, 20)}) {
            QMouseEvent move(QEvent::MouseMove, QPointF(point), QPointF(point),
                             Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(canvas, &move);
            QVERIFY(canvas->projectState().layers[0].image.pixelColor(point).blue() > 0);
        }
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(120, 20));
        QVERIFY(canvas->projectState().layers[0].image.pixelColor(100, 100).blue() > 0);
    }

    void panTracksViewportAcrossTransforms_data()
    {
        QTest::addColumn<int>("rotation"); QTest::addColumn<qreal>("scale");
        for (int rotation : {0, 45, 90, -90})
            for (qreal scale : {0.5, 1.0, 4.0})
                QTest::newRow(qPrintable(QString("%1deg-%2x").arg(rotation).arg(scale))) << rotation << scale;
    }

    void panTracksViewportAcrossTransforms()
    {
        QFETCH(int, rotation); QFETCH(qreal, scale);
        MainWindow window;
        window.newProject(3000, 3000); window.resize(1100, 800);
        window.show(); window.activateWindow(); QTest::qWait(20);
        auto *view = window.findChild<CanvasContainer *>();
        view->setScaleFactor(scale); view->setRotation(rotation); view->setFocus();
        auto *horizontal = view->horizontalScrollBar(); auto *vertical = view->verticalScrollBar();
        horizontal->setValue((horizontal->minimum() + horizontal->maximum()) / 2);
        vertical->setValue((vertical->minimum() + vertical->maximum()) / 2);
        const QPoint scroll(horizontal->value(), vertical->value());
        const QPoint start = view->viewport()->rect().center();
        QTest::keyPress(view, Qt::Key_C);
        QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, start);
        const auto releaseInput = qScopeGuard([&]() {
            QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, start);
            QTest::keyRelease(view, Qt::Key_C);
        });
        for (QPoint delta : {QPoint(1, 0), QPoint(40, 7)}) {
            QMouseEvent move(QEvent::MouseMove, QPointF(start + delta),
                             QPointF(view->viewport()->mapToGlobal(start + delta)),
                             Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(view->viewport(), &move);
            QCOMPARE(QPoint(horizontal->value(), vertical->value()), scroll - delta);
        }
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, start + QPoint(40, 7));
        QTest::keyRelease(view, Qt::Key_C);
        QVERIFY(canvasOf(window)->projectState().layers[0].image.isNull());
    }

    void zoomRoundTripAndReset()
    {
        MainWindow window;
        window.newProject(2000, 2000); window.show(); window.activateWindow(); QTest::qWait(20);
        auto *view = window.findChild<CanvasContainer *>(); view->setFocus();
        QTest::keyClick(view, Qt::Key_Equal); QTest::keyClick(view, Qt::Key_Minus);
        QVERIFY(qAbs(view->currentScaleFactor() - 1) < 0.00001);
        const auto point = view->viewport()->rect().center();
        for (int delta : {120, -120, 0}) {
            QWheelEvent wheel(QPointF(point), QPointF(view->viewport()->mapToGlobal(point)), {}, {0, delta},
                              Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(view->viewport(), &wheel);
        }
        QVERIFY(qAbs(view->currentScaleFactor() - 1) < 0.00001);
        view->setScaleFactor(2); view->setRotation(90); view->moveBy({100, 100});
        QTest::keyClick(view, Qt::Key_Backslash);
        QCOMPARE(view->currentScaleFactor(), qreal(1));
        const auto rect = view->visualRect();
        QVERIFY(qAbs(rect.center().x() - 1000) < 2);
        QVERIFY(qAbs(rect.center().y() - 1000) < 2);
    }

    void offlinePreferences()
    {
        ConfigureDialog dialog;
        QVERIFY(!dialog.findChild<QWidget *>("serverTab"));
        QVERIFY(!dialog.findChild<QWidget *>("skip_replay"));
        QVERIFY(!dialog.findChild<QWidget *>("clearCache"));
        QVERIFY(!dialog.findChild<QWidget *>("enable_tablet"));
        QVERIFY(!dialog.findChild<QWidget *>("experimentalTab"));
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
