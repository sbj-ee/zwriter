#include "ParagraphStyles.hpp"
#include "StyleFonts.hpp"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace ParagraphStyles {

const QList<Style> &named()
{
    static const QList<Style> styles = [] {
        QList<Style> s;
        Style title;
        title.name = QStringLiteral("Title");
        title.display = QStringLiteral("Title");
        title.align = Qt::AlignHCenter;
        title.bottom = ptToPx(12);
        title.pointSize = 26;
        title.weight = 700;
        s << title;
        Style subtitle;
        subtitle.name = QStringLiteral("Subtitle");
        subtitle.display = QStringLiteral("Subtitle");
        subtitle.align = Qt::AlignHCenter;
        subtitle.bottom = ptToPx(12);
        subtitle.pointSize = 16;
        subtitle.italic = true;
        s << subtitle;
        Style quote;
        quote.name = QStringLiteral("Quotations");
        quote.display = QStringLiteral("Block Quote");
        quote.left = cmToPx(1.27);
        quote.right = cmToPx(1.27);
        quote.top = ptToPx(6);
        quote.bottom = ptToPx(6);
        s << quote;
        Style caption;
        caption.name = QStringLiteral("Caption");
        caption.display = QStringLiteral("Caption");
        caption.align = Qt::AlignHCenter;
        caption.top = ptToPx(4);
        caption.bottom = ptToPx(8);
        caption.pointSize = 10;
        caption.italic = true;
        s << caption;
        Style bib;
        bib.name = QStringLiteral("Bibliography");
        bib.display = QStringLiteral("Bibliography");
        bib.left = cmToPx(1.27);
        bib.indent = -cmToPx(1.27); // hanging indent
        bib.bottom = ptToPx(6);
        s << bib;
        return s;
    }();
    return styles;
}

const Style *find(const QString &name)
{
    for (const Style &s : named()) {
        if (s.name == name || s.display == name) {
            return &s;
        }
    }
    return nullptr;
}

QString styleOf(const QTextBlockFormat &format)
{
    const QString n = format.property(StyleNameProperty).toString();
    return find(n) ? find(n)->name : QString();
}

template<typename F>
static void forEachBlock(QTextCursor cursor, F f)
{
    QTextDocument *doc = cursor.document();
    QTextBlock b = doc->findBlock(cursor.selectionStart());
    const QTextBlock last = doc->findBlock(cursor.selectionEnd());
    cursor.beginEditBlock();
    while (b.isValid()) {
        QTextCursor c(b);
        f(c, b);
        if (b == last) {
            break;
        }
        b = b.next();
    }
    cursor.endEditBlock();
}

void apply(QTextCursor cursor, const QString &name)
{
    const Style *s = find(name);
    forEachBlock(cursor, [s](QTextCursor &c, const QTextBlock &b) {
        QTextBlockFormat bf = b.blockFormat();
        const bool had = !styleOf(bf).isEmpty();
        QTextCharFormat cf;
        if (s) {
            bf.setProperty(StyleNameProperty, s->name);
            bf.setHeadingLevel(0);
            bf.setAlignment(s->align);
            bf.setLeftMargin(s->left);
            bf.setRightMargin(s->right);
            bf.setTextIndent(s->indent);
            bf.setTopMargin(s->top);
            bf.setBottomMargin(s->bottom);
            // Family and size from the style fonts (1.15.0, Format > Styles…).
            const StyleFonts::Entry fe = StyleFonts::entry(StyleFonts::active(), s->name);
            cf.setFontPointSize(fe.pointSize);
            if (!fe.family.isEmpty()) {
                cf.setFontFamilies(QStringList{fe.family});
            }
            cf.setFontWeight(s->weight);
            cf.setFontItalic(s->italic);
        } else {
            bf.clearProperty(StyleNameProperty);
            if (had) { // leaving a named style: back to plain geometry
                bf.setAlignment(Qt::AlignLeft);
                bf.setLeftMargin(0);
                bf.setRightMargin(0);
                bf.setTextIndent(0);
                bf.setTopMargin(0);
                bf.setBottomMargin(0);
                cf.setFontItalic(false);
            }
        }
        c.setBlockFormat(bf);
        if (cf.propertyCount() > 0) {
            c.mergeBlockCharFormat(cf);
            QTextCursor text(b);
            text.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            text.mergeCharFormat(cf);
        }
    });
}

void changeIndent(QTextCursor cursor, int steps)
{
    forEachBlock(cursor, [steps](QTextCursor &c, const QTextBlock &b) {
        QTextBlockFormat bf = b.blockFormat();
        bf.setLeftMargin(qMax<qreal>(0.0, bf.leftMargin() + steps * kIndentStep));
        c.setBlockFormat(bf);
    });
}

void setGeometry(QTextCursor cursor, const QTextBlockFormat &src)
{
    forEachBlock(cursor, [&src](QTextCursor &c, const QTextBlock &b) {
        QTextBlockFormat bf = b.blockFormat();
        bf.setAlignment(src.alignment());
        bf.setLeftMargin(src.leftMargin());
        bf.setRightMargin(src.rightMargin());
        bf.setTextIndent(src.textIndent());
        bf.setTopMargin(src.topMargin());
        bf.setBottomMargin(src.bottomMargin());
        if (src.hasProperty(QTextFormat::LineHeightType)) {
            bf.setLineHeight(src.lineHeight(), src.lineHeightType());
        }
        c.setBlockFormat(bf);
    });
}

QString blockCss(const QTextBlockFormat &f)
{
    QString css;
    auto px = [](qreal v) { return QString::number(v, 'f', 1) + QStringLiteral("px"); };
    if (f.leftMargin() != 0) {
        css += QStringLiteral("margin-left:%1;").arg(px(f.leftMargin()));
    }
    if (f.rightMargin() != 0) {
        css += QStringLiteral("margin-right:%1;").arg(px(f.rightMargin()));
    }
    if (f.topMargin() != 0) {
        css += QStringLiteral("margin-top:%1;").arg(px(f.topMargin()));
    }
    if (f.bottomMargin() != 0) {
        css += QStringLiteral("margin-bottom:%1;").arg(px(f.bottomMargin()));
    }
    if (f.textIndent() != 0) {
        css += QStringLiteral("text-indent:%1;").arg(px(f.textIndent()));
    }
    if (f.lineHeightType() == QTextBlockFormat::ProportionalHeight && f.lineHeight() > 0 && f.lineHeight() != 100) {
        css += QStringLiteral("line-height:%1%;").arg(qRound(f.lineHeight()));
    }
    const Qt::Alignment a = f.alignment() & Qt::AlignHorizontal_Mask;
    if (a & Qt::AlignHCenter) {
        css += QStringLiteral("text-align:center;");
    } else if (a & Qt::AlignRight) {
        css += QStringLiteral("text-align:right;");
    } else if (a & Qt::AlignJustify) {
        css += QStringLiteral("text-align:justify;");
    }
    return css;
}

} // namespace ParagraphStyles
