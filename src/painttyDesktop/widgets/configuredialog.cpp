#include <QSettings>
#include <QApplication>
#include <QDir>
#include <QRegularExpression>
#include <QLocale>
#include <QMessageBox>
#include <QTreeWidgetItem>
#include <QMapIterator>
#include <QComboBox>
#include <QKeySequenceEdit>
#include "configuredialog.h"
#include "ui_configuredialog.h"
#include "../common/common.h"
#include "../misc/shortcutmanager.h"
#include "../misc/singleton.h"

ConfigureDialog::ConfigureDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ConfigureDialog),
    auto_disable_ime(false),
    use_droid_font(false)
{
    ui->setupUi(this);
    resize(width() * logicalDpiX() / 96, height() * logicalDpiY() / 96);
    readSettings();
    initLanguageList();
    initShortcutList();
    initUi();

    connect(this, &ConfigureDialog::accepted,
            this, &ConfigureDialog::acceptConfigure);
}

ConfigureDialog::~ConfigureDialog()
{
    delete ui;
}

void ConfigureDialog::readSettings()
{
    QSettings settings(GlobalDef::settingsPath(),
                       QSettings::defaultFormat());
    selectedLanguage = settings.value("global/language").toString();
    auto_disable_ime = settings.value("canvas/auto_disable_ime", true).toBool();
    use_droid_font = settings.value("global/use_droid_font", false).toBool();

}

void ConfigureDialog::initLanguageList()
{
    QDir qmDir(":/translation");
    QStringList qmList = qmDir.entryList(QStringList() << "paintty_*.qm",
                                         QDir::Files);
    ui->languageComboBox->addItem(tr("System Default"), QString());
    ui->languageComboBox->addItem(QLocale(QLocale::English).nativeLanguageName(), QStringLiteral("en"));
    if (selectedLanguage == QStringLiteral("en"))
        ui->languageComboBox->setCurrentIndex(1);
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
        QVariantMap singleEntry = iterator.value()
                .toMap();
        QKeySequence sequence = singleEntry.value("key")
                .value<QKeySequence>();
        ShortcutManager::ShortcutType type =
                ShortcutManager::ShortcutType(singleEntry.value("type").toInt());
        shortcutItem->setText(0, singleEntry.value("description").toString());
        shortcutItem->setText(1,sequence.toString(QKeySequence::NativeText));
        const QString name = iterator.key();
        if (name == "colorpicker" || name == "movetool")
            shortcutItem->setText(2, tr("Hold to use"));
        else if (name == "basicbrush" || name == "basiceraser" || name == "binarybrush"
                 || name == "crayon" || name == "sketchbrush")
            shortcutItem->setText(2, tr("Select tool"));
        else
            shortcutItem->setText(2, tr("Repeat"));
        shortcutItem->setData(0, Qt::UserRole, iterator.key());
        shortcutItem->setData(1, Qt::UserRole, sequence);
        shortcutItem->setData(2, Qt::UserRole, type);
        shortcutItem->setFlags(shortcutItem->flags() | Qt::ItemIsEditable);
    }
    ui->shortcutList->expandAll();
    ui->shortcutList->resizeColumnToContents(2);
    ui->shortcutList->resizeColumnToContents(0);
}

void ConfigureDialog::initUi()
{
    ui->auto_disable_ime_checkbox->setChecked(auto_disable_ime);
    ui->droid_font_checkbox->setChecked(use_droid_font);

}

void ConfigureDialog::acceptConfigure()
{
    QSettings settings(GlobalDef::settingsPath(),
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
            if (oldSequence != newSequence || oldType != newType)
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

    //save auto disable IME settings
    if (ui->auto_disable_ime_checkbox->isChecked() != auto_disable_ime)
    {
        settings.setValue("canvas/auto_disable_ime",
                          ui->auto_disable_ime_checkbox->isChecked());
        needRestart = true;
    }

    settings.sync();

    //see if we need to restart
    if (!needRestart) {
        return;
    }

    QMessageBox::information(this, tr("Preferences"),
                             tr("Language, font and shortcut changes will apply the next time you start Mr.Paint."));
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

}

void ShortcutDelegate::updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    if (!index.isValid() || !index.parent().isValid() || !index.column())
        return;
    editor->setGeometry(option.rect);
}
