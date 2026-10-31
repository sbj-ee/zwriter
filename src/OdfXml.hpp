#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>

#include <functional>
#include <vector>

// A small XML tree for patching ODF parts: QXmlStreamReader builds it,
// QXmlStreamWriter writes it back. Names are kept as written ("text:p"), with
// namespace declarations as ordinary attributes, so a part round-trips with
// the prefixes it had (Qt's ODF writer and LibreOffice use the standard ones).
namespace OdfXml {

struct Node {
    enum Kind { Element, Text, Comment };
    Kind kind = Element;
    QString name;                              // Element: qualified name
    QList<QPair<QString, QString>> attributes; // Element: in document order
    QString text;                              // Text / Comment
    std::vector<Node> children;

    static Node element(const QString &name, const QList<QPair<QString, QString>> &attrs = {});
    static Node textNode(const QString &text);

    bool isElement(const QString &qname) const { return kind == Element && name == qname; }
    bool hasAttribute(const QString &qname) const;
    QString attribute(const QString &qname) const;
    void setAttribute(const QString &qname, const QString &value); // replaces in place, or appends
    bool removeAttribute(const QString &qname);
    // Renames an attribute in place (keeps its position and value).
    bool renameAttribute(const QString &from, const QString &to);
    // First child element with that name, or nullptr.
    Node *child(const QString &qname);
    // The text of every descendant text node.
    QString innerText() const;
};

// Parses a whole document; the root element is returned. Fails on any XML error.
bool parse(const QByteArray &xml, Node *root, QString *error = nullptr);
// Parses a fragment (any number of sibling nodes); the prefixes it uses need
// no declarations (namespace processing is off).
bool parseFragment(const QString &xml, std::vector<Node> *nodes, QString *error = nullptr);
// UTF-8 with an XML declaration.
QByteArray serialize(const Node &root);
// One node (and its subtree) as markup, without a declaration.
QString serializeNode(const Node &node);

// Visits every element, depth first, parent before children.
void forEachElement(Node &root, const std::function<void(Node &)> &fn);

} // namespace OdfXml
