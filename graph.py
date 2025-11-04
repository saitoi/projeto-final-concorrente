# /// script
# requires-python = ">=3.12"
# dependencies = [
#     "matplotlib",
#     "numpy",
#     "pandas",
# ]
# ///

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import os
from pathlib import Path

# Configurar para usar fonte que suporta caracteres especiais
plt.rcParams['font.family'] = 'DejaVu Sans'

# Criar diretório para salvar gráficos
output_dir = Path('results/performance/graphs')
output_dir.mkdir(parents=True, exist_ok=True)

# Ler o CSV
csv_file = 'benchmark_results_wiki-full.csv'
df = pd.read_csv(csv_file)

# Extrair nome do banco de dados do nome do arquivo
db_name = csv_file.replace('benchmark_results_', '').replace('.csv', '')

# Agrupar por data_size, approach e threads, calculando média e desvio padrão
stats = df.groupby(['data_size', 'approach', 'threads']).agg({
    'time_seconds': ['mean', 'std'],
    'speedup': ['mean', 'std'],
    'efficiency': ['mean', 'std']
}).reset_index()

# Flatten multi-level columns
stats.columns = ['_'.join(col).strip('_') for col in stats.columns.values]

# Ordenar data_sizes numericamente
size_order = {
    '5kb': 5,
    '10kb': 10,
    '50kb': 50,
    '100kb': 100,
    '500kb': 500,
    '1mb': 1024,
    '10mb': 10240,
    '50mb': 51200,
    '100mb': 102400,
    '500mb': 512000,
    '1gb': 1048576,
    '5gb': 5242880,
    '6gb': 6291456
}

data_sizes = df['data_size'].unique()
sorted_sizes = sorted(data_sizes, key=lambda x: size_order.get(x, 0))

# Obter todos os números de threads únicos
all_threads = sorted(df['threads'].unique())

# Cores para cada configuração de threads
thread_colors = plt.cm.tab10(np.linspace(0, 1, len(all_threads)))

# Configurar estilo
plt.style.use('seaborn-v0_8-whitegrid')

# Função para plotar com incerteza
def plot_with_uncertainty(ax, x, y_mean, y_std, label, color, marker='o', linestyle='-'):
    ax.plot(x, y_mean, marker=marker, label=label, color=color,
            linewidth=2, markersize=6, linestyle=linestyle)
    ax.fill_between(x, y_mean - y_std, y_mean + y_std, alpha=0.2, color=color)

# 1. GRÁFICO: Tempo de execução (escala log)
fig1, ax1 = plt.subplots(figsize=(12, 7))

# Plotar sequential
df_seq_all = stats[stats['approach'] == 'sequential']
if not df_seq_all.empty:
    times_seq = []
    stds_seq = []
    for size in sorted_sizes:
        df_temp = df_seq_all[df_seq_all['data_size'] == size]
        if not df_temp.empty:
            times_seq.append(df_temp['time_seconds_mean'].iloc[0])
            stds_seq.append(df_temp['time_seconds_std'].fillna(0).iloc[0])
        else:
            times_seq.append(np.nan)
            stds_seq.append(0)

    x_pos = np.arange(len(sorted_sizes))
    times_seq = np.array(times_seq)
    stds_seq = np.array(stds_seq)

    plot_with_uncertainty(ax1, x_pos, times_seq, stds_seq, 'Sequencial',
                         'black', marker='s', linestyle='--')

# Plotar cada configuração de threads
for idx, n_threads in enumerate(all_threads):
    df_thr = stats[(stats['approach'] == 'threaded') & (stats['threads'] == n_threads)]

    if not df_thr.empty:
        times = []
        stds = []
        for size in sorted_sizes:
            df_temp = df_thr[df_thr['data_size'] == size]
            if not df_temp.empty:
                times.append(df_temp['time_seconds_mean'].iloc[0])
                stds.append(df_temp['time_seconds_std'].fillna(0).iloc[0])
            else:
                times.append(np.nan)
                stds.append(0)

        x_pos = np.arange(len(sorted_sizes))
        times = np.array(times)
        stds = np.array(stds)

        plot_with_uncertainty(ax1, x_pos, times, stds, f'{n_threads} threads',
                            thread_colors[idx])

