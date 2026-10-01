#include "FocusRange.hpp"

#include <QTextBoundaryFinder>

namespace FocusRange {

QPair<int, int> sentenceRange(const QString &text, int pos)
{
    const int n = int(text.size());
    if (n == 0) {
        return {0, 0};
    }
    pos = qBound(0, pos, n);

    QTextBoundaryFinder finder(QTextBoundaryFinder::Sentence, text);
    int start = 0;
    int end = n;
    if (pos >= n) {
        finder.toEnd();
        start = int(finder.toPreviousBoundary());
    } else {
        finder.setPosition(pos);
        start = finder.isAtBoundary() ? pos : int(finder.toPreviousBoundary());
        finder.setPosition(pos);
        end = int(finder.toNextBoundary());
    }
    if (start < 0) {
        start = 0;
    }
    if (end < 0 || end > n) {
        end = n;
    }
    // A sentence segment includes the spaces after it: trim both ends.
    while (start < end && text.at(start).isSpace()) {
        ++start;
    }
    while (end > start && text.at(end - 1).isSpace()) {
        --end;
    }
    return {start, end};
}

} // namespace FocusRange
