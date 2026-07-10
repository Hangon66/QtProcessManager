#include "testwidget.h"

#include <QApplication>
#include "processmanager.h"
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    ProcessManager manager;
    manager.setWorkingDirectory("D:/Programs/Qt/6.7.3Project/Union/release/config/config/txt/intersystem/cesium");
    manager.executeShell("D:/Programs/Qt/6.7.3Project/Union/release/config/config/txt/intersystem/cesium/启动服务器.bat");
    TestWidget w;
    w.show();
    return QCoreApplication::exec();
}
