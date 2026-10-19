// Generates the tgk-cpp app icon (rounded blue tile with "C++").
// Built as a separate headless tool; run with QT_QPA_PLATFORM=offscreen.
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QFont>
#include <QLinearGradient>
#include <QPainterPath>
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: icon_gen <output.png>\n");
        return 1;
    }
    QGuiApplication app(argc, argv);

    QImage img(256, 256, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    // background tile: C++ blue gradient with a darker rim
    QLinearGradient g(0, 0, 0, 256);
    g.setColorAt(0.0, QColor("#0B6A9E"));
    g.setColorAt(1.0, QColor("#003A5C"));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRoundedRect(QRectF(8, 8, 240, 240), 48, 48);

    p.setPen(QPen(QColor("#00243A"), 6));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(11, 11, 234, 234), 45, 45);

    // " </> " label — language-neutral
    QFont f("Sans Serif", 72, QFont::Bold);
    p.setFont(f);
    p.setPen(QColor("#FFFFFF"));
    QRectF textRect(8, 8, 240, 240);
    p.drawText(textRect, Qt::AlignCenter, "</>");

    p.end();
    if (!img.save(QString::fromLocal8Bit(argv[1]))) {
        fprintf(stderr, "failed to save %s\n", argv[1]);
        return 1;
    }
    return 0;
}
