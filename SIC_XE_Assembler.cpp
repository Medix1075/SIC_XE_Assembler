// SIC/XE Assembler (C++17) — Blocks, Literals, Expressions, EQU, ORG, Multi-M
// Educational subset for coursework.
//
// Implemented:
// - Directives: START, END, BYTE, WORD, RESB, RESW, BASE, NOBASE, LTORG, USE, EQU, ORG
// - Literals: =C'..', =X'..', =W'<expr>' with LTORG
// - Program blocks: USE <name>
// - Opcodes: broad subset (fmt 3/4) + fmt 2 (CLEAR, COMPR, ADDR, TIXR)
// - Addressing: simple, immediate (#), indirect (@), indexed ,X (with or without space)
// - Object program: H/T/M/E; multiple M records for expression terms (WORD & fmt-4)
//
// Limitations:
// - No control sections (CSECT/EXTDEF/EXTREF)
// - ORG must be absolute (only constants/absolute expressions)
// - For expressions, relocatable when (#plus − #minus) == 1; otherwise absolute
//
// Build: g++ -std=c++17 -O2 -o sicxe main.cpp
// Run:   ./sicxe input.asm listing.lst object.obj

#include <bits/stdc++.h>
using namespace std;

struct Line {
    string raw, label, op, operand;
    bool plus = false;
    int blk = 0;   // program block id
    int off = 0;   // offset within block (pass 1)
    int addr = 0;  // final absolute address
    string obj = "";
};

struct OpInfo {
    int opcode;
    vector<int> formats;
};

static map<string, OpInfo> OPTAB = {
    {"ADD",   {0x18, {3, 4}}}, {"ADDF",  {0x58, {3, 4}}}, {"ADDR", {0x90, {2}}},
    {"SUB",   {0x1C, {3, 4}}}, {"MUL",   {0x20, {3, 4}}}, {"DIV",  {0x24, {3, 4}}}, 
    {"COMP",  {0x28, {3, 4}}}, {"COMPF", {0x88, {3, 4}}}, {"COMPR",{0xA0, {2}}},
    {"AND",   {0x40, {3, 4}}}, {"OR",    {0x44, {3, 4}}},
    {"LDA",   {0x00, {3, 4}}}, {"LDX",   {0x04, {3, 4}}}, {"LDL",  {0x08, {3, 4}}},
    {"LDB",   {0x68, {3, 4}}}, {"LDS",   {0x6C, {3, 4}}}, {"LDT",  {0x74, {3, 4}}},
    {"STA",   {0x0C, {3, 4}}}, {"STX",   {0x10, {3, 4}}}, {"STL",  {0x14, {3, 4}}},
    {"STB",   {0x78, {3, 4}}}, {"STS",   {0x7C, {3, 4}}}, {"STT",  {0x84, {3, 4}}},
    {"LDCH",  {0x50, {3, 4}}}, {"STCH",  {0x54, {3, 4}}},
    {"TIX",   {0x2C, {3, 4}}}, {"TIXR",  {0xB8, {2}}},
    {"J",     {0x3C, {3, 4}}}, {"JEQ",   {0x30, {3, 4}}}, {"JGT",  {0x34, {3, 4}}}, {"JLT", {0x38, {3, 4}}}, {"JSUB", {0x48, {3, 4}}}, {"RSUB", {0x4C, {3}}},
    {"CLEAR", {0xB4, {2}}}
};

static set<string> DIRECT = {
    "START","END","BYTE","WORD","RESB","RESW","BASE","NOBASE","LTORG","USE","EQU","ORG"
};

static string trim(const string& s) {
    size_t i = 0, j = s.size();
    while (i < j && isspace((unsigned char)s[i])) ++i;
    while (j > i && isspace((unsigned char)s[j - 1])) --j;
    return s.substr(i, j - i);
}

