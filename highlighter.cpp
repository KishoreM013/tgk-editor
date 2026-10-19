#include "highlighter.h"

CppHighlighter::CppHighlighter(QTextDocument* parent)
    : QSyntaxHighlighter(parent) {
    // rules must be built before the first rehighlight (setTheme) — otherwise
    // empty comment patterns would loop forever during highlighting
    setLanguage(Lang::Cpp);
    setTheme(findTheme("classic"));
}

void CppHighlighter::setTheme(const Theme& t) {
    auto fmt = [](const QColor& c, bool italic = false, bool bold = false) {
        QTextCharFormat f;
        f.setForeground(c);
        if (italic)
            f.setFontItalic(true);
        if (bold)
            f.setFontWeight(QFont::Bold);
        return f;
    };

    m_formats.clear();
    m_formats.resize(8);
    m_formats[Keyword] = fmt(t.keyword, false, true);
    m_formats[Type] = fmt(t.type);
    m_formats[Str] = fmt(t.str);
    m_formats[Chr] = fmt(t.chr);
    m_formats[Number] = fmt(t.num);
    m_formats[Comment] = fmt(t.comment, true);
    m_formats[Function] = fmt(t.func);
    m_formats[Preproc] = fmt(t.preproc);

    rehighlight();
}

void CppHighlighter::setLanguage(Lang lang) {
    m_lang = lang;
    buildRules();
    rehighlight();
}

void CppHighlighter::buildRules() {
    m_rules.clear();
    m_commentStart = QRegularExpression("(?!)"); // matches nothing
    m_commentEnd = QRegularExpression("(?!)");

    QStringList keywords;
    QStringList types;

    switch (m_lang) {
    case Lang::Cpp:
        m_lineComment = "//";
        m_commentStart = QRegularExpression("/\\*");
        m_commentEnd = QRegularExpression("\\*/");
        keywords = {"alignas", "alignof", "and", "asm", "auto", "bool",
                    "break", "case", "catch", "class", "concept", "const",
                    "consteval", "constexpr", "constinit", "const_cast",
                    "continue", "co_await", "co_return", "co_yield", "decltype",
                    "default", "delete", "do", "dynamic_cast", "else", "enum",
                    "explicit", "export", "extern", "false", "for", "friend",
                    "goto", "if", "inline", "mutable", "namespace", "new",
                    "noexcept", "not", "nullptr", "operator", "or", "private",
                    "protected", "public", "register", "reinterpret_cast",
                    "requires", "return", "sizeof", "static", "static_assert",
                    "static_cast", "struct", "switch", "template", "this",
                    "thread_local", "throw", "true", "try", "typedef", "typeid",
                    "typename", "union", "using", "virtual", "volatile",
                    "while", "xor", "override", "final"};
        types = {"char", "char8_t", "char16_t", "char32_t", "double", "float",
                 "int", "long", "short", "signed", "unsigned", "void",
                 "wchar_t", "size_t", "ssize_t", "int8_t", "int16_t", "int32_t",
                 "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
                 "ptrdiff_t", "intptr_t", "uintptr_t", "nullptr_t", "string",
                 "wstring", "u8string", "u16string", "u32string", "vector",
                 "map", "set", "unordered_map", "unordered_set", "array",
                 "deque", "list", "queue", "stack", "pair", "tuple", "optional",
                 "variant", "unique_ptr", "shared_ptr", "weak_ptr", "function",
                 "any", "string_view", "span", "istream", "ostream", "iostream",
                 "fstream", "stringstream"};
        break;

    case Lang::Java:
        m_lineComment = "//";
        m_commentStart = QRegularExpression("/\\*");
        m_commentEnd = QRegularExpression("\\*/");
        keywords = {"abstract", "assert", "break", "case", "catch", "class",
                    "const", "continue", "default", "do", "else", "enum",
                    "extends", "final", "finally", "for", "goto", "if",
                    "implements", "import", "instanceof", "interface", "native",
                    "new", "package", "private", "protected", "public",
                    "record", "return", "sealed", "permits", "static",
                    "strictfp", "super", "switch", "synchronized", "this",
                    "throw", "throws", "transient", "try", "var", "while",
                    "yield", "true", "false", "null"};
        types = {"boolean", "byte", "char", "double", "float", "int", "long",
                 "short", "void", "String", "Integer", "Long", "Double",
                 "Float", "Boolean", "Character", "Object", "List", "ArrayList",
                 "Map", "HashMap", "Set", "HashSet", "Optional", "Stream",
                 "System", "Math", "Exception", "RuntimeException", "Thread"};
        break;

    case Lang::Python:
        m_lineComment = "#";
        keywords = {"and", "as", "assert", "async", "await", "break", "class",
                    "continue", "def", "del", "elif", "else", "except",
                    "finally", "for", "from", "global", "if", "import", "in",
                    "is", "lambda", "nonlocal", "not", "or", "pass", "raise",
                    "return", "try", "while", "with", "yield", "match", "case",
                    "True", "False", "None", "self"};
        types = {"print", "len", "range", "str", "int", "float", "bool", "list",
                 "dict", "set", "tuple", "open", "input", "type", "super",
                 "isinstance", "enumerate", "zip", "map", "filter", "sum",
                 "min", "max", "abs", "sorted", "reversed", "round",
                 "Exception", "ValueError", "TypeError", "RuntimeError",
                 "KeyError", "IndexError", "FileNotFoundError", "StopIteration",
                 "object", "property", "staticmethod", "classmethod"};
        break;

    case Lang::Plain:
        m_lineComment = "//";
        return;
    }

    for (const QString& w : keywords)
        m_rules.append({QRegularExpression("\\b" + w + "\\b"), Keyword});
    for (const QString& w : types)
        m_rules.append({QRegularExpression("\\b" + w + "\\b"), Type});

    if (m_lang == Lang::Python) {
        // function/class definitions: "def name", "class Name"
        // (fixed-length lookbehind — PCRE requirement)
        m_rules.append({QRegularExpression("(?<=def )[A-Za-z_]\\w*"), Function});
        m_rules.append(
            {QRegularExpression("(?<=class )[A-Za-z_]\\w*"), Function});
        // decorators
        m_rules.append({QRegularExpression("@[A-Za-z_]\\w*"), Function});
        m_rules.append({QRegularExpression(
                            "\\b(0[xX][0-9a-fA-F]+|(\\d+\\.?\\d*|\\.\\d+)"
                            "([eE][+-]?\\d+)?j?)[uUlLfF]*\\b"),
                        Number});
        m_rules.append({QRegularExpression("\"([^\"\\\\]|\\\\.)*\""), Str});
    } else {
        m_rules.append({QRegularExpression(
                            "\\b(0[xX][0-9a-fA-F]+|(\\d+\\.?\\d*|\\.\\d+)"
                            "([eE][+-]?\\d+)?)[uUlLfF]*\\b"),
                        Number});
        m_rules.append({QRegularExpression("\"([^\"\\\\]|\\\\.)*\""), Str});
        m_rules.append({QRegularExpression("'([^'\\\\]|\\\\.)*'"), Chr});
    }

    if (m_lang == Lang::Java) {
        // Java class names / annotations
        m_rules.append({QRegularExpression("@[A-Za-z_]\\w*"), Function});
        m_rules.append({QRegularExpression("\\b[A-Z][A-Za-z0-9_]*\\b"), Type});
    }

    m_rules.append(
        {QRegularExpression("\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*\\()"), Function});
}

