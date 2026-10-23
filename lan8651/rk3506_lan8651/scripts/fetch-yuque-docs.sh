#!/bin/bash
# 抓取合众恒跃语雀知识库首页的目录（公开首页内嵌 window.appData）。
# 单篇文档正文需要登录，无法匿名抓取，故仅保存目录用于索引。
set -eu
OUT="${1:-$REPO_DIR/docs/reference/yuque-appdata.json}"
REPO_DIR="${REPO_DIR:-$(cd "$(dirname "$0")/.." && pwd)}"
URL="${YUQUE_URL:-https://hzhydocs.yuque.com/org-wiki-hzhydocs-rg5tcg/dn05eo}"

mkdir -p "$(dirname "$OUT")"
HTML="$(mktemp)"
curl -fsSL -A "Mozilla/5.0" "$URL" -o "$HTML"

python3 - "$HTML" "$OUT" <<'PY'
import re, sys, json, urllib.parse
html = open(sys.argv[1], encoding='utf-8', errors='replace').read()
m = re.search(r'window\.appData = JSON\.parse\(decodeURIComponent\("([^"]+)"\)\)', html)
if not m:
    print("未找到 appData，可能需要登录"); sys.exit(1)
data = json.loads(urllib.parse.unquote(m.group(1)))
json.dump(data, open(sys.argv[2], 'w'), ensure_ascii=False, indent=1)
toc = data.get('book', {}).get('toc', [])
print(f"book: {data.get('book',{}).get('name')}  文档数: {len(toc)}")
for it in toc:
    print(f"  - {it.get('title')}  ->  {it.get('url')}")
PY
echo "已保存：$OUT"