ax1.set_xlabel('Tamanho dos Dados', fontsize=13, fontweight='bold')
ax1.set_ylabel('Tempo de Execução (segundos)', fontsize=13, fontweight='bold')
ax1.set_title(f'Tempo de Execução por Tamanho de Dados - {db_name}', fontsize=15, fontweight='bold')
ax1.set_xticks(np.arange(len(sorted_sizes)))
ax1.set_xticklabels(sorted_sizes, rotation=45, ha='right')
ax1.legend(fontsize=9, ncol=2, loc='upper left')
ax1.grid(True, alpha=0.3, linestyle='--')
ax1.set_yscale('log')
plt.tight_layout()
output_file = output_dir / f'tempo_execucao_log_{db_name}.png'
plt.savefig(output_file, dpi=300, bbox_inches='tight', transparent=True, format='png')
plt.close()

print(f'Gerado: {output_file}')

# 2. GRÁFICO: Tempo de execução (escala linear, até 100MB)
fig2, ax2 = plt.subplots(figsize=(12, 7))

small_sizes = [s for s in sorted_sizes if size_order.get(s, 0) <= 102400]

# Plotar sequential
if not df_seq_all.empty:
    times_seq = []
    stds_seq = []
    for size in small_sizes:
        df_temp = df_seq_all[df_seq_all['data_size'] == size]
        if not df_temp.empty:
            times_seq.append(df_temp['time_seconds_mean'].iloc[0])
            stds_seq.append(df_temp['time_seconds_std'].fillna(0).iloc[0])
        else:
            times_seq.append(np.nan)
            stds_seq.append(0)

    x_pos = np.arange(len(small_sizes))
    times_seq = np.array(times_seq)
    stds_seq = np.array(stds_seq)

    plot_with_uncertainty(ax2, x_pos, times_seq, stds_seq, 'Sequencial',
                         'black', marker='s', linestyle='--')

# Plotar cada configuração de threads
for idx, n_threads in enumerate(all_threads):
    df_thr = stats[(stats['approach'] == 'threaded') & (stats['threads'] == n_threads)]

    if not df_thr.empty:
        times = []
        stds = []
        for size in small_sizes:
            df_temp = df_thr[df_thr['data_size'] == size]
            if not df_temp.empty:
                times.append(df_temp['time_seconds_mean'].iloc[0])
                stds.append(df_temp['time_seconds_std'].fillna(0).iloc[0])
            else:
                times.append(np.nan)
                stds.append(0)

        x_pos = np.arange(len(small_sizes))
        times = np.array(times)
        stds = np.array(stds)

        plot_with_uncertainty(ax2, x_pos, times, stds, f'{n_threads} threads',
                            thread_colors[idx])

ax2.set_xlabel('Tamanho dos Dados', fontsize=13, fontweight='bold')
ax2.set_ylabel('Tempo de Execução (segundos)', fontsize=13, fontweight='bold')
ax2.set_title(f'Tempo de Execução por Tamanho de Dados (até 100MB) - {db_name}', fontsize=15, fontweight='bold')
ax2.set_xticks(np.arange(len(small_sizes)))
ax2.set_xticklabels(small_sizes, rotation=45, ha='right')
ax2.legend(fontsize=9, ncol=2, loc='upper left')
ax2.grid(True, alpha=0.3, linestyle='--')
plt.tight_layout()
output_file = output_dir / f'tempo_execucao_linear_{db_name}.png'
plt.savefig(output_file, dpi=300, bbox_inches='tight', transparent=True, format='png')
plt.close()

print(f'Gerado: {output_file}')

# 3. GRÁFICO: Tempo de execução (escala normal, todos os tamanhos)
fig3, ax3 = plt.subplots(figsize=(12, 7))

