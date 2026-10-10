#include "mainwindow.h"
#include "piconfigdialog.h"
#include "plccontroller.h"
#include "rackwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QLibraryInfo>
#include <QTranslator>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("S5Sim"));
    QApplication::setApplicationName(QStringLiteral("S5Sim"));
    QApplication::setApplicationVersion(QStringLiteral("0.2"));
    QApplication::setQuitOnLastWindowClosed(false);   // beendet wird über den Baugruppenträger

    // deutsche Texte für Qts eigene Dialoge und Knöpfe ("Abbrechen" statt "Cancel")
    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(QLocale::German), QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTranslator);

    // Entwickleroptionen, z.B. für automatische Bildschirmfotos
    QCommandLineParser cli;
    cli.addHelpOption();
    QCommandLineOption exampleOpt(QStringLiteral("example"), QStringLiteral("Beispiel Nr. laden (0..5)"), QStringLiteral("n"));
    QCommandLineOption inputsOpt(QStringLiteral("inputs"), QStringLiteral("Eingänge setzen, z.B. 0.0,0.6"), QStringLiteral("liste"));
    QCommandLineOption runOpt(QStringLiteral("run"), QStringLiteral("CPU starten"));
    QCommandLineOption stepOpt(QStringLiteral("step"), QStringLiteral("n Einzelschritte ausführen"), QStringLiteral("n"));
    QCommandLineOption editorOpt(QStringLiteral("editor"), QStringLiteral("Editor öffnen"));
    QCommandLineOption mc5Opt(QStringLiteral("mc5"), QStringLiteral("Maschinencode-Ansicht zeigen"));
    QCommandLineOption shotOpt(QStringLiteral("screenshot"), QStringLiteral("Baugruppenträger als PNG speichern und beenden"), QStringLiteral("datei"));
    QCommandLineOption shotEdOpt(QStringLiteral("screenshot-editor"), QStringLiteral("Editor als PNG speichern"), QStringLiteral("datei"));
    QCommandLineOption shotPiOpt(QStringLiteral("screenshot-pidialog"), QStringLiteral("Einstellungsdialog der Pi-Baugruppe als PNG speichern"), QStringLiteral("datei"));
    QCommandLineOption delayOpt(QStringLiteral("delay"), QStringLiteral("Wartezeit vor dem Bildschirmfoto (ms)"), QStringLiteral("ms"), QStringLiteral("1500"));
    cli.addOptions({exampleOpt, inputsOpt, runOpt, stepOpt, editorOpt, mc5Opt, shotOpt, shotEdOpt, shotPiOpt, delayOpt});
    cli.process(app);

    PlcController plc;
    MainWindow editor(&plc);
    RackWindow rack(&plc, &editor);
    rack.show();

    if (cli.isSet(exampleOpt)) editor.loadExample(cli.value(exampleOpt).toInt());
    for (const QString& in : cli.value(inputsOpt).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QStringList parts = in.split(QLatin1Char('.'));
        if (parts.size() == 2) plc.cpu().setInput(parts[0].toInt(), parts[1].toInt(), true);
    }
    if (cli.isSet(editorOpt) || cli.isSet(shotEdOpt)) rack.showEditor();
    if (cli.isSet(mc5Opt)) editor.showMc5View();
    if (cli.isSet(runOpt)) plc.run();
    for (int i = 0; i < cli.value(stepOpt).toInt(); ++i) plc.step();
    if (cli.isSet(shotPiOpt)) {
        auto* dlg = new PiConfigDialog(PiConfig(), &rack);
        dlg->show();
        const QString file = cli.value(shotPiOpt);
        QTimer::singleShot(cli.value(delayOpt).toInt(), dlg, [dlg, file] {
            dlg->grab().save(file);
            QApplication::quit();
        });
    }
    if (cli.isSet(shotOpt) || cli.isSet(shotEdOpt)) {
        const QString rackFile = cli.value(shotOpt), editorFile = cli.value(shotEdOpt);
        QTimer::singleShot(cli.value(delayOpt).toInt(), &rack, [&] {
            if (!rackFile.isEmpty()) rack.grab().save(rackFile);
            if (!editorFile.isEmpty()) editor.grab().save(editorFile);
            QApplication::quit();
        });
    }
    return app.exec();
}
