# AtCoder ABC Discord Reminder

AtCoder公式の予定コンテスト表から次回のAtCoder Beginner Contest（ABC）を取得し、開始2時間前に固定のDiscordチャンネルへ通知するC++17の常駐サービスです。実行機はUbuntu Serverを採用したDynabook、配置先は`/srv/shared/remider`（Windows側の`Y:\remider`）を前提にしています。

## 1. 動作仕様

- `https://atcoder.jp/contests/?lang=ja`をHTTPSで取得する
- `#contest-table-upcoming`内のURLが`/contests/abc数字`の行だけを対象にする
- 開始日時と開催時間から終了日時を算出する
- 開始2時間前に次の本文を送信する

```text
# AtCoder Beginner Contest {N}

本日 {START} ～ {END} に [AtCoder Beginner Contest {N}](https://atcoder.jp/contests/abc{N}) が開催されます。

皆さんぜひ参加しましょう！🔥
```

通知状態はSQLiteへ保存します。送信途中の停止や通信失敗があっても、保留状態から再試行します。Wi-Fi接続状態の確認は行わず、HTTP要求の成否だけを扱います。

## 2. 構成と配置先

ソースは本リポジトリで管理し、サーバーへはビルド済み実行ファイルとsystemd定義だけを転送します。

```text
開発PC / WSL
  └─ kyopro_reminderbot/
       └─ build/atcoder-abc-reminder
             │ SSH/SCP
             v
ryutoserver
  └─ /srv/shared/remider/
       ├─ bin/atcoder-abc-reminder
       └─ state.db

/etc/atcoder-abc-reminder/atcoder-abc-reminder.env  # Botトークン等
/etc/systemd/system/atcoder-abc-reminder.service   # サービス定義
```

`state.db`は通常のデプロイで上書きしません。BotトークンはGit、ソース、`/srv/shared/remider`へ保存しません。

## 3. ローカル環境の準備とビルド

UbuntuまたはWSLで、プロジェクトディレクトリへ移動します。

```bash
cd /mnt/c/Users/ryuto/mcc/kyopro_reminderbot
```

必要なパッケージをAPTで導入します。いずれも無料の標準パッケージです。

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config \
  libcurl4-openssl-dev libxml2-dev libsqlite3-dev nlohmann-json3-dev
```

ビルドとテストを実行します。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

生成物:

```text
build/atcoder-abc-reminder
build/reminder_tests
```

AtCoderのHTMLを保存している場合は、実ページ形式の解析も確認できます。

```bash
curl --fail --silent --show-error \
  'https://atcoder.jp/contests/?lang=ja' \
  -o /tmp/atcoder_contests.html
build/reminder_tests /tmp/atcoder_contests.html
```

## 4. Discord Botの準備

1. [Discord Developer Portal](https://discord.com/developers/applications)で、トークンを発行したアプリケーションを開く。
2. `OAuth2` → `URL Generator`を開く。
3. Scopeで`bot`を選択する。
4. Bot権限として次を選択する。
   - View Channels
   - Send Messages
   - Embed Links
   - Read Message History
5. 生成されたURLから、通知先サーバーへBotを追加する。
6. 通知先チャンネルの個別権限でも、Botに同じ権限を許可する。
7. Discordの開発者モードを有効にし、通知先チャンネルのIDをコピーする。

Botトークンはパスワードと同じ扱いにし、チャットやGitへ貼り付けません。環境ファイルには`Bot `を付けず、トークン本体だけを設定します。

## 5. サーバー初回セットアップ

以下はSSH先`ryutoserver`で実行します。

サービスユーザーを作成し、実行ディレクトリを準備します。

```bash
getent passwd abc-reminder || sudo useradd --system \
  --home-dir /srv/shared/remider --no-create-home \
  --shell /usr/sbin/nologin abc-reminder

