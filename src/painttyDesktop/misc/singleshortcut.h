#ifndef SINGLESHORTCUT_H
#define SINGLESHORTCUT_H

#include <QObject>
#include <QKeySequence>

class QShortcut;
class QWidget;

class SingleShortcut : public QObject
{
    Q_OBJECT
public:
    explicit SingleShortcut(QWidget *parent);
    void setKey(int k);
    void setKey(QKeySequence ks);
    QKeySequence key();
    bool eventFilter(QObject *obj, QEvent *event);
    
signals:
    void activated();
    void inactivated();
    
public slots:
    void setEnabled(bool e);
private:
    Q_DISABLE_COPY(SingleShortcut)
    QKeySequence key_;
    QShortcut *shortcut_;
    QWidget *window_;
    bool active_ = false;
    void release();
    
};

#endif // SINGLESHORTCUT_H
