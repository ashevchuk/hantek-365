// GUI tests against tests/meter_emulator.py, driven through the widgets only.

#include <QtTest>
#include <QAbstractAxis>
#include <QChartView>
#include <QComboBox>
#include <QDateTimeAxis>
#include <QLabel>
#include <QLineSeries>
#include <QProcess>
#include <QPushButton>

#include <memory>

#include "mainwindow.h"

class GuiTest : public QObject
{
    Q_OBJECT

private slots:
    void cleanup();

    void overloadIsShownButNotPlotted();
    void overloadCapturedFromRealMeterIsShown();
    void unacknowledgedModeIsNotHighlighted();
    void clickDuringModeChangeIsNotHighlighted();
    void modeChangeWhilePausedStaysPaused();
    void relButtonFollowsTheMeter();
    void zoomSurvivesNewReadings();
    void staleReadingsAfterCommandsAreDropped();

private:
    static constexpr int TIMEOUT_MS = 5000;
    static constexpr double READING_MV = 34.6;   // emulator's regular reading

    void startAndConnect(const QStringList &emulatorArgs);
    void connectToEmulator(const QStringList &emulatorArgs);
    QPushButton *button(const QString &text) const;
    QLineSeries *series() const;
    QDateTimeAxis *timeAxis() const;
    bool chartHoldsOnlyRegularReadings() const;

    std::unique_ptr<MainWindow> m_window;
    std::unique_ptr<QProcess>   m_emulator;
    QLabel                     *m_valueLabel = nullptr;
};

void GuiTest::startAndConnect(const QStringList &emulatorArgs)
{
    m_emulator = std::make_unique<QProcess>();
    m_emulator->start(PYTHON_EXECUTABLE, QStringList{"-I", METER_EMULATOR} + emulatorArgs);
    QVERIFY2(m_emulator->waitForReadyRead(TIMEOUT_MS), "emulator did not start");
    const QString pty = QString::fromLatin1(m_emulator->readLine()).trimmed();
    QVERIFY2(pty.startsWith("/dev/pts/"), qPrintable("emulator reported: " + pty));

    m_window = std::make_unique<MainWindow>();
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));

    for (auto *label : m_window->findChildren<QLabel *>())
        if (label->text() == "No data")
            m_valueLabel = label;
    QVERIFY(m_valueLabel);

    auto *ports = m_window->findChild<QComboBox *>();
    ports->addItem(pty);
    ports->setCurrentText(pty);
    QTest::mouseClick(button("Connect"), Qt::LeftButton);
}

void GuiTest::connectToEmulator(const QStringList &emulatorArgs)
{
    startAndConnect(emulatorArgs);
    if (QTest::currentTestFailed())
        return;

    // The window selects VDC after connecting
    QTRY_VERIFY_WITH_TIMEOUT(button("VDC")->isChecked(), TIMEOUT_MS);
    QTRY_VERIFY_WITH_TIMEOUT(series()->count() > 0, TIMEOUT_MS);
}

void GuiTest::cleanup()
{
    m_window.reset();
    if (m_emulator) {
        m_emulator->kill();
        m_emulator->waitForFinished();
        m_emulator.reset();
    }
    m_valueLabel = nullptr;
}

QPushButton *GuiTest::button(const QString &text) const
{
    for (auto *b : m_window->findChildren<QPushButton *>())
        if (b->text() == text)
            return b;
    qFatal("no button '%s'", qPrintable(text));
}

QLineSeries *GuiTest::series() const
{
    return qobject_cast<QLineSeries *>(
        m_window->findChild<QChartView *>()->chart()->series().first());
}

QDateTimeAxis *GuiTest::timeAxis() const
{
    return qobject_cast<QDateTimeAxis *>(
        m_window->findChild<QChartView *>()->chart()->axes(Qt::Horizontal).first());
}

bool GuiTest::chartHoldsOnlyRegularReadings() const
{
    const auto points = series()->points();
    return !points.isEmpty()
        && std::all_of(points.begin(), points.end(), [](const QPointF &p) {
               return qFuzzyCompare(p.y(), READING_MV);
           });
}

void GuiTest::overloadIsShownButNotPlotted()
{
    connectToEmulator({"--script", "ol"});
    if (QTest::currentTestFailed())
        return;
    QTRY_VERIFY_WITH_TIMEOUT(m_valueLabel->text().startsWith("OL"), TIMEOUT_MS);
    QVERIFY(chartHoldsOnlyRegularReadings());
}

