#include "brushsettingswidget.h"
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QHBoxLayout>

BrushSettingsWidget::BrushSettingsWidget(QWidget *parent) :
    QWidget(parent),
    layout_(nullptr)
{
    widthLabel = new QLabel(tr("Width"), this);
    widthSlider = new QSlider(this);
    widthSpinBox = new QSpinBox(this);
    separator = new QFrame(this);
    hardnessLabel = new QLabel(tr("Hardness"), this);
    hardnessSlider = new QSlider(this);
    hardnessSpinBox = new QSpinBox(this);
    thicknessLabel = new QLabel(tr("Thickness"), this);
    thicknessSlider = new QSlider(this);
    thicknessSpinBox = new QSpinBox(this);
    waterLabel = new QLabel(tr("Water"), this);
    waterSlider = new QSlider(this);
    waterSpinBox = new QSpinBox(this);
    extendLabel = new QLabel(tr("Extend"), this);
    extendSlider = new QSlider(this);
    extendSpinBox = new QSpinBox(this);
    mixinLabel = new QLabel(tr("Mixin"), this);
    mixinSlider = new QSlider(this);
    mixinSpinBox = new QSpinBox(this);
    
    // 新增压感笔刷控件
    smoothnessLabel = new QLabel(tr("平滑度"), this);
    smoothnessSlider = new QSlider(this);
    smoothnessSpinBox = new QSpinBox(this);
    pressureCurveLabel = new QLabel(tr("压感曲线"), this);
    pressureCurveSlider = new QSlider(this);
    pressureCurveSpinBox = new QSpinBox(this);
    tiltSensitivityLabel = new QLabel(tr("倾角灵敏度"), this);
    tiltSensitivitySlider = new QSlider(this);
    tiltSensitivitySpinBox = new QSpinBox(this);

    widthLabel->setAlignment(Qt::AlignCenter);
    widthSlider->setSizePolicy(QSizePolicy::Preferred,
                               QSizePolicy::Fixed);
    widthSlider->setRange(1, 100);
    widthSpinBox->setRange(1, 100);

    hardnessLabel->setAlignment(Qt::AlignCenter);
    separator->setFrameShadow(QFrame::Sunken);
    hardnessSlider->setSizePolicy(QSizePolicy::Preferred,
                                  QSizePolicy::Fixed);
    hardnessSlider->setRange(0, 100);
    hardnessSpinBox->setRange(0, 100);

    thicknessLabel->setAlignment(Qt::AlignCenter);
    thicknessSlider->setSizePolicy(QSizePolicy::Preferred,
                                  QSizePolicy::Fixed);
    thicknessSlider->setRange(0, 100);
    thicknessSpinBox->setRange(0, 100);

    waterLabel->setAlignment(Qt::AlignCenter);
    waterSlider->setSizePolicy(QSizePolicy::Preferred,
                                   QSizePolicy::Fixed);
    waterSlider->setRange(0, 100);
    waterSpinBox->setRange(0, 100);

    extendLabel->setAlignment(Qt::AlignCenter);
    extendSlider->setSizePolicy(QSizePolicy::Preferred,
                               QSizePolicy::Fixed);
    extendSlider->setRange(0, 100);
    extendSpinBox->setRange(0, 100);

    mixinLabel->setAlignment(Qt::AlignCenter);
    mixinSlider->setSizePolicy(QSizePolicy::Preferred,
                                QSizePolicy::Fixed);
    mixinSlider->setRange(0, 100);
    mixinSpinBox->setRange(0, 100);
    
    // 设置压感笔刷控件
    smoothnessLabel->setAlignment(Qt::AlignCenter);
    smoothnessSlider->setSizePolicy(QSizePolicy::Preferred,
                                    QSizePolicy::Fixed);
    smoothnessSlider->setRange(0, 100);
    smoothnessSpinBox->setRange(0, 100);
    
    pressureCurveLabel->setAlignment(Qt::AlignCenter);
    pressureCurveSlider->setSizePolicy(QSizePolicy::Preferred,
                                       QSizePolicy::Fixed);
    pressureCurveSlider->setRange(10, 200);
    pressureCurveSpinBox->setRange(10, 200);
    
    tiltSensitivityLabel->setAlignment(Qt::AlignCenter);
    tiltSensitivitySlider->setSizePolicy(QSizePolicy::Preferred,
                                         QSizePolicy::Fixed);
    tiltSensitivitySlider->setRange(0, 100);
    tiltSensitivitySpinBox->setRange(0, 100);

    connect(widthSlider, &QSlider::valueChanged,
            this, &BrushSettingsWidget::widthChanged);
    connect(widthSlider, &QSlider::valueChanged,
            widthSpinBox, &QSpinBox::setValue);
    connect(widthSpinBox,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            widthSlider, &QSlider::setValue);

    connect(hardnessSlider, &QSlider::valueChanged,
            this, &BrushSettingsWidget::hardnessChanged);
    connect(hardnessSlider, &QSlider::valueChanged,
            hardnessSpinBox, &QSpinBox::setValue);
    connect(hardnessSpinBox,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            hardnessSlider, &QSlider::setValue);

    connect(thicknessSlider, &QSlider::valueChanged,
            this, &BrushSettingsWidget::thicknessChanged);
    connect(thicknessSlider, &QSlider::valueChanged,
            thicknessSpinBox, &QSpinBox::setValue);
    connect(thicknessSpinBox,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            thicknessSlider, &QSlider::setValue);

    connect(waterSlider, &QSlider::valueChanged,
            this, &BrushSettingsWidget::waterChanged);
    connect(waterSlider, &QSlider::valueChanged,
            waterSpinBox, &QSpinBox::setValue);
    connect(waterSpinBox,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            waterSlider, &QSlider::setValue);

    connect(extendSlider, &QSlider::valueChanged,
            this, &BrushSettingsWidget::extendChanged);
    connect(extendSlider, &QSlider::valueChanged,
            extendSpinBox, &QSpinBox::setValue);
    connect(extendSpinBox,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            extendSlider, &QSlider::setValue);

    connect(mixinSlider, &QSlider::valueChanged,
            this, &BrushSettingsWidget::mixinChanged);
    connect(mixinSlider, &QSlider::valueChanged,
            mixinSpinBox, &QSpinBox::setValue);
    connect(mixinSpinBox,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            mixinSlider, &QSlider::setValue);
    
    // 连接压感笔刷控件信号
    connect(smoothnessSlider, &QSlider::valueChanged,
            this, &BrushSettingsWidget::smoothnessChanged);
    connect(smoothnessSlider, &QSlider::valueChanged,
            smoothnessSpinBox, &QSpinBox::setValue);
    connect(smoothnessSpinBox,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            smoothnessSlider, &QSlider::setValue);
    
    connect(pressureCurveSlider, &QSlider::valueChanged,
            this, &BrushSettingsWidget::pressureCurveChanged);
    connect(pressureCurveSlider, &QSlider::valueChanged,
            pressureCurveSpinBox, &QSpinBox::setValue);
    connect(pressureCurveSpinBox,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            pressureCurveSlider, &QSlider::setValue);
    
    connect(tiltSensitivitySlider, &QSlider::valueChanged,
            this, &BrushSettingsWidget::tiltSensitivityChanged);
    connect(tiltSensitivitySlider, &QSlider::valueChanged,
            tiltSensitivitySpinBox, &QSpinBox::setValue);
    connect(tiltSensitivitySpinBox,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            tiltSensitivitySlider, &QSlider::setValue);

    setOrientation(Qt::Horizontal);
    setHardnessEnabled(false);
    setThicknessEnabled(false);
    setWaterEnabled(false);
    setExtendEnabled(false);
    setMixinEnabled(false);
    setSmoothnessEnabled(false);
    setPressureCurveEnabled(false);
    setTiltSensitivityEnabled(false);
}

