#!/usr/bin/env -S uv run --script
#
# /// script
# requires-python = ">=3.13"
# dependencies = [
#     "tqdm",
# ]
# ///


"""
Script de benchmark - Analisa tempo de execução do app C
Compara: sequencial vs paralelo (1, 2, 4, 6, 8, 16 threads)
"""

import subprocess
import sqlite3
import re
import csv
import sys
import argparse
import os
from pathlib import Path
from statistics import mean, stdev
from dataclasses import dataclass
from typing import Optional
from tqdm import tqdm

# Diretório para salvar logs
LOG_DIR = Path("benchmark_logs")

# Colunas do CSV
CSV_FIELDNAMES = [
    "data_size",
    "approach",
    "threads",
    "time_seconds",
    "speedup",
    "efficiency",
]


@dataclass
class BenchmarkConfig:
    data_size: str  # ex: "5kb", "10kb", etc
    approach: str  # "sequential" ou "threaded"
    threads: int  # número de threads (1 para sequential)
    entries: int  # número de entradas (article_id)
    manual: bool  # linkar manualmente bibliotecas externas
    db: str

    def to_key(self) -> tuple:
        """Retorna uma chave única para esta configuração."""
        return (self.data_size, self.approach, self.threads)


def get_csv_filename(db_path: str) -> str:
    """Gera nome do CSV baseado no nome do banco de dados."""
    db_name = Path(db_path).stem  # Remove extensão e caminho
    return f"benchmark_results_{db_name}.csv"


def load_existing_results(
    csv_filename: str, expected_iterations: int
) -> tuple[list[dict], set[tuple]]:
    """Carrega resultados existentes do CSV e retorna configurações completas.

    Returns:
        tuple: (lista de resultados, set de configurações completas)
    """
    if not Path(csv_filename).exists():
        return [], set()

    results = []
    config_counts = {}  # {config_key: count}

    try:
        with open(csv_filename, "r") as f:
            reader = csv.DictReader(f)
            for row in reader:
                # Converter tipos
                row["threads"] = int(row["threads"])
                row["time_seconds"] = float(row["time_seconds"])
                row["speedup"] = float(row["speedup"])
                row["efficiency"] = float(row["efficiency"])

                results.append(row)

                # Contar execuções por configuração
                config_key = (row["data_size"], row["approach"], row["threads"])
                config_counts[config_key] = config_counts.get(config_key, 0) + 1

        # Identificar configurações completas
        completed_configs = {
            k for k, v in config_counts.items() if v >= expected_iterations
        }

        return results, completed_configs
    except Exception as e:
        print(f"\033[33mAVISO: Erro ao carregar {csv_filename}: {e}\033[0m")
        return [], set()


def save_results_to_csv(csv_filename: str, results: list[dict]):
    """Salva resultados no CSV."""
    with open(csv_filename, "w", newline="") as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=CSV_FIELDNAMES)
        writer.writeheader()
        for result in results:
            writer.writerow(result)


