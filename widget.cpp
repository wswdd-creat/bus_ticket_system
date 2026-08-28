#include "widget.h"
#include "ui_widget.h"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidgetItem>
#include <QLineEdit>
#include <QSettings>

Widget::Widget(QWidget *parent)
    : QWidget(parent), ui(new Ui::Widget)
{
    ui->setupUi(this);
    setWindowTitle(QStringLiteral("客运售票管理系统 · V0.6"));
    setMinimumSize(900, 620);

    ui->verticalLayout_2->setContentsMargins(28, 22, 28, 22);
    ui->verticalLayout->setSpacing(14);
    ui->gridLayout->setHorizontalSpacing(12);
    ui->gridLayout->setVerticalSpacing(12);
    ui->horizontalLayout->setSpacing(12);
    ui->titleLabel->setAlignment(Qt::AlignCenter);
    ui->titleLabel->setMinimumHeight(64);

    ui->searchEdit->setClearButtonEnabled(true);
    ui->routeNumberEdit->setClearButtonEnabled(true);
    ui->departureEdit->setClearButtonEnabled(true);
    ui->destinationEdit->setClearButtonEnabled(true);

    ui->routeTable->verticalHeader()->setVisible(false);
    ui->routeTable->horizontalHeader()->setMinimumHeight(42);
    ui->routeTable->setAlternatingRowColors(true);
    ui->routeTable->setSortingEnabled(false);
    ui->routeTable->setShowGrid(false);

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
            padding: 0 14px;
            color: #f8fafc;
            background-color: #1e293b;
            border: 1px solid #334155;
            border-radius: 8px;
            selection-background-color: #2563eb;
        }
        QLineEdit:hover { border-color: #64748b; }
        QLineEdit:focus {
            border: 2px solid #3b82f6;
            padding: 0 13px;
        }
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
        QScrollBar:vertical {
            width: 10px;
            background: #111827;
            margin: 4px;
        }
        QScrollBar::handle:vertical {
            min-height: 28px;
            background: #475569;
            border-radius: 5px;
        }
    )"));

    // 初始化班次表格
    ui->routeTable->setColumnCount(3);
    ui->routeTable->setHorizontalHeaderLabels(
        {"班次号", "出发地", "目的地"});

    // 表格列宽自动填满
    ui->routeTable->horizontalHeader()->setSectionResizeMode(
        QHeaderView::Stretch);
    ui->routeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->routeTable->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->routeTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // 点击“添加班次”按钮
    connect(ui->addRouteButton, &QPushButton::clicked, this, [this]()
            {
        const QString routeNumber = ui->routeNumberEdit->text().trimmed();
        const QString departure = ui->departureEdit->text().trimmed();
        const QString destination = ui->destinationEdit->text().trimmed();

        // 检查是否存在空输入
        if (routeNumber.isEmpty()
            || departure.isEmpty()
            || destination.isEmpty()) {
            QMessageBox::warning(
                this,
                QStringLiteral("输入错误"),
                QStringLiteral("请完整填写班次号、出发地和目的地。")
            );
            return;
        }

        // 在表格末尾添加一行
        const int row = ui->routeTable->rowCount();
        ui->routeTable->insertRow(row);

        ui->routeTable->setItem(
            row, 0, new QTableWidgetItem(routeNumber)
        );
        ui->routeTable->setItem(
            row, 1, new QTableWidgetItem(departure)
        );
        ui->routeTable->setItem(
            row, 2, new QTableWidgetItem(destination)
        );

        // 添加成功后清空输入框
        ui->routeNumberEdit->clear();
        ui->departureEdit->clear();
        ui->destinationEdit->clear();

        ui->routeNumberEdit->setFocus();
        saveRoutes(); });
    // =========================
    // 修改选中的班次
    // =========================

    connect(
        ui->editRouteButton,
        &QPushButton::clicked,
        this,
        [this]()
        {
            const int currentRow = ui->routeTable->currentRow();

            if (currentRow < 0)
            {
                QMessageBox::information(
                    this,
                    QStringLiteral("提示"),
                    QStringLiteral("请先选择要修改的班次。"));
                return;
            }

            // 记录正在修改的行
            editingRow = currentRow;

            // 将表格中的数据放回输入框
            ui->routeNumberEdit->setText(
                ui->routeTable->item(currentRow, 0)->text());

            ui->departureEdit->setText(
                ui->routeTable->item(currentRow, 1)->text());

            ui->destinationEdit->setText(
                ui->routeTable->item(currentRow, 2)->text());

            // 进入修改状态
            ui->saveEditButton->setEnabled(true);
            ui->addRouteButton->setEnabled(false);
            ui->editRouteButton->setEnabled(false);
            ui->deleteRouteButton->setEnabled(false);

            ui->routeNumberEdit->setFocus();
        });

    // =========================
    // 保存修改
    // =========================

    connect(
        ui->saveEditButton,
        &QPushButton::clicked,
        this,
        [this]()
        {
            if (editingRow < 0 || editingRow >= ui->routeTable->rowCount())
            {
                QMessageBox::warning(
                    this,
                    QStringLiteral("错误"),
                    QStringLiteral("没有可以修改的班次。"));
                return;
            }

            const QString routeNumber =
                ui->routeNumberEdit->text().trimmed();

            const QString departure =
                ui->departureEdit->text().trimmed();

            const QString destination =
                ui->destinationEdit->text().trimmed();

            if (routeNumber.isEmpty() || departure.isEmpty() || destination.isEmpty())
            {
                QMessageBox::warning(
                    this,
                    QStringLiteral("输入错误"),
                    QStringLiteral(
                        "请完整填写班次号、出发地和目的地。"));
                return;
            }

            // 更新表格内容
            ui->routeTable->item(editingRow, 0)->setText(routeNumber);
            ui->routeTable->item(editingRow, 1)->setText(departure);
            ui->routeTable->item(editingRow, 2)->setText(destination);

            // 清空输入框
            ui->routeNumberEdit->clear();
            ui->departureEdit->clear();
            ui->destinationEdit->clear();

            // 退出修改状态
            editingRow = -1;

            ui->saveEditButton->setEnabled(false);
            ui->addRouteButton->setEnabled(true);
            ui->editRouteButton->setEnabled(true);
            ui->deleteRouteButton->setEnabled(true);

            ui->routeTable->clearSelection();

            saveRoutes();

            QMessageBox::information(
                this,
                QStringLiteral("修改成功"),
                QStringLiteral("班次信息已经更新。"));
        });
    // 点击删除按钮
    connect(
        ui->deleteRouteButton,
        &QPushButton::clicked,
        this,
        [this]()
        {
            // 获取当前选中的行
            const int currentRow =
                ui->routeTable->currentRow();

            // currentRow 小于 0 表示没有选中任何行
            if (currentRow < 0)
            {
                QMessageBox::information(
                    this,
                    QStringLiteral("提示"),
                    QStringLiteral("请先选择要删除的班次。"));

                return;
            }

            // 询问用户是否确定删除
            const auto result = QMessageBox::question(
                this,
                QStringLiteral("确认删除"),
                QStringLiteral("确定要删除选中的班次吗？"),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);

            // 用户选择“是”后删除当前行
            if (result == QMessageBox::Yes)
            {
                ui->routeTable->removeRow(currentRow);
                saveRoutes();
            }
        });

    // =========================
    // 搜索班次
    // =========================

    connect(
        ui->searchButton,
        &QPushButton::clicked,
        this,
        [this]()
        {
            const QString keyword =
                ui->searchEdit->text().trimmed();

            // 搜索框为空时显示全部数据
            if (keyword.isEmpty())
            {
                for (int row = 0;
                     row < ui->routeTable->rowCount();
                     ++row)
                {
                    ui->routeTable->setRowHidden(row, false);
                }

                return;
            }

            int matchCount = 0;

            for (int row = 0;
                 row < ui->routeTable->rowCount();
                 ++row)
            {
                bool matched = false;

                // 检查班次号、出发地和目的地三列
                for (int column = 0; column < 3; ++column)
                {
                    const QTableWidgetItem *item =
                        ui->routeTable->item(row, column);

                    if (item != nullptr && item->text().contains(
                                               keyword,
                                               Qt::CaseInsensitive))
                    {
                        matched = true;
                        break;
                    }
                }

                ui->routeTable->setRowHidden(row, !matched);

                if (matched)
                {
                    ++matchCount;
                }
            }

            if (matchCount == 0)
            {
                QMessageBox::information(
                    this,
                    QStringLiteral("搜索结果"),
                    QStringLiteral("没有找到匹配的班次。"));
            }
        });

    // =========================
    // 显示全部班次
    // =========================

    connect(
        ui->showAllButton,
        &QPushButton::clicked,
        this,
        [this]()
        {
            ui->searchEdit->clear();

            for (int row = 0;
                 row < ui->routeTable->rowCount();
                 ++row)
            {
                ui->routeTable->setRowHidden(row, false);
            }

            ui->routeTable->clearSelection();
        });

    //=========================
    // 回车触发搜索
    connect(
        ui->searchEdit,
        &QLineEdit::returnPressed,
        ui->searchButton,
        &QPushButton::click);

    loadRoutes();
}

