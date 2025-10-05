from sample_articles
select regexp_replace(strip_accents(article_text), '[^\x20-\x7E]+', '', 'g') printable_ascii;