def get_size_thresholds(db_file: str) -> dict[str, Optional[int]]:
    """Executa query SQL para obter IDs nos diferentes thresholds de tamanho."""

    query = """
    SELECT
      MIN(CASE WHEN running_total >= 5 * 1024 THEN article_id END)                AS id_at_5kb,
      MIN(CASE WHEN running_total >= 10 * 1024 THEN article_id END)               AS id_at_10kb,
      MIN(CASE WHEN running_total >= 50 * 1024 THEN article_id END)               AS id_at_50kb,
      MIN(CASE WHEN running_total >= 100 * 1024 THEN article_id END)              AS id_at_100kb,
      MIN(CASE WHEN running_total >= 500 * 1024 THEN article_id END)              AS id_at_500kb,
      MIN(CASE WHEN running_total >= 1 * 1024 * 1024 THEN article_id END)         AS id_at_1mb,
      MIN(CASE WHEN running_total >= 10 * 1024 * 1024 THEN article_id END)        AS id_at_10mb,
      MIN(CASE WHEN running_total >= 50 * 1024 * 1024 THEN article_id END)        AS id_at_50mb,
      MIN(CASE WHEN running_total >= 100 * 1024 * 1024 THEN article_id END)       AS id_at_100mb,
      MIN(CASE WHEN running_total >= 500 * 1024 * 1024 THEN article_id END)       AS id_at_500mb,
      MIN(CASE WHEN running_total >= 1 * 1024 * 1024 * 1024 THEN article_id END)  AS id_at_1gb,
      MIN(CASE WHEN running_total >= 5 * 1024 * 1024 * 1024 THEN article_id END)  AS id_at_5gb,
      MIN(CASE WHEN running_total >= 6 * 1024 * 1024 * 1024 THEN article_id END)  AS id_at_6gb,
      MIN(CASE WHEN running_total >= 7 * 1024 * 1024 * 1024 THEN article_id END)  AS id_at_7gb,
      MIN(CASE WHEN running_total >= 8 * 1024 * 1024 * 1024 THEN article_id END)  AS id_at_8gb,
      MIN(CASE WHEN running_total >= 9 * 1024 * 1024 * 1024 THEN article_id END)  AS id_at_9gb,
      MIN(CASE WHEN running_total >= 10 * 1024 * 1024 * 1024 THEN article_id END) AS id_at_10gb,
      MAX(article_id) AS id_at_maximum
    FROM running_totals;
    """

    try:
        conn = sqlite3.connect(db_file)
        cursor = conn.cursor()
        cursor.execute(query)
        row = cursor.fetchone()
        conn.close()

        if not row:
            return {}

        sizes = [
            "5kb",
            "10kb",
            "50kb",
            "100kb",
            "500kb",
            "1mb",
            "10mb",
            "50mb",
            "100mb",
            "500mb",
            "1gb",
            "5gb",
            "6gb",
            "7gb",
            "8gb",
            "9gb",
            "10gb",
            "maximum",
        ]

        return {
            size: int(val) if val is not None else None for size, val in zip(sizes, row)
        }

    except sqlite3.OperationalError as e:
        print(f"\033[33mAVISO: Tabela running_totals não existe: {e}\033[0m")
        print("\033[33mUsando valores de fallback para teste...\033[0m")
        # Valores de fallback para teste
        return {
            "5kb": 100,
            "10kb": 200,
            "50kb": 500,
            "100kb": 1000,
        }


def compile_app(
    sequential: bool = False, nthreads: int = 4, *, manual: bool = False
) -> bool:
    """Compila o app C com configurações específicas."""

    make_cmd = ["make", "clean", "all", "VERBOSE=0"]

    if manual:
        make_cmd.append("MANUAL=1")

    if sequential:
        make_cmd.append("SEQ=1")
    else:
        make_cmd.append(f"NTHR={nthreads}")

    try:
        result = subprocess.run(make_cmd, capture_output=True, text=True, timeout=60)
        return result.returncode == 0
    except subprocess.TimeoutExpired:
        print(f"\033[31mERRO: Timeout na compilação\033[0m")
        return False
    except Exception as e:
        print(f"\033[31mERRO: Erro na compilação: {e}\033[0m")
        return False


def clean_models() -> bool:
    """Limpa os modelos salvos em ./models/ para forçar recálculo."""
    try:
        result = subprocess.run(
            ["make", "clean_models"], capture_output=True, text=True, timeout=10
        )
        return result.returncode == 0
    except Exception as e:
        print(f"\033[31mERRO: Erro ao limpar modelos: {e}\033[0m")
        return False


def run_single_benchmark(
    entries: int,
    table: str,
    nthreads: int = 4,
    db: str = "./data/wiki-full.db",
    *,
    manual: bool = False,
    log_file: Optional[Path] = None,
) -> Optional[float]:
    """Executa o app uma vez e retorna o tempo total em segundos."""

    # Limpar modelos antes de cada execução para forçar recálculo
    if not clean_models():
        print(f"\033[33mAVISO: Falha ao limpar modelos\033[0m")

    cmd = [
        "make",
        "run",
        f"ENTRIES={str(entries)}",
        f"TBL={table}",
        f"NTHR={str(nthreads)}",
        f"DB={str(db)}",
    ]

    if manual:
        cmd.append("MANUAL=1")

    try:
        result = subprocess.run(cmd, capture_output=True, text=True)

        # Salvar output em arquivo de log se especificado
        if log_file:
            with open(log_file, "a") as f:
                f.write(f"{'=' * 80}\n")
                f.write(f"Command: {' '.join(cmd)}\n")
                f.write(f"{'=' * 80}\n")
                f.write("STDOUT:\n")
                f.write(result.stdout)
                f.write("\nSTDERR:\n")
                f.write(result.stderr)
                f.write(f"\n{'=' * 80}\n\n")

        # Parsear: [TEMPO TOTAL] 0.464 segundos
        match = re.search(r"\[TEMPO TOTAL\]\s+([\d.]+)\s+segundos", result.stdout)
        if match:
            return float(match.group(1))
        else:
            print(f"\033[33mAVISO: Não conseguiu parsear tempo do output\033[0m")
            return None

    except subprocess.TimeoutExpired:
        print(f"\033[31mERRO: Timeout na execução (entries={entries})\033[0m")
        return None
    except Exception as e:
        print(f"\033[31mERRO: Erro na execução: {e}\033[0m")
        return None


