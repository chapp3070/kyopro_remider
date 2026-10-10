# AtCoder ABC Discord Reminder

AtCoder公式の予定コンテスト表から次回のAtCoder Beginner Contest（ABC）を取得し、開始2時間前にDiscordへ通知するC++17製の常駐サービスです。

通知は次の形式です。`{ROLE_ID}`にはDiscordの`競プロ`ロールIDが入ります。

```text
<@&{ROLE_ID}>

# AtCoder Beginner Contest {N}

本日 {START} ～ {END} に [AtCoder Beginner Contest {N}](https://atcoder.jp/contests/abc{N}) が開催されます。

皆さんぜひ参加しましょう！🔥
```

Discord APIには、設定されたロールだけをメンション対象として渡します。全体メンションや、設定外のロール・ユーザーメンションは許可しません。

## 0. セットアップの全体像

このシステムは、次の2台を使って構築します。

| 役割 | 作業内容 |
|---|---|
| 開発PC / WSL | GitHubからclone、依存パッケージの導入、ビルド、テスト、ファイル転送 |
| 対象サーバー | 実行ユーザー・配置先・環境ファイル・systemdサービスの設定、常駐実行 |

作業の順番は次のとおりです。

1. 開発PC / WSLへリポジトリをcloneする
2. 開発PC / WSLで依存パッケージを導入し、ビルドとテストを行う
3. Discord Webhook、通知チャンネル、`競プロ`ロールを準備する
4. 対象サーバーに実行ユーザー、配置先、環境ファイルを準備する
5. ビルド成果物とsystemd定義を対象サーバーへ転送する
6. systemdサービスを有効化・起動する
7. Discordへの認証テストとロールメンション付き送信テストを行う

以降の見出しに`開発PC / WSL`または`対象サーバー`と書かれている場合は、その環境で実行してください。SSH接続中に実行するコマンドと、SSH接続を抜けてから実行するコマンドを分けています。

## 1. GitHubからリポジトリを取得

### 1.1 開発PC / WSLでGitを準備

Gitが未導入の場合だけ、次を実行します。

```bash
sudo apt update
sudo apt install -y git
```

### 1.2 cloneとブランチ確認

開発PC / WSLで、ソースを置きたい親ディレクトリへ移動してcloneします。

```bash
cd /path/to/source-directory
REPOSITORY_URL="https://github.com/chapp3070/kyopro_remider.git"
git clone "$REPOSITORY_URL" kyopro_reminderbot
cd kyopro_reminderbot
git status
git branch --show-current
```

`/path/to/source-directory`は実際にソースを保存する場所へ変更してください。別のフォークを使う場合は`REPOSITORY_URL`をそのリポジトリのURLへ変更します。通常は`main`ブランチが表示されます。

すでにclone済みの場合は、新しいディレクトリを作らず、既存のプロジェクトディレクトリで次を実行します。

```bash
cd /path/to/kyopro_reminderbot
git pull --ff-only origin main
```

GitHubリポジトリが非公開の場合は、HTTPS認証またはSSH鍵を設定してからcloneしてください。認証情報やDiscord Webhook URLをリポジトリURLへ埋め込まないでください。

## 2. 動作仕様

- AtCoder公式ページ`https://atcoder.jp/contests/?lang=ja`をHTTPSで取得する
- `#contest-table-upcoming`内のURLが`/contests/abc数字`の行だけを対象にする
- 開始日時と開催時間から終了日時を算出する
- 開始2時間前にDiscordへ通知する
- 毎週土曜日19時（日本時間）に当日のABC開催予定を再確認し、開催予定がない場合は設定済みロールへ「今日ABCは開催されません。」と通知する
- 土曜日の確認結果をSQLiteへ保存し、同じ日の通知を重複させない
- 開催日時の変更を検知し、未送信なら新しい時刻で通知する
- 送信状態をSQLiteへ保存し、再起動後も重複送信を防ぐ
- Webhook送信が成功した場合はDiscordのメッセージIDをSQLiteへ保存する
- 送信結果が不明な通信失敗では未送信扱いのまま再試行する
- Wi-Fi接続状態は確認せず、HTTP要求の成否を扱う

## 3. 現在の構成

ソースコードはGitで管理し、実行対象のUbuntu Serverなどへビルド済み実行ファイルを転送します。現在のサービス定義は、次の配置を前提にしています。

