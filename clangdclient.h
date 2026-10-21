#pragma once

#include <QObject>
#include <QProcess>
#include <QVector>
#include <QHash>
#include <QString>
#include <QJsonObject>
#include <functional>

// One completion candidate coming back from clangd.
struct ClangdItem {
    QString label;        // e.g. "push_back"
    QString detail;       // e.g. "void (const value_type &)"
    QString insertText;   // text to insert (snippet placeholders stripped)
    int kind = 0;         // LSP CompletionItemKind
    bool hasRange = false; // replacement range in UTF-16 offsets
    int startLine = -1, startCol = -1, endLine = -1, endCol = -1;
};

struct ClangdDiagnostic {
    QString message;
    int severity = 1; // 1=error 2=warning 3=note
    int line = 0, col = 0;
    int endLine = 0, endCol = 0; // LSP range end (0-based)
    bool unnecessary = false;    // LSP DiagnosticTag.Unnecessary
};

// Minimal LSP client that talks to the clangd language server over stdio.
// Single-threaded; must live on the GUI thread.
class ClangdClient : public QObject {
    Q_OBJECT
public:
    explicit ClangdClient(QObject* parent = nullptr);
    ~ClangdClient() override;

    bool start(const QString& rootDir,
               const QString& compileCommandsDir = QString());
    bool isRunning() const;

    void openDocument(const QString& path, const QString& text);
    void changeDocument(const QString& path, const QString& text);
    void closeDocument(const QString& path);

    using CompletionCallback =
        std::function<void(bool ok, const QVector<ClangdItem>& items)>;

    // line / character are 0-based, UTF-16 code units (LSP convention).
    void completion(const QString& path, int line, int character,
                    CompletionCallback cb);

signals:
    void diagnostics(const QString& path, const QVector<ClangdDiagnostic>& diags);
    void serverDied();

private:
    void sendRaw(const QJsonObject& msg);
    void sendRequest(const QString& method, const QJsonObject& params);
    void sendNotification(const QString& method, const QJsonObject& params);
    void enqueue(std::function<void()> fn);
    void flushQueue();
    void processBuffer();
    void handlePayload(const QByteArray& json);

    QProcess* m_proc = nullptr;
    QByteArray m_buf;
    qint64 m_expected = 0;
    int m_nextId = 1;
    int m_initId = 0;
    bool m_initialized = false;
    QString m_rootUri;

    struct Pending {
        int id;
        CompletionCallback cb;
    };
    QVector<Pending> m_pending;
    QHash<QString, int> m_versions;
    QHash<QString, int> m_diagVersions; // last published diagnostics version
    QHash<QString, QString> m_lastText; // avoid duplicate didChange messages
    QVector<std::function<void()>> m_queue;
};
