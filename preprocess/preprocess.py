#!/usr/bin/env python3
"""
Script de preprocessamento de texto
Uso:
    python preprocess.py "texto para processar"
    python preprocess.py -f arquivo.txt
    python preprocess.py -d database.db [-t tabela]
"""
import re
import sys
import argparse
import unicodedata
import sqlite3

# Padrões regex do arquivo original
LETTER = r"[^\W\d_]"
APOS = rf"(?<={LETTER})'(?={LETTER})"
DASH = rf"(?<=[\w])-(?=[\w])"
DOT = rf"(?<=[\w])\.(?=[\w])"
SYMBOLS = r"[#:\"()\[\]{}~=_|;<>*\$`\\]"


def remove_accents(text: str) -> str:
    """Remove acentos do texto"""
    nfd = unicodedata.normalize('NFD', text)
    return ''.join(char for char in nfd if unicodedata.category(char) != 'Mn')


def remove_non_ascii(text: str) -> str:
    """Remove caracteres não-ASCII"""
    return text.encode('ascii', 'ignore').decode('ascii')


def unescape_quotes(text: str) -> str:
    """Remove escape de aspas simples"""
    return re.sub(r"\\'", "'", text)


def clean_quotes(text: str) -> str:
    """Limpa aspas simples, preservando apóstrofos válidos"""
    ph = "\uffff"
    t = re.sub(APOS, ph, text)
    t = re.sub(r"'+", " ", t)
    t = re.sub(r"\s+", " ", t).strip()
    return t.replace(ph, "'")


def clean_commas(text: str) -> str:
    """Limpa vírgulas, preservando separadores numéricos"""
    ph = "\uffff"
    num_comma = r"(?<=\d),(?=\d)"
    t = re.sub(num_comma, ph, text)
    t = re.sub(r",+", " ", t)
    t = re.sub(r"\s+", " ", t).strip()
    return t.replace(ph, ",")


def clean_dashes(text: str) -> str:
    """Limpa traços, preservando hífens entre palavras"""
    ph = "\uffff"
    t = re.sub(DASH, ph, text)
    t = re.sub(r"-+", " ", t)
    t = re.sub(r"\s+", " ", t).strip()
    return t.replace(ph, "-")


def clean_dots(text: str) -> str:
    """Limpa pontos, preservando pontos decimais"""
    ph = "\uffff"
    t = re.sub(DOT, ph, text)
    t = re.sub(r"\.+", " ", t)
    t = re.sub(r"\s+", " ", t).strip()
    return t.replace(ph, ".")


def clean_symbols(text: str) -> str:
    """Remove símbolos especiais"""
    t = re.sub(SYMBOLS, " ", text)
    t = re.sub(r"\s+", " ", t).strip()
    return t


def remove_question_exclamation(text: str) -> str:
    """Remove ?, ! e / do texto"""
    t = text.replace("?", " ").replace("!", " ").replace("/", " ")
    t = re.sub(r"\s+", " ", t).strip()
    return t


def remove_apostrophe_s(text: str) -> str:
    """Remove apóstrofo + s (possessivos)"""
    t = text.replace("'s ", " ")
    t = re.sub(r"\s+", " ", t).strip()
    return t


def preprocess(text: str) -> str:
    """
    Aplica todas as etapas de preprocessamento na ordem:
    1. Lowercase
    2. Remoção de acentos
    3. Remoção de caracteres não-ASCII
    4. Unescape quotes
    5. Limpeza de aspas
    6. Limpeza de vírgulas
    7. Limpeza de traços
    8. Limpeza de pontos
    9. Limpeza de símbolos
    10. Remoção de ? e !
    11. Remoção de apóstrofo + s (possessivos)
    """
    text = text.lower()

    text = remove_accents(text)

    text = remove_non_ascii(text)

    text = unescape_quotes(text)

    text = clean_quotes(text)

    text = clean_commas(text)

    text = clean_dashes(text)

    text = clean_dots(text)

    text = clean_symbols(text)

    text = remove_question_exclamation(text)

    text = remove_apostrophe_s(text)

    return text


def process_database(db_path: str, table: str = 'test_tbl_1'):
    """
    Processa os textos de um banco de dados SQLite
    Atualiza a coluna article_text com os textos preprocessados
    """
    try:
        conn = sqlite3.connect(db_path)
        cursor = conn.cursor()

        # Verificar se a tabela existe
        cursor.execute(f"SELECT name FROM sqlite_master WHERE type='table' AND name='{table}'")
        if not cursor.fetchone():
            print(f"Erro: Tabela '{table}' não encontrada no banco de dados", file=sys.stderr)
            conn.close()
            sys.exit(1)

        # Processar cada linha
        cursor.execute(f"SELECT article_id, article_text FROM {table}")
        rows = cursor.fetchall()

        print(f"Processando {len(rows)} registros da tabela '{table}'...", file=sys.stderr)

        for art_id, article_text in rows:
            if article_text:
                processed_text = preprocess(article_text)
                cursor.execute(f"UPDATE {table} SET article_text = ? WHERE article_id = ?",
                             (processed_text, art_id))

        conn.commit()
        conn.close()

        print(f"Processamento concluído! {len(rows)} registros atualizados.", file=sys.stderr)

    except sqlite3.Error as e:
        print(f"Erro ao processar banco de dados: {e}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"Erro inesperado: {e}", file=sys.stderr)
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(
        description='Preprocessa texto aplicando normalização e limpeza'
    )
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('text', nargs='?', help='Texto para preprocessar')
    group.add_argument('-f', '--file', help='Arquivo de texto para preprocessar')
    group.add_argument('-d', '--database', help='Arquivo SQLite (.db) para preprocessar')

    parser.add_argument('-t', '--table', default='sample_articles',
                       help='Nome da tabela no banco de dados (padrão: sample_articles)')

    args = parser.parse_args()

    # Processar banco de dados SQLite
    if args.database:
        process_database(args.database, args.table)
        return

    # Ler texto de arquivo ou argumento
    if args.file:
        try:
            with open(args.file, 'r', encoding='utf-8') as f:
                text = f.read()
        except FileNotFoundError:
            print(f"Erro: Arquivo '{args.file}' não encontrado", file=sys.stderr)
            sys.exit(1)
        except Exception as e:
            print(f"Erro ao ler arquivo: {e}", file=sys.stderr)
            sys.exit(1)
    else:
        text = args.text

    # Preprocessar e imprimir resultado
    result = preprocess(text)
    print(result)


if __name__ == "__main__":
    main()