BrushSettingsWidget::~BrushSettingsWidget()
{
}

int BrushSettingsWidget::width()
{
    return widthSlider->value();
}

void BrushSettingsWidget::setWidth(int width)
{
    if(width != widthSlider->value())
        widthSlider->setValue(width);
}

int BrushSettingsWidget::hardness()
{
    return hardnessSlider->value();
}

int BrushSettingsWidget::thickness()
{
    return thicknessSlider->value();
}

int BrushSettingsWidget::water()
{
    return waterSlider->value();
}

int BrushSettingsWidget::extend()
{
    return extendSlider->value();
}

int BrushSettingsWidget::mixin()
{
    return mixinSlider->value();
}

// 新增压感笔刷属性
int BrushSettingsWidget::smoothness()
{
    return smoothnessSlider->value();
}

int BrushSettingsWidget::pressureCurve()
{
    return pressureCurveSlider->value();
}

int BrushSettingsWidget::tiltSensitivity()
{
    return tiltSensitivitySlider->value();
}

void BrushSettingsWidget::setHardness(int hardness)
{
    if (hardness != hardnessSlider->value())
        hardnessSlider->setValue(hardness);
}

void BrushSettingsWidget::setThickness(int thickness)
{
    if (thickness != thicknessSlider->value())
        thicknessSlider->setValue(thickness);
}

