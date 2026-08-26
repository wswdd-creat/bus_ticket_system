#include "widget.h"
#include "ui_widget.h"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidgetItem>

Widget::Widget(QWidget *parent)
    : QWidget(parent), ui(new Ui::Widget)
{
    ui->setupUi(this);


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

        ui->routeNumberEdit->setFocus(); });
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
            }
        });
}

Widget::~Widget()
{
    delete ui;
}