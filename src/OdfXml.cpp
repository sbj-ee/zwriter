#include "OdfXml.hpp"

#include <QBuffer>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace OdfXml {

Node Node::element(const QString &name, const QList<QPair<QString, QString>> &attrs)
{
    Node n;
    n.kind = Element;
    n.name = name;
    n.attributes = attrs;
    return n;
}

Node Node::textNode(const QString &text)
{
    Node n;
    n.kind = Text;
    n.text = text;
    return n;
}

bool Node::hasAttribute(const QString &qname) const
{
    for (const auto &a : attributes) {
        if (a.first == qname) {
            return true;
        }
    }
    return false;
}

QString Node::attribute(const QString &qname) const
{
    for (const auto &a : attributes) {
        if (a.first == qname) {
            return a.second;
        }
    }
    return {};
}

void Node::setAttribute(const QString &qname, const QString &value)
{
    for (auto &a : attributes) {
        if (a.first == qname) {
            a.second = value;
            return;
        }
    }
    attributes.append({qname, value});
}

bool Node::removeAttribute(const QString &qname)
{
    for (qsizetype i = 0; i < attributes.size(); ++i) {
        if (attributes.at(i).first == qname) {
            attributes.removeAt(i);
            return true;
        }
    }
    return false;
}

bool Node::renameAttribute(const QString &from, const QString &to)
{
    for (auto &a : attributes) {
        if (a.first == from) {
            removeAttribute(to);
            for (auto &b : attributes) {
                if (b.first == from) {
                    b.first = to;
                    return true;
                }
            }
        }
    }
    return false;
}

Node *Node::child(const QString &qname)
{
    for (Node &c : children) {
        if (c.isElement(qname)) {
            return &c;
        }
    }
    return nullptr;
}

QString Node::innerText() const
{
    if (kind == Text) {
        return text;
    }
    QString out;
    for (const Node &c : children) {
        out += c.innerText();
    }
    return out;
}

namespace {

// Reads nodes into `top` until the reader ends (or its element closes).
bool readInto(QXmlStreamReader &r, std::vector<Node> *top, QString *error)
{
    std::vector<std::vector<Node> *> stack{top};
    std::vector<Node *> open;
    while (!r.atEnd()) {
        const QXmlStreamReader::TokenType t = r.readNext();
        std::vector<Node> *cur = stack.back();
        switch (t) {
        case QXmlStreamReader::StartElement: {
            Node n = Node::element(r.qualifiedName().toString());
            for (const QXmlStreamNamespaceDeclaration &d : r.namespaceDeclarations()) {
                n.attributes.append({d.prefix().isEmpty() ? QStringLiteral("xmlns")
                                                          : QStringLiteral("xmlns:") + d.prefix().toString(),
                                     d.namespaceUri().toString()});
            }
            for (const QXmlStreamAttribute &a : r.attributes()) {
                n.attributes.append({a.qualifiedName().toString(), a.value().toString()});
            }
            cur->push_back(std::move(n));
            Node *added = &cur->back();
            open.push_back(added);
            stack.push_back(&added->children);
            break;
        }
        case QXmlStreamReader::EndElement:
            if (open.empty()) {
                break;
            }
            open.pop_back();
            stack.pop_back();
            break;
        case QXmlStreamReader::Characters:
            if (!cur->empty() && cur->back().kind == Node::Text) {
                cur->back().text += r.text();
            } else {
                cur->push_back(Node::textNode(r.text().toString()));
            }
            break;
        case QXmlStreamReader::Comment: {
            Node c;
            c.kind = Node::Comment;
            c.text = r.text().toString();
            cur->push_back(std::move(c));
            break;
        }
        default:
            break;
        }
    }
    if (r.hasError()) {
        if (error) {
            *error = QStringLiteral("XML error at line %1: %2").arg(r.lineNumber()).arg(r.errorString());
        }
        return false;
    }
    return true;
}

void writeNode(QXmlStreamWriter &w, const Node &n)
{
    switch (n.kind) {
    case Node::Text:
        w.writeCharacters(n.text);
        return;
    case Node::Comment:
        w.writeComment(n.text);
        return;
    case Node::Element:
        break;
    }
    if (n.children.empty()) {
        w.writeEmptyElement(n.name);
    } else {
        w.writeStartElement(n.name);
    }
    for (const auto &a : n.attributes) {
        w.writeAttribute(a.first, a.second);
    }
    for (const Node &c : n.children) {
        writeNode(w, c);
    }
    if (!n.children.empty()) {
        w.writeEndElement();
    }
}

} // namespace

bool parse(const QByteArray &xml, Node *root, QString *error)
{
    QXmlStreamReader r(xml);
    r.setNamespaceProcessing(false);
    std::vector<Node> top;
    if (!readInto(r, &top, error)) {
        return false;
    }
    for (Node &n : top) {
        if (n.kind == Node::Element) {
            *root = std::move(n);
            return true;
        }
    }
    if (error) {
        *error = QStringLiteral("XML has no root element.");
    }
    return false;
}

bool parseFragment(const QString &xml, std::vector<Node> *nodes, QString *error)
{
    QXmlStreamReader r(QStringLiteral("<zw-fragment>") + xml + QStringLiteral("</zw-fragment>"));
    r.setNamespaceProcessing(false);
    std::vector<Node> top;
    if (!readInto(r, &top, error) || top.empty()) {
        return false;
    }
    *nodes = std::move(top.front().children);
    return true;
}

QByteArray serialize(const Node &root)
{
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    QXmlStreamWriter w(&buf);
    w.setAutoFormatting(false);
    w.writeStartDocument(QStringLiteral("1.0"));
    writeNode(w, root);
    w.writeEndDocument();
    return out;
}

QString serializeNode(const Node &node)
{
    QString out;
    QXmlStreamWriter w(&out);
    w.setAutoFormatting(false);
    writeNode(w, node);
    return out;
}

void forEachElement(Node &root, const std::function<void(Node &)> &fn)
{
    if (root.kind != Node::Element) {
        return;
    }
    fn(root);
    for (Node &c : root.children) {
        forEachElement(c, fn);
    }
}

} // namespace OdfXml
