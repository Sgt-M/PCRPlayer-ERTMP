# B案: 自動次スレ移動時の継続表示 (設計 v1) 2026-11-16

## 問題
- 現行: レス上限(stop=1000)到達→次スレ移動はフルリロード(1から)
- 報告(旧仕様): 1000の下に次スレが続き表示 / ただし飛飛び(3,4,9)表示バグ
- 原因: 次スレのレス番号(1..)が旧スレと衝突(アンカー res:N / 既読比較 / 追記カーソル)

## 方式: 表示オフセット法 (ID衝突を構造的に消す)
- CBrowserDlg::resOffset_ (累載): 各スレ切替時 `resOffset_ += 旧スレdat件数`
- 新スレのDOMアンカー = `res:<レス番号 + resOffset_>` (一意)
- レスリンク(>>N)解決: クリック要素の**親レスの表示番号**で所属スレを判定し、
  候補 `N + off[該当スレ]` から存在確認(pcr.hasId)してジャンプ。無ければ素のN。
- 親レス番号の取得: bridge elInfo に親 res ID を追加 (el.pres) ← 新Bridge API
- ExtractBrowserDlg(popup抽出): "res:" ids → 現在スレ分のみ -resOffset_ で真番号化
- PopupBrowserDlg ジャンプ(508): 常に現在スレコンテキスト → resolve(N) 委譲

## 変更点(レイヤ順)
### A. operator/manager (自動移動の合図)
1. BBSOperator::changeThread(key, ctrl, bool autoMove=false) に引数追加。
   datThread の 816〜828 の自動移動のみ change(...,true) 相当で呼ぶ
   (BBSOperator::change(key) は呼び出し元で分解 or 追加オーバーロード)
2. changeThread(autoMove) 時、manager 側の次 getDat で moved=true を1回返す
   (BBSManager dat_ へ moved フラグ。設定経路は実装時に確認:
    dat_->init セット箇所と同場所。create()→update→dat_ swap のあたり)
3. getDat(data, init) → getDat(data, init, moved) (out追加)。呼び出し元: BrowserDlg:270(+他確認)

### B. view (BrowserDlg / BrowserDlgSub)
4. this_ に `LONG resOffset_` / `std::vector<LONG> offHist_`(スレ別オフセット)追加
5. OnCallbackDat init分岐: moved=true のとき
   - LoadFromString しない / 仕切り行 AppendDocument(次スレへ(リンク)・タイトル)
   - resOffset_ += this_.dat.size(); offHist_.push_back(resOffset_)
   - this_.pos=0, this_.first=false, this_.url=新URL, reset=false
   - createRes() 経由で追記 (init=falseパスと共有) → autoScroll
   ※ moved=false & URL変化(手動切替) は従来どおりリロード
6. createRes::resid(): offset適用 id=`res:<num+offset>` (numberはwstring→_wtoi64変換)
7. OnAnchorMouseClick anchor.matched: resolveResAnchor(N, el.pres) → ScrollIntoViewId
8. resolveResAnchor(新): 親表示番号P → offHist_上位から P-offが[1..size]範囲のtを発見
   → cand=N+off[t]、pcr.hasId(cand)→採用。順に fallback、最後は素のN

### C. bridge (PcrWeb.cpp)
9. elInfo に `pres`: 最上位で最も近い [id^="res:"] 先祖のid (無ければnull)
10. pcr.hasId(id) 追加

### D. popup / extract
11. PopupBrowserDlg.cpp:508 跳先解決 → target_->resolveResAnchor(N, 現在pres無)
    (popup抽出は現在スレ前提=常に最新オフセット側。親無しの呼び出しは hasId チェーンで妥当なのへ)
12. ExtractBrowserDlg.cpp:130 ids→番号化で -resOffset_(>0のみ); 旧スレ要素は対象外化

## テスト (mock BBS)
- python ローカル2chプロトコルサーバー: /testbbs/dat/<oldkey>.dat (12レス・>>リンク込み)
  + <newkey>.dat (動的増加) + subject.txt (speed付き2件)
- PCRPlayer設定: stop=BBS_THREAD_STOP(10) ※BaseBBS.cpp:817 読込元を特定して設定
  / board cfg.move ON (BoardControl のUI/XMLキー特定)
- 検証: ①10到達→仕切り+次スレ1から追記(飛飛びなし) ②旧/新レスリンク夫々正しい③popup抽出④新着色⑤自動スクロール⑥2回目のrollover

## 保留 (v1対象外)
- 旧スレへの >>1005 跨ぎリンク解決 (IE時代不在。hasId fallbackで妥当な線着)
- DOM上限cut (連結無限成長) — 2スレ目以降のメモリで必要になったら対応
- read.dat / counter は新スレで新規 (datThread 823 clear 済みのため問題なし、確認だけ)
