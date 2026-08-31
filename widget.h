#pragma once

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
    class Widget;
}
QT_END_NAMESPACE

class QLabel;
class QPushButton;
class QSpinBox;

class Widget : public QWidget
{
    Q_OBJECT

public:
    explicit Widget(QWidget *parent = nullptr);
    ~Widget();

private:
    // 数据持久化：保存和恢复所有班次及其售票统计。
    void loadRoutes();
    void saveRoutes() const;

    // V0.8 售票中心：刷新选中班次、统计数据，并处理售票和退票。
    void updateTicketSelection(int row);
    void updateStatistics();
    void sellTickets();
    void refundTickets();

    Ui::Widget *ui;
    int editingRow = -1;

    QLabel *selectedRouteLabel = nullptr;
    QLabel *routeCountLabel = nullptr;
    QLabel *remainingTotalLabel = nullptr;
    QLabel *soldTotalLabel = nullptr;
    QLabel *revenueTotalLabel = nullptr;
    QSpinBox *ticketQuantitySpin = nullptr;
    QPushButton *sellTicketButton = nullptr;
    QPushButton *refundTicketButton = nullptr;
};