void BrushSettingsWidget::setWater(int water)
{
    if (water != waterSlider->value())
        waterSlider->setValue(water);
}

void BrushSettingsWidget::setExtend(int extend)
{
    if (extend != extendSlider->value())
        extendSlider->setValue(extend);
}

void BrushSettingsWidget::setMixin(int mixin)
{
    if (mixin != mixinSlider->value())
        mixinSlider->setValue(mixin);
}

// 新增压感笔刷设置方法
void BrushSettingsWidget::setSmoothness(int smoothness)
{
    if (smoothness != smoothnessSlider->value())
        smoothnessSlider->setValue(smoothness);
}

void BrushSettingsWidget::setPressureCurve(int pressureCurve)
{
    if (pressureCurve != pressureCurveSlider->value())
        pressureCurveSlider->setValue(pressureCurve);
}

void BrushSettingsWidget::setTiltSensitivity(int tiltSensitivity)
{
    if (tiltSensitivity != tiltSensitivitySlider->value())
        tiltSensitivitySlider->setValue(tiltSensitivity);
}

void BrushSettingsWidget::widthUp()
{
    widthSlider->setValue(widthSlider->value()
                          + widthSlider->singleStep());
}

void BrushSettingsWidget::widthDown()
{
    widthSlider->setValue(widthSlider->value()
                          - widthSlider->singleStep());
}

void BrushSettingsWidget::hardnessUp()
{
    hardnessSlider->setValue(hardnessSlider->value()
                             + hardnessSlider->singleStep());
}

void BrushSettingsWidget::hardnessDown()
{
    hardnessSlider->setValue(hardnessSlider->value()
                             - hardnessSlider->singleStep());
}

void BrushSettingsWidget::thicknessUp()
{
    thicknessSlider->setValue(thicknessSlider->value()
                             + thicknessSlider->singleStep());
}

void BrushSettingsWidget::thicknessDown()
{
    thicknessSlider->setValue(thicknessSlider->value()
                             - thicknessSlider->singleStep());
}

void BrushSettingsWidget::waterUp()
{
    waterSlider->setValue(waterSlider->value()
                             + waterSlider->singleStep());
}

void BrushSettingsWidget::waterDown()
{
    waterSlider->setValue(waterSlider->value()
                             - waterSlider->singleStep());
}

void BrushSettingsWidget::extendUp()
{
    extendSlider->setValue(extendSlider->value()
                             + extendSlider->singleStep());
}

void BrushSettingsWidget::extendDown()
{
    extendSlider->setValue(extendSlider->value()
                             - extendSlider->singleStep());
}

void BrushSettingsWidget::mixinUp()
{
    mixinSlider->setValue(mixinSlider->value()
                             + mixinSlider->singleStep());
}

void BrushSettingsWidget::mixinDown()
{
    mixinSlider->setValue(mixinSlider->value()
                          - mixinSlider->singleStep());
}

// 新增压感笔刷操作方法
void BrushSettingsWidget::smoothnessUp()
{
    smoothnessSlider->setValue(smoothnessSlider->value()
                               + smoothnessSlider->singleStep());
}

void BrushSettingsWidget::smoothnessDown()
{
    smoothnessSlider->setValue(smoothnessSlider->value()
                               - smoothnessSlider->singleStep());
}

void BrushSettingsWidget::pressureCurveUp()
{
    pressureCurveSlider->setValue(pressureCurveSlider->value()
                                  + pressureCurveSlider->singleStep());
}

void BrushSettingsWidget::pressureCurveDown()
{
    pressureCurveSlider->setValue(pressureCurveSlider->value()
                                  - pressureCurveSlider->singleStep());
}

void BrushSettingsWidget::tiltSensitivityUp()
{
    tiltSensitivitySlider->setValue(tiltSensitivitySlider->value()
                                    + tiltSensitivitySlider->singleStep());
}

