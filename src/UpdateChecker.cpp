#include "UpdateChecker.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(5000);
    connect(m_timeout, &QTimer::timeout, this, &UpdateChecker::onTimeout);
}

QList<int> UpdateChecker::parseSemver(QStringView s)
{
    if (!s.isEmpty() && (s.front() == QLatin1Char('v') || s.front() == QLatin1Char('V'))) {
        s = s.mid(1);
    }
    QList<int> parts{0, 0, 0};
    const QList<QStringView> bits = s.split(QLatin1Char('.'));
    for (int i = 0; i < 3 && i < bits.size(); ++i) {
        int value = 0;
        for (const QChar c : bits[i]) {
            if (!c.isDigit()) {
                break;
            }
            value = value * 10 + c.digitValue();
        }
        parts[i] = value;
    }
    return parts;
}

bool UpdateChecker::isNewer(const QString &tag, const QString &current)
{
    const QList<int> a = parseSemver(tag);
    const QList<int> b = parseSemver(current);
    for (int i = 0; i < 3; ++i) {
        if (a[i] > b[i]) {
            return true;
        }
        if (a[i] < b[i]) {
            return false;
        }
    }
    return false;
}

void UpdateChecker::checkForUpdates(const QString &currentVersion, const QString &repo)
{
    if (m_checking) {
        return;
    }
    m_currentVersion = currentVersion;
    m_checking = true;

    const QUrl url(QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(repo));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("zwriter-update-check"));
    req.setRawHeader("Accept", "application/vnd.github+json");

    m_reply = m_nam->get(req);
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        if (m_reply) {
            onFinished(m_reply);
        }
    });
    m_timeout->start();
}

void UpdateChecker::onTimeout()
{
    if (m_reply) {
        m_reply->abort();
    }
}

void UpdateChecker::onFinished(QNetworkReply *reply)
{
    m_timeout->stop();
    m_checking = false;
    m_reply = nullptr;

    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit checkFailed();
        return;
    }

    const QByteArray body = reply->readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        emit checkFailed();
        return;
    }
    const QJsonObject obj = doc.object();
    if (!obj.contains(QStringLiteral("tag_name")) || !obj.value(QStringLiteral("tag_name")).isString()) {
        // No releases yet (GitHub 404 body) — soft fail.
        emit checkFailed();
        return;
    }

    const QString tag = obj.value(QStringLiteral("tag_name")).toString();
    if (!isNewer(tag, m_currentVersion)) {
        emit upToDate();
        return;
    }

    const QString htmlUrl = obj.value(QStringLiteral("html_url")).toString();
    emit updateAvailable(tag, htmlUrl);
}
