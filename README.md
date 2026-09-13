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

## 1. 動作仕様

- AtCoder公式ページ`https://atcoder.jp/contests/?lang=ja`をHTTPSで取得する
- `#contest-table-upcoming`内のURLが`/contests/abc数字`の行だけを対象にする
- 開始日時と開催時間から終了日時を算出する
- 開始2時間前にDiscordへ通知する
- 開催日時の変更を検知し、未送信なら新しい時刻で通知する
- 送信状態をSQLiteへ保存し、再起動後も重複送信を防ぐ
- 送信結果が不明な場合はDiscord上のメッセージを照合してから再試行する
- Wi-Fi接続状態は確認せず、HTTP要求の成否を扱う

## 2. 現在の構成

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

通常の更新では`state.db`を上書きしません。BotトークンをGit、ソースコード、ビルド成果物、`/srv/shared/remider`へ保存しないでください。

## 3. 作業前に変更する箇所

環境ごとに、次の値を変更します。

| 項目 | 例 | 変更が必要な場合 |
|---|---|---|
| プロジェクトディレクトリ | `/path/to/kyopro_reminderbot` | ソースを置く場所が異なる場合 |
| SSH接続先 | `user@server.example` | 接続先・SSHユーザーが異なる場合 |
| アプリ配置先 | `/srv/shared/remider` | サーバー上の配置先を変える場合。サービス定義も変更 |
| サービス名 | `atcoder-abc-reminder` | systemdサービス名を変える場合。ファイル名・コマンドも変更 |
| Discord Bot Token | 発行したトークン本体 | 必ず環境ファイルへ設定 |
| Discord Channel ID | 通知先チャンネルの数値ID | 通知先を変える場合 |
| Discord Role ID | `競プロ`ロールの数値ID | メンション対象ロールを変える場合 |

以降のコマンドは、現在の構成である`/srv/shared/remider`とサービス名`atcoder-abc-reminder`を使用しています。別の値にする場合は、表の項目に合わせて読み替えてください。

## 4. ローカル環境の準備とビルド

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

## 5. Discord Botの準備

