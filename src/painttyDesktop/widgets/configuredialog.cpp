#include <QSettings>
#include <QApplication>
#include <QDir>
#include <QRegularExpression>
#include <QLocale>
#include <QMessageBox>
#include <QProcess>
#include <QTreeWidgetItem>
#include <QMapIterator>
#include <QComboBox>
#include <QKeySequenceEdit>
#include <QHeaderView>
#include <QDateTime>
#include "configuredialog.h"
#include "ui_configuredialog.h"
#include "../common/common.h"
#include "../misc/shortcutmanager.h"
#include "../misc/singleton.h"
#include "../common/room-info-manager.h"

ConfigureDialog::ConfigureDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ConfigureDialog),
    msg_notify(false),
    auto_disable_ime(false),
    skip_replay(false),
    use_droid_font(false),
    use_default_server(true)
{
    ui->setupUi(this);
    resize(width() * logicalDpiX() / 96, height() * logicalDpiY() / 96);
    readSettings();
    initLanguageList();
    initShortcutList();
    initServerSettings();
    initUi();
    initMyRoomsTab();

    connect(this, &ConfigureDialog::accepted,
            this, &ConfigureDialog::acceptConfigure);
}

ConfigureDialog::~ConfigureDialog()
{
    delete ui;
}

void ConfigureDialog::readSettings()
{
    QSettings settings(GlobalDef::SETTINGS_NAME,
                       QSettings::defaultFormat());
    selectedLanguage = settings.value("global/language").toString();
    msg_notify = settings.value("chat/msg_notify", true).toBool();
    auto_disable_ime = settings.value("canvas/auto_disable_ime", true).toBool();
    // TODO: v3笔刷成熟后可以默认为true
    enable_tablet = settings.value("canvas/enable_tablet", false).toBool();
    use_default_server = settings.value("global/server/use_default", true).toBool();
    addr = settings.value("global/server/addr").toString();
    skip_replay = settings.value("canvas/skip_replay", true).toBool();
    use_droid_font = settings.value("global/use_droid_font", false).toBool();

}

void ConfigureDialog::initLanguageList()
{
    QDir qmDir(":/translation");
    QStringList qmList = qmDir.entryList(QStringList() << "paintty_*.qm",
                                         QDir::Files);
    ui->languageComboBox->addItem(tr("System Default"), QString());
    for (QString &qmFile: qmList)
    {
        qmFile.remove(QRegularExpression(".?paintty_", QRegularExpression::CaseInsensitiveOption));
        qmFile.remove(".qm", Qt::CaseInsensitive);
        QString languageName = QLocale(qmFile).nativeLanguageName();
        ui->languageComboBox->addItem(languageName, qmFile);
        if (selectedLanguage == qmFile)
            ui->languageComboBox->setCurrentIndex(ui->languageComboBox->count() - 1);
    }
}

void ConfigureDialog::initShortcutList()
{
    const QVariantMap& shortcutMap = Singleton<ShortcutManager>::instance().allShortcutMap();
    ShortcutDelegate *delegate = new ShortcutDelegate(ui->shortcutList);
    ui->shortcutList->setItemDelegate(delegate);
    QTreeWidgetItem *categoryItem = new QTreeWidgetItem(ui->shortcutList);
    categoryItem->setText(0, tr("Brushes"));
    ui->shortcutList->addTopLevelItem(categoryItem);
    QMapIterator<QString, QVariant> iterator(shortcutMap);
    while (iterator.hasNext())
    {
        iterator.next();
        QTreeWidgetItem *shortcutItem = new QTreeWidgetItem(categoryItem);
        QVariantMap singleEntry = iterator.value()  //we get QVariant for a single QVariantMap entry
                .toMap();                           //we get QVariantMap for a single entry
        QKeySequence sequence = singleEntry.value("key")    //we get QVariant for a QKeySequence
                .value<QKeySequence>();                     //we get QKeySequence
        ShortcutManager::ShortcutType type =
                ShortcutManager::ShortcutType(singleEntry.value("type").toInt());
        shortcutItem->setText(0, singleEntry.value("description").toString());
        shortcutItem->setText(1,sequence.toString(QKeySequence::NativeText));
        if (type == ShortcutManager::Single)
            shortcutItem->setText(2, tr("Immediately"));
        else if (type == ShortcutManager::Multiple)
            shortcutItem->setText(2, tr("When Release"));
        shortcutItem->setData(0, Qt::UserRole, iterator.key());
        shortcutItem->setData(1, Qt::UserRole, sequence);
        shortcutItem->setData(2, Qt::UserRole, type);
        shortcutItem->setFlags(shortcutItem->flags() | Qt::ItemIsEditable);
    }
    ui->shortcutList->expandAll();
    ui->shortcutList->resizeColumnToContents(2);
    ui->shortcutList->resizeColumnToContents(0);
}

