#!/usr/bin/env python3
import http.server
import socketserver
import ssl
import urllib.parse
import subprocess
import os
import re
import sqlite3
import time
import shlex
from datetime import datetime
from html import escape as html_escape

# Database configurations
DATABASES = [
    {"path": "data/wiki-small.db", "table": "sample_articles", "name": "wiki-small", "total_docs": 97549},
    {"path": "data/food-reviews.db", "table": "sample_articles", "name": "food-reviews", "total_docs": 568454}
]

APP_LOG_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "app_web.log")

def get_documents_by_ids(db_path, table_name, doc_ids):
    """Fetch full document content and title for a list of IDs."""
    try:
        conn = sqlite3.connect(db_path)
        cursor = conn.cursor()
        documents = []
        for doc_id in doc_ids:
            row = cursor.execute(
                f"SELECT article_text, title FROM {table_name} WHERE article_id = ?",
                (doc_id,),
            ).fetchone()
            if row:
                content, title = row
                documents.append(
                    {
                        "content": content if content else "(no content available)",
                        "title": title if title else "(untitled)",
                    }
                )
            else:
                documents.append(
                    {"content": "(no content available)", "title": "(untitled)"}
                )
        conn.close()
        return documents
    except Exception as e:
        print(f"Error fetching documents: {e}")
        return [{"content": "(error fetching content)", "title": "(untitled)"}] * len(
            doc_ids
        )

def perform_search(db_path, table_name, query, num_threads, max_documents, topK):
    if not query or len(query.strip()) == 0:
        return []

    cmd = [
        './app',
        '--db', db_path,
        '--table', table_name,
        '--query_user', query,
        '--nthreads', str(num_threads),
        '--k', str(topK),
        '--entries', str(max_documents)
    ]
    command_str = ' '.join(shlex.quote(part) for part in cmd)
    print(f"Executing: {command_str}")
    log_lines = [
        f"[{datetime.now().isoformat()}] COMMAND: {command_str}\n"
    ]

    try:
        # Execute command and capture output
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        output = result.stdout + result.stderr
        log_lines.append(output if output.endswith('\n') else output + '\n')
        log_lines.append(f"RETURN CODE: {result.returncode}\n\n")
        with open(APP_LOG_FILE, "a", encoding="utf-8") as log_file:
            log_file.writelines(log_lines)

        # Parse output to extract document IDs and scores
        # Format: [ID] score text...
        doc_ids = []
        scores = []

        for line in output.split('\n'):
            if not line.strip():
                continue

            # Look for [ID] pattern
            id_match = re.search(r'\[(\d+)\]', line)
            if id_match:
                doc_id = int(id_match.group(1))

                # Find score after the ID (skip ANSI codes)
                # Format: [4147] [36m0.181462[90m or just [4147] 0.181462
                score_part = line[id_match.end():]
                # Remove ANSI escape codes
                score_part = re.sub(r'\x1b\[[0-9;]*m', '', score_part)
                score_match = re.search(r'([\d.]+)', score_part)

                if score_match:
                    score = float(score_match.group(1))
                    doc_ids.append(doc_id)
                    scores.append(score)

                    if len(doc_ids) >= topK:
                        break

        if not doc_ids:
            print("No results found")
            return []

        print(f"Found {len(doc_ids)} results, fetching full content...")

        # Fetch full document content
        documents = get_documents_by_ids(db_path, table_name, doc_ids)

        # Build results
        results = []
        for i in range(len(doc_ids)):
            doc_info = documents[i] if i < len(documents) else {}
            results.append({
                'docId': doc_ids[i],
                'title': doc_info.get('title', '(untitled)'),
                'score': scores[i],
                'content': doc_info.get('content', '(no content available)')
            })

        return results

    except subprocess.TimeoutExpired:
        print("Search timed out")
        log_lines.append("ERROR: Search timed out\n\n")
        with open(APP_LOG_FILE, "a", encoding="utf-8") as log_file:
            log_file.writelines(log_lines)
        return []
    except Exception as e:
        print(f"Error executing search: {e}")
        log_lines.append(f"ERROR: {e}\n\n")
        with open(APP_LOG_FILE, "a", encoding="utf-8") as log_file:
            log_file.writelines(log_lines)
        return []

