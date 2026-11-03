#include "TransmitFrameDelegate.h"
#include "viewmodels/SignalTableModels.h"
#include <QLineEdit>
#include <QPersistentModelIndex>
#include <QRegularExpression>
#include <QSpinBox>
#include <QValidator>
namespace host {
namespace {
class PayloadValidator : public QValidator {
public:
    PayloadValidator(int length,QObject*parent):QValidator(parent),m_length(length){}
    State validate(QString&text,int&)const override{
        auto hex=text;hex.remove(QRegularExpression("\\s"));
        if(hex.size()>m_length*2||hex.contains(QRegularExpression("[^0-9a-fA-F]")))return Invalid;
        return hex.size()%2?Intermediate:Acceptable;
    }
private:int m_length;
};
class PayloadEditor : public QLineEdit {
public:
    PayloadEditor(QWidget*parent,const QModelIndex&index):QLineEdit(parent),m_index(index){
        setObjectName("transmitPayloadEditor");m_length=index.data(TransmitTableRoles::PayloadLengthRole).toInt();
        setValidator(new PayloadValidator(m_length,this));
        connect(this,&QLineEdit::textEdited,this,[this](const QString&text){
            auto hex=text;hex.remove(QRegularExpression("\\s"));
            // Publish complete frames while the editor remains open. Partial input
            // stays local until Enter/Tab/focus loss confirms zero padding.
            if(hex.size()==m_length*2&&hasAcceptableInput())submit();
        });
    }
    void load(const QModelIndex&index){
        if(m_loaded)return;m_loaded=true;setText(index.data(Qt::EditRole).toString());selectAll();
    }
    void submit(){
        if(!m_index.isValid()||!isModified()||!hasAcceptableInput())return;
        // A model notification may synchronously call load(). The session buffer
        // remains owned by this editor until the transaction ends.
        if(const_cast<QAbstractItemModel*>(m_index.model())->setData(m_index,text(),Qt::EditRole))setModified(false);
    }
private:QPersistentModelIndex m_index;int m_length=0;bool m_loaded=false;
};
class DlcEditor : public QSpinBox {
public:
    DlcEditor(QWidget*parent,const QModelIndex&index):QSpinBox(parent){
        setObjectName("transmitDlcEditor");setRange(index.data(TransmitTableRoles::DlcMinimumRole).toInt(),8);
    }
    void load(const QModelIndex&index){if(m_loaded)return;m_loaded=true;setValue(index.data(Qt::EditRole).toInt());selectAll();}
private:bool m_loaded=false;
};
}
QWidget*TransmitFrameDelegate::createEditor(QWidget*parent,const QStyleOptionViewItem&option,const QModelIndex&index)const{
    if(index.data(TransmitTableRoles::PayloadLengthRole).isValid())return new PayloadEditor(parent,index);
    if(index.data(TransmitTableRoles::DlcMinimumRole).isValid())return new DlcEditor(parent,index);
    return QStyledItemDelegate::createEditor(parent,option,index);
}
void TransmitFrameDelegate::setEditorData(QWidget*editor,const QModelIndex&index)const{
    if(auto*payload=dynamic_cast<PayloadEditor*>(editor)){payload->load(index);return;}
    if(auto*dlc=dynamic_cast<DlcEditor*>(editor)){dlc->load(index);return;}
    QStyledItemDelegate::setEditorData(editor,index);
}
void TransmitFrameDelegate::setModelData(QWidget*editor,QAbstractItemModel*model,const QModelIndex&index)const{
    if(auto*payload=dynamic_cast<PayloadEditor*>(editor)){payload->submit();return;}
    QStyledItemDelegate::setModelData(editor,model,index);
}
}
