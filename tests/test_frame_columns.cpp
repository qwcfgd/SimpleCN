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
#include <QElapsedTimer>
#include <QFileDialog>
#include <QInputDialog>
#include <QMouseEvent>
#include <QTimer>
#include "model/FramePresentation.h"
#include "model/FrameFilterProxy.h"
#include "model/DatabaseImporter.h"
#include "views/FrameColumnMenu.h"
#include "views/FrameHeaderView.h"
#include "views/FrameDataDelegate.h"
#include "views/FrameTableView.h"
#include "views/ChannelPage.h"
#include "views/MainWindow.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QTabWidget>
#include "localization/Language.h"
#include "infrastructure/BusHardware.h"
#include "infrastructure/SignalTransmitter.h"
#include "domain/TraceClock.h"
#include "infrastructure/TraceReader.h"
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
    void peakClockMixedSourcesAndExport(){
        FrameTableModel model;
        auto a=record(100000,0x3d);a.bus=signal::Bus::Lin;a.channel="LIN1";
        a.timestamp="12:47:16.239";a.peakClockDomain=1;a.hardwareUs=0;
        auto b=a;b.captureUs+=64;b.hardwareUs=10001;
        model.append({a,b});
        const auto &first=model.history().first();
        QCOMPARE(first.timeUs,qint64(0));QCOMPARE(first.timestamp,a.timestamp);
        QCOMPARE(model.history()[1].relativeTime,QString("0.010001"));
        QCOMPARE(model.history()[1].intervalSeconds,QString("0.010001"));
        QTemporaryDir directory;QString error;
        for(const auto ext:{"csv","asc","blf"}){
            const auto path=directory.filePath("clock."+QString(ext));
            QVERIFY2(model.exportTrace(path,error),qPrintable(error));
            if(QString(ext)=="csv"){
                QFile csv(path);QVERIFY(csv.open(QIODevice::ReadOnly));const auto data=csv.readAll();
                QVERIFY(data.contains(QString("PEAK 设备时间戳/µs").toUtf8()));
                QVERIFY(data.contains("\"1\",\"0\",\"100000\",\"Host\""));
                QVERIFY(data.contains("\"1\",\"10001\",\"100064\",\"PEAK\""));
            }else{
                const auto log=TraceReader::read(path);QVERIFY2(log.error.isEmpty(),qPrintable(log.error));
                QCOMPARE(log.frames.size(),2);QCOMPARE(log.frames.last().timeUs,qint64(10001));
                QCOMPARE(log.frames.last().peakClockDomain,quint64(0)); // A replay file has its own clock.
            }
        }
        auto sw=a;sw.captureUs=120000;sw.peakClockDomain=0;sw.hardwareUs=999;sw.request=true;
        auto can=record(125000,0x715);can.peakClockDomain=2;can.hardwareUs=(quint64(1)<<54);
        auto next=can;next.captureUs+=32;next.hardwareUs+=1;
        auto again=b;again.captureUs=140000;again.hardwareUs=30003;
        model.append({sw,can,next,again});
        const auto &rows=model.history();
        QCOMPARE(rows[2].relativeTime,QString("0.02"));QCOMPARE(rows[2].intervalSeconds,QString("0.019936"));
        QCOMPARE(rows[3].relativeTime,QString("0.025"));QCOMPARE(rows[3].intervalSeconds,QString("0.005"));
        QCOMPARE(rows[4].timeUs,qint64(25001));QCOMPARE(rows[4].intervalSeconds,QString("0.000001"));
        QCOMPARE(rows[5].timeUs,qint64(30003));QCOMPARE(rows[5].intervalSeconds,QString("0.014968"));
        QVERIFY(!rows[2].intervalFromHardware && !rows[3].intervalFromHardware && !rows[5].intervalFromHardware);
        model.clear();model.append({again});QCOMPARE(model.history().first().timeUs,qint64(0));
    }
    void monitorNameColumn_data(){
        QTest::addColumn<bool>("lin");
        QTest::newRow("CAN")<<false;
        QTest::newRow("LIN")<<true;
    }
    void monitorNameColumn(){
        QFETCH(bool,lin);
        QTemporaryDir dir;QVERIFY(dir.isValid());
        MainWindow window(dir.filePath("clean.json"),true);
        window.resize(1440,1000);
        auto *tabs=window.findChild<QTabWidget*>("channelTabs");
        QVERIFY(tabs);
        tabs->setCurrentIndex(lin?1:0);
        window.show();
        auto *channel=lin?window.linChannel():window.canChannel();
        auto *page=tabs->currentWidget();
        auto *table=page->findChild<QTreeView*>("frameTable");
        QVERIFY(channel&&table);
        QVERIFY(page->findChild<QWidget*>("eventLogPanel")->isVisible());
        auto *header=table->header();
        const auto nameColumn=FrameTableModel::NameColumn;
        QCOMPARE(header->visualIndex(nameColumn),header->visualIndex(5)+1);
        QCOMPARE(header->visualIndex(6),header->visualIndex(nameColumn)+1);
        QCOMPARE(table->model()->headerData(nameColumn,Qt::Horizontal).toString(),Language::text("名称"));
        QTRY_COMPARE(table->horizontalScrollBar()->maximum(),0);
        for(int c=0;c<header->count();++c){
            if(header->isSectionHidden(c))continue;
            const auto caption=table->model()->headerData(c,Qt::Horizontal).toString();
            QVERIFY(header->sectionViewportPosition(c)+header->sectionSize(c)<=table->viewport()->width());
            QVERIFY2(header->fontMetrics().horizontalAdvance(caption)+32<=header->sectionSize(c),qPrintable(caption));
        }

        const auto bus=lin?signal::Bus::Lin:signal::Bus::Can;
        auto *vm=channel->signalTransmission();
        QString error;
        const auto fixture=QString(TEST_SOURCE_DIR)+(lin?"/fixtures/signals-synthetic.ldf":"/fixtures/signals-synthetic.dbc");
        QVERIFY2(vm->importFile(fixture,error),qPrintable(error));
        const quint32 knownId=lin?0x10:0x123;
        const quint32 customId=lin?0x2a:0x55a;
        const quint32 unknownId=lin?0x2b:0x1fffffff;
        QString expectedDb,extendedName;
        for(const auto &definition:vm->database()->frames)if(definition.id==knownId){
            if(definition.extended)extendedName=definition.name;else expectedDb=definition.name;
        }
        QVERIFY(!expectedDb.isEmpty());
        QVERIFY2(vm->putCustom({},"CustomOnly",customId,2,100,error),qPrintable(error));
        if(!lin)QVERIFY2(vm->putCustom({},"CustomWithDatabaseId",knownId,2,100,error,true),qPrintable(error));
        auto known=record(1,knownId,"Tx");known.echo=true;known.bus=bus;
        known.length=lin?2:8;known.payload=QByteArray(known.length,char(3));
        auto custom=record(2,customId);custom.bus=bus;
        auto unknown=record(3,unknownId);unknown.bus=bus;unknown.extended=!lin;
        auto wrongType=known;wrongType.captureUs=4;wrongType.extended=!lin;wrongType.bus=lin?signal::Bus::Can:bus;
        channel->frames()->append({known,custom,unknown,wrongType});
        const auto name=[&](int row){return channel->frames()->index(row,nameColumn).data().toString();};
        QCOMPARE(name(0),expectedDb);
        QCOMPARE(name(1),QString("CustomOnly"));
        QVERIFY(name(2).isEmpty());
        QCOMPARE(name(3),extendedName);
        QVERIFY(table->fontMetrics().horizontalAdvance(unknown.identifier)+8<=header->sectionSize(5));
        QVERIFY(header->sectionSize(3)<120&&header->sectionSize(4)<100&&header->sectionSize(5)<150&&header->sectionSize(6)<90);
        const auto key=vm->working().customFrames.first().key;
        QVERIFY2(vm->putCustom(key,"RenamedCustom",customId,2,100,error),qPrintable(error));
        QCOMPARE(name(1),QString("RenamedCustom"));
        channel->frames()->setRolling(true);
        QCOMPARE(name(0),expectedDb);
        QCOMPARE(name(1),QString("RenamedCustom"));
        const auto child=channel->frames()->index(0,0);
        QVERIFY(channel->frames()->rowCount(child)>1);
        for(int c=5;c<=8;++c)QVERIFY(!channel->frames()->index(0,c,child).data().toString().isEmpty());
        QVERIFY(!channel->frames()->index(0,nameColumn,child).data().isValid());
        QString renamedKey;
        for(const auto &frame:vm->working().customFrames)if(frame.id==customId)renamedKey=frame.key;
        QVERIFY(!renamedKey.isEmpty());
        QVERIFY2(vm->removeCustom(renamedKey,error),qPrintable(error));
        QVERIFY(name(1).isEmpty());
        table->setColumnWidth(5,111);
        channel->frames()->append({known});
        QCOMPARE(table->columnWidth(5),111);
    }
    void monitorNameFilterAndSort_data(){monitorNameColumn_data();}
    void monitorNameFilterAndSort(){
        QFETCH(bool,lin);
        auto config=ChannelSettings::defaults(lin?communication::Bus::Lin:communication::Bus::Can);
        config.simulation=true;
        ChannelViewModel channel(config);
        ChannelPage page(&channel);
        page.resize(1440,1000);
        page.show();
        auto *table=page.findChild<QTreeView*>("frameTable");
        QVERIFY(table);
        auto *header=qobject_cast<FrameHeaderView*>(table->header());
        auto *proxy=qobject_cast<FrameFilterProxy*>(table->model());
        QVERIFY(header&&proxy);
        const int column=FrameTableModel::NameColumn;
        const auto bus=lin?signal::Bus::Lin:signal::Bus::Can;
        const auto fixture=QString(TEST_SOURCE_DIR)+(lin?"/fixtures/signals-synthetic.ldf":"/fixtures/signals-synthetic.dbc");
        auto *signalVm=channel.signalTransmission();
        QString error;
        QVERIFY2(signalVm->importFile(fixture,error),qPrintable(error));
        const quint32 knownId=lin?0x10:0x123;
        const quint32 alphaId=lin?0x2a:0x55a;
        const quint32 zuluId=lin?0x2b:0x55b;
        const quint32 blankId=lin?0x2c:0x55c;
        QVERIFY2(signalVm->putCustom({},"AlphaLocal",alphaId,2,100,error),qPrintable(error));
        QVERIFY2(signalVm->putCustom({},"ZuluLocal",zuluId,2,100,error),qPrintable(error));
        auto localFrame=[&](qint64 time,quint32 id){
            auto frame=record(time,id);
            frame.bus=bus;
            return frame;
        };
        channel.frames()->append({localFrame(1,zuluId),localFrame(2,blankId),
                                  localFrame(3,knownId),localFrame(4,alphaId)});
        auto displayedNames=[&]{
            QStringList result;
            for(int row=0;row<proxy->rowCount();++row)
                result.append(proxy->index(row,column).data().toString());
            return result;
        };
        QCOMPARE(proxy->rowCount(),4);
        QVERIFY(displayedNames().contains(QString()));
        QCoreApplication::processEvents();
        const QPoint caption(header->sectionViewportPosition(column)+8,header->height()/2);
        QTest::mouseClick(header->viewport(),Qt::LeftButton,Qt::NoModifier,caption);
        QCOMPARE(header->sortIndicatorSection(),column);
        const auto firstSort=header->sortIndicatorOrder();
        const auto firstNames=displayedNames();
        QVERIFY((firstNames.indexOf("AlphaLocal")<firstNames.indexOf("ZuluLocal"))==(firstSort==Qt::AscendingOrder));
        QTest::mouseClick(header->viewport(),Qt::LeftButton,Qt::NoModifier,caption);
        QCOMPARE(header->sortIndicatorSection(),column);
        QVERIFY(header->sortIndicatorOrder()!=firstSort);
        const auto secondNames=displayedNames();
        QVERIFY((secondNames.indexOf("AlphaLocal")<secondNames.indexOf("ZuluLocal"))!=(firstSort==Qt::AscendingOrder));

        const QPoint button=header->filterButtonRect(column).center();
        QMouseEvent hover(QEvent::MouseMove,QPointF(button),QPointF(header->viewport()->mapToGlobal(button)),
                          Qt::NoButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(header->viewport(),&hover);
        QVERIFY(header->filterButtonVisible(column));

        // Drive the asynchronous menu that opens from the real header button.
        bool confirmed=false;
        bool clearing=false;
        bool cleared=false;
        bool sawBlank=false;
        bool sawAlpha=false;
        QElapsedTimer deadline;
        deadline.start();
        QTimer menuInput;
        menuInput.setInterval(10);
        connect(&menuInput,&QTimer::timeout,&page,[&]{
            auto *menu=page.findChild<FrameColumnMenu*>("frameColumnMenu");
            if(!menu)return;
            auto *buttons=menu->findChild<QDialogButtonBox*>("frameColumnButtons");
            if(deadline.elapsed()>3000){menu->close();return;}
            if(clearing){
                auto *action=menu->findChild<QAction*>("clearColumnFilter");
                if(action&&action->isEnabled()){
                    cleared=true;
                    QTest::mouseClick(menu,Qt::LeftButton,Qt::NoModifier,menu->actionGeometry(action).center());
                }
                return;
            }
            if(!buttons->button(QDialogButtonBox::Ok)->isEnabled())return;
            auto *list=menu->findChild<QListView*>("frameColumnValues");
            for(int row=0;row<list->model()->rowCount();++row){
                const auto value=list->model()->index(row,0).data(Qt::UserRole).toString();
                sawBlank|=value.isEmpty();
                sawAlpha|=value=="AlphaLocal";
            }
            menu->findChild<QLineEdit*>("frameColumnSearch")->setText("alpha");
            confirmed=true;
            buttons->button(QDialogButtonBox::Ok)->click();
        });
        menuInput.start();
        QTest::mouseClick(header->viewport(),Qt::LeftButton,Qt::NoModifier,button);
        menuInput.stop();
        QVERIFY(confirmed&&sawBlank&&sawAlpha);
        QCOMPARE(displayedNames(),QStringList{"AlphaLocal"});
        QCOMPARE(header->sortIndicatorSection(),column);
        QCOMPARE(proxy->filteredHistory().size(),1);
        QCOMPARE(proxy->filteredHistory().first().id,alphaId);

        QTemporaryDir exportDir;
        QVERIFY(exportDir.isValid());
        const auto exportPath=exportDir.filePath("filtered.csv");
        auto *exportButton=page.findChild<QPushButton*>("exportFrames");
        QVERIFY(exportButton);
        const bool nativeDisabled=QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,true);
        QTimer exportInput;
        exportInput.setInterval(15);
        deadline.restart();
        connect(&exportInput,&QTimer::timeout,&page,[&]{
            if(auto *input=page.findChild<QInputDialog*>();input&&input->isVisible()){
                if(deadline.elapsed()>3000){input->reject();return;}
                if(auto *scope=input->findChild<QComboBox*>()){
                    scope->setCurrentIndex(1);
                    input->accept();
                }
            }else if(auto *file=page.findChild<QFileDialog*>();file&&file->isVisible()){
                if(deadline.elapsed()>3000){file->reject();return;}
                file->selectNameFilter("CSV (*.csv)");
                if(auto *name=file->findChild<QLineEdit*>("fileNameEdit")){
                    name->setText(exportPath);
                    QMetaObject::invokeMethod(file,"accept");
                }
            }
        });
        exportInput.start();
        exportButton->click();
        exportInput.stop();
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,nativeDisabled);
        QFile file(exportPath);
        QVERIFY2(file.open(QIODevice::ReadOnly),qPrintable(channel.logs().join('\n')));
        const auto csv=file.readAll();
        QCOMPARE(csv.count('\n'),2);
        QVERIFY(csv.contains(("0x"+QByteArray::number(alphaId,16).toUpper()).constData()));

        auto candidates=proxy->candidateValues(column);
        const auto key=signal::frameKey(bus,alphaId);
        QVERIFY2(signalVm->putCustom(key,"ChangedLocal",alphaId,2,100,error),qPrintable(error));
        QCOMPARE(proxy->rowCount(),0);
        QTRY_VERIFY(candidates.isFinished());
        bool keptOldName=false;
        for(const auto &candidate:candidates.result())
            keptOldName|=candidate.first=="AlphaLocal";
        QVERIFY(keptOldName);
        clearing=true;
        deadline.restart();
        menuInput.start();
        QTest::mouseClick(header->viewport(),Qt::LeftButton,Qt::NoModifier,button);
        menuInput.stop();
        QVERIFY(cleared);
        QCOMPARE(proxy->rowCount(),4);
        QVERIFY(displayedNames().contains("ChangedLocal"));

        QVERIFY(proxy->setColumnFilter(column,rule(FrameFilterOp::Contains,"CHANGED")));
        QCOMPARE(displayedNames(),QStringList{"ChangedLocal"});
        channel.frames()->setRolling(true);
        channel.frames()->append({localFrame(5,blankId),localFrame(6,alphaId)});
        QCOMPARE(displayedNames(),QStringList{"ChangedLocal"});
        QCOMPARE(proxy->filteredHistory().size(),2);

        ChannelViewModel reopened(channel.snapshotSettings());
        ChannelPage reopenedPage(&reopened);
        reopened.frames()->append(channel.frames()->history());
        auto *reopenedTable=reopenedPage.findChild<QTreeView*>("frameTable");
        auto *reopenedProxy=qobject_cast<FrameFilterProxy*>(reopenedTable->model());
        QVERIFY(reopenedProxy);
        QTRY_COMPARE(reopenedTable->header()->sortIndicatorSection(),column);
        QCOMPARE(reopenedTable->header()->sortIndicatorOrder(),header->sortIndicatorOrder());
        QVERIFY(reopenedProxy->columnActive(column));
        QCOMPARE(reopenedProxy->filteredHistory().size(),2);
        QVERIFY(reopenedProxy->setColumnFilter(column,rule(FrameFilterOp::Equal,QString())));
        QCOMPARE(reopenedProxy->filteredHistory().size(),2);
    }
    void oldMonitorColumnsKeepLogicalNumbers(){
        const QJsonArray lengthFilter{QJsonObject{{"column",6},{"valuesEnabled",true},{"values",QJsonArray{"2"}}}};
        const auto legacy=QJsonObject{{"columnFilters",lengthFilter},{"sortColumn",8},{"sortOrder",1}};
        signal::Bus bus=signal::Bus::Can;
        SignalTransmitViewModel old(bus);
        old.setUiSettings(QJsonObject{{"monitor",legacy}});
        auto configuration=old.configuration();
        for(int attempt=0;attempt<2;++attempt){
            ChannelViewModel vm(settings());
            ChannelPage page(&vm);
            QString error;
            QVERIFY2(vm.signalTransmission()->readConfiguration(configuration,{},error),qPrintable(error));
            auto *table=page.findChild<QTreeView*>("frameTable");
            auto *proxy=qobject_cast<FrameFilterProxy*>(table->model());QVERIFY(proxy);
            QCOMPARE(proxy->saveFilters().at(0).toObject().value("column").toInt(),6);
            QCOMPARE(table->header()->sortIndicatorSection(),8);
            QCOMPARE(table->header()->sortIndicatorOrder(),Qt::DescendingOrder);
            auto larger=record(2,0x124);larger.length=3;
            vm.frames()->append({record(1,0x123,"TxPending"),larger});
            QCOMPARE(proxy->rowCount(),1);
            QVERIFY(proxy->index(0,8).data(Qt::ToolTipRole).toString().contains("CAN ISO-TP"));
            const auto stored=QJsonDocument(vm.signalTransmission()->configuration()).toJson();
            configuration=QJsonDocument::fromJson(stored).object();
        }
        auto empty=legacy;empty["columnFilters"]=QJsonArray();
        old.setUiSettings(QJsonObject{{"monitor",empty}});
        ChannelViewModel vm(settings());ChannelPage page(&vm);
        QString error;QVERIFY(vm.signalTransmission()->readConfiguration(old.configuration(),{},error));
        auto *proxy=qobject_cast<FrameFilterProxy*>(page.findChild<QTreeView*>("frameTable")->model());
        QVERIFY(proxy&&proxy->saveFilters().isEmpty());
        vm.frames()->append({record(1,0x123,"TxPending")});
        QCOMPARE(proxy->rowCount(),1);
    }
    void nativeFrameStripesCoverTreeIndentation_data(){
        monitorNameColumn_data();
    }
    void nativeFrameStripesCoverTreeIndentation(){
        QFETCH(bool,lin);
        auto s=ChannelSettings::defaults(lin?communication::Bus::Lin:communication::Bus::Can);
        s.simulation=true;
        ChannelViewModel vm(s);
        ChannelPage page(&vm);
        page.resize(1440,1000);page.show();
        auto *tree=page.findChild<QTreeView*>("frameTable");
        auto *rolling=page.findChild<QCheckBox*>("frameRolling");
        QVERIFY(tree&&rolling);
        tree->setSelectionMode(QAbstractItemView::NoSelection);
        tree->setFocusPolicy(Qt::NoFocus);
        QPalette palette=tree->palette();
        palette.setColor(QPalette::Base,Qt::white);
        palette.setColor(QPalette::AlternateBase,QColor(230,230,230));
        tree->setPalette(palette);
        const auto bus=lin?signal::Bus::Lin:signal::Bus::Can;
        const auto fixture=QString(TEST_SOURCE_DIR)+(lin?"/fixtures/signals-synthetic.ldf":"/fixtures/signals-synthetic.dbc");
        QString error;QVERIFY2(vm.signalTransmission()->importFile(fixture,error),qPrintable(error));
        const quint32 id=lin?0x10:0x123;
        auto tx=record(1,id,"Tx");tx.bus=bus;tx.echo=true;tx.length=lin?2:8;tx.payload=QByteArray(tx.length,0);
        auto hidden=tx;hidden.captureUs=2;hidden.echo=false;hidden.request=true;
        auto repeated=tx;repeated.captureUs=3;
        auto rx=record(4,id+1);rx.bus=bus;
        auto next=record(5,id+2);next.bus=bus;
        vm.frames()->append({tx,hidden,repeated,rx,next});
        auto *model=tree->model();
        auto verify=[&](QList<QColor> &colors,bool ordered=true){
            colors.clear();
            const auto image=tree->viewport()->grab().toImage();
            QColor previous;
            for(int n=0;n<model->rowCount();++n){
                const auto row=tree->visualRect(model->index(n,1));
                const int y=row.top()+3;
                const auto shade=image.pixelColor(row.right()-4,y);
                QCOMPARE(image.pixelColor(2,y),shade);
                QCOMPARE(image.pixelColor(tree->visualRect(model->index(n,7)).right()-4,y),shade);
                QVERIFY(shade==Qt::white||shade==QColor(230,230,230));
                if(n&&ordered)QVERIFY(shade!=previous);
                previous=shade;
                colors.append(shade);
            }
        };
        QTRY_COMPARE(model->rowCount(),4);
        QCOMPARE(tree->treePosition(),1);
        QList<QColor> historyColors,rollingColors,updatedColors;
        verify(historyColors);
        QTest::mouseClick(rolling,Qt::LeftButton);
        QTRY_COMPARE(model->rowCount(),3);
        QVERIFY(model->rowCount(model->index(0,0))>1);
        verify(rollingColors);
        QCOMPARE(rollingColors.size(),3);
        QSignalSpy resets(vm.frames(),&QAbstractItemModel::modelReset);
        repeated.captureUs=6;
        repeated.payload[0]=char(0x06);
        repeated.data=QString::fromLatin1(repeated.payload.toHex(' ')).toUpper();
        vm.frames()->append({repeated});
        QTRY_COMPARE(model->index(0,7).data().toString(),repeated.data);
        QCOMPARE(model->rowCount(),3);
        QCOMPARE(resets.count(),0);
        verify(updatedColors);
        QCOMPARE(updatedColors,rollingColors);
        tree->sortByColumn(5,Qt::AscendingOrder);
        auto earlier=record(7,id-1);earlier.bus=bus;
        auto later=record(8,id+3);later.bus=bus;
        vm.frames()->append({earlier,later});
        QTRY_COMPARE(model->rowCount(),5);
        verify(updatedColors,false);
        QCOMPARE(updatedColors.mid(1,3),rollingColors);
        QVERIFY(updatedColors.first()!=rollingColors.last());
        QCOMPARE(updatedColors.last(),rollingColors.last());
        tree->sortByColumn(5,Qt::DescendingOrder);
        QList<QColor> afterSort;
        verify(afterSort,false);
        std::reverse(updatedColors.begin(),updatedColors.end());
        QCOMPARE(afterSort,updatedColors);
        QCOMPARE(resets.count(),0);
        QTest::mouseClick(rolling,Qt::LeftButton);
        QTRY_COMPARE(model->rowCount(),7);
        verify(historyColors);
        QCOMPARE(historyColors.size(),7);
    }
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
        bar->setSliderDown(false);vm.frames()->append({record(20001,0x795)});QTRY_COMPARE(bar->value(),bar->maximum());
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
        vm.frames()->append({record(20002000,0x795)});QTRY_COMPARE(bar->value(),bar->maximum());
    }
    void peakStatusDecodingIsExplicitAndVendorScoped(){
        auto hardware=createCanHardware();
        QCOMPARE(hardware->classifyStatus(PCAN_ERROR_OK),CanBusState::Ready);
        QCOMPARE(hardware->classifyStatus(PCAN_ERROR_QRCVEMPTY),CanBusState::Ready);
        QCOMPARE(hardware->classifyStatus(PCAN_ERROR_BUSHEAVY),CanBusState::Warning);
        QCOMPARE(hardware->classifyStatus(PCAN_ERROR_BUSPASSIVE),CanBusState::Warning);
        QCOMPARE(hardware->classifyStatus(PCAN_ERROR_BUSOFF),CanBusState::BusOff);
        QCOMPARE(hardware->classifyStatus(PCAN_ERROR_BUSOFF|PCAN_ERROR_QXMTFULL),CanBusState::BusOff);
        QCOMPARE(hardware->classifyStatus(PCAN_ERROR_BUSOFF|PCAN_ERROR_ILLHW),CanBusState::Fatal);
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
    void rollingTimeOrderStaysStable_data(){
        QTest::addColumn<bool>("lin");QTest::addColumn<int>("column");QTest::addColumn<bool>("descending");
        for(bool lin:{false,true})for(int column=0;column<3;++column)for(bool descending:{false,true})
            QTest::newRow(qPrintable(QString("%1-%2-%3").arg(lin?"LIN":"CAN").arg(column).arg(descending)))<<lin<<column<<descending;
    }
    void rollingTimeOrderStaysStable(){
        QFETCH(bool,lin);QFETCH(int,column);QFETCH(bool,descending);
        FrameTableModel model;model.setRolling(true);FrameFilterProxy proxy;proxy.setSourceModel(&model);
        auto a=record(10,0x10),b=record(20,0x20);a.bus=b.bus=lin?signal::Bus::Lin:signal::Bus::Can;
        model.append({a,b});proxy.sort(column,descending?Qt::DescendingOrder:Qt::AscendingOrder);
        auto ids=[&]{QStringList values;for(int n=0;n<proxy.rowCount();++n)values<<proxy.index(n,5).data().toString();return values;};
        const auto original=ids();
        for(int n=0;n<5;++n){a.captureUs=100+n*200;b.captureUs=200+n*200;model.append({a});QCOMPARE(ids(),original);model.append({b});QCOMPARE(ids(),original);}
        QCOMPARE(model.historyCount(),qint64(12));QCOMPARE(model.frameAt(model.index(0,0))->captureUs,qint64(900));
        proxy.sort(5,Qt::DescendingOrder);QCOMPARE(proxy.index(0,5).data().toString(),QString("0x20"));
        model.setPaused(true);QCOMPARE(proxy.rowCount(),12);model.setPaused(false);QCOMPARE(proxy.rowCount(),2);
        model.clear();b.captureUs=10;a.captureUs=20;model.append({b,a});proxy.sort(0,Qt::AscendingOrder);QCOMPARE(proxy.index(0,5).data().toString(),QString("0x20"));
    }
    void rollingTimeOrderSurvivesWindowRebuild(){
        FrameTableModel model;model.setRolling(true);FrameFilterProxy proxy;proxy.setSourceModel(&model);proxy.sort(0,Qt::AscendingOrder);
        auto a=record(1,0x10),b=record(2,0x20);model.append({a,b});FrameBatch fill;
        for(int n=0;n<FrameTableModel::Capacity-2;++n)fill.append(record(n+3,quint32(n+0x100)));
        model.append(fill);b.captureUs=30000;a.captureUs=30001;model.append({b,a});model.append({record(30002,0x10000)});
        QCOMPARE(proxy.index(0,5).data().toString(),QString("0x10"));QCOMPARE(proxy.index(1,5).data().toString(),QString("0x20"));
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
    void rollingRowsKeepVisibleOrderWhenOtherFramesAreHidden(){
        FrameTableModel model;model.setRolling(true);FrameFilterProxy proxy;proxy.setSourceModel(&model);
        auto first=record(1,0x100),hidden=record(2,0x200),second=record(3,0x300);
        model.append({first,hidden,second});QVERIFY(proxy.setColumnFilter(5,rule(FrameFilterOp::NotEqual,"200")));
        proxy.sort(0,Qt::AscendingOrder);QCOMPARE(proxy.rowCount(),2);
        auto firstId=[&]{return proxy.index(0,5).data().toString();};
        QCOMPARE(firstId(),QString("0x100"));
        hidden.captureUs=10;model.append({hidden});QCOMPARE(proxy.rowCount(),2);QCOMPARE(firstId(),QString("0x100"));
        first.captureUs=11;model.append({first});QCOMPARE(proxy.rowCount(),2);QCOMPARE(firstId(),QString("0x100"));
        proxy.sort(5,Qt::DescendingOrder);QCOMPARE(firstId(),QString("0x300"));
        model.setRolling(false);QVERIFY(proxy.setColumnFilter(5,{}));QCOMPARE(proxy.rowCount(),5);
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
        FrameTableModel model;
        auto pending=record(0,1,"CAN Tx");pending.request=true;
        auto echo=record(1,2,"TX ECHO");echo.echo=true;
        auto sim=record(2,3,"SIM TX");sim.simulated=true;
        auto rx=record(3,4,"RX");auto absent=rx;absent.noResponse=true;auto error=rx;error.error=true;auto hardware=rx;hardware.hardwareStatus=true;
        model.append({pending,echo,sim,rx,absent,error,hardware});
        QCOMPARE(model.index(0,4).data().toString(),QString("TxPending"));QCOMPARE(model.index(0,8).data().toString(),QString("待确认"));
        QCOMPARE(model.index(1,4).data().toString(),QString("Tx"));QCOMPARE(model.index(2,4).data().toString(),QString("Tx"));QCOMPARE(model.index(3,4).data().toString(),QString("Rx"));
        QCOMPARE(model.index(4,8).data().toString(),QString("无响应"));QCOMPARE(model.index(5,8).data().toString(),QString("错误"));QCOMPARE(model.index(6,8).data().toString(),QString("硬件状态"));
        QVERIFY(model.index(2,8).data(Qt::ToolTipRole).toString().contains("CAN ISO-TP"));QVERIFY(model.index(2,8).data(Qt::ToolTipRole).toString().contains("模拟"));
        FrameFilterProxy proxy;proxy.setSourceModel(&model);
        QCOMPARE(proxy.rowCount(),6);QCOMPARE(proxy.filteredHistory().size(),6);QVERIFY(model.history()[0].request);QVERIFY(model.history()[1].echo);
        QVERIFY(proxy.setColumnFilter(4,{}));QCOMPARE(proxy.rowCount(),7);
        model.append(model.history().mid(0,1));QCOMPARE(model.index(7,4).data().toString(),QString("TxPending"));QCOMPARE(model.history()[7].detail,QString("CAN ISO-TP"));
    }
    void hiddenRequestsKeepTxStripesAndScroll_data(){
        QTest::addColumn<bool>("customFilter");
        QTest::newRow("default directions")<<false;
        QTest::newRow("ID and direction filter")<<true;
    }
    void hiddenRequestsKeepTxStripesAndScroll(){
        QFETCH(bool,customFilter);
        ChannelViewModel vm(settings());ChannelPage page(&vm);page.resize(1200,760);page.show();
        QString error;
        QVERIFY(vm.signalTransmission()->importFile(QString(TEST_SOURCE_DIR)+"/fixtures/signals-synthetic.dbc",error));
        auto *tree=page.findChild<QTreeView*>("frameTable");
        auto *proxy=qobject_cast<FrameFilterProxy*>(tree->model());QVERIFY(proxy);
        if(customFilter){
            FrameColumnFilter filter;filter.valuesEnabled=true;filter.values={QString::number(0x123)};
            QVERIFY(proxy->setColumnFilter(5,filter));
        }
        EchoCanHardware hw;SignalTransmitter transmitter;
        signal::TxPlan plan;plan.bus=signal::Bus::Can;plan.run=1;plan.periodic=true;
        plan.items={{signal::frameKey(plan.bus,0x123),0x123,false,QByteArray::fromHex("0321000000000000"),200,1,{},false,true}};
        transmitter.start(plan,500000,false,&hw,nullptr);
        QTRY_VERIFY(hw.writes>0);TPCANMsg raw={};QCOMPARE(hw.recvRaw(1,&raw,nullptr),DWORD(1));
        transmitter.stop();QVERIFY(raw.MSGTYPE&PCAN_MESSAGE_ECHO);
        auto echo=record(0,raw.ID,"TX ECHO");echo.echo=true;echo.length=raw.LEN;
        echo.payload=QByteArray(reinterpret_cast<const char*>(raw.DATA),raw.LEN);
        echo.data=echo.payload.toHex(' ');auto pending=echo;pending.echo=false;pending.request=true;pending.direction="TX";
        FrameBatch traffic;
        for(int n=0;n<100;++n){
            pending.captureUs=200000LL*n;echo.captureUs=pending.captureUs+100;
            traffic.append(pending);traffic.append(echo);
        }
        vm.frames()->append(traffic);QCOMPARE(proxy->rowCount(),100);
        auto *bar=tree->verticalScrollBar();QTRY_VERIFY(bar->maximum()>10);
        QTRY_COMPARE(bar->value(),bar->maximum());bar->setValue(0);QTest::qWait(120);
        const auto anchor=bar->value();QCOMPARE(anchor,0);
        auto colors=[&](){
            const QImage image=tree->viewport()->grab().toImage();QList<QColor> result;
            for(int n=0;n<3;++n){
                const auto rect=tree->visualRect(proxy->index(n,1));
                result.append(image.pixelColor(rect.right()-4,rect.center().y()));
            }
            return result;
        };
        const auto before=colors();QCOMPARE(before.at(0),before.at(2));QVERIFY(before.at(0)!=before.at(1));
        auto *timer=page.findChild<QTimer*>("frameRefreshTimer");QVERIFY(timer);
        QSignalSpy follow(timer,&QTimer::timeout);
        pending.captureUs=20000000;vm.frames()->append({pending});QTest::qWait(100);
        QCOMPARE(proxy->rowCount(),100);QCOMPARE(bar->value(),anchor);QCOMPARE(colors(),before);QCOMPARE(follow.count(),0);
        FrameBatch hidden(FrameTableModel::Capacity+1,pending);vm.frames()->append(hidden);QTest::qWait(100);
        QCOMPARE(proxy->rowCount(),100);QCOMPARE(bar->value(),anchor);QCOMPARE(colors(),before);QCOMPARE(follow.count(),0);
        echo.captureUs=pending.captureUs+100;vm.frames()->append({echo});
        QTRY_COMPARE(bar->value(),bar->maximum());QCOMPARE(proxy->rowCount(),101);QCOMPARE(vm.frames()->historyCount(),qint64(202+hidden.size()));
        page.findChild<QCheckBox*>("frameRolling")->setChecked(true);
        QCOMPARE(proxy->rowCount(),1);
        const auto parent=proxy->index(0,0);tree->expand(parent);QVERIFY(tree->isExpanded(parent));
        vm.frames()->append({pending});QCOMPARE(proxy->rowCount(),1);QVERIFY(tree->isExpanded(parent));
        vm.frames()->append({echo});QVERIFY(tree->isExpanded(parent));
        QCOMPARE(proxy->index(1,6,parent).data().toString(),QString("3"));
        QCOMPARE(proxy->index(2,6,parent).data().toString(),QString("33"));
    }

    void confirmedFramesKeepPresentation_data(){
        QTest::addColumn<bool>("lin");
        QTest::addColumn<bool>("rolling");
        QTest::newRow("CAN expanded echo")<<false<<true;
        QTest::newRow("LIN expanded echo")<<true<<true;
        QTest::newRow("CAN history stripes")<<false<<false;
        QTest::newRow("LIN history stripes")<<true<<false;
    }
    void confirmedFramesKeepPresentation(){
        QFETCH(bool,lin);QFETCH(bool,rolling);
        const auto bus=lin?signal::Bus::Lin:signal::Bus::Can;
        const auto fixture=QString(TEST_SOURCE_DIR)+(lin?"/fixtures/signals-synthetic.ldf":"/fixtures/signals-synthetic.dbc");
        const auto imported=signal::DatabaseImporter::load(fixture,bus);
        QVERIFY2(imported.database,qPrintable(imported.error));
        FrameTableModel model;model.setDatabase(imported.database);model.setRolling(rolling);
        FrameFilterProxy proxy;proxy.setSourceModel(&model);
        FrameTableView view;view.setModel(&proxy);
        auto pending=record(0,lin?0x10:0x123,"TX");
        pending.bus=bus;pending.request=true;pending.length=lin?2:8;
        pending.payload=QByteArray(pending.length,0);pending.payload[0]=3;
        auto echo=pending;echo.request=false;echo.echo=true;
        if(!lin){
            EchoCanHardware hardware;SignalTransmitter tx;signal::BusFrameEvents submitted;
            connect(&tx,&SignalTransmitter::events,this,[&](const signal::BusFrameEvents &batch){submitted+=batch;});
            signal::TxPlan plan;plan.bus=bus;plan.run=1;plan.periodic=true;
            plan.items={{signal::frameKey(bus,pending.id),pending.id,false,pending.payload,200,1,{},false,true}};
            tx.start(plan,500000,false,&hardware,nullptr);
            QTRY_VERIFY(!submitted.isEmpty());QCOMPARE(submitted.first().source,signal::EventSource::RequestAccepted);
            TPCANMsg message={};QCOMPARE(hardware.recvRaw(1,&message,nullptr),DWORD(1));
            echo.echo=bool(message.MSGTYPE&PCAN_MESSAGE_ECHO);QVERIFY(echo.echo);
            echo.id=message.ID;echo.payload=QByteArray(reinterpret_cast<const char*>(message.DATA),message.LEN);
            QCOMPARE(echo.payload,pending.payload);tx.stop();
        }
        model.append({pending,echo});
        QCOMPARE(proxy.rowCount(),1);
        const auto first=proxy.index(0,0);
        if(rolling){
            view.expand(first);QVERIFY(view.isExpanded(first));
            QCOMPARE(proxy.index(1,6,first).data().toString(),QString("3"));
        }
        pending.captureUs=200000;model.append({pending});
        if(rolling){QCOMPARE(proxy.rowCount(),1);QVERIFY(view.isExpanded(proxy.index(0,0)));}
        echo.captureUs=200100;model.append({echo});
        QCOMPARE(model.historyCount(),qint64(4));QCOMPARE(proxy.rowCount(),rolling?1:2);
        if(rolling){
            auto rx=echo;rx.echo=false;rx.direction="Rx";rx.payload[0]=5;
            model.append({rx});QVERIFY(view.isExpanded(proxy.index(0,0)));
            QCOMPARE(proxy.index(0,4).data().toString(),QString("Rx"));
            QCOMPARE(proxy.index(1,6,proxy.index(0,0)).data().toString(),QString("5"));
            rx.id=0x3b;model.append({rx});QCOMPARE(proxy.rowCount(proxy.index(1,0)),0);
            FrameColumnFilter only;only.valuesEnabled=true;only.values={QString::number(pending.id)};
            QVERIFY(proxy.setColumnFilter(5,only));QCOMPARE(proxy.rowCount(),1);
            QCOMPARE(proxy.index(1,6,proxy.index(0,0)).data().toString(),QString("5"));
        }else{
            QCOMPARE(proxy.index(1,4).data().toString(),QString("Tx"));
        }
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
        QVERIFY(vm.sendDiagnostic(0,QByteArray::fromHex("1001"),error,true));QTRY_VERIFY(!vm.diagnosticBusy());QTRY_VERIFY(!vm.frames()->history().isEmpty());
        for(const auto&r:vm.frames()->history()){QVERIFY(r.direction=="Tx"||r.direction=="Rx");QCOMPARE(r.status,QString("正常"));QVERIFY(!r.detail.isEmpty());}
        vm.frames()->clear();auto*signalVm=vm.signalTransmission();QVERIFY(signalVm->putCustom({},"test",0x600,2,10,error));QVERIFY(signalVm->start(false,error));QTRY_VERIFY(!signalVm->running());QTRY_VERIFY(!vm.frames()->history().isEmpty());for(const auto&r:vm.frames()->history()){QCOMPARE(r.direction,QString("Tx"));QCOMPARE(r.status,QString("正常"));}
        vm.frames()->clear();QTemporaryDir dir;QFile image(dir.filePath("sample.bin"));QVERIFY(image.open(QIODevice::WriteOnly));image.write(QByteArray(128,char(0x5A)));image.close();auto download=vm.settings();download.applicationPath=image.fileName();QFile driver(dir.filePath("driver.bin"));QVERIFY(driver.open(QIODevice::WriteOnly));driver.write(QByteArray(128,char(0x44)));driver.close();download.flashPath=driver.fileName();download.downloadProfile["resetWaitMs"]=0;QVERIFY(vm.setSettings(download));QTRY_VERIFY2(vm.canStart(),qPrintable(vm.startHint()+" / "+vm.error()+" / "+vm.logs().join(";")));vm.start();QTRY_COMPARE_WITH_TIMEOUT(vm.taskState(),TaskState::Completed,8000);QTRY_VERIFY(!vm.frames()->history().isEmpty());
        for(const auto&r:vm.frames()->history()){QVERIFY(r.direction=="Tx"||r.direction=="Rx");QCOMPARE(r.status,QString("正常"));QVERIFY(!r.detail.isEmpty());}
    }
};
QTEST_MAIN(FrameColumnsTest)
#include "test_frame_columns.moc"
