#pragma once

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
    class Widget;
}
QT_END_NAMESPACE

class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTabWidget;

class Widget : public QWidget
{
    Q_OBJECT

public:
    explicit Widget(QWidget *parent = nullptr);
    ~Widget();

private:
    // 班次数据持久化：保存和恢复全部班次及其售票统计。
    void loadRoutes();
    void saveRoutes() const;

    // 售票中心：刷新选中班次、运营指标，并处理售票和退票。
    void updateTicketSelection(int row);
    void updateStatistics();
    void sellTickets();
    void refundTickets();

    // V0.9 交易流水：记录、保存、读取、筛选与导出每次交易。
    void recordTransaction(const QString &type, int routeRow,
                           int quantity, double unitPrice,
                           double amount, int remaining);
    void saveTransactions() const;
    void loadTransactions();
    void filterTransactions();
    void exportTransactions();

    Ui::Widget *ui;
    int editingRow = -1;

    QLabel *selectedRouteLabel = nullptr;
    QLabel *routeCountLabel = nullptr;
    QLabel *remainingTotalLabel = nullptr;
    QLabel *soldTotalLabel = nullptr;
    QLabel *revenueTotalLabel = nullptr;
    QLabel *lowStockLabel = nullptr;
    QSpinBox *ticketQuantitySpin = nullptr;
    QPushButton *sellTicketButton = nullptr;
    QPushButton *refundTicketButton = nullptr;

    QTabWidget *contentTabs = nullptr;
    QTableWidget *transactionTable = nullptr;
    QLineEdit *transactionSearchEdit = nullptr;
};
