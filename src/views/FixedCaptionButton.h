#pragma once

#include "localization/Language.h"
#include <QFontMetrics>
#include <QPushButton>
#include <QStringList>
#include <QStyleOptionButton>

namespace host {
class FixedCaptionButton final : public QPushButton {
public:
    FixedCaptionButton(const QString &text, const QStringList &captions)
        : QPushButton(text), m_captions(captions)
    {
        for(const auto &caption : captions) m_captions.append(Language::englishText(caption));
        setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);
    }

    QSize sizeHint() const override
    {
        ensurePolished();
        QFont measurementFont=parentWidget()?parentWidget()->font():font();
        // Measure the emphasized caption even while the active button uses the regular weight.
        measurementFont.setBold(true);
        const QFontMetrics metrics(measurementFont,this);
        int width=0;
        for(const auto &caption : m_captions) width=qMax(width,metrics.horizontalAdvance(caption));
        QStyleOptionButton option;
        initStyleOption(&option);
        option.fontMetrics=metrics;
        return style()->sizeFromContents(QStyle::CT_PushButton,&option,{width,metrics.height()},this);
    }

    QSize minimumSizeHint() const override { return sizeHint(); }

private:
    QStringList m_captions;
};
}
