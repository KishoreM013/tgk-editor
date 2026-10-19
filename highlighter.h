#pragma once

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>
#include <QVector>

#include "themeh.h"

// Syntax highlighter supporting several languages. Colors come from the
// active Theme.
class CppHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    enum class Lang { Cpp, Java, Python, Plain };

    explicit CppHighlighter(QTextDocument* parent = nullptr);

    void setTheme(const Theme& theme);
    void setLanguage(Lang lang);
    Lang language() const { return m_lang; }

    // Line-comment marker for the current language ("//" or "#").
    QString lineCommentPrefix() const { return m_lineComment; }

protected:
    void highlightBlock(const QString& text) override;

private:
    enum Role { Keyword, Type, Str, Chr, Number, Comment, Function, Preproc };

    struct Rule {
        QRegularExpression pattern;
        int role = 0;
    };

    void buildRules();
    Lang m_lang = Lang::Cpp;
    QVector<Rule> m_rules;
    QVector<QTextCharFormat> m_formats; // indexed by Role
    QString m_lineComment = "//";
    QRegularExpression m_commentStart;
    QRegularExpression m_commentEnd;
};