# Plotar sequential
if not df_seq_all.empty:
    times_seq = []
    stds_seq = []
    for size in sorted_sizes:
        df_temp = df_seq_all[df_seq_all['data_size'] == size]
        if not df_temp.empty:
            times_seq.append(df_temp['time_seconds_mean'].iloc[0])
            stds_seq.append(df_temp['time_seconds_std'].fillna(0).iloc[0])
        else:
            times_seq.append(np.nan)
            stds_seq.append(0)

    x_pos = np.arange(len(sorted_sizes))
    times_seq = np.array(times_seq)
    stds_seq = np.array(stds_seq)

    plot_with_uncertainty(ax3, x_pos, times_seq, stds_seq, 'Sequencial',
                         'black', marker='s', linestyle='--')

# Plotar cada configuração de threads
for idx, n_threads in enumerate(all_threads):
    df_thr = stats[(stats['approach'] == 'threaded') & (stats['threads'] == n_threads)]

    if not df_thr.empty:
        times = []
        stds = []
        for size in sorted_sizes:
            df_temp = df_thr[df_thr['data_size'] == size]
            if not df_temp.empty:
                times.append(df_temp['time_seconds_mean'].iloc[0])
                stds.append(df_temp['time_seconds_std'].fillna(0).iloc[0])
            else:
                times.append(np.nan)
                stds.append(0)

        x_pos = np.arange(len(sorted_sizes))
        times = np.array(times)
        stds = np.array(stds)

        plot_with_uncertainty(ax3, x_pos, times, stds, f'{n_threads} threads',
                            thread_colors[idx])

ax3.set_xlabel('Tamanho dos Dados', fontsize=13, fontweight='bold')
ax3.set_ylabel('Tempo de Execução (segundos)', fontsize=13, fontweight='bold')
ax3.set_title(f'Tempo de Execução por Tamanho de Dados (Escala Normal) - {db_name}', fontsize=15, fontweight='bold')
ax3.set_xticks(np.arange(len(sorted_sizes)))
ax3.set_xticklabels(sorted_sizes, rotation=45, ha='right')
ax3.legend(fontsize=9, ncol=2, loc='upper left')
ax3.grid(True, alpha=0.3, linestyle='--')
plt.tight_layout()
output_file = output_dir / f'tempo_execucao_normal_{db_name}.png'
plt.savefig(output_file, dpi=300, bbox_inches='tight')
plt.close()

print(f'Gerado: {output_file}')

# 4. GRÁFICO: Aceleração (Speedup)
fig4, ax4 = plt.subplots(figsize=(12, 7))

# Plotar cada configuração de threads
for idx, n_threads in enumerate(all_threads):
    df_thr = stats[(stats['approach'] == 'threaded') & (stats['threads'] == n_threads)]

    if not df_thr.empty:
        speedups = []
        stds = []
        for size in sorted_sizes:
            df_temp = df_thr[df_thr['data_size'] == size]
            if not df_temp.empty:
                speedups.append(df_temp['speedup_mean'].iloc[0])
                stds.append(df_temp['speedup_std'].fillna(0).iloc[0])
            else:
                speedups.append(np.nan)
                stds.append(0)

        x_pos = np.arange(len(sorted_sizes))
        speedups = np.array(speedups)
        stds = np.array(stds)

        plot_with_uncertainty(ax4, x_pos, speedups, stds, f'{n_threads} threads',
                            thread_colors[idx])

# Linha ideal
x_pos = np.arange(len(sorted_sizes))
max_speedup = max(all_threads)
ax4.axhline(y=max_speedup, linestyle=':', color='gray',
           label=f'Ideal ({max_speedup}x)', alpha=0.7, linewidth=2)

