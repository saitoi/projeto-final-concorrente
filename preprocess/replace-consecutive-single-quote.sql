select article_text, regexp_replace(article_text, '''{2,}', ' ', 'g') as fixed
from sample_articles
where not regexp_matches(article_text, '^[^'']*''[^'']*$')
limit 10;