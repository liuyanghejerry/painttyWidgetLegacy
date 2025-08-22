#ifndef PANORAMASLIDER_H
#define PANORAMASLIDER_H

#include <QWidget>
#include <QRegularExpression>

class QSlider;
class QLineEdit;

class PanoramaSlider : public QWidget
{
    Q_OBJECT
public:
    explicit PanoramaSlider(QWidget *parent = 0);
signals:
    void scaled(qreal scaleFactor);
public slots:
    void setScale(qreal scaleFactor);
private:
    QSlider *slider;
    QLineEdit *input;
    qreal internalFactor;
    QRegularExpression inputReg;
private slots:
    void calculateScale(int sliderValue);
    void inputScaleConfirmed();
};

#endif // PANORAMASLIDER_H