void ConfigureDialog::initServerSettings()
{
    connect(ui->use_default_server_checkbox, &QCheckBox::stateChanged,
            [this](int n_state){
        if(n_state == Qt::Checked){
            ui->ipv4_lineedit->setDisabled(true);
            ui->server_notice_label->setVisible(false);
        }else{
            ui->ipv4_lineedit->setDisabled(false);
            ui->server_notice_label->setVisible(true);
        }
    });
    ui->use_default_server_checkbox->setChecked(use_default_server);

    ui->ipv4_lineedit->setText(addr);
}

void ConfigureDialog::initUi()
{
    ui->msg_notify_checkbox->setChecked(msg_notify);
    ui->auto_disable_ime_checkbox->setChecked(auto_disable_ime);
    ui->enable_tablet->setChecked(enable_tablet);
    ui->skip_replay->setChecked(skip_replay);
    ui->droid_font_checkbox->setChecked(use_droid_font);

    connect(ui->clearCache, &QPushButton::clicked,
            [](){
        QDir cacheDir("cache");
        cacheDir.removeRecursively();
    });

    // TODO: v3笔刷成熟后可以放开
    ui->enable_tablet->setDisabled(true);
}

void ConfigureDialog::initMyRoomsTab()
{
    // 初始化我的房间表格
    ui->myRoomsTable->setColumnCount(2);
    QStringList headers;
    headers << tr("房间名称") << tr("创建时间");
    ui->myRoomsTable->setHorizontalHeaderLabels(headers);
    ui->myRoomsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    // ui->myRoomsTable->setSortingEnabled(true);
    // ui->myRoomsTable->sortByColumn(1, Qt::DescendingOrder);
    
    // 连接信号槽
    connect(ui->refreshMyRoomsButton, &QPushButton::clicked,
            this, &ConfigureDialog::refreshMyRooms);
    
    // 初始状态
    ui->myRoomsStatusLabel->setText(tr("点击刷新按钮获取您的房间列表"));
    refreshMyRooms();
}

void ConfigureDialog::refreshMyRooms()
{
    // 使用RoomInfoManager获取本地房间列表
    RoomInfoManager& roomInfoManager = RoomInfoManager::instance();
    myRoomsInfo = roomInfoManager.listMyRooms();
    
    qDebug() << "refreshMyRooms: 找到" << myRoomsInfo.size() << "个本地房间";
    
    // 更新UI显示
    updateMyRoomsTable();
}

void ConfigureDialog::updateMyRoomsTable()
{
    ui->myRoomsTable->clearContents();
    ui->myRoomsTable->setRowCount(0);
    ui->myRoomsTable->setSortingEnabled(false);
    
    int row = 0;
    for (const auto& roomInfo : myRoomsInfo) {
        ui->myRoomsTable->insertRow(row);
        
        // 房间名称
        QTableWidgetItem* nameItem = new QTableWidgetItem(roomInfo->roomName);
        nameItem->setTextAlignment(Qt::AlignCenter);
        ui->myRoomsTable->setItem(row, 0, nameItem);
        
        // 创建时间（从本地缓存获取）
        QTableWidgetItem* timeItem = new QTableWidgetItem("N/A");
        timeItem->setTextAlignment(Qt::AlignCenter);
        ui->myRoomsTable->setItem(row, 1, timeItem);
        
        row++;
    }
    
    ui->myRoomsTable->setSortingEnabled(true);
    ui->refreshMyRoomsButton->setEnabled(true);
    ui->myRoomsStatusLabel->setText(tr("找到 %1 个房间").arg(row));
}



