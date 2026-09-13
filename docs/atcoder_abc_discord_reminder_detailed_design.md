# AtCoder Beginner Contest Discord Reminder Bot 詳細設計書

## 1. 文書情報

| 項目 | 内容 |
|---|---|
| 文書名 | AtCoder Beginner Contest Discord Reminder Bot 詳細設計書 |
| 対応する要件定義書 | `docs/atcoder_abc_discord_reminder_requirements.md` |
| 改訂日 | 2026-09-13 |
| 改訂内容 | ESP32/TypeScriptからUbuntu Server/C++へ変更、通知本文を画像仕様へ変更 |
| 実行機体 | Ubuntu Serverを搭載したDynabookパソコン（SSHホスト: `ryutoserver`） |
| 配置先 | `/srv/shared/remider`（Windows側の`Y:\remider`に対応） |
| ソース管理場所 | 本リポジトリの開発ディレクトリ |
| 基本言語 | C++17 |
| 通知方式 | Discord REST API（Gateway不使用） |
| 外部サーバー | 使用しない |
| 月額固定費 | 増加させない |

## 2. 目的と設計方針

AtCoder Beginner Contest（ABC）の次回開催日時をAtCoder公式ページから自動取得し、開催2時間前にDiscordへ通知する。通知先は固定のDiscordチャンネルとし、Ubuntu Serverを搭載したDynabookを常時稼働させて実行する。

今回の変更により、要件定義書に残るESP32固有の制約は次のように置き換える。

| 旧方針 | 新方針 |
|---|---|
| ESP32単体 | Ubuntu Serverを搭載したDynabook上の常駐サービス |
| TypeScript + Moddable SDK | C++17 + CMake |
| Arduino `Preferences` / NVS | SQLite3によるローカル永続化 |
| ESP32向け軽量化・DOMなし解析 | PCのメモリを利用したlibxml2 HTML解析 |
| Wi-Fi接続状態の確認 | 対象外。OSが提供するネットワークを利用し、接続確認処理は持たない |
| ESP32再起動復元 | systemd再起動・OS再起動後のSQLite復元 |

### 2.1 優先順位

1. 誤ったコンテスト日時を通知しない
2. 通知失敗後に再送できる
3. プロセス再起動後も通知状態を復元できる
4. 同一通知の重複を抑止する
5. AtCoderへのアクセスを必要最小限にする
6. 実行速度を最適化する

実行速度より確実な動作を優先するため、HTML解析と状態保存には実績のある外部ライブラリ・SQLiteを使用する。HTMLを推測で解析できたことにしたり、保存に失敗した状態を通知済みにしたりしない。

### 2.2 初期リリースの範囲

含める機能は次のとおりとする。

- ABCのみの自動検出
- 次回ABCの選択
- 開始2時間前のDiscord通知
- 開催日時変更の検知
- 通知後に日時が変更された場合の訂正通知
- 通知済み状態のSQLite保存
- systemdによる自動起動・異常終了時の再起動
- Discord Gatewayを使わないREST通知

初期リリースにはARC、AHC、Web設定画面、Discordからの設定変更、OTA、クラウドサーバー、外部DB、外部監視サービスを含めない。

### 2.3 ソースとデプロイ先の分離

ソースコードは本リポジトリの開発ディレクトリで管理し、実行時に必要な成果物だけをSSH経由でサーバーへデプロイする。開発用ソース、テスト、ビルド中間ファイルを実行環境へ直接配置しない。

```text
開発環境
  └─ 本リポジトリ
       ├─ C++ソース
       ├─ テスト
       └─ ビルド成果物
              |
              | SSH/SCPまたはrsync
              v
SSH接続先 ryutoserver
  └─ /srv/shared/remider
       ├─ bin/atcoder-abc-reminder
       ├─ state.db
       └─ release-info.txt
```

ソースから生成した実行ファイルは一時ファイルへ転送し、ハッシュ値と実行権限を確認してから配置する。systemdの再起動またはサービス再読込は、転送・検証が成功した後に実行する。

## 3. 通知本文仕様

### 3.1 Discordへ送る本文

Discord APIへ送る`content`は次の形式に固定する。`{N}`はABC番号、`{START}`と`{END}`はJSTの`HH:mm`で置換する。先頭の`#`はDiscord Markdownの見出し、`[...]`はコンテストURLへのインラインリンクである。

