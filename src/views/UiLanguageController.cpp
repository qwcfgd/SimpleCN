#include "UiLanguageController.h"
#include "localization/Language.h"
#include <QApplication>
#include <QAbstractButton>
#include <QAction>
#include <QComboBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QStatusBar>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTableWidget>
#include <QTreeWidget>
#include <QTimer>
#include <QEvent>
#include <QScrollBar>
#include <QTextCursor>
namespace host {
UiLanguageController::UiLanguageController():QObject(qApp){
    qApp->installEventFilter(this);
    connect(&Language::instance(),&Language::changed,this,[this]{refresh();});
}
UiLanguageController &UiLanguageController::instance(){static auto *controller=new UiLanguageController;return *controller;}
QString UiLanguageController::value(QObject *object,const QString &key,const QString &current){
    const auto sourceKey=("_language_source_"+key).toUtf8(),renderedKey=("_language_rendered_"+key).toUtf8();
    const auto previous=object->property(renderedKey.constData());const auto languageKey=("_language_code_"+key).toUtf8();
    if(previous.isValid()&&previous.toString()==current&&object->property(languageKey.constData()).toString()==Language::instance().code())return current;
    const QString source=previous.isValid()&&previous.toString()==current?object->property(sourceKey.constData()).toString():current;
    const auto translated=Language::text(source);
    object->setProperty(languageKey.constData(),Language::instance().code());
    if(!previous.isValid()||previous.toString()!=translated||object->property(sourceKey.constData()).toString()!=source){object->setProperty(sourceKey.constData(),source);object->setProperty(renderedKey.constData(),translated);}
    return translated;
}
void UiLanguageController::property(QObject *object,const char *name){
    const auto original=object->property(name);if(!original.isValid())return;
    const auto translated=value(object,QString::fromLatin1(name),original.toString());
    if(translated!=original.toString()){QSignalBlocker blocker(object);object->setProperty(name,translated);}
}
void UiLanguageController::translateWidget(QWidget *widget){
    if(!widget||m_updating||widget->property("preserveUserText").toBool())return;
    QScopedValueRollback<bool> guard(m_updating,true);
    for(const auto name:{"windowTitle","toolTip","statusTip","whatsThis","accessibleName","accessibleDescription"})property(widget,name);
    if(qobject_cast<QLabel*>(widget)||qobject_cast<QAbstractButton*>(widget))property(widget,"text");
    if(qobject_cast<QGroupBox*>(widget))property(widget,"title");
    if(qobject_cast<QLineEdit*>(widget)||qobject_cast<QPlainTextEdit*>(widget))property(widget,"placeholderText");
    if(qobject_cast<QProgressBar*>(widget))property(widget,"format");
    if(auto *status=qobject_cast<QStatusBar*>(widget)){const auto current=status->currentMessage(),translated=value(status,"message",current);if(current!=translated)status->showMessage(translated,10000);}
    if(auto *tabs=qobject_cast<QTabBar*>(widget)){
        // Software channel names are user data, unlike the fixed task tabs.
        const bool channels=tabs->parent()&&tabs->parent()->inherits("host::ChannelTabs");
        QSignalBlocker blocker(tabs);
        for(int i=0;i<tabs->count();++i){
            // QSignalBlocker does not suppress layout/paint invalidation.
            // Rewriting unchanged tab text from Paint can keep the UI busy.
            if(!channels){
                const auto current=tabs->tabText(i),translated=value(tabs,"tab"+QString::number(i),current);
                if(current!=translated)tabs->setTabText(i,translated);
            }
            const auto tip=tabs->tabToolTip(i),translatedTip=value(tabs,"tabTip"+QString::number(i),tip);
            if(tip!=translatedTip)tabs->setTabToolTip(i,translatedTip);
        }
    }
    if(auto *combo=qobject_cast<QComboBox*>(widget)){
        // Editable/database/node/schedule/enum selectors contain user data.
        const QSet<QString> fixed={"signalLinRole","modeCombo","hardwareCombo","softwareChannelCombo","channelType","plotDisplayMode","plotAxisMode","downloadFlow","fileTypeCombo"};
        if(fixed.contains(combo->objectName())||combo->property("translateOptions").toBool()){
            QSignalBlocker blocker(combo);for(int i=0;i<combo->count();++i)combo->setItemText(i,value(combo,"item"+QString::number(i),combo->itemText(i)));
        }
        if((combo->objectName()=="signalCanNode"||combo->property("translateFirstOption").toBool())&&combo->count()&&combo->itemData(0).toString().isEmpty()){QSignalBlocker blocker(combo);combo->setItemText(0,value(combo,"nodePrompt",combo->itemText(0)));}
    }
    for(auto *action:widget->actions()){property(action,"text");property(action,"toolTip");property(action,"statusTip");}
    if(auto *tree=qobject_cast<QTreeWidget*>(widget)){
        QSignalBlocker blocker(tree);for(int c=0;c<tree->columnCount();++c)tree->headerItem()->setText(c,value(tree,"header"+QString::number(c),tree->headerItem()->text(c)));
    }
    if(auto *table=qobject_cast<QTableWidget*>(widget)){
        QSignalBlocker blocker(table);for(int c=0;c<table->columnCount();++c)if(auto *item=table->horizontalHeaderItem(c))item->setText(value(table,"header"+QString::number(c),item->text()));
        // Download steps are application-defined, not database field names.
        const auto columns=table->property("translatedColumns").toList();
        for(int r=0;r<table->rowCount();++r)for(int c=0;c<table->columnCount();++c)if(table->property("translateCells").toBool()||columns.contains(c))if(auto *item=table->item(r,c)){
            const auto key=QString("cell%1_%2").arg(r).arg(c);item->setText(value(table,key,item->text()));item->setToolTip(value(table,key+"tip",item->toolTip()));
        }
    }
    if(auto *log=qobject_cast<QPlainTextEdit*>(widget))if(log->isReadOnly()&&log->property("translateContent").toBool()){
        const auto current=log->toPlainText(),translated=value(log,"plainText",current);
        if(current!=translated){QSignalBlocker blocker(log);const int scroll=log->verticalScrollBar()->value();const auto cursor=log->textCursor();log->setPlainText(translated);QTextCursor restored(log->document());restored.setPosition(qMin(cursor.anchor(),int(translated.size())));restored.setPosition(qMin(cursor.position(),int(translated.size())),QTextCursor::KeepAnchor);log->setTextCursor(restored);log->verticalScrollBar()->setValue(scroll);}
    }
}
void UiLanguageController::refresh(){
    QSet<QAbstractItemModel*> refreshed;
    for(auto *widget:QApplication::allWidgets()){
        translateWidget(widget);widget->update();
        // Combo popup models hold choices/user data. Emitting dataChanged on
        // them replaces an editable combo's draft with its selected item text.
        bool comboPopup=false;for(auto *parent=widget->parentWidget();parent;parent=parent->parentWidget())if(qobject_cast<QComboBox*>(parent)){comboPopup=true;break;}
        if(comboPopup)continue;
        if(auto *view=qobject_cast<QAbstractItemView*>(widget))if(auto *model=view->model())if(!refreshed.contains(model)){
            refreshed.insert(model);if(model->columnCount())emit model->headerDataChanged(Qt::Horizontal,0,model->columnCount()-1);
            if(model->rowCount()&&model->columnCount())emit model->dataChanged(model->index(0,0),model->index(model->rowCount()-1,model->columnCount()-1),{Qt::DisplayRole,Qt::ToolTipRole});
        }
    }
}
bool UiLanguageController::eventFilter(QObject *object,QEvent *event){
    if(!m_updating&&(event->type()==QEvent::Show||event->type()==QEvent::Polish||event->type()==QEvent::Paint||event->type()==QEvent::ToolTip||event->type()==QEvent::LayoutRequest)){
        if(auto *widget=qobject_cast<QWidget*>(object)){translateWidget(widget);if(event->type()==QEvent::Paint&&widget->parentWidget())translateWidget(widget->parentWidget());}
    }
    return QObject::eventFilter(object,event);
}
}
