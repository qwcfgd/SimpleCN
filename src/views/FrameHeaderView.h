#pragma once
#include <QHeaderView>
#include <QSet>
namespace host {
class FrameHeaderView : public QHeaderView {
    Q_OBJECT
public:
    explicit FrameHeaderView(QWidget*parent=nullptr);
    void setFilteredColumns(const QSet<int>&);
    void setOpenColumn(int column);
    QRect filterButtonRect(int column)const;
    bool filterButtonVisible(int column)const;
signals:void filterRequested(int column,QPoint globalPosition);
protected:
    void paintSection(QPainter*,const QRect&,int)const override;
    void mouseMoveEvent(QMouseEvent*)override;
    void leaveEvent(QEvent*)override;
    void mousePressEvent(QMouseEvent*)override;
    void mouseReleaseEvent(QMouseEvent*)override;
private:int m_hover=-1,m_open=-1;bool m_filterPressed=false;QSet<int>m_filtered;
};
}
