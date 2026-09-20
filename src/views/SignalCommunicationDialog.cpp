#include "SignalTransmitPage.h"
#include "viewmodels/SignalTableModels.h"
#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QTableView>
#include <QHeaderView>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QSignalBlocker>
namespace host {
void openSignalCommunicationDialog(SignalTransmitViewModel*vm,const QString&key,QWidget*parent){
    QDialog dialog(parent);dialog.setObjectName("signalCommunicationDialog");dialog.setWindowTitle("信号通信配置");dialog.resize(1060,520);auto*layout=new QVBoxLayout(&dialog);
    auto*description=new QLabel("默认值为信号有效位全 1；文件初值仅覆盖使用值。使用值与主页面共用同一个发送模型。");description->setWordWrap(true);description->setTextFormat(Qt::PlainText);layout->addWidget(description);
    auto*selector=new QComboBox;selector->setObjectName("signalInitialFrame");layout->addWidget(selector);auto*model=new SignalValueTableModel(vm,true,&dialog);auto*table=new QTableView;table->setObjectName("signalInitialValues");table->setModel(model);table->setItemDelegateForColumn(3,new SignalValueDelegate(table));table->setAlternatingRowColors(true);table->horizontalHeader()->setStretchLastSection(true);table->setColumnWidth(0,160);table->setColumnWidth(1,160);table->setColumnWidth(2,160);layout->addWidget(table,1);
    auto*result=new QLabel;result->setTextFormat(Qt::PlainText);result->setWordWrap(true);layout->addWidget(result);
    auto*actions=new QHBoxLayout;auto*save=new QPushButton("保存通信配置…");save->setObjectName("signalSaveConfiguration");auto*read=new QPushButton("读取通信配置…");read->setObjectName("signalReadConfiguration");actions->addWidget(save);actions->addWidget(read);actions->addStretch();layout->addLayout(actions);
    auto refresh=[&]{const auto selected=selector->currentData().toString();QSignalBlocker block(selector);selector->clear();for(const auto&f:vm->definitions())selector->addItem(f.name,f.key);selector->setCurrentIndex(qMax(0,selector->findData(selected.isEmpty()?key:selected)));model->setFrame(selector->currentData().toString());};refresh();
    QObject::connect(selector,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[&]{model->setFrame(selector->currentData().toString());});
    QObject::connect(vm,&SignalTransmitViewModel::structureChanged,&dialog,refresh);QObject::connect(vm,&SignalTransmitViewModel::changed,&dialog,[&]{read->setEnabled(vm->canStructure());});read->setEnabled(vm->canStructure());
    QObject::connect(model,&SignalValueTableModel::validation,result,&QLabel::setText);
    QObject::connect(save,&QPushButton::clicked,&dialog,[&]{const auto path=QFileDialog::getSaveFileName(&dialog,"保存通信配置","signal-communication.json","JSON (*.json)");if(path.isEmpty())return;QString error;if(vm->save(path,error))result->setText(error.isEmpty()?"保存完成；配置基准不变":error);else result->setText(error);});
    QObject::connect(vm,&SignalTransmitViewModel::notice,result,&QLabel::setText);
    QObject::connect(read,&QPushButton::clicked,&dialog,[&]{const auto path=QFileDialog::getOpenFileName(&dialog,"读取通信配置",{},"JSON (*.json)");if(path.isEmpty())return;vm->readAsync(path);result->setText(vm->message());});
    auto*close=new QDialogButtonBox(QDialogButtonBox::Close);layout->addWidget(close);QObject::connect(close,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);dialog.exec();
}
}