def run_benchmark_config(
    config: BenchmarkConfig, table: str, iterations: int, seq_baseline: dict[str, float]
) -> tuple[list[float], float]:
    """Executa benchmark para uma configuração específica N vezes.

    Returns:
        tuple: (lista de tempos, tempo médio)
    """

    # Compilar
    is_seq = config.approach == "sequential"
    if not compile_app(
        sequential=is_seq, nthreads=config.threads, manual=config.manual
    ):
        tqdm.write(
            f"\033[31mERRO: Falha na compilação para {config.data_size} {config.approach} threads={config.threads}\033[0m"
        )
        return [], 0.0

    # Criar arquivo de log para esta configuração
    LOG_DIR.mkdir(exist_ok=True)
    log_filename = f"{config.data_size}_{config.approach}_t{config.threads}.txt"
    log_file = LOG_DIR / log_filename

    # Limpar arquivo de log anterior se existir
    if log_file.exists():
        log_file.unlink()

    # Executar N vezes
    times = []
    for i in range(iterations):
        time = run_single_benchmark(
            config.entries,
            table,
            config.threads,
            manual=config.manual,
            log_file=log_file,
            db=config.db,
        )
        if time is not None:
            times.append(time)

    # Resultado final com speedup e efficiency
    if times:
        avg = mean(times)
        sd = stdev(times) if len(times) > 1 else 0

        # Calcular speedup e efficiency
        seq_time = seq_baseline.get(config.data_size)
        if seq_time and seq_time > 0:
            speedup = seq_time / avg
            efficiency = speedup / config.threads
            tqdm.write(
                f"  [{config.data_size:>6}] {config.approach:10} t={config.threads:2} | \033[32mOK\033[0m avg={avg:.3f}s ±{sd:.3f}s ({len(times)}/{iterations}) | speedup={speedup:.2f}x efficiency={efficiency:.2f}"
            )
        else:
            tqdm.write(
                f"  [{config.data_size:>6}] {config.approach:10} t={config.threads:2} | \033[32mOK\033[0m avg={avg:.3f}s ±{sd:.3f}s ({len(times)}/{iterations})"
            )

        return times, avg
    else:
        tqdm.write(
            f"  [{config.data_size:>6}] {config.approach:10} t={config.threads:2} | \033[31mERRO\033[0m"
        )
        return [], 0.0


