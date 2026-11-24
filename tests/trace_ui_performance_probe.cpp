// In-memory UI workload only. Never opens hardware or starts a download.
#include <QApplication>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTreeView>
#include <QScrollBar>
#include <QTimer>
#include <QEvent>
#include <algorithm>
#include <cstdio>
#include "views/ChannelPage.h"

using namespace host;
class ProbeApplication final:public QApplication {
public:
    using QApplication::QApplication;
    QObject *viewport=nullptr,*tree=nullptr,*followTimer=nullptr;
    int paints=0,layouts=0,refreshes=0;double paintMs=0,layoutMs=0,refreshMs=0;
    void reset(){paints=layouts=refreshes=0;paintMs=layoutMs=refreshMs=0;}
    bool notify(QObject *receiver,QEvent *event)override{
        const bool paint=receiver==viewport&&event->type()==QEvent::Paint;
        const bool layout=(receiver==tree||receiver==viewport)&&event->type()==QEvent::LayoutRequest;
        const bool refresh=receiver==followTimer&&event->type()==QEvent::Timer;
        if(!paint&&!layout&&!refresh)return QApplication::notify(receiver,event);
        QElapsedTimer timer;timer.start();const bool result=QApplication::notify(receiver,event);const double ms=timer.nsecsElapsed()/1000000.0;
        if(paint){++paints;paintMs+=ms;}if(layout){++layouts;layoutMs+=ms;}if(refresh){++refreshes;refreshMs+=ms;}
        return result;
    }
};
static FrameRecord frame(qint64 ordinal){
    FrameRecord r;r.captureUs=ordinal*200;r.epochMs=1794355200000LL+ordinal/5;
    r.timestamp="12:00:00.000";r.bus=signal::Bus::Can;r.channel="CAN1";
    r.id=0x500+ordinal%512;r.typed=true;r.payload=QByteArray::fromHex("2101020304050607");
    r.direction=ordinal%2?"Tx":"Rx";r.length=8;return r;
}
int main(int argc,char **argv){
    ProbeApplication app(argc,argv);QApplication::setStyle("Fusion");
    QJsonArray runs;
    const int batchSize=qMax(1,qEnvironmentVariableIntValue("TRACE_UI_BATCH_SIZE"));
    const int batches=qEnvironmentVariableIsSet("TRACE_UI_BATCHES")?qMax(1,qEnvironmentVariableIntValue("TRACE_UI_BATCHES")):300;
    for(const auto &mode:QStringList{"all-follow","all-no-follow","id-sorted-follow","data-sorted-follow","merged"}){
        auto settings=ChannelSettings::defaults(communication::Bus::Can);settings.simulation=true;
        ChannelViewModel vm(settings);ChannelPage page(&vm);page.resize(1400,960);page.show();
        auto *tree=page.findChild<QTreeView*>("frameTable");
        page.findChild<QCheckBox*>("frameRolling")->setChecked(mode=="merged");
        page.findChild<QCheckBox*>("frameFollow")->setChecked(mode!="all-no-follow");
        if(mode=="id-sorted-follow")tree->sortByColumn(5,Qt::AscendingOrder);
        if(mode=="data-sorted-follow")tree->sortByColumn(7,Qt::AscendingOrder);
        auto record=[&](qint64 n){auto r=frame(n);if(mode=="data-sorted-follow"){r.payload[0]=char(0x20+n%16);for(int i=1;i<8;++i)r.payload[i]=char((n*37+i*83)>>(i%3));}return r;};
        QCoreApplication::processEvents();
        FrameBatch initial;for(int n=0;n<10000;++n)initial.append(record(n));vm.frames()->append(initial);
        QCoreApplication::processEvents();
        app.viewport=tree->viewport();app.tree=tree;app.followTimer=page.findChild<QTimer*>("frameRefreshTimer");app.reset();
        int sourceInserts=0,sourceRemoves=0,proxyInserts=0,proxyLayouts=0,scrollChanges=0;
        QObject::connect(vm.frames(),&QAbstractItemModel::rowsInserted,&page,[&]{++sourceInserts;});
        QObject::connect(vm.frames(),&QAbstractItemModel::rowsRemoved,&page,[&]{++sourceRemoves;});
        QObject::connect(tree->model(),&QAbstractItemModel::rowsInserted,&page,[&]{++proxyInserts;});
        QObject::connect(tree->model(),&QAbstractItemModel::layoutChanged,&page,[&]{++proxyLayouts;});
        QObject::connect(tree->verticalScrollBar(),&QScrollBar::valueChanged,&page,[&]{++scrollChanges;});
        double appendMs=0,eventMs=0;
        QVector<double> latency;QElapsedTimer total;total.start();
        for(int batch=0;batch<batches;++batch){
            FrameBatch input;for(int n=0;n<batchSize;++n)input.append(record(10000+batch*batchSize+n));
            QElapsedTimer slice;slice.start();vm.frames()->append(input);const double appended=slice.nsecsElapsed()/1000000.0;appendMs+=appended;QCoreApplication::processEvents();eventMs+=slice.nsecsElapsed()/1000000.0-appended;
            latency.append(slice.nsecsElapsed()/1000000.0);
        }
        const double wallMs=total.nsecsElapsed()/1000000.0;
        std::sort(latency.begin(),latency.end());
        const bool complete=vm.frames()->historyCount()==10000+batches*batchSize;
        runs.append(QJsonObject{{"mode",mode},{"wallMs",wallMs},{"p50Ms",latency[latency.size()/2]},
            {"p95Ms",latency[latency.size()*95/100]},{"maxMs",latency.last()},
            {"historyCount",double(vm.frames()->historyCount())},{"visibleRows",tree->model()->rowCount()},
            {"appendIncludingProxyMs",appendMs},{"processEventsMs",eventMs},{"viewportPaintMs",app.paintMs},{"viewportPaints",app.paints},
            {"layoutEventDispatchMs",app.layoutMs},{"layoutEvents",app.layouts},{"followRefreshMs",app.refreshMs},{"followRefreshes",app.refreshes},
            {"sourceInsertSignals",sourceInserts},{"sourceRemoveSignals",sourceRemoves},{"proxyInsertSignals",proxyInserts},{"proxyLayoutSignals",proxyLayouts},{"scrollValueChanges",scrollChanges},
            {"completeHistory",complete}});
        app.viewport=app.tree=app.followTimer=nullptr;
        if(!complete)return 2;
    }
    const auto json=QJsonDocument(QJsonObject{{"qt",qVersion()},{"batchSize",batchSize},{"runs",runs}}).toJson();
    std::fwrite(json.constData(),1,size_t(json.size()),stdout);return 0;
}
