#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include "model/DatabaseImporter.h"
#include "model/FrameTableModel.h"
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

using namespace host;
static double cpuMs(){
#ifdef Q_OS_WIN
    FILETIME created,exited,kernel,user;
    if(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)){
        ULARGE_INTEGER k,u;k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;
        u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;
        return double(k.QuadPart+u.QuadPart)/10000.0;
    }
#endif
    return -1;
}
template<class F> static QJsonObject measure(F operation){
    const auto cpu=cpuMs();QElapsedTimer elapsed;elapsed.start();const qint64 checksum=operation();
    return {{"wallMs",elapsed.nsecsElapsed()/1000000.0},{"cpuMs",cpu<0?-1:cpuMs()-cpu},{"checksum",double(checksum)}};
}
static FrameRecord frame(qint64 n){
    FrameRecord r;r.captureUs=n*1000;r.epochMs=1789718400000LL+n;r.timestamp="12:00:00.000";
    r.bus=signal::Bus::Can;r.channel="CAN1";r.id=291;r.typed=true;r.payload=QByteArray::fromHex("020a000000000000");
    r.length=8;r.identifier="0x123";r.data="02 0A 00 00 00 00 00 00";r.direction="RX";return r;
}
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    const auto db=signal::DatabaseImporter::load(QString(TEST_SOURCE_DIR)+"/fixtures/signals-synthetic.dbc",signal::Bus::Can);
    if(!db.database)return 1;
    FrameBatch input;input.reserve(100000);for(int n=0;n<100000;++n)input.append(frame(n));
    QJsonArray runs;
    for(int run=0;run<3;++run){
        QJsonObject result;
        for(const auto &mode:QStringList{"pausedAppend","liveAppend","rollingAppend"}){
            FrameTableModel model;model.setPaused(mode=="pausedAppend");
            if(mode=="rollingAppend"){model.setDatabase(db.database);model.setRolling(true);}
            result[mode]=measure([&]{for(int n=0;n<input.size();n+=100)model.append(input.mid(n,100));return model.historyCount();});
            if(model.historyCount()!=100000)return 2;
        }
        FrameTableModel history;history.setPaused(true);history.append(input);history.setHistoryStart(0);
        result["browseWindows"]=measure([&]{for(int n=0;n<200;++n)history.setHistoryStart((n%90)*1000);return history.windowStart();});
        result["repeatWindow"]=measure([&]{for(int n=0;n<200;++n)history.setHistoryStart(history.windowStart());return history.windowStart();});
        result["paintTime"]=measure([&]{qint64 checksum=0;for(int n=0;n<1000000;++n)checksum+=history.index(n%10000,1).data().toString().size();return checksum;});
        FrameTableModel tree;tree.setDatabase(db.database);tree.setRolling(true);
        FrameBatch channels;for(int n=0;n<10000;++n){auto r=frame(n);r.channel=QString::number(n);channels.append(r);}tree.append(channels);
        const auto child=tree.index(1,5,tree.index(9999,0));
        result["childParentLookup"]=measure([&]{qint64 checksum=0;for(int n=0;n<100000;++n)checksum+=tree.parent(child).row();return checksum;});
        if(tree.parent(child)!=tree.index(9999,0))return 3;
        runs.append(result);
    }
    const auto output=QJsonDocument(QJsonObject{{"qt",qVersion()},{"records",100000},{"runs",runs}}).toJson();
    std::fwrite(output.constData(),1,size_t(output.size()),stdout);return 0;
}
