#include "widget.h"
#include "ui_widget.h"
#include <QHeaderView>
#include <QMessageBox>
#include <QTableWidgetItem>
Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    ui->setupUi(this);
        // 初始化班次表格
    ui->routeTable->setColumnCount(3);
    ui->routeTable->setHorizontalHeaderLabels(
        {"班次号", "出发地", "目的地"}
    );

    // 表格列宽自动填满
    ui->routeTable->horizontalHeader()->setSectionResizeMode(
        QHeaderView::Stretch
    );

    // 点击“添加班次”按钮
    connect(ui->addRouteButton, &QPushButton::clicked, this, [this]() {
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
    });
}

Widget::~Widget()
{
    delete ui;
}
