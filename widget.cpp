#include "widget.h"
#include "database.h"
#include "ui_widget.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QCollator>
#include <QCompleter>
#include <QDateEdit>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIntValidator>
#include <QInputDialog>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimer>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QProgressBar>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionTab>
#include <QStylePainter>
#include <QTabBar>
#include <QStringConverter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QVBoxLayout>
#include <QTime>
#include <QUrl>

#include <algorithm>

// ==================== 表格列定义与通用辅助工具 ====================
// 用枚举代替数字下标，后续看到 Number、Price 等名称就能知道对应哪一列。
namespace {
constexpr int ColumnCount = 11;
enum Column {
    Number, DepartureDate, Departure, Destination, DepartureTime,
    ArrivalTime, SeatType, Price, TotalSeats, Remaining, Status
};

// 售票统计保存在“班次号”单元格的自定义数据角色中，不占用可见表格列。
constexpr int SoldRole = Qt::UserRole + 1;
constexpr int RevenueRole = Qt::UserRole + 2;

// 订单主表仅保留高频字段；敏感详情和计算字段存入订单号单元格的数据角色。
enum OrderColumn { OrderExpand, OrderNumber, OrderCreated, OrderPassenger,
                   OrderRoute, OrderDate, OrderSeatType, OrderJourney,
                   OrderAmount, OrderStatus, OrderAction, OrderColumnCount };
constexpr int OrderIdRole = Qt::UserRole + 20;
constexpr int OrderPhoneRole = Qt::UserRole + 21;
constexpr int OrderQuantityRole = Qt::UserRole + 22;
constexpr int OrderUnitPriceRole = Qt::UserRole + 23;
constexpr int OrderSeatNumberRole = Qt::UserRole + 24;
constexpr int OrderDetailRole = Qt::UserRole + 25;

QString orderData(const QTableWidget *table, int row, int role)
{
    const auto *item = table->item(row, OrderNumber);
    return item ? item->data(role).toString() : QString();
}

bool isOrderDetailRow(const QTableWidget *table, int row)
{
    const auto *item = table->item(row, OrderExpand);
    return item && item->data(OrderDetailRole).toBool();
}

QString cleanSeatNumber(QString value)
{
    value = QUrl::fromPercentEncoding(value.trimmed().toUtf8()).trimmed();
    static const QRegularExpression valid(QStringLiteral("^[0-9]{2}[ABCDF](、[0-9]{2}[ABCDF])*$"));
    return valid.match(value).hasMatch() ? value : QStringLiteral("—");
}

// 按对象名称查找输入框，方便读取程序运行时动态创建的控件。

// ==================== V0.10 横排侧边导航控件 ====================
// Qt 的 West 页签默认旋转文字；这里保留左侧位置，但按正常阅读方向绘制图标和文字。
class HorizontalTabBar : public QTabBar
{
public:
    explicit HorizontalTabBar(QWidget *parent = nullptr)
        : QTabBar(parent)
    {
        setFocusPolicy(Qt::StrongFocus);
        setElideMode(Qt::ElideRight);
        setUsesScrollButtons(false);
    }

    QSize tabSizeHint(int index) const override
    {
        Q_UNUSED(index);
        return QSize(178, 58);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QStylePainter painter(this);

        for (int index = 0; index < count(); ++index) {
            QStyleOptionTab option;
            initStyleOption(&option, index);

            // 先交给当前 QSS 绘制选中、悬停和焦点状态的背景。
            painter.drawControl(QStyle::CE_TabBarTabShape, option);

            QRect contentRect = option.rect.adjusted(16, 0, -12, 0);
            const QIcon icon = tabIcon(index);
            if (!icon.isNull()) {
                const QSize size = iconSize();
                const QIcon::Mode mode = isTabEnabled(index)
                                             ? QIcon::Normal : QIcon::Disabled;
                const QIcon::State state = currentIndex() == index
                                               ? QIcon::On : QIcon::Off;
                const QPixmap pixmap = icon.pixmap(size, mode, state);
                const QRect iconRect(
                    contentRect.left(),
                    contentRect.center().y() - size.height() / 2,
                    size.width(), size.height());
                painter.drawItemPixmap(iconRect, Qt::AlignCenter, pixmap);
                contentRect.setLeft(iconRect.right() + 12);
            }

            // 文字始终水平、左对齐，符合中文桌面软件的自然阅读方向。
            painter.drawText(contentRect, Qt::AlignLeft | Qt::AlignVCenter,
                             tabText(index));
        }
    }
};

class NavigationTabWidget : public QTabWidget
{
public:
    explicit NavigationTabWidget(QWidget *parent = nullptr)
        : QTabWidget(parent)
    {
        setTabBar(new HorizontalTabBar(this));
    }
};
QLineEdit *field(QWidget *window, const char *name)
{
    return window->findChild<QLineEdit *>(QString::fromLatin1(name));
}
}

// ==================== V0.17 SQLite 数据库初始化与旧数据迁移 ====================
// 首次启动将旧 QSettings 快照导入 SQLite；以后启动以数据库为准回写兼容镜像。
bool Widget::initializeDatabase()
{
    QString error;
    auto &database = DatabaseManager::instance();
    if (!database.initialize(&error)) {
        QMessageBox::critical(this, QStringLiteral("数据库初始化失败"),
                              QStringLiteral("SQLite 无法启动，程序将暂时使用旧存储。\n%1").arg(error));
        return false;
    }

    if (database.isEmpty(&error)) {
        if (!database.replaceAll(captureSettingsSnapshot(), &error)) {
            QMessageBox::critical(this, QStringLiteral("旧数据迁移失败"),
                                  QStringLiteral("旧数据尚未写入 SQLite：\n%1").arg(error));
            return false;
        }
    } else {
        const QJsonObject snapshot = database.exportSnapshot(&error);
        if (snapshot.isEmpty() || !writeSettingsSnapshot(snapshot, &error)) {
            QMessageBox::critical(this, QStringLiteral("数据库读取失败"),
                                  QStringLiteral("无法从 SQLite 恢复业务数据：\n%1").arg(error));
            return false;
        }
    }
    return true;
}

