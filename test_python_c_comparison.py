#!/usr/bin/env python3

import subprocess
import sqlite3
import re
import sys
import argparse
import csv
from pathlib import Path

def get_csv_filename(db_path: str) -> str:
    db_name = Path(db_path).stem
    return f"correctness_results_{db_name}.csv"

def get_queries(db_path: str, table_ids: list[int] | None = None) -> list[tuple[str, int, str]]:
    conn = sqlite3.connect(db_path)
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
    pattern = r'\[(\d+)\]\s+(?:\x1b\[[0-9;]+m)*([\d.]+)(?:\x1b\[[0-9;]+m)*\s+'
    for match in re.finditer(pattern, output):
        doc_id = int(match.group(1))
        similarity = float(match.group(2))
        if similarity > 0.0:
            similarities[doc_id] = similarity
    return similarities

def run_app_c(table: str, query: str, k: int = 10, entries: int | None = None, db: str = './data/test.db', *, sequential: bool = False, nthreads: int = 4) -> str:
    cmd = ['./app', '--table', table, '--query_user', query, '--k', str(k), "--db", db]
    if entries is not None:
        cmd.extend(['--entries', str(entries)])
    if sequential:
        cmd[0] = './app_seq'
    else:
        cmd.extend(['--nthreads', str(nthreads)])

    result = subprocess.run(cmd, capture_output=True, text=True)
    return result.stdout

def run_tf_idf_py(table: str, query: str, k: int = 10, *, db: str = './data/test.db', entries: int | None = None) -> str:
    cmd = ['uv', 'run', 'tf_idf.py', '-d', db, '-t', table, '-q', query, '-k', str(k)]
    if entries is not None:
        cmd.extend(['--entries', str(entries)])
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    return result.stdout

def compare_results(c_sims: dict[int, float], py_sims: dict[int, float], k: int = 10, tolerance: float = 1e-3) -> tuple[bool, str, float]:
    """Compara resultados com tolerância para erros de ponto flutuante.

    Returns:
        tuple: (passou, mensagem, max_diff)
    """

    if not c_sims and not py_sims:
        return True, "Ambos retornaram resultados vazios", 0.0

    if not c_sims:
        return False, f"App C não retornou resultados, Python retornou {len(py_sims)} docs", -1.0

    if not py_sims:
        return False, f"Python não retornou resultados, App C retornou {len(c_sims)} docs", -1.0

    # Comparar top-k documentos
    c_sorted = sorted(c_sims.items(), key=lambda x: (x[1], x[0]), reverse=True)[:k]
    py_sorted = sorted(py_sims.items(), key=lambda x: (x[1], x[0]), reverse=True)[:k]

    errors: list[str] = []
    max_diff = 0.0

    for i, ((c_id, c_sim), (py_id, py_sim)) in enumerate(zip(c_sorted, py_sorted)):
        diff = abs(c_sim - py_sim)
        max_diff = max(max_diff, diff)

        if c_id != py_id:
            errors.append(f"  Pos {i}: Ordem diferente - C=[{c_id}], Python=[{py_id}]")
        elif diff > tolerance:
            errors.append(f"  Doc [{c_id}]: Diferença - C={c_sim:.6f}, Python={py_sim:.6f}, diff={diff:.10f} (tol={tolerance})")

    if errors:
        return False, "\n".join(errors), max_diff

    return True, f"Top-{len(c_sorted)} documentos correspondem com tolerância {tolerance}", max_diff

