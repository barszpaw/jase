#include "BreakExpr.h"
#include <cctype>
#include <vector>

namespace {

enum class TokType { Number, Ident, Op, LParen, RParen, Comma, End };

struct Token {
	TokType type;
	int32_t num = 0;
	std::string text; // Ident name (upper-cased) or operator spelling
};

// Tokenizer -- a single pass, no lookahead needed beyond one character.
class Lexer {
public:
	explicit Lexer(const std::string& src) : s_(src), pos_(0) {}

	bool next(Token* out, std::string* error) {
		skipSpace();
		if (pos_ >= s_.size()) { *out = Token{TokType::End, 0, ""}; return true; }
		char c = s_[pos_];

		if (std::isdigit((unsigned char)c)) return lexNumber(out, error);
		if (c == '$') { pos_++; return lexHex(out, error); }
		if (std::isalpha((unsigned char)c) || c == '_') return lexIdent(out, error);

		if (c == '(') { pos_++; *out = Token{TokType::LParen, 0, "("}; return true; }
		if (c == ')') { pos_++; *out = Token{TokType::RParen, 0, ")"}; return true; }
		if (c == ',') { pos_++; *out = Token{TokType::Comma, 0, ","}; return true; }

		// Two-character operators first.
		if (pos_ + 1 < s_.size()) {
			std::string two = s_.substr(pos_, 2);
			if (two == "==" || two == "!=" || two == "<=" || two == ">=" ||
			    two == "&&" || two == "||") {
				pos_ += 2;
				*out = Token{TokType::Op, 0, two};
				return true;
			}
		}
		if (c == '<' || c == '>' || c == '+' || c == '-' || c == '*' || c == '/' ||
		    c == '%' || c == '&' || c == '|' || c == '^' || c == '!') {
			pos_++;
			*out = Token{TokType::Op, 0, std::string(1, c)};
			return true;
		}

		*error = std::string("unexpected character '") + c + "'";
		return false;
	}

private:
	void skipSpace() {
		while (pos_ < s_.size() && std::isspace((unsigned char)s_[pos_])) pos_++;
	}

	bool lexHex(Token* out, std::string* error) {
		size_t start = pos_;
		while (pos_ < s_.size() && std::isxdigit((unsigned char)s_[pos_])) pos_++;
		if (pos_ == start) { *error = "expected hex digits after '$'"; return false; }
		*out = Token{TokType::Number, (int32_t)strtol(s_.substr(start, pos_ - start).c_str(), nullptr, 16), ""};
		return true;
	}

	bool lexNumber(Token* out, std::string* error) {
		size_t start = pos_;
		if (s_.compare(pos_, 2, "0x") == 0 || s_.compare(pos_, 2, "0X") == 0) {
			pos_ += 2;
			size_t hexStart = pos_;
			while (pos_ < s_.size() && std::isxdigit((unsigned char)s_[pos_])) pos_++;
			if (pos_ == hexStart) { *error = "expected hex digits after '0x'"; return false; }
			*out = Token{TokType::Number, (int32_t)strtol(s_.substr(hexStart, pos_ - hexStart).c_str(), nullptr, 16), ""};
			return true;
		}
		// Scan a maximal alnum run (covers plain decimal and trailing-H hex
		// like "1a2bH" -- Z80-assembler convention).
		while (pos_ < s_.size() && std::isalnum((unsigned char)s_[pos_])) pos_++;
		std::string tok = s_.substr(start, pos_ - start);
		if (!tok.empty() && (tok.back() == 'h' || tok.back() == 'H')) {
			std::string digits = tok.substr(0, tok.size() - 1);
			for (char ch : digits) {
				if (!std::isxdigit((unsigned char)ch)) { *error = "invalid hex literal '" + tok + "'"; return false; }
			}
			*out = Token{TokType::Number, (int32_t)strtol(digits.c_str(), nullptr, 16), ""};
			return true;
		}
		for (char ch : tok) {
			if (!std::isdigit((unsigned char)ch)) { *error = "invalid number '" + tok + "'"; return false; }
		}
		*out = Token{TokType::Number, (int32_t)strtol(tok.c_str(), nullptr, 10), ""};
		return true;
	}

	bool lexIdent(Token* out, std::string* error) {
		(void)error;
		size_t start = pos_;
		while (pos_ < s_.size() && (std::isalnum((unsigned char)s_[pos_]) || s_[pos_] == '_')) pos_++;
		std::string tok = s_.substr(start, pos_ - start);
		for (char& ch : tok) ch = (char)std::toupper((unsigned char)ch);
		*out = Token{TokType::Ident, 0, tok};
		return true;
	}

	const std::string& s_;
	size_t pos_;
};

} // namespace

