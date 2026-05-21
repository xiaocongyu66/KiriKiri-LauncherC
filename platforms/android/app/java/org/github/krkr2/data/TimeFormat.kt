package org.github.krkr2.data

import java.util.concurrent.TimeUnit

/**
 * Relative-time and duration formatters that mirror KrKr2-Next's
 * `_formatDate()` / `formatPlayDuration()` output exactly.
 */
object TimeFormat {

    fun relativeDate(millis: Long, strings: Strings): String {
        if (millis <= 0L) return strings.never
        val now = System.currentTimeMillis()
        val diff = now - millis
        if (diff < 0L) return strings.justNow
        val minutes = TimeUnit.MILLISECONDS.toMinutes(diff)
        val hours = TimeUnit.MILLISECONDS.toHours(diff)
        val days = TimeUnit.MILLISECONDS.toDays(diff)
        return when {
            minutes < 1 -> strings.justNow
            hours < 1 -> strings.minutesAgo(minutes.toInt())
            days < 1 -> strings.hoursAgo(hours.toInt())
            days < 7 -> strings.daysAgo(days.toInt())
            else -> {
                // Fallback: ISO-like calendar date.
                val cal = java.util.Calendar.getInstance().apply { timeInMillis = millis }
                "%04d-%02d-%02d".format(
                    cal.get(java.util.Calendar.YEAR),
                    cal.get(java.util.Calendar.MONTH) + 1,
                    cal.get(java.util.Calendar.DAY_OF_MONTH),
                )
            }
        }
    }

    /** `2h 30m` / `45m` style — matches Next's `formatPlayDuration`. */
    fun playDuration(seconds: Long): String {
        if (seconds < 60L) return "<1m"
        val totalMinutes = seconds / 60L
        val hours = totalMinutes / 60L
        val minutes = totalMinutes % 60L
        return if (hours > 0L) "${hours}h ${minutes}m" else "${minutes}m"
    }

    /** Small abstraction so the formatter can be used from EN / ZH UI without context. */
    interface Strings {
        val never: String
        val justNow: String
        fun minutesAgo(n: Int): String
        fun hoursAgo(n: Int): String
        fun daysAgo(n: Int): String
    }
}
