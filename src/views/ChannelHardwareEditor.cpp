#include "ChannelHardwareEditor.h"
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QFormLayout>
#include <QSignalBlocker>
#include <QScopedValueRollback>
namespace host {
ChannelHardwareEditor::ChannelHardwareEditor(ChannelViewModel*vm,QWidget*parent):QWidget(parent),m_vm(vm){
    setObjectName("channelHardwareEditor");auto*form=new QFormLayout(this);form->setContentsMargins(0,0,0,0);
    m_mode=new QComboBox;m_mode->setObjectName("modeCombo");m_mode->addItems({"在线硬件","模拟模式"});
    m_hardware=new QComboBox;m_hardware->setObjectName("hardwareCombo");
    m_software=new QComboBox;m_software->setObjectName("softwareChannelCombo");
    m_bitrate=new QComboBox;m_bitrate->setObjectName("bitrateCombo");
    const auto rates=vm->settings().bus==communication::Bus::Lin?QList<int>{19200,9600,10400,20000,2400,4800}:QList<int>{1000000,800000,500000,250000,125000,100000,50000,20000,10000,5000};
    for(int rate:rates)m_bitrate->addItem(QString::number(rate)+" bit/s",rate);
    m_reconnect=new QCheckBox("自动重连");m_reconnect->setObjectName("autoReconnect");
    m_refresh=new QPushButton("刷新硬件");m_refresh->setObjectName("refreshButton");
    form->addRow("运行模式",m_mode);form->addRow("硬件选择",m_hardware);form->addRow("硬件通道",m_software);form->addRow("波特率",m_bitrate);form->addRow(m_reconnect);form->addRow(m_refresh);
    loadSettings();render();
    connect(vm,&ChannelViewModel::settingsChanged,this,[this]{if(!m_applying)loadSettings();});
    connect(vm,&ChannelViewModel::hardwareListChanged,this,[this]{refreshHardware();});
    connect(vm,&ChannelViewModel::changed,this,[this]{render();});
    connect(m_refresh,&QPushButton::clicked,vm,&ChannelViewModel::refresh);
    for(auto*c:{m_mode,m_software,m_bitrate})connect(c,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{applyForm();});
    connect(m_reconnect,&QCheckBox::toggled,this,[this]{applyForm();});
    connect(m_hardware,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{if(!m_loading&&!m_refreshing)refreshPorts(true);});
}
void ChannelHardwareEditor::loadSettings(){
    QScopedValueRollback<bool>guard(m_loading,true);const auto&s=m_vm->settings();m_mode->setCurrentIndex(s.simulation?1:0);
    int rate=m_bitrate->findData(s.bitrate);if(rate<0){m_bitrate->addItem(QString::number(s.bitrate)+" bit/s",s.bitrate);rate=m_bitrate->count()-1;}m_bitrate->setCurrentIndex(rate);m_reconnect->setChecked(s.autoReconnect);refreshHardware();
}
void ChannelHardwareEditor::applyForm(){
    if(m_loading||m_applying||m_externalLocked||m_vm->busy())return;
    auto s=m_vm->settings();const bool simulation=m_mode->currentIndex()==1;
    // The port combo still contains the previous backend until enumeration returns.
    if(s.simulation!=simulation){s.hardwareKey.clear();s.handle=0;}
    else{s.hardwareKey=m_software->currentData().toString();s.handle=m_software->currentData(Qt::UserRole+1).toUInt();}
    s.simulation=simulation;s.bitrate=m_bitrate->currentData().toInt();s.autoReconnect=m_reconnect->isChecked();
    QScopedValueRollback<bool>guard(m_applying,true);m_vm->setSettings(s);
}
void ChannelHardwareEditor::addConnectionControl(QPushButton*button){auto*form=qobject_cast<QFormLayout*>(layout());form->insertRow(form->rowCount()-1,button);}
void ChannelHardwareEditor::render(){const bool editable=!m_externalLocked&&!m_vm->hardwareLocked();for(auto*w:QList<QWidget*>{m_mode,m_hardware,m_software,m_bitrate,m_reconnect,m_refresh})w->setEnabled(editable);}
void ChannelHardwareEditor::refreshHardware() {
    if(m_refreshing)return;QScopedValueRollback<bool> guard(m_refreshing,true);
    QSignalBlocker blocker(m_hardware);const auto &s=m_vm->settings();
    const QString prior=m_hardware->currentData().toString();
    QString selected;QSet<QString> devices;
    m_hardware->clear();
    for(const auto &h:m_vm->hardware()){
        const auto device=hardwareDeviceKey(h);
        if(!devices.contains(device)){
            devices.insert(device);m_hardware->addItem(h.label.section(" / SDK channel",0,0).section(" / channel",0,0),device);
            m_hardware->setItemData(m_hardware->count()-1,h.label+" · "+device,Qt::ToolTipRole);
        }
        if(h.key==s.hardwareKey)selected=device;
    }
    if(selected.isEmpty() && !s.hardwareKey.isEmpty()) {
        // Keep an unplugged selection visible; never silently bind a different adapter.
        selected=s.hardwareKey.section(':',0,-2);
        if(!devices.contains(selected))m_hardware->addItem("已选设备未连接",selected);
    }
    if(selected.isEmpty() && devices.contains(prior))selected=prior;
    if(selected.isEmpty() && devices.size()==1)selected=m_hardware->itemData(0).toString();
    if(selected.isEmpty()){m_hardware->insertItem(0,devices.isEmpty()?"未发现硬件":"请选择硬件",QString());m_hardware->setCurrentIndex(0);}
    else m_hardware->setCurrentIndex(m_hardware->findData(selected));
    m_hardware->setToolTip(m_hardware->currentText());refreshPorts();
}
void ChannelHardwareEditor::refreshPorts(bool deviceChanged) {
    QSignalBlocker blocker(m_software);const auto s=m_vm->settings();
    const QString device=m_hardware->currentData().toString();int chosen=-1;
    m_software->clear();
    for(const auto &h:m_vm->hardware()) {
        if(hardwareDeviceKey(h)!=device)continue;
        const bool own=(m_externalLocked || m_vm->hardwareLocked() || m_vm->communicationIndicator()==2) && h.key==s.hardwareKey;
        if(!h.available && !own)continue;
        m_software->addItem(QString("通道 %1").arg(h.controller)+(own && !h.available && !m_vm->connected()?" · 已占用":""),h.key);
        const int row=m_software->count()-1;m_software->setItemData(row,h.handle,Qt::UserRole+1);
        m_software->setItemData(row,QString("SDK 通道 %1 · 句柄 0x%2").arg(h.controller).arg(h.handle,0,16),Qt::ToolTipRole);
        if(!deviceChanged && h.key==s.hardwareKey)chosen=row;
    }
    if(!deviceChanged && chosen<0 && !s.hardwareKey.isEmpty()){
        m_software->addItem("原通道 · 未连接",s.hardwareKey);chosen=m_software->count()-1;
        m_software->setItemData(chosen,s.handle,Qt::UserRole+1);
    }
    if(!m_software->count()){m_software->addItem(device.isEmpty()?"请先选择硬件":"无剩余可用通道",QString());chosen=0;}
    m_software->setCurrentIndex(chosen<0?0:chosen);
    if(!m_vm->hardwareLocked() && (deviceChanged || m_vm->communicationIndicator()!=2))applyForm();
    render();
}
}
