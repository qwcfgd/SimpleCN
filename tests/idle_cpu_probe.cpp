#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QTabBar>
#include <qt_windows.h>
#include "views/MainWindow.h"
#include "views/UiLanguageController.h"
#include "localization/Language.h"

static double cpuMs(bool gui){
    FILETIME created,exited,kernel,user;
    const bool ok=gui?GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user):GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user);
    if(!ok)return -1;
    ULARGE_INTEGER k,u;k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;
    return double(k.QuadPart+u.QuadPart)/10000.0;
}
class Events final:public QObject {
public:
    QMap<QString,qint64> counts;
    bool eventFilter(QObject *object,QEvent *event)override{
        if(event->type()==QEvent::Paint||event->type()==QEvent::LayoutRequest){
            const auto name=QString::fromLatin1(object->metaObject()->className())+":"+object->objectName()+":"+(event->type()==QEvent::Paint?"paint":"layout");
            ++counts[name];
        }
        return false;
    }
};
int main(int argc,char **argv){
    QApplication app(argc,argv);QApplication::setStyle("Fusion");
    app.setFont(QFont(host::MainWindowInitialValues::fontFamily,host::MainWindowInitialValues::fontSize));
    if(app.arguments().size()<2)return 2;
    const QString path=app.arguments().at(1);QDir().mkpath(QFileInfo(path).absolutePath());
    host::Language::instance().setCode(app.arguments().contains("--english")?"en":"zh_CN");
    host::MainWindow window(QFileInfo(path).absolutePath()+"/probe-config.json",app.arguments().contains("--demo"));
    window.setAttribute(Qt::WA_ShowWithoutActivating);window.show();
    auto &language=host::UiLanguageController::instance();Events events;app.installEventFilter(&events);
    QJsonArray phases;int phase=0;QElapsedTimer clock;double cpu=0,gui=0;
    QTimer stage;stage.setSingleShot(true);
    const auto begin=[&]{events.counts.clear();cpu=cpuMs(false);gui=cpuMs(true);clock.restart();stage.start(5000);};
    QObject::connect(&stage,&QTimer::timeout,&app,[&]{
        QJsonObject counts;for(auto it=events.counts.cbegin();it!=events.counts.cend();++it)counts[it.key()]=double(it.value());
        phases.append(QJsonObject{{"phase",QStringList{"enabled","disabled","restored","tabsExcluded"}.at(phase)},{"wallMs",clock.elapsed()},{"cpuMs",cpuMs(false)-cpu},{"guiCpuMs",cpuMs(true)-gui},{"events",counts}});
        if(++phase==4){
            QFile file(path);if(!file.open(QIODevice::WriteOnly)){app.exit(3);return;}
            file.write(QJsonDocument(QJsonObject{{"qt",qVersion()},{"simulation",app.arguments().contains("--demo")},{"phases",phases}}).toJson());app.quit();return;
        }
        if(phase==1)app.removeEventFilter(&language);
        else if(phase==2){app.installEventFilter(&language);language.refresh();}
        else {for(auto *tabs:window.findChildren<QTabBar*>())tabs->setProperty("preserveUserText",true);window.update();}
        QTimer::singleShot(1000,&app,begin);
    });
    QTimer::singleShot(3000,&app,begin);
    return app.exec();
}