def generate_html(query, db_index, threads, topK, entries, results, search_time):
    query_escaped = query.replace('"', '&quot;').replace('<', '&lt;').replace('>', '&gt;') if query else ''

    html = f"""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>TF-IDF Document Search</title>
    <style>
        body {{ font-family: Arial, sans-serif; margin: 0; padding: 20px; display: flex; justify-content: center; }}
        .container {{ max-width: 900px; width: 100%; }}
        #searchInput {{ padding: 10px; font-size: 16px; width: 600px; }}
        #searchButton {{ padding: 10px 20px; font-size: 16px; cursor: pointer; }}
        .result {{ margin-bottom: 20px; border-bottom: 1px solid #ccc; padding-bottom: 10px; }}
        .result-content {{ margin: 5px 0; }}
        .result-preview {{ display: block; }}
        .result-full {{ display: none; }}
        .expand-btn {{ cursor: pointer; color: blue; text-decoration: underline; margin-top: 5px; display: inline-block; }}
    </style>
</head>
<body>
    <div class="container">
    <h1>TF-IDF Search</h1>
    <form id="searchForm">
        <input type="text" id="searchInput" name="q" placeholder="Search..." value="{query_escaped}">
        <button type="button" id="searchButton">Search</button>
        <input type="hidden" id="dbIndex" name="db" value="{db_index}">
        <input type="hidden" id="threadsInput" name="threads" value="{threads}">
        <input type="hidden" id="topkInput" name="topk" value="{topK}">
        <input type="hidden" id="entriesInput" name="entries" value="{entries}">
    </form>
    <p>
        Database: <select name="db" onchange="updateParam('db', this.value)">"""

    for i, db in enumerate(DATABASES):
        selected = ' selected' if i == db_index else ''
        html += f'<option value="{i}"{selected}>{db["name"]}</option>'

    html += f"""</select> |
        Threads: <input type="number" name="threads" value="{threads}" min="1" max="16" onchange="updateParam('threads', this.value)" size="4"> |
        Entries: <input type="number" name="entries" value="{entries}" min="100" max="100000000" step="100" onchange="updateParam('entries', this.value)" size="6"> |
        Results: <input type="number" name="topk" value="{topK}" min="1" max="100" onchange="updateParam('topk', this.value)" size="3">
    </p>
"""

    if query and results:
        html += f'    <p>About {len(results)} results ({search_time:.3f} seconds)</p>\n'
        for i, result in enumerate(results):
            content = html_escape(result['content'])
            title = html_escape(result.get('title', '(untitled)'))
            preview = content[:200] if len(content) > 200 else content
            has_more = len(content) > 200

            html_content = f"""    <div class="result">
        <div><strong>{title}</strong> | Doc ID: {result['docId']} | Score: {result['score']:.4f}</div>
        <div class="result-content">
            <span class="result-preview" id="preview{i}">{preview}{'...' if has_more else ''}</span>
            <span class="result-full" id="full{i}">{content}</span>
        </div>
"""
            if has_more:
                html_content += f'        <span class="expand-btn" onclick="toggleExpand({i})">Expandir</span>\n'
            html_content += '    </div>\n'
            html += html_content
    elif query:
        html += '    <p>No results found</p>\n'

    html += """    <hr>
    <p><small>TF-IDF Document Search System</small></p>
    </div>
    <script>
        // Search function
        function performSearch() {
            const query = document.getElementById('searchInput').value.trim();
            if (query) {
                const db = document.getElementById('dbIndex').value;
                const threads = document.getElementById('threadsInput').value;
                const topk = document.getElementById('topkInput').value;
                const entries = document.getElementById('entriesInput').value;
                window.location.href = 'search.html?q=' + encodeURIComponent(query) +
                    '&db=' + db + '&threads=' + threads + '&topk=' + topk + '&entries=' + entries;
            }
        }

        // Search on Enter key
        document.getElementById('searchInput').addEventListener('keypress', function(e) {
            if (e.key === 'Enter') performSearch();
        });

        // Search on button click
        document.getElementById('searchButton').addEventListener('click', performSearch);

        function updateParam(key, value) {
            const fieldMap = {
                'db': 'dbIndex',
                'threads': 'threadsInput',
                'entries': 'entriesInput',
                'topk': 'topkInput'
            };
            const hiddenId = fieldMap[key];
            if (hiddenId) {
                document.getElementById(hiddenId).value = value;
            }
        }

        // Expand/collapse results
        function toggleExpand(index) {
            const preview = document.getElementById('preview' + index);
            const full = document.getElementById('full' + index);
            const btn = event.target;
            if (preview.style.display !== 'none') {
                preview.style.display = 'none';
                full.style.display = 'block';
                btn.textContent = 'Colapsar';
            } else {
                preview.style.display = 'block';
                full.style.display = 'none';
                btn.textContent = 'Expandir';
            }
        }
    </script>
</body>
</html>
"""

    return html

class SearchHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        params = urllib.parse.parse_qs(parsed.query)

        # Handle search page (with or without query)
        if parsed.path == '/search.html' or parsed.path == '/':
            # Parse parameters with defaults
            query = params.get('q', [''])[0]

            # Safely parse integer parameters
            try:
                db_index = int(params.get('db', ['0'])[0])
            except ValueError:
                db_index = 0

            try:
                threads = int(params.get('threads', ['4'])[0])
            except ValueError:
                threads = 4

            try:
                topk = int(params.get('topk', ['10'])[0])
            except ValueError:
                topk = 10

            try:
                entries = int(params.get('entries', ['10000'])[0])
            except ValueError:
                entries = 10000

            # Validate parameters
            db_index = max(0, min(db_index, len(DATABASES) - 1))
            threads = max(1, min(threads, 16))
            topk = max(1, min(topk, 10))
            entries = max(100, min(entries, 100000))

            # Perform search only if query is provided
            results = []
            search_time = 0

            if query and query.strip():
                print(f"\nSearching: '{query}'")
                print(f"Database: {DATABASES[db_index]['name']}")
                print(f"Threads: {threads}, Top K: {topk}, Entries: {entries}")

                # Perform search
                start_time = time.time()
                results = perform_search(
                    DATABASES[db_index]['path'],
                    DATABASES[db_index]['table'],
                    query,
                    threads,
                    entries,
                    topk
                )
                search_time = time.time() - start_time

                print(f"Found {len(results)} results in {search_time:.3f}s\n")

            # Generate HTML (with or without results)
            html = generate_html(query, db_index, threads, topk, entries, results, search_time)

            # Send response
            self.send_response(200)
            self.send_header('Content-type', 'text/html; charset=utf-8')
            self.end_headers()
            self.wfile.write(html.encode('utf-8'))
        else:
            # Serve static files from web/ directory
            self.path = '/web' + self.path if not self.path.startswith('/web') else self.path
            super().do_GET()

if __name__ == '__main__':
    os.chdir(os.path.dirname(os.path.abspath(__file__)))

    print("\nTF-IDF Search Server")
    print("=" * 50)
    print("Server starting at https://saitoi.foo")
    print(f"Serving files from: {os.getcwd()}/web/\n")

    cert_path = "/etc/letsencrypt/live/saitoi.foo/fullchain.pem"
    key_path = "/etc/letsencrypt/live/saitoi.foo/privkey.pem"

    with socketserver.TCPServer(("", 443), SearchHandler) as httpd:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(certfile=cert_path, keyfile=key_path)
        httpd.socket = context.wrap_socket(httpd.socket, server_side=True)

        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\n\nServer shutting down...\n")
