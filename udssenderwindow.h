#ifndef UDSSENDERWINDOW_H
#define UDSSENDERWINDOW_H

#include <QDialog>

namespace Ui {
class UdsSenderWindow;
}

class UdsSenderWindow : public QDialog
{
    Q_OBJECT

public:
    explicit UdsSenderWindow(QWidget *parent = nullptr);
    ~UdsSenderWindow();

private:
    Ui::UdsSenderWindow *ui;

    void writeRequest(int row);
    void moveRequest(int offset);
};

#endif
