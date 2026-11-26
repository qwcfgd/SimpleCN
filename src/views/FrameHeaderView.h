#pragma once
#include <QHeaderView>
#include <QSet>
namespace host {
class FrameHeaderView : public QHeaderView {
  Q_OBJECT
 public:
  explicit FrameHeaderView(QWidget* parent = nullptr);
  void setFilteredColumns(const QSet<int>&);
  void setOpenColumn(int column);
  QRect filterButtonRect(int column) const;
  bool filterButtonVisible(int column) const;
 signals:
  void filterRequested(int column, QPoint globalPosition);

 protected:
  void paintSection(QPainter*, const QRect&, int) const override;
  void mouseMoveEvent(QMouseEvent*) override;
  void leaveEvent(QEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;

 private:
  int hover_column_ = -1, open_column_ = -1;
  bool filter_pressed_ = false;
  QSet<int> filtered_columns_;
};
}  // namespace host
