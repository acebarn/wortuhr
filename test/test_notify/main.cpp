// Host-Tests fuer den Notify-Stapel.
//
//   pio test -e native

#include <unity.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "wordclock/Notify.h"

using namespace wordclock;

void setUp(void) {}
void tearDown(void) {}

static char g_msg[256];
static const char* msg(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    std::vsnprintf(g_msg, sizeof(g_msg), fmt, a);
    va_end(a);
    return g_msg;
}

static NotifyRequest req(const char* id, uint8_t prio, uint32_t ttl,
                         NotifyStyle style = NotifyStyle::Tint, Rgb color = kWhite) {
    NotifyRequest r;
    r.id = id;
    r.prio = prio;
    r.ttlSeconds = ttl;
    r.style = style;
    r.color = color;
    return r;
}

// --- Stapelverhalten -------------------------------------------------------

static void test_higher_priority_wins() {
    NotifyStack s;
    s.push(req("fenster", 20, 0), 0);
    s.push(req("klingel", 90, 3), 0);

    TEST_ASSERT_NOT_NULL(s.top());
    TEST_ASSERT_EQUAL_STRING("klingel", s.top()->id);
}

// Das Kernversprechen: ein kurzes Ereignis legt sich ueber einen Dauerzustand
// und faellt danach von selbst wieder ab -- der Dauerzustand ist dann wieder da.
static void test_transient_falls_back_to_persistent() {
    NotifyStack s;
    uint32_t now = 1000;

    s.push(req("fenster", 20, 0), now);  // Dauerzustand
    s.tick(now, true);
    TEST_ASSERT_EQUAL_STRING("fenster", s.top()->id);

    now += 120000;
    s.push(req("klingel", 90, 3), now);  // 3 s Ereignis
    s.tick(now, true);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("klingel", s.top()->id, "Ereignis muss oben liegen");

    now += 3500;
    s.tick(now, true);
    TEST_ASSERT_NOT_NULL_MESSAGE(s.top(), "der Dauerzustand darf nicht mitverschwinden");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("fenster", s.top()->id,
                                     "nach Ablauf muss der Dauerzustand wieder sichtbar sein");
}

static void test_same_id_replaces_and_refreshes() {
    NotifyStack s;
    s.push(req("ambient", 10, 60), 0);
    s.tick(0, true);
    TEST_ASSERT_EQUAL_UINT8(1, s.size());

    // Kurz vor Ablauf erneut senden -> Restlaufzeit faengt neu an.
    s.push(req("ambient", 10, 60), 55000);
    s.tick(55000, true);
    s.tick(100000, true);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, s.size(), "erneutes Senden muss die Laufzeit auffrischen");

    s.tick(120000, true);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, s.size(), "ohne Auffrischen muss es ablaufen");
}

static void test_clear_by_id() {
    NotifyStack s;
    s.push(req("fenster", 20, 0), 0);
    s.push(req("tuer", 20, 0), 0);
    TEST_ASSERT_EQUAL_UINT8(2, s.size());

    TEST_ASSERT_TRUE(s.clear("fenster"));
    TEST_ASSERT_EQUAL_UINT8(1, s.size());
    TEST_ASSERT_NULL(s.find("fenster"));
    TEST_ASSERT_NOT_NULL(s.find("tuer"));

    TEST_ASSERT_FALSE_MESSAGE(s.clear("gibtsnicht"), "unbekannter Kanal darf nichts tun");
}

static void test_ttl_zero_never_expires_while_online() {
    NotifyStack s;
    s.push(req("fenster", 20, 0), 0);
    for (uint32_t t = 0; t < 60UL * 60 * 1000; t += 60000) s.tick(t, true);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, s.size(), "ttl 0 darf online nicht verfallen");
}

// Selbstheilung: ein verlorenes clear-Telegramm darf die Uhr nicht wochenlang
// einfaerben.
static void test_offline_clears_everything_eventually() {
    NotifyStack s;
    s.tick(0, true);
    s.push(req("fenster", 20, 0), 0);
    s.push(req("waesche", 30, 0), 0);

    s.tick(kOfflineExpiryMs - 1000, false);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, s.size(), "vor Ablauf der Frist noch nicht raeumen");

    s.tick(kOfflineExpiryMs + 1000, false);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, s.size(), "nach der Frist muss der Stapel leer sein");
}