// ==================== 页面初始化与功能绑定 ====================
// 构造函数：窗口创建时依次完成界面初始化、信号连接和历史数据读取。
Widget::Widget(QWidget *parent)
    : QWidget(parent), ui(new Ui::Widget)
{
    // 读取 widget.ui，并创建 Designer 中设计的所有控件。
    ui->setupUi(this);
    databaseReady = initializeDatabase();
    setWindowTitle(QStringLiteral("客运售票运营中心 · V0.17"));
    setMinimumSize(1100, 650);

    ui->verticalLayout_2->setContentsMargins(28, 22, 28, 22);
    ui->verticalLayout->setSpacing(14);
    ui->gridLayout->setHorizontalSpacing(12);
    ui->gridLayout->setVerticalSpacing(12);
    ui->horizontalLayout->setSpacing(10);
    ui->titleLabel->setAlignment(Qt::AlignCenter);
    ui->titleLabel->setMinimumHeight(64);

    // ---------- V0.11 班次日期、时刻与席位库存输入 ----------
    // 每一行代表“车次 + 日期 + 席位”，使不同席位拥有独立票价与余票。
    auto *dateEdit = new QDateEdit(this);
    dateEdit->setObjectName(QStringLiteral("departureDateEdit"));
    dateEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    dateEdit->setCalendarPopup(true);
    dateEdit->setDate(QDate::currentDate());

    auto *timeEdit = new QLineEdit(this);
    timeEdit->setObjectName(QStringLiteral("departureTimeEdit"));
    timeEdit->setPlaceholderText(QStringLiteral("发车时间 08:30"));
    timeEdit->setInputMask(QStringLiteral("00:00;_"));

    auto *arrivalTimeEdit = new QLineEdit(this);
    arrivalTimeEdit->setObjectName(QStringLiteral("arrivalTimeEdit"));
    arrivalTimeEdit->setPlaceholderText(QStringLiteral("到达时间 12:30"));
    arrivalTimeEdit->setInputMask(QStringLiteral("00:00;_"));

    // 到达日：支持跨天班次（如 23:00 发车、次日 06:00 到达）。
    auto *arrivalDayCombo = new QComboBox(this);
    arrivalDayCombo->setObjectName(QStringLiteral("arrivalDayCombo"));
    arrivalDayCombo->addItems({QStringLiteral("当日到达"), QStringLiteral("次日到达")});
    arrivalDayCombo->setMinimumWidth(118);

    auto *seatTypeCombo = new QComboBox(this);
    seatTypeCombo->setObjectName(QStringLiteral("seatTypeCombo"));
    seatTypeCombo->addItems({QStringLiteral("商务座"), QStringLiteral("一等座"),
                             QStringLiteral("二等座"), QStringLiteral("软卧"),
                             QStringLiteral("硬卧"), QStringLiteral("硬座"),
                             QStringLiteral("无座")});
    seatTypeEditor = seatTypeCombo;

    auto *priceEdit = new QLineEdit(this);
    priceEdit->setObjectName(QStringLiteral("priceEdit"));
    priceEdit->setPlaceholderText(QStringLiteral("票价（元）"));
    priceEdit->setValidator(new QDoubleValidator(0.0, 99999.99, 2, priceEdit));
    seatPriceEdit = priceEdit;

    auto *seatsEdit = new QLineEdit(this);
    seatsEdit->setObjectName(QStringLiteral("remainingEdit"));
    seatsEdit->setPlaceholderText(QStringLiteral("座位总数"));
    seatsEdit->setValidator(new QIntValidator(0, 99999, seatsEdit));
    seatCapacityEdit = seatsEdit;

    ui->horizontalLayout->insertWidget(1, dateEdit);
    ui->horizontalLayout->insertWidget(4, timeEdit);
    ui->horizontalLayout->insertWidget(5, arrivalTimeEdit);
    ui->horizontalLayout->insertWidget(6, seatTypeCombo);
    ui->horizontalLayout->insertWidget(7, priceEdit);
    ui->horizontalLayout->insertWidget(8, seatsEdit);

    for (QLineEdit *edit : {ui->searchEdit, ui->routeNumberEdit,
                            ui->departureEdit, ui->destinationEdit,
                            timeEdit, arrivalTimeEdit, priceEdit, seatsEdit}) {
        edit->setClearButtonEnabled(true);
    }

    ui->searchEdit->setPlaceholderText(
        QStringLiteral("搜索车次号、发车日期、出发地、目的地或时刻"));

    // ---------- 班次表格初始化 ----------
    // 设置十列，完整展示日期、发到时刻、席位容量和实时余票。
    ui->routeTable->setColumnCount(ColumnCount);
    ui->routeTable->setHorizontalHeaderLabels(
        {QStringLiteral("车次号"), QStringLiteral("发车日期"),
         QStringLiteral("出发地"), QStringLiteral("目的地"),
         QStringLiteral("发车时间"), QStringLiteral("到达时间"),
         QStringLiteral("席位"), QStringLiteral("票价"),
         QStringLiteral("座位数"), QStringLiteral("余票"),
         QStringLiteral("状态")});
    ui->routeTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->routeTable->horizontalHeader()->setMinimumHeight(42);
    ui->routeTable->verticalHeader()->setVisible(false);
    ui->routeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->routeTable->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->routeTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->routeTable->setAlternatingRowColors(true);
    ui->routeTable->setShowGrid(false);
    // 班次管理只显示班次本身；席位、价格和库存移到独立的席位库存页面。
    for (int column = SeatType; column <= Remaining; ++column) {
        ui->routeTable->setColumnHidden(column, true);
    }

    // ==================== V0.9 品牌头部与运营驾驶舱 ====================
    // 隐藏 Designer 的旧标题，用更完整的品牌头部展示系统定位和运行状态。
    ui->titleLabel->hide();
    auto *heroPanel = new QFrame(this);
    heroPanel->setObjectName(QStringLiteral("heroPanel"));
    auto *heroLayout = new QHBoxLayout(heroPanel);
    heroLayout->setContentsMargins(22, 16, 22, 16);

    auto *heroTextLayout = new QVBoxLayout;
    auto *heroTitle = new QLabel(QStringLiteral("客运售票运营中心"), heroPanel);
    heroTitle->setObjectName(QStringLiteral("heroTitle"));
    auto *heroSubtitle = new QLabel(
        QStringLiteral("班次调度 · 票务交易 · 库存预警 · 经营数据一体化"), heroPanel);
    heroSubtitle->setObjectName(QStringLiteral("heroSubtitle"));
    heroTextLayout->addWidget(heroTitle);
    heroTextLayout->addWidget(heroSubtitle);

    systemBadge = new QLabel(databaseReady
        ? QStringLiteral("●  SQLite 数据库正常")
        : QStringLiteral("⚠  兼容存储模式"), heroPanel);
    systemBadge->setObjectName(QStringLiteral("systemBadge"));
    systemBadge->setAlignment(Qt::AlignCenter);
    heroLayout->addLayout(heroTextLayout);
    heroLayout->addStretch();
    heroLayout->addWidget(systemBadge);
    ui->verticalLayout->insertWidget(0, heroPanel);

    // 关键指标使用独立卡片放在页面顶部，让经营状态一眼可见。
    auto *statsPanel = new QFrame(this);
    statsPanel->setObjectName(QStringLiteral("statsPanel"));
    auto *statsLayout = new QHBoxLayout(statsPanel);
    statsLayout->setContentsMargins(0, 0, 0, 0);
    statsLayout->setSpacing(12);

    routeCountLabel = new QLabel(statsPanel);
    remainingTotalLabel = new QLabel(statsPanel);
    soldTotalLabel = new QLabel(statsPanel);
    revenueTotalLabel = new QLabel(statsPanel);
    lowStockLabel = new QLabel(statsPanel);
    const QList<QPair<QLabel *, QString>> statisticCards{
        {routeCountLabel, QStringLiteral("routeCard")},
        {remainingTotalLabel, QStringLiteral("remainingCard")},
        {soldTotalLabel, QStringLiteral("soldCard")},
        {revenueTotalLabel, QStringLiteral("revenueCard")},
        {lowStockLabel, QStringLiteral("warningCard")}};
    for (const auto &card : statisticCards) {
        card.first->setObjectName(card.second);
        card.first->setProperty("statistic", true);
        card.first->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        statsLayout->addWidget(card.first, 1);
    }
    ui->verticalLayout->insertWidget(1, statsPanel);

    // ==================== V0.10 多页面运营工作台 ====================
    // 采用左侧导航与堆叠页面，把总览、班次、售票和流水拆开，避免功能持续增加后互相拥挤。
    contentTabs = new NavigationTabWidget(this);
    contentTabs->setObjectName(QStringLiteral("contentTabs"));
    contentTabs->setTabPosition(QTabWidget::West);
    contentTabs->setIconSize(QSize(20, 20));
    contentTabs->setDocumentMode(true);

    // ---------- 页面一：运营总览 ----------
    // 总览页只展示关键指标、售票率和自动运营建议，并提供常用任务快捷入口。
    auto *overviewPage = new QWidget(contentTabs);
    overviewPage->setObjectName(QStringLiteral("overviewPage"));
    auto *overviewLayout = new QVBoxLayout(overviewPage);
    overviewLayout->setContentsMargins(18, 18, 18, 18);
    overviewLayout->setSpacing(16);

    auto *overviewTitle = new QLabel(QStringLiteral("运营总览"), overviewPage);
    overviewTitle->setObjectName(QStringLiteral("pageTitle"));
    auto *overviewDescription = new QLabel(
        QStringLiteral("集中查看班次供给、售票表现、营业收入与库存风险"), overviewPage);
    overviewDescription->setObjectName(QStringLiteral("pageDescription"));
    overviewLayout->addWidget(overviewTitle);
    overviewLayout->addWidget(overviewDescription);
    overviewLayout->addWidget(statsPanel);

    auto *overviewDetails = new QHBoxLayout;
    overviewDetails->setSpacing(14);

    auto *healthCard = new QFrame(overviewPage);
    healthCard->setObjectName(QStringLiteral("overviewPanelCard"));
    auto *healthLayout = new QVBoxLayout(healthCard);
    healthLayout->setContentsMargins(20, 18, 20, 18);
    healthLayout->setSpacing(12);
    auto *healthTitle = new QLabel(QStringLiteral("运营健康度"), healthCard);
    healthTitle->setObjectName(QStringLiteral("sectionTitle"));
    auto *healthCaption = new QLabel(
        QStringLiteral("售票率根据累计销量与当前余票实时计算"), healthCard);
    healthCaption->setObjectName(QStringLiteral("sectionDescription"));
    occupancyProgress = new QProgressBar(healthCard);
    occupancyProgress->setObjectName(QStringLiteral("occupancyProgress"));
    occupancyProgress->setRange(0, 100);
    occupancyProgress->setTextVisible(true);
    overviewInsightLabel = new QLabel(healthCard);
    overviewInsightLabel->setObjectName(QStringLiteral("overviewInsightLabel"));
    overviewInsightLabel->setWordWrap(true);
    healthLayout->addWidget(healthTitle);
    healthLayout->addWidget(healthCaption);
    healthLayout->addWidget(occupancyProgress);
    healthLayout->addWidget(overviewInsightLabel);
    healthLayout->addStretch();

    auto *quickCard = new QFrame(overviewPage);
    quickCard->setObjectName(QStringLiteral("overviewPanelCard"));
    auto *quickLayout = new QVBoxLayout(quickCard);
    quickLayout->setContentsMargins(20, 18, 20, 18);
    quickLayout->setSpacing(10);
    auto *quickTitle = new QLabel(QStringLiteral("快捷工作入口"), quickCard);
    quickTitle->setObjectName(QStringLiteral("sectionTitle"));
    auto *quickCaption = new QLabel(
        QStringLiteral("按业务任务进入独立页面，减少来回查找"), quickCard);
    quickCaption->setObjectName(QStringLiteral("sectionDescription"));
    auto *manageShortcut = new QPushButton(
        QStringLiteral("进入班次管理"), quickCard);
    manageShortcut->setObjectName(QStringLiteral("overviewShortcutButton"));
    auto *ticketShortcut = new QPushButton(
        QStringLiteral("进入售票中心"), quickCard);
    ticketShortcut->setObjectName(QStringLiteral("overviewShortcutButton"));
    auto *historyShortcut = new QPushButton(
        QStringLiteral("查看交易流水"), quickCard);
    historyShortcut->setObjectName(QStringLiteral("overviewShortcutButton"));
    quickLayout->addWidget(quickTitle);
    quickLayout->addWidget(quickCaption);
    quickLayout->addWidget(manageShortcut);
    quickLayout->addWidget(ticketShortcut);
    quickLayout->addWidget(historyShortcut);
    auto *dataSafetyTitle = new QLabel(QStringLiteral("数据安全工具"), quickCard);
    dataSafetyTitle->setObjectName(QStringLiteral("dataSafetyTitle"));
    auto *dataSafetyLayout = new QHBoxLayout;
    dataSafetyLayout->setSpacing(8);
    auto *backupDataButton = new QPushButton(QStringLiteral("备份数据"), quickCard);
    backupDataButton->setObjectName(QStringLiteral("dataSafetyButton"));
    backupDataButton->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    auto *restoreDataButton = new QPushButton(QStringLiteral("恢复数据"), quickCard);
    restoreDataButton->setObjectName(QStringLiteral("dataSafetyButton"));
    restoreDataButton->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    auto *healthCheckButton = new QPushButton(QStringLiteral("数据体检"), quickCard);
    healthCheckButton->setObjectName(QStringLiteral("dataSafetyButton"));
    healthCheckButton->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
    backupDataButton->setToolTip(QStringLiteral("将全部班次、席位、订单、流水和站点保存为 JSON"));
    restoreDataButton->setToolTip(QStringLiteral("从本系统 JSON 备份恢复全部业务数据"));
    healthCheckButton->setToolTip(QStringLiteral("检查库存、订单、金额和座位关联是否一致"));
    dataSafetyLayout->addWidget(backupDataButton);
    dataSafetyLayout->addWidget(restoreDataButton);
    dataSafetyLayout->addWidget(healthCheckButton);
    quickLayout->addWidget(dataSafetyTitle);
    quickLayout->addLayout(dataSafetyLayout);
    quickLayout->addStretch();

    overviewDetails->addWidget(healthCard, 3);
    overviewDetails->addWidget(quickCard, 2);
    overviewLayout->addLayout(overviewDetails);

    // ==================== V0.14 运营预警与数据分析 ====================
    // 预警和分析直接放在总览页下半区，不增加新的导航页面。
    auto *analysisRow = new QHBoxLayout;
    analysisRow->setSpacing(14);
    auto *warningCard = new QFrame(overviewPage);
    warningCard->setObjectName(QStringLiteral("overviewPanelCard"));
    auto *warningLayout = new QVBoxLayout(warningCard);
    warningLayout->setContentsMargins(18, 16, 18, 16);
    auto *warningTitle = new QLabel(QStringLiteral("运营预警"), warningCard);
    warningTitle->setObjectName(QStringLiteral("sectionTitle"));
    warningLayout->addWidget(warningTitle);
    warningTable = new QTableWidget(warningCard);
    warningTable->setObjectName(QStringLiteral("warningTable"));
    warningTable->setColumnCount(3);
    warningTable->setHorizontalHeaderLabels({QStringLiteral("级别"), QStringLiteral("班次"), QStringLiteral("预警说明")});
    warningTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    warningTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    warningTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    warningTable->verticalHeader()->hide();
    warningTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    warningTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    warningTable->setAlternatingRowColors(true);
    warningTable->setMaximumHeight(220);
    warningLayout->addWidget(warningTable);

    auto *analysisCard = new QFrame(overviewPage);
    analysisCard->setObjectName(QStringLiteral("overviewPanelCard"));
    auto *analysisLayout = new QVBoxLayout(analysisCard);
    analysisLayout->setContentsMargins(18, 16, 18, 16);
    auto *analysisTitle = new QLabel(QStringLiteral("近七日经营分析"), analysisCard);
    analysisTitle->setObjectName(QStringLiteral("sectionTitle"));
    analysisLayout->addWidget(analysisTitle);
    salesTrendLabel = new QLabel(analysisCard);
    salesTrendLabel->setObjectName(QStringLiteral("salesTrendLabel"));
    popularRoutesLabel = new QLabel(analysisCard);
    popularRoutesLabel->setObjectName(QStringLiteral("popularRoutesLabel"));
    analysisLayout->addWidget(salesTrendLabel);
    analysisLayout->addWidget(popularRoutesLabel);
    analysisLayout->addStretch();
    analysisRow->addWidget(warningCard, 3);
    analysisRow->addWidget(analysisCard, 2);
    overviewLayout->addLayout(analysisRow, 1);

    // ---------- 页面二：班次管理 ----------
    // 原有班次表格和增删改查逻辑保持不变，仅移动到独立管理页面。
    auto *routePage = new QWidget(contentTabs);
    routePage->setObjectName(QStringLiteral("routePage"));
    auto *routePageLayout = new QVBoxLayout(routePage);
    routePageLayout->setContentsMargins(12, 12, 12, 12);
    if (QLayoutItem *oldItem =
            ui->verticalLayout->replaceWidget(ui->routeTable, contentTabs)) {
        delete oldItem;
    }

    // ==================== 班次管理工具区 ====================
    // 将搜索、操作按钮和六字段录入表单真正放入班次页面，而不是留在外层布局中。
    auto *routeTitle = new QLabel(QStringLiteral("班次管理"), routePage);
    routeTitle->setObjectName(QStringLiteral("pageTitle"));
    auto *routeDescription = new QLabel(
        QStringLiteral("维护班次基础资料、发车计划、票价和初始库存"), routePage);
    routeDescription->setObjectName(QStringLiteral("pageDescription"));

    auto *managementPanel = new QFrame(routePage);
    managementPanel->setObjectName(QStringLiteral("routeManagementPanel"));
    auto *managementLayout = new QVBoxLayout(managementPanel);
    managementLayout->setContentsMargins(16, 16, 16, 16);
    managementLayout->setSpacing(12);

    // 搜索栏：支持关键词筛选，并可一键恢复全部班次。
    auto *searchToolbar = new QHBoxLayout;
    searchToolbar->setSpacing(10);
    searchToolbar->addWidget(ui->searchEdit, 1);
    searchToolbar->addWidget(ui->searchButton);
    searchToolbar->addWidget(ui->showAllButton);

    // 操作栏：修改、保存和删除只针对表格当前选中班次。
    auto *actionToolbar = new QHBoxLayout;
    actionToolbar->setSpacing(10);
    actionToolbar->addWidget(ui->editRouteButton);
    actionToolbar->addWidget(ui->saveEditButton);
    actionToolbar->addWidget(ui->deleteRouteButton);
    toggleRouteStatusButton = new QPushButton(QStringLiteral("停用班次"), managementPanel);
    toggleRouteStatusButton->setObjectName(QStringLiteral("toggleRouteStatusButton"));
    toggleRouteStatusButton->setEnabled(false);
    auto *addStationButton = new QPushButton(QStringLiteral("新增站点"), managementPanel);
    addStationButton->setObjectName(QStringLiteral("addStationButton"));
    actionToolbar->addWidget(toggleRouteStatusButton);
    actionToolbar->addWidget(addStationButton);
    actionToolbar->addStretch();

    // 录入区只维护班次基础信息，不再要求调度人员同时填写席位和票价。
    auto *inputPanel = new QFrame(managementPanel);
    inputPanel->setObjectName(QStringLiteral("routeInputPanel"));
    auto *inputGrid = new QGridLayout(inputPanel);
    inputGrid->setContentsMargins(12, 12, 12, 12);
    inputGrid->setHorizontalSpacing(10);
    inputGrid->setVerticalSpacing(10);
    // 地名列分配更多横向空间，窗口缩放时优先保证站点名称完整可读。
    inputGrid->setColumnStretch(0, 2);
    inputGrid->setColumnStretch(1, 2);
    inputGrid->setColumnStretch(2, 3);
    inputGrid->setColumnStretch(3, 3);
    inputGrid->addWidget(ui->routeNumberEdit, 0, 0);
    inputGrid->addWidget(dateEdit, 0, 1);
    // 站点采用可搜索下拉框，原 Designer 输入框保留但隐藏，避免破坏 objectName。
    ui->departureEdit->hide();
    ui->destinationEdit->hide();
    departureStationCombo = new QComboBox(inputPanel);
    departureStationCombo->setObjectName(QStringLiteral("departureStationCombo"));
    departureStationCombo->setEditable(true);
    departureStationCombo->setInsertPolicy(QComboBox::NoInsert);
    departureStationCombo->setMinimumWidth(190);
    departureStationCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    departureStationCombo->setMinimumContentsLength(10);
    departureStationCombo->lineEdit()->setPlaceholderText(QStringLiteral("选择出发站"));
    destinationStationCombo = new QComboBox(inputPanel);
    destinationStationCombo->setObjectName(QStringLiteral("destinationStationCombo"));
    destinationStationCombo->setEditable(true);
    destinationStationCombo->setInsertPolicy(QComboBox::NoInsert);
    destinationStationCombo->setMinimumWidth(190);
    destinationStationCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    destinationStationCombo->setMinimumContentsLength(10);
    destinationStationCombo->lineEdit()->setPlaceholderText(QStringLiteral("选择目的站"));
    loadStations();
    inputGrid->addWidget(departureStationCombo, 0, 2);
    inputGrid->addWidget(destinationStationCombo, 0, 3);
    inputGrid->addWidget(timeEdit, 1, 0);
    inputGrid->addWidget(arrivalTimeEdit, 1, 1);
    inputGrid->addWidget(arrivalDayCombo, 1, 2);
    inputGrid->addWidget(ui->addRouteButton, 1, 3);

    managementLayout->addLayout(searchToolbar);
    managementLayout->addLayout(actionToolbar);
    managementLayout->addWidget(inputPanel);

    routePageLayout->addWidget(routeTitle);
    routePageLayout->addWidget(routeDescription);
    routePageLayout->addWidget(managementPanel);
    routePageLayout->addWidget(ui->routeTable, 1);

    // ---------- 页面三：售票中心 ----------
    // 售票页面稍后接收原售票面板；用户从班次页选择后可双击进入。
    auto *ticketPage = new QWidget(contentTabs);
    ticketPage->setObjectName(QStringLiteral("ticketPage"));
    auto *ticketPageLayout = new QVBoxLayout(ticketPage);
    ticketPageLayout->setContentsMargins(18, 18, 18, 18);
    ticketPageLayout->setSpacing(14);

    auto *ticketPageTitle = new QLabel(QStringLiteral("售票中心"), ticketPage);
    ticketPageTitle->setObjectName(QStringLiteral("pageTitle"));
    auto *ticketPageDescription = new QLabel(
        QStringLiteral("针对已选班次办理售票与退票，交易结果实时回写库存和经营指标"), ticketPage);
    ticketPageDescription->setObjectName(QStringLiteral("pageDescription"));
    ticketPageLayout->addWidget(ticketPageTitle);
    ticketPageLayout->addWidget(ticketPageDescription);
    // ==================== V0.10 售票中心独立选班次 ====================
    // 售票员可直接在本页搜索和选择班次，不必先返回班次管理页面。
    auto *ticketRouteSelector = new QFrame(ticketPage);
    ticketRouteSelector->setObjectName(QStringLiteral("ticketRouteSelector"));
    auto *ticketRouteSelectorLayout = new QHBoxLayout(ticketRouteSelector);
    ticketRouteSelectorLayout->setContentsMargins(16, 12, 16, 12);
    ticketRouteSelectorLayout->setSpacing(12);
    auto *ticketRouteSelectorLabel = new QLabel(
        QStringLiteral("选择办理班次"), ticketRouteSelector);
    ticketRouteSelectorLabel->setObjectName(
        QStringLiteral("ticketRouteSelectorLabel"));
    ticketRouteCombo = new QComboBox(ticketRouteSelector);
    ticketRouteCombo->setObjectName(QStringLiteral("ticketRouteCombo"));
    ticketRouteCombo->setEditable(true);
    ticketRouteCombo->setInsertPolicy(QComboBox::NoInsert);
    ticketRouteCombo->setMaxVisibleItems(12);
    ticketRouteCombo->completer()->setCaseSensitivity(Qt::CaseInsensitive);
    ticketRouteCombo->completer()->setFilterMode(Qt::MatchContains);
    ticketRouteCombo->completer()->setCompletionMode(QCompleter::PopupCompletion);
    ticketRouteCombo->setMinimumWidth(520);
    ticketRouteCombo->lineEdit()->setPlaceholderText(
        QStringLiteral("输入班次号或地点快速查找"));
    ticketRouteSelectorLayout->addWidget(ticketRouteSelectorLabel);
    ticketRouteSelectorLayout->addWidget(ticketRouteCombo, 1);
    auto *ticketSeatLabel = new QLabel(QStringLiteral("席位"), ticketRouteSelector);
    ticketSeatLabel->setObjectName(QStringLiteral("ticketRouteSelectorLabel"));
    ticketSeatCombo = new QComboBox(ticketRouteSelector);
    ticketSeatCombo->setObjectName(QStringLiteral("ticketSeatCombo"));
    ticketSeatCombo->setMinimumWidth(260);
    ticketSeatCombo->setPlaceholderText(QStringLiteral("请先选择班次"));
    ticketRouteSelectorLayout->addWidget(ticketSeatLabel);
    ticketRouteSelectorLayout->addWidget(ticketSeatCombo, 1);
    ticketPageLayout->addWidget(ticketRouteSelector);

    // ==================== V0.11 实名旅客信息 ====================
    // 售票前采集姓名、身份证号和手机号；统一放入一个信息卡片，减少误填和漏填。
    auto *passengerPanel = new QFrame(ticketPage);
    passengerPanel->setObjectName(QStringLiteral("passengerPanel"));
    auto *passengerLayout = new QGridLayout(passengerPanel);
    passengerLayout->setContentsMargins(16, 14, 16, 14);
    passengerLayout->setHorizontalSpacing(12);
    passengerLayout->setVerticalSpacing(8);

    auto *passengerTitle = new QLabel(
        QStringLiteral("实名旅客信息"), passengerPanel);
    passengerTitle->setObjectName(QStringLiteral("ticketPanelTitle"));
    auto *passengerHint = new QLabel(
        QStringLiteral("请核对旅客证件信息，售票成功后将生成唯一订单号"),
        passengerPanel);
    passengerHint->setObjectName(QStringLiteral("passengerHint"));
    passengerNameEdit = new QLineEdit(passengerPanel);
    passengerNameEdit->setObjectName(QStringLiteral("passengerNameEdit"));
    passengerNameEdit->setPlaceholderText(QStringLiteral("旅客姓名"));
    passengerNameEdit->setClearButtonEnabled(true);
    passengerIdEdit = new QLineEdit(passengerPanel);
    passengerIdEdit->setObjectName(QStringLiteral("passengerIdEdit"));
    passengerIdEdit->setPlaceholderText(QStringLiteral("18 位身份证号"));
    passengerIdEdit->setMaxLength(18);
    passengerIdEdit->setClearButtonEnabled(true);
    passengerPhoneEdit = new QLineEdit(passengerPanel);
    passengerPhoneEdit->setObjectName(QStringLiteral("passengerPhoneEdit"));
    passengerPhoneEdit->setPlaceholderText(QStringLiteral("11 位手机号"));
    passengerPhoneEdit->setMaxLength(11);
    passengerPhoneEdit->setClearButtonEnabled(true);
    passengerLayout->addWidget(passengerTitle, 0, 0);
    passengerLayout->addWidget(passengerHint, 0, 1, 1, 2);
    passengerLayout->addWidget(passengerNameEdit, 1, 0);
    passengerLayout->addWidget(passengerIdEdit, 1, 1);
    passengerLayout->addWidget(passengerPhoneEdit, 1, 2);
    passengerLayout->setColumnStretch(0, 2);
    passengerLayout->setColumnStretch(1, 3);
    passengerLayout->setColumnStretch(2, 2);
    ticketPageLayout->addWidget(passengerPanel);

    connect(ticketRouteCombo, &QComboBox::currentIndexChanged,
            this, [this](int index) {
        if (index < 0) {
            updateTicketSelection(-1);
            refreshTicketSeatChoices();
            return;
        }
        refreshTicketSeatChoices();
        const int row = ticketSeatCombo->currentIndex() >= 0
                            ? ticketSeatCombo->currentData().toInt() : -1;
        if (row >= 0 && row < ui->routeTable->rowCount()) {
            ui->routeTable->selectRow(row);
            updateTicketSelection(row);
            ticketQuantitySpin->setValue(1);
        }
    });
    connect(ticketSeatCombo, &QComboBox::currentIndexChanged,
            this, [this](int index) {
        const int row = index >= 0 ? ticketSeatCombo->itemData(index).toInt() : -1;
        if (row >= 0 && row < ui->routeTable->rowCount()) {
            ui->routeTable->selectRow(row);
            updateTicketSelection(row);
        } else {
            updateTicketSelection(-1);
        }
        ticketQuantitySpin->setValue(1);
    });

    // ---------- 页面四：交易流水 ----------
    // 流水页面保留即时搜索、CSV 导出和清空审计记录功能。
    auto *transactionPage = new QWidget(contentTabs);
    transactionPage->setObjectName(QStringLiteral("transactionPage"));
    auto *transactionPageLayout = new QVBoxLayout(transactionPage);
    transactionPageLayout->setContentsMargins(12, 12, 12, 12);
    transactionPageLayout->setSpacing(10);

    auto *transactionTitle = new QLabel(QStringLiteral("交易流水"), transactionPage);
    transactionTitle->setObjectName(QStringLiteral("pageTitle"));
    auto *transactionDescription = new QLabel(
        QStringLiteral("追踪每一笔售票与退票记录，支持搜索和导出审计"), transactionPage);
    transactionDescription->setObjectName(QStringLiteral("pageDescription"));
    transactionPageLayout->addWidget(transactionTitle);
    transactionPageLayout->addWidget(transactionDescription);

    auto *transactionToolbar = new QHBoxLayout;
    transactionSearchEdit = new QLineEdit(transactionPage);
    transactionSearchEdit->setObjectName(QStringLiteral("transactionSearchEdit"));
    transactionSearchEdit->setPlaceholderText(
        QStringLiteral("搜索时间、类型、班次号、路线或金额"));
    transactionSearchEdit->setClearButtonEnabled(true);
    exportTransactionButton = new QPushButton(QStringLiteral("导出当前结果"), transactionPage);
    exportTransactionButton->setObjectName(QStringLiteral("exportButton"));
    auto *clearHistoryButton = new QPushButton(
        QStringLiteral("清空流水"), transactionPage);
    clearHistoryButton->setObjectName(QStringLiteral("clearHistoryButton"));
    exportTransactionButton->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    clearHistoryButton->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    transactionToolbar->addWidget(transactionSearchEdit, 1);
    transactionToolbar->addWidget(exportTransactionButton);
    transactionToolbar->addWidget(clearHistoryButton);

    transactionTable = new QTableWidget(transactionPage);
    transactionTable->setObjectName(QStringLiteral("transactionTable"));
    transactionTable->setColumnCount(8);
    transactionTable->setHorizontalHeaderLabels(
        {QStringLiteral("交易时间"), QStringLiteral("类型"),
         QStringLiteral("班次号"), QStringLiteral("路线"),
         QStringLiteral("数量"), QStringLiteral("单价"),
         QStringLiteral("交易金额"), QStringLiteral("交易后余票")});
    transactionTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    transactionTable->horizontalHeader()->setMinimumHeight(42);
    transactionTable->verticalHeader()->setVisible(false);
    transactionTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    transactionTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    transactionTable->setAlternatingRowColors(true);
    transactionTable->setShowGrid(false);
    transactionPageLayout->addLayout(transactionToolbar);
    transactionPageLayout->addWidget(transactionTable);

    // ---------- 页面五：订单管理 ----------
    // 实名售票生成独立订单；订单页负责检索旅客、核对订单状态和按单退票。
    auto *orderPage = new QWidget(contentTabs);
    orderPage->setObjectName(QStringLiteral("orderPage"));
    auto *orderPageLayout = new QVBoxLayout(orderPage);
    orderPageLayout->setContentsMargins(12, 12, 12, 12);
    orderPageLayout->setSpacing(10);

    auto *orderTitle = new QLabel(QStringLiteral("订单管理"), orderPage);
    orderTitle->setObjectName(QStringLiteral("pageTitle"));
    auto *orderDescription = new QLabel(
        QStringLiteral("按订单号、旅客、手机号或班次快速查单，并安全办理整单退票"),
        orderPage);
    orderDescription->setObjectName(QStringLiteral("pageDescription"));
    orderPageLayout->addWidget(orderTitle);
    orderPageLayout->addWidget(orderDescription);

    // 订单组合筛选：按旅客、状态、车次和下单日期范围精确缩小结果。
    auto *orderFilterPanel = new QFrame(orderPage);
    orderFilterPanel->setObjectName(QStringLiteral("orderFilterPanel"));
    auto *orderFilterLayout = new QGridLayout(orderFilterPanel);
    orderFilterLayout->setContentsMargins(14, 12, 14, 12);
    orderFilterLayout->setHorizontalSpacing(10);
    orderFilterLayout->setVerticalSpacing(8);

    orderPassengerFilterEdit = new QLineEdit(orderFilterPanel);
    orderPassengerFilterEdit->setObjectName(QStringLiteral("orderPassengerFilterEdit"));
    orderPassengerFilterEdit->setPlaceholderText(QStringLiteral("旅客姓名 / 手机号"));
    orderPassengerFilterEdit->setClearButtonEnabled(true);
    orderStatusFilterCombo = new QComboBox(orderFilterPanel);
    orderStatusFilterCombo->setObjectName(QStringLiteral("orderStatusFilterCombo"));
    orderStatusFilterCombo->addItems({QStringLiteral("全部状态"), QStringLiteral("已出票"),
                                     QStringLiteral("已退票"), QStringLiteral("已取消")});
    orderRouteFilterEdit = new QLineEdit(orderFilterPanel);
    orderRouteFilterEdit->setObjectName(QStringLiteral("orderRouteFilterEdit"));
    orderRouteFilterEdit->setPlaceholderText(QStringLiteral("车次号"));
    orderRouteFilterEdit->setClearButtonEnabled(true);
    orderStartDateEdit = new QDateEdit(orderFilterPanel);
    orderStartDateEdit->setObjectName(QStringLiteral("orderStartDateEdit"));
    orderStartDateEdit->setCalendarPopup(true);
    orderStartDateEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    orderStartDateEdit->setDate(QDate(2000, 1, 1));
    orderEndDateEdit = new QDateEdit(orderFilterPanel);
    orderEndDateEdit->setObjectName(QStringLiteral("orderEndDateEdit"));
    orderEndDateEdit->setCalendarPopup(true);
    orderEndDateEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    orderEndDateEdit->setDate(QDate(2099, 12, 31));
    auto *queryOrderButton = new QPushButton(QStringLiteral("查询"), orderFilterPanel);
    queryOrderButton->setObjectName(QStringLiteral("queryOrderButton"));
    queryOrderButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogContentsView));
    auto *resetOrderButton = new QPushButton(QStringLiteral("重置"), orderFilterPanel);
    resetOrderButton->setObjectName(QStringLiteral("resetOrderButton"));
    resetOrderButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    refundOrderButton = new QPushButton(QStringLiteral("按订单退票"), orderFilterPanel);
    refundOrderButton->setObjectName(QStringLiteral("refundOrderButton"));
    refundOrderButton->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    refundOrderButton->setEnabled(false);
    rescheduleOrderButton = new QPushButton(QStringLiteral("办理改签"), orderFilterPanel);
    rescheduleOrderButton->setObjectName(QStringLiteral("rescheduleOrderButton"));
    rescheduleOrderButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    rescheduleOrderButton->setEnabled(false);

    orderFilterLayout->addWidget(new QLabel(QStringLiteral("旅客"), orderFilterPanel), 0, 0);
    orderFilterLayout->addWidget(orderPassengerFilterEdit, 0, 1);
    orderFilterLayout->addWidget(new QLabel(QStringLiteral("订单状态"), orderFilterPanel), 0, 2);
    orderFilterLayout->addWidget(orderStatusFilterCombo, 0, 3);
    orderFilterLayout->addWidget(new QLabel(QStringLiteral("车次号"), orderFilterPanel), 0, 4);
    orderFilterLayout->addWidget(orderRouteFilterEdit, 0, 5);
    orderFilterLayout->addWidget(new QLabel(QStringLiteral("下单日期"), orderFilterPanel), 1, 0);
    orderFilterLayout->addWidget(orderStartDateEdit, 1, 1);
    orderFilterLayout->addWidget(new QLabel(QStringLiteral("至"), orderFilterPanel), 1, 2);
    orderFilterLayout->addWidget(orderEndDateEdit, 1, 3);
    orderFilterLayout->addWidget(queryOrderButton, 1, 4);
    orderFilterLayout->addWidget(resetOrderButton, 1, 5);
    orderFilterLayout->addWidget(rescheduleOrderButton, 1, 6);
    orderFilterLayout->addWidget(refundOrderButton, 1, 7);
    exportOrderButton = new QPushButton(QStringLiteral("导出当前结果"), orderFilterPanel);
    exportOrderButton->setObjectName(QStringLiteral("exportOrderButton"));
    exportOrderButton->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    exportOrderButton->setToolTip(QStringLiteral("将当前筛选后的订单导出为 CSV"));
    orderFilterLayout->addWidget(exportOrderButton, 0, 6, 1, 2);

    orderTable = new QTableWidget(orderPage);
    orderTable->setObjectName(QStringLiteral("orderTable"));
    orderTable->setColumnCount(OrderColumnCount);
    orderTable->setHorizontalHeaderLabels(
        {QStringLiteral(""), QStringLiteral("订单号"), QStringLiteral("创建时间"),
         QStringLiteral("旅客姓名"), QStringLiteral("车次号"), QStringLiteral("发车日期"),
         QStringLiteral("席位"), QStringLiteral("行程"), QStringLiteral("订单金额"),
         QStringLiteral("状态"), QStringLiteral("操作")});
    orderTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    orderTable->horizontalHeader()->setSectionResizeMode(OrderExpand, QHeaderView::Fixed);
    orderTable->setColumnWidth(OrderExpand, 42);
    orderTable->horizontalHeader()->setSectionResizeMode(OrderAction, QHeaderView::Fixed);
    orderTable->setColumnWidth(OrderAction, 54);
    orderTable->horizontalHeader()->setMinimumHeight(42);
    orderTable->verticalHeader()->setVisible(false);
    // 主订单行使用统一高度，为两侧图标按钮保留稳定的上下留白。
    orderTable->verticalHeader()->setDefaultSectionSize(46);
    orderTable->verticalHeader()->setMinimumSectionSize(46);
    orderTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    orderTable->setSelectionMode(QAbstractItemView::SingleSelection);
    orderTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    orderTable->setAlternatingRowColors(true);
    orderTable->setShowGrid(false);
    orderPageLayout->addWidget(orderFilterPanel);
    orderPageLayout->addWidget(orderTable, 1);

    connect(queryOrderButton, &QPushButton::clicked, this, &Widget::filterOrders);
    connect(exportOrderButton, &QPushButton::clicked, this, &Widget::exportOrders);
    connect(resetOrderButton, &QPushButton::clicked, this, [this] {
        orderPassengerFilterEdit->clear();
        orderRouteFilterEdit->clear();
        orderStatusFilterCombo->setCurrentIndex(0);
        orderStartDateEdit->setDate(QDate(2000, 1, 1));
        orderEndDateEdit->setDate(QDate(2099, 12, 31));
        filterOrders();
    });
    // ==================== 售票中心 ====================
    auto *ticketPanel = new QFrame(this);
    ticketPanel->setObjectName(QStringLiteral("ticketPanel"));
    auto *ticketPanelLayout = new QVBoxLayout(ticketPanel);
    ticketPanelLayout->setContentsMargins(18, 14, 18, 14);
    ticketPanelLayout->setSpacing(10);

    auto *ticketTitle = new QLabel(QStringLiteral("快捷售票台"), ticketPanel);
    ticketTitle->setObjectName(QStringLiteral("ticketPanelTitle"));
    selectedRouteLabel = new QLabel(
        QStringLiteral("请先在表格中选择一个班次，再进行售票或退票。"), ticketPanel);
    selectedRouteLabel->setObjectName(QStringLiteral("selectedRouteLabel"));
    selectedRouteLabel->setWordWrap(true);

    auto *operationLayout = new QHBoxLayout;
    operationLayout->setSpacing(10);
    auto *quantityLabel = new QLabel(QStringLiteral("交易数量"), ticketPanel);
    ticketQuantitySpin = new QSpinBox(ticketPanel);
    ticketQuantitySpin->setObjectName(QStringLiteral("ticketQuantitySpin"));
    ticketQuantitySpin->setRange(1, 99);
    ticketQuantitySpin->setValue(1);
    ticketQuantitySpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    ticketQuantitySpin->setAlignment(Qt::AlignCenter);
    ticketQuantitySpin->setToolTip(QStringLiteral("本次需要购买或退回的票数"));

    auto *decreaseQuantityButton = new QPushButton(QStringLiteral("−"), ticketPanel);
    decreaseQuantityButton->setObjectName(QStringLiteral("quantityStepButton"));
    decreaseQuantityButton->setToolTip(QStringLiteral("数量减 1"));
    decreaseQuantityButton->setEnabled(false);
    auto *increaseQuantityButton = new QPushButton(QStringLiteral("+"), ticketPanel);
    increaseQuantityButton->setObjectName(QStringLiteral("quantityStepButton"));
    increaseQuantityButton->setToolTip(QStringLiteral("数量加 1"));

    connect(decreaseQuantityButton, &QPushButton::clicked,
            ticketQuantitySpin, &QSpinBox::stepDown);
    connect(increaseQuantityButton, &QPushButton::clicked,
            ticketQuantitySpin, &QSpinBox::stepUp);
    connect(ticketQuantitySpin, &QSpinBox::valueChanged, this,
            [decreaseQuantityButton, increaseQuantityButton](int value) {
        decreaseQuantityButton->setEnabled(value > 1);
        increaseQuantityButton->setEnabled(value < 99);
    });

    // ---------- 页面三：席位库存 ----------
    // 调度班次与席位价格分开维护，选中班次后再配置各席位容量和票价。
    auto *seatPage = new QWidget(contentTabs);
    seatPage->setObjectName(QStringLiteral("seatPage"));
    auto *seatPageLayout = new QVBoxLayout(seatPage);
    seatPageLayout->setContentsMargins(12, 12, 12, 12);
    seatPageLayout->setSpacing(10);
    auto *seatPageTitle = new QLabel(QStringLiteral("席位库存"), seatPage);
    seatPageTitle->setObjectName(QStringLiteral("pageTitle"));
    auto *seatPageDescription = new QLabel(
        QStringLiteral("先选择班次，再分别配置各席位票价和座位总数"), seatPage);
    seatPageDescription->setObjectName(QStringLiteral("pageDescription"));

    auto *seatEditorPanel = new QFrame(seatPage);
    seatEditorPanel->setObjectName(QStringLiteral("seatEditorPanel"));
    auto *seatEditorLayout = new QGridLayout(seatEditorPanel);
    seatEditorLayout->setContentsMargins(14, 14, 14, 14);
    seatEditorLayout->setSpacing(10);
    seatServiceCombo = new QComboBox(seatEditorPanel);
    seatServiceCombo->setObjectName(QStringLiteral("seatServiceCombo"));
    seatServiceCombo->setPlaceholderText(QStringLiteral("请选择需要配置席位的班次"));
    seatEditorLayout->addWidget(new QLabel(QStringLiteral("班次"), seatEditorPanel), 0, 0);
    seatEditorLayout->addWidget(seatServiceCombo, 0, 1, 1, 5);
    seatEditorLayout->addWidget(new QLabel(QStringLiteral("席位类型"), seatEditorPanel), 1, 0);
    seatEditorLayout->addWidget(seatTypeEditor, 1, 1);
    seatEditorLayout->addWidget(new QLabel(QStringLiteral("票价"), seatEditorPanel), 1, 2);
    seatEditorLayout->addWidget(seatPriceEdit, 1, 3);
    seatEditorLayout->addWidget(new QLabel(QStringLiteral("座位总数"), seatEditorPanel), 1, 4);
    seatEditorLayout->addWidget(seatCapacityEdit, 1, 5);
    auto *addSeatButton = new QPushButton(QStringLiteral("添加席位"), seatEditorPanel);
    addSeatButton->setObjectName(QStringLiteral("addSeatButton"));
    auto *saveSeatButton = new QPushButton(QStringLiteral("保存修改"), seatEditorPanel);
    saveSeatButton->setObjectName(QStringLiteral("saveSeatButton"));
    auto *deleteSeatButton = new QPushButton(QStringLiteral("删除席位"), seatEditorPanel);
    deleteSeatButton->setObjectName(QStringLiteral("deleteSeatButton"));
    saveSeatButton->setEnabled(false);
    deleteSeatButton->setEnabled(false);
    seatEditorLayout->addWidget(addSeatButton, 2, 3);
    seatEditorLayout->addWidget(saveSeatButton, 2, 4);
    seatEditorLayout->addWidget(deleteSeatButton, 2, 5);

    seatInventoryTable = new QTableWidget(seatPage);
    seatInventoryTable->setObjectName(QStringLiteral("seatInventoryTable"));
    seatInventoryTable->setColumnCount(6);
    seatInventoryTable->setHorizontalHeaderLabels(
        {QStringLiteral("席位"), QStringLiteral("票价"), QStringLiteral("座位总数"),
         QStringLiteral("已售"), QStringLiteral("余票"), QStringLiteral("库存状态")});
    seatInventoryTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    seatInventoryTable->verticalHeader()->setVisible(false);
    seatInventoryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    seatInventoryTable->setSelectionMode(QAbstractItemView::SingleSelection);
    seatInventoryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    seatInventoryTable->setAlternatingRowColors(true);
    seatInventoryTable->setShowGrid(false);
    seatPageLayout->addWidget(seatPageTitle);
    seatPageLayout->addWidget(seatPageDescription);
    seatPageLayout->addWidget(seatEditorPanel);
    seatPageLayout->addWidget(seatInventoryTable, 1);

    connect(seatServiceCombo, &QComboBox::currentIndexChanged,
            this, [this] { refreshSeatInventoryPage(); });
    connect(seatInventoryTable, &QTableWidget::itemSelectionChanged, this,
            [this, saveSeatButton, deleteSeatButton] {
        const int viewRow = seatInventoryTable->currentRow();
        const bool selected = viewRow >= 0 && seatInventoryTable->item(viewRow, 0);
        saveSeatButton->setEnabled(selected);
        deleteSeatButton->setEnabled(selected);
        if (!selected) return;
        const int sourceRow = seatInventoryTable->item(viewRow, 0)
                                  ->data(Qt::UserRole).toInt();
        seatTypeEditor->setCurrentText(
            ui->routeTable->item(sourceRow, SeatType)->text());
        seatPriceEdit->setText(
            ui->routeTable->item(sourceRow, Price)->text().remove(QChar(0x00A5)));
        seatCapacityEdit->setText(
            ui->routeTable->item(sourceRow, TotalSeats)->text());
    });

    const auto validateSeatInput = [this] {
        if (seatServiceCombo->currentIndex() < 0
            || seatPriceEdit->text().trimmed().isEmpty()
            || seatCapacityEdit->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("席位信息不完整"),
                                 QStringLiteral("请选择班次，并填写席位票价和座位总数。"));
            return false;
        }
        return true;
    };

    // ==================== V0.14 历史席位模板自动填充 ====================
    // 选择席位类型时，从同车次最近日期的相同席位复制票价和容量；销量与余票不会复制。
    const auto fillSeatFromHistory = [this] {
        // clearSelection() 不会清除 currentRow()，必须按真实选择状态判断是否正在编辑。
        if (seatServiceCombo->currentIndex() < 0
            || seatInventoryTable->selectionModel()->hasSelection()) return;
        const QString currentKey = seatServiceCombo->currentData().toString();
        const QString currentNumber = currentKey.section(QChar('|'), 0, 0);
        const QString seatType = seatTypeEditor->currentText();
        if (currentNumber.isEmpty() || seatType.isEmpty()) return;

        // 当前班次已继承该席位时，自动选中库存行并带出票价、容量，直接进入修改状态。
        for (int viewRow = 0; viewRow < seatInventoryTable->rowCount(); ++viewRow) {
            if (seatInventoryTable->item(viewRow, 0)
                && seatInventoryTable->item(viewRow, 0)->text() == seatType) {
                seatInventoryTable->selectRow(viewRow);
                seatInventoryTable->scrollToItem(seatInventoryTable->item(viewRow, 0));
                return;
            }
        }

        int templateRow = -1;
        QDate latestDate;
        for (int row = 0; row < ui->routeTable->rowCount(); ++row) {
            const QString rowNumber = ui->routeTable->item(row, Number)->text().toUpper();
            if (rowNumber != currentNumber
                || ui->routeTable->item(row, SeatType)->text() != seatType) continue;
            const QString rowKey = rowNumber + QChar('|')
                                   + ui->routeTable->item(row, DepartureDate)->text();
            if (rowKey == currentKey) continue;
            const QDate candidateDate = QDate::fromString(
                ui->routeTable->item(row, DepartureDate)->text(), QStringLiteral("yyyy-MM-dd"));
            if (templateRow < 0 || candidateDate > latestDate) {
                templateRow = row;
                latestDate = candidateDate;
            }
        }
        if (templateRow < 0) return;
        seatPriceEdit->setText(
            ui->routeTable->item(templateRow, Price)->text().remove(QChar(0x00A5)));
        seatCapacityEdit->setText(ui->routeTable->item(templateRow, TotalSeats)->text());
        seatPriceEdit->setToolTip(
            QStringLiteral("已参考 %1 的 %2 配置自动填充")
                .arg(latestDate.toString(QStringLiteral("yyyy-MM-dd")), seatType));
        seatCapacityEdit->setToolTip(seatPriceEdit->toolTip());
    };
    connect(seatTypeEditor, &QComboBox::currentTextChanged, this,
            [fillSeatFromHistory](const QString &) { fillSeatFromHistory(); });
    connect(seatServiceCombo, &QComboBox::currentIndexChanged, this,
            [this, fillSeatFromHistory](int) {
        // 切换班次时同时清掉表格的当前索引，保证自动填充不会被旧 currentRow 阻断。
        seatInventoryTable->clearSelection();
        seatInventoryTable->setCurrentCell(-1, -1);
        seatPriceEdit->clear();
        seatCapacityEdit->clear();
        seatPriceEdit->setToolTip(QString());
        seatCapacityEdit->setToolTip(QString());
        fillSeatFromHistory();
    });
    connect(addSeatButton, &QPushButton::clicked, this,
            [this, validateSeatInput] {
        if (!validateSeatInput()) return;
        const QString key = seatServiceCombo->currentData().toString();
        int baseRow = -1;
        int placeholderRow = -1;
        for (int row = 0; row < ui->routeTable->rowCount(); ++row) {
            const QString rowKey = ui->routeTable->item(row, Number)->text().toUpper()
                                   + QChar('|')
                                   + ui->routeTable->item(row, DepartureDate)->text();
            if (rowKey != key) continue;
            if (baseRow < 0) baseRow = row;
            if (ui->routeTable->item(row, SeatType)->text()
                    == seatTypeEditor->currentText()) {
                QMessageBox::warning(this, QStringLiteral("席位已存在"),
                                     QStringLiteral("该班次已经配置了这个席位类型。"));
                return;
            }
            if (ui->routeTable->item(row, SeatType)->text() == QStringLiteral("未配置")) {
                placeholderRow = row;
            }
        }
        if (baseRow < 0) return;
        const int targetRow = placeholderRow >= 0
                                  ? placeholderRow : ui->routeTable->rowCount();
        if (placeholderRow < 0) {
            ui->routeTable->insertRow(targetRow);
            for (int column = Number; column <= ArrivalTime; ++column) {
                ui->routeTable->setItem(
                    targetRow, column,
                    new QTableWidgetItem(ui->routeTable->item(baseRow, column)->text()));
            }
            for (int column = SeatType; column <= Remaining; ++column) {
                ui->routeTable->setItem(targetRow, column, new QTableWidgetItem);
            }
            ui->routeTable->setItem(
                targetRow, Status,
                new QTableWidgetItem(ui->routeTable->item(baseRow, Status)->text()));
        }
        const int capacity = seatCapacityEdit->text().toInt();
        ui->routeTable->item(targetRow, SeatType)->setText(seatTypeEditor->currentText());
        ui->routeTable->item(targetRow, Price)->setText(
            QStringLiteral("¥%1").arg(seatPriceEdit->text().toDouble(), 0, 'f', 2));
        ui->routeTable->item(targetRow, TotalSeats)->setText(QString::number(capacity));
        ui->routeTable->item(targetRow, Remaining)->setText(QString::number(capacity));
        ui->routeTable->item(targetRow, Number)->setData(SoldRole, 0);
        ui->routeTable->item(targetRow, Number)->setData(RevenueRole, 0.0);
        if (!saveBusinessState()) {
            QMessageBox::warning(this, QStringLiteral("保存失败"),
                                 QStringLiteral("数据未能写入本地存储，请检查系统权限。"));
        }
        updateStatistics();
        seatPriceEdit->clear();
        seatCapacityEdit->clear();
    });

    connect(saveSeatButton, &QPushButton::clicked, this,
            [this, validateSeatInput] {
        if (!validateSeatInput()) return;
        const int viewRow = seatInventoryTable->currentRow();
        if (viewRow < 0 || !seatInventoryTable->item(viewRow, 0)) return;
        const int sourceRow = seatInventoryTable->item(viewRow, 0)
                                  ->data(Qt::UserRole).toInt();
        const QString key = seatServiceCombo->currentData().toString();
        for (int row = 0; row < ui->routeTable->rowCount(); ++row) {
            const QString rowKey = ui->routeTable->item(row, Number)->text().toUpper()
                                   + QChar('|')
                                   + ui->routeTable->item(row, DepartureDate)->text();
            if (row != sourceRow && rowKey == key
                && ui->routeTable->item(row, SeatType)->text()
                       == seatTypeEditor->currentText()) {
                QMessageBox::warning(this, QStringLiteral("席位已存在"),
                                     QStringLiteral("该班次已经配置了这个席位类型。"));
                return;
            }
        }
        const int sold = ui->routeTable->item(sourceRow, Number)->data(SoldRole).toInt();
        const int capacity = seatCapacityEdit->text().toInt();
        if (capacity < sold) {
            QMessageBox::warning(this, QStringLiteral("座位数不足"),
                                 QStringLiteral("座位总数不能小于累计已售 %1 张。")
                                     .arg(sold));
            return;
        }
        ui->routeTable->item(sourceRow, SeatType)->setText(seatTypeEditor->currentText());
        ui->routeTable->item(sourceRow, Price)->setText(
            QStringLiteral("¥%1").arg(seatPriceEdit->text().toDouble(), 0, 'f', 2));
        ui->routeTable->item(sourceRow, TotalSeats)->setText(QString::number(capacity));
        ui->routeTable->item(sourceRow, Remaining)->setText(
            QString::number(capacity - sold));
        if (!saveBusinessState()) {
            QMessageBox::warning(this, QStringLiteral("保存失败"),
                                 QStringLiteral("数据未能写入本地存储，请检查系统权限。"));
        }
        updateStatistics();
    });

    connect(deleteSeatButton, &QPushButton::clicked, this, [this] {
        const int viewRow = seatInventoryTable->currentRow();
        if (viewRow < 0 || !seatInventoryTable->item(viewRow, 0)) return;
        const int sourceRow = seatInventoryTable->item(viewRow, 0)
                                  ->data(Qt::UserRole).toInt();
        const int sold = ui->routeTable->item(sourceRow, Number)->data(SoldRole).toInt();
        if (sold > 0) {
            QMessageBox::warning(this, QStringLiteral("无法删除席位"),
                                 QStringLiteral("该席位已有订单或销售记录，不能删除。"));
            return;
        }
        if (QMessageBox::question(this, QStringLiteral("确认删除席位"),
                                  QStringLiteral("确定删除所选席位库存吗？"),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes) return;
        const QString key = ui->routeTable->item(sourceRow, Number)->text().toUpper()
                            + QChar('|')
                            + ui->routeTable->item(sourceRow, DepartureDate)->text();
        int inventoryCount = 0;
        for (int row = 0; row < ui->routeTable->rowCount(); ++row) {
            const QString rowKey = ui->routeTable->item(row, Number)->text().toUpper()
                                   + QChar('|')
                                   + ui->routeTable->item(row, DepartureDate)->text();
            if (rowKey == key
                && ui->routeTable->item(row, SeatType)->text() != QStringLiteral("未配置")) {
                ++inventoryCount;
            }
        }
        if (inventoryCount <= 1) {
            ui->routeTable->item(sourceRow, SeatType)->setText(QStringLiteral("未配置"));
            ui->routeTable->item(sourceRow, Price)->setText(QStringLiteral("¥0.00"));
            ui->routeTable->item(sourceRow, TotalSeats)->setText(QStringLiteral("0"));
            ui->routeTable->item(sourceRow, Remaining)->setText(QStringLiteral("0"));
        } else {
            ui->routeTable->removeRow(sourceRow);
        }
        if (!saveBusinessState()) {
            QMessageBox::warning(this, QStringLiteral("保存失败"),
                                 QStringLiteral("数据未能写入本地存储，请检查系统权限。"));
        }
        updateStatistics();
    });

    // 六个业务页面在全部创建完成后统一注册，确保左侧导航顺序稳定。
    contentTabs->addTab(overviewPage, style()->standardIcon(QStyle::SP_ComputerIcon),
                        QStringLiteral("运营总览"));
    contentTabs->addTab(routePage, style()->standardIcon(QStyle::SP_FileDialogListView),
                        QStringLiteral("班次管理"));
    contentTabs->addTab(seatPage, style()->standardIcon(QStyle::SP_DriveHDIcon),
                        QStringLiteral("席位库存"));
    contentTabs->addTab(ticketPage, style()->standardIcon(QStyle::SP_DialogApplyButton),
                        QStringLiteral("售票中心"));
    contentTabs->addTab(orderPage, style()->standardIcon(QStyle::SP_FileIcon),
                        QStringLiteral("订单管理"));
    contentTabs->addTab(transactionPage,
                        style()->standardIcon(QStyle::SP_FileDialogDetailedView),
                        QStringLiteral("交易流水"));
    contentTabs->setCurrentIndex(0);
    ui->verticalLayout->setStretchFactor(contentTabs, 1);
    connect(manageShortcut, &QPushButton::clicked, this,
            [this] { contentTabs->setCurrentIndex(1); });
    connect(ticketShortcut, &QPushButton::clicked, this,
            [this] { contentTabs->setCurrentIndex(3); });
    connect(historyShortcut, &QPushButton::clicked, this,
            [this] { contentTabs->setCurrentIndex(5); });
    connect(backupDataButton, &QPushButton::clicked,
            this, &Widget::backupBusinessData);
    connect(restoreDataButton, &QPushButton::clicked,
            this, &Widget::restoreBusinessData);
    connect(healthCheckButton, &QPushButton::clicked,
            this, &Widget::runDataHealthCheck);

    sellTicketButton = new QPushButton(QStringLiteral("确认售票"), ticketPanel);
    sellTicketButton->setObjectName(QStringLiteral("sellTicketButton"));
    sellTicketButton->setEnabled(false);
    operationLayout->addWidget(quantityLabel);
    operationLayout->addWidget(decreaseQuantityButton);
    operationLayout->addWidget(ticketQuantitySpin);
    operationLayout->addWidget(increaseQuantityButton);
    operationLayout->addWidget(sellTicketButton);
    operationLayout->addStretch();

    ticketPanelLayout->addWidget(ticketTitle);
    ticketPanelLayout->addWidget(selectedRouteLabel);
    ticketPanelLayout->addLayout(operationLayout);
    ticketPageLayout->addWidget(ticketPanel);
    ticketPageLayout->addStretch();

    // 切换班次时数量重置为 1，防止把上一班次的交易数量误用于下一班次。
    connect(ui->routeTable, &QTableWidget::itemSelectionChanged, this,
            [this] {
        ticketQuantitySpin->setValue(1);
        const int row = ui->routeTable->selectedItems().isEmpty()
                            ? -1 : ui->routeTable->currentRow();
        updateTicketSelection(row);
        const bool selected = row >= 0 && ui->routeTable->item(row, Status);
        toggleRouteStatusButton->setEnabled(selected);
        if (selected) {
            toggleRouteStatusButton->setText(
                ui->routeTable->item(row, Status)->text() == QStringLiteral("停用")
                    ? QStringLiteral("启用班次") : QStringLiteral("停用班次"));
        }
    });
    connect(sellTicketButton, &QPushButton::clicked,
            this, &Widget::sellTickets);

    connect(transactionSearchEdit, &QLineEdit::textChanged,
            this, &Widget::filterTransactions);
    connect(exportTransactionButton, &QPushButton::clicked,
            this, &Widget::exportTransactions);
    connect(clearHistoryButton, &QPushButton::clicked, this, [this] {
        if (transactionTable->rowCount() == 0) return;
        if (QMessageBox::question(
                this, QStringLiteral("确认清空流水"),
                QStringLiteral("这只会清空交易流水，不会修改班次余票和经营统计。\n"
                               "确定继续吗？"),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No) == QMessageBox::Yes) {
            transactionTable->setRowCount(0);
            if (!saveBusinessState()) {
                QMessageBox::warning(this, QStringLiteral("保存失败"),
                                     QStringLiteral("流水变更未能写入本地存储。"));
            }
        }
    });

    // 订单选中状态与退票、改签按钮联动，只有已出票订单可以办理。
    connect(orderTable, &QTableWidget::itemSelectionChanged, this, [this] {
        const int row = orderTable->currentRow();
        const bool operable = row >= 0 && !isOrderDetailRow(orderTable, row)
                              && orderTable->item(row, OrderStatus)
                              && orderTable->item(row, OrderStatus)->text()
                                     == QStringLiteral("已出票");
        refundOrderButton->setEnabled(operable);
        rescheduleOrderButton->setEnabled(operable);
    });
    connect(refundOrderButton, &QPushButton::clicked,
            this, &Widget::refundSelectedOrder);
    connect(rescheduleOrderButton, &QPushButton::clicked,
            this, &Widget::rescheduleSelectedOrder);

    // 新增站点写入站点字典，并立即刷新两个可搜索下拉框。
    connect(addStationButton, &QPushButton::clicked, this, [this] {
        bool accepted = false;
        const QString station = QInputDialog::getText(
            this, QStringLiteral("新增站点"), QStringLiteral("站点名称："),
            QLineEdit::Normal, QString(), &accepted).trimmed();
        if (!accepted || station.isEmpty()) return;
        if (stationNames.contains(station, Qt::CaseInsensitive)) {
            QMessageBox::information(this, QStringLiteral("站点已存在"),
                                     QStringLiteral("站点字典中已经存在该站点。"));
            return;
        }
        stationNames.append(station);
        // 使用中文区域的校对规则，使站点按拼音字典序排列。
        QCollator stationCollator(QLocale(QLocale::Chinese, QLocale::China));
        stationCollator.setCaseSensitivity(Qt::CaseInsensitive);
        std::sort(stationNames.begin(), stationNames.end(),
                  [&stationCollator](const QString &left, const QString &right) {
                      return stationCollator.compare(left, right) < 0;
                  });
        if (!saveBusinessState()) {
            QMessageBox::warning(this, QStringLiteral("保存失败"),
                                 QStringLiteral("站点字典未能写入本地存储。"));
        }
        loadStations();
    });

    // 班次逻辑停用：保留历史订单和流水，仅从售票选择器中移除。
    connect(toggleRouteStatusButton, &QPushButton::clicked, this, [this] {
        const int selectedRow = ui->routeTable->currentRow();
        if (selectedRow < 0) return;
        const QString number = ui->routeTable->item(selectedRow, Number)->text();
        const QString date = ui->routeTable->item(selectedRow, DepartureDate)->text();
        const bool disable = ui->routeTable->item(selectedRow, Status)->text()
                             != QStringLiteral("停用");
        for (int row = 0; row < ui->routeTable->rowCount(); ++row) {
            if (ui->routeTable->item(row, Number)->text() == number
                && ui->routeTable->item(row, DepartureDate)->text() == date) {
                ui->routeTable->item(row, Status)->setText(
                    disable ? QStringLiteral("停用") : QStringLiteral("启用"));
                if (!disable) {
                    // 重新启用时清除停运警示色，库存颜色会由统计刷新重新计算。
                    for (int column = 0; column < ColumnCount; ++column) {
                        ui->routeTable->item(row, column)->setBackground(QBrush());
                        ui->routeTable->item(row, column)->setForeground(QBrush());
                    }
                }
            }
        }
        if (!saveBusinessState()) {
            QMessageBox::warning(this, QStringLiteral("保存失败"),
                                 QStringLiteral("数据未能写入本地存储，请检查系统权限。"));
        }
        refreshScheduleRows();
        refreshTicketRouteChoices();
        refreshSeatInventoryPage();
        updateStatistics();
        toggleRouteStatusButton->setText(
            disable ? QStringLiteral("启用班次") : QStringLiteral("停用班次"));
    });

// ==================== 按钮图标与交互提示 ====================
    // 使用 Qt 标准图标，不依赖外部图片，并保留所有现有 objectName。
    ui->searchButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogContentsView));
    ui->showAllButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    ui->addRouteButton->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
    ui->saveEditButton->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    ui->editRouteButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    ui->deleteRouteButton->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    sellTicketButton->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));

    ui->searchButton->setToolTip(QStringLiteral("按条件筛选班次"));
    ui->showAllButton->setToolTip(QStringLiteral("清除条件并显示全部班次"));
    sellTicketButton->setToolTip(QStringLiteral("为当前选中班次售票"));

    // 全局视觉样式由 main.cpp 从 :/bus_ticket_system/styles/app.qss 统一加载。

    const auto clearInputs = [this, dateEdit, timeEdit, arrivalTimeEdit,
                              arrivalDayCombo] {
        ui->routeNumberEdit->clear();
        dateEdit->setDate(QDate::currentDate());
        departureStationCombo->setCurrentIndex(-1);
        destinationStationCombo->setCurrentIndex(-1);
        departureStationCombo->lineEdit()->clear();
        destinationStationCombo->lineEdit()->clear();
        timeEdit->clear();
        arrivalTimeEdit->clear();
        arrivalDayCombo->setCurrentIndex(0);
        ui->routeNumberEdit->setFocus();
    };

    const auto readInputs = [this, dateEdit, timeEdit, arrivalTimeEdit,
                             arrivalDayCombo] {
        // 次日到达时在时间前加“次日”前缀写入表格，所有展示处自动保持一致。
        const QString arrival = arrivalTimeEdit->text().trimmed();
        return QStringList{ui->routeNumberEdit->text().trimmed(),
                           dateEdit->date().toString(QStringLiteral("yyyy-MM-dd")),
                           departureStationCombo->currentText().trimmed(),
                           destinationStationCombo->currentText().trimmed(),
                           timeEdit->text().trimmed(),
                           arrivalDayCombo->currentIndex() == 1
                               ? QStringLiteral("次日 ") + arrival
                               : arrival};
    };

    // ==================== V0.14 历史班次模板自动填充 ====================
    // 输入完整车次号后查找该车次最近一次记录，只复用路线和发到时刻，保留当前新日期。
    connect(ui->routeNumberEdit, &QLineEdit::textEdited, this,
            [this, timeEdit, arrivalTimeEdit, arrivalDayCombo](const QString &input) {
        if (editingRow >= 0) return;
        const QString number = input.trimmed();
        if (number.isEmpty()) return;
        int templateRow = -1;
        QDate latestDate;
        for (int row = 0; row < ui->routeTable->rowCount(); ++row) {
            if (ui->routeTable->item(row, Number)->text().compare(number, Qt::CaseInsensitive) != 0)
                continue;
            const QDate candidateDate = QDate::fromString(
                ui->routeTable->item(row, DepartureDate)->text(), QStringLiteral("yyyy-MM-dd"));
            if (templateRow < 0 || candidateDate > latestDate) {
                templateRow = row;
                latestDate = candidateDate;
            }
        }
        if (templateRow < 0) {
            ui->routeNumberEdit->setToolTip(QString());
            return;
        }
        departureStationCombo->setCurrentText(ui->routeTable->item(templateRow, Departure)->text());
        destinationStationCombo->setCurrentText(ui->routeTable->item(templateRow, Destination)->text());
        timeEdit->setText(ui->routeTable->item(templateRow, DepartureTime)->text());
        const QString storedArrival = ui->routeTable->item(templateRow, ArrivalTime)->text();
        const bool nextDay = storedArrival.startsWith(QStringLiteral("次日 "));
        arrivalTimeEdit->setText(nextDay ? storedArrival.mid(QStringLiteral("次日 ").size()) : storedArrival);
        arrivalDayCombo->setCurrentIndex(nextDay ? 1 : 0);
        ui->routeNumberEdit->setToolTip(
            QStringLiteral("已参考 %1 的历史班次资料自动填充；发车日期保持不变")
                .arg(latestDate.toString(QStringLiteral("yyyy-MM-dd"))));
    });
    // ---------- 输入校验 ----------
    // 检查必填项、时间格式、票价和余票，错误时弹窗并阻止继续操作。
    const auto validate = [this](const QStringList &values) {
        for (const QString &value : values) {
            if (value.isEmpty()) {
                QMessageBox::warning(
                    this, QStringLiteral("输入不完整"),
                    QStringLiteral("请填写车次号、日期、出发地、目的地、发车时间和到达时间。"));
                return false;
            }
        }
        const QTime departureTime =
            QTime::fromString(values.at(DepartureTime), QStringLiteral("HH:mm"));
        if (!departureTime.isValid()) {
            QMessageBox::warning(this, QStringLiteral("时间格式错误"),
                                 QStringLiteral("发车时间请按 HH:mm 输入，例如 08:30。"));
            return false;
        }
        // 到达时间可能带“次日”前缀；跨天班次只校验时间本身，不比较先后。
        const bool nextDayArrival =
            values.at(ArrivalTime).startsWith(QStringLiteral("次日 "));
        const QString arrivalText =
            nextDayArrival
                ? values.at(ArrivalTime).mid(QStringLiteral("次日 ").size())
                : values.at(ArrivalTime);
        const QTime arrivalTime =
            QTime::fromString(arrivalText, QStringLiteral("HH:mm"));
        if (!arrivalTime.isValid()) {
            QMessageBox::warning(this, QStringLiteral("时间格式错误"),
                                 QStringLiteral("到达时间请按 HH:mm 输入，例如 12:30。"));
            return false;
        }
        // 当日到达要求到达晚于发车；次日到达允许任意时刻（例如 23:00 发车、次日 06:00 到达）。
        if (!nextDayArrival && arrivalTime <= departureTime) {
            QMessageBox::warning(this, QStringLiteral("时间顺序有误"),
                                 QStringLiteral("到达时间必须晚于发车时间。"));
            return false;
        }
        if (!stationNames.contains(values.at(Departure), Qt::CaseInsensitive)
            || !stationNames.contains(values.at(Destination), Qt::CaseInsensitive)) {
            QMessageBox::warning(this, QStringLiteral("站点不存在"),
                                 QStringLiteral("出发地和目的地必须从站点字典中选择。"));
            return false;
        }
        if (values.at(Departure).compare(values.at(Destination), Qt::CaseInsensitive) == 0) {
            QMessageBox::warning(this, QStringLiteral("站点重复"),
                                 QStringLiteral("出发地和目的地不能相同。"));
            return false;
        }
        return true;
    };

    connect(ui->addRouteButton, &QPushButton::clicked, this,
            [this, clearInputs, readInputs, validate] {
        const QStringList values = readInputs();
        if (!validate(values)) return;

        for (int row = 0; row < ui->routeTable->rowCount(); ++row) {
            const bool sameNumber = ui->routeTable->item(row, Number)->text()
                .compare(values.at(Number), Qt::CaseInsensitive) == 0;
            const bool sameDate = ui->routeTable->item(row, DepartureDate)->text()
                                  == values.at(DepartureDate);
            if (sameNumber && sameDate) {
                QMessageBox::warning(this, QStringLiteral("班次已存在"),
                                     QStringLiteral("同一车次号和发车日期只能创建一个班次。"));
                return;
            }
        }

        // ==================== V0.15 整套席位模板继承 ====================
        // 查找同车次最近一期已配置席位的班次，新日期一次性继承全部席位类型、票价和容量。
        QDate templateDate;
        for (int sourceRow = 0; sourceRow < ui->routeTable->rowCount(); ++sourceRow) {
            if (ui->routeTable->item(sourceRow, Number)->text()
                    .compare(values.at(Number), Qt::CaseInsensitive) != 0
                || ui->routeTable->item(sourceRow, SeatType)->text() == QStringLiteral("未配置")) {
                continue;
            }
            const QDate candidateDate = QDate::fromString(
                ui->routeTable->item(sourceRow, DepartureDate)->text(), QStringLiteral("yyyy-MM-dd"));
            if (candidateDate.isValid() && (!templateDate.isValid() || candidateDate > templateDate))
                templateDate = candidateDate;
        }

        QList<int> templateRows;
        if (templateDate.isValid()) {
            for (int sourceRow = 0; sourceRow < ui->routeTable->rowCount(); ++sourceRow) {
                if (ui->routeTable->item(sourceRow, Number)->text()
                        .compare(values.at(Number), Qt::CaseInsensitive) == 0
                    && ui->routeTable->item(sourceRow, DepartureDate)->text()
                           == templateDate.toString(QStringLiteral("yyyy-MM-dd"))
                    && ui->routeTable->item(sourceRow, SeatType)->text() != QStringLiteral("未配置")) {
                    templateRows.append(sourceRow);
                }
            }
        }

        const auto appendServiceRow = [this, &values](const QString &seatType,
                                                       const QString &price,
                                                       const QString &capacity) {
            const int targetRow = ui->routeTable->rowCount();
            ui->routeTable->insertRow(targetRow);
            for (int column = 0; column <= ArrivalTime; ++column)
                ui->routeTable->setItem(targetRow, column, new QTableWidgetItem(values.at(column)));
            const int totalSeats = capacity.toInt();
            ui->routeTable->setItem(targetRow, SeatType, new QTableWidgetItem(seatType));
            ui->routeTable->setItem(targetRow, Price, new QTableWidgetItem(price));
            ui->routeTable->setItem(targetRow, TotalSeats, new QTableWidgetItem(QString::number(totalSeats)));
            ui->routeTable->setItem(targetRow, Remaining, new QTableWidgetItem(QString::number(totalSeats)));
            ui->routeTable->setItem(targetRow, Status, new QTableWidgetItem(QStringLiteral("启用")));
            // 只继承配置，不继承历史销量、收入、余票或已占座位。
            ui->routeTable->item(targetRow, Number)->setData(SoldRole, 0);
            ui->routeTable->item(targetRow, Number)->setData(RevenueRole, 0.0);
        };

        if (templateRows.isEmpty()) {
            appendServiceRow(QStringLiteral("未配置"), QStringLiteral("¥0.00"), QStringLiteral("0"));
        } else {
            for (const int sourceRow : templateRows) {
                appendServiceRow(ui->routeTable->item(sourceRow, SeatType)->text(),
                                 ui->routeTable->item(sourceRow, Price)->text(),
                                 ui->routeTable->item(sourceRow, TotalSeats)->text());
            }
        }
        clearInputs();
        if (!saveBusinessState()) {
            QMessageBox::warning(this, QStringLiteral("保存失败"),
                                 QStringLiteral("数据未能写入本地存储，请检查系统权限。"));
        }
        updateStatistics();
    });

    // 历史关联检查：只要订单或流水中出现过该班次，就不允许物理删除或修改身份字段。
    const auto serviceHasHistory = [this](const QString &number, const QString &date) {
        for (int row = 0; row < orderTable->rowCount(); ++row) {
            if (!isOrderDetailRow(orderTable, row)
                && orderTable->item(row, OrderRoute) && orderTable->item(row, OrderDate)
                && orderTable->item(row, OrderRoute)->text().compare(number, Qt::CaseInsensitive) == 0
                && orderTable->item(row, OrderDate)->text() == date) return true;
        }
        for (int row = 0; row < transactionTable->rowCount(); ++row) {
            if (transactionTable->item(row, 2) && transactionTable->item(row, 3)
                && transactionTable->item(row, 2)->text().compare(number, Qt::CaseInsensitive) == 0
                && transactionTable->item(row, 3)->text().startsWith(date)) return true;
        }
        return false;
    };

    connect(ui->editRouteButton, &QPushButton::clicked, this,
            [this, dateEdit, timeEdit, arrivalTimeEdit, arrivalDayCombo,
             serviceHasHistory] {
        const int row = ui->routeTable->currentRow();
        if (row < 0) {
            QMessageBox::information(this, QStringLiteral("提示"),
                                     QStringLiteral("请先选择要修改的班次。"));
            return;
        }
        const QString number = ui->routeTable->item(row, Number)->text();
        const QString date = ui->routeTable->item(row, DepartureDate)->text();
        if (serviceHasHistory(number, date)) {
            QMessageBox::information(
                this, QStringLiteral("班次已产生历史记录"),
                QStringLiteral("该班次已有订单或交易流水。为保证历史关联，不能修改班次身份信息；如停止销售，请使用“停用班次”。"));
            return;
        }
        editingRow = row;
        ui->routeNumberEdit->setText(number);
        dateEdit->setDate(QDate::fromString(
            ui->routeTable->item(row, DepartureDate)->text(),
            QStringLiteral("yyyy-MM-dd")));
        departureStationCombo->setCurrentText(ui->routeTable->item(row, Departure)->text());
        destinationStationCombo->setCurrentText(ui->routeTable->item(row, Destination)->text());
        timeEdit->setText(ui->routeTable->item(row, DepartureTime)->text());
        // 回填到达时间时拆分“次日”前缀并恢复下拉，保证编辑前后信息一致。
        const QString storedArrival = ui->routeTable->item(row, ArrivalTime)->text();
        const bool storedNextDay = storedArrival.startsWith(QStringLiteral("次日 "));
        arrivalTimeEdit->setText(storedNextDay
                                     ? storedArrival.mid(QStringLiteral("次日 ").size())
                                     : storedArrival);
        arrivalDayCombo->setCurrentIndex(storedNextDay ? 1 : 0);
        ui->saveEditButton->setEnabled(true);
        ui->addRouteButton->setEnabled(false);
        ui->editRouteButton->setEnabled(false);
        ui->deleteRouteButton->setEnabled(false);
        ui->routeNumberEdit->setFocus();
    });

    connect(ui->saveEditButton, &QPushButton::clicked, this,
            [this, clearInputs, readInputs, validate] {
        if (editingRow < 0 || editingRow >= ui->routeTable->rowCount()) return;
        const QStringList values = readInputs();
        if (!validate(values)) return;

        const QString oldNumber = ui->routeTable->item(editingRow, Number)->text();
        const QString oldDate = ui->routeTable->item(editingRow, DepartureDate)->text();

        for (int row = 0; row < ui->routeTable->rowCount(); ++row) {
            const bool sameNumber = ui->routeTable->item(row, Number)->text()
                .compare(values.at(Number), Qt::CaseInsensitive) == 0;
            const bool sameDate = ui->routeTable->item(row, DepartureDate)->text()
                                  == values.at(DepartureDate);
            const bool belongsToEditingService =
                ui->routeTable->item(row, Number)->text() == oldNumber
                && ui->routeTable->item(row, DepartureDate)->text() == oldDate;
            if (!belongsToEditingService && sameNumber && sameDate) {
                QMessageBox::warning(this, QStringLiteral("班次已存在"),
                                     QStringLiteral("同一车次号和发车日期只能创建一个班次。"));
                return;
            }
        }

        // 修改班次时同步更新其全部席位库存记录的基础字段。
        for (int row = 0; row < ui->routeTable->rowCount(); ++row) {
            if (ui->routeTable->item(row, Number)->text() != oldNumber
                || ui->routeTable->item(row, DepartureDate)->text() != oldDate) {
                continue;
            }
            for (int column = 0; column <= ArrivalTime; ++column) {
                ui->routeTable->item(row, column)->setText(values.at(column));
            }
        }

        editingRow = -1;
        clearInputs();
        ui->saveEditButton->setEnabled(false);
        ui->addRouteButton->setEnabled(true);
        ui->editRouteButton->setEnabled(true);
        ui->deleteRouteButton->setEnabled(true);
        ui->routeTable->clearSelection();
        if (!saveBusinessState()) {
            QMessageBox::warning(this, QStringLiteral("保存失败"),
                                 QStringLiteral("数据未能写入本地存储，请检查系统权限。"));
        }
        updateStatistics();
        updateTicketSelection(editingRow);
        QMessageBox::information(this, QStringLiteral("修改成功"),
                                 QStringLiteral("班次信息已经更新。"));
    });

    connect(ui->deleteRouteButton, &QPushButton::clicked, this, [this, serviceHasHistory] {
        const int row = ui->routeTable->currentRow();
        if (row < 0) {
            QMessageBox::information(this, QStringLiteral("提示"),
                                     QStringLiteral("请先选择要删除的班次。"));
            return;
        }
        const QString number = ui->routeTable->item(row, Number)->text();
        const QString date = ui->routeTable->item(row, DepartureDate)->text();
        if (serviceHasHistory(number, date)) {
            QMessageBox::warning(
                this, QStringLiteral("无法删除班次"),
                QStringLiteral("该班次已有历史订单或交易流水，不能物理删除。请使用“停用班次”保留历史关联。"));
            return;
        }
        if (QMessageBox::question(
                this, QStringLiteral("确认删除"),
                QStringLiteral("确定删除班次“%1　%2”及其全部席位配置吗？")
                    .arg(number, date),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No) == QMessageBox::Yes) {
            for (int sourceRow = ui->routeTable->rowCount() - 1;
                 sourceRow >= 0; --sourceRow) {
                if (ui->routeTable->item(sourceRow, Number)->text() == number
                    && ui->routeTable->item(sourceRow, DepartureDate)->text() == date) {
                    ui->routeTable->removeRow(sourceRow);
                }
            }
            if (!saveBusinessState()) {
            QMessageBox::warning(this, QStringLiteral("保存失败"),
                                 QStringLiteral("数据未能写入本地存储，请检查系统权限。"));
        }
            updateStatistics();
            updateTicketSelection(-1);
        }
    });

    const auto search = [this] {
        ui->routeTable->clearSelection();
        toggleRouteStatusButton->setEnabled(false);
        const QString keyword = ui->searchEdit->text().trimmed();
        int matches = 0;
        QSet<QString> countedKeys;
        for (int row = 0; row < ui->routeTable->rowCount(); ++row) {
            bool matched = keyword.isEmpty();
            for (int column = 0; !matched && column <= ArrivalTime; ++column) {
                const auto *item = ui->routeTable->item(row, column);
                matched = item && item->text().contains(keyword, Qt::CaseInsensitive);
            }
            const QString key = ui->routeTable->item(row, Number)->text().toUpper()
                                + QChar('|')
                                + ui->routeTable->item(row, DepartureDate)->text();
            const bool duplicate = countedKeys.contains(key);
            ui->routeTable->setRowHidden(row, duplicate || !matched);
            if (!duplicate && matched) ++matches;
            countedKeys.insert(key);
        }
        if (!keyword.isEmpty() && matches == 0) {
            QMessageBox::information(this, QStringLiteral("搜索结果"),
                                     QStringLiteral("没有找到匹配的班次。"));
        }
    };

    connect(ui->searchButton, &QPushButton::clicked, this, search);
    connect(ui->searchEdit, &QLineEdit::returnPressed, this, search);
    connect(ui->showAllButton, &QPushButton::clicked, this, [this, search] {
        ui->searchEdit->clear();
        search();
        ui->routeTable->clearSelection();
    });

    // 窗口启动的最后一步：恢复班次、审计流水和 V0.11 实名订单。
    loadRoutes();
    loadTransactions();
    loadOrders();
    updateStatistics();
    updateTicketSelection(ui->routeTable->currentRow());
    // 记录启动完成后的可靠快照，并把首次生成的默认站点同步到 SQLite。
    lastSavedSettings = captureSettingsSnapshot();
    if (databaseReady) {
        QString databaseError;
        if (!DatabaseManager::instance().replaceAll(lastSavedSettings, &databaseError)) {
            databaseReady = false;
            QMessageBox::warning(this, QStringLiteral("数据库同步失败"), databaseError);
        } else if (systemBadge) {
            systemBadge->setToolTip(QStringLiteral("SQLite 数据库：%1")
                                        .arg(DatabaseManager::instance().databasePath()));
        }
    }
}

