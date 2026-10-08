#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("S5Sim"));
    QApplication::setApplicationName(QStringLiteral("S5Sim"));
    QApplication::setApplicationVersion(QStringLiteral("0.1"));

    // Entwickleroptionen, z.B. für automatische Bildschirmfotos
    QCommandLineParser cli;
    cli.addHelpOption();
    QCommandLineOption exampleOpt(QStringLiteral("example"), QStringLiteral("Beispiel Nr. laden (0..4)"), QStringLiteral("n"));
    QCommandLineOption inputsOpt(QStringLiteral("inputs"), QStringLiteral("Eingänge setzen, z.B. 0.0,0.6"), QStringLiteral("liste"));
    QCommandLineOption runOpt(QStringLiteral("run"), QStringLiteral("CPU starten"));
    QCommandLineOption stepOpt(QStringLiteral("step"), QStringLiteral("n Einzelschritte ausführen"), QStringLiteral("n"));
    QCommandLineOption mc5Opt(QStringLiteral("mc5"), QStringLiteral("Maschinencode-Ansicht zeigen"));
    QCommandLineOption shotOpt(QStringLiteral("screenshot"), QStringLiteral("Fenster als PNG speichern und beenden"), QStringLiteral("datei"));
    QCommandLineOption delayOpt(QStringLiteral("delay"), QStringLiteral("Wartezeit vor dem Bildschirmfoto (ms)"), QStringLiteral("ms"), QStringLiteral("1500"));
    cli.addOptions({exampleOpt, inputsOpt, runOpt, stepOpt, mc5Opt, shotOpt, delayOpt});
    cli.process(app);

    MainWindow w;
    w.show();

    if (cli.isSet(exampleOpt)) w.loadExample(cli.value(exampleOpt).toInt());
    for (const QString& in : cli.value(inputsOpt).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QStringList parts = in.split(QLatin1Char('.'));
        if (parts.size() == 2) w.cpu().setInput(parts[0].toInt(), parts[1].toInt(), true);
    }
    if (cli.isSet(mc5Opt)) w.showMc5View();
    if (cli.isSet(runOpt)) w.runCpu();
    for (int i = 0; i < cli.value(stepOpt).toInt(); ++i) w.stepCpu();
    if (cli.isSet(shotOpt)) {
        const QString file = cli.value(shotOpt);
        QTimer::singleShot(cli.value(delayOpt).toInt(), &w, [&w, file] {
            w.grab().save(file);
            QApplication::quit();
        });
    }
    return app.exec();
}
