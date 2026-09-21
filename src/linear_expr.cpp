#include "linear_expr.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <set>
#include <sstream>

namespace {

const double kTol = 1e-12;

std::string fmt(double v) {
    if (std::fabs(v) < kTol) v = 0.0;
    std::ostringstream ss;
    ss << v;
    return ss.str();
}

// ---------------------------------------------------------------- 词法分析

struct Token {
    enum Kind { Number, Identifier, Op, LParen, RParen, End };
    Kind kind = End;
    double number = 0.0;
    std::string text;
};

class Lexer {
public:
    explicit Lexer(const std::string& src) : s_(src), i_(0) {}

    bool next(Token& tok, std::string& error) {
        while (i_ < s_.size() &&
               std::isspace(static_cast<unsigned char>(s_[i_]))) {
            i_++;
        }
        if (i_ >= s_.size()) {
            tok = Token();
            tok.kind = Token::End;
            return true;
        }

        const char c = s_[i_];

        const bool starts_number =
            std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '.' && i_ + 1 < s_.size() &&
             std::isdigit(static_cast<unsigned char>(s_[i_ + 1])));
        if (starts_number) {
            const size_t start = i_;
            while (i_ < s_.size() &&
                   std::isdigit(static_cast<unsigned char>(s_[i_]))) {
                i_++;
            }
            if (i_ < s_.size() && s_[i_] == '.') {
                i_++;
                while (i_ < s_.size() &&
                       std::isdigit(static_cast<unsigned char>(s_[i_]))) {
                    i_++;
                }
            }
            if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
                const size_t save = i_;
                i_++;
                if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) i_++;
                if (i_ < s_.size() &&
                    std::isdigit(static_cast<unsigned char>(s_[i_]))) {
                    while (i_ < s_.size() &&
                           std::isdigit(static_cast<unsigned char>(s_[i_]))) {
                        i_++;
                    }
                } else {
                    i_ = save;  // 不是合法的指数，回退
                }
            }
            tok = Token();
            tok.kind = Token::Number;
            tok.text = s_.substr(start, i_ - start);
            tok.number = std::strtod(tok.text.c_str(), nullptr);
            return true;
        }

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            const size_t start = i_;
            while (i_ < s_.size() &&
                   (std::isalnum(static_cast<unsigned char>(s_[i_])) ||
                    s_[i_] == '_')) {
                i_++;
            }
            tok = Token();
            tok.kind = Token::Identifier;
            tok.text = s_.substr(start, i_ - start);
            return true;
        }

        tok = Token();
        if (c == '(') {
            i_++;
            tok.kind = Token::LParen;
            tok.text = "(";
            return true;
        }
        if (c == ')') {
            i_++;
            tok.kind = Token::RParen;
            tok.text = ")";
            return true;
        }
        if (c == '+' || c == '-' || c == '*' || c == '/') {
            i_++;
            tok.kind = Token::Op;
            tok.text = std::string(1, c);
            return true;
        }

        error = std::string("表达式中出现无法识别的字符 '") + c + "'";
        return false;
    }

private:
    const std::string& s_;
    size_t i_;
};

// ---------------------------------------------------------------- 语法分析

void add_into(LinearForm& dst, const LinearForm& src, double scale) {
    for (const auto& kv : src.coeffs) {
        double& slot = dst.coeffs[kv.first];
        slot += scale * kv.second;
        if (std::fabs(slot) < kTol) dst.coeffs.erase(kv.first);
    }
    dst.constant += scale * src.constant;
}

void multiply_into(LinearForm& form, double factor) {
    if (std::fabs(factor) < kTol) {
        form.coeffs.clear();
        form.constant = 0.0;
        return;
    }
    for (auto& kv : form.coeffs) kv.second *= factor;
    form.constant *= factor;
}

// 递归下降：
//   expr    := term (('+' | '-') term)*
//   term    := factor (('*' | '/') factor)*
//   factor  := ('+' | '-') factor | '(' expr ')' | number | identifier
class Parser {
public:
    Parser(const std::string& src,
           const std::set<std::string>& allowed,
           const std::string& original)
        : lex_(src), allowed_(allowed), original_(original) {}

