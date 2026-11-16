#include "providers/dgg/DggCombo.hpp"

#include <QRegularExpression>

namespace chatterino {

QString dggStripSlashCommand(QStringView text)
{
    // chat-gui: regexslashcmd = /^\/([a-z0-9]+)[\s]?/i, replaced by ''
    static const QRegularExpression re(
        QStringLiteral(R"(^/[a-z0-9]+\s?)"),
        QRegularExpression::CaseInsensitiveOption);

    // Only the match at the start is dropped (the pattern is anchored). The
    // white space JavaScript's trim() removes is what QString::trimmed()
    // removes, for everything chat can contain.
    const QString string = text.toString();
    const auto match = re.match(string);
    if (match.hasMatch())
    {
        return string.mid(match.capturedLength()).trimmed();
    }
    return string.trimmed();
}

int dggComboStep(int count)
{
    if (count >= 50)
    {
        return 50;
    }
    if (count >= 30)
    {
        return 30;
    }
    if (count >= 20)
    {
        return 20;
    }
    if (count >= 10)
    {
        return 10;
    }
    if (count >= 5)
    {
        return 5;
    }
    return 2;
}

QString dggComboGroup(int count)
{
    return QStringLiteral("x%1").arg(dggComboStep(count));
}

int dggComboGreyPercent(qint64 elapsedMs)
{
    constexpr qint64 DURATION_MS = 3500;
    constexpr qint64 COLOUR_UNTIL_MS = DURATION_MS * 75 / 100;
    if (elapsedMs <= COLOUR_UNTIL_MS)
    {
        return 0;
    }
    if (elapsedMs >= DURATION_MS)
    {
        return 100;
    }
    return static_cast<int>((elapsedMs - COLOUR_UNTIL_MS) * 100 /
                            (DURATION_MS - COLOUR_UNTIL_MS));
}

}  // namespace chatterino