void Widget::saveRoutes() const
{
    QSettings settings(QStringLiteral("StudentQtProjects"),
                       QStringLiteral("BusTicketSystem"));
    settings.beginWriteArray(QStringLiteral("routes"));
    for (int row = 0; row < ui->routeTable->rowCount(); ++row)
    {
        settings.setArrayIndex(row);
        settings.setValue(QStringLiteral("number"),
                          ui->routeTable->item(row, 0)->text());
        settings.setValue(QStringLiteral("departure"),
                          ui->routeTable->item(row, 1)->text());
        settings.setValue(QStringLiteral("destination"),
                          ui->routeTable->item(row, 2)->text());
    }
    settings.endArray();
}

void Widget::loadRoutes()
{
    QSettings settings(QStringLiteral("StudentQtProjects"),
                       QStringLiteral("BusTicketSystem"));
    const int count = settings.beginReadArray(QStringLiteral("routes"));
    for (int index = 0; index < count; ++index)
    {
        settings.setArrayIndex(index);
        const int row = ui->routeTable->rowCount();
        ui->routeTable->insertRow(row);
        ui->routeTable->setItem(row, 0, new QTableWidgetItem(
            settings.value(QStringLiteral("number")).toString()));
        ui->routeTable->setItem(row, 1, new QTableWidgetItem(
            settings.value(QStringLiteral("departure")).toString()));
        ui->routeTable->setItem(row, 2, new QTableWidgetItem(
            settings.value(QStringLiteral("destination")).toString()));
    }
    settings.endArray();
}
Widget::~Widget()
{
    delete ui;
}