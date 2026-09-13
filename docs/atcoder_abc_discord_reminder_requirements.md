# AtCoder Beginner Contest Discord Reminder Bot 要件定義書

## 1. 文書概要

### 1.1 文書名

AtCoder Beginner Contest Discord Reminder Bot 要件定義書

### 1.2 目的

AtCoderで定期的に開催される **AtCoder Beginner Contest（ABC）** の開催日時を自動取得し、コンテスト開始時刻の2時間前にDiscordへ通知するシステムを構築する。

本システムはESP32などの低スペックなマイコン上でも動作可能な軽量構成とし、AtCoder側でコンテスト日時が変更された場合にも自動追従できるものとする。

---

# 2. システム概要

## 2.1 システムの役割

本システムは以下を自動で実行する。

1. AtCoder公式サイトから今後開催されるABCを取得する
2. コンテスト番号・開始日時を取得する
3. コンテスト開始2時間前の時刻を算出する
4. 開催日時の変更がないか定期的に確認する
5. 開始2時間前になったらDiscordへ通知する
6. Discord通知にコンテストURLを含める
7. 一度送った通知を重複して送らないよう管理する
8. ESP32再起動後も通知状態を維持する

---

# 3. 想定システム構成

```text
+----------------------+
|     AtCoder公式      |
|                      |
|  コンテスト一覧       |
+----------+-----------+
           |
           | HTTPS GET
           v
+--------------------------------+
|             ESP32              |
|                                |
| ・Wi-Fi接続                    |
| ・NTP時刻同期                  |
| ・AtCoder開催情報取得          |
| ・ABC判定                      |
| ・開催時刻変更検知             |
| ・通知時刻判定                 |
| ・通知済み情報保存             |
+---------------+----------------+
                |
                | HTTPS POST
                v
+--------------------------------+
|            Discord             |
|                                |
| 登録済みBotから                 |
| 指定チャンネルへ通知           |
+--------------------------------+
```

サーバーやPCを常時稼働させず、ESP32単体で完結できる構成を基本とする。

---

# 4. 対象コンテスト

## 4.1 対象

AtCoder Beginner Contest（ABC）のみを対象とする。

対象URLは基本的に以下の形式とする。

```text
https://atcoder.jp/contests/abcXXX
```

例：

```text
https://atcoder.jp/contests/abc476
```

## 4.2 ABCの判定方法

コンテスト名称だけを利用した判定は行わない。

スポンサー付きコンテストなどでは正式名称が、

```text
○○ Programming Contest
（AtCoder Beginner Contest XXX）
```

のようになる可能性があるためである。

そのため、URL中の

```text
/contests/abcXXX
```

を基準としてABCを判定する。

想定する判定パターン：

```regex
/contests/abc[0-9]+
```

---

# 5. 機能要件

## FR-01 Wi-Fi接続機能

ESP32は指定されたWi-Fiアクセスポイントに接続できること。

保持する情報：

```text
SSID
Wi-Fi Password
```

Wi-Fi接続に失敗した場合は一定時間待機した後、再接続を試みる。

---

## FR-02 時刻取得機能

ESP32はNTPを利用して正確な現在時刻を取得する。

基準となるタイムゾーンは日本標準時とする。

```text
Asia/Tokyo
UTC+9
```

最低でも以下の情報を取得可能とする。

```text
現在日時
Unix Timestamp
```

NTPによる時刻取得失敗時には、Discord通知判定を行わない。

---

## FR-03 AtCoderコンテスト情報取得機能

AtCoder公式サイトからHTTPS通信を利用して、今後開催予定のコンテスト情報を取得する。

取得対象：

```text
https://atcoder.jp/contests/?lang=ja
```

取得する情報：

- ABC番号
- 開始日時
- コンテストURL

例：

```text
Contest:
AtCoder Beginner Contest 476

Contest ID:
abc476

Start:
2026/09/19 21:00

URL:
https://atcoder.jp/contests/abc476
```

---

## FR-04 次回ABC判定機能

取得したABCの中から、

```text
現在時刻より後に開始する最も近いABC
```

を次回通知対象として選択する。

過去に終了したABCは通知対象としない。

---

## FR-05 通知時刻計算機能

コンテスト開始時刻から2時間を引いた時刻をリマインダー時刻とする。

計算式：

```text
ReminderTime = ContestStartTime - 2時間
```

