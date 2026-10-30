#pragma once
#include <cstdint>
#include <memory>
#include <string>

// Small, self-contained boolean-expression evaluator for conditional
// breakpoints over the debug protocol (BREAKIF, see debugserver.cpp) --
// e.g. "DE>=0x8000", "SP<0x100 && PEEK(HL)==0xFF". The vocabulary
// (register names, PEEK()/PEEKW(), C-style operators) is deliberately
// modeled on ZEsarUX's own ZRCP `set-breakpoint` condition language
// (read as a HOWTO for what's useful to support, same clean-room policy
// as the rest of this project's ZEsarUX-derived facts -- see
// Dandanator.h's class comment -- no code shared, this is a from-scratch
// recursive-descent parser over a much smaller grammar than ZEsarUX's
// own). Motivation: a long debugging session (2026-10-30, see project
// memory) repeatedly needed "run until some register/memory condition
// holds" and had no way to express that beyond manual STEP-and-check
// loops driven externally over the socket -- slow and ad-hoc compared
// to a real conditional breakpoint evaluated natively every instruction.
//
// Grammar (standard C-like precedence, low to high):
//   or-expr   := and-expr (('||'|'or') and-expr)*
//   and-expr  := bitor-expr (('&&'|'and') bitor-expr)*
//   bitor-expr  := bitxor-expr ('|' bitxor-expr)*
//   bitxor-expr := bitand-expr ('^' bitand-expr)*
//   bitand-expr := equality ('&' equality)*
//   equality  := relational (('=='|'!=') relational)*
//   relational := additive (('<'|'>'|'<='|'>=') additive)*
//   additive  := term (('+'|'-') term)*
//   term      := unary (('*'|'/'|'%') unary)*
//   unary     := ('-'|'!'|'not')? primary
//   primary   := NUMBER | IDENT | IDENT '(' or-expr ')' | '(' or-expr ')'
//
// NUMBER accepts plain decimal (123), 0x-prefixed hex (0x1a2b), $-prefixed
// hex ($1a2b), or trailing-H hex (1a2bH) -- the last matching both
// Z80-assembler convention and ZEsarUX's own NUMERICVALUE syntax.
//
// Variables (case-insensitive): PC, SP, AF, BC, DE, HL, IX, IY, A, B, C,
// D, E, H, L, AF2, BC2, DE2, HL2 (shadow registers), I, R, IFF1, IFF2, IM.
// Functions: PEEK(addr) (8-bit), PEEKW(addr) (16-bit, little-endian).
class BreakExprContext
{
public:
	virtual ~BreakExprContext() {}
	// `name` is already upper-cased by the parser before this is called.
	virtual int32_t reg(const std::string& name) const = 0;
	virtual uint8_t peek8(uint16_t addr) const = 0;
};

class BreakExpr
{
public:
	// Forward-declared here, fully defined in BreakExpr.cpp -- public
	// (not just forward-declared under `private:`) because the parser
	// and evaluator free functions in the .cpp build and walk the tree
	// from outside the class itself.
	struct Node;

	// Returns nullptr and fills *error with a human-readable message on
	// a malformed expression -- never throws.
	static std::unique_ptr<BreakExpr> parse(const std::string& text, std::string* error);

	// Non-zero result means "the breakpoint fires".
	int32_t evaluate(const BreakExprContext& ctx) const;

	~BreakExpr();
	// Always used via std::unique_ptr<BreakExpr> (see parse()) -- no
	// need for BreakExpr itself to be copyable or movable, and
	// explicitly deleting both here (rather than leaving them
	// implicit) avoids the compiler trying to instantiate them -- and
	// thus Node's deleter -- in translation units that only see Node
	// forward-declared (the usual incomplete-type-with-unique_ptr
	// pitfall).
	BreakExpr(const BreakExpr&) = delete;
	BreakExpr& operator=(const BreakExpr&) = delete;
	BreakExpr(BreakExpr&&) = delete;
	BreakExpr& operator=(BreakExpr&&) = delete;

private:
	// Declared only -- defined in BreakExpr.cpp (used solely by parse()
	// there), since Node is only forward-declared at this point and an
	// inline body here would need it complete (the classic
	// incomplete-type-behind-unique_ptr pitfall).
	explicit BreakExpr(std::unique_ptr<Node> root);
	std::unique_ptr<Node> root_;
};
