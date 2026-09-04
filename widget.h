#pragma once

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
    class Widget;
}
QT_END_NAMESPACE

class QLabel;
class QComboBox;
class QLineEdit;
class QPushButton;
class QProgressBar;
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
    void refreshTicketRouteChoices();
    void refreshTicketSeatChoices();
    void refreshSeatInventoryPage();
    void refreshScheduleRows();
    void updateStatistics();
    void sellTickets();
    void refundTickets();

    // V0.11 实名订单：校验旅客资料、创建订单并支持按订单安全退票。
    bool validatePassenger();
    void createOrder(int routeRow, int quantity, double amount,
                     const QString &orderNumber);
    void saveOrders() const;
    void loadOrders();
    void filterOrders();
    void refundSelectedOrder();
    int findRouteRow(const QString &routeNumber, const QString &departureDate,
                     const QString &seatType) const;

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
    QComboBox *ticketRouteCombo = nullptr;
    QComboBox *ticketSeatCombo = nullptr;
    QComboBox *seatServiceCombo = nullptr;
    QLabel *routeCountLabel = nullptr;
    QLabel *remainingTotalLabel = nullptr;
    QLabel *soldTotalLabel = nullptr;
    QLabel *revenueTotalLabel = nullptr;
    QLabel *lowStockLabel = nullptr;
    QLabel *overviewInsightLabel = nullptr;
    QProgressBar *occupancyProgress = nullptr;
    QSpinBox *ticketQuantitySpin = nullptr;
    QPushButton *sellTicketButton = nullptr;
    QPushButton *refundTicketButton = nullptr;
    QTableWidget *seatInventoryTable = nullptr;
    QComboBox *seatTypeEditor = nullptr;
    QLineEdit *seatPriceEdit = nullptr;
    QLineEdit *seatCapacityEdit = nullptr;
    QLineEdit *passengerNameEdit = nullptr;
    QLineEdit *passengerIdEdit = nullptr;
    QLineEdit *passengerPhoneEdit = nullptr;

    QTabWidget *contentTabs = nullptr;
    QTableWidget *transactionTable = nullptr;
    QLineEdit *transactionSearchEdit = nullptr;
    QTableWidget *orderTable = nullptr;
    QLineEdit *orderSearchEdit = nullptr;
    QPushButton *refundOrderButton = nullptr;
};