// ==================== 班次时间排序 ====================
// 三个业务页面共用 routeTable 数据源；先按发车日期、发车时间、班次号和席位稳定排序，
// 再刷新班次管理、席位库存和售票中心，保证各页面看到的顺序完全一致。
void Widget::sortRoutesChronologically()
{
    const int rowCount = ui->routeTable->rowCount();
    if (rowCount < 2) return;

    QString selectedKey;
    const int selectedRow = ui->routeTable->currentRow();
    if (selectedRow >= 0) {
        selectedKey = ui->routeTable->item(selectedRow, Number)->text().toUpper()
                      + QChar('|') + ui->routeTable->item(selectedRow, DepartureDate)->text()
                      + QChar('|') + ui->routeTable->item(selectedRow, SeatType)->text();
    }

    QList<QList<QTableWidgetItem *>> rows;
    rows.reserve(rowCount);
    for (int row = 0; row < rowCount; ++row) {
        QList<QTableWidgetItem *> items;
        items.reserve(ColumnCount);
        for (int column = 0; column < ColumnCount; ++column)
            items.append(ui->routeTable->takeItem(row, column));
        rows.append(items);
    }
    std::stable_sort(rows.begin(), rows.end(), [](const auto &left, const auto &right) {
        const QString leftKey = left.at(DepartureDate)->text()
                                + QChar('|') + left.at(DepartureTime)->text()
                                + QChar('|') + left.at(Number)->text().toUpper()
                                + QChar('|') + left.at(SeatType)->text();
        const QString rightKey = right.at(DepartureDate)->text()
                                 + QChar('|') + right.at(DepartureTime)->text()
                                 + QChar('|') + right.at(Number)->text().toUpper()
                                 + QChar('|') + right.at(SeatType)->text();
        return leftKey < rightKey;
    });

    const QSignalBlocker blocker(ui->routeTable);
    ui->routeTable->setRowCount(0);
    for (int row = 0; row < rows.size(); ++row) {
        ui->routeTable->insertRow(row);
        for (int column = 0; column < ColumnCount; ++column)
            ui->routeTable->setItem(row, column, rows.at(row).at(column));
    }
    if (!selectedKey.isEmpty()) {
        for (int row = 0; row < ui->routeTable->rowCount(); ++row) {
            const QString key = ui->routeTable->item(row, Number)->text().toUpper()
                                + QChar('|') + ui->routeTable->item(row, DepartureDate)->text()
                                + QChar('|') + ui->routeTable->item(row, SeatType)->text();
            if (key == selectedKey) {
                ui->routeTable->selectRow(row);
                break;
            }
        }
    }
}

