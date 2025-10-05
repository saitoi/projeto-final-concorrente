# /// script
# dependencies = ["duckdb", "numpy"]
# ///
import re, duckdb

LETTER = r"[^\W\d_]"
APOS = rf"(?<={LETTER})'(?={LETTER})"

def clean_quotes(text: str | None) -> str | None:
    if text is None: return None
    ph = "\uFFFF"
    t = re.sub(APOS, ph, text)
    t = re.sub(r"'+", " ", t)
    t = re.sub(r"\s+", " ", t).strip()
    return t.replace(ph, "'")

tests = [
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

if __name__ == "__main__":
    con = duckdb.connect(":memory:")
    con.create_function("clean_quotes", clean_quotes)
    con.execute("create table t(raw varchar)")
    con.executemany("insert into t values (?)", [(s,) for s in tests])
    rows = con.execute("select raw, clean_quotes(raw) as cleaned from t").fetchall()
    w = max(len(r[0]) for r in rows)
    print(f"{'RAW'.ljust(w)} | CLEANED")
    print("-"*(w+10))
    for raw, cleaned in rows:
        print(f"{raw.ljust(w)} | {cleaned}")