void GuiTest::overloadCapturedFromRealMeterIsShown()
{
    // The real packet is an overload in ohms: nothing is ever plotted,
    // so connectToEmulator() would wait for points in vain.
    startAndConnect({"--script", "ol-real"});
    if (QTest::currentTestFailed())
        return;
    QTRY_VERIFY_WITH_TIMEOUT(m_valueLabel->text().startsWith("OL"), TIMEOUT_MS);
    QVERIFY(m_valueLabel->text().contains(QChar(0x03A9)));   // Ω
    QCOMPARE(series()->count(), 0);
}

void GuiTest::unacknowledgedModeIsNotHighlighted()
{
    connectToEmulator({"--no-ack", "e2"});
    if (QTest::currentTestFailed())
        return;
    QTest::mouseClick(button("6kohm"), Qt::LeftButton);
    QVERIFY(!button("6kohm")->isChecked());
    // The meter acknowledges E0 and E1, then ignores E2 until the 500 ms timeout
    QTRY_VERIFY_WITH_TIMEOUT(!button("VDC")->isChecked(), TIMEOUT_MS);
    QVERIFY(!button("6kohm")->isChecked());
}

void GuiTest::clickDuringModeChangeIsNotHighlighted()
{
    connectToEmulator({});
    if (QTest::currentTestFailed())
        return;
    QTest::mouseClick(button("60VDC"), Qt::LeftButton);
    QTest::mouseClick(button("VAC"), Qt::LeftButton);   // ignored: change in progress
    QVERIFY(!button("VAC")->isChecked());
    QTRY_VERIFY_WITH_TIMEOUT(button("60VDC")->isChecked(), TIMEOUT_MS);
    QVERIFY(!button("VAC")->isChecked());
}

void GuiTest::modeChangeWhilePausedStaysPaused()
{
    connectToEmulator({});
    if (QTest::currentTestFailed())
        return;
    QPushButton *pause = button("⏸ Pause");
    QTest::mouseClick(pause, Qt::LeftButton);
    QTest::mouseClick(button("VAC"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(button("VAC")->isChecked(), TIMEOUT_MS);

    const int points = series()->count();
    QTest::qWait(1500);   // several poll intervals
    QCOMPARE(series()->count(), points);
    QVERIFY(pause->isChecked());
}

void GuiTest::relButtonFollowsTheMeter()
{
    connectToEmulator({});
    if (QTest::currentTestFailed())
        return;
    QPushButton *rel = button("REL");

    QTest::mouseClick(rel, Qt::LeftButton);
    QVERIFY(!rel->isChecked());   // unchanged until the meter reports REL
    QTRY_VERIFY_WITH_TIMEOUT(rel->isChecked(), TIMEOUT_MS);
    QVERIFY(m_valueLabel->text().contains("REL"));

    QTest::mouseClick(rel, Qt::LeftButton);
    QVERIFY(rel->isChecked());
    QTRY_VERIFY_WITH_TIMEOUT(!rel->isChecked(), TIMEOUT_MS);
}

void GuiTest::zoomSurvivesNewReadings()
{
    connectToEmulator({});
    if (QTest::currentTestFailed())
        return;
    QTRY_VERIFY_WITH_TIMEOUT(series()->count() >= 5, TIMEOUT_MS);

    QWidget *viewport = m_window->findChild<QChartView *>()->viewport();
    const QPoint from(viewport->width() * 3 / 10, viewport->height() * 3 / 10);
    const QPoint to(viewport->width() * 6 / 10, viewport->height() * 7 / 10);
    QTest::mousePress(viewport, Qt::LeftButton, {}, from);
    QTest::mouseMove(viewport, to);
    QTest::mouseRelease(viewport, Qt::LeftButton, {}, to);

    const QDateTime zoomedMin = timeAxis()->min(), zoomedMax = timeAxis()->max();
    const int points = series()->count();
    QTRY_VERIFY_WITH_TIMEOUT(series()->count() >= points + 3, TIMEOUT_MS);
    QCOMPARE(timeAxis()->min(), zoomedMin);
    QCOMPARE(timeAxis()->max(), zoomedMax);

    QTest::mouseClick(viewport, Qt::RightButton, {}, from);
    QTRY_COMPARE_WITH_TIMEOUT(
        double(timeAxis()->max().toMSecsSinceEpoch()),
        series()->at(series()->count() - 1).x(), TIMEOUT_MS);
}

void GuiTest::staleReadingsAfterCommandsAreDropped()
{
    connectToEmulator({"--stale"});
    if (QTest::currentTestFailed())
        return;
    QTest::mouseClick(button("60VDC"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(button("60VDC")->isChecked(), TIMEOUT_MS);
    QTest::mouseClick(button("REL"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(button("REL")->isChecked(), TIMEOUT_MS);
    QTest::qWait(500);
    QVERIFY(chartHoldsOnlyRegularReadings());
}

QTEST_MAIN(GuiTest)
#include "gui_test.moc"