ax4.set_xlabel('Tamanho dos Dados', fontsize=13, fontweight='bold')
ax4.set_ylabel('Aceleração (Speedup)', fontsize=13, fontweight='bold')
ax4.set_title(f'Aceleração por Tamanho de Dados - {db_name}', fontsize=15, fontweight='bold')
ax4.set_xticks(np.arange(len(sorted_sizes)))
ax4.set_xticklabels(sorted_sizes, rotation=45, ha='right')
ax4.legend(fontsize=9, ncol=2, loc='upper left')
ax4.grid(True, alpha=0.3, linestyle='--')
plt.tight_layout()
output_file = output_dir / f'aceleracao_{db_name}.png'
plt.savefig(output_file, dpi=300, bbox_inches='tight')
plt.close()

print(f'Gerado: {output_file}')

# 5. GRÁFICO: Eficiência
fig5, ax5 = plt.subplots(figsize=(12, 7))

# Plotar cada configuração de threads
for idx, n_threads in enumerate(all_threads):
    df_thr = stats[(stats['approach'] == 'threaded') & (stats['threads'] == n_threads)]

    if not df_thr.empty:
        efficiencies = []
        stds = []
        for size in sorted_sizes:
            df_temp = df_thr[df_thr['data_size'] == size]
            if not df_temp.empty:
                efficiencies.append(df_temp['efficiency_mean'].iloc[0])
                stds.append(df_temp['efficiency_std'].fillna(0).iloc[0])
            else:
                efficiencies.append(np.nan)
                stds.append(0)

        x_pos = np.arange(len(sorted_sizes))
        efficiencies = np.array(efficiencies)
        stds = np.array(stds)

        plot_with_uncertainty(ax5, x_pos, efficiencies, stds, f'{n_threads} threads',
                            thread_colors[idx])

# Linha ideal (100%)
ax5.axhline(y=1.0, linestyle=':', color='gray',
           label='Ideal (100%)', alpha=0.7, linewidth=2)

ax5.set_xlabel('Tamanho dos Dados', fontsize=13, fontweight='bold')
ax5.set_ylabel('Eficiência', fontsize=13, fontweight='bold')
ax5.set_title(f'Eficiência por Tamanho de Dados - {db_name}', fontsize=15, fontweight='bold')
ax5.set_xticks(np.arange(len(sorted_sizes)))
ax5.set_xticklabels(sorted_sizes, rotation=45, ha='right')
ax5.legend(fontsize=9, ncol=2, loc='upper right')
ax5.grid(True, alpha=0.3, linestyle='--')
plt.tight_layout()
output_file = output_dir / f'eficiencia_{db_name}.png'
plt.savefig(output_file, dpi=300, bbox_inches='tight')
plt.close()

print(f'Gerado: {output_file}')

# 6. GRÁFICO COMBINADO: Todos juntos (3 subplots)
fig6, (ax6a, ax6b, ax6c) = plt.subplots(1, 3, figsize=(20, 6))

# Tempo (escala log)
if not df_seq_all.empty:
    times_seq = []
    stds_seq = []
    for size in sorted_sizes:
        df_temp = df_seq_all[df_seq_all['data_size'] == size]
        if not df_temp.empty:
            times_seq.append(df_temp['time_seconds_mean'].iloc[0])
            stds_seq.append(df_temp['time_seconds_std'].fillna(0).iloc[0])
        else:
            times_seq.append(np.nan)
            stds_seq.append(0)
    x_pos = np.arange(len(sorted_sizes))
    plot_with_uncertainty(ax6a, x_pos, np.array(times_seq), np.array(stds_seq),
                         'Seq.', 'black', marker='s', linestyle='--')

for idx, n_threads in enumerate(all_threads):
    df_thr = stats[(stats['approach'] == 'threaded') & (stats['threads'] == n_threads)]
    if not df_thr.empty:
        times = []
        stds = []
        for size in sorted_sizes:
            df_temp = df_thr[df_thr['data_size'] == size]
            if not df_temp.empty:
                times.append(df_temp['time_seconds_mean'].iloc[0])
                stds.append(df_temp['time_seconds_std'].fillna(0).iloc[0])
            else:
                times.append(np.nan)
                stds.append(0)
        x_pos = np.arange(len(sorted_sizes))
        plot_with_uncertainty(ax6a, x_pos, np.array(times), np.array(stds),
                            f'{n_threads}t', thread_colors[idx])

