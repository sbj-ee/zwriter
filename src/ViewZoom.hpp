#pragma once
// View zoom (1.14.0; every view since 1.14.1): display-only magnification of
// the text in Full Page, Continuous and Markdown views. It never touches the document (fonts, page size, margins), so saved
// files, PDF, EPUB and print come out exactly as at 100 %.
#include <QList>
#include <QtGlobal>

namespace ViewZoom {
inline constexpr int kMinPercent = 50;
inline constexpr int kMaxPercent = 300;
inline constexpr const char *kSettingsKey = "view/zoomPercent";

inline const QList<int> &steps()
{
    static const QList<int> s{50, 67, 75, 90, 100, 110, 125, 150, 175, 200, 250, 300};
    return s;
}
inline int clampPercent(int p) { return qBound(kMinPercent, p, kMaxPercent); }
inline int stepUp(int p)
{
    for (int s : steps()) {
        if (s > p) {
            return s;
        }
    }
    return kMaxPercent;
}
inline int stepDown(int p)
{
    for (auto it = steps().crbegin(); it != steps().crend(); ++it) {
        if (*it < p) {
            return *it;
        }
    }
    return kMinPercent;
}
} // namespace ViewZoom