```text
# AtCoder Beginner Contest {N}

本日 {START} ～ {END} に [AtCoder Beginner Contest {N}](https://atcoder.jp/contests/abc{N}) が開催されます。

皆さんぜひ参加しましょう！🔥
```

例:

```text
# AtCoder Beginner Contest 475

本日 21:00 ～ 22:40 に [AtCoder Beginner Contest 475](https://atcoder.jp/contests/abc475) が開催されます。

皆さんぜひ参加しましょう！🔥
```

### 3.2 Discordリンクプレビュー

本文中のAtCoderインラインリンクにより、Discordの標準リンクプレビューを有効にする。画像に表示されているAtCoderのタイトル、説明、ロゴはAtCoderページのOGP情報をDiscordが取得して表示するものであり、Bot側でカスタムEmbedを生成しない。

したがって、本文のMarkdownとリンク先は固定できるが、プレビューの画像・説明文・表示タイミングはDiscordまたはAtCoder側の仕様に依存する。Discord APIリクエストではリンクプレビューを抑制するフラグを指定しない。

### 3.3 開始・終了時刻

画像の本文に終了時刻が必要なため、Contestモデルに`endTime`を追加する。AtCoder一覧から開始時刻と開催時間を取得し、次の式で算出する。

```text
endTime = startTime + duration
```

開催時間を確実に取得できない場合は、推測値やABC固有の固定値で補完せず、候補を無効として通知しない。開始・終了時刻の双方が検証できた場合だけ通知本文を生成する。

## 4. システム構成

```text
+--------------------------+
| AtCoder                  |
| contests/?lang=ja       |
+------------+-------------+
             | HTTPS GET
             v
+--------------------------------------------+
| Dynabook / Ubuntu Server                  |
|                                            |
| atcoder-abc-reminder.service              |
|   ├─ Scheduler（30秒周期）                |
|   ├─ AtCoderClient（libcurl）              |
|   ├─ AtCoderHtmlParser（libxml2）          |
|   ├─ ContestManager                       |
|   ├─ NotificationManager                  |
|   ├─ DiscordClient（libcurl）              |
|   └─ StateRepository（SQLite3）            |
|                                            |
| /srv/shared/remider/state.db               |
+--------------------+-----------------------+
                     | HTTPS POST/GET
                     v
+--------------------------------------------+
| Discord REST API                           |
| POST /api/v10/channels/{id}/messages       |
| GET  /api/v10/channels/{id}/messages       |
+--------------------------------------------+
```

Ubuntu Server側でネットワーク接続状態を確認する機能は実装しない。HTTP要求が失敗した場合のみ、その要求の失敗として処理し、保存状態を維持してバックオフ後に再試行する。

## 5. 実行環境・使用技術

### 5.1 対象環境

- Ubuntu Server LTSを搭載したDynabook
- x86_64 Linuxを第一対象とする
- C++17
- CMake
- systemd
- OpenSSL/TLS（libcurl経由）

Ubuntuの具体的なバージョンは実装時に対象機体のLTSへ固定する。ホストのタイムゾーン設定に依存しないため、アプリケーション内部では常にJSTを明示して扱う。

### 5.2 使用ライブラリ

| ライブラリ | 用途 | 採用理由 |
|---|---|---|
| libcurl | HTTPS GET/POST、タイムアウト、HTTPヘッダー取得 | Linuxで標準的で、TLSを任せられる |
| libxml2 | 不完全HTMLを含むAtCoderページ解析 | 正規表現のみより誤解析しにくい |
| SQLite3 | 通知状態・現在コンテストの永続化 | 単一ファイル、トランザクション、無料 |
| nlohmann/json | Discord JSON本文の生成・レスポンス解析 | JSONエスケープと解析を手作業にしない |
| OpenSSL | TLS基盤 | libcurlのHTTPS検証に使用 |

依存関係はOSパッケージとして導入し、独自の常駐サーバーや有料SDKは追加しない。標準ライブラリだけで安全に処理できる箇所は標準ライブラリを使用する。

### 5.3 開発・配置ディレクトリ

