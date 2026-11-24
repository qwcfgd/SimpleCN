#include <QtTest>
#include <algorithm>
#include <QAbstractItemModelTester>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QScrollBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QFile>
#include <QTreeView>
#include "model/FramePresentation.h"
#include "model/FrameFilterProxy.h"
#include "model/DatabaseImporter.h"
#include "views/FrameColumnMenu.h"
#include "views/FrameHeaderView.h"
#include "views/FrameDataDelegate.h"
#include "views/FrameTableView.h"
#include "views/ChannelPage.h"
#include "localization/Language.h"
#include "infrastructure/BusHardware.h"
#include "infrastructure/SignalTransmitter.h"
#include "domain/TraceClock.h"
using namespace host;
class EchoCanHardware final:public CanHardware {
public:
    bool echoEnabled=false,allowEcho=true;int writes=0,echoCalls=0;QVector<TPCANMsg> readback;
    CanBusState state=CanBusState::Ready;
    CanBusState classifyStatus(unsigned long)const override{return state;}
    CanBusState busState()override{return state;}
    communication::HardwareChannels scan(QString&)override{return {};}
    bool open(const communication::HardwareChannel&,const communication::SoftwareChannelConfiguration&,QString&)override{return true;}
    bool close(QString&)override{return true;}
    communication::Health health(QString&)override{return communication::Health::Ready;}
    bool enableEcho()override{++echoCalls;echoEnabled=allowEcho;return allowEcho;}
    bool sendRaw(TPCANMsg message)override{++writes;if(echoEnabled){message.MSGTYPE=TPCANMessageType(message.MSGTYPE|PCAN_MESSAGE_ECHO);readback.append(message);}return true;}
    DWORD recvRaw(DWORD count,TPCANMsg*messages,TPCANTimestamp*)override{const int n=qMin(int(count),readback.size());for(int i=0;i<n;++i)messages[i]=readback[i];readback.remove(0,n);return DWORD(n);}
    unsigned long lastErrorCode()const override{return allowEcho?PCAN_ERROR_OK:PCAN_ERROR_ILLPARAMVAL;}
    QString lastErrorText()const override{return "injected echo failure";}
};
class FrameColumnsTest:public QObject {
    Q_OBJECT
    FrameRecord record(qint64 time,quint32 id,QString direction="Rx"){
        FrameRecord r;r.captureUs=time;r.channel="CAN1";r.direction=direction;r.identifier="0x"+QString::number(id,16).toUpper();r.id=id;r.typed=true;r.length=2;r.payload=QByteArray::fromHex("aabb");r.data="AA BB";r.status="CAN ISO-TP";return r;
    }
    FrameColumnFilter rule(FrameFilterOp op,const QString&text,int base=16,const QString&upper={}){
        FrameColumnFilter f;FrameCondition c;c.op=op;c.value=text;c.base=base;c.upper=upper;f.conditions.append(c);return f;
    }
    ChannelSettings settings(){auto s=ChannelSettings::defaults(communication::Bus::Can);s.simulation=true;return s;}
private slots:
    void filteredWindowIgnoresHiddenTrafficAndKeepsVisibleRecords(){
        FrameTableModel model;FrameFilterProxy proxy;proxy.setSourceModel(&model);
        FrameColumnFilter filter;filter.valuesEnabled=true;filter.values={QString::number(0x715),QString::number(0x795)};
        QVERIFY(proxy.setColumnFilter(5,filter));model.append({record(1,0x715),record(2,0x795)});
        QCOMPARE(proxy.rowCount(),2);QSignalSpy inserted(&proxy,&QAbstractItemModel::rowsInserted);
        QSignalSpy removed(&proxy,&QAbstractItemModel::rowsRemoved);QSignalSpy reset(&proxy,&QAbstractItemModel::modelReset);
        FrameBatch hidden;for(int n=0;n<FrameTableModel::Capacity+37;++n)hidden.append(record(3+n,0x38b));
        model.append(hidden);QCOMPARE(proxy.rowCount(),2);QCOMPARE(proxy.index(0,5).data().toString(),QString("0x715"));
        QCOMPARE(proxy.index(1,5).data().toString(),QString("0x795"));QCOMPARE(inserted.count(),0);QCOMPARE(removed.count(),0);QCOMPARE(reset.count(),0);
        model.setPaused(true);model.append({record(20000,0x715),record(20001,0x38c)});QCOMPARE(proxy.rowCount(),2);
        model.setPaused(false);QCOMPARE(proxy.rowCount(),3);QVERIFY(proxy.setColumnFilter(5,{}));
        QCOMPARE(model.rowCount(),FrameTableModel::Capacity);QCOMPARE(model.historyCount(),qint64(FrameTableModel::Capacity+41));
    }
    void filteredResumeScansOnlyPausedBacklog(){
        FrameTableModel model;int evaluations=0;
        model.setDisplayFilter([&](const FrameRecord &r){++evaluations;return r.id==0x715;});
        FrameBatch initial;for(int n=0;n<10000;++n)initial.append(record(n,n%2?0x715:0x38b));model.append(initial);
        QCOMPARE(model.viewHistoryCount(),qint64(5000));model.setPaused(true);evaluations=0;
        FrameBatch backlog;for(int n=0;n<100;++n)backlog.append(record(20000+n,n%2?0x715:0x38b));model.append(backlog);
        QCOMPARE(evaluations,0);model.setPaused(false);QCOMPARE(evaluations,100);QCOMPARE(model.viewHistoryCount(),qint64(5050));
        model.setPaused(true);model.append({record(25000,0x795)});
        model.setDisplayFilter([&](const FrameRecord &r){++evaluations;return r.id==0x795;});QCOMPARE(model.viewHistoryCount(),qint64(0));
        evaluations=0;model.append({record(25001,0x795)});model.setPaused(false);QCOMPARE(evaluations,2);QCOMPARE(model.viewHistoryCount(),qint64(2));
        model.setDisplayFilter([&](const FrameRecord &r){++evaluations;return r.id==0x715;});
        model.setPaused(true);model.clear();evaluations=0;model.append({record(30000,0x715),record(30001,0x38b)});
        model.setPaused(false);QCOMPARE(evaluations,2);QCOMPARE(model.viewHistoryCount(),qint64(1));
    }
    void filteredAlternatingRowsPaintFullViewport(){
        FrameTableModel model;FrameFilterProxy proxy;proxy.setSourceModel(&model);
        FrameColumnFilter filter;filter.valuesEnabled=true;filter.values={QString::number(0x715),QString::number(0x795)};
        QVERIFY(proxy.setColumnFilter(5,filter));
        model.append({record(1,0x715),record(2,0x795),record(3,0x715),record(4,0x795)});
        FrameTableView view;view.setAlternatingRowColors(true);view.setUniformRowHeights(true);
        view.setSelectionMode(QAbstractItemView::NoSelection);view.setFocusPolicy(Qt::NoFocus);
        QPalette palette=view.palette();palette.setColor(QPalette::Base,Qt::white);palette.setColor(QPalette::AlternateBase,QColor(230,230,230));view.setPalette(palette);
        view.setItemDelegate(new FrameDataDelegate(&view));view.setModel(&proxy);view.resize(1100,220);view.show();QCoreApplication::processEvents();
        auto pixels=[&](){QList<QList<QColor>> result;const auto image=view.viewport()->grab().toImage();
            for(int row=0;row<4;++row){const auto rect=view.visualRect(proxy.index(row,0));const int y=rect.center().y();
                result.append({image.pixelColor(2,y),image.pixelColor(rect.right()-4,y),image.pixelColor(image.width()-5,y)});}
            return result;};
        const auto before=pixels();QCOMPARE(before.size(),4);
        for(int row=0;row<4;++row){QCOMPARE(before[row][0],before[row][1]);QCOMPARE(before[row][1],before[row][2]);
            if(row)QVERIFY(before[row][0]!=before[row-1][0]);}
        FrameBatch hidden;for(int n=0;n<FrameTableModel::Capacity+4;++n)hidden.append(record(100+n,0x38b));
        model.append(hidden);QCoreApplication::processEvents();QCOMPARE(pixels(),before);
    }
    void filteredViewPreservesManualScrollDuringHiddenTraffic(){
        ChannelViewModel vm(settings());ChannelPage page(&vm);page.resize(1100,700);page.show();QCoreApplication::processEvents();
        auto *tree=page.findChild<QTreeView*>("frameTable");auto *follow=page.findChild<QCheckBox*>("frameFollow");QVERIFY(tree&&follow);
        auto *proxy=qobject_cast<FrameFilterProxy*>(tree->model());QVERIFY(proxy);follow->setChecked(true);
        FrameColumnFilter filter;filter.valuesEnabled=true;filter.values={QString::number(0x715),QString::number(0x795)};QVERIFY(proxy->setColumnFilter(5,filter));
        FrameBatch visible;for(int n=0;n<150;++n)visible.append(record(n+1,n%2?0x795:0x715));vm.frames()->append(visible);
        QCoreApplication::processEvents();auto *bar=tree->verticalScrollBar();QVERIFY(bar->maximum()>5);
        bar->setSliderDown(true);bar->setValue(bar->maximum()/3);const int anchor=bar->value();
        FrameBatch hidden;for(int n=0;n<FrameTableModel::Capacity+20;++n)hidden.append(record(1000+n,0x38b));
        vm.frames()->append(hidden);QCoreApplication::processEvents();QCOMPARE(proxy->rowCount(),150);QCOMPARE(bar->value(),anchor);
        vm.frames()->append({record(20000,0x715)});QCoreApplication::processEvents();QCOMPARE(bar->value(),anchor);
        bar->setSliderDown(false);vm.frames()->append({record(20001,0x795)});QCoreApplication::processEvents();QCOMPARE(bar->value(),bar->maximum());
    }
    void filteredFullWindowKeepsDraggedRecordWhenOldestEvicted(){
        ChannelViewModel vm(settings());ChannelPage page(&vm);page.resize(1100,700);page.show();QCoreApplication::processEvents();
        auto *tree=page.findChild<QTreeView*>("frameTable");auto *follow=page.findChild<QCheckBox*>("frameFollow");QVERIFY(tree&&follow);
        auto *proxy=qobject_cast<FrameFilterProxy*>(tree->model());QVERIFY(proxy);follow->setChecked(true);
        FrameColumnFilter filter;filter.valuesEnabled=true;filter.values={QString::number(0x715),QString::number(0x795)};QVERIFY(proxy->setColumnFilter(5,filter));
        FrameBatch batch;for(int n=0;n<FrameTableModel::Capacity;++n)batch.append(record(n*1000,n%2?0x795:0x715));vm.frames()->append(batch);QCoreApplication::processEvents();
        auto *bar=tree->verticalScrollBar();QVERIFY(bar->maximum()>10);bar->setSliderDown(true);bar->setValue(bar->maximum()/2);
        const auto first=[&]{return tree->indexAt(QPoint(50,10)).siblingAtColumn(1).data();};const auto anchor=first();QVERIFY(anchor.isValid());
        vm.frames()->append({record(20000000,0x38b)});QCoreApplication::processEvents();QCOMPARE(first(),anchor);
        vm.frames()->append({record(20001000,0x715)});QCoreApplication::processEvents();QCOMPARE(first(),anchor);
        bar->setSliderDown(false);const auto afterRelease=first();QCOMPARE(afterRelease,anchor);
        vm.frames()->append({record(20002000,0x795)});QCoreApplication::processEvents();QCOMPARE(bar->value(),bar->maximum());
    }
    void peakStatusDecodingIsExplicitAndVendorScoped(){
        auto hardware=createCanHardware();
        QCOMPARE(hardware->classifyStatus(PCAN_ERROR_OK),CanBusState::Ready);
        QCOMPARE(hardware->classifyStatus(PCAN_ERROR_QRCVEMPTY),CanBusState::Ready);
        QCOMPARE(hardware->classifyStatus(PCAN_ERROR_BUSHEAVY),CanBusState::Warning);
        QCOMPARE(hardware->classifyStatus(PCAN_ERROR_BUSPASSIVE),CanBusState::Warning);
        QCOMPARE(hardware->classifyStatus(PCAN_ERROR_BUSOFF),CanBusState::BusOff);
        QCOMPARE(hardware->classifyStatus(PCAN_ERROR_INITIALIZE),CanBusState::Fatal);
        TPCANMsg status={};status.MSGTYPE=PCAN_MESSAGE_STATUS;status.DATA[3]=0x10;
        QCOMPARE(hardware->statusFrame(status),CanBusState::BusOff);
        status.DATA[3]=0;status.DATA[1]=4;QCOMPARE(hardware->statusFrame(status),CanBusState::Warning);
        status.MSGTYPE=PCAN_MESSAGE_STANDARD;QCOMPARE(hardware->statusFrame(status),CanBusState::Unsupported);
        EchoCanHardware other;QCOMPARE(other.statusFrame(status),CanBusState::Unsupported);
    }
    void canRtrEchoRequiresMatchingDlc(){
        EchoCanHardware hardware;SignalTransmitter tx;signal::TxPlan plan;plan.bus=signal::Bus::Can;
        plan.run=1;plan.replay=true;plan.repeatCount=1;plan.replayStartUs=captureTimeUs();plan.replayDurationUs=1000000;
        signal::ReplayRecord record;record.id=0x123;record.rtr=true;record.length=8;plan.replayFrames={record};
        tx.start(plan,500000,false,&hardware,nullptr);QTRY_COMPARE(hardware.writes,1);QVERIFY(tx.hasUnconfirmedCanFrame());
        signal::BusFrameEvent echo;echo.bus=signal::Bus::Can;echo.id=0x123;echo.rtr=true;echo.length=4;echo.source=signal::EventSource::HardwareEcho;
        tx.observe({echo});QVERIFY(tx.hasUnconfirmedCanFrame());echo.length=8;tx.observe({echo});QVERIFY(!tx.hasUnconfirmedCanFrame());tx.stop();
    }
    void canWarningStopsPeriodicWithoutAutomaticRetry(){
        EchoCanHardware hardware;SignalTransmitter tx;int attempts=0;
        tx.guardedCanSend=[&](const signal::TxItem&,QString &error){++attempts;hardware.state=CanBusState::Warning;error="bus warning";return false;};
        connect(&tx,&SignalTransmitter::canRecoveryNeeded,&tx,[&](int,quint64){tx.linkFailed("bus warning");},Qt::QueuedConnection);
        signal::TxPlan plan;plan.bus=signal::Bus::Can;plan.run=1;plan.periodic=true;
        plan.items={{"CAN:STD:1813",0x715,false,QByteArray::fromHex("023101dd01"),1,1,{},false,true}};
        tx.start(plan,500000,false,&hardware,nullptr);QTRY_VERIFY(!tx.running());
        QCOMPARE(attempts,1);hardware.state=CanBusState::Ready;QTest::qWait(50);QCOMPARE(attempts,1);
    }
    void canPeriodicPauseKeepsOneUnconfirmedAndNoCatchup(){
        for(bool periodic:{false,true}){
            EchoCanHardware hardware;SignalTransmitter tx;bool failOnce=true;
            tx.guardedCanSend=[&](const signal::TxItem&i,QString&e){
                if(failOnce){failOnce=false;hardware.state=CanBusState::Warning;e="bus passive";return false;}
                TPCANMsg frame={};frame.ID=i.id;frame.LEN=BYTE(i.payload.size());std::copy(i.payload.cbegin(),i.payload.cend(),frame.DATA);return hardware.sendRaw(frame);
            };
            signal::TxPlan plan;plan.bus=signal::Bus::Can;plan.run=1;plan.periodic=periodic;plan.repeatCount=2;
            plan.items={{"CAN:STD:1813",0x715,false,QByteArray::fromHex("023101dd01"),1,1,{},false,true}};
            tx.start(plan,500000,false,&hardware,nullptr);QVERIFY(tx.running());QTest::qWait(10);QCOMPARE(hardware.writes,0);
            hardware.state=CanBusState::Ready;tx.setCanPaused(false);QTRY_COMPARE(hardware.writes,1);QTest::qWait(10);QCOMPARE(hardware.writes,1);
            signal::BusFrameEvent echo;echo.bus=signal::Bus::Can;echo.id=0x715;echo.bytes=plan.items[0].payload;echo.source=signal::EventSource::HardwareEcho;
            auto wrong=echo;wrong.id=0x700;tx.observe({wrong});QTest::qWait(5);QCOMPARE(hardware.writes,1);
            tx.observe({echo});QTRY_COMPARE(hardware.writes,2);
            tx.setCanPaused(true,true);QTest::qWait(10);QCOMPARE(hardware.writes,2);
            if(periodic){tx.setCanPaused(false);QTRY_COMPARE(hardware.writes,3);QTest::qWait(5);QCOMPARE(hardware.writes,3);}
            tx.stop();const int count=hardware.writes;tx.observe({echo});QTest::qWait(10);QCOMPARE(hardware.writes,count);
        }
    }
    void filteredHiddenUpdatesKeepVisibleAge(){
        FrameTableModel model;model.setRolling(true);FrameFilterProxy proxy;proxy.setSourceModel(&model);
        auto visible=record(10,0x100),hidden=record(11,0x200);model.append({visible,hidden});
        QVERIFY(proxy.setColumnFilter(5,rule(FrameFilterOp::Equal,"100")));proxy.sort(0,Qt::DescendingOrder);QCOMPARE(proxy.rowCount(),1);
        const auto before=proxy.index(0,7).data(FrameTableModel::NibbleAgeRole).toList();QVERIFY(!before.isEmpty());
        for(int n=0;n<3;++n){hidden.captureUs+=100;hidden.payload[0]=char(n);model.append({hidden});QCOMPARE(proxy.rowCount(),1);QCOMPARE(proxy.index(0,5).data().toString(),QString("0x100"));QCOMPARE(proxy.index(0,7).data(FrameTableModel::NibbleAgeRole).toList(),before);}
        visible.captureUs=1000;model.append({visible});const auto after=proxy.index(0,7).data(FrameTableModel::NibbleAgeRole).toList();QVERIFY(after!=before);
        QVERIFY(proxy.setColumnFilter(5,{}));QCOMPARE(proxy.rowCount(),2);proxy.sort(5,Qt::DescendingOrder);QCOMPARE(proxy.index(1,5).data().toString(),QString("0x100"));QCOMPARE(proxy.index(1,7).data(FrameTableModel::NibbleAgeRole).toList(),after);
    }
    void hiddenTrafficDoesNotResetVisibleAgeAtCapacity(){
        FrameTableModel model;model.setRolling(true);FrameFilterProxy proxy;proxy.setSourceModel(&model);
        QVERIFY(proxy.setColumnFilter(5,rule(FrameFilterOp::Equal,"100")));
        auto visible=record(1,0x100);model.append({visible,visible,visible});
        FrameBatch hidden;
        for(int n=0;n<9998;++n){auto r=record(10+n,quint32(0x1000+n));r.extended=true;hidden.append(r);}
        model.append(hidden);visible.captureUs=20000;model.append({visible});
        auto a=record(20001,0x9000),b=record(20002,0x9001);a.extended=b.extended=true;
        model.append({a});QCOMPARE(proxy.rowCount(),1);
        const auto before=proxy.index(0,7).data(FrameTableModel::NibbleAgeRole).toList();QVERIFY(before.first().toInt()>0);
        model.append({b});QCOMPARE(proxy.rowCount(),1);
        QCOMPARE(proxy.index(0,7).data(FrameTableModel::NibbleAgeRole).toList(),before);
        visible.captureUs=20003;model.append({visible});
        QVERIFY(proxy.index(0,7).data(FrameTableModel::NibbleAgeRole).toList().first().toInt()>before.first().toInt());
    }
    void rollingRowBackgroundFollowsFrameIdentity(){
        FrameTableModel model;model.setRolling(true);FrameFilterProxy proxy;proxy.setSourceModel(&model);
        auto first=record(1,0x100),hidden=record(2,0x200),second=record(3,0x300);
        model.append({first,hidden,second});QVERIFY(proxy.setColumnFilter(5,rule(FrameFilterOp::NotEqual,"200")));
        proxy.sort(0,Qt::AscendingOrder);QCOMPARE(proxy.rowCount(),2);
        auto background=[&](const QString&id){for(int n=0;n<proxy.rowCount();++n)if(proxy.index(n,5).data().toString()==id)return proxy.index(n,0).data(Qt::BackgroundRole);return QVariant();};
        const auto one=background("0x100"),two=background("0x300");QVERIFY(!one.isValid());QVERIFY(!two.isValid());
        hidden.captureUs=10;model.append({hidden});QCOMPARE(background("0x100"),one);QCOMPARE(background("0x300"),two);
        first.captureUs=11;model.append({first});QCOMPARE(background("0x100"),one);QCOMPARE(background("0x300"),two);
        proxy.sort(5,Qt::DescendingOrder);QCOMPARE(background("0x100"),one);QCOMPARE(background("0x300"),two);
        model.setRolling(false);QVERIFY(proxy.setColumnFilter(5,{}));QVERIFY(model.index(0,0).data(Qt::BackgroundRole).isValid());
    }
    void historyFilteredRemainingRowKeepsBackgroundAfterEviction(){
        FrameTableModel model;FrameFilterProxy proxy;proxy.setSourceModel(&model);
        FrameColumnFilter f;f.valuesEnabled=true;f.values={QString::number(0x715),QString::number(0x795)};
        QVERIFY(proxy.setColumnFilter(5,f));proxy.sort(0,Qt::AscendingOrder);
        FrameBatch batch;batch.append(record(1,0x715));batch.append(record(2,0x795));
        for(int n=0;n<FrameTableModel::Capacity-2;++n)batch.append(record(3+n,0x38b));
        model.append(batch);QCOMPARE(proxy.rowCount(),2);
        FrameTableView view;view.setAlternatingRowColors(true);view.setUniformRowHeights(true);view.setSelectionMode(QAbstractItemView::NoSelection);
        view.setItemDelegate(new FrameDataDelegate(&view));view.setModel(&proxy);view.resize(700,160);view.show();QCoreApplication::processEvents();
        QTest::mouseMove(view.viewport(),view.visualRect(proxy.index(0,0)).center());QCoreApplication::processEvents();
        auto color=[&](){for(int row=0;row<proxy.rowCount();++row)if(proxy.index(row,5).data().toString()=="0x795"){
            const auto rect=view.visualRect(proxy.index(row,0));const auto shot=view.viewport()->grab().toImage();
            return shot.pixelColor(rect.right()-4,rect.center().y());}return QColor();};
        const auto before=color();QVERIFY(before.isValid());
        model.append({record(20000,0x38c)});QCoreApplication::processEvents();
        QCOMPARE(proxy.rowCount(),2);QCOMPARE(proxy.index(1,5).data().toString(),QString("0x795"));QCOMPARE(color(),before);
    }
    void historyEvictionPreservesOtherSelectedCurrent(){
        FrameTableModel model;FrameFilterProxy proxy;proxy.setSourceModel(&model);
        FrameColumnFilter f;f.valuesEnabled=true;f.values={QString::number(0x715),QString::number(0x795)};
        QVERIFY(proxy.setColumnFilter(5,f));proxy.sort(0,Qt::AscendingOrder);
        FrameBatch batch;batch.append(record(1,0x715));batch.append(record(2,0x795));
        for(int n=0;n<FrameTableModel::Capacity-2;++n)batch.append(record(3+n,0x38b));
        model.append(batch);FrameTableView view;view.setSelectionMode(QAbstractItemView::ExtendedSelection);view.setModel(&proxy);
        view.selectionModel()->setCurrentIndex(proxy.index(1,0),QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);
        model.append({record(20000,0x38c)});QCoreApplication::processEvents();
        QCOMPARE(proxy.rowCount(),2);QCOMPARE(view.currentIndex().sibling(view.currentIndex().row(),5).data().toString(),QString("0x795"));
        QVERIFY(view.selectionModel()->isRowSelected(1,QModelIndex()));
    }
    void filtered715795BackgroundRemainsStableDuringAppTraffic_data(){
        QTest::addColumn<bool>("rolling");QTest::newRow("history")<<false;QTest::newRow("merged")<<true;
    }
    void filtered715795BackgroundRemainsStableDuringAppTraffic(){
        QFETCH(bool,rolling);FrameTableModel model;model.setRolling(rolling);FrameFilterProxy proxy;proxy.setSourceModel(&model);
        FrameColumnFilter f;f.valuesEnabled=true;f.values={QString::number(0x715),QString::number(0x795)};
        QVERIFY(proxy.setColumnFilter(5,f));proxy.sort(0,Qt::AscendingOrder);
        // Fill the retained history with application traffic so subsequent
        // hidden frames also evict hidden rows ahead of the diagnostic rows.
        FrameBatch older;for(int n=0;n<10020;++n){const quint32 ids[]={0x38b,0x388,0x38c};older.append(record(n,ids[n%3]));}
        model.append(older);model.append({record(10021,0x715),record(10022,0x795)});QCOMPARE(proxy.rowCount(),2);
        FrameTableView view;view.setAlternatingRowColors(true);view.setUniformRowHeights(true);
        view.setItemDelegate(new FrameDataDelegate(&view));view.setModel(&proxy);view.resize(700,160);view.show();QCoreApplication::processEvents();
        auto colors=[&](){QList<QColor> result;const auto shot=view.viewport()->grab().toImage();
            for(int n=0;n<proxy.rowCount();++n){const auto rect=view.visualRect(proxy.index(n,0));
                if(rect.isValid()&&shot.rect().contains(QPoint(rect.right()-4,rect.center().y())))result.append(shot.pixelColor(rect.right()-4,rect.center().y()));}
            return result;};
        const auto before=colors();QCOMPARE(before.size(),2);
        for(int n=0;n<300;++n){const quint32 ids[]={0x38b,0x388,0x38c};auto app=record(10023+n,ids[n%3]);model.append({app});
            if(n%30==0){QCoreApplication::processEvents();QCOMPARE(proxy.rowCount(),2);QCOMPARE(colors(),before);}}
        QCoreApplication::processEvents();QCOMPARE(colors(),before);
    }
    void initTestCase(){QApplication::setStyle("Fusion");QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");QApplication::setFont(QFont("Microsoft YaHei",9));}
    void cleanup(){Language::instance().setCode("zh_CN");}
    void sharedPresentationAndDefaultDirection(){
        FrameTableModel model;FrameFilterProxy proxy;proxy.setSourceModel(&model);
        auto pending=record(0,1,"CAN Tx");pending.request=true;
        auto echo=record(1,2,"TX ECHO");echo.echo=true;
        auto sim=record(2,3,"SIM TX");sim.simulated=true;
        auto rx=record(3,4,"RX");auto absent=rx;absent.noResponse=true;auto error=rx;error.error=true;auto hardware=rx;hardware.hardwareStatus=true;
        model.append({pending,echo,sim,rx,absent,error,hardware});
        QCOMPARE(model.index(0,4).data().toString(),QString("TxPending"));QCOMPARE(model.index(0,8).data().toString(),QString("待确认"));
        QCOMPARE(model.index(1,4).data().toString(),QString("Tx"));QCOMPARE(model.index(2,4).data().toString(),QString("Tx"));QCOMPARE(model.index(3,4).data().toString(),QString("Rx"));
        QCOMPARE(model.index(4,8).data().toString(),QString("无响应"));QCOMPARE(model.index(5,8).data().toString(),QString("错误"));QCOMPARE(model.index(6,8).data().toString(),QString("硬件状态"));
        QVERIFY(model.index(2,8).data(Qt::ToolTipRole).toString().contains("CAN ISO-TP"));QVERIFY(model.index(2,8).data(Qt::ToolTipRole).toString().contains("模拟"));
        QCOMPARE(proxy.rowCount(),6);QCOMPARE(proxy.filteredHistory().size(),6);QVERIFY(model.history()[0].request);QVERIFY(model.history()[1].echo);
        QVERIFY(proxy.setColumnFilter(4,{}));QCOMPARE(proxy.rowCount(),7);
        model.append(model.history().mid(0,1));QCOMPARE(model.index(7,4).data().toString(),QString("TxPending"));QCOMPARE(model.history()[7].detail,QString("CAN ISO-TP"));
    }
    void physicalCanEchoVisibleWithDefaultFilter_data(){
        QTest::addColumn<int>("mode");QTest::newRow("one-shot")<<0;QTest::newRow("periodic")<<1;QTest::newRow("replay")<<2;
    }
    void physicalCanEchoVisibleWithDefaultFilter(){
        QFETCH(int,mode);EchoCanHardware hardware;SignalTransmitter tx;signal::BusFrameEvents events;signal::RunStatus status;
        connect(&tx,&SignalTransmitter::events,this,[&](const signal::BusFrameEvents&batch){events+=batch;});connect(&tx,&SignalTransmitter::statusChanged,this,[&](const signal::RunStatus&s){status=s;});
        signal::TxPlan plan;plan.bus=signal::Bus::Can;plan.run=1;plan.periodic=mode==1;plan.items={{"CAN:STD:291",0x123,false,QByteArray::fromHex("1234"),20,1,{},false,true}};
        if(mode==2){plan.replay=true;plan.replayStartUs=captureTimeUs();plan.replayDurationUs=100000;signal::ReplayRecord r;r.id=0x123;r.length=2;r.payload=plan.items[0].payload;plan.replayFrames.append(r);}
        tx.start(plan,500000,false,&hardware,nullptr);QTRY_VERIFY(hardware.writes>0);QCOMPARE(hardware.echoCalls,1);QTRY_VERIFY(!events.isEmpty());tx.stop();
        QCOMPARE(events.first().source,signal::EventSource::RequestAccepted);QVERIFY(!hardware.readback.isEmpty());
        FrameTableModel model;FrameFilterProxy proxy;proxy.setSourceModel(&model);auto pending=record(0,0x123,"TX");pending.request=true;model.append({pending});QCOMPARE(proxy.rowCount(),0);
        TPCANMsg echo={};QCOMPARE(hardware.recvRaw(1,&echo,nullptr),DWORD(1));QVERIFY(echo.MSGTYPE&PCAN_MESSAGE_ECHO);
        auto confirmed=record(1,echo.ID,"TX ECHO");confirmed.echo=bool(echo.MSGTYPE&PCAN_MESSAGE_ECHO);confirmed.payload=QByteArray(reinterpret_cast<const char*>(echo.DATA),echo.LEN);
        normalizeFramePresentation(confirmed);model.append({confirmed});QCOMPARE(proxy.rowCount(),1);QCOMPARE(proxy.index(0,4).data().toString(),QString("Tx"));QCOMPARE(model.history().last().payload,plan.items[0].payload);
        EchoCanHardware refused;refused.allowEcho=false;events.clear();++plan.run;plan.replayStartUs=captureTimeUs();tx.start(plan,500000,false,&refused,nullptr);
        QCOMPARE(status.state,signal::RunState::Faulted);QVERIFY(status.detail.contains("injected echo failure"));QCOMPARE(refused.writes,0);QVERIFY(events.isEmpty());
    }
    void strictColumnsNumbersAndWildcards(){
        FrameTableModel model;FrameFilterProxy proxy;proxy.setSourceModel(&model);auto a=record(0,0x10),b=record(1,0x100);b.channel="CAN2";b.data="11 22";model.append({a,b});
        QVERIFY(proxy.setColumnFilter(5,rule(FrameFilterOp::Greater,"20")));QCOMPARE(proxy.rowCount(),1);QCOMPARE(proxy.index(0,5).data().toString(),QString("0x100"));
        QVERIFY(proxy.setColumnFilter(5,rule(FrameFilterOp::Between,"10000",2,"100000000")));QCOMPARE(proxy.rowCount(),2);
        QVERIFY(proxy.setColumnFilter(3,rule(FrameFilterOp::Equal,"CAN?")));QCOMPARE(proxy.rowCount(),2);
        QVERIFY(proxy.setColumnFilter(7,rule(FrameFilterOp::Contains,"CAN1")));QCOMPARE(proxy.rowCount(),0);
        QVERIFY(proxy.setColumnFilter(7,rule(FrameFilterOp::Equal,"AA *")));QCOMPARE(proxy.rowCount(),1);
        QVERIFY(proxy.setColumnFilter(3,rule(FrameFilterOp::NotEqual,"CAN1")));QCOMPARE(proxy.rowCount(),0);
        QVERIFY(proxy.setColumnFilter(7,{}));QCOMPARE(proxy.rowCount(),1);
        auto invalid=rule(FrameFilterOp::Equal,"20000000");QString error;QVERIFY(!proxy.setColumnFilter(5,invalid,&error));QVERIFY(!error.isEmpty());QCOMPARE(proxy.rowCount(),1);
        FrameColumnFilter orFilter=rule(FrameFilterOp::Equal,"10");orFilter.conditions+=rule(FrameFilterOp::Equal,"256",10).conditions;orFilter.all=false;
        QVERIFY(proxy.setColumnFilter(3,{}));QVERIFY(proxy.setColumnFilter(5,orFilter));QCOMPARE(proxy.rowCount(),2);orFilter.all=true;QVERIFY(proxy.setColumnFilter(5,orFilter));QCOMPARE(proxy.rowCount(),0);
        const auto saved=proxy.saveFilters();FrameFilterProxy restored;restored.restoreFilters(saved);QCOMPARE(restored.saveFilters(),saved);restored.restoreFilters({});QVERIFY(!restored.columnActive(4));
    }
    void numericSortingAndSignalOwnership(){
        FrameTableModel model;FrameFilterProxy proxy;proxy.setSourceModel(&model);QAbstractItemModelTester tester(&proxy,QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.append({record(20,256),record(10,2),record(30,16),record(40,2)});proxy.sort(5,Qt::AscendingOrder);QCOMPARE(proxy.index(0,5).data().toString(),QString("0x2"));QCOMPARE(proxy.mapToSource(proxy.index(1,0)).row(),3);
        proxy.sort(5,Qt::DescendingOrder);QCOMPARE(proxy.index(0,5).data().toString(),QString("0x100"));QCOMPARE(proxy.mapToSource(proxy.index(2,0)).row(),1);
        proxy.sort(0,Qt::AscendingOrder);QCOMPARE(proxy.mapToSource(proxy.index(0,0)).row(),1);QCOMPARE(model.history()[0].id,quint32(256));
        model.clear();model.setDatabase(signal::DatabaseImporter::load(QString(TEST_SOURCE_DIR)+"/fixtures/signals-synthetic.dbc",signal::Bus::Can).database);model.setRolling(true);
        auto frame=record(0,291);frame.payload=QByteArray(8,0);frame.length=8;frame.data=frame.payload.toHex(' ');model.append({frame});proxy.sort(5,Qt::DescendingOrder);
        const auto parent=proxy.index(0,0);QCOMPARE(proxy.rowCount(parent),5);QCOMPARE(proxy.index(0,5,parent).data().toString(),QString("信号名称"));QCOMPARE(proxy.index(1,5,parent).data().toString(),QString("Small"));
        QVERIFY(proxy.setColumnFilter(7,rule(FrameFilterOp::Contains,"Small")));QCOMPARE(proxy.rowCount(),0);
    }
    void allColumnsAndCandidateSnapshot(){
        FrameTableModel model;FrameFilterProxy proxy;proxy.setSourceModel(&model);model.append({record(0,16),record(1000,256)});
        for(int column=0;column<9;++column){auto f=rule(FrameFilterOp::Equal,column==5?"10":model.index(0,column).data().toString());QVERIFY(proxy.setColumnFilter(column,f));QVERIFY(proxy.rowCount()>=1);QVERIFY(proxy.setColumnFilter(column,{}));}
        QVERIFY(proxy.setColumnFilter(5,rule(FrameFilterOp::Equal,"10")));auto future=proxy.candidateValues(5);model.append({record(2000,32)});QTRY_VERIFY(future.isFinished());const auto values=future.result();QCOMPARE(values.size(),2);QCOMPARE(values[0].first,QString("16"));QCOMPARE(values[1].first,QString("256"));
        auto other=proxy.candidateValues(7);QTRY_VERIFY(other.isFinished());QCOMPARE(other.result().size(),1);
    }
    void directionMenuSelectionAndCancel(){
        FrameTableModel model;FrameFilterProxy proxy;proxy.setSourceModel(&model);auto pending=record(0,1,"TxPending");pending.request=true;auto sent=record(1,2,"Tx");sent.echo=true;model.append({pending,sent,record(2,3)});QCOMPARE(proxy.rowCount(),2);
        FrameColumnMenu menu(&proxy,4);menu.popup(QPoint(30,30));auto*buttons=menu.findChild<QDialogButtonBox*>("frameColumnButtons");QTRY_VERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());auto*list=menu.findChild<QListView*>("frameColumnValues");
        for(int row=0;row<list->model()->rowCount();++row){const auto i=list->model()->index(row,0);if(i.data(Qt::UserRole)=="TxPending"){QCOMPARE(i.data(Qt::CheckStateRole).toInt(),int(Qt::Unchecked));QVERIFY(list->model()->setData(i,Qt::Checked,Qt::CheckStateRole));}}
        buttons->button(QDialogButtonBox::Ok)->click();QVERIFY(menu.applied());QVERIFY(proxy.setColumnFilter(4,menu.result()));QCOMPARE(proxy.rowCount(),3);QVERIFY(!proxy.columnActive(4));
        const auto before=proxy.saveFilters();FrameColumnMenu cancel(&proxy,4);cancel.popup(QPoint(30,30));cancel.findChild<QLineEdit*>("frameColumnSearch")->setText("Rx");cancel.findChild<QDialogButtonBox*>("frameColumnButtons")->button(QDialogButtonBox::Cancel)->click();QVERIFY(!cancel.applied());QCOMPARE(proxy.saveFilters(),before);
    }
    void defaultDirectionChoicesVisibleWithoutMatchingFrames(){
        FrameTableModel model;FrameFilterProxy proxy;proxy.setSourceModel(&model);
        const auto defaults=proxy.columnFilter(4);
        for(int scenario=0;scenario<3;++scenario){
            if(scenario==1)model.append({record(0,1)});
            if(scenario==2)QVERIFY(proxy.setColumnFilter(5,rule(FrameFilterOp::Equal,"FF")));
            FrameColumnMenu menu(&proxy,4);menu.popup(QPoint(30,30));
            auto*buttons=menu.findChild<QDialogButtonBox*>("frameColumnButtons");QTRY_VERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());
            auto*list=menu.findChild<QListView*>("frameColumnValues");QCOMPARE(list->model()->rowCount(),3);
            QSet<QString> shown;
            for(int row=0;row<list->model()->rowCount();++row){const auto item=list->model()->index(row,0);const auto value=item.data(Qt::UserRole).toString();shown.insert(value);QCOMPARE(item.data().toString(),value);QCOMPARE(item.data(Qt::CheckStateRole).toInt(),int(value=="TxPending"?Qt::Unchecked:Qt::Checked));}
            QCOMPARE(shown,QSet<QString>({"Tx","Rx","TxPending"}));
            buttons->button(QDialogButtonBox::Ok)->click();QVERIFY(menu.applied());QVERIFY(menu.result().valuesEnabled);QCOMPARE(menu.result().values,defaults.values);
        }
        FrameColumnMenu menu(&proxy,4);menu.popup(QPoint(30,30));auto*buttons=menu.findChild<QDialogButtonBox*>("frameColumnButtons");QTRY_VERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());
        menu.findChild<QLineEdit*>("frameColumnSearch")->setText("TxPending");auto*list=menu.findChild<QListView*>("frameColumnValues");QCOMPARE(list->model()->rowCount(),1);QVERIFY(list->model()->setData(list->model()->index(0,0),Qt::Checked,Qt::CheckStateRole));
        buttons->button(QDialogButtonBox::Ok)->click();QVERIFY(proxy.setColumnFilter(4,menu.result()));QCOMPARE(proxy.columnFilter(4).values,QSet<QString>({"TxPending"}));
        QVERIFY(proxy.setColumnFilter(4,{}));QVERIFY(!proxy.columnActive(4));
    }
    void headerHitTestingAndMenuCommit(){
        ChannelViewModel vm(settings());ChannelPage page(&vm);page.resize(1480,980);page.show();page.raise();page.activateWindow();QVERIFY(QTest::qWaitForWindowExposed(&page));QCoreApplication::processEvents();auto*tree=page.findChild<QTreeView*>("frameTable");auto*header=qobject_cast<FrameHeaderView*>(tree->header());auto*proxy=qobject_cast<FrameFilterProxy*>(tree->model());QVERIFY(header&&proxy);
        QVERIFY(!page.findChild<QLineEdit*>("frameFilter"));QVERIFY(!page.findChild<QCheckBox*>("frameFilterEnabled"));QCOMPARE(header->sortIndicatorSection(),0);QCOMPARE(header->sortIndicatorOrder(),Qt::AscendingOrder);QVERIFY(header->filterButtonVisible(4));
        vm.frames()->append({record(0,16),record(1000,256)});const QPoint title(header->sectionViewportPosition(5)+15,header->height()/2);QTest::mouseMove(header->viewport(),QPoint(2,2),20);QTest::mouseMove(header->viewport(),title,20);QTRY_VERIFY(header->filterButtonVisible(5));
        QVERIFY(header->filterButtonRect(5).right()<header->sectionViewportPosition(5)+header->sectionSize(5));QTest::mouseClick(header->viewport(),Qt::LeftButton,Qt::NoModifier,title);QCOMPARE(header->sortIndicatorSection(),5);QCOMPARE(header->sortIndicatorOrder(),Qt::AscendingOrder);
        QTest::mouseClick(header->viewport(),Qt::LeftButton,Qt::NoModifier,title);QCOMPARE(header->sortIndicatorOrder(),Qt::DescendingOrder);
        bool completed=false;QTimer driver;driver.setInterval(10);connect(&driver,&QTimer::timeout,&page,[&]{auto*menu=page.findChild<FrameColumnMenu*>();if(!menu||!menu->isVisible())return;auto*buttons=menu->findChild<QDialogButtonBox*>("frameColumnButtons");if(!buttons->button(QDialogButtonBox::Ok)->isEnabled())return;
            auto*search=menu->findChild<QLineEdit*>("frameColumnSearch");search->setText("0x10");QCOMPARE(proxy->rowCount(),2);QCOMPARE(menu->findChild<QListView*>()->model()->rowCount(),2); // 0x10 also matches 0x100
            search->setText("0x100");vm.frames()->append({record(2000,32)});QCOMPARE(search->text(),QString("0x100"));QCOMPARE(proxy->rowCount(),3);
            QDir().mkpath(QCoreApplication::applicationDirPath()+"/artifacts/columns");menu->grab().save(QCoreApplication::applicationDirPath()+"/artifacts/columns/menu.png");completed=true;buttons->button(QDialogButtonBox::Ok)->click();
        });driver.start();QTest::mouseClick(header->viewport(),Qt::LeftButton,Qt::NoModifier,header->filterButtonRect(5).center());driver.stop();QVERIFY(completed);QCOMPARE(proxy->rowCount(),1);QCOMPARE(header->sortIndicatorOrder(),Qt::DescendingOrder);QVERIFY(header->filterButtonVisible(5));
        auto restoredSettings=vm.snapshotSettings();ChannelViewModel restoredVm(restoredSettings);ChannelPage restored(&restoredVm);auto*restoredTree=restored.findChild<QTreeView*>("frameTable");QTRY_VERIFY(static_cast<FrameFilterProxy*>(restoredTree->model())->columnActive(5));QCOMPARE(restoredTree->header()->sortIndicatorSection(),5);QCOMPARE(restoredTree->header()->sortIndicatorOrder(),Qt::DescendingOrder);
        proxy->setColumnFilter(5,{});QEvent leave(QEvent::Leave);QApplication::sendEvent(header,&leave);QVERIFY(!header->filterButtonVisible(5));QVERIFY(header->filterButtonVisible(4));
        QDir().mkpath(QCoreApplication::applicationDirPath()+"/artifacts/columns");page.grab().save(QCoreApplication::applicationDirPath()+"/artifacts/columns/monitor.png");
    }
    void ruleDialogValidationAndMultipleRows(){
        FrameRuleDialog dialog(5,"ID",{},FrameFilterOp::Between,0x3f);dialog.show();auto*table=dialog.findChild<QTableWidget*>("frameColumnRules");table->item(0,1)->setText("10");table->item(0,2)->setText("40");auto*buttons=dialog.findChild<QDialogButtonBox*>();buttons->button(QDialogButtonBox::Ok)->click();QVERIFY(dialog.isVisible());QVERIFY(!dialog.findChild<QLabel*>("frameRuleError")->text().isEmpty());
        table->item(0,2)->setText("20");dialog.findChild<QPushButton*>("frameAddRule")->click();QCOMPARE(table->rowCount(),2);table->item(1,1)->setText("30");dialog.findChild<QComboBox*>("frameRuleJoin")->setCurrentIndex(1);buttons->button(QDialogButtonBox::Ok)->click();QCOMPARE(dialog.result().conditions.size(),2);QVERIFY(!dialog.result().all);QCOMPARE(dialog.QDialog::result(),int(QDialog::Accepted));
        Language::instance().setCode("en");FrameRuleDialog english(7,"Data",{},FrameFilterOp::Contains,0x1fffffff);english.show();QVERIFY(!english.windowTitle().contains(QRegularExpression("[\\x{3400}-\\x{9fff}]")));QCOMPARE(english.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->text(),QString("OK"));english.grab().save(QCoreApplication::applicationDirPath()+"/artifacts/columns/rules-en.png");
    }
    void legacyRulesMigrateAndDirectionClearPersists(){
        ChannelViewModel vm(settings());vm.signalTransmission()->setUiSettings(QJsonObject{{"monitor",QJsonObject{{"filter","must not match another column"},{"idFilter",QJsonObject{{"enabled",true},{"pass",true},{"rules",QJsonArray{QJsonObject{{"text","10-20"},{"base",16}}}}}}}}});
        ChannelPage page(&vm);auto*proxy=static_cast<FrameFilterProxy*>(page.findChild<QTreeView*>("frameTable")->model());vm.frames()->append({record(0,16),record(1,32),record(2,48),record(3,16,"TxPending")});QCOMPARE(proxy->rowCount(),2);
        const auto migrated=vm.signalTransmission()->working().uiSettings["monitor"].toObject();QVERIFY(migrated.contains("columnFilters"));QVERIFY(!migrated.contains("filter"));QVERIFY(!migrated.contains("idFilter"));
        QVERIFY(proxy->setColumnFilter(4,{}));QCOMPARE(proxy->rowCount(),3);ChannelViewModel restoredVm(vm.snapshotSettings());ChannelPage restored(&restoredVm);restoredVm.frames()->append(vm.frames()->history());auto*restoredProxy=static_cast<FrameFilterProxy*>(restored.findChild<QTreeView*>("frameTable")->model());QTRY_VERIFY(restoredProxy->columnActive(5));QVERIFY(!restoredProxy->columnActive(4));QCOMPARE(restoredProxy->rowCount(),3);
    }
    void taskProducersUseSamePresentation(){
        ChannelViewModel vm(settings());QTRY_VERIFY(vm.canConnect());vm.toggleConnection();QTRY_VERIFY(vm.connected()&&!vm.pending());QString error;
        QVERIFY(vm.sendDiagnostic(0,QByteArray::fromHex("1001"),error,true));QTRY_VERIFY(!vm.diagnosticBusy());QVERIFY(!vm.frames()->history().isEmpty());
        for(const auto&r:vm.frames()->history()){QVERIFY(r.direction=="Tx"||r.direction=="Rx");QCOMPARE(r.status,QString("正常"));QVERIFY(!r.detail.isEmpty());}
        vm.frames()->clear();auto*signalVm=vm.signalTransmission();QVERIFY(signalVm->putCustom({},"test",0x600,2,10,error));QVERIFY(signalVm->start(false,error));QTRY_VERIFY(!signalVm->running());QVERIFY(!vm.frames()->history().isEmpty());for(const auto&r:vm.frames()->history()){QCOMPARE(r.direction,QString("Tx"));QCOMPARE(r.status,QString("正常"));}
        vm.frames()->clear();QTemporaryDir dir;QFile image(dir.filePath("sample.bin"));QVERIFY(image.open(QIODevice::WriteOnly));image.write(QByteArray(128,char(0x5A)));image.close();auto download=vm.settings();download.applicationPath=image.fileName();download.flashRequired=false;download.downloadProfile["resetWaitMs"]=0;QVERIFY(vm.setSettings(download));QTRY_VERIFY(vm.canStart());vm.start();QTRY_COMPARE_WITH_TIMEOUT(vm.taskState(),TaskState::Completed,8000);QVERIFY(!vm.frames()->history().isEmpty());
        for(const auto&r:vm.frames()->history()){QVERIFY(r.direction=="Tx"||r.direction=="Rx");QCOMPARE(r.status,QString("正常"));QVERIFY(!r.detail.isEmpty());}
    }
};
QTEST_MAIN(FrameColumnsTest)
#include "test_frame_columns.moc"
