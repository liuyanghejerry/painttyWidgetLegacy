#include "easycopylineedit.h"

EasyCopyLineEdit::EasyCopyLineEdit(QWidget *parent) :
    QLineEdit(parent)
{
}

void EasyCopyLineEdit::enterEvent(QEnterEvent *)
{
    selectAll();

}
