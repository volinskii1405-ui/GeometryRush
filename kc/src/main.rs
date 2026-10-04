// kc — маленький компилируемый низкоуровневый язык с C-подобным синтаксисом.
// Компилятор: kc -> x86-64 NASM -> nasm + ld -> ELF для Linux.
use std::collections::HashMap;
use std::process::Command;
use std::{env, fs, process};

type Res<T> = Result<T, String>;

// Стандартная библиотека, написанная на самом kc (поверх syscall).
const PRELUDE: &str = r#"
int strlen(int s) {
    int n = 0;
    while (load8(s + n) != 0) { n++; }
    return n;
}
int write(int fd, int buf, int len) { return syscall(1, fd, buf, len); }
int read(int fd, int buf, int len)  { return syscall(0, fd, buf, len); }
int exit(int code) { syscall(60, code); return 0; }
int alloc(int n) { return syscall(9, 0, n, 3, 34, -1, 0); } // mmap anon rw
int print(int s)   { return write(1, s, strlen(s)); }
int putchar(int c) { int b = 0; store8(&b, c); return write(1, &b, 1); }
int println(int s) { print(s); return putchar(10); }
int print_neg(int n) {
    if (n <= -10) { print_neg(n / 10); }
    return putchar('0' - (n % 10));
}
int print_int(int n) {
    if (n < 0) { putchar('-'); return print_neg(n); }
    return print_neg(-n);
}
int free(int p, int n) { return syscall(11, p, n); }   // munmap
int abs(int n) { return n < 0 ? -n : n; }
int strcmp(int a, int b) {
    while (load8(a) != 0 && load8(a) == load8(b)) { a++; b++; }
    return load8(a) - load8(b);
}
int memset(int p, int c, int n) {
    for (int i = 0; i < n; i++) { store8(p + i, c); }
    return p;
}
int memcpy(int dst, int src, int n) {
    for (int i = 0; i < n; i++) { store8(dst + i, load8(src + i)); }
    return dst;
}
"#;

const KEYWORDS: [&str; 10] = ["int", "void", "if", "else", "while", "do", "for", "return", "break", "continue"];

// ───────────────────────── Лексер ─────────────────────────