例：

```text
コンテスト開始
2026/09/19 21:00

↓

通知時刻
2026/09/19 19:00
```

---

## FR-06 Discord通知機能

リマインダー時刻になった場合、Discordの指定チャンネルにBotアカウントからメッセージを送信する。

Discord Gatewayへの常時接続は行わない。

通知時のみDiscord REST APIへHTTPS POSTを実行する。

必要情報：

```text
Discord Bot Token
Discord Channel ID
```

Botには最低限以下の権限を付与する。

```text
View Channel
Send Messages
```

---

## FR-07 Discord通知内容

Discord通知には最低限以下を含める。

- ABC番号
- 開始日時
- 開始まであと2時間であること
- コンテストURL

通知例：

```text
📢 AtCoder Beginner Contest 476

コンテスト開始まであと2時間です！

🕘 開始日時
2026/09/19 21:00

🔗 コンテストURL
https://atcoder.jp/contests/abc476

参加する方はお忘れなく！
```

---

## FR-08 開催日時変更検知機能

一度取得したコンテストについても、AtCoder公式サイトを定期的に確認する。

保存済みの開始日時と新しく取得した開始日時が異なる場合、

```text
oldStartTime != newStartTime
```

を日時変更として判定する。

変更が検出された場合、以下を更新する。

```text
開始日時
通知予定時刻
```

例：

```text
変更前

ABC480
2026/10/17 21:00

通知
19:00
```

AtCoder側で変更：

```text
変更後

ABC480
2026/10/18 14:00

通知
12:00
```

この場合、旧通知時刻である10月17日19:00には通知を行わない。

---

## FR-09 通知後の日程変更対応

すでに「開始2時間前通知」を送った後にAtCoder側で開始日時が変更された場合は、訂正通知をDiscordへ送信する。

通知例：

```text
⚠️ AtCoder Beginner Contest 480 の
開催日時が変更されました。

変更後：

2026/10/18 14:00～

🔗
https://atcoder.jp/contests/abc480
```

必要に応じて変更前日時も表示可能とする。

---

## FR-10 重複通知防止機能

同一ABCについて通常の「2時間前通知」は1回のみ送信する。

通知状態として、

```text
notified = true / false
```

を保持する。

通知成功時に、

```text
notified = true
```

へ変更する。

ネットワークエラー等によりDiscord通知が失敗した場合は通知済み扱いにしない。

---

## FR-11 永続保存機能

ESP32が再起動した場合でも以下の情報を保持する。

- ABC番号
- 開始日時
- 通知済みフラグ

ESP32のNVSを使用する。

Arduino Frameworkでは、

```cpp
Preferences
```

を利用する。

保存データ例：

```text
contestNumber = 476
startTime     = 1789822800
notified      = false
```

---

## FR-12 次回コンテストへの自動移行

現在管理しているABCが終了した場合、次回開催予定ABCを自動検索する。

次のABCが見つかった場合、

```text
contestNumber
startTime
notified
```

を更新する。

新しいコンテストでは、

```text
notified = false
```

とする。

手動でABC番号を書き換える必要がないこと。

---

# 6. AtCoder確認頻度

AtCoderサーバーへの不要なアクセスを避けるため、確認頻度を制御する。

基本仕様：

| コンテストまでの残り時間 | 確認間隔 |
|---|---:|
| 24時間以上 | 60分 |
| 6〜24時間 | 15分 |
| 6時間以内 | 5分 |

通知時刻そのものの判定についてはAtCoderへアクセスせず、ESP32内部時計を使用する。

内部での通知判定間隔：

```text
30〜60秒程度
```

とする。

---

# 7. 軽量化要件

ESP32でも安定動作することを重要要件とする。

## 7.1 Discord Gatewayを使用しない

Discord Gatewayによる、

```text
WebSocket
Heartbeat
イベント受信
```

は使用しない。

Discordとの通信は通知時のREST APIのみとする。

---

## 7.2 HTML全体のDOM解析を行わない

AtCoderのHTMLを完全なDOMツリーへ展開しない。

受信した文字列から必要な情報のみを検索する。

主な検索対象：

```text
/contests/abc
```

---

## 7.3 データベースを使用しない

MySQL、SQLite等は使用しない。

ESP32内部のNVSだけで管理する。

---

## 7.4 JSONライブラリを必須にしない

