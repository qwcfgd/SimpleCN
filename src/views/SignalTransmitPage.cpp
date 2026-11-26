#include "localization/Language.h"
#include "SignalTransmitPage.h"
#include "FixedCaptionButton.h"
#include "SignalPlotDialog.h"
#include "SignalValueDelegate.h"
#include "TransmitFrameDelegate.h"
#include "viewmodels/SignalTableModels.h"
#include <QBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QComboBox>
#include <QTreeView>
#include <QTableView>
#include <QTableWidget>
#include <QHeaderView>
#include <QSplitter>
#include <QToolButton>
#include <QSortFilterProxyModel>
#include <QFileDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include <QInputDialog>
#include <QTimer>
#include "TransmitRowDrag.h"
#include <QDrag>
#include <QDropEvent>
#include <QMimeData>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QApplication>
#include <QMessageBox>
#include <algorithm>
#include <QMenu>
#include <limits>
#include <QJsonArray>
namespace host {
using namespace signal;
static QLabel*plain(const QString&s){auto*l=new QLabel(s);l->setTextFormat(Qt::PlainText);l->setWordWrap(true);return l;}
static QPushButton*button(const QString&text,const char*name){auto*b=new QPushButton(text);b->setObjectName(name);return b;}
static void tableStyle(QTableView*t){t->setAlternatingRowColors(true);t->setSelectionBehavior(QAbstractItemView::SelectRows);t->setSelectionMode(QAbstractItemView::SingleSelection);t->verticalHeader()->hide();t->horizontalHeader()->setStretchLastSection(true);t->verticalHeader()->setDefaultSectionSize(27);}
namespace {
class LinScheduleComboBox final : public QComboBox {
public:
    QSize minimumSizeHint() const override {
        const QSize hint = QComboBox::minimumSizeHint();
        return QSize(2 * hint.width(), hint.height());
    }
};

const char *sourceMime="application/x-simplecn-source";
const char *planMime="application/x-simplecn-plan";
void beginDrag(QAbstractItemView *view,const char *format,Qt::DropAction action){
    QDrag drag(view);drag.setMimeData(new TransmitRowDrag(view,QLatin1String(format)));drag.exec(action);
}
class SourceTree final:public QTreeView{
public:
    static QStringList keysFor(QModelIndexList rows){
        auto path=[](QModelIndex i){QVector<int> p;while(i.isValid()){p.prepend(i.row());i=i.parent();}return p;};
        std::sort(rows.begin(),rows.end(),[&](const auto &a,const auto &b){return path(a)<path(b);});
        QStringList keys;
        for(const auto &row:rows){const auto key=row.data(Qt::UserRole).toString();if(!key.isEmpty())keys.append(key);}
        return keys;
    }
    QStringList selectedKeys()const{return keysFor(selectionModel()->selectedRows());}
protected:
    void startDrag(Qt::DropActions) override{if(!selectedKeys().isEmpty())beginDrag(this,sourceMime,Qt::CopyAction);}
};
class PlanTable final:public QTableView{
public:
    SourceTree *sourceTree=nullptr;
    std::function<void(const QStringList &)> add;
    std::function<void(QVector<int>,int)> move;
    std::function<void(QVector<int>)> remove;
    QVector<int> rows() const{
        QVector<int> result;for(const auto &index:selectionModel()->selectedRows())result.append(index.row());return result;
    }
protected:
    QItemSelectionModel::SelectionFlags selectionCommand(const QModelIndex &index,const QEvent *event=nullptr)const override{
        if(event&&(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseButtonRelease)
                &&static_cast<const QMouseEvent*>(event)->button()==Qt::RightButton
                &&index.isValid()&&selectionModel()->isRowSelected(index.row(),{}))return QItemSelectionModel::NoUpdate;
        return QTableView::selectionCommand(index,event);
    }
    void keyPressEvent(QKeyEvent *event)override{
        if(event->key()==Qt::Key_Delete&&event->modifiers()==Qt::NoModifier&&state()!=EditingState
                &&QApplication::focusWidget()==this){
            const auto selected=rows();
            if(!selected.isEmpty() && !event->isAutoRepeat())remove(selected);
            event->accept();
            return;
        }
        QTableView::keyPressEvent(event);
    }
    void startDrag(Qt::DropActions) override{if(!rows().isEmpty())beginDrag(this,planMime,Qt::MoveAction);}
    bool acceptSource(QDropEvent *e) const{
        const auto *drag=dynamic_cast<const TransmitRowDrag*>(e->mimeData());
        return drag&&drag->valid()&&((drag->origin==this&&drag->hasFormat(planMime))||
               (drag->origin==sourceTree&&drag->hasFormat(sourceMime)));
    }
    void dragEnterEvent(QDragEnterEvent *e) override{if(acceptSource(e))e->acceptProposedAction();}
    void dragMoveEvent(QDragMoveEvent *e) override{if(acceptSource(e))e->acceptProposedAction();}
    void dropEvent(QDropEvent *e) override{
        if(!acceptSource(e))return;
        const auto *drag=static_cast<const TransmitRowDrag*>(e->mimeData());
        if(drag->origin==sourceTree){
            QModelIndexList indices;for(const auto &i:drag->rows)indices.append(i);add(SourceTree::keysFor(indices));
        }
        else{
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
            const auto point=e->position().toPoint();
#else
            const auto point=e->pos();
#endif
            const auto index=indexAt(point);
            const int destination=index.isValid()?index.row()+(point.y()>visualRect(index).center().y()):model()->rowCount();
            QVector<int> selected;for(const auto &i:drag->rows)selected.append(i.row());move(selected,destination);
        }
        e->acceptProposedAction();
    }
};
}
SignalTransmitPage::SignalTransmitPage(SignalTransmitViewModel*vm,QWidget*parent):QWidget(parent),m_vm(vm){
    setObjectName("signalTransmitPage");setAttribute(Qt::WA_StyledBackground,true);auto*root=new QVBoxLayout(this);root->setContentsMargins(10,6,10,6);root->setSpacing(5);
    m_configurationDialog=new QDialog(this);m_configurationDialog->setObjectName("signalSettingsDialog");m_configurationDialog->setWindowTitle("通信配置");m_configurationDialog->resize(800,480);
    auto*configuration=new QVBoxLayout(m_configurationDialog);
    auto*heading=new QHBoxLayout;auto*title=plain("报文工作台");title->setObjectName("sectionTitle");title->setWordWrap(false);heading->addWidget(title);
    m_canPanel=new QWidget;auto*canOptions=new QHBoxLayout(m_canPanel);canOptions->setContentsMargins(0,0,0,0);
    m_canNode=new QComboBox;m_canNode->setObjectName("signalCanNode");m_canDirection=new QComboBox;m_canDirection->setObjectName("signalCanDirection");m_canDirection->addItems({"Tx","Rx","Tx/Rx"});
    canOptions->addWidget(plain("DBC 节点"));canOptions->addWidget(m_canNode);canOptions->addWidget(plain("方向"));canOptions->addWidget(m_canDirection);
    heading->addSpacing(16);heading->addWidget(m_canPanel);heading->addStretch();
    auto*settings=button("通信配置…","signalSettings");heading->addWidget(settings);
    auto*plot=button("图像…","signalPlot");heading->addWidget(plot);root->addLayout(heading);
    connect(plot,&QPushButton::clicked,this,[this]{if(!m_plotDialog)m_plotDialog=new SignalPlotDialog(m_vm,this);m_plotDialog->show();m_plotDialog->raise();m_plotDialog->activateWindow();});
    connect(settings,&QPushButton::clicked,this,[this]{m_configurationDialog->exec();});
    auto*files=new QHBoxLayout;m_path=new QLineEdit;m_path->setObjectName("signalDatabasePath");m_path->setReadOnly(true);m_path->setPlaceholderText(vm->database()->bus==Bus::Can?"导入只读 DBC，或新增自建 CAN 报文":"导入只读 LDF");
    m_import=button("导入…","signalImport");m_reload=button("重新加载","signalReload");files->addWidget(m_path,1);files->addWidget(m_import);files->addWidget(m_reload);configuration->addLayout(files);
    auto *replay=new QHBoxLayout;m_replayPath=new QLineEdit;m_replayPath->setObjectName("signalReplayPath");m_replayPath->setReadOnly(true);m_replayPath->setPlaceholderText("信号回放源文件（ASC / BLF）");
    m_replayImport=button("导入…","signalReplayImport");m_replayReset=button("重置","signalReplayReset");m_replayMap=button("通道映射…","signalReplayMapping");replay->addWidget(m_replayPath,1);replay->addWidget(m_replayImport);replay->addWidget(m_replayReset);replay->addWidget(m_replayMap);configuration->addLayout(replay);
    connect(m_replayImport,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(m_configurationDialog,Language::text("导入信号回放源"),m_replayPath->text(),Language::text("日志 (*.blf *.asc *.BLF *.ASC)"));if(!path.isEmpty())m_vm->importReplay(path);});
    connect(m_replayReset,&QPushButton::clicked,m_vm,&SignalTransmitViewModel::resetReplay);connect(m_replayMap,&QPushButton::clicked,this,&SignalTransmitPage::editReplayMapping);
    m_summary=plain("");m_summary->setObjectName("signalDatabaseSummary");configuration->addWidget(m_summary);
    m_databaseDiagnostics=new QPlainTextEdit;m_databaseDiagnostics->setObjectName("signalDatabaseDiagnostics");
    m_databaseDiagnostics->setReadOnly(true);m_databaseDiagnostics->setMaximumHeight(96);
    configuration->addWidget(m_databaseDiagnostics);
    m_rolePanel=new QWidget;auto*roles=new QHBoxLayout(m_rolePanel);roles->setContentsMargins(0,0,0,0);
    m_role=new QComboBox;m_role->setObjectName("signalLinRole");m_role->addItems({"主节点","从节点","观测节点"});m_node=new QComboBox;m_node->setObjectName("signalLinNode");m_schedule=new LinScheduleComboBox;m_schedule->setObjectName("signalLinSchedule");
    roles->addWidget(plain("节点类型"));roles->addWidget(m_role);roles->addWidget(plain("LIN 节点"));roles->addWidget(m_node);roles->addWidget(plain("调度表"));roles->addWidget(m_schedule,1);
    m_newSchedule=button("新建表","signalNewSchedule");
    m_removeSchedule=button("删除表","signalRemoveSchedule");
    roles->addWidget(m_newSchedule);roles->addWidget(m_removeSchedule);heading->insertWidget(2,m_rolePanel);
    connect(m_newSchedule,&QPushButton::clicked,this,[this]{QString why;if(!m_vm->createSchedule(why))feedback(why);});
    connect(m_removeSchedule,&QPushButton::clicked,this,[this]{
        const QString name=m_vm->working().schedule;
        if(QMessageBox::question(this,"删除表",QString("删除调度表 %1？").arg(name),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
        QString why;if(name==m_vm->working().schedule&&!m_vm->deleteSchedule(why))feedback(why);
    });
    m_canDirection->setToolTip("相对于所选节点；切换时替换数据库报文，保留自建报文");
    auto*split=new QSplitter(Qt::Horizontal);split->setObjectName("signalMainSplit");auto*browser=new QWidget;auto*bl=new QVBoxLayout(browser);bl->setContentsMargins(0,0,0,0);
    m_search=new QLineEdit;m_search->setObjectName("signalSearch");m_search->setPlaceholderText("浏览节点 / 搜索报文，不改变发送选择");bl->addWidget(m_search);
    auto *sourceTree=new SourceTree;m_tree=sourceTree;m_tree->setObjectName("signalTree");auto*treeModel=new SignalTreeModel(vm,this);auto*proxy=new QSortFilterProxyModel(this);proxy->setSourceModel(treeModel);proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);proxy->setFilterKeyColumn(-1);proxy->setRecursiveFilteringEnabled(true);m_tree->setModel(proxy);m_tree->header()->setStretchLastSection(false);m_tree->header()->setSectionResizeMode(0,QHeaderView::Stretch);m_tree->header()->setSectionResizeMode(1,QHeaderView::Fixed);m_tree->setColumnWidth(1,85);bl->addWidget(m_tree);split->addWidget(browser);
    auto *planTable=new PlanTable;m_plan=planTable;m_plan->setObjectName("signalPlanTable");tableStyle(m_plan);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setDragEnabled(true);m_plan->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_plan->setDragDropMode(QAbstractItemView::DragDrop);m_plan->setAcceptDrops(true);
    planTable->sourceTree=sourceTree;
    planTable->add=[this](const QStringList &keys){
        QScopedValueRollback<bool> adding(m_addingFromTree,true);
        QString why;if(!m_vm->addImported(keys,why))feedback(why);
    };
    planTable->move=[this](QVector<int> rows,int destination){
        QString why;if(!m_vm->moveRows(rows,destination,why)){feedback(why);return;}
        const int first=destination-int(std::count_if(rows.cbegin(),rows.cend(),[&](int row){return row<destination;}));
        const auto top=m_plan->model()->index(first,0);
        const auto bottom=m_plan->model()->index(first+rows.size()-1,m_plan->model()->columnCount()-1);
        m_plan->selectionModel()->setCurrentIndex(top,QItemSelectionModel::NoUpdate);
        m_plan->selectionModel()->select(QItemSelection(top,bottom),QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);
    };
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree,&QWidget::customContextMenuRequested,this,[this,sourceTree,planTable](const QPoint &p){
        const auto index=m_tree->indexAt(p);if(!index.isValid()||index.sibling(index.row(),0).data(Qt::UserRole).toString().isEmpty())return;
        if(!m_tree->selectionModel()->isSelected(index))m_tree->setCurrentIndex(index);
        QMenu menu(this);menu.setObjectName("signalSourceMenu");auto *add=menu.addAction("添加");add->setEnabled(m_vm->canEditList()&&!sourceTree->selectedKeys().isEmpty());
        const auto key=index.sibling(index.row(),0).data(Qt::UserRole).toString();
        const auto *frame=m_vm->frame(key);
        QAction *use=nullptr,*clear=nullptr;
        if(frame){
            const auto wire=frameKey(m_vm->database()->bus,frame->id,frame->extended);
            if(m_vm->database()->sharedCanIds.contains(wire)){
                use=menu.addAction("对此 CAN ID 使用此定义");use->setObjectName("selectCanDefinition");
                use->setCheckable(true);use->setChecked(m_vm->resolvedFrameKey(wire)==key);use->setEnabled(m_vm->canStructure());
                clear=menu.addAction("清除定义选择，仅显示原始报文");
                clear->setEnabled(m_vm->canStructure()&&!m_vm->resolvedFrameKey(wire).isEmpty());
            }
        }
        const auto *action=menu.exec(m_tree->viewport()->mapToGlobal(p));
        if(action==add)planTable->add(sourceTree->selectedKeys());
        else if(action&&(action==use||action==clear)){
            QString error;if(!m_vm->chooseCanDefinition(key,action==use,error))feedback(error);
            else selectFrame(key);
        }
    });
    m_plan->setItemDelegate(new TransmitFrameDelegate(m_plan));
    connect(m_plan,&QTableView::clicked,this,[this](const QModelIndex&index){
        if((index.data(TransmitTableRoles::PayloadLengthRole).isValid()||index.data(TransmitTableRoles::DlcMinimumRole).isValid())&&(index.flags()&Qt::ItemIsEditable))m_plan->edit(index);
    });
    if(vm->database()->bus==Bus::Can){m_canModel=new CanTxTableModel(vm,this);m_plan->setModel(m_canModel);connect(m_canModel,&CanTxTableModel::validation,this,&SignalTransmitPage::feedback);m_plan->setColumnWidth(0,45);m_plan->setColumnWidth(1,45);m_plan->setColumnWidth(2,150);m_plan->setColumnWidth(3,90);m_plan->setColumnWidth(6,45);m_plan->setColumnWidth(7,80);}
    else{m_linModel=new LinScheduleTableModel(vm,this);m_plan->setModel(m_linModel);connect(m_linModel,&LinScheduleTableModel::validation,this,&SignalTransmitPage::feedback);}
    split->addWidget(m_plan);split->setSizes({220,780});
    auto *analysisSplit=new QSplitter(Qt::Vertical);
    analysisSplit->setObjectName("signalAnalysisSplit");
    analysisSplit->setChildrenCollapsible(false);
    analysisSplit->addWidget(split);
    auto *analysis=new QWidget;
    auto *editor=new QVBoxLayout(analysis);
    editor->setContentsMargins(0,0,0,0);
    editor->setSpacing(4);
    auto *toggle=new QToolButton;
    toggle->setObjectName("signalAnalysisToggle");
    toggle->setText("信号解析");
    toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toggle->setArrowType(Qt::DownArrow);
    toggle->setCheckable(true);
    toggle->setChecked(true);
    auto *actions=new QHBoxLayout;
    actions->addWidget(toggle);
    actions->addStretch();
    m_count=new QSpinBox;m_count->setObjectName("signalRepeatCount");m_count->setRange(1,1000000);m_count->setValue(m_vm->working().repeatCount);m_count->setToolTip("发送次数");connect(m_count,qOverload<int>(&QSpinBox::valueChanged),m_vm,&SignalTransmitViewModel::setRepeatCount);
    const QStringList sendCaptions=m_canModel
        ?QStringList{"发送","周期发送","发送中...","开始接收","停止","多次回放","周期回放"}
        :QStringList{"调度","周期调度","调度中...","启动从节点响应","开始监听","多次回放","周期回放"};
    m_once=new FixedCaptionButton(sendCaptions.at(0),sendCaptions);m_once->setObjectName("signalSendOnce");
    m_start=new FixedCaptionButton(sendCaptions.at(1),sendCaptions);m_start->setObjectName("signalStart");
    actions->addWidget(m_count);actions->addWidget(m_once);actions->addWidget(m_start);
    m_values=new QTableView;m_values->setObjectName("signalValues");m_valueModel=new SignalValueTableModel(vm,this);m_values->setModel(m_valueModel);
    connect(m_values,&QTableView::clicked,this,[this](const QModelIndex &index){
        if((index.column()==2||index.column()==3)&&(index.flags()&Qt::ItemIsEditable))m_values->edit(index);
    });m_values->setItemDelegate(new SignalValueDelegate(m_values));tableStyle(m_values);m_values->setColumnWidth(0,150);m_values->setColumnWidth(1,130);m_values->setColumnWidth(2,130);m_values->setColumnWidth(3,145);editor->addWidget(m_values,1);
    editor->addLayout(actions);
    analysisSplit->addWidget(analysis);
    analysisSplit->setSizes({360,360});
    root->addWidget(analysisSplit,1);
    connect(toggle,&QToolButton::toggled,this,[this,analysis,analysisSplit,toggle,actions,upperShare=0.5](bool expanded) mutable{
        const auto sizes=analysisSplit->sizes();
        const int total=sizes[0]+sizes[1];
        toggle->setArrowType(expanded?Qt::DownArrow:Qt::UpArrow);
        if(!expanded){
            if(total>0)upperShare=double(sizes[0])/total;
            m_values->hide();
            analysis->setMaximumHeight(actions->sizeHint().height());
            return;
        }
        analysis->setMaximumHeight(QWIDGETSIZE_MAX);
        m_values->show();
        const int upper=qRound(total*upperShare);
        analysisSplit->setSizes({upper,total-upper});
    });
    m_back=button("回退：上一步","signalBack");m_restore=button("撤销：恢复配置基准","signalRestore");
    auto*history=new QHBoxLayout;history->addWidget(m_back);history->addWidget(m_restore);history->addStretch();configuration->addLayout(history);
    m_configurationFeedback=plain("");m_configurationFeedback->setObjectName("signalConfigurationFeedback");configuration->addWidget(m_configurationFeedback);configuration->addStretch();
    auto*close=new QDialogButtonBox(QDialogButtonBox::Ok);close->button(QDialogButtonBox::Ok)->setText("确认");configuration->addWidget(close);connect(close,&QDialogButtonBox::accepted,m_configurationDialog,&QDialog::accept);
    m_validation=plain("");m_validation->setObjectName("signalValidation");root->addWidget(m_validation);
    connect(m_search,&QLineEdit::textChanged,proxy,&QSortFilterProxyModel::setFilterFixedString);
    connect(m_tree->selectionModel(),&QItemSelectionModel::currentChanged,this,[this,proxy](const QModelIndex&i){const auto source=proxy->mapToSource(i.sibling(i.row(),0));const auto key=source.data(Qt::UserRole).toString();if(!key.isEmpty()){for(int row=0;row<m_plan->model()->rowCount();++row)if((m_canModel?m_canModel->key(row):m_linModel->key(row))==key){m_plan->selectRow(row);return;}
        if(m_vm->database()->requiresDbcCompatibility){m_plan->clearSelection();selectFrame(key);}
    }});
    connect(m_plan->selectionModel(),&QItemSelectionModel::currentRowChanged,this,[this](const QModelIndex&i){selectFrame(m_canModel?m_canModel->key(i.row()):m_linModel->key(i.row()));});
    connect(m_import,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(m_configurationDialog,Language::text("导入只读信号数据库"),m_path->text(),m_canModel?"DBC (*.dbc *.DBC)":"LDF (*.ldf *.LDF)");if(!path.isEmpty())m_vm->importAsync(path);});
    connect(m_reload,&QPushButton::clicked,this,[this]{m_vm->importAsync(m_vm->database()->path);});
    connect(m_valueModel,&SignalValueTableModel::validation,this,&SignalTransmitPage::feedback);
    planTable->remove=[this](const QVector<int> &rows){
        QString error;
        if(m_canModel){
            if(!m_vm->canStructure())return;
            QStringList keys;
            for(int row:rows)keys.append(m_canModel->key(row));
            if(!m_vm->removeQueuedFrames(keys,error))feedback(error);
        }else if(m_vm->canEditList()&&!m_vm->removeSlots(rows,error))feedback(error);
    };
    m_plan->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_plan,&QWidget::customContextMenuRequested,this,[this,planTable](const QPoint&position){
        const auto index=m_plan->indexAt(position);QMenu menu(this);menu.setObjectName("signalPlanMenu");
        auto*create=menu.addAction(m_canModel?"新建报文…":"新建帧…");create->setObjectName("signalCreateFrame");create->setEnabled(m_vm->canStructure()&&(m_canModel||!m_vm->working().schedule.isEmpty()));connect(create,&QAction::triggered,this,&SignalTransmitPage::editCustom);
        if(index.isValid()&&m_canModel){
            const auto key=m_canModel->key(index.row());
            if(!m_plan->selectionModel()->isRowSelected(index.row(),{})){
                m_plan->selectionModel()->setCurrentIndex(index,QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);
            }
            auto*duplicate=menu.addAction("复制报文 (同ID)");duplicate->setObjectName("signalDuplicateFrame");duplicate->setEnabled(m_vm->canStructure());
            connect(duplicate,&QAction::triggered,this,[this,key]{QString error;if(!m_vm->duplicateFrame(key,error))feedback(error);else rebuild();});
        }
        if(index.isValid()){
            auto *remove=menu.addAction(m_canModel?"删除报文":"删除槽");
            if(m_canModel)remove->setObjectName("signalRemoveFrame");
            remove->setEnabled(m_canModel?m_vm->canStructure():m_vm->canEditList());
            auto rows=planTable->rows();
            if(!rows.contains(index.row()))rows={index.row()};
            connect(remove,&QAction::triggered,this,[planTable,rows,anchor=QPersistentModelIndex(index)]{
                if(anchor.isValid())planTable->remove(rows);
            });
        }
        menu.exec(m_plan->viewport()->mapToGlobal(position));
    });
    auto selectCan=[this]{if(m_rendering)return;QString error;if(!m_vm->selectCanNode(m_canNode->currentData().toString(),m_canDirection->currentText(),error)){feedback(error);rebuild();}};
    connect(m_canNode,qOverload<int>(&QComboBox::currentIndexChanged),this,selectCan);
    connect(m_canDirection,qOverload<int>(&QComboBox::currentIndexChanged),this,selectCan);
    const auto toggleSend=[this](bool periodic){
        if(m_vm->running()){m_vm->stop();return;}
        QString error;if(!m_vm->start(periodic,error))feedback(error);
    };
    connect(m_once,&QPushButton::clicked,this,[toggleSend]{toggleSend(false);});
    connect(m_start,&QPushButton::clicked,this,[toggleSend]{toggleSend(true);});
    connect(m_back,&QPushButton::clicked,m_vm,&SignalTransmitViewModel::back);connect(m_restore,&QPushButton::clicked,m_vm,&SignalTransmitViewModel::restore);
    connect(m_role,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int role){if(m_rendering)return;QString error;
        if(!m_vm->setRole(LinRole(role),{},error)){feedback(error);rebuild();}});
    connect(m_node,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{if(m_rendering)return;QString error;if(!m_vm->setRole(LinRole(m_role->currentIndex()),m_node->currentText(),error))feedback(error);});
    connect(m_schedule,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{if(m_rendering)return;QString error;if(!m_vm->selectSchedule(m_schedule->currentText(),error))feedback(error);render();});

    connect(m_plan->model(),&QAbstractItemModel::modelReset,this,&SignalTransmitPage::rebuild);connect(vm,&SignalTransmitViewModel::changed,this,[this](bool countersOnly){if(!countersOnly)render();});
    connect(vm,&SignalTransmitViewModel::frameChanged,this,[this](const QString&key){if(key==m_key)renderFrame();});
    for(auto *table:{m_plan,m_values}){auto *header=table->horizontalHeader();header->setStretchLastSection(false);header->setMinimumSectionSize(38);header->setMaximumSectionSize(4096);header->setSectionResizeMode(QHeaderView::Interactive);header->setSectionsMovable(true);}
    m_plan->setColumnWidth(m_canModel?8:7,260);m_values->setColumnWidth(5,260);restoreTableLayouts();
    for(auto *table:{m_plan,m_values}){auto *header=table->horizontalHeader();connect(header,&QHeaderView::sectionMoved,this,&SignalTransmitPage::saveTableLayouts);connect(header,&QHeaderView::sectionResized,this,&SignalTransmitPage::saveTableLayouts);}
    connect(vm,&SignalTransmitViewModel::configurationRestored,this,&SignalTransmitPage::restoreTableLayouts);rebuild();
}
void SignalTransmitPage::saveTableLayouts(){
    if(m_restoringColumns)return;auto ui=m_vm->working().uiSettings;auto tables=ui.value("tableLayouts").toObject();
    for(auto *table:{m_plan,m_values}){const auto *header=table->horizontalHeader();QJsonArray order,widths;for(int i=0;i<header->count();++i){order.append(header->logicalIndex(i));widths.append(header->sectionSize(i));}
        tables[table==m_plan?(m_canModel?"canTransmit":"linTransmit"):"signalValues"]=QJsonObject{{"order",order},{"widths",widths}};}
    ui["tableLayouts"]=tables;m_vm->setUiSettings(ui);
}
void SignalTransmitPage::restoreTableLayouts(){
    QScopedValueRollback<bool> guard(m_restoringColumns,true);const auto tables=m_vm->working().uiSettings.value("tableLayouts").toObject();
    for(auto *table:{m_plan,m_values}){auto *header=table->horizontalHeader();QSignalBlocker blocker(header);
        const auto saved=tables.value(table==m_plan?(m_canModel?"canTransmit":"linTransmit"):"signalValues").toObject();const auto order=saved.value("order").toArray(),widths=saved.value("widths").toArray();
        if(order.size()!=header->count()||widths.size()!=header->count())continue;QSet<int> seen;bool valid=true;
        for(int i=0;i<order.size();++i){const int logical=order[i].toInt(-1),width=widths[i].toInt(-1);if(logical<0||logical>=header->count()||seen.contains(logical)||width<38||width>4096||order[i].toDouble()!=logical||widths[i].toDouble()!=width){valid=false;break;}seen.insert(logical);}
        if(!valid)continue;for(int visual=0;visual<header->count();++visual){header->moveSection(header->visualIndex(order[visual].toInt()),visual);header->resizeSection(visual,widths[visual].toInt());}
    }
}
void SignalTransmitPage::feedback(const QString&text){m_validation->setText(text);m_validation->setVisible(!text.isEmpty());m_configurationFeedback->setText(text);}
void SignalTransmitPage::selectFrame(const QString&key){if(key.isEmpty()||!m_vm->frame(key))return;m_key=key;m_valueModel->setFrame(key);renderFrame();}
void SignalTransmitPage::rebuild(){QScopedValueRollback<bool>guard(m_rendering,true);m_path->setText(m_vm->database()->path);m_role->setCurrentIndex(int(m_vm->working().role));m_node->clear();
    for(const auto&n:m_vm->database()->nodes)if(m_vm->working().role!=LinRole::Slave||n!=m_vm->database()->master)m_node->addItem(n);m_node->setCurrentText(m_vm->working().node);
    m_schedule->clear();for(const auto &s:m_vm->working().schedules)m_schedule->addItem(s.name);m_schedule->setCurrentText(m_vm->working().schedule);
    m_canNode->clear();m_canNode->addItem("请选择节点",QString());for(const auto&node:m_vm->database()->nodes)m_canNode->addItem(node,node);
    m_canNode->setCurrentIndex(m_canNode->findData(m_vm->working().canNode));m_canDirection->setCurrentText(m_vm->working().canDirection);
    m_tree->header()->setStretchLastSection(false);m_tree->header()->setSectionResizeMode(0,QHeaderView::Stretch);m_tree->header()->setSectionResizeMode(1,QHeaderView::Fixed);m_tree->setColumnWidth(1,85);
    if(!m_addingFromTree)m_tree->expandToDepth(0);
    int selected=-1;
    for(int row=0;row<m_plan->model()->rowCount();++row)if((m_canModel?m_canModel->key(row):m_linModel->key(row))==m_key){selected=row;break;}
    if(selected<0 && m_plan->model()->rowCount()>0)selected=0;
    if(selected>=0){m_plan->selectRow(selected);selectFrame(m_canModel?m_canModel->key(selected):m_linModel->key(selected));}
    else if(m_vm->database()->requiresDbcCompatibility && m_vm->frame(m_key))selectFrame(m_key);
    else{m_key.clear();m_valueModel->setFrame({});}render();
}
void SignalTransmitPage::render(){QScopedValueRollback<bool>guard(m_rendering,true);const bool can=m_canModel!=nullptr;const bool structure=m_vm->canStructure();
    m_replayPath->setText(m_vm->working().replaySettings.value("path").toString());m_replayImport->setEnabled(structure);m_replayReset->setEnabled(structure&&m_vm->replayConfigured());m_replayMap->setEnabled(structure&&!m_vm->replayLog().channels.isEmpty());
    const bool isCanRx=can&&m_vm->working().canDirection=="Rx";
    m_rolePanel->setVisible(!can);m_import->setEnabled(structure);m_reload->setEnabled(structure&&!m_vm->database()->path.isEmpty());m_canPanel->setVisible(can);m_canNode->setEnabled(structure);m_canDirection->setEnabled(structure);
    m_newSchedule->setEnabled(structure);m_removeSchedule->setEnabled(m_vm->canEditList()&&!m_vm->working().schedule.isEmpty());m_role->setEnabled(structure);m_node->setEnabled(structure&&m_vm->working().role==LinRole::Slave);
    const auto state=m_vm->status().state;m_schedule->setEnabled(!m_vm->status().cleanupPending&&!m_vm->replayRunning()&&(structure||m_vm->running())&&state!=RunState::SwitchPending&&state!=RunState::Switching&&state!=RunState::Stopping);
    m_schedule->setCurrentText(m_vm->status().pending.isEmpty()?m_vm->working().schedule:m_vm->status().pending);
    const bool running=m_vm->running();
    const bool periodicRun=running&&m_vm->periodicRun();
    const bool finiteRun=running&&!periodicRun;
    const bool canStop=state!=RunState::Stopping;
    const bool replay=m_vm->replayConfigured();
    const bool enabled=m_vm->canStart()&&(replay?!m_vm->replayLog().frames.isEmpty():
        m_plan->model()->rowCount()>0&&m_vm->hasEnabled());
    const bool finiteEnabled=enabled&&!isCanRx&&(replay||can||m_vm->working().role==LinRole::Master);
    m_once->setVisible(!isCanRx||finiteRun);
    m_once->setEnabled(finiteRun?canStop:finiteEnabled);
    m_count->setEnabled(finiteEnabled);
    {QSignalBlocker block(m_count);m_count->setValue(m_vm->working().repeatCount);}
    m_start->setEnabled(periodicRun?canStop:isCanRx?m_vm->canStart():enabled);
    const char *runningText=can?"发送中...":"调度中...";
    m_once->setText(finiteRun?runningText:replay?"多次回放":can?"发送":"调度");
    const char *periodicText=replay?"周期回放":isCanRx?"开始接收":can?"周期发送":m_vm->working().role==LinRole::Master?"周期调度":m_vm->working().role==LinRole::Slave?"启动从节点响应":"开始监听";
    m_start->setText(periodicRun?(isCanRx&&!replay?"停止":runningText):periodicText);
    const auto setPrimary=[](QPushButton *button,bool primary){
        if(button->property("primary").toBool()==primary)return;
        button->setProperty("primary",primary);
        button->style()->unpolish(button);
        button->style()->polish(button);
        button->update();
    };
    setPrimary(m_once,!finiteRun);
    setPrimary(m_start,!periodicRun);
    m_back->setEnabled(m_vm->canBack());m_restore->setEnabled(m_vm->canRestore());
    const auto db=m_vm->database();m_summary->setText(m_vm->importing()?m_vm->message():QString("%1 · %2 个节点 / %3 个帧 · %4%5").arg(can?"DBC":"LDF").arg(db->nodes.size()).arg(m_vm->definitions().size()).arg(db->version,db->diagnostics.isEmpty()?QString():QString(" · %1 条能力诊断（悬停查看）").arg(db->diagnostics.size())));const auto details=db->diagnostics.join('\n');
    m_summary->setToolTip(details);
    if(m_databaseDiagnostics->toPlainText()!=details)m_databaseDiagnostics->setPlainText(details);
    const bool extendedDetails=db->requiresDbcCompatibility && !details.isEmpty();
    m_databaseDiagnostics->setVisible(extendedDetails);
    if(!extendedDetails&&!details.isEmpty())m_summary->setText(m_summary->text()+"\n"+details);
    if(!m_vm->message().isEmpty())m_configurationFeedback->setText(m_vm->message());renderFrame();
}
void SignalTransmitPage::renderFrame(){if(m_values->isEnabled()!=m_vm->canData())m_values->setEnabled(m_vm->canData());}
void SignalTransmitPage::editCustom(){
    if(!m_vm->canStructure())return;
    QDialog dialog(this);dialog.setObjectName("signalCustomDialog");dialog.setWindowTitle(m_canModel?"新建 CAN 报文":"新建 LIN 帧");auto*layout=new QFormLayout(&dialog);auto*name=new QLineEdit;auto*id=new QLineEdit;name->setObjectName("signalCustomName");id->setObjectName("signalCustomId");auto*type=plain(m_canModel?"ID ≤ 0x7FF：标准；其余：扩展（自动）":"LIN ID 0–0x3D；0x3C/0x3D 使用经典校验和 8 字节");
    auto*length=new QSpinBox;length->setRange(m_canModel?0:1,8);length->setValue(8);auto*period=new QSpinBox;period->setRange(0,std::numeric_limits<int>::max());period->setValue(100);auto*error=plain("");
    layout->addRow("名称",name);layout->addRow("ID（HEX）",id);layout->addRow(type);layout->addRow("字节数",length);layout->addRow("周期 ms",period);layout->addRow(error);auto*buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addRow(buttons);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{bool parsed=false;const auto value=id->text().toULongLong(&parsed,16);QString why;if(!parsed||value>(m_canModel?0x1fffffff:61)){error->setText("ID 必须在 0–0x1FFFFFFF");return;}if(!m_vm->putCustom({},name->text(),quint32(value),length->value(),period->value(),why,true)){error->setText(why);return;}if(!m_vm->working().customFrames.isEmpty()){m_key=m_vm->working().customFrames.last().key;}else{m_key=frameKey(m_canModel?Bus::Can:Bus::Lin,quint32(value),value>0x7ff);}rebuild();dialog.accept();});dialog.exec();
}

}

