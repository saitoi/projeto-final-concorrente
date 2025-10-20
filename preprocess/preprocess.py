#!/usr/bin/env python3
"""
Script de preprocessamento de texto
Uso:
    python preprocess.py "texto para processar"
    python preprocess.py -f arquivo.txt
"""
import re
import sys
import argparse
import unicodedata

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
    10. Remoção de apóstrofo + s (possessivos)
    """
    # Etapa 1: lowercase
    text = text.lower()

    # Etapa 2: remover acentos
    text = remove_accents(text)

    # Etapa 3: remover caracteres não-ASCII
    text = remove_non_ascii(text)

    # Etapa 4: unescape quotes
    text = unescape_quotes(text)

    # Etapa 5: limpar aspas
    text = clean_quotes(text)

    # Etapa 6: limpar vírgulas
    text = clean_commas(text)

    # Etapa 7: limpar traços
    text = clean_dashes(text)

    # Etapa 8: limpar pontos
    text = clean_dots(text)

    # Etapa 9: limpar símbolos
    text = clean_symbols(text)

    # Etapa 10: remover apóstrofo + s
    text = remove_apostrophe_s(text)

    return text


def main():
    parser = argparse.ArgumentParser(
        description='Preprocessa texto aplicando normalização e limpeza'
    )
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('text', nargs='?', help='Texto para preprocessar')
    group.add_argument('-f', '--file', help='Arquivo de texto para preprocessar')

    args = parser.parse_args()

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
