// Operator-only receive check. Never sends frames or runs from CTest.
#include <QApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTreeView>
#include <QDir>
#include <QCheckBox>
#include <QScrollBar>
#include <chrono>
#include <cmath>
#include "infrastructure/TosunHardware.h"
#include "views/ChannelPage.h"
#include "views/UiLanguageController.h"
using namespace host;
static bool waitFor(const std::function<bool()> &ready,int timeout=5000){
    QElapsedTimer timer;timer.start();while(timer.elapsed()<timeout){QCoreApplication::processEvents();if(ready())return true;QThread::msleep(5);}return false;
}
int main(int argc,char **argv){
    QApplication app(argc,argv);QApplication::setStyle("Fusion");UiLanguageController::instance();QTextStream out(stdout);const auto args=app.arguments();
    if(args.size()!=5||args[1]!="--can-monitor"){
        out<<"Usage: trace_receive_probe --can-monitor <hardware-key> <bitrate> <artifact-dir>"<<Qt::endl;return 2;
    }
    bool rateOk=false;const int rate=args[3].toInt(&rateOk);if(!rateOk||rate<=0)return 2;
    QString error;auto runtime=tosun::sharedRuntime();const auto ports=runtime->scan(communication::Bus::Can,error);communication::HardwareChannel selected;
    for(const auto &port:ports)if(port.key==args[2])selected=port;
    if(selected.key.isEmpty()){out<<"Hardware unavailable: "<<error<<Qt::endl;return 3;}
    auto settings=ChannelSettings::defaults(communication::Bus::Can);settings.simulation=false;settings.bitrate=rate;
    settings.hardwareKey=selected.key;settings.handle=selected.handle;settings.softwareId="CAN_RX_CHECK";
    ChannelViewModel vm(settings);ChannelPage page(&vm);page.resize(1380,920);page.show();
    if(!waitFor([&]{return vm.canConnect();})){out<<"Channel unavailable"<<Qt::endl;return 4;}
    vm.toggleConnection();
    if(!waitFor([&]{return vm.connected()&&!vm.pending();})){out<<vm.logs().join(" | ")<<Qt::endl;return 4;}
    if(!waitFor([&]{return vm.frames()->history().size()>1;})){
        vm.toggleConnection();waitFor([&]{return !vm.connected()&&!vm.pending();});out<<"No received frames"<<Qt::endl;return 5;
    }
    const auto before=vm.frames()->history().last();const auto wall=std::chrono::steady_clock::now();
    auto wallMs=[&]{return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-wall).count();};
    waitFor([&]{return wallMs()>=3000;},3500);const qint64 elapsed=wallMs();const auto after=vm.frames()->history().last();
    const double delta=after.relativeTime.toDouble()-before.relativeTime.toDouble();
    auto *table=page.findChild<QTreeView*>("frameTable");const auto last=table->model()->index(table->model()->rowCount()-1,1);
    const QString displayed=last.data().toString();
    bool formatOk=true;int received=0;for(const auto &record:vm.frames()->history()){
        if(!record.echo&&!record.error&&!record.simulated)++received;
        if(record.relativeTime.contains('.')&&record.relativeTime.endsWith('0'))formatOk=false;
        if(std::abs(record.relativeTime.toDouble()-record.timeUs/1000000.0)>0.000001)formatOk=false;
    }
    const bool timeOk=delta>2&&std::abs(delta-elapsed/1000.0)<0.25;
    const QString directory=QDir(args[4]).absolutePath();QDir().mkpath(directory);table->scrollToBottom();QCoreApplication::processEvents();
    const bool captured=page.grab().save(directory+"/hardware-monitor.png");
    auto *pause=page.findChild<QCheckBox*>("framePause");pause->setChecked(true);
    const auto frozen=table->model()->index(table->model()->rowCount()-1,1).data();const int position=table->verticalScrollBar()->value();
    const auto countBeforePause=vm.frames()->historyCount();
    const bool continued=waitFor([&]{return vm.frames()->historyCount()>countBeforePause+4;});
    const bool frozenOk=frozen==table->model()->index(table->model()->rowCount()-1,1).data()&&position==table->verticalScrollBar()->value();
    page.grab().save(directory+"/hardware-paused.png");pause->setChecked(false);
    const bool resumed=table->model()->index(table->model()->rowCount()-1,1).data().toDouble()>frozen.toDouble();
    vm.frames()->clear();const bool restarted=waitFor([&]{return vm.frames()->historyCount()>1;})&&vm.frames()->index(0,1).data().toString()=="0";
    vm.toggleConnection();const bool closed=waitFor([&]{return !vm.connected()&&!vm.pending();});
    const QJsonObject report{{"wallMs",double(elapsed)},{"captureDeltaUs",double(after.captureUs-before.captureUs)},
        {"hardwareDeltaUs",double(after.hardwareUs-before.hardwareUs)},{"displayDeltaSeconds",delta},{"lastDisplaySeconds",displayed},
        {"unitVerified",timeOk},{"trimmedFormatVerified",formatOk},{"receivedFrames",received},{"screenshot",captured},{"closed",closed},
        {"recordedWhilePaused",continued},{"viewFrozen",frozenOk},{"resumed",resumed},{"clearRestartedAtZero",restarted}};
    out<<QString::fromUtf8(QJsonDocument(report).toJson())<<Qt::endl;
    return timeOk&&formatOk&&received>1&&captured&&closed&&continued&&frozenOk&&resumed&&restarted?0:6;
}
