#pragma once

#include <QColor>
#include <QString>
#include <QVector>

// A complete color scheme for the whole IDE.
struct Theme {
    QString id;
    QString name;
    // editor
    QColor editorBg, editorFg, currentLineBg, selectionBg;
    // line-number gutter
    QColor lineNumBg, lineNumFg, lineNumActiveFg;
    // console / problems
    QColor consoleBg, consoleFg;
    // syntax tokens
    QColor keyword, type, str, chr, num, comment, func, preproc;
    // diagnostics (squiggles in the editor and gutter markers)
    QColor diagError, diagWarning, diagInfo;
    // window chrome (optional; invalid = keep the system palette). Zed-style
    // themes fill these in so the whole window matches the editor.
    QColor windowBg, panelBg, border, mutedFg, accent;
};

inline QVector<Theme> builtinThemes() {
    return {
        {// Zed One Dark — the default; palette taken from Zed's One theme
         "zed-dark", "Zed One Dark",
         "#282C33", "#ACB2BE", "#2D323A", "#3D74ADE8",
         "#282C33", "#4E5A5F", "#D0D4DA",
         "#282C33", "#ACB2BE",
         "#B477CF", "#6EB4BF", "#A1C181", "#A1C181", "#BF956A",
         "#5D636F", "#73ADE9", "#B477CF",
         "#D07277", "#DEC184", "#74ADE8",
         "#3B414D", "#2F343E", "#464B57", "#A9AFBC", "#74ADE8"},

        {// Zed One Light
         "zed-light", "Zed One Light",
         "#FAFAFA", "#242529", "#F0F0F1", "#3D5C78E2",
         "#FAFAFA", "#B4B4BB", "#44454B",
         "#FAFAFA", "#242529",
         "#A449AB", "#3882B7", "#649F57", "#649F57", "#AD6E25",
         "#A2A3A7", "#5B79E3", "#A449AB",
         "#D36151", "#A48819", "#5C78E2",
         "#DCDCDD", "#EBEBEC", "#C9C9CA", "#58585A", "#5C78E2"},

        {// 1. Dev-C++ classic light — comfortable default
         "classic", "Dev-C++ Classic (Light)",
         "#FFFFFF", "#1E1E1E", "#EAF1FB", "#BBD7F0",
         "#F0F0F0", "#8A8A8A", "#1E1E1E",
         "#F7F7F7", "#1E1E1E",
         "#000080", "#008080", "#A31515", "#A31515", "#098658",
         "#6A9955", "#795E26", "#AF00DB",
         "#D13438", "#B7791F", "#2B6CB0"},

        {// 2. Dark (VS Code dark)
         "dark", "Dark",
         "#1E1E1E", "#D4D4D4", "#2A2D2E", "#264F78",
         "#252526", "#6E7681", "#AEAFAD",
         "#181818", "#CCCCCC",
         "#569CD6", "#4EC9B0", "#CE9178", "#CE9178", "#B5CEA8",
         "#6A9955", "#DCDCAA", "#C586C0",
         "#F14C4C", "#CCA700", "#3794FF"},

        {// 3. Monokai
         "monokai", "Monokai",
         "#272822", "#F8F8F2", "#3E3D32", "#49483E",
         "#272822", "#75715E", "#F8F8F2",
         "#1E1F1C", "#F8F8F2",
         "#F92672", "#66D9EF", "#E6DB74", "#E6DB74", "#AE81FF",
         "#75715E", "#A6E22E", "#F92672",
         "#F92672", "#E6DB74", "#66D9EF"},

        {// 4. Solarized Light
         "solarized-light", "Solarized Light",
         "#FDF6E3", "#657B83", "#EEE8D5", "#D3CBB7",
         "#EEE8D5", "#93A1A1", "#586E75",
         "#FDF6E3", "#657B83",
         "#268BD2", "#B58900", "#2AA198", "#2AA198", "#D33682",
         "#93A1A1", "#268BD2", "#CB4B16",
         "#DC322F", "#B58900", "#268BD2"},

        {// 5. Solarized Dark
         "solarized-dark", "Solarized Dark",
         "#002B36", "#839496", "#073642", "#073642",
         "#073642", "#586E75", "#93A1A1",
         "#002B36", "#93A1A1",
         "#268BD2", "#B58900", "#2AA198", "#2AA198", "#D33682",
         "#586E75", "#268BD2", "#CB4B16",
         "#DC322F", "#B58900", "#268BD2"},
    };
}

inline Theme findTheme(const QString& id) {
    for (const Theme& t : builtinThemes())
        if (t.id == id)
            return t;
    return builtinThemes().front();
}
