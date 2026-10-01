// Japanese pitch accent dictionary.
//
// Each entry maps a kana spelling to an accent kernel position:
//   0   = heiban           L H H H ...   (no fall, the most common pattern)
//   N>0 = kernel on mora N L H H ... H↓ L L ...   (fall AFTER mora N)
//
// Special boundary cases that fall out of the rule:
//   accent_pos == 1            -> atamadaka: H L L L ...
//   accent_pos == n_moras      -> odaka:     L H H H↓ (fall on next particle)
//   0 < accent_pos < n_moras   -> nakadaka:  L H ... H↓ L L
//
// Coverage is the few hundred highest-frequency items: function words,
// particles, greetings, pronouns, common verbs and nouns, basic
// adjectives. Anything not found here falls back to heiban (LHHH...),
// which is the right default for ~70% of Tokyo Japanese vocabulary.
//
// Sources for the patterns:
//   NHK 日本語発音アクセント新辞典 2016 (the authoritative reference)
//   Daniel Vance "The Sounds of Japanese" 2008 §6
//   OJAD (Online Japanese Accent Dictionary, Univ. of Tokyo)

#include "klattalker/japanese.hpp"
#include <unordered_map>

namespace klattalker {

const std::unordered_map<std::string, int>& japanese_accent_dict() {
    static const std::unordered_map<std::string, int> m = {
        // ---- pronouns ----
        {"わたし",  0},   // I (heiban)
        {"わたくし", 4},  // I, formal
        {"あなた",  0},   // you
        {"きみ",    2},   // you (familiar) -- odaka
        {"おれ",    0},   // I (rough male)
        {"ぼく",    2},   // I (male) -- odaka
        {"かれ",    1},   // he
        {"かのじょ", 1},  // she
        {"これ",    0},   // this
        {"それ",    0},   // that (near you)
        {"あれ",    0},   // that over there
        {"どれ",    1},   // which
        {"ここ",    0},   // here
        {"そこ",    0},   // there
        {"あそこ",  0},   // over there
        {"どこ",    1},   // where
        {"だれ",    1},   // who
        {"なに",    1},   // what
        {"いつ",    1},   // when
        {"なん",    1},   // what (in なんですか etc)

        // ---- common nouns ----
        {"せんせい", 0},  // teacher (heiban per NHK; LHHH)
        {"がっこう", 0},  // school (heiban)
        {"だいがく", 0},  // university
        {"がくせい", 0},  // student
        {"なまえ",  0},   // name (heiban)
        {"ともだち", 0},  // friend
        {"かぞく",  1},   // family -- atamadaka
        {"いえ",    2},   // house -- odaka
        {"くるま",  0},   // car
        {"でんしゃ", 0},  // train
        {"みず",    0},   // water
        {"おみず",  0},   // water (polite)
        {"ごはん",  1},   // rice / meal -- atamadaka
        {"おちゃ",  0},   // tea
        {"こうちゃ", 0},  // black tea
        {"コーヒー", 3},  // coffee
        {"パン",    1},   // bread -- atamadaka
        {"さかな",  0},   // fish
        {"にく",    2},   // meat -- odaka
        {"たまご",  2},   // egg (nakadaka)
        {"こころ",  3},   // heart -- odaka
        {"いぬ",    2},   // dog -- odaka
        {"ねこ",    1},   // cat (atamadaka)
        {"とり",    0},   // bird (heiban)
        {"あめ",    1},   // rain (atamadaka) — also "candy" heiban; pick the
                          // commoner gloss
        {"はし",    1},   // chopsticks (atamadaka) — bridge would be heiban
        {"かみ",    1},   // paper/god (atamadaka)
        {"はな",    2},   // flower -- odaka  (nose would be atamadaka)
        {"き",      1},   // tree (atamadaka)
        {"やま",    2},   // mountain -- odaka
        {"かわ",    2},   // river -- odaka
        {"うみ",    1},   // sea (atamadaka)
        {"そら",    1},   // sky (atamadaka)
        {"つき",    2},   // moon -- odaka
        {"ひ",      1},   // sun / fire (atamadaka)
        {"てんき",  1},   // weather (atamadaka)
        {"きょう",  1},   // today (atamadaka)
        {"きのう",  2},   // yesterday (nakadaka)
        {"あした",  3},   // tomorrow (odaka, sometimes nakadaka)
        {"いま",    1},   // now (atamadaka)
        {"あさ",    1},   // morning (atamadaka)
        {"ひる",    2},   // noon -- odaka
        {"よる",    1},   // night (atamadaka)
        {"ばん",    0},   // evening
        {"ねん",    1},   // year
        {"つき",    2},   // month (also moon)
        {"ひと",    0},   // person (heiban)
        {"おとこ",  3},   // man -- odaka (LHH↓)
        {"おんな",  3},   // woman -- odaka
        {"こども",  0},   // child (heiban)
        {"いえ",    2},   // house -- odaka
        {"へや",    2},   // room -- odaka
        {"くに",    0},   // country (heiban)
        {"にほん",  2},   // Japan (nakadaka)
        {"にほんご", 0},  // Japanese language
        {"えいご",  0},   // English
        {"ちゅうごく", 1},// China (atamadaka)
        {"アメリカ", 0},  // America
        {"たろう",  1},   // Taro (typical name, atamadaka)
        {"はなこ",  1},   // Hanako (typical name)

        // ---- greetings / common phrases (treated as atomic) ----
        {"おはよう", 0},
        {"おはようございます", 0},
        {"こんにちは", 5},
        {"こんばんは", 5},
        {"さようなら", 2},
        {"ありがとう", 2},
        {"ありがとうございます", 2},
        {"すみません", 4},
        {"はい",    1},
        {"いいえ",  2},
        {"よろしく", 2},
        {"おねがいします", 0},
        {"ごめんなさい", 4},

        // ---- common verbs (dictionary forms) ----
        {"いく",    2},   // go
        {"くる",    1},   // come (atamadaka)
        {"する",    0},   // do (heiban actually)
        {"いる",    0},   // be (animate)
        {"ある",    1},   // be (inanimate, atamadaka)
        {"みる",    1},   // see (atamadaka)
        {"きく",    0},   // listen / ask (heiban)
        {"はなす",  2},   // speak
        {"よむ",    1},   // read (atamadaka)
        {"かく",    1},   // write (atamadaka)
        {"たべる",  2},   // eat
        {"のむ",    1},   // drink (atamadaka)
        {"ねる",    0},   // sleep
        {"おきる",  2},   // wake up
        {"わかる",  3},   // understand
        {"しる",    0},   // know (heiban)
        {"おもう",  2},   // think
        {"いう",    0},   // say (heiban)
        {"つかう",  0},   // use
        {"あそぶ",  0},   // play
        {"はたらく", 0},  // work
        {"べんきょうする", 6},  // study
        // polite forms
        {"です",    1},
        {"ます",    0},
        {"でした",  0},
        {"ました",  1},
        {"ません",  3},
        {"でしょう", 0},
        {"だ",      0},

        // ---- common adjectives ----
        {"おおきい", 3},  // big (nakadaka)
        {"ちいさい", 3},  // small
        {"あたらしい", 4},// new
        {"ふるい",  2},   // old
        {"いい",    1},   // good (atamadaka)
        {"わるい",  2},   // bad
        {"たかい",  2},   // tall/expensive
        {"やすい",  2},   // cheap
        {"きれい",  1},   // pretty (atamadaka)
        {"おいしい", 0},  // delicious
        {"あつい",  2},   // hot
        {"さむい",  2},   // cold
        {"すき",    2},   // like -- odaka

        // ---- particles -- assigned a "post-kernel" L role by default ----
        // Particles attach to the previous content word. The pitch they
        // bear depends on the preceding word's kernel; we treat them as
        // their own one-mora "word" here. A heuristic adjustment is
        // applied at emission time so they slot into the right L/H slot.
        {"は",      0},   // wa (topic) -- becomes L if preceding had kernel
        {"が",      0},   // ga (subject)
        {"を",      0},   // wo (object)
        {"に",      0},   // ni (locative / dative)
        {"の",      0},   // no (genitive)
        {"と",      0},   // to (with / and)
        {"で",      0},   // de (means / locative)
        {"も",      0},   // mo (also)
        {"へ",      0},   // e (directional)
        {"や",      0},   // ya (and)
        {"か",      0},   // ka (question)
        {"ね",      0},   // ne (confirmation)
        {"よ",      0},   // yo (emphasis)
        {"から",    0},   // kara (from)
        {"まで",    0},   // made (until)
        {"より",    0},   // yori (than)
        {"だけ",    0},   // dake (only)
        {"でも",    0},   // demo (but)
        {"のに",    0},   // noni
    };
    return m;
}

}  // namespace klattalker
