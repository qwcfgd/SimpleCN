#pragma once
#include "localization/Language.h"
#include "CddDatabase.h"
#include <QAbstractTableModel>
#include <QColor>
namespace host {
class UdsServiceTableModel final : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit UdsServiceTableModel(QObject *parent=nullptr):QAbstractTableModel(parent){}
    void setServices(QVector<diag::Service> services){beginResetModel();m_services=std::move(services);endResetModel();}
    const diag::Service *service(int row) const {return row>=0&&row<m_services.size()?&m_services[row]:nullptr;}
    int rowCount(const QModelIndex &parent={}) const override{return parent.isValid()?0:m_services.size();}
    int columnCount(const QModelIndex &parent={}) const override{return parent.isValid()?0:5;}
    QVariant headerData(int section,Qt::Orientation orientation,int role) const override {
        if(orientation!=Qt::Horizontal||role!=Qt::DisplayRole)return {};
        return Language::text(QStringList{"SID","服务 / 实例","子功能 / DID / RID","参数","状态"}.value(section));
    }
    QVariant data(const QModelIndex &i,int role) const override {
        auto s=service(i.row());if(!i.isValid()||!s)return {};
        if(role==Qt::ToolTipRole)return s->name+"\n"+s->group+"\n"+(s->issue.isEmpty()?s->conditions:s->issue);
        if(role==Qt::ForegroundRole&&!s->issue.isEmpty())return QColor("#A33A2B");
        if(role!=Qt::DisplayRole)return {};
        int count=0;for(const auto &f:s->request.fields)if(!f.constant)++count;
        switch(i.column()){
        case 0:return s->sid<0?QString("?"):QString("0x%1").arg(s->sid,2,16,QChar('0')).toUpper();
        case 1:return s->name;case 2:return s->selector();case 3:return count?Language::text("%1 项").arg(count):Language::text("无需输入");
        case 4:return Language::text(!s->issue.isEmpty()?QString("定义不完整"):(s->physical?QString("可配置"):QString("仅功能寻址")));default:return {};
        }
    }
private: QVector<diag::Service> m_services;
};
}