void BrushSettingsWidget::tiltSensitivityDown()
{
    tiltSensitivitySlider->setValue(tiltSensitivitySlider->value()
                                    - tiltSensitivitySlider->singleStep());
}

void BrushSettingsWidget::setHardnessEnabled(bool on)
{
    hardnessLabel->setVisible(on);
    hardnessSpinBox->setVisible(on);
    hardnessSlider->setVisible(on);
}

void BrushSettingsWidget::setThicknessEnabled(bool on)
{
    thicknessLabel->setVisible(on);
    thicknessSpinBox->setVisible(on);
    thicknessSlider->setVisible(on);
}

void BrushSettingsWidget::setWaterEnabled(bool on)
{
    waterLabel->setVisible(on);
    waterSpinBox->setVisible(on);
    waterSlider->setVisible(on);
}

void BrushSettingsWidget::setExtendEnabled(bool on)
{
    extendLabel->setVisible(on);
    extendSpinBox->setVisible(on);
    extendSlider->setVisible(on);
}

void BrushSettingsWidget::setMixinEnabled(bool on)
{
    mixinLabel->setVisible(on);
    mixinSpinBox->setVisible(on);
    mixinSlider->setVisible(on);
}

// 新增压感笔刷启用/禁用方法
void BrushSettingsWidget::setSmoothnessEnabled(bool on)
{
    smoothnessLabel->setVisible(on);
    smoothnessSpinBox->setVisible(on);
    smoothnessSlider->setVisible(on);
}

void BrushSettingsWidget::setPressureCurveEnabled(bool on)
{
    pressureCurveLabel->setVisible(on);
    pressureCurveSpinBox->setVisible(on);
    pressureCurveSlider->setVisible(on);
}

void BrushSettingsWidget::setTiltSensitivityEnabled(bool on)
{
    tiltSensitivityLabel->setVisible(on);
    tiltSensitivitySpinBox->setVisible(on);
    tiltSensitivitySlider->setVisible(on);
}

void BrushSettingsWidget::setOrientation(Qt::Orientation ori)
{
    if(layout_){
        delete layout_;
    }
    if(ori == Qt::Horizontal){
        layout_ = new QHBoxLayout(this);
        separator->setFrameShape(QFrame::HLine);
    }else{
        layout_ = new QVBoxLayout(this);
        separator->setFrameShape(QFrame::VLine);
    }
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setAlignment(Qt::AlignCenter);
    layout_->addWidget(widthLabel);
    layout_->addWidget(widthSlider);
    layout_->addWidget(widthSpinBox);
    layout_->addWidget(separator);
    layout_->addWidget(hardnessLabel);
    layout_->addWidget(hardnessSlider);
    layout_->addWidget(hardnessSpinBox);
    layout_->addWidget(thicknessLabel);
    layout_->addWidget(thicknessSlider);
    layout_->addWidget(thicknessSpinBox);
    layout_->addWidget(waterLabel);
    layout_->addWidget(waterSlider);
    layout_->addWidget(waterSpinBox);
    layout_->addWidget(extendLabel);
    layout_->addWidget(extendSlider);
    layout_->addWidget(extendSpinBox);
    layout_->addWidget(mixinLabel);
    layout_->addWidget(mixinSlider);
    layout_->addWidget(mixinSpinBox);
    
    // 添加压感笔刷控件到布局
    layout_->addWidget(smoothnessLabel);
    layout_->addWidget(smoothnessSlider);
    layout_->addWidget(smoothnessSpinBox);
    layout_->addWidget(pressureCurveLabel);
    layout_->addWidget(pressureCurveSlider);
    layout_->addWidget(pressureCurveSpinBox);
    layout_->addWidget(tiltSensitivityLabel);
    layout_->addWidget(tiltSensitivitySlider);
    layout_->addWidget(tiltSensitivitySpinBox);
    
    widthSlider->setOrientation(ori);
    hardnessSlider->setOrientation(ori);
    thicknessSlider->setOrientation(ori);
    waterSlider->setOrientation(ori);
    extendSlider->setOrientation(ori);
    mixinSlider->setOrientation(ori);
    smoothnessSlider->setOrientation(ori);
    pressureCurveSlider->setOrientation(ori);
    tiltSensitivitySlider->setOrientation(ori);
    setLayout(layout_);
}
