#pragma once
#include <QTreeView>
#include <QItemSelectionModel>
#include <QHash>
#include <QStyleOptionViewItem>

namespace host {
class FrameTableView : public QTreeView {
public:
    using QTreeView::QTreeView;
    void setFrozenStripes(bool enabled,int rowKeyRole) {
        if(m_frozenStripes==enabled)return;
        m_frozenStripes=enabled;
        m_rowKeyRole=rowKeyRole;
        m_stripes.clear();
        m_nextAlternate=false;
        // drawRow supplies the same stripe flag to the branch and the cells.
        setAlternatingRowColors(!enabled);
    }
    void setModel(QAbstractItemModel *model) override {
        disconnect(m_aboutToRemove);disconnect(m_removed);
        disconnect(m_inserted);disconnect(m_reset);
        m_stripes.clear();m_nextAlternate=false;
        m_currentRemoved=false;
        if(model)m_aboutToRemove=connect(model,&QAbstractItemModel::rowsAboutToBeRemoved,this,
            [this](const QModelIndex &parent,int first,int last){
                if(m_frozenStripes&&!parent.isValid())
                    for(int row=first;row<=last;++row)m_stripes.remove(rowKey(row));
                const auto current=currentIndex();
                m_currentRemoved=current.isValid()&&current.parent()==parent&&current.row()>=first&&current.row()<=last;
            });
        QTreeView::setModel(model);
        if(!model)return;
        m_inserted=connect(model,&QAbstractItemModel::rowsInserted,this,[this](const QModelIndex &parent,int first,int last){
            if(!m_frozenStripes||parent.isValid())return;
            for(int row=first;row<=last;++row){
                const auto key=rowKey(row);
                if(!m_stripes.contains(key))m_stripes.insert(key,nextStripe());
            }
        });
        m_reset=connect(model,&QAbstractItemModel::modelReset,this,[this]{refreshStripes();});
        refreshStripes();
        m_removed=connect(model,&QAbstractItemModel::rowsRemoved,this,
            [this](const QModelIndex&,int,int){
                if(m_currentRemoved&&selectionModel())selectionModel()->setCurrentIndex(QModelIndex(),QItemSelectionModel::NoUpdate);
                m_currentRemoved=false;
            });
    }
protected:
    void drawRow(QPainter *painter,const QStyleOptionViewItem &option,const QModelIndex &index) const override {
        if(!m_frozenStripes){QTreeView::drawRow(painter,option,index);return;}
        QStyleOptionViewItem striped(option);
        const bool alternate=index.parent().isValid()?bool(index.row()&1):m_stripes.value(index.data(m_rowKeyRole).toString());
        striped.features.setFlag(QStyleOptionViewItem::Alternate,alternate);
        QTreeView::drawRow(painter,striped,index);
    }
private:
    QString rowKey(int row) const {return model()->index(row,0).data(m_rowKeyRole).toString();}
    bool nextStripe(){
        const bool alternate=m_nextAlternate;
        m_nextAlternate=!m_nextAlternate;
        return alternate;
    }
    void refreshStripes(){
        if(!m_frozenStripes)return;
        QHash<QString,bool> stripes;
        const int count=model()->rowCount();
        stripes.reserve(count);
        for(int row=0;row<count;++row){
            const auto key=rowKey(row);
            const auto prior=m_stripes.constFind(key);
            stripes.insert(key,prior==m_stripes.cend()?nextStripe():prior.value());
        }
        m_stripes.swap(stripes);
        if(m_stripes.isEmpty())m_nextAlternate=false;
    }
    bool m_currentRemoved=false,m_frozenStripes=false,m_nextAlternate=false;
    int m_rowKeyRole=Qt::UserRole;
    QHash<QString,bool> m_stripes;
    QMetaObject::Connection m_aboutToRemove,m_removed,m_inserted,m_reset;
};
}