    bool parse(LinearForm& out, std::string& error) {
        if (!advance(error)) return false;
        if (!parse_expr(out, error)) return false;
        if (cur_.kind != Token::End) {
            error = "表达式 '" + original_ + "' 在 '" + cur_.text + "' 之后有多余内容";
            return false;
        }
        return true;
    }

private:
    bool advance(std::string& error) { return lex_.next(cur_, error); }

    bool parse_expr(LinearForm& out, std::string& error) {
        if (!parse_term(out, error)) return false;
        while (cur_.kind == Token::Op &&
               (cur_.text == "+" || cur_.text == "-")) {
            const bool plus = (cur_.text == "+");
            if (!advance(error)) return false;
            LinearForm rhs;
            if (!parse_term(rhs, error)) return false;
            add_into(out, rhs, plus ? 1.0 : -1.0);
        }
        return true;
    }

    bool parse_term(LinearForm& out, std::string& error) {
        if (!parse_factor(out, error)) return false;
        while (cur_.kind == Token::Op &&
               (cur_.text == "*" || cur_.text == "/")) {
            const bool multiply = (cur_.text == "*");
            if (!advance(error)) return false;
            LinearForm rhs;
            if (!parse_factor(rhs, error)) return false;

            if (multiply) {
                if (out.is_constant()) {
                    multiply_into(rhs, out.constant);
                    out = rhs;
                } else if (rhs.is_constant()) {
                    multiply_into(out, rhs.constant);
                } else {
                    error = "表达式 '" + original_ +
                            "' 是二次的（变量×变量），仅支持线性模型";
                    return false;
                }
            } else {
                if (!rhs.is_constant()) {
                    error = "表达式 '" + original_ +
                            "' 中不允许除以含变量的式子";
                    return false;
                }
                if (std::fabs(rhs.constant) < kTol) {
                    error = "表达式 '" + original_ + "' 中出现了除以 0";
                    return false;
                }
                multiply_into(out, 1.0 / rhs.constant);
            }
        }
        return true;
    }

    bool parse_factor(LinearForm& out, std::string& error) {
        if (cur_.kind == Token::Op &&
            (cur_.text == "+" || cur_.text == "-")) {
            const bool negate = (cur_.text == "-");
            if (!advance(error)) return false;
            if (!parse_factor(out, error)) return false;
            if (negate) multiply_into(out, -1.0);
            return true;
        }

        if (cur_.kind == Token::LParen) {
            if (!advance(error)) return false;
            if (!parse_expr(out, error)) return false;
            if (cur_.kind != Token::RParen) {
                error = "表达式 '" + original_ + "' 中括号不匹配";
                return false;
            }
            return advance(error);
        }

        if (cur_.kind == Token::Number) {
            const double value = cur_.number;
            out = LinearForm();
            out.constant = value;
            return advance(error);
        }

        if (cur_.kind == Token::Identifier) {
            const std::string name = cur_.text;
            if (allowed_.find(name) == allowed_.end()) {
                error = "变量 '" + name + "' 未在 variables 中声明";
                return false;
            }
            out = LinearForm();
            out.coeffs[name] = 1.0;
            return advance(error);
        }

        error = "表达式 '" + original_ + "' 在此处应为数字、变量或括号";
        return false;
    }

    Lexer lex_;
    const std::set<std::string>& allowed_;
    const std::string& original_;
    Token cur_;
};

}  // namespace

// ---------------------------------------------------------------- 对外接口

double LinearForm::evaluate(const std::map<std::string, double>& assignment) const {
    double total = constant;
    for (const auto& kv : coeffs) {
        const auto it = assignment.find(kv.first);
        if (it == assignment.end()) {
            // 变量缺失时按 0 处理会让验证悄悄失真，这里直接算作失败信号
            return std::nan("");
        }
        total += kv.second * it->second;
    }
    return total;
}

