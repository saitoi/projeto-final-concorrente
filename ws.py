#!/usr/bin/env -S uv run --script
#
# /// script
# requires-python = ">=3.13"
# dependencies = [
#     "fastapi",
#     "uvicorn",
# ]
# ///

from fastapi import FastAPI, HTTPException, Response, Cookie
from fastapi.responses import HTMLResponse
import subprocess
import sqlite3
import re
import shlex
import logging
import time
import secrets
from datetime import datetime, timedelta
from html import escape as html_escape

# Setup logging
logging.basicConfig(
    level=logging.INFO,
    format="%(levelname)s:     %(message)s",
)
logger = logging.getLogger("uvicorn")

app = FastAPI()

# Session management
SESSION_SECRET = secrets.token_hex(32)
SESSION_DURATION_HOURS = 24
active_sessions = {}  # session_token -> expiry_time

def create_session():
    """Create a new session token."""
    token = secrets.token_urlsafe(32)
    expiry = datetime.now() + timedelta(hours=SESSION_DURATION_HOURS)
    active_sessions[token] = expiry
    logger.info(f"Created new session (expires in {SESSION_DURATION_HOURS}h)")
    return token

def is_session_valid(session_token: str | None) -> bool:
    """Check if session token is valid and not expired."""
    if not session_token:
        return False

    if session_token not in active_sessions:
        return False

    expiry = active_sessions[session_token]
    if datetime.now() > expiry:
        # Session expired, remove it
        del active_sessions[session_token]
        logger.info("Session expired and removed")
        return False

    return True

def cleanup_expired_sessions():
    """Remove expired sessions."""
    now = datetime.now()
    expired = [token for token, expiry in active_sessions.items() if now > expiry]
    for token in expired:
        del active_sessions[token]
    if expired:
        logger.info(f"Cleaned up {len(expired)} expired sessions")

# Configuration
API_KEY = "abc123"
HOST = "0.0.0.0"
PORT = 8051
SSL_KEYFILE = "/etc/letsencrypt/live/saitoi.foo/privkey.pem"
SSL_CERTFILE = "/etc/letsencrypt/live/saitoi.foo/fullchain.pem"
APP_LOG_FILE = "app_web.log"

# Database configurations
DATABASES = {
    "wiki-small": {"path": "data/wiki-small.db", "table": "sample_articles"},
    "food-reviews": {"path": "data/food-reviews.db", "table": "sample_articles"}
}

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
                documents.append({
                    "content": content if content else "(no content available)",
                    "title": title if title else "(untitled)",
                })
            else:
                documents.append({
                    "content": "(no content available)",
                    "title": "(untitled)"
                })
        conn.close()
        return documents
    except Exception as e:
        logger.error(f"Error fetching documents: {e}")
        return [{"content": "(error fetching content)", "title": "(untitled)"}] * len(doc_ids)

def perform_search(db_path, table_name, query, num_threads, max_documents, topK):
    """Execute TF-IDF search using the C application."""
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
    logger.info(f"Executing: {command_str}")

    log_lines = [f"[{datetime.now().isoformat()}] COMMAND: {command_str}\n"]

    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        output = result.stdout + result.stderr
        log_lines.append(output if output.endswith('\n') else output + '\n')
        log_lines.append(f"RETURN CODE: {result.returncode}\n\n")

        with open(APP_LOG_FILE, "a", encoding="utf-8") as log_file:
            log_file.writelines(log_lines)

        # Parse output to extract document IDs and scores
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
                score_part = line[id_match.end():]
                score_part = re.sub(r'\x1b\[[0-9;]*m', '', score_part)
                score_match = re.search(r'([\d.]+)', score_part)

                if score_match:
                    score = float(score_match.group(1))
                    doc_ids.append(doc_id)
                    scores.append(score)

                    if len(doc_ids) >= topK:
                        break

        if not doc_ids:
            logger.info("No results found")
            return []

        logger.info(f"Found {len(doc_ids)} results, fetching full content...")

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
        logger.error("Search timed out")
        log_lines.append("ERROR: Search timed out\n\n")
        with open(APP_LOG_FILE, "a", encoding="utf-8") as log_file:
            log_file.writelines(log_lines)
        return []
    except Exception as e:
        logger.error(f"Error executing search: {e}")
        log_lines.append(f"ERROR: {e}\n\n")
        with open(APP_LOG_FILE, "a", encoding="utf-8") as log_file:
            log_file.writelines(log_lines)
        return []

