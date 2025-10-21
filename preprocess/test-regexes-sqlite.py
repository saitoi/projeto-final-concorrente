# /// script
# dependencies = ["numpy"]
# ///
import re, sqlite3, unicodedata

LETTER = r"[^\W\d_]"
APOS = rf"(?<={LETTER})'(?={LETTER})"
COMMA = rf"(?<={LETTER}),(?={LETTER})"
DASH = rf"(?<=[\w])-(?=[\w])"
DOT = rf"(?<=[\w])\.(?=[\w])"
SYMBOLS = r"[#:\"()\[\]{}~=_|;<>*\$`\\]"

def remove_accents(text: str | None) -> str | None:
    """Remove acentos do texto"""
    if text is None:
        return None
    nfd = unicodedata.normalize('NFD', text)
    return ''.join(char for char in nfd if unicodedata.category(char) != 'Mn')

def remove_non_ascii(text: str | None) -> str | None:
    """Remove caracteres não-ASCII"""
    if text is None:
        return None
    return text.encode('ascii', 'ignore').decode('ascii')

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

def remove_question_exclamation(text: str | None) -> str | None:
    """Remove ?, ! e / do texto"""
    if text is None:
        return None
    t = text.replace("?", " ").replace("!", " ").replace("/", " ")
    t = re.sub(r"\s+", " ", t).strip()
    return t

def remove_apostrophe_s(text: str | None) -> str | None:
    """Remove apóstrofo + s (possessivos)"""
    if text is None:
        return None
    t = text.replace("'s ", " ")
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

tests_accents = [
    "café com açúcar",
    "São Paulo é ótimo",
    "naïve résumé",
    "Ñoño señor",
    "no accents here",
]

tests_non_ascii = [
    "hello 世界",
    "test ♥ emoji 🚀",
    "café → coffee",
    "normal ascii text",
]

tests_question_exclamation = [
    "What? Really!",
    "path/to/file",
    "question? answer!",
    "multiple??? exclamations!!!",
    "no special chars",
]

tests_apostrophe_s = [
    "John's book",
    "the cat's tail",
    "it's working",
    "multiple 's in 's text",
    "no possessive",
]

# ------------------ EXECUÇÃO ------------------

if __name__ == "__main__":
    con = sqlite3.connect(":memory:")
    con.create_function("remove_accents", 1, remove_accents)
    con.create_function("remove_non_ascii", 1, remove_non_ascii)
    con.create_function("unescape_quotes", 1, unescape_quotes)
    con.create_function("clean_quotes", 1, clean_quotes)
    con.create_function("clean_commas", 1, clean_commas)
    con.create_function("clean_dashes", 1, clean_dashes)
    con.create_function("clean_dots", 1, clean_dots)
    con.create_function("clean_symbols", 1, clean_symbols)
    con.create_function("remove_question_exclamation", 1, remove_question_exclamation)
    con.create_function("remove_apostrophe_s", 1, remove_apostrophe_s)

    cur = con.cursor()
    cur.execute("create table test_accents(text)")
    cur.execute("create table test_non_ascii(text)")
    cur.execute("create table test_unescape(text)")
    cur.execute("create table test_quotes(text)")
    cur.execute("create table test_commas(text)")
    cur.execute("create table test_dashes(text)")
    cur.execute("create table test_dots(text)")
    cur.execute("create table test_symbols(text)")
    cur.execute("create table test_question_exclamation(text)")
    cur.execute("create table test_apostrophe_s(text)")

    cur.executemany("insert into test_accents values (?)", [(s,) for s in tests_accents])
    cur.executemany("insert into test_non_ascii values (?)", [(s,) for s in tests_non_ascii])
    cur.executemany("insert into test_unescape values (?)", [(s,) for s in tests_unescape])
    cur.executemany("insert into test_quotes values (?)", [(s,) for s in tests_apost])
    cur.executemany("insert into test_commas values (?)", [(s,) for s in tests_comma])
    cur.executemany("insert into test_dashes values (?)", [(s,) for s in tests_dash])
    cur.executemany("insert into test_dots values (?)", [(s,) for s in tests_dots])
    cur.executemany("insert into test_symbols values (?)", [(s,) for s in tests_symbols])
    cur.executemany("insert into test_question_exclamation values (?)", [(s,) for s in tests_question_exclamation])
    cur.executemany("insert into test_apostrophe_s values (?)", [(s,) for s in tests_apostrophe_s])

    print("=== remove_accents ===")
    for raw, cleaned in cur.execute("select text, remove_accents(text) from test_accents"):
        print(f"{raw!r} -> {cleaned!r}")

    print("\n=== remove_non_ascii ===")
    for raw, cleaned in cur.execute("select text, remove_non_ascii(text) from test_non_ascii"):
        print(f"{raw!r} -> {cleaned!r}")

    print("\n=== unescape_quotes ===")
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

    print("\n=== remove_question_exclamation ===")
    for raw, cleaned in cur.execute("select text, remove_question_exclamation(text) from test_question_exclamation"):
        print(f"{raw!r} -> {cleaned!r}")

    print("\n=== remove_apostrophe_s ===")
    for raw, cleaned in cur.execute("select text, remove_apostrophe_s(text) from test_apostrophe_s"):
        print(f"{raw!r} -> {cleaned!r}")

    con.close()