struct BreakExpr::Node {
	enum class Kind { Number, Var, Peek8, Peek16, Neg, Not, BinOp } kind;
	int32_t number = 0;
	std::string name; // Var name, already upper-cased
	char op = 0;       // BinOp: see evaluate()
	std::unique_ptr<Node> left, right;
};

namespace {

// Recursive-descent parser -- builds a BreakExpr::Node tree directly,
// one grammar rule per method, standard precedence-climbing structure.
class Parser {
public:
	Parser(const std::string& text, std::string* error) : lex_(text), error_(error), ok_(true) {
		advance();
	}

	std::unique_ptr<BreakExpr::Node> parseAll() {
		auto node = parseOr();
		if (!ok_) return nullptr;
		if (cur_.type != TokType::End) {
			setError("unexpected trailing input near '" + cur_.text + "'");
			return nullptr;
		}
		return node;
	}

private:
	using Node = BreakExpr::Node;
	using Kind = Node::Kind;

	void advance() {
		if (!ok_) return;
		std::string err;
		if (!lex_.next(&cur_, &err)) { setError(err); }
	}

	void setError(const std::string& msg) {
		if (ok_) { *error_ = msg; ok_ = false; }
	}

	bool isOp(const char* spelling) const {
		return cur_.type == TokType::Op && cur_.text == spelling;
	}
	bool isIdentOp(const char* word) const {
		return cur_.type == TokType::Ident && cur_.text == word; // "AND"/"OR"/"NOT"
	}

	std::unique_ptr<Node> makeBin(char op, std::unique_ptr<Node> l, std::unique_ptr<Node> r) {
		auto n = std::make_unique<Node>();
		n->kind = Kind::BinOp;
		n->op = op;
		n->left = std::move(l);
		n->right = std::move(r);
		return n;
	}

	std::unique_ptr<Node> parseOr() {
		auto left = parseAnd();
		while (ok_ && (isOp("||") || isIdentOp("OR"))) {
			advance();
			auto right = parseAnd();
			if (!ok_) return nullptr;
			left = makeBin('O', std::move(left), std::move(right));
		}
		return left;
	}
	std::unique_ptr<Node> parseAnd() {
		auto left = parseBitOr();
		while (ok_ && (isOp("&&") || isIdentOp("AND"))) {
			advance();
			auto right = parseBitOr();
			if (!ok_) return nullptr;
			left = makeBin('A', std::move(left), std::move(right));
		}
		return left;
	}
	std::unique_ptr<Node> parseBitOr() {
		auto left = parseBitXor();
		while (ok_ && isOp("|")) {
			advance();
			auto right = parseBitXor();
			if (!ok_) return nullptr;
			left = makeBin('|', std::move(left), std::move(right));
		}
		return left;
	}
	std::unique_ptr<Node> parseBitXor() {
		auto left = parseBitAnd();
		while (ok_ && isOp("^")) {
			advance();
			auto right = parseBitAnd();
			if (!ok_) return nullptr;
			left = makeBin('^', std::move(left), std::move(right));
		}
		return left;
	}
	std::unique_ptr<Node> parseBitAnd() {
		auto left = parseEquality();
		while (ok_ && isOp("&")) {
			advance();
			auto right = parseEquality();
			if (!ok_) return nullptr;
			left = makeBin('&', std::move(left), std::move(right));
		}
		return left;
	}
	std::unique_ptr<Node> parseEquality() {
		auto left = parseRelational();
		while (ok_ && (isOp("==") || isOp("!="))) {
			char op = isOp("==") ? 'E' : 'N';
			advance();
			auto right = parseRelational();
			if (!ok_) return nullptr;
			left = makeBin(op, std::move(left), std::move(right));
		}
		return left;
	}
	std::unique_ptr<Node> parseRelational() {
		auto left = parseAdditive();
		while (ok_ && (isOp("<") || isOp(">") || isOp("<=") || isOp(">="))) {
			char op = isOp("<") ? '<' : isOp(">") ? '>' : isOp("<=") ? 'L' : 'G';
			advance();
			auto right = parseAdditive();
			if (!ok_) return nullptr;
			left = makeBin(op, std::move(left), std::move(right));
		}
		return left;
	}
	std::unique_ptr<Node> parseAdditive() {
		auto left = parseTerm();
		while (ok_ && (isOp("+") || isOp("-"))) {
			char op = isOp("+") ? '+' : '-';
			advance();
			auto right = parseTerm();
			if (!ok_) return nullptr;
			left = makeBin(op, std::move(left), std::move(right));
		}
		return left;
	}
	std::unique_ptr<Node> parseTerm() {
		auto left = parseUnary();
		while (ok_ && (isOp("*") || isOp("/") || isOp("%"))) {
			char op = isOp("*") ? '*' : isOp("/") ? '/' : '%';
			advance();
			auto right = parseUnary();
			if (!ok_) return nullptr;
			left = makeBin(op, std::move(left), std::move(right));
		}
		return left;
	}
	std::unique_ptr<Node> parseUnary() {
		if (ok_ && isOp("-")) {
			advance();
			auto n = std::make_unique<Node>();
			n->kind = Kind::Neg;
			n->left = parseUnary();
			return n;
		}
		if (ok_ && (isOp("!") || isIdentOp("NOT"))) {
			advance();
			auto n = std::make_unique<Node>();
			n->kind = Kind::Not;
			n->left = parseUnary();
			return n;
		}
		return parsePrimary();
	}
	std::unique_ptr<Node> parsePrimary() {
		if (!ok_) return nullptr;
		if (cur_.type == TokType::Number) {
			auto n = std::make_unique<Node>();
			n->kind = Kind::Number;
			n->number = cur_.num;
			advance();
			return n;
		}
		if (cur_.type == TokType::LParen) {
			advance();
			auto inner = parseOr();
			if (!ok_) return nullptr;
			if (cur_.type != TokType::RParen) { setError("expected ')'"); return nullptr; }
			advance();
			return inner;
		}
		if (cur_.type == TokType::Ident) {
			std::string name = cur_.text;
			advance();
			if (cur_.type == TokType::LParen) {
				advance();
				auto arg = parseOr();
				if (!ok_) return nullptr;
				if (cur_.type != TokType::RParen) { setError("expected ')' after " + name + "(...)"); return nullptr; }
				advance();
				auto n = std::make_unique<Node>();
				if (name == "PEEK") n->kind = Kind::Peek8;
				else if (name == "PEEKW") n->kind = Kind::Peek16;
				else { setError("unknown function '" + name + "'"); return nullptr; }
				n->left = std::move(arg);
				return n;
			}
			auto n = std::make_unique<Node>();
			n->kind = Kind::Var;
			n->name = name;
			return n;
		}
		setError("unexpected token '" + cur_.text + "'");
		return nullptr;
	}