def generate_html(query, db_name, threads, topK, entries, results, search_time):
    """Generate HTML page for search interface."""
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
    <h1>Multi-threaded C TF-IDF Search</h1>
    <form id="searchForm">
        <input id="searchInput" name="q" placeholder="Search..." value="{query_escaped}">
        <button type="button" id="searchButton">Search</button>
        <input type="hidden" id="dbInput" name="db" value="{db_name}">
        <input type="hidden" id="threadsInput" name="threads" value="{threads}">
        <input type="hidden" id="topkInput" name="topk" value="{topK}">
        <input type="hidden" id="entriesInput" name="entries" value="{entries}">
    </form>
    <p>
        Database: <select name="db" onchange="updateParam('db', this.value)">"""

    for db_key in DATABASES.keys():
        selected = ' selected' if db_key == db_name else ''
        html += f'<option value="{db_key}"{selected}>{db_key}</option>'

    html += f"""</select> |
        Threads: <input type="number" name="threads" value="{threads}" min="1" max="16" onchange="updateParam('threads', this.value)" size="4"> |
        Entries: <input type="number" name="entries" value="{entries}" min="100" max="100000000" step="100" onchange="updateParam('entries', this.value)" size="6"> |
        Results: <input type="number" name="topk" value="{topK}" min="1" max="200" onchange="updateParam('topk', this.value)" size="3">
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
    <p><small>TF-IDF Document Search System | <a href="/logout">Logout</a></small></p>
    </div>
    <script>
        function performSearch() {
            const query = document.getElementById('searchInput').value.trim();
            if (query) {
                const db = document.getElementById('dbInput').value;
                const threads = document.getElementById('threadsInput').value;
                const topk = document.getElementById('topkInput').value;
                const entries = document.getElementById('entriesInput').value;
                window.location.href = '/search?q=' + encodeURIComponent(query) +
                    '&db=' + db + '&threads=' + threads + '&topk=' + topk + '&entries=' + entries;
            }
        }

        document.getElementById('searchInput').addEventListener('keypress', function(e) {
            if (e.key === 'Enter') performSearch();
        });

        document.getElementById('searchButton').addEventListener('click', performSearch);

        function updateParam(key, value) {
            const fieldMap = {
                'db': 'dbInput',
                'threads': 'threadsInput',
                'entries': 'entriesInput',
                'topk': 'topkInput'
            };
            const hiddenId = fieldMap[key];
            if (hiddenId) {
                document.getElementById(hiddenId).value = value;
            }
        }

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

@app.get("/", response_class=HTMLResponse)
@app.get("/tf_idf/search", response_class=HTMLResponse)
def search_interface(
    response: Response,
    key: str | None = None,
    q: str = "",
    db: str = "wiki-small",
    threads: int = 4,
    topk: int = 10,
    entries: int = 10000,
    session_token: str | None = Cookie(None)
):
    """HTML search interface - requires key on first access, then uses session."""
    # Cleanup expired sessions periodically
    cleanup_expired_sessions()

    # Check authentication: valid session OR valid API key
    authenticated = is_session_valid(session_token)

    if not authenticated:
        # No valid session, check if key is provided
        if key == API_KEY:
            # Valid key provided, create new session
            new_token = create_session()
            response.set_cookie(
                key="session_token",
                value=new_token,
                max_age=SESSION_DURATION_HOURS * 3600,
                httponly=True,
                secure=True,
                samesite="lax"
            )
            authenticated = True
            logger.info("New session created via API key")
        elif key is not None:
            # Key provided but invalid
            logger.warning("Invalid API key attempt")
            return HTMLResponse(
                content="""
                <!DOCTYPE html>
                <html>
                <head><title>Unauthorized</title></head>
                <body style="font-family: Arial; text-align: center; padding: 50px;">
                    <h1>Invalid API Key</h1>
                    <p>The provided API key is incorrect.</p>
                    <p><a href="/">Try again</a></p>
                </body>
                </html>
                """,
                status_code=401
            )
        else:
            # No key and no valid session - show login page
            return HTMLResponse(
                content="""
                <!DOCTYPE html>
                <html>
                <head>
                    <meta charset="UTF-8">
                    <title>TF-IDF Search - Login</title>
                    <style>
                        body { font-family: Arial; display: flex; text-align: center; justify-content: center; align-items: center; height: 100vh; margin: 0; }
                        input { padding: 10px; font-size: 16px; width: 300px; margin: 10px 0; }
                        .container { display: flex; flex-direction: column; align-items: center; }
                    </style>
                </head>
                <body>
                    <div class="container">
                        <h1>TF-IDF Document Search</h1>
                        <p>Password to access the search interface</p>
                        <form method="GET" action="/">
                            <input type="password" name="key" placeholder="API Key" required>
                            <br>
                            <button type="submit">Login</button>
                        </form>
                    </div>
                </body>
                </html>
                """
            )

    # User is authenticated, proceed with search
    if db not in DATABASES:
        db = "wiki-small"
    threads = max(1, min(threads, 16))
    topk = max(1, min(topk, 200))
    entries = max(100, min(entries, 100000000))

    results = []
    search_time = 0

    if q and q.strip():
        logger.info(f"HTML Search: '{q}' | DB: {db} | Threads: {threads} | TopK: {topk} | Entries: {entries}")

        start_time = time.time()
        db_config = DATABASES[db]
        results = perform_search(
            db_config["path"],
            db_config["table"],
            q,
            threads,
            entries,
            topk
        )
        search_time = time.time() - start_time

        logger.info(f"Found {len(results)} results in {search_time:.3f}s")

    return generate_html(q, db, threads, topk, entries, results, search_time)