void ConfigureDialog::acceptConfigure()
{
    QSettings settings(GlobalDef::SETTINGS_NAME,
                       QSettings::defaultFormat(),
                       qApp);
    bool needRestart = false;

    //save language settings
    QString newLanguage = ui->languageComboBox->itemData(ui->languageComboBox->currentIndex())
            .toString();
    if (newLanguage != selectedLanguage)
    {
        settings.setValue("global/language", newLanguage);
        needRestart = true;
    }

    // droid font
    if(ui->droid_font_checkbox->isChecked() != use_droid_font)
    {
        settings.setValue("global/use_droid_font", ui->droid_font_checkbox->isChecked());
        needRestart = true;
    }

    //save shortcut settings
    QVariantMap shortcutMap = Singleton<ShortcutManager>::instance().allShortcutMap();
    for (int i = 0; i < ui->shortcutList->topLevelItemCount(); i++)
    {
        QTreeWidgetItem *categoryItem = ui->shortcutList->topLevelItem(i);
        if (!categoryItem)
            continue;
        for (int k = 0; k < categoryItem->childCount(); k++)
        {
            QTreeWidgetItem *shortcutItem = categoryItem->child(k);
            if (!shortcutItem)
                return;
            QVariantMap oldEntry =
                    shortcutMap.value(shortcutItem->data(0, Qt::UserRole).toString()).toMap();
            QKeySequence oldSequence = oldEntry.value("key").value<QKeySequence>();
            QKeySequence newSequence = shortcutItem->data(1, Qt::UserRole).value<QKeySequence>();
            ShortcutManager::ShortcutType oldType =
                    ShortcutManager::ShortcutType(oldEntry.value("type").toInt());
            ShortcutManager::ShortcutType newType =
                    ShortcutManager::ShortcutType(shortcutItem->data(2, Qt::UserRole).toInt());
            if (oldSequence != newSequence || oldType != newType) //we compare old sequence with new one
                //to see if we need restart and set new value.
            {
                needRestart = true;
                Singleton<ShortcutManager>::instance()
                        .setShortcut(shortcutItem->data(0, Qt::UserRole).toString(),
                                     newSequence,
                                     newType);
            }
        }
    }
    Singleton<ShortcutManager>::instance().saveToConfigure();

    //save msg notify settings
    if (ui->msg_notify_checkbox->isChecked() != msg_notify)
    {
        settings.setValue("chat/msg_notify", ui->msg_notify_checkbox->isChecked());
        needRestart = true;
    }

    //save auto disable IME settings
    if (ui->auto_disable_ime_checkbox->isChecked() != auto_disable_ime)
    {
        settings.setValue("canvas/auto_disable_ime",
                          ui->auto_disable_ime_checkbox->isChecked());
        needRestart = true;
    }

    //save tablet settings
    if (ui->enable_tablet->isChecked() != enable_tablet)
    {
        settings.setValue("canvas/enable_tablet",
                          ui->enable_tablet->isChecked());
        needRestart = true;
    }

    // save server settings
    {
        if (ui->use_default_server_checkbox->isChecked() != use_default_server)
        {
            settings.setValue("global/server/use_default",
                              ui->use_default_server_checkbox->isChecked());
            needRestart = true;
        }
        QString ip_t(ui->ipv4_lineedit->text().trimmed());
        if (ip_t != addr)
        {
            settings.setValue("global/server/addr",
                              ip_t);
            needRestart = true;
        }
    }

    // save canvas replay-skip settings
    if (ui->skip_replay->isChecked() != skip_replay)
    {
        settings.setValue("canvas/skip_replay",
                          ui->skip_replay->isChecked());
        needRestart = true;
    }


    settings.sync();

    //see if we need to restart
    if (!needRestart) {
        return;
    }

    int result = QMessageBox::warning(this, tr("Restart"),
                                      tr("Application must restart to "
                                         "enable some of the settings.\n"
                                         "Do you want to restart right now?"),
                                      QMessageBox::Yes | QMessageBox::No);
    if (result == QMessageBox::Yes) {
        qApp->closeAllWindows();
        qApp->exit(1);
        QProcess::startDetached(qApp->applicationFilePath(), QStringList());
    } else if (result == QMessageBox::No) {
        QMessageBox::warning(this , tr("Restart"),
                             tr("New settings will be applied on next start."));
    }
}

ShortcutDelegate::ShortcutDelegate(QObject *parent) :
    QItemDelegate(parent)
{
}

QWidget* ShortcutDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &/*option*/, const QModelIndex &index) const
{
    if (!index.isValid() || !index.parent().isValid() || !index.column())
        return 0;
    if (index.column() == 1)
        return new QKeySequenceEdit(parent);
    else if (index.column() == 2)
    {
        //        QComboBox *comboBox = new QComboBox(parent);
        //        comboBox->addItems(QStringList() << tr("Immediately")
        //                           << tr("When Release"));
        //        return comboBox;
        return 0;
    }
    else
        return 0;
}

void ShortcutDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    if (!index.isValid() || !index.parent().isValid() || !index.column())
        return;
    if (index.column() == 1)
    {
        QKeySequenceEdit *shortcutEditor = qobject_cast<QKeySequenceEdit*>(editor);
        shortcutEditor->setKeySequence(index.data(Qt::UserRole).value<QKeySequence>());
    }
    else if (index.column() == 2)
    {
        QComboBox *comboBox = qobject_cast<QComboBox*>(editor);
        if (index.data(Qt::UserRole).toInt() == ShortcutManager::Single)
            comboBox->setCurrentText(tr("Immediately"));
        else if (index.data(Qt::UserRole).toInt() == ShortcutManager::Multiple)
            comboBox->setCurrentText(tr("When Release"));
    }
}

void ShortcutDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const
{
    if (!index.isValid() || !index.parent().isValid() || !index.column())
        return;
    if (index.column() == 1)
    {
        QKeySequenceEdit *shortcutEditor = qobject_cast<QKeySequenceEdit*>(editor);
        model->setData(index, shortcutEditor->keySequence(), Qt::UserRole);
        model->setData(index, shortcutEditor->keySequence().toString(), Qt::DisplayRole);
    }
    else if (index.column() == 2)
    {
        QComboBox *comboBox = qobject_cast<QComboBox*>(editor);
        if (comboBox->currentText() == tr("Immediately"))
            model->setData(index, ShortcutManager::Single, Qt::UserRole);
        else if (comboBox->currentText() == tr("When Release"))
            model->setData(index, ShortcutManager::Multiple, Qt::UserRole);
        model->setData(index, comboBox->currentText(), Qt::DisplayRole);
    }
}

void ShortcutDelegate::updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    if (!index.isValid() || !index.parent().isValid() || !index.column())
        return;
    editor->setGeometry(option.rect);
}