void CppHighlighter::highlightBlock(const QString& text) {
    const bool python = (m_lang == Lang::Python);

    // Python triple-quoted strings (block state: 1 = ''', 2 = """)
    if (python) {
        int state = previousBlockState();
        int start = 0;
        if (state == 1 || state == 2) {
            const QString delim = (state == 1) ? "'''" : "\"\"\"";
            start = 0;
            while (start >= 0) {
                int end = text.indexOf(delim, start);
                if (end < 0) {
                    setCurrentBlockState(state);
                    setFormat(0, text.length(), m_formats.at(Str));
                    return;
                }
                setFormat(0, end + 3, m_formats.at(Str));
                start = text.indexOf(delim, end + 3);
                state = 0;
                if (start >= 0) {
                    setCurrentBlockState(0);
                    break;
                }
            }
            if (start < 0)
                return;
        } else {
            int t3 = text.indexOf("'''");
            int d3 = text.indexOf("\"\"\"");
            if (t3 >= 0 || d3 >= 0) {
                bool triple = (t3 >= 0 && (d3 < 0 || t3 < d3));
                const QString delim = triple ? "'''" : "\"\"\"";
                int begin = triple ? t3 : d3;
                int end = text.indexOf(delim, begin + 3);
                if (end < 0) {
                    setCurrentBlockState(triple ? 1 : 2);
                    setFormat(begin, text.length() - begin, m_formats.at(Str));
                } else {
                    setFormat(begin, end + 3 - begin, m_formats.at(Str));
                }
            }
        }
    }

    for (const Rule& r : m_rules) {
        auto it = r.pattern.globalMatch(text);
        while (it.hasNext()) {
            auto m = it.next();
            setFormat(int(m.capturedStart()), int(m.capturedLength()),
                      m_formats.at(r.role));
        }
    }

    // preprocessor line (C/C++ only)
    if (m_lang == Lang::Cpp && text.trimmed().startsWith('#')) {
        setFormat(0, text.length(), m_formats.at(Preproc));
        return;
    }

    // block comments (C/C++/Java)
    if (m_lang == Lang::Cpp || m_lang == Lang::Java) {
        setCurrentBlockState(0);
        int startIdx = 0;
        if (previousBlockState() != 1) {
            auto m = m_commentStart.match(text);
            startIdx = m.hasMatch() ? int(m.capturedStart()) : -1;
        }
        while (startIdx >= 0) {
            auto m = m_commentEnd.match(text, startIdx);
            if (m.hasMatch()) {
                int endIdx = int(m.capturedEnd());
                if (endIdx <= startIdx)
                    break; // zero-length match: never loop forever
                setFormat(startIdx, endIdx - startIdx, m_formats.at(Comment));
                startIdx = text.indexOf(m_commentStart, endIdx);
                if (startIdx >= 0 &&
                    m_commentStart.match(text, startIdx).capturedLength() == 0)
                    break;
            } else {
                setCurrentBlockState(1);
                setFormat(startIdx, text.length() - startIdx,
                          m_formats.at(Comment));
                startIdx = -1;
            }
        }
    }

    // line comment
    const int li = text.indexOf(m_lineComment);
    if (li >= 0) {
        int quotes = 0;
        for (int i = 0; i < li; ++i)
            if (text[i] == '"' || text[i] == '\'')
                ++quotes;
        if (quotes % 2 == 0)
            setFormat(li, text.length() - li, m_formats.at(Comment));
    }
}
