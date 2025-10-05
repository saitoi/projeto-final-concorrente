# /// script
# dependencies = ["duckdb", "numpy"]
# ///
import re, duckdb

LETTER = r"[^\W\d_]"
APOS = rf"(?<={LETTER})'(?={LETTER})"
COMMA = rf"(?<={LETTER}),(?={LETTER})"
DASH = rf"(?<=[\w])-(?=[\w])"
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

def clean_symbols(text: str | None) -> str | None:
    if text is None:
        return None
    # substitui todos os símbolos da lista por espaço e normaliza múltiplos espaços
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
    con = duckdb.connect(":memory:")
    con.create_function("unescape_quotes", unescape_quotes)
    con.create_function("clean_quotes", clean_quotes)
    con.create_function("clean_commas", clean_commas)
    con.create_function("clean_dashes", clean_dashes)
    con.create_function("clean_symbols", clean_symbols)

    con.execute("create table test_unescape(text string)")
    con.execute("create table test_quotes(text string)")
    con.execute("create table test_commas(text string)")
    con.execute("create table test_dashes(text string)")
    con.execute("create table test_symbols(text string)")

    con.executemany("insert into test_unescape values (?)", [(s,) for s in tests_unescape])
    con.executemany("insert into test_quotes values (?)", [(s,) for s in tests_apost])
    con.executemany("insert into test_commas values (?)", [(s,) for s in tests_comma])
    con.executemany("insert into test_dashes values (?)", [(s,) for s in tests_dash])
    con.executemany("insert into test_symbols values (?)", [(s,) for s in tests_symbols])

    print("=== unescape_quotes ===")
    rows = con.execute("select text, unescape_quotes(text) from test_unescape").fetchall()
    for raw, cleaned in rows:
        print(f"{raw!r} -> {cleaned!r}")

    print("\n=== clean_quotes ===")
    rows = con.execute("select text, clean_quotes(text) from test_quotes").fetchall()
    for raw, cleaned in rows:
        print(f"{raw!r} -> {cleaned!r}")

    print("\n=== clean_commas ===")
    rows = con.execute("select text, clean_commas(text) from test_commas").fetchall()
    for raw, cleaned in rows:
        print(f"{raw!r} -> {cleaned!r}")

    print("\n=== clean_dashes ===")
    rows = con.execute("select text, clean_dashes(text) from test_dashes").fetchall()
    for raw, cleaned in rows:
        print(f"{raw!r} -> {cleaned!r}")

    print("\n=== clean_symbols ===")
    rows = con.execute("select text, clean_symbols(text) from test_symbols").fetchall()
    for raw, cleaned in rows:
        print(f"{raw!r} -> {cleaned!r}")

    con.close()
