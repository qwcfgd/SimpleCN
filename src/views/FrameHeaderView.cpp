#include "FrameHeaderView.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionHeader>
namespace host {
FrameHeaderView::FrameHeaderView(QWidget*parent):QHeaderView(Qt::Horizontal,parent){setObjectName("frameColumnHeader");setSectionsClickable(true);setSectionsMovable(true);setMouseTracking(true);viewport()->setMouseTracking(true);setMinimumSectionSize(64);setSortIndicatorShown(true);}
void FrameHeaderView::setFilteredColumns(const QSet<int>&columns){m_filtered=columns;viewport()->update();}
void FrameHeaderView::setOpenColumn(int column){m_open=column;viewport()->update();}
QRect FrameHeaderView::filterButtonRect(int column)const{return QRect(sectionViewportPosition(column)+sectionSize(column)-24,2,20,qMax(0,height()-4));}
bool FrameHeaderView::filterButtonVisible(int column)const{return !isSectionHidden(column)&&(m_hover==column||m_open==column||m_filtered.contains(column));}
void FrameHeaderView::paintSection(QPainter*p,const QRect&rect,int column)const{
    if(!rect.isValid())return;QStyleOptionHeader option;initStyleOption(&option);option.rect=rect;option.section=column;option.text.clear();option.sortIndicator=QStyleOptionHeader::None;
    if(m_hover==column)option.state|=QStyle::State_MouseOver;style()->drawControl(QStyle::CE_Header,&option,p,this);
    p->save();p->setClipRect(rect);p->setPen(palette().color(QPalette::ButtonText));
    const int right=rect.right()-26;auto textRect=rect.adjusted(5,0,-27,0);
    const bool sorted=isSortIndicatorShown()&&sortIndicatorSection()==column;if(sorted)textRect.setRight(textRect.right()-12);
    const auto text=model()?model()->headerData(column,Qt::Horizontal).toString():QString();p->drawText(textRect,Qt::AlignLeft|Qt::AlignVCenter,fontMetrics().elidedText(text,Qt::ElideRight,qMax(0,textRect.width())));
    if(sorted){const int x=right-5,y=rect.center().y();QPolygon arrow;
        if(sortIndicatorOrder()==Qt::AscendingOrder)arrow<<QPoint(x-4,y+3)<<QPoint(x+4,y+3)<<QPoint(x,y-3);
        else arrow<<QPoint(x-4,y-3)<<QPoint(x+4,y-3)<<QPoint(x,y+3);
        p->setBrush(p->pen().color());p->drawPolygon(arrow);
    }
    if(filterButtonVisible(column)){const auto button=filterButtonRect(column);if(m_hover==column||m_open==column)p->fillRect(button,palette().color(QPalette::Midlight));
        const auto center=button.center();p->setPen(m_filtered.contains(column)?QColor("#087F99"):palette().color(QPalette::ButtonText));p->setBrush(p->pen().color());
        if(m_filtered.contains(column)){QPainterPath funnel;funnel.moveTo(center.x()-5,center.y()-4);funnel.lineTo(center.x()+5,center.y()-4);funnel.lineTo(center.x()+1,center.y());funnel.lineTo(center.x()+1,center.y()+5);funnel.lineTo(center.x()-1,center.y()+4);funnel.lineTo(center.x()-1,center.y());funnel.closeSubpath();p->drawPath(funnel);}
        else p->drawPolygon(QPolygon()<<QPoint(center.x()-4,center.y()-2)<<QPoint(center.x()+4,center.y()-2)<<QPoint(center.x(),center.y()+3));
    }p->restore();
}
void FrameHeaderView::mouseMoveEvent(QMouseEvent*event){const int column=logicalIndexAt(event->pos());if(column!=m_hover){m_hover=column;viewport()->update();}QHeaderView::mouseMoveEvent(event);}
void FrameHeaderView::leaveEvent(QEvent*event){m_hover=-1;viewport()->update();QHeaderView::leaveEvent(event);}
void FrameHeaderView::mousePressEvent(QMouseEvent*event){
    const int column=logicalIndexAt(event->pos());if(event->button()==Qt::LeftButton&&column>=0&&filterButtonVisible(column)&&filterButtonRect(column).contains(event->pos())){m_filterPressed=true;event->accept();return;}QHeaderView::mousePressEvent(event);
}
void FrameHeaderView::mouseReleaseEvent(QMouseEvent*event){
    if(m_filterPressed){m_filterPressed=false;const int column=logicalIndexAt(event->pos());if(column>=0&&filterButtonRect(column).contains(event->pos()))emit filterRequested(column,viewport()->mapToGlobal(QPoint(sectionViewportPosition(column),height())));event->accept();return;}QHeaderView::mouseReleaseEvent(event);
}
}