// ==================== V0.10 售票中心班次选择器 ====================
// 从已按时间排序的班次主表刷新下拉选项，并尽量保留当前选择。
void Widget::refreshTicketRouteChoices()
{
    if (!ticketRouteCombo) return;

    const QString previousKey = ticketRouteCombo->currentData().toString();
    const QSignalBlocker blocker(ticketRouteCombo);
    ticketRouteCombo->clear();
    QSet<QString> addedKeys;
    for (int row = 0; row < ui->routeTable->rowCount(); ++row) {
        QString unavailableReason;
        if (!isRouteSellable(row, &unavailableReason)) continue;
        const QString key = ui->routeTable->item(row, Number)->text().toUpper()
                            + QChar('|')
                            + ui->routeTable->item(row, DepartureDate)->text();
        if (addedKeys.contains(key)) continue;
        addedKeys.insert(key);
        const QString summary =
            QStringLiteral("%1　%2　%3 → %4　%5-%6")
                .arg(ui->routeTable->item(row, Number)->text(),
                     ui->routeTable->item(row, DepartureDate)->text(),
                     ui->routeTable->item(row, Departure)->text(),
                     ui->routeTable->item(row, Destination)->text(),
                     ui->routeTable->item(row, DepartureTime)->text(),
                     ui->routeTable->item(row, ArrivalTime)->text());
        ticketRouteCombo->addItem(summary, key);
    }
    int selectedIndex = ticketRouteCombo->findData(previousKey);
    if (selectedIndex < 0 && ticketRouteCombo->count() > 0) selectedIndex = 0;
    ticketRouteCombo->setCurrentIndex(selectedIndex);
    refreshTicketSeatChoices();
}

