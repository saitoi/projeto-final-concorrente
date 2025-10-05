# /// script
# dependencies = ["duckdb", "numpy"]
# ///
import re, duckdb

LETTER = r"[^\W\d_]"
APOS = rf"(?<={LETTER})'(?={LETTER})"


def clean_quotes(text: str | None) -> str | None:
    if text is None:
        return None
    ph = "\uffff"
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
    con = duckdb.connect("../wiki-small.db")
    con.create_function("clean_quotes", clean_quotes)
    con.execute("""
        update sample_articles
        set article_text = clean_quotes(
            regexp_replace(
                strip_accents(
                    lower(article_text)
                ), '[^\x20-\x7e]+', '', 'g'));
                """)
    rows = con.execute("from sample_articles limit 5;").fetchall()
    print(rows)
    con.close()