static void test_reconnect_resets_the_offline_timer() {
    NotifyStack s;
    s.tick(0, true);
    s.push(req("fenster", 20, 0), 0);

    s.tick(kOfflineExpiryMs - 1000, false);
    s.tick(kOfflineExpiryMs, true);  // Broker wieder da
    s.tick(2 * kOfflineExpiryMs - 1000, false);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, s.size(), "Wiederverbinden muss die Frist zuruecksetzen");
}

static void test_full_stack_only_evicts_weaker() {
    NotifyStack s;
    char ids[kMaxNotifications][8];
    for (uint8_t i = 0; i < kMaxNotifications; ++i) {
        std::snprintf(ids[i], sizeof(ids[i]), "k%u", i);
        TEST_ASSERT_TRUE(s.push(req(ids[i], uint8_t(50 + i), 0), 0));
    }
    TEST_ASSERT_EQUAL_UINT8(kMaxNotifications, s.size());

    TEST_ASSERT_FALSE_MESSAGE(s.push(req("schwach", 10, 0), 0),
                              "Schwaecheres darf nichts verdraengen");
    TEST_ASSERT_NULL(s.find("schwach"));

    TEST_ASSERT_TRUE_MESSAGE(s.push(req("wichtig", 200, 0), 0),
                             "Wichtigeres muss das Schwaechste verdraengen");
    TEST_ASSERT_NOT_NULL(s.find("wichtig"));
    TEST_ASSERT_NULL_MESSAGE(s.find("k0"), "das Schwaechste haette weichen muessen");
    TEST_ASSERT_EQUAL_UINT8(kMaxNotifications, s.size());
}

static void test_equal_priority_newest_wins() {
    NotifyStack s;
    s.push(req("alt", 50, 0), 0);
    s.push(req("neu", 50, 0), 1000);
    TEST_ASSERT_EQUAL_STRING("neu", s.top()->id);
}

static void test_rejects_empty_id() {
    NotifyStack s;
    TEST_ASSERT_FALSE(s.push(req(nullptr, 50, 0), 0));
    TEST_ASSERT_FALSE(s.push(req("", 50, 0), 0));
    TEST_ASSERT_EQUAL_UINT8(0, s.size());
}

// --- Anwendung auf die Ausgabe ---------------------------------------------

static void test_empty_stack_changes_nothing() {
    NotifyStack s;
    Modifiers mod;
    Overlay ov;
    ov.clear();
    s.apply(mod, ov, 0, false);

    TEST_ASSERT_FALSE(mod.recolor);
    TEST_ASSERT_EQUAL_UINT8(255, mod.modulation);
}

static void test_tint_recolors_without_moving() {
    NotifyStack s;
    s.push(req("f", 20, 0, NotifyStyle::Tint, {0, 120, 255}), 0);

    for (uint32_t ms = 0; ms < 5000; ms += 37) {
        Modifiers mod;
        Overlay ov;
        ov.clear();
        s.apply(mod, ov, ms, false);
        TEST_ASSERT_TRUE(mod.recolor);
        TEST_ASSERT_TRUE(mod.tintColor == Rgb({0, 120, 255}));
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(255, mod.modulation, "tint darf sich nicht bewegen");
    }
}

// Umfaerben muss aufhellen koennen. Multiplikativ waere ein blauer tint auf
// bernsteinfarbener Uhr fast schwarz.
static void test_recolor_can_brighten() {
    const Rgb amber{255, 180, 60};
    const Rgb blue{0, 120, 255};

    const Rgb multiplied = modulate(amber, blue);
    const Rgb recolored = recolorTo(amber, blue);

    TEST_ASSERT_TRUE_MESSAGE(recolored.b > multiplied.b,
                             "Umfaerben muss den Blauanteil erhalten, Multiplikation loescht ihn");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(255, recolored.b, "voll aufgedrehte LED bleibt voll");
}

