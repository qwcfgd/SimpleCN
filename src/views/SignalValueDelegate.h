#pragma once
#include <QStyledItemDelegate>
namespace host {
class SignalValueDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget*,const QStyleOptionViewItem&,const QModelIndex&)const override;
    void setEditorData(QWidget*,const QModelIndex&)const override;
    void setModelData(QWidget*,QAbstractItemModel*,const QModelIndex&)const override;
};
}