```text
atcoder-abc-reminder/
├─ CMakeLists.txt
├─ README.md
├─ .gitignore
├─ include/
│  └─ atcoder_reminder/
│     ├─ atcoder_client.hpp
│     ├─ atcoder_html_parser.hpp
│     ├─ config.hpp
│     ├─ discord_client.hpp
│     ├─ domain.hpp
│     ├─ http_client.hpp
│     ├─ message_template.hpp
│     ├─ ports.hpp
│     ├─ reminder_policy.hpp
│     ├─ state_repository.hpp
│     └─ time_service.hpp
├─ src/
│  ├─ atcoder_client.cpp
│  ├─ atcoder_html_parser.cpp
│  ├─ discord_client.cpp
│  ├─ http_client.cpp
│  ├─ main.cpp
│  ├─ message_template.cpp
│  ├─ reminder_policy.cpp
│  ├─ state_repository.cpp
│  └─ time_service.cpp
├─ tests/
│  └─ test_main.cpp
└─ deploy/
   ├─ atcoder-abc-reminder.env.example
   └─ atcoder-abc-reminder.service
```

### 5.4 デプロイ成果物

サーバーへ配置するものは次に限定する。

| 成果物 | 配置先 | 備考 |
|---|---|---|
| 実行ファイル | `/srv/shared/remider/bin/atcoder-abc-reminder` | CMakeのReleaseビルド |
| SQLite | `/srv/shared/remider/state.db` | 初回起動時に生成。既存DBを上書きしない |
| リリース情報 | `/srv/shared/remider/release-info.txt` | バージョン、ビルド日時、Git SHA等 |
| systemd定義 | `/etc/systemd/system/atcoder-abc-reminder.service` | SSH先でsudoにより更新 |
| 秘密情報 | `/etc/atcoder-abc-reminder/atcoder-abc-reminder.env` | ソース・共有フォルダーへコピーしない |

サーバー上の`/srv/shared/remider`は実行環境として扱い、ソースコードの編集場所にはしない。デプロイは次の順序で行う。

1. 開発環境でテスト、静的解析、Releaseビルドを実行する。
2. 実行ファイルのSHA-256を計算する。
3. SSH経由で`/srv/shared/remider/.staging/`へ転送する。
4. サーバー上でハッシュ値、所有者、実行権限を確認する。
5. サービスを停止する。
6. 既存実行ファイルをバックアップしてから新しい実行ファイルへ切り替える。
7. `systemctl daemon-reload`後にサービスを起動する。
8. `systemctl is-active`とjournalで起動結果を確認する。

SQLiteの`state.db`は通常のデプロイで上書きしない。スキーマ変更が必要な場合は、バックアップ取得と互換性確認を行ったうえで、アプリケーションのマイグレーション処理を使用する。

## 6. モジュール設計

### 6.1 `domain`

外部ライブラリに依存しない業務ルールを担当する。

```cpp
namespace reminder {

using UnixSeconds = std::int64_t;

struct Contest {
    std::string id;       // abc475
    int number{};
    UnixSeconds startTime{};
    UnixSeconds endTime{};
    std::string url;      // 検証済みの正規URL
};

enum class NotificationKind {
    Normal,
    ScheduleChanged,
};

enum class NotificationStatus {
    None,
    Pending,
    Sent,
};

struct CurrentContest {
    Contest contest;
    UnixSeconds reminderTime{};
    NotificationStatus normalStatus{NotificationStatus::Pending};
    std::optional<UnixSeconds> normalSentForStartTime;
    NotificationStatus changeStatus{NotificationStatus::None};
    std::optional<UnixSeconds> changeSentForStartTime;
    std::optional<NotificationKind> pendingKind;
    std::string pendingMessageKey;
};

} // namespace reminder
```

IDは`abc`に数字が1つ以上続く形だけを許可する。URLはIDから生成し、AtCoder以外のホストへ向けない。

### 6.2 `time_service`

- `std::chrono::system_clock`から現在のUnix秒を取得する。
- `chrony`または`systemd-timesyncd`等、Ubuntu側の時刻同期を前提とする。
- アプリケーション自身はNTPサーバーへ直接接続しない。
- 現在時刻の妥当性を起動時に確認する。
- 表示時だけJSTへ変換する。