sudo install -d -o abc-reminder -g abc-reminder -m 750 /srv/shared/remider
sudo install -d -o root -g root -m 755 /srv/shared/remider/bin
```

`/srv/shared`の親ディレクトリにサービスユーザーの通過権限がない環境では、次を設定します。読み取り権限ではなく、ディレクトリを通過する権限だけを追加します。

```bash
sudo chmod o+x /srv/shared
```

環境ファイルを作成します。

```bash
sudo install -d -m 755 /etc/atcoder-abc-reminder
sudo touch /etc/atcoder-abc-reminder/atcoder-abc-reminder.env
sudo chown root:root /etc/atcoder-abc-reminder/atcoder-abc-reminder.env
sudo chmod 600 /etc/atcoder-abc-reminder/atcoder-abc-reminder.env
sudoedit /etc/atcoder-abc-reminder/atcoder-abc-reminder.env
```

内容:

```env
ATCODER_DISCORD_BOT_TOKEN=Botページで発行したトークン本体
ATCODER_DISCORD_CHANNEL_ID=通知先チャンネルの数値ID
```

## 6. ビルド成果物とサービス定義の転送

以下はSSH接続を抜けた状態のPowerShellで実行します。`scp`は送信元と送信先の両方が必要です。

```powershell
scp "C:\Users\ryuto\mcc\kyopro_reminderbot\build\atcoder-abc-reminder" administer@ryutoserver:/tmp/atcoder-abc-reminder
scp "C:\Users\ryuto\mcc\kyopro_reminderbot\deploy\atcoder-abc-reminder.service" administer@ryutoserver:/tmp/atcoder-abc-reminder.service
```

SSH先で転送を確認し、配置します。

```bash
ls -l /tmp/atcoder-abc-reminder /tmp/atcoder-abc-reminder.service
sudo install -o root -g root -m 755 /tmp/atcoder-abc-reminder /srv/shared/remider/bin/atcoder-abc-reminder
sudo install -o root -g root -m 644 /tmp/atcoder-abc-reminder.service /etc/systemd/system/atcoder-abc-reminder.service
```

サービス定義を読み込み、自動起動を有効にします。

```bash
sudo systemctl daemon-reload
sudo systemctl enable atcoder-abc-reminder
```

## 7. 起動確認

```bash
sudo systemctl restart atcoder-abc-reminder
sudo systemctl status atcoder-abc-reminder
sudo systemctl is-active atcoder-abc-reminder
```

次の状態なら常駐に成功しています。

```text
Active: active (running)
active
```

ログ確認:

```bash
sudo journalctl -u atcoder-abc-reminder -n 50 --no-pager
```

初回起動時は`STATE_LOAD_OK`、新しい予定を選択した場合は`CONTEST_SELECTED`が記録されます。通知時刻前にDiscordへ送信しないのは正常です。

## 8. 送信テスト

### 8.1 トークン認証だけを確認する

次の確認はメッセージを送信しません。`HTTP 200`ならBotトークンは有効です。

```bash
sudo bash -c '
set -a
. /etc/atcoder-abc-reminder/atcoder-abc-reminder.env
set +a
printf "Authorization: Bot %s\n" "${ATCODER_DISCORD_BOT_TOKEN}" | \
curl --silent --show-error -o /dev/null -w "HTTP %{http_code}\n" \
  --header @- \
  https://discord.com/api/v10/users/@me
'
```

### 8.2 Discordへテスト通知を送る

次のコマンドは、設定済みチャンネルへ実際に1件送信します。SQLiteの状態は変更しません。

```bash
sudo bash -c '
set -a
. /etc/atcoder-abc-reminder/atcoder-abc-reminder.env
set +a
printf "Authorization: Bot %s\n" "${ATCODER_DISCORD_BOT_TOKEN}" | \
curl --fail-with-body --silent --show-error \
  -X POST \
  --header @- \
  -H "Content-Type: application/json" \
  "https://discord.com/api/v10/channels/${ATCODER_DISCORD_CHANNEL_ID}/messages" \
  --data-raw "{\"content\":\"# AtCoder Beginner Contest 476\\n\\n本日 21:00 ～ 22:40 に [AtCoder Beginner Contest 476](https://atcoder.jp/contests/abc476) が開催されます。\\n\\n皆さんぜひ参加しましょう！🔥\",\"allowed_mentions\":{\"parse\":[]},\"nonce\":\"manual-test:$(date +%s)\",\"enforce_nonce\":true}"
'
```

結果の目安:

| HTTP | 意味 |
|---:|---|
| 200 | トークン認証成功（認証確認API） |
| 401 | トークンが無効、または余計な文字が混入 |
| 403 / Missing Access | Botがサーバー・チャンネルへアクセスできない |
| 403 / Missing Permissions | Botに送信権限がない |
| 404 | チャンネルIDが誤っている、またはBotから見えない |
| 429 / 5xx | 一時的なエラー。サービスが再試行する |

## 9. 障害対応

| 状態 | 確認・対処 |
|---|---|
| `Unit ... not found` | `/etc/systemd/system/atcoder-abc-reminder.service`へ配置し、`sudo systemctl daemon-reload`を実行 |
| `217/USER` | `abc-reminder`ユーザーを作成 |
| `200/CHDIR` | `/srv/shared/remider`の存在と、`/srv/shared`の通過権限を確認 |
| `203/EXEC` | `/srv/shared/remider/bin/atcoder-abc-reminder`の存在・実行権限・共有ライブラリを確認 |
| `401` | Botページのトークン本体を再設定。`Bot `は環境ファイルへ書かない |
| `403 Missing Access` | Botのサーバー参加、チャンネルID、チャンネル閲覧権限を確認 |
| `403 Missing Permissions` | View Channels、Send Messages、Embed Links、Read Message Historyを確認 |

詳細ログ:

```bash
sudo journalctl -u atcoder-abc-reminder --since "10 minutes ago" --no-pager
```

サービスが停止している場合:

```bash
sudo systemctl restart atcoder-abc-reminder
sudo systemctl is-active atcoder-abc-reminder
```

## 10. 更新とバックアップ

通常更新では、ビルド済み実行ファイルを`/tmp`へ転送し、ハッシュと実行権限を確認してから切り替えます。`state.db`は転送対象にしません。

SQLiteのバックアップを取得する場合:

```bash
sudo systemctl stop atcoder-abc-reminder
sudo cp -a /srv/shared/remider/state.db /srv/shared/remider/state.db.backup
sudo systemctl start atcoder-abc-reminder
```

詳細な状態遷移、SQLiteスキーマ、セキュリティ設計は[`docs/atcoder_abc_discord_reminder_detailed_design.md`](docs/atcoder_abc_discord_reminder_detailed_design.md)を参照してください。