```text
開発PC / WSL
  └─ プロジェクトディレクトリ/
       ├─ build/atcoder-abc-reminder
       └─ deploy/atcoder-abc-reminder.service
             │ SSH/SCP
             v
対象サーバー
  └─ /srv/shared/remider/
       ├─ bin/atcoder-abc-reminder
       └─ state.db

/etc/atcoder-abc-reminder/atcoder-abc-reminder.env
/etc/systemd/system/atcoder-abc-reminder.service
```

`remider`は現在の配置先に合わせたディレクトリ名です。`reminder`など別の名前へ変更する場合は、サービス定義の`ExecStart`、`WorkingDirectory`、`ReadWritePaths`、環境ファイルの`ATCODER_STATE_DB`、および以下のコマンドをすべて同じパスへ変更してください。

通常の更新では`state.db`を上書きしません。Discord Webhook URLをGit、ソースコード、ビルド成果物、`/srv/shared/remider`へ保存しないでください。

## 4. 作業前に変更する箇所

環境ごとに、次の値を変更します。

| 項目 | 例 | 変更が必要な場合 |
|---|---|---|
| プロジェクトディレクトリ | `/path/to/kyopro_reminderbot` | ソースを置く場所が異なる場合 |
| SSH接続先 | `user@server.example` | 接続先・SSHユーザーが異なる場合 |
| アプリ配置先 | `/srv/shared/remider` | サーバー上の配置先を変える場合。サービス定義も変更 |
| サービス名 | `atcoder-abc-reminder` | systemdサービス名を変える場合。ファイル名・コマンドも変更 |
| Discord Webhook URL | 通知先チャンネルで発行したWebhook URL | 必ず環境ファイルへ設定 |
| Discord Role ID | `競プロ`ロールの数値ID | メンション対象ロールを変える場合 |

以降のコマンドは、現在の構成である`/srv/shared/remider`とサービス名`atcoder-abc-reminder`を使用しています。別の値にする場合は、表の項目に合わせて読み替えてください。

## 5. ローカル環境の準備とビルド

Ubuntu、Debian、またはWSLなど、CMakeを実行できる開発環境で行います。

```bash
cd /path/to/kyopro_reminderbot
```

上のパスは実際のプロジェクトディレクトリへ変更してください。

必要なパッケージをインストールします。いずれも無料で利用できる標準パッケージです。

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

生成物は次の2つです。

```text
build/atcoder-abc-reminder
build/reminder_tests
```

AtCoder実ページ形式の解析も確認する場合は、次を実行します。

```bash
curl --fail --silent --show-error \
  'https://atcoder.jp/contests/?lang=ja' \
  -o /tmp/atcoder_contests.html
build/reminder_tests /tmp/atcoder_contests.html
```

## 6. Discord Webhookの準備

1. Discordで通知先チャンネルを開く。
2. チャンネルの設定から`連携サービス`または`Integrations`を開く。
3. `Webhook`を作成し、名前を`AtCoder ABC Reminder`などにする。
4. 通知先チャンネルが正しいことを確認し、Webhook URLをコピーする。
5. Discordの開発者モードを有効にし、`競プロ`ロールのIDをコピーする。
6. `競プロ`ロールをメンション可能にする。

Webhook URLはパスワードと同じ扱いにしてください。URLを知っている人はそのチャンネルへ投稿できます。チャット、README、Gitの履歴、コマンドライン引数へ貼り付けないでください。

## 7. サーバー初回セットアップ

以下はSSH接続した対象サーバー上で実行します。

### 7.1 配置先とサービスユーザーの作成

```bash
APP_ROOT="/srv/shared/remider"
SERVICE_USER="abc-reminder"

getent passwd "$SERVICE_USER" || sudo useradd --system \
  --home-dir "$APP_ROOT" --no-create-home \
  --shell /usr/sbin/nologin "$SERVICE_USER"

sudo install -d -o "$SERVICE_USER" -g "$SERVICE_USER" -m 750 "$APP_ROOT"
sudo install -d -o root -g root -m 755 "$APP_ROOT/bin"
```

親ディレクトリをサービスユーザーが通過できない場合だけ、権限を確認して対応します。

```bash
namei -l /srv/shared/remider
sudo chmod o+x /srv/shared
```

