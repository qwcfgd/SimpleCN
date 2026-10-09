#include "SignalValueDelegate.h"
#include "viewmodels/SignalTableModels.h"
#include <QComboBox>
#include <QCompleter>
namespace host {
QWidget*SignalValueDelegate::createEditor(QWidget*parent,const QStyleOptionViewItem&option,const QModelIndex&i)const{
    const auto values=i.data(SignalValueTableModel::EnumOptionsRole).toList();
    if(i.column()!=3||values.isEmpty())return QStyledItemDelegate::createEditor(parent,option,i);
    auto*combo=new QComboBox(parent);combo->setObjectName("signalEnumEditor");combo->setEditable(true);combo->setInsertPolicy(QComboBox::NoInsert);
    combo->setProperty("physicalEditable",i.data(SignalValueTableModel::PhysicalEditableRole));
    for(const auto&v:values){const auto entry=v.toMap();combo->addItem(entry["text"].toString(),entry["raw"]);}
    combo->completer()->setCompletionMode(QCompleter::InlineCompletion);combo->completer()->setCaseSensitivity(Qt::CaseInsensitive);
    auto*self=const_cast<SignalValueDelegate*>(this);connect(combo,qOverload<int>(&QComboBox::activated),self,[self,combo]{emit self->commitData(combo);emit self->closeEditor(combo);});return combo;
}
void SignalValueDelegate::setEditorData(QWidget*editor,const QModelIndex&i)const{
    // A live RX/status refresh must never replace an in-progress draft. Each
    // editor gets its initial value once; committing still goes through setData.
    if(editor->property("signalDraftInitialized").toBool())return;
    editor->setProperty("signalDraftInitialized",true);
    auto*combo=qobject_cast<QComboBox*>(editor);if(!combo){QStyledItemDelegate::setEditorData(editor,i);return;}
    const int selected=combo->findData(i.data(SignalValueTableModel::EnumValueRole));
    if(selected>=0)combo->setCurrentIndex(selected);else if(combo->isEditable())combo->setEditText(i.data(Qt::EditRole).toString());else{combo->addItem(i.data().toString());combo->setCurrentIndex(combo->count()-1);}
}
void SignalValueDelegate::setModelData(QWidget*editor,QAbstractItemModel*model,const QModelIndex&i)const{
    auto*combo=qobject_cast<QComboBox*>(editor);if(!combo){QStyledItemDelegate::setModelData(editor,model,i);return;}
    int selected=combo->findText(combo->currentText(),Qt::MatchFixedString);
    if(selected<0)for(int n=0;n<combo->count();++n){const auto text=combo->itemText(n);if(text.left(text.lastIndexOf(" (")).compare(combo->currentText(),Qt::CaseInsensitive)==0){selected=n;break;}}
    if(selected>=0)model->setData(i,combo->itemData(selected),SignalValueTableModel::EnumValueRole);
    else model->setData(i,combo->currentText(),combo->property("physicalEditable").toBool()?int(Qt::EditRole):int(SignalValueTableModel::EnumRawRole));
}
}