static vector<string> split_ws(const string& s) {
    vector<string> out; string cur;
    for (char c : s) {
        if (isspace((unsigned char)c)) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

static bool is_dec(const string& s) {
    if (s.empty()) return false;
    int i = (s[0] == '+' || s[0] == '-') ? 1 : 0;
    if (i == (int)s.size()) return false;
    for (; i < (int)s.size(); ++i) if (!isdigit((unsigned char)s[i])) return false;
    return true;
}

static bool is_hex_pref(const string& s) {
    if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) {
        if (s.size() <= 2) return false;
        for (size_t i = 2; i < s.size(); ++i) if (!isxdigit((unsigned char)s[i])) return false;
        return true;
    }
    return false;
}

static long long to_int(const string& s) {
    if (is_hex_pref(s)) {
        long long x = 0; stringstream ss; ss << std::hex << s; ss >> x; return x;
    }
    return stoll(s);
}

static string to_hex(long long value, int width) {
    unsigned long long mask;
    if (4LL * width >= 64) mask = ~0ULL;                 // avoid shifting by 64
    else                   mask = (1ULL << (4 * width)) - 1ULL;

    stringstream ss;
    ss << uppercase << hex << setw(width) << setfill('0')
       << (static_cast<unsigned long long>(value) & mask);
    return ss.str();
}
// Symbol table entry
struct Sym { int addr = 0; int blk = 0; bool defined = false; bool reloc = true; };

// Expression evaluator with relocation terms
struct Expr {
    long long value = 0;
    bool reloc = false;
    vector<pair<string,int>> terms; // {symbol, +1/-1}
};

static Expr eval_expr_terms(const string& expr, const map<string, Sym>& SYM) {
    // tokenize
    vector<string> tok;
    for (size_t i = 0; i < expr.size();) {
        if (isspace((unsigned char)expr[i])) { ++i; continue; }
        if (strchr("()+-*/", expr[i])) { tok.push_back(string(1, expr[i++])); continue; }
        size_t j = i;
        while (j < expr.size() && !isspace((unsigned char)expr[j]) && !strchr("()+-*/", expr[j])) ++j;
        tok.push_back(expr.substr(i, j - i)); i = j;
    }

    map<string,int> prec = {{"+",1},{"-",1},{"*",2},{"/",2}};
    vector<string> out, op;
    for (auto &t : tok) {
        if (prec.count(t)) {
            while (!op.empty() && prec.count(op.back()) && prec[op.back()] >= prec[t]) {
                out.push_back(op.back()); op.pop_back();
            }
            op.push_back(t);
        } else if (t == "(") {
            op.push_back(t);
        } else if (t == ")") {
            while (!op.empty() && op.back() != "(") { out.push_back(op.back()); op.pop_back(); }
            if (!op.empty()) op.pop_back();
        } else {
            out.push_back(t);
        }
    }
    while (!op.empty()) { out.push_back(op.back()); op.pop_back(); }

    struct Node { long long v; bool reloc; vector<pair<string,int>> terms; };
    vector<Node> st;

    for (auto &t : out) {
        if (prec.count(t)) {
            auto b = st.back(); st.pop_back();
            auto a = st.back(); st.pop_back();
            Node r;
            if (t == "+") {
                r.v = a.v + b.v; r.reloc = a.reloc || b.reloc; r.terms = a.terms;
                r.terms.insert(r.terms.end(), b.terms.begin(), b.terms.end());
            } else if (t == "-") {
                r.v = a.v - b.v; r.reloc = a.reloc || b.reloc; r.terms = a.terms;
                for (auto q : b.terms) r.terms.push_back({q.first, -q.second});
            } else if (t == "*") {
                r.v = a.v * b.v; r.reloc = a.reloc || b.reloc; r.terms.clear();
            } else if (t == "/") {
                r.v = (b.v ? a.v / b.v : 0); r.reloc = a.reloc || b.reloc; r.terms.clear();
            }
            st.push_back(r);
        } else {
            if (is_dec(t) || is_hex_pref(t)) {
                st.push_back({to_int(t), false, {}});
            } else {
                auto it = SYM.find(t);
                if (it == SYM.end() || !it->second.defined) {
                    throw runtime_error(string("Undefined symbol: ") + t);
                }
                st.push_back({it->second.addr, true, {{t, +1}}});
            }
        }
    }

    Expr E; if (st.empty()) return E;
    E.value = st.back().v; E.reloc = false; int net = 0;
    for (auto &q : st.back().terms) net += q.second;
    if (net == 1) { E.reloc = true; E.terms = st.back().terms; }
    else { E.reloc = false; E.terms.clear(); }
    return E;
}

// Literals
struct Lit { string token; vector<unsigned char> bytes; int blk = 0; int off = -1; int addr = 0; };

static vector<unsigned char> parse_const_C(const string& s) {
    vector<unsigned char> b; for (char c : s) b.push_back((unsigned char)c); return b;
}
static vector<unsigned char> parse_const_X(const string& s) {
    vector<unsigned char> b;
    for (size_t i = 0; i < s.size(); i += 2) {
        string t = s.substr(i, 2); unsigned v = 0; stringstream ss; ss << hex << t; ss >> v; b.push_back((unsigned char)v);
    }
    return b;
}

static map<string,int> REG = {{"A",0},{"X",1},{"L",2},{"B",3},{"S",4},{"T",5},{"F",6},{"PC",8},{"SW",9}};

int main(int argc, char** argv) {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    try {
        if (argc < 4) {
            throw runtime_error("Usage: sicxe <input.asm> <listing.lst> <object.obj>");
        }

        string in_file = argv[1], list_file = argv[2], obj_file = argv[3];
        ifstream fin(in_file);
        if (!fin) throw runtime_error("Cannot open input file: " + in_file);

        // ---------- Read source lines ----------
        vector<Line> lines; string raw;
        while (getline(fin, raw)) {
            if (raw.size() && raw.back() == '\r') raw.pop_back();
            if (trim(raw).empty()) continue;

            Line L; L.raw = raw;
            auto parts = split_ws(raw);
            if (parts.empty()) continue;

            int idx = 0;
            if (OPTAB.find(parts[0]) == OPTAB.end() && DIRECT.find(parts[0]) == DIRECT.end() && parts[0][0] != '+') {
                L.label = parts[0]; idx = 1;
            }
            if (idx < (int)parts.size()) {
                L.op = parts[idx];
                if (L.op.size() && L.op[0] == '+') { L.plus = true; L.op = L.op.substr(1); }
                idx++;
            }
            if (idx < (int)parts.size()) {
                string rest;
                for (int k = idx; k < (int)parts.size(); ++k) { if (k > idx) rest.push_back(' '); rest += parts[k]; }
                L.operand = rest;
            }
            lines.push_back(L);
        }

        // ---------- Program blocks ----------
        map<string,int> BLKID; vector<string> BLKNAME; vector<int> LOCC;
        BLKID["DEFAULT"] = 0; BLKNAME.push_back("DEFAULT"); LOCC.push_back(0);
        int curBlk = 0;

        // ---------- Pass 1: SYMTAB, EQU/ORG, Literals ----------
        map<string, Sym> SYM;
        vector<Lit> LITS;

        auto dump_literals = [&](int blk) {
            for (auto &lit : LITS) {
                if (lit.off != -1) continue;
                lit.blk = blk; lit.off = LOCC[blk];
                LOCC[blk] += (int)lit.bytes.size();
            }
        };

        int STARTADDR = 0; int prevORG = -1;

        for (auto &L : lines) {
            if (L.op == "START") {
                STARTADDR = (L.operand.empty() ? 0 : (int)to_int(L.operand));
                L.blk = curBlk; L.off = LOCC[curBlk];
                if (!L.label.empty()) {
                    SYM[L.label] = {LOCC[curBlk], curBlk, true, true};
                }
                continue;
            }

            if (L.op == "USE") {
                string name = trim(L.operand); if (name.empty()) name = "DEFAULT";
                if (!BLKID.count(name)) { BLKID[name] = (int)BLKNAME.size(); BLKNAME.push_back(name); LOCC.push_back(0); }
                curBlk = BLKID[name]; L.blk = curBlk; L.off = LOCC[curBlk];
                continue;
            }

            L.blk = curBlk; L.off = LOCC[curBlk];

            if (L.op == "EQU") {
                if (L.label.empty()) throw runtime_error("EQU without label (a label is required before EQU)");
                if (trim(L.operand) == "*") {
                    SYM[L.label] = {LOCC[curBlk], curBlk, true, true};
                } else {
                    auto E = eval_expr_terms(trim(L.operand), SYM);
                    bool reloc = E.reloc;
                    SYM[L.label] = {(int)E.value, reloc ? curBlk : 0, true, reloc};
                }
                continue;
            }

            if (L.op == "ORG") {
                if (trim(L.operand).empty()) {
                    if (prevORG != -1) LOCC[curBlk] = prevORG;
                    prevORG = -1;
                } else {
                    auto E = eval_expr_terms(trim(L.operand), SYM);
                    if (E.reloc) throw runtime_error("ORG must be absolute (cannot use relocatable symbols)");
                    prevORG = LOCC[curBlk];
                    LOCC[curBlk] = (int)E.value;
                }
                continue;
            }

            if (!L.label.empty()) {
                if (SYM.count(L.label) && SYM[L.label].defined) throw runtime_error("Duplicate symbol: " + L.label);
                SYM[L.label] = {LOCC[curBlk], curBlk, true, true};
            }

            if (L.op == "LTORG") { dump_literals(curBlk); continue; }
            if (L.op == "BASE" || L.op == "NOBASE" || L.op == "END") { continue; }

            if (OPTAB.count(L.op)) {
                if (OPTAB[L.op].formats.size() == 1 && OPTAB[L.op].formats[0] == 2) LOCC[curBlk] += 2;
                else LOCC[curBlk] += (L.plus ? 4 : 3);

                string op = trim(L.operand);
                if (op.size() > 0 && op[0] == '=') {
                    string token = op.substr(1);
                    if (token.rfind("C'", 0) == 0) {
                        auto p = token.find('\'', 2); string s = token.substr(2, p - 2);
                        LITS.push_back({token, parse_const_C(s)});
                    } else if (token.rfind("X'", 0) == 0) {
                        auto p = token.find('\'', 2); string s = token.substr(2, p - 2);
                        LITS.push_back({token, parse_const_X(s)});
                    } else if (token.rfind("W'", 0) == 0) {
                        auto p = token.find('\'', 2); string s = token.substr(2, p - 2);
                        auto ex = eval_expr_terms(s, SYM);
                        long long v = ex.value;
                        vector<unsigned char> b(3); b[0] = (v >> 16) & 0xFF; b[1] = (v >> 8) & 0xFF; b[2] = v & 0xFF;
                        LITS.push_back({token, b});
                    }
                }
            } else if (L.op == "WORD") {
                LOCC[curBlk] += 3;
            } else if (L.op == "RESW") {
                LOCC[curBlk] += 3 * stoi(trim(L.operand));
            } else if (L.op == "RESB") {
                LOCC[curBlk] += stoi(trim(L.operand));
            } else if (L.op == "BYTE") {
                string op = trim(L.operand);
                if (op.rfind("C'", 0) == 0) {
                    auto pos = op.find('\'', 2); string data = op.substr(2, pos - 2); LOCC[curBlk] += (int)data.size();
                } else if (op.rfind("X'", 0) == 0) {
                    auto pos = op.find('\'', 2); string data = op.substr(2, pos - 2); LOCC[curBlk] += (int)(data.size() / 2);
                } else {
                    LOCC[curBlk] += 1;
                }
            }
        }
        dump_literals(curBlk);

        // Block starts and final addresses
        vector<int> BLKSTART(BLKNAME.size(), 0);
        int cur = STARTADDR;
        for (size_t i = 0; i < BLKNAME.size(); ++i) { BLKSTART[i] = cur; cur += LOCC[i]; }
        int PROGLEN = cur - STARTADDR;

        for (auto &L : lines) { L.addr = BLKSTART[L.blk] + L.off; }
        for (auto &kv : const_cast<map<string,Sym>&>(SYM)) {
            if (kv.second.reloc) kv.second.addr = BLKSTART[kv.second.blk] + kv.second.addr;
        }

        // Literal table with final addresses
        map<string, Lit> LITTAB;
        for (auto &lit : LITS) { lit.addr = BLKSTART[lit.blk] + lit.off; LITTAB[lit.token] = lit; }

        // ---------- Pass 2: object code ----------
        int BASE = -1;
        vector<tuple<int,int,int,char>> MODS;   // addr, half-nibbles, start_bit(0), sign

        // CHANGED: store block id too
        vector<tuple<int,string,int>> OBJSEQ;   // addr, hex-bytes, blk

        auto emit_bytes = [&](int addr, const vector<unsigned char>& b, int blk) {
            string s; for (auto c : b) { stringstream ss; ss << uppercase << hex << setw(2) << setfill('0') << (int)c; s += ss.str(); }
            OBJSEQ.emplace_back(addr, s, blk); return s;
        };

        for (auto &L : lines) {
            if (L.op == "START" || L.op == "END" || L.op == "USE" || L.op == "LTORG" || L.op == "EQU") continue;

            if (L.op == "BASE") {
                string sym = trim(L.operand);
                if (!SYM.count(sym)) throw runtime_error("BASE uses undefined symbol: " + sym);
                BASE = SYM[sym].addr; continue;
            }
            if (L.op == "NOBASE") { BASE = -1; continue; }
            if (L.op == "ORG") continue;

            if (OPTAB.count(L.op)) {
                auto info = OPTAB[L.op];

                if (info.formats.size() == 1 && info.formats[0] == 2) {
                    // format 2
                    string a, b; string opnd = trim(L.operand);
                    size_t c = opnd.find(',');
                    if (c == string::npos) a = trim(opnd);
                    else { a = trim(opnd.substr(0, c)); b = trim(opnd.substr(c + 1)); }
                    int r1 = REG.count(a) ? REG[a] : 0; int r2 = b.empty() ? 0 : (REG.count(b) ? REG[b] : 0);
                    int code = (info.opcode << 8) | (r1 << 4) | r2;
                    L.obj = to_hex(code, 4); OBJSEQ.emplace_back(L.addr, L.obj, L.blk); continue;
                }

                int opcode = info.opcode;
                bool plus = L.plus; int n = 1, i = 1, x = 0, b = 0, p = 0, e = (plus ? 1 : 0);
                string operand = trim(L.operand);

                if (L.op == "RSUB") {
                    int op2 = ((opcode & 0xFC) | 0x3); int code = (op2 << 4) | 0; code = (code << 12) | 0;
                    L.obj = to_hex(code, 6); OBJSEQ.emplace_back(L.addr, L.obj, L.blk); continue;
                }

                // handle ",X" or ", X"
                auto posComma = operand.find(',');
                if (posComma != string::npos) {
                    string left = trim(operand.substr(0, posComma));
                    string right = trim(operand.substr(posComma + 1));
                    if (!right.empty() && (right == "X" || right == "x")) { x = 1; operand = left; }
                }

                if (!operand.empty() && operand[0] == '#') { n = 0; i = 1; operand = operand.substr(1); }
                else if (!operand.empty() && operand[0] == '@') { n = 1; i = 0; operand = operand.substr(1); }
                else { n = 1; i = 1; }

                // Literals
                if (!operand.empty() && operand[0] == '=') {
                    string token = operand.substr(1);
                    if (!LITTAB.count(token)) throw runtime_error("Literal not found in pool: " + token);
                    int target = LITTAB[token].addr;
                    operand = to_string(target);
                }

                if (plus) {
                    operand = trim(operand);
                    Expr Einfo;
                    if (is_dec(operand) || is_hex_pref(operand)) { Einfo.value = to_int(operand); Einfo.reloc = false; }
                    else { Einfo = eval_expr_terms(operand, SYM); }
                    int target = (int)Einfo.value;
                    int op2 = ((opcode & 0xFC) | ((n << 1) | i));
                    int flags = (x << 3) | (b << 2) | (p << 1) | e;
                    long long code = ((long long)op2 << 4) | flags; code = (code << 20) | (target & 0xFFFFF);
                    L.obj = to_hex((int)code, 8); OBJSEQ.emplace_back(L.addr, L.obj, L.blk);
                    for (auto &t : Einfo.terms) { char sign = (t.second >= 0) ? '+' : '-'; MODS.emplace_back(L.addr + 1, 5, 0, sign); }
                } else {
                    int disp = 0;
                    if (is_dec(operand) && n == 0) { b = 0; p = 0; disp = (int)to_int(operand); }
                    else {
                        int target = 0;
                        Expr Einfo;
                        if (is_dec(operand) || is_hex_pref(operand)) target = (int)to_int(operand);
                        else { Einfo = eval_expr_terms(operand, SYM); target = (int)Einfo.value; }
                        int next = L.addr + 3; int d = target - next;
                        if (d >= -2048 && d <= 2047) { p = 1; if (d < 0) d = (1 << 12) + d; disp = d; }
                        else if (BASE != -1) {
                            b = 1; int bd = target - BASE; if (bd < 0 || bd > 4095) throw runtime_error("Base displacement out of range (0..4095)");
                            disp = bd;
                        } else {
                            throw runtime_error("Cannot address \"" + operand + "\" without BASE (PC-relative out of range)");
                        }
                    }
                    int op2 = ((opcode & 0xFC) | ((n << 1) | i));
                    int flags = (x << 3) | (b << 2) | (p << 1) | e;
                    int code = (op2 << 4) | flags; code = (code << 12) | (disp & 0xFFF);
                    L.obj = to_hex(code, 6); OBJSEQ.emplace_back(L.addr, L.obj, L.blk);
                }
            } else if (L.op == "BYTE") {
                string op = trim(L.operand); vector<unsigned char> bytes;
                if (op.rfind("C'", 0) == 0) { auto p = op.find('\'', 2); string s = op.substr(2, p - 2); bytes = parse_const_C(s); }
                else if (op.rfind("X'", 0) == 0) { auto p = op.find('\'', 2); string s = op.substr(2, p - 2); bytes = parse_const_X(s); }
                else { auto ex = eval_expr_terms(op, SYM); bytes = { (unsigned char)(ex.value & 0xFF) }; }
                L.obj = emit_bytes(L.addr, bytes, L.blk);
            } else if (L.op == "WORD") {
                string exs = trim(L.operand);
                Expr ex = eval_expr_terms(exs, SYM);
                long long v = ex.value;
                vector<unsigned char> b(3); b[0] = (v >> 16) & 0xFF; b[1] = (v >> 8) & 0xFF; b[2] = v & 0xFF;
                L.obj = emit_bytes(L.addr, b, L.blk);
                for (auto &t : ex.terms) { char sign = (t.second >= 0) ? '+' : '-'; MODS.emplace_back(L.addr, 6, 0, sign); }
            }
        }

        // ---------- Listing ----------
        ofstream lout(list_file);
        for (auto &L : lines) {
            string addr = (L.op == "END" ? string(4, ' ') : to_hex(L.addr, 4));
            string opcol = string(L.plus ? "+" : "") + L.op;
            lout << addr << "\t" << setw(10) << left << L.obj << "\t" << setw(8) << left
                 << L.label << "\t" << setw(8) << left << opcol << "\t" << L.operand << "\n";
        }
        lout.close();

        // ---------- Object program (caret-separated) ----------
        string progname = lines.front().raw.substr(
            0, min((size_t)6, lines.front().raw.find_first_of(" \t")));
        string pname = progname;
        if (pname.size() < 6) pname.append(6 - pname.size(), ' ');

        // Header: H^name^start^length
        string H = "H^" + pname + "^" + to_hex(STARTADDR, 6) + "^" + to_hex(PROGLEN, 6);

        // Build T records (max 30 bytes, contiguous; split on block change)
        vector<string> Trecs; 
        const int MAXLEN = 30;

        size_t idx = 0;
        while (idx < OBJSEQ.size()) {
            int start = get<0>(OBJSEQ[idx]);
            int curBlkForRec = get<2>(OBJSEQ[idx]);
            vector<string> parts; 
            int reclen = 0; 
            size_t j = idx;

            while (j < OBJSEQ.size()) {
                int a   = get<0>(OBJSEQ[j]);
                string s = get<1>(OBJSEQ[j]);
                int blk = get<2>(OBJSEQ[j]);
                int sz  = (int)s.size() / 2;

                // break if block changes
                if (blk != curBlkForRec) break;

                // break if not contiguous
                if (!parts.empty()) {
                    int pa  = get<0>(OBJSEQ[j - 1]);
                    string ps = get<1>(OBJSEQ[j - 1]);
                    int psz = (int)ps.size() / 2;
                    if (a != pa + psz) break;
                }

                // break if exceeds 30 bytes
                if (reclen + sz > MAXLEN) break;

                parts.push_back(s); reclen += sz; j++;
            }

            string objcat; for (auto &p : parts) objcat += p;
            Trecs.push_back("T^" + to_hex(start, 6) + "^" + to_hex(reclen, 2) + "^" + objcat);
            idx = j;
        }

        ofstream oout(obj_file);
        oout << H << "\n";
        for (auto &t : Trecs) oout << t << "\n";

        // Modification records (no explicit +/- here)
        for (auto &m : MODS) {
            int addr, nibbles, startbit; char sign;
            tie(addr, nibbles, startbit, sign) = m;
            oout << "M^" << to_hex(addr, 6) << "^" << to_hex(nibbles, 2) << "\n";
        }

        // End record
        int startExec = STARTADDR;
        auto it = SYM.find("FIRST");
        if (it != SYM.end()) startExec = it->second.addr;
        oout << "E^" << to_hex(startExec, 6) << "\n";
        oout.close();

        cerr << "Assembly completed successfully.\n";
        return 0;
    }
    catch (const exception& e) {
        cerr << "❌ Assembly failed: " << e.what() << "\n";
        return 1;
    }
    catch (...) {
        cerr << "❌ Unknown fatal error occurred.\n";
        return 2;
    }
}
