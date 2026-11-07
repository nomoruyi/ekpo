#include "pf2.h"
#include "testmain.h"

#include <QFile>
#include <QStandardPaths>

class TestPf2 : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void buildsAndReads();
    void measuresLikeGrub();
    void renders();
    void refusesDamagedFonts();
    void refusesUnreadableFont();

private:
    QByteArray m_data;
    std::optional<Pf2Font> m_font;
};

void TestPf2::initTestCase()
{
    if (QStandardPaths::findExecutable(QStringLiteral("grub-mkfont")).isEmpty())
        QSKIP("grub-mkfont isn't installed (grub-common)");
    QString error;
    m_data = makePf2(QStringLiteral("/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf"), 24, QStringLiteral("Ekpo Item"), error);
    QVERIFY2(!m_data.isEmpty(), qPrintable(error));
    m_font = Pf2Font::read(m_data, error);
    QVERIFY2(m_font, qPrintable(error));
}

void TestPf2::buildsAndReads()
{
    const Pf2Font &f = *m_font;
    QCOMPARE(f.name, QStringLiteral("Ekpo Item Regular 24")); // what theme.txt has to name
    QCOMPARE(f.family, QStringLiteral("Ekpo Item"));
    QCOMPARE(f.pointSize, 24); // pixels: the em; accented capitals make lines taller
    QVERIFY(f.ascent + f.descent >= 24 && f.ascent + f.descent <= 32);
    for (const char32_t c : {U'K', U'é', U'Ł', U'€', U'…', U'–'}) // Noto Sans has no arrows
        QVERIFY2(f.glyphs.contains(c), qPrintable(QString::fromUcs4(&c, 1)));
    QVERIFY(!f.glyphs.contains(U'ж')); // only the ranges a menu needs
    QVERIFY(f.glyphs.value(U'K').bitmap.size() > 0);
}

void TestPf2::measuresLikeGrub()
{
    const Pf2Font &f = *m_font;
    int sum = 0;
    for (const QChar c : QStringLiteral("Kubuntu"))
        sum += f.glyphs.value(c.unicode()).advance;
    QCOMPARE(f.textWidth(QStringLiteral("Kubuntu")), sum);
    QVERIFY(sum > 60 && sum < 120);
    // A missing character takes the widest glyph's room.
    QCOMPARE(f.textWidth(QStringLiteral("Kж")), f.glyphs.value(U'K').advance + f.maxWidth);
}

void TestPf2::renders()
{
    const QImage image = m_font->render(QStringLiteral("Kubuntu"), Qt::white);
    QCOMPARE(image.size(), QSize(m_font->textWidth(QStringLiteral("Kubuntu")), m_font->ascent + m_font->descent));
    int ink = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x)
            ink += qAlpha(image.pixel(x, y)) == 255;
    }
    // Some of it is text, most of it isn't; "K" reaches from the baseline up.
    QVERIFY(ink > 50 && ink < image.width() * image.height() / 2);
    QCOMPARE(qAlpha(image.pixel(1, m_font->ascent + 2)), 0); // below the baseline beside "K"
}

void TestPf2::refusesDamagedFonts()
{
    QString error;
    QVERIFY(!Pf2Font::read(QByteArray("not a font"), error));
    QCOMPARE(error, QStringLiteral("not a GRUB .pf2 font"));
    QVERIFY(!Pf2Font::read(m_data.left(200), error)); // the index cut off
    QByteArray badOffset = m_data;
    const qsizetype index = badOffset.indexOf("CHIX") + 8;
    badOffset[index + 5] = char(0x7f); // the first glyph's offset far past the end
    QVERIFY(!Pf2Font::read(badOffset, error));
    QCOMPARE(error, QStringLiteral("a glyph lies outside the file"));
}

void TestPf2::refusesUnreadableFont()
{
    // grub-mkfont exits 0 here and writes an empty font; that must not pass.
    QString error;
    QVERIFY(makePf2(QStringLiteral("/nonexistent/font.ttf"), 24, QStringLiteral("X"), error).isEmpty());
    QVERIFY2(error.startsWith(QStringLiteral("grub-mkfont made no usable font from /nonexistent/font.ttf: ")), qPrintable(error));
}

EKPO_TEST_MAIN(TestPf2)
#include "tst_pf2.moc"
