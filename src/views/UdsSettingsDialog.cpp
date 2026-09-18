#include "UdsSettingsDialog.h"
#include "viewmodels/ChannelViewModel.h"
#include "viewmodels/DiagnosticDraftViewModel.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QScreen>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QFileDialog>
#include <QSignalBlocker>
#include <QKeyEvent>
#include <QIntValidator>

namespace host {
class SettingsDialog final:public QDialog {
public: using QDialog::QDialog;
protected:
    void keyPressEvent(QKeyEvent *event) override {
        // Return belongs to the path/target editors, never to the Save button.
        if(event->key()==Qt::Key_Return||event->key()==Qt::Key_Enter){event->accept();return;}
        QDialog::keyPressEvent(event);
    }
};
void editUdsSettings(ChannelViewModel *vm,QWidget *parent,bool download){
    if(vm->busy())return;
    SettingsDialog dialog(parent);dialog.setObjectName("udsSettingsDialog");dialog.setWindowTitle(download?"下载参数配置":"UDS 设置");
    const auto available=dialog.screen()->availableGeometry().size();dialog.resize(qMin(990,available.width()-40),qMin(520,available.height()-60));
    auto root=new QVBoxLayout(&dialog);auto scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);root->addWidget(scroll,1);
    auto content=new QWidget;auto layout=new QVBoxLayout(content);layout->setContentsMargins(4,4,4,4);scroll->setWidget(content);
    const auto defaults=ChannelSettings::defaults(vm->settings().bus);
    DiagnosticDraftViewModel edit(vm->settings(),vm->diagnosticDatabase());auto &draft=edit.settings;auto &db=edit.database;bool loading=false,targetValid=true;
    auto target=new QGroupBox("诊断数据库与目标");auto targetLayout=new QVBoxLayout(target);layout->addWidget(target);target->setVisible(!download);
    auto fileRow=new QHBoxLayout;auto path=new QLineEdit(draft.cddPath);path->setObjectName("cddPath");path->setPlaceholderText("CANdela 15 或更早版本的 CDD");
    auto browse=new QPushButton("选择 CDD…");browse->setObjectName("browseCdd");
    auto ecu=new QComboBox;ecu->setObjectName("cddEcu");auto variant=new QComboBox;variant->setObjectName("cddVariant");
    for(auto c:{ecu,variant}){c->setEditable(true);c->setInsertPolicy(QComboBox::NoInsert);c->setMinimumContentsLength(10);c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);}
    fileRow->addWidget(new QLabel("CDD"));fileRow->addWidget(path,1);fileRow->addWidget(browse);
    auto indicator=new QLabel;indicator->setObjectName("cddProtocolIndicator");indicator->setFixedSize(12,12);fileRow->addWidget(indicator);
    auto indicate=[&](int state){indicator->setProperty("protocolState",state);indicator->setStyleSheet(QString("background:%1;border-radius:6px;").arg(state==0?"#999999":state==1?"#239B56":"#D64541"));};indicate(0);targetLayout->addLayout(fileRow);
    auto targetRow=new QHBoxLayout;targetRow->addWidget(new QLabel("ECU"));targetRow->addWidget(ecu,1);targetRow->addWidget(new QLabel("Variant"));targetRow->addWidget(variant,1);targetLayout->addLayout(targetRow);
    auto repeatRow=new QHBoxLayout;auto repeat=new QCheckBox("自动重发");repeat->setObjectName("udsAutoRepeat");repeatRow->addWidget(repeat);
    auto repeatValue=[&](const QString &name,const QString &label,int low,int high){
        repeatRow->addWidget(new QLabel(label));
        auto edit=new QLineEdit;edit->setObjectName(name);edit->setValidator(new QIntValidator(low,high,edit));edit->setMaximumWidth(90);
        edit->setStyleSheet("QLineEdit:disabled { background:#E6E8EB;color:#A0A6AC; }");repeatRow->addWidget(edit);return edit;
    };
    auto count=repeatValue("udsRepeatCount","重发次数",1,10000);
    auto delay=repeatValue("udsRepeatDelay","完成后延时 / ms",0,86400000);
    auto enableRepeat=[&]{count->setEnabled(repeat->isChecked());delay->setEnabled(repeat->isChecked());};
    QObject::connect(repeat,&QCheckBox::toggled,&dialog,enableRepeat);
    repeatRow->addStretch();layout->addLayout(repeatRow);
    for(int i=0;i<repeatRow->count();++i)if(auto w=repeatRow->itemAt(i)->widget())w->setVisible(!download);
    count->setToolTip("首次发送之后额外发送的次数；失败或取消立即停止。");
    auto columns=new QHBoxLayout;layout->addLayout(columns);
    auto session=new QGroupBox("会话层");auto sf=new QFormLayout(session);columns->addWidget(session,1);
    auto network=new QGroupBox(draft.bus==communication::Bus::Can?"CAN 网络层（ISO-TP）":"LIN 网络层");auto nf=new QFormLayout(network);columns->addWidget(network,1);
    QMap<QString,QLineEdit*> numbers,canNumbers,texts;
    QMap<QString,QWidget*> rows;
    const auto defaultJson=defaults.toJson();
    auto pair=[&](QFormLayout *form,const QString &key,const QString &label,const QString &value){
        auto row=new QWidget;auto box=new QHBoxLayout(row);box->setContentsMargins(0,0,0,0);
        auto baseline=new QLineEdit(value);baseline->setReadOnly(true);baseline->setStyleSheet("QLineEdit { background:#EEF1F4;color:#687582; } QLineEdit:disabled { background:#E6E8EB;color:#A0A6AC; }");baseline->setObjectName("uds_default_"+key);
        auto edit=new QLineEdit;edit->setObjectName("uds_"+key);edit->setStyleSheet("QLineEdit:disabled { background:#E6E8EB;color:#A0A6AC; }");
        box->addWidget(baseline);box->addWidget(edit);form->addRow(label,row);rows[key]=row;return edit;
    };
    auto header=[](QFormLayout *form){auto row=new QWidget;auto box=new QHBoxLayout(row);box->setContentsMargins(0,0,0,0);box->addWidget(new QLabel("默认值"),1);box->addWidget(new QLabel("使用值"),1);form->addRow("参数",row);};
    header(sf);header(nf);
    auto number=[&](QFormLayout *form,const QString &key,const QString &label,int low,int high,bool ms=true){
        auto edit=pair(form,key,label+(ms?" / ms":""),QString::number(defaultJson.value(key).toInt()));
        edit->setValidator(new QIntValidator(low,high,edit));numbers[key]=edit;return edit;
    };
    auto text=[&](QFormLayout *form,const QString &key,const QString &label){auto edit=pair(form,key,label,defaultJson.value(key).toString());texts[key]=edit;return edit;};
    if(download){text(sf,"profileId","配置名称");number(sf,"programmingSession","编程会话（十进制）",1,127,false);number(sf,"securityLevel","安全级别（十进制）",1,125,false);}
    number(sf,"p2Ms","P2 Client",1,60000);number(sf,"p2StarMs","P2* Client",1,600000);
    number(sf,"p2ServerMs","P2 Server（时序余量基准）",0,60000);number(sf,"p2StarServerMs","P2* Server（时序余量基准）",0,600000);
    number(sf,"p3Ms","P3 Client（抑制正响应等待）",0,600000);
    number(sf,"maxPendingMs","等待响应总超时",1,3600000);
    auto keepalive=new QCheckBox("启用会话保持");keepalive->setObjectName("uds_testerPresentEnabled");sf->addRow(keepalive);
    number(sf,"testerPresentMs","S3 Client / TesterPresent 周期",0,600000);
    text(sf,"testerPresentRequest","TesterPresent HEX")->setToolTip("3E 00 等待正响应；3E 80 抑制正响应。进入诊断会话后按周期发送。");
    auto extended=new QCheckBox("29 位 CAN ID");extended->setObjectName("uds_extendedId");
    if(draft.bus==communication::Bus::Can){
        nf->addRow(extended);text(nf,"requestId","请求 CAN ID（HEX）");text(nf,"responseId","响应 CAN ID（HEX）");text(nf,"functionalId","功能 CAN ID（HEX）");
        auto can=[&](const QString &key,const QString &label,int low,int high,bool ms=false){
            auto edit=pair(nf,key,label+(ms?" / ms":""),QString::number(defaults.canNetwork.value(key).toInt()));
            edit->setValidator(new QIntValidator(low,high,edit));canNumbers[key]=edit;return edit;
        };
        can("blockSize","BlockSize",0,255);can("stMin","STmin（编码值）",0,249)->setToolTip("0–127：毫秒；241–249：100–900 μs。其余编码无效。");
        can("nAsMs","N_As",1,60000,true);can("nArMs","N_Ar（0 = 使用 N_As）",0,60000,true);
        can("nBsMs","N_Bs",1,60000,true);can("nCrMs","N_Cr",1,60000,true);
        can("padding","填充字节（十进制）",0,255);can("receiveCapacity","最大 PDU 字节数",8,4095);can("maxWaitFrames","最大 FC Wait 次数",0,255);
    }else{
        extended->setParent(content);extended->hide();text(nf,"nad","NAD（HEX）");
        number(nf,"linSlotMs","诊断调度间隔（0 = 自动）",0,60000);number(nf,"linAsMs","N_As",1,60000);number(nf,"linCrMs","N_Cr",1,60000);
    }
    auto enableKeepalive=[&]{for(const auto &key:{QString("testerPresentMs"),QString("testerPresentRequest")})rows[key]->setEnabled(keepalive->isChecked());};
    QObject::connect(keepalive,&QCheckBox::toggled,&dialog,enableKeepalive);
    auto error=new QLabel;error->setObjectName("udsSettingsError");error->setWordWrap(true);error->setStyleSheet("color:#A33A2B;");root->addWidget(error);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);buttons->button(QDialogButtonBox::Ok)->setText("保存");buttons->button(QDialogButtonBox::Cancel)->setText("取消");root->addWidget(buttons);
    auto showSettings=[&]{const auto json=draft.toJson();for(auto i=numbers.begin();i!=numbers.end();++i)i.value()->setText(QString::number(json.value(i.key()).toInt()));for(auto i=texts.begin();i!=texts.end();++i)i.value()->setText(json.value(i.key()).toString());for(auto i=canNumbers.begin();i!=canNumbers.end();++i)i.value()->setText(QString::number(draft.canNetwork.value(i.key()).toInt(defaults.canNetwork.value(i.key()).toInt())));
        repeat->setChecked(draft.udsRepeatEnabled);enableRepeat();count->setText(QString::number(draft.udsRepeatCount));delay->setText(QString::number(draft.udsRepeatDelayMs));extended->setChecked(draft.extendedId);keepalive->setChecked(draft.testerPresentEnabled);enableKeepalive();
    };
    auto collect=[&](ChannelSettings &out){
        if(!count->hasAcceptableInput()||!delay->hasAcceptableInput()){error->setText("请输入有效范围内的重发次数和延时");return false;}
        for(const auto &group:{numbers,canNumbers})for(auto edit:group)if(!edit->hasAcceptableInput()){error->setText("请输入有效范围内的整数");edit->setFocus();return false;}
        auto json=draft.toJson();for(auto i=numbers.begin();i!=numbers.end();++i)json[i.key()]=i.value()->text().toInt();for(auto i=texts.begin();i!=texts.end();++i)json[i.key()]=i.value()->text().trimmed();auto can=draft.canNetwork;for(auto i=canNumbers.begin();i!=canNumbers.end();++i)can[i.key()]=i.value()->text().toInt();json["canNetwork"]=can;
        json["udsRepeatEnabled"]=repeat->isChecked();json["udsRepeatCount"]=count->text().toInt();json["udsRepeatDelayMs"]=delay->text().toInt();json["extendedId"]=extended->isChecked();json["testerPresentEnabled"]=keepalive->isChecked();QString message;
        if(!ChannelSettings::fromJson(json,out,message)){error->setText(message);return false;}return true;
    };
    auto selected=[](QComboBox *c){return c->currentIndex()>=0&&c->currentText()==c->itemText(c->currentIndex())?c->currentData().toString():c->currentText().trimmed();};
    auto findEcu=[&]() -> const diag::Ecu* {return edit.ecu(selected(ecu));};
    auto select=[&](bool import){
        if(import){ChannelSettings entered;if(collect(entered))draft=entered;}
        int protocol=2;QString message;targetValid=edit.select(selected(ecu),selected(variant),import,protocol,message);indicate(protocol==0?2:protocol);error->setText(message);
        if(targetValid&&import)showSettings();
    };
    auto fillVariants=[&]{QSignalBlocker blocker(variant);variant->clear();if(auto e=findEcu()){int index=0;for(const auto &v:e->variants){if(v.base)index=variant->count();variant->addItem(v.name,v.id);}int saved=variant->findData(draft.cddVariant);variant->setCurrentIndex(saved>=0?saved:index);}};
    auto populate=[&]{loading=true;{QSignalBlocker blocker(ecu);ecu->clear();for(const auto &e:db.ecus)ecu->addItem(e.name,e.id);int index=ecu->findData(draft.cddEcu);ecu->setCurrentIndex(index>=0?index:0);}fillVariants();loading=false;};
    auto load=[&]{if(loading)return;QString message;if(!edit.loadDatabase(path->text().trimmed(),message)){targetValid=false;indicate(2);error->setText(message);return;}
        // The imported database supplies new protocol values; unsaved changes to
        // unrelated controls are retained where they are valid.
        ChannelSettings entered;if(collect(entered))draft=entered;populate();select(true);path->setText(db.path);path->setModified(false);path->setToolTip(db.path);
    };
    showSettings();populate();if(!db.ecus.isEmpty())select(false);
    QObject::connect(browse,&QPushButton::clicked,&dialog,[&]{const auto name=QFileDialog::getOpenFileName(&dialog,"选择 CDD",path->text(),"CANdela (*.cdd *.CDD)");if(!name.isEmpty()){path->setText(name);load();}});
    QObject::connect(path,&QLineEdit::editingFinished,&dialog,[&]{if(path->isModified())load();});
    QObject::connect(path,&QLineEdit::returnPressed,&dialog,load);
    QObject::connect(ecu,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[&]{if(!loading){fillVariants();select(true);}});
    QObject::connect(variant,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[&]{if(!loading)select(true);});
    QObject::connect(ecu->lineEdit(),&QLineEdit::editingFinished,&dialog,[&]{if(!loading&&selected(ecu)!=draft.cddEcu){fillVariants();select(true);}});
    QObject::connect(variant->lineEdit(),&QLineEdit::editingFinished,&dialog,[&]{if(!loading&&selected(variant)!=draft.cddVariant)select(true);});
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{if(path->text().trimmed()!=db.path){load();if(!targetValid||path->text().trimmed()!=db.path)return;}if(!targetValid)return;if(!db.ecus.isEmpty())select(false);if(!targetValid)return;ChannelSettings result;if(!collect(result))return;QString message;
        if(!vm->applyDiagnosticConfiguration(db,result,message)){error->setText(message);return;}dialog.accept();});
    for(auto b:dialog.findChildren<QPushButton*>())b->setAutoDefault(false);
    dialog.exec();
}
}