Discordへ送信するJSONは非常に単純なため、可能な限り追加ライブラリを使用せず生成する。

例：

```cpp
String body =
    "{\"content\":\"" +
    escapedMessage +
    "\"}";
```

ただし、

```text
"
\
改行
```

などは適切にJSONエスケープすること。

---

# 8. 使用ライブラリ

Arduino Frameworkを使用する場合、基本構成は以下とする。

```cpp
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <time.h>
```

可能な限りESP32標準ライブラリのみで構成する。

---

# 9. 内部データ構造

基本的なコンテスト情報は以下のような構造とする。

```cpp
struct Contest {
    int number;
    uint64_t startTime;
    bool notified;
};
```

コンテストURLは保存せず、

```cpp
"https://atcoder.jp/contests/abc"
+ String(number)
```

のように動的生成してよい。

---

# 10. セキュリティ要件

以下は機密情報として扱う。

```text
Wi-Fi SSID
Wi-Fi Password
Discord Bot Token
Discord Channel ID
```

特にDiscord Bot Tokenはパスワードと同等として扱う。

ソースコードへ直接記述せず、

```cpp
secrets.h
```

などへ分離することを推奨する。

例：

```cpp
#define WIFI_SSID "..."
#define WIFI_PASSWORD "..."

#define DISCORD_BOT_TOKEN "..."
#define DISCORD_CHANNEL_ID "..."
```

Git管理を行う場合、

```gitignore
secrets.h
```

を`.gitignore`へ登録する。

Discord Bot TokenをGitHubなどの公開リポジトリへ公開してはならない。

---

# 11. エラー処理要件

## 11.1 Wi-Fi接続失敗

Wi-Fi接続に失敗した場合、

```text
一定時間待機
↓
再接続
```

を行う。

無限に高速再接続してはならない。

---

## 11.2 NTP取得失敗

NTP時刻取得に失敗した場合、時刻依存のDiscord通知を行わない。

一定時間後に再取得する。

---

## 11.3 AtCoder取得失敗

AtCoderへのHTTPS接続に失敗した場合、既存の保存データを削除しない。

一定時間後に再取得する。

---

## 11.4 HTML解析失敗

HTML構造変更等によりABC情報を取得できなかった場合、

```text
現在保存されているABC情報
```

を維持する。

誤った日時で情報を上書きしてはならない。

---

## 11.5 Discord送信失敗

Discord通知に失敗した場合、

```text
notified = true
```

にしてはならない。

一定時間後に再送を試みる。

ただしDiscordへの過剰な再送を避けるため、再試行間隔を設定する。

例：

```text
5分間隔
```

---

# 12. 基本動作フロー

```text
ESP32起動
    |
    v
Wi-Fi接続
    |
    v
NTP時刻取得
    |
    v
NVSから前回情報読込
    |
    v
AtCoderコンテスト一覧取得
    |
    v
次回ABC検索
    |
    v
ABC番号・日時取得
    |
    +---------------------------+
    |                           |
    | 日時変更あり              | 日時変更なし
    v                           |
開始日時更新                    |
通知時刻再計算                  |
    |                           |
    +-------------+-------------+
                  |
                  v
          現在時刻を確認
                  |
            開始2時間前？
             /       \
           NO         YES
           |           |
           |           v
           |    Discordへ通知
           |           |
           |      送信成功？
           |       /      \
           |      NO      YES
           |      |        |
           |    再試行   notified=true
           |               |
           +-------+-------+
                   |
                   v
                 待機
                   |
                   v
              定期的に再確認
```

---

# 13. 状態管理

システムは最低限以下の状態を管理する。

```text
WAIT_FOR_CONTEST
次回ABCを検索中

WAIT_FOR_REMINDER
ABC検出済み、通知時刻待機

NOTIFYING
Discord送信処理中

NOTIFIED
通知完了

CONTEST_FINISHED
対象ABC終了、次回ABC探索
```

---

# 14. 非機能要件

## NFR-01 軽量性

ESP32上で安定して動作できる程度のCPU・RAM使用量とする。

PCやクラウドサーバーを必要としない。

---

## NFR-02 常時稼働

長期間連続稼働を前提とする。

メモリリークが発生しない設計とする。

---

## NFR-03 再起動耐性

ESP32の電源断・再起動が発生しても、NVSから状態を復元し正常動作を再開できること。

---

