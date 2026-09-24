#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

// Background "Check for Updates" against GitHub releases/latest.
// Same behavior as zedit: non-blocking, ~5s timeout, soft-fail, no-op
// while a check is already in flight. Semver compare tolerates leading 'v'
// and missing parts (= 0).
class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    explicit UpdateChecker(QObject *parent = nullptr);

    bool isChecking() const { return m_checking; }

public slots:
    // Safe to re-trigger while idle; ignored while in flight.
    void checkForUpdates(const QString &currentVersion,
                         const QString &repo = QStringLiteral("sbj-ee/zwriter"));

signals:
    // Emitted only when a newer release is found (never on "up to date" /
    // network failure / no releases — those fail soft).
    void updateAvailable(const QString &tagName, const QString &htmlUrl);
    // Optional: emitted when the check finishes with no newer release
    // (manual Check for Updates can show a brief status). Silent otherwise.
    void upToDate();
    void checkFailed();

private slots:
    void onFinished(QNetworkReply *reply);
    void onTimeout();

private:
    static QList<int> parseSemver(QStringView s);
    static bool isNewer(const QString &tag, const QString &current);

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    QTimer *m_timeout = nullptr;
    QString m_currentVersion;
    bool m_checking = false;
};