Ubuntuのシステム時刻が未同期と判断される場合は、通知とAtCoder候補確定を停止する。ただしWi-Fi接続状態を取得・監視する処理は持たない。

### 6.3 `atcoder_client`

`https://atcoder.jp/contests/?lang=ja`へlibcurlでGETする。

- 接続タイムアウト: 30秒
- 転送タイムアウト: 30秒
- リダイレクト: 最大3回
- 最終ホスト: `atcoder.jp`だけを許可
- HTTP 2xx以外: 取得失敗
- レスポンス上限: 2MiB
- TLS証明書検証: 有効

ESP32向けの256KiB制限は廃止するが、無制限読込は行わない。上限超過・取得失敗・解析失敗時には、現在のSQLite状態を更新しない。

### 6.4 `atcoder_html_parser`

libxml2のHTMLパーサーを使用し、AtCoderページをHTML文書として解析する。外部ネットワーク参照は禁止し、取得済み本文だけを入力にする。

処理手順:

1. HTMLをlibxml2で解析する。
2. コンテスト一覧の行またはリンク要素から`/contests/abc[0-9]+`を探す。
3. 同じコンテスト行から開始日時と開催時間を取得する。
4. `Contest`を生成し、ID、番号、URL、開始・終了時刻を検証する。
5. 同じIDが複数ある場合は、全フィールドが一致するものだけ重複排除する。一致しない場合はそのIDを無効にする。
6. 現在時刻より未来で、開始時刻より終了時刻が後の候補だけを残す。
7. 開始時刻昇順、ID昇順でソートする。

リンクの名称はABC判定に使用しない。スポンサー名付きの名称でもURLがABC形式なら採用する。

日時は、予定コンテスト表（`#contest-table-upcoming`）の各行にある`<time class="fixtime fixtime-full">`要素のテキストから取得する。現在確認できる形式は`YYYY-MM-DD HH:MM:SS+0900`である。開始日時、同じ行の開催時間（例:`01:40`）、コンテストリンクを対応付け、`endTime = startTime + duration`で終了時刻を算出する。日時・開催時間のどちらかが抽出できない場合は候補を破棄する。HTML構造が変わり、候補の妥当性を確定できない場合は解析エラーとする。

### 6.5 `contest_manager`

取得済み候補から次回ABCを選択し、現在状態との差分を適用する。

```text
候補なし（一覧構造を確認できない） -> 取得失敗扱いで状態維持・通知保留
新しいABC                    -> normal=pendingで新規保存
同一ABC・日時変更なし         -> 状態を維持
同一ABC・通知前に日時変更      -> 開始/終了/通知時刻を更新
同一ABC・通知後に日時変更      -> 訂正通知をpendingにする
保存済みABCが未来なのに一覧から消失 -> 状態を維持し、その回の通知を保留
現在ABCの開始時刻を経過       -> 次回候補を取得できた場合のみ置換
```

予定表を取得できても完全な未来ABC候補が0件の場合は、HTML構造変更や必要項目欠落の可能性を考慮して取得失敗扱いとする。現在状態を削除・置換せず、バックオフ後に再取得する。これにより、解析失敗時に古い予定を誤通知しない。

### 6.6 `reminder_policy`

```cpp
constexpr std::int64_t kReminderBeforeSeconds = 2 * 60 * 60;

UnixSeconds reminderTime(const Contest& contest) {
    return contest.startTime - kReminderBeforeSeconds;
}

bool shouldSendNormal(const CurrentContest& state, UnixSeconds now) {
    return state.normalStatus == NotificationStatus::Pending
        && now >= state.reminderTime
        && now < state.contest.startTime;
}
```

開始時刻を過ぎてから初めて検出した通常通知は送らない。開始前に通知時刻を過ぎていれば、次回ループで即時送信する。

### 6.7 `message_template`

JST日時を`HH:mm`で整形し、3章の固定本文を生成する。コンテスト番号は整数から生成し、AtCoder HTMLの名称を本文へ直接コピーしない。

DiscordのJSON本文はnlohmann/jsonで生成し、手作業の文字列連結によるJSON破壊を防ぐ。`allowed_mentions`は次の値に固定する。

```json
{
  "allowed_mentions": { "parse": [] }
}
```

### 6.8 `discord_client`

使用エンドポイント:

```text
POST https://discord.com/api/v10/channels/{channel_id}/messages
GET  https://discord.com/api/v10/channels/{channel_id}/messages?limit=100
```

POST本文:

```json
{
  "content": "...画像仕様の本文...",
  "allowed_mentions": { "parse": [] },
  "nonce": "normal:abc475:...",
  "enforce_nonce": true
}
```

URLの埋め込みプレビューを表示させるため、Embed抑制フラグは送らない。

Botに必要な権限は次のとおりとする。

- View Channel
- Send Messages
- Read Message History（送信結果不明時の照合用）

## 7. 永続化設計

### 7.1 SQLite

保存先:

```text
/srv/shared/remider/state.db
```

SQLiteをWALモードで使用し、状態更新をトランザクションで行う。DBファイル、WALファイル、SHMファイルを同一ディレクトリへ置く。

### 7.2 スキーマ

```sql
CREATE TABLE IF NOT EXISTS current_contest (
    singleton       INTEGER PRIMARY KEY CHECK (singleton = 1),
    contest_id      TEXT NOT NULL,
    contest_number  INTEGER NOT NULL,
    start_time      INTEGER NOT NULL,
    end_time        INTEGER NOT NULL,
    reminder_time   INTEGER NOT NULL,
    normal_status   TEXT NOT NULL CHECK (normal_status IN ('pending', 'sent')),
    normal_sent_at_start INTEGER,
    change_status   TEXT NOT NULL CHECK (change_status IN ('none', 'pending', 'sent')),
    change_sent_at_start INTEGER,
    pending_kind    TEXT CHECK (pending_kind IN ('normal', 'change')),
    pending_key     TEXT,
    last_fetched_at INTEGER,
    updated_at      INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS notification_history (
    message_key     TEXT PRIMARY KEY,
    contest_id      TEXT NOT NULL,
    kind            TEXT NOT NULL CHECK (kind IN ('normal', 'change')),
    start_time      INTEGER NOT NULL,
    status          TEXT NOT NULL CHECK (status IN ('pending', 'sent', 'skipped')),
    discord_message_id TEXT,
    updated_at      INTEGER NOT NULL
);
```

`notification_history`はコンテストが次回へ移行した後も通知キーを保持し、再起動やタイムアウト後の照合に使用する。

### 7.3 通知状態の更新

通常通知の送信前に、同一トランザクションで`notification_history.status = 'pending'`と`current_contest.pending_kind = 'normal'`を保存する。Discord送信成功後に履歴と現在状態を`sent`へ更新する。

日時変更通知も同様に`kind = 'change'`の別キーで管理する。同一コンテストが複数回延期された場合は、変更後の開始時刻ごとに別キーを生成する。

保存成功前に送信済みと確定しない。SQLite更新に失敗した場合は、次回ループで送信前の状態を再確認する。

### 7.4 外部送信との整合性

Discord送信とSQLite更新は同一トランザクションではないため、理論上の完全なExactly-onceは実現できない。本設計では次を組み合わせる。

1. 送信前に保留状態を保存
2. Discord `nonce`と`enforce_nonce`を利用
3. タイムアウト時に直近メッセージを取得し通知キーを照合
4. 再起動時も保留キーを照合
5. 照合できるまで通知済みへ変更しない

照合で既存メッセージが見つかった場合は、DiscordメッセージIDを履歴へ保存して送信済みとする。

## 8. スケジュールと状態遷移

### 8.1 AtCoder確認間隔

| 次回ABCまでの残り時間 | 確認間隔 |
|---|---:|
| 24時間超 | 60分 |
| 6時間超、24時間以下 | 15分 |
| 6時間以下、開始前 | 5分 |
| 対象なし | 60分 |

起動直後、前回取得失敗後のバックオフ完了時、時刻同期完了時は即時取得する。通知時刻の判定は30秒周期の内部時計だけで実施し、AtCoderへアクセスしない。

### 8.2 状態

