#pragma once
#include <QAbstractItemModel>
#include <QHash>
#include "domain/HostTypes.h"
namespace host {
class FrameTableModel : public QAbstractItemModel {
    Q_OBJECT
public:
    static constexpr int Capacity=10000;
    enum { NibbleAgeRole=Qt::UserRole+1, RelativeTimeSecondsRole=Qt::UserRole+3 };
    explicit FrameTableModel(QObject *parent=nullptr):QAbstractItemModel(parent){}
    QModelIndex index(int,int,const QModelIndex &p={}) const override;
    QModelIndex parent(const QModelIndex &) const override;
    int rowCount(const QModelIndex &p={}) const override;
    int columnCount(const QModelIndex & ={}) const override {return 9;}
    QVariant data(const QModelIndex &,int role=Qt::DisplayRole) const override;
    QVariant headerData(int,Qt::Orientation,int role=Qt::DisplayRole) const override;
    bool exportCsv(const QString &,QString &error) const;
    bool exportTrace(const QString &,QString &error) const;
    qint64 startedEpochMs()const{return m_startedEpochMs;}
    bool rolling() const {return m_rolling;}
    bool paused() const {return m_paused;}
    qint64 historyCount() const {return m_history.size();}
    qint64 windowStart() const {return m_windowStart;}
    int windowSize() const {return m_windowSize;}
    const FrameBatch &history() const {return m_history;}
    void setRolling(bool);
    void setPaused(bool);
    void setHistoryStart(qint64);
    void setDatabase(signal::Database);
public slots:
    void append(const host::FrameBatch &);
    void clear();
signals:
    void recorded(host::FrameBatch);
    void cleared();
    void historyChanged();
private:
    struct Detail { quintptr token=0;QVariantList ages;QVector<QStringList> children; };
    QString key(const FrameRecord &) const;
    Detail detail(const FrameRecord &,quintptr token) const;
    void rebuild();
    bool merged()const{return m_rolling&&!m_paused;}
    FrameBatch m_rows,m_history;
    QVector<Detail> m_details;
    QHash<QString,int> m_keys;
    signal::Database m_database;
    QHash<quint64,int> m_frameLookup;
    bool m_rolling=false;
    bool m_paused=false;
    qint64 m_windowStart=0;
    int m_windowSize=0;
    quintptr m_nextToken=1;
    double m_relativeOriginUs=0;
    qint64 m_startedEpochMs=0;
    bool m_hasRelativeOrigin=false;
    double m_previousUs=0;
    bool m_hasPrevious=false;
};
}