ax6a.set_xlabel('Tamanho dos Dados', fontsize=11)
ax6a.set_ylabel('Tempo (s)', fontsize=11)
ax6a.set_title('Tempo de Execução', fontsize=12, fontweight='bold')
ax6a.set_xticks(np.arange(len(sorted_sizes)))
ax6a.set_xticklabels(sorted_sizes, rotation=45, ha='right', fontsize=8)
ax6a.legend(fontsize=8, ncol=2, loc='upper left')
ax6a.grid(True, alpha=0.3, linestyle='--')
ax6a.set_yscale('log')

# Aceleração
for idx, n_threads in enumerate(all_threads):
    df_thr = stats[(stats['approach'] == 'threaded') & (stats['threads'] == n_threads)]
    if not df_thr.empty:
        speedups = []
        stds = []
        for size in sorted_sizes:
            df_temp = df_thr[df_thr['data_size'] == size]
            if not df_temp.empty:
                speedups.append(df_temp['speedup_mean'].iloc[0])
                stds.append(df_temp['speedup_std'].fillna(0).iloc[0])
            else:
                speedups.append(np.nan)
                stds.append(0)
        x_pos = np.arange(len(sorted_sizes))
        plot_with_uncertainty(ax6b, x_pos, np.array(speedups), np.array(stds),
                            f'{n_threads}t', thread_colors[idx])

ax6b.axhline(y=max(all_threads), linestyle=':', color='gray',
            label='Ideal', alpha=0.7, linewidth=2)
ax6b.set_xlabel('Tamanho dos Dados', fontsize=11)
ax6b.set_ylabel('Aceleração', fontsize=11)
ax6b.set_title('Aceleração', fontsize=12, fontweight='bold')
ax6b.set_xticks(np.arange(len(sorted_sizes)))
ax6b.set_xticklabels(sorted_sizes, rotation=45, ha='right', fontsize=8)
ax6b.legend(fontsize=8, ncol=2, loc='upper left')
ax6b.grid(True, alpha=0.3, linestyle='--')

# Eficiência
for idx, n_threads in enumerate(all_threads):
    df_thr = stats[(stats['approach'] == 'threaded') & (stats['threads'] == n_threads)]
    if not df_thr.empty:
        efficiencies = []
        stds = []
        for size in sorted_sizes:
            df_temp = df_thr[df_thr['data_size'] == size]
            if not df_temp.empty:
                efficiencies.append(df_temp['efficiency_mean'].iloc[0])
                stds.append(df_temp['efficiency_std'].fillna(0).iloc[0])
            else:
                efficiencies.append(np.nan)
                stds.append(0)
        x_pos = np.arange(len(sorted_sizes))
        plot_with_uncertainty(ax6c, x_pos, np.array(efficiencies), np.array(stds),
                            f'{n_threads}t', thread_colors[idx])

ax6c.axhline(y=1.0, linestyle=':', color='gray', label='Ideal', alpha=0.7, linewidth=2)
ax6c.set_xlabel('Tamanho dos Dados', fontsize=11)
ax6c.set_ylabel('Eficiência', fontsize=11)
ax6c.set_title('Eficiência', fontsize=12, fontweight='bold')
ax6c.set_xticks(np.arange(len(sorted_sizes)))
ax6c.set_xticklabels(sorted_sizes, rotation=45, ha='right', fontsize=8)
ax6c.legend(fontsize=8, ncol=2, loc='upper right')
ax6c.grid(True, alpha=0.3, linestyle='--')

fig6.suptitle(f'Métricas de Desempenho por Tamanho de Dados - {db_name}',
             fontsize=16, fontweight='bold', y=1.00)
plt.tight_layout()
output_file = output_dir / f'metricas_combinadas_{db_name}.png'
plt.savefig(output_file, dpi=300, bbox_inches='tight')
plt.close()

print(f'Gerado: {output_file}')

print(f'\nTodos os gráficos foram gerados com sucesso em {output_dir}!')