def main():
    parser = argparse.ArgumentParser(
        description='Compara saída do app C com tf_idf.py',
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    _ = parser.add_argument('-k', '--top-k', type=int, default=10, help='Número de documentos top-k a comparar (default: 10)')
    _ = parser.add_argument('-t', '--tables', type=int, nargs='+', help='Lista de IDs de tabelas para testar (ex: 0 1 2). Se não especificado, testa todas.')
    _ = parser.add_argument('-tol', '--tolerance', type=float, default=1e-3, help='Tolerância para comparação de similaridades (default: 1e-3)')
    _ = parser.add_argument('-s', '--sequential', action='store_true', default=False, help='Comparar output sequencial (default: False)')
    _ = parser.add_argument('--nthreads', type=int, nargs='+', default=[1, 2, 4, 8, 16], help='Lista de números de threads para testar (default: 1 2 4 8 16)')
    _ = parser.add_argument('--db', type=str, default='./data/test.db', help='Filepath do banco escolhido para corretude.')
    _ = parser.add_argument('-e', '--entries', type=int, help='Limita a quantidade de documentos carregados pelo app e baseline.')
    _ = parser.add_argument('--tablename', type=str, help='Nome da tabela a ser usado em vez de test_tbl_<id>.')

    args = parser.parse_args()

    # Gerar nome do CSV
    print(args.db)
    csv_filename = get_csv_filename(args.db)

    adversario = "Beta Sequential C" if args.sequential else "Beta tf_idf.py"
    print("-" * 70)
    print(f"CORRETUDE: C Multithreaded vs {adversario}")
    print("-" * 70)
    if args.tables:
        print(f"Tabelas selecionadas: {args.tables}")
    else:
        print("Testando todas as tabelas")
    print(f"Top-K: {args.top_k}")
    if not args.sequential:
        print(f"Threads a testar: {args.nthreads}")
    if args.entries is not None:
        print(f"Entries: {args.entries}")
    if args.tablename:
        print(f"Tabela fixa: {args.tablename}")
    print(f"Resultados serão salvos em: {csv_filename}")
    print()

    # Verificando se binário 'app' existe ?
    try:
        _ = subprocess.run(['./app', '--help'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except FileNotFoundError:
        print("\033[31mERRO: ./app não encontrado. Execute 'make' primeiro.\033[0m")
        sys.exit(1)

    queries = get_queries(db_path=args.db, table_ids=args.tables)

    if not queries:
        print("\033[33mNenhuma query encontrada no banco de dados.\033[0m")
        sys.exit(0)

    total_tests = 0
    passed_tests = 0
    failed_tests = 0
    results = []  # Lista para armazenar resultados para CSV

    for table, query_id, query in queries:
        table_name = args.tablename if args.tablename else f"test_tbl_{table}"

        # Verificar se tabela existe
        conn = sqlite3.connect(args.db)
        cursor = conn.cursor()
        _ = cursor.execute(f"SELECT name FROM sqlite_master WHERE type='table' AND name='{table_name}'")
        if not cursor.fetchone():
            conn.close()
            print(f"\033[33mTabela {table_name} não existe. Ignorando...\033[0m")
            continue
        conn.close()

        print(f"Testando: {table_name} - Query ID {query_id}")
        print(f"  Consulta: \"{query}\"")

        # Executar baseline (Python ou Sequential C)
        try:
            if args.sequential:
                baseline_output = run_app_c(table_name, query, k=args.top_k, sequential=True, db=args.db, entries=args.entries)
            else:
                baseline_output = run_tf_idf_py(table_name, query, k=args.top_k, db=args.db, entries=args.entries)

            baseline_sims = parse_output(baseline_output)

            # Testar C com diferentes números de threads
            for nthreads in args.nthreads:
                total_tests += 1

                c_output = run_app_c(table_name, query, k=args.top_k, nthreads=nthreads, db=args.db, entries=args.entries)
                c_sims = parse_output(c_output)

                # Comparar
                is_equal, message, max_diff = compare_results(c_sims, baseline_sims, k=args.top_k, tolerance=args.tolerance)

                # Armazenar resultado
                results.append({
                    'table': table_name,
                    'query_id': query_id,
                    'query': query,
                    'nthreads': nthreads,
                    'baseline': 'sequential' if args.sequential else 'python',
                    'passed': 'PASS' if is_equal else 'FAIL',
                    'max_diff': f"{max_diff:.10f}" if max_diff >= 0 else "N/A",
                    'tolerance': args.tolerance,
                    'top_k': args.top_k,
                    'error_details': message if not is_equal else ""
                })

                if is_equal:
                    print(f"  [t={nthreads:2}] \033[32mPASSOU\033[0m: {message}")
                    passed_tests += 1
                else:
                    print(f"  [t={nthreads:2}] \033[31mFALHOU\033[0m:")
                    for line in message.split('\n'):
                        print(f"         {line}")
                    failed_tests += 1

        except Exception as e:
            print(f"  \033[31mERRO: {e}\033[0m")
            # Adicionar erro ao CSV também
            for nthreads in args.nthreads:
                results.append({
                    'table': table_name,
                    'query_id': query_id,
                    'query': query,
                    'nthreads': nthreads,
                    'baseline': 'sequential' if args.sequential else 'python',
                    'passed': 'ERROR',
                    'max_diff': 'N/A',
                    'tolerance': args.tolerance,
                    'top_k': args.top_k,
                    'error_details': str(e)
                })
            failed_tests += 1

        print()

    # Salvar CSV
    print(f"\nSalvando resultados em {csv_filename}...")
    csv_fieldnames = ['table', 'query_id', 'query', 'nthreads', 'baseline', 'passed', 'max_diff', 'tolerance', 'top_k', 'error_details']

    with open(csv_filename, 'w', newline='') as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=csv_fieldnames)
        writer.writeheader()
        for result in results:
            writer.writerow(result)

    print(f"\033[32mOK:\033[0m CSV salvo com {len(results)} testes")

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
