#ifndef EASYCOPYLINEEDIT_H
#define EASYCOPYLINEEDIT_H

#include <QLineEdit>
#include <QEnterEvent>

class EasyCopyLineEdit : public QLineEdit
{
    Q_OBJECT
public:
    explicit EasyCopyLineEdit(QWidget *parent = 0);
    
signals:
    
public slots:

protected:
    void enterEvent(QEnterEvent * event) Q_DECL_OVERRIDE;    
};

#endif // EASYCOPYLINEEDIT_H