static void test_pulse_swaps_to_blink_when_breathing() {
    NotifyStack s;
    s.push(req("f", 20, 0, NotifyStyle::Pulse, {0, 255, 0}), 0);

    bool sawZero = false, sawFull = false, sawMiddle = false;
    for (uint32_t ms = 0; ms < 5000; ms += 11) {
        Modifiers mod;
        Overlay ov;
        ov.clear();
        s.apply(mod, ov, ms, /*breathingActive=*/true);
        if (mod.modulation == 0) sawZero = true;
        else if (mod.modulation == 255) sawFull = true;
        else sawMiddle = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(sawZero && sawFull, "muss als Blinken erscheinen");
    TEST_ASSERT_FALSE_MESSAGE(sawMiddle, "Blinken kennt keine Zwischenwerte");
}

static void test_pulse_stays_smooth_without_breathing() {
    NotifyStack s;
    s.push(req("f", 20, 0, NotifyStyle::Pulse, {0, 255, 0}), 0);

    uint8_t lo = 255, hi = 0;
    for (uint32_t ms = 0; ms < 7000; ms += 11) {
        Modifiers mod;
        Overlay ov;
        ov.clear();
        s.apply(mod, ov, ms, /*breathingActive=*/false);
        if (mod.modulation < lo) lo = mod.modulation;
        if (mod.modulation > hi) hi = mod.modulation;
    }
    TEST_ASSERT_TRUE_MESSAGE(hi > lo, "Pulse muss sich bewegen");
    TEST_ASSERT_TRUE_MESSAGE(lo > 0, "Pulse darf nicht ganz erloeschen, sonst ist es Blinken");
}

static void test_word_style_highlights_that_word() {
    NotifyStack s;
    NotifyRequest r = req("w", 20, 0, NotifyStyle::Word, {255, 0, 0});
    r.word = uint8_t(Word::Zwanzig);
    s.push(r, 0);

    Modifiers mod;
    Overlay ov;
    ov.clear();
    s.apply(mod, ov, 0, false);

    Frame f;
    f.clear();
    ov.blendInto(f);

    const WordSpan sp = span(Word::Zwanzig);
    for (uint8_t i = 0; i < sp.len; ++i)
        TEST_ASSERT_TRUE_MESSAGE(f.cell(uint16_t(sp.cell + i)) == Rgb({255, 0, 0}),
                                 msg("Buchstabe %u von ZWANZIG fehlt", i));
    TEST_ASSERT_TRUE_MESSAGE(f.cell(0) == kBlack, "ES darf nicht mitleuchten");
}

static void test_glyph_style_draws_the_bitmap() {
    uint8_t glyph[kGlyphBytes] = {};
    for (uint16_t c : {uint16_t(0), uint16_t(17), uint16_t(109)}) glyph[c / 8] |= uint8_t(1 << (c % 8));

    NotifyStack s;
    NotifyRequest r = req("g", 20, 0, NotifyStyle::Glyph, {10, 20, 30});
    r.glyph = glyph;
    s.push(r, 0);

    Modifiers mod;
    Overlay ov;
    ov.clear();
    s.apply(mod, ov, 0, false);

    Frame f;
    f.clear();
    ov.blendInto(f);

    TEST_ASSERT_TRUE(f.cell(0) == Rgb({10, 20, 30}));
    TEST_ASSERT_TRUE(f.cell(17) == Rgb({10, 20, 30}));
    TEST_ASSERT_TRUE(f.cell(109) == Rgb({10, 20, 30}));
    TEST_ASSERT_TRUE(f.cell(1) == kBlack);
}

static void test_anim_is_reported_not_drawn() {
    NotifyStack s;
    NotifyRequest r = req("ambient", 10, 0, NotifyStyle::Anim, {0, 0, 0});
    r.anim = "plasma";
    s.push(r, 0);

    Modifiers mod;
    Overlay ov;
    ov.clear();
    s.apply(mod, ov, 0, false);

    TEST_ASSERT_EQUAL_STRING("plasma", s.activeAnimation());
    TEST_ASSERT_FALSE_MESSAGE(mod.recolor, "anim faerbt nicht, es ersetzt die Basis");

    s.clear("ambient");
    TEST_ASSERT_NULL(s.activeAnimation());
}

// Benachrichtigungen duerfen die Eckpunkte niemals erreichen -- sonst bricht
// der Vertrag "Bewegung heisst Stoerung" (DESIGN 4, 8.4).
static void test_notifications_never_reach_the_dots() {
    uint8_t glyph[kGlyphBytes];
    std::memset(glyph, 0xFF, sizeof(glyph));  // alles setzen, maximal aggressiv

    for (NotifyStyle st : {NotifyStyle::Tint, NotifyStyle::Pulse, NotifyStyle::Blink,
                           NotifyStyle::Glyph, NotifyStyle::Word, NotifyStyle::Anim}) {
        NotifyStack s;
        NotifyRequest r = req("x", 200, 0, st, {255, 255, 255});
        r.glyph = glyph;
        r.word = uint8_t(Word::Uhr);
        r.anim = "fire";
        s.push(r, 0);

        for (uint32_t ms = 0; ms < 3000; ms += 29) {
            Modifiers mod;
            Overlay ov;
            ov.clear();
            s.apply(mod, ov, ms, false);

            Frame f;
            f.clear();
            for (uint8_t d = 0; d < kDotCount; ++d) f.setDot(d, {1, 2, 3});
            ov.blendInto(f);

            for (uint8_t d = 0; d < kDotCount; ++d)
                TEST_ASSERT_TRUE_MESSAGE(f.dot(d) == Rgb({1, 2, 3}),
                                         msg("Style %s hat Punkt %u erreicht", styleName(st), d));
        }
    }
}

// --- Namen -----------------------------------------------------------------

static void test_style_names_round_trip() {
    for (NotifyStyle st : {NotifyStyle::Tint, NotifyStyle::Pulse, NotifyStyle::Blink,
                           NotifyStyle::Glyph, NotifyStyle::Word, NotifyStyle::Anim}) {
        NotifyStyle back;
        TEST_ASSERT_TRUE_MESSAGE(parseStyle(styleName(st), back), styleName(st));
        TEST_ASSERT_TRUE_MESSAGE(back == st, styleName(st));
    }
    NotifyStyle dummy;
    TEST_ASSERT_FALSE(parseStyle("gibtsnicht", dummy));
    TEST_ASSERT_FALSE(parseStyle(nullptr, dummy));
}

static void test_long_id_is_truncated_not_overflowed() {
    NotifyStack s;
    const char* longId = "einsehrlangerkanalnamederueberlaeuft";
    TEST_ASSERT_TRUE(s.push(req(longId, 50, 0), 0));
    TEST_ASSERT_NOT_NULL(s.top());
    TEST_ASSERT_TRUE(std::strlen(s.top()->id) < kNotifyIdLen);
}

// =============================================================================

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_higher_priority_wins);
    RUN_TEST(test_transient_falls_back_to_persistent);
    RUN_TEST(test_same_id_replaces_and_refreshes);
    RUN_TEST(test_clear_by_id);
    RUN_TEST(test_ttl_zero_never_expires_while_online);
    RUN_TEST(test_offline_clears_everything_eventually);
    RUN_TEST(test_reconnect_resets_the_offline_timer);
    RUN_TEST(test_full_stack_only_evicts_weaker);
    RUN_TEST(test_equal_priority_newest_wins);
    RUN_TEST(test_rejects_empty_id);
    RUN_TEST(test_empty_stack_changes_nothing);
    RUN_TEST(test_tint_recolors_without_moving);
    RUN_TEST(test_recolor_can_brighten);
    RUN_TEST(test_pulse_swaps_to_blink_when_breathing);
    RUN_TEST(test_pulse_stays_smooth_without_breathing);
    RUN_TEST(test_word_style_highlights_that_word);
    RUN_TEST(test_glyph_style_draws_the_bitmap);
    RUN_TEST(test_anim_is_reported_not_drawn);
    RUN_TEST(test_notifications_never_reach_the_dots);
    RUN_TEST(test_style_names_round_trip);
    RUN_TEST(test_long_id_is_truncated_not_overflowed);
    return UNITY_END();
}
