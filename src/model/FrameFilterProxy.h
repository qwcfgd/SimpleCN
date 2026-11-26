#pragma once
#include <QSortFilterProxyModel>
#include <QFuture>
#include <QJsonArray>
#include <QCollator>
#include <QSet>
#include <array>
#include "FrameTableModel.h"
namespace host {
struct FrameIdRange {
  quint32 first = 0, last = 0;
};
bool parseFrameIdRange(const QString &, int base, quint32 maximum,
                       FrameIdRange &);
enum class FrameFilterOp {
  Equal,
  NotEqual,
  Starts,
  Ends,
  Contains,
  NotContains,
  Greater,
  GreaterEqual,
  Less,
  LessEqual,
  Between,
  NotBetween
};
QString frameFilterOperatorName(FrameFilterOp);
struct FrameCondition {
  FrameFilterOp op = FrameFilterOp::Contains;
  QString value, upper;
  int base = 16;
  QRegularExpression expression;
  FrameIdRange range;
};
struct FrameColumnFilter {
  bool valuesEnabled = false, all = true;
  QSet<QString> values;
  QVector<FrameCondition> conditions;
  bool active() const {
    return valuesEnabled || !conditions.isEmpty();
  }
};
bool prepareFrameFilter(int column, FrameColumnFilter &, quint32 maximum,
                        QString &error);
using FrameCandidates = QVector<QPair<QString, QString>>;
class FrameFilterProxy : public QSortFilterProxyModel {
  Q_OBJECT
 public:
  explicit FrameFilterProxy(QObject *parent = nullptr);
  void setSourceModel(QAbstractItemModel *model) override;
  void setIdMaximum(quint32 maximum) {
    id_maximum_ = maximum;
  }
  FrameColumnFilter columnFilter(int column) const;
  bool setColumnFilter(int, const FrameColumnFilter &,
                       QString *error = nullptr);
  bool columnActive(int column) const {
    return columnFilter(column).active();
  }
  QJsonArray saveFilters() const;
  void restoreFilters(const QJsonArray &);
  bool accepts(const FrameRecord &, int exceptColumn = -1) const;
  QFuture<FrameCandidates> candidateValues(int column) const;
  FrameBatch filteredHistory() const;
  FrameBatch selectedFrames(const QModelIndexList &) const;
 signals:
  void filtersChanged();

 protected:
  bool filterAcceptsRow(int, const QModelIndex &) const override;
  bool lessThan(const QModelIndex &, const QModelIndex &) const override;

 private:
  void updateLabels();
  void syncDisplayFilter();
  std::array<FrameColumnFilter, FrameTableModel::ColumnCount> filters_;
  QMap<QString, QString> status_labels_;
  QCollator collator_;
  quint32 id_maximum_ = 0x1fffffff;
};
}  // namespace host
