#pragma once
#include <QAbstractItemView>
#include <QMimeData>
#include <QPersistentModelIndex>
#include <QPointer>

namespace host {
// A drag belongs to its originating page. A model reset invalidates its rows.
class TransmitRowDrag final:public QMimeData {
public:
    TransmitRowDrag(QAbstractItemView *view,const QString &format):origin(view){
        for(const auto &row:view->selectionModel()->selectedRows())rows.append(row);
        setData(format,{});
    }
    bool valid()const{
        if(!origin||rows.isEmpty())return false;
        for(const auto &row:rows)if(!row.isValid())return false;
        return true;
    }
    QPointer<QAbstractItemView> origin;
    QList<QPersistentModelIndex> rows;
};
}
