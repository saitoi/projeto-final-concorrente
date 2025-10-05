# /// script
# dependencies = ["numpy"]
# ///
import re, sqlite3

LETTER = r"[^\W\d_]"
APOS = rf"(?<={LETTER})'(?={LETTER})"
COMMA = rf"(?<={LETTER}),(?={LETTER})"
DASH = rf"(?<=[\w])-(?=[\w])"
DOT = rf"(?<=[\w])\.(?=[\w])"
SYMBOLS = r"[#:\"()\[\]{}~=_|;<>*\$`\\]"

def unescape_quotes(text: str | None) -> str | None:
    if text is None:
        return None
    return re.sub(r"\\'", "'", text)

def clean_quotes(text: str | None) -> str | None:
    if text is None:
        return None
    ph = "\uffff"
    t = re.sub(APOS, ph, text)
    t = re.sub(r"'+", " ", t)
    t = re.sub(r"\s+", " ", t).strip()
    return t.replace(ph, "'")

def clean_commas(text: str | None) -> str | None:
    if text is None:
        return None
    ph = "\uffff"
    num_comma = r"(?<=\d),(?=\d)"
    t = re.sub(num_comma, ph, text)
    t = re.sub(r",+", " ", t)
    t = re.sub(r"\s+", " ", t).strip()
    return t.replace(ph, ",")

def clean_dashes(text: str | None) -> str | None:
    if text is None:
        return None
    ph = "\uffff"
    t = re.sub(DASH, ph, text)
    t = re.sub(r"-+", " ", t)
    t = re.sub(r"\s+", " ", t).strip()
    return t.replace(ph, "-")

def clean_dots(text: str | None) -> str | None:
    if text is None:
        return None
    ph = "\uffff"
    t = re.sub(DOT, ph, text)
    t = re.sub(r"\.+", " ", t)
    t = re.sub(r"\s+", " ", t).strip()
    return t.replace(ph, ".")

def clean_symbols(text: str | None) -> str | None:
    if text is None:
        return None
    t = re.sub(SYMBOLS, " ", text)
    t = re.sub(r"\s+", " ", t).strip()
    return t

# ------------------ TESTES ------------------

tests_unescape = [
    r"escapado \'teste\' simples",
    r"json-like: {\'key\': \'value\'}",
    r"nenhum escape aqui",
    r"múltiplos: \'a\' \'b\' \'c\'",
]

tests_apost = [
    "O'Neill venceu",
    "don't stop",
    "'inicio sem letra",
    "final sem letra'",
    "rock 'n' roll",
    "muitas aspas ''''''' no meio",
    "linha\n'quebra",
    "''duas no começo",
    "meio' sem letra depois",
    "antes sem letra ' depois",
    "normal sem aspas",
]

tests_comma = [
    "rock,roll",
    "apple, banana, orange",
    "1,000",
    "hello,,world",
    "no,comma",
    ",leading comma",
    "trailing comma,",
    "multiple , , commas , in , a , row",
    "text\nwith,linebreak,comma",
    "normal text without commas",
]

tests_dash = [
    "rock-n-roll",
    "A-12",
    "text - with - spaces",
    "--double--dash--",
    "-leading dash",
    "trailing dash-",
    "multiple - - dashes - here",
    "linha\n-com-dash",
    "sem traço algum",
]

tests_dots = [
    "3.14 é pi",
    "versão 1.0.2 estável",
    "U.S.A.",
    "email.com",
    "final de frase.",
    "muitos....pontos...",
    ".começa com ponto",
    "termina com ponto.",
    "texto . com . pontos . soltos",
    "sem pontos",
]

tests_symbols = [
    "#hashtag test",
    "colon: separated",
    "quote\"double\"",
    "parentheses() and brackets[] and braces{}",
    "tilde~equals=underscore_",
    "|pipe;semicolon;",
    "<>stars* dollars$ backtick` backslash\\",
    "mix of all: a#b:c|d;e<f>g*h$i`j\\",
    "no symbols here",
]

# ------------------ EXECUÇÃO ------------------

if __name__ == "__main__":
    con = sqlite3.connect(":memory:")
    con.create_function("unescape_quotes", 1, unescape_quotes)
    con.create_function("clean_quotes", 1, clean_quotes)
    con.create_function("clean_commas", 1, clean_commas)
    con.create_function("clean_dashes", 1, clean_dashes)
    con.create_function("clean_dots", 1, clean_dots)
    con.create_function("clean_symbols", 1, clean_symbols)

    cur = con.cursor()
    cur.execute("create table test_unescape(text)")
    cur.execute("create table test_quotes(text)")
    cur.execute("create table test_commas(text)")
    cur.execute("create table test_dashes(text)")
    cur.execute("create table test_dots(text)")
    cur.execute("create table test_symbols(text)")

    cur.executemany("insert into test_unescape values (?)", [(s,) for s in tests_unescape])
    cur.executemany("insert into test_quotes values (?)", [(s,) for s in tests_apost])
    cur.executemany("insert into test_commas values (?)", [(s,) for s in tests_comma])
    cur.executemany("insert into test_dashes values (?)", [(s,) for s in tests_dash])
    cur.executemany("insert into test_dots values (?)", [(s,) for s in tests_dots])
    cur.executemany("insert into test_symbols values (?)", [(s,) for s in tests_symbols])

    print("=== unescape_quotes ===")
    for raw, cleaned in cur.execute("select text, unescape_quotes(text) from test_unescape"):
        print(f"{raw!r} -> {cleaned!r}")

    print("\n=== clean_quotes ===")
    for raw, cleaned in cur.execute("select text, clean_quotes(text) from test_quotes"):
        print(f"{raw!r} -> {cleaned!r}")

    print("\n=== clean_commas ===")
    for raw, cleaned in cur.execute("select text, clean_commas(text) from test_commas"):
        print(f"{raw!r} -> {cleaned!r}")

    print("\n=== clean_dashes ===")
    for raw, cleaned in cur.execute("select text, clean_dashes(text) from test_dashes"):
        print(f"{raw!r} -> {cleaned!r}")

    print("\n=== clean_dots ===")
    for raw, cleaned in cur.execute("select text, clean_dots(text) from test_dots"):
        print(f"{raw!r} -> {cleaned!r}")

    print("\n=== clean_symbols ===")
    for raw, cleaned in cur.execute("select text, clean_symbols(text) from test_symbols"):
        print(f"{raw!r} -> {cleaned!r}")

    con.close()
