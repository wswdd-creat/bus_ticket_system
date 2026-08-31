#include "widget.h"
#include "ui_widget.h"

#include <QAbstractItemView>
#include <QDoubleValidator>
#include <QHeaderView>
#include <QIntValidator>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTableWidgetItem>
#include <QTime>

// ==================== 表格列定义与通用辅助工具 ====================
// 用枚举代替数字下标，后续看到 Number、Price 等名称就能知道对应哪一列。
namespace {
constexpr int ColumnCount = 6;
enum Column { Number, Departure, Destination, DepartureTime, Price, Remaining };

// 按对象名称查找输入框，方便读取程序运行时动态创建的控件。
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
    setWindowTitle(QStringLiteral("客运售票管理系统 · V0.7"));
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

    // ---------- 界面美化 ----------
    // 集中设置窗口、输入框、按钮和表格的深色主题。
    setStyleSheet(QStringLiteral(R"(
        QWidget {
            background-color: #0f172a;
            color: #e5e7eb;
            font-family: "Microsoft YaHei UI";
            font-size: 14px;
        }
        QLabel#titleLabel {
            color: #f8fafc;
            font-size: 28px;
            font-weight: 600;
            padding: 8px;
        }
        QLineEdit {
            min-height: 42px;
            padding: 0 12px;
            color: #f8fafc;
            background-color: #1e293b;
            border: 1px solid #334155;
            border-radius: 8px;
            selection-background-color: #2563eb;
        }
        QLineEdit:hover { border-color: #64748b; }
        QLineEdit:focus { border: 2px solid #3b82f6; padding: 0 11px; }
        QPushButton {
            min-height: 42px;
            padding: 0 18px;
            color: #e2e8f0;
            background-color: #263449;
            border: 1px solid #3b4b63;
            border-radius: 8px;
            font-weight: 600;
        }
        QPushButton:hover { background-color: #334155; }
        QPushButton:pressed { background-color: #1e293b; }
        QPushButton:disabled {
            color: #64748b;
            background-color: #172033;
            border-color: #273449;
        }
        QPushButton#addRouteButton, QPushButton#searchButton {
            color: white;
            background-color: #2563eb;
            border-color: #3b82f6;
        }
        QPushButton#addRouteButton:hover, QPushButton#searchButton:hover {
            background-color: #1d4ed8;
        }
        QPushButton#deleteRouteButton {
            color: #fecaca;
            background-color: #451a1a;
            border-color: #7f1d1d;
        }
        QPushButton#deleteRouteButton:hover { background-color: #7f1d1d; }
        QTableWidget {
            background-color: #111827;
            alternate-background-color: #162033;
            border: 1px solid #334155;
            border-radius: 10px;
            padding: 2px;
            outline: none;
        }
        QTableWidget::item {
            min-height: 42px;
            padding: 8px 12px;
            border-bottom: 1px solid #253248;
        }
        QTableWidget::item:selected {
            color: white;
            background-color: #1d4ed8;
        }
        QHeaderView::section {
            color: #cbd5e1;
            background-color: #1e293b;
            border: none;
            border-bottom: 1px solid #475569;
            padding: 10px;
            font-weight: 600;
        }
        QScrollBar:vertical { width: 10px; background: #111827; margin: 4px; }
        QScrollBar::handle:vertical {
            min-height: 28px;
            background: #475569;
            border-radius: 5px;
        }
    )"));

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
        clearInputs();
        saveRoutes();
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
    }
    settings.endArray();
}

// ==================== 窗口销毁 ====================
// 释放由界面生成器创建的控件管理对象。
Widget::~Widget()
{
    delete ui;
}