// 售票页第二级选择器：仅列出当前班次已经配置好的席位库存。
void Widget::refreshTicketSeatChoices()
{
    if (!ticketSeatCombo || !ticketRouteCombo) return;
    const QString serviceKey = ticketRouteCombo->currentData().toString();
    const int previousRow = ticketSeatCombo->currentData().toInt();
    const QSignalBlocker blocker(ticketSeatCombo);
    ticketSeatCombo->clear();
    for (int row = 0; row < ui->routeTable->rowCount(); ++row) {
        const QString key = ui->routeTable->item(row, Number)->text().toUpper()
                            + QChar('|')
                            + ui->routeTable->item(row, DepartureDate)->text();
        if (key != serviceKey
            || ui->routeTable->item(row, Status)->text() == QStringLiteral("停用")
            || ui->routeTable->item(row, SeatType)->text() == QStringLiteral("未配置")) {
            continue;
        }
        ticketSeatCombo->addItem(
            QStringLiteral("%1　%2　余票 %3")
                .arg(ui->routeTable->item(row, SeatType)->text(),
                     ui->routeTable->item(row, Price)->text(),
                     ui->routeTable->item(row, Remaining)->text()), row);
    }
    int index = ticketSeatCombo->findData(previousRow);
    if (index < 0 && ticketSeatCombo->count() > 0) index = 0;
    ticketSeatCombo->setCurrentIndex(index);
    ticketSeatCombo->setPlaceholderText(
        ticketSeatCombo->count() == 0
            ? QStringLiteral("该班次尚未配置席位")
            : QStringLiteral("请选择席位"));
}

// 班次管理合并同一车次和日期的多条席位记录，只展示一条班次基础信息。
void Widget::refreshScheduleRows()
{
    QSet<QString> visibleKeys;
    const QString keyword = ui->searchEdit->text().trimmed();
    for (int row = 0; row < ui->routeTable->rowCount(); ++row) {
        const QString key = ui->routeTable->item(row, Number)->text().toUpper()
                            + QChar('|')
                            + ui->routeTable->item(row, DepartureDate)->text();
        const bool duplicate = visibleKeys.contains(key);
        bool matched = keyword.isEmpty();
        for (int column = 0; !matched && column <= ArrivalTime; ++column) {
            matched = ui->routeTable->item(row, column)->text()
                          .contains(keyword, Qt::CaseInsensitive);
        }
        ui->routeTable->setRowHidden(row, duplicate || !matched);
        // V0.14 班次时效颜色：停运用红色，已过发车时间用浅玫红整行警示。
        const bool stopped = ui->routeTable->item(row, Status)->text() == QStringLiteral("停用");
        const QDate routeDate = QDate::fromString(
            ui->routeTable->item(row, DepartureDate)->text(), QStringLiteral("yyyy-MM-dd"));
        const QTime routeTime = QTime::fromString(
            ui->routeTable->item(row, DepartureTime)->text(), QStringLiteral("HH:mm"));
        const bool departed = routeDate.isValid() && routeTime.isValid()
                              && QDateTime(routeDate, routeTime) <= QDateTime::currentDateTime();
        const QColor rowBackground = stopped ? QColor(QStringLiteral("#fee2e2"))
                                   : departed ? QColor(QStringLiteral("#ffe4e6")) : QColor();
        const QColor rowForeground = stopped ? QColor(QStringLiteral("#b4232f"))
                                   : departed ? QColor(QStringLiteral("#9f1239")) : QColor();
        for (int column = 0; column < ColumnCount; ++column) {
            auto *item = ui->routeTable->item(row, column);
            item->setBackground(rowBackground.isValid() ? QBrush(rowBackground) : QBrush());
            if (stopped || departed) item->setForeground(rowForeground);
        }
        auto *statusItem = ui->routeTable->item(row, Status);
        QFont statusFont = statusItem->font();
        statusFont.setBold(stopped || departed);
        statusItem->setFont(statusFont);
        statusItem->setToolTip(departed && !stopped
            ? QStringLiteral("该班次已于 %1 %2 发车，当前不可售票或改签")
                  .arg(routeDate.toString(QStringLiteral("yyyy-MM-dd")), routeTime.toString(QStringLiteral("HH:mm")))
            : QString());
        visibleKeys.insert(key);
    }
}

// 席位库存页：按班次筛选底层库存记录，并展示票价、容量、销量和余票。
void Widget::refreshSeatInventoryPage()
{
    if (!seatServiceCombo || !seatInventoryTable) return;
    const QString previousKey = seatServiceCombo->currentData().toString();
    {
        const QSignalBlocker blocker(seatServiceCombo);
        seatServiceCombo->clear();
        QSet<QString> addedKeys;
        for (int row = 0; row < ui->routeTable->rowCount(); ++row) {
            const QString key = ui->routeTable->item(row, Number)->text().toUpper()
                                + QChar('|')
                                + ui->routeTable->item(row, DepartureDate)->text();
            if (addedKeys.contains(key)) continue;
            addedKeys.insert(key);
            seatServiceCombo->addItem(
                QStringLiteral("%1　%2　%3 → %4　%5-%6")
                    .arg(ui->routeTable->item(row, Number)->text(),
                         ui->routeTable->item(row, DepartureDate)->text(),
                         ui->routeTable->item(row, Departure)->text(),
                         ui->routeTable->item(row, Destination)->text(),
                         ui->routeTable->item(row, DepartureTime)->text(),
                         ui->routeTable->item(row, ArrivalTime)->text()), key);
        }
        int index = seatServiceCombo->findData(previousKey);
        if (index < 0 && seatServiceCombo->count() > 0) index = 0;
        seatServiceCombo->setCurrentIndex(index);
    }

    const QString serviceKey = seatServiceCombo->currentData().toString();
    seatInventoryTable->setRowCount(0);
    for (int sourceRow = 0; sourceRow < ui->routeTable->rowCount(); ++sourceRow) {
        const QString key = ui->routeTable->item(sourceRow, Number)->text().toUpper()
                            + QChar('|')
                            + ui->routeTable->item(sourceRow, DepartureDate)->text();
        if (key != serviceKey
            || ui->routeTable->item(sourceRow, SeatType)->text() == QStringLiteral("未配置")) {
            continue;
        }
        const int row = seatInventoryTable->rowCount();
        seatInventoryTable->insertRow(row);
        const int sold = ui->routeTable->item(sourceRow, Number)->data(SoldRole).toInt();
        const int remaining = ui->routeTable->item(sourceRow, Remaining)->text().toInt();
        const QString status = remaining == 0 ? QStringLiteral("售罄")
                               : remaining <= 5 ? QStringLiteral("库存紧张")
                                                : QStringLiteral("库存充足");
        const QStringList values{
            ui->routeTable->item(sourceRow, SeatType)->text(),
            ui->routeTable->item(sourceRow, Price)->text(),
            ui->routeTable->item(sourceRow, TotalSeats)->text(),
            QString::number(sold), QString::number(remaining), status};
        for (int column = 0; column < values.size(); ++column) {
            auto *item = new QTableWidgetItem(values.at(column));
            item->setTextAlignment(Qt::AlignCenter);
            seatInventoryTable->setItem(row, column, item);
        }
        seatInventoryTable->item(row, 0)->setData(Qt::UserRole, sourceRow);
    }
}
// ==================== V0.8 售票中心业务逻辑 ====================
// 根据表格当前选中行，展示班次摘要并决定售票、退票按钮是否可用。
void Widget::updateTicketSelection(int row)
{
    if (row < 0 || row >= ui->routeTable->rowCount()) {
        selectedRouteLabel->setText(
            QStringLiteral("请先选择班次和席位，再进行实名售票。"));
        sellTicketButton->setEnabled(false);
        return;
    }

    // 管理页和售票页共享当前班次，任一处选择都会同步到另一处。
    if (ticketRouteCombo) {
        const QString key = ui->routeTable->item(row, Number)->text().toUpper()
                            + QChar('|')
                            + ui->routeTable->item(row, DepartureDate)->text();
        const int comboIndex = ticketRouteCombo->findData(key);
        const QSignalBlocker blocker(ticketRouteCombo);
        ticketRouteCombo->setCurrentIndex(comboIndex);
    }
    refreshTicketSeatChoices();
    if (ticketSeatCombo) {
        const QSignalBlocker blocker(ticketSeatCombo);
        ticketSeatCombo->setCurrentIndex(ticketSeatCombo->findData(row));
    }

    const bool enabled = ui->routeTable->item(row, Status)->text() != QStringLiteral("停用");
    const int remaining = ui->routeTable->item(row, Remaining)->text().toInt();
    const int sold = ui->routeTable->item(row, Number)->data(SoldRole).toInt();
    selectedRouteLabel->setText(
        QStringLiteral("已选：%1　%2　%3 → %4　%5 发车 / %6 到达　%7　票价 %8　余票 %9　累计已售 %10")
            .arg(ui->routeTable->item(row, Number)->text(),
                 ui->routeTable->item(row, DepartureDate)->text(),
                 ui->routeTable->item(row, Departure)->text(),
                 ui->routeTable->item(row, Destination)->text(),
                 ui->routeTable->item(row, DepartureTime)->text(),
                 ui->routeTable->item(row, ArrivalTime)->text(),
                 ui->routeTable->item(row, SeatType)->text(),
                 ui->routeTable->item(row, Price)->text())
            .arg(remaining)
            .arg(sold));
    sellTicketButton->setEnabled(enabled && remaining > 0);
}

// ==================== V0.14 发车时限与售票资格 ====================
// 所有售票和改签共用同一套判断，避免不同入口出现规则不一致。
bool Widget::isRouteSellable(int row, QString *reason) const
{
    const auto fail = [reason](const QString &message) {
        if (reason) *reason = message;
        return false;
    };
    if (row < 0 || row >= ui->routeTable->rowCount()) return fail(QStringLiteral("班次不存在。"));
    if (ui->routeTable->item(row, Status)->text() == QStringLiteral("停用"))
        return fail(QStringLiteral("该班次已经停运，不能办理售票或改签。"));
    if (ui->routeTable->item(row, SeatType)->text() == QStringLiteral("未配置"))
        return fail(QStringLiteral("该班次尚未配置席位库存。"));
    const QDate date = QDate::fromString(ui->routeTable->item(row, DepartureDate)->text(), QStringLiteral("yyyy-MM-dd"));
    const QTime time = QTime::fromString(ui->routeTable->item(row, DepartureTime)->text(), QStringLiteral("HH:mm"));
    if (!date.isValid() || !time.isValid()) return fail(QStringLiteral("班次发车日期或时间无效。"));
    const qint64 seconds = QDateTime::currentDateTime().secsTo(QDateTime(date, time));
    if (seconds <= 0) return fail(QStringLiteral("该班次已经发车，不能继续售票。"));
    if (seconds <= 15 * 60) return fail(QStringLiteral("距离发车不足 15 分钟，该班次已经停止售票。"));
    return true;
}

