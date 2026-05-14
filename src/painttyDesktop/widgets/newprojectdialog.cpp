#include "newprojectdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QSize>
#include <QSpinBox>

NewProjectDialog::NewProjectDialog(QWidget *parent)
    : QDialog(parent)
    , widthSpin_(new QSpinBox(this))
    , heightSpin_(new QSpinBox(this))
{
    setWindowTitle(tr("New Project"));

    widthSpin_->setRange(1, 10000);
    widthSpin_->setValue(720);
    heightSpin_->setRange(1, 10000);
    heightSpin_->setValue(480);

    auto *presetLabel = new QLabel(tr("Preset:"), this);
    auto *presetCombo = new QComboBox(this);
    presetCombo->addItem(tr("720 × 480"),     QSize(720, 480));
    presetCombo->addItem(tr("1024 × 768"),    QSize(1024, 768));
    presetCombo->addItem(tr("1920 × 1080"),   QSize(1920, 1080));
    presetCombo->addItem(tr("4096 × 4096"),   QSize(4096, 4096));
    presetCombo->setCurrentIndex(0);

    connect(presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, presetCombo](int index) {
        QSize size = presetCombo->itemData(index).toSize();
        widthSpin_->setValue(size.width());
        heightSpin_->setValue(size.height());
    });

    auto *widthLabel = new QLabel(tr("Width:"), this);
    auto *heightLabel = new QLabel(tr("Height:"), this);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QGridLayout(this);
    layout->addWidget(presetLabel, 0, 0);
    layout->addWidget(presetCombo,  0, 1, 1, 2);
    layout->addWidget(widthLabel,   1, 0);
    layout->addWidget(widthSpin_,   1, 1);
    layout->addWidget(heightLabel,  2, 0);
    layout->addWidget(heightSpin_,  2, 1);
    layout->addWidget(buttonBox,    3, 0, 1, 3);

    setFixedSize(sizeHint());
}

NewProjectDialog::~NewProjectDialog()
{
}

int NewProjectDialog::canvasWidth() const
{
    return widthSpin_->value();
}

int NewProjectDialog::canvasHeight() const
{
    return heightSpin_->value();
}