std::string LinearForm::to_string() const {
    if (coeffs.empty()) return fmt(constant);
    std::ostringstream ss;
    bool first = true;
    for (const auto& kv : coeffs) {
        if (!first) ss << " + ";
        ss << fmt(kv.second) << "*" << kv.first;
        first = false;
    }
    if (std::fabs(constant) > kTol) {
        ss << (constant > 0 ? " + " : " - ") << fmt(std::fabs(constant));
    }
    return ss.str();
}

std::string normalize_math_text(const std::string& text) {
    // UTF-8 字节序列，避免依赖编译器对 \u 窄字符串的处理差异
    static const std::string kLe = "\xE2\x89\xA4";  // ≤
    static const std::string kGe = "\xE2\x89\xA5";  // ≥
    static const std::string kTimes = "\xC3\x97";   // ×
    static const std::string kDiv = "\xC3\xB7";     // ÷
    static const std::string kMinus = "\xE2\x88\x92";  // −
    static const std::string kLparen = "\xEF\xBC\x88";  // （
    static const std::string kRparen = "\xEF\xBC\x89";  // ）
    static const std::string kComma = "\xEF\xBC\x8C";   // ，

    std::string out = text;
    const std::pair<const std::string*, const char*> table[] = {
        {&kLe, "<="}, {&kGe, ">="}, {&kTimes, "*"}, {&kDiv, "/"},
        {&kMinus, "-"}, {&kLparen, "("}, {&kRparen, ")"}, {&kComma, ","},
    };
    for (const auto& rule : table) {
        const std::string& from = *rule.first;
        std::string to = rule.second;
        size_t pos = 0;
        while ((pos = out.find(from, pos)) != std::string::npos) {
            out.replace(pos, from.size(), to);
            pos += to.size();
        }
    }
    return out;
}

bool parse_linear_expression(const std::string& expr,
                            const std::vector<std::string>& allowed_vars,
                            LinearForm& out,
                            std::string& error) {
    const std::string text = normalize_math_text(expr);
    std::string trimmed = text;
    size_t b = trimmed.find_first_not_of(" \t\r\n");
    size_t e = trimmed.find_last_not_of(" \t\r\n");
    if (b == std::string::npos) {
        error = "表达式为空";
        return false;
    }
    trimmed = trimmed.substr(b, e - b + 1);

    std::set<std::string> allowed(allowed_vars.begin(), allowed_vars.end());
    Parser parser(trimmed, allowed, expr);
    return parser.parse(out, error);
}

bool split_relation(const std::string& text,
                    std::string& lhs,
                    std::string& op,
                    std::string& rhs,
                    std::string& error) {
    const std::string t = normalize_math_text(text);
    size_t pos = std::string::npos;
    size_t oplen = 0;
    op.clear();

    if ((pos = t.find("<=")) != std::string::npos) {
        op = "<=";
        oplen = 2;
    } else if ((pos = t.find(">=")) != std::string::npos) {
        op = ">=";
        oplen = 2;
    } else if ((pos = t.find("==")) != std::string::npos) {
        op = "=";
        oplen = 2;
    } else if ((pos = t.find('=')) != std::string::npos) {
        op = "=";
        oplen = 1;
    } else if ((pos = t.find('<')) != std::string::npos) {
        op = "<=";  // LLM 偶尔写 a < b，按 <= 处理
        oplen = 1;
    } else if ((pos = t.find('>')) != std::string::npos) {
        op = ">=";
        oplen = 1;
    } else {
        error = "约束 '" + text + "' 中没有比较运算符（<=、>=、=）";
        return false;
    }

    lhs = t.substr(0, pos);
    rhs = t.substr(pos + oplen);
    if (lhs.find_first_not_of(" \t\r\n") == std::string::npos ||
        rhs.find_first_not_of(" \t\r\n") == std::string::npos) {
        error = "约束 '" + text + "' 的比较运算符两侧不能为空";
        return false;
    }
    return true;
}
