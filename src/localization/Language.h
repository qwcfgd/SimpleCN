#pragma once
#include <QObject>
#include <QString>
namespace host {
// Presentation-only language state. Protocol values, database data and saved
// identifiers always retain their original representation.
class Language final : public QObject {
    Q_OBJECT
public:
    static Language &instance();
    bool english() const { return m_english; }
    QString code() const { return m_english ? "en" : "zh_CN"; }
    void setCode(const QString &code);
    static QString text(const QString &source);
signals:
    void changed();
private:
    Language();
    bool m_english=false;
};
}
