// Tests für MC5-Assembler und -Disassembler.
// Erwartete Codes aus der Siemens-Operationsliste S5-115U ("Auflistung des Maschinencodes")
// bzw. für 135U-Operationen aus dem Tabellenheft S5-135U/155U.
#include "mc5.h"
#include "parser.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace s5;
using namespace s5::mc5;

namespace {

int failures = 0;
std::string currentTest;

#define CHECK(cond)                                                                                  \
    do {                                                                                             \
        if (!(cond)) {                                                                               \
            std::printf("  FEHLER [%s] %s:%d: %s\n", currentTest.c_str(), __FILE__, __LINE__, #cond); \
            ++failures;                                                                              \
        }                                                                                            \
    } while (0)

std::string hexWords(const std::vector<uint16_t>& w)
{
    std::string s;
    for (uint16_t x : w) {
        char b[8];
        std::snprintf(b, sizeof b, "%s%04X", s.empty() ? "" : " ", x);
        s += b;
    }
    return s;
}

AssembleResult assembleText(const std::string& src, CpuFamily fam)
{
    ParseResult r = parse(src);
    for (const auto& e : r.errors) std::printf("  Parserfehler Zeile %d: %s\n", e.line, e.message.c_str());
    if (!r.ok()) ++failures;
    return assemble(r.program, fam);
}

// Code einer einzelnen Anweisung (ohne Kopf und ohne abschließendes BE)
std::string codeOf(const std::string& instr, CpuFamily fam = CpuFamily::S5_115U, bool inFb = false)
{
    std::string src = inFb ? "OB 1\n:SPA FB 1\n:BE\nFB 1\nNAME :TEST\n" : "OB 1\n";
    src += ":" + instr + "\n:BE\n";
    AssembleResult a = assembleText(src, fam);
    if (!a.ok()) {
        for (const auto& e : a.errors) std::printf("  Assemblerfehler '%s': %s\n", instr.c_str(), e.message.c_str());
        return "FEHLER";
    }
    const Mc5Block& b = inFb ? a.blocks.at(1) : a.blocks.at(0);
    const size_t start = inFb ? 10 : 5;
    std::vector<uint16_t> w(b.words.begin() + start, b.words.end() - 1);
    return hexWords(w);
}

#define CODE(instr, expected)                                                                        \
    do {                                                                                             \
        std::string got = codeOf(instr);                                                             \
        if (got != expected) {                                                                       \
            std::printf("  FEHLER [%s] '%s': %s statt %s\n", currentTest.c_str(), instr, got.c_str(), expected); \
            ++failures;                                                                              \
        }                                                                                            \
    } while (0)

#define CODE_AS(instr, fam, inFb, expected)                                                          \
    do {                                                                                             \
        std::string got = codeOf(instr, fam, inFb);                                                  \
        if (got != expected) {                                                                       \
            std::printf("  FEHLER [%s] '%s': %s statt %s\n", currentTest.c_str(), instr, got.c_str(), expected); \
            ++failures;                                                                              \
        }                                                                                            \
    } while (0)

void testBitOps()
{
    CODE("U E 1.0", "C001");
    CODE("U A 4.0", "C084");
    CODE("O E 0.7", "CF00");
    CODE("UN A 1.2", "E281");
    CODE("ON E 127.7", "EF7F");
    CODE("= A 4.3", "DB84");
    CODE("S E 2.1", "D102");
    CODE("R A 0.0", "F080");
    CODE("U M 10.3", "830A");
    CODE("S M 10.3", "930A");
    CODE("= M 0.0", "9800");
    CODE("R M 255.7", "B7FF");
    CODE("U T 5", "F805");
    CODE("O T 5", "F905");
    CODE("UN T 5", "FC05");
    CODE("ON Z 3", "BD03");
    CODE("U Z 3", "B803");
    CODE("R T 5", "3C05");
    CODE("R Z 2", "7C02");
    CODE("S Z 2", "5C02");
    CODE("O", "FB00");
    CODE("U(", "BA00");
    CODE("O(", "BB00");
    CODE(")", "BF00");
}

void testLoadTransfer()
{
    CODE("L EB 3", "4A03");
    CODE("L AB 3", "4A83");
    CODE("T EB 3", "4B03");
    CODE("L EW 4", "5204");
    CODE("T AW 6", "5386");
    CODE("L MB 7", "0A07");
    CODE("T MW 8", "1308");
    CODE("L DL 1", "2201");
    CODE("T DR 2", "2B02");
    CODE("L DW 3", "3203");
    CODE("T DW 4", "3304");
    CODE("L T 1", "0201");
    CODE("L Z 2", "4202");
    CODE("LC T 1", "0C01");
    CODE("LC Z 2", "4C02");
    CODE("L KB 12", "280C");
    CODE("L KF +100", "3004 0064");
    CODE("L KF -1", "3004 FFFF");
    CODE("L KH 00FF", "3040 00FF");
    CODE("L KT 050.1", "3002 1050");
    CODE("L KZ 010", "3001 0010");
    CODE("L KM 1000000000000001", "3080 8001");
    CODE("L KY 1,2", "3020 0102");
    CODE("L KC AB", "3010 4142");
}

void testTimersArithmeticBlocks()
{
    CODE("SI T 1", "3401");
    CODE("SV T 1", "1C01");
    CODE("SE T 1", "2401");
    CODE("SS T 1", "2C01");
    CODE("SA T 1", "1401");
    CODE("ZV Z 1", "6C01");
    CODE("ZR Z 1", "5401");
    CODE("+F", "7900");
    CODE("-F", "5900");
    CODE(">F", "2120");
    CODE("<F", "2140");
    CODE("><F", "2160");
    CODE("!=F", "2180");
    CODE(">=F", "21A0");
    CODE("<=F", "21C0");
    CODE("SPA PB 3", "7503");
    CODE("SPB PB 3", "5503");
    CODE("SPA SB 2", "7D02");
    CODE("SPB SB 2", "5D02");
    CODE("A DB 10", "200A");
    CODE("BEA", "6501");
    CODE("BEB", "0500");
    CODE("STP", "7003");
    CODE("NOP 0", "0000");
    CODE("NOP 1", "FFFF");
    CODE("BLD 130", "1082");
}

void testSupplementary()
{
    const auto f = CpuFamily::S5_115U;
    CODE_AS("UW", f, true, "4100");
    CODE_AS("OW", f, true, "4900");
    CODE_AS("XOW", f, true, "5100");
    CODE_AS("KEW", f, true, "0100");
    CODE_AS("KZW", f, true, "0900");
    CODE_AS("SLW 3", f, true, "6103");
    CODE_AS("SRW 15", f, true, "690F");
    CODE_AS("I 5", f, true, "1105");
    CODE_AS("D 1", f, true, "1901");
    CODE_AS("TAK", f, true, "7002");
    CODE_AS("P D 3.12", f, true, "7046 CC03");
    CODE_AS("PN D 3.12", f, true, "7046 8C03");
    CODE_AS("SU D 255.15", f, true, "7046 4FFF");
    CODE_AS("RU D 0.0", f, true, "7046 0000");
}

void test135Only()
{
    const auto f = CpuFamily::S5_135U;
    CODE_AS("XF", f, false, "6004");
    CODE_AS(":F", f, false, "6000");
    CODE_AS("U D 3.15", f, false, "783F 0F03");
    CODE_AS("O D 0.0", f, false, "783F 1000");
    CODE_AS("= D 255.0", f, false, "783F 60FF");
    CODE_AS("R D 1.1", f, false, "783F 5101");
    CODE_AS("P E 1.2", f, true, "7038 C201");
    CODE_AS("SU A 0.1", f, true, "7038 4180");
    CODE_AS("P M 3.4", f, true, "7049 C403");

    // auf 115U nicht erlaubt
    for (const char* src : {"OB 1\n:XF\n:BE\n", "OB 1\n:L KF +1\n:L KF +1\n::F\n:BE\n", "OB 1\n:U D 1.0\n:BE\n",
                            "OB 1\n:SPA FB 1\n:BE\nFB 1\n:P E 1.0\n:BE\n"}) {
        AssembleResult a = assemble(parse(src).program, CpuFamily::S5_115U);
        CHECK(!a.ok());
        CHECK(!a.errors.empty() && a.errors[0].message.find("135U") != std::string::npos);
    }
    // Datenwort > 255 ist im 1-Wort-Befehl nicht darstellbar
    CHECK(!assemble(parse("OB 1\n:L DW 300\n:BE\n").program, CpuFamily::S5_115U).ok());
}

void testJumps()
{
    // Sprungweite in Wörtern, bezogen auf den Sprungbefehl selbst
    AssembleResult a = assembleText(R"(
OB 1
:SPA FB 1
:BE
FB 1
NAME :SPRUNG
M001:L  KF +1        ; 2 Wörter
:SPB =M002
:SPA =M001
M002:BE
)", CpuFamily::S5_115U);
    CHECK(a.ok());
    const auto& w = a.blocks.at(1).words;
    // Kopf(5) + Parameterblock(5): M001 = 10, L KF = 10..11, SPB = 12, SPA = 13, M002/BE = 14
    CHECK(hexWords({w.begin() + 10, w.end()}) == "3004 0001 FA02 2DFD 6500");
}

void testFbEncoding()
{
    AssembleResult a = assembleText(R"(
OB 1
:SPA FB 10
NAME :MOTOR
EIN  :E 0.0
MOT  :A 4.1
WERT :KF +3
ZEIT :T 7
DBN  :DB 5
BYT  :MB 2
:BE

FB 10
NAME :MOTOR
BEZ  :EIN  E/A/D/B/T/Z: E  BI/BY/W/K: BI
BEZ  :MOT  E/A/D/B/T/Z: A  BI/BY/W/K: BI
BEZ  :WERT E/A/D/B/T/Z: D  KM/KH/KY/KC/KF/KT/KZ/KG: KF
BEZ  :ZEIT E/A/D/B/T/Z: T
BEZ  :DBN  E/A/D/B/T/Z: B
BEZ  :BYT  E/A/D/B/T/Z: E  BI/BY/W/K: BY
:U  =EIN
:=  =MOT
:L  =WERT
:SE =ZEIT
:R  =ZEIT
:B  =DBN
:L  =BYT
:T  =BYT
:BE
)", CpuFamily::S5_115U);
    CHECK(a.ok());
    const auto& ob = a.blocks.at(0).words;
    CHECK(hexWords(ob) == "7070 1001 0000 0000 000E 3D0A 2D07 C000 C184 0003 F807 2005 0A02 6500");
    const auto& fb = a.blocks.at(1).words;
    // Kopf: Typ FB = 08, Länge
    CHECK(fb[0] == 0x7070 && fb[1] == 0x080A && fb[4] == fb.size());
    // SPA= über Name + 6 Parameter (5 + 18 = 23 = 0x17), Name "MOTOR   "
    CHECK(hexWords({fb.begin() + 5, fb.begin() + 10}) == "2D17 4D4F 544F 5220 2020");
    // Parameter: Art/Format + 4 Zeichen Name
    CHECK(hexWords({fb.begin() + 10, fb.begin() + 13}) == "0280 4549 4E20");   // EIN  E BI
    CHECK(hexWords({fb.begin() + 13, fb.begin() + 16}) == "0880 4D4F 5420");   // MOT  A BI
    CHECK(hexWords({fb.begin() + 16, fb.begin() + 19}) == "0404 5745 5254");   // WERT D KF
    CHECK(hexWords({fb.begin() + 19, fb.begin() + 22}) == "0500 5A45 4954");   // ZEIT T
    CHECK(hexWords({fb.begin() + 22, fb.begin() + 25}) == "0700 4442 4E20");   // DBN  B
    CHECK(hexWords({fb.begin() + 25, fb.begin() + 28}) == "0240 4259 5420");   // BYT  E BY
    // Formaloperanden: U= 07, == 1F, L= 46, SE= 26, RD= 3E, B= 76, L= 46, T= 66
    CHECK(hexWords({fb.begin() + 28, fb.end()}) == "0701 1F02 4603 2604 3E04 7605 4606 6606 6500");

    // nicht dokumentierte Fälle werden abgelehnt statt geraten
    CHECK(!assemble(parse("OB 1\n:SPA FB 1\nX :Z 1\n:BE\nFB 1\nBEZ :X Z\n:ZV =X\n:BE\n").program,
                    CpuFamily::S5_115U).ok());
    CHECK(!assemble(parse("OB 1\n:SPA FB 1\nX :E 0.0\n:BE\nFB 1\nBEZ :X E BI\n:SPA FB 2\nY :=X\n:BE\n"
                          "FB 2\nBEZ :Y E BI\n:U =Y\n:BE\n").program,
                    CpuFamily::S5_115U).ok());
}

std::string readFile(const std::string& path)
{
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// AWL -> MC5 -> AWL -> MC5 muss bitgleich sein
void roundTrip(const std::string& name, const std::string& src, CpuFamily fam)
{
    ParseResult p1 = parse(src);
    CHECK(p1.ok());
    AssembleResult a1 = assemble(p1.program, fam);
    for (const auto& e : a1.errors) std::printf("  %s: Zeile %d: %s\n", name.c_str(), e.line, e.message.c_str());
    CHECK(a1.ok());

    std::vector<std::string> warn;
    std::vector<uint8_t> bytes = toBytes(a1.blocks);
    std::vector<Mc5Block> blocks = fromBytes(bytes, warn);
    CHECK(warn.empty());
    CHECK(blocks.size() == a1.blocks.size());

    DisassembleResult d = disassemble(blocks);
    for (const auto& w : d.warnings) std::printf("  %s: Warnung: %s\n", name.c_str(), w.c_str());
    CHECK(d.warnings.empty());

    ParseResult p2 = parse(d.source);
    for (const auto& e : p2.errors)
        std::printf("  %s: Rückübersetzung Zeile %d: %s\n", name.c_str(), e.line, e.message.c_str());
    CHECK(p2.ok());
    AssembleResult a2 = assemble(p2.program, fam);
    CHECK(a2.ok());
    CHECK(a2.blocks.size() == a1.blocks.size());
    for (size_t i = 0; i < a1.blocks.size() && i < a2.blocks.size(); ++i) {
        if (a1.blocks[i].words != a2.blocks[i].words) {
            std::printf("  %s: %s %d unterschiedlich\n    vorher:  %s\n    nachher: %s\n", name.c_str(),
                        blockTypeName(a1.blocks[i].type), a1.blocks[i].number, hexWords(a1.blocks[i].words).c_str(),
                        hexWords(a2.blocks[i].words).c_str());
            ++failures;
        }
    }
}

void testRoundTripExamples()
{
    for (const char* name : {"grundlagen", "zeiten", "zaehler", "ampel", "bausteine", "fb_parameter"})
        roundTrip(name, readFile(std::string(EXAMPLES_DIR) + "/" + name + ".awl"), CpuFamily::S5_115U);

    roundTrip("135U", R"(
OB 1
:A  DB 3
:U  D 1.15
:=  D 2.0
:L  KF +6
:L  KF +7
:XF
:T  DW 0
:L  KF +7
::F
:T  DW 1
:SPA FB 2
NAME :ALLES
X    :KT 010.2
Y    :KC 'ok'
Z    :KZ 123
W    :KY 3,4
V    :KM 0000000011110000
U    :KH BEEF
T    :EW 2
S    :DL 4
:BE
FB 2
NAME :ALLES
BEZ :X D KT
BEZ :Y D KC
BEZ :Z D KZ
BEZ :W D KY
BEZ :V D KM
BEZ :U D KH
BEZ :T E W
BEZ :S A BY
:L  =X
:L  =T
:T  =S
:P  D 0.1
:SU M 1.1
:RU A 3.3
:PN E 0.0
:BE
DB 3
KF +1
KF +2
KF +3
)", CpuFamily::S5_135U);
}

void testDisassemblyText()
{
    AssembleResult a = assembleText("OB 1\n:U E 0.0\n:UN M 1.1\n:= A 4.0\n***\n:L KT 050.1\n:SE T 3\n:BE\n",
                                    CpuFamily::S5_115U);
    DisassembleResult d = disassemble(a.blocks);
    CHECK(d.source.find("OB 1\n:U   E 0.0\n:UN  M 1.1\n:=   A 4.0\n***\n:L   KT 050.1\n:SE  T 3\n:BE\n")
          != std::string::npos);

    // unbekannte Codes werden als Kommentar ausgegeben und gemeldet
    Mc5Block b;
    b.type = BlockType::PB;
    b.number = 1;
    b.words = {0x7070, 0x0401, 0, 0, 8, 0x7201, 0xC000, 0x6500};   // L PB 1 (Peripherie)
    DisassembleResult u = disassemble({b});
    CHECK(!u.warnings.empty());
    CHECK(u.source.find("; nicht unterstützt: L PB") != std::string::npos);
}

void testBytes()
{
    AssembleResult a = assembleText("OB 1\n:SPA PB 1\n:BE\nPB 1\n:U E 0.0\n:= A 0.0\n:BE\nDB 2\nKF +5\n",
                                    CpuFamily::S5_115U);
    std::vector<uint8_t> bytes = toBytes(a.blocks);
    CHECK(bytes.size() == 2u * (7 + 8 + 6));
    CHECK(bytes[0] == 0x70 && bytes[1] == 0x70 && bytes[2] == 0x10 && bytes[3] == 0x01);
    // Füllbytes zwischen den Bausteinen (wie in Speicherabbildern) werden übersprungen
    std::vector<uint8_t> padded(bytes.begin(), bytes.begin() + 14);
    padded.insert(padded.end(), {0, 0, 0xFF, 0xFF});
    padded.insert(padded.end(), bytes.begin() + 14, bytes.end());
    std::vector<std::string> warn;
    std::vector<Mc5Block> blocks = fromBytes(padded, warn);
    CHECK(blocks.size() == 3);
    CHECK(blocks.size() == 3 && blocks[1].type == BlockType::PB && blocks[2].type == BlockType::DB
          && blocks[2].words.back() == 5);
}

}  // namespace

int main()
{
    const std::vector<std::pair<const char*, void (*)()>> tests = {
        {"Bitverknüpfungen", testBitOps},
        {"Laden/Transferieren", testLoadTransfer},
        {"Zeiten, Arithmetik, Bausteine", testTimersArithmeticBlocks},
        {"Ergänzende Operationen", testSupplementary},
        {"Nur S5-135U", test135Only},
        {"Sprünge", testJumps},
        {"FB-Kopf und Aufruf", testFbEncoding},
        {"Rundlauf AWL-MC5-AWL", testRoundTripExamples},
        {"Rückübersetzung Text", testDisassemblyText},
        {"Speicherabbild", testBytes},
    };
    for (const auto& [name, fn] : tests) {
        currentTest = name;
        int before = failures;
        fn();
        std::printf("%s %s\n", failures == before ? "ok    " : "FEHLER", name);
    }
    std::printf("\n%s (%d Fehler)\n", failures ? "TESTS FEHLGESCHLAGEN" : "Alle Tests bestanden", failures);
    return failures ? 1 : 0;
}
