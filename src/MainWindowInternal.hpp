// Shared by the MainWindow*.cpp translation units (not a public header).
#pragma once

#include <QtGlobal>

namespace {
// Reading-time assumption: average adult silent reading ~225 WPM.
constexpr int kReadingWpm = 225;
constexpr int kMaxRecentFiles = 8;
// Typewriter body density on A4 (~pica / 10 CPI). User can enlarge via the picker.
constexpr int kDefaultBodyPointSize = 12;
// Markdown mode (iA-style): iA Writer Duo at 13 pt, a column of about 66
// characters, 150 % line height.
constexpr int kMarkdownBodyPointSize = 13;
constexpr int kMarkdownColumnChars = 66;
constexpr qreal kMarkdownLineHeightPercent = 150.0;

} // namespace