// 双击总览预警后跳到班次管理并定位对应记录。
void Widget::focusRouteFromWarning(int routeRow)
{
    if (routeRow < 0 || routeRow >= ui->routeTable->rowCount()) return;
    contentTabs->setCurrentIndex(1);
    ui->routeTable->selectRow(routeRow);
    ui->routeTable->scrollToItem(ui->routeTable->item(routeRow, Number), QAbstractItemView::PositionAtCenter);
}
// 汇总当前表格中的班次数、总余票、累计销量和累计营业额。
void Widget::updateStatistics()
{
    // 所有派生页面刷新前先整理底层班次顺序，避免三个页面排序不一致。
    sortRoutesChronologically();
    int remainingTotal = 0;
    int soldTotal = 0;
    int lowStockCount = 0;
    double revenueTotal = 0.0;
    QSet<QString> serviceKeys;

    for (int row = 0; row < ui->routeTable->rowCount(); ++row) {
        serviceKeys.insert(
            ui->routeTable->item(row, Number)->text().toUpper()
            + QChar('|')
            + ui->routeTable->item(row, DepartureDate)->text());
        if (ui->routeTable->item(row, SeatType)->text() == QStringLiteral("未配置")) {
            continue;
        }
        auto *remainingItem = ui->routeTable->item(row, Remaining);
        const int remaining = remainingItem->text().toInt();
        const auto *numberItem = ui->routeTable->item(row, Number);
        soldTotal += numberItem->data(SoldRole).toInt();
        revenueTotal += numberItem->data(RevenueRole).toDouble();
        if (ui->routeTable->item(row, Status)->text() == QStringLiteral("停用")) {
            remainingItem->setForeground(QColor(QStringLiteral("#64748b")));
            continue;
        }
        remainingTotal += remaining;

        // 余票 0 标记为售罄，1～5 标记为库存紧张；文字提示避免只靠颜色表达。
        QFont stockFont = remainingItem->font();
        stockFont.setBold(remaining <= 10);
        remainingItem->setFont(stockFont);
        if (remaining == 0) {
            ++lowStockCount;
            remainingItem->setForeground(QColor(QStringLiteral("#b4232f")));
            remainingItem->setToolTip(QStringLiteral("售罄：该班次已无余票"));
        } else if (remaining <= 10) {
            ++lowStockCount;
            remainingItem->setForeground(QColor(QStringLiteral("#a15c00")));
            remainingItem->setToolTip(
                QStringLiteral("库存紧张：仅剩 %1 张").arg(remaining));
        } else {
            remainingItem->setForeground(QColor(QStringLiteral("#166534")));
            remainingItem->setToolTip(QStringLiteral("库存充足"));
        }
    }

    routeCountLabel->setText(
        QStringLiteral("运营班次\n%1 个").arg(serviceKeys.size()));
    remainingTotalLabel->setText(
        QStringLiteral("可售余票\n%1 张").arg(remainingTotal));
    soldTotalLabel->setText(
        QStringLiteral("累计售票\n%1 张").arg(soldTotal));
    revenueTotalLabel->setText(
        QStringLiteral("累计营业额\n¥%1").arg(revenueTotal, 0, 'f', 2));
    lowStockLabel->setText(
        QStringLiteral("库存预警\n%1 个席位").arg(lowStockCount));

    // 同步售票中心的班次选择器，保证各页面看到的是同一份最新数据。
    refreshTicketRouteChoices();
    refreshSeatInventoryPage();
    refreshScheduleRows();

    // ==================== V0.10 运营健康度与自动建议 ====================
    // 售票率 = 累计销量 ÷（累计销量 + 当前余票），用于衡量整体运力消化情况。
    const int totalCapacity = soldTotal + remainingTotal;
    const int sellThroughRate = totalCapacity > 0
                                    ? qRound(100.0 * soldTotal / totalCapacity)
                                    : 0;
    occupancyProgress->setValue(sellThroughRate);
    occupancyProgress->setFormat(
        QStringLiteral("整体售票率  %1%").arg(sellThroughRate));

    if (ui->routeTable->rowCount() == 0) {
        overviewInsightLabel->setText(
            QStringLiteral("尚未录入班次。请先进入“班次管理”创建运营计划。"));
        overviewInsightLabel->setProperty("status", QStringLiteral("neutral"));
    } else if (lowStockCount > 0) {
        overviewInsightLabel->setText(
            QStringLiteral("⚠ 当前有 %1 个班次余票不足，请及时关注库存或调整运力。")
                .arg(lowStockCount));
        overviewInsightLabel->setProperty("status", QStringLiteral("warning"));
    } else if (sellThroughRate >= 70) {
        overviewInsightLabel->setText(
            QStringLiteral("✓ 整体售票表现良好，当前班次库存均处于安全范围。"));
        overviewInsightLabel->setProperty("status", QStringLiteral("healthy"));
    } else {
        overviewInsightLabel->setText(
            QStringLiteral("运营平稳。可结合交易流水关注低售票率班次并优化排班。"));
        overviewInsightLabel->setProperty("status", QStringLiteral("neutral"));
    }

    // ==================== V0.14 运营预警与轻量图表 ====================
    // 从现有班次和订单实时派生，不新增数据库表，也不改变原有数据结构。
    warningTable->setRowCount(0);
    QSet<QString> warningKeys;
    const auto addWarning = [this, &warningKeys](const QString &key, const QString &level,
                                                 int routeRow, const QString &message,
                                                 const QColor &color) {
        if (warningKeys.contains(key)) return;
        warningKeys.insert(key);
        const int target = warningTable->rowCount();
        warningTable->insertRow(target);
        const QStringList values{level,
            QStringLiteral("%1  %2").arg(ui->routeTable->item(routeRow, Number)->text(),
                                         ui->routeTable->item(routeRow, DepartureDate)->text()), message};
        for (int column = 0; column < values.size(); ++column) {
            auto *item = new QTableWidgetItem(values.at(column));
            item->setData(Qt::UserRole, routeRow);
            item->setForeground(color);
            item->setToolTip(QStringLiteral("双击定位到班次管理"));
            warningTable->setItem(target, column, item);
        }
    };
    for (int row = 0; row < ui->routeTable->rowCount(); ++row) {
        const QString serviceKey = ui->routeTable->item(row, Number)->text().toUpper()
                                   + QChar('|') + ui->routeTable->item(row, DepartureDate)->text();
        const QString seat = ui->routeTable->item(row, SeatType)->text();
        if (ui->routeTable->item(row, Status)->text() == QStringLiteral("停用")) {
            addWarning(QStringLiteral("stop|") + serviceKey, QStringLiteral("停运"), row,
                       QStringLiteral("班次已停运，当前不可售票"), QColor(QStringLiteral("#b4232f")));
            continue;
        }
        const QDate warningDate = QDate::fromString(
            ui->routeTable->item(row, DepartureDate)->text(), QStringLiteral("yyyy-MM-dd"));
        const QTime warningTime = QTime::fromString(
            ui->routeTable->item(row, DepartureTime)->text(), QStringLiteral("HH:mm"));
        if (warningDate.isValid() && warningTime.isValid()
            && QDateTime(warningDate, warningTime) <= QDateTime::currentDateTime()) {
            addWarning(QStringLiteral("departed|") + serviceKey, QStringLiteral("已发车"), row,
                       QStringLiteral("已超过计划发车时间，售票和改签均已停止"),
                       QColor(QStringLiteral("#9f1239")));
            continue;
        }
        if (seat == QStringLiteral("未配置")) continue;
        const int remaining = ui->routeTable->item(row, Remaining)->text().toInt();
        if (remaining == 0)
            addWarning(QStringLiteral("sold|") + serviceKey + seat, QStringLiteral("售罄"), row,
                       QStringLiteral("%1 已无余票").arg(seat), QColor(QStringLiteral("#b4232f")));
        else if (remaining <= 10)
            addWarning(QStringLiteral("low|") + serviceKey + seat, QStringLiteral("紧张"), row,
                       QStringLiteral("%1 仅剩 %2 张").arg(seat).arg(remaining), QColor(QStringLiteral("#a15c00")));
        const QDate date = QDate::fromString(ui->routeTable->item(row, DepartureDate)->text(), QStringLiteral("yyyy-MM-dd"));
        const QTime time = QTime::fromString(ui->routeTable->item(row, DepartureTime)->text(), QStringLiteral("HH:mm"));
        const qint64 seconds = QDateTime::currentDateTime().secsTo(QDateTime(date, time));
        if (seconds > 15 * 60 && seconds <= 2 * 3600)
            addWarning(QStringLiteral("soon|") + serviceKey, QStringLiteral("临发"), row,
                       QStringLiteral("距离发车不足 2 小时"), QColor(QStringLiteral("#1769aa")));
    }
    if (warningTable->rowCount() == 0) {
        warningTable->insertRow(0);
        warningTable->setSpan(0, 0, 1, 3);
        warningTable->setItem(0, 0, new QTableWidgetItem(QStringLiteral("✓ 当前没有需要处理的运营预警")));
    }
    lowStockLabel->setText(QStringLiteral("运营预警\n%1 项").arg(warningKeys.size()));

    QMap<QDate, int> dailyOrders;
    QMap<QString, int> routeSales;
    int todayOrderCount = 0;
    for (int offset = 6; offset >= 0; --offset) dailyOrders[QDate::currentDate().addDays(-offset)] = 0;
    for (int row = 0; row < orderTable->rowCount(); ++row) {
        if (isOrderDetailRow(orderTable, row)) continue;
        const QDate created = QDate::fromString(orderTable->item(row, OrderCreated)->text().left(10), QStringLiteral("yyyy-MM-dd"));
        if (dailyOrders.contains(created)) ++dailyOrders[created];
        if (created == QDate::currentDate()) ++todayOrderCount;
        if (orderTable->item(row, OrderStatus)->text() == QStringLiteral("已出票"))
            routeSales[orderTable->item(row, OrderRoute)->text()] += qMax(1, orderData(orderTable, row, OrderQuantityRole).toInt());
    }
    QStringList trendLines;
    for (auto it = dailyOrders.cbegin(); it != dailyOrders.cend(); ++it)
        trendLines << QStringLiteral("%1  %2 %3 单").arg(it.key().toString(QStringLiteral("MM-dd")),
                     QString(qMin(it.value(), 10), QChar(0x2588))).arg(it.value());
    salesTrendLabel->setText(QStringLiteral("近七日订单\n") + trendLines.join(QChar('\n')));
    QList<QPair<QString, int>> ranking;
    for (auto it = routeSales.cbegin(); it != routeSales.cend(); ++it) ranking.append({it.key(), it.value()});
    std::sort(ranking.begin(), ranking.end(), [](const auto &left, const auto &right) { return left.second > right.second; });
    QStringList rankingLines;
    for (int index = 0; index < qMin(3, ranking.size()); ++index)
        rankingLines << QStringLiteral("%1. %2　%3 张").arg(index + 1).arg(ranking.at(index).first).arg(ranking.at(index).second);
    double todayIncome = 0.0;
    double todayRefund = 0.0;
    for (int row = 0; row < transactionTable->rowCount(); ++row) {
        if (!transactionTable->item(row, 0)->text().startsWith(QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")))) continue;
        QString amountText = transactionTable->item(row, 6)->text();
        amountText.remove(QChar(0x00A5));
        const double change = amountText.toDouble();
        if (change >= 0) todayIncome += change;
        else todayRefund += qAbs(change);
    }
    popularRoutesLabel->setText(
        QStringLiteral("今日概览  %1 单　收入 ¥%2　退款 ¥%3　净额 ¥%4\n\n热门班次 TOP3\n")
            .arg(todayOrderCount).arg(todayIncome, 0, 'f', 2).arg(todayRefund, 0, 'f', 2)
            .arg(todayIncome - todayRefund, 0, 'f', 2)
        + (rankingLines.isEmpty() ? QStringLiteral("暂无有效订单") : rankingLines.join(QChar('\n'))));
    // 动态属性改变后重新应用样式，使运营建议的状态色立即刷新。
    overviewInsightLabel->style()->unpolish(overviewInsightLabel);
    overviewInsightLabel->style()->polish(overviewInsightLabel);
}

// ==================== V0.13 可视化座位选择 ====================
// 根据席位容量生成 2+3 车厢座位图；已出票订单占用的座位不可重复选择。
QStringList Widget::chooseSeats(int routeRow, int quantity, int excludedOrderRow)
{
    const int capacity = ui->routeTable->item(routeRow, TotalSeats)->text().toInt();
    const QString number = ui->routeTable->item(routeRow, Number)->text();
    const QString date = ui->routeTable->item(routeRow, DepartureDate)->text();
    const QString seatType = ui->routeTable->item(routeRow, SeatType)->text();
    QSet<QString> occupied;
    for (int row = 0; row < orderTable->rowCount(); ++row) {
        if (row == excludedOrderRow || isOrderDetailRow(orderTable, row)
            || !orderTable->item(row, OrderStatus)
            || orderTable->item(row, OrderStatus)->text() != QStringLiteral("已出票")
            || orderTable->item(row, OrderRoute)->text() != number
            || orderTable->item(row, OrderDate)->text() != date
            || orderTable->item(row, OrderSeatType)->text() != seatType) continue;
        const QStringList seats = orderData(orderTable, row, OrderSeatNumberRole).split(
            QChar(0x3001), Qt::SkipEmptyParts);
        for (const QString &seat : seats) {
            if (seat != QStringLiteral("未分配")) occupied.insert(seat);
        }
    }
    // 旧版本订单没有保存座位号：按累计销量占用前排空位，避免升级后重复分配。
    const int soldCount = ui->routeTable->item(routeRow, Number)->data(SoldRole).toInt();
    const QStringList seatLetters{QStringLiteral("A"), QStringLiteral("B"),
                                  QStringLiteral("C"), QStringLiteral("D"), QStringLiteral("F")};
    for (int index = 0; index < capacity && occupied.size() < soldCount; ++index) {
        const QString seat = QStringLiteral("%1%2").arg(index / 5 + 1, 2, 10, QChar('0'))
                             + seatLetters.at(index % 5);
        occupied.insert(seat);
    }

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("seatMapDialog"));
    dialog.setWindowTitle(QStringLiteral("选择座位"));
    dialog.resize(720, 680);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(14);
    auto *title = new QLabel(QStringLiteral("%1 · %2 · %3").arg(number, date, seatType), &dialog);
    title->setObjectName(QStringLiteral("seatMapTitle"));
    auto *hint = new QLabel(QStringLiteral("请选择 %1 个座位　A/F 靠窗　C/D 靠过道").arg(quantity), &dialog);
    hint->setObjectName(QStringLiteral("seatMapHint"));
    auto *legend = new QLabel(QStringLiteral("■ 可选　　■ 已选择　　■ 已售　　│ 中央过道"), &dialog);
    legend->setObjectName(QStringLiteral("seatMapLegend"));
    layout->addWidget(title);
    layout->addWidget(hint);
    layout->addWidget(legend);

    auto *scroll = new QScrollArea(&dialog);
    scroll->setObjectName(QStringLiteral("seatMapScroll"));
    scroll->setWidgetResizable(true);
    auto *cabin = new QFrame(scroll);
    cabin->setObjectName(QStringLiteral("seatCabin"));
    auto *grid = new QGridLayout(cabin);
    grid->setContentsMargins(28, 22, 28, 22);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(10);
    const QStringList letters{QStringLiteral("A"), QStringLiteral("B"),
                              QStringLiteral("C"), QStringLiteral("D"), QStringLiteral("F")};
    const QList<int> columns{0, 1, 3, 4, 5};
    for (int i = 0; i < letters.size(); ++i) {
        auto *label = new QLabel(letters.at(i), cabin);
        label->setObjectName(QStringLiteral("seatColumnLabel"));
        label->setAlignment(Qt::AlignCenter);
        grid->addWidget(label, 0, columns.at(i));
    }
    auto *aisle = new QLabel(QStringLiteral("过\n道"), cabin);
    aisle->setObjectName(QStringLiteral("seatAisleLabel"));
    aisle->setAlignment(Qt::AlignCenter);
    grid->addWidget(aisle, 1, 2, qMax(1, (capacity + 4) / 5), 1);

    QStringList chosen;
    QList<QPushButton *> buttons;
    auto *selectedLabel = new QLabel(QStringLiteral("已选择 0/%1：尚未选择").arg(quantity), &dialog);
    selectedLabel->setObjectName(QStringLiteral("seatSelectionSummary"));
    auto *actions = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    actions->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确认座位"));
    actions->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    actions->button(QDialogButtonBox::Ok)->setEnabled(false);

    for (int index = 0; index < capacity; ++index) {
        const QString seat = QStringLiteral("%1%2").arg(index / 5 + 1, 2, 10, QChar('0'))
                             + letters.at(index % 5);
        auto *button = new QPushButton(seat, cabin);
        button->setObjectName(QStringLiteral("seatButton"));
        button->setCheckable(true);
        button->setProperty("seatState", occupied.contains(seat) ? "occupied" : "available");
        button->setEnabled(!occupied.contains(seat));
        button->setAccessibleName(QStringLiteral("座位 %1，%2").arg(
            seat, occupied.contains(seat) ? QStringLiteral("已售") : QStringLiteral("可选")));
        grid->addWidget(button, index / 5 + 1, columns.at(index % 5));
        buttons.append(button);
        connect(button, &QPushButton::toggled, &dialog, [&, button, seat](bool checked) {
            if (checked && chosen.size() >= quantity) {
                const QSignalBlocker blocker(button);
                button->setChecked(false);
                return;
            }
            if (checked) chosen.append(seat); else chosen.removeAll(seat);
            button->setProperty("seatState", checked ? "selected" : "available");
            button->style()->unpolish(button);
            button->style()->polish(button);
            selectedLabel->setText(QStringLiteral("已选择 %1/%2：%3")
                .arg(chosen.size()).arg(quantity)
                .arg(chosen.isEmpty() ? QStringLiteral("尚未选择") : chosen.join(QStringLiteral("、"))));
            actions->button(QDialogButtonBox::Ok)->setEnabled(chosen.size() == quantity);
        });
    }
    scroll->setWidget(cabin);
    layout->addWidget(scroll, 1);
    layout->addWidget(selectedLabel);
    layout->addWidget(actions);
    connect(actions, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(actions, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return {};
    return chosen;
}

// ==================== V0.11 实名旅客校验 ====================
// 姓名、身份证号和手机号都通过格式检查后，才允许生成实名订单。
bool Widget::validatePassenger()
{
    const QString name = passengerNameEdit->text().trimmed();
    const QString idNumber = passengerIdEdit->text().trimmed();
    const QString phone = passengerPhoneEdit->text().trimmed();
    const QRegularExpression idPattern(
        QStringLiteral("^[0-9]{17}[0-9Xx]$"));
    const QRegularExpression phonePattern(
        QStringLiteral("^1[3-9][0-9]{9}$"));

    if (name.size() < 2) {
        QMessageBox::warning(this,
                             QStringLiteral("旅客姓名有误"),
                             QStringLiteral("请输入至少 2 个字符的真实姓名。"));
        passengerNameEdit->setFocus();
        return false;
    }
    if (!idPattern.match(idNumber).hasMatch()) {
        QMessageBox::warning(this,
                             QStringLiteral("身份证号有误"),
                             QStringLiteral("请输入正确的 18 位身份证号，末位可以是 X。"));
        passengerIdEdit->setFocus();
        return false;
    }
    if (!phonePattern.match(phone).hasMatch()) {
        QMessageBox::warning(this,
                             QStringLiteral("手机号有误"),
                             QStringLiteral("请输入正确的 11 位中国大陆手机号。"));
        passengerPhoneEdit->setFocus();
        return false;
    }
    return true;
}

// 售票：校验实名信息和库存，确认交易后扣减余票，并创建可追踪订单。
void Widget::sellTickets()
{
    const int row = ui->routeTable->currentRow();
    if (row < 0 || ui->routeTable->item(row, SeatType)->text()
                       == QStringLiteral("未配置")) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先选择已经配置库存的班次和席位。"));
        return;
    }
    if (!validatePassenger()) return;

    // 售票规则：停运、已发车或发车前 15 分钟内的班次统一停止销售。
    QString unavailableReason;
    if (!isRouteSellable(row, &unavailableReason)) {
        QMessageBox::warning(this, QStringLiteral("当前班次不可售"), unavailableReason);
        return;
    }

    // 同一证件不能重复购买同日同车次的有效订单，避免重复占座。
    const QString passengerId = passengerIdEdit->text().trimmed().toUpper();
    const QString routeNumber = ui->routeTable->item(row, Number)->text();
    const QString departureDate = ui->routeTable->item(row, DepartureDate)->text();
    for (int orderRow = 0; orderRow < orderTable->rowCount(); ++orderRow) {
        if (isOrderDetailRow(orderTable, orderRow)) continue;
        if (orderTable->item(orderRow, OrderStatus)->text() == QStringLiteral("已出票")
            && orderData(orderTable, orderRow, OrderIdRole).toUpper() == passengerId
            && orderTable->item(orderRow, OrderRoute)->text() == routeNumber
            && orderTable->item(orderRow, OrderDate)->text() == departureDate) {
            QMessageBox::warning(this, QStringLiteral("发现重复购票"),
                                 QStringLiteral("该旅客已经持有同日同车次的有效订单，不能重复购票。"));
            return;
        }
    }

    const int quantity = ticketQuantitySpin->value();
    auto *remainingItem = ui->routeTable->item(row, Remaining);
    auto *numberItem = ui->routeTable->item(row, Number);
    const int remaining = remainingItem->text().toInt();
    if (quantity > remaining) {
        QMessageBox::warning(
            this, QStringLiteral("余票不足"),
            QStringLiteral("当前仅剩 %1 张票，无法售出 %2 张。")
                .arg(remaining).arg(quantity));
        return;
    }

    selectedSeatNumbers = chooseSeats(row, quantity);
    if (selectedSeatNumbers.size() != quantity) return;

    const double price = ui->routeTable->item(row, Price)
                             ->text().remove(QChar(0x00A5)).toDouble();
    const double amount = price * quantity;
    const QString orderNumber =
        QStringLiteral("BT%1").arg(
            QDateTime::currentDateTime().toString(
                QStringLiteral("yyyyMMddHHmmsszzz")));
    const QString confirmationText = QStringLiteral(
        "旅客：%1\n班次：%2　%3\n时段：%4 - %5\n席位：%6\n座位：%7\n数量：%8 张\n应收：¥%9\n\n确认完成实名售票吗？")
        .arg(passengerNameEdit->text().trimmed())
        .arg(numberItem->text())
        .arg(ui->routeTable->item(row, DepartureDate)->text())
        .arg(ui->routeTable->item(row, DepartureTime)->text())
        .arg(ui->routeTable->item(row, ArrivalTime)->text())
        .arg(ui->routeTable->item(row, SeatType)->text())
        .arg(selectedSeatNumbers.join(QStringLiteral("、")))
        .arg(quantity)
        .arg(amount, 0, 'f', 2);
    QMessageBox confirmation(QMessageBox::Question, QStringLiteral("确认售票"),
                             confirmationText, QMessageBox::Yes | QMessageBox::No, this);
    confirmation.setDefaultButton(QMessageBox::No);
    confirmation.setStyleSheet(QStringLiteral("QLabel{min-width:520px; font-size:16px;} QPushButton{min-width:96px; min-height:38px;}"));
    if (confirmation.exec() != QMessageBox::Yes) return;

    remainingItem->setText(QString::number(remaining - quantity));
    numberItem->setData(SoldRole,
                        numberItem->data(SoldRole).toInt() + quantity);
    numberItem->setData(RevenueRole,
                        numberItem->data(RevenueRole).toDouble() + amount);
    recordTransaction(QStringLiteral("售票"), row, quantity, price,
                      amount, remaining - quantity);
    createOrder(row, quantity, amount, orderNumber);
    if (!saveBusinessState()) {
        remainingItem->setText(QString::number(remaining));
        numberItem->setData(SoldRole, numberItem->data(SoldRole).toInt() - quantity);
        numberItem->setData(RevenueRole, numberItem->data(RevenueRole).toDouble() - amount);
        orderTable->removeRow(0);
        transactionTable->removeRow(0);
        selectedSeatNumbers.clear();
        QMessageBox::critical(this, QStringLiteral("售票未保存"),
                              QStringLiteral("本次售票已回滚，请检查系统权限后重试。"));
        return;
    }
    updateStatistics();
    updateTicketSelection(row);
    QMessageBox::information(
        this, QStringLiteral("售票成功"),
        QStringLiteral("已售出 %1 张票，应收 ¥%2。\n订单号：%3")
            .arg(quantity).arg(amount, 0, 'f', 2).arg(orderNumber));
    passengerNameEdit->clear();
    passengerIdEdit->clear();
    passengerPhoneEdit->clear();
    ticketQuantitySpin->setValue(1);
    selectedSeatNumbers.clear();
}

// ==================== V0.12 订单组合筛选与改签 ====================
// 售票成功后生成订单行；证件号和手机号仅显示脱敏内容，降低隐私泄露风险。
void Widget::createOrder(int routeRow, int quantity, double amount,
                         const QString &orderNumber)
{
    const QString idNumber = passengerIdEdit->text().trimmed().toUpper();
    const QString phone = passengerPhoneEdit->text().trimmed();
    const QString maskedId = idNumber.left(6) + QStringLiteral("********") + idNumber.right(4);
    const QString maskedPhone = phone.left(3) + QStringLiteral("****") + phone.right(4);
    const QString journey = QStringLiteral("%1 → %2　%3-%4")
        .arg(ui->routeTable->item(routeRow, Departure)->text(),
             ui->routeTable->item(routeRow, Destination)->text(),
             ui->routeTable->item(routeRow, DepartureTime)->text(),
             ui->routeTable->item(routeRow, ArrivalTime)->text());
    const double unitPrice = quantity > 0 ? amount / quantity : 0.0;
    const QStringList visible{QString(), orderNumber,
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
        passengerNameEdit->text().trimmed(), ui->routeTable->item(routeRow, Number)->text(),
        ui->routeTable->item(routeRow, DepartureDate)->text(),
        ui->routeTable->item(routeRow, SeatType)->text(), journey,
        QStringLiteral("¥%1").arg(amount, 0, 'f', 2), QStringLiteral("已出票")};
    orderTable->insertRow(0);
    for (int column = OrderExpand; column <= OrderStatus; ++column) {
        auto *item = new QTableWidgetItem(visible.at(column));
        item->setTextAlignment(Qt::AlignCenter);
        item->setToolTip(visible.at(column));
        orderTable->setItem(0, column, item);
    }
    auto *anchor = orderTable->item(0, OrderNumber);
    anchor->setData(OrderIdRole, maskedId);
    anchor->setData(OrderPhoneRole, maskedPhone);
    anchor->setData(OrderQuantityRole, quantity);
    anchor->setData(OrderUnitPriceRole, unitPrice);
    anchor->setData(OrderSeatNumberRole, selectedSeatNumbers.join(QStringLiteral("、")));
    orderTable->item(0, OrderStatus)->setForeground(QColor(QStringLiteral("#15803d")));
    installOrderActionButton(0);
    filterOrders();
}

// 行首箭头在当前订单下插入详情面板；再次点击即收起，不使用隐藏表格列。
void Widget::installOrderActionButton(int row)
{
    auto *expandHost = new QWidget(orderTable);
    expandHost->setObjectName(QStringLiteral("orderCellHost"));
    auto *expandLayout = new QHBoxLayout(expandHost);
    expandLayout->setContentsMargins(4, 5, 4, 5);
    expandLayout->setAlignment(Qt::AlignCenter);
    auto *expand = new QPushButton(QStringLiteral("›"), expandHost);
    expand->setObjectName(QStringLiteral("orderExpandButton"));
    expand->setFixedSize(34, 32);
    expand->setToolTip(QStringLiteral("展开订单详情"));
    expandLayout->addWidget(expand);
    orderTable->setCellWidget(row, OrderExpand, expandHost);
    connect(expand, &QPushButton::clicked, this, [this, expand] {
        const int row = orderTable->indexAt(expand->mapTo(orderTable->viewport(), QPoint(1, 1))).row();
        if (row < 0 || isOrderDetailRow(orderTable, row)) return;
        if (row + 1 < orderTable->rowCount() && isOrderDetailRow(orderTable, row + 1)) {
            orderTable->removeRow(row + 1);
            expand->setText(QStringLiteral("›"));
            return;
        }
        orderTable->insertRow(row + 1);
        auto *marker = new QTableWidgetItem;
        marker->setData(OrderDetailRole, true);
        orderTable->setItem(row + 1, OrderExpand, marker);
        orderTable->setSpan(row + 1, OrderExpand, 1, OrderColumnCount);
        auto *panel = new QFrame(orderTable);
        panel->setObjectName(QStringLiteral("orderDetailPanel"));
        auto *layout = new QHBoxLayout(panel);
        layout->setContentsMargins(22, 12, 22, 12);
        layout->setSpacing(28);
        layout->addWidget(new QLabel(QStringLiteral("证件号\n%1").arg(orderData(orderTable, row, OrderIdRole)), panel));
        layout->addWidget(new QLabel(QStringLiteral("手机号\n%1").arg(orderData(orderTable, row, OrderPhoneRole)), panel));
        layout->addWidget(new QLabel(QStringLiteral("座位号\n%1").arg(cleanSeatNumber(orderData(orderTable, row, OrderSeatNumberRole))), panel));
        layout->addStretch();
        orderTable->setCellWidget(row + 1, OrderExpand, panel);
        orderTable->setRowHeight(row + 1, 70);
        expand->setText(QStringLiteral("⌄"));
    });

    auto *actionHost = new QWidget(orderTable);
    actionHost->setObjectName(QStringLiteral("orderCellHost"));
    auto *actionLayout = new QHBoxLayout(actionHost);
    actionLayout->setContentsMargins(4, 5, 4, 5);
    actionLayout->setAlignment(Qt::AlignCenter);
    auto *action = new QPushButton(actionHost);
    action->setObjectName(QStringLiteral("orderRescheduleAction"));
    action->setFixedSize(34, 32);
    action->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    action->setToolTip(QStringLiteral("办理改签"));
    action->setEnabled(orderTable->item(row, OrderStatus)
                       && orderTable->item(row, OrderStatus)->text() == QStringLiteral("已出票"));
    actionLayout->addWidget(action);
    orderTable->setCellWidget(row, OrderAction, actionHost);
    connect(action, &QPushButton::clicked, this, [this, action] {
        const int row = orderTable->indexAt(action->mapTo(orderTable->viewport(), QPoint(1, 1))).row();
        if (row < 0 || isOrderDetailRow(orderTable, row)) return;
        orderTable->selectRow(row);
        rescheduleSelectedOrder();
    });
}

// 继续沿用原 QSettings 字段结构，数量和单价仅作为内部计算数据，不再渲染成列。
void Widget::saveOrders() const
{
    QSettings settings(QStringLiteral("StudentQtProjects"), QStringLiteral("BusTicketSystem"));
    settings.beginWriteArray(QStringLiteral("orders"));
    int target = 0;
    for (int row = 0; row < orderTable->rowCount(); ++row) {
        if (isOrderDetailRow(orderTable, row)) continue;
        settings.setArrayIndex(target++);
        const QStringList values{orderTable->item(row, OrderNumber)->text(),
            orderTable->item(row, OrderCreated)->text(), orderTable->item(row, OrderPassenger)->text(),
            orderData(orderTable, row, OrderIdRole), orderData(orderTable, row, OrderPhoneRole),
            orderTable->item(row, OrderRoute)->text(), orderTable->item(row, OrderDate)->text(),
            orderTable->item(row, OrderSeatType)->text(), orderTable->item(row, OrderJourney)->text(),
            orderData(orderTable, row, OrderQuantityRole), orderTable->item(row, OrderAmount)->text(),
            orderTable->item(row, OrderStatus)->text(), orderData(orderTable, row, OrderUnitPriceRole),
            orderData(orderTable, row, OrderSeatNumberRole)};
        for (int column = 0; column < values.size(); ++column)
            settings.setValue(QStringLiteral("column%1").arg(column), values.at(column));
    }
    settings.endArray();
}

void Widget::loadOrders()
{
    QSettings settings(QStringLiteral("StudentQtProjects"), QStringLiteral("BusTicketSystem"));
    const int count = settings.beginReadArray(QStringLiteral("orders"));
    for (int row = 0; row < count; ++row) {
        settings.setArrayIndex(row);
        const bool legacy = !settings.contains(QStringLiteral("column10"));
        QStringList raw;
        for (int column = 0; column <= 13; ++column) raw << settings.value(QStringLiteral("column%1").arg(column)).toString();
        if (legacy) {
            raw[6] = QStringLiteral("历史日期"); raw[7] = QStringLiteral("标准座");
            for (int column = 8; column <= 11; ++column) raw[column] = settings.value(QStringLiteral("column%1").arg(column - 2)).toString();
        }
        if (raw[9].isEmpty()) raw[9] = QStringLiteral("1");
        if (raw[12].isEmpty()) raw[12] = QString::number(raw[10].remove(QChar(0x00A5)).toDouble() / qMax(1, raw[9].toInt()));
        orderTable->insertRow(row);
        const QStringList visible{QString(), raw[0], raw[1], raw[2], raw[5], raw[6], raw[7], raw[8], raw[10], raw[11]};
        for (int column = OrderExpand; column <= OrderStatus; ++column) {
            auto *item = new QTableWidgetItem(visible.at(column)); item->setTextAlignment(Qt::AlignCenter);
            item->setToolTip(visible.at(column)); orderTable->setItem(row, column, item);
        }
        auto *anchor = orderTable->item(row, OrderNumber);
        anchor->setData(OrderIdRole, raw[3]); anchor->setData(OrderPhoneRole, raw[4]);
        anchor->setData(OrderQuantityRole, raw[9].toInt()); anchor->setData(OrderUnitPriceRole, raw[12].remove(QChar(0x00A5)).toDouble());
        anchor->setData(OrderSeatNumberRole, cleanSeatNumber(raw[13]));
        const bool issued = raw[11] == QStringLiteral("已出票");
        orderTable->item(row, OrderStatus)->setForeground(QColor(issued ? QStringLiteral("#15803d") : QStringLiteral("#b45309")));
        installOrderActionButton(row);
    }
    settings.endArray();
}

void Widget::filterOrders()
{
    orderTable->clearSelection(); refundOrderButton->setEnabled(false); rescheduleOrderButton->setEnabled(false);
    const QString passenger = orderPassengerFilterEdit->text().trimmed();
    const QString route = orderRouteFilterEdit->text().trimmed();
    const QString status = orderStatusFilterCombo->currentText();
    const QDate startDate = orderStartDateEdit->date(), endDate = orderEndDateEdit->date();
    if (startDate > endDate) { QMessageBox::warning(this, QStringLiteral("日期范围错误"), QStringLiteral("开始日期不能晚于结束日期。")); return; }
    int visibleCount = 0;
    for (int row = 0; row < orderTable->rowCount(); ++row) {
        if (isOrderDetailRow(orderTable, row)) { orderTable->setRowHidden(row, true); continue; }
        const bool passengerMatched = passenger.isEmpty()
            || orderTable->item(row, OrderPassenger)->text().contains(passenger, Qt::CaseInsensitive)
            || orderData(orderTable, row, OrderPhoneRole).contains(passenger, Qt::CaseInsensitive);
        const bool routeMatched = route.isEmpty() || orderTable->item(row, OrderRoute)->text().contains(route, Qt::CaseInsensitive);
        const bool statusMatched = status == QStringLiteral("全部状态") || orderTable->item(row, OrderStatus)->text() == status;
        const QDate orderDate = QDate::fromString(orderTable->item(row, OrderCreated)->text().left(10), QStringLiteral("yyyy-MM-dd"));
        const bool matched = passengerMatched && routeMatched && statusMatched
                             && orderDate.isValid() && orderDate >= startDate && orderDate <= endDate;
        orderTable->setRowHidden(row, !matched);
        if (matched) ++visibleCount;
    }
}

// ==================== V0.14 订单报表导出 ====================
// 只导出当前筛选后可见的主订单行，详情字段一并写入 CSV。
void Widget::exportOrders()
{
    int visibleCount = 0;
    for (int row = 0; row < orderTable->rowCount(); ++row)
        if (!isOrderDetailRow(orderTable, row) && !orderTable->isRowHidden(row)) ++visibleCount;
    if (visibleCount == 0) {
        QMessageBox::information(this, QStringLiteral("没有可导出的数据"), QStringLiteral("当前筛选结果为空。"));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出订单报表"),
        QStringLiteral("订单报表_%1.csv").arg(QDate::currentDate().toString(QStringLiteral("yyyyMMdd"))),
        QStringLiteral("CSV 文件 (*.csv)"));
    if (path.isEmpty()) return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("无法写入所选文件。"));
        return;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << QChar(0xFEFF);
    const auto csv = [](QString value) {
        value.replace(QChar('"'), QStringLiteral("\"\""));
        return QStringLiteral("\"%1\"").arg(value);
    };
    const QStringList headers{QStringLiteral("订单号"), QStringLiteral("创建时间"), QStringLiteral("旅客姓名"),
        QStringLiteral("证件号"), QStringLiteral("手机号"), QStringLiteral("车次号"), QStringLiteral("发车日期"),
        QStringLiteral("席位"), QStringLiteral("座位号"), QStringLiteral("行程"), QStringLiteral("订单金额"), QStringLiteral("状态")};
    for (int column = 0; column < headers.size(); ++column) {
        if (column) stream << ',';
        stream << csv(headers.at(column));
    }
    stream << '\n';
    for (int row = 0; row < orderTable->rowCount(); ++row) {
        if (isOrderDetailRow(orderTable, row) || orderTable->isRowHidden(row)) continue;
        const QStringList values{orderTable->item(row, OrderNumber)->text(), orderTable->item(row, OrderCreated)->text(),
            orderTable->item(row, OrderPassenger)->text(), orderData(orderTable, row, OrderIdRole),
            orderData(orderTable, row, OrderPhoneRole), orderTable->item(row, OrderRoute)->text(),
            orderTable->item(row, OrderDate)->text(), orderTable->item(row, OrderSeatType)->text(),
            cleanSeatNumber(orderData(orderTable, row, OrderSeatNumberRole)), orderTable->item(row, OrderJourney)->text(),
            orderTable->item(row, OrderAmount)->text(), orderTable->item(row, OrderStatus)->text()};
        for (int column = 0; column < values.size(); ++column) {
            if (column) stream << ',';
            stream << csv(values.at(column));
        }
        stream << '\n';
    }
    if (!file.commit()) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("保存 CSV 文件时发生错误。"));
        return;
    }
    QMessageBox::information(this, QStringLiteral("导出成功"),
                             QStringLiteral("已导出 %1 条订单：\n%2").arg(visibleCount).arg(path));
}
// 按订单改签：选择启用班次和席位，自动换库存并记录多退少补差额。
void Widget::rescheduleSelectedOrder()
{
    const int orderRow = orderTable->currentRow();
    if (orderRow < 0 || orderTable->isRowHidden(orderRow)
        || !orderTable->item(orderRow, OrderStatus)
        || orderTable->item(orderRow, OrderStatus)->text() != QStringLiteral("已出票")) {
        QMessageBox::information(this, QStringLiteral("订单不可改签"),
                                 QStringLiteral("请选择一笔状态为“已出票”的订单。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("办理订单改签"));
    dialog.setModal(true);
    dialog.setMinimumWidth(620);
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *routeCombo = new QComboBox(&dialog);
    auto *seatCombo = new QComboBox(&dialog);
    auto *differenceLabel = new QLabel(&dialog);
    differenceLabel->setObjectName(QStringLiteral("rescheduleDifferenceLabel"));
    differenceLabel->setWordWrap(true);
    QSet<QString> routeKeys;
    for (int row = 0; row < ui->routeTable->rowCount(); ++row) {
        QString unavailableReason;
        if (!isRouteSellable(row, &unavailableReason)) continue;
        const QString key = ui->routeTable->item(row, Number)->text().toUpper()
                            + QChar('|') + ui->routeTable->item(row, DepartureDate)->text();
        if (routeKeys.contains(key)) continue;
        routeKeys.insert(key);
        routeCombo->addItem(QStringLiteral("%1　%2　%3 → %4　%5-%6")
            .arg(ui->routeTable->item(row, Number)->text(),
                 ui->routeTable->item(row, DepartureDate)->text(),
                 ui->routeTable->item(row, Departure)->text(),
                 ui->routeTable->item(row, Destination)->text(),
                 ui->routeTable->item(row, DepartureTime)->text(),
                 ui->routeTable->item(row, ArrivalTime)->text()), key);
    }
    form->addRow(QStringLiteral("新班次"), routeCombo);
    form->addRow(QStringLiteral("新席位"), seatCombo);
    layout->addLayout(form);
    layout->addWidget(differenceLabel);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确认改签"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);

    const int quantity = orderData(orderTable, orderRow, OrderQuantityRole).toInt();
    const double oldAmount = orderTable->item(orderRow, OrderAmount)->text().remove(QChar(0x00A5)).toDouble();
    const auto refreshSeats = [this, routeCombo, seatCombo] {
        seatCombo->clear();
        const QString key = routeCombo->currentData().toString();
        for (int row = 0; row < ui->routeTable->rowCount(); ++row) {
            const QString rowKey = ui->routeTable->item(row, Number)->text().toUpper()
                                   + QChar('|') + ui->routeTable->item(row, DepartureDate)->text();
            if (rowKey == key && ui->routeTable->item(row, Status)->text() != QStringLiteral("停用")
                && ui->routeTable->item(row, SeatType)->text() != QStringLiteral("未配置")) {
                seatCombo->addItem(QStringLiteral("%1　%2　余票 %3")
                    .arg(ui->routeTable->item(row, SeatType)->text(),
                         ui->routeTable->item(row, Price)->text(),
                         ui->routeTable->item(row, Remaining)->text()), row);
            }
        }
    };
    const auto refreshDifference = [this, seatCombo, differenceLabel, quantity, oldAmount] {
        const int row = seatCombo->currentData().toInt();
        if (seatCombo->currentIndex() < 0 || row < 0) {
            differenceLabel->setText(QStringLiteral("所选班次暂无可改签席位。"));
            return;
        }
        const double price = ui->routeTable->item(row, Price)->text().remove(QChar(0x00A5)).toDouble();
        const double difference = price * quantity - oldAmount;
        differenceLabel->setText(QStringLiteral("新订单金额：¥%1　差额：%2¥%3（%4）")
            .arg(price * quantity, 0, 'f', 2)
            .arg(difference >= 0 ? QStringLiteral("补收 ") : QStringLiteral("退还 "))
            .arg(qAbs(difference), 0, 'f', 2)
            .arg(difference >= 0 ? QStringLiteral("多补") : QStringLiteral("少退")));
    };
    connect(routeCombo, &QComboBox::currentIndexChanged, &dialog,
            [refreshSeats, refreshDifference] { refreshSeats(); refreshDifference(); });
    connect(seatCombo, &QComboBox::currentIndexChanged, &dialog,
            [refreshDifference] { refreshDifference(); });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    refreshSeats();
    refreshDifference();
    if (dialog.exec() != QDialog::Accepted || seatCombo->currentIndex() < 0) return;

    const int targetRow = seatCombo->currentData().toInt();
    const int sourceRow = findRouteRow(orderTable->item(orderRow, OrderRoute)->text(),
                                       orderTable->item(orderRow, OrderDate)->text(),
                                       orderTable->item(orderRow, OrderSeatType)->text());
    if (sourceRow < 0 || targetRow == sourceRow) {
        QMessageBox::information(this, QStringLiteral("无需改签"),
                                 sourceRow < 0 ? QStringLiteral("原订单班次已不存在，无法自动换回库存。")
                                               : QStringLiteral("新班次和席位与原订单相同。"));
        return;
    }
    if (ui->routeTable->item(targetRow, Remaining)->text().toInt() < quantity) {
        QMessageBox::warning(this, QStringLiteral("余票不足"),
                             QStringLiteral("目标席位余票不足，无法完成整单改签。"));
        return;
    }

    const QStringList newSeatNumbers = chooseSeats(targetRow, quantity, orderRow);
    if (newSeatNumbers.size() != quantity) return;

    const double newPrice = ui->routeTable->item(targetRow, Price)->text().remove(QChar(0x00A5)).toDouble();
    const double newAmount = newPrice * quantity;
    const double difference = newAmount - oldAmount;
    auto *sourceNumber = ui->routeTable->item(sourceRow, Number);
    auto *targetNumber = ui->routeTable->item(targetRow, Number);
    // 保存改签前状态；统一落盘失败时完整恢复原班次、目标班次和订单。
    const QString sourceRemainingBefore = ui->routeTable->item(sourceRow, Remaining)->text();
    const int sourceSoldBefore = sourceNumber->data(SoldRole).toInt();
    const double sourceRevenueBefore = sourceNumber->data(RevenueRole).toDouble();
    const QString targetRemainingBefore = ui->routeTable->item(targetRow, Remaining)->text();
    const int targetSoldBefore = targetNumber->data(SoldRole).toInt();
    const double targetRevenueBefore = targetNumber->data(RevenueRole).toDouble();
    QStringList orderBefore;
    for (int column : {OrderRoute, OrderDate, OrderSeatType, OrderJourney, OrderAmount}) orderBefore << orderTable->item(orderRow, column)->text();
    const double unitPriceBefore = orderData(orderTable, orderRow, OrderUnitPriceRole).toDouble();
    const QString seatNumberBefore = orderData(orderTable, orderRow, OrderSeatNumberRole);
    ui->routeTable->item(sourceRow, Remaining)->setText(QString::number(
        ui->routeTable->item(sourceRow, Remaining)->text().toInt() + quantity));
    sourceNumber->setData(SoldRole, qMax(0, sourceNumber->data(SoldRole).toInt() - quantity));
    sourceNumber->setData(RevenueRole, qMax(0.0, sourceNumber->data(RevenueRole).toDouble() - oldAmount));
    ui->routeTable->item(targetRow, Remaining)->setText(QString::number(
        ui->routeTable->item(targetRow, Remaining)->text().toInt() - quantity));
    targetNumber->setData(SoldRole, targetNumber->data(SoldRole).toInt() + quantity);
    targetNumber->setData(RevenueRole, targetNumber->data(RevenueRole).toDouble() + newAmount);

    orderTable->item(orderRow, OrderRoute)->setText(ui->routeTable->item(targetRow, Number)->text());
    orderTable->item(orderRow, OrderDate)->setText(ui->routeTable->item(targetRow, DepartureDate)->text());
    orderTable->item(orderRow, OrderSeatType)->setText(ui->routeTable->item(targetRow, SeatType)->text());
    orderTable->item(orderRow, OrderJourney)->setText(QStringLiteral("%1 → %2　%3-%4")
        .arg(ui->routeTable->item(targetRow, Departure)->text(),
             ui->routeTable->item(targetRow, Destination)->text(),
             ui->routeTable->item(targetRow, DepartureTime)->text(),
             ui->routeTable->item(targetRow, ArrivalTime)->text()));
    orderTable->item(orderRow, OrderAmount)->setText(QStringLiteral("¥%1").arg(newAmount, 0, 'f', 2));
    orderTable->item(orderRow, OrderNumber)->setData(OrderUnitPriceRole, newPrice);
    orderTable->item(orderRow, OrderNumber)->setData(OrderSeatNumberRole, newSeatNumbers.join(QStringLiteral("、")));
    recordTransaction(difference >= 0 ? QStringLiteral("改签补款") : QStringLiteral("改签退款"),
                      targetRow, quantity, newPrice, difference,
                      ui->routeTable->item(targetRow, Remaining)->text().toInt());
    if (!saveBusinessState()) {
        ui->routeTable->item(sourceRow, Remaining)->setText(sourceRemainingBefore);
        sourceNumber->setData(SoldRole, sourceSoldBefore);
        sourceNumber->setData(RevenueRole, sourceRevenueBefore);
        ui->routeTable->item(targetRow, Remaining)->setText(targetRemainingBefore);
        targetNumber->setData(SoldRole, targetSoldBefore);
        targetNumber->setData(RevenueRole, targetRevenueBefore);
        const QList<int> columns{OrderRoute, OrderDate, OrderSeatType, OrderJourney, OrderAmount};
        for (int index = 0; index < columns.size(); ++index)
            orderTable->item(orderRow, columns.at(index))->setText(orderBefore.at(index));
        orderTable->item(orderRow, OrderNumber)->setData(OrderUnitPriceRole, unitPriceBefore);
        orderTable->item(orderRow, OrderNumber)->setData(OrderSeatNumberRole, seatNumberBefore);
        transactionTable->removeRow(0);
        QMessageBox::critical(this, QStringLiteral("改签未保存"),
                              QStringLiteral("本次改签已回滚，请检查系统权限后重试。"));
        return;
    }
    updateStatistics();
    refreshSeatInventoryPage();
    refreshTicketRouteChoices();
    QMessageBox::information(this, QStringLiteral("改签成功"),
        QStringLiteral("订单已改签完成，%1 ¥%2。")
            .arg(difference >= 0 ? QStringLiteral("需补收") : QStringLiteral("应退还"))
            .arg(qAbs(difference), 0, 'f', 2));
}

// 根据订单保存的班次号定位当前班次，供订单退票恢复库存和经营统计。
int Widget::findRouteRow(const QString &routeNumber,
                         const QString &departureDate,
                         const QString &seatType) const
{
    for (int row = 0; row < ui->routeTable->rowCount(); ++row) {
        if (ui->routeTable->item(row, Number)->text() == routeNumber
            && ui->routeTable->item(row, DepartureDate)->text() == departureDate
            && ui->routeTable->item(row, SeatType)->text() == seatType) {
            return row;
        }
    }
    return -1;
}

// 按订单整单退票：检查状态后恢复对应班次库存，并把订单标记为已退票。
void Widget::refundSelectedOrder()
{
    const int orderRow = orderTable->currentRow();
    if (orderRow < 0 || orderTable->isRowHidden(orderRow)
        || !orderTable->item(orderRow, OrderStatus)) {
        QMessageBox::information(this, QStringLiteral("请选择订单"),
                                 QStringLiteral("请先选择需要退票的订单。"));
        return;
    }
    if (orderTable->item(orderRow, OrderStatus)->text() != QStringLiteral("已出票")) {
        QMessageBox::information(this, QStringLiteral("订单不可退"),
                                 QStringLiteral("该订单已经办理过退票。"));
        return;
    }

    const QString routeNumber = orderTable->item(orderRow, OrderRoute)->text();
    const QString departureDate = orderTable->item(orderRow, OrderDate)->text();
    const QString seatType = orderTable->item(orderRow, OrderSeatType)->text();
    const int routeRow = findRouteRow(routeNumber, departureDate, seatType);
    if (routeRow < 0) {
        QMessageBox::warning(this, QStringLiteral("班次不存在"),
                             QStringLiteral("该订单对应的班次已被删除，无法自动恢复库存。"));
        return;
    }
    // 退票规则：已发车不可退，按距离发车时间计算手续费与实际退款。
    const QDate routeDate = QDate::fromString(departureDate, QStringLiteral("yyyy-MM-dd"));
    const QTime routeTime = QTime::fromString(ui->routeTable->item(routeRow, DepartureTime)->text(), QStringLiteral("HH:mm"));
    const qint64 secondsToDeparture = QDateTime::currentDateTime().secsTo(QDateTime(routeDate, routeTime));
    if (secondsToDeparture <= 0) {
        QMessageBox::warning(this, QStringLiteral("订单不可退"), QStringLiteral("该班次已经发车，不能办理退票。"));
        return;
    }
    const double feeRate = secondsToDeparture > 48 * 3600 ? 0.0
                           : secondsToDeparture > 24 * 3600 ? 0.05 : 0.10;
    const int quantity = orderData(orderTable, orderRow, OrderQuantityRole).toInt();
    const double amount = orderTable->item(orderRow, OrderAmount)->text()
                              .remove(QChar(0x00A5)).toDouble();
    const double feeAmount = amount * feeRate;
    const double refundAmount = amount - feeAmount;
    if (QMessageBox::question(
            this, QStringLiteral("确认订单退票"),
            QStringLiteral("订单：%1\n旅客：%2\n班次：%3　%4　%5\n数量：%6 张\n原票款：¥%7\n手续费：%8%（¥%9）\n实际退款：¥%10\n\n确认整单退票吗？")
                .arg(orderTable->item(orderRow, OrderNumber)->text(),
                     orderTable->item(orderRow, OrderPassenger)->text(), routeNumber,
                     departureDate, seatType)
                .arg(quantity).arg(amount, 0, 'f', 2)
                .arg(qRound(feeRate * 100)).arg(feeAmount, 0, 'f', 2).arg(refundAmount, 0, 'f', 2),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) return;

    auto *numberItem = ui->routeTable->item(routeRow, Number);
    auto *remainingItem = ui->routeTable->item(routeRow, Remaining);
    const double unitPrice = quantity > 0 ? amount / quantity : 0.0;
    // 保存退票前状态；落盘失败时恢复库存、统计和订单状态。
    const QString remainingBefore = remainingItem->text();
    const int soldBefore = numberItem->data(SoldRole).toInt();
    const double revenueBefore = numberItem->data(RevenueRole).toDouble();
    remainingItem->setText(
        QString::number(remainingItem->text().toInt() + quantity));
    numberItem->setData(
        SoldRole, qMax(0, numberItem->data(SoldRole).toInt() - quantity));
    numberItem->setData(
        RevenueRole,
        qMax(0.0, numberItem->data(RevenueRole).toDouble() - refundAmount));
    orderTable->item(orderRow, OrderStatus)->setText(QStringLiteral("已退票"));
    orderTable->item(orderRow, OrderStatus)->setForeground(
        QColor(QStringLiteral("#b45309")));
    refundOrderButton->setEnabled(false);
    rescheduleOrderButton->setEnabled(false);
    if (auto *button = qobject_cast<QPushButton *>(orderTable->cellWidget(orderRow, OrderAction))) {
        button->setEnabled(false);
    }
    recordTransaction(QStringLiteral("订单退票（手续费%1%）").arg(qRound(feeRate * 100)), routeRow, -quantity,
                      unitPrice, -refundAmount, remainingItem->text().toInt());
    if (!saveBusinessState()) {
        remainingItem->setText(remainingBefore);
        numberItem->setData(SoldRole, soldBefore);
        numberItem->setData(RevenueRole, revenueBefore);
        orderTable->item(orderRow, OrderStatus)->setText(QStringLiteral("已出票"));
        orderTable->item(orderRow, OrderStatus)->setForeground(QColor(QStringLiteral("#15803d")));
        refundOrderButton->setEnabled(true);
        rescheduleOrderButton->setEnabled(true);
        if (auto *button = qobject_cast<QPushButton *>(orderTable->cellWidget(orderRow, OrderAction))) button->setEnabled(true);
        transactionTable->removeRow(0);
        QMessageBox::critical(this, QStringLiteral("退票未保存"),
                              QStringLiteral("本次退票已回滚，请检查系统权限后重试。"));
        return;
    }
    updateStatistics();
    updateTicketSelection(routeRow);
    QMessageBox::information(this, QStringLiteral("退票成功"),
                             QStringLiteral("订单已退票，库存恢复 %1 张。\n手续费 ¥%2，实际退款 ¥%3。")
                                 .arg(quantity).arg(feeAmount, 0, 'f', 2).arg(refundAmount, 0, 'f', 2));
}

// ==================== V0.9 交易流水 ====================
// 每次售票或退票都生成一条不可编辑的流水，便于追踪运营过程。
void Widget::recordTransaction(const QString &type, int routeRow,
                               int quantity, double unitPrice,
                               double amount, int remaining)
{
    const int row = 0;
    transactionTable->insertRow(row);
    const QString routeNumber =
        ui->routeTable->item(routeRow, Number)->text();
    const QString journey =
        QStringLiteral("%1　%2　%3 → %4　%5-%6")
            .arg(ui->routeTable->item(routeRow, DepartureDate)->text(),
                 ui->routeTable->item(routeRow, SeatType)->text(),
                 ui->routeTable->item(routeRow, Departure)->text(),
                 ui->routeTable->item(routeRow, Destination)->text(),
                 ui->routeTable->item(routeRow, DepartureTime)->text(),
                 ui->routeTable->item(routeRow, ArrivalTime)->text());

    const QStringList values{
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
        type,
        routeNumber,
        journey,
        QString::number(quantity),
        QStringLiteral("¥%1").arg(unitPrice, 0, 'f', 2),
        QStringLiteral("%1¥%2")
            .arg(amount < 0 ? QStringLiteral("-") : QString())
            .arg(qAbs(amount), 0, 'f', 2),
        QString::number(remaining)};

    for (int column = 0; column < values.size(); ++column) {
        auto *item = new QTableWidgetItem(values.at(column));
        item->setTextAlignment(Qt::AlignCenter);
        transactionTable->setItem(row, column, item);
    }
    transactionTable->item(row, 1)->setForeground(
        QColor(type == QStringLiteral("售票")
                   ? QStringLiteral("#6ee7b7")
                   : QStringLiteral("#fcd34d")));
    filterTransactions();
}

// ==================== V0.16 数据安全：快照与备份文件 ====================
// 将 QSettings 展平成带版本信息的 JSON，备份文件和失败回滚共用同一格式。
QJsonObject Widget::captureSettingsSnapshot() const
{
    QSettings settings(QStringLiteral("StudentQtProjects"), QStringLiteral("BusTicketSystem"));
    QJsonObject values;
    for (const QString &key : settings.allKeys())
        values.insert(key, QJsonValue::fromVariant(settings.value(key)));
    return {{QStringLiteral("appId"), QStringLiteral("bus_ticket_system")},
            {QStringLiteral("formatVersion"), 1},
            {QStringLiteral("appVersion"), QStringLiteral("V0.17")},
            {QStringLiteral("backupTime"), QDateTime::currentDateTime().toString(Qt::ISODate)},
            {QStringLiteral("settings"), values}};
}

// 校验备份来源和键名后再覆盖设置，避免错误 JSON 污染业务数据。
bool Widget::writeSettingsSnapshot(const QJsonObject &snapshot, QString *error) const
{
    const QJsonValue settingsValue = snapshot.value(QStringLiteral("settings"));
    if (snapshot.value(QStringLiteral("appId")).toString() != QStringLiteral("bus_ticket_system")
        || snapshot.value(QStringLiteral("formatVersion")).toInt() != 1
        || !settingsValue.isObject()) {
        if (error) *error = QStringLiteral("文件不是受支持的客运售票系统备份。");
        return false;
    }
    const QJsonObject values = settingsValue.toObject();
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        const QString key = it.key();
        if (key != QStringLiteral("stations")
            && !key.startsWith(QStringLiteral("routes/"))
            && !key.startsWith(QStringLiteral("orders/"))
            && !key.startsWith(QStringLiteral("transactions/"))) {
            if (error) *error = QStringLiteral("备份中包含未知数据项：%1").arg(key);
            return false;
        }
    }
    QSettings settings(QStringLiteral("StudentQtProjects"), QStringLiteral("BusTicketSystem"));
    settings.clear();
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
        settings.setValue(it.key(), it.value().toVariant());
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        if (error) *error = QStringLiteral("系统设置存储不可写，请检查当前用户权限。");
        return false;
    }
    return true;
}

// 使用 QSaveFile 原子写入：只有完整内容成功写出后才替换目标文件。
bool Widget::writeBackupFile(const QString &path, const QJsonObject &snapshot,
                             QString *error) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    const QByteArray payload = QJsonDocument(snapshot).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

// ==================== V0.16 数据安全：界面重载与操作反馈 ====================
void Widget::reloadBusinessData()
{
    ui->routeTable->setRowCount(0);
    orderTable->setRowCount(0);
    transactionTable->setRowCount(0);
    stationNames.clear();
    loadStations();
    loadRoutes();
    loadTransactions();
    loadOrders();
    filterOrders();
    filterTransactions();
    updateStatistics();
    updateTicketSelection(ui->routeTable->currentRow());
}

// 顶部状态徽标显示即时结果，数秒后自动恢复为正常运行状态。
void Widget::showOperationFeedback(const QString &message, bool success)
{
    if (!systemBadge) return;
    systemBadge->setText((success ? QStringLiteral("✓  ") : QStringLiteral("⚠  ")) + message);
    systemBadge->setProperty("feedbackState", success ? "success" : "error");
    systemBadge->style()->unpolish(systemBadge);
    systemBadge->style()->polish(systemBadge);
    QTimer::singleShot(3800, this, [this] {
        if (!systemBadge) return;
        systemBadge->setText(databaseReady
            ? QStringLiteral("●  SQLite 数据库正常")
            : QStringLiteral("⚠  兼容存储模式"));
        systemBadge->setProperty("feedbackState", "normal");
        systemBadge->style()->unpolish(systemBadge);
        systemBadge->style()->polish(systemBadge);
    });
}

// ==================== V0.16 数据安全：手动备份与恢复 ====================
void Widget::backupBusinessData()
{
    if (!saveBusinessState()) return;
    const QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        + QStringLiteral("/客运售票系统备份_%1.json").arg(
              QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("备份全部业务数据"), defaultPath,
        QStringLiteral("JSON 备份文件 (*.json)"));
    if (path.isEmpty()) return;
    QString error;
    if (!writeBackupFile(path, captureSettingsSnapshot(), &error)) {
        showOperationFeedback(QStringLiteral("备份失败"), false);
        QMessageBox::critical(this, QStringLiteral("备份失败"), error);
        return;
    }
    showOperationFeedback(QStringLiteral("数据备份完成"));
    QMessageBox::information(this, QStringLiteral("备份完成"),
                             QStringLiteral("全部业务数据已安全保存到：\n%1").arg(path));
}

void Widget::restoreBusinessData()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择业务数据备份"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        QStringLiteral("JSON 备份文件 (*.json)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 20 * 1024 * 1024) {
        showOperationFeedback(QStringLiteral("备份文件无法读取"), false);
        QMessageBox::critical(this, QStringLiteral("恢复失败"),
                              QStringLiteral("文件无法读取或大小超过 20 MB。"));
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    const QJsonObject snapshot = document.object();
    const QJsonObject values = snapshot.value(QStringLiteral("settings")).toObject();
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || snapshot.value(QStringLiteral("appId")).toString() != QStringLiteral("bus_ticket_system")
        || snapshot.value(QStringLiteral("formatVersion")).toInt() != 1
        || !snapshot.value(QStringLiteral("settings")).isObject()) {
        showOperationFeedback(QStringLiteral("备份格式无效"), false);
        QMessageBox::critical(this, QStringLiteral("恢复失败"),
                              QStringLiteral("所选文件不是有效的本系统备份。"));
        return;
    }
    const auto answer = QMessageBox::warning(
        this, QStringLiteral("确认恢复数据"),
        QStringLiteral("即将恢复 %1 条班次库存、%2 条订单和 %3 条交易流水。\n"
                       "当前数据会先自动备份，确认继续吗？")
            .arg(values.value(QStringLiteral("routes/size")).toInt())
            .arg(values.value(QStringLiteral("orders/size")).toInt())
            .arg(values.value(QStringLiteral("transactions/size")).toInt()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    const QJsonObject currentSnapshot = captureSettingsSnapshot();
    const QString safetyPath = QFileInfo(path).absolutePath()
        + QStringLiteral("/恢复前自动备份_%1.json").arg(
              QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    QString error;
    if (!writeBackupFile(safetyPath, currentSnapshot, &error)) {
        showOperationFeedback(QStringLiteral("恢复前备份失败"), false);
        QMessageBox::critical(this, QStringLiteral("恢复已取消"),
                              QStringLiteral("无法创建恢复前安全备份：%1").arg(error));
        return;
    }
    if (!writeSettingsSnapshot(snapshot, &error)
        || (databaseReady
            && !DatabaseManager::instance().replaceAll(snapshot, &error))) {
        // JSON 恢复和 SQLite 更新视为一个整体，任一步失败都恢复原快照。
        QString rollbackError;
        writeSettingsSnapshot(currentSnapshot, &rollbackError);
        if (databaseReady)
            DatabaseManager::instance().replaceAll(currentSnapshot, &rollbackError);
        showOperationFeedback(QStringLiteral("恢复失败，原数据已保留"), false);
        QMessageBox::critical(this, QStringLiteral("恢复失败"), error);
        return;
    }
    reloadBusinessData();
    lastSavedSettings = captureSettingsSnapshot();
    showOperationFeedback(QStringLiteral("数据恢复完成"));
    QMessageBox::information(this, QStringLiteral("恢复完成"),
        QStringLiteral("业务数据已恢复。\n恢复前的数据备份在：\n%1").arg(safetyPath));
}

// ==================== V0.16 数据一致性体检 ====================
// 只读检查库存恒等式、关联关系、金额和座位冲突，不自动改动业务数据。
void Widget::runDataHealthCheck()
{
    QStringList issues;
    QSet<QString> routeKeys;
    int routeRows = 0;
    for (int row = 0; row < ui->routeTable->rowCount(); ++row) {
        bool complete = true;
        for (int column = 0; column < ColumnCount; ++column)
            complete = complete && ui->routeTable->item(row, column);
        if (!complete) {
            issues << QStringLiteral("班次表第 %1 行存在缺失字段。").arg(row + 1);
            continue;
        }
        ++routeRows;
        const QString number = ui->routeTable->item(row, Number)->text().trimmed();
        const QString date = ui->routeTable->item(row, DepartureDate)->text().trimmed();
        const QString seat = ui->routeTable->item(row, SeatType)->text().trimmed();
        const QString key = number.toUpper() + QChar('|') + date + QChar('|') + seat;
        if (routeKeys.contains(key))
            issues << QStringLiteral("发现重复班次席位：%1 / %2 / %3。").arg(number, date, seat);
        routeKeys.insert(key);
        const int total = ui->routeTable->item(row, TotalSeats)->text().toInt();
        const int remaining = ui->routeTable->item(row, Remaining)->text().toInt();
        const int sold = ui->routeTable->item(row, Number)->data(SoldRole).toInt();
        if (total < 0 || remaining < 0 || sold < 0 || remaining + sold != total)
            issues << QStringLiteral("%1 %2 %3 库存不一致：总数 %4，余票 %5，已售 %6。")
                          .arg(number, date, seat).arg(total).arg(remaining).arg(sold);
        if (!stationNames.contains(ui->routeTable->item(row, Departure)->text())
            || !stationNames.contains(ui->routeTable->item(row, Destination)->text()))
            issues << QStringLiteral("%1 %2 使用了站点字典之外的站名。").arg(number, date);
        if (!QDate::fromString(date, QStringLiteral("yyyy-MM-dd")).isValid()
            || !QTime::fromString(ui->routeTable->item(row, DepartureTime)->text(),
                                  QStringLiteral("HH:mm")).isValid())
            issues << QStringLiteral("%1 %2 的日期或发车时间格式无效。").arg(number, date);
    }

    QSet<QString> occupiedSeats;
    int orderRows = 0;
    for (int row = 0; row < orderTable->rowCount(); ++row) {
        if (isOrderDetailRow(orderTable, row)) continue;
        if (!orderTable->item(row, OrderNumber) || !orderTable->item(row, OrderStatus)) {
            issues << QStringLiteral("订单表第 %1 行存在缺失字段。").arg(row + 1);
            continue;
        }
        ++orderRows;
        const QString orderNo = orderTable->item(row, OrderNumber)->text();
        const int quantity = qMax(1, orderTable->item(row, OrderNumber)
                                         ->data(OrderQuantityRole).toInt());
        const double unitPrice = orderTable->item(row, OrderNumber)
                                     ->data(OrderUnitPriceRole).toDouble();
        QString amountText = orderTable->item(row, OrderAmount)->text();
        amountText.remove(QChar(0x00A5));
        if (qAbs(amountText.toDouble() - unitPrice * quantity) > 0.011)
            issues << QStringLiteral("订单 %1 的订单金额与单价、数量不一致。").arg(orderNo);
        if (orderTable->item(row, OrderStatus)->text() == QStringLiteral("已出票")) {
            const QString number = orderTable->item(row, OrderRoute)->text();
            const QString date = orderTable->item(row, OrderDate)->text();
            const QString seatType = orderTable->item(row, OrderSeatType)->text();
            if (findRouteRow(number, date, seatType) < 0)
                issues << QStringLiteral("有效订单 %1 找不到对应班次席位。").arg(orderNo);
            const QString seatNumbers = cleanSeatNumber(
                orderTable->item(row, OrderNumber)->data(OrderSeatNumberRole).toString());
            if (seatNumbers != QStringLiteral("—")) {
                for (const QString &seatNo : seatNumbers.split(QStringLiteral("、"))) {
                    const QString seatKey = number.toUpper() + QChar('|') + date
                        + QChar('|') + seatType + QChar('|') + seatNo;
                    if (occupiedSeats.contains(seatKey))
                        issues << QStringLiteral("座位冲突：%1 %2 %3 %4 被重复占用。")
                                      .arg(number, date, seatType, seatNo);
                    occupiedSeats.insert(seatKey);
                }
            }
        }
        if (issues.size() >= 100) {
            issues << QStringLiteral("问题过多，已停止继续扫描。");
            break;
        }
    }

    if (issues.isEmpty()) {
        showOperationFeedback(QStringLiteral("数据体检通过"));
        QMessageBox::information(this, QStringLiteral("数据体检通过"),
            QStringLiteral("已检查 %1 条班次库存和 %2 条订单。\n"
                           "库存、金额、站点、班次关联及座位占用均未发现异常。")
                .arg(routeRows).arg(orderRows));
        return;
    }
    showOperationFeedback(QStringLiteral("发现 %1 项数据问题").arg(issues.size()), false);
    QMessageBox report(QMessageBox::Warning, QStringLiteral("数据体检报告"),
        QStringLiteral("共发现 %1 项需要关注的数据问题。\n"
                       "点击“显示详细信息”查看完整清单，系统未自动修改数据。")
            .arg(issues.size()), QMessageBox::Ok, this);
    report.setDetailedText(issues.join(QChar('\n')));
    report.exec();
}

// ==================== V0.17 SQLite 事务保存与失败回滚 ====================
// 界面先生成兼容快照，再用数据库事务一次性更新五类业务表，避免只保存一半。
bool Widget::saveBusinessState()
{
    const QJsonObject previous = lastSavedSettings;
    saveRoutes();
    saveOrders();
    saveTransactions();
    saveStations();
    QSettings settings(QStringLiteral("StudentQtProjects"), QStringLiteral("BusTicketSystem"));
    settings.sync();
    const QJsonObject current = captureSettingsSnapshot();
    QString databaseError;
    const bool settingsSaved = settings.status() == QSettings::NoError;
    const bool databaseSaved = !databaseReady
        || DatabaseManager::instance().replaceAll(current, &databaseError);
    if (!settingsSaved || !databaseSaved) {
        QString rollbackError;
        const bool restored = !previous.isEmpty()
            && writeSettingsSnapshot(previous, &rollbackError);
        // SQLite 的 replaceAll 自带事务，失败时数据库会自动保持旧状态。
        QTimer::singleShot(0, this, [this] { reloadBusinessData(); });
        showOperationFeedback(restored ? QStringLiteral("保存失败，已自动回滚")
                                       : QStringLiteral("保存失败，请立即备份现有数据"), false);
        return false;
    }
    lastSavedSettings = current;
    showOperationFeedback(databaseReady ? QStringLiteral("数据已写入 SQLite")
                                        : QStringLiteral("数据已安全保存"));
    return true;
}


// 将交易流水写入独立的 QSettings 数组，不与班次数据混在一起。
void Widget::saveTransactions() const
{
    QSettings settings(QStringLiteral("StudentQtProjects"),
                       QStringLiteral("BusTicketSystem"));
    settings.beginWriteArray(QStringLiteral("transactions"));
    for (int row = 0; row < transactionTable->rowCount(); ++row) {
        settings.setArrayIndex(row);
        for (int column = 0; column < transactionTable->columnCount(); ++column) {
            settings.setValue(
                QStringLiteral("column%1").arg(column),
                transactionTable->item(row, column)->text());
        }
    }
    settings.endArray();
}

// 启动时恢复历史流水；V0.8 没有流水数据时保持空表即可。
void Widget::loadTransactions()
{
    QSettings settings(QStringLiteral("StudentQtProjects"),
                       QStringLiteral("BusTicketSystem"));
    const int count =
        settings.beginReadArray(QStringLiteral("transactions"));
    for (int row = 0; row < count; ++row) {
        settings.setArrayIndex(row);
        transactionTable->insertRow(row);
        for (int column = 0; column < transactionTable->columnCount(); ++column) {
            auto *item = new QTableWidgetItem(
                settings.value(QStringLiteral("column%1").arg(column)).toString());
            item->setTextAlignment(Qt::AlignCenter);
            transactionTable->setItem(row, column, item);
        }
        if (transactionTable->item(row, 1)) {
            transactionTable->item(row, 1)->setForeground(
                QColor(transactionTable->item(row, 1)->text()
                               == QStringLiteral("售票")
                           ? QStringLiteral("#6ee7b7")
                           : QStringLiteral("#fcd34d")));
        }
    }
    settings.endArray();
}

// 流水搜索支持任意列关键词，输入内容后即时过滤。
void Widget::filterTransactions()
{
    const QString keyword = transactionSearchEdit->text().trimmed();
    for (int row = 0; row < transactionTable->rowCount(); ++row) {
        bool matched = keyword.isEmpty();
        for (int column = 0;
             !matched && column < transactionTable->columnCount(); ++column) {
            const auto *item = transactionTable->item(row, column);
            matched = item
                      && item->text().contains(keyword, Qt::CaseInsensitive);
        }
        transactionTable->setRowHidden(row, !matched);
    }
}

// 将当前全部流水导出为 UTF-8 CSV，便于用 Excel 打开或提交报表。
void Widget::exportTransactions()
{
    if (transactionTable->rowCount() == 0) {
        QMessageBox::information(this, QStringLiteral("没有可导出的数据"),
                                 QStringLiteral("当前交易流水为空。"));
        return;
    }

    const QString defaultName =
        QStringLiteral("交易流水_%1.csv")
            .arg(QDateTime::currentDateTime()
                     .toString(QStringLiteral("yyyyMMdd_HHmmss")));
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出交易流水"), defaultName,
        QStringLiteral("CSV 文件 (*.csv)"));
    if (path.isEmpty()) return;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"),
                             QStringLiteral("无法写入所选文件。"));
        return;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << QChar(0xFEFF);
    const auto escapeCsv = [](QString value) {
        value.replace(QChar('"'), QStringLiteral("\"\""));
        return QStringLiteral("\"%1\"").arg(value);
    };

    for (int column = 0; column < transactionTable->columnCount(); ++column) {
        if (column > 0) stream << ',';
        stream << escapeCsv(
            transactionTable->horizontalHeaderItem(column)->text());
    }
    stream << '\n';

    int exportedCount = 0;
    for (int row = 0; row < transactionTable->rowCount(); ++row) {
        if (transactionTable->isRowHidden(row)) continue;
        ++exportedCount;
        for (int column = 0; column < transactionTable->columnCount(); ++column) {
            if (column > 0) stream << ',';
            stream << escapeCsv(transactionTable->item(row, column)->text());
        }
        stream << '\n';
    }

    if (!file.commit()) {
        QMessageBox::warning(this, QStringLiteral("导出失败"),
                             QStringLiteral("保存 CSV 文件时发生错误。"));
        return;
    }
    QMessageBox::information(
        this, QStringLiteral("导出成功"),
        QStringLiteral("已导出 %1 条当前筛选流水：\n%2").arg(exportedCount).arg(path));
}

// ==================== V0.12 站点字典 ====================
// 站点字典继续使用 QSettings 保存，和现有数据架构保持一致。
void Widget::loadStations()
{
    QSettings settings(QStringLiteral("StudentQtProjects"), QStringLiteral("BusTicketSystem"));
    stationNames = settings.value(QStringLiteral("stations")).toStringList();
    if (stationNames.isEmpty()) {
        stationNames = {QStringLiteral("北京"), QStringLiteral("上海"), QStringLiteral("广州"),
                        QStringLiteral("深圳"), QStringLiteral("南京"), QStringLiteral("杭州"),
                        QStringLiteral("苏州"), QStringLiteral("天津"), QStringLiteral("重庆"),
                        QStringLiteral("成都"), QStringLiteral("武汉"), QStringLiteral("西安"),
                        QStringLiteral("郑州"), QStringLiteral("长沙"), QStringLiteral("太原"),
                        QStringLiteral("大同"), QStringLiteral("福州"), QStringLiteral("厦门")};
        saveStations();
    }
    // zh_CN 的 QCollator 按中文拼音校对规则排序，而不是按 Unicode 编码值排序。
    QCollator stationCollator(QLocale(QLocale::Chinese, QLocale::China));
    stationCollator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(stationNames.begin(), stationNames.end(),
              [&stationCollator](const QString &left, const QString &right) {
                  return stationCollator.compare(left, right) < 0;
              });
    for (QComboBox *combo : {departureStationCombo, destinationStationCombo}) {
        if (!combo) continue;
        const QString previous = combo->currentText();
        const QSignalBlocker blocker(combo);
        combo->clear();
        combo->addItems(stationNames);
        combo->setCurrentText(previous);
        combo->completer()->setCaseSensitivity(Qt::CaseInsensitive);
        combo->completer()->setFilterMode(Qt::MatchContains);
        combo->completer()->setCompletionMode(QCompleter::PopupCompletion);
    }
}

void Widget::saveStations() const
{
    QSettings settings(QStringLiteral("StudentQtProjects"), QStringLiteral("BusTicketSystem"));
    settings.setValue(QStringLiteral("stations"), stationNames);
}

// ==================== 自动保存数据 ====================
// 把表格所有行写入 QSettings；添加、修改、删除后都会调用这里。
void Widget::saveRoutes() const
{
    QSettings settings(QStringLiteral("StudentQtProjects"),
                       QStringLiteral("BusTicketSystem"));
    settings.beginWriteArray(QStringLiteral("routes"));
    for (int row = 0; row < ui->routeTable->rowCount(); ++row) {
        settings.setArrayIndex(row);
        settings.setValue(QStringLiteral("number"),
                          ui->routeTable->item(row, Number)->text());
        settings.setValue(QStringLiteral("departureDate"),
                          ui->routeTable->item(row, DepartureDate)->text());
        settings.setValue(QStringLiteral("departure"),
                          ui->routeTable->item(row, Departure)->text());
        settings.setValue(QStringLiteral("destination"),
                          ui->routeTable->item(row, Destination)->text());
        settings.setValue(QStringLiteral("time"),
                          ui->routeTable->item(row, DepartureTime)->text());
        settings.setValue(QStringLiteral("arrivalTime"),
                          ui->routeTable->item(row, ArrivalTime)->text());
        settings.setValue(QStringLiteral("seatType"),
                          ui->routeTable->item(row, SeatType)->text());
        settings.setValue(QStringLiteral("price"),
                          ui->routeTable->item(row, Price)->text().remove(QChar(0x00A5)));
        settings.setValue(QStringLiteral("totalSeats"),
                          ui->routeTable->item(row, TotalSeats)->text());
        settings.setValue(QStringLiteral("remaining"),
                          ui->routeTable->item(row, Remaining)->text());
        settings.setValue(QStringLiteral("status"),
                          ui->routeTable->item(row, Status)->text());
        // V0.8 新增：保存每个班次的累计销量和累计营业额。
        const auto *numberItem = ui->routeTable->item(row, Number);
        settings.setValue(QStringLiteral("sold"),
                          numberItem->data(SoldRole).toInt());
        settings.setValue(QStringLiteral("revenue"),
                          numberItem->data(RevenueRole).toDouble());
    }
    settings.endArray();
}

// ==================== 自动读取数据 ====================
// 程序启动时从 QSettings 恢复数据，并兼容旧版本只有三列的记录。
void Widget::loadRoutes()
{
    QSettings settings(QStringLiteral("StudentQtProjects"),
                       QStringLiteral("BusTicketSystem"));
    const int count = settings.beginReadArray(QStringLiteral("routes"));
    for (int index = 0; index < count; ++index) {
        settings.setArrayIndex(index);
        const int row = ui->routeTable->rowCount();
        ui->routeTable->insertRow(row);
        const QStringList values{
            settings.value(QStringLiteral("number")).toString(),
            settings.value(QStringLiteral("departureDate"),
                           QDate::currentDate().toString(
                               QStringLiteral("yyyy-MM-dd"))).toString(),
            settings.value(QStringLiteral("departure")).toString(),
            settings.value(QStringLiteral("destination")).toString(),
            settings.value(QStringLiteral("time"), QStringLiteral("待补充")).toString(),
            settings.value(QStringLiteral("arrivalTime"),
                           QStringLiteral("待补充")).toString(),
            settings.value(QStringLiteral("seatType"),
                           QStringLiteral("标准座")).toString(),
            QStringLiteral("¥%1").arg(
                settings.value(QStringLiteral("price"), QStringLiteral("0.00")).toString()),
            settings.value(
                QStringLiteral("totalSeats"),
                settings.value(QStringLiteral("remaining"), 0).toInt()
                    + settings.value(QStringLiteral("sold"), 0).toInt()).toString(),
            settings.value(QStringLiteral("remaining"), 0).toString(),
            settings.value(QStringLiteral("status"), QStringLiteral("启用")).toString()};
        for (int column = 0; column < ColumnCount; ++column) {
            ui->routeTable->setItem(row, column,
                                    new QTableWidgetItem(values.at(column)));
        }
        // 旧版本没有售票统计时自动使用 0，保证历史数据仍然可以打开。
        ui->routeTable->item(row, Number)->setData(
            SoldRole, settings.value(QStringLiteral("sold"), 0).toInt());
        ui->routeTable->item(row, Number)->setData(
            RevenueRole, settings.value(QStringLiteral("revenue"), 0.0).toDouble());
    }
    settings.endArray();
}

// ==================== 窗口销毁 ====================
// 释放由界面生成器创建的控件管理对象。
Widget::~Widget()
{
    delete ui;
}