## NFR-04 ネットワーク障害耐性

一時的に、

```text
Wi-Fi
AtCoder
Discord
NTP
```

へ接続できなくても、プログラム全体が停止しないこと。

---

## NFR-05 サーバー負荷抑制

AtCoderへのアクセス頻度を必要最小限にする。

秒単位でのスクレイピングは禁止する。

---

# 15. 推奨PlatformIO構成

```text
atcoder-abc-reminder/
│
├─ platformio.ini
│
├─ .gitignore
│
├─ README.md
│
│
├─ include/
│   ├─ config.h
│   └─ secrets.h
│
└─ src/
    └─ main.cpp
```

より機能を分離する場合は、

```text
src/
├─ main.cpp
├─ AtCoderClient.cpp
├─ DiscordClient.cpp
├─ ContestManager.cpp
└─ TimeManager.cpp
```

とすることも可能だが、ESP32向けの小規模プログラムであるため、初期実装では`main.cpp`中心の単純な構成でもよい。

---

# 16. config.h

設定値と機密情報は分離する。

`config.h`

```cpp
#pragma once

#define ATCODER_CHECK_INTERVAL_NORMAL 3600
#define ATCODER_CHECK_INTERVAL_24H     900
#define ATCODER_CHECK_INTERVAL_6H      300

#define REMINDER_BEFORE_SECONDS        7200

#define DISCORD_RETRY_INTERVAL         300
```

---

# 17. secrets.h

```cpp
#pragma once

#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

#define DISCORD_BOT_TOKEN "YOUR_DISCORD_BOT_TOKEN"
#define DISCORD_CHANNEL_ID "YOUR_CHANNEL_ID"
```

`.gitignore`

```gitignore
include/secrets.h
```

---

# 18. 必須要件一覧

| ID | 要件 | 必須 |
|---|---|---|
| FR-01 | Wi-Fi接続 | 必須 |
| FR-02 | NTP時刻取得 | 必須 |
| FR-03 | AtCoder情報取得 | 必須 |
| FR-04 | 次回ABC自動検出 | 必須 |
| FR-05 | 開始2時間前計算 | 必須 |
| FR-06 | Discord Bot通知 | 必須 |
| FR-07 | URL付き通知 | 必須 |
| FR-08 | 日時変更自動検知 | 必須 |
| FR-09 | 通知後の日程変更通知 | 推奨 |
| FR-10 | 重複通知防止 | 必須 |
| FR-11 | ESP32再起動後の状態復元 | 必須 |
| FR-12 | 次回ABCへの自動移行 | 必須 |
| NFR-01 | ESP32上で軽量動作 | 必須 |
| NFR-02 | 長期間稼働 | 必須 |
| NFR-03 | 再起動耐性 | 必須 |
| NFR-04 | 通信障害耐性 | 必須 |
| NFR-05 | AtCoderへの低負荷アクセス | 必須 |

---

# 19. 完成条件

以下をすべて満たした時点で本システムを完成とする。

1. ESP32単体で動作する
2. AtCoderから次回ABCを自動取得できる
3. ABC番号を手動入力する必要がない
4. 開始日時を自動取得できる
5. 開始2時間前にDiscordへ通知できる
6. 通知にABCのURLが含まれる
7. 開催日時変更を自動検知できる
8. 変更後の正しい時刻に通知できる
9. 同一ABCを重複通知しない
10. ESP32再起動後も状態を復元できる
11. コンテスト終了後、次回ABCへ自動移行する
12. AtCoderやDiscordへの一時的な通信失敗でプログラムが停止しない
13. Discord Gatewayなどの常時接続を必要としない
14. 長期間ESP32上で安定稼働できる

---

# 20. 将来的な拡張候補

初期実装には含めないが、以下への拡張を可能とする。

- AtCoder Regular Contest（ARC）通知
- AtCoder Heuristic Contest（AHC）通知
- Typical Contest通知
- 通知時刻を「2時間前」以外に変更
- 24時間前通知
- 10分前通知
- Discord Embed形式による通知
- 複数Discordサーバーへの通知
- 複数チャンネル対応
- ESP32上のWeb設定画面
- Discordからの設定変更
- Wi-Fi設定のWebポータル化
- OTAによるESP32ファームウェア更新

初期バージョンではこれらを実装せず、ABC開始2時間前通知という主要目的を優先する。
