#include "welcomedialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QSettings>
#include <QFileInfo>
#include <QFont>

WelcomeDialog::WelcomeDialog(QWidget *parent)
    : QDialog(parent)
    , choice_(NewProject)
{
    setupUi();
    loadRecentFiles();
}

WelcomeDialog::~WelcomeDialog()
{
}

void WelcomeDialog::setupUi()
{
    setWindowTitle(tr("Mr.Paint"));
    setFixedSize(480, 360);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(24, 24, 24, 24);

    auto *titleLabel = new QLabel(tr("Welcome to Mr.Paint"));
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(18);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(titleLabel);

    auto *subtitleLabel = new QLabel(tr("A free digital painting tool"));
    subtitleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(subtitleLabel);

    mainLayout->addSpacing(8);

    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(16);

    newButton_ = new QPushButton(tr("&New Project"));
    newButton_->setMinimumHeight(48);
    newButton_->setMinimumWidth(160);
    QFont btnFont = newButton_->font();
    btnFont.setPointSize(14);
    newButton_->setFont(btnFont);
    connect(newButton_, &QPushButton::clicked, this, &WelcomeDialog::onNewProject);

    openButton_ = new QPushButton(tr("&Open Project..."));
    openButton_->setMinimumHeight(48);
    openButton_->setMinimumWidth(160);
    openButton_->setFont(btnFont);
    connect(openButton_, &QPushButton::clicked, this, &WelcomeDialog::onOpenProject);

    buttonLayout->addStretch();
    buttonLayout->addWidget(newButton_);
    buttonLayout->addWidget(openButton_);
    buttonLayout->addStretch();

    mainLayout->addLayout(buttonLayout);

    mainLayout->addSpacing(8);

    auto *recentLabel = new QLabel(tr("Recent projects:"));
    mainLayout->addWidget(recentLabel);

    recentList_ = new QListWidget();
    recentList_->setMinimumHeight(100);
    connect(recentList_, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem *item) {
        selectedRecentFile_ = item->data(Qt::UserRole).toString();
        choice_ = OpenProject;
        accept();
    });
    mainLayout->addWidget(recentList_);

    newButton_->setFocus();
}

void WelcomeDialog::loadRecentFiles()
{
    QSettings settings("mrpaint.ini", QSettings::IniFormat);
    int size = settings.beginReadArray("recentProjects");
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        QString path = settings.value("path").toString();
        if (QFileInfo::exists(path)) {
            auto *item = new QListWidgetItem(QFileInfo(path).fileName());
            item->setData(Qt::UserRole, path);
            item->setToolTip(path);
            recentList_->addItem(item);
        }
    }
    settings.endArray();
}

WelcomeDialog::Result WelcomeDialog::userChoice() const
{
    return choice_;
}

QString WelcomeDialog::selectedRecentFile() const
{
    return selectedRecentFile_;
}

void WelcomeDialog::onNewProject()
{
    choice_ = NewProject;
    accept();
}

void WelcomeDialog::onOpenProject()
{
    choice_ = OpenProject;
    accept();
}
