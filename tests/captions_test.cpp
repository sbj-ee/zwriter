// Automatic figure numbers (1.9.0).
#include "Captions.hpp"
#include "DocumentIo.hpp"
#include "ParagraphStyles.hpp"

#include <QGuiApplication>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <cstdio>

namespace {
int g_failures = 0;
void check(bool ok, const char *what, const QString &detail = QString())
{
    std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.isEmpty() ? "" : "  -- ", qPrintable(detail));
    if (!ok) {
        ++g_failures;
    }
}
QStringList captions(const QTextDocument &d)
{
    QStringList l;
    for (QTextBlock b = d.begin(); b.isValid(); b = b.next()) {
        if (Captions::isCaption(b)) {
            l << b.text();
        }
    }
    return l;
}
void addCaption(QTextCursor &c, const QString &text)
{
    c.insertBlock();
    c.insertText(text);
    ParagraphStyles::apply(c, QStringLiteral("Caption"));
}
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTextDocument doc;
    QTextCursor c(&doc);
    c.insertText(QStringLiteral("Text."));
    addCaption(c, QStringLiteral("Trumpeter swan"));
    c.insertBlock();
    c.insertText(QStringLiteral("More text."));
    ParagraphStyles::apply(c, QString());
    addCaption(c, QStringLiteral("Figure 7. Mallard pair")); // typed by hand, wrong number
    addCaption(c, QStringLiteral("Wood duck"));

    check(Captions::renumber(&doc, 4) == 3, "three captions labelled");
    check(captions(doc) == QStringList({QStringLiteral("Figure 4.1. Trumpeter swan"), QStringLiteral("Figure 4.2. Mallard pair"),
                                        QStringLiteral("Figure 4.3. Wood duck")}),
          "Figure <chapter>.<n>, a hand-typed number replaced", captions(doc).join(QStringLiteral(" | ")));
    check(Captions::renumber(&doc, 4) == 0, "nothing to do: document untouched");
    check(!doc.toPlainText().contains(QLatin1String("Figure 4.1. Figure")), "no doubled labels");

    // Move the last figure to the front.
    const QTextBlock last = doc.lastBlock();
    QTextCursor cut(last);
    cut.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    const QString moved = cut.selectedText();
    cut.removeSelectedText();
    cut.deletePreviousChar(); // the paragraph break
    QTextCursor front(doc.findBlockByNumber(1));
    front.insertText(QStringLiteral("x"));
    front.deletePreviousChar();
    front.movePosition(QTextCursor::StartOfBlock);
    front.insertBlock();
    front.movePosition(QTextCursor::PreviousBlock);
    front.insertText(moved);
    ParagraphStyles::apply(front, QStringLiteral("Caption"));
    Captions::renumber(&doc, 4);
    check(captions(doc) == QStringList({QStringLiteral("Figure 4.1. Wood duck"), QStringLiteral("Figure 4.2. Trumpeter swan"),
                                        QStringLiteral("Figure 4.3. Mallard pair")}),
          "a moved figure renumbers all", captions(doc).join(QStringLiteral(" | ")));

    Captions::renumber(&doc, 2);
    check(captions(doc).first() == QLatin1String("Figure 2.1. Wood duck"), "the chapter moves: new chapter number");
    Captions::renumber(&doc, 0);
    check(captions(doc).first() == QLatin1String("Figure 1. Wood duck"), "outside a book: Figure <n>");

    // ODT keeps the Caption style and the label; read back, nothing changes.
    Captions::renumber(&doc, 3);
    QTemporaryDir tmp;
    const QString path = tmp.filePath(QStringLiteral("f.odt"));
    DocumentIo::save(&doc, path, DocumentIo::Format::Odt);
    QTextDocument back;
    DocumentIo::load(&back, path);
    check(captions(back) == captions(doc), "ODT round trip keeps captions and labels", captions(back).join(QStringLiteral(" | ")));
    check(Captions::renumber(&back, 3) == 0, "reopened: already numbered");
    check(Captions::count(&back) == 3, "count");
    std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "OK", g_failures);
    return g_failures ? 1 : 0;
}