```text
STARTING
  |
  v
LOADING_STATE
  |
  v
FETCHING_CONTEST
  |--失敗--> WAIT（状態維持、バックオフ）
  |
  v
WAIT_FOR_REMINDER
  |--通知時刻到達--> SENDING_NORMAL
  |--開始時刻経過--> FETCHING_NEXT
  |--確認時刻到達--> FETCHING_CONTEST
  |
  +--日時変更後の訂正通知--> SENDING_CHANGE

SENDING_NORMAL/SENDING_CHANGE
  |--成功--> MARK_SENT -> WAIT_FOR_REMINDER
  |--失敗--> RECONCILING
                         |--既存発見--> MARK_SENT
                         |--未発見--> WAIT（再試行）

FETCHING_NEXT --取得成功--> 新しいABCを保存 -> WAIT_FOR_REMINDER
FETCHING_NEXT --取得失敗--> 現在状態を維持 -> WAIT
```

### 8.3 起動時

1. systemdからプロセスを起動する。
2. SQLiteを開き、スキーマを検証する。
3. 保留通知があれば、最初にDiscord履歴を照合する。
4. OS時刻が有効であることを確認する。
5. AtCoder一覧を即時取得する。
6. 現在状態と候補を比較し、日時変更または次回移行を適用する。
7. 30秒周期のループを開始する。

## 9. エラー処理

### 9.1 ネットワーク・HTTP

Wi-Fi接続確認は行わないが、HTTP要求の失敗は処理する。

| エラー | 処理 |
|---|---|
| DNS、接続、TLS、タイムアウト | 5分後から指数バックオフで再試行、最大1時間 |
| AtCoder HTTP 5xx | 保存状態を維持して再試行 |
| AtCoder HTTP 4xx | URL・アクセス仕様をログし、30分以上の間隔で再試行 |
| Discord 429 | `Retry-After`以上待機して再試行 |
| Discord 5xx | 指数バックオフで再試行 |
| Discord 400 | 本文・リクエスト不正。未送信状態を保持し、30分間隔でログ |
| Discord 401/403 | Token・チャンネル・権限の問題。未送信状態を保持し、30分間隔でログ |
| HTML解析エラー | 現在状態を更新せず、次回取得 |
| SQLiteエラー | 通知済みにせず、エラー終了または次回処理へ回す |

### 9.2 プロセス障害

- 最上位で例外を捕捉し、秘密情報を含めずにjournalへ出力する。
- 予期しない終了時はsystemdの`Restart=on-failure`で再起動する。
- 同時実行を防ぐため、systemdのサービスを1インスタンスに限定する。
- SQLiteのトランザクション途中で終了しても、SQLiteのロールバックに任せる。

## 10. セキュリティ設計

### 10.1 秘密情報

Bot Token、Wi-Fi Password、Channel IDはソースコードやGitへ記録しない。Ubuntu上のroot所有ファイルへ保存する。

```text
/etc/atcoder-abc-reminder/atcoder-abc-reminder.env
```

権限:

```text
root:root 600
```

サービス専用ユーザーだけが読み取れるよう、実運用時には`LoadCredential`またはサービス専用のEnvironmentFileを使用する。Tokenをコマンドライン引数に置かない。

### 10.2 systemdサンドボックス

サービス定義では次を設定する。

```ini
User=abc-reminder
Group=abc-reminder
NoNewPrivileges=true
PrivateTmp=true
ProtectHome=true
ProtectSystem=strict
ReadWritePaths=/srv/shared/remider
Restart=on-failure
RestartSec=30
```

ネットワーク通信に必要な権限以外を付与しない。ログへToken、Password、Authorizationヘッダー、HTML本文を出力しない。

### 10.3 入力検証

- AtCoder URLは`https://atcoder.jp/contests/abc[0-9]+`に限定する。
- ABC番号は整数として解析し、異常に大きい値を拒否する。
- 日時・開催時間が不正な候補は採用しない。
- Discord本文は固定テンプレートと検証済み値だけで生成する。
- `allowed_mentions.parse`を空配列にする。
- 外部HTMLから取得した文字列をSQLへ連結せず、プレースホルダーを使う。

## 11. systemd配置設計

サービスファイル例:

```ini
[Unit]
Description=AtCoder ABC Discord Reminder Bot
After=network-online.target

[Service]
Type=simple
ExecStart=/srv/shared/remider/bin/atcoder-abc-reminder
EnvironmentFile=/etc/atcoder-abc-reminder/atcoder-abc-reminder.env
User=abc-reminder
Group=abc-reminder
WorkingDirectory=/srv/shared/remider
Restart=on-failure
RestartSec=30
NoNewPrivileges=true
PrivateTmp=true
ProtectHome=true
ProtectSystem=strict
ReadWritePaths=/srv/shared/remider

[Install]
WantedBy=multi-user.target
```