	Lexer lex_;
	Token cur_;
	std::string* error_;
	bool ok_;
};

} // namespace

std::unique_ptr<BreakExpr> BreakExpr::parse(const std::string& text, std::string* error)
{
	std::string localError;
	Parser p(text, &localError);
	auto root = p.parseAll();
	if (!root) {
		if (error) *error = localError.empty() ? "parse error" : localError;
		return nullptr;
	}
	return std::unique_ptr<BreakExpr>(new BreakExpr(std::move(root)));
}

BreakExpr::BreakExpr(std::unique_ptr<Node> root) : root_(std::move(root)) {}

BreakExpr::~BreakExpr() = default;

namespace {
int32_t evalNode(const BreakExpr::Node* n, const BreakExprContext& ctx)
{
	using Kind = BreakExpr::Node::Kind;
	switch (n->kind) {
	case Kind::Number: return n->number;
	case Kind::Var: return ctx.reg(n->name);
	case Kind::Peek8: return ctx.peek8((uint16_t)evalNode(n->left.get(), ctx));
	case Kind::Peek16: {
		uint16_t addr = (uint16_t)evalNode(n->left.get(), ctx);
		return (int32_t)(ctx.peek8(addr) | (ctx.peek8((uint16_t)(addr + 1)) << 8));
	}
	case Kind::Neg: return -evalNode(n->left.get(), ctx);
	case Kind::Not: return evalNode(n->left.get(), ctx) == 0 ? 1 : 0;
	case Kind::BinOp: {
		// Short-circuit && / || like any normal expression language --
		// matters here since conditions like "A!=0 && PEEK(HL)==1" should
		// be safe to write without a separate null/range check.
		if (n->op == 'A') return (evalNode(n->left.get(), ctx) != 0 && evalNode(n->right.get(), ctx) != 0) ? 1 : 0;
		if (n->op == 'O') return (evalNode(n->left.get(), ctx) != 0 || evalNode(n->right.get(), ctx) != 0) ? 1 : 0;
		int32_t l = evalNode(n->left.get(), ctx);
		int32_t r = evalNode(n->right.get(), ctx);
		switch (n->op) {
		case '+': return l + r;
		case '-': return l - r;
		case '*': return l * r;
		case '/': return r != 0 ? l / r : 0;
		case '%': return r != 0 ? l % r : 0;
		case '&': return l & r;
		case '|': return l | r;
		case '^': return l ^ r;
		case '<': return l < r ? 1 : 0;
		case '>': return l > r ? 1 : 0;
		case 'L': return l <= r ? 1 : 0;
		case 'G': return l >= r ? 1 : 0;
		case 'E': return l == r ? 1 : 0;
		case 'N': return l != r ? 1 : 0;
		}
		return 0;
	}
	}
	return 0;
}
} // namespace

int32_t BreakExpr::evaluate(const BreakExprContext& ctx) const
{
	return evalNode(root_.get(), ctx);
}
