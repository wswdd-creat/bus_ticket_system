#include <QApplication>
#include <QFile>
#include "widget.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // ==================== 全局视觉主题 ====================
    // 从 Qt 资源系统读取统一 QSS，让所有窗口和弹窗共享同一套商业化样式。
    QFile styleFile(QStringLiteral(":/bus_ticket_system/styles/app.qss"));
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        a.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    }

    Widget w;
    w.showMaximized();

    return a.exec();
}
