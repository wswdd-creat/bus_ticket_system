#include "widget.h"
#include "ui_widget.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QProgressBar>
#include <QSaveFile>
#include <QSettings>
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

// ==================== 表格列定义与通用辅助工具 ====================
// 用枚举代替数字下标，后续看到 Number、Price 等名称就能知道对应哪一列。
namespace {
constexpr int ColumnCount = 6;
enum Column { Number, Departure, Destination, DepartureTime, Price, Remaining };

// 售票统计保存在“班次号”单元格的自定义数据角色中，不占用可见表格列。
constexpr int SoldRole = Qt::UserRole + 1;
constexpr int RevenueRole = Qt::UserRole + 2;

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

// ==================== 页面初始化与功能绑定 ====================
// 构造函数：窗口创建时依次完成界面初始化、信号连接和历史数据读取。
Widget::Widget(QWidget *parent)
    : QWidget(parent), ui(new Ui::Widget)
{
    // 读取 widget.ui，并创建 Designer 中设计的所有控件。
    ui->setupUi(this);
    setWindowTitle(QStringLiteral("客运售票运营中心 · V0.10"));
    setMinimumSize(1100, 650);

    ui->verticalLayout_2->setContentsMargins(28, 22, 28, 22);
    ui->verticalLayout->setSpacing(14);
    ui->gridLayout->setHorizontalSpacing(12);
    ui->gridLayout->setVerticalSpacing(12);
    ui->horizontalLayout->setSpacing(10);
    ui->titleLabel->setAlignment(Qt::AlignCenter);
    ui->titleLabel->setMinimumHeight(64);

    // ---------- V0.7 新增输入框：发车时间、票价、余票 ----------
    // 这三个控件在代码中动态创建，并插入 Designer 原有的输入栏。
    auto *timeEdit = new QLineEdit(this);
    timeEdit->setObjectName(QStringLiteral("departureTimeEdit"));
    timeEdit->setPlaceholderText(QStringLiteral("发车时间 08:30"));
    timeEdit->setInputMask(QStringLiteral("00:00;_"));

    auto *priceEdit = new QLineEdit(this);
    priceEdit->setObjectName(QStringLiteral("priceEdit"));
    priceEdit->setPlaceholderText(QStringLiteral("票价（元）"));
    priceEdit->setValidator(new QDoubleValidator(0.0, 99999.99, 2, priceEdit));

    auto *seatsEdit = new QLineEdit(this);
    seatsEdit->setObjectName(QStringLiteral("remainingEdit"));
    seatsEdit->setPlaceholderText(QStringLiteral("余票"));
    seatsEdit->setValidator(new QIntValidator(0, 99999, seatsEdit));

    ui->horizontalLayout->insertWidget(3, timeEdit);
    ui->horizontalLayout->insertWidget(4, priceEdit);
    ui->horizontalLayout->insertWidget(5, seatsEdit);

    for (QLineEdit *edit : {ui->searchEdit, ui->routeNumberEdit,
                            ui->departureEdit, ui->destinationEdit,
                            timeEdit, priceEdit, seatsEdit}) {
        edit->setClearButtonEnabled(true);
    }

    ui->searchEdit->setPlaceholderText(
        QStringLiteral("搜索班次号、地点、时间、票价或余票"));

    // ---------- 班次表格初始化 ----------
    // 设置六列、表头、整行单选以及禁止直接修改单元格。
    ui->routeTable->setColumnCount(ColumnCount);
    ui->routeTable->setHorizontalHeaderLabels(
        {QStringLiteral("班次号"), QStringLiteral("出发地"),
         QStringLiteral("目的地"), QStringLiteral("发车时间"),
         QStringLiteral("票价"), QStringLiteral("余票")});
    ui->routeTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->routeTable->horizontalHeader()->setMinimumHeight(42);
    ui->routeTable->verticalHeader()->setVisible(false);
    ui->routeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->routeTable->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->routeTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->routeTable->setAlternatingRowColors(true);
    ui->routeTable->setShowGrid(false);

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

    auto *systemBadge = new QLabel(QStringLiteral("●  系统运行正常"), heroPanel);
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
    quickLayout->addStretch();

    overviewDetails->addWidget(healthCard, 3);
    overviewDetails->addWidget(quickCard, 2);
    overviewLayout->addLayout(overviewDetails, 1);

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
    actionToolbar->addStretch();

    // 录入区：六项班次字段分成两行，避免超宽输入栏显得拥挤。
    auto *inputPanel = new QFrame(managementPanel);
    inputPanel->setObjectName(QStringLiteral("routeInputPanel"));
    auto *inputGrid = new QGridLayout(inputPanel);
    inputGrid->setContentsMargins(12, 12, 12, 12);
    inputGrid->setHorizontalSpacing(10);
    inputGrid->setVerticalSpacing(10);
    inputGrid->addWidget(ui->routeNumberEdit, 0, 0);
    inputGrid->addWidget(ui->departureEdit, 0, 1);
    inputGrid->addWidget(ui->destinationEdit, 0, 2);
    inputGrid->addWidget(timeEdit, 0, 3);
    inputGrid->addWidget(priceEdit, 1, 0);
    inputGrid->addWidget(seatsEdit, 1, 1);
    inputGrid->addWidget(ui->addRouteButton, 1, 2, 1, 2);

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
    auto *exportButton = new QPushButton(QStringLiteral("导出 CSV"), transactionPage);
    exportButton->setObjectName(QStringLiteral("exportButton"));
    auto *clearHistoryButton = new QPushButton(
        QStringLiteral("清空流水"), transactionPage);
    clearHistoryButton->setObjectName(QStringLiteral("clearHistoryButton"));
    exportButton->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    clearHistoryButton->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    transactionToolbar->addWidget(transactionSearchEdit, 1);
    transactionToolbar->addWidget(exportButton);
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

    contentTabs->addTab(
        overviewPage, style()->standardIcon(QStyle::SP_ComputerIcon),
        QStringLiteral("运营总览"));
    contentTabs->addTab(
        routePage, style()->standardIcon(QStyle::SP_FileDialogListView),
        QStringLiteral("班次管理"));
    contentTabs->addTab(
        ticketPage, style()->standardIcon(QStyle::SP_DialogApplyButton),
        QStringLiteral("售票中心"));
    contentTabs->addTab(
        transactionPage, style()->standardIcon(QStyle::SP_FileDialogDetailedView),
        QStringLiteral("交易流水"));
    contentTabs->setCurrentIndex(0);
    ui->verticalLayout->setStretchFactor(contentTabs, 1);

    // 总览页快捷按钮只负责页面导航，不复制业务逻辑。
    connect(manageShortcut, &QPushButton::clicked, this,
            [this] { contentTabs->setCurrentIndex(1); });
    connect(ticketShortcut, &QPushButton::clicked, this,
            [this] { contentTabs->setCurrentIndex(2); });
    connect(historyShortcut, &QPushButton::clicked, this,
            [this] { contentTabs->setCurrentIndex(3); });

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

    sellTicketButton = new QPushButton(QStringLiteral("确认售票"), ticketPanel);
    sellTicketButton->setObjectName(QStringLiteral("sellTicketButton"));
    sellTicketButton->setEnabled(false);
    refundTicketButton = new QPushButton(QStringLiteral("办理退票"), ticketPanel);
    refundTicketButton->setObjectName(QStringLiteral("refundTicketButton"));
    refundTicketButton->setEnabled(false);

    operationLayout->addWidget(quantityLabel);
    operationLayout->addWidget(decreaseQuantityButton);
    operationLayout->addWidget(ticketQuantitySpin);
    operationLayout->addWidget(increaseQuantityButton);
    operationLayout->addWidget(sellTicketButton);
    operationLayout->addWidget(refundTicketButton);
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
    });
    connect(sellTicketButton, &QPushButton::clicked,
            this, &Widget::sellTickets);
    connect(refundTicketButton, &QPushButton::clicked,
            this, &Widget::refundTickets);

    connect(transactionSearchEdit, &QLineEdit::textChanged,
            this, &Widget::filterTransactions);
    connect(exportButton, &QPushButton::clicked,
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
            saveTransactions();
        }
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
    refundTicketButton->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));

    ui->searchButton->setToolTip(QStringLiteral("按条件筛选班次"));
    ui->showAllButton->setToolTip(QStringLiteral("清除条件并显示全部班次"));
    sellTicketButton->setToolTip(QStringLiteral("为当前选中班次售票"));
    refundTicketButton->setToolTip(QStringLiteral("为当前选中班次退票"));

    // 全局视觉样式由 main.cpp 从 :/bus_ticket_system/styles/app.qss 统一加载。

    const auto clearInputs = [this, timeEdit, priceEdit, seatsEdit] {
        ui->routeNumberEdit->clear();
        ui->departureEdit->clear();
        ui->destinationEdit->clear();
        timeEdit->clear();
        priceEdit->clear();
        seatsEdit->clear();
        ui->routeNumberEdit->setFocus();
    };

    const auto readInputs = [this, timeEdit, priceEdit, seatsEdit] {
        return QStringList{ui->routeNumberEdit->text().trimmed(),
                           ui->departureEdit->text().trimmed(),
                           ui->destinationEdit->text().trimmed(),
                           timeEdit->text().trimmed(),
                           priceEdit->text().trimmed(),
                           seatsEdit->text().trimmed()};
    };

    // ---------- 输入校验 ----------
    // 检查必填项、时间格式、票价和余票，错误时弹窗并阻止继续操作。
    const auto validate = [this](const QStringList &values) {
        for (const QString &value : values) {
            if (value.isEmpty()) {
                QMessageBox::warning(
                    this, QStringLiteral("输入不完整"),
                    QStringLiteral("请填写班次号、出发地、目的地、发车时间、票价和余票。"));
                return false;
            }
        }
        if (!QTime::fromString(values.at(DepartureTime), QStringLiteral("HH:mm")).isValid()) {
            QMessageBox::warning(this, QStringLiteral("时间格式错误"),
                                 QStringLiteral("发车时间请按 HH:mm 输入，例如 08:30。"));
            return false;
        }
        if (values.at(Price).toDouble() < 0 || values.at(Remaining).toInt() < 0) {
            QMessageBox::warning(this, QStringLiteral("数值错误"),
                                 QStringLiteral("票价和余票不能为负数。"));
            return false;
        }
        return true;
    };

    connect(ui->addRouteButton, &QPushButton::clicked, this,
            [this, clearInputs, readInputs, validate] {
        const QStringList values = readInputs();
        if (!validate(values)) return;

        for (int row = 0; row < ui->routeTable->rowCount(); ++row) {
            const auto *item = ui->routeTable->item(row, Number);
            if (item && item->text().compare(values.at(Number), Qt::CaseInsensitive) == 0) {
                QMessageBox::warning(this, QStringLiteral("班次已存在"),
                                     QStringLiteral("班次号不能重复。"));
                return;
            }
        }

        const int row = ui->routeTable->rowCount();
        ui->routeTable->insertRow(row);
        for (int column = 0; column < ColumnCount; ++column) {
            QString display = values.at(column);
            if (column == Price) display = QStringLiteral("¥%1").arg(display);
            ui->routeTable->setItem(row, column, new QTableWidgetItem(display));
        }
        // 新班次还没有发生交易，累计销量和营业额均从 0 开始。
        ui->routeTable->item(row, Number)->setData(SoldRole, 0);
        ui->routeTable->item(row, Number)->setData(RevenueRole, 0.0);
        clearInputs();
        saveRoutes();
        updateStatistics();
    });

    connect(ui->editRouteButton, &QPushButton::clicked, this,
            [this, timeEdit, priceEdit, seatsEdit] {
        const int row = ui->routeTable->currentRow();
        if (row < 0) {
            QMessageBox::information(this, QStringLiteral("提示"),
                                     QStringLiteral("请先选择要修改的班次。"));
            return;
        }
        editingRow = row;
        ui->routeNumberEdit->setText(ui->routeTable->item(row, Number)->text());
        ui->departureEdit->setText(ui->routeTable->item(row, Departure)->text());
        ui->destinationEdit->setText(ui->routeTable->item(row, Destination)->text());
        timeEdit->setText(ui->routeTable->item(row, DepartureTime)->text());
        priceEdit->setText(ui->routeTable->item(row, Price)->text().remove(QChar(0x00A5)));
        seatsEdit->setText(ui->routeTable->item(row, Remaining)->text());
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

        for (int row = 0; row < ui->routeTable->rowCount(); ++row) {
            const auto *item = ui->routeTable->item(row, Number);
            if (row != editingRow && item
                && item->text().compare(values.at(Number), Qt::CaseInsensitive) == 0) {
                QMessageBox::warning(this, QStringLiteral("班次已存在"),
                                     QStringLiteral("班次号不能重复。"));
                return;
            }
        }

        for (int column = 0; column < ColumnCount; ++column) {
            QString display = values.at(column);
            if (column == Price) display = QStringLiteral("¥%1").arg(display);
            ui->routeTable->item(editingRow, column)->setText(display);
        }

        editingRow = -1;
        clearInputs();
        ui->saveEditButton->setEnabled(false);
        ui->addRouteButton->setEnabled(true);
        ui->editRouteButton->setEnabled(true);
        ui->deleteRouteButton->setEnabled(true);
        ui->routeTable->clearSelection();
        saveRoutes();
        updateStatistics();
        updateTicketSelection(editingRow);
        QMessageBox::information(this, QStringLiteral("修改成功"),
                                 QStringLiteral("班次信息已经更新。"));
    });

    connect(ui->deleteRouteButton, &QPushButton::clicked, this, [this] {
        const int row = ui->routeTable->currentRow();
        if (row < 0) {
            QMessageBox::information(this, QStringLiteral("提示"),
                                     QStringLiteral("请先选择要删除的班次。"));
            return;
        }
        const QString number = ui->routeTable->item(row, Number)->text();
        if (QMessageBox::question(
                this, QStringLiteral("确认删除"),
                QStringLiteral("确定删除班次“%1”吗？").arg(number),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No) == QMessageBox::Yes) {
            ui->routeTable->removeRow(row);
            saveRoutes();
            updateStatistics();
            updateTicketSelection(-1);
        }
    });

    const auto search = [this] {
        const QString keyword = ui->searchEdit->text().trimmed();
        int matches = 0;
        for (int row = 0; row < ui->routeTable->rowCount(); ++row) {
            bool matched = keyword.isEmpty();
            for (int column = 0; !matched && column < ColumnCount; ++column) {
                const auto *item = ui->routeTable->item(row, column);
                matched = item && item->text().contains(keyword, Qt::CaseInsensitive);
            }
            ui->routeTable->setRowHidden(row, !matched);
            if (matched) ++matches;
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

    // 窗口启动的最后一步：从本地配置文件恢复上次保存的班次。
    loadRoutes();
    updateStatistics();
    updateTicketSelection(ui->routeTable->currentRow());
}

// ==================== V0.8 售票中心业务逻辑 ====================
// 根据表格当前选中行，展示班次摘要并决定售票、退票按钮是否可用。
void Widget::updateTicketSelection(int row)
{
    if (row < 0 || row >= ui->routeTable->rowCount()) {
        selectedRouteLabel->setText(
            QStringLiteral("请先在表格中选择一个班次，再进行售票或退票。"));
        sellTicketButton->setEnabled(false);
        refundTicketButton->setEnabled(false);
        return;
    }

    const int remaining = ui->routeTable->item(row, Remaining)->text().toInt();
    const int sold = ui->routeTable->item(row, Number)->data(SoldRole).toInt();
    selectedRouteLabel->setText(
        QStringLiteral("已选：%1　%2 → %3　发车 %4　票价 %5　余票 %6　累计已售 %7")
            .arg(ui->routeTable->item(row, Number)->text(),
                 ui->routeTable->item(row, Departure)->text(),
                 ui->routeTable->item(row, Destination)->text(),
                 ui->routeTable->item(row, DepartureTime)->text(),
                 ui->routeTable->item(row, Price)->text())
            .arg(remaining)
            .arg(sold));
    sellTicketButton->setEnabled(remaining > 0);
    refundTicketButton->setEnabled(sold > 0);
}

// 汇总当前表格中的班次数、总余票、累计销量和累计营业额。
void Widget::updateStatistics()
{
    int remainingTotal = 0;
    int soldTotal = 0;
    int lowStockCount = 0;
    double revenueTotal = 0.0;

    for (int row = 0; row < ui->routeTable->rowCount(); ++row) {
        auto *remainingItem = ui->routeTable->item(row, Remaining);
        const int remaining = remainingItem->text().toInt();
        remainingTotal += remaining;

        const auto *numberItem = ui->routeTable->item(row, Number);
        soldTotal += numberItem->data(SoldRole).toInt();
        revenueTotal += numberItem->data(RevenueRole).toDouble();

        // 余票 0 标记为售罄，1～5 标记为库存紧张；文字提示避免只靠颜色表达。
        QFont stockFont = remainingItem->font();
        stockFont.setBold(remaining <= 5);
        remainingItem->setFont(stockFont);
        if (remaining == 0) {
            ++lowStockCount;
            remainingItem->setForeground(QColor(QStringLiteral("#b4232f")));
            remainingItem->setToolTip(QStringLiteral("售罄：该班次已无余票"));
        } else if (remaining <= 5) {
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
        QStringLiteral("运营班次\n%1 个").arg(ui->routeTable->rowCount()));
    remainingTotalLabel->setText(
        QStringLiteral("可售余票\n%1 张").arg(remainingTotal));
    soldTotalLabel->setText(
        QStringLiteral("累计售票\n%1 张").arg(soldTotal));
    revenueTotalLabel->setText(
        QStringLiteral("累计营业额\n¥%1").arg(revenueTotal, 0, 'f', 2));
    lowStockLabel->setText(
        QStringLiteral("库存预警\n%1 个班次").arg(lowStockCount));

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

    // 动态属性改变后重新应用样式，使运营建议的状态色立即刷新。
    overviewInsightLabel->style()->unpolish(overviewInsightLabel);
    overviewInsightLabel->style()->polish(overviewInsightLabel);
}

// 售票：检查库存，确认交易后扣减余票，并累计销量与营业额。
void Widget::sellTickets()
{
    const int row = ui->routeTable->currentRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先选择需要售票的班次。"));
        return;
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

    const double price = ui->routeTable->item(row, Price)
                             ->text().remove(QChar(0x00A5)).toDouble();
    const double amount = price * quantity;
    if (QMessageBox::question(
            this, QStringLiteral("确认售票"),
            QStringLiteral("班次：%1\n数量：%2 张\n应收：¥%3\n\n确认完成售票吗？")
                .arg(numberItem->text()).arg(quantity).arg(amount, 0, 'f', 2),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    remainingItem->setText(QString::number(remaining - quantity));
    numberItem->setData(SoldRole,
                        numberItem->data(SoldRole).toInt() + quantity);
    numberItem->setData(RevenueRole,
                        numberItem->data(RevenueRole).toDouble() + amount);
    recordTransaction(QStringLiteral("售票"), row, quantity, price,
                      amount, remaining - quantity);
    saveRoutes();
    updateStatistics();
    updateTicketSelection(row);
    QMessageBox::information(
        this, QStringLiteral("售票成功"),
        QStringLiteral("已售出 %1 张票，应收 ¥%2。")
            .arg(quantity).arg(amount, 0, 'f', 2));
}

// 退票：最多只能退回本系统累计售出的数量，并恢复余票和营业额。
void Widget::refundTickets()
{
    const int row = ui->routeTable->currentRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先选择需要退票的班次。"));
        return;
    }

    const int quantity = ticketQuantitySpin->value();
    auto *remainingItem = ui->routeTable->item(row, Remaining);
    auto *numberItem = ui->routeTable->item(row, Number);
    const int sold = numberItem->data(SoldRole).toInt();
    if (quantity > sold) {
        QMessageBox::warning(
            this, QStringLiteral("退票数量错误"),
            QStringLiteral("该班次累计仅售出 %1 张，无法退回 %2 张。")
                .arg(sold).arg(quantity));
        return;
    }

    const double price = ui->routeTable->item(row, Price)
                             ->text().remove(QChar(0x00A5)).toDouble();
    const double amount = price * quantity;
    if (QMessageBox::question(
            this, QStringLiteral("确认退票"),
            QStringLiteral("班次：%1\n数量：%2 张\n应退：¥%3\n\n确认完成退票吗？")
                .arg(numberItem->text()).arg(quantity).arg(amount, 0, 'f', 2),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    remainingItem->setText(
        QString::number(remainingItem->text().toInt() + quantity));
    numberItem->setData(SoldRole, sold - quantity);
    numberItem->setData(
        RevenueRole,
        qMax(0.0, numberItem->data(RevenueRole).toDouble() - amount));
    recordTransaction(QStringLiteral("退票"), row, -quantity, price,
                      -amount, remainingItem->text().toInt());
    saveRoutes();
    updateStatistics();
    updateTicketSelection(row);
    QMessageBox::information(
        this, QStringLiteral("退票成功"),
        QStringLiteral("已退回 %1 张票，应退 ¥%2。")
            .arg(quantity).arg(amount, 0, 'f', 2));
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
        QStringLiteral("%1 → %2")
            .arg(ui->routeTable->item(routeRow, Departure)->text(),
                 ui->routeTable->item(routeRow, Destination)->text());

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
    saveTransactions();
    filterTransactions();
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

    for (int row = 0; row < transactionTable->rowCount(); ++row) {
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
        QStringLiteral("交易流水已保存到：\n%1").arg(path));
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
        settings.setValue(QStringLiteral("departure"),
                          ui->routeTable->item(row, Departure)->text());
        settings.setValue(QStringLiteral("destination"),
                          ui->routeTable->item(row, Destination)->text());
        settings.setValue(QStringLiteral("time"),
                          ui->routeTable->item(row, DepartureTime)->text());
        settings.setValue(QStringLiteral("price"),
                          ui->routeTable->item(row, Price)->text().remove(QChar(0x00A5)));
        settings.setValue(QStringLiteral("remaining"),
                          ui->routeTable->item(row, Remaining)->text());
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
            settings.value(QStringLiteral("departure")).toString(),
            settings.value(QStringLiteral("destination")).toString(),
            settings.value(QStringLiteral("time"), QStringLiteral("待补充")).toString(),
            QStringLiteral("¥%1").arg(
                settings.value(QStringLiteral("price"), QStringLiteral("0.00")).toString()),
            settings.value(QStringLiteral("remaining"), 0).toString()};
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