def main():
    parser = argparse.ArgumentParser(
        description="Benchmark de performance do app C TF-IDF",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    _ = parser.add_argument(
        "--db",
        type=str,
        default="data/wiki-small.db",
        help="Arquivo do banco de dados (default: data/wiki-small.db)",
    )
    _ = parser.add_argument(
        "--table",
        type=str,
        default="sample_articles",
        help="Nome da tabela no banco de dados (default: sample_articles)",
    )
    _ = parser.add_argument(
        "--iter",
        type=int,
        default=30,
        help="Número de iterações por configuração (default: 30)",
    )
    _ = parser.add_argument(
        "--manual",
        action="store_true",
        default=False,
        help="Linkar manualmente bibliotecas externas (default: False)",
    )

    args = parser.parse_args()

    # Gerar nome do CSV
    csv_filename = get_csv_filename(args.db)

    print("=" * 70)
    print("BENCHMARK DE PERFORMANCE - App C TF-IDF")
    print("=" * 70)
    print(f"Banco de dados: {args.db}")
    print(f"Tabela: {args.table}")
    print(f"Iterações por configuração: {args.iter}")
    print(f"Logs serão salvos em: {LOG_DIR}/")
    print(f"Resultados serão salvos em: {csv_filename}")
    print()

    # Carregar resultados existentes
    print("Verificando resultados anteriores...")
    results, completed_configs = load_existing_results(csv_filename, args.iter)

    if completed_configs:
        print(
            f"\033[32mOK:\033[0m Encontrados {len(results)} resultados de execuções anteriores"
        )
        print(
            f"\033[32mOK:\033[0m {len(completed_configs)} configurações já completas serão puladas"
        )
    else:
        print("Nenhum resultado anterior encontrado. Iniciando do zero.")
    print()

    # Verificar se app existe
    try:
        subprocess.run(
            ["./app", "--help"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=5,
        )
    except FileNotFoundError:
        print("\033[31mERRO: ./app não encontrado. Execute 'make' primeiro.\033[0m")
        sys.exit(1)

    # Obter thresholds de tamanho
    print("Obtendo thresholds de tamanho do banco de dados...")
    thresholds = get_size_thresholds(args.db)

    if not thresholds:
        print("\033[31mERRO: Não foi possível obter thresholds\033[0m")
        sys.exit(1)

    print(f"\033[32mOK:\033[0m Encontrados {len(thresholds)} thresholds")
    for size, article_id in thresholds.items():
        if article_id:
            print(f"  {size:>6}: article_id={article_id}")
    print()

    # Gerar configurações de benchmark
    configs = []

    for size, article_id in thresholds.items():
        if article_id is None:
            continue

        # Converter article_id para número de entradas (article_id + 1)
        # Ex: article_id=0 -> processar 1 artigo (índice 0)
        num_entries = article_id + 1

        # Sequencial
        configs.append(
            BenchmarkConfig(
                data_size=size,
                approach="sequential",
                threads=1,
                entries=num_entries,
                manual=args.manual,
                db=args.db,
            )
        )

        # Paralelo com diferentes threads
        for nthreads in [1, 2, 4, 6, 8, 16]:
            configs.append(
                BenchmarkConfig(
                    data_size=size,
                    approach="threaded",
                    threads=nthreads,
                    entries=num_entries,
                    manual=args.manual,
                    db=args.db,
                )
            )

    # Filtrar configurações já completas
    configs_to_run = [c for c in configs if c.to_key() not in completed_configs]

    print(f"Total de configurações: {len(configs)}")
    if completed_configs:
        print(
            f"Configurações a executar: {len(configs_to_run)} (pulando {len(configs) - len(configs_to_run)} já completas)"
        )
    print(f"Total de execuções: {len(configs_to_run) * args.iter}")
    print()

    # Executar benchmarks
    # Baseline sequencial por data_size (reconstruir dos resultados existentes)
    seq_baseline = {}  # {data_size: avg_sequential_time}

    # Calcular seq_baseline dos resultados existentes
    for r in results:
        if r["approach"] == "sequential" and r["data_size"] not in seq_baseline:
            # Calcular média dos tempos sequenciais existentes
            seq_times_for_size = [
                x["time_seconds"]
                for x in results
                if x["data_size"] == r["data_size"] and x["approach"] == "sequential"
            ]
            if seq_times_for_size:
                seq_baseline[r["data_size"]] = mean(seq_times_for_size)

    # Armazenar médias para calcular speedup/efficiency
    avg_times = {}  # {(data_size, approach, threads): avg_time}

    with tqdm(configs_to_run, desc="Benchmarks", unit="config", ncols=120) as pbar:
        for config in pbar:
            pbar.set_description(
                f"Benchmark [{config.data_size}] {config.approach} t={config.threads}"
            )
            times, avg_time = run_benchmark_config(
                config, args.table, args.iter, seq_baseline
            )

            # Armazenar média
            if times:
                key = (config.data_size, config.approach, config.threads)
                avg_times[key] = avg_time

                # Se é sequencial, atualizar baseline
                if config.approach == "sequential":
                    seq_baseline[config.data_size] = avg_time

            # Calcular seq_time para esta configuração
            seq_time = seq_baseline.get(config.data_size)

            # Armazenar resultados brutos com speedup/efficiency já calculados
            for time in times:
                if seq_time and seq_time > 0:
                    speedup = seq_time / time
                    efficiency = speedup / config.threads
                else:
                    speedup = 1.0 if config.approach == "sequential" else 0.0
                    efficiency = 1.0 if config.approach == "sequential" else 0.0

                results.append(
                    {
                        "data_size": config.data_size,
                        "approach": config.approach,
                        "threads": config.threads,
                        "time_seconds": time,
                        "speedup": speedup,
                        "efficiency": efficiency,
                    }
                )

            # Salvar resultados incrementalmente após cada configuração
            save_results_to_csv(csv_filename, results)

    # Salvamento final
    print()
    print(f"Salvando resultados finais em {csv_filename}...")
    save_results_to_csv(csv_filename, results)
    print(f"\033[32mOK:\033[0m CSV salvo com {len(results)} registros")

    # Sumário
    print()
    print("=" * 70)
    print("SUMÁRIO")
    print("=" * 70)
    print(f"Total de configurações possíveis: {len(configs)}")
    print(f"Configurações executadas nesta sessão: {len(configs_to_run)}")
    print(f"Total de execuções bem-sucedidas: {len(results)}")
    total_expected = len(configs) * args.iter
    print(
        f"Taxa de sucesso: {100 * len(results) / total_expected:.1f}% ({len(results)}/{total_expected})"
    )
    print()
    print(f"\033[32mOK: Benchmark concluído!\033[0m")
    print(f"Resultados salvos em: {csv_filename}")


if __name__ == "__main__":
    main()
