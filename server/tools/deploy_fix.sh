#!/bin/sh
# ============================================================================
#  tools/deploy_fix.sh  -  アップロード後に毎回実行する後処理 (ロリポップSSH)
# ----------------------------------------------------------------------------
#  ファイルを再アップロードすると public/api/*.py の shebang(1行目) が
#  #!/usr/bin/env python3 に戻り、実行権限も外れて CGI が動かなくなる。
#  このスクリプトで shebang を venv の python に固定し、実行権限を付け直す。
#
#  使い方 (server ディレクトリ直下で):  sh tools/deploy_fix.sh
# ============================================================================
set -e
DIR="$(cd "$(dirname "$0")/.." && pwd)"     # server/ 直下
VENV_PY="$DIR/venv/bin/python3"

if [ ! -x "$VENV_PY" ]; then
  echo "venv が見つかりません: $VENV_PY"
  echo "先に  python3 -m venv venv && ./venv/bin/pip install -r requirements.txt  を実行してください。"
  exit 1
fi

for f in "$DIR"/public/api/*.py "$DIR"/public/webapi/*.py "$DIR"/tools/seed_admin.py; do
  [ -f "$f" ] || continue
  sed -i "1s|.*|#!$VENV_PY|" "$f"
done
chmod 705 "$DIR"/public/api/*.py "$DIR"/public/webapi/*.py

echo "OK: shebang を $VENV_PY に固定し、public/api/*.py と public/webapi/*.py に実行権限(705)を付与しました。"
echo "確認: curl -sS https://your-domain.example/public/api/session.py"
