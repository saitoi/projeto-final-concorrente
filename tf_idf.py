# /// script
# requires-python = ">=3.13"
# dependencies = [
#     "nltk",
#     "prettytable",
# ]
# ///

import os
import re
import nltk
from math import log2
from prettytable import PrettyTable, HRuleStyle, VRuleStyle
from nltk.stem import SnowballStemmer
from collections import Counter

class MatrizFrequencia:
    def __init__(
        self,
        word_freq: dict[str, dict[int, int]],
        stopwords: list[str] | None = None,
        stemmer: SnowballStemmer | None = None,
        separadores: list[str] | None = None,
        words: list[str] | None = None,
        idf: dict[str, float] | None = None,
    ) -> None:
        self.word_freq = word_freq
        self.words = words or sorted(word_freq.keys())
        self.stopwords = list(nltk.corpus.stopwords.words('english')) if stopwords is None else stopwords
        self.stemmer = stemmer or SnowballStemmer("english")
        self.separadores = separadores or []

        self.docs = {d for m in self.word_freq.values() for d in m}
        self.ndocs = len(self.docs)

        self.idf = idf or self._compute_idf()
        self.doc_vecs = self._compute_vecs()
        self.doc_norms = self._compute_norms()
        self.queries: list[MatrizFrequencia] = []

    def __getitem__(self, key) -> dict[int, int]:
        if key not in self.words:
            raise KeyError(f"Word '{key}' não encontrada.")
        return self.word_freq.get(key, {})

    def freq(self, key, doc_id) -> int:
        return self[key].get(doc_id, 0)

    def tf(self, key: str, doc_id: int) -> float:
        f = self.freq(key, doc_id)
        return 0 if f == 0 else (1 + log2(f))

    def _compute_idf(self) -> dict[str, float]:
        return {k: 0 if self.n_i(k) == 0 else log2(self.ndocs / self.n_i(k)) for k in self.words}

    def n_i(self, key: str) -> int:
        return len(self.word_freq.get(key, {}))

    def weight(self, key, doc_id) -> float:
        f = self.freq(key, doc_id)
        return 0 if f == 0 else self.tf(key, doc_id) * self.idf[key]

    def _compute_vecs(self) -> list[list[float]]:
        return [[self.weight(w, d) for w in self.words] for d in range(self.ndocs)]

    def _compute_norms(self) -> list[float]:
        return [sum(v**2 for v in self.doc_vecs[d])**0.5 for d in range(self.ndocs)]

    def sim(self, doc_id: int, query_id: int) -> float:
        q = self.queries[query_id]
        num = sum(v1 * v2 for v1, v2 in zip(self.doc_vecs[doc_id], q.doc_vecs[0]))
        denom = self.doc_norms[doc_id] * q.doc_norms[0]
        return 0.0 if denom == 0 else num / denom

    def vec_search(
        self,
        query_id: int | None = None,
        query: str | None = None,
        query_filepath: str | None = None,
        query_filename: str = 'consulta.txt',
        token: str = ' ',
    ) -> list[tuple[int, float]]:
        if query_id is None or (query_id >= len(self.queries)):
            query_id = len(self.queries)
            self._process_query(query, query_filepath, query_filename, token)
        res = [(doc_id, self.sim(doc_id, query_id)) for doc_id in range(self.ndocs)]
        return sorted(res, key=lambda x: x[1], reverse=True)

    def show_vec_search(
        self,
        query_id: int | None = None,
        query: str | None = None,
        query_filepath: str | None = None,
        query_filename: str = 'consulta.txt',
        token: str = ' ',
    ) -> None:
        res = self.vec_search(query_id, query, query_filepath, query_filename, token)
        table = PrettyTable()
        table.field_names = ["DocId", "TF-IDF"]
        table.hrules = HRuleStyle.ALL
        table.vrules = VRuleStyle.NONE
        for i, r in res:
            table.add_row([i, r])
        print(table)

    @staticmethod
    def _preprocess_docs(docs, separadores, stopwords, stemmer):
        separadores_regex = "|".join(map(re.escape, separadores))
        tokenizados = [list(filter(None, re.split(separadores_regex, d))) for d in docs]
        normalizados = [[t.lower() for t in d] for d in tokenizados]
        sem_stop = [[t for t in d if t not in stopwords] for d in normalizados]
        stemmizados = [[stemmer.stem(t) for t in d] for d in sem_stop]
        return stemmizados

    @staticmethod
    def _get_word_freq(processed_docs):
        word_doc = {}
        for doc_id, doc in enumerate(processed_docs):
            for word, freq in Counter(doc).items():
                word_doc.setdefault(word, {})[doc_id] = freq
        return word_doc

    def _process_query(self, query=None, query_filepath=None, query_filename='consulta.txt', token=' '):
        q = self._read_query(query, query_filepath, query_filename)
        query_mf = MatrizFrequencia._build([q], self.separadores, self.stopwords, self.stemmer, self.words, self.idf)
        self.queries.append(query_mf)
        print(f"Consulta adicionada com id={len(self.queries)-1}")

    @classmethod
    def from_db(cls, stopwords=None, stemmer=None, separadores_filename='./assets/separadores.txt', db_filename='wiki-small.db', tablename='test_tbl_2', encoding='utf-8'):
        import sqlite3

        conn = sqlite3.connect(db_filename)
        cursor = conn.cursor()

        cursor.execute(f"select article_id, article_text from {tablename} order by article_id")
        rows = cursor.fetchall()
        conn.close()

        docs = [row[1] for row in rows]
        with open(separadores_filename, "r", encoding=encoding) as f:
            separadores = f.read().splitlines()

        stopwords = stopwords or list(nltk.corpus.stopwords.words('english'))
        stemmer = stemmer or SnowballStemmer("english")
        return cls._build(docs, separadores, stopwords, stemmer)
        

    @classmethod
    def from_dir(cls, base_dir, stopwords=None, stemmer=None, separadores_filename='./assets/separadores.txt', docs_dir='documentos', encoding='utf-8'):
        docs = []
        docdir = os.path.join(base_dir, docs_dir)
        for e in sorted(os.scandir(docdir), key=lambda e: int(e.name.removeprefix("doc").removesuffix(".txt"))):
            if e.is_file():
                with open(e.path, 'r', encoding=encoding) as f:
                    docs.append(f.read())
        with open(os.path.join(base_dir, separadores_filename), "r", encoding=encoding) as f:
            separadores = f.read().splitlines()
        stopwords = stopwords or list(nltk.corpus.stopwords.words('portuguese'))
        stemmer = stemmer or SnowballStemmer("portuguese")
        return cls._build(docs, separadores, stopwords, stemmer)

    @classmethod
    def _build(cls, docs, separadores, stopwords, stemmer, words=None, idf=None):
        docs_stemmizados = cls._preprocess_docs(docs, separadores, stopwords, stemmer)
        word_freq = cls._get_word_freq(docs_stemmizados)
        return cls(word_freq, stopwords, stemmer, separadores, words, idf)

    @staticmethod
    def _read_query(query=None, query_filepath=None, query_filename='consulta.txt', encoding='utf-8'):
        if query_filepath:
            with open(os.path.join(query_filepath, query_filename), 'r', encoding=encoding) as f:
                return f.read()
        if not query:
            raise ValueError("Consulta vazia")
        return query

    def print_weight_matrix(self, zero='.'):
        table = PrettyTable()
        table.field_names = ["word"] + [str(d) for d in range(self.ndocs)]
        table.hrules = HRuleStyle.ALL
        table.vrules = VRuleStyle.NONE
        for w in self.words:
            row = [zero if self.weight(w, d) == 0 else self.weight(w, d) for d in range(self.ndocs)]
            table.add_row([w] + row)
        print(table)

if __name__ == "__main__":
    import argparse

    nltk.download('stopwords')

    parser = argparse.ArgumentParser(description='TF-IDF Vector Search')
    parser.add_argument('-d', '--database', type=str, default='./data/wiki-small.db', help='Nome do arquivo de banco de dados')
    parser.add_argument('-t', '--tablename', type=str, default='test_tbl_2', help='Nome da tabela no banco de dados')
    parser.add_argument('-s', '--separadores', type=str, default='./assets/separadores.txt', help='Arquivo de separadores')
    parser.add_argument('-q', '--query', type=str, help='Consulta a ser processada')

    args = parser.parse_args()

    mf = MatrizFrequencia.from_db(
        db_filename=args.database,
        tablename=args.tablename,
        separadores_filename=args.separadores
    )

    if args.query:
        mf.show_vec_search(query=args.query)
    else:
        print(f"Matriz de frequência carregada: {mf.ndocs} documentos, {len(mf.words)} palavras únicas")
        print("Use -q/--query para executar uma busca")
