#pragma once
#include <QTabWidget>
#include <QTabBar>
#include <QToolButton>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStylePainter>
#include <QStyleOptionTab>
#include <algorithm>
namespace host {
// Horizontal labels in a vertically scrolling navigation rail with bus-specific styling.
class ChannelTabBar final : public QTabBar {
public:
    explicit ChannelTabBar(QWidget *parent=nullptr):QTabBar(parent){setObjectName("channelTabBar");setExpanding(false);setUsesScrollButtons(true);}
    int rowHeight()const{return qMax(38,fontMetrics().height()+18);}
    QSize minimumSizeHint()const override{return QSize(104,rowHeight());}
    bool isLinTab(int i)const{
        const QString d=tabData(i).toString();
        if(!d.isEmpty())return d.compare("LIN",Qt::CaseInsensitive)==0;
        if(auto *tabs=qobject_cast<const QTabWidget*>(parentWidget())){
            if(auto *w=tabs->widget(i))return w->objectName()=="linPage";
        }
        return tabText(i).startsWith("LIN",Qt::CaseInsensitive);
    }
protected:
    QSize tabSizeHint(int index)const override {Q_UNUSED(index);return QSize(104,rowHeight());}
    void mouseDoubleClickEvent(QMouseEvent*event)override{if(event->button()==Qt::LeftButton)QTabBar::mouseDoubleClickEvent(event);else event->ignore();}
    void wheelEvent(QWheelEvent*event)override{
        // Scroll the rail without changing the active channel or any connection.
        m_wheel+=event->angleDelta().y();int steps=m_wheel/120;m_wheel%=120;
        if(!steps && !event->pixelDelta().isNull())steps=event->pixelDelta().y()>0?1:-1;
        auto buttons=findChildren<QToolButton*>(QString(),Qt::FindDirectChildrenOnly);
        std::sort(buttons.begin(),buttons.end(),[](QToolButton*a,QToolButton*b){const auto pa=a->geometry().center(),pb=b->geometry().center();return pa.y()==pb.y()?pa.x()<pb.x():pa.y()<pb.y();});
        if(buttons.size()>=2){auto*button=steps>0?buttons.first():buttons.last();for(int i=0;i<qAbs(steps)&&button->isVisible()&&button->isEnabled();++i)button->click();}
        event->accept();
    }
    void paintEvent(QPaintEvent *)override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing,true);
        for(int i=0;i<count();++i){
            QStyleOptionTab option;initStyleOption(&option,i);
            const bool enabled=isTabEnabled(i)&&(option.state & QStyle::State_Enabled);
            const bool selected=(i==currentIndex());
            const bool hovered=(option.state & QStyle::State_MouseOver);
            const bool lin=isLinTab(i);
            const QRect rect=tabRect(i);

            // Soft azure for CAN, soft sage green for LIN
            QColor bg=Qt::transparent;
            if(!enabled){
                bg=lin?QColor("#EDF4F0"):QColor("#EDF2F7");
            }else if(selected){
                bg=lin?QColor("#E7F2EB"):QColor("#E6EEF6");
            }else if(hovered){
                bg=lin?QColor("#F0F7F2"):QColor("#F0F5FA");
            }
            if(bg!=Qt::transparent){
                painter.fillRect(rect,bg);
            }

            // Left accent line
            if(selected&&enabled){
                const QColor accent=lin?QColor("#2D7A5A"):QColor("#2B6CB0");
                painter.fillRect(QRect(rect.left(),rect.top(),3,rect.height()),accent);
            }

            // Status icon
            const QIcon icon=tabIcon(i);
            const int iconPx=16;
            const int leftPad=8;
            const QRect iconRect(rect.left()+leftPad,rect.top()+(rect.height()-iconPx)/2,iconPx,iconPx);
            if(!icon.isNull()){
                icon.paint(&painter,iconRect);
            }

            // Channel label text
            const QRect textRect=rect.adjusted(leftPad+iconPx+6,0,-4,0);
            QFont f=font();
            if(selected&&enabled)f.setBold(true);
            painter.setFont(f);
            const QColor textCol=!enabled?(lin?QColor("#889D91"):QColor("#8898AA")):(selected?(lin?QColor("#1E5E3A"):QColor("#1E4E79")):QColor("#688095"));
            painter.setPen(textCol);
            painter.drawText(textRect,Qt::AlignLeft|Qt::AlignVCenter|Qt::TextSingleLine,tabText(i));
        }
    }
private:int m_wheel=0;
};
class ChannelTabs final : public QTabWidget {
public:
    explicit ChannelTabs(QWidget *parent=nullptr):QTabWidget(parent){
        setTabBar(new ChannelTabBar(this));setTabPosition(West);setContextMenuPolicy(Qt::CustomContextMenu);
        m_blank=new QWidget(this);m_blank->setObjectName("channelCreationSpace");m_blank->setContextMenuPolicy(Qt::CustomContextMenu);
        m_blank->setToolTip("右键空白处新建软件通道；滚轮浏览通道列表");
        connect(m_blank,&QWidget::customContextMenuRequested,this,[this](const QPoint&p){emit customContextMenuRequested(m_blank->mapTo(this,p));});
    }
    int navigationWidth()const{return 104;}
protected:
    void resizeEvent(QResizeEvent*event)override{
        const int row=static_cast<ChannelTabBar*>(tabBar())->rowHeight();
        tabBar()->setMaximumHeight(qMax(row,height()-row));QTabWidget::resizeEvent(event);
        m_blank->setGeometry(0,qMax(0,height()-row),navigationWidth(),row);m_blank->raise();
    }
private:QWidget*m_blank;
};
}
