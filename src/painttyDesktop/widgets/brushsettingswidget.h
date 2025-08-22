#ifndef BRUSHSETTINGSWIDGET_H
#define BRUSHSETTINGSWIDGET_H

#include <QWidget>

class QBoxLayout;
class QLabel;
class QSlider;
class QSpinBox;
class QFrame;

class BrushSettingsWidget : public QWidget
{
    Q_OBJECT
    
public:
    explicit BrushSettingsWidget(QWidget *parent = 0);
    ~BrushSettingsWidget();
    int width();
    int hardness();
    int thickness();
    int water();
    int extend();
    int mixin();
    // 新增压感笔刷属性
    int smoothness();
    int pressureCurve();
    int tiltSensitivity();
    
public slots:
    void setWidth(int width);
    void setHardness(int hardness);
    void setThickness(int thickness);
    void setWater(int water);
    void setExtend(int extend);
    void setMixin(int mixin);
    // 新增压感笔刷设置方法
    void setSmoothness(int smoothness);
    void setPressureCurve(int pressureCurve);
    void setTiltSensitivity(int tiltSensitivity);
    
    void widthUp();
    void widthDown();
    void hardnessUp();
    void hardnessDown();
    void thicknessUp();
    void thicknessDown();
    void waterUp();
    void waterDown();
    void extendUp();
    void extendDown();
    void mixinUp();
    void mixinDown();
    // 新增压感笔刷操作方法
    void smoothnessUp();
    void smoothnessDown();
    void pressureCurveUp();
    void pressureCurveDown();
    void tiltSensitivityUp();
    void tiltSensitivityDown();
    
    void setHardnessEnabled(bool on);
    void setThicknessEnabled(bool on);
    void setWaterEnabled(bool on);
    void setExtendEnabled(bool on);
    void setMixinEnabled(bool on);
    // 新增压感笔刷启用/禁用方法
    void setSmoothnessEnabled(bool on);
    void setPressureCurveEnabled(bool on);
    void setTiltSensitivityEnabled(bool on);
    
    void setOrientation(Qt::Orientation ori);
    
signals:
    void widthChanged(int width);
    void hardnessChanged(int hardness);
    void thicknessChanged(int thickness);
    void waterChanged(int water);
    void extendChanged(int extend);
    void mixinChanged(int mixin);
    // 新增压感笔刷信号
    void smoothnessChanged(int smoothness);
    void pressureCurveChanged(int pressureCurve);
    void tiltSensitivityChanged(int tiltSensitivity);
    
private:
    QLabel *widthLabel;
    QSlider *widthSlider;
    QSpinBox *widthSpinBox;
    QFrame *separator;
    QLabel *hardnessLabel;
    QSlider *hardnessSlider;
    QSpinBox *hardnessSpinBox;
    QLabel *thicknessLabel;
    QSlider *thicknessSlider;
    QSpinBox *thicknessSpinBox;
    QLabel *waterLabel;
    QSlider *waterSlider;
    QSpinBox *waterSpinBox;
    QLabel *extendLabel;
    QSlider *extendSlider;
    QSpinBox *extendSpinBox;
    QLabel *mixinLabel;
    QSlider *mixinSlider;
    QSpinBox *mixinSpinBox;
    
    // 新增压感笔刷控件
    QLabel *smoothnessLabel;
    QSlider *smoothnessSlider;
    QSpinBox *smoothnessSpinBox;
    QLabel *pressureCurveLabel;
    QSlider *pressureCurveSlider;
    QSpinBox *pressureCurveSpinBox;
    QLabel *tiltSensitivityLabel;
    QSlider *tiltSensitivitySlider;
    QSpinBox *tiltSensitivitySpinBox;
    
    QBoxLayout *layout_;
};

#endif // BRUSHSETTINGSWIDGET_H