1. [Discord Developer Portal](https://discord.com/developers/applications)でアプリケーションを作成または選択する。
2. Botを作成し、トークンを発行する。
3. OAuth2のURL Generatorで`bot`スコープを選択する。
4. Botに次の権限を付与する。

   - View Channels
   - Send Messages
   - Embed Links
   - Read Message History

5. 生成されたURLから、通知先サーバーへBotを追加する。
6. 通知先チャンネルの個別権限でも、Botに同じ権限を許可する。
7. Discordの開発者モードを有効にし、通知先チャンネルのIDをコピーする。
8. `競プロ`ロールのIDをコピーする。
9. `競プロ`ロールをメンション可能にする。メンション不可のロールを使用する場合は、Botに`Mention @everyone, @here, and All Roles`権限を付与する。

Botトークンはパスワードと同じ扱いにしてください。チャット、README、Gitの履歴、コマンドライン引数へ貼り付けないでください。環境ファイルには`Bot `を付けず、トークン本体だけを設定します。

## 6. サーバー初回セットアップ

以下はSSH接続した対象サーバー上で実行します。

### 6.1 配置先とサービスユーザーの作成

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

### 6.2 環境ファイルの作成

```bash
sudo install -d -m 755 /etc/atcoder-abc-reminder
sudo install -o root -g root -m 600 /dev/null \
  /etc/atcoder-abc-reminder/atcoder-abc-reminder.env
sudoedit /etc/atcoder-abc-reminder/atcoder-abc-reminder.env
```

内容は次のとおりです。

```env
ATCODER_DISCORD_BOT_TOKEN=Botページで発行したトークン本体
ATCODER_DISCORD_CHANNEL_ID=通知先チャンネルの数値ID
ATCODER_DISCORD_ROLE_ID=競プロロールの数値ID
# 任意。省略時は/srv/shared/remider/state.db
# ATCODER_STATE_DB=/srv/shared/remider/state.db
```

保存後に権限を確認します。

```bash
sudo chown root:root /etc/atcoder-abc-reminder/atcoder-abc-reminder.env
sudo chmod 600 /etc/atcoder-abc-reminder/atcoder-abc-reminder.env
```

### 6.3 サービス定義の確認

リポジトリの`deploy/atcoder-abc-reminder.service`は、次を固定値として使用します。

```ini
ExecStart=/srv/shared/remider/bin/atcoder-abc-reminder
EnvironmentFile=/etc/atcoder-abc-reminder/atcoder-abc-reminder.env
User=abc-reminder
Group=abc-reminder
WorkingDirectory=/srv/shared/remider
```

配置先を変更した場合は、サービス定義のパスも変更してから転送してください。

## 7. 初回デプロイ

### 7.1 開発PCからファイルを転送

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

### 7.2 サーバーへ配置

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

### 7.3 systemdの有効化と起動

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

## 8. 更新デプロイ

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

## 9. 送信テスト

### 9.1 トークン認証の確認

次のコマンドはメッセージを送信しません。`HTTP 200`ならBotトークンの認証に成功しています。

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

### 9.2 ロールメンション付きテスト通知

次のコマンドは設定済みチャンネルへ実際に1件送信します。SQLiteの状態は変更しません。

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
  -w "\nHTTP %{http_code}\n" \
  "https://discord.com/api/v10/channels/${ATCODER_DISCORD_CHANNEL_ID}/messages" \
  --data-raw "{\"content\":\"<@&${ATCODER_DISCORD_ROLE_ID}>\\n\\n# AtCoder Beginner Contest 476\\n\\n本日 21:00 ～ 22:40 に [AtCoder Beginner Contest 476](https://atcoder.jp/contests/abc476) が開催されます。\\n\\n皆さんぜひ参加しましょう！🔥\",\"allowed_mentions\":{\"parse\":[],\"roles\":[\"${ATCODER_DISCORD_ROLE_ID}\"]},\"nonce\":\"manual-test:$(date +%s)\",\"enforce_nonce\":true}"
'
```

成功時は`HTTP 200`とDiscordメッセージのJSONが返ります。Discord上で先頭に`@競プロ`が表示され、その下に本文が表示されれば成功です。

## 10. 障害対応

| 状態 | 確認・対処 |
|---|---|
| `Unit ... not found` | サービス定義を`/etc/systemd/system/atcoder-abc-reminder.service`へ配置し、`sudo systemctl daemon-reload`を実行 |
| `217/USER` | `abc-reminder`ユーザーが存在するか確認 |
| `200/CHDIR` | `WorkingDirectory`の存在と、親ディレクトリの通過権限を確認 |
| `203/EXEC` | `ExecStart`のパス、実行権限、共有ライブラリを確認 |
| `CONFIG_ERROR` | 環境ファイルにToken、Channel ID、Role IDが設定されているか確認。Token本体に`Bot `を付けない |
| `401` | Botトークンを再発行・再設定。ログやチャットへトークンを貼らない |
| `403 Missing Access` | Botが対象サーバーへ参加しているか、チャンネルIDが正しいか確認 |
| `403 Missing Permissions` | View Channels、Send Messages、Embed Links、Read Message Historyを確認 |
| ロールに通知されない | Role ID、ロールのメンション可否、Botの全ロールメンション権限を確認 |
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

## 11. バックアップとロールバック

SQLiteの状態をバックアップする場合は、サービスを停止してからコピーします。

```bash
sudo systemctl stop atcoder-abc-reminder
sudo cp -a /srv/shared/remider/state.db \
  /srv/shared/remider/state.db.backup
sudo systemctl start atcoder-abc-reminder
```

更新前の実行ファイルを保持していない場合は、Gitのコミットから再ビルドして再配置します。`state.db`を削除・上書きすると通知済み状態が失われるため、通常の更新では操作しません。

詳細な状態遷移、SQLiteスキーマ、セキュリティ設計は[`docs/atcoder_abc_discord_reminder_detailed_design.md`](docs/atcoder_abc_discord_reminder_detailed_design.md)を参照してください。
