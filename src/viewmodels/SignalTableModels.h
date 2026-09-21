#pragma once
#include "viewmodels/SignalTransmitViewModel.h"
#include <QAbstractTableModel>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
namespace host {
class SignalValueTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum { EnumOptionsRole=Qt::UserRole+1, EnumValueRole, PhysicalEditableRole, EnumRawRole };
    SignalValueTableModel(SignalTransmitViewModel*,QObject*parent=nullptr);
    void setFrame(const QString&);
    QString frameKey()const{return m_key;}
    int rowCount(const QModelIndex&parent=QModelIndex())const override;
    int columnCount(const QModelIndex&parent=QModelIndex())const override{return parent.isValid()?0:7;}
    QVariant data(const QModelIndex&,int role=Qt::DisplayRole)const override;
    QVariant headerData(int,Qt::Orientation,int role=Qt::DisplayRole)const override;
    Qt::ItemFlags flags(const QModelIndex&)const override;
    bool setData(const QModelIndex&,const QVariant&,int role=Qt::EditRole)override;
signals:void validation(QString);
private:SignalTransmitViewModel*m_vm;QString m_key;
};
class SignalValueDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget*,const QStyleOptionViewItem&,const QModelIndex&)const override;
    void setEditorData(QWidget*,const QModelIndex&)const override;
    void setModelData(QWidget*,QAbstractItemModel*,const QModelIndex&)const override;
};
class CanTxTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit CanTxTableModel(SignalTransmitViewModel*,QObject*parent=nullptr);
    QString key(int row)const;
    int rowCount(const QModelIndex&p=QModelIndex())const override{return p.isValid()?0:m_definitions.size();}
    int columnCount(const QModelIndex&p=QModelIndex())const override{return p.isValid()?0:9;}
    QVariant data(const QModelIndex&,int role=Qt::DisplayRole)const override;
    QVariant headerData(int,Qt::Orientation,int role=Qt::DisplayRole)const override;
    Qt::ItemFlags flags(const QModelIndex&)const override;
    bool setData(const QModelIndex&,const QVariant&,int role=Qt::EditRole)override;
signals:void validation(QString);
private:SignalTransmitViewModel*m_vm;QVector<signal::FrameDefinition>m_definitions;
};
class SignalTreeModel : public QStandardItemModel {
    Q_OBJECT
public:explicit SignalTreeModel(SignalTransmitViewModel*,QObject*parent=nullptr);
private:void rebuild();SignalTransmitViewModel*m_vm;
};
class LinScheduleTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit LinScheduleTableModel(SignalTransmitViewModel*,QObject*parent=nullptr);
    QString key(int row)const;
    int rowCount(const QModelIndex&p=QModelIndex())const override;
    int columnCount(const QModelIndex&p=QModelIndex())const override{return p.isValid()?0:9;}
    QVariant data(const QModelIndex&,int role=Qt::DisplayRole)const override;
    QVariant headerData(int,Qt::Orientation,int role=Qt::DisplayRole)const override;
    Qt::ItemFlags flags(const QModelIndex&)const override;
    bool setData(const QModelIndex&,const QVariant&,int role=Qt::EditRole)override;
signals:void validation(QString);
private:const signal::Schedule*schedule()const;SignalTransmitViewModel*m_vm;QString m_schedule;
};
}
