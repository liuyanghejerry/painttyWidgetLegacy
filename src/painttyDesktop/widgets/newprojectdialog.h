#ifndef NEWPROJECTDIALOG_H
#define NEWPROJECTDIALOG_H

#include <QDialog>

class QSpinBox;

class NewProjectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NewProjectDialog(QWidget *parent = nullptr);
    ~NewProjectDialog();

    int canvasWidth() const;
    int canvasHeight() const;

private:
    QSpinBox *widthSpin_;
    QSpinBox *heightSpin_;
};

#endif // NEWPROJECTDIALOG_H
