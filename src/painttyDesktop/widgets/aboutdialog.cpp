#include "aboutdialog.h"
#include "ui_aboutdialog.h"

#include <QCoreApplication>

AboutDialog::AboutDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AboutDialog)
{
    ui->setupUi(this);
    resize(width() * logicalDpiX() / 96, height() * logicalDpiY() / 96);
    ui->info_label->setText(ui->info_label->text().arg(QCoreApplication::applicationVersion()));
}

AboutDialog::~AboutDialog()
{
    delete ui;
}