@app.get("/logout", response_class=HTMLResponse)
def logout(
    response: Response,
    session_token: str | None = Cookie(None)
):
    """Logout endpoint - invalidates the session."""
    if session_token and session_token in active_sessions:
        del active_sessions[session_token]
        logger.info("User logged out, session invalidated")

    # Clear the cookie
    response.delete_cookie(key="session_token")

    return HTMLResponse(
        content="""
        <!DOCTYPE html>
        <html>
        <head>
            <meta charset="UTF-8">
            <title>Logged Out</title>
            <style>
                body { font-family: Arial; display: flex; justify-content: center; align-items: center; height: 150vh; margin: 0; background: #f0f0f0; }
                .box { background: white; padding: 40px; text-align: center; }
                a { color: #007bff; text-decoration: none; }
            </style>
        </head>
        <body>
            <div class="box">
                <h1>Logged Out</h1>
                <p>Bye bye.</p>
                <p><a href="/">Login again</a></p>
            </div>
        </body>
        </html>
        """
    )

@app.get("/tf_idf/api")
def tf_idf_search(
    key: str | None = None,
    q: str | None = None,
    db: str = "wiki-small",
    threads: int = 4,
    topk: int = 10,
    entries: int = 10000
):
    """
    TF-IDF search endpoint

    Parameters:
    - key: API key for authentication (required)
    - q: Search query (required)
    - db: Database name ("wiki-small" or "food-reviews", default: "wiki-small")
    - threads: Number of threads (1-16, default: 4)
    - topk: Number of top results to return (1-200, default: 10)
    - entries: Max documents to process (100-100000000, default: 10000)
    """

    # Autenticação
    if key != API_KEY:
        logger.warning("Unauthorized access attempt")
        raise HTTPException(status_code=401, detail="Unauthorized")

    if not q or not q.strip():
        raise HTTPException(status_code=400, detail="Parameter 'q' (query) is required")

    # Validate database
    if db not in DATABASES:
        raise HTTPException(
            status_code=400,
            detail=f"Invalid database. Choose from: {', '.join(DATABASES.keys())}"
        )

    # Validate and clamp parameters
    threads = max(1, min(threads, 16))
    topk = max(1, min(topk, 200))
    entries = max(100, min(entries, 100000000))

    db_config = DATABASES[db]

    logger.info(f"Search: '{q}' | DB: {db} | Threads: {threads} | TopK: {topk} | Entries: {entries}")

    # Perform search
    results = perform_search(
        db_config["path"],
        db_config["table"],
        q,
        threads,
        entries,
        topk
    )

    logger.info(f"Returning {len(results)} results")

    return {
        "query": q,
        "database": db,
        "threads": threads,
        "topk": topk,
        "entries": entries,
        "results": results,
        "count": len(results)
    }

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(
        "ws:app",
        host=HOST,
        port=PORT,
        ssl_keyfile=SSL_KEYFILE,
        ssl_certfile=SSL_CERTFILE,
    )
