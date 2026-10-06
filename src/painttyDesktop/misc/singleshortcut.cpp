#include "singleshortcut.h"
#include <QApplication>
#include <QKeyEvent>
#include <QShortcut>
#include <QWidget>

SingleShortcut::SingleShortcut(QWidget *parent) :
    QObject(parent),
    shortcut_(new QShortcut(parent)),
    window_(parent->window())
{
    shortcut_->setAutoRepeat(false);
    qApp->installEventFilter(this);
    connect(shortcut_, &QShortcut::activated, this, [this]() {
        if (active_) return;
        active_ = true;
        emit activated();
    });
}

void SingleShortcut::setKey(int k)
{
    setKey(QKeySequence(k));
}

void SingleShortcut::setKey(QKeySequence ks)
{
    release();
    key_ = ks;
    shortcut_->setKey(key_);
}

QKeySequence SingleShortcut::key()
{
    return key_;
}

void SingleShortcut::setEnabled(bool enabled)
{
    if (!enabled) release();
    shortcut_->setEnabled(enabled);
}

void SingleShortcut::release()
{
    if (!active_) return;
    active_ = false;
    emit inactivated();
}

bool SingleShortcut::eventFilter(QObject *obj, QEvent *event)
{
    if (!active_) return false;
    if (event->type() == QEvent::KeyRelease) {
        const auto *keyEvent = static_cast<QKeyEvent *>(event);
        // Match the key that activated us, even if modifiers or focus changed.
        if (!keyEvent->isAutoRepeat() && !key_.isEmpty()
                && keyEvent->key() == key_[key_.count() - 1].key()) {
            release();
            return true;
        }
    } else if ((event->type() == QEvent::WindowDeactivate && obj == window_)
               || event->type() == QEvent::ApplicationDeactivate) {
        release();
    }
    return false;
}
