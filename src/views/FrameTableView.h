#pragma once
#include <QTreeView>
#include <QItemSelectionModel>

namespace host {
class FrameTableView : public QTreeView {
public:
    using QTreeView::QTreeView;
    void setModel(QAbstractItemModel *model) override {
        disconnect(m_aboutToRemove);disconnect(m_removed);
        m_currentRemoved=false;
        if(model)m_aboutToRemove=connect(model,&QAbstractItemModel::rowsAboutToBeRemoved,this,
            [this](const QModelIndex &parent,int first,int last){
                const auto current=currentIndex();
                m_currentRemoved=current.isValid()&&current.parent()==parent&&current.row()>=first&&current.row()<=last;
            });
        QTreeView::setModel(model);
        if(!model)return;
        m_removed=connect(model,&QAbstractItemModel::rowsRemoved,this,
            [this](const QModelIndex&,int,int){
                if(m_currentRemoved&&selectionModel())selectionModel()->setCurrentIndex(QModelIndex(),QItemSelectionModel::NoUpdate);
                m_currentRemoved=false;
            });
    }
private:
    bool m_currentRemoved=false;
    QMetaObject::Connection m_aboutToRemove,m_removed;
};
}
