// ptx_parser.cpp — PTX lexer + recursive-descent parser

#include "ptx_parser.h"

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <sstream>

namespace riscv_gpgpu {
namespace ptx {

// ── Tokenizer ─────────────────────────────────────────────────────────────────

static bool isWordChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.';
}

void PtxParser::tokenize(const std::string& text) {
    tokens_.clear();
    tok_pos_ = 0;
    size_t i = 0;
    const size_t n = text.size();

    while (i < n) {
        // Skip whitespace
        if (std::isspace(static_cast<unsigned char>(text[i]))) { ++i; continue; }

        // Skip // line comments
        if (i + 1 < n && text[i] == '/' && text[i+1] == '/') {
            while (i < n && text[i] != '\n') ++i;
            continue;
        }
        // Skip /* block comments */
        if (i + 1 < n && text[i] == '/' && text[i+1] == '*') {
            i += 2;
            while (i + 1 < n && !(text[i] == '*' && text[i+1] == '/')) ++i;
            if (i + 1 < n) i += 2;
            continue;
        }

        Token tok;

        if (text[i] == '%') {
            // Register or special register: %r0, %tid.x, %ntid.y ...
            tok.type = TK::PERCENT;
            ++i;
            size_t start = i;
            while (i < n && isWordChar(text[i])) ++i;
            tok.text = text.substr(start, i - start);
        } else if (text[i] == '$') {
            // Label reference: $L__BB0_end
            tok.type = TK::DOLLAR;
            ++i;
            size_t start = i;
            while (i < n && (std::isalnum(static_cast<unsigned char>(text[i]))
                           || text[i] == '_')) ++i;
            tok.text = text.substr(start, i - start);
        } else if (std::isalpha(static_cast<unsigned char>(text[i])) || text[i] == '_') {
            // Identifier
            tok.type = TK::WORD;
            size_t start = i;
            while (i < n && isWordChar(text[i])) ++i;
            tok.text = text.substr(start, i - start);
        } else if (text[i] == '.') {
            // Dot-prefixed keyword / type
            tok.type = TK::WORD;
            size_t start = i;
            ++i;
            while (i < n && isWordChar(text[i])) ++i;
            tok.text = text.substr(start, i - start);
        } else if (std::isdigit(static_cast<unsigned char>(text[i])) ||
                   (text[i] == '-' && i + 1 < n && std::isdigit(static_cast<unsigned char>(text[i+1])))) {
            // Number
            size_t start = i;
            if (text[i] == '-') ++i;
            bool is_float = false;
            if (i + 1 < n && text[i] == '0' && (text[i+1] == 'x' || text[i+1] == 'X')) {
                // Hex
                i += 2;
                while (i < n && std::isxdigit(static_cast<unsigned char>(text[i]))) ++i;
            } else {
                while (i < n && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
                if (i < n && text[i] == '.') { is_float = true; ++i; while (i < n && std::isdigit(static_cast<unsigned char>(text[i]))) ++i; }
                if (i < n && (text[i] == 'e' || text[i] == 'E')) { is_float = true; ++i; if (i < n && (text[i]=='+' || text[i]=='-')) ++i; while (i < n && std::isdigit(static_cast<unsigned char>(text[i]))) ++i; }
            }
            std::string numstr = text.substr(start, i - start);
            if (is_float) {
                tok.type = TK::FLOAT;
                tok.fval = std::stod(numstr);
            } else {
                tok.type = TK::INT;
                tok.ival = std::stoll(numstr, nullptr, 0);
            }
            tok.text = numstr;
        } else {
            // Single-character tokens
            char c = text[i++];
            switch (c) {
            case '(': tok.type = TK::LPAREN;    break;
            case ')': tok.type = TK::RPAREN;    break;
            case '{': tok.type = TK::LBRACE;    break;
            case '}': tok.type = TK::RBRACE;    break;
            case '<': tok.type = TK::LANGLE;    break;
            case '>': tok.type = TK::RANGLE;    break;
            case '[': tok.type = TK::LBRACKET;  break;
            case ']': tok.type = TK::RBRACKET;  break;
            case ',': tok.type = TK::COMMA;     break;
            case ';': tok.type = TK::SEMICOLON; break;
            case ':': tok.type = TK::COLON;     break;
            case '@': tok.type = TK::AT;        break;
            case '!': tok.type = TK::BANG;      break;
            case '+': tok.type = TK::PLUS;      break;
            case '-': tok.type = TK::MINUS;     break;
            default:  continue;  // skip unknown chars (e.g. '=', '"', ...)
            }
            tok.text = std::string(1, c);
        }
        tokens_.push_back(tok);
    }
    tokens_.push_back(Token{TK::END, ""});
}

// ── Helpers ───────────────────────────────────────────────────────────────────

bool PtxParser::expect(TK type, const std::string& hint) {
    if (cur().type != type) {
        error_ = "Expected " + hint + " but got '" + cur().text + "'";
        return false;
    }
    advance();
    return true;
}

// ── Entry point ───────────────────────────────────────────────────────────────

PtxKernel PtxParser::parse(const std::string& ptx_text) {
    error_.clear();
    tokenize(ptx_text);

    PtxKernel k;
    while (cur().type != TK::END) {
        if (cur().type == TK::WORD && cur().text == ".address_size") {
            advance();
            if (cur().type != TK::INT || (cur().ival != 32 && cur().ival != 64)) {
                error_ = ".address_size must be 32 or 64";
                return {};
            }
            k.address_size = static_cast<uint32_t>(cur().ival);
            k.address_size_seen = true;
            advance();
            continue;
        }
        if (cur().type == TK::WORD && cur().text == ".entry") {
            advance();
            if (!parseKernel(k)) return {};
            return k;  // parse first kernel only
        }
        advance();
    }
    error_ = "No .entry found in PTX";
    return {};
}

// ── Kernel entry ─────────────────────────────────────────────────────────────

bool PtxParser::parseKernel(PtxKernel& k) {
    // Optional .visible modifier was already consumed as part of scanning
    // Current token: kernel name (WORD)
    if (cur().type != TK::WORD) {
        error_ = "Expected kernel name after .entry, got '" + cur().text + "'";
        return false;
    }
    k.name = cur().text;
    advance();

    // Parameter list: ( ... )
    if (cur().type == TK::LPAREN) {
        advance();
        if (!parseParams(k.params)) return false;
        if (!expect(TK::RPAREN, "')'")) return false;
    }

    // Skip optional attributes before '{'
    while (cur().type != TK::LBRACE && cur().type != TK::END) advance();
    if (cur().type != TK::LBRACE) { error_ = "Expected '{'"; return false; }
    advance();

    if (!parseBody(k)) return false;

    if (!expect(TK::RBRACE, "'}'")) return false;
    return true;
}

// ── Params ───────────────────────────────────────────────────────────────────

bool PtxParser::parseParams(std::vector<PtxParam>& out) {
    uint32_t idx = 0;
    while (cur().type != TK::RPAREN && cur().type != TK::END) {
        PtxParam p;
        p.index = idx++;
        if (cur().type == TK::WORD && cur().text == ".param") advance();
        if (cur().type == TK::WORD) { p.space = cur().text; advance(); }
        else { error_ = "Expected parameter type"; return false; }
        while (cur().type == TK::WORD && !cur().text.empty() && cur().text[0] == '.') {
            if (cur().text == ".ptr") {
                p.is_pointer = true;
                advance();
            } else if (cur().text == ".global" || cur().text == ".shared"
                       || cur().text == ".local" || cur().text == ".const") {
                p.pointer_space = cur().text;
                advance();
            } else if (cur().text == ".align") {
                advance();
                if (cur().type != TK::INT || cur().ival <= 0) {
                    error_ = "Expected positive parameter alignment";
                    return false;
                }
                p.alignment = static_cast<uint32_t>(cur().ival);
                advance();
            } else {
                error_ = "Unsupported parameter attribute: " + cur().text;
                return false;
            }
        }
        if (cur().type == TK::WORD) { p.name = cur().text; advance(); }
        else { error_ = "Expected param name"; return false; }
        out.push_back(p);
        if (cur().type == TK::COMMA) advance();
    }
    return true;
}

// ── Body ─────────────────────────────────────────────────────────────────────

bool PtxParser::parseBody(PtxKernel& k) {
    while (cur().type != TK::RBRACE && cur().type != TK::END) {
        // .reg declaration
        if (cur().type == TK::WORD && cur().text == ".reg") {
            if (!parseRegDecl(k)) return false;
            continue;
        }
        // Skip other directives (.loc, .func, .maxntid, etc.)
        if (cur().type == TK::WORD && !cur().text.empty() && cur().text[0] == '.'
            && cur().text != ".reg") {
            while (cur().type != TK::SEMICOLON && cur().type != TK::RBRACE
                   && cur().type != TK::END)
                advance();
            if (cur().type == TK::SEMICOLON) advance();
            continue;
        }

        // Label: $L__xxx: or word:
        if ((cur().type == TK::DOLLAR || cur().type == TK::WORD)
            && peek().type == TK::COLON) {
            PtxInstr lbl;
            lbl.label = cur().text;
            advance(); advance();  // consume name + ':'
            k.body.push_back(lbl);
            continue;
        }

        // Instruction
        PtxInstr instr;
        if (!parseInstr(instr)) return false;
        k.body.push_back(instr);
    }
    return true;
}

// ── Register declaration ──────────────────────────────────────────────────────

bool PtxParser::parseRegDecl(PtxKernel& k) {
    advance();  // consume '.reg'

    PtxRegDecl rd;
    // type e.g. .pred, .u32, .f32, .b64
    if (cur().type != TK::WORD) { error_ = "Expected type in .reg decl"; return false; }
    std::string type_str = cur().text;  // ".pred", ".u32", etc.
    advance();

    if (type_str == ".pred")       rd.type = "pred";
    else if (type_str == ".u32" || type_str == ".b32") rd.type = "u32";
    else if (type_str == ".s32")   rd.type = "s32";
    else if (type_str == ".f32")   rd.type = "f32";
    else if (type_str == ".u64" || type_str == ".b64" || type_str == ".s64") rd.type = "b64";
    else { error_ = "Unsupported register type: " + type_str; return false; }

    // Register name with count: %r<16>
    if (cur().type != TK::PERCENT) { error_ = "Expected %reg in .reg decl"; return false; }
    rd.prefix = "%" + cur().text;
    advance();

    // Optional <count>
    if (cur().type == TK::LANGLE) {
        advance();
        if (cur().type == TK::INT) { rd.count = static_cast<uint32_t>(cur().ival); advance(); }
        if (cur().type == TK::RANGLE) advance();
    } else {
        rd.count = 1;
    }

    k.reg_decls.push_back(rd);
    if (cur().type == TK::SEMICOLON) advance();
    return true;
}

// ── Instruction ───────────────────────────────────────────────────────────────

bool PtxParser::parseInstr(PtxInstr& out) {
    // Optional predicate: @%p0 or @!%p0
    if (cur().type == TK::AT) {
        advance();
        if (cur().type == TK::BANG) { out.pred_not = true; advance(); }
        if (cur().type != TK::PERCENT) {
            error_ = "Expected predicate register after @";
            return false;
        }
        out.pred = cur().text;
        advance();
    }

    // Opcode (WORD)
    if (cur().type != TK::WORD) {
        error_ = "Expected opcode, got '" + cur().text + "'";
        return false;
    }
    out.op = cur().text;
    advance();

    // Operands, comma-separated, terminated by ';'
    while (cur().type != TK::SEMICOLON && cur().type != TK::RBRACE
           && cur().type != TK::END) {
        PtxOperand operand;
        if (!parseOperand(operand)) return false;
        out.operands.push_back(operand);
        if (cur().type == TK::COMMA) advance();
    }
    if (cur().type == TK::SEMICOLON) advance();
    return true;
}

// ── Operand ───────────────────────────────────────────────────────────────────

bool PtxParser::parseOperand(PtxOperand& op) {

    if (cur().type == TK::PERCENT) {
        // Register or special register
        std::string name = cur().text;
        advance();
        // Special regs: tid.x, ctaid.x, ntid.x, etc.
        if (name.rfind("tid.", 0) == 0 || name.rfind("ctaid.", 0) == 0
            || name.rfind("ntid.", 0) == 0 || name == "warpid"
            || name == "laneid" || name == "nwarpid") {
            op.kind = PtxOperand::Kind::SpecialReg;
        } else {
            op.kind = PtxOperand::Kind::Reg;
        }
        op.name = name;
        return true;
    }

    if (cur().type == TK::DOLLAR) {
        op.kind = PtxOperand::Kind::Label;
        op.name = cur().text;
        advance();
        return true;
    }

    if (cur().type == TK::LBRACKET) {
        return parseMemRef(op);
    }

    if (cur().type == TK::INT) {
        op.kind    = PtxOperand::Kind::IntImm;
        op.int_val = cur().ival;
        advance();
        return true;
    }

    if (cur().type == TK::FLOAT) {
        op.kind    = PtxOperand::Kind::FltImm;
        op.flt_val = cur().fval;
        advance();
        return true;
    }

    if (cur().type == TK::MINUS) {
        // Negative number
        advance();
        if (cur().type == TK::INT) {
            op.kind    = PtxOperand::Kind::IntImm;
            op.int_val = -cur().ival;
            advance();
        } else if (cur().type == TK::FLOAT) {
            op.kind    = PtxOperand::Kind::FltImm;
            op.flt_val = -cur().fval;
            advance();
        }
        return true;
    }

    // Word — could be a label reference (branch target without $)
    if (cur().type == TK::WORD) {
        op.kind = PtxOperand::Kind::Label;
        op.name = cur().text;
        advance();
        return true;
    }

    error_ = "Unsupported operand token: " + cur().text;
    return false;
}

// ── Memory reference ──────────────────────────────────────────────────────────

bool PtxParser::parseMemRef(PtxOperand& op) {
    op.kind = PtxOperand::Kind::MemRef;
    advance();  // consume '['

    if (cur().type == TK::PERCENT) {
        // [%r0] or [%r0+4] or [%r0 + 4]
        op.mem_base = cur().text;
        advance();
        // Optional offset
        if (cur().type == TK::PLUS) {
            advance();
            if (cur().type != TK::INT) {
                error_ = "Expected immediate memory offset";
                return false;
            }
            op.mem_offset = cur().ival;
            advance();
        } else if (cur().type == TK::MINUS) {
            advance();
            if (cur().type != TK::INT) {
                error_ = "Expected immediate memory offset";
                return false;
            }
            op.mem_offset = -cur().ival;
            advance();
        } else if (cur().type == TK::INT && cur().ival < 0) {
            op.mem_offset = cur().ival;
            advance();
        }
    } else if (cur().type == TK::WORD) {
        op.param_name = cur().text;
        advance();
        if (cur().type == TK::PLUS) {
            advance();
            if (cur().type != TK::INT) {
                error_ = "Expected immediate memory offset";
                return false;
            }
            op.mem_offset = cur().ival;
            advance();
        }
    } else {
        error_ = "Expected register or symbol in memory reference";
        return false;
    }

    if (cur().type != TK::RBRACKET) {
        error_ = "Expected closing ']' in memory reference";
        return false;
    }
    advance();
    return true;
}

} // namespace ptx
} // namespace riscv_gpgpu
