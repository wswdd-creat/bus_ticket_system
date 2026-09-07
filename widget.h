#pragma once

#include <QWidget>
#include <QStringList>
#include <QJsonObject>

QT_BEGIN_NAMESPACE
namespace Ui { class Widget; }
QT_END_NAMESPACE

class QLabel;
class QComboBox;
class QDateEdit;
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
    // 班次和站点数据持久化。
    void loadRoutes();
    void saveRoutes() const;
    void loadStations();
    void saveStations() const;
    bool saveBusinessState();
    QJsonObject captureSettingsSnapshot() const;
    bool writeSettingsSnapshot(const QJsonObject &snapshot, QString *error = nullptr) const;
    bool writeBackupFile(const QString &path, const QJsonObject &snapshot, QString *error = nullptr) const;
    void reloadBusinessData();
    void backupBusinessData();
    void restoreBusinessData();
    void runDataHealthCheck();
    void showOperationFeedback(const QString &message, bool success = true);

    // 售票中心、席位库存和经营统计。
    void updateTicketSelection(int row);
    void sortRoutesChronologically();
    void refreshTicketRouteChoices();
    void refreshTicketSeatChoices();
    void refreshSeatInventoryPage();
    void refreshScheduleRows();
    void updateStatistics();
    bool isRouteSellable(int row, QString *reason = nullptr) const;
    void focusRouteFromWarning(int routeRow);
    void sellTickets();

    // 实名订单：组合筛选、退票和改签。
    bool validatePassenger();
    QStringList chooseSeats(int routeRow, int quantity, int excludedOrderRow = -1);
    void createOrder(int routeRow, int quantity, double amount,
                     const QString &orderNumber);
    void saveOrders() const;
    void loadOrders();
    void filterOrders();
    void exportOrders();
    void refundSelectedOrder();
    void rescheduleSelectedOrder();
    void installOrderActionButton(int row);
    int findRouteRow(const QString &routeNumber, const QString &departureDate,
                     const QString &seatType) const;

    // 交易流水：记录、保存、读取、筛选与导出。
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
    QComboBox *departureStationCombo = nullptr;
    QComboBox *destinationStationCombo = nullptr;
    QStringList stationNames;
    QStringList selectedSeatNumbers;
    QJsonObject lastSavedSettings;
    QLabel *routeCountLabel = nullptr;
    QLabel *remainingTotalLabel = nullptr;
    QLabel *soldTotalLabel = nullptr;
    QLabel *revenueTotalLabel = nullptr;
    QLabel *lowStockLabel = nullptr;
    QLabel *overviewInsightLabel = nullptr;
    QLabel *systemBadge = nullptr;
    QLabel *salesTrendLabel = nullptr;
    QLabel *popularRoutesLabel = nullptr;
    QTableWidget *warningTable = nullptr;
    QProgressBar *occupancyProgress = nullptr;
    QSpinBox *ticketQuantitySpin = nullptr;
    QPushButton *sellTicketButton = nullptr;
    QPushButton *toggleRouteStatusButton = nullptr;
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
    QLineEdit *orderPassengerFilterEdit = nullptr;
    QLineEdit *orderRouteFilterEdit = nullptr;
    QComboBox *orderStatusFilterCombo = nullptr;
    QDateEdit *orderStartDateEdit = nullptr;
    QDateEdit *orderEndDateEdit = nullptr;
    QPushButton *refundOrderButton = nullptr;
    QPushButton *rescheduleOrderButton = nullptr;
    QPushButton *exportOrderButton = nullptr;
    QPushButton *exportTransactionButton = nullptr;
};
