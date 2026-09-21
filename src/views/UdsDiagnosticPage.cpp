#include "UdsDiagnosticPage.h"
#include "localization/Language.h"
#include "UdsSettingsDialog.h"
#include "viewmodels/DiagnosticDraftViewModel.h"
#include <QBoxLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QTableView>
#include <QTableWidget>
#include <QPlainTextEdit>
#include <QCheckBox>
#include <QSplitter>
#include <QHeaderView>
#include <QFileDialog>
#include <QSortFilterProxyModel>
#include <QSignalBlocker>
#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QScopedValueRollback>
#include <QSpinBox>

namespace host {
static QLabel *plain(const QString &s){auto l=new QLabel(s);l->setTextFormat(Qt::PlainText);return l;}
UdsDiagnosticPage::UdsDiagnosticPage(ChannelViewModel *vm,QWidget *parent):QWidget(parent),m_vm(vm){
    setObjectName("udsDiagnosticPage");auto root=new QVBoxLayout(this);root->setContentsMargins(10,6,10,6);root->setSpacing(5);
    m_settings=new QPushButton("UDS 设置…");m_settings->setObjectName("udsSettings");
    m_repeatStatus=plain("");m_repeatStatus->setObjectName("udsRepeatStatus");
    auto split=new QSplitter(Qt::Horizontal);split->setObjectName("udsMainSplit");split->setChildrenCollapsible(false);
    auto left=new QWidget;auto leftLayout=new QVBoxLayout(left);leftLayout->setContentsMargins(0,0,0,0);leftLayout->setSpacing(4);
    auto consoleHeading=new QHBoxLayout;auto console=plain("诊断控制台");console->setObjectName("sectionTitle");consoleHeading->addWidget(console);consoleHeading->addStretch();consoleHeading->addWidget(m_repeatStatus);consoleHeading->addWidget(m_settings);leftLayout->addLayout(consoleHeading);
    auto filter=new QLineEdit;filter->setObjectName("udsServiceFilter");filter->setPlaceholderText("搜索服务名称、SID、DID 或 RID");leftLayout->addWidget(filter);
    m_services=new QTableView;m_services->setObjectName("udsServiceTable");m_services->setSelectionBehavior(QAbstractItemView::SelectRows);m_services->setSelectionMode(QAbstractItemView::SingleSelection);
    m_services->setEditTriggers(QAbstractItemView::NoEditTriggers);m_services->setAlternatingRowColors(true);m_services->setSortingEnabled(true);m_services->verticalHeader()->hide();m_services->verticalHeader()->setDefaultSectionSize(25);
    m_proxy=new QSortFilterProxyModel(this);m_proxy->setSourceModel(vm->diagnosticServices());m_proxy->setFilterKeyColumn(-1);m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);m_services->setModel(m_proxy);
    m_services->setColumnWidth(0,60);m_services->setColumnWidth(1,230);m_services->setColumnWidth(2,140);m_services->setColumnWidth(3,70);m_services->horizontalHeader()->setStretchLastSection(true);m_services->sortByColumn(0,Qt::AscendingOrder);
    leftLayout->addWidget(m_services,1);
    auto right=new QWidget;auto editor=new QVBoxLayout(right);editor->setContentsMargins(0,0,0,0);editor->setSpacing(4);
    auto detailSplit=new QSplitter(Qt::Vertical);detailSplit->setObjectName("udsDetailSplit");detailSplit->setChildrenCollapsible(false);editor->addWidget(detailSplit);
    auto parameterPanel=new QWidget;auto parameterLayout=new QVBoxLayout(parameterPanel);parameterLayout->setContentsMargins(0,0,0,0);parameterLayout->setSpacing(3);
    auto requestPanel=new QWidget;auto requestLayout=new QVBoxLayout(requestPanel);requestLayout->setContentsMargins(0,0,0,0);requestLayout->setSpacing(3);
    auto options=new QHBoxLayout;auto parameterHeading=plain("请求参数");parameterHeading->setObjectName("sectionTitle");options->addWidget(parameterHeading,1);m_suppress=new QCheckBox("抑制正响应");m_suppress->setObjectName("udsSuppressResponse");options->addWidget(m_suppress);parameterLayout->addLayout(options);
    m_parameters=new QTableWidget;m_parameters->setObjectName("udsParameterTable");m_parameters->setColumnCount(3);m_parameters->setProperty("translatedColumns",QVariantList{2});m_parameters->setHorizontalHeaderLabels({"字段","输入值","类型 / 约束"});
    m_parameters->verticalHeader()->hide();m_parameters->setColumnWidth(0,150);m_parameters->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);m_parameters->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
    m_parameters->setMinimumHeight(60);parameterLayout->addWidget(m_parameters,1);
    auto rawHeading=plain("报文（RAW）");rawHeading->setObjectName("sectionTitle");rawHeading->setAlignment(Qt::AlignLeft|Qt::AlignVCenter);requestLayout->addWidget(rawHeading);
    m_request=new QPlainTextEdit;m_request->setObjectName("udsCommand");m_request->setReadOnly(true);m_request->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));m_request->setMinimumHeight(40);requestLayout->addWidget(m_request,1);
    m_validation=plain("");m_validation->setObjectName("udsValidation");m_validation->setWordWrap(true);requestLayout->addWidget(m_validation);
    auto actions=new QHBoxLayout;m_copy=new QPushButton("复制指令");m_copy->setObjectName("copyUdsCommand");m_send=new QPushButton("发送");m_send->setObjectName("sendUdsRequest");m_send->setProperty("primary",true);
    actions->addWidget(m_copy);actions->addStretch();actions->addWidget(m_send);m_revert=new QPushButton("撤销");m_revert->setObjectName("revertUdsCommand");actions->addWidget(m_revert);requestLayout->addLayout(actions);
    m_response=new QPlainTextEdit;m_response->setObjectName("udsResponse");m_response->setReadOnly(true);m_response->setMaximumBlockCount(1000);m_response->setPlaceholderText("请求结果、响应 HEX 和 CDD 字段解析");m_response->setMinimumHeight(55);
    auto responsePanel=new QWidget;auto responseLayout=new QVBoxLayout(responsePanel);responseLayout->setContentsMargins(0,0,0,0);responseLayout->setSpacing(3);auto responseHeading=plain("解析结果");responseHeading->setObjectName("sectionTitle");responseHeading->setAlignment(Qt::AlignLeft|Qt::AlignVCenter);responseLayout->addWidget(responseHeading);responseLayout->addWidget(m_response,1);
    detailSplit->addWidget(parameterPanel);detailSplit->addWidget(requestPanel);detailSplit->addWidget(responsePanel);detailSplit->setSizes({130,115,100});
    split->addWidget(left);split->addWidget(right);split->setSizes({550,450});root->addWidget(split,1);setMinimumHeight(335);
    connect(filter,&QLineEdit::textChanged,m_proxy,&QSortFilterProxyModel::setFilterFixedString);
    connect(m_settings,&QPushButton::clicked,this,[this]{editUdsSettings(m_vm,this);});
    connect(m_services->selectionModel(),&QItemSelectionModel::currentRowChanged,this,[this]{selectService();});
    connect(m_suppress,&QCheckBox::toggled,this,&UdsDiagnosticPage::updateRequest);
    connect(m_request,&QPlainTextEdit::textChanged,this,[this]{if(!m_updating){m_custom=true;updateRequest();}});
    connect(m_revert,&QPushButton::clicked,this,[this]{m_custom=false;updateRequest();});
    connect(m_copy,&QPushButton::clicked,this,[this]{if(!m_bytes.isEmpty()&&m_error.isEmpty())QApplication::clipboard()->setText(QString::fromLatin1(m_bytes.toHex(' ')).toUpper());});
    connect(m_send,&QPushButton::clicked,this,[this]{
        if(m_vm->diagnosticBusy()){m_vm->cancel();return;}
        updateRequest();if(!m_error.isEmpty())return;QString error;
        bool sent=m_vm->settings().udsRepeatEnabled?m_vm->repeatDiagnostic(sourceRow(),m_bytes,error):m_vm->sendDiagnostic(sourceRow(),m_bytes,error);
        if(!sent)m_validation->setText(error);
    });
    connect(vm,&ChannelViewModel::diagnosticDatabaseChanged,this,&UdsDiagnosticPage::refreshDatabase);
    connect(vm,&ChannelViewModel::changed,this,&UdsDiagnosticPage::render);
    connect(&Language::instance(),&Language::changed,this,&UdsDiagnosticPage::render);
    refreshDatabase();render();
}
int UdsDiagnosticPage::sourceRow()const {auto i=m_services->currentIndex();return i.isValid()?m_proxy->mapToSource(i).row():-1;}
void UdsDiagnosticPage::refreshDatabase(){
    if(m_proxy->rowCount())m_services->selectRow(0);selectService();
}
void UdsDiagnosticPage::selectService(){
    QScopedValueRollback<bool> guard(m_updating,true);QSignalBlocker suppress(m_suppress);
    m_custom=false;m_suppress->setChecked(false);m_parameters->setRowCount(0);m_request->clear();
    auto s=m_vm->diagnosticServices()->service(sourceRow());bool supports=false;
    if(s){
        for(const auto &f:s->request.fields){
            supports|=f.suppressible;if(f.constant)continue;const int row=m_parameters->rowCount();m_parameters->insertRow(row);
            auto name=new QTableWidgetItem(f.name);name->setToolTip(f.name);name->setData(Qt::UserRole,f.key);name->setFlags(name->flags()&~Qt::ItemIsEditable);m_parameters->setItem(row,0,name);
            bool choices=!f.array&&!f.choices.isEmpty();
            if(choices){
                auto combo=new QComboBox;combo->setEditable(true);combo->setInsertPolicy(QComboBox::NoInsert);combo->addItem("请选择 / 输入原始值",QString());
                for(const auto &c:f.choices)combo->addItem(QString("%1 · 0x%2%3").arg(c.text,QString::number(c.first,16).toUpper(),c.last==c.first?QString():"–0x"+QString::number(c.last,16).toUpper()),"0x"+QString::number(c.first,16));
                combo->setObjectName("udsField_"+f.key);m_parameters->setCellWidget(row,1,combo);connect(combo,&QComboBox::currentTextChanged,this,[this]{updateRequest();});
            }else{
                auto edit=new QLineEdit;edit->setObjectName("udsField_"+f.key);
                edit->setPlaceholderText(f.array?(f.encoding=="asc"||f.encoding.startsWith("utf")?"文本":"HEX 字节，如 01 02"):"物理值；0x 前缀为原始值");
                m_parameters->setCellWidget(row,1,edit);connect(edit,&QLineEdit::textChanged,this,[this]{updateRequest();});
            }
            auto constraint=new QTableWidgetItem(f.encoding+" · "+f.constraint());constraint->setFlags(constraint->flags()&~Qt::ItemIsEditable);constraint->setToolTip(constraint->text());m_parameters->setItem(row,2,constraint);
        }
    }
    m_suppress->setVisible(supports);m_updating=false;updateRequest();
}
diag::Values UdsDiagnosticPage::values()const{
    diag::Values values;for(int i=0;i<m_parameters->rowCount();++i){auto w=m_parameters->cellWidget(i,1);QString value;
        if(auto edit=qobject_cast<QLineEdit*>(w))value=edit->text();
        else if(auto combo=qobject_cast<QComboBox*>(w))value=combo->currentText()==combo->itemText(combo->currentIndex())?combo->currentData().toString():combo->currentText();
        values[m_parameters->item(i,0)->data(Qt::UserRole).toString()]=value;
    }return values;
}
void UdsDiagnosticPage::updateRequest(){
    if(m_updating)return;QScopedValueRollback<bool> guard(m_updating,true);m_bytes.clear();m_error.clear();
    const auto preview=DiagnosticDraftViewModel::preview(m_vm->diagnosticServices()->service(sourceRow()),values(),m_custom,m_request->toPlainText(),m_suppress->isChecked());
    m_bytes=preview.bytes;m_error=preview.error;if(!m_custom)m_request->setPlainText(preview.text);
    render();
}
void UdsDiagnosticPage::render(){
    const bool idle=!m_vm->busy();for(auto w:QList<QWidget*>{m_settings,m_services})w->setEnabled(idle);
    m_repeatStatus->setText(m_vm->diagnosticRepeatStatus());m_repeatStatus->setToolTip(m_repeatStatus->text());
    m_parameters->setEnabled(idle&&!m_custom);m_request->setReadOnly(!idle);m_revert->setEnabled(idle&&m_custom);m_suppress->setEnabled(idle&&!m_custom);
    m_send->setText(m_vm->diagnosticBusy()?"取消发送":"发送");
    auto hint=m_vm->diagnosticHint();auto s=m_vm->diagnosticServices()->service(sourceRow());
    m_send->setEnabled(m_vm->diagnosticBusy()||(m_error.isEmpty()&&!m_bytes.isEmpty()&&hint.isEmpty()&&s&&s->physical));m_copy->setEnabled(m_error.isEmpty()&&!m_bytes.isEmpty());
    m_validation->setText(!m_error.isEmpty()?m_error:(!hint.isEmpty()?hint:QString("%1 字节 · %2").arg(m_bytes.size()).arg(m_vm->settings().simulation?"模拟响应仅用于功能验证":"物理寻址")));
    m_validation->setStyleSheet(m_error.isEmpty()?QString():"color:#A33A2B;");
    const auto response=Language::text(m_vm->diagnosticResult());if(m_response->toPlainText()!=response)m_response->setPlainText(response);
}
}