`network-online.target`は起動順序の宣言にのみ使用する。アプリケーションはWi-Fi接続確認を行わず、HTTPの成否で通信可能性を判断する。

Dynabookを通知装置として常時稼働させるため、Ubuntuの自動サスペンド・休止を無効にする設定を導入手順へ含める。これはアプリケーションのWi-Fi判定とは別の、機体の電源管理設定である。

## 12. テスト設計

### 12.1 単体テスト

GoogleTest等の無料テストフレームワーク、またはCMakeで導入できる最小テストランナーを使用する。

| 対象 | 確認内容 |
|---|---|
| URL判定 | `abc475`を採用し、`arc475`、名称だけのABC、別ホストを除外 |
| HTML解析 | 正常、スポンサー名、重複、日時欠落、構造変更、巨大本文 |
| 日時 | JST、ISO形式、無効日付、日跨ぎ、終了時刻計算 |
| 次回選択 | 過去・現在・未来・複数候補・同時刻 |
| 通知時刻 | 開始7200秒前、24時間境界、6時間境界 |
| 本文 | 画像仕様の改行、番号、開始・終了時刻、URL、絵文字 |
| 変更前 | 通常通知未送信時に通知時刻だけ再計算 |
| 変更後 | 訂正通知を1回、再変更時は新日時ごとに1回 |
| SQLite | トランザクション、再起動復元、保留・送信済み状態 |
| Discord | 2xx、429、5xx、タイムアウト、履歴照合、JSONエスケープ |

### 12.2 統合テスト

libcurl、Discord、AtCoderをFakeサーバーへ差し替え、次を実行する。

- 起動から次回ABC取得・保存
- 通知時刻到達からDiscord POST・SQLite更新
- POSTタイムアウト後に履歴から既存メッセージを発見
- AtCoder取得失敗後も既存の通知時刻を維持
- 通知前の日時変更
- 通知後の訂正通知
- プロセス再起動後の保留通知再照合
- コンテスト終了後の次回ABC移行
- 連続HTTP失敗からのバックオフ復旧

### 12.3 実機・運用テスト

Dynabook上で次を確認する。

- `systemctl enable --now`で自動起動する
- 24時間以上連続稼働する
- systemdによる異常終了後の自動再起動
- Ubuntu再起動後のSQLite状態復元
- 自動サスペンド・休止無効化後に通知時刻まで稼働する
- AtCoder実ページから開始・終了時刻を取得する
- Discordの本文とリンクプレビューが画像仕様になる
- 通知後に開催日時を変更した場合、訂正通知が送られる

## 13. 要件トレーサビリティ

| 要件 | 設計対応 |
|---|---|
| FR-01 Wi-Fi接続 | 変更によりアプリケーションでの接続確認を対象外とする。OSのネットワークを利用する。 |
| FR-02 時刻取得 | Ubuntuの時刻同期とC++時刻サービス、JST変換 |
| FR-03 AtCoder取得 | libcurlによるHTTPS GETとlibxml2解析 |
| FR-04 次回ABC | ContestManagerの未来候補ソート |
| FR-05 2時間前 | `startTime - 7200` |
| FR-06 Discord通知 | Discord REST API POST |
| FR-07 通知内容 | 画像仕様の固定本文、AtCoder URL、標準リンクプレビュー |
| FR-08 日時変更 | 同一IDの開始・終了時刻比較 |
| FR-09 通知後変更 | `notification_history`と訂正通知 |
| FR-10 重複防止 | SQLite、nonce、履歴照合 |
| FR-11 状態保存 | SQLiteのローカルファイルとトランザクション |
| FR-12 次回移行 | 開始後に取得成功した次回候補へ置換 |
| NFR-01 軽量性 | ESP32制約を解除。ただし本文・候補・HTTP応答に上限を設ける |
| NFR-02 常時稼働 | systemd、自動再起動、タイムアウト |
| NFR-03 再起動耐性 | SQLite、保留通知、履歴照合 |
| NFR-04 障害耐性 | HTTP/Discord/HTML/SQLite別の失敗処理 |
| NFR-05 負荷抑制 | 60分/15分/5分の段階的確認間隔 |

