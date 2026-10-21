#pragma once

#include <QScrollArea>

QT_BEGIN_NAMESPACE
class QGroupBox;
QT_END_NAMESPACE

// The standard controls: buttons and menus, text input (where the IME is
// exercised), ranges, item views, and buttons that open modal dialogs. Each
// one reports what happened through message(), which ends up in the status
// bar.
class ControlsPage : public QScrollArea
{
    Q_OBJECT

public:
    explicit ControlsPage(QWidget *parent = nullptr);

signals:
    void message(const QString &text);

private:
    QGroupBox *buttonsGroup();
    QGroupBox *textGroup();
    QGroupBox *rangesGroup();
    QGroupBox *viewsGroup();
};
