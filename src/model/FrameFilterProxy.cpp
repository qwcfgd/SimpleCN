#include "FrameFilterProxy.h"
#include "FramePresentation.h"
#include "localization/Language.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
namespace host {
namespace {
constexpr int kDirectionColumn = 4;
constexpr int kIdentifierColumn = 5;
constexpr int kLengthColumn = 6;
constexpr int kStatusColumn = 8;
}  // namespace
bool parseFrameIdRange(const QString &text, int base, quint32 maximum,
                       FrameIdRange &out) {
  if (base != 2 && base != 8 && base != 10 && base != 16) {
    return false;
  }
  const auto parts = text.trimmed().split('-');
  if (parts.size() < 1 || parts.size() > 2) {
    return false;
  }
  auto number = [base, maximum](QString s, quint32 &v) {
    s = s.trimmed();
    const auto prefix = base == 16  ? QString("0x")
                        : base == 2 ? QString("0b")
                        : base == 8 ? QString("0o")
                                    : QString();
    if (!prefix.isEmpty() && s.startsWith(prefix, Qt::CaseInsensitive)) {
      s.remove(0, 2);
    }
    if (s.isEmpty()) {
      return false;
    }
    for (auto c : s) {
      const int n = QString("0123456789abcdef").indexOf(c.toLower());
      if (n < 0 || n >= base) {
        return false;
      }
    }
    bool ok = false;
    const auto n = s.toULongLong(&ok, base);
    if (!ok || n > maximum) {
      return false;
    }
    v = quint32(n);
    return true;
  };
  FrameIdRange next;
  if (!number(parts[0], next.first)) {
    return false;
  }
  if (parts.size() == 1) {
    next.last = next.first;
  } else if (!number(parts[1], next.last)) {
    return false;
  }
  if (next.first > next.last) {
    return false;
  }
  out = next;
  return true;
}
QString frameFilterOperatorName(FrameFilterOp op) {
  return Language::text(QStringList{"等于", "不等于", "开头是", "结尾是",
                                    "包含", "不包含", "大于", "大于或等于",
                                    "小于", "小于或等于", "介于", "不介于"}
                            .value(int(op)));
}
static QString WildcardPattern(const QString &text) {
  QString result;
  for (int i = 0; i < text.size(); ++i) {
    const auto c = text[i];
    if (c == '~' && i + 1 < text.size()) {
      result += QRegularExpression::escape(QString(text[++i]));
    } else if (c == '*') {
      result += ".*";
    } else if (c == '?') {
      result += '.';
    } else {
      result += QRegularExpression::escape(QString(c));
    }
  }
  return result;
}
bool prepareFrameFilter(int column, FrameColumnFilter &filter, quint32 maximum,
                        QString &error) {
  if (column < 0 || column >= FrameTableModel::ColumnCount) {
    error = "未知筛选列";
    return false;
  }
  for (auto &c : filter.conditions) {
    const int op = int(c.op);
    if (op < 0 || op > int(FrameFilterOp::NotBetween)) {
      error = "未知筛选规则";
      return false;
    }
    if (column == kIdentifierColumn) {
      if (op >= int(FrameFilterOp::Starts) &&
          op <= int(FrameFilterOp::NotContains)) {
        error = "ID 筛选需要数值规则";
        return false;
      }
      const bool between =
          c.op == FrameFilterOp::Between || c.op == FrameFilterOp::NotBetween;
      const auto input =
          between && !c.upper.isEmpty() ? c.value + "-" + c.upper : c.value;
      if (!parseFrameIdRange(input, c.base, maximum, c.range) ||
          (!between && c.range.first != c.range.last)) {
        error = "ID 或范围无效，请检查进制、上下限和总线范围";
        return false;
      }
    } else {
      if (op > int(FrameFilterOp::NotContains)) {
        error = "此列仅支持文本筛选";
        return false;
      }
      auto pattern = WildcardPattern(c.value);
      if (c.op == FrameFilterOp::Equal || c.op == FrameFilterOp::NotEqual) {
        pattern = "\\A" + pattern + "\\z";
      } else if (c.op == FrameFilterOp::Starts) {
        pattern = "\\A" + pattern;
      } else if (c.op == FrameFilterOp::Ends) {
        pattern += "\\z";
      }
      c.expression = QRegularExpression(
          pattern, QRegularExpression::CaseInsensitiveOption |
                       QRegularExpression::DotMatchesEverythingOption);
    }
  }
  error.clear();
  return true;
}
static QString DisplayValue(const QString &key, int column,
                            const QMap<QString, QString> &labels) {
  if (column == kIdentifierColumn) {
    return "0x" + QString::number(key.toUInt(), 16).toUpper();
  }
  return column == kStatusColumn ? labels.value(key, key) : key;
}
static QString ColumnKey(const FrameRecord &record, int column,
                         const FrameTableModel::NameLookup &names) {
  return column == FrameTableModel::NameColumn ? names.name(record)
                                               : frameColumnKey(record, column);
}
static bool MatchesColumn(const FrameRecord &r, int column,
                          const FrameColumnFilter &filter,
                          const QMap<QString, QString> &labels,
                          const FrameTableModel::NameLookup &names) {
  if (!filter.active()) {
    return true;
  }
  const auto key = ColumnKey(r, column, names);
  if (filter.valuesEnabled && !filter.values.contains(key)) {
    return false;
  }
  if (filter.conditions.isEmpty()) {
    return true;
  }
  const auto text = DisplayValue(key, column, labels);
  for (const auto &c : filter.conditions) {
    bool hit = false;
    if (column == kIdentifierColumn) {
      const auto id = r.id, a = c.range.first, b = c.range.last;
      switch (c.op) {
        case FrameFilterOp::Equal:
          hit = id == a;
          break;
        case FrameFilterOp::NotEqual:
          hit = id != a;
          break;
        case FrameFilterOp::Greater:
          hit = id > a;
          break;
        case FrameFilterOp::GreaterEqual:
          hit = id >= a;
          break;
        case FrameFilterOp::Less:
          hit = id < a;
          break;
        case FrameFilterOp::LessEqual:
          hit = id <= a;
          break;
        case FrameFilterOp::Between:
          hit = id >= a && id <= b;
          break;
        case FrameFilterOp::NotBetween:
          hit = id < a || id > b;
          break;
        default:
          break;
      }
    } else {
      hit = c.expression.match(text).hasMatch();
      if (c.op == FrameFilterOp::NotEqual ||
          c.op == FrameFilterOp::NotContains) {
        hit = !hit;
      }
    }
    if (filter.all && !hit) {
      return false;
    }
    if (!filter.all && hit) {
      return true;
    }
  }
  return filter.all;
}
static bool AcceptsRecord(
    const FrameRecord &record,
    const std::array<FrameColumnFilter, FrameTableModel::ColumnCount> &filters,
    const QMap<QString, QString> &labels,
    const FrameTableModel::NameLookup &names, int except) {
  for (int column = 0; column < FrameTableModel::ColumnCount; ++column) {
    if (column != except &&
        !MatchesColumn(record, column, filters[column], labels, names)) {
      return false;
    }
  }
  return true;
}
FrameFilterProxy::FrameFilterProxy(QObject *parent)
    : QSortFilterProxyModel(parent) {
  collator_.setNumericMode(true);
  collator_.setCaseSensitivity(Qt::CaseInsensitive);
  filters_[kDirectionColumn].valuesEnabled = true;
  filters_[kDirectionColumn].values = {"Tx", "Rx"};
  updateLabels();
  connect(&Language::instance(), &Language::changed, this, [this] {
    updateLabels();
    syncDisplayFilter();
    invalidate();
  });
}
void FrameFilterProxy::setSourceModel(QAbstractItemModel *model) {
  if (model == sourceModel()) {
    return;
  }
  if (auto *old = qobject_cast<FrameTableModel *>(sourceModel())) {
    disconnect(old, &FrameTableModel::frameNamesChanged, this, nullptr);
    old->setDisplayFilter({});
  }
  QSortFilterProxyModel::setSourceModel(model);
  if (auto *frames = qobject_cast<FrameTableModel *>(model)) {
    connect(frames, &FrameTableModel::frameNamesChanged, this, [this] {
      if (!columnActive(FrameTableModel::NameColumn)) {
        return;
      }
      syncDisplayFilter();
      invalidateFilter();
    });
  }
  syncDisplayFilter();
}
void FrameFilterProxy::syncDisplayFilter() {
  auto *model = qobject_cast<FrameTableModel *>(sourceModel());
  if (!model) {
    return;
  }
  bool active = false;
  for (int column = 0; column < FrameTableModel::ColumnCount; ++column) {
    const auto &f = filters_[column];
    const bool is_default_direction =
        column == kDirectionColumn && f.valuesEnabled &&
        f.values == QSet<QString>({"Tx", "Rx"}) && f.conditions.isEmpty();
    if (f.active() && !is_default_direction) {
      active = true;
      break;
    }
  }
  if (!active) {
    if (filters_[kDirectionColumn].valuesEnabled) {
      // Apply the default direction selection before merging rows or assigning
      // stripes.
      model->setDisplayFilter(
          [](const FrameRecord &r) { return r.direction != "TxPending"; },
          true);
    } else if (model->hasDisplayFilter()) {
      model->setDisplayFilter({});
    }
    return;
  }
  const auto filters = filters_;
  const auto labels = status_labels_;
  const auto names = filters[FrameTableModel::NameColumn].active()
                         ? model->name_lookup()
                         : FrameTableModel::NameLookup();
  model->setDisplayFilter([filters, labels, names](const FrameRecord &r) {
    return AcceptsRecord(r, filters, labels, names, -1);
  });
}
void FrameFilterProxy::updateLabels() {
  for (const auto &state :
       QStringList{"待确认", "正常", "无响应", "错误", "硬件状态"}) {
    status_labels_[state] = Language::text(state);
  }
}
FrameColumnFilter FrameFilterProxy::columnFilter(int column) const {
  return column >= 0 && column < FrameTableModel::ColumnCount
             ? filters_[column]
             : FrameColumnFilter();
}
bool FrameFilterProxy::setColumnFilter(int column,
                                       const FrameColumnFilter &input,
                                       QString *error) {
  auto next = input;
  QString problem;
  if (!prepareFrameFilter(column, next, id_maximum_, problem)) {
    if (error) {
      *error = problem;
    }
    return false;
  }
  filters_[column] = std::move(next);
  syncDisplayFilter();
  invalidateFilter();
  emit filtersChanged();
  return true;
}
QJsonArray FrameFilterProxy::saveFilters() const {
  QJsonArray result;
  for (int column = 0; column < FrameTableModel::ColumnCount; ++column) {
    const auto &f = filters_[column];
    if (!f.active()) {
      continue;
    }
    QJsonArray values, rules;
    QStringList sorted = f.values.values();
    sorted.sort();
    for (const auto &v : sorted) {
      values.append(v);
    }
    for (const auto &c : f.conditions) {
      rules.append(QJsonObject{{"op", int(c.op)},
                               {"value", c.value},
                               {"upper", c.upper},
                               {"base", c.base}});
    }
    result.append(QJsonObject{{"column", column},
                              {"valuesEnabled", f.valuesEnabled},
                              {"values", values},
                              {"all", f.all},
                              {"rules", rules}});
  }
  return result;
}
void FrameFilterProxy::restoreFilters(const QJsonArray &array) {
  filters_ = {};
  for (const auto &value : array) {
    const auto o = value.toObject();
    const int column = o["column"].toInt(-1);
    if (column < 0 || column >= FrameTableModel::ColumnCount) {
      continue;
    }
    FrameColumnFilter f;
    f.valuesEnabled = o["valuesEnabled"].toBool();
    f.all = o.value("all").toBool(true);
    for (const auto &v : o["values"].toArray()) {
      f.values.insert(v.toString());
    }
    for (const auto &v : o["rules"].toArray()) {
      const auto c = v.toObject();
      FrameCondition rule;
      rule.op = FrameFilterOp(c["op"].toInt(-1));
      rule.value = c["value"].toString();
      rule.upper = c["upper"].toString();
      rule.base = c.value("base").toInt(16);
      f.conditions.append(rule);
    }
    QString error;
    if (prepareFrameFilter(column, f, id_maximum_, error)) {
      filters_[column] = f;
    }
  }
  syncDisplayFilter();
  invalidateFilter();
  emit filtersChanged();
}
bool FrameFilterProxy::accepts(const FrameRecord &record,
                               int excluded_column) const {
  static const FrameTableModel::NameLookup emptyNames;
  const auto *model = static_cast<const FrameTableModel *>(sourceModel());
  return AcceptsRecord(record, filters_, status_labels_,
                       model ? model->name_lookup() : emptyNames,
                       excluded_column);
}
bool FrameFilterProxy::filterAcceptsRow(int row,
                                        const QModelIndex &parent) const {
  const auto *model = static_cast<const FrameTableModel *>(sourceModel());
  if (!model) {
    return false;
  }
  const auto *record =
      model->frameAt(parent.isValid() ? parent : model->index(row, 0));
  return record && accepts(*record);
}
bool FrameFilterProxy::lessThan(const QModelIndex &a,
                                const QModelIndex &b) const {
  const auto stable = [&] {
    return sortOrder() == Qt::AscendingOrder ? a.row() < b.row()
                                             : a.row() > b.row();
  };
  if (a.parent().isValid() || b.parent().isValid()) {
    return stable();
  }
  const auto *model = static_cast<const FrameTableModel *>(sourceModel());
  const auto *x = model->frameAt(a), *y = model->frameAt(b);
  if (!x || !y) {
    return stable();
  }
  const int column = a.column();
  int cmp = 0;
  // A merged row shows the newest frame, but its time-sort position belongs
  // to the first occurrence. History/pause still sort individual events.
  if (column <= 2 && model->rolling() && !model->paused()) {
    x = model->firstFrameAt(a);
    y = model->firstFrameAt(b);
  }
  if (column == FrameTableModel::NameColumn) {
    cmp = collator_.compare(a.data().toString(), b.data().toString());
  } else if (column <= 2 || column == kIdentifierColumn ||
             column == kLengthColumn) {
    auto number = [column](const FrameRecord &r) -> double {
      if (column == 0) {
        return r.captureUs;
      }
      if (column == 1) {
        return std::isfinite(r.relativeSeconds) ? r.relativeSeconds : -1;
      }
      if (column == 2) {
        return r.intervalSeconds.toDouble();
      }
      return column == kIdentifierColumn ? r.id : r.length;
    };
    const auto l = number(*x), r = number(*y);
    cmp = l < r ? -1 : l > r ? 1 : 0;
  } else {
    cmp = collator_.compare(
        DisplayValue(frameColumnKey(*x, column), column, status_labels_),
        DisplayValue(frameColumnKey(*y, column), column, status_labels_));
  }
  return cmp == 0 ? stable() : cmp < 0;
}
QFuture<FrameCandidates> FrameFilterProxy::candidateValues(int column) const {
  const auto *model = static_cast<const FrameTableModel *>(sourceModel());
  const auto history = model ? model->history() : FrameBatch();
  const auto filters = filters_;
  const auto labels = status_labels_;
  // The background menu scan keeps the names that matched this history
  // snapshot.
  const auto names =
      model ? model->name_lookup() : FrameTableModel::NameLookup();
  return QtConcurrent::run([history, filters, labels, names, column] {
    QSet<QString> unique;
    // Direction choices stay inspectable before capture and when other filters
    // hide every frame.
    if (column == kDirectionColumn) {
      unique = {"TxPending", "Tx", "Rx"};
    }
    for (const auto &r : history) {
      if (AcceptsRecord(r, filters, labels, names, column)) {
        unique.insert(ColumnKey(r, column, names));
      }
    }
    FrameCandidates result;
    result.reserve(unique.size());
    for (const auto &key : unique) {
      result.append({key, DisplayValue(key, column, labels)});
    }
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(result.begin(), result.end(), [&](const auto &a, const auto &b) {
      return column == kIdentifierColumn
                 ? a.first.toUInt() < b.first.toUInt()
                 : collator.compare(a.second, b.second) < 0;
    });
    return result;
  });
}
FrameBatch FrameFilterProxy::filteredHistory() const {
  FrameBatch result;
  const auto *model = static_cast<const FrameTableModel *>(sourceModel());
  if (model) {
    for (const auto &r : model->history()) {
      if (accepts(r)) {
        result.append(r);
      }
    }
  }
  return result;
}
FrameBatch FrameFilterProxy::selectedFrames(
    const QModelIndexList &selection) const {
  FrameBatch result;
  const auto *model = static_cast<const FrameTableModel *>(sourceModel());
  if (!model) {
    return result;
  }
  QSet<int> rows;
  for (auto index : selection) {
    if (index.model() != this || !index.isValid()) {
      continue;
    }
    while (index.parent().isValid()) {
      index = index.parent();
    }
    rows.insert(mapToSource(index).row());
  }
  auto ordered = rows.values();
  std::sort(ordered.begin(), ordered.end());
  for (int row : ordered) {
    if (const auto *r = model->frameAt(model->index(row, 0))) {
      result.append(*r);
    }
  }
  return result;
}
}  // namespace host