FR-01はユーザー指定により「Wi-Fi接続状態を確認しない」へ変更した。Wi-Fiが利用できるかどうかを検出する処理は実装しないが、HTTP要求自体の失敗は通知保護のため処理する。

## 14. 費用・運用

### 14.1 継続費

- C++、CMake、libcurl、libxml2、SQLite3、nlohmann/json、systemd: 無料で利用できるソフトウェアを使用する。
- AtCoder: 公開ページを取得するだけで、有料APIを利用しない。
- Discord: 既存のBot、サーバー、チャンネルを利用し、Gateway常駐基盤や有料ホスティングを使わない。
- クラウドサーバー、DB、監視サービス: 使用しない。

月額固定費は増加させない。Dynabookの購入費や必要な周辺機器は一括購入費であり、電気代は機体を常時稼働させることによる変動費として発生する。

### 14.2 バックアップ

SQLiteファイルは通知履歴を含むため、サービス停止後に`state.db`、`state.db-wal`、`state.db-shm`を整合した状態でバックアップする。バックアップ先は同一ディスク上だけにせず、必要に応じてユーザーが手動で外部媒体へコピーする。

## 15. 作業単位

### UOW-01 C++ビルド骨格

- CMake、C++17、libcurl、libxml2、SQLite3、JSONライブラリを導入する。
- `main.cpp`から起動・終了ログを出す。
- 完了条件: Ubuntu Server上でビルドと起動ができる。

### UOW-02 ドメイン・時刻・本文

- Contest、通知時刻、終了時刻、画像仕様の本文を実装する。
- 完了条件: 日時境界と本文比較テストに通る。

### UOW-03 AtCoder取得・解析

- libcurlとlibxml2で開始・終了時刻付きABC候補を取得する。
- 完了条件: フィクスチャと実ページで妥当な候補だけを抽出する。

### UOW-04 SQLite永続化

- スキーマ、トランザクション、通知履歴、再起動復元を実装する。
- 完了条件: 送信前・送信後・保存失敗のテストに通る。

### UOW-05 Discord通知

- REST POST、履歴照合、429、再試行、リンクプレビューを実装する。
- 完了条件: Discordの実チャンネルで本文とプレビューを確認する。

### UOW-06 systemd・長期稼働

- サービスユーザー、環境ファイル、systemd、自動再起動、電源管理を設定する。
- 完了条件: Ubuntu再起動・プロセス異常終了・24時間稼働テストに通る。

## 16. 既知の制約・残存リスク

1. AtCoder一覧HTMLの構造変更時は候補を安全側で無効にする。実装更新が必要になる。
2. Discord送信とSQLite保存は別システムのため、数学的なExactly-onceは保証できない。nonceと履歴照合で重複を最小化する。
3. Discordのリンクプレビューは外部サービスの生成結果であり、画像・説明・表示の完全一致は保証できない。本文とURL配置は画像仕様に固定する。
4. Dynabookが電源断、休止、故障した時間帯は通知できない。常時給電と自動サスペンド無効化を運用条件とする。
5. Bot Tokenが漏洩した場合はDiscord Developer Portalで再生成し、環境ファイルを更新する。
6. Ubuntuの時刻同期が壊れている場合は誤通知防止のため通知しない。

## 17. 完成条件

- Ubuntu Server搭載Dynabook上でC++17プログラムが常駐する。
- AtCoderからABC番号、開始時刻、終了時刻を自動取得する。
- ABC番号の手動入力が不要である。
- 開始2時間前に画像仕様の本文をDiscordへ送信する。
- 本文中のAtCoderインラインリンクにより標準リンクプレビューを表示できる。
- 開催日時変更を検知し、通知前は予定時刻を更新、通知後は訂正通知を行う。
- 同じ通知キーの通知を重複させない。
- Ubuntu・プロセス再起動後もSQLiteから状態を復元する。
- コンテスト終了後、次回ABCへ自動移行する。
- HTTP障害、HTML解析失敗、Discord失敗で誤った状態へ遷移しない。
- Discord Gateway、クラウドサーバー、月額サービスを必要としない。
