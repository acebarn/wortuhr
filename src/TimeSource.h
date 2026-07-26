#pragma once

#include <Arduino.h>
#include <time.h>

// Zeitbezug ueber NTP mit POSIX-Zeitzone.
//
// Die Sommerzeitregel steckt vollstaendig im TZ-String und wird von der
// C-Bibliothek ausgewertet. Das Altprojekt rechnete die Umstellung von Hand und
// brauchte dafuer einen eigenen Bugfix-Commit -- diese Mathematik schreiben wir
// nicht neu.
//
// CET-1CEST,M3.5.0,M10.5.0/3
//   CET       Normalzeit, 1 Stunde oestlich von UTC (Vorzeichen ist invertiert)
//   CEST      Sommerzeit
//   M3.5.0    letzter Sonntag im Maerz
//   M10.5.0/3 letzter Sonntag im Oktober, 3 Uhr
inline constexpr char kPosixTz[] = "CET-1CEST,M3.5.0,M10.5.0/3";

// Alles vor diesem Zeitpunkt ist keine echte Uhrzeit, sondern der Startwert der
// ungestellten Systemuhr.
inline constexpr time_t kPlausibleEpoch = 1700000000;  // Nov 2023

class TimeSource {
public:
    void begin() {
        configTime(0, 0, "pool.ntp.org", "europe.pool.ntp.org");
        setenv("TZ", kPosixTz, 1);
        tzset();
    }

    // Regelmaessig aufrufen. Erkennt, wann die Systemuhr erstmals oder erneut
    // gestellt wurde.
    void tick(uint32_t nowMs) {
        const time_t t = time(nullptr);
        if (t < kPlausibleEpoch) return;

        // SNTP stellt die Uhr im Hintergrund. Einen Sprung nach vorn, der
        // groesser ist als die vergangene Laufzeit, werten wir als Sync.
        const uint32_t elapsedMs = nowMs - lastCheckMs_;
        const int32_t drift = int32_t(t - lastSeenEpoch_) - int32_t(elapsedMs / 1000);

        if (!everSynced_ || drift > 2 || drift < -2) {
            lastSyncMs_ = nowMs;
            everSynced_ = true;
        }

        lastSeenEpoch_ = t;
        lastCheckMs_ = nowMs;
    }

    bool everSynced() const { return everSynced_; }

    uint32_t secondsSinceSync(uint32_t nowMs) const {
        return everSynced_ ? (nowMs - lastSyncMs_) / 1000 : 0;
    }

    // Lokale Zeit. Nur sinnvoll, wenn everSynced().
    void localHm(uint8_t& hours, uint8_t& minutes) const {
        const time_t t = time(nullptr);
        struct tm lt;
        localtime_r(&t, &lt);
        hours = uint8_t(lt.tm_hour);
        minutes = uint8_t(lt.tm_min);
    }

private:
    bool everSynced_ = false;
    uint32_t lastSyncMs_ = 0;
    uint32_t lastCheckMs_ = 0;
    time_t lastSeenEpoch_ = 0;
};
