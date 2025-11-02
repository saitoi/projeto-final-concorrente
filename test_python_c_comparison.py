#!/usr/bin/env python3

"""
Script de teste de corretude - compara saída do app C com tf_idf.py
"""

import subprocess
import sqlite3
import re
import sys
import argparse

DB_FILE = "data/test.db"

def get_queries(table_ids: list[int] | None = None) -> list[tuple[str, int, str]]:
    conn = sqlite3.connect(DB_FILE)
    cursor = conn.cursor()

    if table_ids is not None:
        placeholders = ','.join('?' * len(table_ids))
        query = f'SELECT table_id, query_id, query FROM queries WHERE table_id IN ({placeholders}) ORDER BY table_id, query_id'
        _ = cursor.execute(query, table_ids)
    else:
        _ = cursor.execute('SELECT table_id, query_id, query FROM queries ORDER BY table_id, query_id')

    queries = cursor.fetchall()
    conn.close()
    return queries

def parse_output(output: str) -> dict[int, float]:
    similarities: dict[int, float] = {}
    # Procura linhas no formato: [<doc_id>] <similarity> <doc_txt>...
    pattern = r'\[(\d+)\]\s+([\d.]+)\s+'
    for match in re.finditer(pattern, output):
        doc_id = int(match.group(1))
        similarity = float(match.group(2))
        similarities[doc_id] = similarity
    return similarities

def run_app_c(table: str, query: str, k: int = 10, entries: int | None = None) -> str:
    cmd = ['./app', '--table', table, '--query_user', query, '--k', str(k), "--db", DB_FILE]
    if entries:
        cmd.extend(['--entries', str(entries)])

    result = subprocess.run(cmd, capture_output=True, text=True)
    return result.stdout

def run_tf_idf_py(table: str, query: str, k: int = 10) -> str:
    cmd = ['uv', 'run', 'tf_idf.py', '-d', DB_FILE, '-t', table, '-q', query, '-k', str(k)]
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    return result.stdout

def compare_results(c_sims: dict[int, float], py_sims: dict[int, float], k: int = 10, tolerance: float = 1e-3) -> tuple[bool, str]:
    """Compara resultados com tolerância para erros de ponto flutuante."""

    if not c_sims and not py_sims:
        return True, "Ambos retornaram resultados vazios"

    if not c_sims:
        return False, f"App C não retornou resultados, Python retornou {len(py_sims)} docs"

    if not py_sims:
        return False, f"Python não retornou resultados, App C retornou {len(c_sims)} docs"

    # Comparar top-k documentos
    c_sorted = sorted(c_sims.items(), key=lambda x: x[1], reverse=True)[:k]
    py_sorted = sorted(py_sims.items(), key=lambda x: x[1], reverse=True)[:k]

    errors: list[str] = []

    for i, ((c_id, c_sim), (py_id, py_sim)) in enumerate(zip(c_sorted, py_sorted)):
        diff = abs(c_sim - py_sim)
        if c_id != py_id:
            errors.append(f"  Pos {i}: Ordem diferente - C=[{c_id}], Python=[{py_id}]")
        elif diff > tolerance:
            errors.append(f"  Doc [{c_id}]: Diferença - C={c_sim:.6f}, Python={py_sim:.6f}, diff={diff:.10f} (tol={tolerance})")

    if errors:
        return False, "\n".join(errors)

    return True, f"Top-{len(c_sorted)} documentos correspondem com tolerância {tolerance}"

def main():
    parser = argparse.ArgumentParser(
        description='Compara saída do app C com tf_idf.py',
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    _ = parser.add_argument('-k', '--top-k', type=int, default=10, help='Número de documentos top-k a comparar (default: 10)')
    _ = parser.add_argument('-t', '--tables', type=int, nargs='+', help='Lista de IDs de tabelas para testar (ex: 0 1 2). Se não especificado, testa todas.')
    _ = parser.add_argument('-tol', '--tolerance', type=float, default=1e-3, help='Tolerância para comparação de similaridades (default: 1e-3)')

    args = parser.parse_args()

    print("-" * 57)
    print("CORRETUDE: Chad Multithreaded C TF-IDF vs Beta tf_idf.py")
    print("-" * 57)
    if args.tables:
        print(f"Tabelas selecionadas: {args.tables}")
    else:
        print("Testando todas as tabelas")
    print(f"Top-K: {args.top_k}")
    print()

    # Verificando se binário 'app' existe ?
    try:
        _ = subprocess.run(['./app', '--help'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except FileNotFoundError:
        print("\033[31mERRO: ./app não encontrado. Execute 'make' primeiro.\033[0m")
        sys.exit(1)

    queries = get_queries(args.tables)

    if not queries:
        print("\033[33mNenhuma query encontrada no banco de dados.\033[0m")
        sys.exit(0)

    total_tests = 0
    passed_tests = 0
    failed_tests = 0

    for table, query_id, query in queries:
        table_name = f"test_tbl_{table}"

        # Verificar se tabela existe
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        _ = cursor.execute(f"SELECT name FROM sqlite_master WHERE type='table' AND name='{table_name}'")
        if not cursor.fetchone():
            conn.close()
            print(f"\033[33mTabela {table_name} não existe. Ignorando...\033[0m")
            continue
        conn.close()

        total_tests += 1

        print(f"[{total_tests}] Testando: {table_name} - Query ID {query_id}")
        print(f"    Consulta: \"{query}\"")

        # Executar ambos os programas
        try:
            c_output = run_app_c(table_name, query, k=args.top_k)
            py_output = run_tf_idf_py(table_name, query, k=args.top_k)

            # Parse das saídas
            c_sims = parse_output(c_output)
            py_sims = parse_output(py_output)

            # Comparar
            is_equal, message = compare_results(c_sims, py_sims, k=args.top_k, tolerance=args.tolerance)
            if is_equal:
                print(f"\033[32m    PASSOU: {message}\033[0m")
                passed_tests += 1
            else:
                print("\033[31m    FALHOU:\033[0m")
                for line in message.split('\n'):
                    print(f"      {line}")
                failed_tests += 1

        except Exception as e:
            print(f"\033[31m    ERRO: {e}\033[0m")
            failed_tests += 1
        print()

    # Sumário
    print(f"\nTotal de testes: {total_tests}")
    print(f"\033[32mPassou: {passed_tests}\033[0m" if passed_tests > 0 else f"\033[31mPassou: {passed_tests}\033[0m")
    print(f"\033[32mFalhou: {failed_tests}\033[0m" if failed_tests == 0 else f"\033[31mFalhou: {failed_tests}\033[0m")
    print()

    if failed_tests == 0 and total_tests > 0:
        print("\033[32mTODOS OS TESTES PASSARAM!\033[0m")
        sys.exit(0)
    else:
        print("\033[31mALGUNS TESTES FALHARAM\033[0m")
        sys.exit(1)

if __name__ == "__main__":
    main()