namespace host {
void SignalTransmitPage::editReplayMapping(){
    QDialog dialog(m_configurationDialog);dialog.setObjectName("replayChannelMapping");dialog.setWindowTitle("日志通道 → 软件通道");dialog.resize(600,420);auto *layout=new QVBoxLayout(&dialog);
    layout->addWidget(plain("无：排除此日志通道。多个日志通道可映射到同一个软件通道；一次操作统一控制全部映射目标。"));
    auto *table=new QTableWidget;table->setColumnCount(2);table->setHorizontalHeaderLabels({"日志通道","目标软件通道"});table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);layout->addWidget(table);
    const auto channels=m_vm->replayLog().channels;const auto mapping=m_vm->working().replaySettings.value("mapping").toObject();table->setRowCount(channels.size());
    for(int n=0;n<channels.size();++n){auto *item=new QTableWidgetItem(channels[n]);item->setFlags(item->flags()&~Qt::ItemIsEditable);table->setItem(n,0,item);auto *target=new QComboBox;target->setProperty("translateFirstOption",true);target->addItem("无",QString());if(m_vm->replayTargets)for(const auto &name:m_vm->replayTargets(channels[n].startsWith("CAN:")?signal::Bus::Can:signal::Bus::Lin))target->addItem(name,name);target->setCurrentIndex(qMax(0,target->findData(mapping.value(channels[n]).toString())));table->setCellWidget(n,1,target);}
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return;QJsonObject result;for(int n=0;n<channels.size();++n)result[channels[n]]=qobject_cast<QComboBox*>(table->cellWidget(n,1))->currentData().toString();m_vm->setReplayMapping(result);
}
}