`chmod o+x`は`/srv/shared`の中身を読み取れるようにするものではなく、ディレクトリを通過する権限だけを追加します。より厳しい権限管理が必要な環境では、ACLなどの運用ルールに合わせて設定してください。

### 7.2 環境ファイルの作成

```bash
sudo install -d -m 755 /etc/atcoder-abc-reminder
sudo install -o root -g root -m 600 /dev/null \
  /etc/atcoder-abc-reminder/atcoder-abc-reminder.env
sudoedit /etc/atcoder-abc-reminder/atcoder-abc-reminder.env
```

内容は次のとおりです。

```env
ATCODER_DISCORD_WEBHOOK_URL=https://discord.com/api/webhooks/...
ATCODER_DISCORD_ROLE_ID=競プロロールの数値ID
# 任意。省略時は/srv/shared/remider/state.db
# ATCODER_STATE_DB=/srv/shared/remider/state.db
```

保存後に権限を確認します。

```bash
sudo chown root:root /etc/atcoder-abc-reminder/atcoder-abc-reminder.env
sudo chmod 600 /etc/atcoder-abc-reminder/atcoder-abc-reminder.env
```

### 7.3 サービス定義の確認

リポジトリの`deploy/atcoder-abc-reminder.service`は、次を固定値として使用します。

```ini
ExecStart=/srv/shared/remider/bin/atcoder-abc-reminder
EnvironmentFile=/etc/atcoder-abc-reminder/atcoder-abc-reminder.env
User=abc-reminder
Group=abc-reminder
WorkingDirectory=/srv/shared/remider
```

配置先を変更した場合は、サービス定義のパスも変更してから転送してください。

## 8. 初回デプロイ

### 8.1 開発PCからファイルを転送

SSH接続中の場合は、いったん`exit`でサーバーから抜けます。開発PC側で次の変数を実際の値へ変更して実行します。

```bash
PROJECT_DIR="/path/to/kyopro_reminderbot"
REMOTE="your-ssh-user@your-server.example"

cd "$PROJECT_DIR"
scp "build/atcoder-abc-reminder" \
  "$REMOTE:/tmp/atcoder-abc-reminder"
scp "deploy/atcoder-abc-reminder.service" \
  "$REMOTE:/tmp/atcoder-abc-reminder.service"
```

SSHホスト鍵が未登録の場合は、指紋を確認してから接続してください。ホスト鍵検証を無効にしたまま転送しないでください。

### 8.2 サーバーへ配置

SSH接続後、転送されたファイルを確認して配置します。

```bash
APP_ROOT="/srv/shared/remider"

ls -l /tmp/atcoder-abc-reminder /tmp/atcoder-abc-reminder.service
sha256sum /tmp/atcoder-abc-reminder

sudo install -o root -g root -m 755 \
  /tmp/atcoder-abc-reminder \
  "$APP_ROOT/bin/atcoder-abc-reminder"
sudo install -o root -g root -m 644 \
  /tmp/atcoder-abc-reminder.service \
  /etc/systemd/system/atcoder-abc-reminder.service
```

### 8.3 systemdの有効化と起動

```bash
sudo systemctl daemon-reload
sudo systemctl enable atcoder-abc-reminder
sudo systemctl restart atcoder-abc-reminder
sudo systemctl is-active atcoder-abc-reminder
sudo systemctl status atcoder-abc-reminder --no-pager
```

次の状態なら常駐に成功しています。

```text
active
Active: active (running)
```

ログを確認します。

```bash
sudo journalctl -u atcoder-abc-reminder -n 50 --no-pager
```

初回起動時の`STATE_LOAD_OK`または`STATE_EMPTY`は正常です。新しい予定を選択した場合は`CONTEST_SELECTED`が記録されます。通知時刻前にDiscordへ送信しないのも正常です。

## 9. 更新デプロイ

ソースを更新した後、開発PCでビルドとテストを実行します。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

実行ファイルだけを一時ファイルとして転送し、サーバー側で確認してから置き換えます。`state.db`は転送しません。

```bash
REMOTE="your-ssh-user@your-server.example"
scp build/atcoder-abc-reminder \
  "$REMOTE:/tmp/atcoder-abc-reminder.new"
```

サーバー側:

```bash
APP_ROOT="/srv/shared/remider"

sha256sum /tmp/atcoder-abc-reminder.new
sudo install -o root -g root -m 755 \
  /tmp/atcoder-abc-reminder.new \
  "$APP_ROOT/bin/atcoder-abc-reminder"
sudo systemctl restart atcoder-abc-reminder
sudo systemctl is-active atcoder-abc-reminder
sudo journalctl -u atcoder-abc-reminder -n 50 --no-pager
```

## 10. 送信テスト

### 10.1 Webhook URLの確認

次のコマンドはメッセージを送信しません。`HTTP 200`ならWebhook URLが有効です。

```bash
sudo bash -c '
set -a
. /etc/atcoder-abc-reminder/atcoder-abc-reminder.env
set +a
curl --silent --show-error -o /dev/null -w "HTTP %{http_code}\n" \
  "${ATCODER_DISCORD_WEBHOOK_URL}"
'
```

### 10.2 ロールメンション付きテスト通知

次のコマンドは設定済みチャンネルへ実際に1件送信します。SQLiteの状態は変更しません。

```bash
sudo bash -c '
set -a
. /etc/atcoder-abc-reminder/atcoder-abc-reminder.env
set +a
payload=$(cat <<JSON
{"content":"<@&${ATCODER_DISCORD_ROLE_ID}>\\n\\n# AtCoder Beginner Contest 476\\n\\n本日 21:00 ～ 22:40 に [AtCoder Beginner Contest 476](https://atcoder.jp/contests/abc476) が開催されます。\\n\\n皆さんぜひ参加しましょう！🔥","allowed_mentions":{"parse":[],"roles":["${ATCODER_DISCORD_ROLE_ID}"]},"username":"AtCoder ABC Reminder"}
JSON
)
separator="?"
case "$ATCODER_DISCORD_WEBHOOK_URL" in
  *\?*) separator="&" ;;
esac
curl --fail-with-body --silent --show-error \
  -X POST \
  -H "Content-Type: application/json" \
  -w "\nHTTP %{http_code}\n" \
  "${ATCODER_DISCORD_WEBHOOK_URL}${separator}wait=true" \
  --data-raw "$payload"
'
```

成功時は`HTTP 200`とDiscordメッセージのJSONが返ります。Discord上で先頭に`@競プロ`が表示され、その下に本文が表示されれば成功です。

## 11. 障害対応

| 状態 | 確認・対処 |
|---|---|
| `Unit ... not found` | サービス定義を`/etc/systemd/system/atcoder-abc-reminder.service`へ配置し、`sudo systemctl daemon-reload`を実行 |
| `217/USER` | `abc-reminder`ユーザーが存在するか確認 |
| `200/CHDIR` | `WorkingDirectory`の存在と、親ディレクトリの通過権限を確認 |
| `203/EXEC` | `ExecStart`のパス、実行権限、共有ライブラリを確認 |
| `CONFIG_ERROR` | 環境ファイルにWebhook URLとRole IDが設定されているか確認 |
| `401`、`404` | Webhook URLを再発行・再設定。ログやチャットへWebhook URLを貼らない |
| `403 Missing Access` | Webhookの通知先チャンネルが存在するか、Webhookが削除されていないか確認 |
| ロールに通知されない | Role IDとロールのメンション可否を確認 |
| `429`や`5xx` | 一時的なDiscord障害の可能性。ログを確認し、サービスの再試行を待つ |

詳細ログ:

```bash
sudo journalctl -u atcoder-abc-reminder --since "10 minutes ago" --no-pager
```

サービスが停止している場合:

```bash
sudo systemctl restart atcoder-abc-reminder
sudo systemctl is-active atcoder-abc-reminder
```

## 12. バックアップとロールバック

SQLiteの状態をバックアップする場合は、サービスを停止してからコピーします。

```bash
sudo systemctl stop atcoder-abc-reminder
sudo cp -a /srv/shared/remider/state.db \
  /srv/shared/remider/state.db.backup
sudo systemctl start atcoder-abc-reminder
```

更新前の実行ファイルを保持していない場合は、Gitのコミットから再ビルドして再配置します。`state.db`を削除・上書きすると通知済み状態が失われるため、通常の更新では操作しません。

詳細な状態遷移、SQLiteスキーマ、セキュリティ設計は[`docs/atcoder_abc_discord_reminder_detailed_design.md`](docs/atcoder_abc_discord_reminder_detailed_design.md)を参照してください。