#[derive(Debug, Clone, PartialEq)]
enum Tok {
    Num(i64),
    Str(Vec<u8>),
    Id(String),
    P(&'static str),
    Eof,
}

const PUNCTS: &[&str] = &[
    "<<=", ">>=", "==", "!=", "<=", ">=", "&&", "||", "<<", ">>", "++", "--", "+=", "-=", "*=",
    "/=", "%=", "&=", "|=", "^=", "+", "-", "*", "/", "%", "&", "|", "^", "~", "!", "<", ">", "=",
    "(", ")", "{", "}", "[", "]", ";", ",", "?", ":",
];

// Разбирает escape-последовательность; `i` указывает на символ после '\\'.
fn esc_at(b: &[u8], i: &mut usize, line: usize) -> Res<u8> {
    if *i >= b.len() {
        return Err(format!("line {}: unterminated escape", line));
    }
    if b[*i] == b'x' {
        let st = *i + 1;
        let mut en = st;
        while en < b.len() && en < st + 2 && b[en].is_ascii_hexdigit() {
            en += 1;
        }
        if en == st {
            return Err(format!("line {}: '\\x' needs hex digits", line));
        }
        let v = u8::from_str_radix(std::str::from_utf8(&b[st..en]).unwrap(), 16).unwrap();
        *i = en;
        return Ok(v);
    }
    let r = esc(b[*i], line)?;
    *i += 1;
    Ok(r)
}

fn esc(c: u8, line: usize) -> Res<u8> {
    Ok(match c {
        b'e' => 27,
        b'n' => 10,
        b't' => 9,
        b'r' => 13,
        b'0' => 0,
        b'\\' => b'\\',
        b'"' => b'"',
        b'\'' => b'\'',
        _ => return Err(format!("line {}: unknown escape '\\{}'", line, c as char)),
    })
}

fn lex(src: &str) -> Res<Vec<(Tok, usize)>> {
    let b = src.as_bytes();
    let mut i = 0;
    let mut line = 1;
    let mut out: Vec<(Tok, usize)> = Vec::new();
    while i < b.len() {
        let c = b[i];
        if c == b'\n' {
            line += 1;
            i += 1;
            continue;
        }
        if c.is_ascii_whitespace() {
            i += 1;
            continue;
        }
        if c == b'/' && i + 1 < b.len() && b[i + 1] == b'/' {
            while i < b.len() && b[i] != b'\n' {
                i += 1;
            }
            continue;
        }
        if c == b'/' && i + 1 < b.len() && b[i + 1] == b'*' {
            i += 2;
            loop {
                if i + 1 >= b.len() {
                    return Err(format!("line {}: unterminated comment", line));
                }
                if b[i] == b'\n' {
                    line += 1;
                }
                if b[i] == b'*' && b[i + 1] == b'/' {
                    i += 2;
                    break;
                }
                i += 1;
            }
            continue;
        }
        if c.is_ascii_digit() {
            if c == b'0' && i + 1 < b.len() && (b[i + 1] == b'x' || b[i + 1] == b'X') {
                i += 2;
                let st = i;
                while i < b.len() && b[i].is_ascii_hexdigit() {
                    i += 1;
                }
                let v = u64::from_str_radix(&src[st..i], 16)
                    .map_err(|_| format!("line {}: bad hex literal", line))?;
                out.push((Tok::Num(v as i64), line));
            } else {
                let st = i;
                while i < b.len() && b[i].is_ascii_digit() {
                    i += 1;
                }
                let v: u64 = src[st..i]
                    .parse()
                    .map_err(|_| format!("line {}: bad number", line))?;
                out.push((Tok::Num(v as i64), line));
            }
            continue;
        }
        if c.is_ascii_alphabetic() || c == b'_' {
            let st = i;
            while i < b.len() && (b[i].is_ascii_alphanumeric() || b[i] == b'_') {
                i += 1;
            }
            out.push((Tok::Id(src[st..i].to_string()), line));
            continue;
        }
        if c == b'"' {
            i += 1;
            let mut s = Vec::new();
            loop {
                if i >= b.len() || b[i] == b'\n' {
                    return Err(format!("line {}: unterminated string", line));
                }
                if b[i] == b'"' {
                    i += 1;
                    break;
                }
                if b[i] == b'\\' {
                    i += 1;
                    s.push(esc_at(b, &mut i, line)?);
                } else {
                    s.push(b[i]);
                    i += 1;
                }
            }
            out.push((Tok::Str(s), line));
            continue;
        }
        if c == b'\'' {
            i += 1;
            if i >= b.len() {
                return Err(format!("line {}: bad char literal", line));
            }
            let v = if b[i] == b'\\' {
                i += 1;
                esc_at(b, &mut i, line)?
            } else {
                let r = b[i];
                i += 1;
                r
            };
            if i >= b.len() || b[i] != b'\'' {
                return Err(format!("line {}: bad char literal", line));
            }
            i += 1;
            out.push((Tok::Num(v as i64), line));
            continue;
        }
        let mut found = false;
        for p in PUNCTS {
            if b[i..].starts_with(p.as_bytes()) {
                out.push((Tok::P(*p), line));
                i += p.len();
                found = true;
                break;
            }
        }
        if !found {
            return Err(format!("line {}: unexpected character '{}'", line, c as char));
        }
    }
    out.push((Tok::Eof, line));
    Ok(out)
}

// ───────────────────────── AST ─────────────────────────

#[derive(Debug, Clone)]
enum Expr {
    Num(i64),
    Str(Vec<u8>),
    Var(String, usize),
    Addr(String, usize),
    Deref(Box<Expr>),
    Un(&'static str, Box<Expr>),
    Bin(&'static str, Box<Expr>, Box<Expr>),
    // op == "" — простое присваивание, иначе составное (`+=` и т.п.); адрес lhs вычисляется один раз
    Assign(&'static str, Box<Expr>, Box<Expr>),
    // ++x / --x (pre = true) и x++ / x-- (pre = false)
    IncDec(bool, i64, Box<Expr>),
    Cond(Box<Expr>, Box<Expr>, Box<Expr>),
    Call(String, Vec<Expr>, usize),
}

#[derive(Debug)]
enum Init {
    None,
    Expr(Expr),
    List(Vec<Expr>),
}

#[derive(Debug)]
struct Decl {
    name: String,
    arr: Option<i64>, // Some(n) — массив из n элементов по 8 байт
    init: Init,
    line: usize,
}

#[derive(Debug)]
enum Stmt {
    Decl(Vec<Decl>),
    Expr(Expr),
    If(Expr, Box<Stmt>, Option<Box<Stmt>>),
    While(Expr, Box<Stmt>),
    DoWhile(Box<Stmt>, Expr),
    For(Option<Box<Stmt>>, Option<Expr>, Option<Expr>, Box<Stmt>),
    Return(Option<Expr>),
    Break(usize),
    Continue(usize),
    Block(Vec<Stmt>),
}

struct Func {
    name: String,
    params: Vec<String>,
    body: Vec<Stmt>,
}

struct Program {
    funcs: Vec<Func>,
    globals: Vec<Decl>,
}

// ───────────────────────── Парсер ─────────────────────────

struct Parser {
    t: Vec<(Tok, usize)>,
    p: usize,
}

fn prec(op: &str) -> Option<u8> {
    Some(match op {
        "||" => 1,
        "&&" => 2,
        "|" => 3,
        "^" => 4,
        "&" => 5,
        "==" | "!=" => 6,
        "<" | "<=" | ">" | ">=" => 7,
        "<<" | ">>" => 8,
        "+" | "-" => 9,
        "*" | "/" | "%" => 10,
        _ => return None,
    })
}

fn is_lvalue(e: &Expr) -> bool {
    matches!(e, Expr::Var(..) | Expr::Deref(_))
}

impl Parser {
    fn peek(&self) -> &Tok {
        &self.t[self.p].0
    }
    fn line(&self) -> usize {
        self.t[self.p].1
    }
    fn next(&mut self) -> Tok {
        let t = self.t[self.p].0.clone();
        if self.p < self.t.len() - 1 {
            self.p += 1;
        }
        t
    }
    fn is_p(&self, s: &str) -> bool {
        matches!(self.peek(), Tok::P(x) if *x == s)
    }
    fn eat_p(&mut self, s: &str) -> bool {
        if self.is_p(s) {
            self.next();
            true
        } else {
            false
        }
    }
    fn is_kw(&self, s: &str) -> bool {
        matches!(self.peek(), Tok::Id(x) if x.as_str() == s)
    }
    fn eat_kw(&mut self, s: &str) -> bool {
        if self.is_kw(s) {
            self.next();
            true
        } else {
            false
        }
    }
    fn expect_p(&mut self, s: &str) -> Res<()> {
        if self.eat_p(s) {
            Ok(())
        } else {
            Err(format!("line {}: expected '{}', found {:?}", self.line(), s, self.peek()))
        }
    }
    fn ident(&mut self) -> Res<String> {
        let line = self.line();
        match self.next() {
            Tok::Id(n) if !KEYWORDS.contains(&n.as_str()) => Ok(n),
            t => Err(format!("line {}: expected identifier, found {:?}", line, t)),
        }
    }
    fn const_num(&mut self) -> Res<i64> {
        let neg = self.eat_p("-");
        let line = self.line();
        match self.next() {
            Tok::Num(n) => Ok(if neg { n.wrapping_neg() } else { n }),
            t => Err(format!("line {}: expected constant number, found {:?}", line, t)),
        }
    }

    fn program(&mut self, prog: &mut Program) -> Res<()> {
        while *self.peek() != Tok::Eof {
            if !(self.is_kw("int") || self.is_kw("void")) {
                return Err(format!(
                    "line {}: expected 'int' or 'void' at top level, found {:?}",
                    self.line(),
                    self.peek()
                ));
            }
            let save = self.p;
            self.next();
            let name = self.ident()?;
            if self.eat_p("(") {
                let mut params = Vec::new();
                if !self.is_p(")") && !(self.is_kw("void") && matches!(self.t[self.p + 1].0, Tok::P(")"))) {
                    loop {
                        if !self.eat_kw("int") {
                            return Err(format!("line {}: expected 'int' in parameter list", self.line()));
                        }
                        params.push(self.ident()?);
                        if !self.eat_p(",") {
                            break;
                        }
                    }
                } else {
                    self.eat_kw("void");
                }
                self.expect_p(")")?;
                if params.len() > 6 {
                    return Err(format!("function '{}': at most 6 parameters supported", name));
                }
                let body = self.block()?;
                prog.funcs.push(Func { name, params, body });
            } else {
                self.p = save;
                for d in self.decls(true)? {
                    prog.globals.push(d);
                }
            }
        }
        Ok(())
    }

    fn block(&mut self) -> Res<Vec<Stmt>> {
        self.expect_p("{")?;
        let mut v = Vec::new();
        while !self.is_p("}") {
            if *self.peek() == Tok::Eof {
                return Err(format!("line {}: unexpected end of file, missing '}}'", self.line()));
            }
            v.push(self.stmt()?);
        }
        self.expect_p("}")?;
        Ok(v)
    }

    // int a = 1, b, buf[16], xs[] = {1, 2, 3};
    // В глобальной области (global = true) инициализаторы — только константы и строки.
    fn decls(&mut self, global: bool) -> Res<Vec<Decl>> {
        if !self.eat_kw("int") {
            return Err(format!("line {}: variables must be declared as 'int'", self.line()));
        }
        let mut out = Vec::new();
        loop {
            let line = self.line();
            let name = self.ident()?;
            let mut arr = None;
            let mut open_arr = false;
            if self.eat_p("[") {
                if self.is_p("]") {
                    open_arr = true;
                } else {
                    let n = self.const_num()?;
                    if n <= 0 {
                        return Err(format!("line {}: array size must be positive", line));
                    }
                    arr = Some(n);
                }
                self.expect_p("]")?;
            }
            let mut init = Init::None;
            if self.eat_p("=") {
                if self.eat_p("{") {
                    if arr.is_none() && !open_arr {
                        return Err(format!("line {}: '{{...}}' initializer requires an array", line));
                    }
                    let mut items = Vec::new();
                    if !self.is_p("}") {
                        loop {
                            items.push(if global { self.global_value()? } else { self.expr()? });
                            if !self.eat_p(",") || self.is_p("}") {
                                break;
                            }
                        }
                    }
                    self.expect_p("}")?;
                    if open_arr {
                        arr = Some(items.len().max(1) as i64);
                    }
                    if items.len() as i64 > arr.unwrap() {
                        return Err(format!("line {}: too many initializers for '{}'", line, name));
                    }
                    init = Init::List(items);
                } else {
                    if arr.is_some() || open_arr {
                        return Err(format!("line {}: array '{}' needs a '{{...}}' initializer", line, name));
                    }
                    init = Init::Expr(if global { self.global_value()? } else { self.expr()? });
                }
            } else if open_arr {
                return Err(format!("line {}: array '{}' without size needs an initializer", line, name));
            }
            out.push(Decl { name, arr, init, line });
            if !self.eat_p(",") {
                break;
            }
        }
        self.expect_p(";")?;
        Ok(out)
    }

    fn global_value(&mut self) -> Res<Expr> {
        if let Tok::Str(s) = self.peek().clone() {
            self.next();
            return Ok(Expr::Str(s));
        }
        let line = self.line();
        self.const_num()
            .map(Expr::Num)
            .map_err(|_| format!("line {}: global initializer must be a constant or a string", line))
    }

    fn stmt(&mut self) -> Res<Stmt> {
        let line = self.line();
        if self.is_p("{") {
            return Ok(Stmt::Block(self.block()?));
        }
        if self.eat_p(";") {
            return Ok(Stmt::Block(vec![]));
        }
        if self.is_kw("int") {
            return Ok(Stmt::Decl(self.decls(false)?));
        }
        if self.eat_kw("if") {
            self.expect_p("(")?;
            let c = self.expr()?;
            self.expect_p(")")?;
            let t = Box::new(self.stmt()?);
            let e = if self.eat_kw("else") { Some(Box::new(self.stmt()?)) } else { None };
            return Ok(Stmt::If(c, t, e));
        }
        if self.eat_kw("while") {
            self.expect_p("(")?;
            let c = self.expr()?;
            self.expect_p(")")?;
            let b = Box::new(self.stmt()?);
            return Ok(Stmt::While(c, b));
        }
        if self.eat_kw("do") {
            let b = Box::new(self.stmt()?);
            if !self.eat_kw("while") {
                return Err(format!("line {}: expected 'while' after 'do' body", self.line()));
            }
            self.expect_p("(")?;
            let c = self.expr()?;
            self.expect_p(")")?;
            self.expect_p(";")?;
            return Ok(Stmt::DoWhile(b, c));
        }
        if self.eat_kw("for") {
            self.expect_p("(")?;
            let init = if self.eat_p(";") {
                None
            } else if self.is_kw("int") {
                Some(Box::new(Stmt::Decl(self.decls(false)?)))
            } else {
                let e = self.expr()?;
                self.expect_p(";")?;
                Some(Box::new(Stmt::Expr(e)))
            };
            let cond = if self.is_p(";") { None } else { Some(self.expr()?) };
            self.expect_p(";")?;
            let step = if self.is_p(")") { None } else { Some(self.expr()?) };
            self.expect_p(")")?;
            let body = Box::new(self.stmt()?);
            return Ok(Stmt::For(init, cond, step, body));
        }
        if self.eat_kw("return") {
            if self.eat_p(";") {
                return Ok(Stmt::Return(None));
            }
            let e = self.expr()?;
            self.expect_p(";")?;
            return Ok(Stmt::Return(Some(e)));
        }
        if self.eat_kw("break") {
            self.expect_p(";")?;
            return Ok(Stmt::Break(line));
        }
        if self.eat_kw("continue") {
            self.expect_p(";")?;
            return Ok(Stmt::Continue(line));
        }
        let e = self.expr()?;
        self.expect_p(";")?;
        Ok(Stmt::Expr(e))
    }

    fn expr(&mut self) -> Res<Expr> {
        self.assign()
    }

    fn assign(&mut self) -> Res<Expr> {
        let lhs = self.ternary()?;
        if let Tok::P(p) = self.peek().clone() {
            let op: Option<&'static str> = match p {
                "=" => Some(""),
                "+=" => Some("+"),
                "-=" => Some("-"),
                "*=" => Some("*"),
                "/=" => Some("/"),
                "%=" => Some("%"),
                "&=" => Some("&"),
                "|=" => Some("|"),
                "^=" => Some("^"),
                "<<=" => Some("<<"),
                ">>=" => Some(">>"),
                _ => None,
            };
            if let Some(op) = op {
                let line = self.line();
                self.next();
                if !is_lvalue(&lhs) {
                    return Err(format!("line {}: invalid assignment target", line));
                }
                let rhs = self.assign()?;
                return Ok(Expr::Assign(op, Box::new(lhs), Box::new(rhs)));
            }
        }
        Ok(lhs)
    }

    fn ternary(&mut self) -> Res<Expr> {
        let c = self.binary(1)?;
        if !self.eat_p("?") {
            return Ok(c);
        }
        let a = self.expr()?;
        self.expect_p(":")?;
        let b = self.ternary()?;
        Ok(Expr::Cond(Box::new(c), Box::new(a), Box::new(b)))
    }

    fn binary(&mut self, min: u8) -> Res<Expr> {
        let mut lhs = self.unary()?;
        loop {
            let (op, pr) = match self.peek() {
                Tok::P(p) => match prec(p) {
                    Some(pr) if pr >= min => (*p, pr),
                    _ => break,
                },
                _ => break,
            };
            self.next();
            let rhs = self.binary(pr + 1)?;
            lhs = Expr::Bin(op, Box::new(lhs), Box::new(rhs));
        }
        Ok(lhs)
    }

    fn unary(&mut self) -> Res<Expr> {
        let line = self.line();
        if self.eat_p("+") {
            return self.unary();
        }
        if self.eat_p("-") {
            return Ok(match self.unary()? {
                Expr::Num(n) => Expr::Num(n.wrapping_neg()),
                e => Expr::Un("-", Box::new(e)),
            });
        }
        if self.eat_p("!") {
            return Ok(Expr::Un("!", Box::new(self.unary()?)));
        }
        if self.eat_p("~") {
            return Ok(Expr::Un("~", Box::new(self.unary()?)));
        }
        if self.is_p("++") || self.is_p("--") {
            let d = if self.is_p("++") { 1 } else { -1 };
            self.next();
            let e = self.unary()?;
            if !is_lvalue(&e) {
                return Err(format!("line {}: invalid operand for ++/--", line));
            }
            return Ok(Expr::IncDec(true, d, Box::new(e)));
        }
        if self.eat_p("*") {
            return Ok(Expr::Deref(Box::new(self.unary()?)));
        }
        if self.eat_p("&") {
            return match self.unary()? {
                Expr::Var(n, l) => Ok(Expr::Addr(n, l)),
                Expr::Deref(inner) => Ok(*inner), // &*p == p, &a[i] == a + i*8
                _ => Err(format!("line {}: cannot take address of this expression", line)),
            };
        }
        self.postfix()
    }

    fn postfix(&mut self) -> Res<Expr> {
        let mut e = self.primary()?;
        loop {
            let line = self.line();
            if self.eat_p("[") {
                let i = self.expr()?;
                self.expect_p("]")?;
                let off = Expr::Bin("*", Box::new(i), Box::new(Expr::Num(8)));
                e = Expr::Deref(Box::new(Expr::Bin("+", Box::new(e), Box::new(off))));
            } else if self.is_p("++") || self.is_p("--") {
                let d = if self.is_p("++") { 1 } else { -1 };
                self.next();
                if !is_lvalue(&e) {
                    return Err(format!("line {}: invalid operand for ++/--", line));
                }
                e = Expr::IncDec(false, d, Box::new(e));
            } else {
                break;
            }
        }
        Ok(e)
    }

    fn primary(&mut self) -> Res<Expr> {
        let line = self.line();
        match self.next() {
            Tok::Num(n) => Ok(Expr::Num(n)),
            Tok::Str(s) => Ok(Expr::Str(s)),
            Tok::Id(name) => {
                if KEYWORDS.contains(&name.as_str()) {
                    return Err(format!("line {}: unexpected keyword '{}'", line, name));
                }
                if self.eat_p("(") {
                    let mut args = Vec::new();
                    if !self.is_p(")") {
                        loop {
                            args.push(self.expr()?);
                            if !self.eat_p(",") {
                                break;
                            }
                        }
                    }
                    self.expect_p(")")?;
                    Ok(Expr::Call(name, args, line))
                } else {
                    Ok(Expr::Var(name, line))
                }
            }
            Tok::P("(") => {
                let e = self.expr()?;
                self.expect_p(")")?;
                Ok(e)
            }
            t => Err(format!("line {}: unexpected {:?}", line, t)),
        }
    }
}

// ───────────────────────── Кодогенерация ─────────────────────────

const REGS: [&str; 6] = ["rdi", "rsi", "rdx", "rcx", "r8", "r9"];
const SYS: [&str; 7] = ["rax", "rdi", "rsi", "rdx", "r10", "r8", "r9"];

#[derive(Clone, Copy)]
struct Local {
    off: i64,    // адрес = rbp - off
    array: bool, // имя массива вычисляется в его адрес и не присваивается
}

struct Gen {
    body: String,
    data: String,
    nstr: usize,
    nlabel: usize,
    globals: HashMap<String, bool>, // имя -> массив?
    funcs: HashMap<String, usize>,
    scopes: Vec<HashMap<String, Local>>,
    nslots: i64,
    loops: Vec<(String, String)>, // (continue, break)
    ret: String,
}

impl Gen {
    fn new() -> Gen {
        Gen {
            body: String::new(),
            data: String::new(),
            nstr: 0,
            nlabel: 0,
            globals: HashMap::new(),
            funcs: HashMap::new(),
            scopes: Vec::new(),
            nslots: 0,
            loops: Vec::new(),
            ret: String::new(),
        }
    }

    fn e(&mut self, s: &str) {
        self.body.push_str("    ");
        self.body.push_str(s);
        self.body.push('\n');
    }
    fn lbl(&mut self, l: &str) {
        self.body.push_str(l);
        self.body.push_str(":\n");
    }
    fn new_label(&mut self) -> String {
        self.nlabel += 1;
        format!("L{}", self.nlabel)
    }
    fn string(&mut self, s: &[u8]) -> String {
        let id = self.nstr;
        self.nstr += 1;
        let mut bytes: Vec<String> = s.iter().map(|b| b.to_string()).collect();
        bytes.push("0".to_string());
        self.data.push_str(&format!("str_{}: db {}\n", id, bytes.join(",")));
        format!("str_{}", id)
    }

    fn declare(&mut self, name: &str, slots: i64, array: bool, line: usize) -> Res<i64> {
        if self.scopes.last().unwrap().contains_key(name) {
            return Err(format!("line {}: variable '{}' is already declared in this scope", line, name));
        }
        self.nslots += slots;
        let off = self.nslots * 8;
        self.scopes.last_mut().unwrap().insert(name.to_string(), Local { off, array });
        Ok(off)
    }

    // Операнд памяти переменной и признак «это массив».
    fn var_operand(&self, name: &str, line: usize) -> Res<(String, bool)> {
        for s in self.scopes.iter().rev() {
            if let Some(v) = s.get(name) {
                return Ok((format!("[rbp-{}]", v.off), v.array));
            }
        }
        if let Some(arr) = self.globals.get(name) {
            return Ok((format!("[rel g_{}]", name), *arr));
        }
        Err(format!("line {}: undefined variable '{}'", line, name))
    }

    // Кладёт в rax адрес lvalue.
    fn addr(&mut self, x: &Expr) -> Res<()> {
        match x {
            Expr::Var(n, l) => {
                let (o, arr) = self.var_operand(n, *l)?;
                if arr {
                    return Err(format!("line {}: cannot assign to array '{}'", l, n));
                }
                self.e(&format!("lea rax, {}", o));
            }
            Expr::Deref(p) => self.expr(p)?,
            _ => return Err("invalid assignment target".to_string()),
        }
        Ok(())
    }

    // rax = rax <op> rcx
    fn binop(&mut self, op: &str) {
        match op {
            "+" => self.e("add rax, rcx"),
            "-" => self.e("sub rax, rcx"),
            "*" => self.e("imul rax, rcx"),
            "/" => {
                self.e("cqo");
                self.e("idiv rcx");
            }
            "%" => {
                self.e("cqo");
                self.e("idiv rcx");
                self.e("mov rax, rdx");
            }
            "&" => self.e("and rax, rcx"),
            "|" => self.e("or rax, rcx"),
            "^" => self.e("xor rax, rcx"),
            "<<" => self.e("shl rax, cl"),
            ">>" => self.e("sar rax, cl"),
            _ => {
                let cc = match op {
                    "==" => "e",
                    "!=" => "ne",
                    "<" => "l",
                    "<=" => "le",
                    ">" => "g",
                    _ => "ge",
                };
                self.e("cmp rax, rcx");
                self.e(&format!("set{} al", cc));
                self.e("movzx eax, al");
            }
        }
    }

    fn expr(&mut self, x: &Expr) -> Res<()> {
        match x {
            Expr::Num(n) => self.e(&format!("mov rax, {}", n)),
            Expr::Str(s) => {
                let l = self.string(s);
                self.e(&format!("lea rax, [rel {}]", l));
            }
            Expr::Var(n, l) => {
                let (o, arr) = self.var_operand(n, *l)?;
                let ins = if arr { "lea" } else { "mov" };
                self.e(&format!("{} rax, {}", ins, o));
            }
            Expr::Addr(n, l) => {
                let (o, _) = self.var_operand(n, *l)?;
                self.e(&format!("lea rax, {}", o));
            }
            Expr::Deref(p) => {
                self.expr(p)?;
                self.e("mov rax, [rax]");
            }
            Expr::Un(op, a) => {
                self.expr(a)?;
                match *op {
                    "-" => self.e("neg rax"),
                    "~" => self.e("not rax"),
                    _ => {
                        self.e("cmp rax, 0");
                        self.e("sete al");
                        self.e("movzx eax, al");
                    }
                }
            }
            Expr::Assign(op, l, r) => {
                // быстрый путь: x = expr
                if let (true, Expr::Var(n, ln)) = (op.is_empty(), &**l) {
                    let (o, arr) = self.var_operand(n, *ln)?;
                    if arr {
                        return Err(format!("line {}: cannot assign to array '{}'", ln, n));
                    }
                    self.expr(r)?;
                    self.e(&format!("mov {}, rax", o));
                    return Ok(());
                }
                self.addr(l)?;
                self.e("push rax");
                if op.is_empty() {
                    self.expr(r)?;
                } else {
                    self.e("push qword [rax]");
                    self.expr(r)?;
                    self.e("mov rcx, rax");
                    self.e("pop rax");
                    self.binop(op);
                }
                self.e("pop rcx");
                self.e("mov [rcx], rax");
            }
            Expr::IncDec(pre, d, e) => {
                self.addr(e)?;
                self.e("mov rcx, rax");
                self.e("mov rax, [rcx]");
                if *pre {
                    self.e(&format!("add rax, {}", d));
                    self.e("mov [rcx], rax");
                } else {
                    self.e(&format!("lea rdx, [rax{:+}]", d));
                    self.e("mov [rcx], rdx");
                }
            }
            Expr::Cond(c, a, b) => {
                let lelse = self.new_label();
                let lend = self.new_label();
                self.expr(c)?;
                self.e("cmp rax, 0");
                self.e(&format!("je {}", lelse));
                self.expr(a)?;
                self.e(&format!("jmp {}", lend));
                self.lbl(&lelse);
                self.expr(b)?;
                self.lbl(&lend);
            }
            Expr::Bin(op, a, b) => match *op {
                "&&" => {
                    let lf = self.new_label();
                    let le = self.new_label();
                    self.expr(a)?;
                    self.e("cmp rax, 0");
                    self.e(&format!("je {}", lf));
                    self.expr(b)?;
                    self.e("cmp rax, 0");
                    self.e(&format!("je {}", lf));
                    self.e("mov eax, 1");
                    self.e(&format!("jmp {}", le));
                    self.lbl(&lf);
                    self.e("xor eax, eax");
                    self.lbl(&le);
                }
                "||" => {
                    let lt = self.new_label();
                    let le = self.new_label();
                    self.expr(a)?;
                    self.e("cmp rax, 0");
                    self.e(&format!("jne {}", lt));
                    self.expr(b)?;
                    self.e("cmp rax, 0");
                    self.e(&format!("jne {}", lt));
                    self.e("xor eax, eax");
                    self.e(&format!("jmp {}", le));
                    self.lbl(&lt);
                    self.e("mov eax, 1");
                    self.lbl(&le);
                }
                _ => {
                    self.expr(a)?;
                    self.e("push rax");
                    self.expr(b)?;
                    self.e("mov rcx, rax");
                    self.e("pop rax");
                    self.binop(op);
                }
            },
            Expr::Call(name, args, line) => match name.as_str() {
                "syscall" => {
                    if args.is_empty() || args.len() > 7 {
                        return Err(format!("line {}: syscall takes 1..7 arguments", line));
                    }
                    for a in args {
                        self.expr(a)?;
                        self.e("push rax");
                    }
                    for i in (0..args.len()).rev() {
                        self.e(&format!("pop {}", SYS[i]));
                    }
                    self.e("syscall");
                }
                "load8" => {
                    if args.len() != 1 {
                        return Err(format!("line {}: load8 takes 1 argument", line));
                    }
                    self.expr(&args[0])?;
                    self.e("movzx eax, byte [rax]");
                }
                "store8" => {
                    if args.len() != 2 {
                        return Err(format!("line {}: store8 takes 2 arguments", line));
                    }
                    self.expr(&args[0])?;
                    self.e("push rax");
                    self.expr(&args[1])?;
                    self.e("pop rcx");
                    self.e("mov [rcx], al");
                }
                _ => {
                    let n = *self
                        .funcs
                        .get(name)
                        .ok_or_else(|| format!("line {}: undefined function '{}'", line, name))?;
                    if n != args.len() {
                        return Err(format!(
                            "line {}: function '{}' expects {} argument(s), got {}",
                            line,
                            name,
                            n,
                            args.len()
                        ));
                    }
                    for a in args {
                        self.expr(a)?;
                        self.e("push rax");
                    }
                    for i in (0..args.len()).rev() {
                        self.e(&format!("pop {}", REGS[i]));
                    }
                    self.e(&format!("call f_{}", name));
                }
            },
        }
        Ok(())
    }

    fn local_decl(&mut self, d: &Decl) -> Res<()> {
        match d.arr {
            None => {
                match &d.init {
                    Init::Expr(e) => self.expr(e)?,
                    _ => self.e("xor eax, eax"),
                }
                let off = self.declare(&d.name, 1, false, d.line)?;
                self.e(&format!("mov [rbp-{}], rax", off));
            }
            Some(n) => {
                // массив: n слотов, элемент i лежит по адресу base + 8*i
                let items: &[Expr] = match &d.init {
                    Init::List(v) => v,
                    _ => &[],
                };
                // инициализаторы считаем до объявления имени (как и для скаляров)
                for it in items {
                    self.expr(it)?;
                    self.e("push rax");
                }
                let base = self.declare(&d.name, n, true, d.line)?;
                self.e(&format!("lea rdi, [rbp-{}]", base));
                self.e(&format!("mov rcx, {}", n));
                self.e("xor eax, eax");
                self.e("rep stosq");
                for i in (0..items.len()).rev() {
                    self.e("pop rax");
                    self.e(&format!("mov [rbp-{}], rax", base - 8 * i as i64));
                }
            }
        }
        Ok(())
    }

    fn stmt(&mut self, s: &Stmt) -> Res<()> {
        match s {
            Stmt::Block(v) => {
                self.scopes.push(HashMap::new());
                for st in v {
                    self.stmt(st)?;
                }
                self.scopes.pop();
            }
            Stmt::Decl(ds) => {
                for d in ds {
                    self.local_decl(d)?;
                }
            }
            Stmt::Expr(e) => self.expr(e)?,
            Stmt::If(c, t, e) => {
                let lelse = self.new_label();
                let lend = self.new_label();
                self.expr(c)?;
                self.e("cmp rax, 0");
                self.e(&format!("je {}", lelse));
                self.stmt(t)?;
                self.e(&format!("jmp {}", lend));
                self.lbl(&lelse);
                if let Some(e) = e {
                    self.stmt(e)?;
                }
                self.lbl(&lend);
            }
            Stmt::While(c, b) => {
                let lcont = self.new_label();
                let lbrk = self.new_label();
                self.lbl(&lcont);
                self.expr(c)?;
                self.e("cmp rax, 0");
                self.e(&format!("je {}", lbrk));
                self.loops.push((lcont.clone(), lbrk.clone()));
                self.stmt(b)?;
                self.loops.pop();
                self.e(&format!("jmp {}", lcont));
                self.lbl(&lbrk);
            }
            Stmt::DoWhile(b, c) => {
                let ltop = self.new_label();
                let lcont = self.new_label();
                let lbrk = self.new_label();
                self.lbl(&ltop);
                self.loops.push((lcont.clone(), lbrk.clone()));
                self.stmt(b)?;
                self.loops.pop();
                self.lbl(&lcont);
                self.expr(c)?;
                self.e("cmp rax, 0");
                self.e(&format!("jne {}", ltop));
                self.lbl(&lbrk);
            }
            Stmt::For(init, cond, step, body) => {
                self.scopes.push(HashMap::new());
                if let Some(i) = init {
                    self.stmt(i)?;
                }
                let ltop = self.new_label();
                let lcont = self.new_label();
                let lbrk = self.new_label();
                self.lbl(&ltop);
                if let Some(c) = cond {
                    self.expr(c)?;
                    self.e("cmp rax, 0");
                    self.e(&format!("je {}", lbrk));
                }
                self.loops.push((lcont.clone(), lbrk.clone()));
                self.stmt(body)?;
                self.loops.pop();
                self.lbl(&lcont);
                if let Some(st) = step {
                    self.expr(st)?;
                }
                self.e(&format!("jmp {}", ltop));
                self.lbl(&lbrk);
                self.scopes.pop();
            }
            Stmt::Return(e) => {
                match e {
                    Some(e) => self.expr(e)?,
                    None => self.e("xor eax, eax"),
                }
                let r = self.ret.clone();
                self.e(&format!("jmp {}", r));
            }
            Stmt::Break(line) => {
                let l = self.loops.last().ok_or(format!("line {}: 'break' outside of a loop", line))?.1.clone();
                self.e(&format!("jmp {}", l));
            }
            Stmt::Continue(line) => {
                let l = self.loops.last().ok_or(format!("line {}: 'continue' outside of a loop", line))?.0.clone();
                self.e(&format!("jmp {}", l));
            }
        }
        Ok(())
    }

    fn func(&mut self, f: &Func) -> Res<String> {
        self.body.clear();
        self.scopes = vec![HashMap::new()];
        self.nslots = 0;
        self.loops.clear();
        self.ret = self.new_label();
        for (i, p) in f.params.iter().enumerate() {
            let off = self.declare(p, 1, false, 0).map_err(|e| format!("in '{}': {}", f.name, e))?;
            self.e(&format!("mov [rbp-{}], {}", off, REGS[i]));
        }
        self.scopes.push(HashMap::new());
        for s in &f.body {
            self.stmt(s).map_err(|e| format!("in function '{}': {}", f.name, e))?;
        }
        self.scopes.pop();
        self.e("xor eax, eax");
        let r = self.ret.clone();
        self.lbl(&r);
        let frame = (self.nslots * 8 + 15) / 16 * 16;
        Ok(format!(
            "f_{}:\n    push rbp\n    mov rbp, rsp\n    sub rsp, {}\n{}    leave\n    ret\n\n",
            f.name, frame, self.body
        ))
    }

    fn global_word(&mut self, e: &Expr) -> String {
        match e {
            Expr::Num(n) => n.to_string(),
            Expr::Str(s) => self.string(s),
            _ => unreachable!("parser only produces constant global initializers"),
        }
    }

    fn program(&mut self, p: &Program) -> Res<String> {
        for g in &p.globals {
            if self.globals.insert(g.name.clone(), g.arr.is_some()).is_some() {
                return Err(format!("line {}: global '{}' defined twice", g.line, g.name));
            }
        }
        for f in &p.funcs {
            if ["syscall", "load8", "store8"].contains(&f.name.as_str()) {
                return Err(format!("'{}' is a reserved intrinsic name", f.name));
            }
            if self.funcs.insert(f.name.clone(), f.params.len()).is_some() {
                return Err(format!("function '{}' defined twice", f.name));
            }
        }
        match self.funcs.get("main") {
            None => return Err("no 'main' function".to_string()),
            Some(n) if *n != 0 => return Err("'main' must take no parameters".to_string()),
            _ => {}
        }
        let mut text = String::new();
        for f in &p.funcs {
            let code = self.func(f)?;
            text.push_str(&code);
        }
        let mut gdata = String::new();
        for g in &p.globals {
            let words: Vec<String> = match &g.init {
                Init::None => vec![],
                Init::Expr(e) => vec![self.global_word(e)],
                Init::List(v) => v.iter().map(|e| self.global_word(e)).collect(),
            };
            let n = g.arr.unwrap_or(1) as usize;
            let pad = n - words.len().min(n);
            if words.is_empty() {
                gdata.push_str(&format!("g_{}: times {} dq 0\n", g.name, n));
            } else {
                gdata.push_str(&format!("g_{}: dq {}\n", g.name, words.join(",")));
                if pad > 0 {
                    gdata.push_str(&format!("    times {} dq 0\n", pad));
                }
            }
        }
        let mut out = String::new();
        out.push_str("bits 64\ndefault rel\nsection .text\nglobal _start\n_start:\n");
        out.push_str("    call f_main\n    mov rdi, rax\n    mov eax, 60\n    syscall\n\n");
        out.push_str(&text);
        out.push_str("section .data\n");
        out.push_str(&gdata);
        out.push_str(&self.data);
        Ok(out)
    }
}

// ───────────────────────── Драйвер ─────────────────────────

fn compile(src: &str) -> Res<String> {
    let mut prog = Program { funcs: Vec::new(), globals: Vec::new() };
    for (name, code) in [("prelude", PRELUDE), ("input", src)] {
        let toks = lex(code).map_err(|e| format!("{}: {}", name, e))?;
        let mut p = Parser { t: toks, p: 0 };
        p.program(&mut prog).map_err(|e| format!("{}: {}", name, e))?;
    }
    Gen::new().program(&prog)
}

fn run(cmd: &str, args: &[&str]) -> Res<()> {
    let st = Command::new(cmd)
        .args(args)
        .status()
        .map_err(|e| format!("cannot run '{}': {} (is it installed?)", cmd, e))?;
    if st.success() {
        Ok(())
    } else {
        Err(format!("'{}' failed", cmd))
    }
}

fn usage() -> ! {
    eprintln!("usage: kc <file.kc> [-o output] [-S] [--keep]\n  -S      only write <output>.asm (do not assemble/link)\n  --keep  keep intermediate .asm/.o files");
    process::exit(2);
}

fn main() {
    let args: Vec<String> = env::args().skip(1).collect();
    let mut input: Option<String> = None;
    let mut output = String::from("a.out");
    let mut asm_only = false;
    let mut keep = false;
    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "-o" => {
                i += 1;
                if i >= args.len() {
                    usage();
                }
                output = args[i].clone();
            }
            "-S" => asm_only = true,
            "--keep" => keep = true,
            "-h" | "--help" => usage(),
            s => input = Some(s.to_string()),
        }
        i += 1;
    }
    let input = input.unwrap_or_else(|| usage());
    let src = fs::read_to_string(&input).unwrap_or_else(|e| {
        eprintln!("kc: cannot read '{}': {}", input, e);
        process::exit(1);
    });
    let asm = compile(&src).unwrap_or_else(|e| {
        eprintln!("kc: error: {}", e);
        process::exit(1);
    });
    let asm_path = format!("{}.asm", output);
    let obj_path = format!("{}.o", output);
    if let Err(e) = fs::write(&asm_path, asm) {
        eprintln!("kc: cannot write '{}': {}", asm_path, e);
        process::exit(1);
    }
    if asm_only {
        return;
    }
    let r = run("nasm", &["-f", "elf64", &asm_path, "-o", &obj_path])
        .and_then(|_| run("ld", &[&obj_path, "-o", &output]));
    if !keep {
        let _ = fs::remove_file(&asm_path);
        let _ = fs::remove_file(&obj_path);
    }
    if let Err(e) = r {
        eprintln!("kc: error: {}", e);
        process::exit(1);
    }
}
