#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include "infrastructure/TosunHardware.h"
#include "viewmodels/ChannelViewModel.h"
using namespace host;
// Operator-only test: isolated CAN1/CAN2 loopback at 500 kbit/s. Never opens LIN.
static bool waitFor(const std::function<bool()> &ready,int timeout=4000){
    QElapsedTimer timer;timer.start();while(timer.elapsed()<timeout){QCoreApplication::processEvents();if(ready())return true;QThread::msleep(5);}return false;
}
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);QTextStream out(stdout);const auto args=app.arguments();
    if(args.size()!=3||args[1]!="--can-loopback"){out<<"Explicit --can-loopback <adapter-serial> required; CAN1/CAN2 must be isolated."<<Qt::endl;return 2;}
    auto runtime=tosun::sharedRuntime();QString error;const auto ports=runtime->scan(communication::Bus::Can,error);communication::HardwareChannel one,two;
    for(const auto &port:ports)if(port.key.section(':',1,1)==args[2]){if(port.controller==1)one=port;if(port.controller==2)two=port;}
    if(one.key.isEmpty()||two.key.isEmpty()){out<<"CAN1/CAN2 unavailable: "<<error<<Qt::endl;return 2;}
    auto settings=[](const communication::HardwareChannel&p,const QString&name){auto s=ChannelSettings::defaults(communication::Bus::Can);s.simulation=false;s.bitrate=500000;s.hardwareKey=p.key;s.handle=p.handle;s.softwareId=name;return s;};
    ChannelViewModel tx(settings(one,"PROBE_TX")),rx(settings(two,"PROBE_RX"));
    auto *sender=tx.signalTransmission();const auto key=signal::frameKey(signal::Bus::Can,291);
    if(!sender->importFile(QString(TEST_SOURCE_DIR)+"/fixtures/signals-synthetic.dbc",error)||!sender->selectCanNode("Tester","Tx",error)){out<<error<<Qt::endl;return 3;}
    for(const auto &frame:sender->queuedDefinitions())if(!sender->setFrameEnabled(frame.key,frame.key==key,error)){out<<error<<Qt::endl;return 3;}
    if(!sender->setCanOptions(key,true,20,error)){out<<error<<Qt::endl;return 3;}
    if(!waitFor([&]{return tx.canConnect()&&rx.canConnect();})){out<<"Channels did not become available"<<Qt::endl;return 3;}
    tx.toggleConnection();rx.toggleConnection();
    if(!waitFor([&]{return tx.connected()&&rx.connected()&&!tx.pending()&&!rx.pending();})){out<<"Connection failed: "<<tx.logs().join(" | ")<<" / "<<rx.logs().join(" | ")<<Qt::endl;return 4;}
    auto received=[&](int raw){for(const auto&r:rx.frames()->history())if(r.id==291&&r.payload.size()==8&&quint8(r.payload[1])==raw&&!r.echo&&!r.error)return true;return false;};
    if(!sender->start(true,error)||!waitFor([&]{return received(12);})){out<<"Initial frame not received: "<<error<<Qt::endl;return 5;}
    const auto before=rx.frames()->history().last();QElapsedTimer wall;wall.start();
    for(int value:{25,30}){
        if(!sender->editSignal(key,1,QString::number(value),false,error)||!waitFor([&]{return received(value);})){out<<"Live payload update failed for "<<value<<": "<<error<<Qt::endl;return 6;}
        out<<"CAN1 -> CAN2 live raw update "<<value<<" confirmed"<<Qt::endl;
    }
    waitFor([&]{return wall.elapsed()>=1250;},1500);const auto after=rx.frames()->history().last();
    const double rtDelta=after.relativeTime.toDouble()-before.relativeTime.toDouble();const qint64 elapsed=wall.elapsed();
    const bool timeOk=std::abs(rtDelta-elapsed/1000.0)<0.15&&rtDelta>1;
    sender->stop();const bool stopped=waitFor([&]{return !sender->running();});
    tx.toggleConnection();rx.toggleConnection();const bool closed=waitFor([&]{return !tx.connected()&&!rx.connected()&&!tx.pending()&&!rx.pending();});
    QJsonObject report{{"wallMs",double(elapsed)},{"captureDeltaUs",double(after.captureUs-before.captureUs)},{"rtDeltaSeconds",rtDelta},{"firstRtSeconds",before.relativeTime},{"lastRtSeconds",after.relativeTime},{"unitVerified",timeOk},{"stopVerified",stopped},{"closeVerified",closed},{"rxRecords",rx.frames()->history().size()}};
    out<<QString::fromUtf8(QJsonDocument(report).toJson())<<Qt::endl;return timeOk&&stopped&&closed?0:7;
}
