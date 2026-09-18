#pragma once
#include <boost/multiprecision/cpp_int.hpp>
#include <QRegularExpression>
#include <QString>
#include <stdexcept>
namespace host::signal::decimal {
using Int=boost::multiprecision::cpp_int;
// Exact rational arithmetic: decimal coefficients never pass through double.
struct Number {
    Int n=0,d=1;
    Number()=default;
    Number(Int value):n(std::move(value)){}
    Number(Int num,Int den):n(std::move(num)),d(std::move(den)){
        if(d==0)throw std::runtime_error("zero divisor");
        if(d<0){n=-n;d=-d;}
    }
};
inline Number operator+(const Number&a,const Number&b){return {a.n*b.d+b.n*a.d,a.d*b.d};}
inline Number operator-(const Number&a,const Number&b){return {a.n*b.d-b.n*a.d,a.d*b.d};}
inline Number operator*(const Number&a,const Number&b){return {a.n*b.n,a.d*b.d};}
inline Number operator/(const Number&a,const Number&b){return {a.n*b.d,a.d*b.n};}
inline bool operator<(const Number&a,const Number&b){return a.n*b.d<b.n*a.d;}
inline bool operator==(const Number&a,const Number&b){return a.n*b.d==b.n*a.d;}
inline Int pow10(int exponent){Int r=1;for(int i=0;i<exponent;++i)r*=10;return r;}
inline bool parse(QString text,Number &out){
    text=text.trimmed();
    static const QRegularExpression re("^([+-]?)([0-9]+)(?:\\.([0-9]*))?(?:[eE]([+-]?[0-9]+))?$");
    if(text.size()>512)return false;
    auto m=re.match(text);if(!m.hasMatch())return false;
    bool ok=true;int exp=m.captured(4).isEmpty()?0:m.captured(4).toInt(&ok);
    if(!ok||qAbs(qint64(exp))>512)return false;
    Int n=0;for(QChar c:m.captured(2)+m.captured(3)){n*=10;n+=c.digitValue();}
    if(m.captured(1)=="-")n=-n;
    exp-=m.captured(3).size();
    out=exp>=0?Number(n*pow10(exp)):Number(n,pow10(-exp));return true;
}
inline Number require(const QString &text){Number n;if(!parse(text,n))throw std::runtime_error("invalid decimal");return n;}
inline QString text(const Int &n){return QString::fromStdString(n.convert_to<std::string>());}
inline QString text(const Number &v){
    Int n=v.n;QString sign;if(n<0){n=-n;sign="-";}
    QString out=sign+text(Int(n/v.d));Int r=n%v.d;
    if(r!=0){out+='.';for(int i=0;i<1024&&r!=0;++i){r*=10;out+=text(Int(r/v.d));r%=v.d;}if(r!=0)out+=QChar(0x2026);}
    return out;
}
}
