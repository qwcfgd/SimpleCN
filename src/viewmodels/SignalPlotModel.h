#pragma once
#include <QAbstractTableModel>
#include <QColor>
#include <QSet>
#include <deque>
#include "domain/HostTypes.h"
namespace host {
class SignalTransmitViewModel;
class SignalPlotModel : public QAbstractTableModel {
    Q_OBJECT
public:
    struct Point {qint64 us=0;double value=0;QString exact,unit;bool valid=false;};
    struct Series {
        QString key,frameKey,name,frameName,unit,raw,physical,status,group,groupName;
        QMap<double,QString> labels;
        quint32 id=0;bool extended=false;int field=-1;
        QColor color;bool marked=false;
        std::deque<Point> points;
    };
    struct Reading {bool valid=false;double value=0;QString text,unit;};
    explicit SignalPlotModel(SignalTransmitViewModel *,QObject *parent=nullptr);
    int rowCount(const QModelIndex &p={}) const override {return p.isValid()?0:m_series.size();}
    int columnCount(const QModelIndex &p={}) const override {return p.isValid()?0:8;}
    QVariant data(const QModelIndex &,int role=Qt::DisplayRole) const override;
    QVariant headerData(int,Qt::Orientation,int role=Qt::DisplayRole) const override;
    bool addSignal(const QString &frameKey,const QString &name);
    bool removeRows(int,int,const QModelIndex & ={}) override;
    void setColor(int,QColor);
    void mark(const QSet<int>&,int active);
    void setCursors(bool,bool,qint64,qint64);
    Reading valueAt(int,qint64) const;
    QString differenceAt(int,qint64,qint64) const;
    const QVector<Series> &series() const {return m_series;}
    int activeRow() const {return m_active;}
    quint64 revision() const {return m_revision;}
    void append(const FrameBatch &);
    void clearSamples();
    void setCapacity(int n);
    int capacity() const {return m_capacity;}
    void refresh();
    void saveConfiguration();void restoreConfiguration();
    bool setOrder(const QStringList&,const QMap<QString,QString>&);
    void createGroup(const QSet<int>&,const QString&);void dissolveGroup(const QString&);void renameGroup(const QString&,const QString&);
    int representative(int)const;
    struct RenderPoint{Point point;bool breakBefore=false;};
    QVector<RenderPoint> renderPoints(int,qint64,qint64,int limit=1000)const;
signals:
    void selectionChanged();
    void structureChanged();
private:
    void rebind();
    void ingest(const FrameBatch &,int onlyRow=-1,bool replay=false);
    SignalTransmitViewModel *m_vm;
    signal::Database m_database;
    QHash<QString,int> m_frameLookup;
    QVector<Series> m_series;
    QHash<QString,QVector<FrameRecord>> m_pending;
    int m_active=-1,m_capacity=100000;
    quint64 m_revision=0;
    bool m_cursor=false,m_difference=false,m_restoring=false;
    qint64 m_t1=0,m_t2=1000000;
};
}
