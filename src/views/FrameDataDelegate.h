#pragma once
#include <QStyledItemDelegate>
#include <QPainter>
#include <QApplication>
#include <QFontDatabase>
#include "model/FrameTableModel.h"
namespace host {
class FrameDataDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &o,const QModelIndex &i) const override {
        auto s=QStyledItemDelegate::sizeHint(o,i);s.setHeight(qMax(27,s.height()));return s;
    }
    void paint(QPainter *p,const QStyleOptionViewItem &option,const QModelIndex &i) const override {
        const auto ages=i.data(FrameTableModel::NibbleAgeRole).toList();
        if(ages.isEmpty()){QStyledItemDelegate::paint(p,option,i);return;}
        QStyleOptionViewItem o(option);initStyleOption(&o,i);const auto text=o.text;o.text.clear();
        auto style=o.widget?o.widget->style():QApplication::style();style->drawControl(QStyle::CE_ItemViewItem,&o,p,o.widget);
        p->save();p->setClipRect(o.rect);p->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        const QFontMetrics fm(p->font());
        int x=o.rect.x()+4,n=0;const int y=o.rect.y()+(o.rect.height()+fm.ascent()-fm.descent())/2;
        const QColor normal=(o.state&QStyle::State_Selected)?o.palette.color(QPalette::HighlightedText):i.data(Qt::ForegroundRole).value<QColor>();
        const QColor gray=(o.state&QStyle::State_Selected)?QColor("#B9C3CE"):QColor("#969DA5");
        for(auto c:text){QColor color=normal;if(!c.isSpace()){
                double a=qBound(0,ages.value(n++).toInt(),10)/10.0;
                color=QColor(qRound(normal.red()*(1-a)+gray.red()*a),qRound(normal.green()*(1-a)+gray.green()*a),qRound(normal.blue()*(1-a)+gray.blue()*a));
            }p->setPen(color);p->drawText(x,y,QString(c));x+=fm.horizontalAdvance(c);}
        p->restore();
    }
};
}
