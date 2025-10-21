#!/usr/bin/env python3
"""
Pipeline de preprocessamento para SQLite
Uso: python pipeline-sqlite.py database.db tablename
"""
import re, sqlite3, unicodedata, sys

LETTER = r"[^\W\d_]"
APOS = rf"(?<={LETTER})'(?={LETTER})"
DASH = rf"(?<=[\w])-(?=[\w])"
DOT = rf"(?<=[\w])\.(?=[\w])"
SYMBOLS = r"[#:\"()\[\]{}~=_|;<>*\$`\\]"

def remove_accents(text):
    if text is None: return None
    nfd = unicodedata.normalize('NFD', text)
    return ''.join(char for char in nfd if unicodedata.category(char) != 'Mn')

def remove_non_ascii(text):
    if text is None: return None
    return text.encode('ascii', 'ignore').decode('ascii')

def unescape_quotes(text):
    if text is None: return None
    return re.sub(r"\\'", "'", text)

def clean_quotes(text):
    if text is None: return None
    ph = "\uffff"
    t = re.sub(APOS, ph, text)
    t = re.sub(r"'+", " ", t)
    t = re.sub(r"\s+", " ", t).strip()
    return t.replace(ph, "'")

def clean_commas(text):
    if text is None: return None
    ph = "\uffff"
    num_comma = r"(?<=\d),(?=\d)"
    t = re.sub(num_comma, ph, text)
    t = re.sub(r",+", " ", t)
    t = re.sub(r"\s+", " ", t).strip()
    return t.replace(ph, ",")

def clean_dashes(text):
    if text is None: return None
    ph = "\uffff"
    t = re.sub(DASH, ph, text)
    t = re.sub(r"-+", " ", t)
    t = re.sub(r"\s+", " ", t).strip()
    return t.replace(ph, "-")

def clean_dots(text):
    if text is None: return None
    ph = "\uffff"
    t = re.sub(DOT, ph, text)
    t = re.sub(r"\.+", " ", t)
    t = re.sub(r"\s+", " ", t).strip()
    return t.replace(ph, ".")

def clean_symbols(text):
    if text is None: return None
    t = re.sub(SYMBOLS, " ", text)
    t = re.sub(r"\s+", " ", t).strip()
    return t

def remove_question_exclamation(text):
    if text is None: return None
    t = text.replace("?", " ").replace("!", " ").replace("/", " ")
    t = re.sub(r"\s+", " ", t).strip()
    return t

def remove_apostrophe_s(text):
    if text is None: return None
    t = text.replace("'s ", " ")
    t = re.sub(r"\s+", " ", t).strip()
    return t

def main():
    if len(sys.argv) != 3:
        print(f"Uso: {sys.argv[0]} <database.db> <tablename>", file=sys.stderr)
        sys.exit(1)

    db_file = sys.argv[1]
    table = sys.argv[2]

    print(f"Conectando ao banco: {db_file}")
    print(f"Processando tabela: {table}")

    con = sqlite3.connect(db_file)

    # Registrar todas as funções como UDFs
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

    # Verificar se a tabela existe
    cur = con.cursor()
    cur.execute(f"SELECT name FROM sqlite_master WHERE type='table' AND name=?", (table,))
    if not cur.fetchone():
        print(f"Erro: Tabela '{table}' não encontrada", file=sys.stderr)
        con.close()
        sys.exit(1)

    # Contar registros
    cur.execute(f"SELECT COUNT(*) FROM {table}")
    total = cur.fetchone()[0]
    print(f"Total de registros: {total}")

    # Pipeline eficiente: aplica todas as etapas em uma única query UPDATE
    print("Aplicando pipeline de preprocessamento...")

    update_query = f"""
    UPDATE {table}
    SET article_text = remove_apostrophe_s(
                         remove_question_exclamation(
                           clean_symbols(
                             clean_dots(
                               clean_dashes(
                                 clean_commas(
                                   clean_quotes(
                                     unescape_quotes(
                                       remove_non_ascii(
                                         remove_accents(
                                           lower(article_text)
                                         )
                                       )
                                     )
                                   )
                                 )
                               )
                             )
                           )
                         )
                       )
    """

    cur.execute(update_query)
    con.commit()

    print(f"Processamento concluído! {cur.rowcount} registros atualizados.")
    con.close()

if __name__ == "__main__":
    main()
