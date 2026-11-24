// Offline replay of a recorded SafeTrace through the real window and queued
// model signals. All channels use PreviewBackend; no bus or ECU is opened.
#include <QApplication>
#include <QFile>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCheckBox>
#include <QThread>
#include <QTreeView>
#include <QTimer>
#include <QEvent>
#include <QFont>
#include <QAbstractProxyModel>
#include <QTabWidget>
#include <functional>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <cstdio>
#include <qt_windows.h>
#include "views/MainWindow.h"
#include "model/FramePresentation.h"
#include "model/FrameFilterProxy.h"

using namespace host;
static double cpuMs(bool gui){
    FILETIME c,e,k,u;
    if(!(gui?GetThreadTimes(GetCurrentThread(),&c,&e,&k,&u):GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u)))return -1;
    ULARGE_INTEGER kernel,user;kernel.LowPart=k.dwLowDateTime;kernel.HighPart=k.dwHighDateTime;
    user.LowPart=u.dwLowDateTime;user.HighPart=u.dwHighDateTime;
    return double(kernel.QuadPart+user.QuadPart)/10000.0;
}
class ProbeApplication:public QApplication {
public:
    using QApplication::QApplication;
    struct Counter{qint64 count=0;double ms=0;};
    QMap<QString,Counter> counters;
    bool notify(QObject *receiver,QEvent *event)override{
        // QApplication also delivers worker events. Profile GUI events here;
        // process/thread CPU counters cover the workers without a shared map.
        if(QThread::currentThread()!=thread())return QApplication::notify(receiver,event);
        QString category;
        switch(event->type()){
        case QEvent::MetaCall:category="queued:";break;
        case QEvent::Timer:category="timer:";break;
        case QEvent::Paint:category="paint:";break;
        case QEvent::LayoutRequest:category="layout:";break;
        default:return QApplication::notify(receiver,event);
        }
        category+=QString::fromLatin1(receiver->metaObject()->className())+":"+receiver->objectName();
        QElapsedTimer t;t.start();const bool result=QApplication::notify(receiver,event);
        auto &counter=counters[category];++counter.count;counter.ms+=t.nsecsElapsed()/1e6;return result;
    }
};
int main(int argc,char **argv){
    ProbeApplication app(argc,argv);QApplication::setStyle("Fusion");
    app.setFont(QFont(MainWindowInitialValues::fontFamily,MainWindowInitialValues::fontSize));
    if(argc<4)return 2;
    const QString config=app.arguments()[1],trace=app.arguments()[2],output=app.arguments()[3];
    const double speed=argc>4?qMax(0.01,app.arguments()[4].toDouble()):1.0;
    QFile file(trace);if(!file.open(QIODevice::ReadOnly))return 3;file.readLine();
    FrameBatch input;signal::BusFrameEvents events;
    while(!file.atEnd()){
        const auto cells=file.readLine().trimmed().split(',');if(cells.size()<8)continue;
        const auto direction=cells[3];if(direction!="RX"&&direction!="TX request"&&direction!="TX echo")continue;
        FrameRecord r;r.captureUs=cells[2].toLongLong();r.epochMs=cells[1].toLongLong();r.channel="CAN01";
        r.id=cells[4].mid(2).toUInt(nullptr,16);r.length=cells[5].toInt();r.typed=true;
        for(const auto &byte:cells[6].split(' ')){if(!byte.isEmpty())r.payload.append(byte=="**"?char(0):char(byte.toUInt(nullptr,16)));}
        r.echo=direction=="TX echo";r.request=direction=="TX request";r.direction=direction=="RX"?"RX":"TX";
        r.identifier=QString("0x%1").arg(r.id,3,16,QChar('0')).toUpper();r.data=QString::fromLatin1(r.payload.toHex(' ')).toUpper();r.status="CAN";
        normalizeFramePresentation(r);input.append(r);
        signal::BusFrameEvent e;e.id=r.id;e.bus=signal::Bus::Can;e.bytes=r.payload;e.arrivalUs=r.captureUs;e.valid=true;
        e.source=r.echo?signal::EventSource::HardwareEcho:r.request?signal::EventSource::RequestAccepted:signal::EventSource::Received;events.append(e);
    }
    if(input.isEmpty())return 4;
    QDir().mkpath(QFileInfo(output).absolutePath());
    const bool physicalEnumeration=app.arguments().contains("--physical-idle");
    const bool idleOnly=app.arguments().contains("--idle-only");
    if(physicalEnumeration&&!idleOnly)return 11;
    MainWindow window(QFileInfo(output).absolutePath()+"/scratch-config.json",!physicalEnumeration);QString error;
    if(!window.restoreChannels(config,error))return 5;
    window.setWindowTitle(QString("Offline trace pipeline probe - Qt %1").arg(qVersion()));
    window.setAttribute(Qt::WA_ShowWithoutActivating);window.resize(1480,1000);window.show();
    auto *vm=window.canChannel();if(!vm)return 6;
    auto *source=vm->findChild<ChannelModel*>();if(!source)return 7;
    // Controlled reference for the previous, measured hot path. No production
    // switch is added: the rest of the queued pipeline and window stay equal.
    const bool legacyHistory=app.arguments().contains("--legacy-history");
    FrameBatch legacyFrames;qint64 legacyOrigin=-1;
    if(legacyHistory){
        QObject::disconnect(source,&ChannelModel::observedFrames,vm->signalTransmission(),&SignalTransmitViewModel::observeFrames);
        QObject::connect(source,&ChannelModel::observedFrames,&app,[&](FrameBatch frames){
            for(auto &r:frames){if(legacyOrigin<0)legacyOrigin=r.captureUs;r.timeUs=r.captureUs-legacyOrigin;}
            legacyFrames+=frames;if(legacyFrames.size()>10000)legacyFrames.remove(0,legacyFrames.size()-10000);
            emit vm->signalTransmission()->framesObserved(frames);
        });
    }
    // Widget stacking can reorder QObject children. Bind the page by its data
    // model, rather than accidentally configuring a hidden LIN page.
    ChannelPage *page=nullptr;
    for(auto *candidate:window.findChildren<ChannelPage*>()){
        auto *tree=candidate->findChild<QTreeView*>("frameTable");
        auto *proxy=tree?qobject_cast<QAbstractProxyModel*>(tree->model()):nullptr;
        if(proxy&&proxy->sourceModel()==vm->frames()){page=candidate;break;}
    }
    if(!page)return 8;
    for(auto *tabs:window.findChildren<QTabWidget*>())if(tabs->indexOf(page)>=0)tabs->setCurrentWidget(page);
    QJsonArray phases;QElapsedTimer phaseClock,heartbeatClock;QVector<double> heartbeat;
    double processStart=0,guiStart=0;std::atomic<bool> finished{false};QThread *producer=nullptr;
    SYSTEM_INFO systemInfo;GetSystemInfo(&systemInfo);
    QTimer beat;beat.setInterval(16);beat.setTimerType(Qt::PreciseTimer);
    QObject::connect(&beat,&QTimer::timeout,&app,[&]{heartbeat.append(heartbeatClock.nsecsElapsed()/1e6);heartbeatClock.restart();});
    const auto begin=[&]{app.counters.clear();heartbeat.clear();heartbeatClock.restart();phaseClock.restart();processStart=cpuMs(false);guiStart=cpuMs(true);beat.start();};
    const auto record=[&](const QString &name){
        beat.stop();std::sort(heartbeat.begin(),heartbeat.end());QJsonObject counts;
        for(auto it=app.counters.cbegin();it!=app.counters.cend();++it)counts[it.key()]=QJsonObject{{"count",double(it.value().count)},{"ms",it.value().ms}};
        const double wall=phaseClock.nsecsElapsed()/1e6,process=cpuMs(false)-processStart,gui=cpuMs(true)-guiStart;
        phases.append(QJsonObject{{"phase",name},{"wallMs",wall},{"cpuMs",process},{"guiCpuMs",gui},
            {"oneCorePercent",100*process/wall},{"taskManagerPercent",100*process/wall/systemInfo.dwNumberOfProcessors},
            {"heartbeatP95Ms",heartbeat.isEmpty()?0:heartbeat[heartbeat.size()*95/100]},
            {"heartbeatMaxMs",heartbeat.isEmpty()?0:heartbeat.last()},{"events",counts}});
        std::fprintf(stdout,"%s: wall %.1f ms, process CPU %.1f ms, GUI CPU %.1f ms\n",qPrintable(name),wall,process,gui);std::fflush(stdout);
    };
    QTimer completion;completion.setInterval(100);
    QObject::connect(&completion,&QTimer::timeout,&app,[&]{
        if(!finished)return;
        completion.stop();vm->flushMonitorFrames();record("trace");
        const int visibleRows=page->findChild<QTreeView*>("frameTable")->model()->rowCount();
        const bool complete=vm->frames()->historyCount()==input.size()&&
            visibleRows==qMin(int(input.size()),FrameTableModel::Capacity)&&!vm->frames()->rolling()&&page->isVisible();
        QFile result(output);if(!result.open(QIODevice::WriteOnly)){app.exit(9);return;}
        result.write(QJsonDocument(QJsonObject{{"qt",qVersion()},{"settings",config},{"trace",trace},{"speed",speed},
            {"platform",QApplication::platformName()},{"visibleRows",visibleRows},{"pageVisible",page->isVisible()},
            {"filtersCleared",true},{"sortColumn",1},
            {"follow",page->findChild<QCheckBox*>("frameFollow")->isChecked()},{"rolling",page->findChild<QCheckBox*>("frameRolling")->isChecked()},
            {"payloadMasked",true},{"hardwareOpened",false},{"logicalProcessors",int(systemInfo.dwNumberOfProcessors)},
            {"inputFrames",input.size()},{"historyFrames",double(vm->frames()->historyCount())},{"completeHistory",complete},
            {"traceHistoryFrames",legacyHistory?legacyFrames.size():vm->signalTransmission()->traceHistory().size()},
            {"historyImplementation",legacyHistory?"original-QVector":"deque"},{"phases",phases}}).toJson());app.exit(complete?0:10);
    });
    std::function<void()> startProbe;
    QElapsedTimer warmup;warmup.start();
    startProbe=[&]{
        for(auto *channel:window.channels())if(channel->signalTransmission()->importing()){
            if(warmup.elapsed()>60000){app.exit(12);return;}
            QTimer::singleShot(100,&app,startProbe);return;
        }
        // Async configuration restoration must finish before overriding the
        // display mode for a comparable, all-frame performance measurement.
        page->findChild<QCheckBox*>("frameRolling")->setChecked(false);
        page->findChild<QCheckBox*>("frameFollow")->setChecked(!app.arguments().contains("--no-follow"));
        auto *table=page->findChild<QTreeView*>("frameTable");
        auto *proxy=qobject_cast<FrameFilterProxy*>(table->model());if(!proxy){app.exit(14);return;}
        proxy->restoreFilters({});table->sortByColumn(1,Qt::AscendingOrder);
        if(vm->frames()->rolling()){app.exit(13);return;}
        QTimer::singleShot(1000,&app,[&]{begin();QTimer::singleShot(8000,&app,[&]{
        record("idle");
        if(idleOnly){
            QFile result(output);if(!result.open(QIODevice::WriteOnly)){app.exit(9);return;}
            result.write(QJsonDocument(QJsonObject{{"qt",qVersion()},{"settings",config},{"hardwareOpened",false},
                {"physicalEnumeration",physicalEnumeration},{"logicalProcessors",int(systemInfo.dwNumberOfProcessors)},
                {"guiThreadId",double(GetCurrentThreadId())},{"phases",phases}}).toJson());result.close();app.exit(0);return;
        }
        begin();completion.start();
        producer=QThread::create([&]{
            const auto start=std::chrono::steady_clock::now();const auto origin=input.first().captureUs;
            for(int i=0;i<input.size();++i){
                std::this_thread::sleep_until(start+std::chrono::microseconds(qint64((input[i].captureUs-origin)/speed)));
                // Same three queued deliveries as ChannelWorker -> ChannelModel.
                const FrameBatch frame{input[i]};const signal::BusFrameEvents bus{events[i]};
                QMetaObject::invokeMethod(source,[source,frame]{emit source->observedFrames(frame);},Qt::QueuedConnection);
                QMetaObject::invokeMethod(source,[source,frame]{emit source->framesReceived(frame);},Qt::QueuedConnection);
                if(!input[i].request)QMetaObject::invokeMethod(source,[source,bus]{emit source->busEvents(bus);},Qt::QueuedConnection);
            }
            QMetaObject::invokeMethod(source,[&]{finished=true;},Qt::QueuedConnection);
        });producer->start();
    });});};
    QTimer::singleShot(3000,&app,startProbe);
    const int code=app.exec();if(producer){producer->wait();delete producer;}return code;
}
